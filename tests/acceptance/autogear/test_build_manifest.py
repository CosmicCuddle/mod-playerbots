"""Build manifest: exact clean tuple, matching binaries and config templates, revision stamp."""
import hashlib
import json
import tempfile
import unittest
from pathlib import Path

from build_manifest import manifest_problems, revision_stamp_problems

CORE = "d5eee59878bed7991611b0bf6764e73e03885b39"
PB = "f68ed339d8d82627479f7d40754c97dca9b79521"
EXPECTED = {"core": CORE, "mod-playerbots": PB}


class Fixture:
    def __init__(self, root):
        self.dist = root / "dist"
        (self.dist / "bin").mkdir(parents=True)
        (self.dist / "etc" / "modules").mkdir(parents=True)
        for name in ("authserver", "worldserver"):
            (self.dist / "bin" / name).write_text(name)
        (self.dist / "etc" / "worldserver.conf.dist").write_text("a")
        (self.dist / "etc" / "modules" / "playerbots.conf.dist").write_text("b")
        self.path = root / "manifest.json"
        self.data = {"core": CORE, "modules": {"mod-playerbots": PB}, "clean": True,
                     "conf_dir": str(self.dist / "etc"), "binaries": {"authserver": "A", "worldserver": "W"},
                     "configs": {rel: hashlib.sha256((self.dist / "etc" / rel).read_bytes()).hexdigest()
                                 for rel in ("worldserver.conf.dist", "modules/playerbots.conf.dist")}}
        self.stamp = b"AzerothCore rev. " + CORE[:12].encode()

    def problems(self, expected=EXPECTED):
        self.path.write_text(json.dumps(self.data))
        ids = {"authserver": "A", "worldserver": "W"}
        return manifest_problems(self.path, expected, self.dist, lambda p: ids[Path(p).name],
                                 read_bytes=lambda p: self.stamp)


class BuildManifestTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.fx = Fixture(Path(self.tmp.name))

    def tearDown(self):
        self.tmp.cleanup()

    def test_exact_manifest_passes(self):
        self.assertEqual(self.fx.problems(), [])

    def test_tuple_must_equal_expect_tuple_exactly(self):
        self.assertTrue(self.fx.problems(expected={"core": CORE}))
        self.assertTrue(self.fx.problems(expected={**EXPECTED, "mod-individual-progression": PB}))
        self.fx.data["modules"]["mod-playerbots"] = CORE
        self.assertTrue(self.fx.problems())

    def test_bad_sha_or_unclean_refuses(self):
        self.fx.data["core"] = CORE[:12]
        self.assertTrue(self.fx.problems(expected={**EXPECTED, "core": CORE[:12]}))
        self.fx.data["core"] = CORE
        for clean in (False, None, "true"):
            with self.subTest(clean=clean):
                self.fx.data["clean"] = clean
                self.assertTrue(self.fx.problems())

    def test_binaries_must_match(self):
        self.fx.data["binaries"]["worldserver"] = "other"
        self.assertTrue(self.fx.problems())
        del self.fx.data["binaries"]["authserver"]
        self.assertTrue(self.fx.problems())

    def test_conf_dir_must_be_this_install(self):
        self.fx.data["conf_dir"] = "/opt/owner/etc"
        self.assertTrue(self.fx.problems())

    def test_config_templates_bound_exactly(self):
        (self.fx.dist / "etc" / "worldserver.conf.dist").write_text("changed")
        self.assertTrue(self.fx.problems())

    def test_unlisted_or_escaping_config_refuses(self):
        (self.fx.dist / "etc" / "modules" / "extra.conf.dist").write_text("x")
        self.assertTrue(self.fx.problems())
        (self.fx.dist / "etc" / "modules" / "extra.conf.dist").unlink()
        self.fx.data["configs"]["../../escape.conf.dist"] = "0" * 64
        self.assertTrue(self.fx.problems())

    def test_stale_or_missing_revision_stamp_refuses(self):
        self.fx.stamp = b"AzerothCore rev. c779e7b4f735"
        self.assertTrue(self.fx.problems())
        self.fx.stamp = b""
        self.assertTrue(self.fx.problems())

    def test_stamp_alone_never_passes(self):
        self.assertEqual(revision_stamp_problems(CORE[:12].encode(), CORE), [])
        self.fx.path.write_text("{")
        self.assertTrue(manifest_problems(self.fx.path, EXPECTED, self.fx.dist, lambda p: "A",
                                          read_bytes=lambda p: self.fx.stamp))
        self.assertTrue(revision_stamp_problems(b"x", None))

    def test_unreadable_manifest_refuses(self):
        self.assertTrue(manifest_problems(Path(self.tmp.name) / "none.json", EXPECTED, self.fx.dist,
                                          lambda p: "A"))


if __name__ == "__main__":
    unittest.main()
