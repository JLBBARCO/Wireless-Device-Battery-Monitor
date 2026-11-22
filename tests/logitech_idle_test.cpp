// Exercise the production poller and opt-in policy without HID access or real timers.
#include "../src/drivers/drivers.h"
#include "../src/battery_state.h"
#include <cassert>
#include <cstdio>
#include <deque>
#include <map>

static ULONGLONG now = 100;
static ULONGLONG TestTickCount64() { return now; }
#define GetTickCount64 TestTickCount64
#include "../src/drivers/logitech.cpp"
#undef GetTickCount64

struct FakeMouse {
    std::string name = "MX Master 4";
    BYTE percent = 45;
    BYTE unit = 1;
    bool responding = true;
    bool unitLookupResponding = true;
    bool nameLookupResponding = true;
    bool unified = false;
    bool socSupported = true;
    bool capabilitiesResponding = true;
    BYTE coarse = 1;
    BYTE chargeStatus = 0;
    bool batteryStatusAvailable = true;
    bool estimatesAvailable = false;
    bool capabilitiesError = false;
    BYTE pingError = 0;
    bool featureWriteFails = false;
    bool featureError = false;
    bool silenceAfterFeatureFailure = false;
};
static std::map<BYTE, FakeMouse> mice;
static std::deque<std::vector<BYTE>> responses;
static bool openOk = true, writeOk = true;
static ULONGLONG lastBatteryTick;

bool HidDev::Open(const HidColl& c) {
    c_ = c;
#ifdef _WIN32
    h_ = openOk ? reinterpret_cast<HANDLE>(1) : INVALID_HANDLE_VALUE;
#else
    fd_ = openOk ? 1 : -1;
#endif
    return openOk;
}
void HidDev::Close() {
#ifdef _WIN32
    h_ = INVALID_HANDLE_VALUE;
#else
    fd_ = -1;
#endif
}
void HidDev::Flush() { responses.clear(); }
bool HidDev::Write(const std::vector<BYTE>& request, int) {
    if (!writeOk) return false;
    const BYTE slot = request[1], feature = request[2], fn = request[3] >> 4;
    auto it = mice.find(slot);
    if (it == mice.end() || !it->second.responding) return true;
    auto& mouse = it->second;
    if (feature == 0 && fn == 0 && mouse.featureWriteFails) {
        if (mouse.silenceAfterFeatureFailure) mouse.responding = false;
        return false;
    }
    std::vector<BYTE> reply(20, 0);
    std::copy(request.begin(), request.begin() + 4, reply.begin());
    if (feature == 0 && fn == 0 && mouse.featureError) {
        if (mouse.silenceAfterFeatureFailure) mouse.responding = false;
        reply[2] = 0xFF; reply[3] = feature; reply[4] = request[3]; reply[5] = 4;
        responses.push_back(reply);
        return true;
    }
    if (feature == 0 && fn == 1) {
        if (mouse.pingError) {
            reply[2] = 0xFF; reply[3] = feature; reply[4] = request[3]; reply[5] = mouse.pingError;
        } else reply[4] = 2;  // HID++ 2.0 ping
    }
    else if (feature == 0 && fn == 0) {
        const unsigned id = (request[4] << 8) | request[5];
        if (id == 0x0005 && !mouse.nameLookupResponding) return true;
        reply[4] = id == 0x0005 ? 1 : id == 0x1000 && mouse.batteryStatusAvailable ? 2 :
                   id == 0x0003 ? 3 : id == 0x1004 && mouse.unified ? 4 :
                   id == 0x1001 && mouse.estimatesAvailable ? 5 :
                   id == 0x1F20 && mouse.estimatesAvailable ? 6 : 0;
    } else if (feature == 1 && fn == 0) reply[4] = static_cast<BYTE>(mouse.name.size());
    else if (feature == 1 && fn == 1) {
        const auto chunk = mouse.name.substr(request[4], 16);
        std::copy(chunk.begin(), chunk.end(), reply.begin() + 4);
    } else if (feature == 1 && fn == 2) reply[4] = 3;  // mouse type
    else if (feature == 2 && fn == 0) {
        reply[4] = mouse.percent;
        reply[6] = mouse.chargeStatus;
        lastBatteryTick = now;
    } else if (feature == 4 && fn == 0) {
        if (!mouse.capabilitiesResponding) return true;
        if (mouse.capabilitiesError) {
            reply[2] = 0xFF; reply[3] = feature; reply[4] = request[3]; reply[5] = 4;
            responses.push_back(reply);
            return true;
        }
        reply[5] = mouse.socSupported ? 2 : 0;
    } else if (feature == 4 && fn == 1) {
        reply[4] = mouse.percent;
        reply[5] = mouse.coarse;
        reply[6] = mouse.chargeStatus;
        lastBatteryTick = now;
    } else if (feature == 3 && fn == 0) {
        if (!mouse.unitLookupResponding) return true;
        now += 30000;  // delayed unit metadata must not refresh receipt time
        reply[8] = mouse.unit;
    } else assert(false);
    responses.push_back(reply);
    return true;
}
int HidDev::Read(std::vector<BYTE>& reply, int timeoutMs) {
    if (responses.empty()) { now += timeoutMs; return -1; }
    reply = responses.front();
    responses.pop_front();
    if (reply.size() > c_.inLen()) reply.resize(c_.inLen());
    return static_cast<int>(reply.size());
}
std::wstring HidProduct(const HidColl&) { return L"Logitech USB Receiver"; }
std::wstring Hex4(USHORT value) {
    wchar_t result[5]; swprintf(result, 5, L"%04X", value); return result;
}
DeviceKind GuessKind(const std::wstring&) { return DeviceKind::Mouse; }
int Interpolate(int, const int*, const int*, int) { assert(false); return -1; }

