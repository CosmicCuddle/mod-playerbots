# PB59 CS02 AutoGear selection audit and implementation plan

Owner: CosmicCuddle. Coordination #59, with #41 (Darkmoon) and #52 (15 Nov workload).
Phase 1 was the read-only audit. Phase 2 adds pure selection headers and focused tests. Commands, transactions, and Core private APIs stay unwired.

## Phase 2 status

Delivered on `feature/cs02-pb59-autogear-selection-20261115`, still based on `69943153b86f872cd6a42417ea3f4b76a8ba5602`. No COORDINATE FIRST file was edited. No CMake or test-list entry was required: the new tests compile the same way as the existing `tests/pb59_autogear_*.cpp` fixtures, with `-Isrc/Bot`.

| Task | New file |
| --- | --- |
| Cap composer, including the levelling average | `src/Bot/AutoGearSelectionCap.h` |
| Shared item facts | `src/Bot/AutoGearSelectionTypes.h` |
| Constraint filler | `src/Bot/AutoGearConstraintFill.h` |
| Score filler | `src/Bot/AutoGearScoreFill.h` |
| Candidate-domain collector | `src/Bot/AutoGearCandidateDomain.h` |
| Solver call and plan | `src/Bot/AutoGearSelectionPlan.h` |
| Outcome mapping | `src/Bot/AutoGearSelectionOutcome.h` |
| One eligibility function for every command form | `src/Bot/AutoGearCommandEligibility.h` |
| RNDBOT-only tier-sync predicate | `src/Bot/AutoGearRndbotTierSync.h` |
| Cap, command-parity, and population tests | `tests/pb59_autogear_selection_cap.cpp` |
| Selection, constraint, Darkmoon, and outcome tests | `tests/pb59_autogear_selection_plan.cpp` |
| Variant-aware solver prototype | `src/Bot/AutoGearVariantSolver.h` |
| Differential variant-solver tests | `tests/pb59_autogear_variant_solver.cpp` |

CS01 review `6898131567` is applied on this branch, still based on `69943153`. It is not rebased onto PB `881256249e`.

`ComposeCap` calls `PlayerbotsAutoGearCap::Resolve`. `CapMode::Leveling` requires the sender average. `Endgame` and `Match` do not. The recorded proposal 0=66, 1=78, 2=78, 3=83, 4=83, 5=83, 6=88, 7=92 is `ProposedVanillaCompletionAwaitingOwnerReview`. Stages 0 and 1 are confirmed. Stages 2 to 7 are proposed and are not a hidden default. A zero completion entry still preserves. The era ceiling in the caller's policy still clamps.

`CollectDomains` range-checks the slot before any shift. Slots 19, 31, 32, and 255 are invalid. A complete-mask bit outside the eligible combat mask is invalid. Overflow is `Bounded` and stores nothing.

`Unknown` is not `Unavailable`. An unknown spec, role, capability, item source, or equip result preserves. It does not become a genuine no-set. Scoring starts only after that preflight, and only when the fact list is within budget.

Bags default to `Unknown`. Full or unknown bags are `TechnicalFailure` and preserve. `StripToBags` is a move of combat items into empty storage. Body and tabard stay out of the domain. Nothing is deleted. `SelectFromFacts` does not authorize recovery.

The two plans are `doc/PB59_CS02_NATIVE_FACT_SOURCE_MATRIX_2026-11-15.md` and `doc/PB59_CS02_COMMAND_WIRING_PLAN_2026-11-15.md`.

Darkmoon fixtures stay synthetic. The real rows are coordination #64.

Later corrections, still on `69943153` and not rebased onto `881256249e`: command parsing keeps the occupied master-slot match unless a caller opts into another source or denominator; an unknown mode or a mismatched form preserves; pure selection cannot permit restore; `sourceComplete` is false for Invalid and Incomplete. The fact-source matrix v2 and the set-bonus comparison are separate files.

CS01 review `6898853300` is applied on the same head. `CopyBoundedSource` keeps known-unavailable separate from unknown or malformed. Any unknown, unloaded, zero-entry, or zero-item-level row is Incomplete, so it cannot become a complete empty list and authorize a strip. `SourceEligibility` defaults to Unknown. The planner copies that source before it scores, and the fact list has to match the copy. Class (1–11), spec tab, role, and weapon kind are validated before any shift. The required mask must be a non-empty subset of the eligible mask. An allocation failure while building the domain or solving preserves, as a throwing scorer already did. `NoteRecoveryNeeded` is diagnostic only. There is no public restore grant on this lane. `SameFinalCopy` compares sender IP, sender equipment, bot equipment, session and bot incarnations, and policy, and only on immutable copies. The set-bonus demo bounds its rule count and rejects a malformed, duplicate, or overflowing rule. It invents no weight. `AutoGearVariantSolver.h` is a prototype: variant identity is entry plus random property, and unique, MaxCount, and category use the template entry. On non-variant domains it matches `AutoGearSetSolver`. That shared header is not edited. Feature off and the match default are unchanged. Darkmoon stays synthetic.

CS01 checkpoint 4 (`6899847073`) is applied on the same head. Playable classes are 1–9 and 11. Class 10 does not shift. An out-of-range `SourceEligibility` is Invalid, not eligible. Facts are bounded and validated before they are matched to native rows. A match needs entry, signed variant, and item level. Each eligible slot needs its own list state. Unknown or Incomplete stays a preserve. `completeMask` and `hasNativeSource` do not by themselves authorize a strip. Category HAVE is separate from category EQUIP. Bags, bank, equipped copies, and gems are retained counts. Any unknown count preserves. The variant solver reports an allocation fault as Incomplete and covers the HAVE/EQUIP and tie cases. Darkmoon availability and procs stay unknown. Tooltip base stats from coordination #64 comment `6900198011` (item level 66, required level 60, unique trinkets, Heroism +15 stamina) are model fixtures only and are never forced BiS. The shared solver is still unchanged.

Work order `6902772558` is applied on the same head. Slot lists now carry provenance: duplicate, missing, stale, and mismatched rows stay Incomplete or Invalid, and a strip requires a proven empty list whose set bonuses are proven absent. Retained-constraint, fixed-seed oracle, export-check, and symbolic cap-matrix fixtures are separate. The read-only adapter contract and the offline export spec are `doc/PB59_CS02_NATIVE_ADAPTER_CONTRACT_2026-11-15.md` and `doc/PB59_CS02_OFFLINE_EXPORT_VALIDATION_2026-11-15.md`. The combined note is `doc/PB59_CS02_CHECKPOINT_REPORT_2026-11-15.md`.

