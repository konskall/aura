#include "aura/aoraton.hpp"
#include "aura/voice_container.hpp"
#include "aura/text_util.hpp"

#include <algorithm>

namespace aura {
namespace {
constexpr std::size_t kMaxMessageBytes = 512;
constexpr std::size_t kMaxMessages = 64;

std::string bounded(std::string value, std::size_t maximum) {
    if (value.size() > maximum) value.resize(maximum);
    return value;
}
}

MockAoratonService::MockAoratonService() { reset_mock(); }

bool aoraton_sync_settings_valid(const AoratonSyncSettings& settings) noexcept {
    return settings.interval_seconds == 10 || settings.interval_seconds == 30 ||
           settings.interval_seconds == 60 || settings.interval_seconds == 300;
}
std::string serialize_aoraton_sync_settings(const AoratonSyncSettings& settings) {
    if (!aoraton_sync_settings_valid(settings)) return {};
    return std::string("{\"version\":1,\"enabled\":") + (settings.polling_enabled ? "true" : "false") +
           ",\"seconds\":" + std::to_string(settings.interval_seconds) + "}\n";
}
bool parse_aoraton_sync_settings(std::string_view text, AoratonSyncSettings& settings) {
    const auto parsed = parse_flat_json(text, 128);
    if (!parsed || parsed->size() != 3 || parsed->integer("version") != 1) return false;
    const auto enabled = parsed->boolean("enabled");
    const auto seconds = parsed->integer("seconds");
    if (!enabled || !seconds || *seconds < 0 || *seconds > 300) return false;
    AoratonSyncSettings candidate{*enabled, static_cast<std::uint32_t>(*seconds)};
    if (!aoraton_sync_settings_valid(candidate)) return false;
    settings = candidate;
    return true;
}
bool MockAoratonService::set_sync_settings(const AoratonSyncSettings& settings) {
    if (!aoraton_sync_settings_valid(settings)) return false;
    if (settings == snapshot_.sync_settings) return true;
    snapshot_.sync_settings = settings;
    touch();
    return true;
}

void MockAoratonService::touch() { ++snapshot_.sequence; }

AoratonRoom* MockAoratonService::find_room(const std::string& room_id) {
    const auto it = std::find_if(snapshot_.rooms.begin(), snapshot_.rooms.end(),
                                 [&](const AoratonRoom& room) { return room.id == room_id; });
    return it == snapshot_.rooms.end() ? nullptr : &*it;
}

void MockAoratonService::update_preview(AoratonRoom& room, const std::string& text) {
    room.preview = bounded(text, 64);
}

bool MockAoratonService::connect() {
    if (!network_available_) {
        snapshot_.connection = AoratonConnectionState::Disconnected;
        snapshot_.last_error = "NETWORK_UNAVAILABLE";
        touch();
        return false;
    }
    snapshot_.connection = AoratonConnectionState::Connected;
    snapshot_.last_error.clear();
    reconnect_elapsed_ms_ = 0;
    touch();
    return true;
}

void MockAoratonService::disconnect() {
    snapshot_.connection = AoratonConnectionState::Disconnected;
    snapshot_.last_error.clear();
    touch();
}

bool MockAoratonService::select_room(const std::string& room_id) {
    auto* room = find_room(room_id);
    if (!room) {
        snapshot_.last_error = "ROOM_NOT_FOUND";
        touch();
        return false;
    }
    snapshot_.selected_room_id = room_id;
    room->unread = 0;
    snapshot_.last_error.clear();
    touch();
    return true;
}

bool MockAoratonService::inject_incoming(const std::string& room_id, const std::string& fictional_text) {
    auto* room = find_room(room_id);
    if (!room || fictional_text.empty() || fictional_text.size() > kMaxMessageBytes) {
        snapshot_.last_error = room ? "MESSAGE_REJECTED" : "ROOM_NOT_FOUND";
        touch();
        return false;
    }
    AoratonMessage message;
    message.id = "mock-in-" + std::to_string(next_id_++);
    message.room_id = room_id;
    message.author = language_ == InterfaceLanguage::English ? "Elena • DEMO" : "Ελένη • DEMO";
    message.local_display_text = fictional_text;
    message.received_at = language_ == InterfaceLanguage::English ? "now" : "τώρα";
    message.direction = AoratonDirection::Incoming;
    message.delivery = AoratonDelivery::Received;
    snapshot_.messages.push_back(std::move(message));
    if (snapshot_.messages.size() > kMaxMessages) snapshot_.messages.erase(snapshot_.messages.begin());
    update_preview(*room, fictional_text);
    if (snapshot_.selected_room_id != room_id) room->unread = std::min<std::uint32_t>(99, room->unread + 1);
    snapshot_.last_error.clear();
    touch();
    return true;
}

bool MockAoratonService::prepare_send(const std::string& room_id, const std::string& plaintext) {
    if (snapshot_.awaiting_confirmation || !find_room(room_id) || plaintext.empty() ||
        plaintext.size() > kMaxMessageBytes) {
        snapshot_.last_error = snapshot_.awaiting_confirmation ? "CONFIRMATION_ALREADY_PENDING" : "MESSAGE_REJECTED";
        touch();
        return false;
    }
    snapshot_.pending_room_id = room_id;
    snapshot_.pending_text = plaintext;
    snapshot_.awaiting_confirmation = true;
    snapshot_.last_error.clear();
    touch();
    return true;
}

bool MockAoratonService::prepare_voice(const std::string& room_id, std::vector<std::uint8_t> wav) {
    VoiceTrack track;
    if (snapshot_.awaiting_confirmation || !find_room(room_id) || !voice_demux(wav.data(), wav.size(), track) ||
        track.codec != VoiceCodec::Pcm16 || track.duration_ms == 0 || track.duration_ms > 60000) {
        snapshot_.last_error = "VOICE_INVALID";
        touch();
        return false;
    }
    snapshot_.pending_room_id = room_id;
    snapshot_.pending_text.clear();
    snapshot_.pending_voice_ms = track.duration_ms;
    snapshot_.awaiting_confirmation = true;
    snapshot_.last_error.clear();
    touch();
    return true;
}

bool MockAoratonService::confirm_send() {
    if (!snapshot_.awaiting_confirmation) {
        snapshot_.last_error = "NO_PENDING_MESSAGE";
        touch();
        return false;
    }
    if (!network_available_ || snapshot_.connection != AoratonConnectionState::Connected) {
        snapshot_.last_error = "NETWORK_UNAVAILABLE";
        touch();
        return false;
    }
    auto* room = find_room(snapshot_.pending_room_id);
    if (!room) {
        snapshot_.last_error = "ROOM_NOT_FOUND";
        touch();
        return false;
    }
    AoratonMessage message;
    message.id = "mock-out-" + std::to_string(next_id_++);
    message.room_id = snapshot_.pending_room_id;
    message.author = language_ == InterfaceLanguage::English ? "You • DEMO" : "Εσύ • DEMO";
    message.local_display_text = snapshot_.pending_text;
    if (snapshot_.pending_voice_ms > 0) {
        message.attachment = AoratonAttachmentKind::Voice;
        message.attachment_name = "voice_DEMO.wav";
    }
    message.received_at = language_ == InterfaceLanguage::English ? "now" : "τώρα";
    message.direction = AoratonDirection::Outgoing;
    message.delivery = AoratonDelivery::Sent;
    snapshot_.messages.push_back(std::move(message));
    if (snapshot_.messages.size() > kMaxMessages) snapshot_.messages.erase(snapshot_.messages.begin());
    update_preview(*room, snapshot_.pending_text);
    snapshot_.selected_room_id = snapshot_.pending_room_id;
    snapshot_.pending_room_id.clear();
    snapshot_.pending_text.clear();
    snapshot_.pending_voice_ms = 0;
    snapshot_.awaiting_confirmation = false;
    snapshot_.last_error.clear();
    touch();
    return true;
}

bool MockAoratonService::cancel_send() {
    if (!snapshot_.awaiting_confirmation) return false;
    snapshot_.pending_room_id.clear();
    snapshot_.pending_text.clear();
    snapshot_.pending_voice_ms = 0;
    snapshot_.awaiting_confirmation = false;
    snapshot_.last_error.clear();
    touch();
    return true;
}

void MockAoratonService::set_network_available(bool available) {
    network_available_ = available;
    reconnect_elapsed_ms_ = 0;
    if (!available) {
        snapshot_.connection = AoratonConnectionState::Disconnected;
        snapshot_.last_error = "NETWORK_UNAVAILABLE";
    } else if (snapshot_.connection != AoratonConnectionState::Connected) {
        snapshot_.connection = AoratonConnectionState::Reconnecting;
        snapshot_.last_error.clear();
    }
    touch();
}

void MockAoratonService::reject_invalid_payload(const std::string& reason) {
    snapshot_.last_error = reason.empty() ? "INVALID_AUTHENTICATED_PAYLOAD" : bounded(reason, 64);
    touch();
}

void MockAoratonService::tick(std::uint32_t elapsed_ms) {
    if (!network_available_ || snapshot_.connection != AoratonConnectionState::Reconnecting) return;
    reconnect_elapsed_ms_ = std::min<std::uint32_t>(2000, reconnect_elapsed_ms_ + elapsed_ms);
    if (reconnect_elapsed_ms_ >= 600) {
        snapshot_.connection = AoratonConnectionState::Connected;
        snapshot_.last_error.clear();
        reconnect_elapsed_ms_ = 0;
        touch();
    }
}

namespace {
// A labelled stand-in picture: a soft diagonal gradient, never a real photo.
std::shared_ptr<const PhotoImage> demo_picture(unsigned width, unsigned height) {
    auto image = std::make_shared<PhotoImage>();
    image->width = width;
    image->height = height;
    if (!image->pixels.resize(std::size_t{width} * height)) return {};
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x) {
            const unsigned r = 40 + x * 120 / width, g = 70 + y * 90 / height, b = 150 + (x + y) * 80 / (width + height);
            image->pixels[std::size_t{y} * width + x] =
                static_cast<std::uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        }
    return image;
}

AoratonMessage demo_message(const char* id, const char* room, const char* at, AoratonDirection direction,
                            AoratonDelivery delivery) {
    AoratonMessage message;
    message.id = id;
    message.room_id = room;
    message.received_at = at;
    message.direction = direction;
    message.delivery = delivery;
    return message;
}
}  // namespace

