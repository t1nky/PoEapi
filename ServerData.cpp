/*
* ServerData.cpp, 8/18/2020 6:43 PM
*/

#include <algorithm>
#include <unordered_map>

static std::map<string, int> inventory_cell_offsets {
    {"item", 0x0},
    {"x",    0x8},
    {"y",    0xc},
};

class InventoryCell : public RemoteMemoryObject {
public:

    shared_ptr<Item> item;
    int index, x, y;

    InventoryCell(addrtype address)
        : RemoteMemoryObject(address, &inventory_cell_offsets)
    {
        x = read<int>("x");
        y = read<int>("y");
        item = shared_ptr<Item>(new Item(read<addrtype>("item")));
    }

    Item& get_item() {
        addrtype addr = read<addrtype>("item");
        if (item->address != addr)
            item = shared_ptr<Item>(new Item(addr));

        return *item;
    }

    void to_print() {
        wprintf(L"    %llx: (%d, %d), %S\n", address, x, y, item->name().c_str());
        item->list_components();
    }
};

static std::map<string, int> inventory_offsets {
    {"id",               0x0},
    {"internal",         0x8},
        {"type",         0x0},
        {"sub_type",     0x4},
        {"is_requested", 0x4},
        {"cols",         0xc},
        {"rows",        0x10},
        {"items",       0x30},
        {"count",       0x50},
};

class Inventory : public RemoteMemoryObject, public AhkObj {
public:

    std::unordered_map<int, InventoryCell> cells;
    int id, type, sub_type, cols, rows;

    Inventory(addrtype address) : RemoteMemoryObject(address, &inventory_offsets) {
        id = read<byte>("id");
        this->address = read<addrtype>("internal");

        type = read<byte>("type");
        sub_type = read<byte>("sub_type");
        cols = read<byte>("cols");
        rows = read<byte>("rows");

        add_method(L"Count", this, (MethodType)&Inventory::count, AhkInt);
        add_method(L"getItems", this, (MethodType)&Inventory::get_items, AhkInt);
    }

    void __new() {
        __set(L"Id", id, AhkInt,
              L"Type", type, AhkInt,
              L"SubType", sub_type, AhkInt,
              L"Cols", cols, AhkInt,
              L"Rows", rows, AhkInt,
              nullptr);
    }

    int count() {
        return read<byte>("count");
    }

    std::unordered_map<int, InventoryCell>& get_items() {
        if (count() > 0) {
            for (auto addr : read_array<addrtype>("items", 0x0, 8) ) {
                if (addr > 0) {
                    InventoryCell cell(addr);
                    cells.insert(std::make_pair(cell.x * rows + cell.y + 1, cell));
                }
            }
        }

        if (obj_ref) {
            AhkObjRef* ahkobj_ref;

            __set(L"Items", nullptr, AhkObject, nullptr);
            __get(L"Items", &ahkobj_ref, AhkObject);
            AhkObj items(ahkobj_ref);
            for (auto& i : cells) {
                Item& item = i.second.get_item();
                item.__set(L"x", i.second.x, AhkInt, L"y", i.second.y, AhkInt, nullptr);
                items.__set(std::to_wstring(i.first).c_str(),
                            (AhkObjRef*)item,
                            AhkObject, nullptr);
            }
        }

        return cells;
    }

    void to_print() {
        printf("    %llx %3d %3d  %3d  %4d\n", address, id, rows, cols, count());
        if (verbose) {
            printf("    ----------- --- ---- ---- -----\n");
            for (auto i : get_items()) {
                i.second.to_print();
            }
        }
    }
};

static const wchar_t* stash_tab_types[] = {
    L"Normal",
    L"Premium",
    L"",
    L"Currency",
    L"",
    L"Map",
    L"Divination",
    L"Quad",
    L"Essence",
    L"Fragment",
    L"",
    L"",
    L"Delve",
    L"Blight",
    L"Metamorph",
    L"Delirium",
};

