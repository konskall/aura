#include "aura/cloud_assistant.hpp"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <random>

#include "aura/assistant_stream.hpp"
#include "aura/json_reader.hpp"
#include "aura/mp3_decoder.hpp"
#include "aura/text_util.hpp"
#include "aura/turn_prompt.hpp"

namespace aura {
namespace {

constexpr const char* kGroqChatUrl = "https://api.groq.com/openai/v1/chat/completions";
constexpr const char* kGroqTranscriptionUrl = "https://api.groq.com/openai/v1/audio/transcriptions";
constexpr const char* kTranscriptionModel = "whisper-large-v3-turbo";
constexpr const char* kAzureOutputFormat = "audio-24khz-48kbitrate-mono-mp3";
// The bridge's speech limits (bridge/src/speech.mjs speechLimits).
constexpr std::size_t kMaxWavBytes = 1100000;
constexpr std::size_t kMinWavSamples = 4800;
constexpr std::size_t kMaxTtsTextBytes = 2000;
constexpr std::size_t kMaxTtsAudioBytes = 2 * 1024 * 1024;
constexpr std::size_t kMaxTranscriptCharacters = 600;

// The languages AURA speaks, their locales and the two voice profiles
// (bridge: SPEECH_LANGUAGES, NEURAL_VOICE_PROFILES).
struct Locale {
    const char* code;
    const char* locale;
    const char* athina;
    const char* nestoras;
};
constexpr Locale kLocales[] = {
    {"el", "el-GR", "el-GR-AthinaNeural", "el-GR-NestorasNeural"},
    {"en", "en-US", "en-US-AvaMultilingualNeural", "en-US-AndrewMultilingualNeural"},
    {"de", "de-DE", "de-DE-KatjaNeural", "de-DE-ConradNeural"},
    {"fr", "fr-FR", "fr-FR-DeniseNeural", "fr-FR-HenriNeural"},
    {"it", "it-IT", "it-IT-ElsaNeural", "it-IT-DiegoNeural"},
    {"es", "es-ES", "es-ES-ElviraNeural", "es-ES-AlvaroNeural"},
};

const Locale* find_locale(const std::string& code) {
    for (const auto& locale : kLocales) {
        if (code == locale.code) return &locale;
    }
    return nullptr;
}

std::size_t utf8_length(unsigned char lead) {
    if (lead >= 0xF0) return 4;
    if (lead >= 0xE0) return 3;
    if (lead >= 0xC0) return 2;
    return 1;
}

bool space_at(std::string_view text, std::size_t at, std::size_t& length) {
    const auto ch = static_cast<unsigned char>(text[at]);
    if (ch == ' ' || (ch >= '\t' && ch <= '\r')) {
        length = 1;
        return true;
    }
    if (ch == 0xC2 && at + 1 < text.size() && static_cast<unsigned char>(text[at + 1]) == 0xA0) {
        length = 2;
        return true;
    }
    return false;
}

// Runs of white space become one space, and the ends are trimmed.
std::string collapse_space(std::string_view text) {
    std::string out;
    bool pending = false;
    for (std::size_t index = 0; index < text.size();) {
        std::size_t length = 0;
        if (space_at(text, index, length)) {
            pending = !out.empty();
            index += length;
            continue;
        }
        if (pending) out += ' ';
        pending = false;
        out += text[index++];
    }
    return out;
}

// Model output for the screen: CR LF becomes LF and control characters other
// than tab, LF and CR go (bridge: sanitizeStreamDelta / sanitizeDisplayText).
std::string sanitize(std::string_view text, bool trim) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t index = 0; index < text.size(); ++index) {
        const auto ch = static_cast<unsigned char>(text[index]);
        if (ch == '\r' && index + 1 < text.size() && text[index + 1] == '\n') continue;
        if ((ch < 0x20 && ch != '\t' && ch != '\n' && ch != '\r') || ch == 0x7F) continue;
        out += static_cast<char>(ch);
    }
    if (!trim) return out;
    const auto first = out.find_first_not_of(" \t\n\r\v\f");
    if (first == std::string::npos) return {};
    return out.substr(first, out.find_last_not_of(" \t\n\r\v\f") - first + 1);
}

