#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "aura/cloud_keys.hpp"
#include "aura/https_client.hpp"
#include "aura/provider.hpp"
#include "aura/voice.hpp"

namespace aura {

class AssistantStreamMailbox;

// The device's own assistant (the owner's choice, 2026-11-05): straight from
// the device, Groq recognises speech (whisper-large-v3-turbo) and answers
// (openai/gpt-oss-120b, streamed), and Azure speaks (the el-GR neural voices),
// with the requests, settings and error codes of the PC bridge
// (bridge/src/groq-runner.mjs, bridge/src/speech.mjs). Audio and text leave
// the device for those services.

// Groq's OpenAI-style server-sent events: each "data:" line carries a delta,
// "data: [DONE]" ends the answer (bridge: consumeGroqSse). The browser
// search's source markers, 【1†L4-L5】, are dropped on the way (bridge:
// createCitationFilter).
class OpenAiStreamDecoder {
public:
    static constexpr std::size_t kMaxAnswerBytes = 12000;
    explicit OpenAiStreamDecoder(AssistantStreamMailbox* mailbox = nullptr) : mailbox_(mailbox) {}
    // False once the stream failed (error_code() says how).
    bool push(std::string_view bytes);
    // The end of the stream: true with an answer.
    bool finish();
    bool done() const noexcept { return done_; }
    const std::string& text() const noexcept { return text_; }
    const std::string& error_code() const noexcept { return error_; }

private:
    bool line(std::string_view line);
    bool publish(const std::string& text);
    bool fail(const char* code);
    AssistantStreamMailbox* mailbox_{};
    std::string pending_;
    std::string held_;  // a possible source marker's start, held until it ends or proves not to be one
    std::string answer_;
    std::string text_;
    std::string error_;
    bool done_{false};
};

// One thread that runs the cloud requests in order (they block for seconds).
// The firmware gives it a large enough stack (app_main sets esp_pthread_cfg).
// Destruction finishes the queued requests, so every promise is kept.
class CloudWorker {
public:
    CloudWorker();
    ~CloudWorker();
    CloudWorker(const CloudWorker&) = delete;
    CloudWorker& operator=(const CloudWorker&) = delete;
    void post(std::function<void()> job);

private:
    void run();
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<std::function<void()>> jobs_;
    bool stopping_{false};
    std::thread thread_;
};

// Answers from Groq. `https` and `worker` outlive the provider.
class CloudAssistantProvider final : public IAssistantProvider {
public:
    static constexpr const char* kModel = "openai/gpt-oss-120b";
    CloudAssistantProvider(IHttpsClient& https, CloudWorker& worker, std::string groq_api_key);
    ~CloudAssistantProvider() override;
    std::future<AssistantReply> start_turn(TurnRequest request) override;
    void cancel() override;
    const char* name() const noexcept override { return "groq"; }

private:
    IHttpsClient& https_;
    CloudWorker& worker_;
    std::string key_;
};

// Speech recognition (Groq) and the voice (Azure). Without Azure keys AURA
// still listens. `https` and `worker` outlive the service.
class CloudSpeechService final : public ISpeechService {
public:
    CloudSpeechService(IHttpsClient& https, CloudWorker& worker, CloudKeys keys);
    ~CloudSpeechService() override;
    bool stt_available() const override;
    bool tts_available(const std::string& language) const override;
    std::future<TranscriptResult> transcribe(std::vector<std::uint8_t> wav, std::string language) override;
    std::future<SpeechResult> synthesize(std::string text, std::string language, std::string voice) override;

private:
    IHttpsClient& https_;
    CloudWorker& worker_;
    CloudKeys keys_;
    std::string azure_url_;
};

// A transcript as the bridge cleans it: bracketed sound tags out, white space
// collapsed, at most 600 characters.
std::string clean_transcript(std::string_view text);

}  // namespace aura
