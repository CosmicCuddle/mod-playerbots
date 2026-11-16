#pragma once
#include "PB59InventorySlotFacts.h"
#include "Item.h"
#include <functional>
#include <map>
constexpr unsigned EQUIPMENT_SLOT_START=0,EQUIPMENT_SLOT_END=19,EQUIPMENT_SLOT_BODY=3,EQUIPMENT_SLOT_TABARD=18,INVENTORY_SLOT_BAG_0=255;
enum InventoryResult {EQUIP_ERR_OK,EQUIP_ERR_OTHER};
struct WorldSession {std::uint64_t incarnation=2;auto GetPB59SessionIncarnation()const{return incarnation;}};
struct Player {
    std::uint64_t owner=1;WorldSession session;bool live=true,admit=true,random=true,use=true,unequip=true;
    std::map<std::uint16_t,PB59::InventoryInstanceFacts> items;
    std::function<void(Item*)> onUse;std::function<void()> onUnequip;
    struct Scope {Player* p;bool Admitted()const{return p->admit;}};
    auto AcquirePB59CallbackScope(){return Scope{this};}auto GetPB59OwnerIncarnation()const{return owner;}
    WorldSession const* GetSession()const{return live?&session:nullptr;}
    std::optional<PB59::InventorySlotFacts> CapturePB59InventorySlot(std::uint16_t pos)const{
        if(!live)return {};
        PB59::InventorySlotFacts out{11,0,pos,{}};auto i=items.find(pos);if(i!=items.end())out.item=i->second;return out;
    }
    InventoryResult CanUnequipItem(std::uint16_t,bool){if(onUnequip)onUnequip();return unequip?EQUIP_ERR_OK:EQUIP_ERR_OTHER;}
    InventoryResult CanUseItem(Item* i,bool){if(onUse)onUse(i);return use?EQUIP_ERR_OK:EQUIP_ERR_OTHER;}
    void Put(std::uint16_t pos,std::uint64_t guid){PB59::InventoryInstanceFacts f;f.guid=guid;f.owner=11;f.entry=42;f.count=1;items[pos]=f;}
};
