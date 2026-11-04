#include "aura/aoraton_realtime.hpp"
#include "aura/json_reader.hpp"
#include "aura/text_util.hpp"

#include <algorithm>

namespace aura {
namespace {
std::string topic(const std::string& room) { return "realtime:messages:" + room; }
}  // namespace

bool AoratonRealtimeProtocol::valid_room(std::string_view room) {
    if (room.empty() || room.size() > 256) return false;
    for (const unsigned char c : room) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' ||
              c == '-'))
            return false;
    }
    return true;
}

AoratonRealtimeProtocol::AoratonRealtimeProtocol(std::vector<std::string> rooms) {
    for (auto& room : rooms) {
        if (!valid_room(room) || rooms_.size() == kMaxRooms) continue;
        if (std::find(rooms_.begin(), rooms_.end(), room) == rooms_.end()) rooms_.push_back(std::move(room));
    }
    joined_.resize(rooms_.size(), false);
}

std::vector<std::string> AoratonRealtimeProtocol::joins(const std::string& access_token) const {
    std::vector<std::string> frames;
    if (access_token.empty() || access_token.size() > 16384) return frames;
    for (std::size_t i = 0; i < rooms_.size(); ++i) {
        const auto ref = std::to_string(i + 1);
        frames.push_back("{\"topic\":\"" + json_escape(topic(rooms_[i])) + "\",\"event\":\"phx_join\",\"ref\":\"" +
                         ref + "\",\"join_ref\":\"" + ref +
                         "\",\"payload\":{\"config\":{\"broadcast\":{\"ack\":false,\"self\":false},"
                         "\"presence\":{\"enabled\":false},\"private\":false,\"postgres_changes\":[{"
                         "\"event\":\"INSERT\",\"schema\":\"public\",\"table\":\"messages\",\"filter\":\"room_key=eq." +
                         json_escape(rooms_[i]) + "\"}]},\"access_token\":\"" + json_escape(access_token) + "\"}}");
    }
    return frames;
}

std::string AoratonRealtimeProtocol::heartbeat(const std::string& ref) const {
    return "{\"topic\":\"phoenix\",\"event\":\"heartbeat\",\"ref\":\"" + json_escape(ref) + "\",\"payload\":{}}";
}

bool AoratonRealtimeProtocol::receive(std::string_view frame) {
    JsonValue root;
    JsonLimits limits;
    limits.max_bytes = kMaxFrameBytes;
    limits.max_depth = 10;
    limits.max_nodes = 4096;
    if (!parse_json(frame, root, limits) || root.type() != JsonType::Object) return false;
    const auto event = root.string_at("event");
    const auto channel = root.string_at("topic");
    const auto* payload = root.find("payload");
    if (!payload || payload->type() != JsonType::Object) return false;
    if (channel == "phoenix") {
        if (event == "phx_reply" && payload->string_at("status") == "ok") heartbeat_reply_ = root.string_at("ref");
        return true;
    }
    std::size_t index = 0;
    for (; index < rooms_.size(); ++index)
        if (channel == topic(rooms_[index])) break;
    if (index == rooms_.size()) return true;  // an unrequested topic cannot invalidate data
    if (event == "phx_error" || event == "phx_close") return false;
    if (event == "phx_reply") {
        if (root.string_at("ref") != std::to_string(index + 1)) return true;
        if (payload->string_at("status") != "ok") return false;
        const auto* response = payload->find("response");
        const auto* changes = response ? response->find("postgres_changes") : nullptr;
        if (changes) {
            for (const auto& sub : changes->items()) {
                if (sub.string_at("schema") == "public" && sub.string_at("table") == "messages" &&
                    sub.string_at("event") == "INSERT" && sub.string_at("filter") == "room_key=eq." + rooms_[index]) {
                    joined_[index] = true;
                }
            }
        }
    } else if (event == "system" && payload->string_at("extension") == "postgres_changes") {
        if (payload->string_at("status") != "ok") return false;
        joined_[index] = true;
    } else if (event == "postgres_changes" && joined_[index]) {
        const auto* data = payload->find("data");
        const auto* record = data ? data->find("record") : nullptr;
        if (data && record && data->string_at("schema") == "public" && data->string_at("table") == "messages" &&
            data->string_at("type") == "INSERT" && record->string_at("room_key") == rooms_[index])
            refresh_ = true;
    }
    return true;
}

bool AoratonRealtimeProtocol::active() const {
    return !joined_.empty() && std::all_of(joined_.begin(), joined_.end(), [](bool joined) { return joined; });
}
bool AoratonRealtimeProtocol::take_refresh() {
    const bool value = refresh_;
    refresh_ = false;
    return value;
}
}  // namespace aura
