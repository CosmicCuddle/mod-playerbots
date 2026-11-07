# CosmicCuddle — F14 per-GUID commit-proof preflight (test-only, 7 Nov 2026)

**Status:** Review-only dependent DRAFT; no production source, SQL, CharacterDB access, owner host or 3.3.5a player change. **Owner:** CosmicCuddle. **Parent:** actual owner Playerbots [PB #3](https://github.com/CosmicCuddle/mod-playerbots/pull/3) exact `f209a3a09d4479e1d183ba62f591709822db32fe`; its real creation-to-login observer remains unchanged, and `Playerbots.F14TrackNewRandomBotBirth = 0` remains the distributed default.

## Real source-backed problem

- PB #3's `RandomPlayerbotFactory.cpp` calls `playerBot->SaveToDB(true,false)`, populates the character cache immediately and optionally records a *same-process* candidate. The later `CharacterDatabase.QueueSize()` drain does **not** report an individual character transaction outcome, prior quest/paid state or a durable birth receipt.
- Owner Core `src/server/game/Entities/Player/PlayerStorage.cpp` `Player::SaveToDB(bool,bool)` is **void**, internally creates a transaction then calls `CharacterDatabase.CommitTransaction(trans)`, also returning **void**. The Core `DatabaseWorkerPool.h` separately offers `TransactionCallback AsyncCommitTransaction(trans)` and `Transaction.h` gives `TransactionCallback::AfterComplete(std::function<void(bool)>)`. This does **NOT** automatically mean PB can attach that callback to the existing `SaveToDB` call. Do not change the global Core save path without a cross-module compatibility review. `QueueSize()==0` and a later login do **not** prove successful commit or first-ever character data.
- Existing [IP first-start pure model #124](https://github.com/CosmicCuddle/mod-individual-progression/pull/124) and [combined actual PB/IP trust firewall #125](https://github.com/CosmicCuddle/mod-individual-progression/pull/125) confirm no stage8 from the existing PB diagnostic. Neither proves committed birth or allows an actual stage grant.

## This test-only preflight

`tests/f14_synthetic_db_commit_receipt_model.h/.cpp` is an independent **SYNTHETIC state machine**, compiled together with the **ACTUAL PB #3 `src/Bot/F14NewRandomBotBirthProbe.h`**. The two components are separate; nothing is inserted into production, and no database/mock SQL executor exists. It forces a hypothetical future producer to supply all distinct proof stages:

1. **Pre-creation inspection:** specific GUID/account/race/unique nonce with explicit absence of an existing character record, saved quest history AND paid receipt history. Unknown or already present -> fail closed; `Begin` cannot be satisfied by race or a current bot login.
2. **Actual per-character transaction callback** outcome: a matching nonce / GUID with `success` after the pending phase, never a queue drain, cache add or `void` `SaveToDB`. False, duplicate or out-of-order callbacks -> no success or tombstone.
3. **Post-commit readback:** verify exact saved GUID/account/race and no old quest/paid history. Missing/mismatch/error -> retire the attempt, not an entitlement.
4. **Matching world admission:** real PB #3 observer must independently admit a genuinely headless, AI-present, in-world selected true RandomBot. Different race/account/nonce, repeated admission or retirement cannot be promoted.

The final model accessor is explicitly named `EvidenceCompleteForReview`, **NOT `CanGrantStage8`**. It is a synthetic review state, not a trusted transaction attestation, not durable across restarts, not safe against unproven preflight-to-commit races and NOT a runtime authorization. The test uses all-positive *fake* transaction/readback inputs only to check ordering and negative fail-closed cases. It checks failed commit, future stale success, pre/post history, already existing paid markers, wrong nonce, wrong GUID, wrong account/race, missing AI, race10/11, premature login, duplicate/replay, logout tombstones and eight concurrent workers.

**Remaining genuine engineering/design work BEFORE any runtime producer:** confirm original Core/Playerbots created-character transaction architecture, find a safe **transactionally isolated per-GUID** preflight and commit-result path, determine whether a separate callback safely executes on an owning thread and how errors/shutdown/retries recover, ensure this does not block map/world threads or alter general Player saves, verify character/quest/IP/paid row semantics and backups, implement durable (restart-safe) provenance if required, then get owner decisions for existing 0–7, cap, paid exceptions and `.ip setbot`. Acquire exact installed-source/DB inventory and build/test an approved offline copy; do not touch live database.

## Scope and evidence

Exact child change: **6 new files only** (2 test C++ files, **2** Python source guards, 1 GitHub workflow, this documentation). CI checks exact immutable PB #3 parent and no `src/`, `conf/`, `data/`, SQL, Core, IP, Naxx or Services changes, runs actual PB #3 source checks, compiles real PB3 header plus synthetic test with GCC/Clang C++17/C++20 normal/NDEBUG and ThreadSanitizer. **Do not claim CI success until its actual run is checked.** In a failed run, repair only the test/model on this isolated branch, never weaken any live guard or test preconditions to force a green result.

**Rollback:** discard this child test-only branch; nothing to roll back on host or database. Do not merge this into a release or treat source-level proof as in-game F14, paid or restoration acceptance. Related owner [F14 coordination #9](https://github.com/CosmicCuddle/Naxxramas-Server-Coordination/issues/9), [combined #4](https://github.com/CosmicCuddle/Naxxramas-Server-Coordination/issues/4), [task-routing PR #40](https://github.com/CosmicCuddle/Naxxramas-Server-Coordination/pull/40).

## Subsequent read-only CORE source verification — 7 November

The CI now checks **actual owner Core #2** at immutable `d2066943e19cd959de2a0e9897135573e4124219` (read-only checkout in a disposable GitHub runner). A new `tests/test_f14_pinned_core_save_receipt_source.py` source contract verifies the precise cross-repository call path, rather than merely relying on mock callback outcomes:

- Real Playerbots factory enqueues `Player::SaveToDB(true,false)` **before** the cache/candidate, and delays the global queue poll until *after* creating candidates.
- Real Core `Player::SaveToDB(bool,bool)` returns `void`, calls `CharacterDatabase.CommitTransaction(trans)` which enqueues `TransactionTask` with **no per-character success result**.
- Real Core worker calls `_queue->WaitAndPop(operation)` **before** `operation->call()`; `DatabaseWorkerPool::QueueSize()` returns `_queue->Size()`. A zero queue size can therefore coincide with the transaction executing on a worker, or a failed transaction.
- Separate Core `AsyncCommitTransaction` would create a `TransactionWithResultTask` with a future and `TransactionCallback::AfterComplete(bool)`; `Transaction.cpp` supplies true/false upon outcome. These APIs are **not connected** to the current PB creation save and cannot be inferred through `SaveToDB` or queue polling.
- **Scope:** only pinned public source text and Python checks, no SQL/DB connection, no new PB producer, no stage8, no paid receipt, no owner-host writes. A green source test will prove the exact pinned API expectations, **not** the safety of an unimplemented future callback integration or atomically proven old-data absence. Future Core changes should intentionally fail this contract for review instead of silently being assumed equivalent.

The hosted follow-up CI run must be checked before declaring the new six-file source contract green. This does not change the previously verified five-file synthetic-model commit and native/TSAN success at `0d15896c...`.


## 7 November integration preservation note — separate PB7 review child, NOT production

- The historical sections above faithfully describe original owner **PB #5** `eaff348a3355429eff688d5a41c371d62c24b359` on PB #3. The current new **test-only child of PB integration [DRAFT #7](https://github.com/CosmicCuddle/mod-playerbots/pull/7)** `4f6974ecec081c2006df54ff54aa4cc40cb63f55` carries the original four PB5 C++/Python test files **byte-for-byte unchanged** and adapts only this CI workflow/document's reference to the new review base.
- PB7's actual source observer header is the same Git blob `a75869910778532a235cadc54a16100a0363bd4a` as PB3 and PB6; PB7 also contains actual `PlayerbotMgr.cpp` unconditional diagnostic retirement and owner PB1's 17 earned-marker quest reset tests. The copied synthetic test still calls its fixture local variable `pb3`; this is **a local test variable, not a different linked runtime**.
- The child workflow additionally reruns PB7's 17 reset + six birth + five retirement source tests beside the exact original PB5 synthetic commit/readback tests, eight C++ compiler variants and TSAN. Workflow pins owner Core #2 for read-only save/queue API inspection, checks source/config unchanged and all new F14 flags OFF. A completed green source workflow still **cannot** prove a production per-GUID MySQL commit, durable birth, empty prior quest/paid data, stage8 entitlement, owner-installed module state or gameplay.
- All six added files are test/docs/workflow only. No actual PB source, config, IP, Core, Naxx, Services, SQL, database, client or host modified. Original PB #5 and PB #7 remain independent unmerged drafts; never silently merge/rebase, enable a paid/F14 option or deploy based on this evidence. Discard this review child for zero-host-impact rollback.
