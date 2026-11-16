// Exact pending/queue function bodies, synthetic native dependencies.
#include "AutoGearPendingSessionOwner.h"
#include "AutoGearPublishedSessionOwner.h"
#include <cassert>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
using uint32 = unsigned;
struct ObjectGuid { unsigned value; unsigned GetCounter()const{return value;} std::uint64_t GetRawValue()const{return value;} bool operator==(ObjectGuid const&) const = default; auto operator<=>(ObjectGuid const&) const = default; std::string ToString() const {return std::to_string(value);} };
namespace std { template<> struct hash<ObjectGuid> { size_t operator()(ObjectGuid g) const noexcept { return g.value; } }; }
#define LOG_ERROR(...) ((void)0)
struct WorldSession;
struct Player { struct CallbackScope { bool Admitted() const { return true; } }; CallbackScope AcquirePB59CallbackScope() { return {}; } ObjectGuid guid; std::uint64_t incarnation; WorldSession* session; ObjectGuid GetGUID() const {return guid;} std::uint64_t GetPB59OwnerIncarnation() const {return incarnation;} WorldSession* GetSession() const {return session;} };
static std::map<ObjectGuid,Player*> registered;
struct ObjectAccessor { static Player* FindConnectedPlayer(ObjectGuid guid){auto it=registered.find(guid);return it==registered.end()?nullptr:it->second;} };
struct PlayerbotLoginQueryHolder { ObjectGuid guid; ObjectGuid GetGuid() const {return guid;} };
struct WorldSession
{
    static inline int destroyed=0, logoutCalls=0, loadCalls=0;
    std::uint64_t id;
    std::unique_ptr<Player> player;
    std::function<void()> callback;
    bool inCallback=false, veto=false, fault=false;
    explicit WorldSession(std::uint64_t i):id(i){}
    std::uint64_t GetPB59SessionIncarnation() const noexcept {return id;}
    bool IsHeadless() const noexcept {return true;}
    Player* GetPlayer() const noexcept {return player.get();}
    void HandlePlayerLoginFromDB(PlayerbotLoginQueryHolder const& holder){++loadCalls;player=std::make_unique<Player>(Player{holder.guid,id+100,this});registered[holder.guid]=player.get();}
    void ProcessQueryCallbacks(){inCallback=true; struct Reset {bool& b;~Reset(){b=false;}} reset{inCallback};if(callback){auto fn=std::move(callback);fn();}}
    void LogoutPlayer(bool){assert(!inCallback);++logoutCalls;if(veto)return;if(player){auto it=registered.find(player->guid);if(it!=registered.end()&&it->second==player.get())registered.erase(it);player.reset();}if(fault)throw std::runtime_error("logout");}
    ~WorldSession(){assert(!inCallback);assert(!player);++destroyed;}
};
struct PendingBotLogin
{
    using Owner=PB59::PendingSessionOwner<WorldSession>;
    PendingBotLogin(uint32 account,std::uint64_t masterPlayer,std::uint64_t masterSession,std::unique_ptr<WorldSession> session):masterAccountId(account),masterPlayerIncarnation(masterPlayer),masterSessionIncarnation(masterSession),owner(std::move(session)){}
    uint32 const masterAccountId;
    std::uint64_t const masterPlayerIncarnation,masterSessionIncarnation;
    std::uint64_t botPlayerIncarnation=0;
    Owner owner;
};
struct PlayerbotHolder
{
    PB59::RecoveryRegistry::HolderLifetime _pb59HolderLifetime;
    std::map<ObjectGuid,Player*> playerBots;
    std::function<void(Player*)> onPublish;
    static inline std::unordered_map<ObjectGuid,PendingBotLogin> botLoading;
    static inline bool _pendingLoginsStopping=false;
    static void UpdatePendingLogins(); static void StopPendingLogins(); static void ClearPendingLogins();
    void HandlePlayerBotLoginCallback(PlayerbotLoginQueryHolder const&,WorldSession*);
    static bool CompleteQueuedBotLogin(ObjectGuid,std::uint64_t);
    bool timedRegistrationAllowed=true;
    bool RegisterTimedBot(Player*){return timedRegistrationAllowed;}
    void ForgetTimedBot(ObjectGuid,std::uint64_t,std::uint64_t){}
    bool OnBotLogin(Player*,PendingBotLogin::Owner&);
    Player* GetPlayerBot(ObjectGuid guid){auto it=playerBots.find(guid);return it==playerBots.end()?nullptr:it->second;}
    void OnBotLoginInternal(Player* p){if(onPublish)onPublish(p);}
};
struct RandomPlayerbotMgr:PlayerbotHolder {std::function<void(Player*)> loginHook; static RandomPlayerbotMgr& instance(){static RandomPlayerbotMgr m;return m;}void OnPlayerLogin(Player* p){if(loginHook)loginHook(p);}};
#define sRandomPlayerbotMgr RandomPlayerbotMgr::instance()
struct WorldSessionMgr {std::map<uint32,WorldSession*> masters, offline; WorldSession* FindOfflineSessionForCharacterGUID(unsigned g){auto it=offline.find(g);return it==offline.end()?nullptr:it->second;} WorldSession* FindSession(uint32 a){auto it=masters.find(a);return it==masters.end()?nullptr:it->second;}};
static WorldSessionMgr sessions;
static WorldSessionMgr* sWorldSessionMgr=&sessions;
struct PlayerbotsMgr { PlayerbotHolder* personal=nullptr;std::function<void()> beforePublish;static PlayerbotsMgr& instance(){static PlayerbotsMgr m;return m;} PlayerbotHolder* GetPlayerbotMgr(Player*){return personal;}void AddPlayerbotData(Player*,bool){if(beforePublish)beforePublish();}};
struct PlayerbotOperation {virtual ~PlayerbotOperation()=default;virtual bool Execute()=0;virtual ObjectGuid GetBotGuid()const=0;virtual uint32 GetPriority()const=0;virtual std::string GetName()const=0;virtual bool IsValid()const=0;};
#include "queued_native.inc"
struct PlayerbotWorldThreadProcessor {bool accept=true;std::vector<std::unique_ptr<PlayerbotOperation>> queue;static PlayerbotWorldThreadProcessor& instance(){static PlayerbotWorldThreadProcessor p;return p;}bool QueueOperation(std::unique_ptr<PlayerbotOperation> op){if(!accept)return false;queue.push_back(std::move(op));return true;}bool Run(){assert(queue.size()==1);auto op=std::move(queue.back());queue.clear();return op->IsValid()&&op->Execute();}};
#include "pending_native.inc"
using Phase=PendingBotLogin::Owner::Phase;
using H=PlayerbotHolder;
static WorldSession* Add(ObjectGuid guid,std::uint64_t id,uint32 account=0,std::uint64_t masterPlayer=0,std::uint64_t masterSession=0){auto session=std::make_unique<WorldSession>(id);auto* raw=session.get();auto [it,inserted]=H::botLoading.try_emplace(guid,account,masterPlayer,masterSession,std::move(session));assert(inserted);raw->callback=[guid,raw]{sRandomPlayerbotMgr.HandlePlayerBotLoginCallback({guid},raw);};return raw;}
static void DeletePublished(H& holder,ObjectGuid guid){
    using R=PB59::PublishedSessionOwner<WorldSession>;using J=PB59::RecoveryRegistry;
    auto* p=holder.playerBots.at(guid);auto* s=p->session;
    R::Identity id{guid.GetRawValue(),p->incarnation,holder._pb59HolderLifetime.Observe()->identity,s->id};
    assert(R::Inspect(id)==R::Phase::Published);
    auto permit=R::TryReserveRetirement(id);assert(permit);
    auto record=J::Reserve(id.guid,id.player,id.holder,J::Kind::Logout);assert(record&&record.MarkMutation());
    auto handoff=R::TakeForRecovery(id,permit,record);assert(handoff);
    assert(handoff.Unlink([&](R::Identity exact){assert(exact==id);return holder.playerBots.erase(guid)==1;}));
    assert(handoff.WithSession([](WorldSession& session){session.LogoutPlayer(true);}));
    assert(handoff.DeleteDetached()&&R::Inspect(id)==R::Phase::Missing);
}
int main()
{
    assert(PB59::SessionCustody<WorldSession>::BindOwnerThread());
    auto& queue=PlayerbotWorldThreadProcessor::instance();auto& random=sRandomPlayerbotMgr;
    // Queued owner stays alive and transfers exactly once at publication.
    auto* first=Add({1},11);H::UpdatePendingLogins();assert(H::botLoading.at({1}).owner.State()==Phase::Queued);assert(H::botLoading.at({1}).owner.Get()==first&&queue.queue.size()==1);assert(queue.Run());assert(H::botLoading.empty()&&random.playerBots.at({1})->session==first);assert(!H::CompleteQueuedBotLogin({1},11));DeletePublished(random,{1});
    // Another client/bot finishes login while the pending query is in flight.
    Add({9},23);WorldSession connected(900);connected.HandlePlayerLoginFromDB({{9}});
    int loads=WorldSession::loadCalls;H::UpdatePendingLogins();assert(H::botLoading.empty()&&registered.at({9})==connected.GetPlayer()&&WorldSession::loadCalls==loads&&queue.queue.empty());connected.LogoutPlayer(true);
    // The offline session map can own the character even when not registered.
    Add({10},24);WorldSession offline(901);offline.HandlePlayerLoginFromDB({{10}});registered.erase({10});sessions.offline[10]=&offline;
    loads=WorldSession::loadCalls;H::UpdatePendingLogins();assert(H::botLoading.empty()&&WorldSession::loadCalls==loads&&queue.queue.empty()&&offline.GetPlayer());sessions.offline.clear();offline.LogoutPlayer(true);
    // A failed timer-slot registration cannot release the pending owner.
    random.timedRegistrationAllowed=false;Add({8},22);H::UpdatePendingLogins();assert(!queue.Run());assert(H::botLoading.at({8}).owner.State()==Phase::Failed&&random.playerBots.empty());H::UpdatePendingLogins();assert(H::botLoading.empty());random.timedRegistrationAllowed=true;
    // Queue refusal and dropped operation clean outside callback and no stale publish.
    queue.accept=false;int destroyed=WorldSession::destroyed;Add({2},12);H::UpdatePendingLogins();assert(H::botLoading.empty()&&WorldSession::destroyed==destroyed+1);queue.accept=true;
    Add({2},13);H::UpdatePendingLogins();queue.queue.clear();H::UpdatePendingLogins();assert(H::botLoading.empty());Add({2},14);assert(!H::CompleteQueuedBotLogin({2},13));H::UpdatePendingLogins();assert(queue.Run());DeletePublished(random,{2});
    // Replacement master account session must not attach or fall back to random.
    H personal;PlayerbotsMgr::instance().personal=&personal;WorldSession master(1000),replacement(1001);master.HandlePlayerLoginFromDB({{100}});replacement.HandlePlayerLoginFromDB({{101}});sessions.masters[10]=&master;
    Add({3},15,10,master.GetPlayer()->incarnation,1000);H::UpdatePendingLogins();sessions.masters[10]=&replacement;assert(!queue.Run());assert(H::botLoading.at({3}).owner.State()==Phase::Failed);H::UpdatePendingLogins();assert(H::botLoading.empty()&&personal.playerBots.empty()&&random.playerBots.empty());master.LogoutPlayer(true);replacement.LogoutPlayer(true);sessions.masters.clear();
    // Before-publication exception preserves pending owner; after publication
    // exception reports false without deleting the session owned by the holder.
    PlayerbotsMgr::instance().beforePublish=[] {throw std::runtime_error("before");};Add({4},16);H::UpdatePendingLogins();assert(!queue.Run());assert(H::botLoading.at({4}).owner.State()==Phase::Failed);PlayerbotsMgr::instance().beforePublish={};H::UpdatePendingLogins();assert(H::botLoading.empty());
    random.onPublish=[](Player*){throw std::runtime_error("after");};auto* partial=Add({4},17);H::UpdatePendingLogins();assert(!queue.Run());assert(H::botLoading.empty()&&random.playerBots.at({4})->session==partial);random.onPublish={};DeletePublished(random,{4});
    // An identity change after publication must report incomplete, not success.
    random.onPublish=[](Player* p){++p->incarnation;};auto* changed=Add({11},25);
    H::UpdatePendingLogins();assert(!queue.Run());assert(H::botLoading.empty()&&random.playerBots.at({11})->session==changed);
    random.onPublish={};changed->GetPlayer()->incarnation=changed->id+100;DeletePublished(random,{11});
    // Native cleanup veto/throw leaves exact owner in sticky recovery, no replay.
    for(unsigned n=0;n<2;++n){auto* s=Add({5},18+n);s->veto=n==0;s->fault=n==1;H::UpdatePendingLogins();queue.queue.clear();int calls=WorldSession::logoutCalls;H::UpdatePendingLogins();assert(H::botLoading.at({5}).owner.State()==Phase::RecoveryRequired&&WorldSession::logoutCalls==calls+1);H::UpdatePendingLogins();assert(WorldSession::logoutCalls==calls+1);s->veto=s->fault=false;s->LogoutPlayer(true);H::botLoading.erase({5});}
    // Shutdown during callback must not delete the session on its own stack.
    random.loginHook=[](Player*){H::ClearPendingLogins();};Add({6},20);H::UpdatePendingLogins();assert(H::botLoading.empty()&&queue.queue.empty());random.loginHook={};H::_pendingLoginsStopping=false;
    // Pending callback insertion rehashes the map; exact key snapshot is stable.
    random.loginHook=[](Player*){for(unsigned i=100;i<300;++i)Add({i},i+1000);};Add({7},21);H::UpdatePendingLogins();assert(H::botLoading.size()==201&&queue.queue.size()==1);random.loginHook={};assert(queue.Run());DeletePublished(random,{7});H::ClearPendingLogins();assert(H::botLoading.empty());H::_pendingLoginsStopping=false;
    assert(registered.empty());
    assert(PB59::PublishedSessionOwner<WorldSession>::Outstanding()==0);
}
