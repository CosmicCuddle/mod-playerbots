import unittest
from strip_evidence import strip_precondition, strip_outcome_problems

BEFORE = {'rows': [(0, 4, 100, 20), (0, 3, 102, 22), (0, 19, 103, 23)], 'mail': 0}
AFTER = {'rows': [(0, 4, 101, 21), (0, 23, 100, 20), (0, 3, 102, 22), (0, 19, 103, 23)], 'mail': 0}
LEVELS = {20: 80, 21: 60}
RESULT = {'stripped': '1'}


class StripEvidenceTests(unittest.TestCase):
    def check(self, before=BEFORE, after=AFTER, result=RESULT, levels=LEVELS):
        return strip_outcome_problems(before, after, result, levels, 66)

    def test_valid_strip(self):
        self.assertEqual(self.check(), [])

    def test_equipped_bag_destination(self):
        after = {**AFTER, 'rows': [(103, 0, 100, 20) if r[2] == 100 else r for r in AFTER['rows']]}
        self.assertEqual(self.check(after=after), [])

    def test_no_over_cap_precondition(self):
        self.assertTrue(strip_precondition(BEFORE, {20: 66}, 66))

    def test_missing_level(self):
        self.assertTrue(strip_precondition(BEFORE, {}, 66))
        self.assertTrue(self.check(levels={20: 80}))

    def test_unchanged_over_cap_gear(self):
        self.assertTrue(self.check(after=BEFORE, result={'stripped': '0'}))

    def test_old_item_moves_to_bank(self):
        after = {**AFTER, 'rows': [(0, 39, 100, 20) if r[2] == 100 else r for r in AFTER['rows']]}
        self.assertTrue(self.check(after=after))

    def test_old_item_in_unidentified_container(self):
        after = {**AFTER, 'rows': [(999, 0, 100, 20) if r[2] == 100 else r for r in AFTER['rows']]}
        self.assertTrue(self.check(after=after))

    def test_wrong_strip_count(self):
        for result in (None, {'stripped': '0'}, {'stripped': '2'}):
            with self.subTest(result=result):
                self.assertTrue(self.check(result=result))

    def test_existing_owned_over_cap_re_equipped(self):
        before = {**BEFORE, 'rows': BEFORE['rows'] + [(0, 24, 104, 20)]}
        after = {**AFTER, 'rows': AFTER['rows'] + [(0, 5, 104, 20)]}
        self.assertTrue(self.check(before=before, after=after))


if __name__ == '__main__':
    unittest.main()
