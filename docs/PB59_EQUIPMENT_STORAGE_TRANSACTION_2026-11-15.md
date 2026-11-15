# PB59 equipment storage and rollback — development evidence

Owner: CosmicCuddle. Coordination #59. Stacked PB draft #29, parent 33b333671eb75efc7f550c94c45f43e50a9fc442. Core paired draft #37 82d340c1ee321e3a1adeaacadaeb99e08031c281. No activation.

## Native preflight

PrepareEquipmentStorage now enumerates exact empty destinations that Core CanStoreItem accepts for the full original stack, then uses bipartite augmenting-path matching rather than greedy first-fit. This avoids false InventoryFull when a flexible item can use a specialized bag and another cannot. Complete matching is read-only, 17 original combat slots max, bounded by the representable 16 backpack + 4x256 bag positions. No stack merging or movement. Source GUID and count, reserved destination and destination bag GUID are retained; offhand moves first so reverse order restores mainhand before offhand. ValidateEquipmentStorage checks exact original identities/counts, no overlap, storage positions, trade/loot exclusions, destination bag identity and capacity, CanUnequipItem/CanStoreItem, then rechecks the entire plan after callbacks. A missing/replaced bag is not an empty destination. No production class/tier policy selected.

## Rollback coordinator

AutoGearStorageTransaction.h provides bounded synchronous orchestration over a backend contract: Inspect returns exact identity/empty/unknown, MoveExact must preserve item instance and must not merge or overwrite. Reject stale authority or invalid/occupied/unknown preflight, verify every move and all final positions, rollback in reverse only when original slot is empty and exact item is still at its reserved destination. Observe actual state even when move reports false or throws. RecoveryRequired is distinct from RolledBack; never report successful restoration when proof is missing. Native movement backend is deliberately NOT connected. Core SwapItem can merge or invoke callbacks, so it cannot be assumed to satisfy MoveExact. Native preflight uses ValidMoves only. No real strip or replacement transaction yet.

## Exact locally executed tests

- Storage matching header ff8ae579b10049e4fd8da15c61e462e358e43ec8, fixture 76065d5ebac1dbaa2a866a058076f2e292fed87e: GCC and Clang C++20 -Wall -Wextra -Werror, Clang ASan/UBSan PASS. Exhaustive 512 three-by-three bipartite graphs compared with independent permutation oracle; exact GUID/source/destination uniqueness, incomplete/budget/invalid/full cases.
- Transaction header a50be35c42d38fadf1490dc5324a666a239e0def, fixture bdd92714479334995c7f3567395ff03f8b764ea9: GCC/Clang/ASan+UBSan PASS. Nine injected failures (each of three moves x fail before, fail after, throw after); exact restoration; stale authority, occupied/unknown preflight, duplicate destination, conflicting original slot and failed rollback report appropriate result.
- Reproduce from module root: g++ -std=c++20 -Wall -Wextra -Werror -Isrc/Bot tests/pb59_autogear_storage_matching.cpp -o /tmp/pb59-storage && /tmp/pb59-storage; repeat for tests/pb59_autogear_storage_transaction.cpp and clang++, and clang++ -fsanitize=address,undefined.
- Full native compile, actual Player inventory callbacks, installed module composition, gameplay and persistence/crash recovery NOT tested. Fixture backend is synthetic and does not establish native MoveExact safety.

## Next gate / rollback

Need native non-merging exact move with explicit callback/reentrancy/lifetime rules, original/new-item ownership and rollback evidence, persistent commit/crash safety and final coherent IP/equipment command B. Do not use DestroyItem or existing uncapped factory reset for originals. No suitable-set decision must still come from a complete validated domain and authorized command; full bags/unknown state/infrastructure preserve gear. Revert these isolated draft commits for rollback; no DB, config, client or live changes. No merge/deploy.
