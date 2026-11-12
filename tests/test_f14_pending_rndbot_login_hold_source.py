"""Strict original-source F14 pending RNDbot callback guard tests, PB14 parent.
Do not infer Worldserver or in-game success from this fixture.
"""
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
BASE = "26d24f3cddf2dbcd66a545fd973e7c7289763e39"


def source(path):
    return (ROOT / path).read_text(encoding="utf-8")


def frozen(path):
    return subprocess.check_output(
        ["git", "show", f"{BASE}:{path}"], cwd=ROOT
    ).decode("utf-8")


class PendingRandomBotCallbackTest(unittest.TestCase):
    def test_old_callback_is_red_new_callback_has_pre_login_hold(self):
        old = frozen("src/Bot/PlayerbotMgr.cpp")
        new = source("src/Bot/PlayerbotMgr.cpp")
        self.assertNotIn("ShouldHoldF14PendingRandomBotLogin(", old)
        self.assertIn("ShouldHoldF14PendingRandomBotLogin(", new)
        start = new.index("    // The Vanilla-only hold may have been enabled")
        end = new.index("    botSession->HandlePlayerLoginFromDB(holder);", start)
        gate = new[start:end]
        self.assertIn("holder.GetGuid(), botAccountId, holder.GetMasterAccountId() == 0", gate)
        self.assertIn("AbandonPendingLogin(holder.GetGuid());", gate)
        self.assertIn("return;", gate)
        self.assertNotIn("delete ", gate)
        self.assertNotIn("LogoutPlayer(", gate)
        self.assertNotIn("SetEventValue(", gate)
        self.assertLess(start, end)
        self.assertEqual(new[:start] + new[end:], old)

    def test_real_manager_callback_provenance_and_exact_parent(self):
        path = "src/Bot/RandomPlayerbotMgr.cpp"
        new = source(path)
        old = frozen(path)
        include = '#include "F14VanillaTbcPendingRandomBotLoginHold.h"\n'
        self.assertEqual(new.count(include), 1)
        start = new.index("// Recheck when the already-queued headless")
        end = new.index("bool RandomPlayerbotMgr::IsAccountType(", start)
        method = new[start:end]
        self.assertIn("if (!manualVanillaHold || !zeroMasterRequest || !guid.IsPlayer())", method)
        self.assertIn("currentBots.contains(guid.GetCounter())", method)
        self.assertIn("sCharacterCache->GetCharacterCacheByGuid(guid)", method)
        self.assertIn("entry->AccountId", method)
        self.assertIn("rndBotTypeAccounts.begin(), rndBotTypeAccounts.end()", method)
        self.assertIn("CosmicF14VanillaPendingRandomBotLogin::ShouldHold(", method)
        self.assertNotIn("SaveToDB(", method)
        self.assertNotIn("CharacterDatabase.", method)
        self.assertNotIn("SetEventValue(", method)
        self.assertEqual((new[:start] + new[end:]).replace(include, ""), old)

    def test_original_header_and_shared_callback_not_rewritten(self):
        path = "src/Bot/RandomPlayerbotMgr.h"
        new = source(path)
        old = frozen(path)
        addition = (
            "    // A queued headless login still needs its own F14 gate just before DB login.\n"
            "    // READ-ONLY: never use race or masterAccountId=0 alone as RandomBot proof.\n"
            "    bool ShouldHoldF14PendingRandomBotLogin(ObjectGuid const& guid, uint32 expectedAccountId,\n"
            "                                            bool zeroMasterRequest) const;\n"
        )
        self.assertEqual(new.count(addition), 1)
        self.assertEqual(new.replace(addition, ""), old)
        callback = source("src/Bot/PlayerbotMgr.cpp")
        self.assertIn("session->ProcessQueryCallbacks();", callback)
        self.assertIn("if (itr == botLoading.end() || !itr->second.failed)", callback)
        self.assertIn("session->LogoutPlayer(true);\n        delete session;\n        botLoading.erase(itr);", callback)
        self.assertIn("botSession->HandlePlayerLoginFromDB(holder);", callback)

    def test_off_default_and_world_thread_order(self):
        config = source("conf/playerbots.conf.dist")
        self.assertIn("Playerbots.F14HoldTbcRaceRandomBotsDuringVanilla = 0", config)
        world = source("src/Script/Playerbots.cpp")
        start = world.index("class PlayerbotsWorldScript")
        update = world.index("PlayerbotHolder::UpdatePendingLogins();", start)
        quota = world.index("sRandomPlayerbotMgr.UpdateAI(diff);", start)
        self.assertLess(update, quota)

    def test_pure_policy_is_current_source_and_has_all_identity_guards(self):
        policy = source("src/Bot/F14VanillaTbcPendingRandomBotLoginHold.h")
        for gate in (
            "zeroMasterRequest && selectedGuid && cacheEntryPresent",
            "requestedAccountId != 0",
            "requestedAccountId == currentCachedAccountId",
            "manualVanillaHold, assignedType1RandomBot, cachedRace",
            "Result::HoldBeforeAddEvent",
        ):
            self.assertIn(gate, policy)
        self.assertNotIn("SaveToDB", policy)
        self.assertNotIn("delete ", policy)


if __name__ == "__main__":
    unittest.main()
