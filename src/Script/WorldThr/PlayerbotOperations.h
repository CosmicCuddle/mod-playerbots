/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_PLAYERBOTOPERATIONS_H
#define PLAYERBOTS_PLAYERBOTOPERATIONS_H

#include "ArenaTeamMgr.h"
#include "Group.h"
#include "GroupMgr.h"
#include "PlayerbotIdentityGeneration.h"
#include "GuildMgr.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotMgr.h"
#include "PlayerbotOperation.h"
#include "PlayerbotRepository.h"
#include "PlayerbotSecurity.h"
#include "PlayerbotTextMgr.h"
#include "Playerbots.h"
#include "RandomPlayerbotFactory.h"
#include "RandomPlayerbotMgr.h"
#include "ScriptMgr.h"
#include "UseMeetingStoneAction.h"
#include "WorldSession.h"
#include "WorldSessionMgr.h"
#include "WorldPacket.h"

class GroupAcceptOperation : public PlayerbotOperation
{
public:
    GroupAcceptOperation(ObjectGuid botGuid, ObjectGuid inviterGuid, ObjectGuid inviteGroupGuid,
                         std::uint64_t botEpoch, std::uint64_t inviterEpoch, std::uint64_t invitationGeneration)
        : m_botGuid(botGuid), m_inviterGuid(inviterGuid), m_inviteGroupGuid(inviteGroupGuid),
          m_botEpoch(botEpoch), m_inviterEpoch(inviterEpoch), m_invitationGeneration(invitationGeneration)
    {
    }

    bool Execute() override
    {
        if (!EpochsMatch())
            return false;
        Player* bot = ObjectAccessor::FindPlayer(m_botGuid);
        Player* inviter = ObjectAccessor::FindPlayer(m_inviterGuid);
        if (!bot || !inviter || !bot->GetSession() || bot->GetSession()->GetPlayer() != bot)
            return false;

        Group* invite = bot->GetGroupInvite();
        if (!invite || invite->GetGUID() != m_inviteGroupGuid || invite->GetLeaderGUID() != m_inviterGuid)
            return false;

        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        if (!botAI)
            return false;

        if (!botAI->GetSecurity()->CheckLevelFor(PLAYERBOT_SECURITY_INVITE, false, inviter))
        {
            WorldPacket data(SMSG_GROUP_DECLINE, 10);
            data << bot->GetName();
            inviter->SendDirectMessage(&data);
            bot->UninviteFromGroup();
            return false;
        }

        if (bot->isAFK() && !IsSelfBot(bot))
            bot->ToggleAFK();

        WorldPacket packet;
        uint32 rolesMask = 0;
        packet << rolesMask;
        bot->GetSession()->HandleGroupAcceptOpcode(packet);

        Group* group = bot->GetGroup();
        if (!group || !group->IsMember(m_inviterGuid))
            return false;

        if (sRandomPlayerbotMgr.IsRandomBot(bot))
            botAI->SetMaster(inviter);

        botAI->ResetStrategies();
        botAI->ChangeStrategy("+follow,-lfg,-bg", BOT_STATE_NON_COMBAT);
        botAI->Reset();
        botAI->TellMaster(PlayerbotTextMgr::instance().GetBotTextOrDefault("hello", "Hello", {}));

        if (sPlayerbotAIConfig.SummonWhenGroup && bot->GetDistance(inviter) > sPlayerbotAIConfig.SightDistance)
        {
            SummonAction summonAction(botAI, "group summon");
            summonAction.Teleport(inviter, bot, true);
        }
        return true;
    }

    ObjectGuid GetBotGuid() const override { return m_botGuid; }
    uint32 GetPriority() const override { return 50; }
    std::string GetName() const override { return "GroupAccept"; }

    bool IsValid() const override
    {
        if (!EpochsMatch())
            return false;
        Player* bot = ObjectAccessor::FindPlayer(m_botGuid);
        Group* invite = bot ? bot->GetGroupInvite() : nullptr;
        return invite && invite->GetGUID() == m_inviteGroupGuid && invite->GetLeaderGUID() == m_inviterGuid;
    }

private:
    bool EpochsMatch() const
    {
        PlayerbotIdentityGeneration& epochs = PlayerbotIdentityGeneration::Instance();
        return m_botEpoch && m_inviterEpoch && m_invitationGeneration &&
               epochs.CurrentLogin(m_botGuid) == m_botEpoch &&
               epochs.CurrentLogin(m_inviterGuid) == m_inviterEpoch &&
               epochs.CurrentInvitation(m_botGuid) == m_invitationGeneration;
    }

