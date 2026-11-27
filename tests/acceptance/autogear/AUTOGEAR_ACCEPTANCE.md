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
nine completed cases is incomplete. Even nine observed passes are provisional, not release acceptance. Do not reuse a partially provisioned fixture.

The guard unit tests were run with temporary directories and synthetic schema lists; no actual DB
or server was started. The integrated runner still requires review of binary compiled-prefix,
database identity/privileges, credentials, inherited config, and case oracle before any execution.

## Write-free preflight and install identity (Claude, PB68 child)

`--build-dir` is now required, and every run starts with the same collected preflight before its
first write. `--preflight-only` runs it, prints one JSON report and stops; it writes no file,
config or database row. The report has one entry per check:

| Check | Refuses when |
|---|---|
| `fresh_install` | the install is not canonical, has a `.conf`, a symlink, `data/` or `logs/` |
| `fresh_databases` | the MySQL instance has any non-system schema, or cannot be listed |
| `install_identity` | the build's `CMakeCache.txt` `CONF_DIR` (or `CMAKE_INSTALL_PREFIX/etc`) is not this install's `etc/`, or an installed binary's ELF build-ID differs from the build tree's |
| `ports` | the auth or world port is already accepting connections on 127.0.0.1, or the two are equal |
| `dbc` | the DBC directory has no `.dbc` file |
| `source_tuple` | no `--expect-tuple`, or `--source-dir` (`core`) or `modules/<name>` is not a clean git checkout at the expected full SHA |
| `build_manifest` | no `--build-manifest`, or the manifest's tuple is not exactly `--expect-tuple`, is not marked clean, or its `conf_dir`, binary build-IDs or `etc/**/*.dist` SHA-256s differ from the install, or the worldserver's revision stamp doesn't show the expected Core SHA (`build_manifest.py`) |
| `db_identity` | no `--expect-db-identity`, the identity query fails, the live flavor, `@@server_uuid` (MySQL; null on MariaDB), hostname, port, datadir, server_id or `CURRENT_USER()` differ from the operator's record, or the grants are not exactly the allowlist, or any grant reaches beyond `USAGE ON *.*` and `<prefix>\_` schemas (`db_identity.py`) |

The compiled-prefix check closes the gap noted above: module configs come from the binary's
compiled `CONF_DIR`, so a copied install would load another install's module configs. A rebuilt
but not reinstalled binary is caught by its build-ID. A passing preflight is a precondition, not
evidence of isolation or of any case result.

## Fixture requirements

**Build.** A disposable install prefix (`bin/authserver`, `bin/worldserver`, `etc/*.conf.dist`,
`etc/modules/*.conf.dist`) built from the exact tuple under test. Module configs are read from the
install prefix compiled into the binary, so the install must be a scratch prefix, never a server's
own.

Historical fixture reference only (not an approved runnable tuple; the composed PB60 child requires a new exact Core/PB/module manifest and native receipt proof before execution):

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

**Accounts.** The runner creates two new accounts in the fixture auth DB, with SRP6 salt and verifier. If either already exists, it stops before account or realmlist writes; never delete, replace or reuse accounts. Create a new isolated environment for each attempt:

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
  means neither and is always refused, even with the historical alt-command setting enabled. Mixed signals preserve gear; AddClass bots are excluded.
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
| A1 | alt | `autogear` | refusal 1, unchanged |
| A2 | alt | `autogear reset` | refusal 1, unchanged |
| A3 | alt | `autogear bis` (BiS disabled) | refusal 1, unchanged; not proof of enabled BiS exclusion |
| B1 | random | `autogear banana` | refusal 1, unchanged |
| B2 | random | `autogear bis` (BiS disabled) | refusal 1, unchanged |
| R1 | random | `autogear` | ran; over-cap gear is stripped through the native executor |
| R2 | random | `autogear reset` | ran; every combat item is stripped, then regeared |
| R3 | random | `autogear reset` with bags filled | refusal 5, nothing changed |
| G1 | random, GM sender | `autogear` | refusal 2, nothing changed |

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
3. Run the preflight and keep its report:

   ```bash
   python3 tests/acceptance/autogear/run_autogear_acceptance.py --preflight-only --isolated-fixture \
     --dist <scratch install> --build-dir <its build tree> --dbc <dbc dir> --source-dir <core source> \
     --mysql "127.0.0.1;<port>;<user>" --mysql-password-file <0600 file> --prefix agacc1 --expected-cap <reviewed cap> \
     --expect-tuple core=<sha> --expect-tuple mod-playerbots=<sha> \
     --expect-tuple mod-individual-progression=<sha> --expect-tuple mod-character-services=<sha> \
     --expect-tuple mod-naxxramas-core=<sha> \
     --build-manifest <operator build manifest.json> --expect-db-identity <operator identity.json>
   ```

   Continue only when it exits 0. Then run it:

   ```bash
   python3 tests/acceptance/autogear/run_autogear_acceptance.py \
     --dist <scratch install> --build-dir <its build tree> --dbc <dbc dir> --source-dir <core source> \
     --expect-tuple <the same five name=sha pairs> \
     --build-manifest <the same manifest> --expect-db-identity <the same identity> \
     --mysql "127.0.0.1;<port>;<user>" --mysql-password-file <0600 file> --prefix agacc1 \
     --world-sql <core>/modules/mod-naxxramas-core/docs/install/2026_10_13_00_ep_tower_lock_module_bind.sql \
     --expected-cap <independently reviewed cap> --isolated-fixture --report agacc1_report.json
   ```

   Reuse is disabled. Create a new isolated fixture for every run.
