#include "esp32_phone_link.hpp"

#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include <algorithm>
#include <cstring>

extern "C" void ble_store_config_init(void);

namespace aura::firmware {

Esp32PhoneLink* Esp32PhoneLink::instance = nullptr;

namespace {

constexpr const char* kTag = "aura-phone";

// ANCS UUIDs, little-endian as NimBLE stores them (see aura::ancs for the text form).
const ble_uuid128_t kAncsService = BLE_UUID128_INIT(
    0xD0, 0x00, 0x2D, 0x12, 0x1E, 0x4B, 0x0F, 0xA4, 0x99, 0x4E, 0xCE, 0xB5, 0x31, 0xF4, 0x05, 0x79);
const ble_uuid128_t kNotificationSource = BLE_UUID128_INIT(
    0xBD, 0x1D, 0xA2, 0x99, 0xE6, 0x25, 0x58, 0x8C, 0xD9, 0x42, 0x01, 0x63, 0x0D, 0x12, 0xBF, 0x9F);
const ble_uuid128_t kControlPoint = BLE_UUID128_INIT(
    0xD9, 0xD9, 0xAA, 0xFD, 0xBD, 0x9B, 0x21, 0x98, 0xA8, 0x49, 0xE1, 0x45, 0xF3, 0xD8, 0xD1, 0x69);
const ble_uuid128_t kDataSource = BLE_UUID128_INIT(
    0xFB, 0x7B, 0x7C, 0xCE, 0x6A, 0xB3, 0x44, 0xBE, 0xB5, 0x4B, 0xD6, 0x24, 0xE9, 0xC6, 0xEA, 0x22);

// Everything below runs in the NimBLE host task only.
struct Link {
    std::uint8_t own_address_type{0};
    std::uint16_t connection{BLE_HS_CONN_HANDLE_NONE};
    std::uint16_t service_end{0};
    std::uint16_t notification_source{0};
    std::uint16_t control_point{0};
    std::uint16_t data_source{0};
    bool request_in_flight{false};
    std::vector<std::uint32_t> fetch_queue;
    std::vector<ancs::NotificationSourceEvent> known;
    ancs::AttributeAssembler assembler;
};
Link link;

void publish_link(PhoneLinkState state, PhonePlatform platform) {
    PhoneEvent event;
    event.kind = PhoneEvent::Kind::Link;
    event.link = state;
    event.platform = platform;
    if (Esp32PhoneLink::instance) Esp32PhoneLink::instance->push(std::move(event));
}

int gap_event(ble_gap_event* event, void* argument);

void advertise() {
    // Flags, the name, and a 128-bit service solicitation for ANCS, which makes
    // an iPhone offer AURA its notifications after pairing.
    std::uint8_t data[31]{};
    std::size_t length = 0;
    const std::uint8_t flags[] = {2, 0x01, 0x06};
    const std::uint8_t name[] = {5, 0x09, 'A', 'U', 'R', 'A'};
    std::memcpy(data + length, flags, sizeof(flags));
    length += sizeof(flags);
    std::memcpy(data + length, name, sizeof(name));
    length += sizeof(name);
    data[length++] = 17;
    data[length++] = 0x15;
    std::memcpy(data + length, kAncsService.value, 16);
    length += 16;
    if (ble_gap_adv_set_data(data, static_cast<int>(length)) != 0) {
        ESP_LOGW(kTag, "advertising data rejected");
        return;
    }
    ble_gap_adv_params parameters{};
    parameters.conn_mode = BLE_GAP_CONN_MODE_UND;
    parameters.disc_mode = BLE_GAP_DISC_MODE_GEN;
    const int result = ble_gap_adv_start(link.own_address_type, nullptr, BLE_HS_FOREVER, &parameters, gap_event, nullptr);
    if (result != 0 && result != BLE_HS_EALREADY) ESP_LOGW(kTag, "advertising failed: %d", result);
}

void request_next_attributes() {
    if (link.request_in_flight || link.fetch_queue.empty() || link.control_point == 0) return;
    const auto uid = link.fetch_queue.front();
    link.fetch_queue.erase(link.fetch_queue.begin());
    const auto command = ancs::get_notification_attributes(uid);
    link.assembler.reset();
    if (ble_gattc_write_flat(link.connection, link.control_point, command.data(),
                             static_cast<std::uint16_t>(command.size()), nullptr, nullptr) == 0) {
        link.request_in_flight = true;
    }
}

void on_notification_source(const std::uint8_t* data, std::size_t size) {
    const auto event = ancs::parse_notification_source(data, size);
    if (!event) return;
    auto& known = link.known;
    known.erase(std::remove_if(known.begin(), known.end(),
                               [&](const ancs::NotificationSourceEvent& item) { return item.uid == event->uid; }),
                known.end());
    if (event->event == ancs::EventId::Removed) {
        PhoneEvent removed;
        removed.kind = PhoneEvent::Kind::Remove;
        removed.uid = event->uid;
        if (Esp32PhoneLink::instance) Esp32PhoneLink::instance->push(std::move(removed));
        return;
    }
    if (event->silent() && event->pre_existing()) return;
    if (known.size() >= 32) known.erase(known.begin());
    known.push_back(*event);
    if (link.fetch_queue.size() < 16) link.fetch_queue.push_back(event->uid);
    request_next_attributes();
}

void on_data_source(const std::uint8_t* data, std::size_t size) {
    const auto result = link.assembler.push(data, size);
    if (result == ancs::AttributeAssembler::Result::NeedMore) return;
    link.request_in_flight = false;
    if (result == ancs::AttributeAssembler::Result::Complete) {
        PhoneEvent upsert;
        upsert.kind = PhoneEvent::Kind::Upsert;
        auto& item = upsert.notification;
        item.uid = link.assembler.uid();
        item.app = ancs::app_display_name(link.assembler.app_identifier());
        item.title = link.assembler.title();
        item.message = link.assembler.message().empty() ? link.assembler.subtitle() : link.assembler.message();
        for (const auto& known : link.known) {
            if (known.uid != item.uid) continue;
            item.category = ancs::category_from_id(known.category);
            item.important = known.important();
            item.can_accept = known.positive_action();
            item.can_decline = known.negative_action();
        }
        if (Esp32PhoneLink::instance) Esp32PhoneLink::instance->push(std::move(upsert));
    }
    request_next_attributes();
}

int on_subscribed(std::uint16_t, const ble_gatt_error* error, ble_gatt_attr*, void* argument) {
    if (error->status != 0) {
        ESP_LOGW(kTag, "ANCS subscription failed: %d", error->status);
        return 0;
    }
    // Data Source first (argument 0), then Notification Source (argument 1).
    if (argument == nullptr) {
        const std::uint8_t enable[] = {0x01, 0x00};
        ble_gattc_write_flat(link.connection, static_cast<std::uint16_t>(link.notification_source + 1), enable,
                             sizeof(enable), on_subscribed, reinterpret_cast<void*>(1));
    } else {
        publish_link(PhoneLinkState::Connected, PhonePlatform::Iphone);
    }
    return 0;
}

int on_characteristic(std::uint16_t, const ble_gatt_error* error, const ble_gatt_chr* characteristic, void*) {
    if (error->status == 0 && characteristic) {
        if (ble_uuid_cmp(&characteristic->uuid.u, &kNotificationSource.u) == 0) {
            link.notification_source = characteristic->val_handle;
        } else if (ble_uuid_cmp(&characteristic->uuid.u, &kControlPoint.u) == 0) {
            link.control_point = characteristic->val_handle;
        } else if (ble_uuid_cmp(&characteristic->uuid.u, &kDataSource.u) == 0) {
            link.data_source = characteristic->val_handle;
        }
        return 0;
    }
    if (error->status != BLE_HS_EDONE) return 0;
    if (link.notification_source == 0 || link.control_point == 0 || link.data_source == 0) {
        publish_link(PhoneLinkState::Connected, PhonePlatform::Unknown);
        return 0;
    }
    // The client configuration descriptor directly follows each value handle
    // on iOS (NEEDS HARDWARE confirmation; a descriptor discovery is the fallback).
    const std::uint8_t enable[] = {0x01, 0x00};
    ble_gattc_write_flat(link.connection, static_cast<std::uint16_t>(link.data_source + 1), enable, sizeof(enable),
                         on_subscribed, nullptr);
    return 0;
}

int on_service(std::uint16_t connection, const ble_gatt_error* error, const ble_gatt_svc* service, void*) {
    if (error->status == 0 && service) {
        link.service_end = service->end_handle;
        ble_gattc_disc_all_chrs(connection, service->start_handle, service->end_handle, on_characteristic, nullptr);
        return 0;
    }
    if (error->status == BLE_HS_EDONE && link.service_end == 0) {
        // Not an iPhone (or ANCS refused): the link exists without notifications.
        publish_link(PhoneLinkState::Connected, PhonePlatform::Unknown);
    }
    return 0;
}

int gap_event(ble_gap_event* event, void*) {
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status != 0) {
                advertise();
                return 0;
            }
            link = Link{link.own_address_type};
            link.connection = event->connect.conn_handle;
            publish_link(PhoneLinkState::Pairing, PhonePlatform::Unknown);
            ble_gap_security_initiate(link.connection);
            return 0;
        case BLE_GAP_EVENT_ENC_CHANGE:
            if (event->enc_change.status == 0) {
                ble_gattc_disc_svc_by_uuid(link.connection, &kAncsService.u, on_service, nullptr);
            }
            return 0;
        case BLE_GAP_EVENT_NOTIFY_RX: {
            std::uint8_t buffer[512];
            std::uint16_t length = 0;
            if (ble_hs_mbuf_to_flat(event->notify_rx.om, buffer, sizeof(buffer), &length) != 0) return 0;
            if (event->notify_rx.attr_handle == link.notification_source) on_notification_source(buffer, length);
            else if (event->notify_rx.attr_handle == link.data_source) on_data_source(buffer, length);
            return 0;
        }
        case BLE_GAP_EVENT_DISCONNECT:
            link.connection = BLE_HS_CONN_HANDLE_NONE;
            publish_link(PhoneLinkState::Disconnected, PhonePlatform::Unknown);
            advertise();
            return 0;
        case BLE_GAP_EVENT_ADV_COMPLETE:
            advertise();
            return 0;
        case BLE_GAP_EVENT_REPEAT_PAIRING: {
            // The phone forgot AURA: drop the old bond and pair again.
            ble_gap_conn_desc description{};
            if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &description) == 0) {
                ble_store_util_delete_peer(&description.peer_id_addr);
            }
            return BLE_GAP_REPEAT_PAIRING_RETRY;
        }
        default:
            return 0;
    }
}