    ObjectGuid m_botGuid;
    ObjectGuid m_inviterGuid;
    ObjectGuid m_inviteGroupGuid;
    std::uint64_t m_botEpoch;
    std::uint64_t m_inviterEpoch;
    std::uint64_t m_invitationGeneration;
};

// Group invite operation
class GroupInviteOperation : public PlayerbotOperation
{
public:
    GroupInviteOperation(ObjectGuid botGuid, ObjectGuid targetGuid)
        : m_botGuid(botGuid), m_targetGuid(targetGuid),
          m_botEpoch(PlayerbotIdentityGeneration::Instance().CurrentLogin(botGuid)),
          m_targetEpoch(PlayerbotIdentityGeneration::Instance().CurrentLogin(targetGuid))
    {
    }

    bool Execute() override
    {
        if (!EpochsMatch())
            return false;
        Player* bot = ObjectAccessor::FindPlayer(m_botGuid);
        Player* target = ObjectAccessor::FindPlayer(m_targetGuid);

        if (!bot || !target)
        {
            LOG_DEBUG("playerbots", "GroupInviteOperation: Bot or target not found");
            return false;
        }

        // Check if target is already in a group
        if (target->GetGroup())
        {
            LOG_DEBUG("playerbots", "GroupInviteOperation: Target {} is already in a group", target->GetName());
            return false;
        }

        std::string targetName = target->GetName();
        if (!sScriptMgr->OnPlayerCanGroupInvite(bot, targetName))
            return false;

        Group* group = bot->GetGroup();

        // Create group if bot doesn't have one
        if (!group)
        {
            group = new Group;
            if (!group->Create(bot))
            {
                delete group;
                LOG_ERROR("playerbots", "GroupInviteOperation: Failed to create group for bot {}", bot->GetName());
                return false;
            }
            sGroupMgr->AddGroup(group);
            LOG_DEBUG("playerbots", "GroupInviteOperation: Created new group for bot {}", bot->GetName());
        }

        // Convert to raid if needed (more than 5 members)
        if (!group->isRaidGroup() && group->GetMembersCount() >= 5)
        {
            group->ConvertToRaid();
            LOG_DEBUG("playerbots", "GroupInviteOperation: Converted group to raid");
        }

        // Add member to group
        if (group->AddMember(target))
        {
            LOG_DEBUG("playerbots", "GroupInviteOperation: Successfully added {} to group", target->GetName());
            if (sPlayerbotAIConfig.SummonWhenGroup && target->GetDistance(bot) > sPlayerbotAIConfig.SightDistance)
            {
                PlayerbotAI* targetAI = sPlayerbotsMgr.GetPlayerbotAI(target);
                if (targetAI)
                {
                    SummonAction summonAction(targetAI, "group summon");
                    summonAction.Teleport(bot, target, true);
                }
            }
            return true;
        }
        else
        {
            LOG_ERROR("playerbots", "GroupInviteOperation: Failed to add {} to group", target->GetName());
            return false;
        }
    }

    ObjectGuid GetBotGuid() const override { return m_botGuid; }

    uint32 GetPriority() const override { return 50; }  // High priority (player-facing)

    std::string GetName() const override { return "GroupInvite"; }

    bool IsValid() const override
    {
        if (!EpochsMatch())
            return false;
        // Check if bot still exists and is online
        Player* bot = ObjectAccessor::FindPlayer(m_botGuid);
        Player* target = ObjectAccessor::FindPlayer(m_targetGuid);
        return bot && target;
    }

private:
    bool EpochsMatch() const
    {
        PlayerbotIdentityGeneration& epochs = PlayerbotIdentityGeneration::Instance();
        return m_botEpoch && m_targetEpoch && epochs.CurrentLogin(m_botGuid) == m_botEpoch &&
               epochs.CurrentLogin(m_targetGuid) == m_targetEpoch;
    }

    ObjectGuid m_botGuid;
    ObjectGuid m_targetGuid;
    std::uint64_t m_botEpoch;
    std::uint64_t m_targetEpoch;
};

