// Stranger-sender security seam (test-only): exact PlayerbotSecurity.h enums/class and
// PlayerbotSecurity.cpp constructor/LevelFor/CheckLevelFor, compiled against mocks. Not native proof.
// Built by tests/run_pb64_stranger_sender_security.py, which also pins the HandleCommand admission gates.
#include <cstdint>
#include <ctime>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>
using uint8 = std::uint8_t; using uint32 = std::uint32_t; using int32 = std::int32_t;
struct ObjectGuid { std::uint64_t v = 0; bool operator<(ObjectGuid const& o) const { return v < o.v; } bool operator==(ObjectGuid const& o) const { return v == o.v; } };
enum TeamId { TEAM_ALLIANCE, TEAM_HORDE };
enum { LANG_UNIVERSAL = 0 };
enum { CONFIG_MAX_PLAYER_LEVEL = 0 };
struct Session {};
struct Group;
class Player;
struct PlayerbotAI;
#include "security_h.inc"
struct Group { ObjectGuid leader; bool full = false; bool IsFull() const { return full; } ObjectGuid GetLeaderGUID() const { return leader; } };
class Player
{
public:
    ObjectGuid guid; std::string name; Session session; bool hasSession = true; bool gm = false; Group* group = nullptr;
    uint8 level = 60; uint32 guild = 0; PlayerbotAI* ai = nullptr; std::vector<std::pair<Player*, std::string>>* whispersOut = nullptr;
    Session* GetSession() { return hasSession ? &session : nullptr; }
    bool CanBeGameMaster() const { return gm; }
    Group* GetGroup() const { return group; }
    uint8 GetLevel() const { return level; }
    uint32 GetGuildId() const { return guild; }
    bool InBattleground() const { return false; }
    uint32 GetBattlegroundId() const { return 0; }
    TeamId GetBgTeamId() const { return TEAM_ALLIANCE; }
    bool InBattlegroundQueue() const { return false; }
    uint32 GetAreaId() const { return 0; }
    ObjectGuid GetGUID() const { return guid; }
    bool IsInWorld() const { return true; }
    std::string const& GetName() const { return name; }
    void Whisper(std::string const& text, int, Player* to) { if (whispersOut) whispersOut->push_back({to, text}); }
};
struct PlayerbotAI
{
    Player* master = nullptr; Player* groupLeader = nullptr; bool opposing = false;
    bool IsOpposing(Player*) const { return opposing; }
    bool HasGameClientMaster() const { return master != nullptr; }
    Player* GetMaster() const { return master; }
    Player* GetGroupLeader() const { return groupLeader; }
    float GetEquipGearScore(Player*) const { return 0.f; }
};
inline PlayerbotAI* GET_PLAYERBOT_AI(Player* p) { return p ? p->ai : nullptr; }
inline bool IsRealPlayer(Player* p) { return p && !p->ai; }
inline bool IsSelfBot(Player* p) { return p && p->ai && p->ai->GetMaster() == p; }
struct Config { std::vector<uint32> random{501}; bool AllowBattlegroundCommands = false; int32 GroupInvitationPermission = 1; bool GearScoreCheck = false; uint32 RepeatDelay = 2000;
    bool IsInRandomAccountList(uint32 id) const { for (auto a : random) if (a == id) return true; return false; } } sPlayerbotAIConfig;