std::string trim_ascii(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    return std::string(text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1));
}

std::string xml_escape(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 16);
    for (const char ch : text) {
        switch (ch) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default: out += ch; break;
        }
    }
    return out;
}

std::string azure_url(std::string region) {
    std::transform(region.begin(), region.end(), region.begin(),
                   [](char ch) { return static_cast<char>(ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch); });
    region = trim_ascii(region);
    const bool valid = region.size() >= 2 && region.size() <= 40 &&
                       std::all_of(region.begin(), region.end(), [](char ch) {
                           return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-';
                       });
    return valid ? "https://" + region + ".tts.speech.microsoft.com/cognitiveservices/v1" : std::string{};
}

// No response at all: the transport's own code when it is one the screen
// explains (TIMEOUT, CANCELLED), otherwise `otherwise`.
std::string transport_error(const HttpsResponse& response, const char* otherwise) {
    if (response.error == "TIMEOUT" || response.error == "CANCELLED") return response.error;
    return otherwise;
}

void wipe(std::string& text) {
    std::fill(text.begin(), text.end(), '\0');
    text.clear();
}

// The request's headers carry a key: wipe them once the request is over.
void wipe_headers(HttpsRequest& request) {
    for (auto& header : request.headers) wipe(header.value);
}

AssistantReply ask_groq(IHttpsClient& https, const std::string& key, const TurnRequest& request) {
    if (request.prompt.size() > kTurnMaxPromptBytes) return {false, {}, "PROMPT_TOO_LARGE"};
    const std::string instructions = build_turn_instructions(request);
    if (instructions.empty()) return {false, {}, "INVALID_REQUEST"};
    if (instructions.size() > kTurnMaxInstructionsBytes) return {false, {}, "PROMPT_TOO_LARGE"};
    // The PC bridge's Groq runner: one user message, streamed, temperature 0.2.
    HttpsRequest http;
    http.url = kGroqChatUrl;
    http.headers = {{"Authorization", "Bearer " + key}, {"Content-Type", "application/json"}};
    http.body = std::string("{\"model\":\"") + CloudAssistantProvider::kModel +
                "\",\"messages\":[{\"role\":\"user\",\"content\":\"" + json_escape(instructions) +
                "\"}],\"stream\":true,\"temperature\":0.2,\"max_completion_tokens\":1200}";
    http.timeout_ms = 45000;
    OpenAiStreamDecoder decoder(request.stream.get());
    const auto response = https.post(http, [&decoder](std::string_view chunk) { return decoder.push(chunk); });
    wipe_headers(http);
    if (!decoder.error_code().empty()) return {false, {}, decoder.error_code()};
    if (response.status == 0) return {false, {}, transport_error(response, "PROVIDER_UNAVAILABLE")};
    if (response.status == 401 || response.status == 403) return {false, {}, "PROVIDER_AUTH_FAILED"};
    if (response.status == 429) return {false, {}, "RATE_LIMIT"};
    if (response.status >= 400 && response.status < 500) return {false, {}, "PROVIDER_REQUEST_FAILED"};
    if (response.status < 200 || response.status >= 300) return {false, {}, "PROVIDER_UNAVAILABLE"};
    if (!decoder.finish()) return {false, {}, decoder.error_code()};
    return {true, decoder.text(), {}};
}

std::string boundary_for(const std::vector<std::uint8_t>& wav) {
    static std::random_device random;
    for (;;) {
        char boundary[40];
        std::snprintf(boundary, sizeof(boundary), "aura-%08x%08x", random(), random());
        const std::string_view text(boundary);
        const auto found = std::search(wav.begin(), wav.end(), text.begin(), text.end());
        if (found == wav.end()) return boundary;
    }
}