CS01 review `6903067303` is applied on the same head. `AssessSlotList` rejects a duplicate or a contradiction in either order, including a present row paired with an unavailable copy. HAVE and MaxCount go through the variant solver for the whole chosen set, including two variants of one template and retained copies. Native mode requires a slot proof bound to the same revision, the copied rows, and the accepted facts. A contradictory proof does not strip. A known non-empty set that cannot be worn stays a preserve. `CheckExport` uses SHA-256, separate world and spell manifests, a bounded sorted index, signed spell references, and override, proc, and source evidence. `MetadataChecked` is not production readiness.

CS01 review `6903748406` is applied on the same head. The plan keeps the chosen entry and signed random property, and copies both into the storage request. Any fact with a random property, or two properties of one entry, uses the variant solver. Each accepted fact is scored once. An unknown or inconsistent score cannot commit or strip. Proof rows, including unavailable ones, are bounded before the duplicate scan, and set evidence has to match the fact. Export hashing is a stream over file chunks. The world file and the spell file are hashed separately. A nonpositive spell record id is invalid. A negative item reference is not treated as a spell id. `MetadataChecked` is still not production readiness.

CS01 review `6903969713` is applied on the same head. The export reader hashes every declared chunk and parses counts from those bytes. A storage request records the entry, signed property, slot, and revisions, and it is not ready when a revision is missing or stale. Gear moved into bags stays in the retained count. The additive solver is not described as set-optimal. Unknown set evidence preserves.

GCC 13, Clang 18, and GCC ASan+UBSan each report 295 checks: cap 44, plan 72, command parse 22, fact source 24, set bonus 12, variant solver 20, slot enumeration 12, retained constraints 9, solver oracle 14, export check 9, cap matrix 14, adversarial 23, and checkpoint 8 20. Clang ASan+UBSan still cannot link. No hosted CI was started from here.

## Integration still waiting on Maintenance

The wiring order is `doc/PB59_CS02_COMMAND_WIRING_PLAN_2026-11-15.md`. TrainerAction is coordinate-first for that wiring as well. Do not do these until Maintenance says the shared files are free:

- Replace `ResolveCommandSenderAutoGearCap` in `src/Ai/Base/Actions/TrainerAction.cpp` so plain, reset, match, quality, numeric, and both BiS forms call `ResolveCommandEligibility`. Do not delete gear there yet.
- Pass `StatsWeightCalculator::CalculateItem` into `SelectFromFacts` from the factory path. That requires `src/Bot/Factory/PlayerbotFactory.cpp`, which is COORDINATE FIRST.
- Perform a strip or a commit only through `PlayerbotsAutoGearStorage::Execute` after Maintenance enables a `MoveExact` backend. On `RecoveryRequired`, keep using `PB59::TransactionAdmission::RequireRecovery` and `Player::ReconcilePB59VisibleEquipment` under `Access::Recovery`. `RecoveryRequest` is only the reason to ask for that. It is not the call.
- Attach `AllowsPopulationTierSync` in front of any future RNDBOT regear. `src/Bot/RandomPlayerbotMgr.cpp` is COORDINATE FIRST. Do not call it from `Refresh` or logout in this phase.
- Read the sender sample and the IP snapshot on the sender's thread before filling `CapInput`. `src/Bot/AutoGearNativeAuthority.h` is COORDINATE FIRST.

The sections below are the phase 1 audit and are unchanged except for this status.

## Pins

| Piece | Value |
| --- | --- |
| This audit | module commit `69943153b86f872cd6a42417ea3f4b76a8ba5602` (`feature/cs01-pb59-native-authority-20261115`) |
| Verified | GitHub PR #29 head was still that SHA on 15 Nov 2026. It had not moved. |
| Parent | `feature/codex-pb59-autogear-audit-20261114` at `6ab8e7bcf6a173f8bd928323449afd7d64584f0e` |
| Paired Core | CosmicCuddle/azerothcore-wotlk PR #37 head `31c0bc95628377bc509372012bc0ffc927150340` (still the head recorded in #52). Core comments below are from that blob. |
| Server scope | Vanilla Individual Progression, level cap 60, no AddClass bots |

Line numbers below are from those two commits. Labels:

- **FACT** — read from that source or from the coordination text cited next to it.
- **HYPOTHESIS** — a reading of how the pieces would behave together. Not executed here.
- **UNRESOLVED** — no approved source. Do not invent a number or a rule.

## 1. What exists

The live chat commands still gear through `PlayerbotFactory::InitEquipment`. The newer cap, sample, scan, solver, authority and storage types are compiled into the module and are not called by those commands.

### 1.1 Live selection — FACT

`AutoGearAction::Execute` and `BisGearAction::Execute` live in `src/Ai/Base/Actions/TrainerAction.cpp`.

- Both start at `ResolveCommandSenderAutoGearCap` (`TrainerAction.cpp:22-29`). If `Playerbots.AutoGearIPCap.Enable` is false, the function sets `hardCap = 0` and returns success. If it is true, the function returns failure and the command tells the player the command is unavailable. It does not read a sender, a stage, or `PlayerbotsAutoGearCap::Resolve`.
- `PlayerbotFactory::AutoGear` (`PlayerbotFactory.cpp:4357-4371`) builds a mixed gear-score limit from the requested item level and quality (`CalcMixedGearScore`, `4056-4058`, multiplier in `PlayerbotAI.cpp:6955-6976`) and calls `InitEquipment`.
- `InitEquipment` (`2356-2756`) is the only selector the commands reach.
  - Level below 5 stores the starter outfit and returns (`2358-2398`), after a raw item-level check.
  - Otherwise it walks `initSlotsOrder` (`104-107`). Tabard is absent. Body is present but `SearchAtLevel` skips body and tabard (`AutoGearSlotPolicy.h:19-31`).
  - Each candidate list comes from `RandomItemMgr::GetEquipmentNew` (`RandomItemMgr.cpp:930-944`), keyed by required level and inventory type.
  - One in four candidates is dropped with `urand` (`PlayerbotFactory.cpp:2535-2537`).
  - Expansion filtering is an item-id heuristic: level 60 skips ids at or above 23728; level 70 skips ids at or above 35570 with three exceptions (`2540-2548`).
  - Grey and white skip the mixed gear-score check unless a better quality already found something wearable (`2502-2527`, `2526-2559`). A raw item-level ceiling is applied only when `maxRawItemLevel` is non-zero (`2553-2554`). `AllowsRawItemLevel` treats 0 as unlimited (`AutoGearProgressionCap.h:53-56`).
  - The best remaining item is the highest `StatsWeightCalculator::CalculateItem` score that passes `CanEquipItem` and `CanEquipUnseenItem` (`2600-2628`). Preferred armor subclass multiplies that score by 3 (`2609-2614`).
  - Incremental mode keeps the old item unless the new score is at least 1.2 times the old score (`2640-2644`).
  - The old item is moved with `HandleAutoStoreBagItemOpcode`. If the bag move fails, the slot is left unchanged (`2646-2661`). On success the factory creates a new item with `EquipNewItem`.
  - `second_chance` destroys the equipped item before it knows the replacement will succeed (`2448-2451` and again at `2697-2698`).