// Remove member from group
class GroupRemoveMemberOperation : public PlayerbotOperation
{
public:
    GroupRemoveMemberOperation(ObjectGuid botGuid, ObjectGuid targetGuid)
        : m_botGuid(botGuid), m_targetGuid(targetGuid)
    {
    }

    bool Execute() override
    {
        Player* bot = ObjectAccessor::FindPlayer(m_botGuid);
        Player* target = ObjectAccessor::FindPlayer(m_targetGuid);

        if (!bot || !target)
            return false;

        Group* group = bot->GetGroup();
        if (!group)
        {
            LOG_DEBUG("playerbots", "GroupRemoveMemberOperation: Bot is not in a group");
            return false;
        }

        if (!group->IsMember(target->GetGUID()))
        {
            LOG_DEBUG("playerbots", "GroupRemoveMemberOperation: Target is not in bot's group");
            return false;
        }

        group->RemoveMember(target->GetGUID());
        LOG_DEBUG("playerbots", "GroupRemoveMemberOperation: Removed {} from group", target->GetName());
        return true;
    }

    ObjectGuid GetBotGuid() const override { return m_botGuid; }

    uint32 GetPriority() const override { return 50; }

    std::string GetName() const override { return "GroupRemoveMember"; }

    bool IsValid() const override
    {
        Player* bot = ObjectAccessor::FindPlayer(m_botGuid);
        return bot != nullptr;
    }

private:
    ObjectGuid m_botGuid;
    ObjectGuid m_targetGuid;
};

// Convert group to raid
class GroupConvertToRaidOperation : public PlayerbotOperation
{
public:
    GroupConvertToRaidOperation(ObjectGuid botGuid) : m_botGuid(botGuid) {}

    bool Execute() override
    {
        Player* bot = ObjectAccessor::FindPlayer(m_botGuid);
        if (!bot)
            return false;

        Group* group = bot->GetGroup();
        if (!group)
        {
            LOG_DEBUG("playerbots", "GroupConvertToRaidOperation: Bot is not in a group");
            return false;
        }

        if (group->isRaidGroup())
        {
            LOG_DEBUG("playerbots", "GroupConvertToRaidOperation: Group is already a raid");
            return true;  // Success - already in desired state
        }

        group->ConvertToRaid();
        LOG_DEBUG("playerbots", "GroupConvertToRaidOperation: Converted group to raid");
        return true;
    }

    ObjectGuid GetBotGuid() const override { return m_botGuid; }

    uint32 GetPriority() const override { return 50; }

    std::string GetName() const override { return "GroupConvertToRaid"; }

    bool IsValid() const override
    {
        Player* bot = ObjectAccessor::FindPlayer(m_botGuid);
        return bot != nullptr;
    }

private:
    ObjectGuid m_botGuid;
};

// Set group leader
class GroupSetLeaderOperation : public PlayerbotOperation
{
public:
    GroupSetLeaderOperation(ObjectGuid botGuid, ObjectGuid newLeaderGuid)
        : m_botGuid(botGuid), m_newLeaderGuid(newLeaderGuid)
    {
    }

    bool Execute() override
    {
        Player* bot = ObjectAccessor::FindPlayer(m_botGuid);
        Player* newLeader = ObjectAccessor::FindPlayer(m_newLeaderGuid);

        if (!bot || !newLeader)
            return false;

        Group* group = bot->GetGroup();
        if (!group)
        {
            LOG_DEBUG("playerbots", "GroupSetLeaderOperation: Bot is not in a group");
            return false;
        }

        if (!group->IsMember(newLeader->GetGUID()))
        {
            LOG_DEBUG("playerbots", "GroupSetLeaderOperation: New leader is not in the group");
            return false;
        }

        group->ChangeLeader(newLeader->GetGUID());
        group->SendUpdate();
        LOG_DEBUG("playerbots", "GroupSetLeaderOperation: Changed leader to {}", newLeader->GetName());
        return true;
    }

    ObjectGuid GetBotGuid() const override { return m_botGuid; }

    uint32 GetPriority() const override { return 50; }

    std::string GetName() const override { return "GroupSetLeader"; }

    bool IsValid() const override
    {
        Player* bot = ObjectAccessor::FindPlayer(m_botGuid);
        Player* newLeader = ObjectAccessor::FindPlayer(m_newLeaderGuid);
        return bot && newLeader;
    }

private:
    ObjectGuid m_botGuid;
    ObjectGuid m_newLeaderGuid;
};

