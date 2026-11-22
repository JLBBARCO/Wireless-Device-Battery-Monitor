// Production resident-state policy with deterministic clocks; no HID or persistence.
#include "../src/battery_state.h"
#include "../src/alert_rules.h"
#include "../src/charge_estimate.h"

#include <cassert>
#include <cstdio>

using namespace battery_state;

DeviceStatus Report(int percent, std::uint64_t tick = 1000) {
    DeviceStatus s;
    s.key = L"sonix:388D:0002";
    s.name = L"Lofree Flow 2";
    s.kind = DeviceKind::Keyboard;
    s.vid = 0x388d;
    s.pid = 2;
    s.graceSec = 90;
    s.rawReportPolicy = true;
    s.outcome = BatteryOutcome::Reported;
    s.cacheIdentity = L"flow2|container:receiver-a";
    s.transportId = L"container:receiver-a";
    s.percent = percent;
    s.reportTick = tick;
    s.reportUtc = "2026-11-22T09:00:00Z";
    s.reportSeconds = 1795338000;
    return s;
}

View Apply(Reading& reading, const DeviceStatus& s, std::uint64_t now, bool dim = true) {
    Observe(reading, s, now);
    return Resolve(s, reading, now, 30000, dim);
}

int main() {
    // A first query pending, silence, explicit sleep and unavailable are distinct.
    for (const auto outcome : {BatteryOutcome::NotYetRead, BatteryOutcome::NoResponse,
                               BatteryOutcome::SleepReported, BatteryOutcome::Unavailable,
                               BatteryOutcome::QueryFailed, BatteryOutcome::InvalidResponse}) {
        Reading r;
        auto s = Report(-1);
        s.outcome = outcome;
        auto v = Apply(r, s, 2000);
        assert(v.state == State::Unknown && v.percent == -1 && !v.live && !v.charging);
        assert(v.reason == OutcomeReason(s));
        assert(v.asleep == (outcome == BatteryOutcome::SleepReported));
        assert(r.tick == 0 && r.utc.empty());
    }

    // Every historical reason keeps its original valid report, including 0 and 100.
    for (int percent : {0, 73, 100}) {
        Reading r;
        auto s = Report(percent);
        assert(Apply(r, s, 1500).state == State::Current);
        assert(r.tick == 1000 && r.percent == percent);
        for (const auto outcome : {BatteryOutcome::NoResponse, BatteryOutcome::SleepReported,
                                   BatteryOutcome::Unavailable, BatteryOutcome::QueryFailed,
                                   BatteryOutcome::InvalidResponse, BatteryOutcome::Cached}) {
            auto missing = s;
            missing.percent = -1;
            missing.outcome = outcome;
            missing.charging = missing.full = true;  // unsupported flags cannot leak
            auto v = Apply(r, missing, 950000);
            assert(v.state == State::LastKnown && v.percent == percent && !v.live && v.dim);
            assert(!v.charging && !v.full && r.tick == 1000 && r.utc == s.reportUtc);
            assert(!Resolve(missing, r, 950000, 30000, false).dim);
        }
        s.reportTick = 1000000;
        s.reportUtc = "2026-11-22T09:16:39Z";
        assert(Apply(r, s, 1000500).live && r.tick == 1000000 && r.percent == percent);
    }

    // Age-only transitions are presentation, not invented failed query evidence.
    Reading r;
    auto s = Report(73);
    Apply(r, s, 2000);
    auto aged = Resolve(s, r, 181000, 30000, true);
    assert(aged.state == State::LastKnown && aged.reason == Reason::ReadingAged);
    assert(s.outcome == BatteryOutcome::Reported && r.tick == 1000);
    assert(Resolve(s, r, 86401000, 30000, true).percent == 73);
    // Longer polling intervals keep the existing age thresholds.
    assert(Resolve(s, r, 181000, 600000, true).live);
    // Receipt timestamps survive other slow driver polls and late UI delivery.
    assert(!Apply(r, s, 900000).live && r.tick == 1000);
    assert(!CanMeasure(s, r, Observe(r, s, 900000), 900000, 30000));
    const auto ageBefore = reading_freshness::AgeMs(r.tick, 900000);
    s.reportUtc = "2020-01-01T00:00:00Z";  // changed wall clock does not change elapsed age
    assert(reading_freshness::AgeMs(r.tick, 900000) == ageBefore);
    assert(Resolve(s, r, 900000, 30000, true).reason == Reason::ReadingAged);

    // Missing status beyond grace retains a bound cache, even with another name match.
    DevicePollSnapshot poll;
    poll.inventoryComplete = true;
    poll.presentTransports.insert(s.transportId);
    poll.transportOutcomes[s.transportId] = BatteryOutcome::NoResponse;
    assert(Missing(s, r, poll, 1000, 86401000, 30000, true) == MissingAction::KeepBound);
    assert(MissingOutcome(r, poll) == BatteryOutcome::NoResponse);
    poll.presentTransports.clear();
    poll.inventoryComplete = false;
    assert(Missing(s, r, poll, 1000, 86401000, 30000, false) == MissingAction::KeepBound);
    poll.transportOutcomes.clear();
    assert(MissingOutcome(r, poll) == BatteryOutcome::QueryFailed);
    poll.inventoryComplete = true;
    assert(Missing(s, r, poll, 1000, 86401000, 30000, false) == MissingAction::Invalidate);
    r = Reading{};  // production invalidates the record on that action
    auto reconnect = Report(-1);
    reconnect.outcome = BatteryOutcome::NoResponse;
    assert(Apply(r, reconnect, 90000000).state == State::Unknown);
    poll.inventoryComplete = false;
    poll.ignoredProducts.insert({s.vid, s.pid});
    assert(Missing(s, r, poll, 1000, 2000, 30000, false) == MissingAction::Invalidate);

    // Partial identity cannot attach a new result to a trusted receiver record.
    r = Reading{};
    s = Report(73);
    Apply(r, s, 1500);
    auto unidentified = s;
    unidentified.cacheIdentity.clear();
    unidentified.transportId.clear();
    unidentified.percent = 45;
    assert(!CanApply(unidentified, r));
    assert(!Observe(r, unidentified, 2500).sample && r.percent == 73);
    unidentified.outcome = BatteryOutcome::NoResponse;
    unidentified.percent = -1;
    assert(Resolve(unidentified, r, 2500, 30000, true).percent == -1);
    auto modelLookupFailed = s;
    modelLookupFailed.rawReportPolicy = false;
    modelLookupFailed.cacheIdentity.clear();
    modelLookupFailed.percent = 45;
    assert(!CanApply(modelLookupFailed, r));  // same key may not silently lose its opt-in binding

    // Same public key/name does not transfer readings to another receiver, slot or cable.
    for (const auto identity : {L"flow2|receiver-b", L"logi:unit:b|slot:2", L"cable:a"}) {
        auto replacement = Report(-1);
        replacement.cacheIdentity = identity;
        replacement.transportId = identity;
        replacement.outcome = BatteryOutcome::NoResponse;
        const auto changed = Observe(r, replacement, 3500);
        assert(changed.bindingChanged && !changed.sample && r.percent == -1 && !r.tick);
        assert(Resolve(replacement, r, 3500, 30000, true).state == State::Unknown);
    }

    // Older posted results cannot resurrect a binding after confirmed removal.
    // Logitech unit replacement changes its public key. Slot evidence must
    // invalidate the old key even while the physical receiver stays present.
    auto oldUnit = Report(73);
    oldUnit.key = L"logi:unit-a";
    oldUnit.transportSlot = 1;
    oldUnit.logicalIdentity = L"unit-a";
    oldUnit.cacheIdentity = L"logi:unit-a|receiver-a|slot:1";
    Reading oldReading;
    Apply(oldReading, oldUnit, 1500);
    auto newUnit = oldUnit;
    newUnit.key = L"logi:unit-b";
    newUnit.logicalIdentity = L"unit-b";
    newUnit.cacheIdentity = L"logi:unit-b|receiver-a|slot:1";
    newUnit.percent = 45;
    poll.ignoredProducts.clear();
    poll.presentTransports.insert(oldUnit.transportId);
    poll.devices = {newUnit};
    assert(Missing(oldUnit, oldReading, poll, 1000, 2000, 30000, false) == MissingAction::Invalidate);
    poll.devices.front().rawReportPolicy = false;  // positive different unit, another model
    assert(Superseded(oldReading, poll));
    newUnit.transportSlot = 2;
    poll.devices = {newUnit};
    assert(!Superseded(oldReading, poll));
    newUnit.transportSlot = 1;
    newUnit.logicalIdentity.clear();  // failed unit lookup is not replacement evidence
    poll.devices = {newUnit};
    assert(!Superseded(oldReading, poll));
    std::map<std::wstring, Reading> records{{oldUnit.key, oldReading}};
    assert(AmbiguousSlot(newUnit, records));
    newUnit.transportSlot = 2;
    assert(!AmbiguousSlot(newUnit, records));

    // Older posted results cannot resurrect a binding after confirmed removal.
    std::uint64_t sequence = 0;
    assert(AcceptSnapshot(sequence, 10));
    assert(AcceptSnapshot(sequence, 12));
    assert(!AcceptSnapshot(sequence, 11) && !AcceptSnapshot(sequence, 12));

    // Only newly observed current evidence may drive measurement effects. Repeated
    // UI resolution never changes the cache or re-arms an existing low alert.
    r = Reading{};
    s = Report(10);
    bool warned = false;
    auto observation = Observe(r, s, 1500);
    assert(CanMeasure(s, r, observation, 1500, 30000));
    assert(alerts::ShouldNotify(s, 15, warned));
    auto missing = s;
    missing.percent = -1;
    missing.outcome = BatteryOutcome::NoResponse;
    assert(!CanMeasure(missing, r, Observe(r, missing, 2000), 2000, 30000) && warned);
    for (std::uint64_t now : {2500, 61000, 181000, 10000000}) {
        assert(Resolve(missing, r, now, 30000, true).percent == 10);
        assert(r.tick == 1000 && warned);
    }
    s.reportTick = 10001000;
    Apply(r, s, 10002000);
    assert(!alerts::ShouldNotify(s, 15, warned) && warned);
    s.charging = true;
    charge::Tracker tracker;
    charge::Observe(tracker, true, s.percent, s.reportSeconds);
    const auto before = tracker;
    assert(!Observe(r, missing, 10003000).sample);
    assert(tracker.startTime == before.startTime && tracker.lastPercent == before.lastPercent);

    // The exact smoothing implementation called by the app cannot overwrite raw
    // reports; unopted G502 X retains its existing workaround and expiry.
    ChargeSmoother smoothing;
    auto legacy = Report(72);
    legacy.rawReportPolicy = false;
    legacy.name = L"Logitech G502 X";
    smoothing.Apply(legacy, 1000);
    legacy.percent = 45;
    legacy.charging = true;
    smoothing.Apply(legacy, 2000);
    assert(legacy.percent == 72);
    auto raw = legacy;
    raw.rawReportPolicy = true;
    raw.percent = 45;
    raw.reportTick = 2000;
    smoothing.Apply(raw, 3000);
    Reading rawReading;
    assert(Apply(rawReading, raw, 3000).percent == 45 && rawReading.percent == 45);
    legacy.percent = 45;
    smoothing.Apply(legacy, 12ull * 60 * 60 * 1000 + 1001);
    assert(legacy.percent == 45);

    // Explicit retention restrictions and explicit disconnect still win.
    auto restricted = Report(-1);
    restricted.retainLastPercent = false;
    restricted.outcome = BatteryOutcome::Unavailable;
    assert(Apply(rawReading, restricted, 4000).percent == -1);
    s.connected = false;
    auto offline = Resolve(s, r, 10002000, 30000, false);
    assert(offline.state == State::Disconnected && offline.percent == -1 && offline.dim && !offline.live);
    assert(Reading{}.percent == -1);  // session restart never loads a historical measurement
    // Hiding is independent from dimming and is shared by tray, widget and export.
    View historical;
    historical.state = State::LastKnown;
    historical.percent = 73;
    for (bool dim : {false, true}) {
        historical.dim = dim;
        assert(Hidden(historical, DeviceKind::Keyboard, true, false, false, true, false));
        assert(!Hidden(historical, DeviceKind::Keyboard, true, false, false, false, false));
        assert(!Hidden(historical, DeviceKind::Mouse, true, false, false, true, true));
        assert(Hidden(historical, DeviceKind::Mouse, true, true, false, true, true));
        assert(Hidden(historical, DeviceKind::Mouse, true, false, false, true, false));
    }
    View unknownSleep;
    unknownSleep.asleep = true;
    assert(Hidden(unknownSleep, DeviceKind::Keyboard, false, false, false, true, false));
    assert(!Hidden(offline, DeviceKind::Keyboard, true, false, true, true, false));
    assert(Hidden(offline, DeviceKind::Keyboard, true, true, true, true, false));
    std::puts("Battery state production policy tests passed");
}
