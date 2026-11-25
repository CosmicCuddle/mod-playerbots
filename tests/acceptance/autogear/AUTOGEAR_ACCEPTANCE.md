# Capped AutoGear: disposable feature-ON acceptance run

This runs the progression-capped AutoGear command for real, on a throwaway local server, with the
feature turned ON only in that server's config. It is a developer acceptance fixture. It is **not**
a gameplay test, a live run or an activation, and nothing in it touches an owner database, config
or client.

- `run_autogear_acceptance.py` provisions the fixture, starts the servers, runs the cases and writes
  a JSON report.
- `wotlk_client.py` is a minimal 3.3.5a game client: SRP6 login, the world handshake, character
  create and login, chat and GM commands. It connects only to the ports it is given on 127.0.0.1.

## Mandatory safety preflight (CS01, PB54 child)

Never run against the owner's server, install, database instance or active credentials. A dedicated
disposable local database process and fresh privately owned install are required. Explicit
`--isolated-fixture` attests to this requirement; it is not proof of isolation.
The guard refuses symlinked configuration paths, existing live config files, existing data/log
directories and visible non-system database schemas. A restricted MySQL account may not reveal all
schemas: use a truly isolated instance and verify its identity independently. No automatic DROP,
reuse or skip-provision path. Reports are exclusive new files under the fixture logs. Fewer than
eight completed cases is incomplete, not a passing run. Do not reuse a partially provisioned fixture.

The guard unit tests were run with temporary directories and synthetic schema lists; no actual DB
or server was started. The integrated runner still requires review of binary compiled-prefix,
database identity/privileges, credentials, inherited config, and case oracle before any execution.

## Fixture requirements

**Build.** A disposable install prefix (`bin/authserver`, `bin/worldserver`, `etc/*.conf.dist`,
`etc/modules/*.conf.dist`) built from the exact tuple under test. Module configs are read from the
install prefix compiled into the binary, so the install must be a scratch prefix, never a server's
own.

The proposed tuple for the next run (not yet executed):

| Component | Commit |
|---|---|
| Core74 | `c779e7b4f735cab048f5b351ca6b9053f188787a` |
| Playerbots (PB54) | `ec276855d869bdb30b9aa1907739e386e0fcac1e` |
| Individual Progression | `c1725f84` |
| Character Services | `f43bc2c9` |
| Naxx | `80e9de1c` |

It needs `COSMIC_PB59_INVENTORY_BATCH_API_VERSION == 1` in `Player.h` (the native executor) and
IP's `OnPlayerPB59AutoGearProgression` provider.

**DBC.** The DBC set the tuple is meant to run with, passed as `--dbc`. The runner only symlinks
it. DBC files are never copied into a repository.

**Maps.** None are needed. The runner writes 44-byte header-only `.map` files (`MAPS` v9, no area,
height, liquid or hole data) for every grid of maps 0, 1, 530 and 571. They pass Core's start-area
map check and give no terrain height. vmaps and mmaps are off in the fixture config. The command
doesn't depend on terrain; bots stand in the starting area.

**World DB.** The base world database plus updates from `--source-dir`, created by the servers'
own `--dry-run` with `Updates.AutoSetup`. Extra world SQL is applied with `--world-sql`. For this
tuple that's the Naxx EP bind:
`modules/mod-naxxramas-core/docs/install/2026_10_13_00_ep_tower_lock_module_bind.sql`. The command
needs three things from the world DB:

- `item_template` (item levels, used for the oracle);
- the creature, quest and loot sources the raid source index is built from at startup;
- `instance_encounters`.

**EP.** The EP tower-lock bind SQL above is applied so the Naxx module starts without errors. The
command doesn't depend on EP state.

**Accounts.** The runner creates two accounts in the fixture auth DB, with SRP6 salt and verifier:

- `AGCMD` is a normal player account (security 0). It is the only command sender.
- `AGOPS` is a GM (security 3), used only for setup: `.character level`, `.saveall`, and the
  bag-filler items.

## Supported actor context

The capped command runs only through this path: `PlayerbotAI` queues a `PB59AutoGearWorldOperation`,
the world thread runs it, and that calls `RunPB59AutoGearWorldPlan`. It needs:

- **Sender:** a connected, non-headless player session, in the world and not logging out.
- **Bot:** a headless bot session. The sender and the bot are different players, and both
  identities still match the ones captured when the chat command was queued.