`DestroyEquippedGear` (`4072-4081`) destroys every equipped slot except body and tabard. Reset, BiS fallback, and the BiS apply path call it before they know a replacement exists (`TrainerAction.cpp:319`, `466`, `772`). BiS also destroys equippable weapons and armor in bags (`471-489`) and can destroy a paired ring or trinket (`543-558`). BiS grants the item's required reputation on the bot (`518-530`).

### 1.2 Set solver — FACT

`src/Bot/AutoGearSetSolver.h:52-210`. `PlayerbotFactory::PlanEquipmentSet` (`4061-4070`) forwards to it. No command calls `PlanEquipmentSet`.

What the solver already does, on copied candidates only:

- Rejects an unknown policy, a zero cap, a bad weapon capability mask, a required weapon bit inside `requiredMask`, and any eligible slot outside the 17 combat slots. Body (3) and tabard (18) are outside `CombatMask` (`16-17`, `60-70`).
- Requires every eligible domain to be marked complete. A budget overrun or an incomplete domain returns `Incomplete`, never `NoSet` (`79-85`, `184-209`).
- Checks strictly increasing item ids, consistent metadata for the same id, item level at or under the cap, unique-equipped clashes, and item-limit categories (`87-107`, `124-140`).
- Orders weapons first and rebuilds offhand and ranged domains from the planned main hand, including an exclusive main-hand staff, polearm, or fishing pole (`114`, `163-180`, `AutoGearWeaponDomain.h:57-82`).
- Searches for the highest summed caller-supplied score, with equal scores broken by lower item id (`109-112`, `142-206`).

What it does not do: load items, score them, know class or spec, model set bonuses, gems, enchants, or currently worn instances. A `NoSet` result is meaningful only after the caller has marked the universe complete.

### 1.3 Candidate scan — FACT

`src/Bot/AutoGearCandidateScan.h:37-63` walks one already ordered slot. It returns `InvalidInput`, `Incomplete`, `CompleteEmpty`, or `CompleteWithCandidates`. `CompleteEmpty` is one slot, not a whole set (`34-35`). Nothing in `src/` calls `Scan`. The factory does not build `PlayerbotsAutoGearSet::Domain` values.

### 1.4 Native authority adapter — FACT

`src/Bot/AutoGearNativeAuthority.h:24-66` copies Core `PB59::EquipmentFacts` into a sample: session, revision, level, class, race, an explicit counted-slot mask, a 64-bit raw sum, and the fixed-denominator average from `AutoGearEquipmentSample.h:36-58`. Empty counted slots add zero and do not shrink the denominator. Heirloom quality or a scaling field makes the sample unknown. `Current` compares the whole copied fact record, not only the average.

`PlayerbotFactory::CaptureEquipmentAuthority` and `IsEquipmentAuthorityCurrent` (`4111-4132`) call `Player::CapturePB59Equipment` and `Player::IsPB59EquipmentCurrent` only when `EquipmentCaptureState.h` is present. The legacy `CaptureEquipmentSample` (`4083-4107`) uses policy revision 1 and is commented as not an authorization. No command calls either function.

Core at `31c0bc95`, `Player.h:1100-1106`: capture is owner-context raw facts, not a gear authorization. `CapturePB59InventorySlot` returns nullopt for unknown and never means empty.

### 1.5 Storage and recovery seams — FACT, not a selector

These are Maintenance's. This phase does not change them. They matter because a later selection result has to be committed through them.

- `AutoGearStorageMatching.h:31-95` assigns each original combat item a distinct empty destination. No command calls `PrepareEquipmentStorage`.
- `PlayerbotFactory::PrepareEquipmentStorage` / `ValidateEquipmentStorage` (`4135-4354`) observe exact GUID, count, and bag, recheck after `CanUnequipItem`, and put offhand first.
- `AutoGearStorageTransaction.h:56-145` is a coordinator. `MoveExact` must not merge or destroy. It returns `Rejected`, `Committed`, `RolledBack`, or `RecoveryRequired`. The native backend is not connected. The file says Core `SwapItem` is not that contract (`9-11`).
- Core `Player.h:3031-3044`: `_StoreItem` exact mode and `_EquipItem` exact mode are private insertion building blocks. `RelocatePB59InventoryLinks` is not an equip API and has no caller until reconciliation and quarantine are proven. `ReconcilePB59VisibleEquipment` restores visible fields only.

### 1.6 Cap arithmetic — FACT, unused by commands

`AutoGearProgressionCap.h:35-51` computes one cap from raw stage, effective stage, completion caps, era floors, and era ceilings, then takes the minimum of that cap, a non-zero configured cap, and a non-zero requested cap. Invalid stage pairs, a zero completion entry, a zero era ceiling, or a floor above the ceiling return `{false, 0}`.

Config load is `PlayerbotAIConfig.cpp:745-757`. Defaults in `conf/playerbots.conf.dist:649-652`:

- enable off
- completion `66,78` then seventeen zeros
- era floors `0,0,0`
- era ceilings `78,0,0`

**HYPOTHESIS:** with those defaults, `Resolve(0, 0, ...)` yields 66 and `Resolve(1, 1, ...)` yields 78. `Resolve` for raw stages 2 through 7 returns invalid because those completion entries are 0. TBC and Wrath ceilings are 0, so those eras are invalid too. This was not executed in this workspace.

### 1.7 Integration still missing — FACT

