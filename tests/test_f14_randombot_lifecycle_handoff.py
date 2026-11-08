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
RANDOM_FACTORY = ROOT / "src/Bot/Factory/RandomPlayerbotFactory.cpp"
# CI-only pinned read-only owner repositories. No customer/host data.
CORE_CPP = ROOT / "owner-core/src/server/game/Server/WorldSession.cpp"
CORE_H = ROOT / "owner-core/src/server/game/Server/WorldSession.h"
CORE_PLAYER_H = ROOT / "owner-core/src/server/game/Entities/Player/Player.h"
CORE_PLAYER_CPP = ROOT / "owner-core/src/server/game/Entities/Player/Player.cpp"
CORE_CHAR_HANDLER = ROOT / "owner-core/src/server/game/Handlers/CharacterHandler.cpp"
IP_PLAYER = ROOT / "owner-ip/src/IndividualProgressionPlayer.cpp"
WORLD_SCRIPT = ROOT / "src/Script/Playerbots.cpp"
WORLD_PROC = ROOT / "src/Script/WorldThr/PlayerbotWorldThreadProcessor.cpp"
LOGIN_OPS = ROOT / "src/Script/WorldThr/PlayerbotOperations.h"

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
        cls.random_factory = RANDOM_FACTORY.read_text(encoding="utf-8")
        cls.core_cpp = CORE_CPP.read_text(encoding="utf-8")
        cls.core_h = CORE_H.read_text(encoding="utf-8")
        cls.core_player_h = CORE_PLAYER_H.read_text(encoding="utf-8")
        cls.core_player_cpp = CORE_PLAYER_CPP.read_text(encoding="utf-8")
        cls.core_char_handler = CORE_CHAR_HANDLER.read_text(encoding="utf-8")
        cls.ip_player = IP_PLAYER.read_text(encoding="utf-8")
        cls.world_script = WORLD_SCRIPT.read_text(encoding="utf-8")
        cls.world_processor = WORLD_PROC.read_text(encoding="utf-8")
        cls.login_operations = LOGIN_OPS.read_text(encoding="utf-8")

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
                            "void PlayerbotHolder::AbandonPendingLogin(ObjectGuid guid)") if (
                                "void PlayerbotHolder::AddPlayerBot(" in self.holder
                            ) else self.holder
        self.assertIn("new WorldSession(accountId, \"\", 0x0, nullptr, SEC_PLAYER, EXPANSION_WRATH_OF_THE_LICH_KING", bootstrap)
        self.assertIn("botSession->AddQueryHolderCallback", bootstrap)

    def test_10_ip_bot_login_level_reset_does_not_grant_selected_race_stage8(self):
        """Current IP #46 baseline level<=60 rewrites stage0 unless saved-stage pilot."""
        anchor = "if (!sIndividualProgression->isNormalAccount(player)) // bot or exluded account"
        self.assertEqual(self.ip_player.count(anchor), 1)
        body = self.ip_player.split(anchor, 1)[1].split("else // normal account", 1)[0]
        self.assertIn("if (!preserveVerifiedRandomBotStage)", body)
        self.assertIn("if (player->GetLevel() <= 60)", body)
        self.assertIn("ForceUpdateProgressionState(player, static_cast<ProgressionState>(0));", body)
        self.assertIn("ForceUpdateProgressionState(player, static_cast<ProgressionState>(8));", body)
        self.assertIn("ForceUpdateProgressionState(player, static_cast<ProgressionState>(13));", body)
        self.assertIn("sRandomPlayerbotMgr.IsRandomBot(player->GetGUID().GetCounter())", body)
        self.assertIn("f14PreserveVerifiedRandomBotStageOnLogin", body)
        self.assertLess(body.index("if (player->GetLevel() <= 60)"),
                        body.index("ForceUpdateProgressionState(player, static_cast<ProgressionState>(8));"))


    def test_11_world_script_drains_pending_logins_before_world_ops(self):
        # A source-verified world-thread entry is required: not merely a
        # callback that can execute on a Core map worker or during SQL load.
        world = between(self.world_script, "void OnUpdate(uint32 diff) override",
                        "// Runs before the sessions are kicked on server shutdown")
        ordered = ("PlayerbotHolder::UpdatePendingLogins();",
                   "sRandomPlayerbotMgr.UpdateSessions();",
                   "PlayerbotWorldThreadProcessor::instance().Update(diff);",
                   "sRandomPlayerbotMgr.UpdateAI(diff);")
        for token in ordered:
            self.assertEqual(world.count(token), 1)
        self.assertEqual([world.index(x) for x in ordered],
                         sorted(world.index(x) for x in ordered))

    def test_12_deferred_login_operation_is_only_player_exists_gate(self):
        # This is deliberately insufficient for positive F14 publication.
        ops = between(self.login_operations, "class OnBotLoginOperation : public PlayerbotOperation",
                      "class ArenaTeamAssignOperation : public PlayerbotOperation")
        self.assertIn("ObjectAccessor::FindConnectedPlayer(m_botGuid)", ops)
        self.assertIn("holder->OnBotLogin(bot);", ops)
        self.assertIn("m_masterAccountId", ops)
        self.assertIn("GetPlayerbotMgr(masterPlayer)", ops)
        self.assertNotIn("GET_PLAYERBOT_AI(bot)", ops)
        self.assertNotIn("IsHeadless()", ops)
        self.assertNotIn("IsInWorld()", ops)
        self.assertNotIn("IsRandomBot(", ops)

    def test_13_world_processor_runs_operations_outside_queue_mutex(self):
        body = between(self.world_processor,
                       "void PlayerbotWorldThreadProcessor::ProcessBatch()",
                       "void PlayerbotWorldThreadProcessor::CheckQueueHealth()")
        self.assertIn("std::lock_guard<std::mutex> lock(m_queueMutex)", body)
        self.assertLess(body.index("std::lock_guard<std::mutex> lock(m_queueMutex)"),
                        body.index("bool success = operation->Execute();"))
        self.assertIn("// Execute operations outside of lock", body)
        # We must not publish a hide while a queue lock remains held.
        self.assertNotIn("OnBotLogin(", body)

    def test_14_holder_validates_ai_after_insertion_and_before_any_candidate(self):
        body = self.holder.split("void PlayerbotHolder::OnBotLogin(Player* const bot)", 1)[1].split(
            "void PlayerbotHolder::", 1)[0]
        self.assertIn("playerBots[bot->GetGUID()] = bot;", body)
        self.assertIn("OnBotLoginInternal(bot);", body)
        self.assertIn("PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);", body)
        self.assertIn("if (!botAI)", body)
        self.assertLess(body.index("playerBots[bot->GetGUID()] = bot;"),
                        body.index("if (!botAI)"))
        self.assertLess(body.index("OnBotLoginInternal(bot);"), body.index("if (!botAI)"))
        # No positive readiness stamp exists. A future publisher must also
        # require an exact headless session, genuine RNDBOT, in-world actor
        # and a nonrecycled generation; this callback alone proves none.
        self.assertNotIn("PublishF14", body)

    def test_15_logout_removes_holder_before_session_and_actor_deletion(self):
        body = between(self.holder, "void PlayerbotHolder::LogoutPlayerBot(ObjectGuid guid)",
                       "void PlayerbotHolder::DisablePlayerBot(ObjectGuid guid)")
        ordered = ("RemoveFromPlayerbotsMap(guid);",
                   "botWorldSessionPtr->LogoutPlayer(true);",
                   "delete botWorldSessionPtr;")
        for token in ordered:
            self.assertEqual(body.count(token), 1)
        self.assertEqual([body.index(x) for x in ordered],
                         sorted(body.index(x) for x in ordered))
        self.assertNotIn("RetireF14", body)
        self.assertIn("if (!botAI)", body)
        # Present runtime has no F14 publication/retirement. Actual future
        # retirement must precede removed/deleted pointers and all data races.

    def test_16_new_random_bot_does_not_mark_at_login_first(self):
        # Requiring the normal human first-login bit as a PB "fresh" proof
        # would silently exclude freshly generated real RandomBots.
        bot_create = between(
            self.random_factory,
            "Player* RandomPlayerbotFactory::CreateRandomBot(",
            "std::string const RandomPlayerbotFactory::CreateRandomBotName(")
        self.assertIn("player->Create(", bot_create)
        self.assertIn("player->SetAtLoginFlag(AT_LOGIN_NONE);", bot_create)
        self.assertNotIn("SetAtLoginFlag(AT_LOGIN_FIRST)", bot_create)
        self.assertIn("AT_LOGIN_NONE              = 0x00,", self.core_player_h)
        self.assertIn("AT_LOGIN_FIRST             = 0x20,", self.core_player_h)
        self.assertIn("void SetAtLoginFlag(AtLoginFlags f) { m_atLoginFlags |= f; }",
                      self.core_player_h)
        self.assertIn("m_atLoginFlags = AT_LOGIN_NONE;", self.core_player_cpp)
        # Core Player::Create does not add a FIRST flag implicitly.
        self.assertNotIn("SetAtLoginFlag(AT_LOGIN_FIRST)", self.core_player_cpp)

    def test_17_normal_human_first_login_flag_is_not_random_bot_proof(self):
        self.assertIn("newChar->SetAtLoginFlag(AT_LOGIN_FIRST);", self.core_char_handler)
        self.assertIn("sScriptMgr->OnPlayerLogin(pCurrChar);", self.core_char_handler)
        self.assertIn("pCurrChar->RemoveAtLoginFlag(AT_LOGIN_FIRST);", self.core_char_handler)
        self.assertIn("sScriptMgr->OnPlayerFirstLogin(pCurrChar);", self.core_char_handler)
        self.assertLess(self.core_char_handler.index("sScriptMgr->OnPlayerLogin(pCurrChar);"),
                        self.core_char_handler.index("pCurrChar->RemoveAtLoginFlag(AT_LOGIN_FIRST);"))
        # This proves a human-creation lifecycle, not PB birth provenance.
        self.assertNotIn("AT_LOGIN_FIRST", self.random_factory.split(
            "Player* RandomPlayerbotFactory::CreateRandomBot(",1)[1].split(
            "std::string const RandomPlayerbotFactory::CreateRandomBotName(",1)[0])

if __name__ == "__main__":
    unittest.main()
