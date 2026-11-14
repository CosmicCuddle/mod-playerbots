# PB59 AutoGear progression cap: source audit

**Status:** source audit on PB `3a9e39f1` and IP `05ff032b`. No runtime behavior changed. Tracked in [coordination Issue #59](https://github.com/CosmicCuddle/Naxxramas-Server-Coordination/issues/59).

## Commanding player and IP authority

`PlayerbotAI::HandleCommands` resolves the sender's GUID and checks command permission before passing the sender to `ExternalEventHelper`. `ChatCommandTrigger` passes that sender as `Event::getOwner()`. The AutoGear actions should use this event owner, verify a present, in-world human session, and read that human's IP progression. `GetMaster()` is not necessarily the command sender; the bot's account or stage is not authoritative. The trigger currently holds a raw sender pointer until its event is checked, so the sender's lifetime across deferred execution requires a separate review before dereferencing in a new policy.

IP exposes `GetPlayerProgressionFromQuests(Player*)` and `GetEffectiveProgression(Player*)`. The latter applies paid-access restrictions while retaining the earned stage for ordinary cases. IP's raw getter returns stage 0 for an absent or out-of-world player; that value must not be treated as proof of a valid command sender. No direct IP include/provider is currently present in PB; integration must compile safely when IP is absent and fail closed for the command feature.

## Cap propagation audit

| Command route | Current behavior | Required cap point |
| --- | --- | --- |
| `autogear`, quality, numeric, `reset`, `match` | `AutoGearAction::Execute` computes `ilvlCap` from `AutoGearScoreLimit` (`0` means unlimited), then calls `PlayerbotFactory::AutoGear` | Resolve a nonzero effective cap before argument parsing; use the stricter of configured and stage caps. Reject missing human/IP context before `reset` destroys gear. |
| `autogear bis`, numeric BiS | `BisGearAction::Execute` uses `AutoGearScoreLimit` for explicit argument validation and BiS lookup | Apply the same effective cap before BiS lookup or any gear/bag deletion; keep fallback at that cap. |
| BiS fallback | `RunAutogearFallback` calls `PlayerbotFactory::AutoGear` | Reuse the already checked cap; do not recompute from bot/master state. |

The factory's `ilvl` argument is converted to a mixed gear-score limit using the requested quality. In `InitEquipment`, grey/white fallback candidates can skip that score check. Therefore, merely reducing the `ilvl` argument does **not** enforce a hard item-level maximum. A separate command-scoped raw `ItemTemplate::ItemLevel` ceiling must also reach candidate selection, without changing non-command random-bot factory behavior. The BiS lookup searches downward from the requested row, but individual BiS item templates still need an item-level ceiling before equipment and bag destruction. Existing class/spec/eligibility and scoring paths should otherwise remain intact.

## Policy and remaining evidence

The owner fixed stage 0 at item level 66 and stage 1 at 78. CS01 proposed a completed-content Vanilla table for stages 2-7 in Issue #59. Stage 8+ defaults remain undefined pending source/fairness review; phase ceilings cannot be assumed to be fresh-era caps. A parser-backed table could keep reviewed values in one place, but missing stages must fail closed rather than silently permit unlimited gear. This is a policy decision for the owner before production, not a reason to change live configuration now.

Tests still needed: every command spelling and numeric boundary, `reset` denial before destruction, `match`, BiS fallback and individual item levels, grey/white factory fallback, global cap interaction including `0`, human sender versus bot/master, GM/unknown sender, Altbot/AddClass/RNDBOT, no-IP build, paid effective-stage boundary, and future TBC/Wrath stages. Native build, gameplay tests, and deployment have not run in this workspace.
