# PB59 CS02 read-only native adapter contract

Owner: CosmicCuddle. Coordination #59 work order `6902772558`, task 5.
This is a contract. It does not wire a command, and it does not touch CS01's callback or retirement branches.

The branch stays on `69943153b86f872cd6a42417ea3f4b76a8ba5602`. Feature `Playerbots.AutoGearIPCap.Enable` stays off. `AutoGearSetSolver.h` stays the chooser for non-variant domains.

## Revisions that must match before a move

`SameFinalCopy` compares two immutable copies. A zero revision does not match. The fields are separate:

| Copy field | Whose fact | Reject when |
| --- | --- | --- |
| `senderEpoch` | Sender session epoch | Differs or is zero |
| `senderIpRevision` | Sender Individual Progression snapshot | Differs or is zero |
| `senderEquipmentRevision` | Sender equipment capture | Differs or is zero |
| `botEpoch` | Bot session epoch | Differs or is zero |
| `botEquipmentRevision` | Bot equipment capture | Differs or is zero |
| `sessionIncarnation` | Sender session incarnation | Differs or is zero |
| `botIncarnation` | Bot incarnation | Differs or is zero |
| `policyRevision` | Loaded policy | Differs or is zero |

The bot thread does not read the sender `Player`. A stale plan is a preserve, not a strip. `IsPB59EquipmentCurrent` stays on the owner thread at final B. This contract does not call it.

## Expected fields, by wiring point

None of these files is edited by this branch.

### 1. Command eligibility

`src/Ai/Base/Actions/TrainerAction.cpp`

- `ResolveCommandSenderAutoGearCap` at line 22 refuses the new cap while the feature is on, and the live path still calls it at line 628.
- `GetWornItemLevelAverage` at line 590 is the occupied combat-slot average. Match at line 671 uses it today.
- `DestroyEquippedGear` is called at lines 319, 466, and 772. `CanEquipNewItem` / `EquipNewItem` run at lines 541–563.
- Expected input after a later coordination: command form, explicit cap mode, sender sample, match sample, and a preserve result that returns before any of those gear calls.

### 2. Scoring and candidate facts

`src/Bot/Factory/PlayerbotFactory.cpp`

- `InitEquipment` starts at line 2356. The 25% `urand` skip is lines 2535–2536. `InitEquipment` is called from line 971.
- Expected input: copied item facts (`entry`, signed random property, item level, slot, source known, equip known) and `StatsWeightCalculator::CalculateItem(itemId, randomProperty, slot)` unchanged.
- A list past the budget, an unknown fact, or an incomplete slot proof preserves. The 25% skip must not become a shorter complete list.

### 3. Storage and recovery

`src/Bot/AutoGearStorageTransaction.h` `Execute` at line 57.

- Expected input: admitted final B, exact moves, and `BagSpace::Enough`.
- Combat items move into empty storage. Body and tabard stay. Full or unknown bags preserve. Nothing is deleted.
- Restore permission is not granted here. CS01's transaction and recovery files stay on their own branches.

### 4. Population tier sync

`src/Bot/RandomPlayerbotMgr.cpp` is the attachment point. The predicate is `AllowsPopulationTierSync` in `src/Bot/AutoGearRndbotTierSync.h`.

- Expected input: population class only. True only for a random bot.
- It does not write a stage and it does not strip.

### 5. Sender facts

`src/Bot/AutoGearNativeAuthority.h` `Capture` at line 24.

- Expected input: `PB59::EquipmentFacts` (guid, session, revision, level, class, race, slots), `policyRevision`, and a counted mask inside the combat mask.
- Output: a sample with the fixed-denominator average, or `nullopt` when a required field is missing.
- The bot thread reads that sample. It does not capture the sender again.

## Slot proof required before a native strip

A native no-set strips only when `AssessSlotList` returns `ProvenEmpty`, `setBonusProvenAbsent` is true, bags are enough, and the solver status is `NoSet`. Duplicate, missing, stale, mismatched, and unknown rows preserve. Known-unavailable rows may leave a proven empty list. An unproven set bonus does not.

## Not in this contract

CS01 callback and retirement branches, Core private equip calls, live SQL, and activation.
