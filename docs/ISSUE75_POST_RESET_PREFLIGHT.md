# Issue #75 — controlled Playerbot profession rebuild preflight

**DRAFT — read-only/offline procedure. No server or database changes authorized.**

CosmicCuddle has reported the designated controlled Playerbots' old professions **manually reset** on 22 November 2026. The primary engineering objective is now **post-reset verification** and narrowly approved **skill-300 retraining**, not broad profession removal.

## Exact evidence and restrictions

- Inspected Core53 source at \`79b0b1fe2d0fac93227c28329508553cfec15c4d\`. Its client unlearn handler calls \`Player::SetSkill(skill, 0, 0, 0)\`, which clears skill data, linked rank spells and enchantment effects. The \`.setskill\` GM command rejects zero or negative skill, and \`.unlearn\` targets a selected in-world player's specified spell. Neither is a verified **offline controlled Altbot reset** mechanism.
- Core's \`.learn all recipes\` directly traverses DBC \`SkillLineAbility\` relationships, not the pre-Molten Core source/progression allowlist. **Do not use it.** The source explicitly documents a missing-first-rank spell hazard: incomplete cleanup can re-grant a skill after relog.
- Playerbots \`trainer\` action operates on an in-world selected trainer and handles spells using the engine, but is not an offline grant facility or whitelist enforcement.
- Information's 1.12-versus-owner-DBC cross-check: **411** historical trainer craft candidates, **28** starter checks, **52** raid/phase holds. These are discovery sets, **not** approved grants. The world records for 24 Sword/Hammer/Axe crafts require skill **330+**, outside this skill-300 scope.
- Owner explicitly excluded Enchant Gloves — Skinning (spell **13698**). Finkle's Skinner (+10, if equipped and eligible) reaches at most **310** from base 300, not a universal MC skinning guarantee.

## Read-only verification for ONE bot

On a **private isolated copy** of the installed characters DB, after verifying the actual schema with \`SHOW CREATE TABLE\`, export one owner-confirmed controlled Altbot into CSV files:

| CSV | Columns |
|---|---|
| characters.csv | guid, account, class, level |
| character_skills.csv | guid, skill, value, max |
| character_spell.csv | guid, spell, specMask |
| character_queststatus.csv | guid, quest, status |
| character_queststatus_rewarded.csv | guid, quest, active |
| character_inventory.csv | guid, bag, slot, item |
| item_instance.csv | guid, itemEntry, owner_guid |

Every required file must have headers, even when there are zero rows. The inventory \`item\` is an **instance GUID** joining to \`item_instance.guid\`, not the item-template entry. Preserve and compare quest histories and external Guild Raid Manager recipe reserves separately. Never upload private rosters, data exports, keys or backup dumps to GitHub.

The audit must refuse mixed-character data, duplicate skill/spell keys, mismatched item ownership and missing item-instance joins. It should flag residual old rank/specialty spells after the manual reset; rival specialization spells; target skill above 300; held/unknown recipes and excluded glove enchant. Identity as a controlled Altbot versus RNDBOT or real player **cannot be inferred from CSV alone** and requires separate installed-source/owner confirmation.

Recipe classification fields: \`profession_skill,craft_spell,decision,era,source,branch,required_rank,required_level,verified_source\`. Default uncertain quest, faction, reputation, vendor, event, phase or acquisition sources to \`HOLD\`. A craft spell and an item teaching spell are different; never teach all catalog entries merely because skill level is 300. All results are **candidates only** until the engine/client accepts one-bot testing.

## First safe acceptance sequence — NOT EXECUTED

1. Confirm installed Core/Playerbots/IP SHAs and effective single-realm Vanilla cap, clone DB + custom owner DBC dependencies, and verify restore for a sacrificial controlled Altbot. No change to Takali/real players.
2. Independently confirm the manual profession reset: no old profession skill row, leftover rank/specialization spells, skill-restricted equipment anomalies or unexpected quest changes.
3. With **CosmicCuddle's explicit approval for an isolated test**, invoke a reviewed in-engine, world-thread, single-bot teaching route (or legitimate in-world trainer), establishing appropriate first/rank spells, profession skill 300 and **only** the Information-approved craft spells. Do not use raw SQL writes, blanket grants, \`.learn all recipes\`, physical recipe-item distribution or RNDBOT factory RNG.
4. Test an ordinary trainer craft, a selected-branch gated craft, and an allowed nontrainer recipe-origin craft; actual materials and output, equipped tool requirements, save/relog consistency, unrelated records unchanged, and successful rollback. Reject MC/later, 330+, opposite-specialty, and unknown-origin recipes.
5. Only after evidence and separate owner approval can a scoped multi-bot plan be prepared.

**Not yet established:** installed schema, installed fork revisions, single-bot identity, actual source allowlist, in-world retraining, gameplay/crafting, production backup or rollback. The accompanying offline Python audit is held as a **separate locally tested artifact** pending direct review of its source, not treated as deployment proof.

References:
- https://github.com/CosmicCuddle/Naxxramas-Server-Coordination/issues/75
- https://github.com/CosmicCuddle/Naxxramas-Server-Coordination/issues/75#issuecomment-7079434857
