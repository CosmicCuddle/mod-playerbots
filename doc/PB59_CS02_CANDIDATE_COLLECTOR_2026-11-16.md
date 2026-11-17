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

## Checkpoint 23 known defects

`tests/pb59_autogear_cp23_known_defects.cpp` locks two certificates from coordination #59 comment `6947858462`. No slot policy, collector, or solver code changes in that fixture. `PB59_CP23_KNOWN_DEFECT` defaults to 1 and asserts the certificates that exist today. Compile with `-DPB59_CP23_KNOWN_DEFECT=0` to require the outcomes below. That switch fails until the production fix lands. Leave the default at 1, and set it to 0 in the same change as the fix. CS01 chooses which headers change.

B1. `SearchAtLevel` refuses finger slots below level 20 and trinket slots below 50. With `exhaustiveLegalDomain` set, a required-level-1 ring at bot level 15, or a trinket at level 49, is recorded unavailable and the slot list is `ProvenEmpty`. Required rings or trinkets then return `StripToBags` and `claimedEmpty`. A required chest plus those rings at level 15 returns `CommitSet` while both finger lists are `ProvenEmpty`. The required outcome is `Preserve`, with that slot `Incomplete`, and never `ProvenEmpty`, `claimedEmpty`, or `StripToBags`. The same rows at level 20 and level 50 still commit. The native cache walk is not part of this certificate: it forces `exhaustiveLegalDomain` false.

B2. An EQUIP `ItemLimitCategory` (mode 1) is stored as `categoryLimit`. The plain solver counts only items it has just picked, so a retained socketed gem or a retained equipped copy outside the cleared slots still returns `CommitSet`. The required outcome is `Preserve`. Replacing the occupant of the slot this plan clears may still commit: the fixture retires that original for a verified random bot and expects `CommitSet` in both switch positions. Unknown gem data (`gemsKnown` false) must `Preserve`. Two required trinkets that share an EQUIP limit of 1 already stay `Preserve`.
