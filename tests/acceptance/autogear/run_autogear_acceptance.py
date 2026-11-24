#!/usr/bin/env python3
# This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
# information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
# or (at your option) any later version.
"""Disposable feature-ON acceptance run for the progression-capped AutoGear command.

It runs a real authserver and worldserver from a disposable install against throwaway databases on
a local MySQL, turns the capped command ON only in that install's config, logs in real game clients
(wotlk_client.py) and sends the command the way a player does: a whisper from a non-GM commander to
a bot in the commander's group. Every case is checked against the server's own result line and the
saved inventory in the throwaway character database.

The operator must supply a genuinely isolated disposable install and local MySQL instance. Preflight
rejects existing configurations, output paths and visible non-system schemas; it does not prove isolation.
No database deletion or reuse. See AUTOGEAR_ACCEPTANCE.md for requirements.
"""

import argparse
import json
import os
import re
import shutil
import signal
import struct
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import wotlk_client as wc  # noqa: E402
from fixture_guard import fresh_install, fresh_database_names  # noqa: E402
from acceptance_oracle import parse_result, evidence_problems  # noqa: E402

MARKER = "# autogear-acceptance: disposable fixture config, never a live server"
PREFIX_RE = re.compile(r"^agacc[a-z0-9_]{0,24}$")
OPERATOR = ("AGOPS", "agops")      # GM account: setup only (levels, saves, bag filler)
COMMANDER = ("AGCMD", "agcmd")     # normal player account: the only command sender
CMD_CHAR, ALT_CHAR, OPS_CHAR = "Agcmd", "Agalt", "Agops"
FILLER_ITEM = 25                   # Worn Shortsword: white, unique-free, stack size 1
CMSG_GROUP_INVITE = 0x06E
SMSG_GROUP_INVITE = 0x06F
CMSG_SET_SELECTION = 0x13D

# Config written into the disposable install. Only this fixture turns the feature on.
WORLD_OVERRIDES = {
    "Warden.Enabled": "0",
    "vmap.enableLOS": "0",
    "vmap.enableHeight": "0",
    "vmap.petLOS": "0",
    "vmap.enableIndoorCheck": "0",
    "MoveMaps.Enable": "0",
    "Console.Enable": "0",
    "Ra.Enable": "0",
    "SOAP.Enabled": "0",
    "BindIP": '"127.0.0.1"',
    "SkipCinematics": "2",
    "Updates.AutoSetup": "1",
}
AUTH_OVERRIDES = {"BindIP": '"127.0.0.1"', "Updates.AutoSetup": "1"}
PLAYERBOTS_OVERRIDES = {
    "Playerbots.Enabled": "1",
    "Playerbots.AutoGearCommand": "1",
    "Playerbots.AutoGearCommandAltBots": "1",
    "Playerbots.AutoGearIPCap.Enable": "1",
    "Playerbots.RandomBotAutologin": "1",
    "Playerbots.MinRandomBots": "12",
    "Playerbots.MaxRandomBots": "12",
    "Playerbots.AddClassAccountPoolSize": "0",
    "Playerbots.RandomBotMinLevel": "55",
    "Playerbots.RandomBotMaxLevel": "60",
    "Playerbots.RandomBotsPerInterval": "12",
    "Playerbots.RandomBotJoinLfg": "0",
    "Playerbots.RandomBotJoinBG": "0",
    "Playerbots.GearScoreCheck": "0",
    "Playerbots.CommandServerPort": "0",
}


def log(line):
    print(time.strftime("%H:%M:%S"), line, flush=True)


