"""MySQL client calls for the fixture that never put the password in argv, the environment or a report.

- The password is read from a protected file (`--mysql-password-file`): a regular file, not a symlink,
  owned by this user, with no group or other permissions. A password inside `--mysql` is refused,
  because the runner's own argv is visible to every local user.
- Each call writes a private option file (directory 0700, file 0600) and passes it with
  `--defaults-file`, so no other option file is read. The file is deleted when the call ends.
- `MYSQL_PWD` is removed from the child's environment and never set.
- Every call has a timeout. Failures and timeouts raise `MysqlError` with the return code and the last
  stderr line only, scrubbed of the password. The command line is never reported.
"""
import os
import stat
import subprocess
import tempfile
from contextlib import contextmanager
from pathlib import Path

DEFAULT_TIMEOUT = 600


class MysqlError(OSError):
    pass


class Credentials:
    __slots__ = ("host", "port", "user", "_password")

    def __init__(self, host, port, user, password):
        self.host, self.port, self.user, self._password = host, port, user, password

    @property
    def password(self):
        return self._password

    def __repr__(self):
        return f"Credentials(host={self.host!r}, port={self.port!r}, user={self.user!r}, password=<redacted>)"

    __str__ = __repr__


MAX_PASSWORD_FILE = 4096


def read_password_file(path, after_open=None):
    """Open once, without following a symlink, then check and read that same descriptor.

    Checking a path and then reading it again would let a swapped file through. Here the type,
    owner and mode come from fstat on the open descriptor. The bytes come from it too, and the read is
    bounded, so a file that grows while it is read is still refused.
    O_NONBLOCK keeps a FIFO from blocking the open; a non-regular file is then refused.
    `after_open` is a test hook, called with the path after the open and before the checks.
    """
    flags = os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC | os.O_NONBLOCK
    try:
        fd = os.open(path, flags)
    except OSError as error:
        raise ValueError(f"cannot open the password file: {error.strerror}") from None
    try:
        if after_open:
            after_open(path)
        info = os.fstat(fd)
        if not stat.S_ISREG(info.st_mode):
            raise ValueError("the password file must be a regular file")
        if info.st_uid != os.getuid():
            raise ValueError("the password file must belong to this user")
        if info.st_mode & 0o077:
            raise ValueError("the password file must not be readable by group or others (chmod 600)")
        data = b""
        while len(data) <= MAX_PASSWORD_FILE:
            try:
                chunk = os.read(fd, MAX_PASSWORD_FILE + 1 - len(data))
            except OSError as error:
                raise ValueError(f"cannot read the password file: {error.strerror}") from None
            if not chunk:
                break
            data += chunk
        if len(data) > MAX_PASSWORD_FILE:
            raise ValueError("the password file is too large")
    finally:
        os.close(fd)
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError:
        raise ValueError("the password file is not UTF-8") from None
    password = text[:-1] if text.endswith("\n") else text
    if not password or any(c in password for c in "\n\r\0"):
        raise ValueError("the password file must hold exactly one non-empty line")
    return password


def parse_endpoint(spec, password_file):
    parts = spec.split(";")
    if len(parts) == 4:
        raise ValueError("--mysql must be host;port;user; a password in argv is refused, use --mysql-password-file")
    if len(parts) != 3 or not all(parts):
        raise ValueError("--mysql must be host;port;user")
    if not password_file:
        raise ValueError("--mysql-password-file is required")
    return Credentials(*parts, read_password_file(password_file))


def option_text(creds):
    def quoted(value):
        return '"' + str(value).replace("\\", "\\\\").replace('"', '\\"') + '"'
    return ("[client]\n" f"host={quoted(creds.host)}\n" f"port={quoted(creds.port)}\n"
            f"user={quoted(creds.user)}\n" f"password={quoted(creds.password)}\n" "protocol=TCP\n")


@contextmanager
def option_file(creds):
    directory = tempfile.mkdtemp(prefix="agacc-my-")  # created 0700
    path = Path(directory) / "client.cnf"
    try:
        fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(fd, "w", encoding="utf-8") as handle:
            handle.write(option_text(creds))
        yield path
    finally:
        try:
            path.unlink()
        except FileNotFoundError:
            pass
        os.rmdir(directory)


def scrub(text, creds):
    secret = creds.password if creds else None
    return text.replace(secret, "<redacted>") if secret and text else text


def child_env():
    return {k: v for k, v in os.environ.items() if k != "MYSQL_PWD"}


def run(creds, arguments, stdin=None, timeout=DEFAULT_TIMEOUT, runner=subprocess.run):
    """Run `mysql` with `arguments` (never secrets). Returns stdout; raises MysqlError without argv."""
    with option_file(creds) as path:
        command = ["mysql", f"--defaults-file={path}", *arguments]
        try:
            done = runner(command, stdin=stdin, capture_output=True, text=True, env=child_env(), timeout=timeout)
        except subprocess.TimeoutExpired:
            raise MysqlError(f"mysql timed out after {timeout}s") from None
        except OSError as error:
            raise MysqlError(f"cannot run mysql: {error.strerror}") from None
    if done.returncode:
        last = (done.stderr or "").strip().splitlines()[-1:] or ["no detail"]
        raise MysqlError(f"mysql failed rc={done.returncode}: {scrub(last[0], creds)}")
    return done.stdout


def rows(creds, statement, database=None, **kwargs):
    out = run(creds, ["-N", "-B", "-e", statement] + ([database] if database else []), **kwargs)
    return [line.split("\t") for line in out.splitlines() if line]
