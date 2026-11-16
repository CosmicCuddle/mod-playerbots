/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "PlayerbotMgr.h"
#include "AutoGearTimedRetirementMailbox.h"
#include "PlayerbotsDatabase.h"
#include "BroadcastHelper.h"
#include "ChannelMgr.h"
#include "CharacterCache.h"
#include "CharacterPackets.h"
#include "Common.h"
#include "Containers.h"
#include "DatabaseEnv.h"
#include "Define.h"
#include "Group.h"
#include "PlayerbotIdentityGeneration.h"
#include "GuildMgr.h"
#include "ObjectAccessor.h"
#include "ObjectGuid.h"
#include "ObjectMgr.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotFactory.h"
#include "PlayerbotGuildMgr.h"
#include "PlayerbotOperations.h"
#include "PlayerbotRepository.h"
#include "PlayerbotSecurity.h"
#include "PlayerbotTextMgr.h"
#include "PlayerbotWorldThreadProcessor.h"
#include "Playerbots.h"
#include "RandomPlayerbotMgr.h"
#include "SharedDefines.h"
#include "WorldSession.h"
#include "WorldSessionMgr.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <memory>
#include <openssl/sha.h>
#include <string>
#include <vector>
#include <unordered_set>

class BotInitGuard
{
public:
    BotInitGuard(ObjectGuid guid) : guid(guid), active(false)
    {
        if (!botsBeingInitialized.contains(guid))
        {
            botsBeingInitialized.insert(guid);
            active = true;
        }
    }

    ~BotInitGuard()
    {
        if (active)
            botsBeingInitialized.erase(guid);
    }

    bool IsLocked() const { return !active; }

private:
    ObjectGuid guid;
    bool active;
    static std::unordered_set<ObjectGuid> botsBeingInitialized;
};

std::unordered_set<ObjectGuid> BotInitGuard::botsBeingInitialized;
// Explicit shutdown drains this store; static destruction must not implicitly
// delete a still-attached session after the world/DB services are gone.
std::unordered_map<ObjectGuid, PendingBotLogin>& PlayerbotHolder::botLoading =
    *new std::unordered_map<ObjectGuid, PendingBotLogin>();
bool PlayerbotHolder::_pendingLoginsStopping = false;

PlayerbotHolder::PlayerbotHolder() : PlayerbotAIBase(false) {}

PlayerbotHolder::~PlayerbotHolder()
{
    auto tag = _pb59HolderLifetime.Observe();
    _pb59HolderLifetime.Detach();
    PB59::TimedRetirementMailbox::Instance().ForgetHolder(tag->identity);
}

bool PlayerbotHolder::RegisterTimedBot(Player* bot)
{
    auto tag = _pb59HolderLifetime.Observe();
    auto* session = bot ? bot->GetSession() : nullptr;
    if (!tag->Alive() || !session || !session->IsHeadless() || session->GetPlayer() != bot)
        return false;
    PB59::TimedRetirementMailbox::Identity id{bot->GetGUID().GetRawValue(),
        bot->GetPB59OwnerIncarnation(), session->GetPB59SessionIncarnation(), tag->identity, 0, 0, 0};
    if (this != &sRandomPlayerbotMgr)
    {
        auto* mgr = dynamic_cast<PlayerbotMgr*>(this);
        auto* master = mgr ? mgr->GetMaster() : nullptr;
        auto* masterSession = master ? master->GetSession() : nullptr;
        if (!masterSession || masterSession->GetPlayer() != master ||
            ObjectAccessor::FindConnectedPlayer(master->GetGUID()) != master)
            return false;
        id.masterGuid = master->GetGUID().GetRawValue();
        id.masterPlayer = master->GetPB59OwnerIncarnation();
        id.masterSession = masterSession->GetPB59SessionIncarnation();
    }
    return PB59::TimedRetirementMailbox::Instance().Register(id);
}

void PlayerbotHolder::ForgetTimedBot(ObjectGuid guid, std::uint64_t player, std::uint64_t session)
{
    PB59::TimedRetirementMailbox::Instance().Forget(guid.GetRawValue(), player, session,
        _pb59HolderLifetime.Observe()->identity);
}

bool PlayerbotHolder::RetireBotForClientLogin(Player* bot)
{
    auto* session = bot ? bot->GetSession() : nullptr;
    if (!session || !session->IsHeadless() || session->GetPlayer() != bot ||
        ObjectAccessor::FindConnectedPlayer(bot->GetGUID()) != bot)
        return false;
    auto const guid = bot->GetGUID();
    auto const id = PB59::TimedRetirementMailbox::Instance().Lookup(guid.GetRawValue(),
        bot->GetPB59OwnerIncarnation(), session->GetPB59SessionIncarnation());
    if (!id)
        return false; // not published, stale or recovery-blocked
    PlayerbotHolder* holder = &sRandomPlayerbotMgr;
    if (id->masterGuid)
    {
        Player* master = ObjectAccessor::FindConnectedPlayer(ObjectGuid(id->masterGuid));
        WorldSession* masterSession = master ? master->GetSession() : nullptr;
        if (!master || master->GetPB59OwnerIncarnation() != id->masterPlayer ||
            !masterSession || masterSession->GetPlayer() != master ||
            masterSession->GetPB59SessionIncarnation() != id->masterSession)
            return false;
        holder = GET_PLAYERBOT_MGR(master);
    }
    if (!holder)
        return false;
    auto tag = holder->_pb59HolderLifetime.Observe();
    if (!tag->Alive() || tag->identity != id->holder || holder->GetPlayerBot(guid) != bot)
        return false;
    // The packet hook must not continue merely because logout was requested.
    // No dereference of bot/session/holder after native retirement may delete it.
    if (holder->TryLogoutPlayerBot(guid) != BotRetirementResult::Completed)
        return false;
    return ObjectAccessor::FindConnectedPlayer(guid) == nullptr;
}

void PlayerbotHolder::RequestTimedBotLogout(Player* bot)
{
    auto* session = bot ? bot->GetSession() : nullptr;
    if (!session || !session->IsHeadless() || session->GetPlayer() != bot ||
        session->PlayerLogout() || !session->IsLoggingOut())
        return;
    // Only the bot's own values leave this producer. No holder/master pointer,
    // no native callback and no allocation in the registered mailbox slot.
    auto result = PB59::TimedRetirementMailbox::Instance().Request(bot->GetGUID().GetRawValue(),
        bot->GetPB59OwnerIncarnation(), session->GetPB59SessionIncarnation());
    if (result == PB59::TimedRetirementMailbox::Submit::Exhausted)
        LOG_ERROR("playerbots", "Timed bot logout ticket space exhausted");
}

void PlayerbotHolder::ProcessTimedBotLogouts()
{
    using Mailbox = PB59::TimedRetirementMailbox;
    auto& mailbox = Mailbox::Instance();
    // Bounded work per world tick; the remaining accepted requests stay pending.
    for (unsigned n = 0; n < 128; ++n)
    {
        auto work = mailbox.Take();
        if (!work)
            break;
        struct Finish
        {
            Mailbox& mailbox;
            Mailbox::Work work;
            bool recovery = false;
            ~Finish() { (void)mailbox.Finish(work, recovery); }
        } finish{mailbox, *work};
        auto const& id = work->identity;
        try
        {
            ObjectGuid guid(id.guid);
            Player* bot = ObjectAccessor::FindConnectedPlayer(guid);
            WorldSession* session = bot ? bot->GetSession() : nullptr;
            if (!bot || bot->GetPB59OwnerIncarnation() != id.player || !session ||
                !session->IsHeadless() || session->GetPlayer() != bot ||
                session->GetPB59SessionIncarnation() != id.session)
                continue;
            PlayerbotHolder* holder = &sRandomPlayerbotMgr;
            if (id.masterGuid)
            {
                Player* master = ObjectAccessor::FindConnectedPlayer(ObjectGuid(id.masterGuid));
                WorldSession* masterSession = master ? master->GetSession() : nullptr;
                if (!master || master->GetPB59OwnerIncarnation() != id.masterPlayer ||
                    !masterSession || masterSession->GetPlayer() != master ||
                    masterSession->GetPB59SessionIncarnation() != id.masterSession)
                    continue;
                holder = GET_PLAYERBOT_MGR(master);
            }
            if (!holder)
                continue;
            auto tag = holder->_pb59HolderLifetime.Observe();
            if (!tag->Alive() || tag->identity != id.holder || holder->GetPlayerBot(guid) != bot)
                continue;
            auto* ai = GET_PLAYERBOT_AI(bot);
            if (!ai || !ai->IsTimedLogoutReady())
                continue;
            auto result = holder->TryLogoutPlayerBot(guid, {}, true);
            finish.recovery = result == BotRetirementResult::RecoveryRequired;
            if (finish.recovery)
                LOG_ERROR("playerbots", "Timed logout of {} requires recovery; no automatic retry", guid.ToString());
        }
        catch (...)
        {
            // Unknown outcome must not authorize another attempt.
            finish.recovery = true;
            LOG_ERROR("playerbots", "Timed logout dispatch interrupted for GUID {}", id.guid);
        }
    }
}

void PlayerbotHolder::StopTimedBotLogouts()
{
    auto& mailbox = PB59::TimedRetirementMailbox::Instance();
    mailbox.Stop();
    auto unresolved = mailbox.Outstanding();
    if (unresolved)
        LOG_ERROR("playerbots", "{} timed logout requests pending/blocked at shutdown; native drain not yet implemented", unresolved);
}
class PlayerbotLoginQueryHolder : public LoginQueryHolder
{
private:
    uint32 masterAccountId;
public:
    PlayerbotLoginQueryHolder(uint32 masterAccount, uint32 accountId, ObjectGuid guid)
        : LoginQueryHolder(accountId, guid), masterAccountId(masterAccount)
    {
    }

    uint32 GetMasterAccountId() const { return masterAccountId; }
};

static std::string PersonalBotAccountRejection(uint32 accountId, ObjectGuid guid);

