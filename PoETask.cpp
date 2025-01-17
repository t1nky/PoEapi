/*
* PoETask.cpp, 8/21/2020 10:07 PM
*/

#define DLLEXPORT extern "C" __declspec(dllexport)

#include <mutex>

#include "PoE.cpp"
#include "PoEapi.c"
#include "Task.cpp"
#include "PoEPlugin.cpp"
#include "sqlite3.cpp"
#include "plugins/AutoFlask.cpp"
#include "plugins/AutoOpen.cpp"
#include "plugins/AutoPickup.cpp"
#include "plugins/Messenger.cpp"
#include "plugins/MinimapSymbol.cpp"
#include "plugins/PlayerStatus.cpp"
#include "plugins/KillCounter.cpp"

static std::map<wstring, std::map<string, int>&> offsets = {
    {L"GameStates", game_state_controller_offsets},
    {L"IngameState", in_game_state_offsets},
    {L"IngameData", in_game_data_offsets},
    {L"IngameUI", in_game_ui_offsets},
    {L"ServerData", server_data_offsets},
    {L"Entity", entity_offsets},
    {L"Element", element_offsets},
    {L"StashTab", stash_tab_offsets},
    {L"Inventory", inventory_offsets},
};

class PoETask : public PoE, public Task {
public:
    
    EntitySet entities;
    EntityList labeled_entities, labeled_removed;
    int area_hash;
    wstring league;

    std::map<wstring, shared_ptr<PoEPlugin>> plugins;
    std::wregex ignored_entity_exp;
    std::mutex muxtex;
    bool is_attached = false;
    bool is_active = false;
    shared_ptr<Element> hovered_element;
    shared_ptr<Item> hovered_item;

