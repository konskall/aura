#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace aura {

enum class PhoneCategory {
    Other, IncomingCall, MissedCall, Voicemail, Social, Schedule, Email, News,
    HealthAndFitness, BusinessAndFinance, Location, Entertainment,
};

enum class PhoneLinkState { Unavailable, NotPaired, Pairing, Connected, Disconnected };
enum class PhonePlatform { Unknown, Iphone, Android };

struct PhoneNotification {
    std::uint32_t uid{0};
    PhoneCategory category{PhoneCategory::Other};
    std::string app;
    std::string title;
    std::string message;
    std::int64_t minute{0};  // minute_index() when received; 0 if unknown
    bool important{false};
    bool can_accept{false};   // ANCS positive action (answer a call)
    bool can_decline{false};  // ANCS negative action (decline / dismiss)
    bool demo{false};
};

struct PhoneSnapshot {
    PhoneLinkState link{PhoneLinkState::Unavailable};
    PhonePlatform platform{PhonePlatform::Unknown};
    std::string device_name;
    std::vector<PhoneNotification> notifications;  // newest first, no calls
    std::optional<PhoneNotification> incoming_call;
    std::uint64_t sequence{0};
};

// Notifications mirrored from the owner's phone (iPhone: ANCS over BLE;
// Android: the companion app described in docs/phone/ANDROID_COMPANION.md).
// Contents stay in RAM, are never logged, and reach the assistant only when a
// question is about the phone or its messages.
class PhoneNotificationModel {
public:
    static constexpr std::size_t kMaxNotifications = 20;
    static constexpr std::size_t kContextBytes = 1400;

    void set_link(PhoneLinkState link, PhonePlatform platform, const std::string& device_name);
    void upsert(PhoneNotification notification);
    bool remove(std::uint32_t uid);
    void clear();
    const PhoneNotification* find(std::uint32_t uid) const noexcept;

    std::string context_for(const std::string& question) const;
    const PhoneSnapshot& snapshot() const noexcept { return snapshot_; }

private:
    void changed() noexcept { ++snapshot_.sequence; }
    PhoneSnapshot snapshot_{};
};

bool question_mentions_phone(const std::string& question);
const char* phone_category_name(PhoneCategory category) noexcept;

// Apple Notification Center Service (ANCS) wire format. The firmware's BLE
// client moves bytes; everything that interprets them lives here and is
// unit-tested.
namespace ancs {

inline constexpr char kServiceUuid[] = "7905F431-B5CE-4E99-A40F-4B1E122D00D0";
inline constexpr char kNotificationSourceUuid[] = "9FBF120D-6301-42D9-8C58-25E699A21DBD";
inline constexpr char kControlPointUuid[] = "69D1D8F3-45E1-49A8-9821-9BBDFDAAD9D9";
inline constexpr char kDataSourceUuid[] = "22EAC6E9-24D6-4BB5-BE44-B36ACE7C7BFB";

enum class EventId : std::uint8_t { Added = 0, Modified = 1, Removed = 2 };

struct NotificationSourceEvent {
    EventId event{EventId::Added};
    std::uint8_t flags{0};
    std::uint8_t category{0};
    std::uint8_t category_count{0};
    std::uint32_t uid{0};

    bool silent() const noexcept { return (flags & 0x01U) != 0; }
    bool important() const noexcept { return (flags & 0x02U) != 0; }
    bool pre_existing() const noexcept { return (flags & 0x04U) != 0; }
    bool positive_action() const noexcept { return (flags & 0x08U) != 0; }
    bool negative_action() const noexcept { return (flags & 0x10U) != 0; }
};

std::optional<NotificationSourceEvent> parse_notification_source(const std::uint8_t* data, std::size_t size);

// Get Notification Attributes: app identifier, title, subtitle, message, date.
std::vector<std::uint8_t> get_notification_attributes(std::uint32_t uid);
// Perform Notification Action: positive (accept) or negative (decline).
std::vector<std::uint8_t> perform_action(std::uint32_t uid, bool positive);

// Reassembles one Get Notification Attributes response, which may arrive in
// several Data Source notifications.
class AttributeAssembler {
public:
    enum class Result { NeedMore, Complete, Invalid };

    Result push(const std::uint8_t* data, std::size_t size);
    void reset();

    std::uint32_t uid() const noexcept { return uid_; }
    const std::string& app_identifier() const noexcept { return values_[0]; }
    const std::string& title() const noexcept { return values_[1]; }
    const std::string& subtitle() const noexcept { return values_[2]; }
    const std::string& message() const noexcept { return values_[3]; }
    const std::string& date() const noexcept { return values_[4]; }

private:
    Result parse();

    std::vector<std::uint8_t> buffer_;
    std::uint32_t uid_{0};
    std::string values_[5];
};

PhoneCategory category_from_id(std::uint8_t category) noexcept;
// "net.whatsapp.WhatsApp" -> "WhatsApp"; unknown identifiers use their last part.
std::string app_display_name(const std::string& identifier);

}  // namespace ancs

}  // namespace aura
