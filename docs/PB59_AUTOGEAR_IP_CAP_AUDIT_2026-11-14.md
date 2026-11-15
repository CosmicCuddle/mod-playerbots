# PB59 AutoGear progression cap: source audit

**Status:** source audit and default-off implementation candidate on PB `3a9e39f1` and IP `05ff032b`. Command gear outcomes remain unchanged with the new option disabled. Tracked in [coordination Issue #59](https://github.com/CosmicCuddle/Naxxramas-Server-Coordination/issues/59).

The branch now contains an unmerged source candidate: deferred chat triggers retain a sender GUID; AutoGear and BiS resolve that sender at execution, read optional IP, and pass the resolved hard cap to the factory. BiS filters raw item templates before deleting gear; factory finishers reject over-cap ammo and gems. The new option is disabled by default. Synthetic policy fixtures use later-era values solely to exercise raw/effective stage behavior.

## Commanding player and IP authority

`PlayerbotAI::HandleCommands` resolves the sender's GUID and checks command permission before passing the sender to `ExternalEventHelper`. `ChatCommandTrigger` retains the GUID and puts it in the event. The AutoGear actions re-resolve it, require a present in-world human session, and read that human's IP progression. `GetMaster()` is not necessarily the command sender; the bot's account or stage is not authoritative.

IP exposes `GetPlayerProgressionFromQuests(Player*)` and `GetEffectiveProgression(Player*)`. Both are needed: raw progression records completed content, while effective progression enforces paid expansion access. A raw-13/effective-12 player has completed Kil'jaeden but lacks Wrath access; raw-13/effective-13 has the same completion plus Wrath access. IP's raw getter returns stage 0 for an absent or out-of-world player, so the command checks sender lifetime first. The optional IP include makes an enabled command fail closed when IP is absent; native linking still needs verification.

Map-thread safety: current IP `GetEffectiveProgression` can reach `GetAccountMatch` on a Paid-active raw-stage-13+ cache miss, which performs blocking Auth DB reads. The command refuses that boundary before calling it. A cache-only effective-stage API is required for the paid split before enabling later-era caps.

## Cap propagation audit

| Command route | Current behavior | Required cap point |
| --- | --- | --- |
| `autogear`, quality, numeric, `reset`, `match` | `AutoGearAction::Execute` computes `ilvlCap` from `AutoGearScoreLimit` (`0` means unlimited), then calls `PlayerbotFactory::AutoGear` | Resolve a nonzero effective cap before argument parsing; use the stricter of configured and stage caps. Reject missing human/IP context before `reset` destroys gear. |
| `autogear bis`, numeric BiS | `BisGearAction::Execute` uses `AutoGearScoreLimit` for explicit argument validation and BiS lookup | Apply the same effective cap before BiS lookup or any gear/bag deletion; keep fallback at that cap. |
| BiS fallback | `RunAutogearFallback` calls `PlayerbotFactory::AutoGear` | Reuse the already checked cap; do not recompute from bot/master state. |

The factory's `ilvl` argument is converted to a mixed gear-score limit using the requested quality. In `InitEquipment`, grey/white fallback candidates can skip that score check. Therefore, merely reducing the `ilvl` argument does **not** enforce a hard item-level maximum. A separate command-scoped raw `ItemTemplate::ItemLevel` ceiling must also reach candidate selection, without changing non-command random-bot factory behavior. The BiS lookup searches downward from the requested row, but individual BiS item templates still need an item-level ceiling before equipment and bag destruction. Existing class/spec/eligibility and scoring paths should otherwise remain intact.

## Policy and remaining evidence

Stage 0 item level 66 and stage 1 item level 78 are provisional candidates. CS01 proposed further Vanilla and later-era candidates in Issue #59; none are production-approved. The resolver implements `min(max(completionCap(raw), entryFloor(effectiveEra)), eraCeiling(effectiveEra))`, then the stricter nonzero configured/requested limit. The parser-backed default table has 19 stage entries, with stages 2-18 zero; TBC/Wrath era ceilings are zero. Missing entries fail closed when enabled.

Tests still needed: every command spelling and numeric boundary, `reset` denial before destruction, `match`, BiS fallback and individual item levels, grey/white factory fallback, global cap interaction including `0`, human sender versus bot/master, GM/unknown sender, Altbot/AddClass/RNDBOT, no-IP build, paid effective-stage boundary, and future TBC/Wrath stages. Native build, gameplay tests, and deployment have not run in this workspace.