    PoETask() : Task(L"PoETask"),
        ignored_entity_exp(L"Doodad|WorldItem|Barrel|Basket|Bloom|BonePile|Boulder|Cairn|Crate|Pot|Urn|Vase"
                           "|BlightFoundation|BlightTower|Effects")
    {
        /* add jobs */
        add_job(L"PlayerStatusJob", 99, [&] {this->check_player();});
        add_job(L"EntityJob", 55, [&] {this->check_entities();});
        add_job(L"LabeledEntityJob", 66, [&] {this->check_labeled_entities();});

        /* add plugins */
        add_plugin(new PlayerStatus());
        add_plugin(new AutoFlask());
        add_plugin(new AutoOpen());
        add_plugin(new Messenger(), true);
        add_plugin(new MinimapSymbol());
        add_plugin(new KillCounter());
        add_plugin(new AutoPickup());

        add_property(L"isReady", &is_ready, AhkBool);
        add_property(L"isAttached", &is_attached, AhkBool);
        add_property(L"isActive", &is_active, AhkBool);

        add_method(L"start", (Task*)this, (MethodType)&Task::start, AhkInt);
        add_method(L"stop", this, (MethodType)&PoETask::stop);
        add_method(L"reset", this, (MethodType)&PoETask::reset);
        add_method(L"getLatency", this, (MethodType)&PoETask::get_latency);
        add_method(L"getNearestEntity", this, (MethodType)&PoETask::get_nearest_entity, AhkObject, ParamList{AhkWString});
        add_method(L"getPartyStatus", this, (MethodType)&PoETask::get_party_status);
        add_method(L"getInventory", this, (MethodType)&PoETask::get_inventory, AhkObject);
        add_method(L"getInventorySlots", this, (MethodType)&PoETask::get_inventory_slots, AhkObject);
        add_method(L"getIngameUI", this, (MethodType)&PoETask::get_ingame_ui, AhkObject);
        add_method(L"getStash", this, (MethodType)&PoETask::get_stash, AhkObject);
        add_method(L"getStashTabs", this, (MethodType)&PoETask::get_stash_tabs, AhkObject);
        add_method(L"getVendor", this, (MethodType)&PoETask::get_vendor, AhkObject);
        add_method(L"getPurchase", this, (MethodType)&PoETask::get_purchase, AhkObject);
        add_method(L"getSell", this, (MethodType)&PoETask::get_sell, AhkObject);
        add_method(L"getTrade", this, (MethodType)&PoETask::get_trade, AhkObject);
        add_method(L"getChat", this, (MethodType)&PoETask::get_chat, AhkObject);
        add_method(L"getFavours", this, (MethodType)&PoETask::get_favours, AhkObject);
        add_method(L"getPassiveSkills", this, (MethodType)&PoETask::get_passive_skills, AhkObject);
        add_method(L"getJobs", this, (MethodType)&PoETask::get_jobs, AhkObject);
        add_method(L"setJob", this, (MethodType)&PoETask::set_job, AhkVoid, ParamList{AhkWString, AhkInt});
        add_method(L"getPlugin", this, (MethodType)&PoETask::get_plugin, AhkObject, ParamList{AhkWString});
        add_method(L"getPlugins", this, (MethodType)&PoETask::get_plugins, AhkObject);
        add_method(L"getEntities", this, (MethodType)&PoETask::get_entities, AhkObject, ParamList{AhkWString});
        add_method(L"getPlayer", this, (MethodType)&PoETask::get_player, AhkObject);
        add_method(L"getTerrain", this, (MethodType)&PoETask::get_terrain, AhkObject);
        add_method(L"getHoveredElement", this, (MethodType)&PoETask::get_hovered_element, AhkObject);
        add_method(L"getHoveredItem", this, (MethodType)&PoETask::get_hovered_item, AhkObject);
        add_method(L"setOffset", this, (MethodType)&PoETask::set_offset, AhkVoid, ParamList{AhkWString, AhkString, AhkInt});
        add_method(L"toggleMaphack", this, (MethodType)&PoETask::toggle_maphack, AhkBool);
        add_method(L"toggleHealthBar", this, (MethodType)&PoETask::toggle_health_bar, AhkBool);
        add_method(L"getBuffs", this, (MethodType)&PoETask::get_buffs, AhkObject);
        add_method(L"hasBuff", this, (MethodType)&PoETask::has_buff, AhkInt, ParamList{AhkWString});
        add_method(L"bindHud", (PoE*)this, (MethodType)&PoE::bind_hud, AhkVoid, ParamList{AhkUInt});
        add_method(L"__logout", (PoE*)this, (MethodType)&PoE::logout, AhkVoid);
    }

    ~PoETask() {
        stop();
    }

    void set_job(const wchar_t* name, int period) {
        jobs[name]->delay = period;
    }

    void add_plugin(PoEPlugin* plugin, bool enabled = false) {
        plugins[plugin->name] = shared_ptr<PoEPlugin>(plugin);
        plugin->enabled = enabled;
        plugin->on_load(*this, owner_thread_id);

        log(L"added plugin %S %s", plugin->name.c_str(), plugin->version.c_str());
    }

    int get_party_status() {
        return in_game_state->server_data()->party_status();
    }

    int get_latency() {
        return in_game_state->server_data()->latency();
    }

    AhkObjRef* get_nearest_entity(const wchar_t* text) {
        if (is_in_game()) {
            shared_ptr<Entity>& entity = in_game_ui->get_nearest_entity(*local_player, text);
            if (entity)
                return (AhkObjRef*)*entity;
        }

        return nullptr;
    }

    AhkObjRef* get_ingame_ui() {
        if (is_in_game()) {
            __set(L"ingameUI", (AhkObjRef*)*in_game_ui, AhkObject, nullptr);

            return in_game_ui->obj_ref;
        }
        __set(L"ingameUI", nullptr, AhkObject, nullptr);

        return nullptr;
    }

    AhkObjRef* get_inventory() {
        if (is_in_game()) {
           Inventory* inventory = in_game_ui->get_inventory();
            __set(L"inventory", (AhkObjRef*)*inventory, AhkObject, nullptr);
            return inventory->obj_ref;
        }
        __set(L"inventory", nullptr, AhkObject, nullptr);

        return nullptr;
    }

