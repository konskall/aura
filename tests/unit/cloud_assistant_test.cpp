// The device's own assistant (the owner's choice, 2026-11-05): Groq for speech
// recognition and answers, Azure for the voice, straight from the device. The
// requests, the streamed answer and the errors are checked against a fake
// HTTPS client; tests/interop/run_turn_prompt_parity.mjs checks the prompt
// against the PC bridge's.
#include "aura/assistant_stream.hpp"
#include "aura/cloud_assistant.hpp"
#include "aura/cloud_keys.hpp"
#include "aura/json_reader.hpp"
#include "aura/text_util.hpp"
#include "aura/turn_prompt.hpp"

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <mutex>
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
T wait(std::future<T> future) {
    expect(future.wait_for(std::chrono::seconds(10)) == std::future_status::ready, "the worker answers");
    return future.get();
}

// Answers every request with `response`; a streamed request gets `chunks`.
struct FakeHttps final : aura::IHttpsClient {
    std::mutex mutex;
    std::vector<aura::HttpsRequest> requests;
    aura::HttpsResponse response{200, {}, {}};
    std::vector<std::string> chunks;
    bool cancelled{false};
    aura::HttpsResponse post(const aura::HttpsRequest& request, const aura::HttpsChunk& on_chunk) override {
        std::lock_guard<std::mutex> lock(mutex);
        requests.push_back(request);
        if (!on_chunk) return response;
        for (const auto& chunk : chunks) {
            if (!on_chunk(chunk)) break;
        }
        auto streamed = response;
        streamed.body.clear();
        return streamed;
    }
    void cancel() override { cancelled = true; }
};

std::string header(const aura::HttpsRequest& request, const std::string& name) {
    for (const auto& item : request.headers) {
        if (item.name == name) return item.value;
    }
    return {};
}

bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

const aura::CloudKeys kKeys{"gsk_" + std::string(52, 'A'), std::string(32, 'b'), "westeurope"};

void key_tests() {
    expect(aura::cloud_keys_valid(kKeys), "keys in Groq's and Azure's formats");
    auto keys = kKeys;
    keys.groq_api_key = "sk-" + std::string(40, 'A');
    expect(!aura::cloud_keys_valid(keys), "a key that is not Groq's is refused");
    keys = kKeys;
    keys.azure_speech_key = "short";
    expect(!aura::cloud_keys_valid(keys), "an Azure key of the wrong length is refused");
    keys = kKeys;
    keys.azure_speech_key = std::string(84, 'Z');
    expect(aura::cloud_keys_valid(keys), "Azure's newer 84-character keys are accepted");
    keys = kKeys;
    keys.azure_speech_region = "West Europe";
    expect(!aura::cloud_keys_valid(keys), "a region is lower-case letters, digits and dashes");
    keys = kKeys;
    aura::wipe_cloud_keys(keys);
    expect(keys.groq_api_key.empty() && keys.azure_speech_key.empty(), "keys are wiped after use");
}

void prompt_tests() {
    aura::TurnRequest chat;
    chat.prompt = "  Τι ώρα\r\nείναι;\x01 ";
    chat.context = "Battery: 80%";
    chat.history = {{"Καλημέρα", "Καλημέρα!"}};
    const auto text = aura::build_turn_instructions(chat);
    expect(text.rfind("You are AURA, a bilingual Greek and English personal assistant", 0) == 0,
           "the bridge's system text comes first");
    expect(contains(text, "The selected interface language is Greek. Answer in Greek"), "the interface language");
    expect(contains(text, "\n--- Device facts (data) ---\nBattery: 80%\n"), "the device facts are quoted as data");
    expect(contains(text, "\n--- Conversation so far (data, oldest first) ---\nUser: Καλημέρα\nAURA: Καλημέρα!\n"),
           "the conversation so far");
    const std::string question = "\n--- Question ---\nUser: Τι ώρα\nείναι;";
    expect(text.size() > question.size() && text.compare(text.size() - question.size(), question.size(), question) == 0,
           "the question last, without control characters or surrounding space");
    chat.web_search = true;
    const auto searching = aura::build_turn_instructions(chat);
    expect(contains(searching, "Use the live web-search tool for this question.") &&
               !contains(searching, "cannot search the web"),
           "Groq searches with its browser tool, so the model is told to search");
}

