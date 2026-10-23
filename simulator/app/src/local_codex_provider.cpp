#include "aura/local_codex_provider.hpp"

#include "aura/bridge_http.hpp"
#include "aura/text_util.hpp"

#include <chrono>
#include <thread>

namespace aura::simulator {
namespace {

// The bridge validates every field again; the device context and history are
// data for the model, never instructions (bridge/src/turn-prompt.mjs).
std::string turn_payload(const std::string& id, const TurnRequest& request) {
    std::string payload = "{\"id\":\"" + json_escape(id) + "\",\"prompt\":\"" + json_escape(request.prompt) + "\"";
    if (request.mode == TurnMode::Translate) {
        payload += ",\"mode\":\"translate\",\"source\":\"" + json_escape(request.source_language) +
                   "\",\"target\":\"" + json_escape(request.target_language) + "\"";
    } else {
        payload += ",\"mode\":\"chat\"";
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

LocalCodexProvider::LocalCodexProvider(std::string token, std::uint16_t port)
    : token_(std::move(token)), port_(port) {}

std::future<AssistantReply> LocalCodexProvider::start_turn(TurnRequest request) {
    const auto generation = ++*generation_;
    const auto id = "sim-" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    active_id_ = id;
    // The task owns copies of everything it needs; it never touches `this`.
    return std::async(std::launch::async, [current = generation_, generation, id, token = token_,
                                           port = port_, request = std::move(request)] {
        const auto response = bridge_request(L"POST", port, "/v1/assistant/turn", token, turn_payload(id, request),
                                             "application/json", 50000, 16384);
        if (current->load() != generation) return AssistantReply{false, {}, "CANCELLED"};
        if (response.status == 200) {
            auto text = json_string(response.body, "text");
            if (!text.empty()) return AssistantReply{true, std::move(text), {}};
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