static PollContext PollReceiver() {
    HidColl receiver;
    receiver.vid = 0x046D; receiver.pid = 0xC548;
    receiver.transportId = L"container:receiver-a";
    receiver.caps.UsagePage = 0xFF00; receiver.caps.Usage = 2;
    receiver.caps.InputReportByteLength = receiver.caps.OutputReportByteLength = 20;
    PollContext ctx;
    ctx.colls = {receiver};
    ctx.presentTransports.insert(receiver.transportId);
    ctx.inventoryComplete = true;
    PollLogitech(ctx);
    return ctx;
}

static DevicePollSnapshot Snapshot(const PollContext& ctx) {
    DevicePollSnapshot snapshot;
    snapshot.devices = ctx.out;
    snapshot.presentTransports = ctx.presentTransports;
    snapshot.inventoryComplete = ctx.inventoryComplete;
    snapshot.transportOutcomes = ctx.transportOutcomes;
    snapshot.slotOutcomes = ctx.slotOutcomes;
    return snapshot;
}

static std::vector<HidColl> ReceiverCollections(const std::wstring& instance) {
    HidColl shortColl, longColl;
    shortColl.vid = longColl.vid = 0x046D;
    shortColl.pid = longColl.pid = 0xC548;
    const std::wstring prefix = L"\\\\?\\hid#vid_046d&pid_c548&mi_02&col";
    shortColl.path = prefix + L"01#8&" + instance + L"&0&0000#{guid}";
    longColl.path = prefix + L"02#8&" + instance + L"&0&0001#{guid}";
    shortColl.caps.UsagePage = longColl.caps.UsagePage = 0xFF00;
    shortColl.caps.Usage = 1; longColl.caps.Usage = 2;
    shortColl.caps.InputReportByteLength = shortColl.caps.OutputReportByteLength = 7;
    longColl.caps.InputReportByteLength = longColl.caps.OutputReportByteLength = 20;
    shortColl.transportId = L"path:" + hid_inventory::Lower(shortColl.path);
    longColl.transportId = L"path:" + hid_inventory::Lower(longColl.path);
    return {shortColl, longColl};
}

static PollContext PollCollections(const std::vector<HidColl>& collections) {
    PollContext ctx;
    ctx.colls = collections;
    for (const auto& c : collections) ctx.presentTransports.insert(c.transportId);
    PollLogitech(ctx);
    return ctx;
}

