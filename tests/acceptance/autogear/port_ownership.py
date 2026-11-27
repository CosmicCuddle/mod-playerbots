"""After start: prove that our spawned servers own every listener on the fixture ports.

A free port at preflight does not prove that our server answers later. Another process can bind
first, or listen on a different address or on IPv6. Before any client connects, every LISTEN
socket on a fixture port, in /proc/net/tcp and /proc/net/tcp6, must belong to a process in our
spawned tree.

- PID reuse: each spawned root's start time (field 22 of /proc/<pid>/stat) must equal the value
  recorded at spawn. A descendant counts only if its parent chain reaches a verified root and it
  started no earlier than that root.
- An inode with no visible owner (permissions, a race, an exited process) refuses.
- A port with no listener refuses, and a missing tcp6 table refuses.

Inputs are plain /proc text, so the checks run on synthetic snapshots. `read_snapshot` reads the
live /proc. It is not called from the runner in this scope: no server is started here.
"""
import os
from pathlib import Path

LISTEN = "0A"


def parse_listeners(table_text):
    """[(port, inode)] for LISTEN rows of a /proc/net/tcp or tcp6 table."""
    rows = []
    for line in table_text.splitlines()[1:]:
        fields = line.split()
        if len(fields) < 10:
            raise ValueError(f"short /proc/net row: {line!r}")
        if fields[3] != LISTEN:
            continue
        rows.append((int(fields[1].rsplit(":", 1)[1], 16), fields[9]))
    return rows


def parse_stat(stat_text):
    """(ppid, starttime) from /proc/<pid>/stat; the command name may contain spaces or ')'."""
    rest = stat_text.rsplit(")", 1)[1].split()
    if len(rest) < 20:
        raise ValueError("short /proc stat")
    return int(rest[1]), int(rest[19])


def our_tree(roots, procs):
    """PIDs in our tree, plus problems for roots that are gone or reused."""
    problems, stats, tree = [], {}, {}
    for pid, info in procs.items():
        try:
            stats[pid] = parse_stat(info["stat"])
        except (KeyError, ValueError, IndexError):
            continue
    for pid, recorded in roots.items():
        if pid not in stats:
            problems.append(f"spawned pid {pid} is gone")
        elif stats[pid][1] != recorded:
            problems.append(f"pid {pid} start time changed: PID reused")
        else:
            tree[pid] = recorded
    changed = True
    while changed:
        changed = False
        for pid, (ppid, start) in stats.items():
            if pid not in tree and ppid in tree and start >= tree[ppid]:
                tree[pid] = start
                changed = True
    return set(tree), problems


def ownership_problems(ports, roots, snapshot):
    """Empty only when every LISTEN socket on every fixture port belongs to our spawned tree."""
    problems = []
    tables = {name: snapshot.get(name) for name in ("tcp", "tcp6")}
    for name, text in tables.items():
        if text is None:
            problems.append(f"/proc/net/{name} unavailable; ownership unverified")
    procs = snapshot.get("procs", {})
    tree, tree_problems = our_tree(roots, procs)
    problems += tree_problems
    owners = {}
    for pid, info in procs.items():
        for inode in info.get("fds", ()):
            owners.setdefault(inode, set()).add(pid)
    listeners = []
    for text in tables.values():
        if text is not None:
            try:
                listeners += parse_listeners(text)
            except (ValueError, IndexError) as error:
                problems.append(str(error))
    for port in ports:
        on_port = [inode for p, inode in listeners if p == port]
        if not on_port:
            problems.append(f"nothing listens on port {port}")
        for inode in on_port:
            who = owners.get(inode, set())
            if not who:
                problems.append(f"port {port} listener inode {inode} has no visible owner")
            elif not who <= tree:
                problems.append(f"port {port} listener belongs to foreign pid(s) {sorted(who - tree)}")
    return problems


def read_snapshot(proc=Path("/proc")):
    """Live /proc snapshot: both TCP tables and, per readable process, stat and socket inodes."""
    snapshot = {}
    for name in ("tcp", "tcp6"):
        try:
            snapshot[name] = (proc / "net" / name).read_text()
        except OSError:
            snapshot[name] = None
    procs = {}
    for entry in proc.iterdir():
        if not entry.name.isdigit():
            continue
        try:
            stat_text = (entry / "stat").read_text()
            fd_entries = list((entry / "fd").iterdir())
        except OSError:
            continue  # unreadable process: its sockets stay ownerless, so a listener there refuses
        fds = []
        for fd in fd_entries:
            try:
                target = os.readlink(fd)
            except OSError:
                continue  # closed while scanning
            if target.startswith("socket:["):
                fds.append(target[8:-1])
        procs[int(entry.name)] = {"stat": stat_text, "fds": fds}
    snapshot["procs"] = procs
    return snapshot


def start_time(pid, proc=Path("/proc")):
    return parse_stat((proc / str(pid) / "stat").read_text())[1]
