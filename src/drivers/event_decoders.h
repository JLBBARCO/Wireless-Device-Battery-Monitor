#pragma once
// Vendor knowledge for push events: which HID collections carry unsolicited reports, and what a
// report means. One decoder per protocol; HidEventListener (the engine) never knows a vendor.
//
// To add a vendor: implement IHidEventDecoder next to its driver, register it in
// event_decoders.cpp. Nothing else changes.
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "../hid.h"

struct HidEvent {
    enum class Kind {
        None,        // not an event (a reply, input, noise)
        Connection,  // a device behind a receiver came or went
        Battery,     // the device reported a battery change by itself
    } kind = Kind::None;
    std::wstring subject;  // which device on the channel, e.g. the receiver slot ("2")
    bool present = false;  // Connection: the device is now reachable
    std::wstring what;     // for `--events`, e.g. "device 2 connected"
};

// Connection events are edges, not levels. Vendor software (openlogi, G HUB, Solaar) asks receivers
// to re-announce their paired devices now and then, which repeats "connected" for a device that
// never left; only a change of state is worth a poll. Applied by the engine to every decoder.
class ConnectionEdges {
public:
    // `channel` identifies where the event came from (the collection path).
    bool IsChange(const std::wstring& channel, const HidEvent& ev) {
        if (ev.kind != HidEvent::Kind::Connection) return ev.kind != HidEvent::Kind::None;
        auto [it, inserted] = present_.emplace(channel + L"|" + ev.subject, ev.present);
        if (inserted) return true;  // first word about this device: we cannot know it is a repeat
        if (it->second == ev.present) return false;
        it->second = ev.present;
        return true;
    }

private:
    std::map<std::wstring, bool> present_;
};

struct IHidEventDecoder {
    virtual ~IHidEventDecoder() = default;
    virtual const wchar_t* Name() const = 0;
    virtual bool Wants(const HidColl& c) const = 0;
    virtual HidEvent Decode(const std::uint8_t* report, std::size_t n) const = 0;
    // How long to wait after an event before polling (a just-connected device needs a moment).
    virtual std::uint32_t SettleMs() const { return 1500; }
};

std::vector<std::unique_ptr<IHidEventDecoder>> HidEventDecoders();