static void TestPhysicalGrouping() {
    mice = {{1, FakeMouse{}}};
    auto channels = ReceiverCollections(L"receiver-a");
    const auto fallback = PollCollections(channels);
    assert(fallback.out.size() == 1 && fallback.out[0].key == L"logi:unit:00000001");
    assert(fallback.out[0].transportId == channels[1].transportId);
    assert(PhysicalKey(channels[0], channels) == PhysicalKey(channels[1], channels));

    const auto container = std::wstring(L"container:receiver-a");
    channels[0].transportId = channels[1].transportId = container;
    const auto known = PollCollections(channels).out.at(0);
    for (const int missing : {0, 1}) {
        auto mixed = channels;
        mixed[missing].transportId = L"path:" + hid_inventory::Lower(mixed[missing].path);
        const auto poll = PollCollections(mixed);
        assert(poll.out.size() == 1 && poll.out[0].transportId == container);
        assert(poll.out[0].cacheIdentity == known.cacheIdentity);
        assert(poll.slotOutcomes.count({container, 1}) == 1);
        battery_state::Reading reading;
        battery_state::Observe(reading, known, now);
        assert(!battery_state::Observe(reading, poll.out[0], now).bindingChanged);
        mice[1].responding = false;
        const auto idle = PollCollections(mixed);
        assert(idle.out.empty() && idle.transportOutcomes.count(container));
        assert(battery_state::MissingOutcome(reading, Snapshot(idle)) == BatteryOutcome::NoResponse);
        mice[1].responding = true;
    }

    // Known containers continue to group collections on different interfaces.
    auto crossInterface = channels;
    const auto mi = crossInterface[1].path.find(L"mi_02");
    assert(mi != std::wstring::npos);
    crossInterface[1].path.replace(mi, 5, L"mi_03");
    assert(PollCollections(crossInterface).out.size() == 1);

    // A second identical receiver stays separate, independent of enumeration order.
    auto both = ReceiverCollections(L"receiver-b");
    both.insert(both.end(), channels.begin(), channels.end());
    const auto first = PollCollections(both);
    assert(first.out.size() == 2 && first.out[0].cacheIdentity != first.out[1].cacheIdentity);
    std::reverse(both.begin(), both.end());
    const auto reversed = PollCollections(both);
    assert(reversed.out.size() == 2);
    std::set<std::wstring> bindings;
    for (const auto& s : first.out) bindings.insert(s.cacheIdentity);
    for (const auto& s : reversed.out) assert(bindings.count(s.cacheIdentity));

    // No container can be inferred from an unparseable path or conflicting evidence.
    auto conflict = channels;
    conflict[1].transportId = L"container:receiver-b";
    auto extra = channels[0];
    extra.transportId = L"path:" + hid_inventory::Lower(extra.path);
    conflict.push_back(extra);
    const auto ambiguous = PhysicalKey(extra, conflict);
    assert(ambiguous != conflict[0].transportId && ambiguous != conflict[1].transportId);
    extra.path = L"unparseable";
    extra.transportId = L"path:unparseable";
    assert(PhysicalKey(extra, conflict) == extra.transportId);
    extra.transportId.clear();
    assert(PhysicalKey(extra, conflict).empty());  // preserve legacy non-Windows behavior
}