    AhkObjRef* get_inventory_slots() {
        if (is_in_game()) {
            AhkObj inventory_slots;
            for (auto& i : server_data->get_inventory_slots()) {
                InventorySlot* slot = i.second.get();
                inventory_slots.__set(std::to_wstring(slot->id).c_str(),
                                      (AhkObjRef*)*slot, AhkObject, nullptr);
            }
            __set(L"inventories", (AhkObjRef*)inventory_slots, AhkObject, nullptr);
            return inventory_slots.obj_ref;
        }
        __set(L"inventories", nullptr, AhkObject, nullptr);

        return nullptr;
    }

    AhkObjRef* get_stash() {
        if (is_in_game()) {
            Stash* stash = in_game_ui->get_stash();
            stash->__set(L"tabs", nullptr, AhkObject, nullptr);
            __set(L"stash", (AhkObjRef*)*stash, AhkObject, nullptr);
            return stash->obj_ref;
        }
        __set(L"stash", nullptr, AhkObject, nullptr);

        return nullptr;
    }

    AhkObjRef* get_vendor() {
        if (is_in_game()) {
            Vendor* vendor = in_game_ui->get_vendor();
            return (AhkObjRef*)*vendor;
       }

        return nullptr;
    }

    AhkObjRef* get_purchase() {
        if (is_in_game()) {
            Purchase* purchase = in_game_ui->get_purchase();
            return (AhkObjRef*)*purchase;
       }

        return nullptr;
    }

    AhkObjRef* get_sell() {
        if (is_in_game()) {
            Sell* sell = in_game_ui->get_sell();
            return (AhkObjRef*)*sell;
       }

        return nullptr;
    }

    AhkObjRef* get_trade() {
        if (is_in_game()) {
            Trade* trade = in_game_ui->get_trade();
            return (AhkObjRef*)*trade;
       }

        return nullptr;
    }

    AhkObjRef* get_chat() {
        if (is_in_game()) {
            Chat* chat = in_game_ui->get_chat();
            return (AhkObjRef*)*chat;
       }

        return nullptr;
    }

    AhkObjRef* get_favours() {
        Favours* favours = in_game_ui->get_favours();
        if (favours)
            return *favours;
        return nullptr;
    }

    AhkObjRef* get_passive_skills() {
        if (is_in_game()) {
            AhkObj passive_skills;
            
            for (auto i : server_data->get_passive_skills())
                passive_skills.__set(L"", i, AhkInt, nullptr);
            __set(L"passiveSkills", (AhkObjRef*)passive_skills, AhkObject, nullptr);
            return passive_skills;
       }

        return nullptr;
    }

    AhkObjRef* get_stash_tabs() {
        if (is_in_game()) {
            AhkObj stash_tabs;
            for (auto& i : server_data->get_stash_tabs()) {
                if (i->folder_id == -1) {
                    stash_tabs.__set(L"", (AhkObjRef*)*i, AhkObject, nullptr);
                    if (i->type == 16) {
                        AhkObj tabs;
                        for (auto& t : i->tabs)
                            tabs.__set(L"", (AhkObjRef*)*t, AhkObject, nullptr);
                        i->__set(L"tabs", (AhkObjRef*)tabs, AhkObject, nullptr);
                    }
                }
            }
            __set(L"stashTabs", (AhkObjRef*)stash_tabs, AhkObject, nullptr);
            return stash_tabs.obj_ref;
        }
        __set(L"stashTabs", nullptr, AhkObject, nullptr);

        return nullptr;
    }

    AhkObjRef* get_jobs() {
        AhkTempObj temp_jobs;
        for (auto& i : jobs) {
            AhkObj job;
            job.__set(L"name", i.second->name.c_str(), AhkWString,
                      L"id", i.second->id, AhkUInt,
                      L"delay", i.second->delay, AhkInt,
                      L"resolution", i.second->resolution, AhkUInt,
                      nullptr);
            temp_jobs.__set(i.first.c_str(), (AhkObjRef*)job, AhkObject, nullptr);
        }
        return temp_jobs;
    }

