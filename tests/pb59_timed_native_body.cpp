// Exact source extracts with synthetic game dependencies. No native server run.
#include "AutoGearTimedRetirementMailbox.h"
#include "AutoGearRecoveryRegistry.h"
#include <cassert>
#include <ctime>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

struct ObjectGuid
{
    std::uint64_t value;
    std::uint64_t GetRawValue() const { return value; }
    auto operator<=>(ObjectGuid const&) const = default;
    std::string ToString() const { return std::to_string(value); }
};
#define LOG_ERROR(...) ((void)0)
constexpr int PLAYER_FLAGS=1, PLAYER_FLAGS_RESTING=2, UNIT_STATE_IN_FLIGHT=3;
namespace rbac { constexpr int RBAC_PERM_INSTANT_LOGOUT=4; }
struct Player;
struct WorldSession
{
    std::uint64_t id;
    Player* player=nullptr;
    bool headless=true, active=false, timer=false, ready=false, permission=false;
    bool IsHeadless()const{return headless;} Player* GetPlayer()const{return player;}
    bool PlayerLogout()const{return active;} bool IsLoggingOut()const{return active||timer;}
    bool ShouldLogOut(std::time_t)const{return timer&&ready;}
    bool HasPermission(int)const{return permission;}
    std::uint64_t GetPB59SessionIncarnation()const{return id;}
};
struct Player
{
    ObjectGuid guid; std::uint64_t incarnation; WorldSession* session;
    bool inWorld=true, resting=false, flight=false;
    ObjectGuid GetGUID()const{return guid;} std::uint64_t GetPB59OwnerIncarnation()const{return incarnation;}
    WorldSession* GetSession()const{return session;} bool IsInWorld()const{return inWorld;}
    bool HasFlag(int,int)const{return resting;} bool HasUnitState(int)const{return flight;}
};
struct PlayerbotAI { Player* bot; Player* master=nullptr; bool IsTimedLogoutReady(); };
static std::map<ObjectGuid,Player*> players;
static std::map<Player*,PlayerbotAI*> ais;
struct ObjectAccessor { static Player* FindConnectedPlayer(ObjectGuid g){auto i=players.find(g);return i==players.end()?nullptr:i->second;} };
struct PlayerbotMgr;
static std::map<Player*,PlayerbotMgr*> managers;
#define GET_PLAYERBOT_MGR(p) (managers.contains(p)?managers.at(p):nullptr)
#define GET_PLAYERBOT_AI(p) (ais.contains(p)?ais.at(p):nullptr)
enum class BotRetirementResult { Refused, Completed, RecoveryRequired };
struct PlayerbotHolder
{
    PB59::RecoveryRegistry::HolderLifetime _pb59HolderLifetime;
    std::map<ObjectGuid,Player*> bots;
    unsigned calls=0;
    BotRetirementResult next=BotRetirementResult::Completed;
    bool throws=false;
    virtual ~PlayerbotHolder();
    bool RegisterTimedBot(Player*);
    void ForgetTimedBot(ObjectGuid,std::uint64_t,std::uint64_t);
    static void RequestTimedBotLogout(Player*); static void ProcessTimedBotLogouts(); static void StopTimedBotLogouts();
    Player* GetPlayerBot(ObjectGuid g){auto i=bots.find(g);return i==bots.end()?nullptr:i->second;}
    BotRetirementResult TryLogoutPlayerBot(ObjectGuid g,std::function<void()> const& callback,bool timed)
    {
        assert(timed&&!callback); auto* p=GetPlayerBot(g); assert(p&&GET_PLAYERBOT_AI(p)->IsTimedLogoutReady());++calls;
        if(throws)throw std::runtime_error("retirement fault");
        if(next==BotRetirementResult::Completed){ForgetTimedBot(g,p->incarnation,p->session->id);bots.erase(g);}
        return next;
    }
};
struct PlayerbotMgr:PlayerbotHolder { explicit PlayerbotMgr(Player* p):master(p){} Player* master; Player* GetMaster(){return master;} };
struct RandomPlayerbotMgr:PlayerbotHolder { static RandomPlayerbotMgr& instance(){static RandomPlayerbotMgr r;return r;} };
#define sRandomPlayerbotMgr RandomPlayerbotMgr::instance()
#include "ready_native.inc"
#include "timed_native.inc"