TranscriptResult transcribe_with_groq(IHttpsClient& https, const std::string& key,
                                      const std::vector<std::uint8_t>& wav, const std::string& language) {
    if (language != "auto" && !find_locale(language)) return {false, {}, "INVALID_LANGUAGE"};
    PcmClip clip;
    if (wav.size() > kMaxWavBytes || !parse_wav(wav.data(), wav.size(), clip)) return {false, {}, "INVALID_AUDIO"};
    if (clip.samples.size() / std::max<std::size_t>(1, clip.format.channels) < kMinWavSamples) {
        return {false, {}, "AUDIO_TOO_SHORT"};
    }
    // The PC bridge's form: the WAV, the model, JSON out, temperature 0, the language.
    const std::string boundary = boundary_for(wav);
    const auto field = [&boundary](const char* name, const std::string& value) {
        return "--" + boundary + "\r\nContent-Disposition: form-data; name=\"" + name + "\"\r\n\r\n" + value + "\r\n";
    };
    HttpsRequest http;
    http.url = kGroqTranscriptionUrl;
    http.headers = {{"Authorization", "Bearer " + key},
                    {"Content-Type", "multipart/form-data; boundary=" + boundary}};
    http.body = "--" + boundary +
                "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"aura.wav\"\r\n"
                "Content-Type: audio/wav\r\n\r\n";
    http.body.append(wav.begin(), wav.end());
    http.body += "\r\n" + field("model", kTranscriptionModel) + field("response_format", "json") +
                 field("temperature", "0");
    if (language != "auto") http.body += field("language", language);
    http.body += "--" + boundary + "--\r\n";
    http.timeout_ms = 60000;
    http.max_body_bytes = 64 * 1024;
    const auto response = https.post(http, {});
    wipe_headers(http);
    if (response.status == 0) return {false, {}, transport_error(response, "STT_UNAVAILABLE")};
    if (response.status == 401 || response.status == 403) return {false, {}, "PROVIDER_AUTH_FAILED"};
    if (response.status == 429) return {false, {}, "RATE_LIMIT"};
    if (response.status >= 500) return {false, {}, "STT_UNAVAILABLE"};
    if (response.status < 200 || response.status >= 300) return {false, {}, "STT_FAILED"};
    JsonValue payload;
    const std::string text = parse_json(response.body, payload) ? clean_transcript(payload.string_at("text")) : "";
    if (text.empty()) return {false, {}, "NO_SPEECH"};
    return {true, text, {}};
}

SpeechResult synthesize_with_azure(IHttpsClient& https, const std::string& key, const std::string& url,
                                   const std::string& text, const std::string& language, const std::string& voice) {
    const auto* locale = find_locale(language);
    if (!locale) return {false, {}, "INVALID_LANGUAGE"};
    if (voice != "athina" && voice != "nestoras") return {false, {}, "INVALID_VOICE"};
    std::string said;
    said.reserve(text.size());
    for (const char ch : text) {
        const auto byte = static_cast<unsigned char>(ch);
        said += byte < 0x20 || byte == 0x7F ? ' ' : ch;
    }
    said = collapse_space(said);
    if (said.empty()) return {false, {}, "INVALID_REQUEST"};
    if (said.size() > kMaxTtsTextBytes) return {false, {}, "TEXT_TOO_LARGE"};
    if (key.empty() || url.empty()) return {false, {}, "TTS_UNAVAILABLE"};
    HttpsRequest http;
    http.url = url;
    http.headers = {{"Ocp-Apim-Subscription-Key", key},
                    {"Content-Type", "application/ssml+xml"},
                    {"X-Microsoft-OutputFormat", kAzureOutputFormat},
                    {"User-Agent", "AURA"}};
    http.body = std::string("<speak version=\"1.0\" xml:lang=\"") + locale->locale + "\"><voice name=\"" +
                (voice == "athina" ? locale->athina : locale->nestoras) + "\">" + xml_escape(said) +
                "</voice></speak>";
    http.max_body_bytes = kMaxTtsAudioBytes;
    const auto response = https.post(http, {});
    wipe_headers(http);
    if (response.status == 0) return {false, {}, transport_error(response, "TTS_UNAVAILABLE")};
    if (response.status == 401 || response.status == 403) return {false, {}, "PROVIDER_AUTH_FAILED"};
    if (response.status == 429) return {false, {}, "RATE_LIMIT"};
    if (response.status >= 500) return {false, {}, "TTS_UNAVAILABLE"};
    if (response.status < 200 || response.status >= 300) return {false, {}, "TTS_FAILED"};
    // The MP3 becomes PCM for the speaker, fed in pieces the decoder can hold.
    Mp3StreamDecoder decoder;
    Mp3Pcm pcm;
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(response.body.data());
    constexpr std::size_t kPiece = 16 * 1024;
    for (std::size_t offset = 0; offset < response.body.size(); offset += kPiece) {
        decoder.feed(bytes + offset, std::min(kPiece, response.body.size() - offset));
        while (decoder.decode(pcm) > 0) {
        }
    }
    while (decoder.decode(pcm, 1152 * 8, true) > 0) {
    }
    if (pcm.samples.empty() || pcm.sample_rate == 0 || pcm.channels == 0) return {false, {}, "TTS_FAILED"};
    SpeechResult result;
    result.ok = true;
    result.clip.format.sample_rate = pcm.sample_rate;
    result.clip.format.channels = pcm.channels;
    result.clip.samples = std::move(pcm.samples);
    return result;
}

}  // namespace

