# PB59 — native timer retirement dispatch, 15 November 2026

Owner: CosmicCuddle. Maintenance CS01. Isolated stacked development candidate, not release approval.

## Exact baseline

- PB #34 8eef5a3e1b7650c9a1131c8f0fec26f329db553e (parent #33 c4f8c97d, #32 31b172e5), paired Core #38 1e12bf841fe1415294940255ac98a8bfdb28f754. Original #37 b7d50628 / #29 88125624 unchanged.
- Child branch feature/cs01-pb59-timed-retirement-20261115.
- Native timer producer/consumer/publication changes only. Published owner #34 remains unwired; no native AutoGear Begin, command enablement or equipment mutation.

## Source finding

Core WorldSession.h at paired pin: PlayerLogout() returns m_playerLogout (actual native teardown); IsLoggingOut() returns _logoutTime || m_playerLogout; ShouldLogOut() checks 20-second countdown. PB #34 PlayerbotAI.cpp timer branch calls LogoutPlayerBot directly, but TryLogoutPlayerBot rejects IsLoggingOut() both before mutation and after callbacks. Timer-ready work therefore cannot pass that guard. Core World.cpp 1228/1255/1356 and MapMgr.cpp 269–280 establish session pass, map-worker wait, then world-update script. The barrier is a phase boundary, not a general native lifetime pin.

## Implemented

- TimedRetirementMailbox owns only value registrations (GUID, Player incarnation, native session incarnation, actual holder tag, optional exact original personal master GUID/Player/session). Each published bot gets one slot before pending ownership is released; failure rolls back holder insertion and retains pending owner. No new allocation per request, duplicate pending/claimed requests coalesce, rotating cursor, exact nonrecycled ticket and compare-and-finish. No raw Player/holder/AI/session or native callback crosses threads. Registered map bounded by published bot count; no added arbitrary population cap. 128 claims per world update, remaining work stays pending.
- Native timer producer now submits only its own exact bot/session identity; no map-thread holder/master lookup for dispatch. Native readiness helper preserves existing countdown/instant conditions (no master, master absent/not in world, bot/master rest/taxi/permission) for headless sessions and refuses active native logout. World consumer re-resolves exact bot/session and registered actual holder (random vs original personal master), validates master and holder incarnations, re-evaluates readiness, then invokes typed TryLogoutPlayerBot(guid, {}, true). A cancelled/not-ready timer produces no native call. Explicit chat logout command unchanged.
- Native TryLogoutPlayerBot differentiates actual logout from pending timer; timed mode requires readiness, ordinary mode retains prior IsLoggingOut refusal. Revalidate timer state and exact native session incarnation across callbacks; disable also checks session incarnation. Both forget exact mailbox slot before holder erase; base holder destruction forgets exact holder registrations. RecoveryRequired/exception blocks a retained slot (no blind retry); if slot already removed the existing retirement journal owns diagnostic recovery state, not the mailbox. No new native custody or durable state.
- Shutdown stops new timer producers and logs pending/blocked count before existing pending-login stop and random bulk logout. It does NOT implement a complete drain or force deletion. Refused work before mutation becomes idle for a future fresh eligible AI request. This is not a command or scheduler redesign.

## Verification

Exact source blobs at candidate:
- src/Bot/AutoGearTimedRetirementMailbox.h 9963b5a0a8fb9a9724912b41bf9aa275c76d66f3
- tests/pb59_timed_retirement_mailbox.cpp 3c3f870d4059d578a707fa0ed85bd6c03db2ccd7
- tests/pb59_timed_native_body.cpp f7d33b48fbc0004ee61ef96a711f4d40cc515053
- tests/pb59_pending_native_body.cpp 15b9ec3651e275d3353f47abe503d7adcec495bb
- src/Bot/PlayerbotMgr.cpp ec3dfbf63d549eec8c4dec9dd8fe21b5d138271f
- src/Bot/PlayerbotAI.cpp 9549393a72c8d66e00e2f59dead9f34bf4055bcc
- src/Bot/PlayerbotMgr.h dca53e01f116dbece1afff87361cb6f7c53871db
- src/Bot/PlayerbotAI.h a72ab8b9e39624b9e5176cbea6ca085a1798816a
- src/Script/Playerbots.cpp a74f136751b7522d52bd5cfff4da12c838453075