void PlayerbotHolder::AddPlayerBot(ObjectGuid playerGuid, uint32 masterAccountId)
{
    if (_pendingLoginsStopping || botLoading.find(playerGuid) != botLoading.end())
        return;

    // Connected includes a Player outside the world/map during transfer.
    // Never initiate a second bot login while another exact character is live.
    if (ObjectAccessor::FindConnectedPlayer(playerGuid) ||
        sWorldSessionMgr->FindOfflineSessionForCharacterGUID(playerGuid.GetCounter()))
        return;

    uint32 accountId = sCharacterCache->GetCharacterAccountIdByGuid(playerGuid);
    if (!accountId)
        return;

    if (masterAccountId)
    {
        if (std::string rejection = PersonalBotAccountRejection(accountId, playerGuid); !rejection.empty())
        {
            LOG_DEBUG("playerbots", "Personal bot login refused: {}", rejection);
            return;
        }
    }

    WorldSession* masterSession = masterAccountId ? sWorldSessionMgr->FindSession(masterAccountId) : nullptr;
    Player* masterPlayer = masterSession ? masterSession->GetPlayer() : nullptr;

    bool isRndbot = !masterAccountId;
    bool sameAccount = sPlayerbotAIConfig.AllowAccountBots && accountId == masterAccountId;
    Guild* guild = masterPlayer ? sGuildMgr->GetGuildById(masterPlayer->GetGuildId()) : nullptr;
    bool sameGuild = sPlayerbotAIConfig.AllowGuildBots && guild && guild->GetMember(playerGuid);
    bool addClassBot = sRandomPlayerbotMgr.IsAddclassBot(playerGuid.GetCounter());
    bool linkedAccount = sPlayerbotAIConfig.AllowTrustedAccountBots && IsAccountLinked(accountId, masterAccountId);

    bool allowed = true;
    std::ostringstream out;
    std::string botName;
    sCharacterCache->GetCharacterNameByGuid(playerGuid, botName);
    if (!isRndbot && !sameAccount && !sameGuild && !addClassBot && !linkedAccount)
    {
        allowed = false;
        out << "Failure: You are not allowed to control bot " << botName.c_str();
    }
    if (masterAccountId && masterPlayer)
    {
        PlayerbotMgr* mgr = GET_PLAYERBOT_MGR(masterPlayer);
        if (!mgr)
        {
            LOG_DEBUG("playerbots", "PlayerbotMgr not found for master player with GUID: {}", masterPlayer->GetGUID().GetRawValue());
            return;
        }
        uint32 loadingForMaster = 0;
        for (auto const& [guid, pending] : botLoading)
        {
            if (pending.masterAccountId == masterAccountId)
                ++loadingForMaster;
        }
        uint32 count = mgr->GetPlayerbotsCount() + loadingForMaster;
        if (count >= uint32(PlayerbotAIConfig::Instance().MaxAddedBots))
        {
            allowed = false;
            out << "Failure: You have added too many bots (more than " << sPlayerbotAIConfig.MaxAddedBots << ")";
        }
    }
    if (!allowed)
    {
        if (masterSession)
        {
            ChatHandler ch(masterSession);
            ch.SendSysMessage(out.str());
        }
        return;
    }
    std::shared_ptr<PlayerbotLoginQueryHolder> holder =
        std::make_shared<PlayerbotLoginQueryHolder>(masterAccountId, accountId, playerGuid);
    if (!holder->Initialize())
    {
        return;
    }

    // Personal login must remain bound to the original active master, not
    // silently attach to a replacement account session or the random holder.
    if (masterAccountId && (!masterPlayer || !masterSession ||
        !masterPlayer->GetPB59OwnerIncarnation() || !masterSession->GetPB59SessionIncarnation()))
        return;

    auto session = std::make_unique<WorldSession>(accountId, "", 0x0, nullptr, SEC_PLAYER,
        EXPANSION_WRATH_OF_THE_LICH_KING, time_t(0), sWorld->GetDefaultDbcLocale(), 0, false, false, 0);
    auto const sessionId = session->GetPB59SessionIncarnation();
    if (!sessionId || !session->IsHeadless())
        return;
    auto [pendingIt, inserted] = botLoading.try_emplace(playerGuid, masterAccountId,
        masterAccountId ? masterPlayer->GetPB59OwnerIncarnation() : 0,
        masterAccountId ? masterSession->GetPB59SessionIncarnation() : 0, std::move(session));
    if (!inserted)
        return;

    // Capture only the value identity. Pending/queued owner retains the session
    // until the exact holder publishes it or cleanup completes.
    try
    {
        pendingIt->second.owner.Get()->AddQueryHolderCallback(CharacterDatabase.DelayQueryHolder(holder))
            .AfterComplete([playerGuid, sessionId](SQLQueryHolderBase const& queryHolder)
            {
                using Phase = PendingBotLogin::Owner::Phase;
                auto it = botLoading.find(playerGuid);
                if (it == botLoading.end() || !it->second.owner.Matches(sessionId) ||
                    !it->second.owner.Busy() || it->second.owner.State() != Phase::Loading)
                    return;
                auto& pending = it->second;
                PlayerbotLoginQueryHolder const& holder = static_cast<PlayerbotLoginQueryHolder const&>(queryHolder);
                if (_pendingLoginsStopping || holder.GetGuid() != playerGuid ||
                    holder.GetMasterAccountId() != pending.masterAccountId)
                {
                    pending.owner.Fail();
                    return;
                }
                PlayerbotHolder* target = &RandomPlayerbotMgr::instance();
                if (pending.masterAccountId)
                {
                    WorldSession* masterSession = sWorldSessionMgr->FindSession(pending.masterAccountId);
                    Player* master = masterSession ? masterSession->GetPlayer() : nullptr;
                    if (!master || masterSession->GetPB59SessionIncarnation() != pending.masterSessionIncarnation ||
                        master->GetPB59OwnerIncarnation() != pending.masterPlayerIncarnation)
                    {
                        pending.owner.Fail();
                        return;
                    }
                    target = PlayerbotsMgr::instance().GetPlayerbotMgr(master);
                }
                if (!target)
                {
                    pending.owner.Fail();
                    return;
                }
                target->HandlePlayerBotLoginCallback(holder, pending.owner.Get());
            });
    }
    catch (...)
    {
        auto current = botLoading.find(playerGuid);
        if (current != botLoading.end() && current->second.owner.Matches(sessionId))
            current->second.owner.Fail();
    }
}

void PlayerbotHolder::UpdatePendingLogins()
{
    using Phase = PendingBotLogin::Owner::Phase;
    // A callback can insert another entry. Snapshot exact IDs, not raw pointers
    // or iterators, and never erase a busy owner.
    std::vector<std::pair<ObjectGuid, std::uint64_t>> keys;
    keys.reserve(botLoading.size());
    for (auto const& [guid, pending] : botLoading)
        keys.emplace_back(guid, pending.owner.Identity());
    for (auto const& [guid, sessionId] : keys)
    {
        auto it = botLoading.find(guid);
        if (it == botLoading.end() || !it->second.owner.Matches(sessionId) || it->second.owner.Busy())
            continue;
        auto& owner = it->second.owner;
        if (_pendingLoginsStopping || owner.QueueDropped())
            owner.Fail();
        if (owner.Begin(Phase::Loading))
        {
            try { owner.Get()->ProcessQueryCallbacks(); }
            catch (...) { owner.Fail(); }
            owner.End();
        }
        if (owner.State() == Phase::Failed)
        {
            if (owner.CloseFailed([](WorldSession& session) { session.LogoutPlayer(true); }))
            {
                // The owner remained busy throughout native logout/deletion.
                // Re-find after callbacks, which may have rehashed the map.
                auto current = botLoading.find(guid);
                if (current != botLoading.end() && current->second.owner.Identity() == sessionId &&
                    current->second.owner.State() == Phase::Closed)
                    botLoading.erase(current);
            }
            else
                LOG_ERROR("playerbots", "Pending bot {} session {} cleanup unresolved; retained, no blind retry",
                    guid.ToString(), sessionId);
        }
    }
}

void PlayerbotHolder::StopPendingLogins()
{
    _pendingLoginsStopping = true;
}

void PlayerbotHolder::ClearPendingLogins()
{
    StopPendingLogins();
    // No new requests or queued publication after the shutdown fence.
    for (auto& [guid, pending] : botLoading)
        pending.owner.Fail();
    UpdatePendingLogins();
    if (!botLoading.empty())
        LOG_ERROR("playerbots", "{} pending bot sessions remain unresolved at shutdown", botLoading.size());
}

bool PlayerbotHolder::IsAccountLinked(uint32 accountId, uint32 linkedAccountId)
{
    PlayerbotsDatabasePreparedStatement* stmt = PlayerbotsDatabase.GetPreparedStatement(PLAYERBOTS_SEL_ACCOUNT_LINK);
    stmt->SetData(0, accountId);
    stmt->SetData(1, linkedAccountId);
    return PlayerbotsDatabase.Query(stmt) != nullptr;
}

void PlayerbotHolder::HandlePlayerBotLoginCallback(PlayerbotLoginQueryHolder const& holder, WorldSession* botSession)
{
    using Phase = PendingBotLogin::Owner::Phase;
    auto const guid = holder.GetGuid();
    auto it = botLoading.find(guid);
    if (!botSession || it == botLoading.end() || it->second.owner.Get() != botSession ||
        !it->second.owner.Busy() || it->second.owner.State() != Phase::Loading)
        return;
    auto& pending = it->second;
    auto const sessionId = pending.owner.Identity();
    // The character could have logged in after the DB query was submitted.
    // Refuse before Player allocation/registration, not merely after loading.
    if (botSession->GetPlayer() || ObjectAccessor::FindConnectedPlayer(guid) ||
        sWorldSessionMgr->FindOfflineSessionForCharacterGUID(guid.GetCounter()))
    {
        pending.owner.Fail();
        return;
    }
    botSession->HandlePlayerLoginFromDB(holder);
    Player* bot = botSession->GetPlayer();
    if (!bot || bot->GetGUID() != guid || !bot->GetPB59OwnerIncarnation() ||
        ObjectAccessor::FindConnectedPlayer(guid) != bot || _pendingLoginsStopping)
    {
        pending.owner.Fail();
        return;
    }
    auto const playerId = bot->GetPB59OwnerIncarnation();
    sRandomPlayerbotMgr.OnPlayerLogin(bot);
    // Revalidate after the login hook; it must not authorize a replacement.
    if (pending.owner.State() != Phase::Loading || _pendingLoginsStopping ||
        ObjectAccessor::FindConnectedPlayer(guid) != bot || botSession->GetPlayer() != bot ||
        bot->GetPB59OwnerIncarnation() != playerId)
    {
        pending.owner.Fail();
        return;
    }
    pending.botPlayerIncarnation = playerId;
    auto marker = std::make_shared<int>(0);
    auto op = std::make_unique<OnBotLoginOperation>(guid, sessionId, marker);
    if (!pending.owner.MarkQueued(marker) ||
        !PlayerbotWorldThreadProcessor::instance().QueueOperation(std::move(op)))
        pending.owner.Fail();
    // No erase here: the queue owns only its marker, not the WorldSession.
}

bool PlayerbotHolder::CompleteQueuedBotLogin(ObjectGuid guid, std::uint64_t sessionId)
{
    using Phase = PendingBotLogin::Owner::Phase;
    auto it = botLoading.find(guid);
    if (it == botLoading.end() || !it->second.owner.Matches(sessionId))
        return false;
    auto& pending = it->second;
    auto& owner = pending.owner;
    if (_pendingLoginsStopping)
    {
        owner.Fail();
        return false;
    }
    if (!owner.Begin(Phase::Queued))
        return false;
    bool published = false;
    bool interrupted = false;
    try
    {
        WorldSession* session = owner.Get();
        Player* bot = session->GetPlayer();
        PlayerbotHolder* target = &RandomPlayerbotMgr::instance();
        if (pending.masterAccountId)
        {
            WorldSession* masterSession = sWorldSessionMgr->FindSession(pending.masterAccountId);
            Player* master = masterSession ? masterSession->GetPlayer() : nullptr;
            target = master && masterSession->GetPB59SessionIncarnation() == pending.masterSessionIncarnation &&
                master->GetPB59OwnerIncarnation() == pending.masterPlayerIncarnation ?
                PlayerbotsMgr::instance().GetPlayerbotMgr(master) : nullptr;
        }
        if (target && bot && ObjectAccessor::FindConnectedPlayer(guid) == bot &&
            bot->GetGUID() == guid && bot->GetPB59OwnerIncarnation() == pending.botPlayerIncarnation)
            target->OnBotLogin(bot, owner);
        published = owner.State() == Phase::Released;
    }
    catch (...)
    {
        // If the holder was already published, do not delete its session.
        // Post-publication callback recovery belongs to the native holder phase.
        published = owner.State() == Phase::Released;
        interrupted = true;
        LOG_ERROR("playerbots", "Queued login {} session {} interrupted; published {}", guid.ToString(), sessionId, published);
    }
    if (!published)
        owner.Fail();
    owner.End();
    if (published)
    {
        auto current = botLoading.find(guid);
        if (current != botLoading.end() && current->second.owner.Identity() == sessionId &&
            current->second.owner.State() == Phase::Released)
            botLoading.erase(current);
    }
    return published && !interrupted;
}

