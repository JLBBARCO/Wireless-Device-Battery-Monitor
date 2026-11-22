#pragma once
// Resident presentation policy. No HID access, UI, clocks or persistence here.
#include "devices.h"
#include "reading_freshness.h"

namespace battery_state {

enum class State { Current, LastKnown, Unknown, Disconnected };
enum class Reason {
    None, SleepReported, NoResponse, Unavailable, QueryFailed, InvalidResponse,
    NotYetRead, ReadingAged, Disconnected
};

struct Reading {
    int percent = -1;
    std::uint64_t tick = 0;
    std::string utc;
    std::wstring identity;
    std::wstring transport;
    int slot = -1;
    std::wstring logicalIdentity;
};

struct Observation {
    bool bindingChanged = false;
    bool sample = false;
};

inline bool Confirmed(const DeviceStatus& s) {
    return reading_freshness::Confirmed(s.percent, s.connected, s.asleep) &&
           (s.outcome == BatteryOutcome::Unspecified || s.outcome == BatteryOutcome::Reported) &&
           (!s.rawReportPolicy || (s.outcome == BatteryOutcome::Reported && s.reportTick != 0));
}

inline bool SameBinding(const DeviceStatus& s, const Reading& r) {
    return !s.rawReportPolicy || (!s.cacheIdentity.empty() && !s.transportId.empty() &&
           s.cacheIdentity == r.identity && s.transportId == r.transport);
}

inline bool CanApply(const DeviceStatus& s, const Reading& r) {
    return r.identity.empty() ||
           (s.rawReportPolicy && !s.cacheIdentity.empty() && !s.transportId.empty());
}

inline Observation Observe(Reading& r, const DeviceStatus& s, std::uint64_t now) {
    Observation result;
    if (s.rawReportPolicy) {
        // An unidentified result cannot overwrite or borrow an established binding.
        if (s.cacheIdentity.empty() || s.transportId.empty()) return result;
        // A legacy estimate has no raw binding and cannot seed its first one.
        // A genuinely empty cache is still normal startup, not a replacement.
        if ((!r.identity.empty() || r.percent >= 0) && !SameBinding(s, r)) {
            r = Reading{};
            result.bindingChanged = true;
        }
        r.identity = s.cacheIdentity;
        r.transport = s.transportId;
        r.slot = s.transportSlot;
        r.logicalIdentity = s.logicalIdentity;
    }
    if (Confirmed(s)) {
        r.percent = s.percent;
        r.tick = s.rawReportPolicy ? s.reportTick : (s.reportTick ? s.reportTick : now);
        r.utc = s.reportUtc;
        result.sample = true;
    } else if (!s.retainLastPercent) {
        r.percent = -1;
        r.tick = 0;
        r.utc.clear();
    }
    return result;
}

inline bool AcceptSnapshot(std::uint64_t& applied, std::uint64_t sequence) {
    if (sequence <= applied) return false;
    applied = sequence;
    return true;
}

inline bool Removed(const Reading& r, const DevicePollSnapshot& poll) {
    return !r.transport.empty() && poll.inventoryComplete && !poll.presentTransports.count(r.transport);
}

inline BatteryOutcome MissingOutcome(const Reading& r, const DevicePollSnapshot& poll) {
    auto slot = poll.slotOutcomes.find({r.transport, r.slot});
    if (slot != poll.slotOutcomes.end()) return slot->second;
    auto found = poll.transportOutcomes.find(r.transport);
    return found == poll.transportOutcomes.end() ? BatteryOutcome::QueryFailed : found->second;
}

enum class MissingAction { KeepBound, KeepGrace, Remove, Invalidate };

inline bool SameSlot(const Reading& r, const DeviceStatus& s) {
    return r.slot >= 0 && r.slot == s.transportSlot && !r.transport.empty() && r.transport == s.transportId;
}

inline bool Superseded(const Reading& r, const DevicePollSnapshot& poll) {
    for (const auto& replacement : poll.devices)
        if (SameSlot(r, replacement) &&
            !replacement.logicalIdentity.empty() && replacement.logicalIdentity != r.logicalIdentity)
            return true;
    return false;
}

inline bool AmbiguousSlot(const DeviceStatus& s, const std::map<std::wstring, Reading>& readings) {
    if (!s.logicalIdentity.empty()) return false;
    for (const auto& item : readings)
        if (SameSlot(item.second, s) && !item.second.logicalIdentity.empty()) return true;
    return false;
}

// Only inventory evidence or an explicit exclusion invalidates an established binding.
// Legacy, unbound devices keep their existing short grace period.
inline MissingAction Missing(const DeviceStatus& s, const Reading& r, const DevicePollSnapshot& poll,
                             std::uint64_t lastSeen, std::uint64_t now, std::uint64_t pollMs,
                             bool sameNameLive) {
    if (poll.ignoredProducts.count({s.vid, s.pid}) || Superseded(r, poll)) return MissingAction::Invalidate;
    if (s.rawReportPolicy && !r.transport.empty())
        return Removed(r, poll) ? MissingAction::Invalidate : MissingAction::KeepBound;
    if (s.graceSec > 0 && s.connected && !sameNameLive &&
        reading_freshness::AgeMs(lastSeen, now) <
            std::max<std::uint64_t>(static_cast<std::uint64_t>(s.graceSec) * 1000, 2 * pollMs))
        return MissingAction::KeepGrace;
    return MissingAction::Remove;
}

// Keep the legacy charging workaround local to unopted devices. Opted-in reports
// are immutable even if an older report with the same display name was higher.
class ChargeSmoother {
    struct Idle { int percent; std::uint64_t tick; };
    std::map<std::wstring, Idle> idle_;
public:
    void Apply(DeviceStatus& s, std::uint64_t now) {
        if (s.rawReportPolicy || s.percent < 0 || s.name.empty() ||
            s.name.find(L"wireless device") != std::wstring::npos) return;
        if (!s.charging) { idle_[s.name] = {s.percent, now}; return; }
        auto it = idle_.find(s.name);
        if (it == idle_.end() || reading_freshness::AgeMs(it->second.tick, now) > 12ull * 60 * 60 * 1000) return;
        if (s.percent < it->second.percent) {
            s.percent = it->second.percent;
            s.levelText.clear();
        } else idle_.erase(it);
    }
};

inline Reason OutcomeReason(const DeviceStatus& s) {
    switch (s.outcome) {
        case BatteryOutcome::SleepReported: return Reason::SleepReported;
        case BatteryOutcome::NoResponse: return Reason::NoResponse;
        case BatteryOutcome::Unavailable: return Reason::Unavailable;
        case BatteryOutcome::QueryFailed: return Reason::QueryFailed;
        case BatteryOutcome::InvalidResponse: return Reason::InvalidResponse;
        case BatteryOutcome::NotYetRead: return Reason::NotYetRead;
        case BatteryOutcome::Disconnected: return Reason::Disconnected;
        default: break;
    }
    if (s.asleep && s.outcome == BatteryOutcome::Unspecified) return Reason::SleepReported;
    if (s.percent > 100) return Reason::InvalidResponse;
    if (s.percent < 0) return Reason::NoResponse;
    return Reason::None;
}

struct View {
    State state = State::Unknown;
    Reason reason = Reason::NotYetRead;
    int percent = -1;
    bool live = false;
    bool dim = true;
    bool charging = false;
    bool full = false;
    bool asleep = false;
    std::uint64_t readingTick = 0;
    std::string readingUtc;
};

inline View Resolve(const DeviceStatus& s, const Reading& r, std::uint64_t now,
                    std::uint64_t pollMs, bool dimHistorical) {
    View v;
    const bool bound = SameBinding(s, r);
    if (bound && r.percent >= 0) {
        v.percent = r.percent;
        v.readingTick = r.tick;
        v.readingUtc = r.utc;
    }
    const bool confirmed = Confirmed(s);
    if (confirmed) {
        v.percent = s.percent;
        v.readingTick = s.reportTick ? s.reportTick : (bound ? r.tick : 0);
        v.readingUtc = !s.reportUtc.empty() ? s.reportUtc : (bound ? r.utc : std::string{});
    }
    if (!s.connected || s.outcome == BatteryOutcome::Disconnected) {
        v.state = State::Disconnected;
        v.reason = Reason::Disconnected;
        v.percent = -1;
        return v;
    }
    const auto age = reading_freshness::Classify(v.readingTick, now, pollMs);
    v.live = confirmed && age == reading_freshness::State::Fresh;
    v.reason = OutcomeReason(s);
    if (v.live) {
        v.state = State::Current;
        v.reason = Reason::None;
        v.dim = false;
        v.charging = s.charging;
        v.full = s.full || (s.charging && v.percent == 100);
    } else if (v.percent >= 0) {
        v.state = State::LastKnown;
        v.dim = dimHistorical;
        if (v.reason == Reason::None) v.reason = Reason::ReadingAged;
    } else {
        v.state = State::Unknown;
    }
    v.asleep = v.reason == Reason::SleepReported;
    return v;
}

inline bool CanMeasure(const DeviceStatus& s, const Reading& r, const Observation& observation,
                       std::uint64_t now, std::uint64_t pollMs) {
    return observation.sample && Resolve(s, r, now, pollMs, false).live;
}

inline bool Hidden(const View& view, DeviceKind kind, bool hasReading, bool explicitlyHidden,
                   bool showDisconnected, bool hideLastKnown, bool keepMouse) {
    if (explicitlyHidden) return true;
    if (showDisconnected && view.state == State::Disconnected) return false;
    if (hideLastKnown && keepMouse && kind == DeviceKind::Mouse && hasReading) return false;
    return hideLastKnown && (view.asleep || view.state == State::LastKnown);
}

inline const char* StateId(State state) {
    switch (state) {
        case State::Current: return "current";
        case State::LastKnown: return "last_known";
        case State::Disconnected: return "disconnected";
        default: return "unknown";
    }
}

inline const char* ReasonId(Reason reason) {
    switch (reason) {
        case Reason::None: return "none";
        case Reason::SleepReported: return "sleep_reported";
        case Reason::NoResponse: return "no_response";
        case Reason::Unavailable: return "unavailable";
        case Reason::QueryFailed: return "query_failed";
        case Reason::InvalidResponse: return "invalid_response";
        case Reason::ReadingAged: return "reading_aged";
        case Reason::Disconnected: return "disconnected";
        default: return "not_yet_read";
    }
}

}  // namespace battery_state
