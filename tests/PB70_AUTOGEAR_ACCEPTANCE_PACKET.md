# AutoGear acceptance packet: CS03-IP Part 41 batch (PB69–PB77)

CS01 Part 41 Task 5 ([#80 7198695947](https://github.com/CosmicCuddle/Naxxramas-Server-Coordination/issues/80#issuecomment-7198695947)).
All of this is source, compile and standalone-test evidence. **Nothing was run natively**: no server, no
database, no gameplay, no full link. AutoGear and the paid features stay default OFF.

## Source references

| Item | PR | Head | Base | Kind |
|---|---|---|---|---|
| Task 1: write-free fixture preflight | [#69](https://github.com/CosmicCuddle/mod-playerbots/pull/69) | `9591a480` | PB68 `f68ed339` | test-only |
| Task 2: safety matrix | [#70](https://github.com/CosmicCuddle/mod-playerbots/pull/70) | `31e826fd` | PB68 | test-only |
| Task 3: Core88 interface audit | [#71](https://github.com/CosmicCuddle/mod-playerbots/pull/71) | `deddacd1` | PB68 | test and doc |
| A1: reproduction and proposal | [#72](https://github.com/CosmicCuddle/mod-playerbots/pull/72) | `e32ce5d8` | PB68 | test and doc |
| Task 4: adversarial cases | [#73](https://github.com/CosmicCuddle/mod-playerbots/pull/73) | `ed123ba7` | PB68 | test-only |
| A1 fix: readiness gate | [#74](https://github.com/CosmicCuddle/mod-playerbots/pull/74) | `482a275d` | #72 | **`src/`** (two files) |
| Oracle NotReady=10, stale alt-bot fix, audit | [#75](https://github.com/CosmicCuddle/mod-playerbots/pull/75) | `5e131910` | composed | test and doc |
| Retirement fixture stubs | [#76](https://github.com/CosmicCuddle/mod-playerbots/pull/76) | `0853a0ed` | PB68 | test-only |
| Preflight hardening and this refresh | #77 (this branch) | see PR | #75 | test and doc |
| Composition evidence | branch `claude/pb68-autogear-composed-20261126` | `2eaaac7d` | PB68 | merges of PR69–PR74 |

The reference tuple (source only) is Core88 `d5eee598` + PB68 `f68ed339` + IP `c1725f84` + CS `f43bc2c9` +
Naxx `80e9de1c`.

## Tests on the composed tree

Commands are run from the module root. `<core>` is a Core88 checkout.

| Command | Result |
|---|---|
| `cd tests/acceptance/autogear && python3 -B -m unittest discover -p 'test_*.py'` | OK: 196 tests, including NotReady oracle (7), DB identity (11), build manifest (10) and port ownership (14) |
| `g++ -std=c++20 -Isrc/Bot -Isrc tests/pb70_autogear_safety_matrix.cpp` | 1340/1340 |
| `g++ -std=c++20 -Isrc/Bot -Isrc tests/pb70_a1_quarantine_reproduction.cpp` | 9/9 (A1 fixed) |
| `g++ -std=c++20 -Isrc/Bot -Isrc tests/pb59_autogear_capped_command.cpp` | 367/367 |
| `g++ -std=c++20 -Isrc/Bot -Isrc tests/pb59_autogear_core_batch_outcome.cpp` | 89/89 (stale PB53 alt-bot expectation corrected in PR75) |
| `… -I<core>/src/server/game/Entities/Player -I<core>/src/common tests/pb70_core88_interface_audit.cpp` | 75/75 |
| same includes, `tests/pb70_autogear_adversarial_request.cpp` | 22/22 |
| same includes, `tests/pb59_autogear_core_batch_request.cpp` | 20/20 |
| `python3 tests/pb59_test_autogear_adapter_contract.py` | rules pass, 61/61 controls |
| `python3 tests/pb59_test_bot_retirement.py --core <core>` | PASS with PR76 (GCC, Clang, ASan+UBSan). Fails to compile without it, on plain PB68 too |

**Known failures:** none on the composition of PB68 with PR69–PR77. Without PR76, the only failure is the
retirement fixture's compile error, which also happens on plain PB68.

- The C++ tests also pass on GCC with ASan/UBSan and on Clang with `-Werror`, as reported in each PR.
- Mutant evidence is in each PR: 6 for the matrix, 1 for the audit, 2 for the adversarial cases, and 7 for the
  recovery stop (PR54).
- Object compiles against Core88: `TrainerAction`, `PlayerbotAI`, `PlayerbotFactory`, `Player`,
  `PlayerPB59Inventory` and `PlayerPB59Recovery` for PB68, plus `TrainerAction` with the PB74 gate. All have
  0 warnings.

## Fixture requirements for a later native run

These are in `tests/acceptance/autogear/AUTOGEAR_ACCEPTANCE.md`:
- a fresh disposable install built from the exact tuple;
- its build tree, checked by `--build-dir` (compiled `CONF_DIR` and ELF build-IDs);
- an empty, dedicated, disposable local MySQL or MariaDB, recorded in an `--expect-db-identity` file with an
  exact grant allowlist on `<prefix>\_` schemas;
- an operator build manifest (`--build-manifest`) of the clean tuple, binaries and config templates;
- a DBC directory;
- header-only map stubs, which the runner writes;
- the Naxx EP bind SQL;
- clean git checkouts at `--expect-tuple`;
- free ports 13724 and 18085.

Run `--preflight-only` first and keep its JSON report.

## Preserved behaviour

- Alt bots are refused by every command, whatever the historical setting (PB60).
- Random-bot-only stripping, the cap from the human commander's verified IP stage, and the raid source gate
  are unchanged.
- No implicit off-hand mail, old ammo is kept, nothing owned is destroyed, and no move happens outside Core's
  executor.
- A recovery stop touches nothing further (PB54). Applied custody is checked (PB56 and PB57).
- Quarantine is never reset, bypassed or retried.

## Risks, limits and open decisions

1. **A1 gate (PR74) changes `src/`.** Development scope is approved; it still needs review. The oracle now
   accepts exactly 10 (PR75). There's no translation SQL for the new text yet.
2. **A2:** a mixed population is refused with NotVerified, which gives a misleading reply. It's safe, but the
   wording is CS01's decision.
3. **Stale PB53 expectation:** corrected in PR75.
4. **Repair durability:** fixed in Core120, which isn't in this tuple. PB74 is independent of it.
5. **Blocked, needing interfaces that don't exist yet:**
   - durable per-command receipt and nonce;
   - database readback (Core114+);
   - shutdown persistence;
   - native ammo, off-hand and effects.
6. Provenance: the build manifest is written by the operator, and the revision stamp is extra evidence only.
   Isolation of the database instance stays operator-attested. Listener ownership is a library, not yet
   called after start. See `acceptance/autogear/PREFLIGHT_LIMITS_AUDIT.md`.

## Rollback

Every item is a separate draft child of PB68, and none is merged. Rollback is closing the PRs and deleting the
branches. Only PR74 touches runtime source: two files, and the default is OFF.

## Next integration request

1. CS01 review of PR69–PR77.
2. A NotReady native case, and calling listener ownership after `start()`, both of which need a native run.
3. Then a build-scope request for Core88 + PB68 + PB74 (+IP, CS, Naxx), followed by `--preflight-only` and,
   if it's approved, the disposable native acceptance run.
