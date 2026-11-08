# CosmicCuddle F14 — new RandomBot per-GUID commit/readback audit (8 November 2026)

**State:** isolated owner Playerbots review branch, **OFF by default**, no release/merge/deployment approval. Existing [PB #7](https://github.com/CosmicCuddle/mod-playerbots/pull/7) `4f6974ecec081c2006df54ff54aa4cc40cb63f55` is left intact. The original new-bot creation route is unchanged unless **both** `Playerbots.F14TrackNewRandomBotBirth=1` and new `Playerbots.F14AuditNewBirthCommitReadback=1` on a separately approved disposable environment.

## Real Core source justification

- Owner AzerothCore `PlayerStorage.cpp::Player::SaveToDB(bool,bool)` enqueues a transaction with a **void** `CharacterDatabase.CommitTransaction`; queue drainage and CharacterCache insertion do **not** acknowledge per-GUID commit outcome.
- The same owner Core supports `Player::SaveToDB(CharacterDatabaseTransaction,bool,bool)`, `CharacterDatabase.AsyncCommitTransaction(trans)`, and movable `TransactionCallback::AfterComplete(bool)` plus `InvokeIfReady()`. Stock human character creation uses the same transaction/future pattern via session lifecycle. The optional Playerbots branch instead holds only pointer-free callbacks until its existing creation queue drains and polls ready results.
- Existing `CHAR_SEL_CHECK_GUID` is a synchronous prepared pre-save GUID presence check; `CHAR_SEL_DATA_BY_GUID` provides GUID, account, name, gender, race, class and level. The optional callback performs an independent synchronous positive post-save readback of GUID/account/name/race before recording a **process-local observation bit**. Nonready callback results are deliberately discarded rather than treated as success.

## Important: what it does NOT establish

The combined evidence is stronger than the old attempt-only `RecordCandidate`, but is **not a complete first-birth entitlement**. It does not establish the absence of orphaned old character quest rewards, character settings, paid expansion receipts, or old linked rows when a GUID is reused; a live DB delete/reuse workflow can leave those behind. It cannot establish future persistence across a restart, protected run epoch, positive genuine RNDBOT account ownership from DB or complete negative human rights. It does not grant stage 8, update quests, modify a saved character, hide a human/Altbot or NPC, suppress WHO, write custom SQL, modify race/phase, or offer a production feature. It leaves the original five-repository native compatibility and owner in-client test gates outstanding.

## Review and testing

Existing PB #7 earned marker, birth observer and holder retirement tests must remain intact. Added source guards check OFF defaults, original SaveToDB fallthrough, synchronous preflight, individually acknowledged transaction, exact row readback, bounded callback lifetime, no raw or destructive SQL, and no stage/visibility writers; a standalone C++ test exercises the actual same-process evidence state. Exact owner PB C++ **and five-owner Worldserver** compilation, disposable DB failure/restart/rollback test and positive-first-birth oracle are unverified until independently run.

**Rollback:** discard the new branch; PB #7 preserved. Never perform owner-host Git operations, SQL cleanup/updates or test installation without owner-approved inventory of ignored/untracked custom SQL/cleanup files, other installed modules, effective `Playerbots.DeleteRandomBotAccounts=0`, all source/config/DBC/client/binaries and proven recoverable DB backup. No live-host or data operations were performed while drafting this change.
