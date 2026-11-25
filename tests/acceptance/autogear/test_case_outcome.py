import unittest
from case_outcome import case_outcome_problems
from test_result_identity import LINE


class CaseOutcomeTests(unittest.TestCase):
    def test_valid_refusal(self):
        self.assertEqual(case_outcome_problems([LINE], {'refusal': 5, 'ran': 0, 'unchanged': True}), [])

    def test_wrong_refusal(self):
        self.assertTrue(case_outcome_problems([LINE], {'refusal': 2}))

    def test_wrong_ran(self):
        self.assertTrue(case_outcome_problems([LINE], {'ran': 1}))

    def test_unchanged_activity(self):
        for field in ('stripped', 'new', 'revoked'):
            with self.subTest(field=field):
                self.assertTrue(case_outcome_problems([LINE.replace(field + '=0', field + '=1')], {'unchanged': True}))
        self.assertTrue(case_outcome_problems([LINE.replace('ran=false', 'ran=true')], {'unchanged': True}))

    def test_gm_authority_false(self):
        gm = LINE.replace('authority=true', 'authority=false').replace('refusal=5', 'refusal=2')
        expect = {'refusal': 2, 'ran': 0, 'unchanged': True, 'authority': False}
        self.assertEqual(case_outcome_problems([gm], expect), [])
        self.assertTrue(case_outcome_problems([gm.replace('authority=false', 'authority=true')], expect))

    def test_malformed_or_missing(self):
        for lines in ([], [LINE, LINE], [LINE + ' extra=1'], ['unrelated']):
            with self.subTest(lines=lines):
                self.assertTrue(case_outcome_problems(lines, {}))

    def test_multiple_allowed_refusals(self):
        self.assertEqual(case_outcome_problems([LINE], {'refusal': (2, 5)}), [])

    def test_invalid_authority_expectation(self):
        self.assertTrue(case_outcome_problems([LINE], {'authority': 'true'}))


if __name__ == '__main__':
    unittest.main()
