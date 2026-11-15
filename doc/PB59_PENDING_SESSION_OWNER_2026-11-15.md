# PB59 — pending and queued native session ownership, 15 November 2026

Owner: CosmicCuddle. Maintenance CS01. Isolated stacked development candidate; not release approval.

## Pins and scope

- Parent Playerbots #32: 31b172e51325b8bc3d3df4ec18c5c1d63f5e41d8.
- Paired Core #38: 1e12bf841fe1415294940255ac98a8bfdb28f754 (native GetPB59SessionIncarnation).
- Original PB #29 881256249e8aef5daed5545f74174252397ef491 / Core #37 b7d50628782565189332af1c490c7cf67e59b3d7 remain unchanged.
- Child branch feature/cs01-pb59-pending-session-owner-20261115. No native equipment transaction Begin, command enablement, DB/SQL/schema/config/DBC/client/production changes.

## Source problem and implemented change

The previous AddPlayerBot allocated a raw headless WorldSession. Pending callback removed botLoading even if QueueOperation returned false; OnBotLoginOperation later re-resolved only the bot GUID and master account, with a random-holder fallback if a personal master had disappeared. Pending cleanup used raw delete after LogoutPlayer, without checking the Player had detached. Core constructor at WorldSession.cpp:149 sets headless from null socket; CharacterHandler.cpp:803–910 loads and registers the Player (ObjectAccessor::AddObject). These are source facts, not a reproduced native failure.

New PendingSessionOwner owns a real unique_ptr from allocation through pending DB callback and queued attach. An operation contains only exact GUID/session incarnation and a strong marker; pending holds a weak marker. Queue rejection fails the pending record; dropping/clearing a queued operation expires the marker and is detected during world update. Callback busy fences prevent cleanup on the session's own stack. Update snapshots GUID + session ID, re-finds after callbacks, and only erases exact closed/released entries. Failed cleanup requires normal native return and detached Player before deleting; veto/throw latches RecoveryRequired and retains owner without retry. Shutdown stops producers before random LogoutAllBots and attempts pending cleanup afterwards; unresolved owners are logged, not forcibly deleted by static destruction.

Queued publication verifies the original session and Player incarnation, original personal master's session and Player incarnation, and holder liveness before map insertion. Exact holder insertion then releases unique_ptr to the EXISTING legacy holder responsibility before OnBotLoginInternal and unchanged initialization tail. The pending registry is not the live manager/recovery owner after publication. Exceptions after publication report failure, retain the legacy owner's responsibility, and do not delete through the pending registry. Existing normal retirement code and account-link query are unchanged.

**Review decision:** personal requests with absent/nonmatching original master now fail closed instead of falling back to a random holder or a replacement account session. This is a proposed safety behavior change, not an approved gameplay policy; review owner intent and offline/admin request workflows before integration. Existing random requests (masterAccountId zero) continue to use random holder. No automatic progression/tier changes.

## Evidence

Exact tested source blobs:
- src/Bot/AutoGearPendingSessionOwner.h: 301f9c0801761787537ae26b075c67d352910506.
- tests/pb59_pending_session_owner.cpp: 480ae6fc4a7e5636c132157cc80334cd73e631f4.
- tests/pb59_pending_native_body.cpp: 1b1311d85d1178a9dbbc58b95db86ae3c8029d31.
- src/Bot/PlayerbotMgr.cpp: 05676df8ca7d485af7e466ad8624164ff933034a.
- src/Bot/PlayerbotMgr.h: 837989224cd8bc57f173fd1ac8f6c081516c7dc1.
- src/Script/WorldThr/PlayerbotOperations.h: 99f8aed3e20c94a0c59f2063d26fcef390bb845c.
- src/Script/Playerbots.cpp: 906cbee13cef1e01f07028eebe57319d5bac25c9.

Local compiled exact extracts matched fetched source by length and FNV64: pending_native.inc 7309 bytes bca2dc943e2baca7 (SHA256 5adfe2aaf2098c741617e265ec2d350b38f0a4dadbac8764b12793fef315df5a); queued_native.inc 957 bytes ed0ba3e567134564 (SHA256 963c3026b0977342d0db90493a8840c96e12e1504129c32b092e86c8cf498577). Pending extract includes UpdatePendingLogins, Stop/ClearPendingLogins, HandlePlayerBotLoginCallback, CompleteQueuedBotLogin and OnBotLogin prefix through OnBotLoginInternal; fixture closes prefix instead of compiling the rest of the gameplay initialization. Queued class exact. Native dependencies synthetic. All 6 new GCC/Clang/Clang ASan+UBSan compile-execute runs passed, repeated in combined 12/12 run with earlier exact session identity and custody helpers. Source checks 10/10: allocation/capture/master fence/no raw deletion, queue failure, insertion-release-callback order, shutdown ordering, linked account query/legacy tail/retirement unchanged.

Reproduce on candidate checkout:
    python3 tests/run_pb59_pending_login.py --cxx g++
    python3 tests/run_pb59_pending_login.py --cxx clang++
    python3 tests/run_pb59_pending_login.py --cxx clang++ --sanitizers

The runner is provided for reproduction; local runs compiled matching manually extracted includes and exact test blobs. Not a full native build, DB test, thread sanitizer, gameplay or runtime claim.

## Remaining gates / risks

- Verify every native callsite, including pending allocation from commands/console, exact world-owner scheduling and Core map barrier, all headless deletion paths, callback registration allocation/exception ordering, secure-login replacement, and real shutdown order. Source fixture does not compile AddPlayerBot's full producer body or script hooks. Full native build must use paired Core #38.
- A real pending session is now uniquely owned, but manager publication releases to existing raw-holder responsibility. Next convert the published holder's actual exclusive owner to a safe independently transferable owner, and wire #32 custody before erase/logout/master teardown. No arbitrary borrowed-pointer wrapping.
- Pending recovery records are process-local. Process-lifetime storage avoids implicit destruction after services stop, not a recovery scheduler or shutdown durability. An unresolved entry can block further random population login via existing botLoading.empty() condition; do not activate/merge until bounded reconciliation/drain and operator policy are reviewed.
- If existing native initialization throws after publication, legacy holder owns the session and the operation reports failure; no full post-publication recovery, AI lifetime or callback reentrancy guarantee. Source needs exact native lifecycle and gameplay fault tests.
- Queue disabled indefinitely can hold pending entries; no automatic timeout chosen. Review explicit queue drain/admission rather than inventing a gameplay timer.
- Full native tests, successful/failed personal/random login, queue rejection/drop, master reconnect, same GUID/address, direct destruction, shutdown and relog remain required. Preserve approved account/progression policy and existing feature-OFF behavior. AutoGear stays disabled; no native equipment transaction execution.
- Rollback: isolated child branch/draft only; close/drop it and keep #32/#29 and #38/#37 intact. No live rollback or migration needed.

