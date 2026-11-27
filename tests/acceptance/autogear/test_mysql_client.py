"""Secret handling of fixture MySQL calls: never in argv, environment, errors, reports or leftovers."""
import json
import os
import stat
import subprocess
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

import mysql_client
from mysql_client import Credentials, MysqlError, option_text, parse_endpoint, read_password_file, run, scrub

SECRET = 'p#ss"w\\rd;x'


class Recorder:
    """Stands in for subprocess.run; checks what a real mysql child would see."""

    def __init__(self, returncode=0, stdout="", stderr="", raises=None):
        self.result = SimpleNamespace(returncode=returncode, stdout=stdout, stderr=stderr)
        self.raises, self.calls = raises, []

    def __call__(self, command, **kwargs):
        option = Path(command[1].split("=", 1)[1])
        info = os.stat(option)
        self.calls.append({"command": command, "env": kwargs["env"], "timeout": kwargs["timeout"],
                           "mode": stat.S_IMODE(info.st_mode), "dir_mode": stat.S_IMODE(os.stat(option.parent).st_mode),
                           "text": option.read_text(), "path": option})
        if self.raises:
            raise self.raises
        return self.result


def creds():
    return Credentials("127.0.0.1", "3307", "agacc", SECRET)


class MysqlClientTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def password_file(self, text=SECRET + "\n", mode=0o600):
        path = self.dir / "pw"
        path.write_text(text)
        os.chmod(path, mode)
        return path

    def test_password_never_in_argv_or_environment(self):
        os.environ["MYSQL_PWD"] = "inherited"
        try:
            rec = Recorder(stdout="a\tb\n")
            self.assertEqual(mysql_client.rows(creds(), "SELECT 1", runner=rec), [["a", "b"]])
        finally:
            del os.environ["MYSQL_PWD"]
        call = rec.calls[0]
        self.assertNotIn(SECRET, " ".join(call["command"]))
        self.assertTrue(call["command"][1].startswith("--defaults-file="))
        self.assertNotIn("MYSQL_PWD", call["env"])
        self.assertNotIn(SECRET, json.dumps(call["env"]))
        self.assertTrue(all("-p" != a[:2] or a.startswith("--") for a in call["command"][2:]))

    def test_option_file_is_private_and_removed(self):
        rec = Recorder()
        run(creds(), ["-e", "SELECT 1"], runner=rec)
        call = rec.calls[0]
        self.assertEqual(call["mode"], 0o600)
        self.assertEqual(call["dir_mode"], 0o700)
        self.assertFalse(call["path"].exists())
        self.assertFalse(call["path"].parent.exists())

    def test_option_file_removed_on_failure_and_timeout(self):
        for rec in (Recorder(returncode=1, stderr="ERROR 1045: denied"),
                    Recorder(raises=subprocess.TimeoutExpired(["mysql", SECRET], 5)),
                    Recorder(raises=FileNotFoundError(2, "No such file"))):
            with self.subTest(rec=rec.raises or rec.result):
                with self.assertRaises(MysqlError) as caught:
                    run(creds(), ["-e", "x"], runner=rec)
                self.assertNotIn(SECRET, str(caught.exception))
                self.assertIsNone(caught.exception.__cause__)
                self.assertFalse(rec.calls[0]["path"].exists())

    def test_stderr_echoing_the_password_is_scrubbed(self):
        rec = Recorder(returncode=1, stderr=f"warning\nERROR near '{SECRET}'")
        with self.assertRaises(MysqlError) as caught:
            run(creds(), ["-e", "x"], runner=rec)
        self.assertNotIn(SECRET, str(caught.exception))
        self.assertIn("<redacted>", str(caught.exception))

    def test_every_call_has_a_timeout(self):
        rec = Recorder()
        run(creds(), [], runner=rec)
        self.assertEqual(rec.calls[0]["timeout"], mysql_client.DEFAULT_TIMEOUT)

    def test_option_file_quotes_the_password(self):
        text = option_text(creds())
        self.assertIn('password="p#ss\\"w\\\\rd;x"', text)
        self.assertTrue(text.startswith("[client]\n"))

    def test_repr_never_shows_the_password(self):
        self.assertNotIn(SECRET, repr(creds()))
        self.assertNotIn(SECRET, str(creds()))
        self.assertNotIn(SECRET, str(vars(SimpleNamespace(c=creds()))))

    def test_password_in_argv_is_refused(self):
        with self.assertRaises(ValueError) as caught:
            parse_endpoint("127.0.0.1;3307;agacc;hunter2", self.password_file())
        self.assertIn("password in argv is refused", str(caught.exception))
        self.assertNotIn("hunter2", str(caught.exception))

    def test_endpoint_needs_three_fields_and_a_file(self):
        for spec in ("127.0.0.1;3307", "127.0.0.1;;agacc", ""):
            with self.subTest(spec=spec), self.assertRaises(ValueError):
                parse_endpoint(spec, self.password_file())
        with self.assertRaises(ValueError):
            parse_endpoint("127.0.0.1;3307;agacc", None)
        self.assertEqual(parse_endpoint("127.0.0.1;3307;agacc", self.password_file()).password, SECRET)

    def test_password_file_must_be_protected(self):
        with self.assertRaises(ValueError):
            read_password_file(self.password_file(mode=0o640))
        link = self.dir / "link"
        link.symlink_to(self.password_file())
        with self.assertRaises(ValueError):
            read_password_file(link)
        with self.assertRaises(ValueError):
            read_password_file(self.dir)
        for text in ("", "\n", "a\nb\n"):
            with self.subTest(text=text), self.assertRaises(ValueError):
                read_password_file(self.password_file(text=text))
        with self.assertRaises(ValueError) as caught:
            read_password_file(self.dir / "missing")
        self.assertNotIn(SECRET, str(caught.exception))

    def test_scrub(self):
        self.assertEqual(scrub(f"x {SECRET} y", creds()), "x <redacted> y")
        self.assertEqual(scrub("", creds()), "")
        self.assertEqual(scrub("x", None), "x")


