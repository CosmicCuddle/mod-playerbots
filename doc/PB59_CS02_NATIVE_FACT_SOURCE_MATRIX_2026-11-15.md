# PB59 CS02 native fact-source matrix

Owner: CosmicCuddle. Coordination #59 review `6898131567`, at PB #30 `7021354829c92d9e61cc64dc12eb49ce7ea47f90`.
This is a plan. It does not wire a command, read a live player, or change a shared file.

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
