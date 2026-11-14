#include "aura/voice_turn.hpp"

#include <chrono>
#include <optional>
#include <utility>

namespace aura {
namespace {

template <typename T>
bool finished(const std::future<T>& future) {
    return future.valid() && future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready;
}

// The service could not be reached at all: the assistant is offline, not wrong.
bool unreachable(const std::string& code) {
    return code == "PROVIDER_UNAVAILABLE" || code == "STT_UNAVAILABLE" || code == "PROVIDER_NOT_CONFIGURED" ||
           code == "NETWORK_UNAVAILABLE";
}

}  // namespace

VoiceAssistantTurn::VoiceAssistantTurn(AssistantController& controller, ISpeechService& speech,
                                       IAssistantProvider& provider, IAudioService& audio, Clock clock)
    : controller_(controller), speech_(speech), provider_(provider), audio_(audio), clock_(std::move(clock)) {
    if (!clock_) {
        clock_ = [] {
            return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
        };
    }
}

void VoiceAssistantTurn::set_answer_copy(std::string action_question, std::string action_refused,
                                         std::string empty_answer) {
    action_question_ = std::move(action_question);
    action_refused_ = std::move(action_refused);
    empty_answer_ = std::move(empty_answer);
}

void VoiceAssistantTurn::begin_listening(bool automatic) {
    // A new question interrupts the previous one, and the voice still speaking it.
    cancel();
    const auto state = audio_.diagnostics().state;
    if (state == AudioState::Playing || state == AudioState::Paused) audio_.stop();
    if (is_turn_active(controller_.snapshot().state)) controller_.dispatch({EventType::Reset, {}, 0});
    controller_.dispatch({EventType::WakeDetected, {}, 0});
    controller_.dispatch({EventType::BeginRecording, {}, 0});
    listening_ = controller_.snapshot().state == AssistantState::Listening;
    automatic_listen_ = automatic;
}

void VoiceAssistantTurn::finish_listening(std::vector<std::uint8_t> wav, bool heard) {
    if (!listening_) return;
    listening_ = false;
    if (controller_.snapshot().state != AssistantState::Listening) return;  // cancelled meanwhile
    if (!heard) {
        if (automatic_listen_) {
            controller_.dispatch({EventType::ListeningExpired, {}, 0});
        } else {
            controller_.dispatch({EventType::EndRecording, {}, 0});
            controller_.dispatch({EventType::Fail, "NO_SPEECH", 0});
        }
        return;
    }
    controller_.dispatch({EventType::EndRecording, {}, 0});
    transcript_ = speech_.transcribe(std::move(wav), interface_language_code(language_));
}

void VoiceAssistantTurn::ask(std::string question) {
    if (reply_.valid()) {
        provider_.cancel();
        reply_ = {};
    }
    if (!controller_.dispatch({EventType::TurnStarted, question, 0})) return;
    question_ = question;
    TurnRequest request;
    request.history = memory_.recent(clock_());
    request.context = context_ ? context_(question_) : std::string{};
    request.interface_language = language_;
    request.web_search = question_requests_web_search(question_);
    request.prompt = std::move(question);
    stream_ = std::make_shared<AssistantStreamMailbox>();
    stream_sequence_ = 0;
    request.stream = stream_;
    reply_ = provider_.start_turn(std::move(request));
}

void VoiceAssistantTurn::cancel() {
    if (reply_.valid()) provider_.cancel();
    reply_ = {};
    transcript_ = {};
    speech_future_ = {};
    stream_.reset();
    listening_ = false;
    reply_playing_ = false;
}

bool VoiceAssistantTurn::asking() const noexcept {
    const auto& snapshot = controller_.snapshot();
    return (snapshot.state == AssistantState::Thinking || snapshot.state == AssistantState::Responding) &&
           snapshot.user_text == question_;
}

bool VoiceAssistantTurn::poll() {
    bool changed = false;
    if (finished(transcript_)) {
        auto result = transcript_.get();
        if (controller_.snapshot().state == AssistantState::Transcribing) {
            if (result.ok && local_ && local_(result.text)) {
                // The host did it; the turn ends, unless the host ended it already.
                if (controller_.snapshot().state == AssistantState::Transcribing) {
                    controller_.dispatch({EventType::Cancel, {}, 0});
                }
            } else if (result.ok) {
                ask(std::move(result.text));
            } else {
                fail(result.error_code);
            }
            changed = true;
        }
    }
    if (stream_ && asking()) {
        AssistantStreamSnapshot update;
        if (stream_->take_after(stream_sequence_, update)) {
            stream_sequence_ = update.sequence;
            if (!update.text.empty() && controller_.dispatch({EventType::PartialResult, update.text, 0})) changed = true;
        }
    }
    if (finished(reply_)) {
        const auto reply = reply_.get();
        stream_.reset();
        if (asking()) {
            if (reply.ok) {
                deliver(reply.text);
                changed = true;
            } else if (reply.error_code != "CANCELLED") {
                fail(reply.error_code);
                changed = true;
            }
        }
    }
    if (finished(speech_future_)) {
        auto spoken = speech_future_.get();
        // The answer may have been closed while its voice was on the way.
        if (spoken.ok && controller_.snapshot().state == AssistantState::Responding &&
            audio_.play_pcm(spoken.clip)) {
            reply_playing_ = true;
            reply_started_ = false;
            reply_since_ms_ = clock_();
        }
    }
    if (reply_playing_) {
        const auto state = audio_.diagnostics().state;
        const bool sounding = state == AudioState::Playing || state == AudioState::Buffering ||
                              state == AudioState::Paused;
        constexpr std::uint64_t kStartWithinMs = 3000;
        if (sounding) {
            reply_started_ = true;
        } else if (reply_started_ || clock_() - reply_since_ms_ > kStartWithinMs) {
            reply_playing_ = false;
            // Heard to its end, with nothing waiting for a confirmation.
            if (reply_started_ && follow_up_ && listen_again_ &&
                controller_.snapshot().state == AssistantState::Responding) {
                listen_again_();
                changed = true;
            }
        }
    }
    return changed;
}

void VoiceAssistantTurn::deliver(const std::string& raw) {
    auto parsed = parse_assistant_answer(raw);
    // A message goes to the message handler once the answer is shown; any
    // action without its handler is refused, and the answer says so.
    std::optional<AssistantAction> message;
    if (parsed.action && parsed.action->type == AssistantActionType::Message && message_handler_) {
        message = std::move(parsed.action);
        parsed.action.reset();
    }
    if (parsed.action && (!action_handler_ || parsed.action->type == AssistantActionType::Message)) {
        parsed.action_rejected = true;
        parsed.action.reset();
    }
    std::string text = answer_display_text(parsed, action_question_, action_refused_);
    if (text.empty()) text = empty_answer_;
    controller_.dispatch({EventType::FinalResult, text, 0});
    memory_.record(question_, text, clock_());
    if (message) {
        message_handler_(*message);
    } else if (parsed.action) {
        action_handler_(*parsed.action);
        controller_.dispatch({EventType::RequireConfirmation, assistant_action_name(parsed.action->type), 0});
    }
    const char* language = interface_language_code(language_);
    if (spoken_ && speech_.tts_available(language)) speech_future_ = speech_.synthesize(text, language, voice_);
}

void VoiceAssistantTurn::fail(const std::string& code) {
    controller_.dispatch({unreachable(code) ? EventType::ProviderUnavailable : EventType::Fail, code, 0});
}

}  // namespace aura
