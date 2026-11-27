"""Write-free preflight for the disposable AutoGear acceptance fixture.

`collect_preflight` runs every fail-closed check the runner applies before its first write and
returns one report. It never writes a file, a config or a database row; the runner's
--preflight-only mode prints the report and stops. A passing preflight is a precondition for a
native run, not evidence that the fixture is isolated or that any case passes.
"""
import re
import subprocess
from pathlib import Path

from fixture_guard import fresh_database_names, fresh_install
from build_manifest import manifest_problems
from db_identity import db_identity_problems, load_expected, probe
from install_identity import build_id, install_identity_problems, port_problems

TUPLE_NAME = re.compile(r"^(core|[a-z0-9][a-z0-9-]*)$")
SHA = re.compile(r"^[0-9a-f]{40}$")


def parse_expected_tuple(items):
    """`name=full-sha` pairs; `core` is the Core tree, any other name a module under modules/."""
    expected = {}
    for item in items or []:
        name, sep, sha = item.partition("=")
        if not sep or not TUPLE_NAME.match(name) or not SHA.match(sha) or name in expected:
            raise ValueError(f"--expect-tuple needs unique name=<40-hex sha>, got {item!r}")
        expected[name] = sha
    return expected


def git_head(path):
    try:
        out = subprocess.run(["git", "-C", str(path), "rev-parse", "HEAD"], capture_output=True, text=True,
                             check=True).stdout.strip()
        dirty = subprocess.run(["git", "-C", str(path), "status", "--porcelain", "--untracked-files=no"],
                               capture_output=True, text=True, check=True).stdout.strip()
    except (subprocess.CalledProcessError, OSError):
        return None, None
    return (out if SHA.match(out) else None), bool(dirty)


def tuple_report(source_dir, expected, head=git_head):
    """Observed HEAD per expected component; a component passes only on an exact clean match."""
    report, problems = {}, []
    for name, sha in expected.items():
        path = Path(source_dir) if name == "core" else Path(source_dir) / "modules" / name
        observed, dirty = head(path)
        report[name] = {"expected": sha, "observed": observed, "dirty": dirty}
        if observed is None:
            problems.append(f"{name}: no git checkout at {path}; tuple unverified")
        elif observed != sha:
            problems.append(f"{name}: HEAD {observed} is not expected {sha}")
        elif dirty:
            problems.append(f"{name}: checkout has local changes")
    return report, problems


def collect_preflight(args, show_databases, head=git_head, read_build_id=None, connect=None, db_rows=None,
                      read_bytes=None):
    """Every pre-write check, collected instead of raised. `show_databases` returns SHOW DATABASES rows;
    `db_rows(statement)` answers the read-only identity and grant queries."""
    checks = {}

    def record(name, problems):
        checks[name] = {"ok": not problems, "problems": problems}

    try:
        fresh_install(args.dist)
        record("fresh_install", [])
    except ValueError as error:
        record("fresh_install", [str(error)])
    try:
        rows = show_databases()
        fresh_database_names(args.prefix, rows)
        record("fresh_databases", [])
    except (ValueError, subprocess.CalledProcessError, OSError) as error:
        record("fresh_databases", [str(error)])
    kwargs = {"read_build_id": read_build_id} if read_build_id else {}
    record("install_identity", install_identity_problems(args.dist, args.build_dir, **kwargs))
    record("ports", port_problems([args.auth_port, args.world_port], connect=connect))
    dbc = Path(args.dbc)
    record("dbc", [] if dbc.is_dir() and any(dbc.glob("*.dbc")) else [f"no .dbc files in {dbc}"])
    try:
        expected = parse_expected_tuple(getattr(args, "expect_tuple", None))
        tuple_seen, tuple_problems = tuple_report(args.source_dir, expected, head=head)
        if not expected:
            tuple_problems = ["no --expect-tuple given; the source tuple is not recorded"]
    except ValueError as error:
        expected, tuple_seen, tuple_problems = {}, {}, [str(error)]
    record("source_tuple", tuple_problems)
    manifest = getattr(args, "build_manifest", None)
    if not manifest:
        record("build_manifest", ["no --build-manifest given; source-to-binary binding unverified"])
    elif not expected:
        record("build_manifest", ["no expected tuple; manifest not compared"])
    else:
        record("build_manifest", manifest_problems(manifest, expected, args.dist, read_build_id or build_id,
                                                   read_bytes=read_bytes))
    identity = getattr(args, "expect_db_identity", None)
    if not identity:
        record("db_identity", ["no --expect-db-identity given; database endpoint unverified"])
    elif db_rows is None:
        record("db_identity", ["no read-only identity query available"])
    else:
        try:
            record("db_identity", db_identity_problems(probe(db_rows), load_expected(identity), args.prefix))
        except (ValueError, KeyError, OSError, subprocess.CalledProcessError) as error:
            record("db_identity", [f"database identity unverified: {error}"])
    return {"ok": all(c["ok"] for c in checks.values()), "checks": checks, "tuple": tuple_seen,
            "writes": "none", "native_run": False}
