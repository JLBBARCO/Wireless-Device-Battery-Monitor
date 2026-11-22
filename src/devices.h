#pragma once
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

enum class DeviceKind { Other, Mouse, Headset, Keyboard, Gamepad, Phone, Microphone, Speaker };

// Query evidence is independent of a retained percentage or inferred power state.
enum class BatteryOutcome {
    Unspecified, Reported, Cached, SleepReported, NoResponse, Unavailable,
    QueryFailed, InvalidResponse, NotYetRead, Disconnected
};

// One battery-powered device, as seen during a single poll.
struct DeviceStatus {
    std::wstring key;          // stable identity across polls (used for the tray icon id)
    std::wstring name;         // human readable name
    std::wstring via;          // "2.4 GHz receiver", "Bluetooth", "USB cable", ...
    DeviceKind kind = DeviceKind::Other;
    int  percent    = -1;      // 0..100, or -1 when unknown right now
    std::wstring detail;       // extra breakdown for the tooltip, e.g. "L 80%, R 75%, case 40%"
    std::wstring levelText;    // set when the device only reports coarse levels ("Low", "Full"...)
    std::wstring note;         // advice shown while there is no level (an English phrase, translated by the UI)
    bool charging   = false;
    bool full       = false;   // on power and fully charged (charging may also be set)
    bool asleep     = false;   // receiver answered but the device is sleeping (keep last value)
    bool connected  = true;    // false: receiver present, device switched off (hidden)
    int  millivolts = 0;       // battery voltage when reported (0 = unknown)
    int  graceSec   = 0;       // keep showing the last value this long if the device stops answering
    unsigned short vid = 0;    // USB / Bluetooth HID vendor id when known (picks the brand color)
    unsigned short pid = 0;
    bool unsupported = false;  // seen but no driver reads its battery: listed in the menu only (PR #2463)
    bool retainLastPercent = true;  // allow a previous reading while this poll's percentage is unknown
    bool rawReportPolicy = false;  // opted-in paths use immutable validated reports, without charge smoothing
    BatteryOutcome outcome = BatteryOutcome::Unspecified;
    std::wstring cacheIdentity;  // logical device + transport binding; never a display name
    std::wstring transportId;    // physical container, or an unambiguous current-instance identity
    int transportSlot = -1;      // receiver slot when the protocol identifies one
    std::wstring logicalIdentity;  // positive unit identity; empty when only the link is identified
    std::uint64_t reportTick = 0;  // captured at per-report validation, not UI delivery
    std::string reportUtc;
    std::int64_t reportSeconds = 0;  // the same receipt instant, for measurement consumers
};

inline int RememberedPercent(const DeviceStatus& status, int previous) {
    if (status.connected && !status.asleep && status.percent >= 0 && status.percent <= 100 &&
        (status.outcome == BatteryOutcome::Unspecified || status.outcome == BatteryOutcome::Reported))
        return status.percent;
    return status.retainLastPercent ? previous : -1;
}

// Presence comes from enumeration, not from whether a battery query answered.
struct DevicePollSnapshot {
    std::vector<DeviceStatus> devices;
    std::set<std::wstring> presentTransports;
    std::set<std::pair<unsigned short, unsigned short>> ignoredProducts;
    std::map<std::wstring, BatteryOutcome> transportOutcomes;  // evidence for a binding omitted by its driver
    bool inventoryComplete = false;
    std::uint64_t sequence = 0;
    std::uint64_t capturedTick = 0;
};

// Queries every supported device. Blocking (can take a second or two); call from a worker thread.
std::vector<DeviceStatus> PollAllDevices();
#ifdef _WIN32
DevicePollSnapshot PollDevicesSnapshot();
#endif

// Prints every HID collection (for adding support for new devices).
std::wstring DumpHidCollections();

// Releases background driver sessions (e.g. AAP channels) cleanly before exit.
void ShutdownDevices();
