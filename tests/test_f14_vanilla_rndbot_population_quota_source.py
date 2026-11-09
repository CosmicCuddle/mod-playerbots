#!/usr/bin/env python3
"""PB14 exact PB13-to-PB14 source quota-only extension.

Manual hold OFF must keep the original raw scheduled count, with no cache
lookup or SQL. ON only excludes positively assigned type1 BE/D RandomBots
from QUOTA; no saved bot GUID or event is deleted. No owner host touched.
"""
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
PARENT = "5066ba448b3842c4c6b56c843d89d85d5cca6a69"
CPP = ROOT / "src/Bot/RandomPlayerbotMgr.cpp"
HEADER = "F14VanillaTbcRandomBotPopulationCount.h"

COUNT_START = 'namespace\n{\n// Do NOT remove held GUIDs'
COUNT_END = '} // namespace\n'
NEW_QUOTA = """    // Count only genuinely eligible scheduled bots when the separate manual
    // Vanilla hold is ON. The held GUIDs and their saved "add" events remain.
    // When OFF, the helper returns the exact original currentBots.size().
    uint32 const eligibleScheduledBotCount =
        CountF14ScheduledRandomBotsForQuota(currentBots, rndBotTypeAccounts);
    if (eligibleScheduledBotCount < maxAllowedBotCount)
    {
        // Calculate how many eligible bots to add, without deleting old bots.
        maxAllowedBotCount -= eligibleScheduledBotCount;"""
OLD_QUOTA = """    if (currentBots.size() < maxAllowedBotCount)
    {
        // Calculate how many bots to add
        maxAllowedBotCount -= currentBots.size();"""


def get_old():
    r = subprocess.run(["git", "show", PARENT + ":src/Bot/RandomPlayerbotMgr.cpp"],
                       cwd=ROOT, capture_output=True, text=True, check=False)
    if r.returncode:
        raise AssertionError("Immutable PB13 source history must be available")
    return r.stdout


class F14QuotaOriginalSource(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.new = CPP.read_text(encoding="utf-8")
        cls.old = get_old()
        start = cls.new.index(COUNT_START)
        end = cls.new.index(COUNT_END, start) + len(COUNT_END) + 1
        cls.helper = cls.new[start:end]

    def test_only_count_helper_and_two_calls_are_new(self):
        self.assertNotIn('#include "' + HEADER + '"\n', self.old)
        self.assertEqual(self.new.count('#include "' + HEADER + '"\n'), 1)
        self.assertEqual(self.new.count(COUNT_START), 1)
        self.assertEqual(self.new.count(NEW_QUOTA), 1)
        self.assertEqual(self.old.count(OLD_QUOTA), 1)
        candidate = self.new.replace('#include "' + HEADER + '"\n', "", 1)
        candidate = candidate.replace(self.helper, "", 1)
        candidate = candidate.replace(
            "uint32 availableBotCount = CountF14ScheduledRandomBotsForQuota(currentBots, rndBotTypeAccounts);",
            "uint32 availableBotCount = availableBots.size();", 1)
        candidate = candidate.replace(NEW_QUOTA, OLD_QUOTA, 1)
        self.assertEqual(candidate, self.old,
                         "Original PB13 must be byte-identical after only quota changes")

    def test_positive_type1_cache_and_race_provenance_needed(self):
        helper = self.helper
        for required in (
            "F14HoldTbcRaceRandomBotsDuringVanilla.load(",
            "std::memory_order_acquire",
            "if (!manualVanillaHold)",
            "return currentBots.size();",
            "std::unordered_set<uint32> const assignedRandomBotAccounts(",
            "rndBotTypeAccounts.begin(), rndBotTypeAccounts.end()",
            "GetCharacterCacheByGuid(guid)",
            "if (!entry)",
            "return false;",
            "GetCharacterAccountIdByGuid(guid)",
            "assignedRandomBotAccounts.contains(account)",
            "Decide(",
            "true, positivelyAssignedRandomBot, entry->Race",
            "Result::HoldBeforeAddEvent",
            "CountForQuota(",
        ):
            self.assertIn(required, helper)
        self.assertLess(helper.index("if (!manualVanillaHold)"),
                        helper.index("GetCharacterCacheByGuid(guid)"))
        self.assertLess(helper.index("if (!entry)"),
                        helper.index("assignedRandomBotAccounts.contains(account)"))
        self.assertNotIn("if (entry->Race == 10)", helper)
        self.assertNotIn("isBotAccount(", helper)

    def test_no_data_mutations_inside_helper(self):
        for forbidden in (
            "SetEventValue(", "currentBots.erase(", "currentBots.insert(",
            "SaveToDB(", "DELETE FROM", "DirectExecute(", "CharacterDatabase.",
            "PlayerbotsDatabase.", "LogoutPlayerBot(", "AddPlayerBot(",
            "ForceUpdateProgressionState(", "RemoveRewardedQuest(", "SetLevel(",
        ):
            self.assertNotIn(forbidden, self.helper)
        header = (ROOT / "src/Bot" / HEADER).read_text(encoding="utf-8")
        self.assertIn("if (!manualVanillaHold)", header)
        self.assertIn("return scheduledGuids.size();", header)
        self.assertIn("if (!isPositivelyHeld(guid))", header)
        for forbidden in ("erase(", "SetEventValue(", "Database", "RemoveRewardedQuest("):
            self.assertNotIn(forbidden, header)

    def test_both_quota_call_sites_and_all_original_guards_preserved(self):
        update = self.new.split("void RandomPlayerbotMgr::UpdateAIInternal(", 1)[1].split(
            "void RandomPlayerbotMgr::AssignAccountTypes()", 1)[0]
        add = self.new.split("uint32 RandomPlayerbotMgr::AddRandomBots()", 1)[1].split(
            "void RandomPlayerbotMgr::LoadBattleMastersCache()", 1)[0]
        self.assertEqual(update.count(
            "CountF14ScheduledRandomBotsForQuota(currentBots, rndBotTypeAccounts)"), 1)
        self.assertEqual(add.count(
            "CountF14ScheduledRandomBotsForQuota(currentBots, rndBotTypeAccounts)"), 1)
        self.assertIn("if (availableBotCount < maxAllowedBotCount", update)
        self.assertIn("if (eligibleScheduledBotCount < maxAllowedBotCount)", add)
        self.assertIn("maxAllowedBotCount -= eligibleScheduledBotCount;", add)
        self.assertIn("SetEventValue(charInfo.guid, \"add\", 1, add_time);", add)
        self.assertIn("currentBots.insert(charInfo.guid);", add)
        self.assertIn("CosmicF14VanillaTbcPopulationHold::Decide(", add)
        self.assertIn('currentBots.insert(bot)', self.new)
        self.assertIn('AddPlayerBot(botGUID, 0);', self.new)
        conf = (ROOT / "conf/playerbots.conf.dist").read_text(encoding="utf-8")
        self.assertEqual(conf.count(
            "Playerbots.F14HoldTbcRaceRandomBotsDuringVanilla = 0"), 1)


if __name__ == "__main__":
    unittest.main()
