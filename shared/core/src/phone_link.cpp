#include "aura/phone_link.hpp"

#include "aura/text_util.hpp"

#include <algorithm>
#include <array>
#include <utility>

namespace aura {

void PhoneNotificationModel::set_link(PhoneLinkState link, PhonePlatform platform, const std::string& device_name) {
    auto name = sanitize_text(device_name, 48);
    if (link == snapshot_.link && platform == snapshot_.platform && name == snapshot_.device_name) return;
    snapshot_.link = link;
    snapshot_.platform = platform;
    snapshot_.device_name = std::move(name);
    // Without a paired phone nothing it sent stays on the device.
    if (link == PhoneLinkState::Unavailable || link == PhoneLinkState::NotPaired) {
        snapshot_.notifications.clear();
        snapshot_.incoming_call.reset();
    }
    changed();
}

void PhoneNotificationModel::upsert(PhoneNotification notification) {
    notification.app = sanitize_text(notification.app, 40);
    notification.title = sanitize_text(notification.title, 80);
    notification.message = sanitize_text(notification.message, 300);
    if (notification.category == PhoneCategory::IncomingCall) {
        snapshot_.incoming_call = std::move(notification);
        changed();
        return;
    }
    auto& list = snapshot_.notifications;
    const auto uid = notification.uid;
    list.erase(std::remove_if(list.begin(), list.end(),
                              [uid](const PhoneNotification& item) { return item.uid == uid; }), list.end());
    list.insert(list.begin(), std::move(notification));
    if (list.size() > kMaxNotifications) list.resize(kMaxNotifications);
    changed();
}

bool PhoneNotificationModel::remove(std::uint32_t uid) {
    bool removed = false;
    if (snapshot_.incoming_call && snapshot_.incoming_call->uid == uid) {
        snapshot_.incoming_call.reset();
        removed = true;
    }
    auto& list = snapshot_.notifications;
    const auto before = list.size();
    list.erase(std::remove_if(list.begin(), list.end(),
                              [uid](const PhoneNotification& item) { return item.uid == uid; }), list.end());
    removed = removed || list.size() != before;
    if (removed) changed();
    return removed;
}

void PhoneNotificationModel::clear() {
    if (snapshot_.notifications.empty() && !snapshot_.incoming_call) return;
    snapshot_.notifications.clear();
    snapshot_.incoming_call.reset();
    changed();
}

const PhoneNotification* PhoneNotificationModel::find(std::uint32_t uid) const noexcept {
    if (snapshot_.incoming_call && snapshot_.incoming_call->uid == uid) return &*snapshot_.incoming_call;
    for (const auto& item : snapshot_.notifications) {
        if (item.uid == uid) return &item;
    }
    return nullptr;
}

std::string PhoneNotificationModel::context_for(const std::string& question) const {
    if (!question_mentions_phone(question)) return {};
    std::string context;
    for (const auto& item : snapshot_.notifications) {
        std::string line = "- " + item.app;
        if (!item.title.empty()) line += ": " + item.title;
        if (!item.message.empty()) line += " - " + item.message;
        if (item.demo) line += " (DEMO)";
        line += '\n';
        if (context.size() + line.size() > kContextBytes) break;
        context += line;
    }
    return context;
}

bool question_mentions_phone(const std::string& question) {
    const auto folded = fold_text(question);
    return folded_contains_any(folded, {"κινητ", "τηλεφων", "ειδοποιησ", "μηνυμ", "κλησ", "phone", "notification",
                                        "message", "whatsapp", "viber", "sms", "mail", "messenger"});
}

const char* phone_category_name(PhoneCategory category) noexcept {
    switch (category) {
        case PhoneCategory::Other: return "OTHER";
        case PhoneCategory::IncomingCall: return "INCOMING_CALL";
        case PhoneCategory::MissedCall: return "MISSED_CALL";
        case PhoneCategory::Voicemail: return "VOICEMAIL";
        case PhoneCategory::Social: return "SOCIAL";
        case PhoneCategory::Schedule: return "SCHEDULE";
        case PhoneCategory::Email: return "EMAIL";
        case PhoneCategory::News: return "NEWS";
        case PhoneCategory::HealthAndFitness: return "HEALTH";
        case PhoneCategory::BusinessAndFinance: return "BUSINESS";
        case PhoneCategory::Location: return "LOCATION";
        case PhoneCategory::Entertainment: return "ENTERTAINMENT";
    }
    return "OTHER";
}

namespace ancs {
namespace {

constexpr std::uint8_t kGetNotificationAttributes = 0;
constexpr std::uint8_t kPerformNotificationAction = 2;
// Attribute IDs in the order they are requested (and answered).
constexpr std::array<std::uint8_t, 5> kAttributeIds{0, 1, 2, 3, 5};
constexpr std::size_t kMaxResponseBytes = 2048;

void put_uid(std::vector<std::uint8_t>& output, std::uint32_t uid) {
    for (int shift = 0; shift < 32; shift += 8) output.push_back(static_cast<std::uint8_t>((uid >> shift) & 0xFFU));
}

std::uint32_t read_uid(const std::uint8_t* data) {
    return static_cast<std::uint32_t>(data[0]) | (static_cast<std::uint32_t>(data[1]) << 8) |
           (static_cast<std::uint32_t>(data[2]) << 16) | (static_cast<std::uint32_t>(data[3]) << 24);
}

}  // namespace

std::optional<NotificationSourceEvent> parse_notification_source(const std::uint8_t* data, std::size_t size) {
    if (!data || size != 8 || data[0] > 2) return std::nullopt;
    NotificationSourceEvent event;
    event.event = static_cast<EventId>(data[0]);
    event.flags = data[1];
    event.category = data[2];
    event.category_count = data[3];
    event.uid = read_uid(data + 4);
    return event;
}

std::vector<std::uint8_t> get_notification_attributes(std::uint32_t uid) {
    std::vector<std::uint8_t> command{kGetNotificationAttributes};
    put_uid(command, uid);
    command.push_back(0);                          // AppIdentifier
    command.insert(command.end(), {1, 64, 0});     // Title, max 64 bytes
    command.insert(command.end(), {2, 32, 0});     // Subtitle, max 32 bytes
    command.insert(command.end(), {3, 240, 0});    // Message, max 240 bytes
    command.push_back(5);                          // Date
    return command;
}

std::vector<std::uint8_t> perform_action(std::uint32_t uid, bool positive) {
    std::vector<std::uint8_t> command{kPerformNotificationAction};
    put_uid(command, uid);
    command.push_back(positive ? 0 : 1);
    return command;
}

AttributeAssembler::Result AttributeAssembler::push(const std::uint8_t* data, std::size_t size) {
    if (!data || size == 0) return Result::NeedMore;
    if (buffer_.size() + size > kMaxResponseBytes) {
        reset();
        return Result::Invalid;
    }
    buffer_.insert(buffer_.end(), data, data + size);
    const auto result = parse();
    if (result != Result::NeedMore) buffer_.clear();
    return result;
}

void AttributeAssembler::reset() {
    buffer_.clear();
    uid_ = 0;
    for (auto& value : values_) value.clear();
}

AttributeAssembler::Result AttributeAssembler::parse() {
    if (buffer_.size() < 5) return Result::NeedMore;
    if (buffer_[0] != kGetNotificationAttributes) return Result::Invalid;
    uid_ = read_uid(buffer_.data() + 1);
    std::size_t position = 5;
    for (std::size_t index = 0; index < kAttributeIds.size(); ++index) {
        if (position + 3 > buffer_.size()) return Result::NeedMore;
        if (buffer_[position] != kAttributeIds[index]) return Result::Invalid;
        const std::size_t length = static_cast<std::size_t>(buffer_[position + 1]) |
                                   (static_cast<std::size_t>(buffer_[position + 2]) << 8);
        position += 3;
        if (position + length > buffer_.size()) return Result::NeedMore;
        values_[index].assign(reinterpret_cast<const char*>(buffer_.data() + position), length);
        position += length;
    }
    return Result::Complete;
}

PhoneCategory category_from_id(std::uint8_t category) noexcept {
    return category <= 11 ? static_cast<PhoneCategory>(category) : PhoneCategory::Other;
}

void FetchScheduler::enqueue(std::uint32_t uid) {
    if (queue_.size() >= kMaxQueued || std::find(queue_.begin(), queue_.end(), uid) != queue_.end()) return;
    queue_.push_back(uid);
}

void FetchScheduler::remove(std::uint32_t uid) {
    queue_.erase(std::remove(queue_.begin(), queue_.end(), uid), queue_.end());
}

std::optional<std::uint32_t> FetchScheduler::next(std::uint64_t now_ms) {
    if (in_flight_ && now_ms - started_ms_ < kTimeoutMs) return std::nullopt;
    in_flight_ = false;
    if (queue_.empty()) return std::nullopt;
    const auto uid = queue_.front();
    queue_.pop_front();
    in_flight_ = true;
    started_ms_ = now_ms;
    return uid;
}

std::string app_display_name(const std::string& identifier) {
    static constexpr std::pair<const char*, const char*> kKnown[]{
        {"com.apple.MobileSMS", "Messages"}, {"com.apple.mobilephone", "Phone"},
        {"com.apple.mobilemail", "Mail"}, {"com.apple.mobilecal", "Calendar"},
        {"com.apple.reminders", "Reminders"}, {"net.whatsapp.WhatsApp", "WhatsApp"},
        {"com.viber", "Viber"}, {"ph.telegra.Telegraph", "Telegram"},
        {"com.facebook.Messenger", "Messenger"}, {"com.burbn.instagram", "Instagram"},
        {"com.google.Gmail", "Gmail"}, {"com.microsoft.Office.Outlook", "Outlook"},
        {"com.tinyspeck.chatlyio", "Slack"}, {"org.whispersystems.signal", "Signal"},
        {"com.facebook.Facebook", "Facebook"},
    };
    for (const auto& [id, name] : kKnown) {
        if (identifier == id) return name;
    }
    const auto dot = identifier.find_last_of('.');
    auto name = sanitize_text(dot == std::string::npos ? identifier : identifier.substr(dot + 1), 32);
    return name.empty() ? std::string("App") : name;
}

}  // namespace ancs

}  // namespace aura
