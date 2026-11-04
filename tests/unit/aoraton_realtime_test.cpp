#include "aura/aoraton_realtime.hpp"
#include "aura/json_reader.hpp"
#include <cstdlib>
#include <iostream>

namespace {
void expect(bool ok, const char* message) {
    if (!ok) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}
std::string status_frame(const char* room, const char* status = "ok") {
    return std::string("{\"topic\":\"realtime:messages:") + room +
           "\",\"event\":\"system\",\"payload\":{\"extension\":\"postgres_changes\",\"status\":\"" + status + "\"}}";
}
std::string insert(const char* topic, const char* room, const char* schema = "public") {
    return std::string("{\"topic\":\"realtime:messages:") + topic +
           "\",\"event\":\"postgres_changes\",\"payload\":{\"data\":{\"type\":\"INSERT\",\"schema\":\"" + schema +
           "\",\"table\":\"messages\",\"record\":{\"room_key\":\"" + room + "\"}}}}";
}
}  // namespace
int main() {
    aura::AoratonRealtimeProtocol protocol({"alpha", "beta", "alpha", "not,a-filter"});
    const auto joins = protocol.joins("DEMO-jwt");
    expect(joins.size() == 2, "valid unique room subscriptions only");
    aura::JsonValue join;
    expect(aura::parse_json(joins[0], join) && join.string_at("event") == "phx_join", "public Phoenix JSON protocol");
    expect(join.find("payload")->string_at("access_token") == "DEMO-jwt", "session JWT travels in the join payload");
    expect(!protocol.active(), "transport connection alone is never realtime");
    expect(protocol.receive(insert("alpha", "alpha")) && !protocol.take_refresh(),
           "unacknowledged subscription ignored");
    expect(protocol.receive(status_frame("alpha")) && !protocol.active(), "partial readiness is not all-room coverage");
    expect(protocol.receive(status_frame("beta")) && protocol.active(), "server acknowledgements establish readiness");
    expect(protocol.receive(insert("other", "alpha")) && !protocol.take_refresh(), "unrequested topic ignored");
    expect(protocol.receive(insert("alpha", "beta")) && !protocol.take_refresh(), "cross-room record ignored");
    expect(protocol.receive(insert("alpha", "alpha", "other")) && !protocol.take_refresh(), "other schemas ignored");
    expect(protocol.receive(insert("alpha", "alpha")) && protocol.receive(insert("beta", "beta")) &&
               protocol.take_refresh() && !protocol.take_refresh(),
           "burst coalesces into one bounded invalidation");
    expect(protocol.receive(
               "{\"topic\":\"phoenix\",\"event\":\"phx_reply\",\"ref\":\"h1\",\"payload\":{\"status\":\"ok\"}}") &&
               protocol.heartbeat_reply() == "h1",
           "heartbeat acknowledgement is correlated");
    expect(!protocol.receive(status_frame("alpha", "error")), "channel error forces reconnect/fallback");
    expect(!protocol.receive("not-json") && !protocol.receive(std::string(65537, 'x')),
           "malformed/oversized frames rejected");
    aura::AoratonRealtimeProtocol ack({"alpha"});
    expect(
        ack.receive(
            "{\"topic\":\"realtime:messages:alpha\",\"event\":\"phx_reply\",\"ref\":\"1\",\"payload\":{\"status\":\"ok\","
            "\"response\":{\"postgres_changes\":[{\"event\":\"INSERT\",\"schema\":\"public\",\"table\":\"messages\",\"filter\":\"room_key=eq.alpha\"}]}}}") &&
            ack.active(),
        "join replies acknowledge only matching server-side subscription filters");
    std::cout << "aoraton realtime protocol tests passed\n";
}