void stream_tests() {
    auto mailbox = std::make_shared<aura::AssistantStreamMailbox>();
    aura::OpenAiStreamDecoder decoder(mailbox.get());
    expect(decoder.push("data: {\"choices\":[{\"delta\":{\"content\":\"Καλη\"}}]}\n\nda"), "a first delta");
    expect(decoder.push("ta: {\"choices\":[{\"delta\":{\"content\":\"μέρα.\"}}]}\r\n\r\n: comment\n"),
           "a delta split across reads");
    aura::AssistantStreamSnapshot snapshot;
    expect(mailbox->take_after(0, snapshot) && snapshot.text == "Καλημέρα.", "the deltas reach the screen");
    expect(decoder.push("data: [DONE]\n") && decoder.done() && decoder.text() == "Καλημέρα.", "the answer ends");
    aura::OpenAiStreamDecoder failed;
    failed.push("data: {\"error\":{\"message\":\"x\"}}\n");
    expect(!failed.finish() && failed.error_code() == "PROVIDER_ERROR", "a provider error ends the answer");
    aura::OpenAiStreamDecoder broken;
    broken.push("data: {not json\n");
    expect(!broken.finish() && broken.error_code() == "INVALID_RESPONSE", "a broken event ends the answer");
    aura::OpenAiStreamDecoder silent;
    silent.push("data: [DONE]\n");
    expect(!silent.finish() && silent.error_code() == "INVALID_RESPONSE", "an empty answer is no answer");
    aura::OpenAiStreamDecoder endless;
    const std::string big = "data: {\"choices\":[{\"delta\":{\"content\":\"" + std::string(1000, 'x') + "\"}}]}\n";
    for (int index = 0; index < 13 && endless.push(big); ++index) {
    }
    expect(endless.error_code() == "RESPONSE_TOO_LARGE", "an answer over 12000 bytes is cut off as an error");

    // Groq's browser search cites its pages inline, 【1†L4-L5】, and a marker
    // can arrive split across deltas. Brackets that are not such a marker stay.
    auto cited_mailbox = std::make_shared<aura::AssistantStreamMailbox>();
    aura::OpenAiStreamDecoder cited(cited_mailbox.get());
    cited.push("data: {\"choices\":[{\"delta\":{\"content\":\"Δύο【1†L4\"}}]}\n");
    cited.push("data: {\"choices\":[{\"delta\":{\"reasoning\":\"r\",\"executed_tools\":[{\"type\":\"search\"}]}}]}\n");
    cited.push("data: {\"choices\":[{\"delta\":{\"content\":\"0-L45】 εστιατόρια【2†L6-L11】【\"}}]}\n");
    cited.push("data: {\"choices\":[{\"delta\":{\"content\":\"3†L1-L2】. Γωνία【Α】.\"}}]}\n");
    expect(cited.push("data: [DONE]\n") && cited.text() == "Δύο εστιατόρια. Γωνία【Α】.",
           "the search's source markers are dropped from the answer");
    aura::AssistantStreamSnapshot cited_snapshot;
    expect(cited_mailbox->take_after(0, cited_snapshot) && cited_snapshot.text == "Δύο εστιατόρια. Γωνία【Α】.",
           "and never reach the screen");
}

