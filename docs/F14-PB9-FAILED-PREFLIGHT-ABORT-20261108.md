# CosmicCuddle F14 PB9 audited preflight — abort unsafe creation (8 Nov 2026)

**Review-only child of [PB9](https://github.com/CosmicCuddle/mod-playerbots/pull/9)**, parent `3dad409c4dc95f4faa54ec7d5bc6afb4f1c8c4c5`. No merge/deploy, production SQL/DB/config/DBC/client or owner host actions.

## Evidence-based issue

The optional audited creation path in PB9 checks for existing `characters` GUID and three old GUID-linked tables. A failed/missing query or positive historical row vetoes the audit but previously reached the `else { playerBot->SaveToDB(true,false); }` stock save. This could still write an untrusted candidate under an already used or historically contaminated GUID, despite correctly denying the audit bit.

## Minimal fix

Before choosing the `AsyncCommitTransaction` branch, when `f14AuditRequested && !f14BeforeSaveRelevantRowsAbsent`, clean and free the **new unsaved candidate**, then `continue` the class creation loop. No cache insertion, birth candidate, bot count or character DB save is permitted for this unsafe attempt. No existing database row is updated or deleted. The normal path for both diagnostic flags OFF stays unchanged; the passing audited path retains the exact DB commit callback and positive readback checks.

## Limitations and validation

This safety veto does not prove first-born identity, exhaustive orphan data absence, race eligibility, paid eligibility, IP stage grant, observer-effective era or WHO/NPC/loot visibility. A read-only preflight has a possible time-of-check/time-of-use race; correct owner DB account workflows need approved isolated experiments. Source and mutation tests check abort-before-save and unchanged default OFF behaviour; standalone PB9 header regressions and real five-fork C++ build need separate exact-head success. Never turn ON just because CI passes.

**Rollback:** reject this child; original PB9 and PB7 branches preserved. Before real tests inventory/backup tracked, untracked and ignored SQL cleanup/migration files, all installed source/config/DB/DBC/client/binaries and prove restore; explicitly confirm effective `Playerbots.DeleteRandomBotAccounts=0`. The actual owner installed versions remain unverified.