void PlayerbotHolder::UpdateSessions()
{
    // Owner-context only. Never retain a holder iterator across native callbacks.
    auto holderLife = _pb59HolderLifetime.Observe();
    struct Identity { ObjectGuid guid; std::uint64_t player; std::uint64_t session; };
    std::vector<Identity> bots;
    bots.reserve(playerBots.size());
    for (auto const& [guid, bot] : playerBots)
    {
        Player* live = ObjectAccessor::FindConnectedPlayer(guid);
        if (live && live == bot && live->GetSession() && live->GetPB59OwnerIncarnation() &&
            live->GetSession()->GetPB59SessionIncarnation())
            bots.push_back({guid, live->GetPB59OwnerIncarnation(), live->GetSession()->GetPB59SessionIncarnation()});
    }
    for (auto const& id : bots)
    {
        auto resolve = [&]() -> Player*
        {
            if (!holderLife->Alive())
                return nullptr;
            Player* live = ObjectAccessor::FindConnectedPlayer(id.guid);
            if (!live || live != GetPlayerBot(id.guid) || live->GetPB59OwnerIncarnation() != id.player)
                return nullptr;
            WorldSession* session = live->GetSession();
            return session && session->GetPlayer() == live &&
                session->GetPB59SessionIncarnation() == id.session ? live : nullptr;
        };
        Player* bot = resolve();
        if (!bot)
            continue;
        // Preserve the existing mixed query/transaction completion pump.
        // It is not an ordinary packet callback: blocking it on pending
        // save/logout or recovery can prevent the very work needed to drain.
        // Exact identity is revalidated afterwards; this is not a lifetime
        // pin. A typed completion adapter is required before gating this pump.
        bot->GetSession()->ProcessQueryCallbacks();
        bot = resolve();
        if (!bot)
            continue;
        if (bot->IsBeingTeleported())
        {
            auto scope = bot->AcquirePB59CallbackScope();
            if (scope.Admitted())
                if (PlayerbotAI* ai = GET_PLAYERBOT_AI(bot))
                    ai->HandleTeleportAck();
        }
        else if (bot->IsInWorld())
            HandleBotPackets(bot->GetSession());
    }
}

void PlayerbotHolder::HandleBotPackets(WorldSession* session)
{
    if (!session || !session->GetPlayer())
        return;
    auto holderLife = _pb59HolderLifetime.Observe();
    ObjectGuid const guid = session->GetPlayer()->GetGUID();
    auto const playerId = session->GetPlayer()->GetPB59OwnerIncarnation();
    auto const sessionId = session->GetPB59SessionIncarnation();
    while (holderLife->Alive())
    {
        Player* live = ObjectAccessor::FindConnectedPlayer(guid);
        if (!live || live != GetPlayerBot(guid) || live->GetPB59OwnerIncarnation() != playerId ||
            live->GetSession() != session || session->GetPlayer() != live ||
            session->GetPB59SessionIncarnation() != sessionId)
            return;
        auto scope = live->AcquirePB59CallbackScope();
        if (!scope.Admitted())
            return;
        std::unique_ptr<WorldPacket> packet = session->NextQueuedPacket();
        if (!packet)
            return;
        OpcodeClient opcode = static_cast<OpcodeClient>(packet->GetOpcode());
        ClientOpcodeHandler const* opHandle = opcodeTable[opcode];
        if (!opHandle)
        {
            LOG_ERROR("playerbots", "Unhandled opcode {} queued for bot session {}. Packet dropped.", static_cast<uint32>(opcode), session->GetAccountId());
            continue;
        }
        opHandle->Call(session, *packet);
    }
}

void PlayerbotHolder::LogoutAllBots()
{
    // Snapshot exact registered incarnations, not Player pointers that a
    // previous callback could destroy or replace. Owner-context only.
    auto holderLife = _pb59HolderLifetime.Observe();
    std::vector<std::pair<ObjectGuid, std::uint64_t>> bots;
    bots.reserve(playerBots.size());
    for (auto const& [guid, bot] : playerBots)
    {
        Player* live = ObjectAccessor::FindConnectedPlayer(guid);
        if (live && live == bot && live->GetPB59OwnerIncarnation())
            bots.emplace_back(guid, live->GetPB59OwnerIncarnation());
    }

    for (auto const& [guid, incarnation] : bots)
    {
        if (!holderLife->Alive())
            return;
        Player* live = ObjectAccessor::FindConnectedPlayer(guid);
        if (!live || live != GetPlayerBot(guid) || live->GetPB59OwnerIncarnation() != incarnation)
            continue;
        PlayerbotAI* botAI = GET_PLAYERBOT_AI(live);
        if (!botAI || IsSelfBot(live))
            continue;
        auto result = TryLogoutPlayerBot(guid);
        if (result != BotRetirementResult::Completed)
            LOG_ERROR("playerbots", "Bulk bot logout incomplete for {} owner {}; independent live custody still required",
                guid.ToString(), incarnation);
    }
}

void PlayerbotMgr::CancelLogout()
{
    Player* master = GetMaster();
    if (!master)
        return;

    for (PlayerBotMap::const_iterator it = GetPlayerBotsBegin(); it != GetPlayerBotsEnd(); ++it)
    {
        Player* const bot = it->second;
        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        if (!botAI || IsSelfBot(bot))
            continue;

        if (bot->GetSession()->IsLoggingOut())
        {
            WorldPackets::Character::LogoutCancel data = WorldPacket(CMSG_LOGOUT_CANCEL);
            bot->GetSession()->HandleLogoutCancelOpcode(data);
            botAI->TellMaster(PlayerbotTextMgr::instance().GetBotTextOrDefault(
                "logout_cancel", "Logout cancelled!", {}));
        }
    }

    for (PlayerBotMap::const_iterator it = sRandomPlayerbotMgr.GetPlayerBotsBegin();
         it != sRandomPlayerbotMgr.GetPlayerBotsEnd(); ++it)
    {
        Player* const bot = it->second;
        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        if (!botAI || IsSelfBot(bot))
            continue;

        if (botAI->GetMaster() != master)
            continue;

        if (bot->GetSession()->IsLoggingOut())
        {
            WorldPackets::Character::LogoutCancel data = WorldPacket(CMSG_LOGOUT_CANCEL);
            bot->GetSession()->HandleLogoutCancelOpcode(data);
        }
    }
}

bool PlayerbotHolder::LogoutPlayerBot(ObjectGuid guid)
{
    return TryLogoutPlayerBot(guid) == BotRetirementResult::Completed;
}

BotRetirementResult PlayerbotHolder::TryLogoutPlayerBot(ObjectGuid guid, std::function<void()> const& beforeRetirement,
    bool timedLogout)
{
    using Result = BotRetirementResult;
    using Journal = PB59::RecoveryRegistry;
    auto holderLife = _pb59HolderLifetime.Observe();
    if (!holderLife->Alive())
        return Result::Refused;
    Player* bot = GetPlayerBot(guid);
    if (!bot || ObjectAccessor::FindConnectedPlayer(guid) != bot)
        return Result::Refused;

    auto retirement = bot->TryAcquirePB59Retirement();
    if (!retirement)
        return bot->PB59RetirementRecoveryRequired() ? Result::RecoveryRequired : Result::Refused;

    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    WorldSession* botWorldSessionPtr = bot->GetSession();
    if (!botAI || !botWorldSessionPtr || botWorldSessionPtr->GetPlayer() != bot || botWorldSessionPtr->PlayerLogout() ||
        (timedLogout ? !botAI->IsTimedLogoutReady() : botWorldSessionPtr->IsLoggingOut()))
        return Result::Refused;

    auto const ownerIncarnation = bot->GetPB59OwnerIncarnation();
    auto const sessionIncarnation = botWorldSessionPtr->GetPB59SessionIncarnation();
    if (!sessionIncarnation)
        return Result::Refused;
    auto record = Journal::Reserve(guid.GetRawValue(), ownerIncarnation, holderLife->identity, Journal::Kind::Logout);
    if (!record)
    {
        auto existing = Journal::Inspect(guid.GetRawValue(), ownerIncarnation);
        return existing && existing->phase == Journal::Phase::RecoveryRequired ?
            Result::RecoveryRequired : Result::Refused;
    }

    // Owner-context only: reacquire the registered Player before dereferencing
    // after callbacks. Exact token rejects a replacement owner at the same address.
    // This does not make ObjectAccessor a cross-thread lifetime pin.
    auto current = [&]()
    {
        if (!holderLife->Alive())
            return false;
        Player* live = ObjectAccessor::FindConnectedPlayer(guid);
        return live && live == bot && live->GetPB59OwnerIncarnation() == ownerIncarnation &&
            GetPlayerBot(guid) == live &&
            live->OwnsPB59Retirement(retirement) && live->GetSession() == botWorldSessionPtr &&
            GET_PLAYERBOT_AI(live) == botAI && botWorldSessionPtr->GetPlayer() == live &&
            botWorldSessionPtr->GetPB59SessionIncarnation() == sessionIncarnation &&
            !botWorldSessionPtr->PlayerLogout() &&
            (timedLogout ? botWorldSessionPtr->IsLoggingOut() : !botWorldSessionPtr->IsLoggingOut());
    };
    auto partial = [&]()
    {
        LOG_ERROR("playerbots", "Bot {} retirement incomplete; operation {} owner {} recovery required",
            guid.ToString(), record.Operation(), ownerIncarnation);
        return Result::RecoveryRequired;
    };
    if (!retirement.MarkMutation())
        return Result::Refused;
    if (!record.MarkMutation())
        return partial();
    try
    {
        record.MarkStep(Journal::Step::BeforeBookkeeping);
        if (beforeRetirement)
            beforeRetirement();
        if (!current())
            return partial();

        record.MarkStep(Journal::Step::BeforeIdentityRetire);
        PlayerbotIdentityGeneration::Instance().RetireLogin(guid);
        record.MarkStep(Journal::Step::BeforeGroupQueue);
        auto cleanupOp = std::make_unique<BotLogoutGroupCleanupOperation>(guid, ownerIncarnation, sessionIncarnation);
        // Identity retirement already mutated state; queue refusal is not a
        // completed logout and must retain the journal for reconciliation.
        if (!PlayerbotWorldThreadProcessor::instance().QueueOperation(std::move(cleanupOp)))
            return partial();
        if (!current())
            return partial();

        LOG_DEBUG("playerbots", "Bot {} logging out", bot->GetName().c_str());
        record.MarkStep(Journal::Step::BeforeTaxiChange);
        if (!sRandomPlayerbotMgr.IsRandomBot(bot) && bot->isTaxiCheater())
            bot->SetTaxiCheater(false);
        if (!current())
            return partial();
        record.MarkStep(Journal::Step::BeforeSave);
        bot->SaveToDB(false, false);
        if (!current())
            return partial();

        record.MarkStep(Journal::Step::BeforeFarewell);
        std::string message = PlayerbotTextMgr::instance().GetBotTextOrDefault(
            "goodbye", "Goodbye!", {});
        botAI->TellMaster(message);
        if (!current())
            return partial();
        record.MarkStep(Journal::Step::BeforeHolderErase);
        ForgetTimedBot(guid, ownerIncarnation, sessionIncarnation);
        RemoveFromPlayerbotsMap(guid);
        record.MarkStep(Journal::Step::HolderErased);
        record.MarkStep(Journal::Step::BeforeNativeLogout);
        botWorldSessionPtr->LogoutPlayer(true);
        record.MarkStep(Journal::Step::NativeLogoutReturned);
        // A future native logout veto must not be followed by session deletion.
        if (botWorldSessionPtr->GetPlayer())
            return partial();
        record.MarkStep(Journal::Step::BeforeSessionDelete);
        delete botWorldSessionPtr;
        record.MarkStep(Journal::Step::SessionDeleteReturned);
        return retirement.Complete() && record.Complete() ? Result::Completed : partial();
    }
    catch (...)
    {
        // An admitted producer may already have changed scheduler/identity/DB
        // state. The lease latches recovery on unwind; never retry blindly.
        return partial();
    }
}

bool PlayerbotHolder::DisablePlayerBot(ObjectGuid guid)
{
    return TryDisablePlayerBot(guid) == BotRetirementResult::Completed;
}