enum StashTabFlags {
    RemoveOnly = 0x1,
    IsPremium  = 0x4,
    IsPublic   = 0x20,
    IsMap      = 0x40,
    IsHidden   = 0x80,
};

static std::map<string, int> stash_tab_offsets {
    {"name",          0x8},
    {"inventory_id", 0x28},
    {"type",         0x34},
    {"index",        0x38},
    {"flags",        0x3d},
};

class StashTab : public RemoteMemoryObject, public AhkObj {
public:

    wstring name;
    int type, index, flags;
    Inventory *inventory;

    StashTab(addrtype address) : RemoteMemoryObject(address, &stash_tab_offsets) {
        name = read<wstring>("name");
        index = read<byte>("index");
        type = read<byte>("type");
        flags = read<byte>("flags");

        add_method(L"getInventory", this, (MethodType)&StashTab::get_inventory, AhkObject);
    }

    void __new() {
        __set(L"Index", index, AhkInt,
              L"Name", name.c_str(), AhkWString,
              L"Type", type, AhkInt,
              L"Flags", flags, AhkInt,
              nullptr);
    }

    int inventory_id() {
        return read<byte>("inventory_id");
    }

    AhkObjRef* get_inventory() {
        if (inventory)
            return (AhkObjRef*)*inventory;
        return nullptr;
    }

    void to_print() {
        wprintf(L"    %llx %2d %3d %4x  %02d|%-10S %S\n",
                address, index, inventory_id(), flags, type, stash_tab_types[type], name.c_str());
    }

};

static bool compare_stash_tab(shared_ptr<StashTab>& tab1, shared_ptr<StashTab>& tab2) {
    return tab1->index < tab2->index;
}

static std::map<string, int> server_data_offsets {
    {"league",       0x7450},
    {"latency",      0x74c8},
    {"party_status", 0x7500},
    {"stash_tabs",   0x74d8},
    {"inventories",  0x77f0},
};

class ServerData : public RemoteMemoryObject {
public:

    std::vector<shared_ptr<Inventory>> inventories;
    std::vector<shared_ptr<StashTab>> stash_tabs;

    ServerData(addrtype address) : RemoteMemoryObject(address, &server_data_offsets) {
        get_inventories();
        get_stash_tabs();
    }

    wstring league() {
        return read<wstring>("league");
    }

    int latency() {
        return read<int>("latency");
    }

    int party_status() {
        return read<byte>("party_status");
    }

    std::vector<shared_ptr<StashTab>>& get_stash_tabs() {
        for (auto addr : read_array<addrtype>("stash_tabs", 0x40))
            stash_tabs.push_back(shared_ptr<StashTab>(new StashTab(addr)));

        for (auto i = stash_tabs.begin(); i != stash_tabs.end();) {
            if ((*i)->flags & IsHidden)
                i = stash_tabs.erase(i);
            else
                ++i;
        }
        std::sort(stash_tabs.begin(), stash_tabs.end(), compare_stash_tab);

        return stash_tabs;
    }

    std::vector<shared_ptr<Inventory>>& get_inventories() {
        for (auto addr : read_array<addrtype>("inventories", 0x20))
            inventories.push_back(shared_ptr<Inventory>(new Inventory(addr)));

        return inventories;
    }

    void list_stash_tabs() {
        printf("%llx: Stash Tabs\n", read<addrtype>("stash_tabs"));
        printf("    Address      #  Id Flags Type          Name\n");
        printf("    ----------- -- --- ----- ------------- ------------------------\n");
        for (auto tab : get_stash_tabs())
            tab->to_print();
    }

    void list_inventorie(int id = 0) {
        printf("%llx: Inventorys\n", read<addrtype>("inventories"));
        printf("    Address      Id Rows Cols Items\n");
        printf("    ----------- --- ---- ---- -----\n");
        for (auto i : get_inventories()) {
            if (id == 0 || i->id == id) {
                i->to_print();
                break;
            }
        }
    }
};
