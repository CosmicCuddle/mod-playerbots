#include "CallbackAdmission.h"
#include <cassert>
#include <stdexcept>
using PB59::CallbackAdmission;
int main(){
 CallbackAdmission g;
 {auto a=g.TryCallback(); assert(a); assert(!g.TryRetirement());
  {auto b=g.TryCallback(); assert(b); assert(!g.TryRetirement());}
  assert(!g.TryRetirement());}
 {auto r=g.TryRetirement(); assert(r); assert(!g.TryCallback()); assert(!g.TryRetirement());}
 try {auto a=g.TryCallback(); assert(a); throw std::runtime_error("fault");} catch(...){}
 {auto r=g.TryRetirement(); assert(r); r->Close();}
 assert(!g.TryCallback()); assert(!g.TryRetirement());
 CallbackAdmission h; auto a=h.TryCallback(); auto moved=std::move(a); a.reset(); assert(!h.TryRetirement()); moved.reset(); assert(h.TryRetirement());
 std::optional<CallbackAdmission::Lease> surviving;
 {auto owner=std::make_unique<CallbackAdmission>(); auto l=owner->TryCallback(); surviving.emplace(std::move(*l));}
 surviving.reset();
}