void on_sync() {
    ble_hs_util_ensure_addr(0);
    ble_hs_id_infer_auto(0, &link.own_address_type);
    publish_link(PhoneLinkState::NotPaired, PhonePlatform::Unknown);
    advertise();
}

void on_reset(int reason) { ESP_LOGW(kTag, "BLE host reset: %d", reason); }

void host_task(void*) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}

}  // namespace

bool Esp32PhoneLink::start() {
    instance = this;
    if (nimble_port_init() != ESP_OK) {
        ESP_LOGW(kTag, "NimBLE unavailable");
        return false;
    }
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    // Bonded, encrypted links: iOS shares ANCS only with a bonded accessory.
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set("AURA");
    ble_store_config_init();
    nimble_port_freertos_init(host_task);
    started_ = true;
    ESP_LOGI(kTag, "BLE phone link started (ANCS client)");
    return true;
}

void Esp32PhoneLink::set_pairable(bool pairable) {
    if (!started_) return;
    if (pairable && link.connection == BLE_HS_CONN_HANDLE_NONE) advertise();
    if (!pairable) ble_gap_adv_stop();
}

void Esp32PhoneLink::forget() {
    if (!started_) return;
    if (link.connection != BLE_HS_CONN_HANDLE_NONE) ble_gap_terminate(link.connection, BLE_ERR_REM_USER_CONN_TERM);
    ble_store_clear();
    publish_link(PhoneLinkState::NotPaired, PhonePlatform::Unknown);
}

void Esp32PhoneLink::perform_action(std::uint32_t uid, bool positive) {
    if (!started_ || link.connection == BLE_HS_CONN_HANDLE_NONE || link.control_point == 0) return;
    const auto command = ancs::perform_action(uid, positive);
    ble_gattc_write_flat(link.connection, link.control_point, command.data(), static_cast<std::uint16_t>(command.size()),
                         nullptr, nullptr);
}

void Esp32PhoneLink::push(PhoneEvent event) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (events_.size() < 64) events_.push_back(std::move(event));
}

bool Esp32PhoneLink::poll(PhoneEvent& event) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (events_.empty()) return false;
    event = std::move(events_.front());
    events_.erase(events_.begin());
    return true;
}

}  // namespace aura::firmware
