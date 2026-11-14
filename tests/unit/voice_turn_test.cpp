// A spoken question to the device's own assistant, from the talk target to the
// spoken answer: VoiceAssistantTurn drives the controller with fake services.
#include "aura/aoraton.hpp"
#include "aura/assistant_actions.hpp"
#include "aura/assistant_state.hpp"
#include "aura/clock_services.hpp"
#include "aura/display_power.hpp"
#include "aura/notes.hpp"
#include "aura/organizer.hpp"
#include "aura/sounds.hpp"
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
    void set_volume(float value) override { volume = value; }
    void set_muted(bool value) override { muted = value; }
    void tick(std::uint32_t) override {}
    aura::AudioDiagnostics diagnostics() const override {
        aura::AudioDiagnostics result;
        result.state = state;
        result.volume = volume;
        result.muted = muted;
        return result;
    }
    float volume{0.5F};
    bool muted{false};
    aura::AudioState state{aura::AudioState::Ready};
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

    // Without an action handler a proposed action is refused: the screen says nothing was done.
    controller.dispatch({aura::EventType::Reset});
    provider.reply = {true, "Βάζω χρονόμετρο.\nACTION: {\"type\":\"timer\",\"seconds\":600,\"label\":\"pasta\"}", {}};
    turn.ask("Βάλε χρονόμετρο δέκα λεπτά");
    settle(turn);
    const auto& answered = controller.snapshot();
    expect(answered.state == AssistantState::Responding &&
               answered.answer_text.find("Βάζω χρονόμετρο.") != std::string::npos &&
               answered.answer_text.find("Δεν έγινε τίποτα.") != std::string::npos &&
               answered.answer_text.find("ACTION") == std::string::npos,
           "without an action handler a proposed action is refused, and the answer says so");

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

    // On the device a proposed action waits for the owner's confirmation, as
    // in the simulator: the answer asks, the host keeps the action.
    std::vector<aura::AssistantAction> proposed;
    turn.set_local_request({});
    turn.set_action_handler([&proposed](const aura::AssistantAction& action) { proposed.push_back(action); });
    controller.dispatch({aura::EventType::Reset});
    provider.reply = {true, "Βάζω χρονόμετρο.\nACTION: {\"type\":\"timer\",\"seconds\":600,\"label\":\"pasta\"}", {}};
    turn.ask("Βάλε χρονόμετρο δέκα λεπτά");
    settle(turn);
    expect(controller.snapshot().state == AssistantState::ActionConfirmation && proposed.size() == 1 &&
               proposed[0].type == aura::AssistantActionType::Timer && proposed[0].seconds == 600 &&
               controller.snapshot().answer_text.find("Δεν έγινε τίποτα.") == std::string::npos &&
               controller.snapshot().answer_text.find("ACTION") == std::string::npos,
           "a proposed action waits for the owner's confirmation");
    controller.dispatch({aura::EventType::Reset});
    provider.reply = {true, "Θα το στείλω.\nACTION: {\"type\":\"message\",\"room\":\"Family\",\"text\":\"Αργώ\"}", {}};
    turn.ask("Στείλε στην οικογένεια ότι αργώ");
    settle(turn);
    expect(proposed.size() == 1 && controller.snapshot().state == AssistantState::Responding &&
               controller.snapshot().answer_text.find("Δεν έγινε τίποτα.") != std::string::npos,
           "without a message handler a message by voice is refused");

    // With one, the message goes to the host (it opens Aoraton's own
    // confirmation); the answer is shown first and asks nothing itself.
    std::vector<aura::AssistantAction> messages;
    turn.set_message_handler([&messages](const aura::AssistantAction& action) { messages.push_back(action); });
    controller.dispatch({aura::EventType::Reset});
    turn.ask("Στείλε στην οικογένεια ότι αργώ");
    settle(turn);
    expect(messages.size() == 1 && messages[0].room == "Family" && messages[0].text == "Αργώ" &&
               proposed.size() == 1 && controller.snapshot().state == AssistantState::Responding &&
               controller.snapshot().answer_text.find("Δεν έγινε τίποτα.") == std::string::npos,
           "a message by voice goes to the host's message handler");

    // The room the owner named, or the only one; never a guess.
    std::vector<aura::AoratonRoom> rooms(2);
    rooms[0].id = "r1";
    rooms[0].display_name = "Οικογένεια";
    rooms[1].id = "r2";
    rooms[1].display_name = "Εργαστήριο";
    const auto* family = aura::assistant_message_room(rooms, "οικογενεια");
    expect(family && family->id == "r1", "a named room is found whatever its accents and case");
    expect(aura::assistant_message_room(rooms, "") == nullptr &&
               aura::assistant_message_room(rooms, "Γιαγιά") == nullptr,
           "with several rooms an unnamed or unknown room is no room");
    rooms.pop_back();
    const auto* only = aura::assistant_message_room(rooms, "");
    expect(only && only->id == "r1", "the only room needs no name");

    // After a spoken answer the device listens again (continuous
    // conversation), once the voice has finished.
    int listened_again = 0;
    turn.set_listen_again([&listened_again] { ++listened_again; });
    turn.set_follow_up(true);
    speech.tts = true;
    controller.dispatch({aura::EventType::Reset});
    provider.reply = {true, "Είναι 10:00.", {}};
    const int played_before = audio.played;
    turn.ask("Τι ώρα είναι;");
    settle(turn);
    expect(audio.played == played_before + 1 && turn.speaking(), "the spoken answer plays and the turn is speaking");
    audio.state = aura::AudioState::Playing;
    settle(turn);
    expect(turn.speaking() && listened_again == 0, "it does not listen while the answer plays");
    audio.state = aura::AudioState::Ready;
    settle(turn);
    expect(!turn.speaking() && listened_again == 1, "it listens again once the answer has been spoken");
    settle(turn);
    expect(listened_again == 1, "and only once");
    // Not with the setting off, nor while an action waits for confirmation.
    turn.set_follow_up(false);
    controller.dispatch({aura::EventType::Reset});
    turn.ask("Τι ώρα είναι;");
    settle(turn);
    audio.state = aura::AudioState::Playing;
    settle(turn);
    audio.state = aura::AudioState::Ready;
    settle(turn);
    expect(listened_again == 1, "continuous conversation off: no listening again");
    turn.set_follow_up(true);
    controller.dispatch({aura::EventType::Reset});
    provider.reply = {true, "Βάζω χρονόμετρο.\nACTION: {\"type\":\"timer\",\"seconds\":60}", {}};
    turn.ask("Χρονόμετρο ένα λεπτό");
    settle(turn);
    audio.state = aura::AudioState::Playing;
    settle(turn);
    audio.state = aura::AudioState::Ready;
    settle(turn);
    expect(listened_again == 1 && controller.snapshot().state == AssistantState::ActionConfirmation,
           "an action waiting for confirmation is not talked over");
    // A voice that never starts does not leave the turn speaking for ever.
    controller.dispatch({aura::EventType::Reset});
    provider.reply = {true, "Είναι 10:00.", {}};
    turn.ask("Τι ώρα είναι;");
    settle(turn);
    turn.cancel();
    expect(!turn.speaking(), "cancelling ends the spoken answer");

    // A confirmed action is carried out by the shared executor.
    aura::ClockServicesModel clock;
    aura::NotesModel notes;
    aura::OrganizerModel organizer;
    aura::DisplayPowerModel display;
    aura::SoundSettings sound;
    FakeAudio speaker;
    speaker.volume = 0.5F;
    speaker.muted = true;
    const auto targets = [&] {
        return aura::AssistantActionTargets{clock, notes, organizer, display, speaker, sound, 600};
    };
    expect(aura::execute_assistant_action(proposed[0], targets()) && clock.snapshot().timers.size() == 1 &&
               clock.snapshot().timers[0].duration_s == 600,
           "a confirmed timer starts");
    aura::AssistantAction action;
    action.type = aura::AssistantActionType::Shopping;
    action.text = "γάλα";
    expect(aura::execute_assistant_action(action, targets()) && organizer.snapshot().shopping.size() == 1,
           "a confirmed shopping item is added");
    action = {};
    action.type = aura::AssistantActionType::Note;
    action.text = "κλειδιά στο συρτάρι";
    expect(aura::execute_assistant_action(action, targets()) && notes.snapshot().notes.size() == 1,
           "a confirmed note is kept");
    action = {};
    action.type = aura::AssistantActionType::Volume;
    action.relative = true;
    action.value = 20;
    expect(aura::execute_assistant_action(action, targets()) && speaker.volume > 0.69F && speaker.volume < 0.71F &&
               !speaker.muted && !sound.muted,
           "louder steps up from the volume heard now and ends a mute");
    action = {};
    action.type = aura::AssistantActionType::Brightness;
    action.value = 2;
    expect(!aura::execute_assistant_action(action, targets()), "a brightness below 5% is refused");
    action.value = 40;
    expect(aura::execute_assistant_action(action, targets()) &&
               display.snapshot().settings.active_brightness_percent == 40,
           "a confirmed brightness is set");
    action = {};
    action.type = aura::AssistantActionType::Message;
    expect(!aura::execute_assistant_action(action, targets()), "a message is not the executor's");
    std::cout << "voice_turn_test: PASS\n";
    return 0;
}
