"""Listener ownership after start, on synthetic /proc snapshots plus one live self-check."""
import os
import socket
import unittest

from port_ownership import ownership_problems, parse_stat, read_snapshot, start_time

HEADER = "  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode\n"


def row(port, inode, state="0A", addr="0100007F"):
    return f"   0: {addr}:{port:04X} 00000000:0000 {state} 00000000:00000000 00:00000000 00000000  1000 0 {inode}\n"


def row6(port, inode):
    return row(port, inode, addr="00000000000000000000000001000000")


def stat(pid, ppid, start, comm="worldserver"):
    rest = ["S", str(ppid)] + ["0"] * 17 + [str(start)] + ["0"] * 10
    return f"{pid} ({comm}) " + " ".join(rest)


def snapshot(tcp_rows, tcp6_rows="", procs=None, tcp6=True):
    return {"tcp": HEADER + tcp_rows, "tcp6": HEADER + tcp6_rows if tcp6 else None, "procs": procs or {}}


OURS = {100: {"stat": stat(100, 1, 5000, "authserver"), "fds": ["11"]},
        200: {"stat": stat(200, 1, 5100), "fds": ["22"]}}
ROOTS = {100: 5000, 200: 5100}
PORTS = [13724, 18085]


class PortOwnershipTest(unittest.TestCase):
    def test_our_listeners_pass(self):
        snap = snapshot(row(13724, "11") + row(18085, "22") + row(18085, "99", state="01"), procs=OURS)
        self.assertEqual(ownership_problems(PORTS, ROOTS, snap), [])

    def test_foreign_listener_refuses(self):
        procs = {**OURS, 300: {"stat": stat(300, 1, 4000), "fds": ["33"]}}
        snap = snapshot(row(13724, "11") + row(18085, "33"), procs=procs)
        self.assertTrue(ownership_problems(PORTS, ROOTS, snap))

    def test_extra_foreign_ipv6_listener_refuses(self):
        procs = {**OURS, 300: {"stat": stat(300, 1, 4000), "fds": ["33"]}}
        snap = snapshot(row(13724, "11") + row(18085, "22"), row6(18085, "33"), procs=procs)
        self.assertTrue(ownership_problems(PORTS, ROOTS, snap))

    def test_our_ipv6_listener_passes(self):
        snap = snapshot(row(13724, "11"), row6(18085, "22"), procs=OURS)
        self.assertEqual(ownership_problems(PORTS, ROOTS, snap), [])

    def test_missing_tcp6_table_refuses(self):
        snap = snapshot(row(13724, "11") + row(18085, "22"), procs=OURS, tcp6=False)
        self.assertTrue(ownership_problems(PORTS, ROOTS, snap))

    def test_no_listener_refuses(self):
        self.assertTrue(ownership_problems(PORTS, ROOTS, snapshot(row(13724, "11"), procs=OURS)))

    def test_unknown_owner_refuses(self):
        snap = snapshot(row(13724, "11") + row(18085, "77"), procs=OURS)
        self.assertTrue(ownership_problems(PORTS, ROOTS, snap))

    def test_reused_pid_refuses(self):
        procs = {**OURS, 200: {"stat": stat(200, 1, 9999), "fds": ["22"]}}
        snap = snapshot(row(13724, "11") + row(18085, "22"), procs=procs)
        self.assertTrue(ownership_problems(PORTS, ROOTS, snap))

    def test_gone_root_refuses(self):
        snap = snapshot(row(13724, "11") + row(18085, "22"), procs={100: OURS[100]})
        self.assertTrue(ownership_problems(PORTS, ROOTS, snap))

    def test_descendant_counts_only_if_started_after_root(self):
        child = {201: {"stat": stat(201, 200, 5200), "fds": ["22"]}}
        procs = {100: OURS[100], 200: {"stat": stat(200, 1, 5100), "fds": []}, **child}
        snap = snapshot(row(13724, "11") + row(18085, "22"), procs=procs)
        self.assertEqual(ownership_problems(PORTS, ROOTS, snap), [])
        procs[201] = {"stat": stat(201, 200, 5000), "fds": ["22"]}  # older than its "parent": a reused ppid
        self.assertTrue(ownership_problems(PORTS, ROOTS, snapshot(row(13724, "11") + row(18085, "22"),
                                                                  procs=procs)))

    def test_shared_inode_with_foreign_process_refuses(self):
        procs = {**OURS, 300: {"stat": stat(300, 1, 4000), "fds": ["22"]}}
        snap = snapshot(row(13724, "11") + row(18085, "22"), procs=procs)
        self.assertTrue(ownership_problems(PORTS, ROOTS, snap))

    def test_malformed_table_refuses(self):
        snap = {"tcp": HEADER + "garbage\n", "tcp6": HEADER, "procs": OURS}
        self.assertTrue(ownership_problems(PORTS, ROOTS, snap))

    def test_command_name_with_parenthesis_parses(self):
        self.assertEqual(parse_stat(stat(5, 4, 77, comm="a) b")), (4, 77))

    @unittest.skipUnless(os.path.isdir("/proc/self/fd"), "needs Linux /proc")
    def test_live_self_listener(self):
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
            listener.bind(("127.0.0.1", 0))
            listener.listen(1)
            port = listener.getsockname()[1]
            roots = {os.getpid(): start_time(os.getpid())}
            # Without IPv6 (as in some sandboxes) the only problem must be the missing tcp6 table.
            expected = [] if os.path.exists("/proc/net/tcp6") else ["/proc/net/tcp6 unavailable; ownership unverified"]
            self.assertEqual(ownership_problems([port], roots, read_snapshot()), expected)
            self.assertTrue(ownership_problems([port], {os.getpid(): roots[os.getpid()] + 1}, read_snapshot()))


if __name__ == "__main__":
    unittest.main()
