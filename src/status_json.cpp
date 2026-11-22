#include "status_json.h"

#include <windows.h>

#include <cstdio>

namespace {

std::string Utf8(const std::wstring& text) {
    if (text.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), out.data(), n, nullptr, nullptr);
    return out;
}

std::string Quoted(const std::string& utf8) {
    std::string out;
    out.reserve(utf8.size() + 2);
    out.push_back('"');
    for (unsigned char c : utf8) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[7];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back((char)c);  // UTF-8 bytes pass through as they are
                }
        }
    }
    out.push_back('"');
    return out;
}

const char* Bool(bool v) { return v ? "true" : "false"; }

battery_state::View StatusView(const StatusDevice& d) {
    if (d.viewResolved) return d.view;
    // Compatibility for callers without resident cache state. Synthetic ticks let
    // the shared policy classify current data, but are never exported as receipt times.
    DeviceStatus s;
    s.percent = d.percent;
    s.connected = d.connected;
    s.asleep = d.asleep;
    s.charging = d.charging;
    s.reportTick = 1;
    s.outcome = d.asleep ? BatteryOutcome::SleepReported :
                d.percent > 100 ? BatteryOutcome::InvalidResponse :
                d.percent < 0 ? BatteryOutcome::NoResponse : BatteryOutcome::Reported;
    battery_state::Reading r;
    r.percent = d.lastPct >= 0 ? d.lastPct :
                d.asleep && d.percent >= 0 && d.percent <= 100 ? d.percent : -1;
    r.tick = r.percent >= 0 ? 1 : 0;
    auto view = battery_state::Resolve(s, r, 1, 60000, true);
    view.readingTick = 0;
    view.readingUtc.clear();
    return view;
}

}  // namespace

const char* KindId(DeviceKind kind) {
    switch (kind) {
        case DeviceKind::Headset:  return "headset";
        case DeviceKind::Mouse:    return "mouse";
        case DeviceKind::Keyboard: return "keyboard";
        case DeviceKind::Gamepad:  return "gamepad";
        case DeviceKind::Phone:    return "phone";
        default:                   return "other";
    }
}

std::string JsonQuote(const std::wstring& text) { return Quoted(Utf8(text)); }

std::string BuildStatusJson(const std::vector<StatusDevice>& devices, const std::string& source,
                            int lowThreshold, const std::string& updated) {
    std::string out;
    out += "{\n";
    out += "  \"version\": 1,\n";
    out += "  \"updated\": " + Quoted(updated) + ",\n";
    out += "  \"source\": " + Quoted(source) + ",\n";
    out += "  \"devices\": [";
    for (size_t i = 0; i < devices.size(); i++) {
        const StatusDevice& d = devices[i];
        const auto view = StatusView(d);
        const int shown = view.state == battery_state::State::Disconnected ? -1 : view.percent;
        const bool live = d.connected && view.state == battery_state::State::Current &&
                          view.live && shown >= 0 && shown <= 100;
        const bool charging = live && view.charging;
        out += i ? ",\n    {" : "\n    {";
        out += "\"key\": " + JsonQuote(d.key);
        out += ", \"name\": " + JsonQuote(d.name);
        out += ", \"kind\": \"" + std::string(KindId(d.kind)) + "\"";
        out += ", \"via\": " + JsonQuote(d.via);
        out += ", \"percent\": " + std::to_string(shown);
        out += std::string(", \"live\": ") + Bool(live);
        out += std::string(", \"charging\": ") + Bool(charging);
        out += std::string(", \"asleep\": ") + Bool(view.reason == battery_state::Reason::SleepReported);
        out += std::string(", \"connected\": ") + Bool(d.connected);
        out += std::string(", \"low\": ") + Bool(live && !charging && shown <= lowThreshold);
        out += ", \"levelText\": " + JsonQuote(d.levelText);
        out += ", \"detail\": " + JsonQuote(d.detail);
        out += ", \"millivolts\": " + std::to_string(d.millivolts);
        out += ", \"state\": " + Quoted(battery_state::StateId(view.state));
        out += ", \"reason\": " + Quoted(battery_state::ReasonId(view.reason));
        out += ", \"last_reading_at\": " +
               (view.readingUtc.empty() ? std::string("null") : Quoted(view.readingUtc));
        out += ", \"reading_age_seconds\": " +
               (d.readingAgeSeconds < 0 ? std::string("null") : std::to_string(d.readingAgeSeconds));
        out += "}";
    }
    if (!devices.empty()) out += "\n  ";
    out += "]\n}\n";
    return out;
}

std::string IsoTimestampNow() {
    SYSTEMTIME t;
    GetSystemTime(&t);  // UTC
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04u-%02u-%02uT%02u:%02u:%02uZ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
                  t.wSecond);
    return buf;
}

std::wstring StatusSnapshotPath() {
    wchar_t dir[MAX_PATH] = {};
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", dir, MAX_PATH);
    if (!n || n >= MAX_PATH) {
        wchar_t tmp[MAX_PATH] = {};
        DWORD m = GetTempPathW(MAX_PATH, tmp);
        if (!m || m >= MAX_PATH) return L"WirelessBattery-status.json";  // no usable folder: current directory
        return std::wstring(tmp) + L"WirelessBattery\\status.json";
    }
    return std::wstring(dir) + L"\\WirelessBattery\\status.json";
}

bool WriteStatusSnapshot(const std::vector<StatusDevice>& devices, const std::string& source, int lowThreshold) {
    std::wstring path = StatusSnapshotPath();
    size_t slash = path.find_last_of(L'\\');
    if (slash == std::wstring::npos || slash == 0) return false;
    CreateDirectoryW(path.substr(0, slash).c_str(), nullptr);  // ERROR_ALREADY_EXISTS is fine

    std::string json = BuildStatusJson(devices, source, lowThreshold, IsoTimestampNow());
    std::wstring tmp = path + L".tmp";
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = WriteFile(f, json.data(), (DWORD)json.size(), &written, nullptr) != 0 && written == json.size();
    CloseHandle(f);
    if (!ok) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}
