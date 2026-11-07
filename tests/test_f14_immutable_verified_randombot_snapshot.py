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
    def test_atomic_publication_load(self):
        self.assertEqual(self.header.count("std::atomic_load_explicit(&_snapshot, std::memory_order_acquire)"), 4)
        self.assertEqual(self.header.count("std::atomic_store_explicit(&_snapshot, std::move(published), std::memory_order_release)"), 2)
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
