# F14 read-only actor stamp — test-only immutable snapshot prototype (7 Nov 2026)

**Owner:** CosmicCuddle. **Maintainer:** CS01 Maintenance. **Status:** new independent test-only DRAFT. No F14 gameplay policy, runtime Playerbots/Core/IP changes, or feature activation.

## Problem and scope

The verified source `RandomPlayerbotMgr::IsRandomBot(Player*)` distinguishes SelfBots and checks account list plus membership in mutable `currentBots`. There is no proven concurrent synchronization for a Core map worker to invoke that method safely while Playerbots updates the bot set. The initial generation path also cannot assume a bot is already registered. **Neither a race-10/11 check nor an account-prefix check is sufficient.**

The files under `tests/` develop an alternative *publication protocol* for later investigation: trusted single-owner/lifecycle producer supplies nonzero GUID, account, unique incarnation, race10/11, non-self Playerbot AI with a headless session, known RandomBot account and active RandomBot membership. Any missing property **fails visible**. A writer mutex creates an immutable, per-GUID generation-stamped table, then publishes a read-only shared pointer under a brief independent publication mutex. Readers copy the shared pointer under that mutex and traverse the immutable table after unlocking; this avoids deprecated C++20 shared_ptr atomic free functions while supporting C++17. Eight mock map worker threads read only plain IDs from immutable table snapshots and never touch Playerbots' `currentBots`, a real `Player*`, or a real `WorldSession*`. A stale older epoch cannot overwrite a newer one, and a stale logout cannot evict a newer actor.

## Strict limits and additional safety gates

- The prototype is **not included, registered, built, or linked into any runtime module**. It doesn't add an engine hook, set stage8, hide a bot/NPC, filter WHO, reset quests, or mutate any user/player data.
- A real producer must first prove positive actor provenance from its **own safe owning thread**, and define when bot membership is added, when generation transitions occur, and whether account/Playerbot AI/Headless state remains authoritative through relog.
- **A reader with a previously loaded snapshot can still see a retired actor** until its snapshot read completes. A TRUE stamp is therefore only a necessary precondition, **not** authorization to hide; it must be paired with current live session/incarnation validation on Core's actual map owner and a human-viewer rights/corpse/quest/loot/targetability decision.
- The implementation uses a full copy-on-write map on every producer update. This may be too expensive for frequent bot churn on a server with many bots and **must be benchmarked** before any runtime adoption; a generation-aware striped table or a map-owner mail queue may be preferable.
- No client object ACK, cross-thread lifetime guarantee, exact historical hidden-pair restoration or grouped-player rights is supplied.
- Every stored value here is a **synthetic assertion** from test input, not an authoritative proof that a genuine RNDBOT is in the actual world.

## Tests / rollback

Five source guards verify the code remains test-only, positive provenance, generation, no unsafe game API and safe mutex publication. Native GCC/Clang C++17/C++20 normal/NDEBUG permutations and one ThreadSanitizer eight-worker run test coherence and stale identity suppression without touching owner accounts. Tests are NOT full Worldserver or 3.3.5a gameplay proof and remain pending until actual workflow conclusion checked.

The source-only change is isolated from parent [Playerbots DRAFT #1](https://github.com/CosmicCuddle/mod-playerbots/pull/1) exact `ffdeb199d4ba5a058cf2176139e7cb5c7e19755c`. It does not modify the existing custom modifications register or current F14 owner rules. Reject this child draft to roll back with zero live effect. Coordinate [F14 #9](https://github.com/CosmicCuddle/Naxxramas-Server-Coordination/issues/9), [combined test #4](https://github.com/CosmicCuddle/Naxxramas-Server-Coordination/issues/4), and [five-repo routing](https://github.com/CosmicCuddle/Naxxramas-Server-Coordination/pull/40).
