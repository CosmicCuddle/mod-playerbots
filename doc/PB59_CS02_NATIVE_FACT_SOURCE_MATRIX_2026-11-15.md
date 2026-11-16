# PB59 CS02 native fact-source matrix

Owner: CosmicCuddle. Coordination #59. This is a plan. It does not wire a command or change a shared file.

## v4

Playable classes follow `CLASSMASK_ALL_PLAYABLE`: 1 through 9, and 11. Class 10 is a validation failure and does not shift. `SourceEligibility` accepts only Unknown, KnownUnavailable, and KnownEligible. Any other value, including 255, is `CopyStatus::Invalid`.

`SelectFromFacts` bounds the fact list and classifies every fact before it cross-matches a native row. A match is entry, signed random property, item level, and slot. `slotLists` is per slot and defaults to Unknown. A native no-set requires every eligible slot to be Complete and the copied rows to be that same set. `completeMask` or `hasNativeSource` alone does not strip.

`categoryLimit` is the EQUIP cap. `categoryHaveLimit` is the HAVE cap. MaxCount and HAVE use retained bags, bank, equipped copies, and gems. A missing record or any unknown count preserves. The shared solver is not given the HAVE cap. The variant solver applies the two caps separately and returns Incomplete when a required count is unknown or when allocation fails.

Darkmoon cards stay Unknown while availability or the proc is unknown. Item level 66, required level 60, and unique trinket are fixture fields from coordination #64 comment `6900198011`. They are not a score and `requestForceBis` does not accept them.

## v3

`SourceEligibility` is Unknown, KnownUnavailable, or KnownEligible. The default is Unknown. Known-unavailable and known-incapable rows are omitted and can leave a complete source. An unloaded template, unknown capability, unknown eligibility, a zero entry, or a zero item level makes the copy Incomplete and clears it. `SelectFromFacts` runs that copy when `hasNativeSource` is set, before scoring, and preserves unless the fact list is the same set of variants. A complete empty known source can still be a genuine no-set when bags are Enough. An unknown row cannot.

`AutoGearVariantSolver.h` is a prototype beside the shared solver. It does not edit `AutoGearSetSolver.h`. The equipped identity is entry plus signed random property. Unique-equipped, MaxCount, and category limits use the template entry. On a non-variant domain the chosen entries, score, and status match `PlayerbotsAutoGearSet::Solve`. Past the candidate budget the result is Incomplete, not NoSet. The shared solver still cannot store two properties of one entry, so that case stays Incomplete on the old path (`IncompleteVariants`).

Class, spec tab, role, and weapon kind are rejected before a bit shift. WotLK classes are 1 through 11. The required mask must be a non-empty subset of the eligible mask. An allocation failure in scoring, domain build, or solve preserves. `NoteRecoveryNeeded` only records a reason. This lane has no function that permits restore. The native transaction owner still has to prove that.

`SameFinalCopy` requires both copies to be immutable and compares sender epoch, bot epoch, sender IP revision, sender equipment revision, bot equipment revision, session incarnation, bot incarnation, and policy revision. A zero revision does not match. It does not read a live `Player`.

The set-bonus comparison accepts at most four rules. A zero set id, a zero piece count, a duplicate set id, a duplicate piece id, or a bonus that would overflow is bounded and scores zero. No rule means the joint score is the sum of the base scores.

## v2

A loaded item template is not an eligible source. Eligibility is a separate fact (possession, availability, and a real variant). `CopyBoundedSource` refuses to copy when the native row count exceeds the budget, before it builds the copied vector. Two random-property variants of one entry are both retained, and `sourceComplete` stays false so a short list is not treated as the whole universe. The shared solver is not changed to store those variants.

Static capability (class, level, armour skill, weapon kind, and a known equip result) is not the final-set check. A `CanEquip` failure caused only by gear that a strip would remove stays a candidate. Unique, category, and weapon pairing stay on the existing solver.

Final B compares an immutable `OwnerThreadCopy`. It does not call `IsPB59EquipmentCurrent` or any other live `Player` from another thread. Quality and BiS gates stay on the command parse. A genuine no-set still requires `BagSpace::Enough`. Full or unknown bags stay a technical preserve.

Explicit `match` defaults to the master's occupied combat-slot average, capped by config. `MatchSource::CommandingSender` and `MatchDenominator::FixedCountedSlots` are opt-in and await owner confirmation. Feature off does not call `ComposeCap`.

## v1 rows

Selection code only accepts facts the caller has already copied. A missing fact is `Unknown` and preserves. It is not an unavailable item and it is not a genuine no-set. Nothing here is allowed to dereference a human `Player` from the bot's map thread.

