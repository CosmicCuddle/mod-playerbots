# Command route hook

Owner: CosmicCuddle. Coordination #59 review `6915090769`.
`AutoGearAction::Execute` and `BisGearAction::Execute` in `src/Ai/Base/Actions/TrainerAction.cpp` now call `PreserveAutoGearPlan` when `Playerbots.AutoGearIPCap.Enable` is on. That call builds the route inputs, calls `ResolveAutoGearRoute`, logs the typed plan, and returns without changing gear. It does not call `PlayerbotFactory::AutoGear` or `DestroyEquippedGear`.

With the flag off, both functions keep the previous body. A zero score limit on that path is still the legacy unlimited item level. The stage table applies only on the ON path, inside `ResolveAutoGearRoute`.

## Still not called

`RandomPlayerbotMgr::Refresh` is not edited. A later call, after CS01 allows it, is:

```cpp
PlayerbotsAutoGearSelection::RouteRequest request;
request.tierSync = true;
request.ipCapEnabled = sPlayerbotAIConfig.AutoGearIPCapEnable;
request.populationKnown = true;
request.population = /* RandomBot, AltBot, AddClass, or Unknown from a verified check */;
request.playerContextVerified = /* sender snapshot present */;
request.progressionContextVerified = /* stage snapshot present */;
request.rawStage = /* realm stage */;
request.effectiveStage = request.rawStage;
request.scoreLimit = static_cast<uint32>(sPlayerbotAIConfig.RandomGearScoreLimit);
auto const decision = PlayerbotsAutoGearSelection::ResolveAutoGearRoute(request, scorer);
```

The decision does not write a stage. An alt bot is rejected. With the feature off, the action stays Preserve and is not a commit.

`PlayerbotFactory::AutoGear` and `InitEquipment` are not the commit path. A CommitSet plan carries the chosen ids and properties. Applying those items waits on the native authority, the storage move, and a selected-set executor. Until those exist, the ON path preserves.
