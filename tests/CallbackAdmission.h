#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
namespace PB59 {
// Owner-context only. All native destruction routes must honor this gate.
class CallbackAdmission {
    struct State { unsigned callbacks=0; bool retiring=false, closed=false; };
    std::shared_ptr<State> state=std::make_shared<State>();
public:
    enum class Kind { Callback, Retirement };
    class Lease {
        friend class CallbackAdmission;
        std::shared_ptr<State> s;
        Kind kind;
        Lease(std::shared_ptr<State> p, Kind k):s(std::move(p)),kind(k){}
    public:
        Lease(Lease const&)=delete;
        Lease& operator=(Lease const&)=delete;
        Lease(Lease&&)=default;
        Lease& operator=(Lease&&)=delete;
        ~Lease(){ if(s) { if(kind==Kind::Callback) --s->callbacks; else s->retiring=false; } }
        void Close(){ if(s && kind==Kind::Retirement) s->closed=true; }
    };
    CallbackAdmission()=default;
    CallbackAdmission(CallbackAdmission const&)=delete;
    CallbackAdmission& operator=(CallbackAdmission const&)=delete;
    std::optional<Lease> TryCallback(){
        if(state->closed || state->retiring || state->callbacks==static_cast<unsigned>(-1)) return {};
        ++state->callbacks; return Lease(state,Kind::Callback);
    }
    std::optional<Lease> TryRetirement(){
        if(state->closed || state->retiring || state->callbacks) return {};
        state->retiring=true; return Lease(state,Kind::Retirement);
    }
};
}
