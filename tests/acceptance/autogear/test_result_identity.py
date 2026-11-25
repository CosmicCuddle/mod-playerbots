import unittest
from result_identity import identity_problems

LINE = ('PB59 AutoGear bot=GUID Full: 0x0000000000000007 Type: Player Low: 7 '
        'sender=GUID Full: 0x0000000000000002 Type: Player Low: 2 raw=0 effective=0 '
        'authority=true cap=66 form=1 refusal=5 ran=false stripped=0 new=0 revoked=0 '
        'capHeld=false bisRestoreFailed=false ownedMissing=0')


class ResultIdentityTests(unittest.TestCase):
    def check(self, lines=None, **kwargs):
        return identity_problems([LINE] if lines is None else lines, 7, 2, **kwargs)

    def test_valid(self):
        self.assertEqual(self.check(form=1), [])

    def test_wrong_bot_or_sender(self):
        for old, new in (('0007 Type', '0008 Type'), ('Low: 7', 'Low: 8'),
                         ('0002 Type', '0003 Type'), ('Low: 2', 'Low: 3')):
            with self.subTest(old=old):
                self.assertTrue(self.check([LINE.replace(old, new)]))

    def test_authority(self):
        line = LINE.replace('authority=true', 'authority=false')
        self.assertTrue(self.check([line]))
        self.assertEqual(self.check([line], require_authority=False), [])

    def test_form(self):
        self.assertTrue(self.check(form=0))
        self.assertEqual(self.check(form=None), [])
        self.assertTrue(self.check([LINE.replace('form=1', 'form=9')]))

    def test_stages(self):
        for old, new in (('raw=0', 'raw=19'), ('effective=0', 'effective=1')):
            self.assertTrue(self.check([LINE.replace(old, new)]))

    def test_missing_duplicate_and_warning(self):
        for lines in ([], [LINE, LINE], [LINE, 'PB59 AutoGear bot=... needs recovery']):
            self.assertTrue(self.check(lines))

    def test_strict_format(self):
        for line in (LINE + ' extra=1', LINE.replace('Low: 2 ', ''),
                     LINE.replace('raw=0', 'raw=00'), LINE.replace('ran=false', 'ran=maybe')):
            self.assertTrue(self.check([line]))


if __name__ == '__main__':
    unittest.main()
