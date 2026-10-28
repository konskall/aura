#include "aura/bridge_speech_service.hpp"

#include "aura/bridge_http.hpp"
#include "aura/mp3_decoder.hpp"
#include "aura/text_util.hpp"

#include <SDL.h>

#include <algorithm>
#include <cstring>

namespace aura::simulator {
namespace {

bool decode_mp3(const std::string& bytes, PcmClip& clip) {
    if (bytes.size() < 128 || bytes.size() > 8U * 1024U * 1024U) return false;
    Mp3StreamDecoder decoder;
    Mp3Pcm pcm;
    constexpr std::size_t kNetworkChunk = 16U * 1024U;
    for (std::size_t offset = 0; offset < bytes.size(); offset += kNetworkChunk) {
        const auto count = std::min(kNetworkChunk, bytes.size() - offset);
        decoder.feed(reinterpret_cast<const std::uint8_t*>(bytes.data() + offset), count);
        while (decoder.decode(pcm, 1152U * 32U) > 0) {}
    }
    while (decoder.decode(pcm, 1152U * 32U, true) > 0) {}
    if (pcm.samples.empty() || pcm.sample_rate < 8000 || pcm.sample_rate > 48000 ||
        pcm.channels == 0 || pcm.channels > 2) {
        return false;
    }
    clip.format = {pcm.sample_rate, pcm.channels, 16};
    clip.samples = std::move(pcm.samples);
    return true;
}

}  // namespace

BridgeSpeechService::BridgeSpeechService(std::string token, std::uint16_t port)
    : token_(std::move(token)), port_(port) {}

void BridgeSpeechService::refresh_health() {
    const auto response = bridge_request(L"GET", port_, "/health", token_, {}, nullptr, 8000, 4096);
    const bool ok = response.status == 200;
    stt_->store(ok && json_bool(response.body, "stt"));
    tts_el_->store(ok && json_bool(response.body, "tts_el"));
    tts_en_->store(ok && json_bool(response.body, "tts_en"));
    const auto stt_provider = ok ? json_string(response.body, "stt_provider") : std::string{};
    const auto tts_provider = ok ? json_string(response.body, "tts_provider") : std::string{};
    stt_provider_->store(stt_provider == "groq" ? 2 : stt_provider == "local" ? 1 : 0);
    tts_provider_->store(tts_provider == "azure" ? 3 : tts_provider == "edge-neural" ? 2 :
                         tts_provider == "windows" ? 1 : 0);
}

bool BridgeSpeechService::tts_available(const std::string& language) const {
    if (language == "el") return tts_el_->load();
    if (language == "en") return tts_en_->load();
    return false;
}

const char* BridgeSpeechService::stt_provider_name() const noexcept {
    switch (stt_provider_->load()) {
        case 1: return "Local STT";
        case 2: return "Groq STT";
        default: return "";
    }
}

const char* BridgeSpeechService::tts_provider_name() const noexcept {
    switch (tts_provider_->load()) {
        case 1: return "Windows Voice";
        case 2: return "Neural Voice";
        case 3: return "Azure Neural";
        default: return "";
    }
}

std::future<TranscriptResult> BridgeSpeechService::transcribe(std::vector<std::uint8_t> wav, std::string language) {
    return std::async(std::launch::async, [token = token_, port = port_, wav = std::move(wav),
                                           language = std::move(language)] {
        TranscriptResult result;
        const std::string body(wav.begin(), wav.end());
        const auto response = bridge_request(L"POST", port, "/v1/speech/transcribe?lang=" + language, token, body,
                                             "audio/wav", 60000, 16384);
        if (response.status == 200) {
            result.text = sanitize_text(json_string(response.body, "text"), 600);
            result.ok = !result.text.empty();
            if (!result.ok) result.error_code = "NO_SPEECH";
            return result;
        }
        result.error_code = json_string(response.body, "code");
        if (result.error_code.empty()) result.error_code = response.status == 0 ? "BRIDGE_UNAVAILABLE" : "STT_FAILED";
        return result;
    });
}

std::future<SpeechResult> BridgeSpeechService::synthesize(std::string text, std::string language, std::string voice) {
    return std::async(std::launch::async, [token = token_, port = port_, text = std::move(text),
                                           language = std::move(language), voice = std::move(voice)] {
        SpeechResult result;
        const std::string payload = "{\"text\":\"" + json_escape(text) + "\",\"lang\":\"" +
                                    json_escape(language) + "\",\"voice\":\"" + json_escape(voice) + "\"}";
        const auto response = bridge_request(L"POST", port, "/v1/speech/synthesize", token, payload,
                                             "application/json", 45000, 8U * 1024U * 1024U);
        if (response.status == 200) {
            const bool wav = response.content_type.find("audio/wav") != std::string::npos &&
                parse_wav(reinterpret_cast<const std::uint8_t*>(response.body.data()), response.body.size(), result.clip);
            const bool mp3 = response.content_type.find("audio/mpeg") != std::string::npos &&
                decode_mp3(response.body, result.clip);
            if (wav || mp3) {
                result.ok = true;
                return result;
            }
        }
        result.error_code = json_string(response.body, "code");
        if (result.error_code.empty()) result.error_code = response.status == 0 ? "BRIDGE_UNAVAILABLE" : "TTS_FAILED";
        return result;
    });
}

SdlVoiceCapture::~SdlVoiceCapture() { stop(); }

bool SdlVoiceCapture::start() {
    stop();
    error_.clear();
    if (SDL_WasInit(SDL_INIT_AUDIO) == 0 && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        error_ = "AUDIO_SUBSYSTEM";
        return false;
    }
    SDL_AudioSpec desired{};
    desired.freq = static_cast<int>(VoiceRecorder::kSampleRate);
    desired.format = AUDIO_S16SYS;
    desired.channels = 1;
    desired.samples = 512;
    desired.callback = &SdlVoiceCapture::callback;
    desired.userdata = this;
    SDL_AudioSpec obtained{};
    // allowed_changes 0: SDL converts whatever the microphone delivers.
    const SDL_AudioDeviceID device = SDL_OpenAudioDevice(nullptr, 1, &desired, &obtained, 0);
    if (device == 0) {
        error_ = "MIC_UNAVAILABLE";
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_.clear();
    }
    device_ = device;
    SDL_PauseAudioDevice(device, 0);
    return true;
}

void SdlVoiceCapture::stop() {
    if (device_ == 0) return;
    SDL_CloseAudioDevice(static_cast<SDL_AudioDeviceID>(device_));
    device_ = 0;
}

void SdlVoiceCapture::drain(std::vector<std::int16_t>& output) {
    std::lock_guard<std::mutex> lock(mutex_);
    output.insert(output.end(), pending_.begin(), pending_.end());
    pending_.clear();
}

void SdlVoiceCapture::callback(void* user, std::uint8_t* stream, int length) {
    auto* self = static_cast<SdlVoiceCapture*>(user);
    const auto count = static_cast<std::size_t>(length) / sizeof(std::int16_t);
    std::lock_guard<std::mutex> lock(self->mutex_);
    // Bounded: the UI loop drains every few milliseconds.
    if (self->pending_.size() > VoiceRecorder::kSampleRate * 2U) return;
    const auto old_size = self->pending_.size();
    self->pending_.resize(old_size + count);
    std::memcpy(self->pending_.data() + old_size, stream, count * sizeof(std::int16_t));
}

}  // namespace aura::simulator