static void TestSlotFailures() {
    using namespace battery_state;
    mice = {{1, FakeMouse{}}, {2, FakeMouse{"MX Master 4", 73, 2, true}}};
    const auto first = PollReceiver();
    assert(first.out.size() == 2);
    Reading one, two;
    Observe(one, first.out[0], now);
    Observe(two, first.out[1], now);
    mice[1].pingError = 4;
    mice[2].responding = false;
    const auto failed = Snapshot(PollReceiver());
    assert(failed.devices.empty());
    assert(MissingOutcome(one, failed) == BatteryOutcome::QueryFailed);
    assert(MissingOutcome(two, failed) == BatteryOutcome::NoResponse);
    for (const auto* reading : {&one, &two}) {
        auto status = first.out[reading == &one ? 0 : 1];
        status.percent = -1;
        status.outcome = MissingOutcome(*reading, failed);
        const auto view = Resolve(status, *reading, now, 60000, true);
        assert(view.state == State::LastKnown && view.percent == reading->percent);
        assert(view.reason == (reading == &one ? Reason::QueryFailed : Reason::NoResponse));
    }
    mice[1].pingError = 0;
    // A valid ping followed by failed feature queries must preserve the error,
    // even if a later request overwrites Hidpp's most recent request outcome.
    for (const bool protocolError : {false, true}) {
        for (const bool thenSilent : {false, true}) {
            mice[1].responding = true;
            mice[1].featureWriteFails = !protocolError;
            mice[1].featureError = protocolError;
            mice[1].silenceAfterFeatureFailure = thenSilent;
            const auto failedFollowup = Snapshot(PollReceiver());
            assert(failedFollowup.devices.empty());
            assert(MissingOutcome(one, failedFollowup) == BatteryOutcome::QueryFailed);
            assert(MissingOutcome(two, failedFollowup) == BatteryOutcome::NoResponse);
        }
    }
    mice[1].featureWriteFails = mice[1].featureError = false;
    mice[1].silenceAfterFeatureFailure = false;
    mice[1].responding = true;
    mice[2].responding = true;
    const auto recovered = PollReceiver();
    assert(recovered.out.size() == 2 && recovered.out[0].percent == 45 && recovered.out[1].percent == 73);
}

static void TestLegacyToRawBinding() {
    using namespace battery_state;
    mice = {{1, FakeMouse{}}};
    mice[1].unified = true;
    mice[1].percent = 0;
    mice[1].nameLookupResponding = false;
    const auto legacy = PollReceiver().out.at(0);
    assert(!legacy.rawReportPolicy && legacy.percent == 5);
    Reading reading;
    assert(Observe(reading, legacy, now).sample && reading.identity.empty());
    mice[1].nameLookupResponding = true;
    mice[1].capabilitiesResponding = false;
    const auto identified = PollReceiver().out.at(0);
    assert(identified.key == legacy.key && identified.rawReportPolicy && identified.percent == -1);
    const auto observation = Observe(reading, identified, now);
    assert(observation.bindingChanged && !observation.sample);
    assert(Resolve(identified, reading, now, 60000, true).state == State::Unknown);
    assert(reading.percent == -1 && !reading.tick && reading.utc.empty());
    mice[1].capabilitiesResponding = true;
    const auto fresh = PollReceiver().out.at(0);
    assert(fresh.percent == 0 && Observe(reading, fresh, now).sample);
    assert(Resolve(fresh, reading, now, 60000, true).live);
}

