// Exercise the production poller and opt-in policy without HID access or real timers.
#include "../src/drivers/drivers.h"
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
    const auto& mouse = it->second;
    std::vector<BYTE> reply(20, 0);
    std::copy(request.begin(), request.begin() + 4, reply.begin());
    if (feature == 0 && fn == 1) reply[4] = 2;  // HID++ 2.0 ping
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
    std::puts("Logitech idle policy and production poller tests passed");
}
