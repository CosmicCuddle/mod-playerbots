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
- **Tests/status:** source and standalone non-runtime C++ eight variants CI
  proposed; status PENDING until verified actual Actions. Native full
  Worldserver, installed owner source/config/DB, RandomBot recreation,
  save/relog/stage9–18, Altbot negative cases, real 3.3.5a clients all
  **NOT RUN**. Feature disabled by default; DRAFT/UNMERGED; not released.
- **Risk:** malformed owner-config ids, member initialization/copy ABI,
  unexpected reset caller identity, changing bot progression inadvertently
  when enabled, bot persistence and race with config reload. No enablement
  without additional genuine Core/IP/Playerbots/Naxx/Services native build,
  specific tests and owner approval.
- **Decision/rollback:** owner requested a maintainable fork and task routing,
  not activation. Reject draft to roll back with zero live effect. A future
  approved installation must back up binaries, configs, characters database
  and recoverable rollback baseline. Never apply a reset test on owner data.
