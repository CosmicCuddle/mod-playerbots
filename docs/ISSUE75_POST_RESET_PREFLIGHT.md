# Issue #75 — controlled Playerbot profession rebuild preflight

**DRAFT — read-only/offline procedure. No server or database changes authorized.**

CosmicCuddle has reported the designated controlled Playerbots' old professions **manually reset** on 22 November 2026. Information subsequently verified one owner-designated offline controlled bot by a corrected owner-run SQL SELECT audit: **FOUND**, zero primary skill rows and zero flagged residual rank/specialisation spells; secondary First Aid, Cooking and Fishing retained. A separate 440-row realm-wide audit found 41 zero-primary characters, but it did **not** independently identify the complete controlled raid roster (Issue #75 comments 7079626094 and 7079687329). The objective is therefore **post-reset verification; initial acquisition of exactly two selected primary skills per bot; first/rank chain progression to 300; then narrowly approved craft-spell teaching**, not bulk unlearning or filling an existing profession.

## Exact evidence and restrictions

- Inspected Core53 source at `79b0b1fe2d0fac93227c28329508553cfec15c4d`. Its client unlearn handler calls `Player::SetSkill(skill, 0, 0, 0)`, which clears skill data, linked rank spells and enchantment effects. The `.setskill` GM command rejects zero or negative skill, and `.unlearn` targets a selected in-world player's specified spell. Neither is a verified **offline controlled Altbot reset** mechanism.
- Core's `.learn all recipes` directly traverses DBC `SkillLineAbility` relationships, not the pre-Molten Core source/progression allowlist. **Do not use it.** The source explicitly documents a missing-first-rank spell hazard: incomplete cleanup can re-grant a skill after relog.
- Playerbots `trainer` action operates on an in-world selected trainer and handles spells using the engine, but is not an offline grant facility or whitelist enforcement.
- Information's 1.12-versus-owner-DBC cross-check: **411** historical trainer craft candidates, **28** starter checks, **52** raid/phase holds. These are discovery sets, **not** approved grants. The world records for 24 Sword/Hammer/Axe crafts require skill **330+**, outside this skill-300 scope. Information further partitions the 411 into **379 ordinary** (369 with EK/Kalimdor trainer links, 10 with Uldaman/map70 and map530 links) and **32 specialization-gated** (linked on map0/1). Spawn location narrows route research but never overrides skill, quest, faction, rep or trainer availability restrictions.
- Owner explicitly excluded Enchant Gloves — Skinning (spell **13698**). Finkle's Skinner (+10, if equipped and eligible) reaches at most **310** from base 300, not a universal MC skinning guarantee.

## Read-only verification for ONE bot

**Owner-private schema and empty-result checkpoint (22 Nov):** Information has verified that six installed character-table schemas match the relevant pinned Core53 columns: `characters` (including `guid`, `name`, `online`), `character_skills`, `character_spell`, `character_queststatus`, `character_queststatus_rewarded`, and `character_inventory`. This **does not** verify the installed Core binary, all extensions, `item_instance`, actual bot identity or two free engine profession slots. The owner's earlier four **blank SELECT result tabs are not evidence of zero professions**: the original query filtered on the unreplaced or unmatched `BOT_NAME_HERE` value. Information subsequently received the owner's corrected, read-only one-name result: **FOUND, offline, zero primary skill rows, zero audit-flagged residual rank/specialisation spells, all three ordinary secondaries retained** (comment 7079626094). This resolves the original blank-results blocker for that single character only; the spell list was bounded. **Block** any export with zero `characters.csv` rows or missing character identity, even when skill/spell exports are empty. The independent offline audit covers this as a tested negative control (27/27 local tests) and is not an installed-data result.

**Offline audit contract correction (22 Nov):** A known *crafting spell* does **not** prove the character completed a specialization quest or still holds the required branch; a familiar branch spell in `character_spell` is also not sufficient gameplay evidence. The offline audit therefore retains **all specialization-gated recipe candidates on HOLD** pending an owner-approved in-engine single-bot branch/quest verification. Missing or blank recipe-source descriptions are HOLD, and residual specialty spells from *any* primary profession must be flagged after the reported zero-primary reset. Its reviewed standalone Python source and negative-control tests are packaged separately; latest local suite **26/26 PASS**, not a live/installed or PR CI run. Do not treat recipe counts or a clean CSV export as teaching permission.