| Seam | Present | Reached by a command |
| --- | --- | --- |
| Raw cap filter inside `InitEquipment` | yes, only if the caller passes a non-zero cap | only if `hardCap` was set, and the enable flag currently refuses before that |
| `PlayerbotsAutoGearCap::Resolve` | yes | no |
| Fixed-denominator sender sample | yes | no. `autogear match` uses `GetWornItemLevelAverage` (`590-607`), which drops empty slots from the denominator |
| Candidate scan and set solver | yes | no |
| Native authority compare | yes | no |
| `ResolveNextStep` (`AutoGearCommandOutcome.h:31-44`) | yes. Complete `NoSuitableSet` requests a strip. Incomplete search, invalid policy, and infrastructure preserve | no |
| Exact storage commit | coordinator and preflight only | no |

## 2. Vanilla eligibility matrix, stages 0 to 7

IP enum names are the ones recorded in coordination #59: 0 START, 1 MOLTEN_CORE, 2 ONYXIA, 3 BLACKWING_LAIR, 4 PRE_AQ, 5 AQ_WAR, 6 AQ, 7 NAXX40. Stages 8 and above are outside this Vanilla pass. Their completion entries and era ceilings are 0, so `Resolve` fails closed.

| Stage | Recorded meaning | Cap | Status |
| --- | --- | --- | --- |
| 0 | Ragnaros not completed | 66 | Approved. Owner example in the #59 issue body. Codex recorded it as owner-confirmed. It is the first default completion entry. |
| 1 | Ragnaros completed | 78 | Approved the same way. Second default completion entry. |
| 2 | Onyxia completed | none | **UNRESOLVED / owner decision.** CS01 proposed 78 in #59. That proposal was not approved. The loaded completion entry is 0, so `Resolve` is invalid. |
| 3 | Blackwing Lair | none | **UNRESOLVED.** CS01 proposed 83. Not approved. Completion entry is 0. |
| 4 | Pre-AQ | none | **UNRESOLVED.** CS01 proposed 83. Not approved. |
| 5 | AQ war | none | **UNRESOLVED.** CS01 proposed 83. Not approved. |
| 6 | AQ | none | **UNRESOLVED.** CS01 proposed 88. Not approved. |
| 7 | Naxxramas 40 | none | **UNRESOLVED.** CS01 proposed 92. Not approved. |

`conf/playerbots.conf.dist:628-631` lists public item levels (MC/Ony/ZG 78, BWL 83, AQ40 88, Naxx40 92, and separate tier peaks 66, 76, 88, 92). That comment documents `AutoGearScoreLimit`. It is not an approved stage table. Do not copy it into completion caps.

`Playerbots.AutoGearIPCap.EraCeilings` default `78,0,0` is a provisional config value sitting next to the stage 0/1 candidates (`conf/playerbots.conf.dist:641-647`). It is not a separate approval of a Vanilla ceiling for stages 2 to 7. **HYPOTHESIS:** if a later owner cap for stage 3 or above is above 78, the current ceiling would clamp it until the ceiling is changed in the same reviewed change. TBC and Wrath floors and ceilings stay 0 until a later era is in scope.

### Composition with `Playerbots.AutoGearScoreLimit` — FACT plus the rule to implement

`Resolve` already takes `min(progression cap, configuredCap, requestedCap)` and ignores a zero configured or requested cap (`AutoGearProgressionCap.h:46-49`). The stricter non-zero cap wins.

The command path does not use that function. Today, with the option off, `AutoGearScoreLimit` becomes the factory's mixed gear-score argument, and the raw filter is off (`TrainerAction.cpp:634-638`, `773-779`). A score limit of 0 means no mixed-score filter. Grey and white can still skip a non-zero mixed-score filter.

Selection rule, once wired:

1. If enable is off, leave today's command behavior unchanged. Do not invent a hidden cap.
2. If enable is on, call `Resolve`. If it returns invalid, preserve gear. Do not pass 0 into `AllowsRawItemLevel`.
3. Pass the resolved cap in as both the solver cap and `configuredCap`/`requestedCap` so a lower score limit or a lower typed item level still wins.
4. Apply that same raw ceiling to every candidate, including grey, white, ammo, gems, and BiS rows. The mixed gear-score check is not a substitute.

### No player or no IP context — FACT and the fail-safe

- Option on: the command returns before mutation (`TrainerAction.cpp:22-29`, `359-363`, `628-632`). That is fail-closed, and it also refuses a valid sender, because no snapshot is read.
- Option off: a missing master fails only `autogear match` (`663-668`). Plain, reset, numeric, and quality commands still gear the bot with no sender cap.
- `Resolve` itself has no player argument. A missing snapshot cannot be represented as stage 0. Stage 0 is a real, more generous pre-MC state than "unknown".
- Sample average returns unknown for a zero sum (`AutoGearEquipmentSample.h:56-57`). Unknown must not become an unlimited cap and must not become `NoSuitableSet`.

Fail-safe to implement with the option on: no in-world commanding human, no IP header, unknown or stale snapshot, effective stage above raw stage, or `Resolve` invalid means preserve existing gear and report the command unavailable. Never raise the cap. Do not mutate the sender's or the bot's progression.

## 3. Levelling gear selection

Owner requirement in #59: the item-level target is the commanding human's own equipped item level, and that target is also bounded by the IP tier, the effective expansion, the global and requested limits, and what the bot can equip. Bot level still limits equip eligibility. This server's level cap is 60. Bands 61-70 and 71-80 stay fail-closed with the zero TBC and Wrath ceilings. No levelling curve is approved, and this design does not add one.

Proposed target, using only rules already recorded:

```
rawCap = Resolve(rawStage, effectiveStage, policy, AutoGearScoreLimit, typedRequest)
target = senderSample.average          # fixed denominator, empty counted slots are zero
selectionCap = min(target, rawCap)     # both must be known and non-zero
```

Then a candidate is eligible only if all of the following hold:

- its raw `ItemLevel` is at most `selectionCap`
- `RequiredLevel` is at most the bot's level
- `CanEquipItem`, armor skill, and the weapon domain accept it
- `SearchAtLevel` still searches that slot at the bot's level
- the item is inside the bot's class, race, and skill limits

The sender's average is the target at every Vanilla level, including 60. The stage cap is always an upper bound, including while the human is levelling. A CS01 note that the average would apply only while levelling, and the stage cap only at endgame, is a staging interpretation, not an approved switch. **UNRESOLVED:** the exact counted-slot mask, heirloom treatment beyond "unknown", and whether a two-hand main hand changes the denominator. The sample header already refuses unresolved heirlooms. Do not pick a production mask in this phase.

`autogear match` (`590-607`, `661-688`) is the old behavior: occupied slots only, so removing gear raises the average. That is the exploit the later sample was written to close. The new path replaces it. Until the new path is wired, match stays on the old average and, with the option off, still runs.

