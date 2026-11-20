#include "../src/alert_rules.h"
#include <cassert>
#include <cstdio>
#include <map>

int main() {
    using namespace alerts;

    std::map<std::wstring, int> overrides;
    const std::wstring devKey = L"mouse:test";

    // 1. Default threshold is 20%
    assert(EffectiveThreshold(overrides, devKey) == 20);

    DeviceStatus s;
    s.key = devKey;
    s.connected = true;
    s.asleep = false;
    s.charging = false;

    DeviceAlertState state;

    // 2. Initial reading above threshold (25%): No notification
    s.percent = 25;
    assert(EvaluateDeviceAlert(s, 20, state) == AlertType::None);
    assert(!state.lowWarned);

    // 3. Battery drops to 20%: Triggers LowBattery alert
    s.percent = 20;
    assert(EvaluateDeviceAlert(s, 20, state) == AlertType::LowBattery);
    assert(state.lowWarned);

    // 4. Repeated reading at 20%: Deduplicated, no repeated notification
    assert(EvaluateDeviceAlert(s, 20, state) == AlertType::None);

    // 5. Fluctuation within threshold (e.g. 19% or 23%): Still latch locked
    s.percent = 19;
    assert(EvaluateDeviceAlert(s, 20, state) == AlertType::None);
    s.percent = 23;
    assert(EvaluateDeviceAlert(s, 20, state) == AlertType::None);

    // 6. Drops to 10%: Triggers CriticalBattery alert
    s.percent = 10;
    assert(EvaluateDeviceAlert(s, 20, state) == AlertType::CriticalBattery);
    assert(state.criticalWarned);
    assert(EvaluateDeviceAlert(s, 20, state) == AlertType::None);

    // 7. Drops to 5%: Triggers EmergencyBattery alert
    s.percent = 5;
    assert(EvaluateDeviceAlert(s, 20, state) == AlertType::EmergencyBattery);
    assert(state.emergencyWarned);
    assert(EvaluateDeviceAlert(s, 20, state) == AlertType::None);

    // 8. Device starts charging: warnings cleared
    s.charging = true;
    s.percent = 60;
    assert(EvaluateDeviceAlert(s, 20, state) == AlertType::None);
    assert(!state.lowWarned);
    assert(!state.criticalWarned);
    assert(!state.emergencyWarned);

    // 9. Fully charged: Triggers ChargingComplete alert
    s.percent = 100;
    s.full = true;
    assert(EvaluateDeviceAlert(s, 20, state) == AlertType::ChargingComplete);
    // Deduplicated: won't trigger repeatedly while full
    assert(EvaluateDeviceAlert(s, 20, state) == AlertType::None);

    // 10. Unplugged at 100%: Re-armed for discharge
    s.charging = false;
    s.full = false;
    s.percent = 95;
    assert(EvaluateDeviceAlert(s, 20, state) == AlertType::None);

    // 11. Disconnect alert
    s.connected = false;
    assert(EvaluateDeviceAlert(s, 20, state, true, true, true) == AlertType::Disconnected);
    assert(EvaluateDeviceAlert(s, 20, state, true, true, true) == AlertType::None); // Deduplicated

    // 12. Reconnect while low (15%): Already below threshold, notifies once
    s.connected = true;
    s.percent = 15;
    assert(EvaluateDeviceAlert(s, 20, state) == AlertType::LowBattery);
    assert(EvaluateDeviceAlert(s, 20, state) == AlertType::None);

    // 13. Notifications disabled for device
    overrides[devKey] = kDisabled;
    DeviceAlertState disState;
    s.percent = 10;
    assert(EvaluateDeviceAlert(s, kDisabled, disState) == AlertType::None);

    // 14. Invalid or stale readings (-1 or > 100) do not trigger
    s.percent = -1;
    assert(EvaluateDeviceAlert(s, 20, state) == AlertType::None);
    s.percent = 105;
    assert(EvaluateDeviceAlert(s, 20, state) == AlertType::None);

    std::puts("Smarter low-battery notifications tests passed");
    return 0;
}