BotRetirementResult PlayerbotHolder::TryDisablePlayerBot(ObjectGuid guid)
{
    using Result = BotRetirementResult;
    using Journal = PB59::RecoveryRegistry;
    auto holderLife = _pb59HolderLifetime.Observe();
    if (!holderLife->Alive())
        return Result::Refused;
    Player* bot = GetPlayerBot(guid);
    if (!bot || ObjectAccessor::FindConnectedPlayer(guid) != bot)
        return Result::Refused;

    auto retirement = bot->TryAcquirePB59Retirement();
    if (!retirement)
        return bot->PB59RetirementRecoveryRequired() ? Result::RecoveryRequired : Result::Refused;

    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    WorldSession* session = bot->GetSession();
    if (!botAI || !session || session->GetPlayer() != bot)
        return Result::Refused;

    auto const ownerIncarnation = bot->GetPB59OwnerIncarnation();
    auto const sessionIncarnation = session->GetPB59SessionIncarnation();
    if (!sessionIncarnation)
        return Result::Refused;
    auto record = Journal::Reserve(guid.GetRawValue(), ownerIncarnation, holderLife->identity, Journal::Kind::Disable);
    if (!record)
    {
        auto existing = Journal::Inspect(guid.GetRawValue(), ownerIncarnation);
        return existing && existing->phase == Journal::Phase::RecoveryRequired ?
            Result::RecoveryRequired : Result::Refused;
    }

    auto current = [&]()
    {
        if (!holderLife->Alive())
            return false;
        Player* live = ObjectAccessor::FindConnectedPlayer(guid);
        return live && live == bot && live->GetPB59OwnerIncarnation() == ownerIncarnation &&
            GetPlayerBot(guid) == live &&
            live->OwnsPB59Retirement(retirement) && live->GetSession() == session &&
            GET_PLAYERBOT_AI(live) == botAI && session->GetPlayer() == live &&
            session->GetPB59SessionIncarnation() == sessionIncarnation;
    };
    auto partial = [&]()
    {
        LOG_ERROR("playerbots", "Bot {} disable incomplete; operation {} owner {} recovery required",
            guid.ToString(), record.Operation(), ownerIncarnation);
        return Result::RecoveryRequired;
    };
    if (!retirement.MarkMutation())
        return Result::Refused;
    if (!record.MarkMutation())
        return partial();
    try
    {
        record.MarkStep(Journal::Step::BeforeIdentityRetire);
        PlayerbotIdentityGeneration::Instance().RetireLogin(guid);
        record.MarkStep(Journal::Step::BeforeFarewell);
        botAI->TellMaster(PlayerbotTextMgr::instance().GetBotTextOrDefault(
            "goodbye", "Goodbye!", {}));
        if (!current())
            return partial();
        record.MarkStep(Journal::Step::BeforeMotion);
        bot->StopMoving();
        bot->GetMotionMaster()->Clear();
        if (!current())
            return partial();

        record.MarkStep(Journal::Step::BeforeRepositorySave);
        Group* group = bot->GetGroup();
        if (group && !bot->InBattleground() && !bot->InBattlegroundQueue() && IsRealPlayer(botAI->GetMaster()))
        {
            PlayerbotRepository::instance().Save(botAI);
        }
        if (!current())
            return partial();
        LOG_DEBUG("playerbots", "Bot {} logged out", bot->GetName().c_str());
        record.MarkStep(Journal::Step::BeforeSave);
        bot->SaveToDB(false, false);
        if (!current())
            return partial();
        record.MarkStep(Journal::Step::BeforeHolderErase);
        ForgetTimedBot(guid, ownerIncarnation, sessionIncarnation);
        RemoveFromPlayerbotsMap(guid);
        record.MarkStep(Journal::Step::HolderErased);
        record.MarkStep(Journal::Step::BeforeAIDelete);
        delete botAI;
        record.MarkStep(Journal::Step::AIDeleteReturned);
        return retirement.Complete() && record.Complete() ? Result::Completed : partial();
    }
    catch (...)
    {
        return partial();
    }
}

void PlayerbotHolder::RemoveFromPlayerbotsMap(ObjectGuid guid)
{
    playerBots.erase(guid);
}

Player* PlayerbotHolder::GetPlayerBot(ObjectGuid playerGuid) const
{
    PlayerBotMap::const_iterator it = playerBots.find(playerGuid);
    return (it == playerBots.end()) ? 0 : it->second;
}

Player* PlayerbotHolder::GetPlayerBot(ObjectGuid::LowType lowGuid) const
{
    ObjectGuid playerGuid = ObjectGuid::Create<HighGuid::Player>(lowGuid);
    PlayerBotMap::const_iterator it = playerBots.find(playerGuid);
    return (it == playerBots.end()) ? 0 : it->second;
}

void PlayerbotHolder::OnBotLogin(Player* const bot, PendingBotLogin::Owner& pendingOwner)
{
    if (!bot || !pendingOwner.CanPublish(bot->GetSession()))
        return;
    auto const guid = bot->GetGUID();
    auto const playerId = bot->GetPB59OwnerIncarnation();
    auto holderLife = _pb59HolderLifetime.Observe();
    if (playerBots.find(guid) != playerBots.end())
        return;

    // Owner-context scope blocks admitted PB retirement during initialization.
    // It does not own Player/session or guard unrelated Core destruction paths.
    auto callbackScope = bot->AcquirePB59CallbackScope();
    if (!callbackScope.Admitted())
        return;
    auto const sessionId = pendingOwner.Identity();
    PlayerbotsMgr::instance().AddPlayerbotData(bot, true);
    if (!holderLife->Alive() || ObjectAccessor::FindConnectedPlayer(guid) != bot ||
        bot->GetPB59OwnerIncarnation() != playerId || !pendingOwner.CanPublish(bot->GetSession()) ||
        pendingOwner.Get()->GetPlayer() != bot || _pendingLoginsStopping)
        return;
    auto [position, inserted] = playerBots.emplace(guid, bot);
    if (!inserted)
        return;
    // Preallocate the exact value-only timer slot before releasing the pending
    // session owner. No native callback between insertion, registration and release.
    if (!RegisterTimedBot(bot))
    {
        playerBots.erase(position);
        return;
    }
    if (!pendingOwner.ReleasePublished(bot->GetSession()))
    {
        ForgetTimedBot(guid, playerId, pendingOwner.Identity());
        playerBots.erase(position);
        return;
    }

    auto currentBot = [&]() -> bool
    {
        if (!holderLife->Alive())
            return false;
        Player* live = ObjectAccessor::FindConnectedPlayer(guid);
        if (!live || live != bot || live != GetPlayerBot(guid) ||
            live->GetPB59OwnerIncarnation() != playerId)
            return false;
        WorldSession* session = live->GetSession();
        return session && session->GetPlayer() == live &&
            session->GetPB59SessionIncarnation() == sessionId;
    };

    OnBotLoginInternal(bot);
    if (!currentBot())
        return;

    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    if (!botAI)
    {
        // Log a warning here to indicate that the botAI is null
        LOG_DEBUG("mod-playerbots", "PlayerbotAI is null for bot with GUID: {}", bot->GetGUID().GetRawValue());
        return;
    }

    PlayerbotIdentityGeneration::Instance().IssueLogin(bot->GetGUID());

    Player* master = botAI->GetMaster();
    ObjectGuid const masterGuid = master ? master->GetGUID() : ObjectGuid::Empty;
    auto const masterId = master ? master->GetPB59OwnerIncarnation() : 0;
    auto const masterSessionId = master && master->GetSession() ?
        master->GetSession()->GetPB59SessionIncarnation() : 0;
    auto current = [&]() -> bool
    {
        if (!currentBot() || GET_PLAYERBOT_AI(bot) != botAI)
            return false;
        if (!masterGuid)
            return botAI->GetMaster() == nullptr;
        Player* liveMaster = ObjectAccessor::FindConnectedPlayer(masterGuid);
        if (!liveMaster || liveMaster != master || liveMaster->GetPB59OwnerIncarnation() != masterId ||
            botAI->GetMaster() != liveMaster)
            return false;
        auto* session = liveMaster->GetSession();
        return session && session->GetPlayer() == liveMaster && masterSessionId &&
            session->GetPB59SessionIncarnation() == masterSessionId;
    };
    if (!current())
        return;

    Group* group = bot->GetGroup();
    if (group)
    {
        bool groupValid = false;
        Group::MemberSlotList const& slots = group->GetMemberSlots();
        for (Group::MemberSlotList::const_iterator i = slots.begin(); i != slots.end(); ++i)
        {
            ObjectGuid member = i->guid;
            if (master)
            {
                if (master->GetGUID() == member)
                {
                    groupValid = true;
                    break;
                }
            }

            // Don't disband alt groups when master goes away
            // Controlled by config
            if (sPlayerbotAIConfig.KeepAltsInGroup)
            {
                uint32 account = sCharacterCache->GetCharacterAccountIdByGuid(member);
                if (!sPlayerbotAIConfig.IsInRandomAccountList(account))
                {
                    groupValid = true;
                    break;
                }
            }
        }

        if (!groupValid)
        {
            botAI->LeaveOrDisbandGroup();
            if (!current())
                return;
        }
    }

    group = bot->GetGroup();
    if (group)
    {
        botAI->ResetStrategies();
    }
    else
    {
        botAI->ResetStrategies(!sRandomPlayerbotMgr.IsRandomBot(bot));
    }
    if (!current())
        return;
    PlayerbotRepository::instance().Load(botAI);
    if (!current())
        return;

    // The loaded strategies were saved at the bot's last strategy change and can include another
    // instance's strategy, so apply the current map's again.
    if (sPlayerbotAIConfig.ApplyInstanceStrategies)
    {
        botAI->ApplyInstanceStrategies(bot->GetMapId());
        if (!current())
            return;
    }

    if (master && !master->HasUnitState(UNIT_STATE_IN_FLIGHT))
    {
        bot->GetMotionMaster()->MovementExpired();
        bot->CleanupAfterTaxiFlight();
        if (!current())
            return;
    }

    // check activity
    botAI->AllowActivity(ALL_ACTIVITY, true);
    if (!current())
        return;

    // set delay on login
    botAI->SetNextCheckDelay(urand(2000, 4000));

    botAI->TellMaster(PlayerbotTextMgr::instance().GetBotTextOrDefault(
        "hello", "Hello!", {}), PLAYERBOT_SECURITY_TALK);
        if (!current())
            return;

    group = bot->GetGroup(); // callbacks above may have changed membership
    // Queue group operations for world thread
    if (master && master->GetGroup() && !group)
    {
        Group* mgroup = master->GetGroup();
        if (mgroup->GetMembersCount() >= 5)
        {
            // A 5-member party is full, so only a raid can take a 6th member. GroupInviteOperation::
            // Execute() self-converts a non-raid group with >= 5 members to a raid before adding, so a
            // separate ConvertToRaid is unnecessary here. Queue the invite for a raid OR a plain
            // (non-LFG/BG/battlefield) party; we deliberately skip LFG/BG/BFG groups so a bot login
            // never force-converts a live dungeon-finder or battleground group into a raid.
            if (mgroup->isRaidGroup() || (!mgroup->isLFGGroup() && !mgroup->isBGGroup() && !mgroup->isBFGroup()))
            {
                // Queue AddMember operation; Execute() converts the party to a raid before adding.
                auto addOp = std::make_unique<GroupInviteOperation>(master->GetGUID(), bot->GetGUID());
                PlayerbotWorldThreadProcessor::instance().QueueOperation(std::move(addOp));
            }
        }
        else
        {
            // Queue AddMember operation
            auto addOp = std::make_unique<GroupInviteOperation>(master->GetGUID(), bot->GetGUID());
            PlayerbotWorldThreadProcessor::instance().QueueOperation(std::move(addOp));
        }
    }
    else if (master && !group)
    {
        // Queue group creation and AddMember operation
        auto inviteOp = std::make_unique<GroupInviteOperation>(master->GetGUID(), bot->GetGUID());
        PlayerbotWorldThreadProcessor::instance().QueueOperation(std::move(inviteOp));
    }
    // if (master)
    // {
    //     // bot->TeleportTo(master);
    // }
    uint32 accountId = bot->GetSession()->GetAccountId();
    bool isRandomAccount = sPlayerbotAIConfig.IsInRandomAccountList(accountId);

    if (isRandomAccount && sPlayerbotAIConfig.RandomBotFixedLevel)
    {
        bot->SetPlayerFlag(PLAYER_FLAGS_NO_XP_GAIN);
    }
    else if (isRandomAccount && !sPlayerbotAIConfig.RandomBotFixedLevel)
    {
        bot->RemovePlayerFlag(PLAYER_FLAGS_NO_XP_GAIN);
    }

    bot->SaveToDB(false, false);
    if (!current())
        return;
    bool addClassBot = sRandomPlayerbotMgr.IsAccountType(accountId, 2);
    if (addClassBot && master && abs((int)master->GetLevel() - (int)bot->GetLevel()) > 3)
    {
        // PlayerbotFactory factory(bot, master->GetLevel());
        // factory.Randomize(false);
        uint32 mixedGearScore =
            PlayerbotAI::GetMixedGearScore(master, true, false, 12) * sPlayerbotAIConfig.AutoInitEquipLevelLimitRatio;
        // work around: distinguish from 0 if no gear
        if (mixedGearScore == 0)
            mixedGearScore = 1;
        PlayerbotFactory factory(bot, master->GetLevel(), ITEM_QUALITY_LEGENDARY, mixedGearScore);
        factory.Randomize(false);
        if (!current())
            return;
    }

    // bots join World chat if not solo oriented
    if (bot->GetLevel() >= 10 && sRandomPlayerbotMgr.IsRandomBot(bot) && GET_PLAYERBOT_AI(bot) &&
        GET_PLAYERBOT_AI(bot)->GetGrouperType() != GrouperType::SOLO)
    {
        // TODO make action/config
        // Make the bot join the world channel for chat
        WorldPacket pkt(CMSG_JOIN_CHANNEL);
        pkt << uint32(0) << uint8(0) << uint8(0);
        pkt << std::string("World");
        pkt << "";  // Pass
        bot->GetSession()->HandleJoinChannel(pkt);
        if (!current())
            return;
    }

    // join standard channels
    uint8 locale = BroadcastHelper::GetLocale();
    AreaTableEntry const* current_zone = GET_PLAYERBOT_AI(bot)->GetCurrentZone();
    ChannelMgr* cMgr = ChannelMgr::forTeam(bot->GetTeamId());
    std::string current_zone_name = current_zone ? PlayerbotAI::GetLocalizedAreaName(current_zone) : "";

    if (current_zone && cMgr)
    {
        for (uint32 i = 0; i < sChatChannelsStore.GetNumRows(); ++i)
        {
            ChatChannelsEntry const* channel = sChatChannelsStore.LookupEntry(i);
            if (!channel)
                continue;

            Channel* new_channel = nullptr;
            switch (channel->ChannelID)
            {
                case ChatChannelId::GENERAL:
                case ChatChannelId::LOCAL_DEFENSE:
                {
                    char new_channel_name_buf[100];
                    snprintf(new_channel_name_buf, 100, channel->pattern[locale], current_zone_name.c_str());
                    new_channel = cMgr->GetJoinChannel(new_channel_name_buf, channel->ChannelID);
                    break;
                }
                case ChatChannelId::TRADE:
                case ChatChannelId::GUILD_RECRUITMENT:
                {
                    char new_channel_name_buf[100];
                    //3459 is ID for a zone named "City" (only exists for the sake of using its name)
                    //Currently in magons TBC, if you switch zones, then you join "Trade - <zone>" and "GuildRecruitment - <zone>"
                    //which is a core bug, should be "Trade - City" and "GuildRecruitment - City" in both 1.12 and TBC
                    //but if you (actual player) logout in a city and log back in - you join "City" versions
                    constexpr uint32 AREA_ID_CITY = 3459;
                    std::string const cityName =
                        PlayerbotAI::GetLocalizedAreaName(sAreaTableStore.LookupEntry(AREA_ID_CITY));
                    snprintf(new_channel_name_buf, 100, channel->pattern[locale], cityName.c_str());
                    new_channel = cMgr->GetJoinChannel(new_channel_name_buf, channel->ChannelID);
                    break;
                }
                case ChatChannelId::LOOKING_FOR_GROUP:
                case ChatChannelId::WORLD_DEFENSE:
                {
                    new_channel = cMgr->GetJoinChannel(channel->pattern[locale], channel->ChannelID);
                    break;
                }
                default:
                    break;
            }

            if (new_channel)
            {
                new_channel->JoinChannel(bot, "");
                if (!current())
                    return;
            }
        }
    }
}

