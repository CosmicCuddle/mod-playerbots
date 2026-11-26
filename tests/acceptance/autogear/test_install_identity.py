import tempfile
import unittest
from pathlib import Path

from install_identity import compiled_conf_dir, install_identity_problems, port_problems


def make_tree(root, prefix, conf_dir=None):
    dist, build = root / "dist", root / "build"
    for d in (dist / "bin", dist / "etc", build / "src" / "server" / "apps"):
        d.mkdir(parents=True)
    for name in ("authserver", "worldserver"):
        (dist / "bin" / name).write_text(f"installed {name}")
        (build / "src" / "server" / "apps" / name).write_text(f"built {name}")
    cache = f"CMAKE_INSTALL_PREFIX:PATH={prefix}\n"
    if conf_dir:
        cache += f"CONF_DIR:PATH={conf_dir}\n"
    (build / "CMakeCache.txt").write_text(cache)
    return dist, build


def same_id(path):
    return "abc123"


class InstallIdentityTest(unittest.TestCase):
    def test_conf_dir_defaults_to_prefix_etc(self):
        self.assertEqual(compiled_conf_dir("CMAKE_INSTALL_PREFIX:PATH=/x/dist/\n"), "/x/dist/etc")
        self.assertEqual(compiled_conf_dir("CMAKE_INSTALL_PREFIX:PATH=/x\nCONF_DIR:PATH=/y/etc\n"), "/y/etc")

    def test_missing_or_duplicate_cache_entries_refuse(self):
        with self.assertRaises(ValueError):
            compiled_conf_dir("OTHER:BOOL=ON\n")
        with self.assertRaises(ValueError):
            compiled_conf_dir("CONF_DIR:PATH=/a\nCONF_DIR:PATH=/b\n")

    def test_bound_install_passes(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            dist, build = make_tree(root, root / "dist")
            self.assertEqual(install_identity_problems(dist, build, read_build_id=same_id), [])

    def test_other_prefix_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            dist, build = make_tree(root, root / "live-server")
            problems = install_identity_problems(dist, build, read_build_id=same_id)
            self.assertTrue(any("module configs" in p for p in problems))

    def test_explicit_conf_dir_elsewhere_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            dist, build = make_tree(root, root / "dist", conf_dir=root / "elsewhere" / "etc")
            self.assertTrue(install_identity_problems(dist, build, read_build_id=same_id))

    def test_different_binary_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            dist, build = make_tree(root, root / "dist")
            ids = lambda path: "built" if "build" in path.parts else "other"  # noqa: E731
            problems = install_identity_problems(dist, build, read_build_id=ids)
            self.assertEqual(len(problems), 2)

    def test_unreadable_build_id_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            dist, build = make_tree(root, root / "dist")

            def broken(path):
                raise ValueError("no note")
            self.assertEqual(len(install_identity_problems(dist, build, read_build_id=broken)), 2)

    def test_missing_cache_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            dist, build = make_tree(root, root / "dist")
            (build / "CMakeCache.txt").unlink()
            self.assertTrue(install_identity_problems(dist, build, read_build_id=same_id))

    def test_ports(self):
        self.assertEqual(port_problems([13724, 18085], connect=lambda port: False), [])
        self.assertTrue(port_problems([13724, 13724], connect=lambda port: False))
        self.assertTrue(port_problems([80, 18085], connect=lambda port: False))
        self.assertTrue(port_problems([13724, 18085], connect=lambda port: port == 18085))


if __name__ == "__main__":
    unittest.main()
