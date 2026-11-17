#include "event_decoders.h"

#include "hidpp_events.h"

namespace {

// Logitech Unifying / Bolt / LIGHTSPEED receivers: HID++ 0x41 device-connection notifications on
// the short (FF00:0001) and long (FF00:0002) HID++ collections.
class HidppDecoder final : public IHidEventDecoder {
public:
    const wchar_t* Name() const override { return L"Logitech HID++"; }
    bool Wants(const HidColl& c) const override {
        return c.vid == 0x046D && c.page() == 0xFF00 && (c.usage() == 0x0001 || c.usage() == 0x0002) && c.inLen() >= 7;
    }
    HidEvent Decode(const std::uint8_t* r, std::size_t n) const override {
        hidpp::Notification ev = hidpp::Classify(r, n);
        if (ev.kind != hidpp::Notification::Kind::DeviceConnection) return {};
        std::wstring slot = std::to_wstring(ev.index);
        return { HidEvent::Kind::Connection, slot, ev.linkUp, L"device " + slot + (ev.linkUp ? L" connected" : L" disconnected") };
    }
};

}  // namespace

std::vector<std::unique_ptr<IHidEventDecoder>> HidEventDecoders() {
    std::vector<std::unique_ptr<IHidEventDecoder>> v;
    v.push_back(std::make_unique<HidppDecoder>());
    return v;
}