// Form arena group
class ArenaGroupFormationOperation : public PlayerbotOperation
{
public:
    ArenaGroupFormationOperation(ObjectGuid leaderGuid, std::vector<ObjectGuid> memberGuids,
                                 uint32 requiredSize, uint32 arenaTeamId, std::string arenaTeamName)
        : m_leaderGuid(leaderGuid), m_memberGuids(memberGuids),
          m_requiredSize(requiredSize), m_arenaTeamId(arenaTeamId), m_arenaTeamName(arenaTeamName)
    {
    }

    bool Execute() override
    {
        Player* leader = ObjectAccessor::FindPlayer(m_leaderGuid);
        if (!leader)
        {
            LOG_ERROR("playerbots", "ArenaGroupFormationOperation: Leader not found");
            return false;
        }

        // Step 1: Remove all members from their existing groups
        for (ObjectGuid const& memberGuid : m_memberGuids)
        {
            Player* member = ObjectAccessor::FindPlayer(memberGuid);
            if (!member)
                continue;

            Group* memberGroup = member->GetGroup();
            if (memberGroup)
            {
                memberGroup->RemoveMember(memberGuid);
                LOG_DEBUG("playerbots", "ArenaGroupFormationOperation: Removed {} from their existing group",
                         member->GetName());
            }
        }

        // Step 2: Disband leader's existing group
        Group* leaderGroup = leader->GetGroup();
        if (leaderGroup)
        {
            leaderGroup->Disband(true);
            LOG_DEBUG("playerbots", "ArenaGroupFormationOperation: Disbanded leader's existing group");
        }

        // Step 3: Create new group with leader
        Group* newGroup = new Group();
        if (!newGroup->Create(leader))
        {
            delete newGroup;
            LOG_ERROR("playerbots", "ArenaGroupFormationOperation: Failed to create arena group for leader {}",
                     leader->GetName());
            return false;
        }

        sGroupMgr->AddGroup(newGroup);
        LOG_DEBUG("playerbots", "ArenaGroupFormationOperation: Created new arena group with leader {}",
                 leader->GetName());

        // Step 4: Add members to the new group
        uint32 addedMembers = 0;
        for (ObjectGuid const& memberGuid : m_memberGuids)
        {
            Player* member = ObjectAccessor::FindPlayer(memberGuid);
            if (!member)
            {
                LOG_DEBUG("playerbots", "ArenaGroupFormationOperation: Member {} not found, skipping",
                         memberGuid.ToString());
                continue;
            }

            if (member->GetLevel() < 70)
            {
                LOG_DEBUG("playerbots", "ArenaGroupFormationOperation: Member {} is below level 70, skipping",
                         member->GetName());
                continue;
            }

            if (newGroup->AddMember(member))
            {
                addedMembers++;
                LOG_DEBUG("playerbots", "ArenaGroupFormationOperation: Added {} to arena group",
                         member->GetName());
            }
            else
                LOG_ERROR("playerbots", "ArenaGroupFormationOperation: Failed to add {} to arena group",
                         member->GetName());
        }

        if (addedMembers == 0)
        {
            LOG_ERROR("playerbots", "ArenaGroupFormationOperation: No members were added to the arena group");
            newGroup->Disband();
            return false;
        }

        // Step 5: Teleport members to leader and reset AI
        for (ObjectGuid const& memberGuid : m_memberGuids)
        {
            Player* member = ObjectAccessor::FindPlayer(memberGuid);
            if (!member || !newGroup->IsMember(memberGuid))
                continue;

            PlayerbotAI* memberBotAI = PlayerbotsMgr::instance().GetPlayerbotAI(member);
            if (memberBotAI)
                memberBotAI->Reset();

            member->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_TELEPORTED | AURA_INTERRUPT_FLAG_CHANGE_MAP);
            member->TeleportTo(leader->GetMapId(), leader->GetPositionX(), leader->GetPositionY(),
                              leader->GetPositionZ(), 0);

            LOG_DEBUG("playerbots", "ArenaGroupFormationOperation: Teleported {} to leader", member->GetName());
        }

