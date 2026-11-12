#!/usr/bin/env python3
"""Owner PB7 exact native source guard for optional F14 commit+readback audit.

Never equate callback+row evidence with first birth/old orphan data absence,
IP stage8, WHO filter or NPC hide. All audit and tracking flags default OFF.
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


def source(path):
    return (ROOT / path).read_text(encoding="utf-8")


class CommitReadbackSource(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.factory = source("src/Bot/Factory/RandomPlayerbotFactory.cpp")
        cls.probe = source("src/Bot/F14NewRandomBotBirthProbe.h")
        cls.h = source("src/PlayerbotAIConfig.h")
        cls.cpp = source("src/PlayerbotAIConfig.cpp")
        cls.config = source("conf/playerbots.conf.dist")

    def test_both_flags_off_by_default_independent(self):
        self.assertIn("bool F14AuditNewBirthCommitReadback = false;", self.h)
        self.assertIn('GetOption<bool>("Playerbots.F14AuditNewBirthCommitReadback", false)', self.cpp)
        self.assertIn("Playerbots.F14AuditNewBirthCommitReadback = 0", self.config)
        self.assertIn("Playerbots.F14TrackNewRandomBotBirth = 0", self.config)
        self.assertNotIn("Playerbots.F14AuditNewBirthCommitReadback = 1", self.config)

    def test_original_default_stock_save_unmodified(self):
        a = self.factory.index("bool const f14AuditRequested")
        b = self.factory.index("sCharacterCache->AddCharacterCacheEntry(", a)
        block = self.factory[a:b]
        self.assertIn("sPlayerbotAIConfig.F14TrackNewRandomBotBirth &&", block)
        self.assertIn("sPlayerbotAIConfig.F14AuditNewBirthCommitReadback &&", block)
        self.assertIn("(f14Race == RACE_BLOODELF || f14Race == RACE_DRAENEI)", block)
        self.assertIn("else\n            {\n                // Includes ALL normal/default-OFF creations", block)
        self.assertIn("playerBot->SaveToDB(true, false);", block)
        self.assertEqual(block.count("playerBot->SaveToDB(true, false);"), 1)

    def test_preexisting_guid_veto_and_exact_personal_commit(self):
        a = self.factory.index("bool const f14AuditRequested")
        b = self.factory.index("sCharacterCache->AddCharacterCacheEntry(", a)
        block = self.factory[a:b]
        for expected in (
            "GetPreparedStatement(CHAR_SEL_CHECK_GUID)",
            "if (!CharacterDatabase.Query(pre))",
            "if (f14AuditRequested && f14BeforeSaveRelevantRowsAbsent)",
            "SELECT COUNT(*) FROM character_settings WHERE guid = {}",
            "SELECT COUNT(*) FROM character_queststatus_rewarded WHERE guid = {}",
            "SELECT COUNT(*) FROM character_queststatus WHERE guid = {}",
            "oldSettings && oldRewarded && oldQuests",
            "oldSettings->Fetch()[0].Get<uint64>() == 0",
            "oldRewarded->Fetch()[0].Get<uint64>() == 0",
            "oldQuests->Fetch()[0].Get<uint64>() == 0",
            "CharacterDatabase.BeginTransaction()",
            "playerBot->SaveToDB(birthTrans, true, false);",
            "CharacterDatabase.AsyncCommitTransaction(birthTrans)",
            "receipt.AfterComplete(",
            "if (!committed)",
            "GetPreparedStatement(CHAR_SEL_DATA_BY_GUID)",
            "PreparedQueryResult row = CharacterDatabase.Query(post);",
            "if (!row)",
            "fields[0].Get<uint32>() != f14Guid",
            "fields[1].Get<uint32>() != accountId",
            "fields[2].Get<std::string>() != expectedName",
            "fields[4].Get<uint8>() != f14Race",
            "MarkCommittedRowReadback(",
        ):
            self.assertIn(expected, block)
        self.assertLess(block.index("CHAR_SEL_CHECK_GUID"), block.index("SaveToDB(birthTrans"))
        self.assertLess(block.index("SaveToDB(birthTrans"), block.index("AsyncCommitTransaction("))
        self.assertLess(block.index("AsyncCommitTransaction("), block.index("MarkCommittedRowReadback("))

    def test_missing_failed_or_old_saved_tables_all_veto_audit(self):
        section = self.factory[
            self.factory.index("if (f14AuditRequested)"):
            self.factory.index("if (f14AuditRequested && f14BeforeSaveRelevantRowsAbsent)")
        ]
        self.assertIn("if (!CharacterDatabase.Query(pre))", section)
        for table in ("character_settings", "character_queststatus_rewarded",
                      "character_queststatus"):
            self.assertIn("SELECT COUNT(*) FROM " + table + " WHERE guid = {}", section)
        for result in ("oldSettings", "oldRewarded", "oldQuests"):
            self.assertIn(result + " &&", section)
            self.assertIn(result + "->Fetch()[0].Get<uint64>() == 0", section)
        self.assertNotIn("DELETE FROM", section)
        self.assertNotIn("UPDATE ", section)

    def test_callback_is_pointer_free_and_polled_after_stock_queue(self):
        a = self.factory.index("receipt.AfterComplete(")
        b = self.factory.index("f14BirthAuditCallbacks.emplace_back", a)
        block = self.factory[a:b]
        self.assertIn("[f14Guid, accountId, f14Race, expectedName]", block)
        self.assertNotIn("[this", block)
        self.assertNotIn("playerBot->", block)
        self.assertIn("for (TransactionCallback& receipt : f14BirthAuditCallbacks)", self.factory)
        self.assertLess(
            self.factory.index("while (CharacterDatabase.QueueSize())", self.factory.index("if (bot_creation)")),
            self.factory.index("for (TransactionCallback& receipt : f14BirthAuditCallbacks)"),
        )
        self.assertLess(
            self.factory.index("for (TransactionCallback& receipt : f14BirthAuditCallbacks)"),
            self.factory.index("for (WorldSession* session : sessionBots)"),
        )

    def test_evidence_separate_from_one_use_actor_admission(self):
        p = self.probe
        self.assertIn("bool MarkCommittedRowReadback(", p)
        self.assertIn("bool HasAuditedRowAndUnretiredAdmission(", p)
        self.assertIn("it->second.CommittedRowReadback", p)
        self.assertIn("it->second.Consumed && !it->second.Retired", p)
        self.assertIn("it->second.Retired ||", p)
        for blocked in ("ForceUpdateProgressionState(", "RewardQuest(", "SetPhaseMask(",
                        "SendUpdateToPlayer(", "SaveToDB(", "CommitTransaction("):
            self.assertNotIn(blocked, p.split("namespace CosmicF14", 1)[1])
        self.assertNotIn("ForceUpdateProgressionState(", self.factory)
        self.assertNotIn("ForceUpdateProgressionState(", source("src/Bot/PlayerbotMgr.cpp"))

    def test_no_schema_sql_file_or_destructive_changes(self):
        for bad in ("DELETE FROM characters", "TRUNCATE", "DROP TABLE",
                    "ALTER TABLE", "CREATE TABLE", "SetPhaseMask("):
            block = self.factory[
                self.factory.index("// Keep the original save path EXACTLY"):
                self.factory.index("playerBot->CleanupsBeforeDelete();",
                                   self.factory.index("// Keep the original save path EXACTLY"))
            ]
            self.assertNotIn(bad, block)
        self.assertIn("Playerbots.DeleteRandomBotAccounts = 0", self.config)


if __name__ == "__main__":
    unittest.main()
