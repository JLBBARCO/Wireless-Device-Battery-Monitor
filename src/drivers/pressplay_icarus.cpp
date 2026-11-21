// Press Play ICARUS V2 Nearlink (7768:6434) and an EXPERIMENTAL probe for
// AJAZZ AJ159NL P NearLink (7768:6425).
//
// The 06 05 query and [06 05 26 ... battery%] reply are documented for ICARUS V2.
// AJAZZ exposes the same FFB2:0001 64-byte HID collection, but its command/reply
// protocol has NOT yet been confirmed on hardware. For AJAZZ we therefore send
// only this read-only battery query and accept a result only if the complete
// ICARUS reply signature is present. A non-matching response is ignored.
#include "drivers.h"

namespace {

constexpr USHORT kVid = 0x7768;
constexpr USHORT kIcarusPid = 0x6434;
constexpr USHORT kAjazzPid = 0x6425;
constexpr USHORT kUsagePage = 0xFFB2;
constexpr USHORT kUsage = 0x0001;
constexpr size_t kReportLength = 64;
constexpr BYTE kReportId = 0x06;
constexpr BYTE kBatteryCommand = 0x05;
constexpr BYTE kReplyMarker = 0x26;
constexpr size_t kPercentOffset = 13;

bool QueryBattery(const HidColl& c, int& percent) {
    HidDev d;
    if (!d.Open(c)) return false;
    d.Flush();

    std::vector<BYTE> request(c.outLen(), 0), reply;
    request[0] = kReportId;
    request[1] = kBatteryCommand;
    int n = d.SetOutputAndRead(request, reply, 500);
    if (n < (int)(kPercentOffset + 1) || reply.size() < kPercentOffset + 1 || reply[0] != kReportId ||
        reply[1] != kBatteryCommand || reply[2] != kReplyMarker || reply[kPercentOffset] > 100)
        return false;

    percent = reply[kPercentOffset];
    return true;
}

void PollNearlinkProduct(PollContext& ctx, USHORT pid, const wchar_t* key, const wchar_t* name) {
    if (ctx.IsClaimed(kVid, pid)) return;
    for (const HidColl* c : ctx.Find(kVid, pid, [](const HidColl& c) {
             return c.page() == kUsagePage && c.usage() == kUsage && c.inLen() == kReportLength &&
                    c.outLen() == kReportLength;
         })) {
        int percent = -1;
        if (!QueryBattery(*c, percent)) continue;

        DeviceStatus s;
        s.key = key;
        s.name = name;
        s.kind = DeviceKind::Mouse;
        s.via = L"2.4 GHz receiver";
        s.percent = percent;
        ctx.Add(s, kVid, pid);
        break;
    }
}

}  // namespace

void PollPressPlayIcarus(PollContext& ctx) {
    PollNearlinkProduct(ctx, kIcarusPid, L"pressplay:icarus-v2", L"Press Play ICARUS V2 Nearlink");
    // Experimental only: AJAZZ VID:PID and HID report sizes match, but the
    // ICARUS battery command has not yet been verified against this receiver.
    PollNearlinkProduct(ctx, kAjazzPid, L"ajazz:aj159nlp-nearlink", L"AJAZZ AJ159NL P");
}
