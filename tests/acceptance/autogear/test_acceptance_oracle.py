import unittest
from acceptance_oracle import parse_result, evidence_problems, inventory_index

LINE = ('PB59 AutoGear bot=fixture cap=60 refusal=0 ran=true stripped=1 new=1 '
        'revoked=0 capHeld=true bisRestoreFailed=false ownedMissing=0')
BEFORE = {'rows': [(0, 0, 100, 20)], 'mail': 0}
AFTER = {'rows': [(0, 23, 100, 20), (0, 0, 101, 21)], 'mail': 0}


class OracleTests(unittest.TestCase):
    def check(self, line=LINE, before=BEFORE, after=AFTER, levels=None):
        return evidence_problems(before, after, parse_result(line), {21: 60} if levels is None else levels)

    def test_success(self):
        self.assertEqual(self.check(), [])

    def test_numeric_bools(self):
        self.assertEqual(self.check(LINE.replace('true', '1').replace('false', '0')), [])

    def test_missing_fields(self):
        for field in LINE.split()[3:]:
            with self.subTest(field=field):
                self.assertIsNone(parse_result(LINE.replace(' ' + field, '')))

    def test_duplicate_fields(self):
        self.assertIsNone(parse_result(LINE + ' cap=61'))

    def test_bad_bool(self):
        self.assertIsNone(parse_result(LINE.replace('ran=true', 'ran=maybe')))

    def test_invalid_numbers(self):
        for value in ('-1', '99999999999', '4294967296', 'NaN'):
            with self.subTest(value=value):
                self.assertIsNone(parse_result(LINE.replace('cap=60', 'cap=' + value)))

    def test_unknown_refusal(self):
        self.assertIsNone(parse_result(LINE.replace('refusal=0', 'refusal=10')))

    def test_zero_cap(self):
        self.assertTrue(self.check(LINE.replace('cap=60', 'cap=0')))

    def test_missing_level(self):
        self.assertTrue(self.check(levels={}))

    def test_invalid_level(self):
        for level in (None, 0, -1, True, '60'):
            with self.subTest(level=level):
                self.assertTrue(self.check(levels={21: level}))

    def test_over_cap(self):
        self.assertTrue(self.check(levels={21: 61}))

    def test_lost_item(self):
        self.assertTrue(self.check(after={'rows': [(0, 0, 101, 21)], 'mail': 0}))

    def test_changed_entry(self):
        self.assertTrue(self.check(after={'rows': [(0, 23, 100, 99), (0, 0, 101, 21)], 'mail': 0}))

    def test_duplicate_guid(self):
        self.assertTrue(self.check(after={'rows': [(0, 23, 100, 20), (0, 0, 100, 20)], 'mail': 0}))

    def test_duplicate_slot(self):
        self.assertTrue(self.check(after={'rows': [(0, 0, 100, 20), (0, 0, 101, 21)], 'mail': 0}))

    def test_missing_rows(self):
        self.assertTrue(self.check(after={'mail': 0}))

    def test_recovery(self):
        self.assertTrue(self.check(LINE.replace('refusal=0', 'refusal=9')))

    def test_restore_failure(self):
        self.assertTrue(self.check(LINE.replace('bisRestoreFailed=false', 'bisRestoreFailed=true')))

    def test_missing_owned(self):
        self.assertTrue(self.check(LINE.replace('ownedMissing=0', 'ownedMissing=1')))

    def test_not_run_success(self):
        self.assertTrue(self.check(LINE.replace('ran=true', 'ran=false')))

    def test_valid_refusal(self):
        line = LINE.replace('refusal=0', 'refusal=4').replace('ran=true', 'ran=false').replace('cap=60', 'cap=0')
        self.assertEqual(self.check(line, after=BEFORE), [])

    def test_empty_inventory_valid(self):
        self.assertEqual(inventory_index({'rows': [], 'mail': 0}), {})


if __name__ == '__main__':
    unittest.main()
