"""Startup gate: no fixture client connects until our own servers are ready and own the fixture ports.

`spawn` starts a server and records its process start time at once. A child that has not been
waited on stays a zombie, so its PID can't be reused before that read. `wait_ready_and_owned`
then polls until both:
- the worldserver log reports ready;
- `port_ownership.ownership_problems` is empty: every listener on both ports, over IPv4 and IPv6,
  belongs to our process tree with the recorded start times.

It refuses when a spawned server exits, or when the deadline passes while either condition fails.
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
    last = ["not checked yet"]
    while True:
        for name, proc, _, _ in spawned:
            if proc.poll() is not None:
                raise StartupRefused(f"{name} exited early rc={proc.returncode}")
        if is_ready():
            try:
                last = ownership_problems(ports, roots, snapshot())
            except Exception as error:  # noqa: BLE001 - an unreadable snapshot refuses
                last = [f"ownership snapshot failed: {error}"]
            if not last:
                return
        else:
            last = ["worldserver not ready"]
        if clock() >= deadline:
            raise StartupRefused(f"startup gate timed out: {'; '.join(last)}")
        sleep(interval)


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
