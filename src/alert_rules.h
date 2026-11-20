#pragma once
// Pure, portable policy for per-device low-battery notifications.
// No HID access, Windows dependencies, timers or disk/network writes.
#include "devices.h"

#include <map>
#include <string>

namespace alerts {
constexpr int kDefaultThreshold = 20;
constexpr int kUseDefault = -2;   // no per-device override persisted
constexpr int kDisabled = -1;     // notifications disabled for this device
constexpr int kThresholds[] = {5, 10, 15, 20, 25, 30, 35, 40};
constexpr int kThresholdCount = sizeof(kThresholds) / sizeof(kThresholds[0]);

enum class AlertType {
    None,
    LowBattery,        // at or below threshold (e.g. 20%)
    CriticalBattery,   // at or below 10%
    EmergencyBattery,  // at or below 5%
    ChargingComplete,  // fully charged (100% or full)
    Disconnected       // device link lost
};

struct DeviceAlertState {
    bool lowWarned = false;
    bool criticalWarned = false;
    bool emergencyWarned = false;
    bool chargingCompleteWarned = false;
    bool disconnectedWarned = false;
    bool wasConnected = true;
    bool wasCharging = false;
};

inline bool ValidOverride(int value) {
    if (value == kDisabled) return true;
    for (int threshold : kThresholds)
        if (value == threshold) return true;
    return false;
}

inline int OverrideFor(const std::map<std::wstring, int>& overrides, const std::wstring& key) {
    const auto it = overrides.find(key);
    return it == overrides.end() ? kUseDefault : it->second;
}

inline int EffectiveThreshold(const std::map<std::wstring, int>& overrides, const std::wstring& key) {
    int value = OverrideFor(overrides, key);
    return value == kUseDefault ? kDefaultThreshold : value;
}

// Evaluates comprehensive multi-tier alerts
inline AlertType EvaluateDeviceAlert(const DeviceStatus& s, int threshold, DeviceAlertState& st,
                                     bool criticalAlerts = true,
                                     bool chargingCompleteAlerts = true,
                                     bool disconnectAlerts = false) {
    if (threshold == kDisabled) {
        st.lowWarned = false;
        st.criticalWarned = false;
        st.emergencyWarned = false;
        return AlertType::None;
    }

    // Disconnect event check
    if (!s.connected) {
        if (st.wasConnected && disconnectAlerts && !st.disconnectedWarned) {
            st.disconnectedWarned = true;
            st.wasConnected = false;
            return AlertType::Disconnected;
        }
        st.wasConnected = false;
        return AlertType::None;
    }
    st.wasConnected = true;
    st.disconnectedWarned = false;

    // Unknown or sleeping readings never trigger or clear alerts
    if (s.asleep || s.percent < 0 || s.percent > 100) return AlertType::None;

    // Charging state handling
    if (s.charging) {
        st.lowWarned = false;
        st.criticalWarned = false;
        st.emergencyWarned = false;
        if (chargingCompleteAlerts && (s.full || s.percent >= 100) && !st.chargingCompleteWarned) {
            st.chargingCompleteWarned = true;
            return AlertType::ChargingComplete;
        }
        return AlertType::None;
    }

    // Reset charging complete warning when unplugged
    st.chargingCompleteWarned = false;

    // Hysteresis: re-arm only when battery recovers > threshold + 10
    if (s.percent > threshold + 10) {
        st.lowWarned = false;
        st.criticalWarned = false;
        st.emergencyWarned = false;
        return AlertType::None;
    }

    // Emergency 5% warning
    if (criticalAlerts && s.percent <= 5 && !st.emergencyWarned) {
        st.emergencyWarned = true;
        st.criticalWarned = true;
        st.lowWarned = true;
        return AlertType::EmergencyBattery;
    }

    // Critical 10% warning (if threshold is > 10%)
    if (criticalAlerts && threshold > 10 && s.percent <= 10 && !st.criticalWarned) {
        st.criticalWarned = true;
        st.lowWarned = true;
        return AlertType::CriticalBattery;
    }

    // Standard low battery warning at threshold
    if (s.percent <= threshold && !st.lowWarned) {
        st.lowWarned = true;
        return AlertType::LowBattery;
    }

    return AlertType::None;
}

// Backward-compatible single-latch helper
// This is invoked only for a fresh poll result. Unknown, sleeping and disconnected
// readings don't reset the latch: a transient missing report must not spam alerts.
inline bool ShouldNotify(const DeviceStatus& s, int threshold, bool& warned) {
    if (threshold == kDisabled) { warned = false; return false; }
    if (!s.connected || s.asleep || s.percent < 0 || s.percent > 100) return false;
    if (s.charging) { warned = false; return false; }
    if (s.percent > threshold + 10) { warned = false; return false; }
    if (s.percent <= threshold && !warned) {
        warned = true;
        return true;
    }
    return false;
}
}  // namespace alerts
