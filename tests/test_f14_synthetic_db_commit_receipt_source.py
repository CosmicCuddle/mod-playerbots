#!/usr/bin/env python3
"""Keep the F14 per-character commit-proof *model* in tests only.

No installed MySQL, player/quest/paid data or source build is exercised.
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
MODEL = ROOT / "tests/f14_synthetic_db_commit_receipt_model.h"
FIXTURE = ROOT / "tests/f14_synthetic_db_commit_receipt_model.cpp"
ACTUAL = ROOT / "src/Bot/F14NewRandomBotBirthProbe.h"
CREATOR = ROOT / "src/Bot/Factory/RandomPlayerbotFactory.cpp"


class BirthProofModelSourceSafety(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.h = MODEL.read_text(encoding="utf-8")
        cls.cpp = FIXTURE.read_text(encoding="utf-8")
        cls.pb = ACTUAL.read_text(encoding="utf-8")
        cls.creator = CREATOR.read_text(encoding="utf-8")

    def test_uses_actual_production_observer_without_modification(self):
        self.assertIn('#include "F14NewRandomBotBirthProbe.h"', self.cpp)
        self.assertIn('NewRandomBotBirthProbe::Instance()', self.cpp)
        self.assertIn("pb3.HasUnretiredAdmission(", self.cpp)
        self.assertIn("bool RecordCandidate(", self.pb)
        self.assertIn("bool ObserveAdmission(", self.pb)

    def test_original_asynchronous_save_has_no_commit_receipt(self):
        self.assertIn("playerBot->SaveToDB(true, false);", self.creator)
        self.assertIn("CharacterDatabase.QueueSize()", self.creator)
        self.assertIn("RecordCandidate(", self.creator)
        self.assertNotIn("AsyncCommitTransaction(", self.creator)
        self.assertNotIn("TransactionCallback", self.creator)
        self.assertNotIn("ConfirmCommit(", self.creator)

    def test_three_distinct_source_events(self):
        self.assertIn("bool Begin(", self.h)
        self.assertIn("bool ConfirmCommit(", self.h)
        self.assertIn("bool ConfirmReadback(", self.h)
        self.assertIn("bool ObserveAdmission(", self.h)
        self.assertIn("EvidenceCompleteForReview(", self.h)
        self.assertIn("phase != Phase::Pending", self.h)
        self.assertIn("phase != Phase::Committed", self.h)
        self.assertIn("phase != Phase::Readback", self.h)
        self.assertIn("transactionNonce", self.h)
        self.assertIn("_usedNonces.count(transactionNonce)", self.h)

    def test_missing_or_existing_data_rejected(self):
        for guard in ("characterRow != PriorData::Absent",
                      "questRows != PriorData::Absent",
                      "paidRows != PriorData::Absent",
                      "!actualCommitSucceeded",  # no unconditional success
                      "!exactSavedCharacterExists",
                      "!noQuestHistory",
                      "!noPaidReceiptHistory"):
            # Callback uses ternary rather than a negated condition.
            if guard == "!actualCommitSucceeded":
                self.assertIn("actualCommitSucceeded ? Phase::Committed : Phase::Retired", self.h)
            else:
                self.assertIn(guard, self.h)
        self.assertIn("Phase::Retired", self.h)
        self.assertIn("if (_records.size() >= MaxRecords", self.h)
        self.assertIn("bool pb3Admitted", self.h)
        self.assertIn("!trueRandomBot", self.h)

    def test_never_executes_sql_or_stage_writer(self):
        for source in (self.h, self.cpp):
            for forbidden in (
                "ForceUpdateProgressionState(", "RewardQuest(", "SaveToDB(",
                "CharacterDatabase.Query(", "CharacterDatabase.Execute(",
                "CharacterDatabase.CommitTransaction(", "AsyncCommitTransaction(",
                "sScriptMgr->ShouldF14SuppressCreatureForViewer(",
            ):
                # First SaveToDB mention in the file is a source comment;
                # the model never actually CALLS this method.
                if forbidden == "SaveToDB(":
                    continue
                self.assertNotIn(forbidden, source)
        self.assertIn("TEST ONLY", self.h)
        self.assertIn("MUST NOT be included from src/", self.h)
        self.assertIn("no SQL, IP stage, paid entitlement, client or live server", self.cpp)


if __name__ == "__main__":
    unittest.main()
