# PB59 — staged published-session ownership and custody handoff, 15 November 2026

Owner: CosmicCuddle. Maintenance CS01. Development-only candidate, not runtime activation.

## Exact parents
- PB #33 c4f8c97dc604f818fdf1b07fd97a89a2ef8c1a5a, #32 31b172e51325b8bc3d3df4ec18c5c1d63f5e41d8.
- Paired Core #38 1e12bf841fe1415294940255ac98a8bfdb28f754. Original #37 b7d50628 / #29 88125624 remain unchanged.
- Child feature/cs01-pb59-published-session-owner-20261115.

## Implemented adapter
AutoGearPublishedSessionOwner composes the prior PendingSessionOwner, RecoveryRegistry and SessionCustody into an actual exclusive unique_ptr transfer. It reserves the independent published slot before transfer, checks GUID, Core Player incarnation, holder tag and native session incarnation; failed reservation/publication leaves pending ownership. TransferPublished moves to an empty destination without constructing a unique_ptr from a borrowed pointer. The process-lifetime published store owns the session independent of manager lifetime. A detached holder is reported as Orphaned, not proof of replay authority.

All pointer-touching transfers require the already-bound SessionCustody world-owner thread (new IsOwnerThread predicate; no implicit binding). The handoff requires the actual exact active Logout journal Reservation (OwnsInProgress), not a diagnostic snapshot; it transfers that reservation together with the unique_ptr into custody. The old published entry is a fence, no longer a second session owner. Unlink is a one-shot native adapter callback contract; it must actually erase the exact holder reference and quiesce native users before returning true. This is NOT proof that a supplied callback did so. Native Core retirement lease and serialized native lifetime proof remain required. Logout cannot start before unlink acknowledgement, cannot replay after a throw/veto, and detached deletion requires actual normal native return. The handoff marks journal steps and completes its producer only after actual custody deletion. A dropped/failed ticket retains an unresolved fence and custody; no implicit destructor logout, no fabricated durable acknowledgement.

Proposed conservative policy: a GUID with any unresolved published/custody entry blocks republication even with a replacement incarnation. This is staged fail-closed behavior, not owner-approved live gameplay policy. Requires bounded reconciliation, operator workflow and owner review. Successful exact deletion/journal completion clears the fence. Pending publication stopping rejects new reservations and transfers, reports outstanding count, but does NOT implement a native shutdown drain.

## Evidence
Exact tested blob identities, matched to GitHub:
- src/Bot/AutoGearPublishedSessionOwner.h 8b882c7f35949dd6a234a1623024d5e15a25c9ae.
- src/Bot/AutoGearPendingSessionOwner.h 11d096ae9bee348aa6b1de26e53aacbb5d168180 (additive transfer API).
- src/Bot/AutoGearSessionCustody.h f9409361461093d93b269402a39e5ec624098e01 (additive owner-thread query).
- src/Bot/AutoGearRecoveryRegistry.h ad04a2b78666371b6e072b8a0f9469af5d496e1d (additive exact active journal check).
- tests/pb59_published_session_owner.cpp a210208d3d7ad05f4cb6d36373c73f1acdaafca6.

New fixture GCC, Clang, Clang ASan+UBSan 3/3 PASS. Combined recompilation and execution: new fixture + existing pending owner + existing exact native pending/queue extracts + existing custody + existing session identity = 15/15 PASS. C++20 -Wall -Wextra -Werror -pthread; sanitizer -fsanitize=address,undefined. Prior native extracts retain their #33 evidence scope and synthetic native dependencies. No full native build/DB/gameplay/TSAN. Reachable process-lifetime retained entries intentionally simulate unresolved sessions; sanitizer success is NOT proof of bounded recovery/shutdown.

Reproduce new fixture from repository root:
    g++ -std=c++20 -Wall -Wextra -Werror -pthread -Isrc/Bot tests/pb59_published_session_owner.cpp -o /tmp/pb59-published && /tmp/pb59-published
    clang++ -std=c++20 -Wall -Wextra -Werror -pthread -fsanitize=address,undefined -Isrc/Bot tests/pb59_published_session_owner.cpp -o /tmp/pb59-published-san && /tmp/pb59-published-san

## Native integration gates (source verified, no native callers changed)
At PB #33:
- PlayerbotMgr.cpp blob 05676df8ca7d485af7e466ad8624164ff933034a, OnBotLogin lines 734–762 inserts raw holder map and ReleasePublished; TryLogoutPlayerBot 512–616 erases, LogoutPlayer, raw delete; TryDisablePlayerBot 625–713 erases and deletes AI without deleting session. All paths must be coordinated before replacing ReleasePublished; never wire publication alone and leave a raw deleter.
- PlayerbotAI.cpp blob d05622b12cab396bc50270c98a14581263ae4a47 at 600–635/1160–1202 directly invokes LogoutPlayerBot during AI logout/command handling. Source proves call, not all threads. Core World.cpp blob 0405afdf7ed3f8d6d70dfc67fa6962a23417d860 lines 1228/1255/1356 session pass -> map update -> OnWorldUpdate; MapMgr.cpp blob 46f627a10515001f7edfd956ccdbf4359a267400 lines 269–280 schedules/waits map workers. Barrier separates phases but is not ownership or permission to run world-bound custody from a map worker. Need typed deferred value request with exact bot/session/holder identity, world-side resolution, bounded refusal/queue drop, scheduler bookkeeping and existing synchronous callers audited. Do not capture raw callbacks from map thread.
- Script Playerbots.cpp blob 906cbee13cef1e01f07028eebe57319d5bac25c9 BeforeLogout calls bulk logout then random OnPlayerLogout; destructor deletes AI/manager; shutdown fences pending before random logout. Master teardown/holder destruction and normal update loops need exact independent ownership and quiescence, no dangling master pointers. Human/selfbot/disable path must retain its own lifetime contract and must not adopt ordinary client session.
- Core CharacterHandler.cpp blob a984fa21042fd9436f46890225cdd1d9a42dac44 at 698–780 uses offline-session manager then SetPlayer(nullptr), SetPlayer(p), p->SetSession(this). Verify whether headless sessions are registered in that route and secure-login replacement/direct destruction before wiring. No claim that headless bot is currently reachable by offline reconnect.
- Need actual world-thread bind and shutdown stop/drain, admission before native retirement effects, exact holder unlink and native save/cleanup/lease completion, all deletion producers and queue/packet/update fencing. Native source #33 remains untouched in this child; old ReleasePublished remains for its tests/legacy draft. No Core native edit, no command/factory/selection change.
- Recovery scheduler, durable unresolved journal, actual equipment movement/effects/save and command integration remain open. Registry lock protects records only, not native objects; callback, owner-thread and OS mutex/allocation failure behavior need native review. No automatic reconciliation/replay.
- Rollback: close/drop this isolated child draft. All parent branches unchanged, no live/DB/config/SQL/DBC/client/merge/deploy/hosted CI/activation.
