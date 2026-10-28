#include "aura/windows_ancs_service.hpp"

#ifdef _WIN32
#include <windows.h>
#include <setupapi.h>
#include <bluetoothleapis.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <iostream>
#include <unordered_map>
#include <utility>

namespace aura::simulator {

struct WindowsAncsService::Impl {
    std::atomic<bool> stopping{false};
    std::thread worker;
    std::mutex mutex;
    std::deque<WindowsAncsEvent> events;

#ifdef _WIN32
    static bool trace_enabled() noexcept {
        const char* value = std::getenv("AURA_ANCS_TRACE");
        return value && std::strcmp(value, "1") == 0;
    }

    static void trace(const char* stage, HRESULT result = S_OK) {
        if (!trace_enabled()) return;
        std::cerr << "AURA ANCS trace: " << stage;
        if (result != S_OK) std::cerr << " hr=0x" << std::hex << static_cast<unsigned long>(result) << std::dec;
        std::cerr << '\n';
    }

    static std::uint64_t now_ms() {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    // How often a connected iPhone is checked for having gone away.
    static constexpr std::uint64_t kProbeMs = 15000;

    // `service` is opened and closed by the worker and written from both the
    // GATT callback thread and the UI thread (actions): `gatt_mutex` guards it.
    std::mutex gatt_mutex;
    HANDLE service{INVALID_HANDLE_VALUE};
    BTH_LE_GATT_CHARACTERISTIC notification_source{};
    BTH_LE_GATT_CHARACTERISTIC control_point{};
    BTH_LE_GATT_CHARACTERISTIC data_source{};
    BTH_LE_GATT_DESCRIPTOR notification_cccd{};
    bool have_cccd{false};
    BLUETOOTH_GATT_EVENT_HANDLE notification_event{};
    BLUETOOTH_GATT_EVENT_HANDLE data_event{};
    struct CallbackContext {
        Impl* self{};
        bool notification_source{false};
    } notification_context{this, true}, data_context{this, false};
    struct Metadata {
        std::uint8_t flags{0};
        std::uint8_t category{0};
    };
    std::unordered_map<std::uint32_t, Metadata> metadata;
    ancs::FetchScheduler fetches;
    ancs::AttributeAssembler assembler;
    // A failed write may mean the iPhone has gone: check it at once.
    std::atomic<bool> link_suspect{false};
#endif

    void push(WindowsAncsEvent event) {
        std::lock_guard<std::mutex> lock(mutex);
        if (events.size() >= 64) events.pop_front();
        events.push_back(std::move(event));
    }

#ifdef _WIN32
    static bool uuid_equal(const BTH_LE_UUID& value, const GUID& expected) {
        return !value.IsShortUuid && std::memcmp(&value.Value.LongUuid, &expected, sizeof(GUID)) == 0;
    }

    static std::vector<std::wstring> service_paths(const GUID& service_uuid) {
        std::vector<std::wstring> paths;
        HDEVINFO set = SetupDiGetClassDevsW(&service_uuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
        if (set == INVALID_HANDLE_VALUE) return paths;
        for (DWORD index = 0;; ++index) {
            SP_DEVICE_INTERFACE_DATA interface_data{};
            interface_data.cbSize = sizeof(interface_data);
            if (!SetupDiEnumDeviceInterfaces(set, nullptr, &service_uuid, index, &interface_data)) break;
            DWORD required = 0;
            SetupDiGetDeviceInterfaceDetailW(set, &interface_data, nullptr, 0, &required, nullptr);
            if (required < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)) continue;
            std::vector<std::uint8_t> storage(required);
            auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(storage.data());
            detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
            if (SetupDiGetDeviceInterfaceDetailW(set, &interface_data, detail, required, nullptr, nullptr)) {
                paths.emplace_back(detail->DevicePath);
            }
        }
        SetupDiDestroyDeviceInfoList(set);
        return paths;
    }

    static bool characteristics(HANDLE handle, std::vector<BTH_LE_GATT_CHARACTERISTIC>& output) {
        USHORT count = 0;
        const HRESULT first = BluetoothGATTGetCharacteristics(handle, nullptr, 0, nullptr, &count,
                                                               BLUETOOTH_GATT_FLAG_NONE);
        if (first != HRESULT_FROM_WIN32(ERROR_MORE_DATA) || count == 0) return false;
        output.resize(count);
        USHORT actual = 0;
        const HRESULT result = BluetoothGATTGetCharacteristics(handle, nullptr, count, output.data(), &actual,
                                                                BLUETOOTH_GATT_FLAG_NONE);
        if (result != S_OK) return false;
        output.resize(actual);
        return true;
    }

    bool subscribe(const BTH_LE_GATT_CHARACTERISTIC& characteristic, CallbackContext& context,
                   BLUETOOTH_GATT_EVENT_HANDLE& event_handle, BTH_LE_GATT_DESCRIPTOR* cccd = nullptr) {
        USHORT count = 0;
        HRESULT result = BluetoothGATTGetDescriptors(service, const_cast<BTH_LE_GATT_CHARACTERISTIC*>(&characteristic),
                                                      0, nullptr, &count, BLUETOOTH_GATT_FLAG_NONE);
        if (result != HRESULT_FROM_WIN32(ERROR_MORE_DATA) || count == 0) return false;
        std::vector<BTH_LE_GATT_DESCRIPTOR> descriptors(count);
        USHORT actual = 0;
        result = BluetoothGATTGetDescriptors(service, const_cast<BTH_LE_GATT_CHARACTERISTIC*>(&characteristic),
                                             count, descriptors.data(), &actual, BLUETOOTH_GATT_FLAG_NONE);
        if (result != S_OK) return false;
        auto descriptor = std::find_if(descriptors.begin(), descriptors.begin() + actual,
            [](const BTH_LE_GATT_DESCRIPTOR& item) {
                return item.DescriptorType == ClientCharacteristicConfiguration;
            });
        if (descriptor == descriptors.begin() + actual) return false;
        BTH_LE_GATT_DESCRIPTOR_VALUE value{};
        value.DescriptorType = ClientCharacteristicConfiguration;
        value.DescriptorUuid = descriptor->DescriptorUuid;
        value.ClientCharacteristicConfiguration.IsSubscribeToNotification = TRUE;
        value.ClientCharacteristicConfiguration.IsSubscribeToIndication = FALSE;
        result = BluetoothGATTSetDescriptorValue(service, &*descriptor, &value,
                                                  BLUETOOTH_GATT_FLAG_CONNECTION_ENCRYPTED);
        if (result != S_OK) return false;

        BLUETOOTH_GATT_VALUE_CHANGED_EVENT_REGISTRATION registration{};
        registration.NumCharacteristics = 1;
        registration.Characteristics[0] = characteristic;
        result = BluetoothGATTRegisterEvent(service, CharacteristicValueChangedEvent, &registration,
                                            value_changed, &context, &event_handle,
                                            BLUETOOTH_GATT_FLAG_NONE);
        if (result == S_OK && cccd) *cccd = *descriptor;
        return result == S_OK;
    }

    bool write_control(const std::vector<std::uint8_t>& command) {
        if (command.empty()) return false;
        std::vector<std::uint8_t> storage(sizeof(BTH_LE_GATT_CHARACTERISTIC_VALUE) + command.size() - 1);
        auto* value = reinterpret_cast<PBTH_LE_GATT_CHARACTERISTIC_VALUE>(storage.data());
        value->DataSize = static_cast<ULONG>(command.size());
        std::memcpy(value->Data, command.data(), command.size());
        std::lock_guard<std::mutex> lock(gatt_mutex);
        if (service == INVALID_HANDLE_VALUE) return false;
        const HRESULT result = BluetoothGATTSetCharacteristicValue(
            service, &control_point, value, 0, BLUETOOTH_GATT_FLAG_CONNECTION_ENCRYPTED);
        if (result != S_OK) {
            trace("write-control-failed", result);
            link_suspect.store(true);
        }
        return result == S_OK;
    }

    void request_next() {
        std::vector<std::uint8_t> command;
        {
            std::lock_guard<std::mutex> lock(mutex);
            const auto uid = fetches.next(now_ms());
            if (!uid) return;
            assembler.reset();
            command = ancs::get_notification_attributes(*uid);
        }
        if (!write_control(command)) {
            std::lock_guard<std::mutex> lock(mutex);
            fetches.finished();
        }
    }

    void notification_changed(const std::uint8_t* bytes, std::size_t size) {
        const auto parsed = ancs::parse_notification_source(bytes, size);
        if (!parsed) return;
        if (parsed->event == ancs::EventId::Removed) {
            {
                std::lock_guard<std::mutex> lock(mutex);
                metadata.erase(parsed->uid);
                fetches.remove(parsed->uid);
            }
            WindowsAncsEvent event;
            event.kind = WindowsAncsEvent::Kind::Remove;
            event.uid = parsed->uid;
            push(std::move(event));
            return;
        }
        if (parsed->silent() && parsed->pre_existing()) return;
        {
            std::lock_guard<std::mutex> lock(mutex);
            metadata[parsed->uid] = {parsed->flags, parsed->category};
            fetches.enqueue(parsed->uid);
        }
        request_next();
    }

    void data_changed(const std::uint8_t* bytes, std::size_t size) {
        ancs::AttributeAssembler::Result result;
        PhoneNotification notification;
        {
            std::lock_guard<std::mutex> lock(mutex);
            result = assembler.push(bytes, size);
            if (result == ancs::AttributeAssembler::Result::NeedMore) return;
            fetches.finished();
            if (result == ancs::AttributeAssembler::Result::Complete) {
                notification.uid = assembler.uid();
                notification.app = ancs::app_display_name(assembler.app_identifier());
                notification.title = assembler.title();
                notification.message = assembler.message().empty() ? assembler.subtitle() : assembler.message();
                const auto found = metadata.find(notification.uid);
                if (found != metadata.end()) {
                    notification.category = ancs::category_from_id(found->second.category);
                    notification.important = (found->second.flags & 0x02U) != 0;
                    notification.can_accept = (found->second.flags & 0x08U) != 0;
                    notification.can_decline = (found->second.flags & 0x10U) != 0;
                }
            }
        }
        if (result == ancs::AttributeAssembler::Result::Complete) {
            WindowsAncsEvent event;
            event.kind = WindowsAncsEvent::Kind::Upsert;
            event.notification = std::move(notification);
            push(std::move(event));
        }
        request_next();
    }

    static void CALLBACK value_changed(BTH_LE_GATT_EVENT_TYPE, void* parameter, void* raw_context) {
        auto* context = static_cast<CallbackContext*>(raw_context);
        auto* changed = static_cast<PBLUETOOTH_GATT_VALUE_CHANGED_EVENT>(parameter);
        if (!context || !context->self || !changed || !changed->CharacteristicValue) return;
        const auto* bytes = changed->CharacteristicValue->Data;
        const auto size = static_cast<std::size_t>(changed->CharacteristicValue->DataSize);
        if (context->notification_source) context->self->notification_changed(bytes, size);
        else context->self->data_changed(bytes, size);
    }

    // An iPhone that walks away leaves the handle open and simply goes quiet.
    // Reading the subscription descriptor from the device (not the cache)
    // shows whether it is still there and still subscribed.
    bool connection_alive() {
        if (!have_cccd) return false;
        std::lock_guard<std::mutex> lock(gatt_mutex);
        if (service == INVALID_HANDLE_VALUE) return false;
        USHORT size = 0;
        HRESULT result = BluetoothGATTGetDescriptorValue(service, &notification_cccd, 0, nullptr, &size,
                                                         BLUETOOTH_GATT_FLAG_FORCE_READ_FROM_DEVICE);
        if (result != HRESULT_FROM_WIN32(ERROR_MORE_DATA) || size == 0) {
            trace("probe-failed", result);
            return false;
        }
        std::vector<std::uint8_t> storage(size);
        auto* value = reinterpret_cast<PBTH_LE_GATT_DESCRIPTOR_VALUE>(storage.data());
        result = BluetoothGATTGetDescriptorValue(service, &notification_cccd, size, value, nullptr,
                                                 BLUETOOTH_GATT_FLAG_FORCE_READ_FROM_DEVICE);
        if (result != S_OK) {
            trace("probe-failed", result);
            return false;
        }
        return value->ClientCharacteristicConfiguration.IsSubscribeToNotification == TRUE;
    }

    void disconnect() {
        // Unregister without holding gatt_mutex: unregistering waits for a
        // running callback, which may itself be writing the control point.
        if (notification_event) {
            BluetoothGATTUnregisterEvent(notification_event, BLUETOOTH_GATT_FLAG_NONE);
            notification_event = nullptr;
        }
        if (data_event) {
            BluetoothGATTUnregisterEvent(data_event, BLUETOOTH_GATT_FLAG_NONE);
            data_event = nullptr;
        }
        {
            std::lock_guard<std::mutex> lock(gatt_mutex);
            if (service != INVALID_HANDLE_VALUE) {
                CloseHandle(service);
                service = INVALID_HANDLE_VALUE;
            }
            have_cccd = false;
        }
        std::lock_guard<std::mutex> lock(mutex);
        metadata.clear();
        fetches.clear();
        assembler.reset();
    }

    bool connect() {
        static const GUID kService{0x7905F431, 0xB5CE, 0x4E99,
                                   {0xA4, 0x0F, 0x4B, 0x1E, 0x12, 0x2D, 0x00, 0xD0}};
        static const GUID kNotification{0x9FBF120D, 0x6301, 0x42D9,
                                        {0x8C, 0x58, 0x25, 0xE6, 0x99, 0xA2, 0x1D, 0xBD}};
        static const GUID kControl{0x69D1D8F3, 0x45E1, 0x49A8,
                                   {0x98, 0x21, 0x9B, 0xBD, 0xFD, 0xAA, 0xD9, 0xD9}};
        static const GUID kData{0x22EAC6E9, 0x24D6, 0x4BB5,
                                {0xBE, 0x44, 0xB3, 0x6A, 0xCE, 0x7C, 0x7B, 0xFB}};
        const auto paths = service_paths(kService);
        if (paths.empty()) trace("service-not-found");
        for (const auto& path : paths) {
            HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
            if (handle == INVALID_HANDLE_VALUE) {
                trace("open-service-failed", HRESULT_FROM_WIN32(GetLastError()));
                continue;
            }
            std::vector<BTH_LE_GATT_CHARACTERISTIC> values;
            if (!characteristics(handle, values)) {
                trace("read-characteristics-failed");
                CloseHandle(handle);
                continue;
            }
            BTH_LE_GATT_CHARACTERISTIC ns{}, cp{}, ds{};
            bool have_ns = false, have_cp = false, have_ds = false;
            for (const auto& item : values) {
                if (uuid_equal(item.CharacteristicUuid, kNotification)) { ns = item; have_ns = true; }
                else if (uuid_equal(item.CharacteristicUuid, kControl)) { cp = item; have_cp = true; }
                else if (uuid_equal(item.CharacteristicUuid, kData)) { ds = item; have_ds = true; }
            }
            if (!have_ns || !have_cp || !have_ds) {
                trace("required-characteristic-missing");
                CloseHandle(handle);
                continue;
            }
            {
                std::lock_guard<std::mutex> lock(gatt_mutex);
                service = handle;
                notification_source = ns;
                control_point = cp;
                data_source = ds;
            }
            if (!subscribe(data_source, data_context, data_event)) {
                trace("subscribe-data-source-failed");
                disconnect();
                continue;
            }
            if (!subscribe(notification_source, notification_context, notification_event, &notification_cccd)) {
                trace("subscribe-notification-source-failed");
                disconnect();
                continue;
            }
            {
                std::lock_guard<std::mutex> lock(gatt_mutex);
                have_cccd = true;
            }
            link_suspect.store(false);
            WindowsAncsEvent event;
            event.kind = WindowsAncsEvent::Kind::Link;
            event.link = PhoneLinkState::Connected;
            push(std::move(event));
            trace("connected");
            return true;
        }
        return false;
    }

    void wait(int steps) {
        for (int step = 0; step < steps && !stopping.load(); ++step) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    void run() {
        bool announced = false;
        std::uint64_t last_probe = 0;
        while (!stopping.load()) {
            if (service == INVALID_HANDLE_VALUE) {
                if (connect()) {
                    announced = true;
                    last_probe = now_ms();
                    continue;
                }
                if (!announced) {
                    WindowsAncsEvent event;
                    event.kind = WindowsAncsEvent::Kind::Link;
                    event.link = PhoneLinkState::NotPaired;
                    event.error_code = "ANCS_SERVICE_NOT_FOUND";
                    push(std::move(event));
                    announced = true;
                }
                wait(30);
                continue;
            }
            // Connected: keep the queue moving (a lost reply times out), and
            // now and then make sure the iPhone is still there; if it has
            // gone, say so and reconnect when it returns.
            request_next();
            const auto now = now_ms();
            if (link_suspect.exchange(false) || now - last_probe >= kProbeMs) {
                last_probe = now;
                if (!connection_alive()) {
                    trace("disconnected");
                    disconnect();
                    WindowsAncsEvent event;
                    event.kind = WindowsAncsEvent::Kind::Link;
                    event.link = PhoneLinkState::Disconnected;
                    event.error_code = "ANCS_DISCONNECTED";
                    push(std::move(event));
                    continue;
                }
            }
            wait(1);
        }
        disconnect();
    }
#else
    void run() {
        WindowsAncsEvent event;
        event.kind = WindowsAncsEvent::Kind::Link;
        event.link = PhoneLinkState::Unavailable;
        event.error_code = "WINDOWS_ONLY";
        push(std::move(event));
    }
#endif
};

WindowsAncsService::WindowsAncsService() : impl_(std::make_unique<Impl>()) {}
WindowsAncsService::~WindowsAncsService() { stop(); }

void WindowsAncsService::start() {
    if (impl_->worker.joinable()) return;
    impl_->stopping.store(false);
    impl_->worker = std::thread([self = impl_.get()] { self->run(); });
}

void WindowsAncsService::stop() {
    impl_->stopping.store(true);
    if (impl_->worker.joinable()) impl_->worker.join();
}

bool WindowsAncsService::poll(WindowsAncsEvent& event) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->events.empty()) return false;
    event = std::move(impl_->events.front());
    impl_->events.pop_front();
    return true;
}

bool WindowsAncsService::perform_action(std::uint32_t uid, bool positive) {
#ifdef _WIN32
    return impl_->write_control(ancs::perform_action(uid, positive));
#else
    (void)uid;
    (void)positive;
    return false;
#endif
}

}  // namespace aura::simulator
