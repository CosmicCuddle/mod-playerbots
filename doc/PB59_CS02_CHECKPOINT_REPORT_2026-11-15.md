# PB59 CS02 combined checkpoint

Owner: CosmicCuddle. Coordination #59 work order `6902772558`, task 8.
Parent `1365ae21705372e9f1cc0486d32ced02ca7a396d`, itself a descendant of `c694d0c2`. Base remains `69943153b86f872cd6a42417ea3f4b76a8ba5602`. No rebase. Feature off. No shared solver edit.

## Tasks

1. Checkpoint 4 was already on `1365ae21`: class 10, invalid eligibility, fact bound before match, entry plus signed variant plus item level, allocation failure preserves.
2. `AutoGearSlotEnumeration.h` adds per-slot provenance. Duplicate is Invalid. Missing, stale, mismatched, and unknown stay Incomplete. Known-unavailable can leave a proven empty list. A strip requires that empty list and a set bonus proven absent. Otherwise Preserve.
3. `tests/pb59_autogear_retained_constraints.cpp` covers two variants of one MaxCount, a two-hand main hand with an empty off hand, unique rings, unique trinkets, entry and property ties, a set bonus that is not invented, and a unique pair with no feasible set.
4. `tests/pb59_autogear_solver_oracle.cpp` runs eight fixed-seed two-by-two domains against `AutoGearSetSolver` and an exhaustive score oracle. Unsorted and zero-entry input is Invalid. Candidate overflow, a zero node budget, and an injected allocation failure are Incomplete. Search exhaustion through `SelectFromFacts` preserves.
5. `doc/PB59_CS02_NATIVE_ADAPTER_CONTRACT_2026-11-15.md` maps the five wiring points, the revision fields, and stale rejection. It does not edit them and it does not attach CS01's callback or retirement work.
6. `doc/PB59_CS02_OFFLINE_EXPORT_VALIDATION_2026-11-15.md` and `AutoGearExportCheck.h` check a synthetic export. A missing spell, checksum, or header is Unknown. No Darkmoon id is forced.
7. `tests/pb59_autogear_cap_matrix.cpp` uses approved stage 0 = 66 and stage 1 = 78. Stages 2–7 are zero placeholders. Leveling, endgame, and match stay explicit caller choices. The recorded proposal is not this table.

## Commands

From the module root, each of GCC 13, Clang 18, and `g++ -fsanitize=address,undefined`:

```sh
g++ -std=c++20 -Wall -Wextra -Werror -Isrc/Bot tests/pb59_autogear_selection_cap.cpp -o /tmp/pb59-cap && /tmp/pb59-cap
g++ -std=c++20 -Wall -Wextra -Werror -Isrc/Bot tests/pb59_autogear_selection_plan.cpp -o /tmp/pb59-plan && /tmp/pb59-plan
g++ -std=c++20 -Wall -Wextra -Werror -Isrc/Bot tests/pb59_autogear_command_parse.cpp -o /tmp/pb59-parse && /tmp/pb59-parse
g++ -std=c++20 -Wall -Wextra -Werror -Isrc/Bot tests/pb59_autogear_fact_source.cpp -o /tmp/pb59-source && /tmp/pb59-source
g++ -std=c++20 -Wall -Wextra -Werror -Isrc/Bot tests/pb59_autogear_set_bonus.cpp -o /tmp/pb59-set && /tmp/pb59-set
g++ -std=c++20 -Wall -Wextra -Werror -Isrc/Bot tests/pb59_autogear_variant_solver.cpp -o /tmp/pb59-variant && /tmp/pb59-variant
g++ -std=c++20 -Wall -Wextra -Werror -Isrc/Bot tests/pb59_autogear_slot_enumeration.cpp -o /tmp/pb59-slot && /tmp/pb59-slot
g++ -std=c++20 -Wall -Wextra -Werror -Isrc/Bot tests/pb59_autogear_retained_constraints.cpp -o /tmp/pb59-retained && /tmp/pb59-retained
g++ -std=c++20 -Wall -Wextra -Werror -Isrc/Bot tests/pb59_autogear_solver_oracle.cpp -o /tmp/pb59-oracle && /tmp/pb59-oracle
g++ -std=c++20 -Wall -Wextra -Werror -Isrc/Bot tests/pb59_autogear_export_check.cpp -o /tmp/pb59-export && /tmp/pb59-export
g++ -std=c++20 -Wall -Wextra -Werror -Isrc/Bot tests/pb59_autogear_cap_matrix.cpp -o /tmp/pb59-matrix && /tmp/pb59-matrix
```

Clang 18 used the same flags plus the libstdc++ include and library paths. `python3 apps/codestyle/codestyle-cpp.py` passed.

## Counts

GCC 13, Clang 18, and GCC ASan+UBSan each passed:

| Suite | Checks |
| --- | ---: |
| cap | 44 |
| plan | 72 |
| command parse | 22 |
| fact source | 23 |
| set bonus | 12 |
| variant solver | 20 |
| slot enumeration | 12 |
| retained constraints | 9 |
| solver oracle | 14 |
| export check | 6 |
| cap matrix | 14 |
| adversarial | 17 |

Review `6903067303` adds the duplicate-order checks, the variant-solver possession path, native proofs bound to the copied source, and SHA-256 export manifests. `MetadataChecked` is not production readiness. TrainerAction.cpp stays reserved for CS01.

Clang ASan+UBSan did not link. `libclang_rt.asan` is not installed. These are model fixtures, not native proof. No hosted CI was started.

## Blockers and open questions

- Owner confirmation of the match source, the denominator, and the leveling-to-endgame switch.
- Stages 2–7. The proposal stays labeled and is not the placeholder table.
- Tomorrow's `acore_world` and `Spell.csv`, including Darkmoon proc and availability rows.
- The five wiring points remain coordinate-first. The shared solver still has one category limit, so HAVE is not passed through it.
- Restore proof stays with the native transaction owner.

## Proposed next integration

After the owner export passes `CheckExport` and Maintenance frees one wiring point, the first live step is still command eligibility in `TrainerAction.cpp`: return a preserve before `DestroyEquippedGear`. Do not attach storage, the factory, population sync, or sender capture in that same change. Do not rebase onto `881256249e` for it.