class RunnerSourceTest(unittest.TestCase):
    """The runner itself: no -p argument, no MYSQL_PWD, no password-bearing --mysql."""

    def test_runner_never_builds_a_password_argument(self):
        source = (Path(__file__).with_name("run_autogear_acceptance.py")).read_text()
        self.assertNotIn('f"-p{', source)
        self.assertNotIn("MYSQL_PWD", source)
        self.assertNotIn("--no-defaults", source)
        self.assertIn('"--mysql-password-file", required=True', source)


if __name__ == "__main__":
    unittest.main()


class PasswordFileRaceTest(unittest.TestCase):
    """CS01 Part 42: one open, no symlink follow, checks and read on the same descriptor."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)
        self.path = self.dir / "pw"
        self.path.write_text("fictional-original\n")
        os.chmod(self.path, 0o600)

    def tearDown(self):
        self.tmp.cleanup()

    def test_replacement_after_open_reads_the_opened_file(self):
        decoy = self.dir / "decoy"
        decoy.write_text("fictional-replacement\n")
        os.chmod(decoy, 0o600)
        self.assertEqual(read_password_file(self.path, after_open=lambda p: os.replace(decoy, p)),
                         "fictional-original")

    def test_symlink_swapped_in_after_open_is_not_followed(self):
        target = self.dir / "target"
        target.write_text("fictional-target\n")
        os.chmod(target, 0o600)

        def swap(p):
            os.unlink(p)
            os.symlink(target, p)
        self.assertEqual(read_password_file(self.path, after_open=swap), "fictional-original")

    def test_symlink_at_open_refuses(self):
        link = self.dir / "link"
        link.symlink_to(self.path)
        with self.assertRaises(ValueError) as caught:
            read_password_file(link)
        self.assertNotIn("fictional", str(caught.exception))

    def test_fifo_refuses_without_blocking(self):
        fifo = self.dir / "fifo"
        os.mkfifo(fifo, 0o600)
        with self.assertRaises(ValueError):
            read_password_file(fifo)

    def test_permissions_checked_on_the_descriptor(self):
        self.assertRaises(ValueError, read_password_file, self.path, after_open=lambda p: os.chmod(p, 0o644))

    def test_size_bound_and_invalid_data(self):
        for data in (b"x" * (mysql_client.MAX_PASSWORD_FILE + 1), b"\xff\xfe\n", b"a\0b\n", b"a\rb\n", b"a\nb\n",
                     b"", b"\n"):
            with self.subTest(data=data[:8]):
                self.path.write_bytes(data)
                with self.assertRaises(ValueError) as caught:
                    read_password_file(self.path)
                self.assertNotIn("\\x", str(caught.exception))

    def test_exact_bound_is_accepted(self):
        self.path.write_bytes(b"y" * (mysql_client.MAX_PASSWORD_FILE - 1) + b"\n")
        self.assertEqual(len(read_password_file(self.path)), mysql_client.MAX_PASSWORD_FILE - 1)

    def test_open_uses_nofollow_and_cloexec(self):
        source = Path(mysql_client.__file__).read_text()
        for flag in ("os.O_NOFOLLOW", "os.O_CLOEXEC", "os.O_NONBLOCK", "os.fstat(fd)", "os.read(fd"):
            self.assertIn(flag, source)
        self.assertNotIn("os.lstat(", source)
        self.assertNotIn("read_text(", source)