| Input | Who supplies it | Thread | Freshness |
| --- | --- | --- | --- |
| Raw and effective stage | Individual Progression cache-only snapshot, keyed by the sender GUID. Not `GetEffectiveProgression` on the bot thread. | The sender's own map thread publishes. The command reads the copy. | Anything other than Known denies. Compare session epoch and revision again at final B, before a move. |
| Levelling average | `Player::CapturePB59Equipment` and the fixed-denominator sample. Not `GetWornItemLevelAverage`. Used only in `CapMode::Leveling`. | Sender owner thread. | `IsPB59EquipmentCurrent` at final B. Unknown or zero preserves. Endgame and Match do not require it. |
| Stage and era cap | `PlayerbotsAutoGearCap::Resolve` on the caller's `Policy`. Stages 0 and 1 are confirmed at 66 and 78. Stages 2 to 7 are the recorded proposal and stay configurable. | Pure function. The policy is loaded config, not a per-tick query. | A zero completion entry or a zero era ceiling is invalid and preserves. Item level 0 is never unlimited. |
| Score limit | `Playerbots.AutoGearScoreLimit` passed as `Resolve`'s configured cap. | Config load. | Zero means unset. It does not replace a missing stage. |
| Typed item level | The numeric chat argument, and only for the numeric and `bis` numeric forms. | The command event. | A present value of 0 preserves. |
| Levelling versus endgame | An explicit `CapMode`. `ModeForForm` returns Match for the match form and otherwise uses the caller's leveling flag. | The caller. | This function does not infer the transition from level 60. That transition still needs the owner. |
| Bot level | The bot's real level, with `levelKnown`. | Bot owner thread, at selection, and again at final B. | Unknown preserves. A known level below the item's required level is Unavailable. |
| Class | The bot's real class, with `classKnown`. | Bot owner thread. | Unknown preserves. A known class outside the item mask is Unavailable. |
| Spec | The bot's real spec tab, with `specKnown`. | Bot owner thread. | Unknown preserves the whole plan. It must not skip every spec-restricted item and then report no set. |
| Role | A known role. | Bot owner thread. | Unknown preserves. A known mismatch is Unavailable. |
| Weapon capability | Dual wield, Titan Grip, and the allowed weapon kinds, with `weaponsKnown`. Core `EquipmentFacts` carries the two booleans. | Bot owner thread. | If a weapon slot is eligible and the capability is unknown, preserve. |
| Armour capability | Skill bits, with `armorKnown`. | Bot owner thread. | Unknown while the item has an armour subtype preserves. A known miss is Unavailable. |
| Item source and template | Caller-copied `ItemFact` with `sourceKnown` and `metadataKnown`. | The thread that read the template. | Unknown source preserves the whole plan. A known item over the cap, or above the bot's level, is Unavailable. |
| Equip result | `equipKnown` / `equipAllowed` from the bot's real `CanEquip` / `CanUse` check. | Bot owner thread. | Unknown preserves. A known rejection is Unavailable. |
| Unique and category | Template flags on the fact. The existing set solver enforces them. | With the template read. | A half-specified category is Invalid and preserves. |
| Gems and enchants | Template fields. Over the cap is Unavailable. A flagged gem or enchant with item level 0 is Invalid. | With the template read. | Multiplicity and the real socket result stay a native final check. This score does not add them again. |
| Set bonus | The existing scorer's number, unchanged. `setPermitted` is only a known gate. | After `Classify`, and only for an accepted fact. | A joint set search is still unresolved. Do not add a second weight. |
| Bag space | Storage preflight. The plan defaults to `BagSpace::Unknown`. | Bot owner thread. | Enough is required before a commit or a move-to-bags. Full or unknown is `TechnicalFailure` and preserve. |
| Population | `IsRandomBot` versus alt bot. `AllowsPopulationTierSync` is the only decision. | The caller. | The predicate does not write a stage. An alt bot is always denied for population regear. |
| Command authority | IP snapshot plus the equipment stamp. | Facts copied on the sender thread. Final B on the bot owner thread. | Stale or missing authority preserves. |

`StripToBags` means moving combat items into empty storage. Body and tabard stay equipped. No path deletes an item. A recovery reason is recorded only when `mutationStarted` is already true. `SelectFromFacts` never sets that flag, so a pure plan cannot authorize restore.

Darkmoon rows are still unknown. Coordination #64 is the evidence request. Fixtures keep the synthetic ids 910001, 910002, and 910003.