static std::string PersonalBotAccountRejection(uint32 accountId, ObjectGuid guid)
{
    if (!accountId)
        return "bot account classification unavailable";

    if (sRandomPlayerbotMgr.IsRndBotTypeAccount(accountId) ||
        (guid.IsPlayer() && sRandomPlayerbotMgr.IsRandomBot(guid.GetCounter())))
        return "random bots cannot be added as personal bots";

    if (sPlayerbotAIConfig.IsInRandomAccountList(accountId) &&
        !sRandomPlayerbotMgr.IsAddClassAccount(accountId))
        return "bot account classification unavailable";

    return {};
}

std::string const PlayerbotHolder::ProcessBotCommand(std::string const cmd, ObjectGuid guid, ObjectGuid masterguid,
                                                     bool admin, uint32 masterAccountId, uint32)
{
    if (!sPlayerbotAIConfig.Enabled || guid.IsEmpty())
        return "bot system is disabled";

    if (cmd == "add" || cmd == "addaccount" || cmd == "login")
    {
        uint32 accountId = sCharacterCache->GetCharacterAccountIdByGuid(guid);
        if (!accountId)
            return "character not found";

        if (std::string rejection = PersonalBotAccountRejection(accountId, guid); !rejection.empty())
            return rejection;

        if (ObjectAccessor::FindPlayer(guid))
            return "player already logged in";

        // For addaccount command, verify it's an account name
        if (cmd == "addaccount")
        {
            if (!sPlayerbotAIConfig.AllowAccountBots && accountId != masterAccountId &&
                !(sPlayerbotAIConfig.AllowTrustedAccountBots && IsAccountLinked(accountId, masterAccountId)))
            {
                return "you can only add bots from your own account or linked accounts";
            }
        }

        AddPlayerBot(guid, masterAccountId);
        return "ok";
    }
    else if (cmd == "remove" || cmd == "logout" || cmd == "rm")
    {
        if (!ObjectAccessor::FindPlayer(guid))
            return "player is offline";

        if (!GetPlayerBot(guid))
            return "not your bot";

        auto result = TryLogoutPlayerBot(guid);
        if (result == BotRetirementResult::RecoveryRequired)
            return "bot retirement incomplete; recovery required, do not retry";
        if (result == BotRetirementResult::Refused)
            return "bot logout refused; busy or unavailable, please retry";
        return "ok";
    }

    // if (admin)
    // {
    Player* bot = GetPlayerBot(guid);
    if (!bot)
        bot = sRandomPlayerbotMgr.GetPlayerBot(guid);

    if (!bot)
        return "bot not found";

    bool addClassBot = sRandomPlayerbotMgr.IsAddclassBot(guid.GetCounter());

    if (!addClassBot)
    {
        if (!(cmd == "refresh=raid" && sPlayerbotAIConfig.ResetInstanceIdForAltBots))
            return "ERROR: You can only use this command on addclass bots.";
    }

    if (!admin)
    {
        Player* master = ObjectAccessor::FindConnectedPlayer(masterguid);
        if (master && (master->IsInCombat() || bot->IsInCombat()))
        {
            return "ERROR: You can not use this command during combat.";
        }
    }

    if (GET_PLAYERBOT_AI(bot))
    {
        if (Player* master = GET_PLAYERBOT_AI(bot)->GetMaster())
        {
            if (master->GetSession()->GetSecurity() <= SEC_PLAYER && sPlayerbotAIConfig.AutoInitOnly &&
                cmd != "init=auto")
            {
                return "The command is not allowed, use init=auto instead.";
            }

            //  Use boot guard
            BotInitGuard guard(bot->GetGUID());
            if (guard.IsLocked())
            {
                return "Initialization already in progress, please wait.";
            }

            int gs;
            if (cmd == "init=white" || cmd == "init=common")
            {
                PlayerbotFactory factory(bot, master->GetLevel(), ITEM_QUALITY_NORMAL);
                factory.Randomize(false);
                return "ok";
            }
            else if (cmd == "init=green" || cmd == "init=uncommon")
            {
                PlayerbotFactory factory(bot, master->GetLevel(), ITEM_QUALITY_UNCOMMON);
                factory.Randomize(false);
                return "ok";
            }
            else if (cmd == "init=blue" || cmd == "init=rare")
            {
                PlayerbotFactory factory(bot, master->GetLevel(), ITEM_QUALITY_RARE);
                factory.Randomize(false);
                return "ok";
            }
            else if (cmd == "init=epic" || cmd == "init=purple")
            {
                PlayerbotFactory factory(bot, master->GetLevel(), ITEM_QUALITY_EPIC);
                factory.Randomize(false);
                return "ok";
            }
            else if (cmd == "init=legendary" || cmd == "init=yellow")
            {
                PlayerbotFactory factory(bot, master->GetLevel(), ITEM_QUALITY_LEGENDARY);
                factory.Randomize(false);
                return "ok";
            }
            else if (cmd == "init=auto")
            {
                uint32 mixedGearScore = PlayerbotAI::GetMixedGearScore(master, true, false, 12) *
                                        sPlayerbotAIConfig.AutoInitEquipLevelLimitRatio;
                // work around: distinguish from 0 if no gear
                if (mixedGearScore == 0)
                    mixedGearScore = 1;
                PlayerbotFactory factory(bot, master->GetLevel(), ITEM_QUALITY_LEGENDARY, mixedGearScore);
                factory.Randomize(false);
                return "ok, gear score limit: " + std::to_string(mixedGearScore / PlayerbotAI::GetItemScoreMultiplier(ItemQualities(ITEM_QUALITY_EPIC))) +
                       "(for epic)";
            }
            else if (cmd.starts_with("init=") && sscanf(cmd.c_str(), "init=%d", &gs) != -1)
            {
                PlayerbotFactory factory(bot, master->GetLevel(), ITEM_QUALITY_LEGENDARY, gs);
                factory.Randomize(false);
                return "ok, gear score limit: " + std::to_string(gs / PlayerbotAI::GetItemScoreMultiplier(ItemQualities(ITEM_QUALITY_EPIC))) + "(for epic)";
            }
        }

        if (cmd == "refresh=raid")
        {  // TODO: This function is not perfect yet. If you are already in a raid,
            // after the command is executed, the AI ​​needs to go back online or exit the raid and re-enter.
            PlayerbotFactory factory(bot, bot->GetLevel());
            factory.UnbindInstance();
            return "ok";
        }
    }

    if (cmd == "levelup" || cmd == "level")
    {
        PlayerbotFactory factory(bot, bot->GetLevel());
        factory.Randomize(true);
        return "ok";
    }
    else if (cmd == "refresh")
    {
        PlayerbotFactory factory(bot, bot->GetLevel());
        factory.Refresh();
        return "ok";
    }
    else if (cmd == "random")
    {
        sRandomPlayerbotMgr.Randomize(bot);
        return "ok";
    }
    else if (cmd == "quests")
    {
        PlayerbotFactory factory(bot, bot->GetLevel());
        factory.InitInstanceQuests();
        return "Initialization quests";
    }
    // }

    return "unknown command";
}

// Added for gender choice : Returns the gender of an offline character: 0 = male, 1 = female.
static uint8 GetOfflinePlayerGender(ObjectGuid guid)
{
    QueryResult result = CharacterDatabase.Query(
        "SELECT gender FROM characters WHERE guid = {}", guid.GetCounter());

    if (result)
        return (*result)[0].Get<uint8>();       // 0 = male, 1 = female

    return GENDER_MALE;                         // fallback value
}

