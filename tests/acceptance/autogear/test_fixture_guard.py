import tempfile
import unittest
from pathlib import Path
from fixture_guard import fresh_install, fresh_database_names


class FixtureGuardTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name) / 'install'
        for name in ('bin', 'etc/modules'):
            (self.root / name).mkdir(parents=True)
        for name in ('bin/authserver', 'bin/worldserver', 'etc/authserver.conf.dist',
                     'etc/worldserver.conf.dist'):
            (self.root / name).touch()

    def test_fresh(self):
        self.assertEqual(fresh_install(self.root), self.root)

    def test_existing_configs(self):
        for name in ('worldserver.conf', 'authserver.conf', 'modules/playerbots.conf'):
            with self.subTest(name=name):
                p = self.root / 'etc' / name
                p.write_text('do not overwrite')
                with self.assertRaises(ValueError):
                    fresh_install(self.root)
                self.assertEqual(p.read_text(), 'do not overwrite')
                p.unlink()

    def test_existing_outputs(self):
        for name in ('data', 'logs'):
            with self.subTest(name=name):
                p = self.root / name
                p.mkdir()
                with self.assertRaises(ValueError):
                    fresh_install(self.root)
                p.rmdir()

    def test_config_symlink(self):
        (self.root / 'etc/modules/evil.conf.dist').symlink_to(self.root / 'etc/worldserver.conf.dist')
        with self.assertRaises(ValueError):
            fresh_install(self.root)

    def test_output_dangling_symlink(self):
        (self.root / 'logs').symlink_to(self.root / 'missing')
        with self.assertRaises(ValueError):
            fresh_install(self.root)

    def test_root_symlink(self):
        alias = self.root.parent / 'alias'
        alias.symlink_to(self.root, target_is_directory=True)
        with self.assertRaises(ValueError):
            fresh_install(alias)

    def test_missing_binary(self):
        (self.root / 'bin/worldserver').unlink()
        with self.assertRaises(ValueError):
            fresh_install(self.root)

    def test_database_fresh(self):
        self.assertEqual(fresh_database_names('agacc2', [['information_schema'], ['mysql']])['pb'], 'agacc2_pb')

    def test_database_existing_or_unrelated(self):
        for name in ('agacc2_chars', 'AGACC2_AUTH', 'acore_world', 'test'):
            with self.subTest(name=name), self.assertRaises(ValueError):
                fresh_database_names('agacc2', [['information_schema'], [name]])

    def test_database_bad_observation(self):
        for rows in ([], [['mysql']], [['information_schema', 'extra']], [[None]]):
            with self.subTest(rows=rows), self.assertRaises(ValueError):
                fresh_database_names('agacc2', rows)

    def test_database_prefix(self):
        for prefix in ('agacc;DROP', 'acore', 'agacc\n', 'agacc' + 'x' * 25):
            with self.subTest(prefix=prefix), self.assertRaises(ValueError):
                fresh_database_names(prefix, [['information_schema']])


if __name__ == '__main__':
    unittest.main()
