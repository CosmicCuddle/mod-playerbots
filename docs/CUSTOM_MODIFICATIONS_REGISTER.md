# CosmicCuddle — Playerbots Custom Modifications Register

Owner: CosmicCuddle. Source: [CosmicCuddle/mod-playerbots](https://github.com/CosmicCuddle/mod-playerbots).
Every entry must preserve intended behavior, owning component, affected paths, pinned dependencies,
config/SQL/DBC/client, risk, tests, source/build/live status, owner decision and rollback.

## F14 previously rewarded IP stage-marker preservation — 7 November 2026
- **Intent:** optional retain only ALREADY REWARDED quest IDs 66001–66018 when the
  original PlayerbotFactory::ResetQuests executes for a positively identified
  RandomBot. This prevents an expressly selected old marker from being erased by
  that factory reset; it does NOT grant stage8, change progression tiers, hide
  bots/creatures, or protect Altbots/human players.
- **Source and provenance:** first owner-fork development branch based on historical
  Playerbots `e88cc4099e69f35126b28942a412ca702d20453b`; adapted from the
  existing two DISPOSABLE IP development prototype patches at IP
  `feb616e88fe4435edf9b8cb1e140272e5c644112`. New-fork `master`
  is a DIFFERENT upstream PB version `405f81196161017eebae3ee7a612d786a3ed2a9d`
  (43 commits newer); no merge, rebase or upgrade is authorized.
- **Affected eight paths:** `src/PlayerbotAIConfig.h`,
  `src/PlayerbotAIConfig.cpp`, `src/Bot/Factory/PlayerbotFactory.cpp`,
  `conf/playerbots.conf.dist`, `tests/test_f14_earned_marker_reset_source.py`,
  `tests/f14_earned_marker_reset_model.cpp`,
  `.github/workflows/f14-earned-marker-reset-source-20261107.yml`, this file.
- **Config:** `Playerbots.ResetQuestsPreserveQuestIds =` **EMPTY by default**;
  mask is an atomic per-config-reload snapshot. Non-66001–66018 IDs ignored;
  only a matching already-REWARDED quest is kept. No stage grant or new
  Playerbot commands; stock reset semantics with empty setting. No SQL,
  client/DBC or patch requirements; effective owner config is unverified.
- **Dependencies:** pinned Grim Core `0d754d1921261dfd939f91060ebc022eae280f71`,
  current IP progression earned-stage logic, `sRandomPlayerbotMgr.IsRandomBot(bot)`
  existing PB API, and previous IP F14 safety discussion [Coord #9](https://github.com/CosmicCuddle/Naxxramas-Server-Coordination/issues/9).
  This one API does NOT prove positive headless actor status for hiding.
- **Tests/status:** earlier source and standalone non-runtime C++ eight-variant
  GitHub Actions **PASSED** [run 42372602862](https://github.com/CosmicCuddle/mod-playerbots/actions/runs/42372602862):
  8 source/mutation tests and 327,680 pure decisions each compiler mode at
  previous head 0cf8163. Added source-only RandomBot-identity gate and
  deliberate SelfBot/account/active-tracker mutations; these require NEW
  exact-head CI evidence. Native full Worldserver, installed owner source/config/DB,
  RandomBot recreation, save/relog/stage9–18, Altbot negative cases,
  real 3.3.5a clients all **NOT RUN**. Feature disabled by default;
  DRAFT/UNMERGED; not released.
- **Risk:** malformed owner-config ids, member initialization/copy ABI,
  unexpected reset caller identity, changing bot progression inadvertently
  when enabled, bot persistence and race with config reload. A NEW source
  investigation confirmed `RandomPlayerbotMgr::IsRandomBot(Player*)`
  rejects SelfBot then delegates to the GUID overload; the GUID overload
  requires `IsInRandomAccountList` AND `currentBots.contains`.
  The membership is stored in a mutable `std::unordered_set<uint32>`,
  with insertion/removal sites in the manager and no adjacent lock visible
  in the method. **Consequence: do NOT call this method naively from a
  map-thread F14 per-viewer path**; source does NOT establish synchronization
  with bot-manager writes. The method can also return FALSE for a bot not yet
  added to `currentBots` during bootstrap. Confirm real factory-reset
  call ownership/timing and preserve already-earned tiers during setup,
  relog and later resets before enabling this opt-in. The new source
  identity tests prove **only predicate structure**, not bot-lifecycle
  completeness, a race-free value, actual headless session identity or
  3.3.5a world visibility. An immutable, positively stamped actor
  classification may be necessary for future safe Core viewer checks.
  Do not infer race/account/name alone is enough. No enablement without
  full Core/IP/Playerbots/Naxx/Services native build, owner-approved isolated
  reset/Altbot tests, threaded map safety review and explicit approval.
- **Decision/rollback:** owner requested a maintainable fork and task routing,
  not activation. Reject draft to roll back with zero live effect. A future
  approved installation must back up binaries, configs, characters database
  and recoverable rollback baseline. Never apply a reset test on owner data.


### 7 November 2026 — protected RandomBot full-reset source entry paths (CS01)
- **Owner / original behaviour:** CosmicCuddle. Preserve existing `PlayerbotFactory::Randomize(bool incremental)` semantics: full randomization invokes `ResetQuests`, incremental randomization does not. Existing `RandomPlayerbotMgr::RandomizeFirst` and `RandomizeMin` call `PlayerbotFactory::Randomize(false)`. The optional earned quest-marker keep-list remains empty/OFF by default, and still preserves only already-REWARDED selected IDs 66001–66018 for positively recognised RandomBots.
- **Affected files:** Source-only additions to `tests/test_f14_earned_marker_reset_source.py` and this append-only register. **No production C++, config, SQL, DB, DBC, client or host changes.**
- **Evidence / scope:** Five additional Python source/mutation tests enforce the factory `!incremental` quest-reset path, both manager full-reset call sites and deliberate missing-reset / inverted-condition / first/minimum becoming-incremental mutations. Exact CI status must be checked before claiming PASS. The tests are guardrails for source drift, not proof `currentBots` identity is positive at first creation, race-free during manager/map concurrency, or safe for human/Altbot stages.
- **Dependencies / risk / test / rollback:** This audit is pinned to owner Playerbots PR #1's historical first-test branch, NOT to the newer 43-commit `master`; porting or rebasing requires a separate source/SQL/behavior review. Before any feature enablement, confirm startup and reset ordering, previously REWARDED markers versus first-time stage-8 eligibility, saved 8–18 non-regression, known-off historical resets, real-human/Altbot exclusions, character database restore and owner-approved offline gameplay. No Worldserver or gameplay tests run by these source checks. Reject the isolated DRAFT to roll back; deployed game data unchanged.

## F14 new RandomBot first-created birth-to-login diagnostic — 7 November 2026

- **Intent:** acquire independent real Playerbots creation and verified-login source evidence for a later selected BE/Draenei RNDBOT first-stage pilot. **Never** grant stage8 or hide a player, creature, corpse or WHO entry. Existing behaviour remains identical with `Playerbots.F14TrackNewRandomBotBirth = 0`.
- **Owning component / branch:** Playerbots only; from owner PB #1 exact `ffdeb199d4ba5a058cf2176139e7cb5c7e19755c`; independent new DRAFT, no upstream 43-commit sync. Core/IP/Naxx/Services unchanged.
- **Affected:** `src/Bot/F14NewRandomBotBirthProbe.h` (new), `src/Bot/Factory/RandomPlayerbotFactory.cpp`, `src/Bot/PlayerbotMgr.cpp`, `src/PlayerbotAIConfig.h/.cpp`, `conf/playerbots.conf.dist`, tests, CI, this register and design memo.
- **Config/SQL/DBC/client:** new diagnostic toggle `Playerbots.F14TrackNewRandomBotBirth = 0` default OFF. No SQL or DBC/client patches. No DB writes beyond original existing bot creation's stock `SaveToDB`; any future migration requires separate owner approval.
- **Source behavior:** same-process new bot creation candidate after existing stock async SaveToDB queue + cache add, race10/11 only; one-time admission after world-thread OnBotLogin and non-null AI/headless/in-world/verified IsRandomBot checks; retire before observed logout/deletion paths. All numeric state mutex protected; reused GUID/replayed admission denied. No stage writer consumer.
- **Risks:** async character creation may fail; no restart persistence, no exact proof of committed new DB row, incomplete alternate logout/error/shutdown hooks, AI or map ownership not globally verified; `IsRandomBot` mutable manager method is only used in existing source-verified world-thread login callback (NOT Core map worker). 16,384 candidates cap and in-RAM old record tombstones. If OFF no actor map data added.
- **Tests/status:** local native standalone actual-header GCC/Clang C++17/20 **passed** before PR; dedicated GitHub source/native/TSAN CI and full Worldserver native + real bot login in 3.3.5a **pending**. DRAFT, never merged or deployed, player-visible behavior not tested.
- **Decision/rollback:** no owner authorisation to enable F14, write real stage8 or modify SQL; approval only for research/development. Discard DRAFT to roll back, no live changes. Preserve other PB reset customisations and original human progression.

## F14 holder retirement regardless of diagnostic flag — 7 November 2026

- **Owner / intended behaviour:** CosmicCuddle's Playerbots fork, real holder cleanup source. When `LogoutPlayerBot`, `DisablePlayerBot`, or `RemoveFromPlayerbotsMap` is called, unconditionally call the existing mutex-guarded `CosmicF14::NewRandomBotBirthProbe::Retire(lowGuid)` **before** access/deletion. This only revokes an existing diagnostic entry and does NOT enable or create a new candidate, grant IP stage8, affect paid entitlement or alter Core visibility.
- **Exact parent:** owner PB #3 at `f209a3a09d4479e1d183ba62f591709822db32fe`; this is a separate child development DRAFT. Existing PB #1 earned quest-marker preservation unchanged, and PB #3 config remains `Playerbots.F14TrackNewRandomBotBirth = 0`. `RecordCandidate` and `ObserveAdmission` remain gated OFF.
- **Affected source:** only `src/Bot/PlayerbotMgr.cpp`, replacing three `if (F14TrackNewRandomBotBirth)`-guarded calls with the existing unconditional `Retire`. Add one source test, one CI workflow and this register item. No `src/Bot/F14NewRandomBotBirthProbe.h` change. No SQL, account, character save, DBC, client, IP, Core or other module edits.
- **Why:** pre-consumer fail-closed precaution for a future configuration-reload/diagnostic reinitialisation sequence: if ON recorded an entry but OFF at bot logout, the old guarded cleanup could skip retiring that entry. **Current live hot-reload of this specific flag is NOT demonstrated** and no stage/visibility consumer exists, so this is a preventive source correction, not a claimed observed live bug.
- **Risk and dependencies:** calling the existing singleton/mutex lookup also on bot exits while OFF has small additional runtime overhead; it is logically a no-op if no prior candidate. Review thread safety with real holder scheduling, preserve exact bot logout ordering, verify no deadlock across map worker and first initialization. Do not activate F14. The original probe keeps tombstones for process lifetime, so this change does not fix its capacity/restart durability. None of these facts grants authentic RandomBot birth eligibility.
- **Tests / statuses:** source/regression GitHub CI and actual PB3 diagnostic-header GCC/Clang/TSAN are requested, but must check exact Actions outcome before claiming PASS. The workflow alone does **not** compile `PlayerbotMgr.cpp`; require exact five-owner Worldserver S4 integration of this new child and owner-approved offline BE/Draenei/Altbot/logout tests before real pilot. Source, tests, CI and registration are draft only, not deployed.
- **Rollback / owner decisions:** discard this child DRAFT to return to parent PB #3 with zero host/DB effects. No merge/production owner approval; do not change installed config without backups, verified module pins, restore plan, and explicit CosmicCuddle permission.



## 7 November 2026 — isolated PB1/PB6 review integration only (CS01)

- **Ownership/decision:** CosmicCuddle owns Playerbots behaviour and final approval. This new branch starts from PB #1 HEAD `135393376233556eca3426b2a4c53588a72a78bc` and copies EXACT reviewed PB #6 source/header and tests from `1d6bc91be42d7c9a9027ec2bd063c070326dbad3`; original PB #1/#3/#5/#6 DRAFT branches remain unchanged. The existing PB #1 already-REWARDED marker reset remains EMPTY/OFF and PB #6 new birth observer remains `Playerbots.F14TrackNewRandomBotBirth = 0` (OFF).
- **Affected evidence:** Preserve BOTH branches' original register entries (PB1 reset-routing and PB3/PB6 birth, retire); C++ and distributed config must be byte-identical to PB #6 for copied files. This is **not** newer 43-commit upstream Playerbots master and no upstream SQL migration is included.
- **Safety/risks:** The PB3 in-memory probe only observes a birth candidate and verified login. It cannot establish committed character DB creation or trusted no-preexisting progression, grant stage8 or hide NPCs. The PB6 unconditional retire is fail-closed on holder exit but is not tested on owner installed machine. PB1 quest reset's RandomBot identity cannot be assumed map-thread safe or positive before membership insertion.
- **Tests/rollback:** Existing PB1 17 source tests and PB3/PB6 source/native-header/TSAN guards are preserved. A new integrated CI should be exact-base fail-closed and checks all source hashes; source CI is not a full Core+PB+IP+Naxx+Services Worldserver or gameplay. The separate prior S4 native PB6 result applies only to its exact pinned tuple; do not automatically claim this branch native-passed. Reject/discard integration draft for rollback with zero host effect. Any future installation requires approved offline host inventory, restorable database/config/binary backups and in-game stage, resets, Altbot, corpse, loot and GM checks.
