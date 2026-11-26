# AutoGear cross-component interface audit: Core88 + PB68 (R1 source tuple)

CS01 Part 41 Task 3 ([#80 7198695947](https://github.com/CosmicCuddle/Naxxramas-Server-Coordination/issues/80#issuecomment-7198695947)).
This is source and compile evidence only. It isn't gameplay acceptance or a native run. Core114's
holder wiring and its isolated results aren't used as proof here.

## Tuple audited

| Component | Commit | How it was checked |
|---|---|---|
| Core88 | `d5eee59878bed7991611b0bf6764e73e03885b39` | checked out exactly; headers and objects compiled |
| Playerbots PB68 | `f68ed339d8d82627479f7d40754c97dca9b79521` | `src/` copied exactly into the Core tree |
| Individual Progression | `c1725f84abd382d0d314eccfa6bbed0b4aa89b23` | git HEAD in the tree |
| Character Services | `f43bc2c98aa7a291b673158fd578be304607b5e1` | git HEAD in the tree |
| Naxx | `80e9de1c39a6dc995188a80c31a5e785d0f2a6f9` | git HEAD in the tree |

## Compile evidence

- **Objects:** these six translation units compiled against the tuple above with the existing configured
  tree (`cmake .`, then `ninja <objects>`), with 0 warnings and 0 errors. Nothing was linked or installed.
  - Playerbots: `TrainerAction.cpp`, `PlayerbotAI.cpp`, `PlayerbotFactory.cpp`;
  - Core: `Player.cpp`, `PlayerPB59Inventory.cpp`, `PlayerPB59Recovery.cpp`.
- **Symbols `TrainerAction.cpp.o` uses** (`nm`):
  - `Player::ExecutePB59InventoryBatchPlan(ulong, ulong, ulong, PB59::Stamp const&, std::span<…>)`;
  - `Player::CapturePB59InventoryStamp() const`;
  - `Player::CapturePB59InventorySlot(ushort) const`;
  - `ObjectAccessor::FindConnectedPlayer(ObjectGuid)`;
  - `ScriptMgr::TryReadPB59AutoGearProgression(Player*, uchar&, uchar&)`.
- **`tests/pb59_autogear_core_batch_request.cpp`** against Core88's real `PB59InventoryBatchPlan.h`:
  20/20 on GCC and Clang.
- **`tests/pb70_core88_interface_audit.cpp`** (new) against the same header: 75/75 on GCC with
  ASan/UBSan and on Clang, `-Werror`. Ignoring Core's failure reason on Applied fails 9 checks.

## Interface checks

| Topic | Core88 source | Playerbots PB68 | Status |
|---|---|---|---|
| API from Core74 to Core88 | `PB59InventoryBatchPlan.h` and `WorldSession.h` unchanged. `Player.h` adds only private recovery-collector declarations that nothing calls. The executor now also requires the exact item pointer before counting a restore. | Unchanged calls | Compatible, and the restore proof is stricter |
| Owner/session stamp | `CapturePB59InventoryStamp()` returns nothing while logging in or out, not in the world, dead, in combat, teleporting, quarantined, with a transaction already open, or with unsettled originals (`PlayerStorage.cpp` 467–475) | A strip refuses when there's no stamp. The stamp and both incarnations are copied before the call | Compatible |
| Stale observation | `ExecutePB59InventoryBatchPlan` → `CheckPB59InventoryBatchPlan`, which matches owner, owner incarnation, session, session incarnation and `Current(stamp)` (`PlayerStorage.cpp` 477–498). An active reservation or quarantine gives Refused/Context (`PlayerPB59Inventory.cpp` 46–53) | Refused is believed only with an unchanged whole-inventory fingerprint. `SameContext` re-resolves the bot before observing (PB54) | Closed at both ends |
| Outcome mapping | `InventoryBatchOutcome { Refused, Applied, Restored, RecoveryRequired }` | `OutcomeFrom` maps by name. An unknown value is a recovery. All four are pinned by `static_assert` on Core88's type | Verified |
| Failure reporting | `InventoryBatchFailure` has 8 values; the result starts as Context | Any failure reason turns Applied into a recovery. The value is logged as `nativeFailure` | Verified (75/75 matrix) |
| Recovery GUIDs | `std::array<uint64, 19>` plus a `uint8` count. Effect-only recovery can have an empty list | Array size is `static_assert`ed to `EquipmentSlots`. Native RecoveryRequired stays a recovery when the list is empty | Verified |
| Hold/quarantine reporting | There's no public quarantine query. The stamp is withheld and about 20 inventory mutators check `InventoryMutationAllowed()` | No up-front check. See finding A1 | **Gap (A1)** |
| Shutdown | `Main.cpp` 374–375: `KickAll` and one `UpdateSessions` before maps unload. Quarantine is process-local and not durable (CS01 note on #51 7137708293) | Queued commands re-resolve both players and stop when they've gone. No reply goes out after a lost context (PB54) | Safe for Playerbots. Durable recovery is still Core's release blocker |

## Findings for CS01

**A1. Quarantined bot, command with nothing to strip.**
1. The capped command asks for the stamp only inside a strip (`RunCoreBatch`).
2. On a quarantined bot with nothing to strip, the factory's item creation is blocked by Core's mutation guards.
3. The run then counts as a success with 0 new items, and the bot whispers "Geared up … (0 new items)".

That's misleading, not unsafe. The smallest fix is a `src/` change for you to approve: refuse the capped command before planning when `CapturePB59InventoryStamp()` returns nothing. It isn't done here.

**A2 (from Task 2).** A mixed population is refused with NotVerified, not UnknownPopulation. Reported in #80 7200797275.

## Not covered

- Linking and a full static build.
- Native execution, and any gameplay or persistence evidence.
- Core114 and later recovery work.
- Whether the stamp and mutation guards behave as described at run time is source-read only.
