#include "aura/windows_wifi_service.hpp"

#include <algorithm>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#include <wlanapi.h>
#endif

namespace aura::simulator {

namespace {

#ifdef _WIN32
// Windows reports signal quality 0..100, linear between -100 and -50 dBm.
int quality_to_rssi(unsigned long quality) {
    return static_cast<int>(std::min<unsigned long>(quality, 100UL) / 2UL) - 100;
}

WifiSecurity map_security(bool security_enabled, DOT11_AUTH_ALGORITHM algorithm) {
    const auto value = static_cast<unsigned long>(algorithm);
    if (!security_enabled) return value == 10UL ? WifiSecurity::Unknown : WifiSecurity::Open;
    switch (value) {
        case 1UL:   // open authentication with static keys
        case 2UL: return WifiSecurity::Wep;
        case 4UL:   // WPA-PSK
        case 7UL: return WifiSecurity::WpaPersonal;  // RSNA-PSK (WPA2)
        case 9UL: return WifiSecurity::Wpa3Personal;  // WPA3-SAE
        case 3UL:
        case 6UL:
        case 8UL:
        case 11UL: return WifiSecurity::Enterprise;
        default: return WifiSecurity::Unknown;  // OWE and vendor methods
    }
}

std::string ssid_text(const DOT11_SSID& ssid) {
    const auto length = std::min<ULONG>(ssid.uSSIDLength, static_cast<ULONG>(kWifiSsidMaxBytes));
    return std::string(reinterpret_cast<const char*>(ssid.ucSSID), length);
}

// Declared before the client handle so it is destroyed after WlanCloseHandle,
// which waits for any running notification callback.
struct ScanWait {
    HANDLE event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    GUID interface_guid{};
    ~ScanWait() {
        if (event) CloseHandle(event);
    }
};

struct WlanClient {
    HANDLE handle{nullptr};
    ~WlanClient() {
        if (handle) WlanCloseHandle(handle, nullptr);
    }
};

void CALLBACK on_wlan_notification(PWLAN_NOTIFICATION_DATA data, PVOID context) {
    auto* wait = static_cast<ScanWait*>(context);
    if (!data || !wait || data->NotificationSource != WLAN_NOTIFICATION_SOURCE_ACM) return;
    if (!IsEqualGUID(data->InterfaceGuid, wait->interface_guid)) return;
    if (data->NotificationCode == wlan_notification_acm_scan_complete ||
        data->NotificationCode == wlan_notification_acm_scan_fail) {
        SetEvent(wait->event);
    }
}

WifiHostScan run(bool scan) {
    WifiHostScan result;
    ScanWait wait;
    WlanClient client;
    DWORD version = 0;
    if (WlanOpenHandle(2, nullptr, &version, &client.handle) != ERROR_SUCCESS) {
        result.scan.error_code = "WLAN_SERVICE_UNAVAILABLE";
        return result;
    }
    PWLAN_INTERFACE_INFO_LIST interfaces = nullptr;
    if (WlanEnumInterfaces(client.handle, nullptr, &interfaces) != ERROR_SUCCESS || !interfaces ||
        interfaces->dwNumberOfItems == 0) {
        if (interfaces) WlanFreeMemory(interfaces);
        result.scan.error_code = "NO_ADAPTER";
        return result;
    }
    DWORD chosen = 0;
    for (DWORD index = 0; index < interfaces->dwNumberOfItems; ++index) {
        if (interfaces->InterfaceInfo[index].isState == wlan_interface_state_connected) {
            chosen = index;
            break;
        }
    }
    const GUID guid = interfaces->InterfaceInfo[chosen].InterfaceGuid;
    WlanFreeMemory(interfaces);
    result.adapter_present = true;

    PWLAN_CONNECTION_ATTRIBUTES attributes = nullptr;
    DWORD size = 0;
    WLAN_OPCODE_VALUE_TYPE type{};
    if (WlanQueryInterface(client.handle, &guid, wlan_intf_opcode_current_connection, nullptr, &size,
                           reinterpret_cast<PVOID*>(&attributes), &type) == ERROR_SUCCESS && attributes) {
        if (attributes->isState == wlan_interface_state_connected) {
            const auto& association = attributes->wlanAssociationAttributes;
            result.host_connected = true;
            result.host_ssid = ssid_text(association.dot11Ssid);
            result.host_rssi_dbm = quality_to_rssi(association.wlanSignalQuality);
            result.host_security = map_security(attributes->wlanSecurityAttributes.bSecurityEnabled != FALSE,
                                                attributes->wlanSecurityAttributes.dot11AuthAlgorithm);
        }
        WlanFreeMemory(attributes);
    }
    if (!scan) {
        result.scan.ok = true;
        return result;
    }

    wait.interface_guid = guid;
    const bool registered = wait.event &&
        WlanRegisterNotification(client.handle, WLAN_NOTIFICATION_SOURCE_ACM, TRUE, on_wlan_notification, &wait,
                                 nullptr, nullptr) == ERROR_SUCCESS;
    if (WlanScan(client.handle, &guid, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
        // Windows finishes a scan within about four seconds; when it does not,
        // the list below is the adapter's most recent (still real) result.
        if (registered) WaitForSingleObject(wait.event, 4500);
        else Sleep(3000);
    }
    if (registered) {
        WlanRegisterNotification(client.handle, WLAN_NOTIFICATION_SOURCE_NONE, TRUE, nullptr, nullptr, nullptr,
                                 nullptr);
    }

    PWLAN_AVAILABLE_NETWORK_LIST list = nullptr;
    if (WlanGetAvailableNetworkList(client.handle, &guid, 0, nullptr, &list) != ERROR_SUCCESS || !list) {
        result.scan.error_code = "SCAN_FAILED";
        return result;
    }
    result.scan.networks.reserve(list->dwNumberOfItems);
    for (DWORD index = 0; index < list->dwNumberOfItems; ++index) {
        const auto& item = list->Network[index];
        if (item.dot11BssType != dot11_BSS_type_infrastructure) continue;
        WifiNetwork network;
        network.ssid = ssid_text(item.dot11Ssid);
        network.rssi_dbm = quality_to_rssi(item.wlanSignalQuality);
        network.security = map_security(item.bSecurityEnabled != FALSE, item.dot11DefaultAuthAlgorithm);
        result.scan.networks.push_back(std::move(network));
    }
    WlanFreeMemory(list);
    result.scan.ok = true;
    return result;
}
#endif

}  // namespace

WifiHostScan WindowsWifiService::probe() {
#ifdef _WIN32
    return run(false);
#else
    WifiHostScan result;
    result.scan.error_code = "PLATFORM_NOT_SUPPORTED";
    return result;
#endif
}

std::future<WifiHostScan> WindowsWifiService::scan() {
    return std::async(std::launch::async, [] {
#ifdef _WIN32
        return run(true);
#else
        return probe();
#endif
    });
}

}  // namespace aura::simulator
