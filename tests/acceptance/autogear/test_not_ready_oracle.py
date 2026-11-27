"""NotReady (refusal 10, finding A1) in the fixture oracle: accepted only as a no-change refusal.

Refusal 10 is the only new code. Anything above 10 stays malformed, and a NotReady line can never pass as
a success or as a case expecting a different outcome. Identity, cap and duplicate-line safeguards are the
existing ones and are exercised unchanged.
"""
import unittest

from acceptance_oracle import evidence_problems, parse_result, pre_save_problems
from case_outcome import case_outcome_problems


def line(refusal=10, ran="false", stripped=0, new=0, revoked=0, cap=78, cap_held="false"):
    guid = "GUID Full: 0x{:016x} Type: Player Low: {}"
    return (f"PB59 AutoGear bot={guid.format(42, 42)} sender={guid.format(21, 21)} raw=1 effective=1 "
            f"authority=true cap={cap} form=0 refusal={refusal} ran={ran} stripped={stripped} new={new} "
            f"revoked={revoked} capHeld={cap_held} bisRestoreFailed=false ownedMissing=0")


UNCHANGED = {"rows": [(0, 0, 100, 20)], "mail": 0}


class NotReadyOracleTest(unittest.TestCase):
    def test_not_ready_parses(self):
        self.assertEqual(parse_result(line())["refusal"], "10")

    def test_unknown_codes_above_ten_are_malformed(self):
        for code in (11, 12, 99, 4294967295):
            with self.subTest(code=code):
                self.assertIsNone(parse_result(line(refusal=code)))

    def test_not_ready_with_no_change_is_valid_refusal_evidence(self):
        self.assertEqual(evidence_problems(UNCHANGED, UNCHANGED, parse_result(line()), {}, expected_cap=78), [])

    def test_not_ready_reporting_activity_is_refused(self):
        for forged in (line(ran="true"), line(new=1), line(stripped=1), line(revoked=1)):
            with self.subTest(line=forged):
                self.assertTrue(evidence_problems(UNCHANGED, UNCHANGED, parse_result(forged), {}, expected_cap=78))

    def test_not_ready_never_satisfies_a_success_case(self):
        self.assertTrue(case_outcome_problems([line()], {"refusal": 0, "ran": 1}))

    def test_not_ready_satisfies_only_an_expected_not_ready_case(self):
        self.assertEqual(case_outcome_problems([line()], {"refusal": 10, "ran": 0, "unchanged": True}), [])
        self.assertTrue(case_outcome_problems([line()], {"refusal": 2, "ran": 0}))

    def test_existing_safeguards_still_apply(self):
        self.assertTrue(case_outcome_problems([line(), line()], {"refusal": 10}))
        self.assertTrue(pre_save_problems([line(), line()], expected_cap=78))
        self.assertTrue(evidence_problems(UNCHANGED, {"rows": [], "mail": 0}, parse_result(line()), {},
                                          expected_cap=78))


if __name__ == "__main__":
    unittest.main()