std::string clean_transcript(std::string_view text) {
    // Sound tags the model writes for non-speech ("[music]", "(laughs)",
    // "*cough*"), at most 40 characters inside, become a space.
    std::string untagged;
    for (std::size_t index = 0; index < text.size();) {
        const char open = text[index];
        const char close = open == '[' ? ']' : open == '(' ? ')' : open == '*' ? '*' : '\0';
        if (close != '\0') {
            std::size_t cursor = index + 1;
            int characters = 0;
            while (cursor < text.size() && text[cursor] != close && characters <= 40) {
                cursor += utf8_length(static_cast<unsigned char>(text[cursor]));
                ++characters;
            }
            if (cursor < text.size() && text[cursor] == close && characters <= 40) {
                untagged += ' ';
                index = cursor + 1;
                continue;
            }
        }
        untagged += text[index++];
    }
    for (auto& ch : untagged) {
        if (static_cast<unsigned char>(ch) < 0x20 || ch == 0x7F) ch = ' ';
    }
    std::string cleaned = collapse_space(untagged);
    std::size_t characters = 0;
    for (std::size_t index = 0; index < cleaned.size(); index += utf8_length(static_cast<unsigned char>(cleaned[index]))) {
        if (characters++ == kMaxTranscriptCharacters) {
            cleaned.resize(index);
            break;
        }
    }
    return cleaned;
}

bool OpenAiStreamDecoder::fail(const char* code) {
    if (error_.empty()) error_ = code;
    return false;
}

bool OpenAiStreamDecoder::push(std::string_view bytes) {
    if (!error_.empty()) return false;
    if (done_) return true;
    pending_.append(bytes.data(), bytes.size());
    for (std::size_t newline = pending_.find('\n'); newline != std::string::npos; newline = pending_.find('\n')) {
        const std::string text = pending_.substr(0, newline);
        pending_.erase(0, newline + 1);
        if (!line(text)) return false;
        if (done_) return true;
    }
    // A line that never ends is not an event stream.
    if (pending_.size() > 64 * 1024) return fail("INVALID_RESPONSE");
    return true;
}

bool OpenAiStreamDecoder::line(std::string_view raw) {
    const std::string trimmed = trim_ascii(raw);
    if (trimmed.empty() || trimmed.rfind("data:", 0) != 0) return true;
    const std::string data = trim_ascii(std::string_view(trimmed).substr(5));
    if (data == "[DONE]") {
        if (answer_.empty()) return fail("INVALID_RESPONSE");
        text_ = sanitize(answer_, true);
        done_ = true;
        return true;
    }
    JsonValue event;
    JsonLimits limits;
    limits.max_bytes = 64 * 1024;
    if (!parse_json(data, event, limits)) return fail("INVALID_RESPONSE");
    if (const auto* error = event.find("error"); error && !error->is_null()) return fail("PROVIDER_ERROR");
    const auto* choices = event.find("choices");
    if (!choices || choices->items().empty()) return true;
    const auto* delta = choices->items().front().find("delta");
    const auto* content = delta ? delta->find("content") : nullptr;
    if (!content || content->type() != JsonType::String) return true;
    const std::string clean = sanitize(content->as_string(), false);
    if (clean.empty()) return true;
    answer_ += clean;
    if (answer_.size() > kMaxAnswerBytes) return fail("RESPONSE_TOO_LARGE");
    if (mailbox_) mailbox_->publish_delta(clean);
    return true;
}

