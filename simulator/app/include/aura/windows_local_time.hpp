#pragma once
#include "aura/text_util.hpp"
#include <windows.h>

namespace aura::simulator {
// Windows applies the zone's historical daylight-saving rules for this
// instant. The default is the same configured local zone as the device clock.
inline std::optional<LocalDateTime> windows_local_time(std::int64_t unix_seconds,
                                                       const DYNAMIC_TIME_ZONE_INFORMATION* zone = nullptr) {
    if (unix_seconds < 0 || unix_seconds > 253402300799LL) return std::nullopt;
    ULARGE_INTEGER ticks{};
    ticks.QuadPart = (static_cast<std::uint64_t>(unix_seconds) + 11644473600ULL) * 10000000ULL;
    const FILETIME file{ticks.LowPart, ticks.HighPart};
    SYSTEMTIME utc{}, local{};
    DYNAMIC_TIME_ZONE_INFORMATION configured{};
    if (!zone) {
        if (GetDynamicTimeZoneInformation(&configured) == TIME_ZONE_ID_INVALID) return std::nullopt;
        zone = &configured;
    }
    if (!FileTimeToSystemTime(&file, &utc) || !SystemTimeToTzSpecificLocalTimeEx(zone, &utc, &local))
        return std::nullopt;
    return LocalDateTime{local.wYear,   local.wMonth,  local.wDay,      local.wHour,
                         local.wMinute, local.wSecond, local.wDayOfWeek};
}
}  // namespace aura::simulator
