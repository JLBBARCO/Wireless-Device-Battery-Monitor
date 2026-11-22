#include "../src/status_json.h"

#include <cassert>
#include <cstdio>
#include <string>

namespace {

StatusDevice Make(DeviceKind kind, const wchar_t* name, int percent, int lastPct = -1) {
    StatusDevice d;
    d.key = std::wstring(L"key:") + name;
    d.name = name;
    d.kind = kind;
    d.percent = percent;
    d.lastPct = lastPct;
    return d;
}

bool Has(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }

void TestKinds() {
    assert(std::string(KindId(DeviceKind::Headset)) == "headset");
    assert(std::string(KindId(DeviceKind::Mouse)) == "mouse");
    assert(std::string(KindId(DeviceKind::Keyboard)) == "keyboard");
    assert(std::string(KindId(DeviceKind::Gamepad)) == "gamepad");
    assert(std::string(KindId(DeviceKind::Phone)) == "phone");
    assert(std::string(KindId(DeviceKind::Other)) == "other");
}

void TestEmptyList() {
    std::string json = BuildStatusJson({}, "snapshot", 15, "2026-11-17T10:00:00Z");
    assert(Has(json, "\"version\": 1"));
    assert(Has(json, "\"source\": \"snapshot\""));
    assert(Has(json, "\"updated\": \"2026-11-17T10:00:00Z\""));
    assert(Has(json, "\"devices\": []"));
}

void TestDevice() {
    StatusDevice mouse = Make(DeviceKind::Mouse, L"ATK / VXE NK mouse", 75);
    mouse.via = L"2.4 GHz receiver";
    mouse.detail = L"L 80%, R 75%";
    mouse.millivolts = 2400;
    std::string json = BuildStatusJson({mouse}, "snapshot", 15, "2026-11-17T10:00:00Z");
    assert(Has(json, "\"name\": \"ATK / VXE NK mouse\""));
    assert(Has(json, "\"kind\": \"mouse\""));
    assert(Has(json, "\"via\": \"2.4 GHz receiver\""));
    assert(Has(json, "\"percent\": 75"));
    assert(Has(json, "\"live\": true"));
    assert(Has(json, "\"charging\": false"));
    assert(Has(json, "\"asleep\": false"));
    assert(Has(json, "\"connected\": true"));
    assert(Has(json, "\"low\": false"));
    assert(Has(json, "\"millivolts\": 2400"));
}

void TestAsleepKeepsLastKnown() {
    StatusDevice headset = Make(DeviceKind::Headset, L"Arctis", -1, 42);
    headset.asleep = true;
    std::string json = BuildStatusJson({headset}, "cli", 15, "2026-11-17T10:00:00Z");
    assert(Has(json, "\"percent\": 42"));  // what the widget shows while the device sleeps
    assert(Has(json, "\"live\": false"));
    assert(Has(json, "\"asleep\": true"));
    assert(Has(json, "\"low\": false"));
}

void TestUnknownAndLow() {
    StatusDevice unknown = Make(DeviceKind::Keyboard, L"Unknown", -1);
    std::string json = BuildStatusJson({unknown}, "cli", 15, "2026-11-17T10:00:00Z");
    assert(Has(json, "\"percent\": -1"));  // never seen a value: consumers skip it

    StatusDevice low = Make(DeviceKind::Mouse, L"Low", 10);
    assert(Has(BuildStatusJson({low}, "cli", 15, "x"), "\"low\": true"));

    // Charging at 10% is not a low-battery warning.
    low.charging = true;
    assert(Has(BuildStatusJson({low}, "cli", 15, "x"), "\"low\": false"));
    assert(Has(BuildStatusJson({low}, "cli", 15, "x"), "\"charging\": true"));
}

void TestResolvedHistoricalZero() {
    StatusDevice d = Make(DeviceKind::Keyboard, L"Lofree Flow 2", 99, 88);
    d.charging = true;  // stale raw flags must not override resolved historical presentation
    d.asleep = true;
    d.viewResolved = true;
    d.view.state = battery_state::State::LastKnown;
    d.view.reason = battery_state::Reason::NoResponse;
    d.view.percent = 0;
    d.view.readingTick = 1000;
    d.view.readingUtc = "2026-11-22T09:00:00Z";
    d.readingAgeSeconds = 1080;
    const auto first = BuildStatusJson({d}, "snapshot", 15, "2026-11-22T09:18:00Z");
    const auto later = BuildStatusJson({d}, "snapshot", 15, "2026-11-22T09:19:00Z");
    for (const auto* json : {&first, &later}) {
        assert(Has(*json, "\"percent\": 0"));
        assert(Has(*json, "\"live\": false"));
        assert(Has(*json, "\"charging\": false"));
        assert(Has(*json, "\"low\": false"));
        assert(Has(*json, "\"asleep\": false"));
        assert(Has(*json, "\"state\": \"last_known\""));
        assert(Has(*json, "\"reason\": \"no_response\""));
        assert(Has(*json, "\"last_reading_at\": \"2026-11-22T09:00:00Z\""));
        assert(Has(*json, "\"reading_age_seconds\": 1080"));
    }
    assert(first != later);  // only the separately supplied snapshot time changed
    assert(d.readingAgeSeconds == 1080 && d.view.readingTick == 1000);
}

void TestResolvedReasons() {
    using battery_state::Reason;
    const std::pair<Reason, const char*> cases[] = {
        {Reason::None, "none"}, {Reason::SleepReported, "sleep_reported"},
        {Reason::NoResponse, "no_response"}, {Reason::Unavailable, "unavailable"},
        {Reason::QueryFailed, "query_failed"}, {Reason::InvalidResponse, "invalid_response"},
        {Reason::NotYetRead, "not_yet_read"}, {Reason::ReadingAged, "reading_aged"},
        {Reason::Disconnected, "disconnected"}
    };
    for (auto [reason, text] : cases) {
        StatusDevice d = Make(DeviceKind::Mouse, L"Mouse", -1, 5);
        d.viewResolved = true;
        d.view.state = battery_state::State::LastKnown;
        d.view.percent = 5;
        d.view.reason = reason;
        const auto json = BuildStatusJson({d}, "snapshot", 15, "x");
        assert(Has(json, std::string("\"reason\": \"") + text + "\""));
        assert(Has(json, std::string("\"asleep\": ") + (reason == Reason::SleepReported ? "true" : "false")));
        assert(Has(json, "\"live\": false") && Has(json, "\"low\": false"));
        assert(Has(json, "\"last_reading_at\": null"));
        assert(Has(json, "\"reading_age_seconds\": null"));
    }
}

void TestResolvedCurrentAndOffline() {
    StatusDevice current = Make(DeviceKind::Mouse, L"Mouse", -1, 50);
    current.viewResolved = true;
    current.view.state = battery_state::State::Current;
    current.view.reason = battery_state::Reason::None;
    current.view.percent = 0;
    current.view.live = true;
    current.view.readingUtc = "2026-11-22T09:00:00Z";
    current.readingAgeSeconds = 0;
    auto json = BuildStatusJson({current}, "snapshot", 15, "x");
    assert(Has(json, "\"percent\": 0") && Has(json, "\"live\": true"));
    assert(Has(json, "\"low\": true") && Has(json, "\"reading_age_seconds\": 0"));

    current.connected = false;
    current.charging = true;
    current.view.state = battery_state::State::Disconnected;
    current.view.reason = battery_state::Reason::Disconnected;
    current.view.percent = 90;  // defensive offline rendering must hide even an old percentage
    current.view.charging = true;
    json = BuildStatusJson({current}, "snapshot", 15, "x");
    assert(Has(json, "\"percent\": -1") && Has(json, "\"live\": false"));
    assert(Has(json, "\"connected\": false") && Has(json, "\"charging\": false"));
    assert(Has(json, "\"low\": false") && Has(json, "\"state\": \"disconnected\""));
}

void TestLegacyDoesNotInventTimeOrFreshness() {
    auto d = Make(DeviceKind::Mouse, L"Mouse", 5);
    d.asleep = true;
    d.charging = true;
    auto json = BuildStatusJson({d}, "snapshot", 15, "x");
    assert(Has(json, "\"percent\": 5") && Has(json, "\"state\": \"last_known\""));
    assert(Has(json, "\"live\": false") && Has(json, "\"charging\": false"));
    assert(Has(json, "\"low\": false") && Has(json, "\"reason\": \"sleep_reported\""));
    assert(Has(json, "\"last_reading_at\": null") && Has(json, "\"reading_age_seconds\": null"));
    d.connected = false;
    json = BuildStatusJson({d}, "snapshot", 15, "x");
    assert(Has(json, "\"percent\": -1") && Has(json, "\"reason\": \"disconnected\""));
}

void TestEscaping() {
    StatusDevice d = Make(DeviceKind::Other, L"Mouse \"Pro\"\\x\n\ttab \x01", 50);
    std::string json = BuildStatusJson({d}, "cli", 15, "x");
    assert(Has(json, "\"name\": \"Mouse \\\"Pro\\\"\\\\x\\n\\ttab \\u0001\""));
}

void TestUtf8() {
    StatusDevice d = Make(DeviceKind::Other, L"Rato \x00E7\x00E3o \x00FC", 50);  // ção ü
    std::string json = BuildStatusJson({d}, "cli", 15, "x");
    assert(Has(json, "\"name\": \"Rato \xC3\xA7\xC3\xA3o \xC3\xBC\""));
}

void TestTwoDevices() {
    StatusDevice a = Make(DeviceKind::Mouse, L"Mouse", 50);
    StatusDevice b = Make(DeviceKind::Headset, L"Headset", 90);
    std::string json = BuildStatusJson({b, a}, "snapshot", 15, "x");  // caller picks the order
    size_t first = json.find("\"Headset\""), second = json.find("\"Mouse\"");
    assert(first != std::string::npos && second != std::string::npos && first < second);
    assert(json.back() == '\n');
}

void TestPaths() {
    std::wstring path = StatusSnapshotPath();
    assert(!path.empty());
    assert(path.size() >= 11 && path.compare(path.size() - 11, 11, L"status.json") == 0);
    assert(IsoTimestampNow().size() == 20 && IsoTimestampNow().back() == 'Z');
}

}  // namespace

int main() {
    TestKinds();
    TestEmptyList();
    TestDevice();
    TestAsleepKeepsLastKnown();
    TestUnknownAndLow();
    TestResolvedHistoricalZero();
    TestResolvedReasons();
    TestResolvedCurrentAndOffline();
    TestLegacyDoesNotInventTimeOrFreshness();
    TestEscaping();
    TestUtf8();
    TestTwoDevices();
    TestPaths();
    std::puts("Status JSON tests passed");
    return 0;
}