On a **private isolated copy** of the installed characters DB, after verifying the actual schema with `SHOW CREATE TABLE`, export one owner-confirmed controlled Altbot into CSV files:

| CSV | Columns |
|---|---|
| characters.csv | guid, account, class, level |
| character_skills.csv | guid, skill, value, max |
| character_spell.csv | guid, spell, specMask |
| character_queststatus.csv | guid, quest, status |
| character_queststatus_rewarded.csv | guid, quest, active |
| character_inventory.csv | guid, bag, slot, item |
| item_instance.csv | guid, itemEntry, owner_guid |

Every required file must have headers, even when there are zero rows. The inventory `item` is an **instance GUID** joining to `item_instance.guid`, not the item-template entry. Preserve and compare quest histories and external Guild Raid Manager recipe reserves separately. Never upload private rosters, data exports, keys or backup dumps to GitHub.

The audit must refuse mixed-character data, duplicate skill/spell keys, mismatched item ownership and missing item-instance joins. Explicitly detect any residual PRIMARY profession rows/rank spells or stale specialties after the owner's reported zero-primary reset; preserve secondary Cooking, Fishing and First Aid. Actual two-primary-slot/free-point state must be checked in the installed engine, not assumed from SQL. It should flag residual old rank/specialty spells after the manual reset; rival specialization spells; target skill above 300; held/unknown recipes and excluded glove enchant. Identity as a controlled Altbot versus RNDBOT or real player **cannot be inferred from CSV alone** and requires separate installed-source/owner confirmation.

Recipe classification fields: `profession_skill,craft_spell,decision,era,source,branch,required_rank,required_level,verified_source`. Default uncertain quest, faction, reputation, vendor, event, phase or acquisition sources to `HOLD`. A craft spell and an item teaching spell are different; never teach all catalog entries merely because skill level is 300. All results are **candidates only** until the engine/client accepts one-bot testing.

## Fastest single-bot trainer proof — exact source route, TEST REALM ONLY

As of 22 Nov 2026, inspected **Playerbots PR #44** exact source `036b3ffa9a7d0e52bca0503c65a3b269cafcfe42`, `src/Ai/Base/Actions/TrainerAction.cpp` lines 41–188. An in-world controlled bot's `TrainerAction` resolves the **master's currently selected creature**, looks up its `Trainer::Trainer`, filters spells with `PlayerbotFactory::IsTrainerSpellAllowedForBot` and `trainer->CanTeachSpell`, parses an optional linked spell through `chat->parseSpell(param)`, then learns using `bot->CastSpell(..., true)` for `SPELL_EFFECT_LEARN_SPELL` or `bot->learnSpell(..., false)`. This confirms a **source-based mechanism**, not installed-world evidence. Importantly, `Execute` itself does not explicitly call `IsTrainerValidForPlayer` (that is in `isPossible`), so verify dispatch/eligibility in the actual engine test; do not assume it is a blanket-authorization safeguard.

