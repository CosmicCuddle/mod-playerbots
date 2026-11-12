#!/usr/bin/env python3
"""PB12 F14 per-verified-RandomBot saved 'add' replay guard (OFF by default).

Inspect the ACTUAL new source against immutable PB11 and ensure no original
GetBots() behavior was changed outside the one conditional; no SQL, DB or
character data is touched by this source-only test.
"""
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "src/Bot/RandomPlayerbotMgr.cpp"
PB11 = "a612093fde694fadf79d0bd790fcd82cb4f9b6b8"

ORIGINAL = '''            uint32 bot = fields[0].Get<uint32>();
            if (GetEventValue(bot, "add"))
                currentBots.insert(bot);

            if (currentBots.size() >= maxAllowedBotCount)'''
REPLACEMENT_START = '''            uint32 bot = fields[0].Get<uint32>();
            if (GetEventValue(bot, "add"))
            {
                // F14 restart replay:'''
END = '''            if (currentBots.size() >= maxAllowedBotCount)'''


class F14AddEventReplay(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.new = SOURCE.read_text(encoding="utf-8")
        p = subprocess.run(
            ["git", "show", PB11 + ":src/Bot/RandomPlayerbotMgr.cpp"],
            cwd=ROOT, text=True, capture_output=True, check=False)
        if p.returncode:
            raise AssertionError("Need original immutable PB11 parent")
        cls.old = p.stdout

    def test_exact_scope_preserves_every_original_manager_path(self):
        self.assertNotIn('#include "CharacterCache.h"\n', self.old)
        self.assertEqual(self.new.count('#include "CharacterCache.h"\n'), 1)
        self.assertEqual(self.old.count(ORIGINAL), 1)
        self.assertEqual(self.new.count(REPLACEMENT_START), 1)
        body_start = self.new.index(REPLACEMENT_START)
        body_end = self.new.index(END, body_start)
        new = self.new[:body_start] + ORIGINAL + self.new[body_end + len(END):]
        new = new.replace('#include "CharacterCache.h"\n', "", 1)
        self.assertEqual(self.old, new,
                         "Any change beyond the one guarded GetBots path is prohibited")

    def test_restore_event_replay_has_positive_account_and_race_provenance(self):
        src = self.new.split("void RandomPlayerbotMgr::GetBots()", 1)[1].split(
            "std::vector<uint32> RandomPlayerbotMgr::GetBgBots", 1)[0]
        guard = src.split('if (GetEventValue(bot, "add"))', 1)[1].split(
            "currentBots.insert(bot)", 1)[0]
        self.assertIn("F14HoldTbcRaceRandomBotsDuringVanilla.load(", guard)
        self.assertIn("std::memory_order_acquire", guard)
        self.assertIn("GetCharacterCacheByGuid(guid)", guard)
        self.assertIn("GetCharacterAccountIdByGuid(guid)", guard)
        self.assertIn("CharacterCacheEntry const* entry", guard)
        self.assertIn("std::find(rndBotTypeAccounts.begin(), rndBotTypeAccounts.end(),", guard)
        self.assertIn("account) != rndBotTypeAccounts.end()", guard)
        self.assertIn("entry && CosmicF14VanillaTbcPopulationHold::Decide(", guard)
        self.assertIn("true, fromAssignedRandomBotPool, entry->Race", guard)
        self.assertIn("Result::HoldBeforeAddEvent", guard)
        self.assertIn("continue;", guard)
        # A failed cache entry or unknown/non-type1 account cannot classify
        # a real human, Altbot or other unknown character as a hidden RandomBot.
        self.assertNotIn("entry->Race == 10", guard)
        self.assertNotIn("entry->Race == 11", guard)

    def test_no_destructive_effect_and_hold_before_insert(self):
        src = self.new.split("void RandomPlayerbotMgr::GetBots()", 1)[1].split(
            "std::vector<uint32> RandomPlayerbotMgr::GetBgBots", 1)[0]
        self.assertEqual(src.count("currentBots.insert(bot)"), 1)
        self.assertLess(src.index("continue;"), src.index("currentBots.insert(bot)"))
        self.assertLess(src.index("GetEventValue(bot, \"add\")"), src.index("continue;"))
        self.assertEqual(src.count("GetEventValue(bot, \"add\")"), 1)
        for unsafe in ("SetEventValue(", "SaveToDB(", "DELETE FROM", "DirectExecute(",
                       "CharacterDatabase.Execute(", "RemoveRewardedQuest(",
                       "ForceUpdateProgressionState(", "LogoutPlayerBot(", "DeleteBot"):
            self.assertNotIn(unsafe, src)

    def test_still_only_one_original_preadd_hold_and_default_off(self):
        conf = (ROOT / "conf/playerbots.conf.dist").read_text(encoding="utf-8")
        self.assertEqual(conf.count(
            "Playerbots.F14HoldTbcRaceRandomBotsDuringVanilla = 0"), 1)
        self.assertIn("std::atomic_bool F14HoldTbcRaceRandomBotsDuringVanilla{false};",
                      (ROOT / "src/PlayerbotAIConfig.h").read_text())
        self.assertEqual(self.new.count(
            "CosmicF14VanillaTbcPopulationHold::Decide("), 2)
        self.assertIn("currentBots.insert(charInfo.guid);", self.new)


if __name__ == "__main__":
    unittest.main()
