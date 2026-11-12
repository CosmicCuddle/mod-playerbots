#!/usr/bin/env python3
"""Current-upstream Playerbots F14 restack source contract.

This is intentionally not an exact historical-scope test. It validates the
adapted scheduler on the current upstream population model while keeping the
manual Vanilla hold default OFF and preserving upstream downsize safety.
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
MGR = (ROOT / "src/Bot/RandomPlayerbotMgr.cpp").read_text(encoding="utf-8")
HDR = (ROOT / "src/Bot/RandomPlayerbotMgr.h").read_text(encoding="utf-8")
CFG_CPP = (ROOT / "src/PlayerbotAIConfig.cpp").read_text(encoding="utf-8")
CFG_H = (ROOT / "src/PlayerbotAIConfig.h").read_text(encoding="utf-8")
CONF = (ROOT / "conf/playerbots.conf.dist").read_text(encoding="utf-8")

class UpstreamRestackPopulationContract(unittest.TestCase):
    def test_upstream_count_modes_retained_and_old_knobs_not_revived(self):
        for required in (
            "RandomBotCountMode",
            "RandomBotCountMinTime",
            "RandomBotCountMaxTime",
        ):
            self.assertIn(required, CFG_CPP)
            self.assertIn(required, CFG_H)
            self.assertIn("Playerbots." + required, CONF)
        for obsolete in (
            "EnablePeriodicOnlineOffline",
            "PeriodicOnlineOfflineRatio",
            "MinRandomBotInWorldTime",
            "MaxRandomBotInWorldTime",
            "PermanentlyInWorldTime",
        ):
            self.assertNotIn(obsolete, CFG_CPP)
            self.assertNotIn(obsolete, CFG_H)
            self.assertNotIn("Playerbots." + obsolete, CONF)

    def test_f14_options_remain_default_off_and_reset_mask_survives(self):
        self.assertEqual(CONF.count("Playerbots.F14HoldTbcRaceRandomBotsDuringVanilla = 0"), 1)
        self.assertEqual(CONF.count("Playerbots.F14TrackNewRandomBotBirth = 0"), 1)
        self.assertEqual(CONF.count("Playerbots.F14AuditNewBirthCommitReadback = 0"), 1)
        self.assertIn("ResetQuestsPreserveMask.store", CFG_CPP)
        self.assertIn("std::atomic<uint32> ResetQuestsPreserveMask", CFG_H)
        self.assertIn("std::atomic_bool F14HoldTbcRaceRandomBotsDuringVanilla{false}", CFG_H)

    def test_single_snapshot_is_positive_identity_only_and_hold_off_is_raw(self):
        start = MGR.index("struct F14ScheduledPopulationSnapshot")
        end = MGR.index("void PrintStatsThread", start)
        block = MGR[start:end]
        for required in (
            "snapshot.effectiveScheduledBotCount = static_cast<uint32>(currentBots.size())",
            "F14HoldTbcRaceRandomBotsDuringVanilla.load(",
            "if (!manualVanillaHold)",
            "return snapshot;",
            "assignedRandomBotAccounts.contains(account)",
            "GetCharacterCacheByGuid(guid)",
            "GetCharacterAccountIdByGuid(guid)",
            "CosmicF14VanillaTbcPopulationHold::Decide(",
            "Result::HoldBeforeAddEvent",
            "snapshot.heldBotGuids.insert(botGuid)",
            "snapshot.effectiveScheduledBotCount -=",
        ):
            self.assertIn(required, block)
        self.assertLess(block.index("if (!manualVanillaHold)"),
                        block.index("GetCharacterCacheByGuid(guid)"))
        for forbidden in (
            "SetEventValue(", "currentBots.erase(", "LogoutPlayerBot(",
            "AddPlayerBot(", "CharacterDatabase.", "PlayerbotsDatabase.",
        ):
            self.assertNotIn(forbidden, block)

    def test_same_snapshot_drives_outer_add_remove_and_online_budget(self):
        update = MGR.split("void RandomPlayerbotMgr::UpdateAIInternal(", 1)[1].split(
            "void RandomPlayerbotMgr::AssignAccountTypes()", 1)[0]
        self.assertEqual(update.count("BuildF14ScheduledPopulationSnapshot(currentBots, rndBotTypeAccounts)"), 1)
        self.assertIn("uint32 availableBotCount = f14Population.effectiveScheduledBotCount;", update)
        self.assertIn("uint32 onlineBotCount = playerBots.size();", update)
        self.assertIn("uint32 effectiveOnlineBotCount = onlineBotCount;", update)
        self.assertIn("f14Population.heldBotGuids.contains(guid.GetCounter())", update)
        self.assertIn("if (effectiveOnlineBotCount <", update)
        self.assertIn("? maxAllowedBotCount - effectiveOnlineBotCount", update)
        self.assertIn("RealPlayerLastTimeSeen != 0 && onlineBotCount > 0", update)
        self.assertIn("AddRandomBots(availableBotCount);", update)
        self.assertIn("RemoveRandomBots(availableBotCount, f14Population.heldBotGuids);", update)

    def test_add_uses_snapshot_but_preserves_current_upstream_event_semantics(self):
        add = MGR.split("uint32 RandomPlayerbotMgr::AddRandomBots(uint32 effectiveScheduledBotCount)", 1)[1].split(
            "bool RandomPlayerbotMgr::IsRemovableBot", 1)[0]
        self.assertIn("if (effectiveScheduledBotCount < maxAllowedBotCount)", add)
        self.assertIn("maxAllowedBotCount -= effectiveScheduledBotCount;", add)
        self.assertIn("CosmicF14VanillaTbcPopulationHold::Decide(", add)
        self.assertIn('SetEventValue(charInfo.guid, "add", 1, YEAR);', add)
        self.assertIn("currentBots.insert(charInfo.guid);", add)
        self.assertNotIn("CountF14ScheduledRandomBotsForQuota", add)

    def test_remove_uses_effective_total_and_excludes_only_held_from_faction_occupancy(self):
        remove = MGR.split("uint32 RandomPlayerbotMgr::RemoveRandomBots(", 1)[1].split(
            "uint32 RandomPlayerbotMgr::GetScheduledBotCount()", 1)[0]
        self.assertIn("effectiveScheduledBotCount <= maxAllowedBotCount", remove)
        self.assertIn("effectiveScheduledBotCount - maxAllowedBotCount", remove)
        self.assertIn("f14HeldBotGuids.contains(guid.GetCounter())", remove)
        self.assertLess(remove.index("f14HeldBotGuids.contains(guid.GetCounter())"),
                        remove.index("onlineAlliance++"))
        self.assertIn("if (!IsRemovableBot(bot))", remove)
        self.assertIn('SetEventValue(bot, "add", 0, 0);', remove)
        self.assertIn("currentBots.erase(bot);", remove)
        self.assertIn("LogoutPlayerBot(guid);", remove)
        self.assertIn("std::shuffle(allianceBots.begin()", remove)
        self.assertIn("std::shuffle(hordeBots.begin()", remove)

    def test_private_signatures_and_getbots_replay_boundary(self):
        self.assertIn("uint32 AddRandomBots(uint32 effectiveScheduledBotCount);", HDR)
        self.assertIn("std::unordered_set<uint32> const& f14HeldBotGuids", HDR)
        getbots = MGR.split("void RandomPlayerbotMgr::GetBots()", 1)[1].split(
            "std::vector<uint32> RandomPlayerbotMgr::GetBgBots", 1)[0]
        self.assertIn("F14HoldTbcRaceRandomBotsDuringVanilla.load(", getbots)
        self.assertLess(getbots.index("Result::HoldBeforeAddEvent"),
                        getbots.index("currentBots.insert(bot)"))
        self.assertIn("if (currentBots.size() >= maxAllowedBotCount)", getbots)

if __name__ == "__main__":
    unittest.main()
