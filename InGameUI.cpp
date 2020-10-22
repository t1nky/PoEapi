/*
* InGameUI.cpp, 8/18/2020 6:46 PM
*/

#include <unordered_map>

#include "ui/Inventory.cpp"
#include "ui/Stash.cpp"
#include "ui/Vendor.cpp"
#include "ui/Sell.cpp"
#include "ui/Trade.cpp"
#include "ui/OverlayMap.cpp"
#include "ui/Chat.cpp"

static std::map<string, int> in_game_ui_offsets {
    {"inventory",       0x520},
        {"grid",        0x3a8},
    {"stash",           0x528},
        {"tabs",        0x2d8},
    {"overlay_map",     0x5a8},
        {"large",       0x230},
        {"small",       0x238},
    {"chat",            0x400},
        {"messages",    0x2f0},
    {"entity_list",     0x5b0},
        {"root",        0x2a0},
    {"vendor",          0x638},
    {"purchase",        0x658},
    {"sell",            0x660},
    {"trade",           0x668},
    {"gem_level_up",    0x8c8},
};

enum {
    InventoryIndex  = 32,
    StashIndex      = 33,
};

class InGameUI : public Element {
public:

    unique_ptr<Inventory> inventory;
    unique_ptr<Stash> stash;
    unique_ptr<Vendor> vendor;
    unique_ptr<Sell> sell;
    unique_ptr<Trade> trade;
    unique_ptr<OverlayMap> large_map, small_map;
    unique_ptr<Chat> chat;
    shared_ptr<Entity> nearest_entity;

    InGameUI(addrtype address) : Element(address, &in_game_ui_offsets) {
    }

    Inventory* get_inventory() {
        if (!inventory)
            inventory = unique_ptr<Inventory>(new Inventory(read<addrtype>("inventory", "grid")));
        return inventory.get();
    }

    Stash* get_stash() {
        if (!stash)
            stash = unique_ptr<Stash>(new Stash(read<addrtype>("stash", "tabs")));
        return stash.get();
    }

    Vendor* get_vendor() {
        if (!vendor)
            vendor = unique_ptr<Vendor>(new Vendor(read<addrtype>("vendor")));
        return vendor.get();
    }

    Sell* get_sell() {
        if (!sell)
            sell = unique_ptr<Sell>(new Sell(read<addrtype>("sell")));
        sell->get_sell_panel();

        return sell.get();
    }

    Trade* get_trade() {
        if (!trade)
            trade = unique_ptr<Trade>(new Trade(read<addrtype>("trade")));
        trade->get_sell_panel();

        return trade.get();
    }

    OverlayMap* get_overlay_map() {
        if (!large_map) {
            large_map.reset(new OverlayMap(read<addrtype>("overlay_map", "large")));
            large_map->shift_modifier = -20.0;
            small_map.reset(new OverlayMap(read<addrtype>("overlay_map", "small")));
            small_map->shift_modifier = 0;
        }
        
        auto& overlay_map = large_map->is_visible() ? large_map : small_map;
        return overlay_map.get();
    }

    Chat* get_chat() {
        if (!chat) {
            chat = unique_ptr<Chat>(new Chat(read<addrtype>("chat", "messages")));
        }
        return chat.get();
    }

    int get_all_entities(EntityList& entities, EntityList& removed) {
        entities.swap(removed);
        entities.clear();
        addrtype root = read<addrtype>("entity_list", "root");
        addrtype next = root;

        while (1) {
            next = PoEMemory::read<addrtype>(next);
            if (!next || next == root)
                break;

            addrtype label = PoEMemory::read<addrtype>(next + 0x18);
            bool is_visible = PoEMemory::read<byte>(label + 0x111) & 0x4;
            if (!is_visible)
                continue;

            addrtype entity_address = PoEMemory::read<addrtype>(next + 0x10);
            int entity_id = PoEMemory::read<int>(entity_address + 0x58);
            auto i = removed.find(entity_id);
            if (i != removed.end()) {
                entities.insert(*i);
                removed.erase(i);
                continue;
            }

            std::shared_ptr<Entity> entity(new Entity(entity_address));
            entity->label = shared_ptr<Element>(new Element(label));
            entities.insert(std::make_pair(entity_id, entity));

            // Limit the maximum entities found.
            if (entities.size() > 2048)
                break;
        }

        return entities.size();
    }

    shared_ptr<Entity>& get_nearest_entity(LocalPlayer& player, wstring text) {
        unsigned int dist, max_dist = -1;
        EntityList entities, removed;

        get_all_entities(entities, removed);
        nearest_entity = nullptr;
        for (auto i : entities) {
            if ((dist = player.dist(*i.second)) < max_dist) {
                if (i.second->name().find(text) != -1 || i.second->path.find(text) != -1) {
                    nearest_entity = i.second;
                    max_dist = dist;
                }
            }
        }

        return nearest_entity;
    }
};
