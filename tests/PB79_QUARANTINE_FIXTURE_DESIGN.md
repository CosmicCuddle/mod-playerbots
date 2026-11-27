# Design: fixture-only inventory quarantine and read-only probe for the NotReady native case

CS01 Part 42 item 3 ([#80 7218072082](https://github.com/CosmicCuddle/Naxxramas-Server-Coordination/issues/80#issuecomment-7218072082)).
**This is a design for review. Nothing in it is implemented, and it changes no runtime source.** Source facts are
from Core88 `d5eee59878bed7991611b0bf6764e73e03885b39` and PB78 `7bac9925`.

## 1. Source facts the design depends on

| # | Fact | Where |
|---|---|---|
| F1 | Quarantine is set only by Core's batch executor, through `EquipmentCaptureState::SetInventoryQuarantined`. That's private, with `Player` as its only friend. Setting it registers the owner's items in the process-wide `InventoryQuarantine` registry. | `PlayerPB59Inventory.cpp:302` (set), `:507` (cleared on a verified finish); `EquipmentCaptureState.h:100` |
| F2 | `CapturePB59InventoryStamp()` returns nothing while quarantined. It also returns nothing when the player is dead, in combat, teleporting, loading or logging out, has a busy transaction, or has unsettled originals. | `PlayerStorage.cpp:467-475` |
| F3 | **`Player::Update` returns right after `OnPlayerBeforeUpdate` while quarantined.** Playerbots ticks the bot AI, and so processes chat commands (`HandleCommands`), in `OnPlayerAfterUpdate`, at line 440. A quarantined bot therefore never handles a new `autogear` chat command. | `PlayerUpdates.cpp:64`, `:440`; PB `Playerbots.cpp:254-260`, `PlayerbotAI.cpp:615` |
| F4 | The capped command doesn't run in that tick. `HandleCommands` queues a `PB59AutoGearWorldOperation`, and the world thread runs it later. It re-resolves both players and calls `RunPB59AutoGearWorldPlan`, which contains the PR74 gate. It doesn't depend on `Player::Update`. | PB `PlayerbotAI.cpp:658-699`, `TrainerAction.cpp:1148` |
| F5 | While quarantined, `WorldSession::Update` and `HandleSocketClosed` keep the session alive and the player in place. A quarantined owner is retained, never destroyed. | `WorldSession.cpp:388`, `:606`, `:638` |
| F6 | The readiness gate is compiled in only with `COSMIC_PB59_INVENTORY_BATCH_API_VERSION == 1` (`PB59_AUTOGEAR_CORE_BATCH`). Otherwise `Ready()` is always true: the legacy fallback, unverified. | PB `TrainerAction.cpp:38-43` |

**Consequence of F3 and F4.** The only native path where a quarantined bot reaches the gate is: the command is
queued first, the bot is quarantined next, and the world operation runs after that. Quarantining first and then
sending the command doesn't work: the command is never handled, and the case would wait for a result line
forever. That would be a false "blocked" result, not a false pass. The fixture therefore has to place the
quarantine inside that window, deterministically.

## 2. Two cases, in order

**N1a: NotReady from an existing Core state, with no new mechanism. Recommended first.**
F2 says a dead bot has no stamp either. The operator GM account kills the bot with the stock `.die` command,
then sends `autogear`. The dead bot's AI still ticks, because F3 applies only to quarantine. The world operation
then meets `IsAlive() == false`, and the gate returns NotReady.
- It proves the gate on a real Core readiness signal, with no code change.
- It doesn't prove the quarantine-specific A1 path.
- **Risk:** the bot's death strategy may release its spirit or revive before the world operation runs. The probe
  (section 4) has to confirm the bot is still not ready at execution, or the result is inconclusive, never a pass.

**N1b: NotReady under a real quarantine.** This needs the seam in section 3.

## 3. Fixture-only quarantine seam (N1b)

**Compile-time isolation, on both sides.**
- **Core:** CMake option `PB59_TEST_QUARANTINE_FIXTURE`, default OFF. Configure refuses it unless
  `CMAKE_BUILD_TYPE` is `Debug` or `RelWithDebInfo`, *and* `-DPB59_I_UNDERSTAND_TEST_ONLY=ON` is also set. It
  defines `COSMIC_PB59_TEST_QUARANTINE_FIXTURE 1` for `game` only.
- **Playerbots:** a module option of the same name, honoured only when Core's macro is defined. A
  `static_assert` fails if the module asks for it but Core doesn't have it.
- **Release proof:** in a build without the option, `nm -C worldserver` must show no `PB59TestQuarantine`
  symbol. That check goes into the acceptance manifest (section 5), and a CI grep refuses the macro outside the
  `#if` blocks.

**Core API, only under the macro:**
```cpp
// Player.h
#if COSMIC_PB59_TEST_QUARANTINE_FIXTURE
// Test fixture only. Holds the existing quarantine on an idle, stamp-ready bot. There is no release:
// clearing quarantine stays exclusively with the executor's verified finish (F1).
[[nodiscard]] bool PB59TestHoldQuarantine(std::uint64_t ownerIncarnation, std::uint64_t sessionIncarnation);
[[nodiscard]] bool PB59TestProbeQuarantine(bool& quarantined, bool& stampReady) const;
#endif
```
- **`Hold`** refuses unless all of these hold:
  - the player's session is headless (a bot);
  - both incarnations match;
  - `CapturePB59InventoryStamp()` currently has a value, so the bot is idle and settled;
  - no reservation is prepared.

  It then reuses the executor's own `PrepareInventoryReservation` and `SetInventoryQuarantined(true)` on the
  bot's currently equipped items. It writes no item and starts no transaction.
- **There is no release, reset or bypass entry point.** The bot stays quarantined until the disposable server
  stops. That keeps the fixture out of recovery authority, which CS01 reserved.
- **`Probe`** reads `InventoryQuarantined()` and `CapturePB59InventoryStamp().has_value()`. It writes nothing.

**Placing the hold in the window (F4).** A Playerbots seam, only under the module macro:
- `PB59AutoGearWorldOperation::Execute` checks a one-shot arm that is keyed by bot GUID and owner incarnation.
- If the arm is set, it calls `PB59TestHoldQuarantine` immediately before `RunPB59AutoGearWorldPlan`.
- If the hold fails, the operation logs `PB59 test hold refused` and returns false, and the case is
  inconclusive.
- The arm is consumed once and is never re-armed by the seam itself.

**Operator interface, only under the macro:**
- `.pb59test quarantine arm <bot>` and `.pb59test quarantine probe <bot>`;
- security `SEC_CONSOLE`, so they run from the console only and never from a game account;
- refused unless config `Playerbots.PB59TestFixture = 1`. That key is absent from `playerbots.conf.dist`; the
  fixture runner writes it into its own install only.
- The probe prints one line: `PB59 test probe bot=<guid> quarantined=<0|1> stampReady=<0|1>`.

**What the seam may never do:**
- reset, clear or retry a quarantine;
- run outside the world thread;
- act on a non-bot;
- be reachable in a release build or with the config absent;
- change gear, durability, mail or the database.

## 4. N1b case flow and acceptance

1. Preflight (PR77) and the startup gate (PR78), with the hardened timing from this PR.
2. Probe: `quarantined=0 stampReady=1`. Record the inventory, mail and durability evidence.
3. Arm the hold for the bot.
4. Send `autogear`.
5. Wait for exactly one result line.
6. Probe: `quarantined=1 stampReady=0`.
7. Record the same evidence again.

**Pass** only when all of these hold:
- `not_ready_case.blockers(...)` is empty;
- `case_outcome_problems(lines, EXPECT)` is empty, with exactly one `refusal=10 ran=false` line and no activity;
- `invariant_problems(...)` is empty: inventory, mail and durability unchanged, no finisher, repair or factory
  log, and the quarantine kept;
- the log shows `PB59 test hold` exactly once, before the result line.

Anything else is a FAIL, or INCONCLUSIVE when the hold was refused, the probe disagrees, or the run timed out.
Never a pass.

**Teardown.** The quarantined bot keeps its session alive (F5), so a clean SIGINT shutdown may not finish.
`stop_owned` already falls back to killing *only our own child* after the grace period. The fixture is disposable,
so the databases aren't reused.

## 5. Prerequisites and acceptance manifest

| Item | Required value |
|---|---|
| Core | Core88 `d5eee598` plus a reviewed commit that adds only section 3's `#if` blocks (none exists yet) |
| Playerbots | PB68 `f68ed339` + PR74 gate + PR75–PR79 + a reviewed seam commit (none exists yet) |
| IP | `c1725f84` |
| Build | `COSMIC_PB59_INVENTORY_BATCH_API_VERSION=1`; `PB59_TEST_QUARANTINE_FIXTURE=ON` in the fixture build only |
| Release check | a second build with the option OFF: `nm` shows no `PB59TestQuarantine` symbol, and `PB59_AUTOGEAR_CORE_BATCH` is 1 |
| Config (fixture install only) | `AutoGearIPCapEnable = 1`, `Playerbots.PB59TestFixture = 1`. Owner configs untouched |
| Manifest additions (`build_manifest.py`) | `test_fixture: true`, `fixture_options: [PB59_TEST_QUARANTINE_FIXTURE]`, plus the release twin's build-IDs and the `nm` result |
| Database | dedicated disposable instance with a recorded identity (PR77) |

## 6. Residual risks

- **N1a timing:** the AI may revive the bot first. The probe makes that inconclusive, not a pass.
- **Seam drift:** the seam reuses the executor's private quarantine path, so a change there could make the
  fixture's quarantine differ from a real one. Mitigation: `Hold` uses the identical calls, and an interface
  audit test (as in PR71) pins them.
- **A permanent hold** keeps the bot's session alive, so shutdown may need the kill fallback. That's acceptable
  only because the fixture is disposable.
- **No native evidence exists yet.** N1a needs only a native run approval. N1b needs reviewed Core and PB seam
  commits, then a native build and run.
