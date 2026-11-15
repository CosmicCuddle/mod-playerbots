// Exact PlayerbotHolder retirement methods, with synthetic native dependencies.
#include "PB59TransactionAdmission.h"
#include <cassert>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <stdexcept>
using A = PB59::TransactionAdmission;
using ObjectGuid = unsigned;
static std::vector<std::string> trace;
static std::function<void()> duringSave;
#define LOG_DEBUG(...) ((void)0)
#define LOG_ERROR(...) ((void)0)
struct Group {};
struct Motion { void Clear(){ trace.push_back("motion"); } };
struct Player;
struct WorldSession;
static Player* connected=nullptr;
struct ObjectAccessor { static Player* FindConnectedPlayer(ObjectGuid){return connected;} };
enum class BotRetirementResult { Refused, Completed, RecoveryRequired };
struct PlayerbotAI
{
    Player* owner;
    Player* GetMaster(){return owner;}
    void TellMaster(std::string const&){trace.push_back("tell");}
    ~PlayerbotAI(){trace.push_back("delete-ai");}
};
struct Player
{
    A admission;
    PlayerbotAI* ai = new PlayerbotAI{this};
    WorldSession* session = nullptr;
    bool taxi = true, hasGroup = true;
    Group group;
    Motion motion;
    Player(){connected=this;}
    auto TryAcquirePB59Retirement(){ return admission.TryAcquireRetirement(); }
    bool PB59RetirementRecoveryRequired(){return admission.RetirementRecoveryRequired();}
    bool OwnsPB59Retirement(A::RetirementLease const& l){return admission.OwnsRetirement(l);}
    WorldSession* GetSession(){return session;}
    std::string GetName(){return "bot";}
    bool isTaxiCheater(){return taxi;}
    void SetTaxiCheater(bool v){taxi=v;trace.push_back("taxi");}
    void SaveToDB(bool,bool){trace.push_back("save");if(duringSave)duringSave();}
    void StopMoving(){trace.push_back("stop");}
    Motion* GetMotionMaster(){return &motion;}
    Group* GetGroup(){return hasGroup ? &group : nullptr;}
    bool InBattleground(){return false;}
    bool InBattlegroundQueue(){return false;}
    ~Player(){if(connected==this)connected=nullptr;delete ai;trace.push_back("delete-player");}
};
struct WorldSession
{
    Player* player;
    bool loggingOut=false;
    Player* GetPlayer(){return player;}
    bool IsLoggingOut(){return loggingOut;}
    void LogoutPlayer(bool){trace.push_back("logout");delete player;player=nullptr;}
    ~WorldSession(){assert(!player);trace.push_back("delete-session");}
};
#define GET_PLAYERBOT_AI(p) ((p)->ai)
struct PlayerbotIdentityGeneration
{
    static PlayerbotIdentityGeneration& Instance(){static PlayerbotIdentityGeneration i;return i;}
    void RetireLogin(ObjectGuid){trace.push_back("retire");}
};
struct BotLogoutGroupCleanupOperation { explicit BotLogoutGroupCleanupOperation(ObjectGuid){} };
struct PlayerbotWorldThreadProcessor
{
    static PlayerbotWorldThreadProcessor& instance(){static PlayerbotWorldThreadProcessor i;return i;}
    void QueueOperation(std::unique_ptr<BotLogoutGroupCleanupOperation>){trace.push_back("group");}
};
struct Random {bool IsRandomBot(Player*){return false;}} sRandomPlayerbotMgr;
struct PlayerbotTextMgr
{
    static PlayerbotTextMgr& instance(){static PlayerbotTextMgr i;return i;}
    std::string GetBotTextOrDefault(std::string const&,std::string const& s,std::vector<int> const&){return s;}
};
struct PlayerbotRepository
{
    static PlayerbotRepository& instance(){static PlayerbotRepository i;return i;}
    void Save(PlayerbotAI*){trace.push_back("repo");}
};
struct PlayerbotHolder
{
    Player* bot=nullptr;
    Player* GetPlayerBot(ObjectGuid g){return g==1?bot:nullptr;}
    bool IsRealPlayer(Player*){return true;}
    bool LogoutPlayerBot(ObjectGuid);
    BotRetirementResult TryLogoutPlayerBot(ObjectGuid,std::function<void()> const& = {});
    BotRetirementResult TryDisablePlayerBot(ObjectGuid);
    bool DisablePlayerBot(ObjectGuid);
    void RemoveFromPlayerbotsMap(ObjectGuid){trace.push_back("erase");bot=nullptr;}
};
#include "retirement_native.inc"
constexpr A::StableProof stable{true,true,true,true};
int main()
{
    PlayerbotHolder holder;
    assert(!holder.LogoutPlayerBot(1) && !holder.DisablePlayerBot(1));
    for (bool recovery : {false,true})
    {
        auto* p=new Player;
        auto* session=new WorldSession{p}; p->session=session; holder.bot=p;
        auto t=p->admission.Begin();assert(t);
        if(recovery)assert(p->admission.RequireRecovery(t));
        trace.clear();
        assert(holder.TryLogoutPlayerBot(1,[]{trace.push_back("schedule");})==BotRetirementResult::Refused);
        assert(!holder.DisablePlayerBot(1));
        assert(trace.empty() && holder.bot==p && session->GetPlayer()==p && p->taxi);
        assert(p->admission.Finish(t,stable));
        // A save callback cannot start gear replacement or recursively retire.
        duringSave=[&]{assert(!p->admission.Begin());assert(!holder.LogoutPlayerBot(1));assert(!holder.DisablePlayerBot(1));};
        assert(holder.TryLogoutPlayerBot(1,[]{trace.push_back("schedule");})==BotRetirementResult::Completed);
        duringSave={};
        assert((trace==std::vector<std::string>{"schedule","retire","group","taxi","save","tell","erase","logout","delete-ai","delete-player","delete-session"}));
        assert(!holder.bot);
    }
    {
        auto* p=new Player;auto* session=new WorldSession{p};p->session=session;holder.bot=p;
        trace.clear();session->loggingOut=true;
        assert(holder.TryLogoutPlayerBot(1,[]{trace.push_back("schedule");})==BotRetirementResult::Refused);
        assert(trace.empty() && p->admission.CanBegin());
        session->loggingOut=false;
        {
            A::ExternalScope save(p->admission,A::Work::StandaloneSave);
            assert(!holder.LogoutPlayerBot(1) && !holder.DisablePlayerBot(1));
            assert(trace.empty());
        }
        auto t=p->admission.Begin();assert(p->admission.Request(A::Work::StandaloneSave)==A::Decision::DeferStandaloneSave);
        assert(p->admission.Finish(t,stable));
        assert(!holder.LogoutPlayerBot(1) && !holder.DisablePlayerBot(1));
        {A::ExternalScope save(p->admission,A::Work::StandaloneSave);assert(save.CompleteDeferred());}
        duringSave=[&]{assert(!p->admission.Begin());assert(!holder.DisablePlayerBot(1));};
        assert(holder.DisablePlayerBot(1));duringSave={};
        assert((trace==std::vector<std::string>{"retire","tell","stop","motion","repo","save","erase","delete-ai"}));
        assert(p->admission.CanBegin() && !holder.bot && session->player==p);
        p->ai=nullptr;session->LogoutPlayer(true);delete session;
    }
    // Distinguish partial effects from a clean refusal. No blind replay.
    for(int failure=0;failure<4;++failure)
    {
        auto* p=new Player;auto* session=new WorldSession{p};p->session=session;holder.bot=p;
        trace.clear();
        if(failure==0)
        {
            assert(holder.TryLogoutPlayerBot(1,[]{trace.push_back("schedule");throw std::runtime_error("bookkeeping");})==BotRetirementResult::RecoveryRequired);
            assert(trace==std::vector<std::string>{"schedule"});
        }
        else if(failure==1)
        {
            duringSave=[&]{session->loggingOut=true;};
            assert(holder.TryLogoutPlayerBot(1)==BotRetirementResult::RecoveryRequired);
            assert(trace.back()=="save");
        }
        else if(failure==2)
        {
            duringSave=[&]{connected=nullptr;};
            assert(holder.TryLogoutPlayerBot(1)==BotRetirementResult::RecoveryRequired);
            assert(trace.back()=="save");connected=p;
        }
        else
        {
            duringSave=[] {throw std::runtime_error("save");};
            assert(holder.TryDisablePlayerBot(1)==BotRetirementResult::RecoveryRequired);
            assert(trace.back()=="save");
        }
        duringSave={};
        assert(p->admission.RetirementRecoveryRequired()&&!p->admission.CanBegin());
        auto count=trace.size();
        assert(holder.TryLogoutPlayerBot(1)==BotRetirementResult::RecoveryRequired);
        assert(holder.TryDisablePlayerBot(1)==BotRetirementResult::RecoveryRequired);
        assert(trace.size()==count&&holder.bot==p&&session->player==p);
        // Test teardown only: native recovery/release is deliberately not supplied.
        holder.bot=nullptr;session->LogoutPlayer(true);delete session;
    }
}
