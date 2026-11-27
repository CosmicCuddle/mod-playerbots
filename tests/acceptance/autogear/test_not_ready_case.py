"""The NotReady case definition: refuses to run without capabilities, and passes only real NotReady."""
import unittest
from pathlib import Path

import not_ready_case as case
from case_outcome import case_outcome_problems
from test_not_ready_oracle import line

STATE = {"rows": [(0, 0, 100, 20)], "mail": 0, "durability": {100: (60, 60)}}
ALL = {name: True for name in case.CAPABILITIES}


class NotReadyCaseTest(unittest.TestCase):
    def test_marked_not_executed_and_not_wired(self):
        self.assertEqual(case.STATUS, "DEFINED / NOT EXECUTED")
        runner = Path(__file__).with_name("run_autogear_acceptance.py").read_text()
        self.assertNotIn("not_ready_case", runner)
        self.assertIn("EXPECTED_CASES = 9", runner)

    def test_every_capability_is_required(self):
        self.assertEqual(case.blockers(ALL), [])
        for name in case.CAPABILITIES:
            for value in (False, None, "yes"):
                with self.subTest(name=name, value=value):
                    self.assertTrue(case.blockers({**ALL, name: value}))

    def test_legacy_fallback_is_listed_unverified(self):
        problems = case.blockers({**ALL, "executor_api": False})
        self.assertTrue(any("legacy fallback, unverified" in p for p in problems))

    def test_expectation_matches_only_not_ready(self):
        self.assertEqual(case_outcome_problems([line()], case.EXPECT), [])
        for other in (line(refusal=0, ran="true", new=1), line(refusal=2), line(refusal=9), line(ran="true")):
            with self.subTest(line=other):
                self.assertTrue(case_outcome_problems([other], case.EXPECT))
        self.assertTrue(case_outcome_problems([line(), line()], case.EXPECT))
        self.assertTrue(case_outcome_problems([], case.EXPECT))

    def test_invariants(self):
        self.assertEqual(case.invariant_problems(STATE, STATE, True, True, ["PB59 AutoGear bot=..."]), [])
        changed = {**STATE, "durability": {100: (59, 60)}}
        cases = [
            (STATE, changed, True, True, []),
            (STATE, {**STATE, "mail": 1}, True, True, []),
            (STATE, {**STATE, "rows": []}, True, True, []),
            (STATE, {"rows": STATE["rows"], "mail": 0}, True, True, []),
            (STATE, STATE, False, True, []),
            (STATE, STATE, True, False, []),
            (STATE, STATE, True, True, ["Player::DurabilityRepair item 100"]),
        ]
        for args in cases:
            with self.subTest(args=args):
                self.assertTrue(case.invariant_problems(*args))


if __name__ == "__main__":
    unittest.main()