Bot level does not generate an item level. High item level with a low required level is **UNRESOLVED**. The id heuristic at `2540-2548` is not a source-era proof. Such an item stays out of the eligible set until an owner rule says otherwise. Missing sample, missing stage, or sample average 0 preserves gear. It is not a strip.

## 4. Constraint review

| Constraint | Present | Missing for a complete set |
| --- | --- | --- |
| Class | `CanEquipWeapon` is a class switch (`2118-2184`). `CanEquipArmor` checks plate, mail, leather, cloth, and shield skills (`2003-2026`). | The solver never sees class. Weapon capabilities are not built from skills or from Core `dualWield` / `titanGrip`, even though `EquipmentFacts` carries those flags. |
| Spec | `AiFactory::GetPlayerSpecTab` chooses a BiS row. Druid feral tank uses sentinel tab 10 (`418-420`). `StatsWeightCalculator` weights the live spec. PvP specs flip the calculator and force a CC-break trinket (`2413-2474`). | The solver has no spec. BiS refuses a PvP spec entirely (`351-356`) instead of scoring PvP gear. |
| Role | Tank is used only for that druid tab. | Healer, melee, and caster are implicit in stat weights. No role mask is passed to the solver. |
| Weapon | Class subclass list, plus the pure domain builder (`AutoGearWeaponDomain.h`). Offhand is skipped in the factory when a two-hander is equipped and the bot cannot Titan Grip (`2487-2488`). | Factory and solver are not connected. `CanEquipWeapon` is not the same predicate as Core `CanUseItem`. |
| Armor | Skill check plus a 3x score for the class-preferred subclass (`779-803`, `2609-2614`). | Preferred subclass is a score bias, not a hard filter. A cloth wearer can still be offered leather if the skill exists. |
| Slot | `SearchAtLevel`: no trinkets below 50, no neck or head below 30, no rings below 20, and below 5 only main hand, offhand, feet, legs, chest, and ranged. The level-below-5 factory path returns before that loop. | The solver's required mask is not filled from this layout. Body and tabard are excluded by both. Shoulders, waist, wrists, hands, and back are searched from level 5. Whether every searched slot is mandatory is **UNRESOLVED**. CS01 said mandatory slots follow the existing level and class rules, not "every slot". |
| Unique-equipped | Solver rejects two copies when either candidate is flagged (`124-133`). BiS destroys the paired slot and retries (`543-558`). | Nothing sets `uniqueEquipped` from `ITEM_FLAG_UNIQUE_EQUIPPABLE`. The factory relies on `CanEquipUnseenItem` one item at a time, after a random skip. |
| Item-limit category | Solver enforces one category limit per category id. | Gems are applied later. Jeweller gems are a hard-coded category 2 inside `ApplyEnchantAndGemsNew` (`5690`). That limit is not part of the set. |
| Gems | `ApplyEnchantAndGemsNew` (`5540` onward) scores socket gems, honors meta activation, filters gem item level when a raw cap is set (`5548-5549`), and skips WotLK gems for a bot at or below 70 when expansion limiting is on. | Gems are not candidates in the set. A set score ignores socket bonus interaction across pieces except what `CalculateItem` adds for the single piece (`StatsWeightCalculator.cpp:158`). |
| Enchants | Same finisher. Permanent enchants are chosen by `CalculateEnchant`. Level 60 skips enchant spells at or above 27899; level 70 skips spells at or above 44483 (`5607-5610`). Belt buckle is refused at or below 70 when that limit is on (`88-89`). | Spell-id cutoffs are not IP stages. Enchants are applied after items exist, so a failed enchant is not a failed set. |
| Set bonus | `CalculateItem` includes set bonus by default (`StatsWeightCalculator.cpp:88`, `115-156`). The factory scores one slot at a time against items already equipped. | The solver sums independent scores. It cannot trade a weaker piece now for a set bonus later unless the caller encodes that in the score. Encoding it per slot without a joint search is a **HYPOTHESIS** that the current greedy path already mis-ranks sets. Do not treat a greedy result as `NoSet`. |

`CanEquipItem` (`2186-2211`) also rejects timed items, quest binding, required level above the bot, and a zero-required-level item the bot already has. It does not check unique, faction, or skill.

## 5. Darkmoon cards

Coordination #41: consider the owner's customized Vanilla cards by their real stats. Never hard-code them as BiS. The custom numbers are not in this repository.

### How discovery treats them today — FACT

BiS does not scan trinkets. `BisListMgr::LoadAll` (`BisListMgr.cpp:14-43`) loads `playerbots_bis_gear`. `GetBisForNearest` (`74-92`) returns the nearest lower tier within 20 item levels for class, spec tab, and faction. The command then creates those item ids (`TrainerAction.cpp:512-566`). If the table names a card, that command forces that entry. Stat weight is not consulted.

The checked-in table already names public cards, including Vanilla ids 19288 (Blue Dragon) and 19289 (Maelstrom), at specific tier keys. Examples in `data/sql/playerbots/base` history and `data/sql/playerbots/updates/2026_04_28_00_playerbots_bis_gear.sql`: priest rows at key 120 use 19288; paladin and shaman rows at keys 88 and 92 use 19289. Later-era rows use 31858, 42987, 44253, and 44255. Those rows are a curated force. They are not a stat evaluation of Michael's database.

Factory AutoGear can still roll a trinket from `GetEquipmentNew` if the live `item_template` is a trinket of the requested quality and required level. Scoring then reads the process's `ItemTemplate` and a few hard-coded proc spells. `StatsCollector.cpp:401` special-cases spell 39442, Darkmoon Card: Wrath, which is not a Vanilla card. No Vanilla card id is special-cased in that function. A custom card is therefore scored only if its template stats and spells happen to be collected by the generic path.

There is no "always equip this card" branch. The BiS table is the force. New selection must not add another one, and it must not add rows that name cards in order to make them win.

### Data required before any card can be scored — UNRESOLVED

For each customized card, from the owner's installed database, not from a public DBC:

- item entry and name
- whether the entry replaces a public card or is a new row
- item level, quality, required level, class, subclass, inventory type
- bonding, `MaxCount`, unique-equipped flag, item-limit category and limit
- allowable class and race
- every `ItemStat` type and value
- every spell id on the item, and that spell's effects, amounts, proc chance or ppm, and cooldown
- sockets and socket bonus
- item set id, if any
- duration
- how the character obtains it, and any reputation required

