#pragma once
#include <cstdint>
#include <memory>
#include <map>
#include <stdexcept>
struct ObjectGuid {
    std::uint64_t value=0;
    auto GetRawValue() const{return value;}
    explicit operator bool() const{return value!=0;}
    bool IsEmpty()const{return value==0;}
    bool operator==(ObjectGuid const&)const=default;
};
struct ItemTemplate {std::uint32_t ItemLevel=60,Quality=3;};
constexpr unsigned ITEM_FIELD_CONTAINED=1,NULL_SLOT=255,ITEM_NEW=0;
struct Item {
    static inline unsigned alive=0,created=0,failEntry=0,throwEntry=0;
    static inline bool duplicateGuid=false,wrongProperty=false,queued=false,world=false,owned=false;
    static inline std::map<unsigned,ItemTemplate> templates;
    ObjectGuid guid,owner;
    unsigned entry=0,count=1,slot=NULL_SLOT;
    std::int32_t property=0;
    Item(){++alive;} ~Item(){--alive;}
    static std::unique_ptr<Item> CreatePB59Detached(std::uint32_t e,std::int32_t p){
        if(e==throwEntry)throw std::runtime_error("injected allocation failure");
        if(e==failEntry||!templates.contains(e))return {};
        auto i=std::make_unique<Item>();++created;i->guid={duplicateGuid?100u:100u+created};i->entry=e;i->property=wrongProperty?p+1:p;i->owner={owned?22u:0u};return i;
    }
    auto GetGUID()const{return guid;}auto GetOwnerGUID()const{return owner;}ObjectGuid GetGuidValue(unsigned)const{return {};}
    auto GetEntry()const{return entry;}auto GetCount()const{return count;}auto GetSlot()const{return slot;}auto GetItemRandomPropertyId()const{return property;}
    ItemTemplate const* GetTemplate()const{auto i=templates.find(entry);return i==templates.end()?nullptr:&i->second;}
    bool IsBag()const{return false;}bool IsInWorld()const{return world;}bool IsInUpdateQueue()const{return queued;}unsigned GetState()const{return ITEM_NEW;}void* GetContainer()const{return nullptr;}
};
