/*
* Entity.cpp, 8/6/2020 1:48 PM
*/

#include <vector>
#include <unordered_map>
#include <math.h>

static FieldOffsets entity_offsets = {
    {"internal",        0x8},
        {"path",        0x8},
    {"component_list", 0x10},
    {"id",             0x50},
};

class Entity : public PoEObject {
protected:

    std::vector<string> component_names;
    std::unordered_map<string, shared_ptr<Component>> components;

    Component* read_component(const string& name, addrtype address) {
        Component *c = read_object<Component>(name, address);
        c->type_name = name;

        return c;
    }

    void get_all_components() {
        addrtype component_list = read<addrtype>("component_list");
        addrtype component_lookup = PoEMemory::read<addrtype>(address + 0x8, {0x30, 0x30});
        addrtype component_entry = component_lookup;
        string component_name;

        while (1) {
            component_entry = PoEMemory::read<addrtype>(component_entry);
            if (!component_entry || component_entry == component_lookup)
                break;

            // Component name
            component_name = PoEMemory::read<string>(component_entry + 0x10, 32);

            // Component address
            int offset = PoEMemory::read<int>(component_entry + 0x18);
            addrtype addr = PoEMemory::read<addrtype>(component_list + offset * 8);
            addrtype owner_address = PoEMemory::read<addrtype>(addr + 0x8);

            // Invalid component
            if (owner_address != address)
                break;
            
            component_names.insert(component_names.end(), component_name);
            std::shared_ptr<Component> component_ptr(read_component(component_name, addr));
            components.insert(std::make_pair(component_name, component_ptr));
        }
    }
    
public:

    wstring type_name;
    wstring path;
    int id;
    bool is_valid = true;
    shared_ptr<Element> label;
    int x, y;

    /* Monster related fields */
    bool is_monster, is_neutral;
    int rarity;

    Entity(addrtype address) : PoEObject(address, &entity_offsets) {
        path = read<wstring>("internal", "path");
        if (path[0] != L'M') {
            this->is_valid = false;
            return;
        }
        id = read<int>("id");

        get_all_components();
        if (is_monster = has_component("Monster")) {
            rarity = get_component<ObjectMagicProperties>()->rarity();
            is_neutral = get_component<Positioned>()->is_neutral();
        }

        add_property(L"x", &x, AhkInt);
        add_property(L"y", &y, AhkInt);
        add_method(L"getComponent", this,
                   (MethodType)(AhkObjRef* (Entity::*)(const char*))&Entity::get_component,
                   AhkObject, std::vector<AhkType>{AhkString});
        add_method(L"getPos", this, (MethodType)&Entity::get_pos,
                   AhkInt, std::vector<AhkType>{AhkPointer, AhkPointer});
    }

    void __new() {
        PoEObject::__new();
        __set(L"Name", name().c_str(), AhkWString,
              L"Id", id, AhkInt,
              L"Path", path.c_str(), AhkWString,
              L"Components", nullptr, AhkObject,
              nullptr);

        AhkObjRef* ahkobj_ref;
        __get(L"Components", &ahkobj_ref, AhkObject);
        AhkObj cmpnts(ahkobj_ref);
        for (auto& i : components)
            cmpnts.__set(L"", (AhkObjRef*)*i.second, AhkObject, nullptr); 
    }

    wstring& name() {
        if (type_name.empty()) {
            if (has_component("Render"))
                type_name = get_component<Render>()->name();

            if (type_name.empty())
                type_name = path.substr(path.rfind(L'/') + 1);
        }

        return type_name;            
    }

    bool is_dead() {
        Life* life = get_component<Life>();
        return life && life->life() == 0;
    }

    bool has_component(const string& name) {
        return components.find(name) != components.end();
    }

    int has_component(std::vector<string>& names) {
        for (int i = 0; i < names.size(); ++i)
            if (components.find(names[i]) != components.end())
                return i;
        return -1;
    }

    bool is(const string& type_name) {
        return components.find(type_name) != components.end();
    }

    bool get_pos(int& x, int &y) {
        if (label) {
            label->get_pos(x, y);
            this->x = x;
            this->y = y;
            return true;
        }

        return false;
    }

    AhkObjRef* get_component(const char* name) {
        if (components.find(name) != components.end())
            return (AhkObjRef*)*components[name].get();
        return nullptr;
    }

    template <typename T> T* get_component() {
        char* component_name;

        /* typeid's name has format 'N<typename>', N is length of the typename string. */
        strtol(typeid(T).name(), &component_name, 0);

        auto i = components.find(component_name);
        if (i != components.end())
            return dynamic_cast<T*>(i->second.get());

        return  nullptr;
    }

    Component* operator[](const string& component_name) {
        return components[component_name].get();
    }

