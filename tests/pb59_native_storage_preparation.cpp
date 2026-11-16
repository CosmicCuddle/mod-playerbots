#include "AutoGearNativeStoragePreparation.h"
#include <cassert>
#include <iostream>
using namespace PlayerbotsAutoGearStorage;
constexpr auto eq0=std::uint16_t(0xff00),eq1=std::uint16_t(0xff01),bag0=std::uint16_t(0x1300),pack=std::uint16_t(0xff17);
Player setup(){Player p;p.Put(eq0,100);p.Put(eq1,101);p.Put(0xff03,102);p.Put(0xff12,103);p.bags[19].facts.guid=200;for(unsigned i=24;i<39;++i)p.Put(0xff00|i,1000+i);p.allowed[100]={pack,bag0};p.allowed[101]={pack};return p;}
int main(){unsigned n=0;
#define CHECK(x) do { assert(x); ++n; } while(false)
{auto p=setup();auto a=PrepareNativeStorage(p);CHECK(a.status==NativePreparationStatus::Ready);CHECK(a.plan.moves.size()==2);CHECK(a.plan.moves[0].position.destination==bag0);CHECK(a.plan.moves[0].destinationBagGuid==200);CHECK(a.plan.moves[1].position.destination==pack);CHECK(NativeStorageCurrent(p,a.plan));CHECK(p.items.contains(eq0)&&p.items.contains(eq1)&&p.items.contains(0xff03)&&p.items.contains(0xff12));p.bags[19].facts.guid=201;CHECK(!NativeStorageCurrent(p,a.plan));}
{auto p=setup();p.Put(pack,999);auto a=PrepareNativeStorage(p);CHECK(a.status==NativePreparationStatus::NoSpace);CHECK(a.plan.moves.empty());}
{auto p=setup();p.unknown.insert(eq0);CHECK(PrepareNativeStorage(p).status==NativePreparationStatus::Rejected);}
{auto p=setup();p.unknown.insert(pack);CHECK(PrepareNativeStorage(p).status==NativePreparationStatus::Rejected);}
{auto p=setup();p.admit=false;CHECK(PrepareNativeStorage(p).status==NativePreparationStatus::Rejected);}
{auto p=setup();p.unequip=false;CHECK(PrepareNativeStorage(p).status==NativePreparationStatus::Rejected);}
{auto p=setup();p.onUnequip=[&]{p.Put(pack,999);};CHECK(PrepareNativeStorage(p).status==NativePreparationStatus::Rejected);}
{auto p=setup();p.onUnequip=[&]{++p.session.incarnation;};CHECK(PrepareNativeStorage(p).status==NativePreparationStatus::Rejected);}
{auto p=setup();p.onStore=[&]{p.items[eq0].facts.randomProperty=4;};CHECK(PrepareNativeStorage(p).status==NativePreparationStatus::Rejected);}
{auto p=setup();p.onStore=[&]{p.live=false;};CHECK(PrepareNativeStorage(p).status==NativePreparationStatus::Rejected);}
{auto p=setup();p.wrongCount=true;CHECK(PrepareNativeStorage(p).status==NativePreparationStatus::NoSpace);}
{auto p=setup();p.wrongPosition=true;CHECK(PrepareNativeStorage(p).status==NativePreparationStatus::NoSpace);}
{auto p=setup();p.split=true;CHECK(PrepareNativeStorage(p).status==NativePreparationStatus::NoSpace);}
{auto p=setup();p.items.erase(eq0);p.items.erase(eq1);auto a=PrepareNativeStorage(p);CHECK(a.status==NativePreparationStatus::Ready);CHECK(a.plan.moves.empty());CHECK(a.plan.equipment.size()==17);}
{auto p=setup();auto a=PrepareNativeStorage(p);++p.owner;CHECK(!NativeStorageCurrent(p,a.plan));}
{auto p=setup();auto a=PrepareNativeStorage(p);++p.session.incarnation;CHECK(!NativeStorageCurrent(p,a.plan));}
{auto p=setup();auto a=PrepareNativeStorage(p);p.Put(0xff02,555);CHECK(!NativeStorageCurrent(p,a.plan));}
{auto p=setup();p.bags[19].size=MAX_BAG_SIZE+1;CHECK(PrepareNativeStorage(p).status==NativePreparationStatus::Rejected);}
std::cout<<n<<" checks PASS\n";}
