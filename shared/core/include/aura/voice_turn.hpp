#pragma once

#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <vector>

#include "aura/assistant_companion.hpp"
#include "aura/assistant_state.hpp"
#include "aura/assistant_stream.hpp"
#include "aura/audio_service.hpp"
#include "aura/language.hpp"
#include "aura/provider.hpp"
#include "aura/voice.hpp"

namespace aura {

// A spoken question to the device's own assistant, from the press of the talk
// target to the spoken answer. It runs on the UI thread through poll(); the
// services wait on their own worker. The host's recorder hands over the WAV;
// this transcribes it, asks the provider with the device facts and the recent
// conversation, shows the answer as it streams and speaks it. An action the
// model proposes is refused for now (confirming one on the device comes
// later), and the answer says that nothing was done.
class VoiceAssistantTurn {
public:
    using Clock = std::function<std::uint64_t()>;  // milliseconds, for the conversation memory
    VoiceAssistantTurn(AssistantController& controller, ISpeechService& speech, IAssistantProvider& provider,
                       IAudioService& audio, Clock clock = {});

    void set_language(InterfaceLanguage language) noexcept { language_ = language; }
    void set_voice(std::string voice) { voice_ = std::move(voice); }
    void set_spoken_replies(bool spoken) noexcept { spoken_ = spoken; }
    // The device facts for a question (build_device_context), data for the model.
    void set_context(std::function<std::string(const std::string& question)> context) {
        context_ = std::move(context);
    }
    // Localized copy: the question under an action with no words, the note under
    // a refused action, and the text for an empty answer.
    void set_answer_copy(std::string action_question, std::string action_refused, std::string empty_answer);

    // The talk target was pressed: the assistant listens (the host starts its microphone).
    void begin_listening();
    // The recorder's WAV, once the speaker stopped or the target was released.
    void finish_listening(std::vector<std::uint8_t> wav);
    // A question that is already text.
    void ask(std::string question);
    // Drops whatever is in progress: the request, the transcript and the voice.
    void cancel();
    // Collects what the services finished; true when the controller changed.
    bool poll();
    bool listening() const noexcept { return listening_; }

private:
    void deliver(const std::string& raw);
    void fail(const std::string& code);
    bool asking() const noexcept;

    AssistantController& controller_;
    ISpeechService& speech_;
    IAssistantProvider& provider_;
    IAudioService& audio_;
    Clock clock_;
    InterfaceLanguage language_{InterfaceLanguage::Greek};
    std::string voice_{"nestoras"};
    bool spoken_{true};
    std::function<std::string(const std::string&)> context_;
    std::string action_question_;
    std::string action_refused_;
    std::string empty_answer_;
    ConversationMemory memory_;
    bool listening_{false};
    std::string question_;
    std::future<TranscriptResult> transcript_;
    std::future<AssistantReply> reply_;
    std::future<SpeechResult> speech_future_;
    std::shared_ptr<AssistantStreamMailbox> stream_;
    std::uint64_t stream_sequence_{0};
};

}  // namespace aura