static void TestProductionPoller() {
    now = 100;
    mice = {{1, FakeMouse{}}};
    auto fresh = PollReceiver();
    assert(fresh.out.size() == 1);
    const auto first = fresh.out[0];
    assert(first.rawReportPolicy && first.outcome == BatteryOutcome::Reported);
    assert(first.percent == 45 && first.reportTick == lastBatteryTick);
    assert(now >= first.reportTick + 30000 && first.reportUtc.size() == 20);
    assert(first.vid == 0x046D && first.pid == 0xC548);
    assert(first.transportSlot == 1 && first.logicalIdentity == L"00000001");

    mice[1].responding = false;
    auto idle = PollReceiver();
    assert(idle.out.empty() && idle.presentTransports.count(first.transportId));
    assert(idle.transportOutcomes.at(first.transportId) == BatteryOutcome::NoResponse);
    openOk = false;
    auto cannotOpen = PollReceiver();
    assert(cannotOpen.out.empty() && cannotOpen.presentTransports.count(first.transportId));
    assert(cannotOpen.transportOutcomes.at(first.transportId) == BatteryOutcome::QueryFailed);
    openOk = true; writeOk = false;
    auto cannotWrite = PollReceiver();
    assert(cannotWrite.out.empty());
    assert(cannotWrite.transportOutcomes.at(first.transportId) == BatteryOutcome::QueryFailed);
    writeOk = true;

    mice[1].responding = true;
    mice[1].percent = 0;
    mice[2] = FakeMouse{"MX Master 4", 73, 2, true};
    auto paired = PollReceiver();
    assert(paired.out.size() == 2 && paired.out[0].percent == 0);
    assert(paired.out[0].outcome == BatteryOutcome::Reported);
    assert(paired.out[0].cacheIdentity == first.cacheIdentity);
    assert(paired.out[0].cacheIdentity != paired.out[1].cacheIdentity);
    assert(paired.out[0].key != paired.out[1].key);
    const auto second = paired.out[1];
    assert(second.transportSlot == 2 && second.logicalIdentity == L"00000002");
    mice[1].responding = false;
    auto oneIdle = PollReceiver();
    assert(oneIdle.out.size() == 1 && oneIdle.out[0].key == second.key);
    assert(oneIdle.out[0].cacheIdentity == second.cacheIdentity);
    assert(oneIdle.transportOutcomes.at(first.transportId) == BatteryOutcome::NoResponse);
    mice[2].unit = 3;
    auto replaced = PollReceiver();
    assert(replaced.out[0].cacheIdentity != second.cacheIdentity);
    assert(replaced.out[0].transportSlot == second.transportSlot);
    assert(replaced.out[0].logicalIdentity == L"00000003");
    mice[2].unitLookupResponding = false;
    auto unknownUnit = PollReceiver();
    assert(unknownUnit.out.size() == 1 && unknownUnit.out[0].transportSlot == 2);
    assert(unknownUnit.out[0].logicalIdentity.empty());
    assert(unknownUnit.out[0].rawReportPolicy && unknownUnit.out[0].percent == 73);
    assert(unknownUnit.out[0].cacheIdentity == L"logi:link|container:receiver-a|slot:2");
    mice = {{1, FakeMouse{"MX Master 4", 101, 1, true}}};
    auto unavailable = PollReceiver();
    assert(unavailable.out.size() == 1 && unavailable.out[0].percent == -1);
    assert(unavailable.out[0].outcome == BatteryOutcome::InvalidResponse);
    assert(unavailable.out[0].reportTick == 0 && !unavailable.out[0].asleep);
    mice = {{1, FakeMouse{"G502 X", 50, 1, true}}};
    auto legacy = PollReceiver();
    assert(legacy.out.size() == 1 && !legacy.out[0].rawReportPolicy);
    assert(legacy.out[0].outcome == BatteryOutcome::Unspecified);
    assert(legacy.out[0].transportSlot == 1 && legacy.out[0].logicalIdentity == L"00000001");
    mice[1].name = "MX Master 4 clone";
    assert(!PollReceiver().out[0].rawReportPolicy);
    mice[1].name = "mx master 4";
    assert(PollReceiver().out[0].rawReportPolicy);

    mice = {{1, FakeMouse{}}};
    mice[1].nameLookupResponding = false;
    auto unnamed = PollReceiver();
    assert(unnamed.out.size() == 1 && !unnamed.out[0].rawReportPolicy);
    assert(unnamed.out[0].key == first.key && unnamed.out[0].percent == first.percent);
    assert(unnamed.out[0].transportSlot == 1 && unnamed.out[0].logicalIdentity == first.logicalIdentity);
    assert(unnamed.out[0].transportId == first.transportId);

    mice = {{1, FakeMouse{}}};
    mice[1].unified = true; mice[1].percent = 0;
    auto zeroSoc = PollReceiver();
    assert(zeroSoc.out[0].percent == 0 && zeroSoc.out[0].outcome == BatteryOutcome::Reported);
    assert(zeroSoc.out[0].reportTick == lastBatteryTick);
    mice[1].socSupported = false;
    auto noSoc = PollReceiver();
    assert(noSoc.out[0].percent == -1 && noSoc.out[0].outcome == BatteryOutcome::Unavailable);
    assert(noSoc.out[0].reportTick == 0);
    mice[1].socSupported = true; mice[1].capabilitiesResponding = false;
    auto missingCapabilities = PollReceiver();
    assert(missingCapabilities.out[0].percent == -1);
    assert(missingCapabilities.out[0].outcome == BatteryOutcome::NoResponse);
    mice[1].capabilitiesResponding = true; mice[1].capabilitiesError = true;
    assert(PollReceiver().out[0].outcome == BatteryOutcome::QueryFailed);
    mice[1].capabilitiesError = false;
    mice[1].capabilitiesResponding = true; mice[1].percent = 101;
    auto invalidSoc = PollReceiver();
    assert(invalidSoc.out[0].percent == -1 && invalidSoc.out[0].outcome == BatteryOutcome::InvalidResponse);
    mice[1].name = "G502 X"; mice[1].percent = 0;
    auto coarse = PollReceiver();
    assert(!coarse.out[0].rawReportPolicy && coarse.out[0].percent == 5);
    mice[1].name = "MX Master 4"; mice[1].unified = false; mice[1].chargeStatus = 3;
    auto zeroFull = PollReceiver();
    assert(zeroFull.out[0].percent == 0 && zeroFull.out[0].full);
    mice[1].batteryStatusAvailable = false; mice[1].estimatesAvailable = true;
    auto estimatesOnly = PollReceiver();
    assert(estimatesOnly.out[0].rawReportPolicy && estimatesOnly.out[0].percent == -1);
    assert(estimatesOnly.out[0].outcome == BatteryOutcome::Unavailable);
}

