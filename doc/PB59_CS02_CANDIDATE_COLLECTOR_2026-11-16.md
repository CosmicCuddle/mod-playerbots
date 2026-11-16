# Candidate fact collector

Owner: CosmicCuddle. Coordination #59 reviews `6915517646` and `6916923532`.
This is a read-only collector. It does not edit `TrainerAction.cpp`, the authority files, `PlayerbotFactory`, storage, or the PR #30 route.

Callable entry points:

- `PlayerbotsAutoGearSelection::CollectCandidateFacts` in `src/Bot/AutoGearCandidateCollector.h`
- `PlayerbotsAutoGearSelection::CollectNativeCandidateFacts<InventoryType>` in `src/Bot/AutoGearNativeCandidateCollector.h`

`GetEquipmentNew` is called with `InventoryType` through `ExactInventoryType`. The same entry and slot from both caches counts once before the budget. Conflicting copies are rejected. Those caches drop items above epic and invalid items, and they normalize quest levels, so a finished cache is not an empty legal domain.

The collector uses the cap and quality already resolved for the command, including match, leveling, and numeric. It does not recompute an endgame stage cap. Static template eligibility is stored on the source and provenance rows. Equip and whole-set proof stay unset. `ApplyLimitEntry` reads `ItemLimitCategoryEntry` mode 0 as HAVE and mode 1 as EQUIP. `ApplySetEntry` reads `ItemSetEntry` spell thresholds. Cards 19287 through 19290 stay unknown and are not forced. Any other identity has to be verified on its own.

For a verified random bot, the final projected count subtracts only exact approved combat originals. Staging and unrelated rows stay. An alt bot keeps the physical count. Nothing is deleted.

The instantiation `CollectNativeCandidateFacts<InventoryType, RandomItemMgr, ...>` was compiled against core `28df5c5f` with `-std=gnu++20`. The object references `RandomItemMgr::GetEquipmentNew(unsigned int, InventoryType)`.
