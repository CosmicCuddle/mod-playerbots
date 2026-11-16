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
