// Standalone C++20 prototype; not native wiring or a lifetime pin.
#include <cstdint>
#include <utility>
#include <cassert>
#include <optional>
#include <stdexcept>
namespace PB59 {
struct CallbackIdentity {
    std::uint64_t guid, player, session, holder;
    bool operator==(CallbackIdentity const&) const = default;
};
enum class CallbackResult { Completed, IdentityLost, Interrupted };
template<class Resolve, class Invoke>
CallbackResult CheckedCallback(CallbackIdentity const& expected, Resolve&& resolve, Invoke&& invoke) {
    try {
        if (!expected.guid || !expected.player || !expected.session || !expected.holder || resolve() != expected)
            return CallbackResult::IdentityLost;
        std::forward<Invoke>(invoke)();
        return resolve() == expected ? CallbackResult::Completed : CallbackResult::IdentityLost;
    } catch (...) { return CallbackResult::Interrupted; }
}
}
using namespace PB59;
int main(){
 CallbackIdentity id{1,2,3,4}; std::optional<CallbackIdentity> live=id; int calls=0;
 auto resolve=[&]{return live;};
 assert(CheckedCallback(id,resolve,[&]{++calls;})==CallbackResult::Completed);
 live.reset(); assert(CheckedCallback(id,resolve,[&]{++calls;})==CallbackResult::IdentityLost); assert(calls==1);
 live=id; assert(CheckedCallback(id,resolve,[&]{live->session=5;})==CallbackResult::IdentityLost);
 live=id; assert(CheckedCallback(id,resolve,[&]{live->holder=6;})==CallbackResult::IdentityLost);
 live=id; assert(CheckedCallback(id,resolve,[&]{live->player=7;})==CallbackResult::IdentityLost);
 live=id; assert(CheckedCallback(id,resolve,[&]{throw std::runtime_error("fault");})==CallbackResult::Interrupted);
 live=id; assert(CheckedCallback(id,resolve,[&]{live.reset();})==CallbackResult::IdentityLost);
 assert(CheckedCallback(CallbackIdentity{0,2,3,4},resolve,[&]{++calls;})==CallbackResult::IdentityLost);
}
