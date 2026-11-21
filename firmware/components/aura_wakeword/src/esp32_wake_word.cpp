#include "esp32_wake_word.hpp"

#include "aura/wake_word.hpp"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include "frontend.h"
#include "frontend_util.h"
#include "tensorflow/lite/micro/micro_allocator.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_resource_variable.h"
#include "tensorflow/lite/micro/schema/schema_generated.h"

#include <algorithm>
#include <new>

#if AURA_WAKE_WORD_MODEL
extern const std::uint8_t aura_wake_word_model_start[] asm("_binary_avra_tflite_start");
extern const std::uint8_t aura_wake_word_model_end[] asm("_binary_avra_tflite_end");
#endif

namespace aura::firmware {
namespace {

constexpr const char* kTag = "aura-wake";
constexpr int kSampleRate = 16000;
constexpr std::size_t kStepSamples = 160;  // 10 ms: a feature window's step
constexpr std::size_t kFeatures = 40;
// The first model used 30,000 bytes in microWakeWord's manifest; the arena
// lives in PSRAM and its use is logged at start.
constexpr std::size_t kArenaBytes = 48U * 1024U;
constexpr std::size_t kStreamBytes = kSampleRate * sizeof(std::int16_t);  // a second of voice
constexpr int kResourceVariables = 20;
// Every operator microWakeWord's models use (the first one needs 13 of them),
// so a model trained again with other layers still loads.
using OpResolver = tflite::MicroMutableOpResolver<20>;

}  // namespace

struct Esp32WakeWord::Engine {
    const tflite::Model* model{nullptr};
    OpResolver resolver;
    std::uint8_t* arena{nullptr};
    tflite::MicroAllocator* allocator{nullptr};
    tflite::MicroResourceVariables* variables{nullptr};
    tflite::MicroInterpreter* interpreter{nullptr};
    TfLiteTensor* input{nullptr};
    TfLiteTensor* output{nullptr};
    std::size_t stride{3};
    float input_scale{1.0F};
    int input_zero_point{0};
    FrontendState frontend{};
    WakeWordDetector detector;
    std::size_t frame{0};
};

bool Esp32WakeWord::initialize() {
#if !AURA_WAKE_WORD_MODEL
    ESP_LOGI(kTag, "no wake word model in this build");
    return false;
#else
    if (ready_) return true;
    // Every allocation here goes to PSRAM (CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=0).
    auto* engine = new (std::nothrow) Engine;
    if (!engine) return false;
    const auto fail = [engine](const char* why) {
        ESP_LOGE(kTag, "wake word unavailable: %s", why);
        delete engine->interpreter;
        if (engine->arena) heap_caps_free(engine->arena);
        delete engine;
        return false;
    };
    engine->model = tflite::GetModel(aura_wake_word_model_start);
    if (engine->model->version() != TFLITE_SCHEMA_VERSION) return fail("the model's schema");
    auto& ops = engine->resolver;
    ops.AddCallOnce();
    ops.AddVarHandle();
    ops.AddReshape();
    ops.AddReadVariable();
    ops.AddStridedSlice();
    ops.AddConcatenation();
    ops.AddAssignVariable();
    ops.AddConv2D();
    ops.AddMul();
    ops.AddAdd();
    ops.AddMean();
    ops.AddFullyConnected();
    ops.AddLogistic();
    ops.AddQuantize();
    ops.AddDepthwiseConv2D();
    ops.AddAveragePool2D();
    ops.AddMaxPool2D();
    ops.AddPad();
    ops.AddPack();
    ops.AddSplitV();
    engine->arena = static_cast<std::uint8_t*>(
        heap_caps_aligned_alloc(16, kArenaBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!engine->arena) return fail("no memory for its arena");
    engine->allocator = tflite::MicroAllocator::Create(engine->arena, kArenaBytes);
    if (!engine->allocator) return fail("its allocator");
    engine->variables = tflite::MicroResourceVariables::Create(engine->allocator, kResourceVariables);
    engine->interpreter = new (std::nothrow)
        tflite::MicroInterpreter(engine->model, engine->resolver, engine->allocator, engine->variables);
    if (!engine->interpreter || engine->interpreter->AllocateTensors() != kTfLiteOk) return fail("its tensors");
    engine->input = engine->interpreter->input(0);
    engine->output = engine->interpreter->output(0);
    const auto* dims = engine->input ? engine->input->dims : nullptr;
    if (!dims || engine->input->type != kTfLiteInt8 || dims->size != 3 || dims->data[2] != static_cast<int>(kFeatures) ||
        dims->data[1] < 1 || !engine->output || engine->output->type != kTfLiteUInt8) {
        return fail("an input or output it was not made for");
    }
    engine->stride = static_cast<std::size_t>(dims->data[1]);
    engine->input_scale = engine->input->params.scale;
    engine->input_zero_point = engine->input->params.zero_point;

    // The training's features (pymicro-features, which microWakeWord used).
    FrontendConfig config{};
    config.window.size_ms = 30;
    config.window.step_size_ms = 10;
    config.filterbank.num_channels = kFeatures;
    config.filterbank.lower_band_limit = 125.0F;
    config.filterbank.upper_band_limit = 7500.0F;
    config.noise_reduction.smoothing_bits = 10;
    config.noise_reduction.even_smoothing = 0.025F;
    config.noise_reduction.odd_smoothing = 0.06F;
    config.noise_reduction.min_signal_remaining = 0.05F;
    config.pcan_gain_control.enable_pcan = 1;
    config.pcan_gain_control.strength = 0.95F;
    config.pcan_gain_control.offset = 80.0F;
    config.pcan_gain_control.gain_bits = 21;
    config.log_scale.enable_log = 1;
    config.log_scale.scale_shift = 6;
    if (!FrontendPopulateState(&config, &engine->frontend, kSampleRate)) return fail("its features");
    engine->detector.reset();

    auto* stream = xStreamBufferCreateWithCaps(kStreamBytes, sizeof(std::int16_t) * kStepSamples,
                                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!stream) {
        FrontendFreeStateContents(&engine->frontend);
        return fail("no memory for its buffer");
    }
    stream_ = stream;
    engine_ = engine;
    // Its own task, its stack in PSRAM: it never touches the flash.
    if (xTaskCreateWithCaps(&Esp32WakeWord::task_entry, "aura_wake", 6144, this, 3, nullptr,
                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        engine_ = nullptr;
        stream_ = nullptr;
        vStreamBufferDeleteWithCaps(stream);
        FrontendFreeStateContents(&engine->frontend);
        return fail("its task");
    }
    ready_ = true;
    ESP_LOGI(kTag, "wake word ready: model %u bytes, arena %u of %u bytes, %u x %u features an inference",
             static_cast<unsigned>(aura_wake_word_model_end - aura_wake_word_model_start),
             static_cast<unsigned>(engine->interpreter->arena_used_bytes()), static_cast<unsigned>(kArenaBytes),
             static_cast<unsigned>(engine->stride), static_cast<unsigned>(kFeatures));
    return true;
#endif
}

void Esp32WakeWord::feed(const std::int16_t* samples, std::size_t count) noexcept {
    if (!ready_ || !samples || count == 0) return;
    auto stream = static_cast<StreamBufferHandle_t>(stream_);
    std::size_t bytes = count * sizeof(std::int16_t);
    // Whole samples only, so the reader never sees half of one.
    const std::size_t space = xStreamBufferSpacesAvailable(stream) & ~static_cast<std::size_t>(1);
    if (bytes > space) {
        dropped_.fetch_add(static_cast<std::uint32_t>((bytes - space) / sizeof(std::int16_t)));
        bytes = space;
    }
    if (bytes > 0) xStreamBufferSend(stream, samples, bytes, 0);
}

Esp32WakeWord::Stats Esp32WakeWord::take_stats() noexcept {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    Stats stats = stats_;
    stats.dropped = dropped_.exchange(0);
    stats.arena_bytes = engine_ && engine_->interpreter ? engine_->interpreter->arena_used_bytes() : 0;
    stats_ = {};
    return stats;
}

void Esp32WakeWord::task_entry(void* argument) {
    static_cast<Esp32WakeWord*>(argument)->task();
}

void Esp32WakeWord::task() {
    auto stream = static_cast<StreamBufferHandle_t>(stream_);
    Engine& engine = *engine_;
    std::int16_t chunk[kStepSamples];
    std::size_t have = 0;
    for (;;) {
        const std::size_t got =
            xStreamBufferReceive(stream, reinterpret_cast<std::uint8_t*>(chunk) + have * sizeof(std::int16_t),
                                 (kStepSamples - have) * sizeof(std::int16_t), portMAX_DELAY);
        if (restart_.exchange(false)) {
            FrontendReset(&engine.frontend);
            engine.interpreter->Reset();
            engine.detector.reset();
            engine.frame = 0;
            have = 0;
            // What waited belongs to an earlier listen.
            std::uint8_t sink[64];
            while (xStreamBufferReceive(stream, sink, sizeof(sink), 0) > 0) {
            }
            continue;
        }
        have += got / sizeof(std::int16_t);
        if (have < kStepSamples) continue;
        have = 0;
        const std::int16_t* next = chunk;
        std::size_t left = kStepSamples;
        while (left > 0) {
            std::size_t used = 0;
            const FrontendOutput features = FrontendProcessSamples(&engine.frontend, next, left, &used);
            next += used;
            left -= used;
            if (features.size == kFeatures && features.values) {
                std::int8_t* row = engine.input->data.int8 + engine.frame * kFeatures;
                for (std::size_t index = 0; index < kFeatures; ++index) {
                    row[index] = quantize_wake_feature(features.values[index], engine.input_scale,
                                                       engine.input_zero_point);
                }
                if (++engine.frame == engine.stride) {
                    engine.frame = 0;
                    invoke();
                }
            }
            if (used == 0) break;
        }
    }
}

void Esp32WakeWord::invoke() {
    Engine& engine = *engine_;
    const std::int64_t start = esp_timer_get_time();
    const TfLiteStatus status = engine.interpreter->Invoke();
    const auto spent = static_cast<std::uint32_t>(esp_timer_get_time() - start);
    if (status != kTfLiteOk) return;
    engine.detector.set_cutoff(cutoff_.load());
    const bool word = engine.detector.push(engine.output->data.uint8[0]);
    const float peak = engine.detector.take_peak();
    const float miss = engine.detector.take_near_miss();
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.peak = std::max(stats_.peak, peak);
        ++stats_.inferences;
        stats_.slowest_us = std::max(stats_.slowest_us, spent);
        near_miss_ = std::max(near_miss_, miss);
    }
    if (word) detected_.store(true);
}

float Esp32WakeWord::take_near_miss() noexcept {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    const float miss = near_miss_;
    near_miss_ = 0.0F;
    return miss;
}

}  // namespace aura::firmware
