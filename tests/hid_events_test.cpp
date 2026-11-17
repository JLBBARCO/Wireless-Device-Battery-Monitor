#include "../src/drivers/event_decoders.h"
#include "../src/drivers/hidpp_events.h"

#include <cassert>
#include <cstdio>
#include <vector>

using Bytes = std::vector<std::uint8_t>;

HidColl Coll(USHORT vid, USHORT page, USHORT usage, USHORT inLen) {
    HidColl c;
    c.vid = vid;
    c.caps.UsagePage = page;
    c.caps.Usage = usage;
    c.caps.InputReportByteLength = inLen;
    return c;
}

int main() {
    using hidpp::Notification;
    // Device 2 on a LIGHTSPEED receiver switched on: link established (bit 6 clear).
    Bytes on = { 0x10, 0x02, 0x41, 0x0C, 0x02, 0x9F, 0x40 };
    Notification n = hidpp::Classify(on.data(), on.size());
    assert(n.kind == Notification::Kind::DeviceConnection && n.index == 2 && n.linkUp);
    // Switched off: bit 6 set.
    Bytes off = { 0x10, 0x02, 0x41, 0x0C, 0x42, 0x9F, 0x40 };
    n = hidpp::Classify(off.data(), off.size());
    assert(n.kind == Notification::Kind::DeviceConnection && !n.linkUp);
    // Replies are never events: our own battery reply (sw id 0xD), a register read reply, an error.
    Bytes battery = { 0x11, 0x01, 0x06, 0x1D, 0x50, 0x00, 0x00 };
    Bytes regReply = { 0x10, 0xFF, 0x81, 0x00, 0x00, 0x01, 0x00 };
    Bytes error = { 0x10, 0x01, 0x8F, 0x81, 0x0D, 0x03, 0x00 };
    for (const Bytes* b : { &battery, &regReply, &error })
        assert(hidpp::Classify(b->data(), b->size()).kind == Notification::Kind::None);
    // Receiver index 0xFF, index 0, short frames and other report ids are ignored.
    Bytes receiverIdx = { 0x10, 0xFF, 0x41, 0x0C, 0x02, 0x9F, 0x40 };
    Bytes zeroIdx = { 0x10, 0x00, 0x41, 0x0C, 0x02, 0x9F, 0x40 };
    Bytes shortFrame = { 0x10, 0x02, 0x41 };
    Bytes mouseInput = { 0x02, 0x02, 0x41, 0x0C, 0x02, 0x9F, 0x40 };
    for (const Bytes* b : { &receiverIdx, &zeroIdx, &shortFrame, &mouseInput })
        assert(hidpp::Classify(b->data(), b->size()).kind == Notification::Kind::None);

    // Notification flags: the request is a read of register 0x00 on the receiver.
    std::uint8_t req[7];
    hidpp::NotificationFlagsRequest(req);
    assert(req[0] == 0x10 && req[1] == 0xFF && req[2] == 0x81 && req[3] == 0x00);
    std::uint32_t flags = 0;
    assert(hidpp::ParseNotificationFlags(regReply.data(), regReply.size(), flags) && flags == 0x000100);
    assert(flags & hidpp::kWirelessNotifications);
    Bytes flagsOff = { 0x10, 0xFF, 0x81, 0x00, 0x10, 0x00, 0x00 };
    assert(hidpp::ParseNotificationFlags(flagsOff.data(), flagsOff.size(), flags) && !(flags & hidpp::kWirelessNotifications));
    assert(!hidpp::ParseNotificationFlags(on.data(), on.size(), flags));

    // The registered Logitech decoder: wants only the HID++ collections, maps 0x41 to Connection.
    auto decoders = HidEventDecoders();
    assert(!decoders.empty());
    const IHidEventDecoder& d = *decoders[0];
    assert(d.Wants(Coll(0x046D, 0xFF00, 0x0001, 7)));
    assert(d.Wants(Coll(0x046D, 0xFF00, 0x0002, 20)));
    assert(!d.Wants(Coll(0x046D, 0xFF43, 0x0202, 20)));  // headset HID++ (no receiver events)
    assert(!d.Wants(Coll(0x046D, 0x0001, 0x0002, 8)));   // the mouse itself
    assert(!d.Wants(Coll(0x1532, 0xFF00, 0x0001, 7)));   // another vendor
    HidEvent ev = d.Decode(on.data(), on.size());
    assert(ev.kind == HidEvent::Kind::Connection && ev.what == L"device 2 connected");
    assert(ev.subject == L"2" && ev.present);
    assert(d.Decode(battery.data(), battery.size()).kind == HidEvent::Kind::None);

    // Edges, from the capture on a PRO X 2 DEX (receiver C54D) with openlogi running: openlogi
    // re-announces (flags 0x22) every few seconds; only the real off (0x62) and on (0xA2) count.
    {
        Bytes reannounce = { 0x10, 0x01, 0x41, 0x11, 0x22, 0xB8, 0x40 };
        Bytes realOff = { 0x10, 0x01, 0x41, 0x11, 0x62, 0xB8, 0x40 };
        Bytes realOn = { 0x10, 0x01, 0x41, 0x11, 0xA2, 0xB8, 0x40 };
        ConnectionEdges edges;
        auto change = [&](const Bytes& b, const wchar_t* path = L"rx1") {
            return edges.IsChange(path, d.Decode(b.data(), b.size()));
        };
        assert(change(reannounce));   // first word about device 1: cannot know it is a repeat
        assert(!change(reannounce));  // openlogi again
        assert(!change(reannounce));
        assert(change(realOff));      // switched off
        assert(change(realOn));       // switched on
        assert(!change(reannounce));
        assert(change(reannounce, L"rx2"));  // the same slot on another receiver is another device
        assert(!change(battery));            // not an event at all
    }

    std::puts("hid_events_test: ok");
    return 0;
}
