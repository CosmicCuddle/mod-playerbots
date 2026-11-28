"""Startup gate on mocked processes and /proc snapshots: no client before ready and owned.

Nothing here starts a server: Popen, /proc and the clock are fakes. The runner's own
`Fixture.start` and `Fixture.stop` are exercised with those fakes.
"""
import itertools
import signal
import subprocess
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

import run_autogear_acceptance as runner_module
from startup_gate import StartupRefused, spawn, stop_owned, wait_ready_and_owned
from test_port_ownership import HEADER, row, row6, stat

AUTH, WORLD = 13724, 18085


class FakeProc:
    def __init__(self, pid, exit_after=None, ignores_sigint=False):
        self.pid, self.returncode, self.signals = pid, None, []
        self.exit_after, self.polls, self.ignores_sigint, self.killed = exit_after, 0, ignores_sigint, False

    def poll(self):
        self.polls += 1
        if self.exit_after is not None and self.polls > self.exit_after and self.returncode is None:
            self.returncode = 1
        return self.returncode

    def send_signal(self, sig):
        self.signals.append(sig)
        if not self.ignores_sigint:
            self.returncode = -sig

    def wait(self, timeout=None):
        if self.returncode is None:
            raise subprocess.TimeoutExpired("server", timeout)
        return self.returncode

    def kill(self):
        self.killed = True
        self.returncode = -9


class Clock:
    def __init__(self):
        self.now = 0.0

    def __call__(self):
        return self.now

    def sleep(self, seconds):
        self.now += seconds


def owned_snapshot(auth_pid=100, world_pid=200, auth_start=5000, world_start=5100, extra_rows="", tcp6_rows="",
                   extra_procs=None, tcp6=True):
    procs = {auth_pid: {"stat": stat(auth_pid, 1, auth_start, "authserver"), "fds": ["11"]},
             world_pid: {"stat": stat(world_pid, 1, world_start), "fds": ["22"]}, **(extra_procs or {})}
    return {"tcp": HEADER + row(AUTH, "11") + row(WORLD, "22") + extra_rows,
            "tcp6": HEADER + tcp6_rows if tcp6 else None, "procs": procs}


def spawned(auth=None, world=None):
    return [("authserver", auth or FakeProc(100), 5000, None), ("worldserver", world or FakeProc(200), 5100, None)]


def gate(procs, snapshot, ready=lambda: True, timeout=10):
    clock = Clock()
    wait_ready_and_owned(procs, [AUTH, WORLD], ready, snapshot, timeout, clock=clock, sleep=clock.sleep)
    return clock


class StartupGateTest(unittest.TestCase):
    def test_ready_and_owned_passes(self):
        gate(spawned(), owned_snapshot)

    def test_waits_for_ready_then_ownership(self):
        states = itertools.chain([False, False], itertools.repeat(True))
        snaps = iter([owned_snapshot(tcp6=False), owned_snapshot()])
        clock = gate(spawned(), lambda: next(snaps), ready=lambda: next(states))
        self.assertEqual(clock.now, 6.0)

    def refuses(self, snapshot, procs=None, ready=lambda: True, timeout=10):
        with self.assertRaises(StartupRefused) as caught:
            gate(procs or spawned(), snapshot, ready=ready, timeout=timeout)
        return str(caught.exception)

    def test_absent_listener_refuses(self):
        snap = owned_snapshot()
        snap["tcp"] = HEADER + row(AUTH, "11")
        self.assertIn("nothing listens", self.refuses(lambda: snap))

    def test_foreign_listener_refuses(self):
        foreign = {300: {"stat": stat(300, 1, 4000, "mysqld"), "fds": ["33"]}}
        self.assertIn("foreign", self.refuses(lambda: owned_snapshot(extra_rows=row(WORLD, "33"),
                                                                     extra_procs=foreign)))

    def test_foreign_ipv6_listener_refuses(self):
        foreign = {300: {"stat": stat(300, 1, 4000), "fds": ["33"]}}
        self.assertIn("foreign", self.refuses(lambda: owned_snapshot(tcp6_rows=row6(AUTH, "33"),
                                                                     extra_procs=foreign)))

    def test_unknown_owner_and_missing_ipv6_table_refuse(self):
        self.assertIn("no visible owner", self.refuses(lambda: owned_snapshot(extra_rows=row(AUTH, "77"))))
        self.assertIn("tcp6 unavailable", self.refuses(lambda: owned_snapshot(tcp6=False)))

    def test_pid_reuse_refuses(self):
        self.assertIn("PID reused", self.refuses(lambda: owned_snapshot(world_start=9999)))

    def test_unknown_start_time_refuses_before_polling(self):
        procs = spawned()
        procs[1] = ("worldserver", procs[1][1], None, "cannot read start time")
        self.assertIn("start time", self.refuses(lambda: owned_snapshot(), procs=procs))

    def test_process_exit_refuses(self):
        self.assertIn("exited early", self.refuses(owned_snapshot, procs=spawned(world=FakeProc(200, exit_after=0)),
                                                   ready=lambda: False))

    def test_timeout_refuses(self):
        self.assertIn("timed out", self.refuses(owned_snapshot, ready=lambda: False, timeout=5))
        self.assertIn("timed out", self.refuses(lambda: owned_snapshot(tcp6=False), timeout=5))

    def test_snapshot_error_refuses(self):
        def broken():
            raise PermissionError("denied")
        self.assertIn("snapshot failed", self.refuses(broken, timeout=2))

    def test_spawn_records_start_time_immediately(self):
        proc = FakeProc(100)
        name, got, started, problem = spawn("authserver", ["x"], popen=lambda cmd, **kw: proc,
                                            read_start=lambda pid: 5000)
        self.assertEqual((name, got, started, problem), ("authserver", proc, 5000, None))

        def gone(pid):
            raise FileNotFoundError(2, "gone")
        self.assertIsNotNone(spawn("w", ["x"], popen=lambda cmd, **kw: proc, read_start=gone)[3])

    def test_stop_owned_signals_only_our_children(self):
        auth, world = FakeProc(100), FakeProc(200, ignores_sigint=True)
        done = FakeProc(300)
        done.returncode = 0
        stop_owned([("a", auth, 1, None), ("w", world, 1, None), ("d", done, 1, None)], grace=1)
        self.assertEqual(auth.signals, [signal.SIGINT])
        self.assertEqual(world.signals, [signal.SIGINT])
        self.assertTrue(world.killed)
        self.assertEqual(done.signals, [])
        self.assertFalse(auth.killed)