    AhkObjRef* get_plugin(wchar_t* name) {
        if (plugins.find(name) != plugins.end())
            return *plugins[name];
        return nullptr;
    }

    AhkObjRef* get_plugins() {
        AhkObj temp_plugins;
        for (auto& i : plugins) {
            temp_plugins.__set(i.second->name.c_str(), (AhkObjRef*)*i.second, AhkObject, nullptr);
        }
        __set(L"plugins", (AhkObjRef*)temp_plugins, AhkObject, nullptr);

        return temp_plugins;
    }

    AhkObjRef* get_entities(const wchar_t* types) {
        AhkTempObj temp_entities;
        std::wregex types_exp(types);
        for (auto& i : entities.all) {
            if (std::regex_search(i.second->path, types_exp))
                temp_entities.__set(L"", (AhkObjRef*)*i.second, AhkObject, nullptr);
        }

        return temp_entities;
    }

    AhkObjRef* get_player() {
        if (local_player)
            return (AhkObjRef*)*local_player;
        return nullptr;
    }

    AhkObjRef* get_terrain() {
        if (is_in_game())
            return *in_game_data->get_terrain();
        return nullptr;
    }

    AhkObjRef* get_hovered_element() {
        Element* e = in_game_state->get_hovered_element();
        if (e) {
            hovered_element = shared_ptr<Element>(e);
            return *hovered_element;
        }

        return nullptr;
    }

    AhkObjRef* get_hovered_item() {
        Item* item = in_game_state->get_hovered_item();
        if (item) {
            hovered_item = shared_ptr<Item>(item);
            return *hovered_item;
        }

        return nullptr;
    }

    void set_offset(wchar_t* catalog, char* key, int value) {
        auto i = offsets.find(catalog);
        if (i != offsets.end())
            i->second[key] = value;
    }

    void reset() {
        std::unique_lock<std::mutex> lock;

        if (is_ready || !PoE::is_in_game())
            return;
        
        // reset plugins.
        for (auto& i : plugins)
            i.second->reset();

        // clear cached entities.
        entities.all.clear();
        labeled_entities.clear();

        if (in_game_state) {
            in_game_state->reset();
            in_game_ui = in_game_state->in_game_ui();
            in_game_data = in_game_state->in_game_data();
            server_data = in_game_state->server_data();
            if (!in_game_ui || !in_game_data || !server_data)
                return;

            AreaTemplate* world_area = in_game_data->world_area();
            local_player = in_game_data->local_player();
            if (world_area->name().empty() || !local_player)
                return;

            league  = in_game_state->server_data()->league();
            __set(L"league", league.c_str(), AhkWString,
                  L"areaName", in_game_data->world_area()->name().c_str(), AhkWString,
                  L"areaLevel", in_game_data->world_area()->level(), AhkInt,
                  nullptr);
            get_inventory_slots();
            get_stash_tabs();
            get_stash();
            get_inventory();

            Sleep(500);
            is_active = false;
            is_ready = true;
        }
    }

    bool is_in_game() {
        bool in_game_flag = PoE::is_in_game();
        static unsigned int time_in_game = 0;

        if (!is_attached && hwnd) {
            is_attached = true;
            PostThreadMessage(owner_thread_id, WM_PTASK_ATTACHED, (WPARAM)hwnd, (LPARAM)0);
        }

        if (!in_game_flag) {
            if (is_ready) {
                is_ready = false;

                // clear hud
                if (hud) {
                    hud->begin_draw();
                    hud->clear();
                    hud->end_draw();
                }

                PostThreadMessage(owner_thread_id, WM_PTASK_EXIT, (WPARAM)0, (LPARAM)0);
            }

            // increase the delay of timers when PoE isn't in game state.
            Sleep(1000);
        } else {
            if (in_game_state->is_loading()) {
                is_ready = false;
                in_game_data ? in_game_data->force_reset = true : false;
                Sleep(500);

                // clear hud
                if (hud) {
                    hud->begin_draw();
                    hud->clear();
                    hud->end_draw();
                }

                // wait for loading the game instance.
                while (in_game_state->is_loading()) {
                    if (!PoE::is_in_game())
                        return false;
                    Sleep(50);
                }
                PostThreadMessage(owner_thread_id, WM_PTASK_LOADED, (WPARAM)0, (LPARAM)0);
            }
        }

        return in_game_flag;
    }

