# Newer Playerbots SQL DML source preflight — 7 Nov 2026

Owner: CosmicCuddle. Scope: **READ-ONLY TESTS**, independent from the historical first-test Playerbots build and from F14 new-RandomBot DRAFT #3. Source base: owner Playerbots newer `master@405f81196161017eebae3ee7a612d786a3ed2a9d` (43 commits newer than historical `e88cc4099e69f35126b28942a412ca702d20453b`). **Not** an invitation to install or switch the owner live server.

## Why this isolated check exists

Before any upstream PB merge, four new source SQL update files must be protected from silent/custom data overwrites:

- `2026_10_06_00_ai_playerbot_craft_link_texts.sql`: delete/reinsert `craft_usage` (id 1830) and `craft_no_professions` (id 1945) and named chance rows.
- `2026_10_16_00_ai_playerbot_spanish_es-MX_texts.sql`: update exactly 1,739 `ai_playerbot_texts.text_loc7` rows, ids 1–1739. Potentially overwrites custom translated text.
- `2026_10_22_00_ai_playerbot_calc_breakdown_text.sql`: delete/reinsert `calc_item_breakdown` (id 1929) and named chance row.
- `2026_10_27_00_ai_playerbot_class_role_texts.sql`: delete/reinsert 15 `class_role_*` text/chance records with ids 1930–1944.

This test suite checks exact intended SQL text shape, DML table scope, IDs and identifier uniqueness, complete locale ID coverage, and absence of DDL. It uses only Python standard library `pathlib`, `re`, `unittest`; never executes any SQL, accesses a MySQL server, or modifies files.

## Test command

`python3 -m unittest discover -s tests -p 'test_newer_pb_sql_dml_preflight.py' -v`

An automatic hosted Actions workflow runs the same test on this review branch. If any expected upstream SQL file changes, treat a failing test as **needs review**, not permission to delete the SQL, suppress checks or rewrite an established custom feature. It is okay for future separately approved upstream change to intentionally alter these statements *after* the record is revised and reviewed.

## Unverified / required before any adoption

This static check does **not** prove installed schema, locale text, rows, SQL update history, successful migrations or compatibility with owner-local Playerbots custom changes. Before migration: confirm exact installed PB source and DB state using read-only queries, list local custom changes, snapshot text/chance rows and IDs, verify restore on a disposable clone, compare affected quest/class/raid messages, test transactional migration and rollback, then obtain CosmicCuddle's explicit deployment approval. Existing F14/paid features default OFF and any extra source modifications and original module owners must be preserved.

Coordination and safe scope: [43-commit upstream audit](https://github.com/CosmicCuddle/Naxxramas-Server-Coordination/blob/planning/cs01-five-repo-ownership-routing-20261107/areas/Server-Side-Maintenance/PLAYERBOTS_43_COMMIT_UPSTREAM_AUDIT_2026-11-07.md) and [five-fork routing draft #40](https://github.com/CosmicCuddle/Naxxramas-Server-Coordination/pull/40). Rollback of **this test only**: discard the isolated development branch; no SQL/DB/host rollback required because no database was touched.