void MockAoratonService::reset_mock() {
    // The sequence must keep increasing across a reset: the UI skips any
    // snapshot whose sequence it has already applied.
    const auto sequence = snapshot_.sequence;
    const auto sync_settings = snapshot_.sync_settings;
    snapshot_ = {};
    snapshot_.sync_settings = sync_settings;
    snapshot_.sequence = sequence;
    snapshot_.mock_mode = true;
    snapshot_.rooms = {
        {"demo-family", {}, {}, 1, true, true, true, false, "18:44"},
        {"demo-lab", {}, {}, 0, true, true, true, false, "17:10"},
    };
    snapshot_.messages = {
        demo_message("fixture-1", "demo-family", "18:42", AoratonDirection::Incoming, AoratonDelivery::Received),
        demo_message("fixture-2", "demo-family", "18:44", AoratonDirection::Outgoing, AoratonDelivery::Sent),
        demo_message("fixture-3", "demo-lab", "17:10", AoratonDirection::Incoming, AoratonDelivery::Received),
    };
    auto photo = demo_message("fixture-4", "demo-lab", "17:12", AoratonDirection::Incoming, AoratonDelivery::Received);
    photo.attachment = AoratonAttachmentKind::Image;
    photo.attachment_name = "DEMO prototype.jpg";
    photo.thumbnail = demo_picture(216, 144);
    snapshot_.messages.push_back(std::move(photo));
    auto voice = demo_message("fixture-5", "demo-lab", "17:14", AoratonDirection::Incoming, AoratonDelivery::Received);
    voice.attachment = AoratonAttachmentKind::Voice;
    voice.attachment_name = "voice_DEMO.wav";
    snapshot_.messages.push_back(std::move(voice));
    localize_mock_copy();
    network_available_ = true;
    reconnect_elapsed_ms_ = 0;
    next_id_ = 1;
    connect();
}