int main() {
    assert(IsMxMaster4ReportedName(L"MX Master 4"));
    assert(IsMxMaster4ReportedName(L"  LOGITECH  MX MASTER 4  "));
    assert(!IsMxMaster4ReportedName(L"MX Master 3S"));
    assert(!IsMxMaster4ReportedName(L"MX Master 4 clone"));
    assert(!IsMxMaster4ReportedName(L"Logitech G502 X"));

    DeviceStatus mouse;
    mouse.name = L"MX Master 4";
    mouse.percent = 45;
    StampBatteryReport(mouse);
    const auto received = mouse.reportTick;
    const auto utc = mouse.reportUtc;
    now += 30000;  // unit-ID follow-up or delivery cannot make the report younger
    ApplyMxMaster4Policy(mouse, L"container:receiver-a", 1, L"12345678");
    assert(mouse.rawReportPolicy && mouse.outcome == BatteryOutcome::Reported);
    assert(mouse.reportTick == received && mouse.reportUtc == utc && utc.size() == 20);
    assert(mouse.percent == 45 && mouse.transportId == L"container:receiver-a");

    const auto firstBinding = mouse.cacheIdentity;
    ApplyMxMaster4Policy(mouse, L"container:receiver-a", 1, L"87654321");
    assert(mouse.cacheIdentity != firstBinding);
    ApplyMxMaster4Policy(mouse, L"container:receiver-b", 1, L"12345678");
    assert(mouse.cacheIdentity != firstBinding);
    ApplyMxMaster4Policy(mouse, L"container:receiver-a", 2, L"12345678");
    assert(mouse.cacheIdentity != firstBinding);
    assert(mouse.transportSlot == 2 && mouse.logicalIdentity == L"12345678");
    ApplyMxMaster4Policy(mouse, L"container:receiver-a", 2, L"");
    assert(mouse.logicalIdentity.empty());  // lookup failure cannot reuse positive unit evidence

    mouse.percent = 0;
    ApplyMxMaster4Policy(mouse, L"container:receiver-a", 1, L"12345678");
    assert(mouse.outcome == BatteryOutcome::Reported && mouse.percent == 0);
    mouse.percent = -1;
    ApplyMxMaster4Policy(mouse, L"container:receiver-a", 1, L"12345678");
    assert(mouse.outcome == BatteryOutcome::Unavailable && !mouse.asleep);

    DeviceStatus unbound;
    unbound.name = L"MX Master 4";
    unbound.percent = 73;
    ApplyMxMaster4Policy(unbound, L"", 1, L"12345678");
    assert(unbound.rawReportPolicy && unbound.cacheIdentity.empty());

    DeviceStatus legacy;
    legacy.name = L"Logitech G502 X";
    legacy.percent = 50;
    ApplyMxMaster4Policy(legacy, L"container:receiver-a", 1, L"12345678");
    assert(!legacy.rawReportPolicy && legacy.cacheIdentity.empty());
    assert(legacy.outcome == BatteryOutcome::Unspecified && legacy.reportTick == 0);
    TestProductionPoller();
    TestPhysicalGrouping();
    TestSlotFailures();
    TestLegacyToRawBinding();
    std::puts("Logitech idle policy and production poller tests passed");
}