class FixtureStartTest(unittest.TestCase):
    """The runner's Fixture.start/stop with fakes: no client before the gate, cleanup only ours."""

    def fixture(self, root):
        fx = object.__new__(runner_module.Fixture)
        fx.args = SimpleNamespace(auth_port=AUTH, world_port=WORLD, start_timeout=10, source_dir="/src")
        fx.dist, fx.logs, fx.procs = root, root / "logs", []
        fx.logs.mkdir()
        fx.creds = runner_module.mysql_client.Credentials("127.0.0.1", "3307", "agacc", "pw")
        fx.names = {k: f"agacc1_{k}" for k in ("auth", "world", "chars", "pb")}
        return fx

    def start(self, snapshot, world_ready=True, starts=(5000, 5100)):
        with tempfile.TemporaryDirectory() as tmp:
            fx = self.fixture(Path(tmp))
            pids = iter([100, 200])
            made = []

            def popen(command, **kwargs):
                proc = FakeProc(next(pids))
                made.append(proc)
                if world_ready and command[0].endswith("worldserver"):
                    (fx.logs / "worldserver_run.out").write_text("ready...\n")
                return proc
            clock = Clock()
            original = runner_module.spawn

            def spawn_with_start(name, command, **kwargs):
                return original(name, command, read_start=lambda pid: dict(zip((100, 200), starts))[pid], **kwargs)
            runner_module.spawn = spawn_with_start
            try:
                try:
                    fx.start(popen=popen, snapshot=snapshot, clock=clock, sleep=clock.sleep)
                    outcome = None
                except SystemExit as error:
                    outcome = str(error)
                fx.stop()
            finally:
                runner_module.spawn = original
            return outcome, made

    def test_owned_start_returns(self):
        outcome, made = self.start(owned_snapshot)
        self.assertIsNone(outcome)
        self.assertTrue(all(p.signals == [signal.SIGINT] for p in made))

    def test_foreign_listener_refuses_and_stops_only_ours(self):
        foreign = {300: {"stat": stat(300, 1, 4000), "fds": ["33"]}}
        outcome, made = self.start(lambda: owned_snapshot(extra_rows=row(WORLD, "33"), extra_procs=foreign))
        self.assertIn("startup refused before any client connected", outcome)
        self.assertEqual(sorted(p.pid for p in made), [100, 200])
        self.assertTrue(all(p.returncode is not None for p in made))

    def test_not_ready_times_out(self):
        outcome, _ = self.start(owned_snapshot, world_ready=False)
        self.assertIn("timed out", outcome)

    def test_pid_reuse_refuses(self):
        outcome, _ = self.start(owned_snapshot, starts=(5000, 1))
        self.assertIn("PID reused", outcome)

    def test_main_connects_no_client_before_start_returns(self):
        source = Path(runner_module.__file__).read_text()
        body = source[source.index("    runner = Runner(fixture)"):]
        self.assertLess(body.index("fixture.start()"), body.index("runner.setup()"))
        start = source[source.index("    def start(self"):source.index("    def stop(self)")]
        self.assertNotIn("client(", start)


