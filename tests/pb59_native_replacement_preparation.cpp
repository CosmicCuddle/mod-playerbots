#include "AutoGearNativeReplacementPreparation.h"
#include <cassert>
#include <iostream>
using namespace PlayerbotsAutoGearStorage;
int main(){unsigned n=0;
#define CHECK(x) do {assert(x);++n;}while(false)
Item::templates={{11,{60,3}},{12,{66,4}},{13,{90,3}},{14,{60,5}}};
Player p;p.Put(0xff00,1);p.Put(0xff01,2);p.Put(0xff03,3);auto random=[](Player const& b){return b.random;};
auto old=PrepareNativeRandomDiscard(p,random);CHECK(old.has_value());auto saved=p.items;
NativeReplacementSelection s;s.cap=66;s.quality=4;s.eligibleMask=s.requiredMask=3;s.pieces[0]={11,101};s.pieces[1]={12,-201};
auto prepare=[&](NativeReplacementSelection const& x){return PrepareNativeRandomReplacement(p,*old,x,random);};
{auto a=prepare(s);CHECK(a.has_value());CHECK(a->selectedMask==3);CHECK(a->items[0]->GetEntry()==11&&a->items[0]->GetItemRandomPropertyId()==101);CHECK(a->items[1]->GetEntry()==12&&a->items[1]->GetItemRandomPropertyId()==-201);CHECK(!a->items[3]);CHECK(Item::alive==2);CHECK(p.items==saved);}
CHECK(Item::alive==0);auto bad=s;bad.pieces[1]={};CHECK(!prepare(bad));bad=s;bad.pieces[3]={11,0};CHECK(!prepare(bad));bad=s;bad.pieces[2]={0,1};CHECK(!prepare(bad));bad=s;bad.cap=0;CHECK(!prepare(bad));bad=s;bad.requiredMask=0;CHECK(!prepare(bad));bad=s;bad.eligibleMask|=1u<<18;CHECK(!prepare(bad));bad=s;bad.pieces[1]={13,0};CHECK(!prepare(bad));CHECK(Item::alive==0);bad=s;bad.pieces[1]={14,0};CHECK(!prepare(bad));CHECK(Item::alive==0);
Item::failEntry=12;CHECK(!prepare(s));CHECK(Item::alive==0);Item::failEntry=0;Item::throwEntry=12;CHECK(!prepare(s));CHECK(Item::alive==0);Item::throwEntry=0;
Item::duplicateGuid=true;CHECK(!prepare(s));CHECK(Item::alive==0);Item::duplicateGuid=false;
{Player q;q.Put(0xff00,100);auto original=PrepareNativeRandomDiscard(q,random);Item::duplicateGuid=true;CHECK(original.has_value());CHECK(!PrepareNativeRandomReplacement(q,*original,s,random));CHECK(Item::alive==0);Item::duplicateGuid=false;}
Item::wrongProperty=true;CHECK(!prepare(s));Item::wrongProperty=false;Item::queued=true;CHECK(!prepare(s));Item::queued=false;Item::owned=true;CHECK(!prepare(s));Item::owned=false;
p.use=false;CHECK(!prepare(s));p.use=true;p.admit=false;CHECK(!prepare(s));p.admit=true;p.random=false;CHECK(!prepare(s));p.random=true;
p.onUse=[&](Item*){++p.session.incarnation;};CHECK(!prepare(s));--p.session.incarnation;p.onUse={};
p.onUse=[&](Item*){p.items[0xff00].randomProperty=77;};CHECK(!prepare(s));p.items=saved;p.onUse={};
p.onUse=[&](Item* i){i->count=2;};CHECK(!prepare(s));p.onUse={};
p.onUse=[&](Item*){p.random=false;};CHECK(!prepare(s));p.random=true;p.onUse={};
CHECK(Item::alive==0);CHECK(p.items==saved);CHECK(prepare(s).has_value());CHECK(Item::alive==0);
std::cout<<n<<" checks PASS\n";}
