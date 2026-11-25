import unittest
from acceptance_oracle import pre_save_problems

LINE = ('PB59 AutoGear bot=fixture cap=60 refusal=0 ran=true stripped=1 new=1 '
        'revoked=0 capHeld=true bisRestoreFailed=false ownedMissing=0')


class PreSaveTests(unittest.TestCase):
    def test_valid_success(self):
        self.assertEqual(pre_save_problems([LINE], 60), [])

    def test_valid_refusal(self):
        line = LINE.replace('refusal=0', 'refusal=4').replace('ran=true', 'ran=false')
        self.assertEqual(pre_save_problems([line], 60), [])

    def test_missing_or_duplicate_log(self):
        for lines in ([], [LINE, LINE], [LINE, 'malformed']):
            with self.subTest(lines=lines):
                self.assertTrue(pre_save_problems(lines, 60))

    def test_malformed_log(self):
        self.assertTrue(pre_save_problems(['malformed'], 60))
        self.assertTrue(pre_save_problems([LINE.replace('refusal=0', 'refusal=09')], 60))

    def test_uncertain_results(self):
        for old, new in (('refusal=0', 'refusal=9'), ('ownedMissing=0', 'ownedMissing=1'),
                         ('bisRestoreFailed=false', 'bisRestoreFailed=true'),
                         ('capHeld=true', 'capHeld=false'), ('cap=60', 'cap=78'),
                         ('ran=true', 'ran=false')):
            with self.subTest(field=old):
                self.assertTrue(pre_save_problems([LINE.replace(old, new)], 60))


if __name__ == '__main__':
    unittest.main()
