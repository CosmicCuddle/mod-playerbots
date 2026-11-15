# PB59 CS02 command-wiring plan

Owner: CosmicCuddle. Coordination #59 review `6898131567`.
Plan only. No shared file is edited here. This branch stays on `69943153b86f872cd6a42417ea3f4b76a8ba5602`. It does not rebase onto PB `881256249e8aef5daed5545f74174252397ef491`.

The pure headers already decide a cap, a candidate universe, a solver result, and an outcome. Wiring them into commands is five separate steps. Each one waits for Maintenance. None of them runs while `Playerbots.AutoGearIPCap.Enable` is off.

## 1. Command eligibility

File: `src/Ai/Base/Actions/TrainerAction.cpp`. CS01 has marked this wiring coordinate-first as well.

Replace `ResolveCommandSenderAutoGearCap` only after that coordination. Every form — plain, reset, match, quality, numeric, and both BiS forms — calls `ResolveCommandEligibility`. `ModeForForm` picks Match for the match form. For the other forms the caller passes whether the sender is leveling. Do not infer that from level 60.

A preserve result returns before `DestroyEquippedGear`, `EquipNewItem`, or the BiS reputation grant. Reset and BiS must not delete gear on the way to a later preserve.

## 2. Scoring and candidate facts

File: `src/Bot/Factory/PlayerbotFactory.cpp`. Coordinate-first.

`SelectFromFacts` takes a scorer with the `CalculateItem(itemId, randomProperty, slot)` shape. The factory would pass `StatsWeightCalculator::CalculateItem` without changing its weights. The item facts are copies: source known only when the template was loaded, equip known only after the bot's real check, class and level from that bot.

The 25% `urand` skip must not feed this path. If the supplied list is larger than the budget, the plan preserves before it scores. `CollectDomains` returns `Bounded` and an empty slot list rather than a shortened complete domain. The existing set solver remains the only chooser.

## 3. Storage and recovery

Files: the storage headers, and Core's private exact-move and recovery functions. Not free.

A commit or a strip happens only after `BagSpace::Enough` and only through `PlayerbotsAutoGearStorage::Execute`, once Maintenance has a `MoveExact` backend. Combat items move into empty storage. Body and tabard stay. Full bags and unknown bags are `TechnicalFailure` and preserve. Nothing is deleted.

`SelectFromFacts` leaves `mutationStarted` false, so it does not record a restore reason. After a real mutation, a later layer may call `MapOutcome` with `mutationStarted` true and then `RequireRecovery` plus `ReconcilePB59VisibleEquipment` under recovery access. That call is not part of this plan.

## 4. Population tier sync

File: `src/Bot/RandomPlayerbotMgr.cpp`. Coordinate-first.

`AllowsPopulationTierSync` stays a pure predicate. It is true only for a random bot. Alt bot, AddClass, and unknown are denied. Do not call it from `Refresh`, `init`, or `levelup` until an owner-reviewed integration says so. That integration must not write an alt bot's stage and must not strip on its own.

## 5. Sender facts

File: `src/Bot/AutoGearNativeAuthority.h`. Coordinate-first. The IP snapshot is the other half.

Fill `CapInput` from the sender-thread snapshot and the equipment capture. The bot thread reads the copy, not the human. Final B compares the IP epoch and `IsPB59EquipmentCurrent` before any move. Unknown, stale, or a zero cap preserves.

The fact-source matrix in `doc/PB59_CS02_NATIVE_FACT_SOURCE_MATRIX_2026-11-15.md` is the list of which copy each input uses.

## Still not decided

- Owner review of proposed stages 2 to 7 (78, 78, 83, 83, 83, 88, 92) and of the leveling-to-endgame switch.
- Whether Match should also target the sender average.
- Real Darkmoon rows, requested in coordination #64.
- Joint set-bonus search beyond the existing scorer.
- The native `MoveExact` backend and final B. Core `b7d50628782565189332af1c490c7cf67e59b3d7` and PB `881256249e` are Maintenance's later pair. This branch does not take them until Maintenance coordinates a rebase.