Public item data must not be used to fill a missing stat. Until that extract exists, cards stay ordinary candidates if they appear in the live template cache, and they are ineligible when their template is unknown. A BiS row that names one of these entries is a force and is out of policy for #41. Removing or ignoring those rows is an owner decision; this phase does not edit SQL.

## 6. Command variants and the eligibility path

Chat registration is only `autogear` and `autogear bis` (`ChatCommandHandlerStrategy.cpp:121-122`, `ChatActionContext.h:143-144`). One argument is accepted. A second token such as `green 200` does not match a quality word or an all-digit string and is rejected.

| Variant | Where | Same eligibility path? |
| --- | --- | --- |
| `autogear` | `609-779`, incremental, no second chance | Shared prefix only: command enabled, alt-bot account check (`618-624`), IP-enable deny. Then mixed score, no raw cap while the option is off. |
| `autogear reset` | `648-657`, `768-775`. Destroys, then non-incremental, two rounds. | Same prefix. Destruction happens before selection. |
| `autogear reset <quality>` | reset flag plus the quality branch | Same prefix. |
| `autogear reset <number>` | reset flag plus the number branch | Same prefix. Numbers 1 through 5 are rejected as quality-like (`724-734`). |
| `autogear reset match` | reset is stripped first, then `match` runs, then the reset destroy runs | Same prefix, plus the old occupied-slot average. Still destroys. |
| `autogear match` | `661-688` | Same prefix. Reads `GetMaster()` on the bot thread. Not the fixed-denominator sample. |
| `autogear <quality>` | `690-702`. Words in `ChatHelper.cpp:45-62`: poor/gray/grey, normal/common/white, uncommon/green, rare/blue, epic/violet/purple, legendary/yellow/orange, artifact, heirloom. | Same prefix. Quality is clamped to `AutoGearQualityLimit`. Item-level cap is unchanged. |
| `autogear <number>` | `704-746`. Six or more, up to 5 digits. | Same prefix. Clamped to the score limit when that limit is non-zero. Not clamped to a stage. |
| `autogear bis` | `326-578` | Shared prefix, then extra gates: `AutoGearBisCommand` default off, quality limit at least 4, bot not a PvP spec. Table lookup. Destroys gear and bag equipment. Does not call the solver. |
| `autogear bis <number>` | `369-407` | Same BiS gates. Any positive 16-bit integer is an item level, including 1 through 5, which plain `autogear` rejects. With no `hardCap`, the number is refused when it is above `AutoGearScoreLimit`. The default score limit is 0, so every positive number is refused and no gear changes. |
| Unknown text, including `autogear bis` if it were passed as the plain argument | `716-721` | Refuses. No gear change. The real BiS command is the separate trigger, so this branch is the leftover. |
| `reset` followed by a tab instead of a space | `651` requires the prefix `reset ` | **HYPOTHESIS:** `reset\tgreen` does not enter the reset parser and falls through to the unknown-argument error. Not executed here. |

Gaps, all FACT unless marked:

1. No variant calls `Resolve`, the sample, the scan, the solver, the authority compare, or `ResolveNextStep`.
2. Reset and both BiS destroy paths delete gear before a complete search. That cannot be a genuine no-set strip.
3. The 25% skip means an empty factory result is not `searchComplete`. `AutoGearCommandOutcome.h:39-40` would preserve, but the factory never asks it.
4. Raw item-level filtering is off whenever `hardCap` is 0. Grey and white skip the mixed score.
5. `match` uses the shrinking average and a live `Player*`.
6. BiS and plain numeric parsers disagree on 1 through 5. `AutoGearBisCommand` defaults off, so none of this runs until it is enabled. Once it is, a score limit of 0 makes `autogear bis <number>` refuse every positive number, while plain `autogear` treats item level 0 as "any item level" and still gears (`4357-4362`, `762-766`). `GetBisForNearest` asked for item level 0 does not enter its scan (`BisListMgr.cpp:77-78`), so enabled `autogear bis` with no number and no score limit falls through to the destroy-and-AutoGear fallback.
7. BiS can change bot reputation. That is not an item-level check.
8. Alt-bot refusal uses `IsInRandomAccountList` (`618-619`), not `IsRandomBot`. A character on a random-bot account who is not in `currentBots` is not refused. **HYPOTHESIS:** that is wider than "this bot is a live RNDBOT".
9. Adjacent gearing commands do not use this prefix at all. They call `Randomize` from `PlayerbotMgr.cpp`: `init=white/common`, `green/uncommon`, `blue/rare`, `epic/purple`, `legendary/yellow`, `init=auto` (`931-941`, mixed gear score of the master), `init=<number>` (`943-947`), and `level` / `levelup` (`960-964`). GM `initself=rare/epic/legendary` is the same factory on the master (`1090-1129`). `maintenance` (`TrainerAction.cpp:202-239`) does not equip, but on an alt bot it can still complete attunement quests when that option is on. `RandomPlayerbotMgr::Refresh` (`2445-2474`) calls `PlayerbotFactory::Refresh`, whose equipment call is commented out (`1104-1111`). None of these read a stage cap.

## 7. Test plan

Pure tests can be added later beside the existing `tests/pb59_autogear_*.cpp` fixtures. They must not include a native transaction header or edit the retirement fixtures. No test in this phase may call `DestroyItem`, `_StoreItem`, or `_EquipItem`.

| Case | Assert |
| --- | --- |
| Valid selection | Known stage 0 and a non-zero sender sample produce `selectionCap = min(66, sample, score limit, request)`. A complete domain with one legal set returns `SuitableSet` and `CommitPreparedSet`. Stage 1 uses 78 the same way. |
| Unavailable candidate | An item above the cap, above the bot's required level, or failing the weapon domain is absent from the chosen set. A BiS row whose template is missing or over the cap does not force that slot. |
| Tier boundary | Stage 0 accepts item level 66 and rejects 67. Stage 1 accepts 78 and rejects 79. A lower `AutoGearScoreLimit` wins. A zero score limit does not raise the stage cap. |
| Unresolved stages | Raw stages 2 to 7, and any TBC or Wrath effective stage, with the current tables, are invalid and preserve. No fixture may treat 83, 88, or 92 as an expected production cap. |
| Levelling | A level-20 bot cannot receive required level 60. A level-60 bot of a stage-0 sender cannot receive item level 67 even if the sender's average is higher. An empty counted slot does not raise the average. A missing sample preserves. |
| Retained equipment | An equipped item over the cap is not a fallback. It is either replaced by a legal set or, on a complete no-set, included in the strip plan. An equipped legal item may remain as the solver's choice. The 1.2 incremental threshold is not evidence that the item is legal. |
| Invalid combination | Two unique-equipped copies, a category overflow, a two-hand plus offhand without Titan Grip, and an exclusive main-hand plus offhand are `NoSet` or a different set, never a partial equip. |
| No valid set | Complete domains, exhausted search, valid authority: `NoSet` and `RequestStrip`. The same domains with `complete = false`, a hit budget, or the factory's 25% skip: preserve. |
| No context | Option on, missing sender, missing IP header, stale stamp: preserve, including when the bot is naked. |
| Command parity | Every variant in section 6, given the same sender snapshot, produces the same `selectionCap` and the same preserve-or-strip decision. BiS may add a table lookup only after that cap. `init=*` stays out of this assertion until Maintenance accepts a shared gate. |

