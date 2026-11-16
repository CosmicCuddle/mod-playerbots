#include "AutoGearQueuedCommand.h"
#include <cassert>
#include <array>
#include <ctime>
#include <functional>
#include <iostream>
#include <map>
#include <shared_mutex>
#include <string>
#include <utility>
#include <vector>
using uint8 = std::uint8_t;
using uint32 = std::uint32_t;
struct ObjectGuid { std::uint64_t value; std::uint64_t GetRawValue() const {return value;} };
struct Player;
struct PlayerbotAI;
struct Session {
    Player* player = nullptr; std::uint64_t incarnation = 0; uint32 account = 0;
    bool headless = false, logging = false, loggedOut = false;
    Player* GetPlayer() const {return player;}
    std::uint64_t GetPB59SessionIncarnation() const {return incarnation;}
    bool IsHeadless() const {return headless;}
    bool IsLoggingOut() const {return logging;}
    bool PlayerLogout() const {return loggedOut;}
    uint32 GetAccountId() const {return account;}
};
struct Player {
    ObjectGuid guid; std::uint64_t incarnation; Session* session; PlayerbotAI* ai = nullptr;
    bool inWorld = true, teleport = false, removing = false; uint8 level = 60, raw = 0, effective = 0;
    ObjectGuid GetGUID() const {return guid;}
    std::uint64_t GetPB59OwnerIncarnation() const {return incarnation;}
    Session* GetSession() const {return session;}
    bool IsInWorld() const {return inWorld;}
    bool IsBeingTeleported() const {return teleport;}
    bool IsDuringRemoveFromWorld() const {return removing;}
    uint8 GetLevel() const {return level;}
};
enum PlayerbotSecurityLevel { AllowAll };
struct Security {
    bool allowed = true; std::function<void()> callback;
    bool CheckLevelFor(PlayerbotSecurityLevel, bool, Player*) {if(callback) callback();return allowed;}
};
struct PlayerbotAI { Security security; Security* GetSecurity(){return &security;} };
#define GET_PLAYERBOT_AI(p) ((p)->ai)
struct ObjectAccessor {
    static inline std::map<std::uint64_t, Player*> players;
    static Player* FindConnectedPlayer(ObjectGuid g) {auto i=players.find(g.value);return i==players.end()?nullptr:i->second;}
};
struct PlayerbotOperation { virtual ~PlayerbotOperation()=default; virtual bool Execute()=0; virtual ObjectGuid GetBotGuid() const=0; virtual std::string GetName() const=0; };
struct ChatCommandHolder {
    bool capped=false; PB59::CommandOwner sender;
    ChatCommandHolder(std::string const&, ObjectGuid, PlayerbotSecurityLevel, uint32, time_t) {}
    void BindPB59Sender(PB59::CommandOwner s){capped=true;sender=s;}
};
struct Config { bool AutoGearIPCapEnable=true; } sPlayerbotAIConfig;
static int plans=0;
static std::string argument;
static bool lastBis=false;
bool RunPB59AutoGearWorldPlan(Player*, PlayerbotAI*, Player*, std::string_view arg, bool bis) {++plans;argument=arg;lastBis=bis;return true;}
#include "world.inc"
struct PlayerScript {
    bool available=false; uint8 raw=0,effective=0;
    bool OnPlayerPB59AutoGearProgression(Player*, uint8& r,uint8& e){r=raw;e=effective;return available;}
};
constexpr unsigned PLAYERHOOK_ON_PB59_AUTOGEAR_PROGRESSION=0;
template<class T> struct ScriptRegistry {static inline std::array<std::vector<T*>,1> EnabledHooks;};
struct ScriptMgr {bool TryReadPB59AutoGearProgression(Player*,uint8&,uint8&);};
#include "core_provider.inc"
constexpr uint8 PROGRESSION_NAXX40=7;
struct IndividualProgression {
    bool enabled=true; int progressionLimit=7;
    struct AccountMatch {bool excluded=false,bot=false,gm=false;};
    std::map<uint32,AccountMatch> _accountMatchCache; std::shared_mutex _accountMatchLock;
    uint8 GetPlayerProgressionFromQuests(Player* p){return p->raw;}
    uint8 GetEffectiveProgression(Player* p){return p->effective;}
    bool TryReadPB59AutoGearProgression(Player*,uint8&,uint8&);
};
#include "ip_provider.inc"
int main(){
    Session bs, ss; PlayerbotAI ai;
    Player bot{{1},10,&bs,&ai}, sender{{2},20,&ss};
    bs.player=&bot; bs.incarnation=100; bs.headless=true;
    ss.player=&sender; ss.incarnation=200; ss.account=5;
    ObjectAccessor::players={{1,&bot},{2,&sender}};
    auto b=CapturePB59CommandOwner(&bot), s=CapturePB59CommandOwner(&sender);
    assert(b.Valid() && s.Valid() && b!=s);
    assert(!PB59::ParseQueuedAutoGear("autogearx").recognized);
    assert(PB59::ParseQueuedAutoGear("autogear").recognized);
    assert(PB59::ParseQueuedAutoGear("autogear reset match").argument=="reset match");
    assert(PB59::ParseQueuedAutoGear("autogear bis 66").bis);
    assert(PB59::UsesQueuedAutoGear(true,false,"autogear"));
    assert(!PB59::UsesQueuedAutoGear(false,false,"autogear"));
    auto holder=MakePB59ChatCommand("autogear bis 66",&sender,AllowAll,0);
    assert(holder.capped && holder.sender==s);
    auto op=PB59AutoGearWorldOperation(bot.guid,b,sender.guid,s,"autogear bis 66",AllowAll);
    assert(op.Execute() && plans==1 && lastBis && argument=="66");
    ++ss.incarnation; assert(!op.Execute() && plans==1); --ss.incarnation;
    ++bot.incarnation; assert(!op.Execute() && plans==1); --bot.incarnation;
    ai.security.allowed=false; assert(!op.Execute()); ai.security.allowed=true;
    ai.security.callback=[&]{++ss.incarnation;}; assert(!op.Execute()); --ss.incarnation; ai.security.callback={};
    ss.logging=true; assert(!op.Execute()); ss.logging=false;
    sender.teleport=true; assert(!op.Execute()); sender.teleport=false;
    bs.headless=false; assert(!op.Execute()); bs.headless=true;
    ObjectAccessor::players.erase(2); assert(!op.Execute()); ObjectAccessor::players[2]=&sender;
    sPlayerbotAIConfig.AutoGearIPCapEnable=false; assert(!op.Execute());
    assert(!MakePB59ChatCommand("autogear",&sender,AllowAll,0).capped);
    sPlayerbotAIConfig.AutoGearIPCapEnable=true; assert(plans==1);
    ScriptMgr mgr; uint8 raw=9, effective=9; auto& hooks=ScriptRegistry<PlayerScript>::EnabledHooks[0];
    assert(!mgr.TryReadPB59AutoGearProgression(&sender,raw,effective) && raw==0 && effective==0);
    PlayerScript p{true,0,0}, q{true,7,7}; hooks={&p};
    assert(mgr.TryReadPB59AutoGearProgression(&sender,raw,effective) && raw==0);
    hooks={&q}; assert(mgr.TryReadPB59AutoGearProgression(&sender,raw,effective) && raw==7);
    hooks={&p,&q}; assert(!mgr.TryReadPB59AutoGearProgression(&sender,raw,effective) && raw==0);
    q.effective=8;hooks={&q};assert(!mgr.TryReadPB59AutoGearProgression(&sender,raw,effective));
    q.raw=19;q.effective=19;assert(!mgr.TryReadPB59AutoGearProgression(&sender,raw,effective));
    IndividualProgression ip; assert(!ip.TryReadPB59AutoGearProgression(&sender,raw,effective));
    ip._accountMatchCache[5]={}; assert(ip.TryReadPB59AutoGearProgression(&sender,raw,effective) && raw==0);
    sender.raw=sender.effective=7; assert(ip.TryReadPB59AutoGearProgression(&sender,raw,effective) && raw==7);
    ip.progressionLimit=6;assert(!ip.TryReadPB59AutoGearProgression(&sender,raw,effective));ip.progressionLimit=7;
    sender.raw=sender.effective=8;assert(!ip.TryReadPB59AutoGearProgression(&sender,raw,effective));sender.raw=sender.effective=7;
    ip._accountMatchCache[5].excluded=true;assert(!ip.TryReadPB59AutoGearProgression(&sender,raw,effective));ip._accountMatchCache[5].excluded=false;
    ip._accountMatchCache[5].bot=true;assert(!ip.TryReadPB59AutoGearProgression(&sender,raw,effective));ip._accountMatchCache[5].bot=false;
    ip._accountMatchCache[5].gm=true;assert(!ip.TryReadPB59AutoGearProgression(&sender,raw,effective));ip._accountMatchCache[5].gm=false;
    ip.enabled=false;assert(!ip.TryReadPB59AutoGearProgression(&sender,raw,effective));ip.enabled=true;
    sender.level=61;assert(!ip.TryReadPB59AutoGearProgression(&sender,raw,effective));sender.level=60;
    sender.effective=6;assert(!ip.TryReadPB59AutoGearProgression(&sender,raw,effective));sender.effective=7;
    ss.loggedOut=true;assert(!ip.TryReadPB59AutoGearProgression(&sender,raw,effective));
    std::cout<<"PASS: exact queued world operation and Core/IP progression provider scenarios\n";
}
