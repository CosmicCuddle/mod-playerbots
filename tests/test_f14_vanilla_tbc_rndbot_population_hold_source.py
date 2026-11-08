#!/usr/bin/env python3
"""F14 optional Vanilla-only RNDbot preactivation hold: source contract.

Tests source paths only. This is not installed Vanilla or TBC world evidence.
No DB, bot lifecycle, visibility or owner host accessed.
"""
from pathlib import Path
import re
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
BASE = "afc38bf798cd1c96a097ebf5229e041ed0dc4253"
MANAGER = (ROOT / "src/Bot/RandomPlayerbotMgr.cpp").read_text(encoding="utf-8")
CONFIG_H = (ROOT / "src/PlayerbotAIConfig.h").read_text(encoding="utf-8")
CONFIG_CPP = (ROOT / "src/PlayerbotAIConfig.cpp").read_text(encoding="utf-8")
CONF = (ROOT / "conf/playerbots.conf.dist").read_text(encoding="utf-8")
POLICY = (ROOT / "src/Bot/F14VanillaTbcRandomBotPopulationHold.h").read_text(encoding="utf-8")
FLAG = "F14HoldTbcRaceRandomBotsDuringVanilla"
KEY = "Playerbots." + FLAG


def original_manager():
    got = subprocess.run(
        ["git", "show", BASE + ":src/Bot/RandomPlayerbotMgr.cpp"],
        cwd=ROOT, capture_output=True, text=True, check=False)
    if got.returncode:
        raise AssertionError("Need immutable PB10 parent history")
    return got.stdout


def candidate_selector():
    return MANAGER.split(
        "auto tryLoginBot = [&](CharacterInfo const& charInfo) -> bool", 1
    )[1].split("// PHASE 1", 1)[0]


class F14VanillaTbcPopulationHold(unittest.TestCase):
    def test_one_opt_in_and_independent_of_existing_pb_birth_flags(self):
        self.assertEqual(
            len(re.findall(r"^" + re.escape(KEY) + r"\s*=\s*0\s*$",
                           CONF, re.M)), 1)
        self.assertEqual(CONFIG_H.count("std::atomic_bool " + FLAG + "{false};"), 1)
        self.assertEqual(
            CONFIG_CPP.count('GetOption<bool>("' + KEY + '", false)'), 1)
        self.assertIn(FLAG + ".store(", CONFIG_CPP)
        self.assertIn("std::memory_order_release", CONFIG_CPP)
        self.assertIn(FLAG + ".load(", MANAGER)
        self.assertIn("std::memory_order_acquire", MANAGER)
        # Two independent read-only diagnostic flags retain historic defaults.
        for key in ("Playerbots.F14TrackNewRandomBotBirth",
                    "Playerbots.F14AuditNewBirthCommitReadback"):
            self.assertEqual(
                len(re.findall(r"^" + re.escape(key) + r"\s*=\s*0\s*$",
                               CONF, re.M)), 1)

    def test_pure_policy_never_writes_or_treats_other_race_as_be_d(self):
        for term in ("race == 10 || race == 11",
                     "fromAssignedRandomBotPool",
                     "manuallyEnabledForVanilla",
                     "HoldBeforeAddEvent", "StockAdmissionPath"):
            self.assertIn(term, POLICY)
        for term in ("Sql", "Database", "SetEventValue", "currentBots",
                     "ForceUpdateProgressionState(", "RemoveRewardedQuest",
                     "WorldSession", "Player*"):
            self.assertNotIn(term, POLICY)

    def test_only_random_bot_type_one_pool_feeds_candidate_and_guard(self):
        self.assertIn("if (accountType == 1) rndBotTypeAccounts.push_back(accountId);", MANAGER)
        self.assertIn("accountsToUse = rndBotTypeAccounts;", MANAGER)
        self.assertIn("CharacterInfo info;", MANAGER)
        self.assertIn("info.accountId = accountId;", MANAGER)
        src = candidate_selector()
        self.assertIn("std::find(rndBotTypeAccounts.begin(), rndBotTypeAccounts.end(),", src)
        self.assertIn("charInfo.accountId) != rndBotTypeAccounts.end()", src)
        self.assertIn("fromAssignedRandomBotPool, charInfo.rRace", src)
        self.assertNotIn("IsRandomBot(player", src)
        self.assertNotIn("isBotAccount(", src)

    def test_return_precedes_any_login_or_guid_mutation(self):
        src = candidate_selector()
        key = "CosmicF14VanillaTbcPopulationHold::Decide("
        self.assertEqual(src.count(key), 1)
        self.assertEqual(src.count("Result::HoldBeforeAddEvent"), 1)
        hold = src.index(key)
        guard_end = src.index("return false;", src.index("Result::HoldBeforeAddEvent"))
        self.assertLess(hold, guard_end)
        for mutation in ("GetEventValue(charInfo.guid, \"add\")",
                         'SetEventValue(charInfo.guid, "add", 1, add_time);',
                         'SetEventValue(charInfo.guid, "logout", 0, 0);',
                         "currentBots.insert(charInfo.guid);"):
            self.assertIn(mutation, src)
            self.assertLess(guard_end, src.index(mutation))
        self.assertEqual(src.count('SetEventValue(charInfo.guid, "add", 1, add_time);'), 1)
        self.assertEqual(src.count("currentBots.insert(charInfo.guid);"), 1)
        for forbidden in ("SaveToDB(", "DELETE FROM", "CharacterDatabase.Execute(",
                          "ForceUpdateProgressionState(", "bot->", "RemoveRewardedQuest("):
            self.assertNotIn(forbidden, src)

    def test_shared_selector_used_for_all_three_actual_selection_phases(self):
        # Phase 4 only produces a diagnostics message, not another selection.
        phase = MANAGER.split("// PHASE 1: Log-in Alliance", 1)[1].split(
            "std::string const RandomPlayerbotMgr::", 1)[0]
        self.assertEqual(phase.count("if (tryLoginBot(charInfo))"), 3)
        self.assertIn("// PHASE 4:", phase)
        self.assertEqual(MANAGER.count(
            "auto tryLoginBot = [&](CharacterInfo const& charInfo) -> bool"), 1)

    def test_original_manager_diff_is_only_header_and_one_early_guard(self):
        old = original_manager()
        include = '#include "F14VanillaTbcRandomBotPopulationHold.h"\n'
        self.assertNotIn(include, old)
        self.assertEqual(MANAGER.count(include), 1)
        current = MANAGER.replace(include, "", 1)
        start = "            // F14 OWNER VANILLA-ONLY MANUAL HOLD (opt-in, normally OFF)."
        end = '            if (GetEventValue(charInfo.guid, "add") ||'
        self.assertIn(start, current)
        before, tail = current.split(start, 1)
        self.assertIn(end, tail)
        _, after = tail.split(end, 1)
        reconstructed = before + end + after
        self.assertEqual(reconstructed, old,
                         "Unexpected original RNDbot selection-path drift")

    def test_flag_default_does_not_create_global_hide_or_stage8(self):
        self.assertNotIn("F14HoldTbcRaceRandomBotsDuringVanilla = 1", CONF)
        self.assertNotIn("WORLDOBJECTHOOK_ON_F14_", MANAGER)
        self.assertNotIn("ForceUpdateProgressionState(", candidate_selector())
        self.assertNotIn("RemoveRewardedQuest(", candidate_selector())


if __name__ == "__main__":
    unittest.main()