Native, no-IP, relog, and gameplay runs stay on Maintenance's build. This selection plan does not schedule them.

## 8. Failure taxonomy

`ResolveNextStep` (`AutoGearCommandOutcome.h:31-44`) is already the decision table.

| Situation | Result | Gear |
| --- | --- | --- |
| Authority valid, search complete, solver `SuitableSet`, storage plan ready | `CommitPreparedSet` | Replace through the transaction below. |
| Authority valid, search complete, solver `NoSet` | `RequestStrip` | Approved policy: strip so the miss is obvious. Scope is equipped combat slots, not bank, mail, or unrelated bags. |
| Search incomplete, random skip, budget stop, `NotPrepared` | preserve | Not a strip. |
| Unknown, stale, or missing authority | preserve | Not a strip. |
| Invalid policy, unresolved stage, missing sample | preserve | Not a strip. `InvalidPolicy` is not `NoSuitableSet`. |
| Allocation failure, full bags, inspect unknown, move threw, context lost | `InfrastructureFailure` | Preserve or restore. Not a strip. |

Strip semantics that are already recorded, and not widened here: move the original instances into preflighted empty backpack or bag slots. Full bags are infrastructure failure and preserve everything. Destruction is not approved. Body, tabard, and equipped bags stay. The same rule applies to a RNDBOT and an alt bot when a human's command is authorized. No autonomous strip on refresh.

### Technical failure: which API to call

Do not change these APIs. Do not call the private Core insertion functions from selection code while their own comments still say they have no caller.

On a technical failure after a move has started, the recovery call is the one already written:

1. Revalidate with `Player::IsPB59EquipmentCurrent` and `PlayerbotFactory::IsEquipmentAuthorityCurrent` before any commit. A failed revalidation preserves.
2. Admit with `PB59::TransactionAdmission::ExternalScope` for `Work::InventoryMutation`, and take `PB59::TransactionAdmission::MutationScope`, only on the bot's owner context. `CanBegin` must be true. This ledger is `PB59TransactionAdmission.h` at Core `31c0bc95`.
3. Run `PlayerbotsAutoGearStorage::Execute` (`AutoGearStorageTransaction.h:56-145`). Its `MoveExact` backend, when Maintenance enables one, is the private `Player::_StoreItem` / `Player::_EquipItem` exact-reservation pair plus `Player::RelocatePB59InventoryLinks` (`Player.h:3031-3040`). Selection does not implement that backend.
4. If `Execute` returns `RolledBack`, the originals are still in their source slots. Leave them. Do not strip.
5. If it returns `RecoveryRequired`, do not call `Finish`. `MutationScope`'s destructor calls `RequireRecovery`. Visible fields, if Maintenance has already proved that helper, are reconciled with `Player::ReconcilePB59VisibleEquipment` under `Access::Recovery` (`Player.h:3042-3044`). Still do not strip.
6. `Finish` is allowed only with all four `StableProof` fields true, after inventory, effects, and the save queue are proven. Selection cannot prove the save queue. That proof stays with Maintenance.

Until step 3 has a Maintenance-approved backend, every technical failure stops at step 1 or at `Rejected` and the original gear stays where it is. `DestroyEquippedGear`, `EquipNewItem`, and `SwapItem` are not recovery.

## 9. RNDBOT-only tier sync

**FACT:** nothing in this module writes an Individual Progression stage. There is no tier-sync function.

**FACT:** `RandomPlayerbotMgr::IsRandomBot` (`2492-2513`) is true only for a random-account character that is also in `currentBots`. `PlayerbotAI::IsAltBot` (`PlayerbotAI.cpp:4616-4619`) is a client-mastered bot that is not a random bot and not a self-bot. The server has no AddClass bots; `IsAddclassBot` (`2516-2531`) must not become a reason to sync or to skip the alt-bot rule.

**FACT:** population refresh does not re-read a stage and does not re-equip (`RandomPlayerbotMgr.cpp:2445-2474`, `PlayerbotFactory.cpp:1104-1111`). New random bots geared by `Randomize` / `InitEquipment` pass `maxRawItemLevel` 0, so the raw cap is off.

**FACT:** alt-bot progression is not locked by a dedicated guard. What exists instead:

- AutoGear can be refused for non-random accounts when `AutoGearCommandAltBots` is 0. The default is 1, so alt bots are geared on request. That changes equipment, not an IP stage.
- `MaintenanceAction` calls `InitAttunementQuests` for a non-alt always, and for an alt when `AltMaintenanceAttunementQs` is on (`213-239`). Those quest ids (`conf/playerbots.conf.dist:697`) are not the 66001-66018 markers #59 named. They are still quest writes on an alt.
- BiS `SetReputation` (`518-530`) writes the bot's reputation for both populations when the command runs.

**Missing:** a positive `IsRandomBot` gate in front of any future population regear, and a negative "never call quest reward, reputation grant, or an IP setter" gate for `IsAltBot`. Selection for a commanded alt bot may change that alt's equipped items under the sender's cap. It must not change the alt's progression record, quest markers, or paid era.

**HYPOTHESIS:** "tier sync" means a later job that regears logged-in RNDBOTs up to the current stage cap when the realm stage changes. That job is not in this tree. It must not be attached to `Refresh` or `TryLogoutPlayerBot` without the coordination in section 10. AddClass stays out of it.

## 10. Implementation plan

Each task is independent and stops before the commit. None of them edit Core. None of them enable `AutoGearIPCap.Enable`. Shared files are marked **COORDINATE FIRST**: do not open them until Maintenance confirms the native transaction, recovery, lifecycle, and save work is not also editing them, and until Codex's recovery-handoff fixture is not landing in the same test files.

