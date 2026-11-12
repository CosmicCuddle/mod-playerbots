#!/usr/bin/env python3
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
MGR = (ROOT / "src/Bot/RandomPlayerbotMgr.cpp").read_text()
CFG_CPP = (ROOT / "src/PlayerbotAIConfig.cpp").read_text()
CFG_H = (ROOT / "src/PlayerbotAIConfig.h").read_text()
CONF = (ROOT / "conf/playerbots.conf.dist").read_text()

class UpstreamCountModeHold(unittest.TestCase):
    def test_upstream_count_modes_survive_without_obsolete_periodic_model(self):
        combined = "\n".join((MGR, CFG_CPP, CFG_H, CONF))
        for token in ("EnablePeriodicOnlineOffline","PeriodicOnlineOfflineRatio",
                      "MinRandomBotInWorldTime","MaxRandomBotInWorldTime","PermanentlyInWorldTime"):
            self.assertNotIn(token, combined)
        for token in ("RandomBotCountMode","RandomBotCountMinTime","RandomBotCountMaxTime"):
            self.assertIn(token, CFG_CPP)
        self.assertIn("Playerbots.RandomBotCountMode = 0", CONF)

    def test_f14_options_remain_default_off(self):
        for line in ("Playerbots.F14TrackNewRandomBotBirth = 0",
                     "Playerbots.F14AuditNewBirthCommitReadback = 0",
                     "Playerbots.F14HoldTbcRaceRandomBotsDuringVanilla = 0"):
            self.assertIn(line, CONF)
        self.assertIn("F14HoldTbcRaceRandomBotsDuringVanilla{false}", CFG_H)

    def test_effective_count_drives_add_and_remove(self):
        self.assertGreaterEqual(MGR.count(
            "CountF14ScheduledRandomBotsForQuota(currentBots, rndBotTypeAccounts)"), 3)
        update = MGR.split("void RandomPlayerbotMgr::UpdateAIInternal",1)[1].split(
            "void RandomPlayerbotMgr::CheckPlayers",1)[0]
        self.assertIn("availableBotCount = CountF14ScheduledRandomBotsForQuota", update)
        self.assertIn("availableBotCount < maxAllowedBotCount", update)
        self.assertIn("availableBotCount > maxAllowedBotCount", update)
        add = MGR.split("uint32 RandomPlayerbotMgr::AddRandomBots()",1)[1].split(
            "bool RandomPlayerbotMgr::IsRemovableBot",1)[0]
        self.assertIn("eligibleScheduledBotCount", add)
        self.assertNotIn("currentBots.size() < maxAllowedBotCount", add)
        remove = MGR.split("uint32 RandomPlayerbotMgr::RemoveRandomBots()",1)[1].split(
            "uint32 RandomPlayerbotMgr::GetScheduledBotCount",1)[0]
        self.assertIn("effectiveScheduledBotCount", remove)
        self.assertNotIn("currentBots.size() - maxAllowedBotCount", remove)

    def test_held_online_bots_are_outside_faction_downsize(self):
        remove = MGR.split("uint32 RandomPlayerbotMgr::RemoveRandomBots()",1)[1].split(
            "uint32 RandomPlayerbotMgr::GetScheduledBotCount",1)[0]
        held = remove.index("IsF14PositivelyHeldScheduledRandomBot")
        self.assertLess(held, remove.index("onlineAlliance++"))
        self.assertLess(held, remove.index("allianceBots.push_back"))
        for safety in ("bot->IsInCombat()","UNIT_STATE_IN_FLIGHT","bot->InBattleground()",
                       "bot->GetMap()->IsDungeon()","HasGameClientMaster()",
                       "IsRealPlayer(member)","IsSelfBot(member)"):
            self.assertIn(safety, MGR)
        self.assertIn('SetEventValue(bot, "add", 0, 0);', remove)
        self.assertIn("LogoutPlayerBot(guid);", remove)

    def test_positive_identity_and_restart_pending_paths_remain(self):
        self.assertIn("ShouldHoldF14PendingRandomBotLogin", MGR)
        self.assertIn("F14 restart replay", MGR)
        self.assertIn("assignedRandomBotAccounts.contains(account)", MGR)
        self.assertIn("entry->Race", MGR)
        self.assertIn("if (!manualVanillaHold)\n        return currentBots.size();", MGR)

if __name__ == "__main__":
    unittest.main()