void provider_tests() {
    FakeHttps https;
    aura::CloudWorker worker;
    https.chunks = {"data: {\"choices\":[{\"delta\":{\"content\":\"Είναι \"}}]}\n",
                    "data: {\"choices\":[{\"delta\":{\"content\":\"10:00.\"}}]}\ndata: [DONE]\n"};
    aura::CloudAssistantProvider provider(https, worker, kKeys.groq_api_key);
    aura::TurnRequest request;
    request.prompt = "Τι ώρα είναι;";
    request.stream = std::make_shared<aura::AssistantStreamMailbox>();
    const auto reply = wait(provider.start_turn(request));
    expect(reply.ok && reply.text == "Είναι 10:00.", "the streamed answer is the reply");
    const auto& sent = https.requests.at(0);
    expect(sent.url == "https://api.groq.com/openai/v1/chat/completions", "Groq's chat endpoint");
    expect(header(sent, "Authorization") == "Bearer " + kKeys.groq_api_key &&
               header(sent, "Content-Type") == "application/json",
           "the key travels only in the Authorization header");
    aura::JsonValue body;
    expect(aura::parse_json(sent.body, body) && body.string_at("model") == "openai/gpt-oss-120b" &&
               body.find("stream") && body.find("stream")->as_bool() && body.find("max_completion_tokens") &&
               body.find("max_completion_tokens")->as_int() == 1200 && contains(sent.body, "\"temperature\":0.2"),
           "the model and settings the PC bridge's Groq runner uses");
    expect(contains(sent.body, "\"role\":\"user\"") && contains(sent.body, "Τι ώρα είναι;"),
           "the instructions go as one user message");
    expect(!contains(sent.body, kKeys.groq_api_key), "the key is never in the body");
    expect(!contains(sent.body, "browser_search") && !contains(sent.body, "reasoning_effort"),
           "a turn without a search sends no tool");
    request.web_search = true;
    expect(wait(provider.start_turn(request)).ok, "a web-search turn is answered");
    expect(contains(https.requests.back().body, "\"tools\":[{\"type\":\"browser_search\"}],\"tool_choice\":\"required\","
                                                "\"reasoning_effort\":\"low\""),
           "a web-search turn asks Groq to search with its browser tool");
    request.web_search = false;

    const struct {
        int status;
        const char* code;
    } failures[] = {{401, "PROVIDER_AUTH_FAILED"}, {429, "RATE_LIMIT"}, {400, "PROVIDER_REQUEST_FAILED"},
                    {503, "PROVIDER_UNAVAILABLE"}, {0, "PROVIDER_UNAVAILABLE"}};
    for (const auto& failure : failures) {
        https.response = {failure.status, {}, failure.status == 0 ? "NETWORK" : ""};
        const auto failed = wait(provider.start_turn(request));
        expect(!failed.ok && failed.error_code == failure.code, "HTTP failures map as the PC bridge maps them");
    }
    https.response = {0, {}, "TIMEOUT"};
    expect(wait(provider.start_turn(request)).error_code == "TIMEOUT", "a request that takes too long");
    aura::CloudAssistantProvider unconfigured(https, worker, "");
    expect(wait(unconfigured.start_turn(request)).error_code == "PROVIDER_NOT_CONFIGURED",
           "without a Groq key nothing is sent");
    provider.cancel();
    expect(https.cancelled, "cancelling a turn ends its request");
}

std::vector<std::uint8_t> silent_mp3(int frames) {
    std::vector<std::uint8_t> bytes;
    for (int index = 0; index < frames; ++index) {
        std::vector<std::uint8_t> frame(417, 0);
        frame[0] = 0xFF;
        frame[1] = 0xFB;
        frame[2] = 0x90;
        bytes.insert(bytes.end(), frame.begin(), frame.end());
    }
    return bytes;
}