class Fixture:
    def __init__(self, args):
        self.args = args
        host, port, user, password = args.mysql.split(";")
        if host not in ("127.0.0.1", "localhost"):
            raise SystemExit("refusing: the MySQL host must be local")
        if not PREFIX_RE.match(args.prefix):
            raise SystemExit("refusing: --prefix must start with 'agacc' (disposable databases only)")
        self.db = {"host": host, "port": port, "user": user, "password": password}
        self.dist = fresh_install(args.dist)
        self.logs = self.dist / "logs" / args.prefix
        self.procs = []
        self.names = {k: f"{args.prefix}_{k}" for k in ("auth", "world", "chars", "pb")}

    # --- MySQL ------------------------------------------------------------------------------------
    def sql(self, database, statement):
        cmd = ["mysql", "--no-defaults", "--protocol=TCP", "-h", self.db["host"], "-P", self.db["port"], "-u", self.db["user"],
               f"-p{self.db['password']}", "-N", "-B", "-e", statement]
        if database:
            cmd.append(database)
        out = subprocess.run(cmd, capture_output=True, text=True, check=True).stdout
        return [line.split("\t") for line in out.splitlines() if line]

    def env(self):
        info = f"{self.db['host']};{self.db['port']};{self.db['user']};{self.db['password']}"
        env = {k: v for k, v in os.environ.items() if not k.startswith(("AC_", "MYSQL_"))}
        env.update({
            "AC_LOGIN_DATABASE_INFO": f"{info};{self.names['auth']}",
            "AC_WORLD_DATABASE_INFO": f"{info};{self.names['world']}",
            "AC_CHARACTER_DATABASE_INFO": f"{info};{self.names['chars']}",
            "AC_PLAYERBOTS_DATABASE_INFO": f"{info};{self.names['pb']}",
            "AC_DATA_DIR": str(self.dist / "data"),
            "AC_LOGS_DIR": str(self.logs),
            "AC_SOURCE_DIRECTORY": self.args.source_dir,
            "AC_WORLD_SERVER_PORT": str(self.args.world_port),
            "AC_REALM_SERVER_PORT": str(self.args.auth_port),
        })
        return env

    # --- install ----------------------------------------------------------------------------------
    def write_configs(self):
        etc = self.dist / "etc"
        world = etc / "worldserver.conf"
        if world.exists() and MARKER not in world.read_text(errors="replace"):
            raise SystemExit(f"refusing: {world} exists without the fixture marker; use a disposable install")
        plan = [(etc / "worldserver.conf.dist", WORLD_OVERRIDES), (etc / "authserver.conf.dist", AUTH_OVERRIDES)]
        modules = etc / "modules"
        for dist in sorted(modules.glob("*.conf.dist")):
            plan.append((dist, PLAYERBOTS_OVERRIDES if dist.name.startswith("playerbots") else {}))
        for source, overrides in plan:
            text = source.read_text(errors="replace")
            for key, value in overrides.items():
                pattern = re.compile(rf"^{re.escape(key)}\s*=.*$", re.M)
                line = f"{key} = {value}"
                text, count = pattern.subn(line, text)
                if count == 0:
                    text += f"\n{line}\n"
            target = source.with_suffix("")  # drop .dist
            with target.open("x") as handle:
                handle.write(MARKER + "\n" + text)
        self.logs.mkdir(parents=True, exist_ok=True)

    def write_data(self):
        data = self.dist / "data"
        data.mkdir(exist_ok=True)
        dbc = data / "dbc"
        if dbc.is_symlink() or not dbc.exists():
            if dbc.is_symlink():
                dbc.unlink()
            dbc.symlink_to(Path(self.args.dbc).resolve())
        # Header-only terrain (no area, height, liquid or hole data) for every grid of the four
        # continents. It passes Core's start-area map check; positions have no terrain height.
        maps = data / "maps"
        maps.mkdir(exist_ok=True)
        header = struct.pack("<4sI9I", b"MAPS", 9, 0, *([0] * 8))
        for map_id in (0, 1, 530, 571):
            for gx in range(64):
                for gy in range(64):
                    path = maps / f"{map_id:03}{gx:02}{gy:02}.map"
                    if not path.exists():
                        path.write_bytes(header)

    def provision(self):
        # No database deletion or reuse; recheck immediately before provisioning.
        fresh_database_names(self.args.prefix, self.sql(None, "SHOW DATABASES"))
        for binary in ("authserver", "worldserver"):
            log(f"provision: {binary} --dry-run (creates and updates the fixture databases)")
            rc = subprocess.run([str(self.dist / "bin" / binary), "--dry-run"], cwd=self.dist, env=self.env(),
                                stdout=open(self.logs / f"{binary}_provision.out", "w"),
                                stderr=subprocess.STDOUT, timeout=3600).returncode
            if rc != 0:
                raise SystemExit(f"{binary} --dry-run failed rc={rc}")
        for extra in self.args.world_sql or []:
            log(f"provision: world SQL {extra}")
            with open(extra) as handle:
                subprocess.run(["mysql", "--no-defaults", "--protocol=TCP", "-h", self.db["host"], "-P", self.db["port"], "-u", self.db["user"],
                                f"-p{self.db['password']}", self.names["world"]], stdin=handle, check=True)

    def accounts(self):
        auth = self.names["auth"]
        self.sql(auth, f"UPDATE realmlist SET address='127.0.0.1', localAddress='127.0.0.1', "
                       f"port={self.args.world_port}, flag=0 WHERE id=1")
        for (user, password), gm in ((OPERATOR, 3), (COMMANDER, 0)):
            salt, verifier = wc.make_registration(user, password)
            self.sql(auth, f"DELETE FROM account WHERE username='{user}'")
            self.sql(auth, f"INSERT INTO account (username, salt, verifier, expansion) VALUES "
                           f"('{user}', UNHEX('{salt.hex()}'), UNHEX('{verifier.hex()}'), 2)")
            if gm:
                account_id = self.sql(auth, f"SELECT id FROM account WHERE username='{user}'")[0][0]
                self.sql(auth, f"REPLACE INTO account_access (id, gmlevel, RealmID) VALUES ({account_id}, {gm}, -1)")

    # --- processes --------------------------------------------------------------------------------
    def start(self):
        for binary in ("authserver", "worldserver"):
            out = open(self.logs / f"{binary}_run.out", "w")
            proc = subprocess.Popen([str(self.dist / "bin" / binary)], cwd=self.dist, env=self.env(),
                                    stdout=out, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
            self.procs.append((binary, proc))
        deadline = time.monotonic() + self.args.start_timeout
        world_out = self.logs / "worldserver_run.out"
        while time.monotonic() < deadline:
            for binary, proc in self.procs:
                if proc.poll() is not None:
                    raise SystemExit(f"{binary} exited early rc={proc.returncode}; see {self.logs}")
            if "ready..." in world_out.read_text(errors="replace"):
                log("worldserver ready")
                return
            time.sleep(2)
        raise SystemExit("worldserver did not become ready")

    def stop(self):
        for binary, proc in reversed(self.procs):
            if proc.poll() is None:
                proc.send_signal(signal.SIGINT)
                try:
                    proc.wait(timeout=120)
                except subprocess.TimeoutExpired:
                    proc.kill()
            log(f"{binary} stopped rc={proc.returncode}")

    def client(self, account, log_prefix):
        key = wc.auth_login("127.0.0.1", self.args.auth_port, *account)
        return wc.WorldClient("127.0.0.1", self.args.world_port, account[0], key,
                              log=lambda line: log(f"{log_prefix} {line}") if self.args.verbose else None)

    # --- evidence ---------------------------------------------------------------------------------
    def inventory(self, guid):
        rows = self.sql(self.names["chars"],
                        f"SELECT ci.bag, ci.slot, ci.item, COALESCE(ii.itemEntry, 0) FROM character_inventory ci "
                        f"LEFT JOIN item_instance ii ON ii.guid = ci.item WHERE ci.guid = {guid} ORDER BY ci.bag, ci.slot")
        mail = self.sql(self.names["chars"], f"SELECT COUNT(*) FROM mail WHERE receiver = {guid}")[0][0]
        return {"rows": [tuple(int(v) for v in r) for r in rows], "mail": int(mail)}

    def item_levels(self, entries):
        if not entries:
            return {}
        ids = ",".join(str(e) for e in set(entries))
        rows = self.sql(self.names["world"], f"SELECT entry, ItemLevel FROM item_template WHERE entry IN ({ids})")
        return {int(e): int(level) for e, level in rows}

    def result_lines(self, bot_guid_text, since):
        path = self.logs / "Playerbots.log"
        if not path.exists():
            return []
        text = path.read_text(errors="replace")[since:]
        return [line for line in text.splitlines()
                if "PB59 AutoGear bot=" + bot_guid_text in line and " refusal=" in line]

    def log_size(self):
        path = self.logs / "Playerbots.log"
        return len(path.read_text(errors="replace")) if path.exists() else 0


def guid_text(low):
    return f"GUID Full: 0x{low:016x} Type: Player"


class Runner:
    def __init__(self, fixture):
        self.fx = fixture
        self.results = []

    def save_all(self):
        self.ops.command(".saveall", wait=3.0)

    def run_case(self, name, bot, text, expect, prepare=None):
        log(f"case {name}: whisper '{text}' to {bot['name']}")
        if prepare:
            prepare()
        self.save_all()
        before = self.fx.inventory(bot["guid"])
        since = self.fx.log_size()
        start = len(self.cmd.chat)
        self.cmd.whisper(bot["name"], text)
        reply, line = None, None
        deadline = time.monotonic() + 60
        while time.monotonic() < deadline and not (reply and line):
            self.cmd.pump(1.0)
            replies = [m.text for m in self.cmd.whispers_since(start, bot["guid"])]
            reply = reply or (replies[-1] if replies else None)
            lines = self.fx.result_lines(guid_text(bot["guid"]), since)
            line = line or (lines[-1] if lines else None)
        self.save_all()
        after = self.fx.inventory(bot["guid"])
        verdict = self.check(name, bot, expect, before, after, reply, line)
        self.results.append(verdict)
        log(f"case {name}: {'PASS' if verdict['pass'] else 'FAIL'} {verdict['problems']}")
        return verdict

    def check(self, name, bot, expect, before, after, reply, line):
        problems = []
        result = parse_result(line) if line else None
        before_guids = {r[2] for r in before["rows"]}
        after_guids = {r[2] for r in after["rows"]}
        lost = sorted(before_guids - after_guids)
        if lost:
            problems.append(f"owned items left the inventory: {lost}")
        if after["mail"] != before["mail"]:
            problems.append(f"mail count changed {before['mail']} -> {after['mail']}")
        if expect.get("no_result_line"):
            if line:
                problems.append(f"unexpected result line: {line}")
        elif not result:
            problems.append("no PB59 AutoGear result line")
        else:
            wanted = expect.get("refusal")
            if wanted is not None:
                wanted = {str(v) for v in wanted} if isinstance(wanted, (set, tuple, list)) else {str(wanted)}
                if result["refusal"] not in wanted:
                    problems.append(f"refusal={result['refusal']} expected {sorted(wanted)}")
            if expect.get("ran") is not None and result["ran"] != str(expect["ran"]):
                problems.append(f"ran={result['ran']} expected {expect['ran']}")
            if result["ran"] == "1" and result["capHeld"] != "1":
                problems.append("capHeld=0")
            if result["ownedMissing"] not in (None, "0"):
                problems.append(f"ownedMissing={result['ownedMissing']}")
        if expect.get("unchanged") and before["rows"] != after["rows"]:
            problems.append("inventory changed on a refused command")
        new_equipped = [r for r in after["rows"] if r[0] == 0 and r[1] < 19 and r[2] not in before_guids]
        levels = self.fx.item_levels([r[3] for r in new_equipped])
        problems.extend(evidence_problems(before, after, result, levels))
        if expect.get("reply") and not (reply and re.search(expect["reply"], reply)):
            problems.append(f"reply {reply!r} does not match {expect['reply']!r}")
        return {"case": name, "bot": bot["name"], "pass": not problems, "problems": problems, "result": result,
                "reply": reply, "line": line, "lost": lost, "new_equipped": new_equipped,
                "before_count": len(before["rows"]), "after_count": len(after["rows"])}

    # --- setup ------------------------------------------------------------------------------------
    def setup(self):
        fx = self.fx
        self.ops = fx.client(OPERATOR, "ops")
        if not any(c["name"] == OPS_CHAR for c in self.ops.enum_characters()):
            self.ops.create_character(OPS_CHAR, 1, 1)
        cmd_setup = fx.client(COMMANDER, "cmd")
        existing = {c["name"] for c in cmd_setup.enum_characters()}
        for char, cls in ((CMD_CHAR, 1), (ALT_CHAR, 2)):
            if char not in existing:
                code = cmd_setup.create_character(char, 1, cls)
                if code != wc.CHAR_CREATE_SUCCESS:
                    raise SystemExit(f"creating {char} failed: {code:#x}")
        chars = {c["name"]: c for c in cmd_setup.enum_characters()}
        cmd_setup.close()
        ops_guid = next(c["guid"] for c in self.ops.enum_characters() if c["name"] == OPS_CHAR)
        self.ops.login(ops_guid)
        log(f"operator: {self.ops.command(f'.character level {CMD_CHAR} {self.fx.args.commander_level}')}")
        time.sleep(1)
        self.cmd = fx.client(COMMANDER, "cmd")
        self.cmd.login(chars[CMD_CHAR]["guid"])
        self.commander = {"guid": chars[CMD_CHAR]["guid"], "name": CMD_CHAR}
        log(f"commander: {self.cmd.command(f'.playerbots bot add {ALT_CHAR}', wait=8.0)}")
        self.alt = {"guid": chars[ALT_CHAR]["guid"], "name": ALT_CHAR}
        self.random = self.pick_random_bot()
        if self.random:
            self.cmd.send(CMSG_GROUP_INVITE, self.random["name"].encode() + b"\0" + struct.pack("<I", 0))
            self.cmd.pump(10.0)
            log(f"invited random bot {self.random}")

    def pick_random_bot(self):
        # The commander is a level-60 Human: a same-faction random bot within five levels accepts the
        # invite (PlayerbotSecurity). Draenei are left out so F14 settings cannot change the case.
        deadline = time.monotonic() + self.fx.args.bot_timeout
        while time.monotonic() < deadline:
            accounts = [r[0] for r in self.fx.sql(self.fx.names["auth"],
                                                   "SELECT id FROM account WHERE username LIKE 'RNDBOT%'")]
            if accounts:
                rows = self.fx.sql(self.fx.names["chars"],
                                   "SELECT guid, name, race, level FROM characters WHERE online = 1 AND "
                                   "race IN (1, 3, 4, 7) AND level BETWEEN 55 AND 60 AND account IN (" +
                                   ",".join(accounts) + ") ORDER BY guid LIMIT 1")
                if rows:
                    guid, name, race, level = rows[0]
                    return {"guid": int(guid), "name": name, "race": int(race), "level": int(level)}
            self.cmd.pump(5.0)
        log("no Alliance random bot of level 55-60 came online; random-bot cases are skipped")
        return None

    def fill_bags(self, bot, leave_free):
        def prepare():
            self.ops.send(CMSG_SET_SELECTION, struct.pack("<Q", bot["guid"]))
            self.ops.pump(0.5)
            free = self.free_slots(bot)
            for _ in range(max(0, free - leave_free)):
                self.ops.say(f".additem {FILLER_ITEM} 1")
                self.ops.pump(0.2)
            self.ops.pump(2.0)
        return prepare

    def free_slots(self, bot):
        inv = self.fx.inventory(bot["guid"])
        bags = {r[1]: r[3] for r in inv["rows"] if r[0] == 0 and 19 <= r[1] < 23}
        sizes = {}
        if bags:
            ids = ",".join(str(e) for e in bags.values())
            sizes = {int(e): int(s) for e, s in self.fx.sql(
                self.fx.names["world"], f"SELECT entry, ContainerSlots FROM item_template WHERE entry IN ({ids})")}
        capacity = 16 + sum(sizes.get(e, 0) for e in bags.values())
        bag_items = {r[2] for r in inv["rows"] if r[0] == 0 and 19 <= r[1] < 23}
        used = sum(1 for r in inv["rows"] if (r[0] == 0 and 23 <= r[1] < 39) or r[0] in bag_items)
        return capacity - used

    # --- cases ------------------------------------------------------------------------------------
    def cases(self):
        done = r"^Geared up to item level \d+"
        alt, rnd = self.alt, self.random
        self.run_case("A1 alt bot incremental", alt, "autogear", {"refusal": 0, "ran": 1, "reply": done})
        self.run_case("A2 alt bot reset is refused", alt, "autogear reset",
                      {"refusal": 4, "ran": 0, "unchanged": True})
        self.run_case("A3 bad argument is refused", alt, "autogear banana", {"refusal": 1, "ran": 0, "unchanged": True})
        self.run_case("A4 BiS form disabled", alt, "autogear bis", {"refusal": 1, "ran": 0, "unchanged": True})
        if rnd:
            self.run_case("R1 random bot incremental (over-cap strip)", rnd, "autogear",
                          {"refusal": 0, "ran": 1, "reply": done})
            self.run_case("R2 random bot reset", rnd, "autogear reset", {"refusal": 0, "ran": 1, "reply": done})
            self.run_case("R3 random bot reset with full bags is refused", rnd, "autogear reset",
                          {"refusal": 5, "ran": 0, "unchanged": True}, prepare=self.fill_bags(rnd, 0))
        # A GM sender has no verified progression: refused before anything changes.
        self.cmd_backup = self.cmd
        self.cmd = self.ops
        self.run_case("G1 GM sender is not a verified commander", alt, "autogear",
                      {"refusal": 2, "ran": 0, "unchanged": True})
        self.cmd = self.cmd_backup


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--dist", required=True, help="disposable install prefix (bin/, etc/)")
    parser.add_argument("--dbc", required=True, help="DBC directory for this tuple")
    parser.add_argument("--source-dir", required=True, help="Core source tree (SQL base/updates)")
    parser.add_argument("--mysql", required=True, help="host;port;user;password of a local MySQL")
    parser.add_argument("--prefix", default="agacc", help="database prefix; must start with 'agacc'")
    parser.add_argument("--world-sql", action="append", help="extra world SQL applied after provisioning")
    parser.add_argument("--auth-port", type=int, default=13724)
    parser.add_argument("--world-port", type=int, default=18085)
    parser.add_argument("--commander-level", type=int, default=60)
    parser.add_argument("--start-timeout", type=int, default=1800)
    parser.add_argument("--bot-timeout", type=int, default=600)
    parser.add_argument("--isolated-fixture", action="store_true", help="confirm fresh private install and dedicated disposable local DB instance")
    parser.add_argument("--report", default="autogear_acceptance_report.json")
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()

    if not args.isolated_fixture:
        parser.error("requires --isolated-fixture; never point at an owner installation or DB")
    if not re.fullmatch(r"[A-Za-z0-9_.-]+\.json", args.report):
        parser.error("--report must be a simple JSON filename inside fixture logs")
    fixture = Fixture(args)
    fresh_database_names(args.prefix, fixture.sql(None, "SHOW DATABASES"))
    fixture.write_configs()
    fixture.write_data()
    fixture.provision()
    fixture.accounts()
    runner = Runner(fixture)
    try:
        fixture.start()
        runner.setup()
        runner.cases()
    finally:
        for client in (getattr(runner, "cmd", None), getattr(runner, "ops", None)):
            if client:
                try:
                    client.logout()
                except Exception:  # noqa: BLE001 - teardown must reach the server stop
                    pass
                client.close()
        fixture.stop()
    errors = (fixture.logs / "Errors.log")
    completed = len(runner.results)
    report = {"cases": runner.results, "passed": sum(r["pass"] for r in runner.results),
              "total": completed, "expected": 8, "incomplete": max(0, 8 - completed),
              "errors_log": errors.read_text(errors="replace").splitlines() if errors.exists() else []}
    with (fixture.logs / args.report).open("x") as handle:
        handle.write(json.dumps(report, indent=2, default=str))
    log(f"{report['passed']}/{report['total']} cases passed; report {args.report}")
    return 0 if report["total"] == report["expected"] == report["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
