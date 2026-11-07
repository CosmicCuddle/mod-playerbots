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
