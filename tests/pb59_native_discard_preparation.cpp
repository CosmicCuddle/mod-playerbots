#include "AutoGearNativeDiscardPreparation.h"
#include <cassert>
#include <iostream>
using namespace PlayerbotsAutoGearStorage;
constexpr auto head=std::uint16_t(0xff00), neck=std::uint16_t(0xff01), shirt=std::uint16_t(0xff03), tabard=std::uint16_t(0xff12);
Player setup(){Player p;p.Put(head,100);p.Put(neck,101);p.Put(shirt,102);p.Put(tabard,103);for(unsigned i=23;i<39;++i)p.Put(0xff00|i,1000+i);return p;}
int main(){unsigned n=0;
#define CHECK(x) do { assert(x); ++n; } while(false)
auto random=[](Player const&){return true;};auto notRandom=[](Player const&){return false;};
{auto p=setup();auto a=PrepareNativeRandomDiscard(p,random);CHECK(a.has_value());CHECK(a->equipment.size()==17);CHECK(a->equipment[0].item->guid==100);CHECK(a->equipment[1].item->guid==101);CHECK(a->equipment[3].position==0xff04);CHECK(a->equipment.back().position==0xff11);CHECK(NativeDiscardCurrent(p,*a,random));CHECK(p.items.contains(shirt)&&p.items.contains(tabard));CHECK(p.items.size()==20);CHECK(!NativeDiscardCurrent(p,*a,notRandom));}
{auto p=setup();CHECK(!PrepareNativeRandomDiscard(p,notRandom));}
{auto p=setup();p.admit=false;CHECK(!PrepareNativeRandomDiscard(p,random));}
{auto p=setup();p.unequip=false;CHECK(!PrepareNativeRandomDiscard(p,random));}
{auto p=setup();p.unknown.insert(head);CHECK(!PrepareNativeRandomDiscard(p,random));}
{auto p=setup();p.unknown.insert(0xff17);CHECK(PrepareNativeRandomDiscard(p,random).has_value());}
{auto p=setup();p.onUnequip=[&]{p.Put(head,999);};CHECK(!PrepareNativeRandomDiscard(p,random));}
{auto p=setup();p.onUnequip=[&]{++p.session.incarnation;};CHECK(!PrepareNativeRandomDiscard(p,random));}
{auto p=setup();p.onUnequip=[&]{++p.owner;};CHECK(!PrepareNativeRandomDiscard(p,random));}
{auto p=setup();p.onUnequip=[&]{p.live=false;};CHECK(!PrepareNativeRandomDiscard(p,random));}
{auto p=setup();bool verified=true;auto check=[&](Player const&){return verified;};p.onUnequip=[&]{verified=false;};CHECK(!PrepareNativeRandomDiscard(p,check));}
{auto p=setup();auto a=PrepareNativeRandomDiscard(p,random);p.Put(0xff02,777);CHECK(!NativeDiscardCurrent(p,*a,random));}
{auto p=setup();auto a=PrepareNativeRandomDiscard(p,random);p.items[head].facts.randomProperty=8;CHECK(!NativeDiscardCurrent(p,*a,random));}
{auto p=setup();auto a=PrepareNativeRandomDiscard(p,random);a->equipment[0].position=shirt;CHECK(!NativeDiscardCurrent(p,*a,random));}
{auto p=setup();auto a=PrepareNativeRandomDiscard(p,random);p.items.erase(head);p.items.erase(neck);CHECK(!NativeDiscardCurrent(p,*a,random));auto empty=PrepareNativeRandomDiscard(p,random);CHECK(empty.has_value());CHECK(!empty->equipment[0].item&&!empty->equipment[1].item);}
{auto p=setup();p.onUnequip=[](){throw 1;};CHECK(!PrepareNativeRandomDiscard(p,random));}
std::cout<<n<<" checks PASS\n";}
