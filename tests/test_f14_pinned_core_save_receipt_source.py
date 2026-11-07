#!/usr/bin/env python3
"""Pinned ACTUAL owner-Core source contract for PB #3 F14 birth proof.

This does NOT connect to any database, execute transactions or edit
production Core/PB code. Fail closed on any drift in the pinned source.
"""
from pathlib import Path
import os
import re
import unittest

PB = Path(__file__).resolve().parents[1]
CORE_ENV = os.getenv("F14_OWNER_CORE")
CORE = Path(CORE_ENV) if CORE_ENV else None


def function_body(source: str, signature: str, next_signature: str) -> str:
    start = source.index(signature)
    stop = source.index(next_signature, start + len(signature))
    return source[start:stop]


class F14OwnerCoreSaveReceiptSource(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not CORE_ENV or not CORE.is_dir():
            raise AssertionError("F14_OWNER_CORE must point to pinned Core #2 source")
        cls.factory = (PB / "src/Bot/Factory/RandomPlayerbotFactory.cpp").read_text()
        cls.playerh = (CORE / "src/server/game/Entities/Player/Player.h").read_text()
        cls.storage = (CORE / "src/server/game/Entities/Player/PlayerStorage.cpp").read_text()
        cls.poolh = (CORE / "src/server/database/Database/DatabaseWorkerPool.h").read_text()
        cls.poolcpp = (CORE / "src/server/database/Database/DatabaseWorkerPool.cpp").read_text()
        cls.worker = (CORE / "src/server/database/Database/DatabaseWorker.cpp").read_text()
        cls.txh = (CORE / "src/server/database/Database/Transaction.h").read_text()
        cls.txcpp = (CORE / "src/server/database/Database/Transaction.cpp").read_text()

    def test_factory_records_before_global_queue_poll(self):
        save = self.factory.index("playerBot->SaveToDB(true, false);")
        cache = self.factory.index("sCharacterCache->AddCharacterCacheEntry(", save)
        record = self.factory.index("NewRandomBotBirthProbe::Instance().RecordCandidate(", cache)
        drain = self.factory.index("while (CharacterDatabase.QueueSize())", record)
        self.assertLess(save, cache)
        self.assertLess(cache, record)
        self.assertLess(record, drain)
        # A pending queue is not evidence of successful transaction execution.
        self.assertNotIn("AsyncCommitTransaction(", self.factory)
        self.assertNotIn("TransactionCallback", self.factory)

    def test_normal_core_save_uses_void_commit_without_result(self):
        self.assertIn("void SaveToDB(bool create, bool logout);", self.playerh)
        self.assertIn("void SaveToDB(CharacterDatabaseTransaction trans, bool create, bool logout);",
                      self.playerh)
        body = function_body(
            self.storage,
            "void Player::SaveToDB(bool create, bool logout)",
            "void Player::SaveToDB(CharacterDatabaseTransaction trans, bool create, bool logout)",
        )
        for expected in (
            "CharacterDatabase.BeginTransaction()",
            "SaveToDB(trans, create, logout);",
            "CharacterDatabase.CommitTransaction(trans);",
        ):
            self.assertIn(expected, body)
        self.assertNotIn("AsyncCommitTransaction", body)
        self.assertNotRegex(body, r"\breturn\s+(?:true|false)\s*;")

    def test_queue_size_is_pending_count_not_in_flight_or_commit_result(self):
        self.assertIn("void CommitTransaction(SQLTransaction<T> transaction);", self.poolh)
        self.assertIn("TransactionCallback AsyncCommitTransaction(SQLTransaction<T> transaction);",
                      self.poolh)
        self.assertIn("Enqueue(new TransactionTask(transaction));", self.poolcpp)
        qbody = function_body(
            self.poolcpp,
            "std::size_t DatabaseWorkerPool<T>::QueueSize() const",
            "T* DatabaseWorkerPool<T>::GetFreeConnection()",
        )
        self.assertIn("return _queue->Size();", qbody)
        pop = self.worker.index("_queue->WaitAndPop(operation);")
        execution = self.worker.index("operation->call();", pop)
        self.assertLess(pop, execution)
        # Worker removes work item from pending queue BEFORE executing it.
        self.assertNotIn("QueueSize()", qbody.replace("QueueSize() const", ""))

    def test_separate_transaction_future_not_returned_by_normal_save(self):
        self.assertIn("TransactionWithResultTask* task = new TransactionWithResultTask(transaction);",
                      self.poolcpp)
        self.assertIn("TransactionFuture result = task->GetFuture();", self.poolcpp)
        self.assertIn("return TransactionCallback(std::move(result));", self.poolcpp)
        self.assertIn("void AfterComplete(std::function<void(bool)> callback) &", self.txh)
        self.assertIn("TransactionFuture m_future;", self.txh)
        self.assertIn("m_result.set_value(true);", self.txcpp)
        self.assertIn("m_result.set_value(false);", self.txcpp)
        self.assertIn("m_callback(m_future.get());", self.txcpp)
        self.assertIn("m_future.wait_for(0s) == std::future_status::ready", self.txcpp)

    def test_does_not_establish_durable_new_character_or_no_old_paid_data(self):
        for phrase in (
            "Playerbots.F14TrackNewRandomBotBirth = 0",
            "Playerbots.ResetQuestsPreserveQuestIds =",
        ):
            self.assertIn(phrase, (PB / "conf/playerbots.conf.dist").read_text())
        # This is only a source test; no actual quest or character receipt readback.
        self.assertNotIn("RecordCandidate(", self.storage)
        self.assertNotIn("F14", self.txh)


if __name__ == "__main__":
    unittest.main()
