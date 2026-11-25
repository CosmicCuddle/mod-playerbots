"""Exercise fixture config preparation with private temporary directories only."""
from pathlib import Path
from tempfile import TemporaryDirectory
from types import SimpleNamespace
import unittest

from run_autogear_acceptance import Fixture, PLAYERBOTS_OVERRIDES


TEMPLATE = 'Playerbots.AutoGearBisCommand = 0\nPlayerbots.AutoGearQualityLimit = 3\n'


class BisConfigPhaseTests(unittest.TestCase):
    def fixture(self, root, enabled, sources):
        etc = Path(root) / 'etc'
        modules = etc / 'modules'
        modules.mkdir(parents=True)
        for name in ('worldserver', 'authserver'):
            (etc / (name + '.conf.dist')).write_text('Example = 1\n')
        for name, value in sources.items():
            (modules / name).write_text(value)
        return SimpleNamespace(dist=Path(root), args=SimpleNamespace(bis_phase=enabled),
                               logs=Path(root) / 'logs')

    def test_enabled_config_is_separate_and_default_unchanged(self):
        before = dict(PLAYERBOTS_OVERRIDES)
        for enabled in (False, True):
            with self.subTest(enabled=enabled), TemporaryDirectory() as root:
                fx = self.fixture(root, enabled, {'playerbots.conf.dist': TEMPLATE})
                Fixture.write_configs(fx)
                p = fx.dist / 'etc/modules/playerbots.conf'
                value = p.read_text()
                self.assertIn('Playerbots.AutoGearBisCommand = ' + ('1' if enabled else '0') + '\n', value)
                self.assertIn('Playerbots.AutoGearQualityLimit = ' + ('4' if enabled else '3') + '\n', value)
                self.assertIn('Playerbots.AutoGearCommandAltBots = 1\n', value)
                self.assertEqual(p.with_suffix('.conf.dist').read_text(), TEMPLATE)
                self.assertEqual(PLAYERBOTS_OVERRIDES, before)

    def test_missing_or_ambiguous_template_refuses_before_writes(self):
        for sources in ({}, {'playerbots.conf.dist': TEMPLATE, 'playerbots2.conf.dist': TEMPLATE}):
            with self.subTest(sources=sources), TemporaryDirectory() as root:
                fx = self.fixture(root, True, sources)
                with self.assertRaisesRegex(SystemExit, 'exactly one'):
                    Fixture.write_configs(fx)
                self.assertFalse(list(fx.dist.rglob('*.conf')))
                self.assertFalse(fx.logs.exists())

    def test_missing_or_duplicate_key_refuses_before_writes(self):
        for value in ('Playerbots.AutoGearBisCommand = 0\n', TEMPLATE + TEMPLATE):
            with self.subTest(value=value), TemporaryDirectory() as root:
                fx = self.fixture(root, True, {'playerbots.conf.dist': value})
                with self.assertRaisesRegex(SystemExit, 'unique known config keys'):
                    Fixture.write_configs(fx)
                self.assertFalse(list(fx.dist.rglob('*.conf')))
                self.assertFalse(fx.logs.exists())


if __name__ == '__main__':
    unittest.main()