bool MockAoratonService::open_attachment(const std::string& message_id) {
    const auto found = std::find_if(snapshot_.messages.begin(), snapshot_.messages.end(),
                                    [&](const AoratonMessage& message) { return message.id == message_id; });
    if (found == snapshot_.messages.end() ||
        (found->attachment != AoratonAttachmentKind::Image && found->attachment != AoratonAttachmentKind::Voice))
        return false;
    snapshot_.opened_attachment_id = message_id;
    if (found->attachment == AoratonAttachmentKind::Voice) {
        // Two quiet seconds of a 440 Hz tone stand in for the DEMO voice.
        auto clip = std::make_shared<PcmClip>();
        clip->format.sample_rate = 16000;
        clip->format.channels = 1;
        clip->samples.resize(32000);
        for (std::size_t index = 0; index < clip->samples.size(); ++index)
            clip->samples[index] = static_cast<std::int16_t>((index * 440 / 16000) % 2 ? 900 : -900);
        snapshot_.voice_clip = std::move(clip);
        snapshot_.attachment_error.clear();
        snapshot_.opening_attachment = false;
        touch();
        return true;
    }
    snapshot_.opened_image = demo_picture(480, 320);
    snapshot_.attachment_error.clear();
    snapshot_.opening_attachment = false;
    touch();
    return true;
}

