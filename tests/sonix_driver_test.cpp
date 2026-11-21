// Exercise the production SONiX poller without accessing HID hardware.
#include "../src/drivers/drivers.h"
#include <cassert>
#include <cstdio>
#include <deque>

static std::deque<std::vector<BYTE>> replies;
static int opens, writes;
static int readTimeoutLimit = 500;
static ULONGLONG now;
static bool consumeTimeouts;
static ULONGLONG TestTickCount64() { return now; }
#define GetTickCount64 TestTickCount64
#include "../src/drivers/sonix.cpp"
#undef GetTickCount64
static bool openOk = true, writeOk = true;
bool HidDev::Open(const HidColl& c) { ++opens; c_ = c; return openOk; }
void HidDev::Close() {}
void HidDev::Flush() {}
bool HidDev::Write(const std::vector<BYTE>& request, int ms) {
    std::vector<BYTE> expected(33, 0);
    expected[1] = 0x20; expected[2] = 1; expected[32] = 0x21;
    assert(request == expected && ms > 0 && ms <= 500);
    if (consumeTimeouts) now += ms;
    ++writes;
    return writeOk;
}
int HidDev::Read(std::vector<BYTE>& reply, int ms) {
    assert(ms > 0 && ms <= readTimeoutLimit);
    if (consumeTimeouts) now += replies.empty() ? ms : std::min(ms, 100);
    if (replies.empty()) return -1;
    reply = replies.front(); replies.pop_front();
    return reply.empty() ? -1 : static_cast<int>(reply.size());
}
std::wstring HidProduct(const HidColl&) { return L"Keyboard 2.4G"; }
std::wstring Hex4(USHORT value) {
    wchar_t result[5]; swprintf(result, 5, L"%04X", value); return result;
}
static HidColl Collection(USHORT vid = 0x342D, USHORT pid = 0xE479, int iface = 1) {
    HidColl c; c.vid = vid; c.pid = pid; c.iface = iface;
    c.caps.UsagePage = 0xFF60; c.caps.Usage = 0x61;
    c.caps.InputReportByteLength = c.caps.OutputReportByteLength = 33;
    return c;
}
static PollContext Poll(HidColl collection, std::deque<std::vector<BYTE>> input) {
    readTimeoutLimit = 500;
    opens = writes = 0; replies = input;
    PollContext ctx; ctx.colls = {collection}; PollSonix(ctx); return ctx;
}
int main() {
    for (auto id : {std::pair<USHORT, USHORT>{0x05AC, 0x024F},
        {0x0C45, 0xFEFE},
        {0x0C45, 0xFDFD},
        {0x342D, 0xE479},
        {0x342D, 0xE411}, // RSQ_20055_2.4G (issue #3808, interface 1)
        {0x342D, 0xE489}, // EPOMAKER Galaxy100 (#3802): FF60/0061 on interface 2
        {0x342D, 0xE485},
        {0x342D, 0xE4BC},
        {0x342D, 0xE4C2},
        {0x342D, 0xE407},
        {0x388D, 0x0003},
        {0x0DB0, 0x0CA0},
        {0x36B0, 0x30BD},
        {0x342D, 0xE51A},
        {0x342D, 0xE487},
        {0x342D, 0xE537},
        {0x19F5, 0x3245},
        {0x362D, 0xD028},
        {0x362D, 0xD030},
        {0x362D, 0xD101},
        {0x342D, 0xE49F}}) {
        int iface = (id.first == 0x05AC || id.first == 0x0C45) ? 3 :
                    (id.first == 0x342D && id.second == 0xE489) ? 2 : 1;
        auto ctx = Poll(Collection(id.first, id.second, iface), {{0x20, 1, 0, 73}});
        assert(ctx.out.size() == 1 && ctx.out[0].percent == 73);
        assert(ctx.out[0].key == L"sonix:" + Hex4(id.first) + L":" + Hex4(id.second));
        assert(ctx.out[0].name == L"Keyboard" && ctx.out[0].graceSec == 1800);
        PollSonix(ctx); assert(opens == 1 && writes == 1);
    }
    // RSQ_20055_2.4G reports SONiX FF60:0061 on MI_01 only.
    for (int iface : {0, 2, 3, 4}) {
        auto ctx = Poll(Collection(0x342D, 0xE411, iface), {{0x20, 1, 0, 73}});
        assert(ctx.out.empty() && opens == 0 && writes == 0);
    }
    auto wrongSize = Collection(0x342D, 0xE411, 1);
    --wrongSize.caps.InputReportByteLength;
    assert(Poll(wrongSize, {{0x20, 1, 0, 73}}).out.empty() && opens == 0);
    wrongSize = Collection(0x342D, 0xE411, 1);
    --wrongSize.caps.OutputReportByteLength;
    assert(Poll(wrongSize, {{0x20, 1, 0, 73}}).out.empty() && opens == 0);
    // Galaxy100 is matched only on its reported SONiX vendor HID interface (MI_02).
    // In particular, do not probe its other QMK/VIA interface numbers.
    for (int iface : {0, 1, 3}) {
        auto ctx = Poll(Collection(0x342D, 0xE489, iface), {{0x20, 1, 0, 73}});
        assert(ctx.out.empty() && opens == 0 && writes == 0);
    }
    wrongSize = Collection(0x342D, 0xE489, 2);
    --wrongSize.caps.OutputReportByteLength;
    assert(Poll(wrongSize, {{0x20, 1, 0, 73}}).out.empty() && opens == 0);
    for (int level : {0, 1, 73, 100, 101, 255}) {
        auto ctx = Poll(Collection(), {{0x20, 1, 0, static_cast<BYTE>(level)}});
        bool connected = level >= 1 && level <= 100;
        assert(ctx.out.size() == 1 && ctx.out[0].connected == connected);
        assert(ctx.out[0].percent == (connected ? level : -1));
    }
    for (int fault = 0; fault < 8; ++fault) {
        auto c = Collection();
        if (fault == 0) c.vid = 0xFFFF;
        if (fault == 1) c.pid = 0xFFFF;
        if (fault == 2) ++c.caps.UsagePage;
        if (fault == 3) ++c.caps.Usage;
        if (fault == 4) --c.caps.InputReportByteLength;
        if (fault == 5) --c.caps.OutputReportByteLength;
        if (fault == 6) c = Collection(0x05AC, 0x024F, 1);
        if (fault == 7) c = Collection(0x36B0, 0x3002, 1); // PMO/VIA shared ID stays excluded.
        assert(Poll(c, {{0x20, 1, 0, 73}}).out.empty() && opens == 0);
    }
    auto noisy = Poll(Collection(), {{0x20}, {0, 1, 0, 73},
        {0x20, 2, 0, 73},
        {0x20, 1, 1, 73},
        {0x20, 1, 0, 73}});
    assert(noisy.out.size() == 1 && writes == 1);
    assert(Poll(Collection(), {{},
        {0x20, 1, 0, 73}}).out.size() == 1 && writes == 2);
    assert(Poll(Collection(), {}).out.empty() && writes == 2);
    openOk = false;
    assert(Poll(Collection(), {}).out.empty() && writes == 0);
    openOk = true; writeOk = false;
    assert(Poll(Collection(), {}).out.empty() && writes == 2);
    writeOk = true;

    const auto flow2Via = Collection(0x388D, 0x0002, 2);
    auto flow2Receiver = Collection(0x388D, 0x0002, 0);
    flow2Receiver.caps.UsagePage = 1;
    flow2Receiver.caps.Usage = 0;
    flow2Receiver.caps.InputReportByteLength = 64;
    flow2Receiver.caps.OutputReportByteLength = 64;
    const auto pollFlow2 = [&](const HidColl& receiver, std::deque<std::vector<BYTE>> input) {
        opens = writes = 0;
        now = 0;
        readTimeoutLimit = 2500;
        replies = input;
        PollContext ctx;
        ctx.colls = {flow2Via, receiver};
        PollSonix(ctx);
        return ctx;
    };
    const auto unavailable = pollFlow2(flow2Receiver, {{0xFF, 1, 0}});
    assert(unavailable.out.size() == 1);
    const auto& receiverStatus = unavailable.out[0];
    assert(receiverStatus.key == L"sonix:388D:0002" && receiverStatus.kind == DeviceKind::Keyboard);
    assert(receiverStatus.connected && receiverStatus.percent == -1);
    assert(receiverStatus.note == L"Battery unavailable" && !receiverStatus.asleep);
    assert(receiverStatus.graceSec == 0 && !receiverStatus.retainLastPercent && writes == 2);
    assert(pollFlow2(flow2Receiver, {}).out.size() == 1 && writes == 2);
    assert(Poll(flow2Via, {}).out.empty() && opens == 0);
    assert(Poll(Collection(0x388D, 0x0002, 1), {{0x20, 1, 0, 73}}).out.empty() && opens == 0);

    const auto checksum = [](std::vector<BYTE> packet) {
        unsigned sum = 0;
        for (size_t i = 0; i < 31; ++i) sum += packet[i];
        packet[31] = static_cast<BYTE>(sum);
        return packet;
    };
    const auto extension = [&](BYTE percent, BYTE status = 1) {
        std::vector<BYTE> packet(32, 0);
        packet[0] = 0x20; packet[1] = 1;
        packet[3] = percent; packet[4] = status;
        packet[5] = 'L'; packet[6] = 'F'; packet[7] = '2'; packet[8] = 1;
        return checksum(packet);
    };
    for (BYTE level : {0, 1, 73, 100}) {
        auto reading = pollFlow2(flow2Receiver, {extension(level)});
        assert(reading.out.size() == 1 && reading.out[0].percent == level);
        assert(reading.out[0].connected && reading.out[0].graceSec == 0);
        assert(reading.out[0].key == receiverStatus.key && reading.out[0].note.empty());
        PollSonix(reading);
        assert(opens == 1 && writes == 1);  // one stable key claims both collections
    }
    auto missing = pollFlow2(flow2Receiver, {extension(0xFF, 0)});
    assert(missing.out.size() == 1 && missing.out[0].percent == -1);
    assert(missing.out[0].note == L"Battery unavailable" && missing.out[0].graceSec == 0);

    // Use the same state update as the tray and widget lifecycle. An explicit
    // unknown measurement clears Flow2's remembered percentage between polls.
    int remembered = RememberedPercent(pollFlow2(flow2Receiver, {extension(73)}).out[0], -1);
    assert(remembered == 73);
    remembered = RememberedPercent(missing.out[0], remembered);
    assert(remembered == -1);
    remembered = RememberedPercent(pollFlow2(flow2Receiver, {extension(0)}).out[0], remembered);
    assert(remembered == 0);
    remembered = RememberedPercent(pollFlow2(flow2Receiver, {}).out[0], remembered);
    assert(remembered == -1);
    remembered = RememberedPercent(pollFlow2(flow2Receiver, {extension(100)}).out[0], remembered);
    assert(remembered == 100);
    auto legacy = Poll(Collection(), {{0x20, 1, 0, 73}}).out[0];
    assert(legacy.retainLastPercent && RememberedPercent(legacy, -1) == 73);
    legacy.percent = -1;
    legacy.asleep = true;
    assert(RememberedPercent(legacy, 73) == 73);

    // Every malformed variant has a plausible legacy prefix, but none may be
    // interpreted as a legacy percentage on this exact receiver.
    std::deque<std::vector<BYTE>> malformed = {{0x20, 1, 0, 73}};
    for (int fault = 0; fault < 11; ++fault) {
        auto packet = extension(73);
        if (fault == 0) packet.resize(31);
        if (fault == 1) packet.push_back(0);
        if (fault == 2) ++packet[31];
        if (fault == 3) { packet[5] = 'X'; packet = checksum(packet); }
        if (fault == 4) { packet[6] = 'X'; packet = checksum(packet); }
        if (fault == 5) { packet[7] = 'X'; packet = checksum(packet); }
        if (fault == 6) { packet[8] = 2; packet = checksum(packet); }
        if (fault == 7) packet = extension(101);
        if (fault == 8) packet = extension(0xFF);
        if (fault == 9) packet = extension(73, 0);
        if (fault == 10) packet = extension(73, 2);
        auto rejected = pollFlow2(flow2Receiver, {packet});
        assert(rejected.out.size() == 1 && rejected.out[0].percent == -1 && writes == 2);
        malformed.push_back(packet);
    }
    assert(pollFlow2(flow2Receiver, {{0x20, 1, 0, 73}}).out[0].percent == -1);
    malformed.push_front({0x20});
    malformed.push_front({0xFF, 1, 0});
    malformed.push_back(extension(100));
    auto recovered = pollFlow2(flow2Receiver, malformed);
    assert(recovered.out.size() == 1 && recovered.out[0].percent == 100 && writes == 1);
    assert(pollFlow2(flow2Receiver, {{}, extension(0)}).out[0].percent == 0 && writes == 2);
    openOk = false;
    assert(pollFlow2(flow2Receiver, {}).out[0].percent == -1 && writes == 0);
    openOk = true; writeOk = false;
    assert(pollFlow2(flow2Receiver, {}).out[0].percent == -1 && writes == 2);
    writeOk = true;

    // Both attempts share four seconds, including write time and unrelated reports.
    consumeTimeouts = true;
    assert(pollFlow2(flow2Receiver, {}).out[0].percent == -1 && writes == 2 && now == 4000);
    std::deque<std::vector<BYTE>> chatter(100, {0xFF, 1, 0});
    assert(pollFlow2(flow2Receiver, chatter).out[0].percent == -1 && writes == 2 && now == 4000);
    writeOk = false;
    assert(pollFlow2(flow2Receiver, {}).out[0].percent == -1 && writes == 2 && now == 1000);
    writeOk = true;
    consumeTimeouts = false;
    for (int fault = 0; fault < 6; ++fault) {
        auto receiver = flow2Receiver;
        if (fault == 0) receiver.iface = 1;
        if (fault == 1) ++receiver.caps.UsagePage;
        if (fault == 2) ++receiver.caps.Usage;
        if (fault == 3) --receiver.caps.InputReportByteLength;
        if (fault == 4) --receiver.caps.OutputReportByteLength;
        if (fault == 5) ++receiver.caps.FeatureReportByteLength;
        assert(pollFlow2(receiver, {extension(73)}).out.empty() && opens == 0);
    }
    for (int fault = 0; fault < 5; ++fault) {
        auto via = flow2Via;
        if (fault == 0) via.iface = 1;
        if (fault == 1) ++via.caps.UsagePage;
        if (fault == 2) ++via.caps.Usage;
        if (fault == 3) --via.caps.InputReportByteLength;
        if (fault == 4) --via.caps.OutputReportByteLength;
        opens = writes = 0;
        replies = {extension(73)};
        PollContext ctx;
        ctx.colls = {via, flow2Receiver};
        PollSonix(ctx);
        assert(ctx.out.empty() && opens == 0 && writes == 0);
    }
    puts("SONiX driver tests passed (21 legacy receiver IDs and strict Flow 2 LF2-v1)");
}
