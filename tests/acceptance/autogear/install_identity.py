"""Fail-closed checks that a disposable install is the one its binaries were built for.

Module configs are read from the CONF_DIR compiled into the server binaries, not from the
directory the binary runs in. A copied or reused install would silently load another install's
module configs. These checks bind the install to a build tree:

- the build's CMakeCache CONF_DIR (or CMAKE_INSTALL_PREFIX/etc when CONF_DIR is unset) must be
  this install's etc directory;
- each installed binary's ELF build-ID must equal the build tree's binary. Installing rewrites
  RPATH, so file hashes differ; the build-ID note does not change.

They do not prove the source tuple of the build. Pair them with the recorded tuple.
"""
import re
import socket
import subprocess
from pathlib import Path

BINARIES = ("authserver", "worldserver")


def cache_value(cache_text, key):
    values = re.findall(rf"^{re.escape(key)}:[A-Z]+=(.*)$", cache_text, re.M)
    if len(values) > 1:
        raise ValueError(f"duplicate CMake cache entry {key}")
    return values[0].strip() if values else None


def compiled_conf_dir(cache_text):
    conf = cache_value(cache_text, "CONF_DIR")
    if conf:
        return conf
    prefix = cache_value(cache_text, "CMAKE_INSTALL_PREFIX")
    if not prefix:
        raise ValueError("CMake cache has neither CONF_DIR nor CMAKE_INSTALL_PREFIX")
    return prefix.rstrip("/") + "/etc"


def build_id(path, readelf="readelf"):
    out = subprocess.run([readelf, "-n", str(path)], capture_output=True, text=True, check=True).stdout
    ids = re.findall(r"Build ID:\s*([0-9a-f]+)", out)
    if len(ids) != 1:
        raise ValueError(f"expected exactly one ELF build-ID in {path}")
    return ids[0]


def install_identity_problems(dist, build_dir, read_build_id=build_id):
    """Problems binding install `dist` to `build_dir`; empty when bound. Reads only."""
    dist, build_dir = Path(dist), Path(build_dir)
    problems = []
    cache = build_dir / "CMakeCache.txt"
    if not cache.is_file():
        return [f"missing build cache {cache}"]
    try:
        conf = compiled_conf_dir(cache.read_text(errors="strict"))
    except ValueError as error:
        return [str(error)]
    if Path(conf).resolve() != (dist / "etc").resolve():
        problems.append(f"binaries read module configs from {conf}, not this install's etc")
    for name in BINARIES:
        built = build_dir / "src" / "server" / "apps" / name
        installed = dist / "bin" / name
        if not built.is_file() or not installed.is_file():
            problems.append(f"missing {name} in build tree or install")
            continue
        try:
            if read_build_id(built) != read_build_id(installed):
                problems.append(f"installed {name} is not the build tree's binary (build-ID differs)")
        except (ValueError, subprocess.CalledProcessError, OSError) as error:
            problems.append(f"cannot read {name} build-ID: {error}")
    return problems


def port_problems(ports, connect=None):
    """A fixture port that already accepts connections belongs to something else."""
    def default_connect(port):
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
            probe.settimeout(1.0)
            return probe.connect_ex(("127.0.0.1", port)) == 0
    connect = connect or default_connect
    problems = []
    if len(set(ports)) != len(ports):
        problems.append("fixture ports must be distinct")
    for port in ports:
        if not 1024 <= port <= 65535:
            problems.append(f"port {port} outside 1024-65535")
        elif connect(port):
            problems.append(f"127.0.0.1:{port} is already in use")
    return problems