bool OpenAiStreamDecoder::finish() {
    if (!error_.empty()) return false;
    if (done_) return true;
    if (!pending_.empty()) {
        const std::string tail = pending_;
        pending_.clear();
        if (!line(tail)) return false;
        if (done_) return true;
    }
    if (answer_.empty()) return fail("INVALID_RESPONSE");
    text_ = sanitize(answer_, true);
    done_ = true;
    return true;
}

CloudWorker::CloudWorker() : thread_([this] { run(); }) {}

CloudWorker::~CloudWorker() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    wake_.notify_all();
    thread_.join();
}

void CloudWorker::post(std::function<void()> job) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        jobs_.push_back(std::move(job));
    }
    wake_.notify_one();
}

void CloudWorker::run() {
    std::unique_lock<std::mutex> lock(mutex_);
    for (;;) {
        wake_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
        if (jobs_.empty()) return;  // stopping, and every request answered
        auto job = std::move(jobs_.front());
        jobs_.pop_front();
        lock.unlock();
        job();
        lock.lock();
    }
}

CloudAssistantProvider::CloudAssistantProvider(IHttpsClient& https, CloudWorker& worker, std::string groq_api_key)
    : https_(https), worker_(worker), key_(std::move(groq_api_key)) {}

CloudAssistantProvider::~CloudAssistantProvider() { wipe(key_); }

std::future<AssistantReply> CloudAssistantProvider::start_turn(TurnRequest request) {
    auto promise = std::make_shared<std::promise<AssistantReply>>();
    auto future = promise->get_future();
    if (key_.empty()) {
        promise->set_value({false, {}, "PROVIDER_NOT_CONFIGURED"});
        return future;
    }
    IHttpsClient* https = &https_;
    worker_.post([https, key = key_, request = std::move(request), promise]() mutable {
        promise->set_value(ask_groq(*https, key, request));
        wipe(key);
    });
    return future;
}

void CloudAssistantProvider::cancel() { https_.cancel(); }

CloudSpeechService::CloudSpeechService(IHttpsClient& https, CloudWorker& worker, CloudKeys keys)
    : https_(https), worker_(worker), keys_(std::move(keys)), azure_url_(azure_url(keys_.azure_speech_region)) {}

CloudSpeechService::~CloudSpeechService() { wipe_cloud_keys(keys_); }

bool CloudSpeechService::stt_available() const { return !keys_.groq_api_key.empty(); }

bool CloudSpeechService::tts_available(const std::string& language) const {
    return !keys_.azure_speech_key.empty() && !azure_url_.empty() && find_locale(language) != nullptr;
}

std::future<TranscriptResult> CloudSpeechService::transcribe(std::vector<std::uint8_t> wav, std::string language) {
    auto promise = std::make_shared<std::promise<TranscriptResult>>();
    auto future = promise->get_future();
    if (!stt_available()) {
        promise->set_value({false, {}, "STT_UNAVAILABLE"});
        return future;
    }
    IHttpsClient* https = &https_;
    worker_.post([https, key = keys_.groq_api_key, wav = std::move(wav), language = std::move(language),
                  promise]() mutable {
        promise->set_value(transcribe_with_groq(*https, key, wav, language));
        wipe(key);
    });
    return future;
}

std::future<SpeechResult> CloudSpeechService::synthesize(std::string text, std::string language, std::string voice) {
    auto promise = std::make_shared<std::promise<SpeechResult>>();
    auto future = promise->get_future();
    IHttpsClient* https = &https_;
    worker_.post([https, key = keys_.azure_speech_key, url = azure_url_, text = std::move(text),
                  language = std::move(language), voice = std::move(voice), promise]() mutable {
        promise->set_value(synthesize_with_azure(*https, key, url, text, language, voice));
        wipe(key);
    });
    return future;
}

}  // namespace aura
