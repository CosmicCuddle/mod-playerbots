#pragma once
#include "PB59InventorySlotFacts.h"
#include <map>
#include <functional>
#include <vector>
#include <set>

constexpr unsigned EQUIPMENT_SLOT_START=0, EQUIPMENT_SLOT_END=19, EQUIPMENT_SLOT_BODY=3, EQUIPMENT_SLOT_TABARD=18;
constexpr unsigned INVENTORY_SLOT_BAG_0=255, INVENTORY_SLOT_BAG_START=19, INVENTORY_SLOT_BAG_END=23;
constexpr unsigned INVENTORY_SLOT_ITEM_START=23, INVENTORY_SLOT_ITEM_END=39, MAX_BAG_SIZE=36;
enum InventoryResult { EQUIP_ERR_OK, EQUIP_ERR_OTHER };
struct ObjectGuid { std::uint64_t value; auto GetRawValue() const { return value; } };
struct Item { PB59::InventoryInstanceFacts facts; auto GetGUID() const { return ObjectGuid{facts.guid}; } };
struct Bag : Item { unsigned size=1; auto GetBagSize() const { return size; } };
struct WorldSession { std::uint64_t incarnation=2; auto GetPB59SessionIncarnation() const {return incarnation;} };
struct ItemPosCount { std::uint16_t pos; std::uint32_t count; };
using ItemPosCountVec=std::vector<ItemPosCount>;
struct Player {
    std::uint64_t owner=1;
    WorldSession session;
    std::map<std::uint16_t,Item> items;
    std::map<std::uint8_t,Bag> bags;
    std::set<std::uint16_t> unknown;
    bool admit=true, live=true, wrongCount=false, wrongPosition=false, split=false, unequip=true;
    std::function<void()> onUnequip, onStore;
    std::map<std::uint64_t,std::set<std::uint16_t>> allowed;
    struct Scope { Player* p; bool Admitted() const {return p->admit;} };
    auto AcquirePB59CallbackScope() {return Scope{this};}
    auto GetPB59OwnerIncarnation() const {return owner;}
    WorldSession const* GetSession() const {return live?&session:nullptr;}
    Item* GetItemByPos(std::uint16_t pos) {auto i=items.find(pos);return i==items.end()?nullptr:&i->second;}
    Item* GetItemByPos(std::uint8_t bag,std::uint8_t slot) {if(bag==255 && bags.contains(slot))return &bags.at(slot);return GetItemByPos((std::uint16_t(bag)<<8)|slot);}
    Bag* GetBagByPos(std::uint8_t bag) {return bags.contains(bag)?&bags.at(bag):nullptr;}
    std::optional<PB59::InventorySlotFacts> CapturePB59InventorySlot(std::uint16_t pos) const {
        if(!live||unknown.contains(pos))return {};
        auto bag=pos>>8; auto slot=pos&255;
        if(bag!=255 && (!bags.contains(bag)||slot>=int(bags.at(bag).size)))return {};
        PB59::InventorySlotFacts out{11,bag==255?0:bags.at(bag).facts.guid,pos,{}};
        auto i=items.find(pos);if(i!=items.end())out.item=i->second.facts;
        return out;
    }
    InventoryResult CanUnequipItem(std::uint16_t,bool) {if(onUnequip)onUnequip();return unequip?EQUIP_ERR_OK:EQUIP_ERR_OTHER;}
    InventoryResult CanStoreItem(std::uint8_t bag,std::uint8_t slot,ItemPosCountVec& places,Item* item,bool) {
        std::uint16_t pos=(std::uint16_t(bag)<<8)|slot; auto guid=item->facts.guid; auto count=item->facts.count;
        if(onStore)onStore();
        if(!allowed[guid].contains(pos))return EQUIP_ERR_OTHER;
        places.push_back({std::uint16_t(pos+(wrongPosition?1:0)),count+(wrongCount?1u:0u)});
        if(split)places.push_back({std::uint16_t(pos+1),1});
        return EQUIP_ERR_OK;
    }
    void Put(std::uint16_t pos,std::uint64_t guid) {PB59::InventoryInstanceFacts f;f.guid=guid;f.owner=11;f.entry=42;f.count=1;items[pos]=Item{f};}
};