### COORDINATE FIRST

Module files that already contain Maintenance's transaction, recovery, or lifecycle code:

- `src/Bot/Factory/PlayerbotFactory.cpp`
- `src/Bot/Factory/PlayerbotFactory.h`
- `src/Bot/PlayerbotMgr.cpp`
- `src/Bot/PlayerbotMgr.h`
- `src/Bot/RandomPlayerbotMgr.cpp`
- `src/Bot/RandomPlayerbotMgr.h`
- `src/Bot/AutoGearStorageTransaction.h`
- `src/Bot/AutoGearStorageMatching.h`
- `src/Bot/AutoGearExactStorageObservation.h`
- `tests/pb59_autogear_storage_transaction.cpp`
- `tests/pb59_autogear_storage_matching.cpp`
- `tests/pb59_autogear_exact_storage_observation.cpp`
- `tests/pb59_bot_retirement_body.cpp`
- `tests/pb59_test_bot_retirement.py`

`src/Bot/AutoGearNativeAuthority.h` is the consumer of Core capture and revision. Treat it as **COORDINATE FIRST** as well. A selection change that redefines `Current` is an authority-contract change.

Core files at `31c0bc95`, not in this repository, and not to be edited from a selection task: `Player.h`, `Player.cpp`, `PlayerStorage.cpp`, `PlayerUpdates.cpp`, `PB59TransactionAdmission.h`, `ExactStorageGate.h`, `PB59InventoryCustody.h`, `PB59InventorySlotFacts.h`, `PB59VisibleEquipment.h`, `EquipmentCaptureState.h`, and the `tests/pb59` admission, retirement, custody, scheduled-save, and economic fixtures. Codex's recovery-handoff fixture was assigned after this pin and is not in this module tree. Any new file under Core `tests/pb59` for that handoff is **COORDINATE FIRST**.

### Selection tasks

These tasks add new headers and fixtures, or only read the shared files.

1. **Cap composer.** New helper beside `AutoGearProgressionCap.h` that returns the section 3 `selectionCap` or invalid. Fixture: stage 0, stage 1, score-limit minimum, zero score limit, stages 2-7 invalid on the current table, missing sample invalid. Do not fill stages 2-7.
2. **Domain collector.** New header that copies one slot's templates into `PlayerbotsAutoGearSet::Candidate` and calls `Scan`. It sets `complete` only when the source range was fully visited. It does not call `urand`. It does not include a card unless the template is present and under the cap.
3. **Constraint filler.** New header that sets weapon kind, exclusive main hand, unique-equipped, and category fields from template flags. It does not grant reputation or skills. Class weapon and armor predicates are copied from the factory rules only after a **COORDINATE FIRST** agreement if the copy has to live in `PlayerbotFactory.cpp`. Until then the filler takes those booleans as arguments.
4. **Score filler.** Call `StatsWeightCalculator` for a copied score. Do not edit that class. Set-bonus coupling across a set stays **UNRESOLVED** and is not implied by summing slot scores. Gems and enchants stay outside the set score until an owner rule says they change which item wins.
5. **Solver call.** A new function builds the policy mask from `SearchAtLevel` plus the weapon domain and calls `PlanEquipmentSet`. `NoSet` requires every included domain `complete`. This function returns a plan. It does not equip.
6. **Outcome mapping.** Call `ResolveNextStep` unchanged. Map `RequestStrip` to a storage plan of the current combat items and `PreserveGear` to an empty plan. Do not call `Execute` in this task.
7. **Command parity specification tests.** A table of the section 6 variants and the one cap they must share. The test feeds parsed arguments into the composer. It does not replace `TrainerAction` yet. Wiring `TrainerAction.cpp` is a later integration task and deletes no items. `init=*` and `maintenance` stay documented gaps until Maintenance accepts a shared gate in `PlayerbotMgr.cpp` (**COORDINATE FIRST**).
8. **RNDBOT gate.** A new predicate: population regear is allowed only when `IsRandomBot` is true and `IsAltBot` is false. The predicate does not call `Refresh`. Attaching it to refresh or logout is **COORDINATE FIRST** on `RandomPlayerbotMgr.cpp`.
9. **Darkmoon.** No code. Wait for the section 5 extract. A later scorer treats those rows as normal trinkets. No new BiS SQL.

### Integration dependencies

- Task 5 needs tasks 1-4.
- Task 6 needs task 5 and the existing outcome header.
- Replacing `DestroyEquippedGear` inside `TrainerAction.cpp` needs task 6 and a Maintenance statement that `Execute` may be called. Until that statement, the command keeps today's behavior and the option stays off.
- The sender sample needs a cache-only IP snapshot with raw and effective stage, session, and revision, published on the sender's thread. That provider is the IP module, not this repository. `GetEffectiveProgression` stays unused on the bot thread because #59 records a possible blocking account read.
- Final B is `IsPB59EquipmentCurrent` plus the same policy revision and counted mask. Selection does not invent a second stamp.
- Commit and restore go through section 8. Selection does not add a save, a logout lease, or a `Finish` proof.

### Open questions

For the owner:

- Approve or reject a cap for stages 2, 3, 4, 5, 6, and 7. Do not use 78, 83, 88, or 92 until then.
- Confirm the Vanilla era ceiling stays 78 only as the stage-1 bound, and must be raised in the same change as any higher approved stage.
- Confirm the sender's fixed-denominator average is the target at level 60 as well as while levelling.
- Confirm strip means move combat items into empty bags, and full bags preserve rather than delete.
- Supply the Darkmoon extract. Say whether existing `playerbots_bis_gear` rows that name cards should be ignored.
- Decide the high item-level, low required-level case.
- Decide whether a population regear of RNDBOTs is in this workstream.

For Maintenance:

- When, if ever, selection may call `MutationScope`, `RelocatePB59InventoryLinks`, and `ReconcilePB59VisibleEquipment`.
- Confirm `RecoveryRequired` plus `RequireRecovery` is the technical-failure contract, and that selection must not `Finish`.
- Confirm `PlayerbotFactory.cpp`, `PlayerbotMgr.cpp`, and `RandomPlayerbotMgr.cpp` are free before any command wiring or RNDBOT hook.
- Confirm Codex's handoff fixture path so selection tests are not added beside it.

## Out of scope

No source edit in this phase. No SQL, config, database, or production change. No merge. The option remains default off. Stages 8 and above, AddClass, and TBC or Wrath caps are not decided here.