    void check_player() {
        if (!is_in_game() || !is_ready) {
            if (is_attached && !hwnd) {
                is_attached = false;
                PostThreadMessage(owner_thread_id, WM_PTASK_ATTACHED, (WPARAM)0, (LPARAM)0);
            }
            area_hash = 0;
            return;
        }

        HANDLE h = GetForegroundWindow();
        if (h != hwnd) {
            if (is_active) {
                PostThreadMessage(owner_thread_id, WM_PTASK_ACTIVE, (WPARAM)h, (LPARAM)0);
                Sleep(300);
                PostThreadMessage(owner_thread_id, WM_PTASK_ACTIVE, (WPARAM)h, (LPARAM)0);
                is_active = false;
            }
        } else if (!is_active) {
            PostThreadMessage(owner_thread_id, WM_PTASK_ACTIVE, (WPARAM)h, (LPARAM)0);
            is_active = true;
        }

        if (in_game_data->area_hash() != area_hash) {
            area_hash = in_game_data->area_hash();
            AreaTemplate* world_area = in_game_data->world_area();
            if (!world_area->name().empty()) {
                for (auto i : plugins)
                    i.second->on_area_changed(in_game_data->world_area(), area_hash, local_player);
            }
        }

        for (auto& i : plugins)
            if (i.second->enabled)
                i.second->on_player(local_player, in_game_state);
    }

    void check_entities() {
        if (GetForegroundWindow() != hwnd || !is_ready || !is_in_game())
            return;

        in_game_data->get_all_entities(entities, ignored_entity_exp);
        for (auto& i : plugins) {
            if (entities.all.size() > 128)
                SwitchToThread();
            if (is_ready && i.second->enabled && i.second->player)
                i.second->on_entity_changed(entities.all, entities.removed, entities.added);
        }
    }

    void check_labeled_entities() {
        if (GetForegroundWindow() != hwnd || !is_ready || !is_in_game())
            return;

        in_game_ui->get_all_entities(labeled_entities, labeled_removed);
        for (auto& i : plugins) {
            if (labeled_entities.size() > 128)
                SwitchToThread();
            if (is_ready && i.second->enabled && i.second->player)
                i.second->on_labeled_entity_changed(labeled_entities);
        }
    }

    void run() {
        /* yield the execution to make sure the CreateThread() return,
           otherwise log() function may fail. */
        Sleep(50);

        log(L"PoEapi v%d.%d.%d (supported Path of Exile %s).",
            major_version, minor_version, patch_level, supported_PoE_version);

        log(L"PoE task started (%d jobs).",  jobs.size());
        Task::run();
    }

    void stop() {
        is_ready = false;
        Task::stop();
        Sleep(300);
        hud.reset();
    }

    bool toggle_maphack() {
        const char pattern[] = "66 C7 46 58 ?? 00";

        HANDLE handle = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION, false, process_id);
        if (!handle)
            return false;

        if (addrtype addr = find_pattern(pattern)) {
            byte flag = read<byte>(addr + 4) ? 0 : 2;
            if (::write<byte>(handle, addr + 4, &flag, 1)) {
                log(L"Maphack <b style=\"color:blue\">%S</b>.", flag ? L"Enabled" : L"Disabled");
                CloseHandle(handle);
                return true;
            }
        }
        CloseHandle(handle);
            