bool PlayerbotMgr::HandlePlayerbotMgrCommand(ChatHandler* handler, char const* args)
{
    if (!sPlayerbotAIConfig.Enabled)
    {
        handler->PSendSysMessage("|cffff0000Playerbot system is currently disabled!");
        return false;
    }

    WorldSession* m_session = handler->GetSession();
    if (!m_session)
    {
        handler->PSendSysMessage("You may only add bots from an active session");
        return false;
    }

    Player* player = m_session->GetPlayer();
    PlayerbotMgr* mgr = GET_PLAYERBOT_MGR(player);
    if (!mgr)
    {
        handler->PSendSysMessage("You cannot control bots yet");
        return false;
    }

    std::vector<std::string> messages = mgr->HandlePlayerbotCommand(args, player);
    if (messages.empty())
        return true;

    for (std::vector<std::string>::iterator i = messages.begin(); i != messages.end(); ++i)
    {
        handler->PSendSysMessage("{}", i->c_str());
    }

    return true;
}

std::vector<std::string> PlayerbotHolder::HandlePlayerbotCommand(char const* args, Player* master)
{
    std::vector<std::string> messages;

    if (!*args)
    {
        messages.push_back("usage: list/reload/tweak/self or add/addaccount/init/remove PLAYERNAME\n");
        messages.push_back("usage: addclass CLASSNAME [male|female|0|1]");
        return messages;
    }

    char* cmd = strtok((char*)args, " ");
    char* charname = strtok(nullptr, " ");
    char* genderArg = strtok(nullptr, " ");    // Added for gender choice [male|female|0|1] optionnel

    if (!cmd)
    {
        messages.push_back("usage: list/reload/tweak/self or add/init/remove PLAYERNAME or addclass CLASSNAME [male|female]");
        return messages;
    }

    if (!strcmp(cmd, "initself"))
    {
        if (master->CanBeGameMaster())
        {
            // OnBotLogin(master);
            PlayerbotFactory factory(master, master->GetLevel(), ITEM_QUALITY_EPIC);
            factory.Randomize(false);
            messages.push_back("initself ok");
            return messages;
        }
        else
        {
            messages.push_back("ERROR: Only GM can use this command.");
            return messages;
        }
    }

    if (!strncmp(cmd, "initself=", 9))
    {
        if (!strcmp(cmd, "initself=uncommon"))
        {
            if (master->CanBeGameMaster())
            {
                // OnBotLogin(master);
                PlayerbotFactory factory(master, master->GetLevel(), ITEM_QUALITY_UNCOMMON);
                factory.Randomize(false);
                messages.push_back("initself ok");
                return messages;
            }
            else
            {
                messages.push_back("ERROR: Only GM can use this command.");
                return messages;
            }
        }
        if (!strcmp(cmd, "initself=rare"))
        {
            if (master->CanBeGameMaster())
            {
                // OnBotLogin(master);
                PlayerbotFactory factory(master, master->GetLevel(), ITEM_QUALITY_RARE);
                factory.Randomize(false);
                messages.push_back("initself ok");
                return messages;
            }
            else
            {
                messages.push_back("ERROR: Only GM can use this command.");
                return messages;
            }
        }
        if (!strcmp(cmd, "initself=epic"))
        {
            if (master->CanBeGameMaster())
            {
                // OnBotLogin(master);
                PlayerbotFactory factory(master, master->GetLevel(), ITEM_QUALITY_EPIC);
                factory.Randomize(false);
                messages.push_back("initself ok");
                return messages;
            }
            else
            {
                messages.push_back("ERROR: Only GM can use this command.");
                return messages;
            }
        }
        if (!strcmp(cmd, "initself=legendary"))
        {
            if (master->CanBeGameMaster())
            {
                // OnBotLogin(master);
                PlayerbotFactory factory(master, master->GetLevel(), ITEM_QUALITY_LEGENDARY);
                factory.Randomize(false);
                messages.push_back("initself ok");
                return messages;
            }
            else
            {
                messages.push_back("ERROR: Only GM can use this command.");
                return messages;
            }
        }
        int32 gs;
        if (sscanf(cmd, "initself=%d", &gs) != -1)
        {
            if (master->CanBeGameMaster())
            {
                // OnBotLogin(master);
                PlayerbotFactory factory(master, master->GetLevel(), ITEM_QUALITY_LEGENDARY, gs);
                factory.Randomize(false);
                messages.push_back("initself ok, gs = " + std::to_string(gs));
                return messages;
            }
            else
            {
                messages.push_back("ERROR: Only GM can use this command.");
                return messages;
            }
        }
    }

    if (!strcmp(cmd, "list"))
    {
        messages.push_back(ListBots(master));
        return messages;
    }

    if (!strcmp(cmd, "reload"))
    {
        if (master->CanBeGameMaster())
        {
            sPlayerbotAIConfig.Initialize();
            messages.push_back("Config reloaded.");
            return messages;
        }
        else
        {
            messages.push_back("ERROR: Only GM can use this command.");
            return messages;
        }
    }

    if (!strcmp(cmd, "tweak"))
    {
        sPlayerbotAIConfig.TweakValue = sPlayerbotAIConfig.TweakValue++;
        if (sPlayerbotAIConfig.TweakValue > 2)
            sPlayerbotAIConfig.TweakValue = 0;

        messages.push_back("Set tweakvalue to " + std::to_string(sPlayerbotAIConfig.TweakValue));
        return messages;
    }

    if (!strcmp(cmd, "self"))
    {
        if (GET_PLAYERBOT_AI(master))
        {
            messages.push_back("SelfBot is now deactivated.");
            delete GET_PLAYERBOT_AI(master);

            if (master->isTaxiCheater())
                master->SetTaxiCheater(false);
        }
        else if (sPlayerbotAIConfig.SelfBotLevel == 0)
            messages.push_back("SelfBot is disabled server-wide.");
        else if (sPlayerbotAIConfig.SelfBotLevel == 1 && !master->CanBeGameMaster())
            messages.push_back("SelfBot is restricted for this account.");
        else
        {
            messages.push_back("SelfBot is now active.");
            PlayerbotsMgr::instance().AddPlayerbotData(master, true);
            GET_PLAYERBOT_AI(master)->SetMaster(master);
            PlayerbotRepository::instance().Load(GET_PLAYERBOT_AI(master));
        }

        return messages;
    }

    if (!strcmp(cmd, "lookup"))
    {
        messages.push_back(LookupBots(master));
        return messages;
    }

    if (!strcmp(cmd, "addclass"))
    {
        if (sPlayerbotAIConfig.AddClassCommand == 0 && !master->CanBeGameMaster())
        {
            messages.push_back("You do not have permission to create bot by addclass command");
            return messages;
        }
        if (!charname)
        {
            messages.push_back(
                "addclass: invalid CLASSNAME(warrior/paladin/hunter/rogue/priest/shaman/mage/warlock/druid/dk)");
            return messages;
        }
        uint8 claz;
        if (!strcmp(charname, "warrior"))
        {
            claz = 1;
        }
        else if (!strcmp(charname, "paladin"))
        {
            claz = 2;
        }
        else if (!strcmp(charname, "hunter"))
        {
            claz = 3;
        }
        else if (!strcmp(charname, "rogue"))
        {
            claz = 4;
        }
        else if (!strcmp(charname, "priest"))
        {
            claz = 5;
        }
        else if (!strcmp(charname, "shaman"))
        {
            claz = 7;
        }
        else if (!strcmp(charname, "mage"))
        {
            claz = 8;
        }
        else if (!strcmp(charname, "warlock"))
        {
            claz = 9;
        }
        else if (!strcmp(charname, "druid"))
        {
            claz = 11;
        }
        else if (!strcmp(charname, "dk"))
        {
            claz = 6;
        }
        else
        {
            messages.push_back("Error: Invalid Class. Try again.");
            return messages;
        }
        //  Added for gender choice : Parsing gender
        int8 gender = -1; // -1 = gender will be random
        if (genderArg)
        {
            std::string g = genderArg;
            std::transform(g.begin(), g.end(), g.begin(), ::tolower);

            if (g == "male" || g == "0")
                gender = GENDER_MALE; // 0
            else if (g == "female" || g == "1")
                gender = GENDER_FEMALE; // 1
            else
            {
                messages.push_back("Unknown gender : " + g + " (male/female/0/1)");
                return messages;
            }
        } //end

        if (claz == 6 && master->GetLevel() < sWorld->getIntConfig(CONFIG_START_HEROIC_PLAYER_LEVEL))
        {
            messages.push_back("Your level is too low to summon Deathknight");
            return messages;
        }
        uint8 teamId = master->GetTeamId(true);
        std::unordered_set<ObjectGuid> const& guidCache = sRandomPlayerbotMgr.addclassCache[RandomPlayerbotMgr::GetTeamClassIdx(teamId == TEAM_ALLIANCE, claz)];
        auto const pickFirstEligible = [&](auto const& pool) -> ObjectGuid
        {
            for (ObjectGuid const& guid : pool)
            {
                // If the user requested a specific gender, skip any character that doesn't match.
                if (gender != -1 && GetOfflinePlayerGender(guid) != gender)
                    continue;
                if (botLoading.find(guid) != botLoading.end())
                    continue;
                if (ObjectAccessor::FindConnectedPlayer(guid))
                    continue;
                uint32 guildId = sCharacterCache->GetCharacterGuildIdByGuid(guid);
                if (guildId && PlayerbotGuildMgr::instance().IsRealGuild(guildId))
                    continue;
                return guid;
            }
            return ObjectGuid::Empty;
        };

        // The cache is an unordered_set: its iteration order does not change between calls, so the first
        // eligible character is always the same one. Walk a shuffled copy when a random one is wanted,
        // and the cache itself otherwise, so the default path copies nothing.
        ObjectGuid picked = ObjectGuid::Empty;
        if (sPlayerbotAIConfig.AddClassRandomCharacter)
        {
            std::vector<ObjectGuid> candidates(guidCache.begin(), guidCache.end());
            Acore::Containers::RandomShuffle(candidates);
            picked = pickFirstEligible(candidates);
        }
        else
            picked = pickFirstEligible(guidCache);

        if (picked.IsEmpty())
        {
            messages.push_back("Add class failed, no available characters!");
            return messages;
        }

        AddPlayerBot(picked, master->GetSession()->GetAccountId());
        messages.push_back("Add class " + std::string(charname));
        return messages;
    }

    std::string charnameStr;

    if (!charname)
    {
        std::string name;
        bool isPlayer = sCharacterCache->GetCharacterNameByGuid(master->GetTarget(), name);
        // Player* tPlayer = ObjectAccessor::FindConnectedPlayer(master->GetTarget());
        if (isPlayer)
        {
            charnameStr = name;
        }
        else
        {
            messages.push_back("usage: list/reload/tweak/self or add/init/remove PLAYERNAME");
            return messages;
        }
    }
    else
    {
        charnameStr = charname;
    }

    std::string const cmdStr = cmd;

    std::unordered_set<std::string> bots;
    if (charnameStr == "*" && master)
    {
        Group* group = master->GetGroup();
        if (!group)
        {
            messages.push_back("you must be in group");
            return messages;
        }

        Group::MemberSlotList slots = group->GetMemberSlots();
        for (Group::member_citerator i = slots.begin(); i != slots.end(); i++)
        {
            ObjectGuid member = i->guid;

            if (member == master->GetGUID())
                continue;

            std::string bot;
            if (sCharacterCache->GetCharacterNameByGuid(member, bot))
                bots.insert(bot);
        }
    }

    if (charnameStr == "!" && master && master->GetSession()->GetSecurity() > SEC_GAMEMASTER)
    {
        for (PlayerBotMap::const_iterator i = GetPlayerBotsBegin(); i != GetPlayerBotsEnd(); ++i)
        {
            if (Player* bot = i->second)
                if (bot->IsInWorld())
                    bots.insert(bot->GetName());
        }
    }

    std::vector<std::string> chars = split(charnameStr, ',');
    for (std::vector<std::string>::iterator i = chars.begin(); i != chars.end(); i++)
    {
        std::string s = *i;

        if (!strcmp(cmd, "addaccount"))
        {
            // When using addaccount, first try to get account ID directly
            uint32 accountId = GetAccountId(s);
            if (!accountId)
            {
                // If not found, try to get account ID from character name
                std::string charName = s;
                if (!normalizePlayerName(charName))
                {
                    messages.push_back("Neither account nor character '" + s + "' found");
                    continue;
                }
                ObjectGuid charGuid = sCharacterCache->GetCharacterGuidByName(charName);
                if (!charGuid)
                {
                    messages.push_back("Neither account nor character '" + s + "' found");
                    continue;
                }
                accountId = sCharacterCache->GetCharacterAccountIdByGuid(charGuid);
                if (!accountId)
                {
                    messages.push_back("Could not find account for character '" + s + "'");
                    continue;
                }
            }

            if (std::string rejection = PersonalBotAccountRejection(accountId, ObjectGuid::Empty);
                !rejection.empty())
            {
                messages.push_back("Account '" + s + "': " + rejection);
                continue;
            }

            QueryResult results = CharacterDatabase.Query("SELECT name FROM characters WHERE account = {}", accountId);
            if (results)
            {
                do
                {
                    Field* fields = results->Fetch();
                    std::string const charName = fields[0].Get<std::string>();
                    bots.insert(charName);
                } while (results->NextRow());
            }
        }
        else
        {
            // For regular add command, only add the specific character
            if (!normalizePlayerName(s))
            {
                messages.push_back("Character '" + *i + "' not found");
                continue;
            }
            ObjectGuid charGuid = sCharacterCache->GetCharacterGuidByName(s);
            if (!charGuid)
            {
                messages.push_back("Character '" + s + "' not found");
                continue;
            }
            bots.insert(s);
        }
    }

    for (auto i = bots.begin(); i != bots.end(); ++i)
    {
        std::string const bot = *i;

        std::ostringstream out;
        out << cmdStr << ": " << bot << " - ";

        ObjectGuid member = sCharacterCache->GetCharacterGuidByName(bot);
        if (!member)
        {
            out << "character not found";
        }
        else if (master && member != master->GetGUID())
        {
            out << ProcessBotCommand(cmdStr, member, master->GetGUID(),
                                     master->CanBeGameMaster(),
                                     master->GetSession()->GetAccountId(), master->GetGuildId());
        }
        else if (!master)
        {
            out << ProcessBotCommand(cmdStr, member, ObjectGuid::Empty, true, -1, -1);
        }

        messages.push_back(out.str());
    }

    return messages;
}

