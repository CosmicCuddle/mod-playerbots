import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

from preflight import collect_preflight, parse_expected_tuple, tuple_report

CORE = "d5eee59878bed7991611b0bf6764e73e03885b39"
PB = "f68ed339d8d82627479f7d40754c97dca9b79521"
EMPTY = [["information_schema"], ["mysql"], ["performance_schema"], ["sys"]]


def make_fixture(root):
    dist, build, dbc = root / "dist", root / "build", root / "dbc"
    for d in (dist / "bin", dist / "etc" / "modules", build / "src" / "server" / "apps", dbc):
        d.mkdir(parents=True)
    for name in ("authserver", "worldserver"):
        (dist / "bin" / name).write_text(name)
        (build / "src" / "server" / "apps" / name).write_text(name)
    for name in ("authserver.conf.dist", "worldserver.conf.dist"):
        (dist / "etc" / name).write_text("")
    (build / "CMakeCache.txt").write_text(f"CMAKE_INSTALL_PREFIX:PATH={dist}\n")
    (dbc / "Map.dbc").write_bytes(b"WDBC")
    return SimpleNamespace(dist=str(dist), build_dir=str(build), dbc=str(dbc), source_dir=str(root / "core"),
                           prefix="agacc1", auth_port=13724, world_port=18085,
                           expect_tuple=[f"core={CORE}", f"mod-playerbots={PB}"])


def heads(path):
    return (CORE if path.name == "core" else PB), False


def run(args, rows=EMPTY, head=heads):
    return collect_preflight(args, lambda: rows, head=head, read_build_id=lambda p: "id",
                             connect=lambda port: False)


class PreflightTest(unittest.TestCase):
    def test_clean_fixture_passes_and_writes_nothing(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            args = make_fixture(root)
            before = sorted(p.relative_to(root) for p in root.rglob("*"))
            report = run(args)
            self.assertTrue(report["ok"], report)
            self.assertEqual(report["writes"], "none")
            self.assertFalse(report["native_run"])
            self.assertEqual(sorted(p.relative_to(root) for p in root.rglob("*")), before)
            self.assertEqual(report["tuple"]["core"]["observed"], CORE)

    def test_each_failure_is_collected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            args = make_fixture(root)
            (Path(args.dist) / "logs").mkdir()           # used install
            (Path(args.dbc) / "Map.dbc").unlink()         # no DBC
            report = run(args, rows=EMPTY + [["agacc1_auth"]], head=lambda path: (None, None))
            self.assertFalse(report["ok"])
            failed = {name for name, check in report["checks"].items() if not check["ok"]}
            self.assertEqual(failed, {"fresh_install", "fresh_databases", "dbc", "source_tuple"})

    def test_database_inventory_failure_is_a_problem(self):
        with tempfile.TemporaryDirectory() as tmp:
            args = make_fixture(Path(tmp))

            def broken():
                raise OSError("no mysql client")
            report = collect_preflight(args, broken, head=heads, read_build_id=lambda p: "id",
                                       connect=lambda port: False)
            self.assertFalse(report["checks"]["fresh_databases"]["ok"])

    def test_tuple_mismatch_dirty_and_missing(self):
        expected = parse_expected_tuple([f"core={CORE}"])
        _, problems = tuple_report("/x", expected, head=lambda p: (PB, False))
        self.assertTrue(problems)
        _, problems = tuple_report("/x", expected, head=lambda p: (CORE, True))
        self.assertTrue(problems)
        _, problems = tuple_report("/x", expected, head=lambda p: (None, None))
        self.assertTrue(problems)
        _, problems = tuple_report("/x", expected, head=lambda p: (CORE, False))
        self.assertEqual(problems, [])

    def test_tuple_arguments_are_strict(self):
        for bad in (["core=abc"], [f"core={CORE}", f"core={CORE}"], [f"../x={CORE}"], [CORE]):
            with self.assertRaises(ValueError):
                parse_expected_tuple(bad)

    def test_no_expected_tuple_is_not_ok(self):
        with tempfile.TemporaryDirectory() as tmp:
            args = make_fixture(Path(tmp))
            args.expect_tuple = None
            self.assertFalse(run(args)["checks"]["source_tuple"]["ok"])


if __name__ == "__main__":
    unittest.main()
