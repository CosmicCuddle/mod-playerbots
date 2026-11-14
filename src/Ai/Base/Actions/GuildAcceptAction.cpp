/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "GuildAcceptAction.h"
#include "Event.h"
#include "GuildPackets.h"
#include "PlayerbotIdentityGeneration.h"
#include "PlayerbotOperations.h"
#include "PlayerbotSecurity.h"
#include "PlayerbotTextMgr.h"
#include "PlayerbotWorldThreadProcessor.h"
#include "Playerbots.h"

bool GuildAcceptAction::Execute(Event event)
{
    WorldPacket p(event.getPacket());
    p.rpos(0);
    Player* inviter = nullptr;
    std::string Invitedname;
    p >> Invitedname;

    if (normalizePlayerName(Invitedname))
        inviter = ObjectAccessor::FindPlayerByName(Invitedname.c_str());

    if (!inviter)
        return false;

    bool accept = true;
    uint32 guildId = inviter->GetGuildId();
    if (!guildId)
    {
        botAI->TellError(PlayerbotTextMgr::instance().GetBotTextOrDefault(
            "guild_accept_inviter_not_in_guild", "You are not in a guild!", {}));
        accept = false;
    }
    else if (bot->GetGuildId())
    {
        botAI->TellError(PlayerbotTextMgr::instance().GetBotTextOrDefault(
            "guild_accept_already_in_guild", "Sorry, I am in a guild already", {}));
        accept = false;
    }
    else if (!botAI->GetSecurity()->CheckLevelFor(PLAYERBOT_SECURITY_INVITE, false, inviter, true))
    {
        botAI->TellError(PlayerbotTextMgr::instance().GetBotTextOrDefault(
            "guild_accept_declined", "Sorry, I don't want to join your guild :(", {}));
        accept = false;
    }

    if (accept)
    {
        uint32 invitedGuildId = bot->GetGuildIdInvited();
        std::uint64_t botEpoch = PlayerbotIdentityGeneration::Instance().CurrentLogin(bot->GetGUID());
        if (!invitedGuildId || invitedGuildId != guildId || !botEpoch)
            return false;

        auto operation = std::make_unique<GuildAcceptOperation>(bot->GetGUID(), invitedGuildId, botEpoch);
        return PlayerbotWorldThreadProcessor::instance().QueueOperation(std::move(operation));
    }
    else
    {
        WorldPackets::Guild::GuildDeclineInvitation data = WorldPacket(CMSG_GUILD_DECLINE);
        bot->GetSession()->HandleGuildDeclineOpcode(data);
    }

    return true;
}