uint32 PlayerbotHolder::GetAccountId(std::string const name) { return AccountMgr::GetId(name); }

uint32 PlayerbotHolder::GetAccountId(ObjectGuid guid)
{
    if (!guid.IsPlayer())
        return 0;

    // prevent DB access for online player
    if (Player* player = ObjectAccessor::FindConnectedPlayer(guid))
        return player->GetSession()->GetAccountId();

    ObjectGuid::LowType lowguid = guid.GetCounter();

    if (QueryResult result = CharacterDatabase.Query("SELECT account FROM characters WHERE guid = {}", lowguid))
    {
        uint32 acc = (*result)[0].Get<uint32>();
        return acc;
    }

    return 0;
}

std::string const PlayerbotHolder::ListBots(Player* master)
{
    std::set<std::string> bots;
    std::map<uint8, std::string> classNames;

    classNames[CLASS_DEATH_KNIGHT] = "Death Knight";
    classNames[CLASS_DRUID] = "Druid";
    classNames[CLASS_HUNTER] = "Hunter";
    classNames[CLASS_MAGE] = "Mage";
    classNames[CLASS_PALADIN] = "Paladin";
    classNames[CLASS_PRIEST] = "Priest";
    classNames[CLASS_ROGUE] = "Rogue";
    classNames[CLASS_SHAMAN] = "Shaman";
    classNames[CLASS_WARLOCK] = "Warlock";
    classNames[CLASS_WARRIOR] = "Warrior";
    classNames[CLASS_DEATH_KNIGHT] = "DeathKnight";

    std::map<std::string, std::string> online;
    std::vector<std::string> names;
    std::map<std::string, std::string> classes;

    for (PlayerBotMap::const_iterator it = GetPlayerBotsBegin(); it != GetPlayerBotsEnd(); ++it)
    {
        Player* const bot = it->second;
        std::string const name = bot->GetName();
        bots.insert(name);

        names.push_back(name);
        online[name] = "+";
        classes[name] = classNames[bot->getClass()];
    }

    if (master)
    {
        QueryResult results = CharacterDatabase.Query("SELECT class, name FROM characters WHERE account = {}",
                                                      master->GetSession()->GetAccountId());
        if (results)
        {
            do
            {
                Field* fields = results->Fetch();
                uint8 cls = fields[0].Get<uint8>();
                std::string const name = fields[1].Get<std::string>();
                if (bots.find(name) == bots.end() && name != master->GetSession()->GetPlayerName())
                {
                    names.push_back(name);
                    online[name] = "-";
                    classes[name] = classNames[cls];
                }
            } while (results->NextRow());
        }
    }

    std::sort(names.begin(), names.end());

    if (Group* group = master->GetGroup())
    {
        Group::MemberSlotList const& groupSlot = group->GetMemberSlots();
        for (Group::member_citerator itr = groupSlot.begin(); itr != groupSlot.end(); itr++)
        {
            Player* member = ObjectAccessor::FindPlayer(itr->guid);
            if (member && sRandomPlayerbotMgr.IsRandomBot(member))
            {
                std::string const name = member->GetName();

                names.push_back(name);
                online[name] = "+";
                classes[name] = classNames[member->getClass()];
            }
        }
    }

    std::ostringstream out;
    bool first = true;
    out << "Bot roster: ";
    for (std::vector<std::string>::iterator i = names.begin(); i != names.end(); ++i)
    {
        if (first)
            first = false;
        else
            out << ", ";

        std::string const name = *i;
        out << online[name] << name << " " << classes[name];
    }

    return out.str();
}

std::string const PlayerbotHolder::LookupBots(Player*)
{
    std::list<std::string> messages;
    messages.push_back("Classes Available:");
    messages.push_back("|TInterface\\icons\\INV_Sword_27.png:25:25:0:-1|t Warrior");
    messages.push_back("|TInterface\\icons\\INV_Hammer_01.png:25:25:0:-1|t Paladin");
    messages.push_back("|TInterface\\icons\\INV_Weapon_Bow_07.png:25:25:0:-1|t Hunter");
    messages.push_back("|TInterface\\icons\\INV_ThrowingKnife_04.png:25:25:0:-1|t Rogue");
    messages.push_back("|TInterface\\icons\\INV_Staff_30.png:25:25:0:-1|t Priest");
    messages.push_back("|TInterface\\icons\\inv_jewelry_talisman_04.png:25:25:0:-1|t Shaman");
    messages.push_back("|TInterface\\icons\\INV_staff_30.png:25:25:0:-1|t Mage");
    messages.push_back("|TInterface\\icons\\INV_staff_30.png:25:25:0:-1|t Warlock");
    messages.push_back("|TInterface\\icons\\Ability_Druid_Maul.png:25:25:0:-1|t Druid");
    messages.push_back("DK");
    messages.push_back("(Usage: .bot lookup CLASS)");
    std::string ret_msg;
    for (std::string msg : messages)
    {
        ret_msg += msg + "\n";
    }
    return ret_msg;
}

uint32 PlayerbotHolder::GetPlayerbotsCountByClass(uint32 cls)
{
    uint32 count = 0;
    for (PlayerBotMap::const_iterator it = GetPlayerBotsBegin(); it != GetPlayerBotsEnd(); ++it)
    {
        Player* const bot = it->second;
        if (bot && bot->IsInWorld() && bot->getClass() == cls)
        {
            count++;
        }
    }
    return count;
}

PlayerbotMgr::PlayerbotMgr(Player* const master) : PlayerbotHolder(), master(master), lastErrorTell(0) {}

PlayerbotMgr::~PlayerbotMgr()
{
    _pb59HolderLifetime.Detach();
    if (master)
        PlayerbotsMgr::instance().RemovePlayerBotData(master->GetGUID(), false);
}

void PlayerbotMgr::UpdateAIInternal(uint32 elapsed, bool /*minimal*/)
{
    SetNextCheckDelay(sPlayerbotAIConfig.ReactDelay);
    CheckTellErrors(elapsed);
}

void PlayerbotMgr::HandleCommand(uint32 type, std::string const text)
{
    Player* master = GetMaster();
    if (!master)
        return;

    if (text.find(sPlayerbotAIConfig.CommandSeparator) != std::string::npos)
    {
        std::vector<std::string> commands;
        split(commands, text, sPlayerbotAIConfig.CommandSeparator.c_str());
        for (std::vector<std::string>::iterator i = commands.begin(); i != commands.end(); ++i)
        {
            HandleCommand(type, *i);
        }

        return;
    }

    for (PlayerBotMap::const_iterator it = GetPlayerBotsBegin(); it != GetPlayerBotsEnd(); ++it)
    {
        Player* const bot = it->second;
        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        if (botAI)
            botAI->HandleCommand(type, text, master);
    }

    for (PlayerBotMap::const_iterator it = sRandomPlayerbotMgr.GetPlayerBotsBegin();
         it != sRandomPlayerbotMgr.GetPlayerBotsEnd(); ++it)
    {
        Player* const bot = it->second;
        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        if (botAI && botAI->GetMaster() == master)
            botAI->HandleCommand(type, text, master);
    }
}

void PlayerbotMgr::HandleMasterIncomingPacket(WorldPacket const& packet)
{
    for (PlayerBotMap::const_iterator it = GetPlayerBotsBegin(); it != GetPlayerBotsEnd(); ++it)
    {
        Player* const bot = it->second;
        if (!bot)
            continue;
        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        if (botAI)
            botAI->HandleMasterIncomingPacket(packet);
    }

    for (PlayerBotMap::const_iterator it = sRandomPlayerbotMgr.GetPlayerBotsBegin();
         it != sRandomPlayerbotMgr.GetPlayerBotsEnd(); ++it)
    {
        Player* const bot = it->second;
        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        if (botAI && botAI->GetMaster() == GetMaster())
            botAI->HandleMasterIncomingPacket(packet);
    }

    switch (packet.GetOpcode())
    {
        // if master is logging out, log out all bots
        case CMSG_LOGOUT_REQUEST:
        {
            Player* master = GetMaster();
            if (master)
            {
                // Replicate the AFK logout prevention checks from WorldSession::HandleLogoutRequestOpcode
                // so bots are not logged out when the master's own logout is going to be prevented.
                AreaTableEntry const* areaEntry = sAreaTableStore.LookupEntry(master->GetAreaId());
                bool preventAfkSanctuaryLogout = sWorld->getIntConfig(CONFIG_AFK_PREVENT_LOGOUT) == 1
                                                 && master->isAFK() && areaEntry && areaEntry->IsSanctuary();

                bool preventAfkLogout = sWorld->getIntConfig(CONFIG_AFK_PREVENT_LOGOUT) == 2
                                        && master->isAFK();

                if (preventAfkSanctuaryLogout || preventAfkLogout)
                {
                    break;
                }
            }

            LogoutAllBots();
            break;
        }
        // if master cancelled logout, cancel too
        case CMSG_LOGOUT_CANCEL:
        {
            CancelLogout();
            break;
        }
    }
}

void PlayerbotMgr::HandleMasterOutgoingPacket(WorldPacket const& packet)
{
    for (PlayerBotMap::const_iterator it = GetPlayerBotsBegin(); it != GetPlayerBotsEnd(); ++it)
    {
        Player* const bot = it->second;
        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        if (botAI)
            botAI->HandleMasterOutgoingPacket(packet);
    }

    for (PlayerBotMap::const_iterator it = sRandomPlayerbotMgr.GetPlayerBotsBegin();
         it != sRandomPlayerbotMgr.GetPlayerBotsEnd(); ++it)
    {
        Player* const bot = it->second;
        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        if (botAI && botAI->GetMaster() == GetMaster())
            botAI->HandleMasterOutgoingPacket(packet);
    }
}