if __name__ == "__main__":
    unittest.main()


class DeadlineRaceTest(unittest.TestCase):
    """CS01 Part 42 gaps: the deadline holds across readiness and the snapshot, and roots are re-polled."""

    def run_gate(self, procs, snapshot, ready, timeout=10, interval=2.0):
        clock = Clock()
        slept = []

        def sleep(seconds):
            slept.append(seconds)
            clock.sleep(seconds)
        wrapped_ready = (lambda: ready(clock)) if ready else (lambda: True)
        wait_ready_and_owned(procs, [AUTH, WORLD], wrapped_ready, lambda: snapshot(clock), timeout,
                             clock=clock, sleep=sleep, interval=interval)
        return clock, slept

    def test_ready_exactly_at_deadline_refuses(self):
        def ready(clock):
            clock.now = 10.0  # readiness answered exactly at the deadline
            return True
        with self.assertRaises(StartupRefused) as caught:
            self.run_gate(spawned(), lambda clock: owned_snapshot(), ready)
        self.assertIn("deadline", str(caught.exception))

    def test_no_snapshot_once_readiness_crossed_the_deadline(self):
        taken = []

        def ready(clock):
            clock.now = 10.0
            return True
        with self.assertRaises(StartupRefused):
            self.run_gate(spawned(), lambda clock: taken.append(1) or owned_snapshot(), ready)
        self.assertEqual(taken, [])

    def test_ready_just_before_deadline_passes(self):
        def ready(clock):
            clock.now = 9.5
            return True
        self.run_gate(spawned(), lambda clock: owned_snapshot(), ready)

    def test_slow_snapshot_crossing_deadline_refuses(self):
        def slow(clock):
            clock.now += 11.0
            return owned_snapshot()
        with self.assertRaises(StartupRefused) as caught:
            self.run_gate(spawned(), slow, None)
        self.assertIn("deadline", str(caught.exception))

    def test_snapshot_ending_exactly_at_deadline_refuses(self):
        def slow(clock):
            clock.now = 10.0
            return owned_snapshot()
        with self.assertRaises(StartupRefused):
            self.run_gate(spawned(), slow, None)

    def test_child_exit_during_snapshot_refuses(self):
        world = FakeProc(200)

        def exits_during_scan(clock):
            world.returncode = 1  # the scan still shows its listener, but the root is gone
            return owned_snapshot()
        with self.assertRaises(StartupRefused) as caught:
            self.run_gate(spawned(world=world), exits_during_scan, None)
        self.assertIn("exited early", str(caught.exception))

    def test_sleeps_never_exceed_remaining(self):
        record = []
        clock = Clock()

        def sleep(seconds):
            record.append((clock.now, seconds))
            clock.sleep(seconds)
        with self.assertRaises(StartupRefused):
            wait_ready_and_owned(spawned(), [AUTH, WORLD], lambda: False, owned_snapshot, 5, clock=clock,
                                 sleep=sleep, interval=2.0)
        self.assertEqual([s for _, s in record], [2.0, 2.0, 1.0])
        self.assertTrue(all(now + s <= 5 for now, s in record))


class FinalPollDeadlineTest(unittest.TestCase):
    """CS01 7234065778: the post-snapshot root poll must also finish strictly before the deadline."""

    class SlowPoll(FakeProc):
        def __init__(self, pid, clock, after_polls, to):
            super().__init__(pid)
            self.clock, self.after_polls, self.to = clock, after_polls, to

        def poll(self):
            self.polls += 1
            if self.polls == self.after_polls:
                self.clock.now = self.to  # this poll is the final one after the snapshot
            return self.returncode

    def run_with_final_poll_ending_at(self, when):
        clock = Clock()
        # Polls of the world root: 1 at loop start, 2 the final poll after the snapshot.
        world = self.SlowPoll(200, clock, 2, when)
        wait_ready_and_owned(spawned(world=world), [AUTH, WORLD], lambda: True, owned_snapshot, 10,
                             clock=clock, sleep=clock.sleep)

    def test_final_poll_ending_exactly_at_deadline_refuses(self):
        with self.assertRaises(StartupRefused) as caught:
            self.run_with_final_poll_ending_at(10.0)
        self.assertIn("final process poll", str(caught.exception))

    def test_final_poll_overrunning_deadline_refuses(self):
        with self.assertRaises(StartupRefused):
            self.run_with_final_poll_ending_at(12.5)

    def test_final_poll_just_before_deadline_passes(self):
        self.run_with_final_poll_ending_at(9.999)