    void list_components() {
        for (string& name : component_names) {
            std::cout << "\t";
            components[name]->to_print();
            std::cout << endl;
        }
    }

    void to_print() {
        wprintf(L"%llx: ", address);
        if (has_component("Render")) {
            wprintf(L"%S", get_component<Render>()->name().c_str());
        }

        if (verbose) {
           wprintf(L"\n    %4x, %S", id, path.c_str());
           if (verbose > 1)
               list_components();
        }
    }
};

class LocalPlayer : public Entity {
public:

    wstring player_name;
    int level;
    Life* life;
    Positioned* positioned;

    LocalPlayer(addrtype address) : Entity(address) {
        Player* player = get_component<Player>();
        life = get_component<Life>();
        positioned = get_component<Positioned>();;

        player_name = player->name();
        level = player->level();
    }

    wstring& name() {
        return player_name;
    }

    int dist(Entity& entity) {
        if (!entity.has_component("Positioned"))
            return -1;

        Point pos1 = positioned->grid_position();
        Point pos2 = entity.get_component<Positioned>()->grid_position();
        int dx = pos1.x - pos2.x;
        int dy = pos1.y - pos2.y;

        return sqrt(dx * dx + dy * dy);
    }
};

class Item : public Entity {
protected:

    Base* base;
    Mods* mods;
    
public:

    Item(addrtype address) : Entity(address) {
        base = get_component<Base>();
        mods = get_component<Mods>();

        add_method(L"Name", this, (MethodType)&Item::name, AhkWString);
        add_method(L"BaseName", this, (MethodType)&Item::base_name, AhkWString);
        add_method(L"IsIdentified", this, (MethodType)&Item::is_identified);
        add_method(L"IsMirrored", this, (MethodType)&Item::is_mirrored);
        add_method(L"IsCorrupted", this, (MethodType)&Item::is_corrupted);
        add_method(L"IsSynthesised", this, (MethodType)&Item::is_synthesised);
        add_method(L"IsRGB", this, (MethodType)&Item::is_rgb);
        add_method(L"Rarity", this, (MethodType)&Item::get_rarity);
        add_method(L"ItemLevel", this, (MethodType)&Item::get_item_level);
        add_method(L"Quality", this, (MethodType)&Item::get_quality);
        add_method(L"Sockets", this, (MethodType)&Item::get_sockets);
        add_method(L"Links", this, (MethodType)&Item::get_links);
        add_method(L"Tier", this, (MethodType)&Item::get_tier);
        add_method(L"Level", this, (MethodType)&Item::get_level);
        add_method(L"StackCount", this, (MethodType)&Item::get_stack_count);
        add_method(L"StackSize", this, (MethodType)&Item::get_stack_size);
        add_method(L"Charges", this, (MethodType)&Item::get_charges);
        add_method(L"Size", this, (MethodType)&Item::get_size);
    }

    void __new() {
    }

    wstring& name() {
        if (!mods || !mods->is_identified() || mods->rarity == 0)   /* normal or unidentified items */
            return base_name();
        return mods->name(base_name());                             /* magic/rare/unique items */
    }

    wstring& base_name() {
        return base ? base->name() : type_name;
    }

    bool is_identified() {
        return mods ? mods->is_identified() : false;
    }

    bool is_mirrored() {
        return mods ? mods->is_mirrored() : false;
    }

    bool is_corrupted() {
        return base ? base->is_corrupted() : false;
    }

    bool is_synthesised() {
        return mods ? mods->is_synthesised() : false;
    }

    int get_item_level() {
        return mods ? mods->item_level : 0;
    }

    int get_rarity() {
        return mods ? mods->rarity : 0;
    }

    int get_sockets() {
        Sockets* sockets = get_component<Sockets>();
        return sockets ? sockets->sockets() : 0;
    }

    int get_links() {
        Sockets* sockets = get_component<Sockets>();
        return sockets ? sockets->links() : 0;
    }

    bool is_rgb() {
        Sockets* sockets = get_component<Sockets>();
        return sockets ? sockets->is_rgb() : false;
    }

    int get_quality() {
        Quality* quality = get_component<Quality>();
        return quality ? quality->quality() : 0;
    }

    int get_tier() {
        Map* map = get_component<Map>();
        return map ? map->tier() : 0;
    }

    int get_level() {
        SkillGem* gem = get_component<SkillGem>();
        return gem ? gem->level() : 0;
    }

    int get_stack_count() {
        Stack* stack = get_component<Stack>();
        return stack ? stack->stack_count() : 0;
    }

    int get_stack_size() {
        Stack* stack = get_component<Stack>();
        return stack ? stack->stack_size() : 0;
    }

    int get_charges() {
        Charges* charges = get_component<Charges>();
        return charges ? charges->charges(): 0;
    }

    int get_size() {
        int w = base->width();
        int h = base->height();
        return (w << 16) & h;
    }
};
