/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "AcceptInvitationAction.h"
#include "Event.h"
#include "PlayerbotIdentityGeneration.h"
#include "PlayerbotOperations.h"
#include "PlayerbotWorldThreadProcessor.h"

bool AcceptInvitationAction::Execute(Event /*event*/)
{
    Group* grp = bot->GetGroupInvite();
    if (!grp)
        return false;
    PlayerbotIdentityGeneration& epochs = PlayerbotIdentityGeneration::Instance();
    std::uint64_t botEpoch = epochs.CurrentLogin(bot->GetGUID());
    std::uint64_t inviterEpoch = epochs.CurrentLogin(grp->GetLeaderGUID());
    std::uint64_t invitationGeneration = epochs.CurrentInvitation(bot->GetGUID());
    if (!botEpoch || !inviterEpoch || !invitationGeneration)
        return false;

    auto operation = std::make_unique<GroupAcceptOperation>(bot->GetGUID(), grp->GetLeaderGUID(), grp->GetGUID(),
                                                             botEpoch, inviterEpoch, invitationGeneration);
    return PlayerbotWorldThreadProcessor::instance().QueueOperation(std::move(operation));
}
