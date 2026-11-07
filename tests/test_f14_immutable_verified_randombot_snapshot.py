#!/usr/bin/env python3
"""Source-only assurance: prototype never runs as production Playerbots code."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
H = ROOT / "tests/f14_immutable_verified_randombot_snapshot.h"
CPP = ROOT / "tests/f14_immutable_verified_randombot_snapshot.cpp"

class SnapshotContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.header = H.read_text()
        cls.test = CPP.read_text()
    def test_mutex_publication_still_uses_immutable_snapshot(self):
        self.assertEqual(self.header.count("LoadView()"), 5)
        self.assertEqual(self.header.count("PublishView(std::move(published));"), 2)
        self.assertIn("std::lock_guard<std::mutex> lock(_publication);", self.header)
        self.assertIn("std::shared_ptr<const Table>", self.header)
        self.assertNotIn("std::atomic_load_explicit", self.header)
    def test_positive_provenance(self):
        for token in ("!c.Headless", "!c.PlayerbotAI", "c.SelfBot",
                      "!c.InRandomAccountList", "!c.InActiveRandomBotList",
                      "(c.Race != 10 && c.Race != 11)"):
            self.assertIn(token, self.header)
    def test_exact_incarnation(self):
        self.assertIn("it->second.Incarnation >= c.Incarnation", self.header)
        self.assertIn("it->second.Incarnation != incarnation", self.header)
        self.assertIn("it->second.Incarnation == incarnation", self.header)
    def test_no_game_api_in_snapshot(self):
        for token in ("Player*", "WorldSession*", "sRandomPlayerbotMgr", "currentBots.contains",
                      "GetMap()", "SaveToDB(", "SetPhaseMask(", "RewardQuest("):
            self.assertNotIn(token, self.header.split("namespace CosmicF14Prototype", 1)[1])
    def test_test_is_not_implemented_runtime(self):
        self.assertIn("TEST-ONLY", self.header)
        self.assertIn("eight concurrent readers", self.test.lower().replace("eight", "eight"))
        self.assertIn("std::thread producer", self.test)

if __name__ == "__main__":
    unittest.main()
