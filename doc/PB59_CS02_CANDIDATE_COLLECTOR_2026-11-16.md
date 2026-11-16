# Candidate fact collector

Owner: CosmicCuddle. Coordination #59 review `6915517646`.
This is a new read-only collector. It does not edit `TrainerAction.cpp`, the authority files, `PlayerbotFactory`, storage, or the PR #30 route.

Callable entry points:

- `PlayerbotsAutoGearSelection::CollectCandidateFacts` in `src/Bot/AutoGearCandidateCollector.h`
- `PlayerbotsAutoGearSelection::CollectNativeCandidateFacts` in `src/Bot/AutoGearNativeCandidateCollector.h`

`CollectNativeCandidateFacts` walks `GetEquipment` and `GetEquipmentNew`, then `ReadItemTemplate` copies the `ItemTemplate` fields from core `28df5c5f`. The score function is `StatsWeightCalculator::CalculateItem`, wrapped by `StatsWeightScore`. The plan is `SelectFromFacts`. Nothing here equips an item, calls `DestroyEquippedGear`, or calls `PlayerbotFactory::AutoGear`.

A zero score limit uses the stage caps 66, 78, 78, 83, 83, 83, 88, 92. A nonzero limit only lowers that cap. Unknown or incomplete rows preserve. A scan that stops at `rowBudget`, or a cache the caller has not marked complete, does not report an empty domain. A Darkmoon card is not forced. Unknown availability preserves. The template reader does not invent a rolled random property and does not call `CanEquipItem`.

`CollectFromRandomItemMgr` is not compiled into this checkout. The worldserver build is the place that can pass `RandomItemMgr` and `sObjectMgr->GetItemTemplate`.
