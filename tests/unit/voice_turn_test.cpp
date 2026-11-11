// A spoken question to the device's own assistant, from the talk target to the
// spoken answer: VoiceAssistantTurn drives the controller with fake services.
#include "aura/assistant_state.hpp"
#include "aura/voice_turn.hpp"

#include <cstdint>
#include <cstdlib>
#include <future>
#include <iostream>
#include <string>
#include <vector>

namespace {

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

template <typename T>
std::future<T> ready(T value) {
    std::promise<T> promise;
    promise.set_value(std::move(value));
    return promise.get_future();
}

struct FakeSpeech final : aura::ISpeechService {
    aura::TranscriptResult transcript{true, "Τι ώρα είναι;", {}};
    bool tts{true};
    std::vector<std::string> transcribed_languages;
    std::vector<std::string> spoken;
    std::string voice;
    bool stt_available() const override { return true; }
    bool tts_available(const std::string&) const override { return tts; }
    std::future<aura::TranscriptResult> transcribe(std::vector<std::uint8_t>, std::string language) override {
        transcribed_languages.push_back(language);
        return ready(transcript);
    }
    std::future<aura::SpeechResult> synthesize(std::string text, std::string, std::string chosen) override {
        spoken.push_back(text);
        voice = chosen;
        aura::SpeechResult result;
        result.ok = true;
        result.clip.samples = {1, 2, 3};
        return ready(result);
    }
};

struct FakeProvider final : aura::IAssistantProvider {
    aura::AssistantReply reply{true, "Είναι 10:00.", {}};
    std::vector<aura::TurnRequest> requests;
    bool hold{false};  // keep the reply back until release()
    std::promise<aura::AssistantReply> held;
    int cancels{0};
    std::future<aura::AssistantReply> start_turn(aura::TurnRequest request) override {
        requests.push_back(request);
        if (request.stream) request.stream->publish_delta("Είναι ");
        if (!hold) return ready(reply);
        held = std::promise<aura::AssistantReply>();
        return held.get_future();
    }
    void release() { held.set_value(reply); }
    void cancel() override { ++cancels; }
    const char* name() const noexcept override { return "fake"; }
};

struct FakeAudio final : aura::IAudioService {
    int played{0};
    int stopped{0};
    bool initialize() override { return true; }
    void shutdown() override {}
    bool play_pcm(const aura::PcmClip&) override {
        ++played;
        return true;
    }
    bool play_alert(const aura::PcmClip&) override { return true; }
    bool play_wav(const std::string&) override { return true; }
    bool play_test_tone(std::uint32_t) override { return true; }
    bool pause() override { return true; }
    bool resume() override { return true; }
    void stop() override { ++stopped; }
    void set_volume(float) override {}
    void set_muted(bool) override {}
    void tick(std::uint32_t) override {}
    aura::AudioDiagnostics diagnostics() const override { return {}; }
};

// Runs poll() until nothing changes.
void settle(aura::VoiceAssistantTurn& turn) {
    for (int pass = 0; pass < 8; ++pass) turn.poll();
}

}  // namespace

