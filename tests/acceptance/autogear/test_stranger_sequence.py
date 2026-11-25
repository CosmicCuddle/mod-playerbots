"""Optional stranger cases: dispatcher order, stop rules and default-off behaviour (all I/O mocked)."""
import unittest
from types import SimpleNamespace
from unittest.mock import Mock

from run_autogear_acceptance import Runner

BASE = ['A1', 'A2', 'A3', 'B1', 'B2', 'R1', 'R2', 'R3', 'G1']
WITH = ['A1', 'A2', 'A3', 'B1', 'B2', 'S1', 'S1c', 'S2', 'S2c', 'R1', 'R2', 'R3', 'G1']


class StrangerSequenceTests(unittest.TestCase):
    def fixture(self, fail=None, stranger=True, preflight=()):
        r = SimpleNamespace(alt=object(), random=object(), cmd=object(), ops=object(), calls=[], results=[])
        if stranger:
            r.stranger = object()

        def run(name, bot, command, expected, prepare=None):
            case = name.split()[0]
            r.calls.append((case, bot, command))
            return {'pass': case != fail}

        def run_stranger(name, bot, population):
            case = name.split()[0]
            r.calls.append((case, bot, population))
            return {'pass': case != fail}

        r.run_case = Mock(side_effect=run)
        r.run_stranger_case = Mock(side_effect=run_stranger)
        r.stranger_preflight = Mock(return_value=list(preflight))
        r.fill_bags = Mock(return_value=Mock())
        r.stranger_cases = lambda alt, rnd: Runner.stranger_cases(r, alt, rnd)
        return r

    def test_default_off_keeps_nine(self):
        r = self.fixture(stranger=False)
        Runner.cases(r)
        self.assertEqual([c[0] for c in r.calls], BASE)
        r.run_stranger_case.assert_not_called()

    def test_order_targets_and_controls(self):
        r = self.fixture()
        Runner.cases(r)
        self.assertEqual([c[0] for c in r.calls], WITH)
        calls = {c[0]: c for c in r.calls}
        self.assertEqual(calls['S1'][1:], (r.random, 'random'))
        self.assertEqual(calls['S1c'][1:], (r.random, 'autogear banana'))
        self.assertEqual(calls['S2'][1:], (r.alt, 'alt'))
        self.assertEqual(calls['S2c'][1:], (r.alt, 'autogear'))

    def test_stop_after_any_stranger_failure_before_gear_changes(self):
        for i, case in enumerate(['S1', 'S1c', 'S2', 'S2c']):
            with self.subTest(case=case):
                r = self.fixture(fail=case)
                Runner.cases(r)
                self.assertEqual([c[0] for c in r.calls], WITH[:5 + i + 1])

    def test_preflight_failure_sends_nothing(self):
        r = self.fixture(preflight=['stranger is a group member'])
        Runner.cases(r)
        self.assertEqual([c[0] for c in r.calls], BASE[:5])
        self.assertEqual(r.results[-1]['case'], 'S0 stranger preflight')


class StrangerAccountTests(unittest.TestCase):
    def test_stranger_account_only_when_enabled(self):
        from run_autogear_acceptance import Fixture
        for enabled in (False, True):
            with self.subTest(enabled=enabled):
                fx = SimpleNamespace(names={'auth': 'fixture_auth'}, args=SimpleNamespace(world_port=1, stranger_cases=enabled))
                fx.sql = Mock(side_effect=lambda db, statement: [['9']] if statement.startswith('SELECT id FROM account WHERE username=') else [])
                Fixture.accounts(fx)
                statements = [c.args[1] for c in fx.sql.call_args_list]
                self.assertEqual(any("'AGSTR'" in s and s.startswith('INSERT INTO account ') for s in statements), enabled)
                self.assertFalse(any('AGSTR' in s and 'account_access' in s for s in statements))
                self.assertFalse(any(s.startswith(('DELETE', 'REPLACE', 'DROP')) for s in statements))


if __name__ == '__main__':
    unittest.main()