On a **restorable disposable isolated test realm** only, with approval and one independently verified Altbot:
1. Log in as the bot's owner/master, bring in **one bot only**, select an active, allowed trainer NPC, and whisper **only that bot** (never party/raid chat).
2. Use `/w BOT_NAME trainer` to **list** candidate trainer spells; this request alone does not contain the `learn` token. Do **not** issue bare `trainer learn`: in this fork, when no spell ID is parsed it loops over and may teach **every available spell** from the selected trainer.
3. Select the exact intended **first-rank profession spell hyperlink** from the list, then use `/w BOT_NAME trainer learn [EXACT_TRAINING_SPELL_LINK]` to try exactly one. **Do not type the bracket placeholder verbatim**; paste the actual trainer-spell link and verify that its parsed ID is nonzero before any teaching. The `learn` token enables teaching; `chat->parseSpell` supplies the intended exact filter.
4. Immediately re-list and verify only the first chosen profession appeared; repeat for the **second approved primary**, never an unapproved third. Record rank spell, current/max skill, primary slot count, and bot account identity. If an empty/incorrect link learns something else, **STOP and restore the disposable clone**.
5. Advance each profession through the real installed rank chain (Apprentice → Journeyman → Expert → Artisan) **only when the preceding rank and required skill threshold are satisfied**, using exact trainer spells; then reach **300/300** by permitted crafting/gathering or a separately reviewed and approved *disposable-only* GM skill adjustment. Core53 `HandleSetSkillCommand` requires an **in-world selected Player**, changes the skill row/step, and does not itself establish rank acquisition or teach recipes. Never use `.setskill` to conjure a primary profession from a blank state or to skip rank-chain validation.
6. Save and relog the one bot; compare before/after all profession/secondary/quest/unrelated spell/item records and verify a simple permitted craft. Verify successful database **restore** to the exact baseline. Keep specialty quests/branches and all independent recipe sources on HOLD until Information supplies the explicit approved allowlist and installed game evidence.

**Fail closed:** no valid trainer, no exact filtered linked spell, bot not uniquely identified as controlled Altbot, any unexpected primary, missing earlier rank, skill value/max mismatch, owner custom DBC mismatch, uncertain specialty, missing restorable clone, non-target change, or unapproved live action. No SQL mutation, no unrestricted `trainer learn`, no `.learn all recipes`, no blanket GM spell teaching.

### Multi-bot scoping caveat

Information's owner-run bulk audit covered **440 unique level-60 realm characters**, not a confirmed guild roster: **41** had no primary profession skill rows and no spells in its bounded residual check list; **398** had two primary rows and **one** had three. Two of the 41 were provisionally test-named, leaving **39 plausible cleared roster members**, plus a **different benched candidate** with two existing rank-1 primaries. This **39+1=40 is only a hypothesis**, not authority to mutate any of them. Confirm the roster/account mapping privately and preserve protected players, benched members, and secondary skills before any eventual per-bot rollout. Reference https://github.com/CosmicCuddle/Naxxramas-Server-Coordination/issues/75#issuecomment-7079687329.

## First safe acceptance sequence — NOT EXECUTED

1. Confirm installed Core/Playerbots/IP SHAs and effective single-realm Vanilla cap, clone DB + custom owner DBC dependencies, and verify restore for a sacrificial controlled Altbot. No change to Takali/real players.
2. Independently confirm the manual profession reset: **no remaining primary professions** (with normal secondary skills preserved), no old rank/specialization spells, skill-restricted equipment anomalies or unexpected quest changes.
3. With **CosmicCuddle's explicit approval for an isolated test**, invoke a reviewed in-engine, world-thread, single-bot teaching route (or legitimate in-world trainer), **acquiring two selected primary professions**, establishing their base and first/rank spells, advancing each to skill 300 and teaching **only** the Information-approved craft spells. Do not use raw SQL writes, blanket grants, `.learn all recipes`, physical recipe-item distribution or RNDBOT factory RNG.
4. Test an ordinary trainer craft, a selected-branch gated craft, and an allowed nontrainer recipe-origin craft; actual materials and output, equipped tool requirements, save/relog consistency, unrelated records unchanged, and successful rollback. Reject MC/later, 330+, opposite-specialty, and unknown-origin recipes.
5. Only after evidence and separate owner approval can a scoped multi-bot plan be prepared.

**Not yet established:** exact installed binary/DBC revisions, verified controlled-Altbot roster-to-account identity and free engine primary slots, actual source allowlist, in-world retraining, gameplay/crafting, proven disposable-copy restore, production backup or rollback. Six relevant owner-supplied table shapes and the first character's DB baseline **are** established; do not regress these to unverified. The accompanying offline Python audit is held as a **separate locally tested artifact** pending direct review of its source, not treated as deployment proof.

References:
- https://github.com/CosmicCuddle/Naxxramas-Server-Coordination/issues/75
- https://github.com/CosmicCuddle/Naxxramas-Server-Coordination/issues/75#issuecomment-7079434857