void PlayerbotMgr::SaveToDB()
{
    for (PlayerBotMap::const_iterator it = GetPlayerBotsBegin(); it != GetPlayerBotsEnd(); ++it)
    {
        Player* const bot = it->second;
        bot->SaveToDB(false, false);
    }

    for (PlayerBotMap::const_iterator it = sRandomPlayerbotMgr.GetPlayerBotsBegin();
         it != sRandomPlayerbotMgr.GetPlayerBotsEnd(); ++it)
    {
        Player* const bot = it->second;
        if (GET_PLAYERBOT_AI(bot) && GET_PLAYERBOT_AI(bot)->GetMaster() == GetMaster())
            bot->SaveToDB(false, false);
    }
}

void PlayerbotMgr::OnBotLoginInternal(Player* const bot)
{
    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    if (!botAI)
    {
        return;
    }
    botAI->SetMaster(master);
    botAI->ResetStrategies();

    LOG_INFO("playerbots", "Bot {} logged in", bot->GetName().c_str());
}

void PlayerbotMgr::OnPlayerLogin(Player* player)
{
    if (!player)
        return;

    WorldSession* session = player->GetSession();
    if (!session)
    {
        LOG_WARN("playerbots", "Unable to register locale priority for player {} because the session is missing", player->GetName());
        return;
    }

    // DB locale (source of bot text translation)
    LocaleConstant const databaseLocale = session->GetSessionDbLocaleIndex();

    // For bot texts (DB-driven), prefer the database locale with a safe fallback.
    LocaleConstant usedLocale = databaseLocale;
    if (usedLocale >= TOTAL_LOCALES)
        usedLocale = LOCALE_enUS; // fallback

    // set locale priority for bot texts
    PlayerbotTextMgr::instance().AddLocalePriority(usedLocale);

    if (sPlayerbotAIConfig.SelfBotLevel > 2)
        HandlePlayerbotCommand("self", player);

    if (!sPlayerbotAIConfig.BotAutologin)
        return;

    uint32 accountId = session->GetAccountId();
    QueryResult results = CharacterDatabase.Query("SELECT name FROM characters WHERE account = {}", accountId);
    if (results)
    {
        std::ostringstream out;
        out << "add ";
        bool first = true;
        do
        {
            Field* fields = results->Fetch();

            if (first)
                first = false;
            else
                out << ",";

            out << fields[0].Get<std::string>();
        } while (results->NextRow());

        HandlePlayerbotCommand(out.str().c_str(), player);
    }
}

void PlayerbotMgr::TellError(std::string const botName, std::string const text)
{
    std::set<std::string> names = errors[text];
    if (names.find(botName) == names.end())
    {
        names.insert(botName);
    }

    errors[text] = names;
}

void PlayerbotMgr::CheckTellErrors(uint32 /*elapsed*/)
{
    time_t now = time(nullptr);
    if ((now - lastErrorTell) < sPlayerbotAIConfig.ErrorDelay / 1000)
        return;

    lastErrorTell = now;

    for (PlayerBotErrorMap::iterator i = errors.begin(); i != errors.end(); ++i)
    {
        std::string const text = i->first;
        std::set<std::string> names = i->second;

        std::ostringstream out;
        bool first = true;
        for (std::set<std::string>::iterator j = names.begin(); j != names.end(); ++j)
        {
            if (!first)
                out << ", ";
            else
                first = false;

            out << *j;
        }

        out << "|cfff00000: " << text;

        ChatHandler(master->GetSession()).PSendSysMessage(out.str().c_str());
    }

    errors.clear();
}

void PlayerbotsMgr::AddPlayerbotData(Player* player, bool isBotAI)
{
    if (!player)
    {
        return;
    }
    // If the guid already exists in the map, remove it

    if (!isBotAI)
    {
        std::unordered_map<ObjectGuid, PlayerbotAIBase*>::iterator itr = _playerbotsMgrMap.find(player->GetGUID());
        if (itr != _playerbotsMgrMap.end())
        {
            _playerbotsMgrMap.erase(itr);
        }
        PlayerbotMgr* playerbotMgr = new PlayerbotMgr(player);
        ASSERT(_playerbotsMgrMap.emplace(player->GetGUID(), playerbotMgr).second);

        playerbotMgr->OnPlayerLogin(player);
    }
    else
    {
        std::unordered_map<ObjectGuid, PlayerbotAIBase*>::iterator itr = _playerbotsAIMap.find(player->GetGUID());
        if (itr != _playerbotsAIMap.end())
        {
            _playerbotsAIMap.erase(itr);
        }
        PlayerbotAI* botAI = new PlayerbotAI(player);
        ASSERT(_playerbotsAIMap.emplace(player->GetGUID(), botAI).second);
    }
}

void PlayerbotsMgr::RemovePlayerBotData(ObjectGuid const& guid, bool is_AI)
{
    if (is_AI)
    {
        std::unordered_map<ObjectGuid, PlayerbotAIBase*>::iterator itr = _playerbotsAIMap.find(guid);
        if (itr != _playerbotsAIMap.end())
        {
            _playerbotsAIMap.erase(itr);
        }
    }
    else
    {
        std::unordered_map<ObjectGuid, PlayerbotAIBase*>::iterator itr = _playerbotsMgrMap.find(guid);
        if (itr != _playerbotsMgrMap.end())
        {
            _playerbotsMgrMap.erase(itr);
        }
    }
}

PlayerbotAI* PlayerbotsMgr::GetPlayerbotAI(Player* player)
{
    if (!(sPlayerbotAIConfig.Enabled) || !player)
    {
        return nullptr;
    }
    // if (player->GetSession()->IsLoggingOut() || player->IsDuringRemoveFromWorld())
    // {
    //     return nullptr;
    // }
    auto itr = _playerbotsAIMap.find(player->GetGUID());
    if (itr != _playerbotsAIMap.end())
    {
        if (itr->second->IsBotAI())
            return dynamic_cast<PlayerbotAI*>(itr->second);
    }

    return nullptr;
}

PlayerbotMgr* PlayerbotsMgr::GetPlayerbotMgr(Player* player)
{
    if (!(sPlayerbotAIConfig.Enabled) || !player)
    {
        return nullptr;
    }
    auto itr = _playerbotsMgrMap.find(player->GetGUID());
    if (itr != _playerbotsMgrMap.end())
    {
        if (!itr->second->IsBotAI())
            return dynamic_cast<PlayerbotMgr*>(itr->second);
    }

    return nullptr;
}

void PlayerbotMgr::HandleSetSecurityKeyCommand(Player* player, std::string const& key)
{
    uint32 accountId = player->GetSession()->GetAccountId();

    // Hash the security key using SHA-256
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256((unsigned char*)key.c_str(), key.size(), hash);

    // Convert the hash to a hexadecimal string
    std::ostringstream hashedKey;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i)
        hashedKey << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];

    // Store the hashed key in the database
    PlayerbotsDatabasePreparedStatement* stmt = PlayerbotsDatabase.GetPreparedStatement(PLAYERBOTS_REP_ACCOUNT_KEY);
    stmt->SetData(0, accountId);
    stmt->SetData(1, hashedKey.str());
    PlayerbotsDatabase.Execute(stmt);

    ChatHandler(player->GetSession()).PSendSysMessage("Security key set successfully.");
}

void PlayerbotMgr::HandleLinkAccountCommand(Player* player, std::string const& accountName, std::string const& key)
{
    QueryResult result = LoginDatabase.Query("SELECT id FROM account WHERE username = '{}'", accountName);
    if (!result)
    {
        ChatHandler(player->GetSession()).PSendSysMessage("Account not found.");
        return;
    }

    Field* fields = result->Fetch();
    uint32 linkedAccountId = fields[0].Get<uint32>();

    PlayerbotsDatabasePreparedStatement* keyStmt = PlayerbotsDatabase.GetPreparedStatement(PLAYERBOTS_SEL_ACCOUNT_KEY);
    keyStmt->SetData(0, linkedAccountId);
    PreparedQueryResult keyResult = PlayerbotsDatabase.Query(keyStmt);
    if (!keyResult)
    {
        ChatHandler(player->GetSession()).PSendSysMessage("Invalid security key.");
        return;
    }

    // Hash the provided key
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256((unsigned char*)key.c_str(), key.size(), hash);

    // Convert the hash to a hexadecimal string
    std::ostringstream hashedKey;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i)
        hashedKey << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];

    // Compare the hashed key with the stored hashed key
    std::string storedKey = keyResult->Fetch()->Get<std::string>();
    if (hashedKey.str() != storedKey)
    {
        ChatHandler(player->GetSession()).PSendSysMessage("Invalid security key.");
        return;
    }

    uint32 accountId = player->GetSession()->GetAccountId();
    PlayerbotsDatabasePreparedStatement* linkStmt = PlayerbotsDatabase.GetPreparedStatement(PLAYERBOTS_INS_ACCOUNT_LINK);
    linkStmt->SetData(0, accountId);
    linkStmt->SetData(1, linkedAccountId);
    PlayerbotsDatabase.Execute(linkStmt);

    linkStmt = PlayerbotsDatabase.GetPreparedStatement(PLAYERBOTS_INS_ACCOUNT_LINK);
    linkStmt->SetData(0, linkedAccountId);
    linkStmt->SetData(1, accountId);
    PlayerbotsDatabase.Execute(linkStmt);

    ChatHandler(player->GetSession()).PSendSysMessage("Account linked successfully.");
}

void PlayerbotMgr::HandleViewLinkedAccountsCommand(Player* player)
{
    uint32 accountId = player->GetSession()->GetAccountId();
    PlayerbotsDatabasePreparedStatement* stmt = PlayerbotsDatabase.GetPreparedStatement(PLAYERBOTS_SEL_ACCOUNT_LINKS_BY_ACCOUNT);
    stmt->SetData(0, accountId);
    PreparedQueryResult result = PlayerbotsDatabase.Query(stmt);

    if (!result)
    {
        ChatHandler(player->GetSession()).PSendSysMessage("No linked accounts.");
        return;
    }

    ChatHandler(player->GetSession()).PSendSysMessage("Linked accounts:");
    do
    {
        Field* fields = result->Fetch();
        uint32 linkedAccountId = fields[0].Get<uint32>();

        QueryResult accountResult = LoginDatabase.Query("SELECT username FROM account WHERE id = {}", linkedAccountId);
        if (accountResult)
        {
            Field* accountFields = accountResult->Fetch();
            std::string username = accountFields[0].Get<std::string>();
            ChatHandler(player->GetSession()).PSendSysMessage("- {}", username.c_str());
        }
        else
        {
            ChatHandler(player->GetSession()).PSendSysMessage("- Unknown account");
        }
    } while (result->NextRow());
}

void PlayerbotMgr::HandleUnlinkAccountCommand(Player* player, std::string const& accountName)
{
    QueryResult result = LoginDatabase.Query("SELECT id FROM account WHERE username = '{}'", accountName);
    if (!result)
    {
        ChatHandler(player->GetSession()).PSendSysMessage("Account not found.");
        return;
    }

    Field* fields = result->Fetch();
    uint32 linkedAccountId = fields[0].Get<uint32>();
    uint32 accountId = player->GetSession()->GetAccountId();

    PlayerbotsDatabasePreparedStatement* stmt = PlayerbotsDatabase.GetPreparedStatement(PLAYERBOTS_DEL_ACCOUNT_LINK);
    stmt->SetData(0, accountId);
    stmt->SetData(1, linkedAccountId);
    stmt->SetData(2, linkedAccountId);
    stmt->SetData(3, accountId);
    PlayerbotsDatabase.Execute(stmt);

    ChatHandler(player->GetSession()).PSendSysMessage("Account unlinked successfully.");
}
