#!/usr/bin/env python3
"""Source/mutation contracts for disabled-by-default earned RNDBOT marker retention.

NO character DB, bot spawning, stage grant, saved quest mutation or live tests.
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
C = ROOT / "conf/playerbots.conf.dist"
H = ROOT / "src/PlayerbotAIConfig.h"
CFG = ROOT / "src/PlayerbotAIConfig.cpp"
FACTORY = ROOT / "src/Bot/Factory/PlayerbotFactory.cpp"
RANDOM_MGR = ROOT / "src/Bot/RandomPlayerbotMgr.cpp"


def audit(config, hdr, cpp, factory):
    if config.count("Playerbots.ResetQuestsPreserveQuestIds =\n") != 1:
        raise ValueError("nonempty or absent default config")
    if hdr.count("std::atomic<uint32> ResetQuestsPreserveMask{0};") != 1:
        raise ValueError("missing atomic safe default")
    for token in (
        'GetOption<std::string>("Playerbots.ResetQuestsPreserveQuestIds", "")',
        "if (!resetPreserveIds.empty())",
        "if (id >= 66001 && id <= 66018)",
        "markerMask |= uint32(1) << (id - 66001);",
        "ResetQuestsPreserveMask.store(markerMask, std::memory_order_release);",
    ):
        if cpp.count(token) != 1:
            raise ValueError("config parser drift: " + token)
    start = "void PlayerbotFactory::ResetQuests()"
    end = "uint32 PlayerbotFactory::InitTalentsTree"
    if factory.count(start) != 1 or factory.count(end) != 1:
        raise ValueError("ambiguous factory method")
    body = factory.split(start, 1)[1].split(end, 1)[0]
    expected = (
        "ResetQuestsPreserveMask.load(std::memory_order_acquire)",
        "preserveMask != 0 && sRandomPlayerbotMgr.IsRandomBot(bot)",
        "entry >= 66001 && entry <= 66018",
        "bot->GetQuestStatus(entry) == QUEST_STATUS_REWARDED",
        "(preserveMask & (uint32(1) << (entry - 66001))) != 0",
        "bot->RemoveRewardedQuest(entry);",
        "bot->RemoveActiveQuest(entry, false);",
    )
    for token in expected:
        if body.count(token) != 1:
            raise ValueError("reset policy drift: " + token)
    if not (body.index(expected[0]) < body.index(expected[1]) <
            body.index("for (uint8 slot") < body.index(expected[2]) <
            body.index(expected[3]) < body.index(expected[4]) <
            body.index(expected[5]) < body.index(expected[6])):
        raise ValueError("reset guard after mutation or pre-snapshot")
    if "RewardQuest(" in body or "ForceUpdateProgression" in body:
        raise ValueError("never grant stages during preservation")
    return True


def audit_randombot_identity(mgr):
    """Require BOTH account ownership and active-bot membership, after SelfBot veto.

    This source proof is NOT a concurrency/worker ownership or startup ordering test.
    """
    player_start = "bool RandomPlayerbotMgr::IsRandomBot(Player* bot)"
    guid_start = "bool RandomPlayerbotMgr::IsRandomBot(ObjectGuid::LowType bot)"
    end = "bool RandomPlayerbotMgr::IsAddclassBot(Player* bot)"
    if any(mgr.count(marker) != 1 for marker in (player_start, guid_start, end)):
        raise ValueError("ambiguous RandomBot identity method")
    player = mgr.split(player_start, 1)[1].split(guid_start, 1)[0]
    guid = mgr.split(guid_start, 1)[1].split(end, 1)[0]
    if player.count("if (IsSelfBot(bot))") != 1:
        raise ValueError("SelfBot exclusion is required")
    if player.count("return IsRandomBot(bot->GetGUID().GetCounter());") != 1:
        raise ValueError("must delegate to account AND active membership identity")
    if guid.count("if (!sPlayerbotAIConfig.IsInRandomAccountList(") != 1:
        raise ValueError("RandomBot account ownership required")
    if guid.count("return currentBots.contains(bot);") != 1:
        raise ValueError("active RandomBot membership required")
    if player.index("if (IsSelfBot(bot))") > player.index(
        "return IsRandomBot(bot->GetGUID().GetCounter());"
    ):
        raise ValueError("SelfBot exclusion must precede identity return")
    if guid.index("if (!sPlayerbotAIConfig.IsInRandomAccountList(") > guid.index(
        "return currentBots.contains(bot);"
    ):
        raise ValueError("account check must precede active membership check")
    return True


class SourceGate(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.conf = C.read_text()
        cls.hdr = H.read_text()
        cls.cpp = CFG.read_text()
        cls.factory = FACTORY.read_text()
        cls.manager = RANDOM_MGR.read_text()

    def test_exact_source(self):
        self.assertTrue(audit(self.conf, self.hdr, self.cpp, self.factory))

    def test_source_identity_requires_account_and_active_membership(self):
        self.assertTrue(audit_randombot_identity(self.manager))

    def test_reject_selfbot_bypass(self):
        with self.assertRaises(ValueError):
            audit_randombot_identity(self.manager.replace("if (IsSelfBot(bot))",
                                                          "if (false)", 1))

    def test_reject_account_only_identity(self):
        with self.assertRaises(ValueError):
            audit_randombot_identity(self.manager.replace(
                "return currentBots.contains(bot);", "return true;", 1))

    def test_reject_active_only_identity(self):
        with self.assertRaises(ValueError):
            audit_randombot_identity(self.manager.replace(
                "if (!sPlayerbotAIConfig.IsInRandomAccountList(", "if (false && (", 1))

    def test_default_enable_rejected(self):
        with self.assertRaises(ValueError):
            audit(self.conf.replace("Playerbots.ResetQuestsPreserveQuestIds =\n",
                                    "Playerbots.ResetQuestsPreserveQuestIds = 66008\n", 1),
                  self.hdr, self.cpp, self.factory)

    def test_altbot_widening_rejected(self):
        with self.assertRaises(ValueError):
            audit(self.conf, self.hdr, self.cpp,
                  self.factory.replace("preserveMask != 0 && sRandomPlayerbotMgr.IsRandomBot(bot)",
                                       "preserveMask != 0", 1))

    def test_incomplete_quest_preservation_rejected(self):
        with self.assertRaises(ValueError):
            audit(self.conf, self.hdr, self.cpp,
                  self.factory.replace("bot->GetQuestStatus(entry) == QUEST_STATUS_REWARDED",
                                       "bot->GetQuestStatus(entry) != QUEST_STATUS_NONE", 1))

    def test_out_of_range_marker_rejected(self):
        with self.assertRaises(ValueError):
            audit(self.conf, self.hdr, self.cpp,
                  self.factory.replace("entry >= 66001 && entry <= 66018",
                                       "entry >= 66000 && entry <= 66019", 1))

    def test_no_implicit_grant(self):
        with self.assertRaises(ValueError):
            audit(self.conf, self.hdr, self.cpp,
                  self.factory.replace("bot->RemoveActiveQuest(entry, false);",
                                       "bot->RewardQuest(entry);\n        bot->RemoveActiveQuest(entry, false);", 1))

    def test_atomic_publication_required(self):
        with self.assertRaises(ValueError):
            audit(self.conf, self.hdr,
                  self.cpp.replace("std::memory_order_release", "std::memory_order_relaxed", 1),
                  self.factory)

    def test_atomic_read_required(self):
        with self.assertRaises(ValueError):
            audit(self.conf, self.hdr, self.cpp,
                  self.factory.replace("std::memory_order_acquire", "std::memory_order_relaxed", 1))


if __name__ == "__main__":
    unittest.main()
