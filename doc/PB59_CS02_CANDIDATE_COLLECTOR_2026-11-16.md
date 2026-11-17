# Candidate fact collector

Owner: CosmicCuddle. Coordination #59 reviews `6915517646` and `6916923532`.
This is a read-only collector. It does not edit `TrainerAction.cpp`, the authority files, `PlayerbotFactory`, storage, or the PR #30 route.

Callable entry points:

- `PlayerbotsAutoGearSelection::CollectCandidateFacts` in `src/Bot/AutoGearCandidateCollector.h`
- `PlayerbotsAutoGearSelection::CollectNativeCandidateFacts<InventoryType>` in `src/Bot/AutoGearNativeCandidateCollector.h`

`GetEquipmentNew` is called with `InventoryType` through `ExactInventoryType`. The same entry and slot from both caches counts once before the budget. Conflicting copies are rejected. Those caches drop items above epic and invalid items, and they normalize quest levels, so a finished cache is not an empty legal domain.

The collector uses the cap and quality already resolved for the command, including match, leveling, and numeric. It does not recompute an endgame stage cap. Static template eligibility is stored on the source and provenance rows. Equip and whole-set proof stay unset. `ApplyLimitEntry` reads `ItemLimitCategoryEntry` mode 0 as HAVE and mode 1 as EQUIP. `ApplySetEntry` reads `ItemSetEntry` spell thresholds. Cards 19287 through 19290 stay unknown and are not forced. Any other identity has to be verified on its own.

For a verified random bot, the final projected count subtracts only exact approved combat originals. Staging and unrelated rows stay. An alt bot keeps the physical count. Nothing is deleted.

`EnumerateVariants` and `EnumerateInstalledPool` in `src/Bot/AutoGearVariantEnumeration.h` read the installed enchantment-template cache. Membership uses the same rule as `HasItemEnchantMod` at core `8d6ac43`: the group id must be positive and the enchant id nonzero and present. A random-property group becomes positive ids. A random-suffix group becomes negative ids. A missing group, a zero or duplicate member, a DBC mismatch, or both groups at once preserves. A budget that stops early is not a complete list. The collector does not roll or create an item.

`AdaptLoadedGroup` keeps Core rows whose weight is positive and at most 100. The `RandomItemMgr` pool is complete only when it contains exactly those ids. `HasItemEnchantMod` confirms a member. A true answer for one id is not proof that the pool is exhaustive. Property and suffix DBC stores stay separate, so the same number may exist in both.

`CollectCandidateFacts` stops before scoring when the source budget or the expansion budget is exceeded. A partial list is not certified.

`CopyBoundedSource` in `src/Bot/AutoGearVariantSource.h` still treats two signed ids of one entry as `IncompleteVariants`. `CollectCandidateFacts` therefore leaves `hasNativeSource` false when `splitVariant` is set. That is the integration stop. The proposed proof key, not applied to CS01's solver, is `(entryId, signedId)`: a positive id is a random property, a negative id is a random suffix, and zero is the ungrouped template.

`EnumerateInstalledPool<HasItemEnchantMod>` and `AdaptLoadedGroup` were compiled with GCC 13 `-std=gnu++20 -Wall -Wextra`. `ItemEnchantmentMgr.h` is from core `8d6ac43`. The object references `HasItemEnchantMod(int, unsigned int)` and `RandomItemMgr::GetEnchantmentPool(unsigned int)`. The earlier `GetEquipmentNew(unsigned int, InventoryType)` instantiation still stands. Neither object is linked into worldserver.

## Checkpoint 23 search schedule and EQUIP retention

`SearchAtLevel` still does not search finger slots below level 20 or trinket slots below 50. That is only a search schedule. A false result is not proof that the slot is empty, and it is not a Core exclusion. Low-level bots can already wear those items: TBC jewelcrafting rings are available around level 10, and Engineering trinkets exist below 50. A skipped slot means the item was not checked. The equipped item stays. An eligible slot outside the schedule is `Incomplete`, the plan returns `Preserve`, and that path never sets `claimedEmpty` or `StripToBags`. Rings at level 20 and trinkets at level 50 are still searched and can commit. The native cache walk still forces `exhaustiveLegalDomain` false.

An EQUIP `ItemLimitCategory` (mode 1) counts retained socketed gems in that category plus retained equipped copies that this plan does not retire. Retirement is the existing approved-original projection for a verified random bot, so replacing the occupant of that same slot can still commit. Copies left in the projected equipped count sit outside the cleared slots and count toward the limit. The collector refuses a missing or already-full proof and does not subtract. `SelectFromFacts` sends every EQUIP limit through the variant solver, which subtracts that seed once. A direct call with an unreduced limit therefore cannot ignore retained gems. A seed that already fills the limit refuses the domain. Unknown gem or equipped state (`gemsKnown` or `equippedKnown` false, or no category row) is not treated as zero: the domain stays incomplete and the plan returns `Preserve`. HAVE-mode categories are unchanged.

`tests/pb59_autogear_cp23_known_defects.cpp` covers both. `PB59_CP23_KNOWN_DEFECT` is 0, so the fixture requires `Preserve` and `Incomplete`. `-DPB59_CP23_KNOWN_DEFECT=1` replays the old certificates and fails.
