# A1: capped AutoGear on a quarantined bot — reproduction and fix proposal

Requested by CS01 in [#80 7204771857](https://github.com/CosmicCuddle/Naxxramas-Server-Coordination/issues/80#issuecomment-7204771857).
This is a **proposal only**: nothing in `src/` changes until CS01 reviews the scope.

The evidence is source reading of Core88 `d5eee598` and PB68 `f68ed339`, plus the test-only reproduction in
`tests/pb70_a1_quarantine_reproduction.cpp`. Nothing has been run natively.

## What happens today

1. Core sets the quarantine when its executor returns RecoveryRequired (`PlayerPB59Inventory.cpp` 302).
   While a bot is quarantined, `CapturePB59InventoryStamp()` returns nothing (`PlayerStorage.cpp` 467–475).
   About 20 inventory and item mutators refuse through `InventoryMutationAllowed()` /
   `Item::PB59InventoryMutationAllowed()`, including `StoreNewItem`, `EquipNewItem`, `DestroyItem`,
   `Item::SetEnchantment` and `Item::SetItemRandomProperties`.
2. The capped command reads the stamp only inside `RunCoreBatch`, so only when something has to be stripped.
3. With nothing to strip, `Execute` runs the factory. Its `EquipNewItem` calls are silently blocked, so nothing is
   created. The finishers still run, and the command reports success with 0 new items: "Geared up …".
4. **The finishers aren't fully guarded.** `Player::DurabilityRepair` writes `ITEM_FIELD_DURABILITY` directly
   (`Player.cpp` 5062) before its guarded `SetState`. So the capped repair can change a quarantined item's
   durability in memory. This is from source reading and isn't shown natively.
5. `Execute` can't tell this apart from an ordinary no-op: a ready, fully geared bot also gives success with
   0 new items. The reproduction pins both cases.

## Every capped path

| Path | Writes through | Under quarantine today |
|---|---|---|
| Strip (over-cap, reset) | `ExecutePB59InventoryBatchPlan` | No stamp → Refused → `StripRefused`. Nothing changes. Correct refusal, but the reply names the wrong cause |
| Factory fill of an empty slot | `EquipNewItem` (guarded) | Silently blocked; counted as success |
| Factory replacement | executor strip, then `EquipNewItem` | The strip refuses and the slot is kept; then as a fill |
| PvP trinket | executor strip, then `EquipNewItem` | As a replacement |
| BiS overlay | executor strip, then `EquipNewItem` or restore | As a replacement |
| Finishers: ammo | `StoreNewItem` (guarded) | Blocked |
| Finishers: enchants and gems | `Item::SetEnchantment` (guarded) and `Player::ApplyEnchantment` | Item change blocked; stat application not checked here |
| Finishers: repair | `DurabilityRepairAll` → raw durability write | **Not guarded** (in memory) |
| Over-cap revocation | `DestroyItem` (guarded) | Blocked. Unreachable here, because nothing was created |
| Context loss during the run | PB54 `SameContext` and caller re-resolve | No reads and no reply. Unchanged |

## Proposed fix (narrow)

1. **Add a readiness step to the `Execute` adapter contract.** `bool Ready()` is called once, first, only when
   `plan.Runs()`, before `Capture`. When it returns false, `Execute` returns a new `Refusal::NotReady` with
   `ran = false`, and no other adapter method is called.
2. **`PlayerAutoGearInventory::Ready()`** is `_bot->CapturePB59InventoryStamp().has_value()`. That's Core's own
   signal, covering quarantine, an open transaction, unsettled originals, combat, death, teleport and
   login/logout. Without the executor API (`PB59_AUTOGEAR_CORE_BATCH == 0`), `Ready()` returns true and the
   behaviour is unchanged.
3. **`RefusalText(NotReady)`** gets a new translatable text: `autogear_ip_not_ready_error`, "I can't change my
   gear right now. Nothing was changed." Its translation SQL follows the module's SQL rules.
4. **`RunPB59AutoGearWorldPlan`** logs `refusal=<NotReady>` on the existing result line. NotReady is not a
   RecoveryRequired.
5. **Not changed:**
   - Core quarantine and its guards;
   - no reset, clear or bypass;
   - no retry;
   - the strip path keeps its own stamp check;
   - a ready bot that needs nothing stays a success.

A ready bot can become quarantined mid-run only through a native RecoveryRequired, and PB54 already stops on
that. So one check up front is enough.

## Regression expectations

These are already modelled in `pb70_a1_quarantine_reproduction.cpp`, part 2:
- quarantined (no stamp) and nothing to strip → NotReady, not ran, no adapter call;
- quarantined with something to strip → NotReady, before any strip;
- ready and fully geared → success with 0 new items (unchanged);
- ready with an empty slot → geared as before;
- an already refused plan keeps its own refusal and never calls `Ready()`;
- contract rules: `Ready()` is the first adapter call, and `NotReady` is not one of the success or recovery refusals.

Once the fix is approved, the part 1 checks that pin today's defect flip, and the Task 2 matrix gains a
not-ready row.

## Not covered

Native confirmation of the unguarded durability write, and of what stat changes `ApplyEnchantment` makes on a
quarantined bot.
