#!/usr/bin/env python3
"""Fail-closed PB9 optional audited creation, source-only structural guards.
No DB, character, game client or owner PC is exercised here.
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
FACTORY = ROOT / "src/Bot/Factory/RandomPlayerbotFactory.cpp"
CONFIG = ROOT / "conf/playerbots.conf.dist"

def veto_body(source):
    start = source.index("if (f14AuditRequested && !f14BeforeSaveRelevantRowsAbsent)")
    end = source.index("if (f14AuditRequested && f14BeforeSaveRelevantRowsAbsent)", start)
    return source[start:end]

def is_fail_closed(block):
    return (block.count("playerBot->CleanupsBeforeDelete();") == 1
            and block.count("delete playerBot;") == 1
            and block.count("continue;") == 1
            and block.index("playerBot->CleanupsBeforeDelete();")
            < block.index("delete playerBot;") < block.index("continue;")
            and "SaveToDB(" not in block
            and "AddCharacterCacheEntry(" not in block
            and "RecordCandidate(" not in block
            and "bot_creation++" not in block
            and "DELETE FROM" not in block
            and "CharacterDatabase.Execute(" not in block)

class PreflightAbortSource(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = FACTORY.read_text(encoding="utf-8")
        cls.conf = CONFIG.read_text(encoding="utf-8")

    def test_veto_branch_after_preflight_before_any_save_or_cache(self):
        source = self.source
        begin = source.index("bool const f14AuditRequested")
        fail = source.index("if (f14AuditRequested && !f14BeforeSaveRelevantRowsAbsent)", begin)
        pass_gate = source.index("if (f14AuditRequested && f14BeforeSaveRelevantRowsAbsent)", fail)
        stock = source.index("playerBot->SaveToDB(true, false);", fail)
        cache = source.index("sCharacterCache->AddCharacterCacheEntry(", fail)
        self.assertLess(source.index("if (f14AuditRequested)", begin), fail)
        self.assertLess(source.index("CHAR_SEL_CHECK_GUID", begin), fail)
        self.assertLess(source.index("oldSettings && oldRewarded && oldQuests", begin), fail)
        self.assertLess(fail, pass_gate)
        self.assertLess(pass_gate, stock)
        self.assertLess(stock, cache)

    def test_failed_audit_must_cleanup_then_continue_without_any_write(self):
        self.assertTrue(is_fail_closed(veto_body(self.source)))

    def test_mutation_missing_continue_is_rejected(self):
        block = veto_body(self.source)
        self.assertFalse(is_fail_closed(block.replace("continue;", "", 1)))

    def test_mutation_missing_delete_is_rejected(self):
        block = veto_body(self.source)
        self.assertFalse(is_fail_closed(block.replace("delete playerBot;", "", 1)))

    def test_mutation_accidental_fallback_save_is_rejected(self):
        block = veto_body(self.source)
        self.assertFalse(is_fail_closed(block.replace("continue;", "playerBot->SaveToDB(true, false);\n                continue;", 1)))

    def test_original_default_off_save_is_still_single_and_inside_else(self):
        source = self.source
        branch = source[source.index("bool const f14AuditRequested"):
                        source.index("sCharacterCache->AddCharacterCacheEntry(",
                                     source.index("bool const f14AuditRequested"))]
        self.assertIn("sPlayerbotAIConfig.F14TrackNewRandomBotBirth &&", branch)
        self.assertIn("sPlayerbotAIConfig.F14AuditNewBirthCommitReadback &&", branch)
        self.assertIn("else\n            {\n                // Includes ALL normal/default-OFF creations", branch)
        self.assertEqual(branch.count("playerBot->SaveToDB(true, false);"), 1)
        self.assertIn("Playerbots.F14TrackNewRandomBotBirth = 0", self.conf)
        self.assertIn("Playerbots.F14AuditNewBirthCommitReadback = 0", self.conf)

    def test_no_stage8_visibility_or_destructive_sql_added(self):
        block = veto_body(self.source)
        for token in ("RewardQuest(", "ForceUpdateProgressionState(", "SetPhaseMask(",
                      "ShouldF14ExcludeWhoEntryForViewer(", "DELETE FROM",
                      "UPDATE ", "TRUNCATE", "DROP TABLE", "CharacterDatabase.Execute("):
            self.assertNotIn(token, block)
        self.assertIn("playerBot->CleanupsBeforeDelete();", block)

if __name__ == "__main__":
    unittest.main()