4. Exit code 0 means all nine provisional observations passed, NOT release acceptance. The report explicitly sets `release_acceptance: false`. Native unique command correlation, durable per-character receipt, correct GM group/security context, enabled BiS alt exclusion and independently verified fixture isolation remain mandatory. Do not execute until separately approved. The report has each case's server line, the
   bot's reply and the inventory comparison. `logs/Playerbots.log`, `logs/Server.log` and
   `logs/Errors.log` in the install hold the server's side.
5. Retain evidence, then discard the dedicated disposable environment as a unit. Do not run cleanup SQL against any shared or owner database.

## Case-specific observation gate (test-only child)

Before the second explicit fixture save, require the observed refusal and ran values to match the case. An unchanged case must report zero stripped/new/revoked items. G1 requires form 0, refusal 2, ran=false and authority=false; this shows unverified progression, not that a GM-specific IP cache branch caused refusal. No GM group membership is needed according to the reviewed security source, but the native path is not yet demonstrated. A stranger-sender negative case must not treat an absent result as proof of delivery or denial: require explicit source-correlated admission evidence, a confirmed unauthorized sender and unchanged custody; a bounded observation alone stays inconclusive. Enabled-BiS needs a separate fresh disposable run with both positive random-bot and negative alt cases; no live config change. These checks do not establish native command nonce, save durability, disposal or release acceptance.

## Separate enabled-BiS phase (development only)

The optional `--bis-phase` argument selects a new two-case observation run, not a repeat of the nine baseline cases. It requires a NEW isolated install and dedicated empty database instance, separate report and independently derived **BiS** cap. The default invocation retains the original disabled-BiS settings and nine cases. The new phase writes only its disposable config with `Playerbots.AutoGearBisCommand=1` and `Playerbots.AutoGearQualityLimit=4`; the historical alt-command flag stays deliberately enabled in the fixture. It refuses missing or ambiguous Playerbots config templates or non-unique BiS/quality keys before config writes. No changes to repository distribution configs or owner settings.

- E1: alt, `autogear bis`, refusal 1, ran=false, authority=true, default form 0 before parse, no logged gear activity and unchanged observed inventory.
- E2: RNDBOT, `autogear bis`, refusal 0, ran=true, authority=true, form 5, cap held and the normal success reply. The existing oracle checks the independent cap and custody.

Both observations must pass. Missing random bot, early refusal, wrong form, cap mismatch, PvP preset or other failed precondition makes the run incomplete/failed; no retries or relaxed expectations. Before a native run, establish that both bots are the expected populations, neither is a PvP preset (the production `IsSpecPvp` reads the specNo event/cache; a positive RNDBOT alone does not prove the alt was PvE), commander authority and actual loaded settings, and obtain approved exact binary/receipt evidence. E1 alone cannot prove which rejection branch fired; native correlated population/command evidence remains necessary. At level 60 stage 0 the BiS form uses the stage cap rather than the normal occupied-average mode, so do not blindly reuse the baseline expected cap. A separate fresh fixture avoids changing settings between cases or reusing a contaminated inventory. Both reports explicitly retain `release_acceptance: false`; native execution still requires the separate release-test authorization. No live flags, SQL or deployment approved by this test-only change.

## Optional stranger-sender cases (`--stranger-cases`, test-only proposal)

Off by default; the baseline nine cases are unchanged. When on, the runner creates a third normal account `AGSTR`
(no `account_access` row) with a Human character `Agstr` that never groups, guilds or owns a bot, and runs four
whisper-only cases after B2 and before R1. Never use channel, guild, party, raid or say for these cases, and never a GM sender.

| Case | Sender to bot | Command | Expected |
|---|---|---|---|
| S0 | preflight | none | stranger account and character distinct from AGCMD/AGOPS, no GM, group or guild rows, not an RNDBOT account, Alliance, empty `chat_filter` (`ChatFilter.Whisper` defaults on) |
| S1 | Agstr to random | `autogear` | one whisper-inform echo, exactly one bot reply `I am in a group with Agcmd. You can ask him for invite.`, no result line before or after the save, unchanged inventory and mail |
| S1c | Agcmd to same random | `autogear banana` | refusal 1, unchanged (the bot is reachable and admits its master) |
| S2 | Agstr to alt | `autogear` | as S1 with reply `Invite me to your group first` |
| S2c | Agcmd to same alt | `autogear` | refusal 1, unchanged |

A missing reply, missing whisper-inform echo or `SMSG_CHAT_PLAYER_NOT_FOUND` is INCONCLUSIVE, never a pass. Any failed or
inconclusive case stops the run before R1. Each stranger whisper is sent once, after waiting past `Playerbots.RepeatDelay`.
These cases show native whisper delivery and a visible denial for one same-faction stranger on each population. They do
not show the silent party/channel drop, opposing-faction, guild or group-member strangers, logout races, command nonce,
save durability or release acceptance.
