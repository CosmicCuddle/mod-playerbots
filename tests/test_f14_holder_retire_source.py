#!/usr/bin/env python3
"""Default-OFF PB3 F14 birth diagnostic: fail-closed retirement across config changes.

Inspects the actual holder call sites. This is source contract, NOT a proof
that a production config toggle is currently possible or a live bot logout.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
HOLDER = ROOT / "src/Bot/PlayerbotMgr.cpp"
PROBE = ROOT / "src/Bot/F14NewRandomBotBirthProbe.h"
FACTORY = ROOT / "src/Bot/Factory/RandomPlayerbotFactory.cpp"
CONF = ROOT / "conf/playerbots.conf.dist"

RETIRE = "CosmicF14::NewRandomBotBirthProbe::Instance().Retire(guid.GetCounter());"


def method(source, opening, ending):
    return source.split(opening, 1)[1].split(ending, 1)[0]


class HolderAdmissionRetirement(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.holder = HOLDER.read_text(encoding="utf-8")
        cls.probe = PROBE.read_text(encoding="utf-8")
        cls.factory = FACTORY.read_text(encoding="utf-8")
        cls.conf = CONF.read_text(encoding="utf-8")
        cls.logout = method(cls.holder,
                            "void PlayerbotHolder::LogoutPlayerBot(ObjectGuid guid)",
                            "void PlayerbotHolder::DisablePlayerBot(ObjectGuid guid)")
        cls.disable = method(cls.holder,
                             "void PlayerbotHolder::DisablePlayerBot(ObjectGuid guid)",
                             "void PlayerbotHolder::RemoveFromPlayerbotsMap(ObjectGuid guid)")
        cls.remove = method(cls.holder,
                            "void PlayerbotHolder::RemoveFromPlayerbotsMap(ObjectGuid guid)",
                            "Player* PlayerbotHolder::GetPlayerBot(")
        cls.login = method(cls.holder,
                           "void PlayerbotHolder::OnBotLogin(Player* const bot)",
                           "void PlayerbotHolder::")

    def test_all_three_removal_paths_revoke_before_player_lookup_or_erase(self):
        for name, block, before in (
            ("LogoutPlayerBot", self.logout, "GetPlayerBot(guid)"),
            ("DisablePlayerBot", self.disable, "GetPlayerBot(guid)"),
            ("RemoveFromPlayerbotsMap", self.remove, "playerBots.erase(guid);"),
        ):
            with self.subTest(name=name):
                self.assertEqual(block.count(RETIRE), 1)
                self.assertLess(block.index(RETIRE), block.index(before))
        self.assertEqual(self.holder.count(RETIRE), 3)

    def test_retirement_does_not_depend_on_current_diagnostic_toggle(self):
        for name, block in (
            ("LogoutPlayerBot", self.logout),
            ("DisablePlayerBot", self.disable),
            ("RemoveFromPlayerbotsMap", self.remove),
        ):
            with self.subTest(name=name):
                # No stale ON -> OFF -> logout admission must survive merely
                # because a birth *recording* toggle became OFF meanwhile.
                self.assertNotIn("sPlayerbotAIConfig.F14TrackNewRandomBotBirth", block)
                self.assertRegex(
                    block,
                    r"(?m)^    CosmicF14::NewRandomBotBirthProbe::Instance"
                    r"\(\)\.Retire\(guid\.GetCounter\(\)\);$",
                )

    def test_other_bot_admission_and_creation_still_default_off(self):
        self.assertIn("Playerbots.F14TrackNewRandomBotBirth = 0", self.conf)
        self.assertIn("if (sPlayerbotAIConfig.F14TrackNewRandomBotBirth)",
                      self.factory)
        self.assertIn("if (sPlayerbotAIConfig.F14TrackNewRandomBotBirth &&",
                      self.login)
        self.assertIn("sRandomPlayerbotMgr.IsRandomBot(bot)", self.login)
        self.assertIn("bot->GetSession()->IsHeadless()", self.login)
        self.assertIn("bot->IsInWorld()", self.login)

    def test_existing_probe_retirer_is_mutex_guarded_idempotent_tombstone(self):
        block = method(self.probe, "void Retire(std::uint32_t guid)", "bool HasUnretiredAdmission(")
        self.assertIn("std::lock_guard<std::mutex> lock(_lock);", block)
        self.assertIn("_records.find(guid)", block)
        self.assertIn("it->second.Retired = true;", block)
        self.assertNotIn("RecordCandidate(", block)
        self.assertIn("!it->second.Retired", self.probe)
        self.assertIn("if (_records.size() >= MaxCandidates", self.probe)

    def test_no_new_entitlement_quest_npc_or_db_writers(self):
        # The only reviewed source modification is these 3 pre-existing
        # holder removals; ensure it is not relabelled as a stage/loot fix.
        for forbidden in (
            "ForceUpdateProgressionState(", "CharacterDatabase.AsyncQuery(",
            "CharacterDatabase.CommitTransaction(", "ShouldF14SuppressCreatureForViewer(",
            "SendUpdateToPlayer(",
        ):
            self.assertNotIn(forbidden, self.logout + self.disable + self.remove)


if __name__ == "__main__":
    unittest.main()