struct World { uint32 getIntConfig(int) const { return 80; } } worldObj; World* sWorld = &worldObj;
struct AreaTableEntry { char const* area_name[1]; };
struct AreaStore { AreaTableEntry const* LookupEntry(uint32) const { return nullptr; } } sAreaTableStore;
struct CharacterCache { std::map<std::uint64_t, uint32> accounts; uint32 GetCharacterAccountIdByGuid(ObjectGuid g) const { auto i = accounts.find(g.v); return i == accounts.end() ? 0 : i->second; } } cacheObj; CharacterCache* sCharacterCache = &cacheObj;
#include "security_cpp.inc"
static int checks = 0, failures = 0;
static void Check(bool ok, std::string const& what) { ++checks; if (!ok) { ++failures; std::cerr << "FAIL: " << what << "\n"; } }
// Mirrors the two admission gates of PlayerbotAI::HandleCommand (both overloads, pinned textually by the runner):
// gate 1 INVITE, gate 2 ALLOW_ALL, each silent unless whisper. Returns true if the command would be queued.
static bool Admit(PlayerbotSecurity& s, Player* from, bool whisper)
{
    if (!s.CheckLevelFor(PLAYERBOT_SECURITY_INVITE, !whisper, from)) return false;
    if (!s.CheckLevelFor(PLAYERBOT_SECURITY_ALLOW_ALL, !whisper, from)) return false;
    return true;
}
int main()
{
    std::vector<std::pair<Player*, std::string>> out;
    Player commander; commander.guid = {1}; commander.name = "Agcmd";
    Player stranger; stranger.guid = {2}; stranger.name = "Stranger";
    Player gm; gm.guid = {3}; gm.name = "Agops"; gm.gm = true;
    Player rnd; rnd.guid = {10}; rnd.name = "Rnd"; rnd.whispersOut = &out;
    Player alt; alt.guid = {11}; alt.name = "Agalt"; alt.whispersOut = &out;
    PlayerbotAI rndAI; rndAI.master = &commander; rndAI.groupLeader = &commander; rnd.ai = &rndAI;
    PlayerbotAI altAI; altAI.master = &commander; altAI.groupLeader = &commander; alt.ai = &altAI;
    Group group; group.leader = {1}; commander.group = rnd.group = alt.group = &group;
    cacheObj.accounts = {{10, 501}, {11, 900}};  // random-list account vs personal account
    for (Player* bot : {&rnd, &alt})
    {
        std::string const who = bot == &rnd ? "RNDBOT" : "alt";
        PlayerbotSecurity sec(bot);
        // Controls: master and GM have full access; GM needs no group.
        Check(sec.LevelFor(&commander) == PLAYERBOT_SECURITY_ALLOW_ALL, who + ": master ALLOW_ALL");
        Check(sec.LevelFor(&gm) == PLAYERBOT_SECURITY_ALLOW_ALL && !gm.GetGroup(), who + ": GM ALLOW_ALL without group");
        // Stranger: same faction, non-GM, non-master, not in the group.
        DenyReason reason = PLAYERBOT_DENY_NONE;
        PlayerbotSecurityLevel level = sec.LevelFor(&stranger, &reason);
        if (bot == &rnd)
            Check(level == PLAYERBOT_SECURITY_TALK && reason == PLAYERBOT_DENY_NOT_LEADER, who + ": stranger TALK/NOT_LEADER");
        else
            Check(level == PLAYERBOT_SECURITY_INVITE && reason == PLAYERBOT_DENY_NOT_YOURS, who + ": stranger INVITE/NOT_YOURS");
        out.clear();
        Check(!Admit(sec, &stranger, /*whisper*/ true), who + ": stranger whisper not admitted");
        Check(out.size() == 1 && out[0].first == &stranger, who + ": whisper denial is one visible reply to the stranger");
        std::string const first = out.empty() ? "" : out[0].second;
        out.clear();
        Check(!Admit(sec, &stranger, true) && out.empty(), who + ": repeated denial within RepeatDelay is suppressed");
        {   // fresh instance: no earlier reply can hide a non-silent path behind RepeatDelay
            PlayerbotSecurity quiet(bot);
            Check(!Admit(quiet, &stranger, /*whisper*/ false) && out.empty(), who + ": party/channel stranger silently dropped");
            // Queued recheck (PlayerbotAI.cpp world op) is always silent.
            Check(!quiet.CheckLevelFor(PLAYERBOT_SECURITY_ALLOW_ALL, true, &stranger) && out.empty(), who + ": queued recheck silent deny");
        }
        Check(Admit(sec, &commander, true) && Admit(sec, &gm, true) && out.empty(), who + ": master and GM admitted, no reply");
        std::cout << who << " stranger reply: " << first << "\n";
    }
    // Stranger joins the group but is not master: RNDBOT TALK (NOT_YOURS), alt INVITE; neither reaches ALLOW_ALL.
    stranger.group = &group;
    { PlayerbotSecurity s(&rnd); Check(!Admit(s, &stranger, false), "RNDBOT: non-master group member dropped"); }
    { PlayerbotSecurity s(&alt); Check(!Admit(s, &stranger, false), "alt: non-master group member dropped"); }
    std::cout << "checks=" << checks << " failures=" << failures << "\n";
    return failures ? 1 : 0;
}
