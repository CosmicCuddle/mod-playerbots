import unittest
from types import SimpleNamespace
from unittest.mock import Mock

from bis_phase import BIS_CASES, fixture_overrides, run_bis_phase


class BisPhaseTests(unittest.TestCase):
    def test_default_unchanged(self):
        base = {'Playerbots.AutoGearBisCommand': '0', 'other': 'x'}
        out = fixture_overrides(base, False)
        self.assertEqual(out, base)
        self.assertIsNot(out, base)

    def test_enabled_isolated_override(self):
        base = {'Playerbots.AutoGearBisCommand': '0', 'other': 'x'}
        out = fixture_overrides(base, True)
        self.assertEqual(out, {'Playerbots.AutoGearBisCommand': '1',
                               'Playerbots.AutoGearQualityLimit': '4', 'other': 'x'})
        self.assertEqual(base, {'Playerbots.AutoGearBisCommand': '0', 'other': 'x'})

    def test_invalid_mode(self):
        for value in (1, 'true', None):
            with self.subTest(value=value), self.assertRaises(ValueError):
                fixture_overrides({}, value)

    def test_both_cases_and_exact_expectations(self):
        r = SimpleNamespace(alt={'guid': 1}, random={'guid': 2},
                            run_case=Mock(return_value={'pass': True}))
        run_bis_phase(r)
        calls = [c.args for c in r.run_case.call_args_list]
        self.assertEqual(len(calls), BIS_CASES)
        self.assertEqual([c[1] for c in calls], [r.alt, r.random])
        self.assertEqual([c[2] for c in calls], ['autogear bis', 'autogear bis'])
        self.assertEqual(calls[0][3], {'refusal': 1, 'ran': 0, 'unchanged': True,
                                       'authority': True, 'form': 0})
        self.assertEqual(calls[1][3], {'refusal': 0, 'ran': 1, 'authority': True,
                                       'form': 5, 'reply': r'^Geared up to item level \d+'})

    def test_failed_alt_stops(self):
        r = SimpleNamespace(alt={'guid': 1}, random={'guid': 2},
                            run_case=Mock(return_value={'pass': False}))
        run_bis_phase(r)
        self.assertEqual(r.run_case.call_count, 1)

    def test_missing_either_population(self):
        for alt, rnd in ((None, {'guid': 2}), ({'guid': 1}, None), (None, None)):
            r = SimpleNamespace(alt=alt, random=rnd, run_case=Mock())
            run_bis_phase(r)
            r.run_case.assert_not_called()

    def test_positive_failure_not_retried(self):
        r = SimpleNamespace(alt={'guid': 1}, random={'guid': 2},
                            run_case=Mock(side_effect=[{'pass': True}, {'pass': False}]))
        run_bis_phase(r)
        self.assertEqual(r.run_case.call_count, BIS_CASES)


if __name__ == '__main__':
    unittest.main()
