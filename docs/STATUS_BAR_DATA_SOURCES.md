# AURA v8 status-bar data sources

| Field | Windows simulator | ESP32-S3 firmware |
|---|---|---|
| Time | Host local clock, refreshed each second; deterministic capture time uses simulated origin. | RTC/SNTP only when synchronized; otherwise unknown. |
| Wi-Fi | Controlled developer state with 0–3 signal levels. | Board network adapter state/RSSI when implemented. |
| BLE | Controlled developer on/off state; hidden when off/unknown. | BLE stack state when implemented. |
| Battery | Controlled 0–100%, charging, low and unknown states. `DEV` identifies simulator context. | AXP2101/BSP telemetry only after board validation; otherwise unknown. |
| Charging | Controlled developer state. | Confirmed PMIC signal only. |

The component uses a Wi-Fi symbol, optional `BLE`, centered time, graphical battery fill, percentage and charging glyph. There is no cellular modem on the target board; `SIM`, carrier, cellular and mobile-signal labels were removed from visible localization/status output. Battery and connectivity detail screens use `DEMO`, not a phone-network `SIM` label.

The control panel can inject Wi-Fi loss/restore, BLE on/off, battery percentage, battery unknown and charging/on-battery. `DeviceStatusModel` tests bound percentages, signal levels and unavailable states.