int main() {
    using aura::AssistantState;
    aura::AssistantController controller;
    FakeSpeech speech;
    FakeProvider provider;
    FakeAudio audio;
    aura::VoiceAssistantTurn turn(controller, speech, provider, audio);
    turn.set_answer_copy("Να το κάνω;", "Δεν έγινε τίποτα.", "Δεν έχω απάντηση.");
    std::string facts_for;
    turn.set_context([&facts_for](const std::string& question) {
        facts_for = question;
        return std::string("Μπαταρία: 80%");
    });

    turn.begin_listening();
    expect(controller.snapshot().state == AssistantState::Listening, "the talk target starts listening");
    turn.finish_listening(aura::make_wav(std::vector<std::int16_t>(8000, 0), 16000));
    expect(controller.snapshot().state == AssistantState::Transcribing && speech.transcribed_languages.size() == 1 &&
               speech.transcribed_languages[0] == "el",
           "the recording is transcribed in the interface language");
    settle(turn);
    expect(provider.requests.size() == 1 && provider.requests[0].prompt == "Τι ώρα είναι;" &&
               provider.requests[0].context == "Μπαταρία: 80%" && facts_for == "Τι ώρα είναι;" &&
               provider.requests[0].interface_language == aura::InterfaceLanguage::Greek &&
               provider.requests[0].history.empty(),
           "the question goes to the assistant with the device facts");
    expect(controller.snapshot().state == AssistantState::Responding &&
               controller.snapshot().answer_text == "Είναι 10:00.",
           "the answer is shown");
    expect(speech.spoken.size() == 1 && speech.spoken[0] == "Είναι 10:00." && speech.voice == "nestoras" &&
               audio.played == 1,
           "and spoken");

    // A follow-up carries the conversation so far.
    controller.dispatch({aura::EventType::Reset});
    speech.transcript = {true, "Και αύριο;", {}};
    turn.begin_listening();
    turn.finish_listening(aura::make_wav(std::vector<std::int16_t>(8000, 0), 16000));
    settle(turn);
    expect(provider.requests.size() == 2 && provider.requests[1].history.size() == 1 &&
               provider.requests[1].history[0].question == "Τι ώρα είναι;" &&
               provider.requests[1].history[0].answer == "Είναι 10:00.",
           "a follow-up question remembers the last answer");

    // A proposed action is refused for now: the screen says nothing was done.
    controller.dispatch({aura::EventType::Reset});
    provider.reply = {true, "Βάζω χρονόμετρο.\nACTION: {\"type\":\"timer\",\"seconds\":600,\"label\":\"pasta\"}", {}};
    turn.ask("Βάλε χρονόμετρο δέκα λεπτά");
    settle(turn);
    const auto& answered = controller.snapshot();
    expect(answered.state == AssistantState::Responding &&
               answered.answer_text.find("Βάζω χρονόμετρο.") != std::string::npos &&
               answered.answer_text.find("Δεν έγινε τίποτα.") != std::string::npos &&
               answered.answer_text.find("ACTION") == std::string::npos,
           "an action is not done on the device yet, and the answer says so");

    // Failures.
    controller.dispatch({aura::EventType::Reset});
    speech.transcript = {false, {}, "NO_SPEECH"};
    turn.begin_listening();
    turn.finish_listening(aura::make_wav(std::vector<std::int16_t>(8000, 0), 16000));
    settle(turn);
    expect(controller.snapshot().state != AssistantState::Transcribing &&
               controller.snapshot().state != AssistantState::Thinking && provider.requests.size() == 3,
           "silence asks nothing");
    controller.dispatch({aura::EventType::Reset});
    provider.reply = {false, {}, "PROVIDER_UNAVAILABLE"};
    turn.ask("Καιρός;");
    settle(turn);
    expect(controller.snapshot().state == AssistantState::Offline, "an unreachable service is offline");
    controller.dispatch({aura::EventType::Reset});
    provider.reply = {false, {}, "RATE_LIMIT"};
    turn.ask("Καιρός;");
    settle(turn);
    expect(controller.snapshot().state == AssistantState::Error && controller.snapshot().error_code == "RATE_LIMIT",
           "other failures show their code");

    // Cancelling drops a late answer.
    controller.dispatch({aura::EventType::Reset});
    provider.reply = {true, "Αργή απάντηση.", {}};
    provider.hold = true;
    turn.ask("Πες κάτι");
    turn.poll();
    turn.cancel();
    expect(provider.cancels == 1, "cancelling stops the request");
    controller.dispatch({aura::EventType::Reset});
    provider.release();
    const std::size_t spoken_before = speech.spoken.size();
    settle(turn);
    expect(controller.snapshot().state == AssistantState::Idle && speech.spoken.size() == spoken_before,
           "a cancelled answer is neither shown nor spoken");

    // Without a voice the answer is only shown.
    provider.hold = false;
    provider.reply = {true, "Σιωπηλά.", {}};
    speech.tts = false;
    turn.ask("Πες κάτι");
    settle(turn);
    expect(controller.snapshot().answer_text == "Σιωπηλά." && speech.spoken.size() == spoken_before,
           "without Azure the answer is shown, not spoken");

    // A question the device answers itself (the radio) never reaches the
    // assistant, and the turn ends.
    controller.dispatch({aura::EventType::Reset});
    std::vector<std::string> local;
    turn.set_local_request([&local](const std::string& question) {
        local.push_back(question);
        return question == "Βάλε Σφαίρα";
    });
    const std::size_t asked_before = provider.requests.size();
    speech.transcript = {true, "Βάλε Σφαίρα", {}};
    turn.begin_listening();
    turn.finish_listening(aura::make_wav(std::vector<std::int16_t>(8000, 0), 16000));
    settle(turn);
    expect(local.size() == 1 && local[0] == "Βάλε Σφαίρα" && provider.requests.size() == asked_before &&
               controller.snapshot().state == AssistantState::Idle,
           "a local request is the device's own, and the turn ends");
    speech.transcript = {true, "Τι ώρα είναι;", {}};
    turn.begin_listening();
    turn.finish_listening(aura::make_wav(std::vector<std::int16_t>(8000, 0), 16000));
    settle(turn);
    expect(local.size() == 2 && provider.requests.size() == asked_before + 1,
           "anything else still goes to the assistant");
    std::cout << "voice_turn_test: PASS\n";
    return 0;
}