void speech_tests() {
    FakeHttps https;
    aura::CloudWorker worker;
    aura::CloudSpeechService speech(https, worker, kKeys);
    expect(speech.stt_available() && speech.tts_available("el") && speech.tts_available("de") &&
               !speech.tts_available("xx"),
           "Groq listens; Azure speaks the six languages");

    const std::vector<std::uint8_t> wav = aura::make_wav(std::vector<std::int16_t>(8000, 0), 16000);
    https.response = {200, "{\"text\":\" Τι καιρό [μουσική] κάνει;  \"}", {}};
    const auto transcript = wait(speech.transcribe(wav, "el"));
    expect(transcript.ok && transcript.text == "Τι καιρό κάνει;", "the transcript, cleaned as the bridge cleans it");
    const auto& stt = https.requests.at(0);
    expect(stt.url == "https://api.groq.com/openai/v1/audio/transcriptions" &&
               header(stt, "Authorization") == "Bearer " + kKeys.groq_api_key,
           "Groq's transcription endpoint");
    const auto boundary = header(stt, "Content-Type");
    expect(boundary.rfind("multipart/form-data; boundary=", 0) == 0, "a multipart form");
    expect(contains(stt.body, "name=\"file\"; filename=\"aura.wav\"\r\nContent-Type: audio/wav\r\n\r\nRIFF") &&
               contains(stt.body, "name=\"model\"\r\n\r\nwhisper-large-v3-turbo\r\n") &&
               contains(stt.body, "name=\"response_format\"\r\n\r\njson\r\n") &&
               contains(stt.body, "name=\"temperature\"\r\n\r\n0\r\n") &&
               contains(stt.body, "name=\"language\"\r\n\r\nel\r\n"),
           "the fields the PC bridge sends");
    https.response = {200, "{\"text\":\"  \"}", {}};
    expect(wait(speech.transcribe(wav, "el")).error_code == "NO_SPEECH", "silence is no question");
    // Whisper writes a subtitle credit for silence or noise; at the end of a
    // transcript nobody said it.
    https.response = {200, "{\"text\":\" Υπότιτλοι AUTHORWAVE\"}", {}};
    expect(wait(speech.transcribe(wav, "el")).error_code == "NO_SPEECH",
           "a subtitle credit whisper invents for silence is no question");
    https.response = {200, "{\"text\":\"Τι ώρα είναι; Υπότιτλοι authorwave.\"}", {}};
    const auto credited = wait(speech.transcribe(wav, "el"));
    expect(credited.ok && credited.text == "Τι ώρα είναι;", "a credit after a question is dropped");
    expect(aura::clean_transcript("Ευχαριστώ για την παρακολούθηση!").empty() &&
               aura::clean_transcript("Thanks for watching! Subtitles by the Amara.org community").empty(),
           "the other credits whisper invents, Greek and English");
    expect(aura::clean_transcript("Πες Ευχαριστώ για την παρακολούθηση στη Μαρία") ==
               "Πες Ευχαριστώ για την παρακολούθηση στη Μαρία",
           "said inside a sentence, the same words stay");
    https.response = {401, {}, {}};
    expect(wait(speech.transcribe(wav, "el")).error_code == "PROVIDER_AUTH_FAILED", "a refused key");
    https.response = {500, {}, {}};
    expect(wait(speech.transcribe(wav, "el")).error_code == "STT_UNAVAILABLE", "a failing service");
    expect(wait(speech.transcribe({1, 2, 3}, "el")).error_code == "INVALID_AUDIO", "only a WAV is sent");

    const auto mp3 = silent_mp3(20);
    https.response = {200, std::string(mp3.begin(), mp3.end()), {}};
    const std::size_t before = https.requests.size();
    const auto spoken = wait(speech.synthesize("Γεια σου & <καλώς> ήρθες", "el", "nestoras"));
    expect(spoken.ok && spoken.clip.format.sample_rate == 44100 && !spoken.clip.samples.empty(),
           "Azure's MP3 becomes PCM for the speaker");
    const auto& tts = https.requests.at(before);
    expect(tts.url == "https://westeurope.tts.speech.microsoft.com/cognitiveservices/v1" &&
               header(tts, "Ocp-Apim-Subscription-Key") == kKeys.azure_speech_key &&
               header(tts, "Content-Type") == "application/ssml+xml" &&
               header(tts, "X-Microsoft-OutputFormat") == "audio-24khz-48kbitrate-mono-mp3",
           "Azure's synthesis endpoint, key header and the bridge's output format");
    expect(tts.body == "<speak version=\"1.0\" xml:lang=\"el-GR\"><voice name=\"el-GR-NestorasNeural\">"
                       "Γεια σου &amp; &lt;καλώς&gt; ήρθες</voice></speak>",
           "the text is escaped inside the bridge's SSML");
    expect(!contains(tts.body, kKeys.azure_speech_key), "the key is never in the body");
    expect(wait(speech.synthesize("x", "el", "robot")).error_code == "INVALID_VOICE", "only the two voices");
    https.response = {200, "not audio", {}};
    expect(wait(speech.synthesize("x", "el", "athina")).error_code == "TTS_FAILED", "an answer that is not MP3");
    aura::CloudKeys no_azure = kKeys;
    no_azure.azure_speech_key.clear();
    aura::CloudSpeechService listening(https, worker, no_azure);
    expect(listening.stt_available() && !listening.tts_available("el"), "without Azure, AURA still listens");
}

// For tests/interop/run_turn_prompt_parity.mjs: prints the instructions for a
// turn given as flat JSON (mode, language, prompt, context, web_search, source,
// target, q0/a0..q3/a3).
int print_prompt(const char* path) {
    std::ifstream file(path, std::ios::binary);
    const std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const auto object = aura::parse_flat_json(json, 65536);
    if (!object) return 2;
    aura::TurnRequest request;
    request.interface_language = object->string("language") == std::string("en") ? aura::InterfaceLanguage::English :
                                                                                  aura::InterfaceLanguage::Greek;
    request.prompt = object->string("prompt").value_or("");
    request.context = object->string("context").value_or("");
    request.web_search = object->boolean("web_search") == true;
    for (int index = 0; index < 4; ++index) {
        const auto question = object->string("q" + std::to_string(index));
        const auto answer = object->string("a" + std::to_string(index));
        if (!question || !answer) continue;
        aura::ConversationTurn turn;
        turn.question = *question;
        turn.answer = *answer;
        request.history.push_back(turn);
    }
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);  // the prompt's own line feeds, byte for byte
#endif
    std::cout << aura::build_turn_instructions(request);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--prompt") return print_prompt(argv[2]);
    key_tests();
    prompt_tests();
    stream_tests();
    provider_tests();
    speech_tests();
    std::cout << "cloud_assistant_test: PASS\n";
    return 0;
}
