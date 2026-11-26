"""Exercise the F14 write-config gate with disposable temporary directories."""
import os
from pathlib import Path
from tempfile import TemporaryDirectory
from types import SimpleNamespace
import unittest
from unittest.mock import patch

from f14_preflight import KEY, TEMPLATE
from run_autogear_acceptance import Fixture


class F14ConfigPhase(unittest.TestCase):
    def fixture(self, root, sources, stranger=True):
        root = Path(root)
        etc = root / 'etc'
        (etc / 'modules').mkdir(parents=True)
        for name, value in {'worldserver.conf.dist': 'Example = 1\n',
                            'authserver.conf.dist': 'Example = 1\n', **sources}.items():
            (etc / name).write_text(value)
        return SimpleNamespace(dist=root, args=SimpleNamespace(bis_phase=False, stranger_cases=stranger),
                               logs=root / 'logs')

    def refuse(self, sources, env=None):
        with TemporaryDirectory() as root, patch.dict(os.environ, env or {}, clear=True):
            fx = self.fixture(root, sources)
            with self.assertRaisesRegex(SystemExit, 'stranger F14 preflight refused'):
                Fixture.write_configs(fx)
            self.assertEqual(list(fx.dist.rglob('*.conf')), [])
            self.assertFalse(fx.logs.exists())

    def test_explicit_zero_unchanged(self):
        value = f'{KEY} = 0\n'
        with TemporaryDirectory() as root, patch.dict(os.environ, {}, clear=True):
            fx = self.fixture(root, {TEMPLATE: value})
            Fixture.write_configs(fx)
            target = fx.dist / 'etc' / TEMPLATE
            self.assertEqual(target.read_text(), value)
            self.assertIn(value, target.with_suffix('').read_text())
            self.assertTrue(fx.logs.exists())

    def test_missing_or_nonzero_refuses_before_writes(self):
        for sources in ({}, {TEMPLATE: f'# {KEY}=0\n'}, {TEMPLATE: f'{KEY}=1\n'}):
            with self.subTest(sources=sources):
                self.refuse(sources)

    def test_duplicate_across_templates_refuses_before_writes(self):
        for name in ('worldserver.conf.dist', 'authserver.conf.dist', 'modules/other.conf.dist'):
            with self.subTest(name=name):
                self.refuse({TEMPLATE: f'{KEY}=0\n', name: f'{KEY}=0\n'})

    def test_environment_override_refuses_before_writes(self):
        self.refuse({TEMPLATE: f'{KEY}=0\n'}, {'AC_INDIVIDUAL_PROGRESSION_F_14__RANDOM_BOT_SOCIAL_ISOLATION': '0'})

    def test_baseline_without_stranger_does_not_require_ip_template(self):
        with TemporaryDirectory() as root, patch.dict(os.environ, {}, clear=True):
            fx = self.fixture(root, {}, stranger=False)
            Fixture.write_configs(fx)
            self.assertTrue((fx.dist / 'etc/worldserver.conf').exists())


if __name__ == '__main__':
    unittest.main()
