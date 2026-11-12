#!/usr/bin/env python3
"""PB13 exact parent-vs-child source-only proof of pre-AddPlayerBot hold.

No Worldserver run, DB, character/quest mutations or effective flag changes.
The actual PB12 parent must be fetched as immutable local Git history.
"""
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
MANAGER = ROOT / "src/Bot/RandomPlayerbotMgr.cpp"
PARENT = "825b9dc6c5222b193c1af9eb9df2cf50d01808a1"
START = "        // F14 manual Vanilla-only hold: currentBots may already contain"
END = "        AddPlayerBot(botGUID, 0);"
ORIGINAL = """    uint32 randomTime;
    if (!player)
    {
        AddPlayerBot(botGUID, 0);"""


def prior():
    r = subprocess.run(["git", "show", PARENT + ":src/Bot/RandomPlayerbotMgr.cpp"],
                       cwd=ROOT, capture_output=True, text=True, check=False)
    if r.returncode:
        raise AssertionError("Exact immutable PB12 parent required")
    return r.stdout


def process_function(source):
    return source.split("bool RandomPlayerbotMgr::ProcessBot(uint32 bot)", 1)[1].split(
        "bool RandomPlayerbotMgr::ProcessBot(Player* bot)", 1
    )[0]


def hold_block(source):
    body = process_function(source)
    start = body.index(START)
    end = body.index(END, start)
    return body[start:end]


class F14PreloadHoldSource(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.old = prior()
        cls.new = MANAGER.read_text(encoding="utf-8")

    def test_only_new_preload_guard_added_to_original_pb12_manager(self):
        self.assertNotIn(START, self.old)
        self.assertEqual(self.new.count(START), 1)
        self.assertEqual(self.new.count(END), self.old.count(END))
        new = self.new
        begin = new.index(START)
        end = new.index(END, begin)
        reconstructed = new[:begin] + new[end:]
        self.assertEqual(reconstructed, self.old,
                         "Any mutation beyond one new ProcessBot pre-login guard rejected")

    def test_guard_reuses_manual_off_and_positive_account_cache_race(self):
        src = hold_block(self.new)
        for required in (
                "F14HoldTbcRaceRandomBotsDuringVanilla.load(",
                "std::memory_order_acquire",
                "CharacterCacheEntry const* entry",
                "sCharacterCache->GetCharacterCacheByGuid(botGUID)",
                "sCharacterCache->GetCharacterAccountIdByGuid(botGUID)",
                "std::find(rndBotTypeAccounts.begin(), rndBotTypeAccounts.end(),",
                "account) != rndBotTypeAccounts.end()",
                "entry && CosmicF14VanillaTbcPopulationHold::Decide(",
                "true, fromAssignedRandomBotPool, entry->Race",
                "Result::HoldBeforeAddEvent",
                "return false;"):
            self.assertIn(required, src)
        self.assertLess(src.index("entry &&"), src.index("return false;"))
        self.assertNotIn("IsRandomBot(botGUID)", src)  # not yet admitted
        self.assertNotIn("entry->Race == 10", src)  # no global race-only filter
        self.assertNotIn("entry->Race == 11", src)
        flag = "Playerbots.F14HoldTbcRaceRandomBotsDuringVanilla = 0"
        self.assertEqual((ROOT / "conf/playerbots.conf.dist").read_text().count(flag), 1)

    def test_guard_runs_after_original_validity_but_before_add_and_update(self):
        old = process_function(self.old)
        new = process_function(self.new)
        self.assertEqual(old.count("AddPlayerBot(botGUID, 0);"), 1)
        self.assertEqual(new.count("AddPlayerBot(botGUID, 0);"), 1)
        self.assertLess(new.index("uint32 isValid = GetEventValue(bot, \"add\");"),
                        new.index("if (!isValid)"))
        self.assertLess(new.index("if (!isValid)"), new.index(START))
        self.assertLess(new.index(START), new.index("AddPlayerBot(botGUID, 0);"))
        self.assertLess(new.index("return false;", new.index(START)),
                        new.index("AddPlayerBot(botGUID, 0);"))
        self.assertLess(new.index(END), new.index('SetEventValue(bot, "update", 1, randomTime);'))
        self.assertIn("if (!player)", new[:new.index(START)])
        self.assertIn("if (player->GetGroup() || player->HasUnitState(", new)
        self.assertEqual(old.count("SetEventValue(bot, \"add\""), new.count("SetEventValue(bot, \"add\""))

    def test_no_mutation_or_forced_early_logout_on_hold(self):
        src = hold_block(self.new)
        for forbidden in ("SetEventValue(", "currentBots.erase(", "currentBots.insert(",
                          "Delete", "SaveToDB(", "DirectExecute(", "CharacterDatabase.",
                          "RemoveRewardedQuest(", "ForceUpdateProgressionState(",
                          "LogoutPlayerBot(", "AddPlayerBot(", "GetGroup(",
                          "SetLevel(", "TeleportTo("):
            self.assertNotIn(forbidden, src)
        # No preexisting selected/online actor is touched: branch only !player
        self.assertIn("if (!player)", process_function(self.new).split(START)[0])

    def test_preadd_and_getbots_holds_preserved_identically(self):
        self.assertEqual(
            self.new.count("CosmicF14VanillaTbcPopulationHold::Decide("),
            self.old.count("CosmicF14VanillaTbcPopulationHold::Decide(") + 1)
        for name in ("void RandomPlayerbotMgr::GetBots()",
                     "uint32 RandomPlayerbotMgr::AddRandomBots()",
                     "void RandomPlayerbotMgr::OnPlayerLoginError(uint32 bot)"):
            self.assertEqual(self.new.count(name), self.old.count(name))
        self.assertIn("GetCharacterCacheByGuid(guid)", self.new)
        self.assertIn("currentBots.insert(charInfo.guid);", self.new)


if __name__ == "__main__":
    unittest.main()
