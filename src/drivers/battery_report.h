#pragma once
#include "../devices.h"
#include "../platform_compat.h"

#include <ctime>
#include <cwctype>

// Called when an individual reply is validated, before subsequent device queries.
inline void StampBatteryReport(DeviceStatus& status) {
    const auto tick = GetTickCount64();
    status.reportTick = tick ? tick : 1;  // zero is the never-read sentinel
    const std::time_t wall = std::time(nullptr);
    status.reportSeconds = static_cast<std::int64_t>(wall);
    std::tm utc{};
#ifdef _WIN32
    if (gmtime_s(&utc, &wall) != 0) return;
#else
    if (!gmtime_r(&wall, &utc)) return;
#endif
    char text[32]{};
    if (std::strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%SZ", &utc)) status.reportUtc = text;
}

// The canonical HID++ name selects this policy; it is never a cache identity.
inline bool IsMxMaster4ReportedName(const std::wstring& name) {
    std::wstring canonical;
    bool space = false;
    for (wchar_t ch : name) {
        if (std::iswspace(ch)) {
            space = !canonical.empty();
        } else {
            if (space) canonical += L' ';
            canonical += static_cast<wchar_t>(std::towlower(ch));
            space = false;
        }
    }
    return canonical == L"mx master 4" || canonical == L"logitech mx master 4";
}

inline void ApplyMxMaster4Policy(DeviceStatus& status, const std::wstring& transport,
                                 unsigned slot, const std::wstring& unit) {
    status.transportId = transport;
    status.transportSlot = static_cast<int>(slot);
    status.logicalIdentity = unit;  // empty means no positive unit evidence
    if (!IsMxMaster4ReportedName(status.name)) return;
    status.rawReportPolicy = true;
    if (!transport.empty()) {
        status.cacheIdentity = L"logi:" + (unit.empty() ? L"link" : L"unit:" + unit) +
                               L"|" + transport + L"|slot:" + std::to_wstring(slot);
    }
    if (status.outcome == BatteryOutcome::Unspecified || status.outcome == BatteryOutcome::Reported)
        status.outcome = status.percent >= 0 && status.percent <= 100
                             ? BatteryOutcome::Reported : BatteryOutcome::Unavailable;
}
