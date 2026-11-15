// Exact native bulk logout body; registered players and holder are synthetic.
#include "AutoGearRecoveryRegistry.h"
#include "PB59TransactionAdmission.h"
#include <cassert>
#include <functional>
#include <map>
#include <memory>
#include <vector>
using Journal=PB59::RecoveryRegistry;
struct ObjectGuid { unsigned value; ObjectGuid(unsigned v=0):value(v){} auto operator<=>(ObjectGuid const&)const=default; std::string ToString()const{return std::to_string(value);} };
struct PlayerbotAI{};
struct Player { PB59::TransactionAdmission admission; PlayerbotAI ai; bool self=false; bool hasAI=true; std::uint64_t GetPB59OwnerIncarnation(){return admission.OwnerIncarnation();} };
static std::map<ObjectGuid,Player*> registered;
struct ObjectAccessor { static Player* FindConnectedPlayer(ObjectGuid guid){auto it=registered.find(guid);return it==registered.end()?nullptr:it->second;} };
#define GET_PLAYERBOT_AI(p) ((p)->hasAI?&(p)->ai:nullptr)
#define LOG_ERROR(...) ((void)0)
enum class BotRetirementResult { Refused, Completed, RecoveryRequired };
static std::vector<ObjectGuid> called;
static std::function<BotRetirementResult(ObjectGuid)> onLogout;
struct PlayerbotHolder
{
    Journal::HolderLifetime _pb59HolderLifetime;
    std::map<ObjectGuid,Player*> playerBots;
    Player* GetPlayerBot(ObjectGuid guid){auto it=playerBots.find(guid);return it==playerBots.end()?nullptr:it->second;}
    bool IsSelfBot(Player* p){return p->self;}
    BotRetirementResult TryLogoutPlayerBot(ObjectGuid guid){called.push_back(guid);return onLogout(guid);}
    void LogoutAllBots();
};
#include "LogoutAllBots.inc"
int main()
{
    // The first callback destroys/replaces a later bot under the same GUID.
    {PlayerbotHolder holder;Player a,c,d;auto b=std::make_unique<Player>();
     holder.playerBots={{1,&a},{2,b.get()},{3,&c},{4,&d}};registered=holder.playerBots;c.self=true;d.hasAI=false;
     auto old=b->GetPB59OwnerIncarnation();onLogout=[&](ObjectGuid guid){assert(guid==1);b.reset();b=std::make_unique<Player>();assert(b->GetPB59OwnerIncarnation()!=old);registered[2]=b.get();holder.playerBots[2]=b.get();return BotRetirementResult::Completed;};
     holder.LogoutAllBots();assert(called==std::vector<ObjectGuid>{1});called.clear();registered.clear();}
    // An unchanged registered owner is processed; a stale map entry is skipped.
    {PlayerbotHolder holder;Player a,b,stale;holder.playerBots={{1,&a},{2,&b},{3,&stale}};registered={{1,&a},{2,&b}};
     onLogout=[](ObjectGuid guid){return guid==1?BotRetirementResult::Refused:BotRetirementResult::RecoveryRequired;};
     holder.LogoutAllBots();assert((called==std::vector<ObjectGuid>{1,2}));called.clear();registered.clear();}
    // Holder destruction during the first callback must stop the bulk loop.
    {auto holder=std::make_unique<PlayerbotHolder>();Player a,b;holder->playerBots={{1,&a},{2,&b}};registered=holder->playerBots;
     onLogout=[&](ObjectGuid guid){assert(guid==1);holder.reset();return BotRetirementResult::RecoveryRequired;};
     auto* original=holder.get();original->LogoutAllBots();assert(!holder&&called==std::vector<ObjectGuid>{1});called.clear();registered.clear();}
    onLogout={};
}
