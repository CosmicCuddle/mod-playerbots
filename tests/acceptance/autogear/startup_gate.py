"""Startup gate: no fixture client connects until our own servers are ready and own the fixture ports.

`spawn` starts a server and records its process start time at once. A child that has not been
waited on stays a zombie, so its PID can't be reused before that read. `wait_ready_and_owned`
then polls until both:
- the worldserver log reports ready;
- `port_ownership.ownership_problems` is empty: every listener on both ports, over IPv4 and IPv6,
  belongs to our process tree with the recorded start times.

It refuses when a spawned server exits, or when the deadline passes while either condition fails.
Guarantee: on return, readiness, the snapshot and a later poll of every root all completed strictly
before the deadline, and no root had exited. Not guaranteed: what happens after return. A server can
still exit, or a listener change, between this check and the first client connection. Nothing in
user space can close that window; the client fails then, and the runner stops only our own children.

It never signals anything: cleanup is `stop_owned`, which acts only on the Popen objects we hold.
A foreign listener is reported, never killed.
"""
import signal
import subprocess
import time

from port_ownership import ownership_problems, start_time


class StartupRefused(Exception):
    pass


def spawn(name, command, popen=subprocess.Popen, read_start=start_time, **kwargs):
    proc = popen(command, **kwargs)
    try:
        started = read_start(proc.pid)
    except (OSError, ValueError, IndexError) as error:
        return name, proc, None, f"cannot read start time of {name} pid {proc.pid}: {error}"
    return name, proc, started, None


def wait_ready_and_owned(spawned, ports, is_ready, snapshot, timeout, clock=time.monotonic, sleep=time.sleep,
                         interval=2.0):
    """`spawned` is [(name, proc, start_time, problem)]. Returns once safe; raises StartupRefused."""
    for name, proc, started, problem in spawned:
        if problem or started is None:
            raise StartupRefused(problem or f"{name}: start time unknown")
    roots = {proc.pid: started for _, proc, started, _ in spawned}
    deadline = clock() + timeout

    def check_exits():
        for name, proc, _, _ in spawned:
            if proc.poll() is not None:
                raise StartupRefused(f"{name} exited early rc={proc.returncode}")

    def check_deadline(last):
        # Success must be fully established strictly before the deadline; at or past it, refuse.
        if clock() >= deadline:
            raise StartupRefused(f"startup gate timed out: {'; '.join(last)}")

    last = ["not checked yet"]
    while True:
        check_exits()
        check_deadline(last)
        ready = is_ready()
        check_deadline(["worldserver became ready at or after the deadline"] if ready else ["worldserver not ready"])
        if ready:
            try:
                last = ownership_problems(ports, roots, snapshot())
            except Exception as error:  # noqa: BLE001 - an unreadable snapshot refuses
                last = [f"ownership snapshot failed: {error}"]
            check_deadline(last if last else ["ownership snapshot finished at or after the deadline"])
            # A root may have exited while the snapshot was taken; re-poll before accepting.
            check_exits()
            if not last:
                # The final poll itself can take time: success still has to end before the deadline.
                check_deadline(["final process poll finished at or after the deadline"])
                return
        else:
            last = ["worldserver not ready"]
        remaining = deadline - clock()
        check_deadline(last)
        sleep(min(interval, remaining))


def stop_owned(spawned, grace=120, log=lambda line: None):
    """Stop only the children we spawned, newest first. Nothing is looked up or signalled by PID."""
    for name, proc, _, _ in reversed(spawned):
        if proc.poll() is None:
            proc.send_signal(signal.SIGINT)
            try:
                proc.wait(timeout=grace)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
        log(f"{name} stopped rc={proc.returncode}")
