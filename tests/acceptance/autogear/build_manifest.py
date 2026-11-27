"""Build manifest: binds the exact clean source tuple to the installed binaries and configs.

The operator writes the manifest at build time from fresh clean checkouts:
    core        40-hex Core SHA
    modules     {name: 40-hex SHA} for every module built in
    clean       true only when every tree had no local changes at configure and build
    conf_dir    the CONF_DIR compiled into the binaries
    binaries    {"authserver": build-ID, "worldserver": build-ID}
    configs     {path under etc/: sha256} for every *.dist file installed under etc/

Preflight requires the manifest to equal --expect-tuple exactly, the installed binaries and
config templates to match it, and Core's embedded revision stamp to show the expected Core SHA.
The stamp is additional evidence only. It is regenerated only at configure, so it can be stale.
A match never stands in for the manifest, and a mismatch or missing stamp refuses.
"""
import hashlib
import json
import re
from pathlib import Path

SHA = re.compile(r"^[0-9a-f]{40}$")
BINARIES = ("authserver", "worldserver")


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def revision_stamp_problems(worldserver_bytes, core_sha):
    if not SHA.match(core_sha or ""):
        return ["no expected Core SHA to compare with the revision stamp"]
    if core_sha[:12].encode() not in worldserver_bytes:
        return [f"worldserver revision stamp does not show Core {core_sha[:12]} (stale or other build)"]
    return []


def manifest_problems(manifest_path, expected, dist, read_build_id, read_bytes=None):
    read_bytes = read_bytes or (lambda p: Path(p).read_bytes())
    dist = Path(dist)
    try:
        manifest = json.loads(Path(manifest_path).read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        return [f"cannot read build manifest: {error}"]
    if not isinstance(manifest, dict):
        return ["build manifest must be an object"]
    problems = []
    modules = manifest.get("modules")
    if not isinstance(modules, dict):
        return ["build manifest lacks modules"]
    shas = {"core": manifest.get("core"), **modules}
    for name, sha in shas.items():
        if not isinstance(sha, str) or not SHA.match(sha):
            problems.append(f"manifest {name} is not a 40-hex SHA")
    if shas != expected:
        problems.append(f"manifest tuple {shas} is not --expect-tuple {expected}")
    if manifest.get("clean") is not True:
        problems.append("manifest does not record clean source trees")
    conf = manifest.get("conf_dir")
    if not isinstance(conf, str) or Path(conf).resolve() != (dist / "etc").resolve():
        problems.append(f"manifest conf_dir {conf!r} is not this install's etc")
    binaries = manifest.get("binaries") or {}
    for name in BINARIES:
        try:
            if binaries.get(name) != read_build_id(dist / "bin" / name):
                problems.append(f"installed {name} build-ID is not the manifest's")
        except (ValueError, OSError, Exception) as error:  # noqa: B014 - any read failure refuses
            problems.append(f"cannot read {name} build-ID: {error}")
    configs = manifest.get("configs")
    etc = (dist / "etc").resolve()
    installed = {p.relative_to(etc).as_posix() for p in etc.rglob("*.dist")} if etc.is_dir() else set()
    if not isinstance(configs, dict) or not configs:
        problems.append("manifest lists no config templates")
        configs = {}
    if set(configs) != installed:
        problems.append(f"config templates {sorted(installed)} are not the manifest's {sorted(configs)}")
    for rel, digest in configs.items():
        path = (etc / rel).resolve()
        if etc not in path.parents:
            problems.append(f"manifest config path escapes etc: {rel}")
        elif not path.is_file() or sha256(path) != digest:
            problems.append(f"config template {rel} does not match the manifest")
    try:
        problems += revision_stamp_problems(read_bytes(dist / "bin" / "worldserver"), expected.get("core"))
    except OSError as error:
        problems.append(f"cannot read worldserver revision stamp: {error}")
    return problems