void MockAoratonService::close_attachment() {
    snapshot_.opened_attachment_id.clear();
    snapshot_.opened_image.reset();
    snapshot_.voice_clip.reset();
    snapshot_.attachment_error.clear();
    touch();
}

void MockAoratonService::set_interface_language(InterfaceLanguage language) {
    if (language_ == language) return;
    language_ = language;
    localize_mock_copy();
    touch();
}

void MockAoratonService::localize_mock_copy() {
    const bool english = language_ == InterfaceLanguage::English;
    for (auto& room : snapshot_.rooms) {
        if (room.id == "demo-family") room.display_name = english ? "Family • DEMO" : "Οικογένεια • DEMO";
        else if (room.id == "demo-lab") room.display_name = english ? "Workshop • DEMO" : "Εργαστήριο • DEMO";
    }
    for (auto& message : snapshot_.messages) {
        if (message.id == "fixture-1") {
            message.author = english ? "Elena • DEMO" : "Ελένη • DEMO";
            message.local_display_text = english ? "We will be there at 19:30." : "Θα είμαστε εκεί στις 19:30.";
        } else if (message.id == "fixture-2") {
            message.author = english ? "You • DEMO" : "Εσύ • DEMO";
            message.local_display_text = english ? "Great, see you then." : "Τέλεια, τα λέμε τότε.";
        } else if (message.id == "fixture-5") {
            message.author = english ? "Nikos • DEMO" : "Νίκος • DEMO";
        } else if (message.id == "fixture-4") {
            message.author = english ? "Nikos • DEMO" : "Νίκος • DEMO";
            message.local_display_text = english ? "DEMO · The prototype up close" : "DEMO · Το πρωτότυπο από κοντά";
        } else if (message.id == "fixture-3") {
            message.author = english ? "Nikos • DEMO" : "Νίκος • DEMO";
            message.local_display_text = english ? "The prototype is ready for review." :
                                                    "Το πρωτότυπο είναι έτοιμο για έλεγχο.";
        } else if (message.direction == AoratonDirection::Outgoing) {
            message.author = english ? "You • DEMO" : "Εσύ • DEMO";
        } else if (message.direction == AoratonDirection::Incoming) {
            message.author = english ? "Elena • DEMO" : "Ελένη • DEMO";
        }
        if (message.received_at == "now" || message.received_at == "τώρα") {
            message.received_at = english ? "now" : "τώρα";
        }
    }
    for (auto& room : snapshot_.rooms) {
        const auto message = std::find_if(snapshot_.messages.rbegin(), snapshot_.messages.rend(),
            [&](const AoratonMessage& item) { return item.room_id == room.id; });
        room.preview = message == snapshot_.messages.rend() ? std::string{} :
                       bounded(message->local_display_text, 64);
    }
}

std::size_t aoraton_characters_left(std::string_view text, std::size_t max_bytes) noexcept {
    if (text.size() >= max_bytes) return 0;
    const bool wide = std::any_of(text.begin(), text.end(), [](char ch) {
        return static_cast<unsigned char>(ch) >= 0x80;
    });
    return (max_bytes - text.size()) / (wide ? 2 : 1);
}

const char* aoraton_connection_name(AoratonConnectionState state) noexcept {
    switch (state) {
        case AoratonConnectionState::Disconnected: return "DISCONNECTED";
        case AoratonConnectionState::Connecting: return "CONNECTING";
        case AoratonConnectionState::Connected: return "CONNECTED";
        case AoratonConnectionState::Reconnecting: return "RECONNECTING";
        case AoratonConnectionState::Error: return "ERROR";
    }
    return "UNKNOWN";
}

}  // namespace aura
