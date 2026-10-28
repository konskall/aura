#include "aura/local_codex_provider.hpp"

#include "aura/assistant_stream.hpp"
#include "aura/bridge_http.hpp"
#include "aura/text_util.hpp"

#include <chrono>
#include <thread>

namespace aura::simulator {
namespace {

// Every streamed delta repeats a short JSON wrapper and the final line repeats
// the whole answer, so the wire is several times the (bounded) text.
constexpr std::size_t kMaxStreamBytes = 1024 * 1024;

// The bridge validates every field again; the device context and history are
// data for the model, never instructions (bridge/src/turn-prompt.mjs).
std::string turn_payload(const std::string& id, const TurnRequest& request) {
    std::string payload = "{\"id\":\"" + json_escape(id) + "\",\"prompt\":\"" + json_escape(request.prompt) + "\"";
    if (request.mode == TurnMode::Translate) {
        payload += ",\"mode\":\"translate\",\"source\":\"" + json_escape(request.source_language) +
                   "\",\"target\":\"" + json_escape(request.target_language) + "\"";
    } else {
        payload += ",\"mode\":\"chat\",\"language\":\"" +
                   std::string(interface_language_code(request.interface_language)) + "\"";
        if (request.web_search) payload += ",\"web_search\":true";
        if (!request.context.empty()) payload += ",\"context\":\"" + json_escape(request.context) + "\"";
        if (!request.history.empty()) {
            payload += ",\"history\":[";
            for (std::size_t index = 0; index < request.history.size(); ++index) {
                if (index > 0) payload += ',';
                payload += "{\"q\":\"" + json_escape(request.history[index].question) + "\",\"a\":\"" +
                           json_escape(request.history[index].answer) + "\"}";
            }
            payload += ']';
        }
    }
    payload += '}';
    return payload;
}

}  // namespace

LocalCodexProvider::LocalCodexProvider(std::string token, std::uint16_t port, std::string provider_name)
    : token_(std::move(token)), port_(port), provider_name_(std::move(provider_name)) {}

std::future<AssistantReply> LocalCodexProvider::start_turn(TurnRequest request) {
    const auto generation = ++*generation_;
    const auto id = "sim-" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    active_id_ = id;
    // The task owns copies of everything it needs; it never touches `this`.
    return std::async(std::launch::async, [current = generation_, generation, id, token = token_,
                                           port = port_, request = std::move(request)] {
        AssistantStreamDecoder decoder(request.stream.get());
        const auto response = bridge_stream_request(
            L"POST", port, "/v1/assistant/stream", token, turn_payload(id, request), "application/json", 50000,
            kMaxStreamBytes,
            [&](std::string_view chunk) {
                if (current->load() != generation) return false;
                return decoder.push(chunk);
            });
        const bool decoded = decoder.finish();
        if (current->load() != generation) return AssistantReply{false, {}, "CANCELLED"};
        if (response.status == 200) {
            if (decoded && !decoder.final_text().empty()) return AssistantReply{true, decoder.final_text(), {}};
            if (decoded && !decoder.error_code().empty()) return AssistantReply{false, {}, decoder.error_code()};
            return AssistantReply{false, {}, "INVALID_RESPONSE"};
        }
        auto code = json_string(response.body, "code");
        if (code.empty()) code = response.status == 0 ? "BRIDGE_UNAVAILABLE" : "PROVIDER_ERROR";
        return AssistantReply{false, {}, std::move(code)};
    });
}

void LocalCodexProvider::cancel() {
    ++*generation_;
    if (!active_id_.empty()) {
        // Fire-and-forget: a discarded std::async future would block the UI
        // thread until the cancel request completed.
        std::thread([id = active_id_, token = token_, port = port_] {
            bridge_request(L"POST", port, "/v1/assistant/cancel", token, "{\"id\":\"" + json_escape(id) + "\"}",
                           "application/json", 3000, 4096);
        }).detach();
    }
}

}  // namespace aura::simulator
