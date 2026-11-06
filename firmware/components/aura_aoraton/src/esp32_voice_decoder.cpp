#include "aura/esp32_voice_decoder.hpp"

#include "decoder/esp_audio_dec.h"
#include "decoder/impl/esp_aac_dec.h"
#include "decoder/impl/esp_opus_dec.h"
#include "esp_heap_caps.h"
#include "esp_pthread.h"

#include <algorithm>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace aura::firmware {
namespace {

constexpr std::size_t kDecoderStack = 24 * 1024;

bool heap_has_room(std::size_t bytes) {
    return heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) > bytes + 64 * 1024;
}

// One message, frame by frame, to interleaved 16-bit PCM.
bool run(const std::uint8_t* data, const VoiceTrack& track, std::vector<std::int16_t>& pcm, std::uint32_t& rate,
         std::uint32_t& channels) {
    std::unique_ptr<VoiceResampler> resampler;
    std::size_t skip = 0;  // Opus's pre-skip, in input frames
    static std::once_flag registered;
    std::call_once(registered, [] {
        esp_aac_dec_register();
        esp_opus_dec_register();
    });
    esp_aac_dec_cfg_t aac{};
    aac.sample_rate = static_cast<std::int32_t>(track.sample_rate);
    aac.channel = track.channels;
    aac.bits_per_sample = 16;
    aac.no_adts_header = false;  // each frame gets its ADTS header below
    esp_opus_dec_cfg_t opus{};
    opus.sample_rate = 48000;
    opus.channel = track.channels;
    opus.frame_duration = ESP_OPUS_DEC_FRAME_DURATION_INVALID;
    opus.self_delimited = false;
    esp_audio_dec_cfg_t config{};
    if (track.codec == VoiceCodec::Aac) {
        config.type = ESP_AUDIO_TYPE_AAC;
        config.cfg = &aac;
        config.cfg_sz = sizeof(aac);
    } else {
        config.type = ESP_AUDIO_TYPE_OPUS;
        config.cfg = &opus;
        config.cfg_sz = sizeof(opus);
    }
    esp_audio_dec_handle_t decoder = nullptr;
    if (esp_audio_dec_open(&config, &decoder) != ESP_AUDIO_ERR_OK) return false;
    // A 120 ms Opus frame of 48 kHz stereo, or 2,048 AAC samples of stereo.
    std::vector<std::uint8_t> out(48000 / 1000 * 120 * 2 * 2);
    std::vector<std::uint8_t> framed;
    bool ok = true;
    rate = 0;
    channels = 0;
    // 16 kHz mono output: two bytes a sample.
    const std::size_t expected = std::size_t{track.duration_ms + 500} * 16 * 2;
    if (!heap_has_room(expected)) ok = false;
    for (std::size_t index = 0; ok && index < track.frames.size(); ++index) {
        const auto& frame = track.frames[index];
        const std::uint8_t* input = data + frame.offset;
        std::size_t length = frame.size;
        if (track.codec == VoiceCodec::Aac) {
            if (!voice_adts(track.config, input, length, framed)) {
                ok = false;
                break;
            }
            input = framed.data();
            length = framed.size();
        }
        esp_audio_dec_in_raw_t raw{};
        raw.buffer = const_cast<std::uint8_t*>(input);
        raw.len = static_cast<std::uint32_t>(length);
        while (ok && raw.len > 0) {
            esp_audio_dec_out_frame_t decoded{};
            decoded.buffer = out.data();
            decoded.len = static_cast<std::uint32_t>(out.size());
            const auto result = esp_audio_dec_process(decoder, &raw, &decoded);
            if (result == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH && decoded.needed_size > out.size() &&
                heap_has_room(decoded.needed_size)) {
                out.resize(decoded.needed_size);
                continue;
            }
            if (result != ESP_AUDIO_ERR_OK) {
                ok = false;
                break;
            }
            if (!resampler) {
                esp_audio_dec_info_t info{};
                if (esp_audio_dec_get_info(decoder, &info) != ESP_AUDIO_ERR_OK || info.sample_rate == 0 ||
                    info.channel == 0) {
                    ok = false;
                    break;
                }
                rate = info.sample_rate;
                channels = info.channel;
                resampler = std::make_unique<VoiceResampler>(rate, channels);
                resampler->output().reserve(expected / 2);
                skip = track.codec == VoiceCodec::Opus ? std::size_t{track.pre_skip} * rate / 48000 : 0;
            }
            const std::size_t frames = decoded.decoded_size / 2 / channels;
            const std::size_t dropped = std::min(skip, frames);
            skip -= dropped;
            const auto* begin = reinterpret_cast<const std::int16_t*>(out.data());
            if (resampler->output().size() + frames > resampler->output().capacity() &&
                !heap_has_room(4 * (resampler->output().size() + frames))) {
                ok = false;
                break;
            }
            resampler->push(begin + dropped * channels, frames - dropped);
            if (raw.consumed == 0 || raw.consumed > raw.len) break;
            raw.buffer += raw.consumed;
            raw.len -= raw.consumed;
        }
    }
    esp_audio_dec_close(decoder);
    if (ok && resampler) {
        pcm = std::move(resampler->output());
        rate = resampler->rate();
        channels = 1;
    }
    return ok && rate > 0 && !pcm.empty();
}

}  // namespace

bool Esp32VoiceDecoder::decode(const std::uint8_t* data, const VoiceTrack& track, PcmClip& clip) {
    clip = {};
    if (!data || track.frames.empty() || track.channels == 0 ||
        (track.codec != VoiceCodec::Aac && track.codec != VoiceCodec::Opus))
        return false;
    std::vector<std::int16_t> pcm;
    std::uint32_t rate = 0;
    std::uint32_t channels = 0;
    bool ok = false;
    // The decoders' stack lives in PSRAM: they touch no flash.
    esp_pthread_cfg_t previous = esp_pthread_get_default_config();
    const bool had = esp_pthread_get_cfg(&previous) == ESP_OK;
    esp_pthread_cfg_t config = esp_pthread_get_default_config();
    config.thread_name = "voice_dec";
    config.stack_size = kDecoderStack;
    config.prio = 2;
    config.stack_alloc_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    esp_pthread_set_cfg(&config);
    std::thread worker([&] { ok = run(data, track, pcm, rate, channels); });
    if (had) {
        esp_pthread_set_cfg(&previous);
    } else {
        const esp_pthread_cfg_t defaults = esp_pthread_get_default_config();
        esp_pthread_set_cfg(&defaults);
    }
    worker.join();
    if (!ok) return false;
    // Already mono at 16 kHz (or the source's lower rate), pre-skip dropped.
    clip.format.sample_rate = rate;
    clip.format.channels = 1;
    clip.samples = std::move(pcm);
    return !clip.samples.empty();
}

}  // namespace aura::firmware
