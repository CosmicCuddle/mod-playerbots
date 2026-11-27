"""Fail-closed database endpoint identity and privilege checks for the fixture preflight.

A loopback host proves nothing about which server answers: it can be a tunnel. This check
infers nothing from the address. The operator records the dedicated disposable instance in an
expected-identity file when they create it, and preflight requires the live server to match it.

Expected identity (JSON):
    flavor              "mysql" or "mariadb"
    server_uuid         MySQL: required. MariaDB has no @@server_uuid: must be null.
    hostname, port, datadir, server_id
                        all required, for both flavors
    account             the exact CURRENT_USER(), e.g. "agacc@localhost"
    grants              the exact SHOW GRANTS lines, password material omitted
    isolation_evidence  the operator's note of how isolation was established (non-empty)

Privileges must be an exact allowlist. Nothing is elevated, and no global privilege is asked
for. Beyond `USAGE ON *.*`, every grant must target a schema named `<prefix>\\_...`. An account
that can reach any other schema is refused. Password material is redacted before reporting.
"""
import json
import re
from pathlib import Path

REQUIRED = ("flavor", "server_uuid", "hostname", "port", "datadir", "server_id", "account", "grants",
            "isolation_evidence")
IDENTITY_QUERY = ("SELECT @@version, @@hostname, @@port, @@datadir, @@server_id, CURRENT_USER()")
UUID_QUERY = "SELECT @@server_uuid"
SECRET = re.compile(r"(IDENTIFIED\s+(?:BY\s+PASSWORD|BY|WITH\s+\S+\s+(?:AS|BY)))\s+'(?:[^'\\]|\\.)*'", re.I)
GRANT_ON = re.compile(r"^GRANT\s+(.+?)\s+ON\s+(\S+)\s+TO\s", re.I)


def redact(line):
    return " ".join(SECRET.sub(r"\1 '<redacted>'", line).split())


def load_expected(path):
    data = json.loads(Path(path).read_text(encoding="utf-8"))
    missing = [key for key in REQUIRED if key not in data]
    if missing:
        raise ValueError(f"expected identity lacks {missing}")
    if data["flavor"] not in ("mysql", "mariadb"):
        raise ValueError("expected identity flavor must be mysql or mariadb")
    if data["flavor"] == "mysql" and not data["server_uuid"]:
        raise ValueError("a MySQL identity needs server_uuid")
    if data["flavor"] == "mariadb" and data["server_uuid"] is not None:
        raise ValueError("MariaDB has no @@server_uuid; record null")
    if not str(data["isolation_evidence"]).strip():
        raise ValueError("isolation_evidence must say how the instance was isolated")
    if not isinstance(data["grants"], list) or not data["grants"]:
        raise ValueError("grants must be the exact non-empty SHOW GRANTS list")
    return data


def probe(rows):
    """Observed identity from a read-only `rows(statement)` callable. Missing data stays None."""
    observed = {}
    row = rows(IDENTITY_QUERY)
    if len(row) != 1 or len(row[0]) != 6:
        raise ValueError("identity query returned an unexpected shape")
    version, hostname, port, datadir, server_id, account = row[0]
    observed.update(flavor="mariadb" if "mariadb" in version.lower() else "mysql", hostname=hostname,
                    port=port, datadir=datadir, server_id=server_id, account=account)
    try:
        uuid = rows(UUID_QUERY)
        observed["server_uuid"] = uuid[0][0] if len(uuid) == 1 and uuid[0] and uuid[0][0] else None
    except Exception:  # MariaDB: unknown system variable
        observed["server_uuid"] = None
    observed["grants"] = [redact(r[0]) for r in rows("SHOW GRANTS FOR CURRENT_USER()") if r]
    return observed


def grant_scope_problems(grants, prefix):
    problems = []
    for line in grants:
        match = GRANT_ON.match(line)
        if not match:
            problems.append(f"unparsed grant refused: {line}")
            continue
        privileges, target = match.group(1).strip().upper(), match.group(2)
        if target == "*.*":
            if privileges != "USAGE":
                problems.append(f"global privilege refused: {line}")
            continue
        if line.upper().startswith("GRANT PROXY"):
            problems.append(f"proxy grant refused: {line}")
            continue
        schema = target.split(".", 1)[0].strip("`")
        if not schema.startswith(prefix + "\\_"):
            problems.append(f"grant outside {prefix}\\_ schemas refused: {line}")
    return problems


def db_identity_problems(observed, expected, prefix):
    """Empty only when the live endpoint is the recorded one and its grants are the exact allowlist."""
    problems = []
    if observed["flavor"] != expected["flavor"]:
        problems.append(f"server is {observed['flavor']}, expected {expected['flavor']}")
    for key in ("server_uuid", "hostname", "port", "datadir", "server_id", "account"):
        seen, want = observed.get(key), expected[key]
        if want is None:
            if seen is not None:
                problems.append(f"{key} {seen!r} is not the recorded null")
        elif seen is None:
            problems.append(f"{key} unavailable; identity unverified")
        elif str(seen) != str(want):
            problems.append(f"{key} {seen!r} is not the recorded {want!r}")
    want = {redact(g) for g in expected["grants"]}
    seen = set(observed["grants"])
    for extra in sorted(seen - want):
        problems.append(f"grant not in the allowlist: {extra}")
    for missing in sorted(want - seen):
        problems.append(f"allowlisted grant missing: {missing}")
    problems += grant_scope_problems(sorted(want | seen), prefix)
    return problems