        return false;
    }

    bool toggle_health_bar() {
        char pattern[] = "?? ?? 44 8b 82 ?? ?? 00 00 8b 82 ?? ?? 00 00 41 0f af c0";
        if (addrtype addr = find_pattern(pattern)) {
            byte flag = read<byte>(addr);
            flag = (flag == 0x7c) ? 0xeb : 0x7c;
            if (write<byte>(addr, &flag, 1)) {
                log(L"Health bar <b style=\"color:blue\">%S</b>.",
                    (flag == 0xeb) ? L"Enabled" : L"Disabled");
                return true;
            }
        }

        return false;
    }

    AhkObjRef* get_buffs() {
        if (local_player) {
            Buffs* buffs = local_player->get_component<Buffs>();
            if (buffs) {
                AhkTempObj temp_buffs;
                for (auto& i : buffs->get_buffs()) {
                    AhkObj buff;
                    buff.__set(L"name", i.first.c_str(), AhkWString,
                               L"description", i.second.description().c_str(), AhkWString,
                               L"duration", i.second.duration(), AhkFloat,
                               L"timer", i.second.timer(), AhkFloat,
                               L"charges", i.second.charges(), AhkInt,
                               nullptr);
                    temp_buffs.__set(L"", (AhkObjRef*)buff, AhkObject, nullptr);
                }

                return temp_buffs;
            }
        }

        return nullptr;
    }

    int has_buff(wchar_t* name) {
        if (local_player) {
            Buffs* buffs = local_player->get_component<Buffs>();
            return buffs->has_buff(name);
        }

        return 0;
    }
};

/* Global PoE task object. */
PoETask ptask;

extern "C" WINAPI
BOOL DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        SetProcessDPIAware();

        /* register classes in ahkpp module */
        ahkpp_register(L"PoETask", L"AhkObj", []()->PoETask* {return &ptask;});
        ahkpp_register(L"PoEObject", L"AhkObj", []()->PoEObject* {return new PoEObject(0);});
        ahkpp_register(L"Component", L"PoEObject", []()->Component* {return new Component(0);});
        ahkpp_register(L"Entity", L"PoEObject", []()->Entity* {return new Entity(0);});
        ahkpp_register(L"Element", L"PoEObject", []()->Element* {return new Element(0);});
        ahkpp_register(L"Item", L"Entity", []()->Item* {return new Item(0);});
        ahkpp_register(L"LocalPlayer", L"Entity", []()->LocalPlayer* {return new LocalPlayer(0);});
        ahkpp_register(L"InGameUI", L"Element", []()->InGameUI* {return new InGameUI(0);});
        ahkpp_register(L"Inventory", L"Element", []()->Inventory* {return new Inventory(0);});
        ahkpp_register(L"Stash", L"Element", []()->Stash* {return new Stash(0);});
        ahkpp_register(L"StashTab", L"AhkObj", []()->StashTab* {return new StashTab(0);});
        ahkpp_register(L"Vendor", L"Element", []()->Vendor* {return new Vendor(0);});
        ahkpp_register(L"Sell", L"Element", []()->Sell* {return new Sell(0);});
        ahkpp_register(L"Trade", L"Sell", []()->Trade* {return new Trade(0);});
        ahkpp_register(L"Chat", L"Element", []()->Chat* {return new Chat(0);});
        ahkpp_register(L"Charges", L"Component", []()->Charges* {return new Charges(0);});
        ahkpp_register(L"Flask", L"Component", []()->Flask* {return new Flask(0);});
        ahkpp_register(L"sqlite3", L"AhkObj", []()->sqlite3* {return new sqlite3();});
        break;

    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
        break;

    case DLL_PROCESS_DETACH:
        break;
    }

    return true;
}

int main(int argc, char* argv[]) {
    SetProcessDPIAware();
    ptask.reset();
    ptask.list_game_states();
    ptask.start();
    ptask.join();
}
