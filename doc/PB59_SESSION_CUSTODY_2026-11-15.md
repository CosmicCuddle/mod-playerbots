# PB59 — independent session custody, 15 November 2026

Owner: CosmicCuddle. Maintenance CS01. Development only; not a release candidate.

## Exact baseline and scope

- Playerbots PR #29: 881256249e8aef5daed5545f74174252397ef491.
- Core PR #37: b7d50628782565189332af1c490c7cf67e59b3d7.
- Independent child branch: feature/cs01-pb59-session-custody-20261115.
- New header and synthetic fixture only; no native caller, config, database, DBC, live, or command changes.

## Source-grounded ownership problem

At the exact PB pin, src/Bot/PlayerbotMgr.cpp blob 7e13a54216e9ec069264ab2bb9cc1483a531bf4d:
- AddPlayerBot (lines 96–180) allocates a raw headless WorldSession; botLoading holds raw session while callback is pending.
- UpdatePendingLogins / ClearPendingLogins (213–253) call LogoutPlayer and delete; neither is a custody handoff.
- HandlePlayerBotLoginCallback (264–292) queues OnBotLoginOperation and erases botLoading; queue and holder publication must be audited as an ownership chain.
- UpdateSessions (295–314) drives callbacks and packets from holder entries; a recovering bot removed from the holder needs a separate, fenced owner update path.
- TryLogoutPlayerBot (412–517) records holder erasure, invokes native logout, checks GetPlayer and then deletes; a veto/exception after erasure is recorded but leaves no independent session owner.
- PlayerbotMgr destructor (1715–1720) detaches holder observer. Bulk logout (335–365) logs incomplete retirement; it does not retain a live owner.

Core src/server/game/Server/WorldSession.cpp blob 10b6029b30d53c588fbf2cb95ff4f4be57d5eddf:
- Destructor (170–195) invokes LogoutPlayer if Player remains attached. A logout veto alone cannot safely make destruction legal.
- LogoutPlayer (692 onward) does far-transfer, flags, detach and script callbacks before cleanup and saving; arbitrary callback effects cannot be blindly replayed.
- WorldSession.h supplies IsHeadless and GetPlayer; a native session incarnation and proof of exclusive allocation/deletion ownership are still needed.

## Implemented primitive

SessionCustody<Session> takes a real std::unique_ptr<Session>& from the proved exclusive source owner, after preallocating an independent slot. It checks the exact Player pointer, GUID, Core owner incarnation, expected session pointer, headless state and complete operation identity; the holder and session incarnation are supplied by the caller and MUST be independently validated by the native adapter. A reserved record is not custody. A successful Adopt moves ownership without allocation. Duplicate exact Player key, session incarnation or session pointer is refused.

The explicitly bound owner thread alone operates on the store; foreign-thread calls are refused. Native callback borrowing is one-shot; the busy scope prevents reentrant deletion, exceptions or a veto retain custody, and a later unrelated detach cannot authorize deletion. Only a normal return with Player detached permits explicit deletion. A dropped adopted ticket retains its independent owned session; it does not authorize replay. Process-lifetime storage intentionally avoids static-destructor logout: this is not a shutdown plan, and unresolved entries require a real recovery scheduler. No restart durability, enumeration, reconciliation/drain, owner-thread replacement, or native call adapter exists. The owner thread must remain live; thread-id reuse is not a substitute for lifecycle fencing. The primitive assumes valid pointers and sole native ownership; it cannot prevent an external raw-pointer deletion. No session is transferred in running Worldserver by this change.

## Tests

Exact tested blobs:
- src/Bot/AutoGearSessionCustody.h: 743983634d904c335cb387c710c9064993217105 (SHA256 155197913e0fcf05fd5c349e7a5986fd72f82617f4e12fef99be1d79e60633c0)
- tests/pb59_session_custody.cpp: f129f1d20f850a1d414038abe0c267339a5c7a1c (SHA256 c47d085d7c3994e6274cc75d82a9f2a8bcd656a0f606ec263421ebf25198c863)

GCC, Clang, Clang ASan+UBSan C++20 -Wall -Wextra -Werror -pthread: all 3 compile/execute runs PASS. Run from repository root:
    g++ -std=c++20 -Wall -Wextra -Werror -pthread -Isrc/Bot tests/pb59_session_custody.cpp -o /tmp/pb59-custody && /tmp/pb59-custody
    clang++ -std=c++20 -Wall -Wextra -Werror -pthread -fsanitize=address,undefined -Isrc/Bot tests/pb59_session_custody.cpp -o /tmp/pb59-custody-san && /tmp/pb59-custody-san

Synthetic tests cover no binding, non-headless and detached rejection, wrong identity/pointer/incarnation, foreign-thread refusal, duplicate reservation, reserve cancellation, actual move of sole ownership, logout veto, later unrelated detachment, exception after detach, one-shot/no reentry, exact successful deletion, ticket loss and replacement owner. The intentionally retained entries are reachable in a process-lifetime store; sanitizer success is NOT proof of bounded native recovery or cleanup.

## Next integration gates

1. Prove allocation -> pending login -> queued login -> holder publication exclusive ownership. Add a native session incarnation, fence replacement and pending callback deletion; do not wrap an arbitrary borrowed raw pointer in a unique_ptr.
2. Define owner-context transfer for personal vs random headless bots, distinct from human/selfbot sessions. Reserve before effects, adopt before original owner releases, then ensure all raw deletion paths and callback schedulers respect the transfer.
3. Establish independent recovery update/packet fencing, stable ownership on master logout/manager erasure, exact queue identity and shutdown drain. No native logout veto until destructors and direct deletion callers are covered.
4. Establish durable unresolved recovery, save ordering and actual equipment reconciliation before native Begin or command integration. Fault-test real native lifetime and full stack; preserve existing gameplay and default-off behavior.
5. Re-review against then-current Core/PB pair, full build, database and gameplay; no production/merge/activation without approval.

Rollback: isolated child branch only; drop/close the draft, leave PB #29/Core #37 unchanged. No live rollback or migration required.