Local exact source extracts matched fetched source length/FNV64: timed_native.inc 5236 bytes 79bd5570ea55d029; ready_native.inc 827 bytes fcdcb54fedf9ac83; updated pending_native.inc 7496 bytes 760540d0bde98b2. Fixture/header blobs match local git hash-object. Exact native helper/readiness bodies compiled with synthetic game dependencies and STUB TryLogoutPlayerBot (not full actual retirement body); updated #33 prefix regression uses stub registration to inject failure. Actual retirement guard/forget ordering checked from source, not compiled as a full native function. 13 source checks PASS incl exact blob/extract matches, timer guard, publication, shutdown, session identity, unchanged explicit chat logout.

GCC/Clang/Clang ASan+UBSan, C++20 -Wall -Wextra -Werror -pthread: 6/6 new runs PASS (mailbox and exact native timer synthetic fixture); 3/3 updated pending native prefix runs PASS; combined 21/21 runs PASS (7 suites x 3, prior session identity/pending owner/published owner/custody included). Mailbox 16 threads x1000 submissions, exact completion/replacement, round-robin, sticky blocked, stop. Native synthetic cancellation, active/non-headless, instant conditions, master/holder/bot replacement, exception/recovery, 130 requests -> 128+2, shutdown. No full native build, DB, TSAN, actual game logout/save or gameplay proof.

Reproduce from repository root:
    python3 tests/run_pb59_timed_retirement.py --cxx g++
    python3 tests/run_pb59_timed_retirement.py --cxx clang++
    python3 tests/run_pb59_timed_retirement.py --cxx clang++ --sanitizers
    python3 tests/run_pb59_pending_login.py --cxx g++

## Remaining gates / explicit decisions

- This narrow route addresses AI timer only. Explicit chat logout, scheduler bookkeeping callbacks, master logout, disable, direct deletion/secure login, normal packet/session update and destruction paths remain to audit/route. Do not wire #34 published ownership until every actual raw deleter and owner-context caller is converted; no arbitrary borrowed pointer adoption. Native timer still invokes existing raw retirement path; after holder erase, failure remains diagnostic recovery, not real independent custody.
- Ready helper still uses existing AI master pointer/conditions; no new claim of native lifetime safety for a stale master pointer. Actual map-owner/WorldSession/AI context, no reentrant deletion and all native caller phase guarantees require independent review and full build/gameplay. F14/social intents are not pre-retired by mailbox submission; actual retirement retains prior generation retirement. Need review whether to suppress pending work without breaking cancellation.
- No complete native shutdown drain: Stop logs outstanding pending/blocked, normal bulk retirement still refuses pending countdowns, unresolved entries may remain. Need bounded drain/operator recovery and actual custody before activation. No restart durability, scheduler/reconciliation or equipment transaction backend yet.
- Proposed 128 claims per world update is a development performance bound, not a gameplay configuration decision; measure on native server and review. Random bot dispatch now uses actual published random holder even if it has a game-client master, rather than selecting personal manager by AI master pointer. This is intended to fix ownership targeting; verify gameplay/secure-login workflows, preserve owner authority. No human/selfbot session adopted.
- Normal non-timer guard deliberately unchanged; timer readiness rechecked on dispatch, not a claim of exact countdown-request epoch (same session cancel/restart is re-evaluated, no old work acts if not currently ready). Full source/native regression required before merge. Existing #33 personal-master fallback and #34 same-GUID unresolved fence proposals still await owner approval.
- Rollback: close/drop this isolated child draft; parent #34/#33/#32/#29 and Core #38/#37 unchanged. No DB/SQL/config/DBC/client/production/merge/deploy/hosted CI/activation.
