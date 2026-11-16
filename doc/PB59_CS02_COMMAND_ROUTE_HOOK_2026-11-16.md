# Proposed command hook

Owner: CosmicCuddle. Coordination #59 review `6914685646`.
This is the patch CS01 would apply. None of these files is edited on this branch.

The route itself is `ResolveAutoGearRoute` in `src/Bot/AutoGearCommandRoute.h`. Flags stay off. With `Playerbots.AutoGearIPCap.Enable` off, the live command keeps today's behavior. With it on, a zero `AutoGearScoreLimit` uses the approved stage table. The table's own maximum is the era ceiling. There is no fallback ceiling of 78. A nonzero score limit can only lower the result.

## AutoGearAction::Execute

File: `src/Ai/Base/Actions/TrainerAction.cpp`, function `AutoGearAction::Execute`, immediately after the alt-bot refusal and instead of `ResolveCommandSenderAutoGearCap` plus the argument switch that leads to `DestroyEquippedGear`.

```cpp
PlayerbotsAutoGearSelection::RouteRequest request;
request.commandEnabled = sPlayerbotAIConfig.AutoGearCommand;
request.ipCapEnabled = sPlayerbotAIConfig.AutoGearIPCapEnable;
request.altCommandAllowed = sPlayerbotAIConfig.AutoGearCommandAltBots;
request.bis = false;
request.text = event.getParam();
request.population = sRandomPlayerbotMgr.IsRandomBot(bot)
    ? PlayerbotsAutoGearSelection::Population::RandomBot
    : PlayerbotsAutoGearSelection::Population::AltBot;
request.scoreLimit = static_cast<uint32>(sPlayerbotAIConfig.AutoGearScoreLimit);
request.qualityLimit = static_cast<uint32>(sPlayerbotAIConfig.AutoGearQualityLimit);
request.rawStage = /* sender snapshot raw stage */;
request.effectiveStage = /* sender snapshot effective stage */;
request.senderIsLeveling = /* snapshot; do not infer this from level 60 */;
request.firstStageAfterLeveling = /* snapshot of the first stage after leveling */;
request.occupiedSlots = /* master's combat-slot item levels, empty slots as 0 */;
request.senderFixedKnown = /* leveling sample present */;
request.senderFixedAverage = /* leveling fixed-denominator average */;
auto const decision = PlayerbotsAutoGearSelection::ResolveAutoGearRoute(request, scorer);
if (!request.ipCapEnabled)
    ; // keep the existing score-limit body below
else if (decision.rejected || decision.preserve || decision.action == PlayerbotsAutoGearSelection::GearAction::Preserve)
    return false;
else if (decision.action == PlayerbotsAutoGearSelection::GearAction::StripToBags)
    ; // move combat items into empty bags; do not call DestroyEquippedGear
else
    PlayerbotFactory::AutoGear(bot, quality, decision.cap, /*incremental*/ !reset, false, true, decision.cap);
```

`decision.deletesGear` is false. `DestroyEquippedGear` must not run on a preserve or a strip.

## BisGearAction::Execute

Same file, function `BisGearAction::Execute`, after the quality-floor check and instead of `ResolveCommandSenderAutoGearCap`.

The same request with `request.bis = true` and `request.commandEnabled = sPlayerbotAIConfig.AutoGearBisCommand`. A fallback must not call `RunAutogearFallback` until the route returns a commit. `RunAutogearFallback` today calls `DestroyEquippedGear`; that call stays out of a preserve.

## RandomPlayerbotMgr::Refresh

File: `src/Bot/RandomPlayerbotMgr.cpp`, function `RandomPlayerbotMgr::Refresh`. Do not edit it here.

```cpp
PlayerbotsAutoGearSelection::RouteRequest request;
request.tierSync = true;
request.population = /* RandomBot, AltBot, AddClass, or Unknown */;
request.rawStage = /* realm stage */;
request.effectiveStage = request.rawStage;
request.scoreLimit = static_cast<uint32>(sPlayerbotAIConfig.RandomGearScoreLimit);
auto const decision = PlayerbotsAutoGearSelection::ResolveAutoGearRoute(request, scorer);
if (decision.rejected || decision.writesProgression)
    return;
```

An alt bot is rejected. The decision never writes a progression stage and never deletes gear.

## Still native

`AutoGearNativeAuthority.h`, the storage headers, and the factory equip call stay with CS01. The route plans a cap and a preserve, strip-to-bags, or commit. It does not equip an item and it does not move a bag.
