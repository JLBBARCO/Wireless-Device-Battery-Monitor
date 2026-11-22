#pragma once
// Thin Win32 HID layer shared by all drivers.
//
// Report buffers follow hidapi conventions so protocol code from other projects maps 1:1:
//  * Write/SetFeature: data[0] is the report ID (0 when the device does not number its reports);
//    the buffer is zero-padded to the report length.
//  * Read: a leading 0 report ID is stripped, so data[0] is the first payload byte for
//    unnumbered reports and the report ID for numbered ones.
#include "platform_compat.h"

#ifdef _WIN32
// Older MinGW-w64 headers (e.g. Strawberry Perl's) lack extern "C" here.
extern "C" {
#include <hidsdi.h>
#include <hidpi.h>
}
#else
// Only the fields used by HidColl's accessors are needed by the portable drivers.
struct HIDP_CAPS {
    USHORT UsagePage = 0, Usage = 0;
    USHORT InputReportByteLength = 0;
    USHORT OutputReportByteLength = 0;
    USHORT FeatureReportByteLength = 0;
};
#endif

#include <initializer_list>
#include <cwctype>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "devices.h"

struct HidColl {
    std::wstring path;
    std::wstring product;  // filled by the Linux backend from sysfs; unused on Windows
    USHORT vid = 0, pid = 0, version = 0;
    int iface = -1;  // USB interface number (MI_xx in the path), -1 when unknown
    HIDP_CAPS caps{};
    std::wstring transportId;  // physical container, or a conservative current-instance path fallback
    USHORT page() const { return caps.UsagePage; }
    USHORT usage() const { return caps.Usage; }
    USHORT inLen() const { return caps.InputReportByteLength; }
    USHORT outLen() const { return caps.OutputReportByteLength; }
    USHORT featLen() const { return caps.FeatureReportByteLength; }
};

std::vector<HidColl> EnumHid();

// Presence is recorded before a handle is opened: query permissions and report
// capabilities cannot turn an enumerated receiver into a removed receiver.
struct HidInventory {
    std::vector<HidColl> colls;
    std::vector<HidColl> paths;  // every resolved interface, including unopenable ones
    std::set<std::wstring> presentTransports;
    bool complete = false;
};

namespace hid_inventory {
enum class Step { Found, Skipped, Finished, Failed };

inline std::wstring Lower(std::wstring text) {
    for (auto& ch : text) ch = static_cast<wchar_t>(towlower(ch));
    return text;
}

inline bool HasContainerIdentity(const std::wstring& container) {
    return !container.empty() && container != L"{00000000-0000-0000-0000-000000000000}";
}

// Adapter.Next resolves only the interface path and container. Adapter.Load may
// then inspect attributes/caps, but its failure does not erase presence evidence.
template <typename Adapter>
HidInventory Scan(Adapter& adapter) {
    HidInventory out;
    if (!adapter.Begin()) return out;
    out.complete = true;
    for (unsigned index = 0;; ++index) {
        HidColl coll;
        std::wstring container;
        Step step = adapter.Next(index, coll, container);
        if (step == Step::Finished) break;
        if (step == Step::Failed) { out.complete = false; break; }
        if (step == Step::Skipped) { out.complete = false; continue; }
        if (coll.path.empty()) { out.complete = false; continue; }
        if (HasContainerIdentity(container)) coll.transportId = L"container:" + Lower(container);
        else {
            coll.transportId = L"path:" + Lower(coll.path);
            // A fallback cannot prove that a previously seen container vanished.
            out.complete = false;
        }
        out.paths.push_back(coll);
        out.presentTransports.insert(coll.transportId);
        if (adapter.Load(coll)) out.colls.push_back(std::move(coll));
    }
    adapter.End();
    return out;
}
}  // namespace hid_inventory

#ifdef _WIN32
HidInventory EnumHidInventory();
#endif

class HidDev {
public:
    HidDev() = default;
    ~HidDev() { Close(); }
    HidDev(const HidDev&) = delete;
    HidDev& operator=(const HidDev&) = delete;

    bool Open(const HidColl& c);            // read/write, overlapped
    bool OpenReadOnly(const HidColl& c);    // read only, overlapped: for push-only collections another app may hold
    bool OpenFeatureOnly(const HidColl& c);  // zero access: enough for feature reports, works on mice/keyboards
    void Close();
#ifdef _WIN32
    bool ok() const { return h_ != INVALID_HANDLE_VALUE; }
#else
    bool ok() const { return fd_ >= 0; }
#endif
    const HidColl& coll() const { return c_; }
#ifdef _WIN32
    bool HasInputOutputReport(BYTE reportId) const;
#endif

    bool Write(const std::vector<BYTE>& data, int timeoutMs = 500);
    bool Write(std::initializer_list<BYTE> data, int timeoutMs = 500) { return Write(std::vector<BYTE>(data), timeoutMs); }
    // Writes a request and waits for an input response. Windows arms the input read before writing;
    // writeSucceeded distinguishes a failed write from a request that received no response.
    int WriteAndRead(const std::vector<BYTE>& data, std::vector<BYTE>& buf, int timeoutMs,
                     bool* writeSucceeded = nullptr);
    // Reads one input report into `buf` (resized to fit). Returns the byte count, or -1 on timeout/error.
    int Read(std::vector<BYTE>& buf, int timeoutMs);
    // For collections without an interrupt OUT pipe (WriteFile fails there): arms a read, sends
    // `data` with HidD_SetOutputReport, and returns the first input report like Read() does.
    int SetOutputAndRead(const std::vector<BYTE>& data, std::vector<BYTE>& buf, int timeoutMs);
    void Flush();  // discard queued input reports

    bool SetFeature(const std::vector<BYTE>& data);
    bool GetFeature(BYTE reportId, std::vector<BYTE>& out);  // out[0] = report ID
    bool GetFeatureByte(BYTE reportId, BYTE& value);  // numbered 2-byte report on a read handle
    bool GetInputReport(BYTE reportId, std::vector<BYTE>& out);  // current input report via the control pipe; out[0] = report ID

private:
#ifdef _WIN32
    HANDLE h_ = INVALID_HANDLE_VALUE;
    bool overlapped_ = false;
#else
    int fd_ = -1;  // /dev/hidrawN
#endif
    HidColl c_;
};

std::wstring HidProduct(const HidColl& c);
std::wstring Hex4(USHORT v);
int Clamp100(int v);
int MapTo100(int v, int lo, int hi);  // linear map lo..hi -> 0..100, clamped
// Piecewise-linear interpolation through (mv[i], pct[i]); tables sorted by descending voltage.
int Interpolate(int mv, const int* mvTable, const int* pctTable, int n);
DeviceKind GuessKind(const std::wstring& name);
// Device type from the top-level collections a device exposes (mouse > gamepad > keyboard).
DeviceKind KindFromCollections(const std::vector<HidColl>& all, USHORT vid, USHORT pid);