struct Bot
{
    WorldSession session;
    Player player;
    PlayerbotAI ai;
    Bot(std::uint64_t guid,std::uint64_t incarnation,std::uint64_t id,Player* master=nullptr):session{id},player{{guid},incarnation,&session},ai{&player,master}
    {session.player=&player;session.timer=true;session.ready=true;players[player.guid]=&player;ais[&player]=&ai;}
    ~Bot(){if(ObjectAccessor::FindConnectedPlayer(player.guid)==&player)players.erase(player.guid);ais.erase(&player);}
    void Register(PlayerbotHolder& h){h.bots[player.guid]=&player;assert(h.RegisterTimedBot(&player));}
    void Request(){PlayerbotHolder::RequestTimedBotLogout(&player);}
};
int main()
{
    using H=PlayerbotHolder;using M=PB59::TimedRetirementMailbox;
    auto& random=sRandomPlayerbotMgr;auto& mailbox=M::Instance();
    Bot master(100,200,300);master.session.headless=false;
    Bot bot(1,2,3,&master.player);bot.Register(random);
    assert(bot.ai.IsTimedLogoutReady());bot.session.timer=false;assert(!bot.ai.IsTimedLogoutReady());
    bot.session.timer=true;bot.session.active=true;assert(!bot.ai.IsTimedLogoutReady());bot.session.active=false;
    bot.session.headless=false;assert(!bot.ai.IsTimedLogoutReady());bot.session.headless=true;
    bot.session.ready=false;assert(!bot.ai.IsTimedLogoutReady());
    bot.player.resting=true;assert(bot.ai.IsTimedLogoutReady());bot.player.resting=false;
    bot.player.flight=true;assert(bot.ai.IsTimedLogoutReady());bot.player.flight=false;
    bot.session.permission=true;assert(bot.ai.IsTimedLogoutReady());bot.session.permission=false;
    master.player.resting=true;assert(bot.ai.IsTimedLogoutReady());master.player.resting=false;
    master.player.flight=true;assert(bot.ai.IsTimedLogoutReady());master.player.flight=false;
    master.session.permission=true;assert(bot.ai.IsTimedLogoutReady());master.session.permission=false;
    master.player.inWorld=false;assert(bot.ai.IsTimedLogoutReady());master.player.inWorld=true;
    bot.ai.master=nullptr;assert(bot.ai.IsTimedLogoutReady());bot.ai.master=&master.player;
    bot.session.player=nullptr;assert(!bot.ai.IsTimedLogoutReady());bot.session.player=&bot.player;
    // Cancellation after submission, then a fresh countdown; one native call.
    bot.session.ready=true;bot.Request();bot.Request();assert(mailbox.Outstanding()==1);
    bot.session.timer=false;H::ProcessTimedBotLogouts();assert(random.calls==0&&mailbox.Outstanding()==0);
    bot.session.timer=true;bot.Request();H::ProcessTimedBotLogouts();assert(random.calls==1&&!random.GetPlayerBot({1}));
    // Exact original personal master and manager; replacement cannot inherit work.
    PlayerbotMgr personal(&master.player);managers[&master.player]=&personal;
    Bot alt(2,3,4,&master.player);alt.Register(personal);alt.Request();
    { Bot replacement(100,201,301);replacement.session.headless=false;managers[&replacement.player]=&personal;
      H::ProcessTimedBotLogouts();assert(personal.calls==0&&random.calls==1);managers.erase(&replacement.player); }
    players[master.player.guid]=&master.player;alt.Request();
    { PlayerbotMgr newHolder(&master.player);managers[&master.player]=&newHolder;newHolder.bots[alt.player.guid]=&alt.player;
      H::ProcessTimedBotLogouts();assert(newHolder.calls==0&&personal.calls==0); }
    managers[&master.player]=&personal;alt.Request();H::ProcessTimedBotLogouts();assert(personal.calls==1);
    // Same GUID, new session while an old request is queued.
    Bot old(3,4,5);old.Register(random);old.Request();
    { Bot replacement(3,44,55);H::ProcessTimedBotLogouts();assert(random.calls==1);
      random.ForgetTimedBot(old.player.guid,old.player.incarnation,old.session.id);replacement.Register(random);replacement.Request();H::ProcessTimedBotLogouts();assert(random.calls==2); }
    // Recovery and exception latch request, not a blind native retry.
    Bot blocked(4,5,6);blocked.Register(random);random.next=BotRetirementResult::RecoveryRequired;blocked.Request();H::ProcessTimedBotLogouts();assert(random.calls==3);blocked.Request();H::ProcessTimedBotLogouts();assert(random.calls==3&&mailbox.Outstanding()==1);
    Bot fault(5,6,7);fault.Register(random);random.throws=true;fault.Request();H::ProcessTimedBotLogouts();assert(random.calls==4);fault.Request();H::ProcessTimedBotLogouts();assert(random.calls==4&&mailbox.Outstanding()==2);
    random.throws=false;random.next=BotRetirementResult::Completed;
    // Bounded consumer leaves accepted requests pending until a later tick.
    std::vector<std::unique_ptr<Bot>> bots;
    for(unsigned n=0;n<130;++n){auto b=std::make_unique<Bot>(1000+n,2000+n,3000+n);b->Register(random);b->Request();bots.push_back(std::move(b));}
    H::ProcessTimedBotLogouts();assert(random.calls==132&&mailbox.Outstanding()==4);
    H::ProcessTimedBotLogouts();assert(random.calls==134&&mailbox.Outstanding()==2);
    Bot stopped(6,7,8);stopped.Register(random);H::StopTimedBotLogouts();stopped.Request();H::ProcessTimedBotLogouts();assert(random.calls==134&&mailbox.Outstanding()==2);
    managers.clear();
}