        // Check if we have enough members
        if (newGroup->GetMembersCount() < m_requiredSize)
        {
            LOG_INFO("playerbots", "Team #{} <{}> Group is not ready for match (not enough members: {}/{})",
                    m_arenaTeamId, m_arenaTeamName, newGroup->GetMembersCount(), m_requiredSize);
            newGroup->Disband();
            return false;
        }

        LOG_INFO("playerbots", "Team #{} <{}> Group is ready for match with {} members",
                m_arenaTeamId, m_arenaTeamName, newGroup->GetMembersCount());
        return true;
    }

    ObjectGuid GetBotGuid() const override { return m_leaderGuid; }

    uint32 GetPriority() const override { return 60; }  // Very high priority (arena/BG operations)

    std::string GetName() const override { return "ArenaGroupFormation"; }

    bool IsValid() const override
    {
        Player* leader = ObjectAccessor::FindPlayer(m_leaderGuid);
        return leader != nullptr;
    }

private:
    ObjectGuid m_leaderGuid;
    std::vector<ObjectGuid> m_memberGuids;
    uint32 m_requiredSize;
    uint32 m_arenaTeamId;
    std::string m_arenaTeamName;
};

class BotCommandLogoutOperation : public PlayerbotOperation
{
public:
    BotCommandLogoutOperation(ObjectGuid botGuid, ObjectGuid masterGuid, std::uint64_t botEpoch,
                              std::uint64_t masterEpoch)
        : m_botGuid(botGuid), m_masterGuid(masterGuid), m_botEpoch(botEpoch), m_masterEpoch(masterEpoch)
    {
    }

    bool Execute() override
    {
        if (!EpochsMatch())
            return false;

        Player* bot = ObjectAccessor::FindPlayer(m_botGuid);
        Player* master = ObjectAccessor::FindPlayer(m_masterGuid);
        WorldSession* session = bot ? bot->GetSession() : nullptr;
        if (!bot || !master || !session || session->GetPlayer() != bot || session->IsLoggingOut())
            return false;

        PlayerbotMgr* holder = PlayerbotsMgr::instance().GetPlayerbotMgr(master);
        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        if (!holder || holder->GetPlayerBot(m_botGuid) != bot || !botAI || botAI->GetMaster() != master)
            return false;

        holder->LogoutPlayerBot(m_botGuid);
        return true;
    }

    ObjectGuid GetBotGuid() const override { return m_botGuid; }
    std::string GetName() const override { return "BotCommandLogout"; }
    bool IsValid() const override { return EpochsMatch(); }

private:
    bool EpochsMatch() const
    {
        PlayerbotIdentityGeneration& epochs = PlayerbotIdentityGeneration::Instance();
        return m_botEpoch && m_masterEpoch && epochs.CurrentLogin(m_botGuid) == m_botEpoch &&
               epochs.CurrentLogin(m_masterGuid) == m_masterEpoch;
    }

    ObjectGuid m_botGuid;
    ObjectGuid m_masterGuid;
    std::uint64_t m_botEpoch;
    std::uint64_t m_masterEpoch;
};

// Bot logout group cleanup operation
class BotLogoutGroupCleanupOperation : public PlayerbotOperation
{
public:
    BotLogoutGroupCleanupOperation(ObjectGuid botGuid) : m_botGuid(botGuid) {}

    bool Execute() override
    {
        Player* bot = ObjectAccessor::FindPlayer(m_botGuid);
        if (!bot)
            return false;

        PlayerbotAI* botAI = PlayerbotsMgr::instance().GetPlayerbotAI(bot);
        if (!botAI)
            return false;

        Group* group = bot->GetGroup();
        if (group && !bot->InBattleground() && !bot->InBattlegroundQueue() && IsRealPlayer(botAI->GetMaster()))
            PlayerbotRepository::instance().Save(botAI);

        return true;
    }

    ObjectGuid GetBotGuid() const override { return m_botGuid; }
    uint32 GetPriority() const override { return 70; }
    std::string GetName() const override { return "BotLogoutGroupCleanup"; }

    bool IsValid() const override
    {
        Player* bot = ObjectAccessor::FindPlayer(m_botGuid);
        return bot != nullptr;
    }

private:
    ObjectGuid m_botGuid;
};

