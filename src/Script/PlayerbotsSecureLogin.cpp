/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "ObjectAccessor.h"
#include "Opcodes.h"
#include "Player.h"
#include "Playerbots.h"
#include "ScriptMgr.h"
#include "WorldSession.h"

class PlayerbotsSecureLoginServerScript : public ServerScript
{
public:
    PlayerbotsSecureLoginServerScript()
        : ServerScript("PlayerbotsSecureLoginServerScript", { SERVERHOOK_CAN_PACKET_RECEIVE }) {}

    bool CanPacketReceive(WorldSession* session, WorldPacket const& packet) override
    {
        if (packet.GetOpcode() != CMSG_PLAYER_LOGIN || !session)
            return true;

        WorldPacket pkt(packet);
        ObjectGuid loginGuid;
        pkt >> loginGuid;
        if (!loginGuid.IsPlayer())
            return true; // Core performs its normal packet validation.

        Player* existing = ObjectAccessor::FindConnectedPlayer(loginGuid);
        WorldSession* existingSession = existing ? existing->GetSession() : nullptr;
        if (!existing)
            return true;
        if (!existingSession)
        {
            session->SendCharLoginFailed(LoginFailureReason::DuplicateCharacter);
            return false; // registered but unknown ownership is not safe to replace
        }
        if (!existingSession->IsHeadless())
            return true; // ordinary client login/reconnect is unchanged

        // This hook runs BEFORE Core's login opcode admission checks. Verify
        // the enumerated character and the actual account before any effect.
        bool completed = false;
        if (session->CanPB59RetireBotForClientLogin(loginGuid) &&
            session->GetAccountId() == existingSession->GetAccountId())
        {
            try
            {
                completed = PlayerbotHolder::RetireBotForClientLogin(existing);
            }
            catch (...)
            {
                // An uncertain retirement is not permission to replace it.
            }
        }
        if (!completed)
        {
            session->SendCharLoginFailed(LoginFailureReason::DuplicateCharacter);
            return false;
        }
        return true;
    }
};

void AddPlayerbotsSecureLoginScripts()
{
    new PlayerbotsSecureLoginServerScript();
}
