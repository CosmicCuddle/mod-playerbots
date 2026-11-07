#!/usr/bin/env python3
"""Source-only evidence: RandomBot reservation, bot materialisation and retirement.

These tests pin actual *existing* Playerbots code paths to prevent assuming
"currentBots" membership proves an already loaded, headless, map-safe actor.
They do NOT implement or enable F14 visibility, stage8 or reset preservation.
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
MGR = ROOT / "src/Bot/RandomPlayerbotMgr.cpp"
HEADER = ROOT / "src/Bot/RandomPlayerbotMgr.h"
HOLDER = ROOT / "src/Bot/PlayerbotMgr.cpp"
FACTORY = ROOT / "src/Bot/Factory/PlayerbotFactory.cpp"
# CI-only pinned read-only owner repositories. No customer/host data.
CORE_CPP = ROOT / "owner-core/src/server/game/Server/WorldSession.cpp"
CORE_H = ROOT / "owner-core/src/server/game/Server/WorldSession.h"
IP_PLAYER = ROOT / "owner-ip/src/IndividualProgressionPlayer.cpp"

def between(source, start, end):
    if source.count(start) != 1 or source.count(end) != 1:
        raise AssertionError("pinned source method delimiter changed: " + start)
    after = source.split(start, 1)[1]
    if end not in after:
        raise AssertionError("pinned method order changed: " + start)
    return after.split(end, 1)[0]

class LifecycleSourceAudit(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.manager = MGR.read_text(encoding="utf-8")
        cls.manager_header = HEADER.read_text(encoding="utf-8")
        cls.holder = HOLDER.read_text(encoding="utf-8")
        cls.factory = FACTORY.read_text(encoding="utf-8")
        cls.core_cpp = CORE_CPP.read_text(encoding="utf-8")
        cls.core_h = CORE_H.read_text(encoding="utf-8")
        cls.ip_player = IP_PLAYER.read_text(encoding="utf-8")

    def test_01_selected_login_reservation_is_not_live_session(self):
        """The selected bot gets currentBots membership BEFORE actual login."""
        selection = between(self.manager, "auto tryLoginBot = [&](CharacterInfo const& charInfo) -> bool",
                            "// PHASE 1: Log-in Alliance bots")
        self.assertLess(selection.index('SetEventValue(charInfo.guid, "add", 1, add_time);'),
                        selection.index("currentBots.insert(charInfo.guid);"))
        self.assertNotIn("AddPlayerBot(", selection)
        self.assertNotIn("OnBotLogin(", selection)
        scheduler = between(self.manager, "bool RandomPlayerbotMgr::ProcessBot(uint32 bot)",
                            "bool RandomPlayerbotMgr::ProcessBot(Player* bot)")
        self.assertIn("AddPlayerBot(botGUID, 0);", scheduler)
        self.assertLess(self.manager.index("currentBots.insert(charInfo.guid);"),
                        self.manager.index("AddPlayerBot(botGUID, 0);"))

    def test_02_bootstrap_database_membership_is_not_a_player(self):
        """GetBots can rehydrate membership from events without materialising Player."""
        bootstrap = between(self.manager, "void RandomPlayerbotMgr::GetBots()",
                            "std::vector<uint32> RandomPlayerbotMgr::GetBgBots(")
        self.assertIn("PLAYERBOTS_SEL_RANDOM_BOTS_BY_OWNER_AND_EVENT", bootstrap)
        self.assertIn("currentBots.insert(bot);", bootstrap)
        self.assertNotIn("AddPlayerBot(", bootstrap)
        self.assertNotIn("OnBotLogin(", bootstrap)

    def test_03_random_account_and_active_set_still_required(self):
        player = between(self.manager, "bool RandomPlayerbotMgr::IsRandomBot(Player* bot)",
                         "bool RandomPlayerbotMgr::IsRandomBot(ObjectGuid::LowType bot)")
        guid = between(self.manager, "bool RandomPlayerbotMgr::IsRandomBot(ObjectGuid::LowType bot)",
                       "bool RandomPlayerbotMgr::IsAddclassBot(Player* bot)")
        self.assertIn("if (IsSelfBot(bot))", player)
        self.assertIn("return IsRandomBot(bot->GetGUID().GetCounter());", player)
        self.assertIn("sPlayerbotAIConfig.IsInRandomAccountList(", guid)
        self.assertIn("return currentBots.contains(bot);", guid)
        self.assertIn("std::unordered_set<uint32> currentBots;", self.manager_header)
        # No synchronisation proof in this function; not safe to call on Core map
        # workers merely because it exists. Adding a lock later requires review,
        # not automatic permission to suppress a creature.
        self.assertNotIn("std::lock_guard", guid)
        self.assertNotIn("std::shared_lock", guid)

    def test_04_logouts_erase_membership_at_different_points(self):
        scheduler = between(self.manager, "bool RandomPlayerbotMgr::ProcessBot(uint32 bot)",
                            "bool RandomPlayerbotMgr::ProcessBot(Player* bot)")
        self.assertIn("currentBots.erase(bot);\n\n            if (player)\n                LogoutPlayerBot(botGUID);",
                      scheduler)
        self.assertIn("LogoutPlayerBot(botGUID);\n        currentBots.erase(bot);", scheduler)
        # Logout pipeline ordering can vary. Positive identity must not depend
        # on either order, or derive generation merely from GUID.
        errors = between(self.manager, "void RandomPlayerbotMgr::OnPlayerLoginError(uint32 bot)",
                         "Player* RandomPlayerbotMgr::GetRandomPlayer()")
        self.assertIn("currentBots.erase(bot);", errors)

    def test_05_bot_login_is_deferred_and_ai_check_follows_holder_insert(self):
        callback = between(self.holder, "sRandomPlayerbotMgr.OnPlayerLogin(bot);",
                           "PlayerbotHolder::botLoading.erase(holder.GetGuid());")
        self.assertIn("QueueOperation(std::move(op));", callback)
        holder = between(self.holder, "void PlayerbotHolder::OnBotLogin(Player* const bot)",
                         "void PlayerbotHolder::LogoutPlayerBot(") if (
                             self.holder.count("void PlayerbotHolder::LogoutPlayerBot(") == 1 and
                             self.holder.index("void PlayerbotHolder::LogoutPlayerBot(") >
                             self.holder.index("void PlayerbotHolder::OnBotLogin(Player* const bot)")
                         ) else self.holder.split("void PlayerbotHolder::OnBotLogin(Player* const bot)", 1)[1].split(
                             "void PlayerbotHolder::", 1)[0]
        self.assertLess(holder.index("playerBots[bot->GetGUID()] = bot;"),
                        holder.index("PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);"))
        self.assertIn("if (!botAI)", holder)

    def test_06_full_reset_only_preserves_previously_rewarded_marker(self):
        randomize = between(self.factory, "void PlayerbotFactory::Randomize(bool incremental)",
                            "void PlayerbotFactory::Refresh()")
        self.assertIn("if (!incremental)", randomize)
        self.assertIn("ResetQuests();", randomize)
        reset = between(self.factory, "void PlayerbotFactory::ResetQuests()",
                        "uint32 PlayerbotFactory::InitTalentsTree(")
        for required in ("preserveMask != 0 && sRandomPlayerbotMgr.IsRandomBot(bot)",
                         "entry >= 66001 && entry <= 66018",
                         "bot->GetQuestStatus(entry) == QUEST_STATUS_REWARDED",
                         "bot->RemoveRewardedQuest(entry);"):
            self.assertIn(required, reset)
        self.assertNotIn("RewardQuest(", reset)

    def test_07_never_confuse_generic_logout_with_live_actor_retirement(self):
        logout = between(self.holder, "void PlayerbotHolder::LogoutPlayerBot(ObjectGuid guid)",
                         "void PlayerbotHolder::DisablePlayerBot(ObjectGuid guid)")
        self.assertIn("RemoveFromPlayerbotsMap(guid);", logout)
        self.assertIn("botWorldSessionPtr->LogoutPlayer(true);", logout)
        # The generic holder path does not, by itself, write currentBots.
        self.assertNotIn("currentBots.erase(", logout)
        self.assertNotIn("PublishF14", logout)


    def test_08_owner_core_headless_session_is_socket_derived(self):
        """Source contract: headless has no socket; NOT proof of RandomBot type."""
        self.assertEqual(self.core_cpp.count("_headless(!sock)"), 1)
        self.assertIn("m_expansion(expansion)", self.core_cpp)
        self.assertIn("[[nodiscard]] bool IsHeadless() const { return _headless; }", self.core_h)
        self.assertIn("bool const _headless;", self.core_h)

    def test_09_pb_headless_bootstrap_sets_wrath_session_value(self):
        """Don't confuse raw WotLK session expansion with earned IP tier."""
        bootstrap = between(self.holder, "void PlayerbotHolder::AddPlayerBot(",
                            "void PlayerbotHolder::") if (
                                "void PlayerbotHolder::AddPlayerBot(" in self.holder
                            ) else self.holder
        self.assertIn("new WorldSession(accountId, \"\", 0x0, nullptr, SEC_PLAYER, EXPANSION_WRATH_OF_THE_LICH_KING", bootstrap)
        self.assertIn("botSession->AddQueryHolderCallback", bootstrap)

    def test_10_ip_bot_login_level_reset_does_not_grant_selected_race_stage8(self):
        """Current IP #46 baseline level<=60 rewrites stage0 unless saved-stage pilot."""
        body = between(self.ip_player,
                       "if (!sIndividualProgression->isNormalAccount(player)) // bot or exluded account",
                       "else // normal account")
        self.assertIn("if (!preserveVerifiedRandomBotStage)", body)
        self.assertIn("if (player->GetLevel() <= 60)", body)
        self.assertIn("ForceUpdateProgressionState(player, static_cast<ProgressionState>(0));", body)
        self.assertIn("ForceUpdateProgressionState(player, static_cast<ProgressionState>(8));", body)
        self.assertIn("ForceUpdateProgressionState(player, static_cast<ProgressionState>(13));", body)
        self.assertIn("sRandomPlayerbotMgr.IsRandomBot(player->GetGUID().GetCounter())", body)
        self.assertIn("f14PreserveVerifiedRandomBotStageOnLogin", body)
        self.assertLess(body.index("if (player->GetLevel() <= 60)"),
                        body.index("ForceUpdateProgressionState(player, static_cast<ProgressionState>(8));"))


if __name__ == "__main__":
    unittest.main()