- **Permission:** the bot's security allows the sender. For a random bot that means the sender is
  the group master. For an alt bot it means the owning account.
- **Verified progression:** the sender's progression must be verified, or the command is refused
  with `NotVerified`. IP answers only when all of these hold:
  - the account matches neither IP's bot nor excluded-account regex;
  - the account is below GM security;
  - the character is level 60 or below;
  - the quest-derived stage is a Vanilla stage that equals the effective stage.
- **Population:** random bot means a random-bot account and tracked by `RandomPlayerbotMgr`. Alt bot
  means neither. Anything mixed is `UnknownPopulation`. AddClass bots are mixed.
- **Not supported:**
  - console commands (there is no player sender);
  - GM senders (no verified progression);
  - SelfBots;
  - commands from a sender above level 60.

## Cases

Refusal codes are `PlayerbotsAutoGearExec::Refusal`: 0 None, 1 Rejected, 2 NotVerified,
3 UnknownPopulation, 4 ResetNotAllowed, 5 BagsFull, 6 StripRefused, 7 StripRestored,
8 SourcesUnavailable, 9 RecoveryRequired.

Every case checks the same things, read from the server's `PB59 AutoGear bot=... refusal=...` line
and from the bot's saved inventory (saved with `.saveall`, read from the fixture DB) before and
after:

- every item GUID the bot owned before is still in its inventory;
- the bot's mail count is unchanged;
- if gearing ran, `capHeld=1`, `ownedMissing=0`, and no newly equipped item is above the cap the
  server reported;
- a refused command leaves the inventory row-for-row identical.

| Case | Bot | Command | Expected |
|---|---|---|---|
| A1 | alt | `autogear` | ran, refusal 0, success whisper |
| A2 | alt | `autogear reset` | refused (alt bots never strip), nothing changed |
| A3 | alt | `autogear banana` | refusal 1, nothing changed |
| A4 | alt | `autogear bis` (BiS form off) | refusal 1, nothing changed |
| R1 | random | `autogear` | ran; over-cap gear is stripped through the native executor |
| R2 | random | `autogear reset` | ran; every combat item is stripped, then regeared |
| R3 | random | `autogear reset` with bags filled | refusal 5, nothing changed |
| G1 | alt, GM sender | `autogear` | refusal 2, nothing changed |

**Not reachable from the command surface:** the stop cases where a native move fails part way
(`StripRestored`, `RecoveryRequired`, a factory stop). A real server gives no way to make one move
fail, so these are left to Grok's real-executor fault tests on PR75. They are not repeated here.
`SourcesUnavailable` needs the source index to fail to load, and the runner doesn't fake that
either.

## Independent cap expectation (required before running)

The server's reported cap must not serve as its own expected result. Supply
`--expected-cap` before executing the fixture, using the exact runtime revision,
reviewed commander level/stage, equipment and effective configuration. Stage 0
at level 60 can be bounded by the occupied-slot average, so the approved stage
ceiling of 66 is not automatically the expected result. Keep the commander
sample fixed for all successful cases; retain the derivation separately with the
run manifest. If it cannot be established independently, do not run or certify
the fixture. A matching operator-supplied number is not proof of stage or
configuration provenance. The report records the expected value; successful
results must match it exactly in addition to existing item/custody checks.

## Reproduction checklist

1. Build the tuple into a scratch install prefix. Confirm `COSMIC_PB59_INVENTORY_BATCH_API_VERSION 1`
   is in `Player.h`.
2. Start a local MySQL. Give a local user rights to create the `agacc*` databases.
3. Run it:

   ```bash
   python3 tests/acceptance/autogear/run_autogear_acceptance.py \
     --dist <scratch install> --dbc <dbc dir> --source-dir <core source> \
     --mysql "127.0.0.1;<port>;<user>;<password>" --prefix agacc1 \
     --world-sql <core>/modules/mod-naxxramas-core/docs/install/2026_10_13_00_ep_tower_lock_module_bind.sql \
     --expected-cap <independently reviewed cap> --isolated-fixture --report agacc1_report.json
   ```

   Reuse is disabled. Create a new isolated fixture for every run.
4. The exit code is 0 only when every case passes. The report has each case's server line, the
   bot's reply and the inventory comparison. `logs/Playerbots.log`, `logs/Server.log` and
   `logs/Errors.log` in the install hold the server's side.
5. Retain evidence, then discard the dedicated disposable environment as a unit. Do not run cleanup SQL against any shared or owner database.