// Add player bot operation (for logging in bots from map threads)
class AddPlayerBotOperation : public PlayerbotOperation
{
public:
    AddPlayerBotOperation(ObjectGuid botGuid, uint32 masterAccountId)
        : m_botGuid(botGuid), m_masterAccountId(masterAccountId)
    {
    }

    bool Execute() override
    {
        sRandomPlayerbotMgr.AddPlayerBot(m_botGuid, m_masterAccountId);
        return true;
    }

    ObjectGuid GetBotGuid() const override { return m_botGuid; }

    uint32 GetPriority() const override { return 50; }  // High priority

    std::string GetName() const override { return "AddPlayerBot"; }

    bool IsValid() const override
    {
        return !ObjectAccessor::FindConnectedPlayer(m_botGuid);
    }

private:
    ObjectGuid m_botGuid;
    uint32 m_masterAccountId;
};

class OnBotLoginOperation : public PlayerbotOperation
{
public:
    OnBotLoginOperation(ObjectGuid botGuid, uint32 masterAccountId)
        : m_botGuid(botGuid), m_masterAccountId(masterAccountId)
    {
    }

    bool Execute() override
    {
        // find and verify bot still exists
        Player* bot = ObjectAccessor::FindConnectedPlayer(m_botGuid);
        if (!bot)
            return false;

        PlayerbotHolder* holder = &RandomPlayerbotMgr::instance();
        if (m_masterAccountId)
        {
            WorldSession* masterSession = sWorldSessionMgr->FindSession(m_masterAccountId);
            Player* masterPlayer = masterSession ? masterSession->GetPlayer() : nullptr;
            if (masterPlayer)
                holder = PlayerbotsMgr::instance().GetPlayerbotMgr(masterPlayer);
        }

        if (!holder)
            return false;

        holder->OnBotLogin(bot);
        return true;
    }

    ObjectGuid GetBotGuid() const override { return m_botGuid; }
    uint32 GetPriority() const override { return 100; }
    std::string GetName() const override { return "OnBotLogin"; }

    bool IsValid() const override { return ObjectAccessor::FindConnectedPlayer(m_botGuid) != nullptr; }

private:
    ObjectGuid m_botGuid;
    uint32 m_masterAccountId = 0;
};

class ArenaTeamAssignOperation : public PlayerbotOperation
{
public:
    explicit ArenaTeamAssignOperation(ObjectGuid botGuid) : m_botGuid(botGuid) {}

    bool Execute() override
    {
        Player* bot = ObjectAccessor::FindPlayer(m_botGuid);
        if (!bot)
            return false;

        RandomPlayerbotFactory::AssignBotToArenaTeamInternal(bot);
        return true;
    }

    ObjectGuid GetBotGuid() const override { return m_botGuid; }

    std::string GetName() const override { return "ArenaTeamAssign"; }

    bool IsValid() const override { return ObjectAccessor::FindPlayer(m_botGuid) != nullptr; }

private:
    ObjectGuid m_botGuid;
};

// Drop a bot that has left its captain's arena bracket: the team can't queue with it
class ArenaTeamDropMemberOperation : public PlayerbotOperation
{
public:
    ArenaTeamDropMemberOperation(uint32 arenaTeamId, ObjectGuid botGuid)
        : m_arenaTeamId(arenaTeamId), m_botGuid(botGuid)
    {
    }

    bool Execute() override
    {
        ArenaTeam* team = sArenaTeamMgr->GetArenaTeamById(m_arenaTeamId);
        Player* bot = ObjectAccessor::FindPlayer(m_botGuid);
        Player* captain = team ? ObjectAccessor::FindPlayer(team->GetCaptain()) : nullptr;
        if (!bot || !captain || captain == bot || !team->IsMember(m_botGuid))
            return false;

        // levels can change before this runs
        if (RandomPlayerbotFactory::SameArenaBracket(bot->GetLevel(), captain->GetLevel()))
            return false;

        team->DelMember(m_botGuid, true);
        RandomPlayerbotFactory::AssignBotToArenaTeam(bot);
        return true;
    }

    ObjectGuid GetBotGuid() const override { return m_botGuid; }

    std::string GetName() const override { return "ArenaTeamDropMember"; }

    bool IsValid() const override { return ObjectAccessor::FindPlayer(m_botGuid) != nullptr; }

private:
    uint32 m_arenaTeamId;
    ObjectGuid m_botGuid;
};

#endif
