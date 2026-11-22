// Exercise the production enumeration policy without enumerating real HID devices.
#include "../src/hid.h"

#include <cassert>
#include <cstdio>

namespace {
struct Item {
    hid_inventory::Step step = hid_inventory::Step::Found;
    std::wstring path = L"\\\\?\\hid#vid_388d&pid_0002&mi_00#instance";
    std::wstring container = L"{ABCDEF01-0000-0000-0000-000000000001}";
    bool open = true;
    bool caps = true;
};

struct FakeInventory {
    std::vector<Item> items;
    bool begin = true;
    unsigned loads = 0, ends = 0;
    const Item* current = nullptr;

    bool Begin() { return begin; }
    hid_inventory::Step Next(unsigned index, HidColl& coll, std::wstring& container) {
        if (index >= items.size()) return hid_inventory::Step::Finished;
        current = &items[index];
        coll.path = current->path;
        coll.vid = 0x388D;
        coll.pid = 0x0002;
        container = current->container;
        return current->step;
    }
    bool Load(HidColl& coll) {
        ++loads;
        assert(!coll.transportId.empty());  // binding is resolved before any report handle is opened
        if (current->caps) coll.caps.InputReportByteLength = 64;
        return current->open;
    }
    void End() { ++ends; }
};
}  // namespace

int main() {
    FakeInventory failed;
    failed.begin = false;
    auto inventory = hid_inventory::Scan(failed);
    assert(!inventory.complete && inventory.colls.empty() && inventory.presentTransports.empty());
    assert(failed.loads == 0 && failed.ends == 0);

    FakeInventory empty;
    inventory = hid_inventory::Scan(empty);
    assert(inventory.complete && inventory.presentTransports.empty() && empty.ends == 1);

    FakeInventory receiver;
    receiver.items = {Item{}, Item{}};
    receiver.items[1].path = L"\\\\?\\hid#vid_388d&pid_0002&mi_02#instance";
    inventory = hid_inventory::Scan(receiver);
    assert(inventory.complete && inventory.colls.size() == 2 && inventory.paths.size() == 2);
    assert(inventory.presentTransports.size() == 1);
    assert(inventory.colls[0].transportId == L"container:{abcdef01-0000-0000-0000-000000000001}");
    assert(inventory.colls[0].transportId == inventory.colls[1].transportId);

    FakeInventory inaccessible;
    inaccessible.items = {Item{}};
    inaccessible.items[0].open = false;
    inventory = hid_inventory::Scan(inaccessible);
    assert(inventory.complete && inventory.colls.empty() && inventory.paths.size() == 1);
    assert(inventory.presentTransports.size() == 1 && inaccessible.loads == 1);
    assert(inventory.paths[0].vid == 0x388D && inventory.paths[0].pid == 0x0002);

    FakeInventory noCaps;
    noCaps.items = {Item{}};
    noCaps.items[0].caps = false;
    inventory = hid_inventory::Scan(noCaps);
    assert(inventory.complete && inventory.colls.size() == 1 && inventory.presentTransports.size() == 1);
    assert(inventory.colls[0].inLen() == 0);

    FakeInventory partial;
    partial.items = {Item{}, Item{}, Item{}};
    partial.items[1].step = hid_inventory::Step::Skipped;  // detail failure, continue scanning
    partial.items[2].container = L"{ABCDEF02-0000-0000-0000-000000000002}";
    inventory = hid_inventory::Scan(partial);
    assert(!inventory.complete && inventory.colls.size() == 2 && inventory.presentTransports.size() == 2);
    assert(partial.loads == 2 && partial.ends == 1);

    FakeInventory interrupted;
    interrupted.items = {Item{}, Item{}, Item{}};
    interrupted.items[1].step = hid_inventory::Step::Failed;  // abnormal enumerator termination
    inventory = hid_inventory::Scan(interrupted);
    assert(!inventory.complete && inventory.colls.size() == 1 && interrupted.loads == 1);

    FakeInventory unknownIdentity;
    unknownIdentity.items = {Item{}, Item{}};
    unknownIdentity.items[0].container.clear();
    unknownIdentity.items[0].path = L"\\\\?\\HID#MI_00#INSTANCE";
    unknownIdentity.items[1].container = L"{00000000-0000-0000-0000-000000000000}";
    unknownIdentity.items[1].path = L"\\\\?\\HID#MI_02#INSTANCE";
    inventory = hid_inventory::Scan(unknownIdentity);
    assert(!inventory.complete && inventory.colls.size() == 2 && inventory.presentTransports.size() == 2);
    assert(inventory.colls[0].transportId == L"path:\\\\?\\hid#mi_00#instance");
    assert(inventory.colls[0].transportId != inventory.colls[1].transportId);

    std::puts("HID inventory tests passed (presence, completeness, and conservative identity)");
}
