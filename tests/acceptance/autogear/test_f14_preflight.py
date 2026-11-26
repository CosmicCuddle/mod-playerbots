import unittest
from f14_preflight import KEY, TEMPLATE, f14_preflight_problems


class F14Preflight(unittest.TestCase):
    def setUp(self):
        self.sources = {TEMPLATE: f"# {KEY}\n{KEY} = 0\n{KEY}.Reply = test\n", "worldserver.conf.dist": "[worldserver]\nOther = 1\n"}

    def test_exact_zero(self):
        self.assertEqual(f14_preflight_problems(self.sources, {}), [])

    def test_missing_template_or_key(self):
        for value in ({}, {TEMPLATE: f"# {KEY} = 0\n"}, {TEMPLATE: "Other = 0"}):
            with self.subTest(value=value):
                self.assertTrue(f14_preflight_problems(value, {}))

    def test_noncanonical_or_enabled(self):
        for value in ('1', 'true', 'false', '"0"', '0 # comment', '', '00', 'garbage'):
            with self.subTest(value=value):
                self.assertTrue(f14_preflight_problems({TEMPLATE: f"{KEY} = {value}"}, {}))

    def test_duplicate_even_identical(self):
        for value in ('0', '1'):
            with self.subTest(value=value):
                self.assertTrue(f14_preflight_problems({TEMPLATE: f"{KEY}=0\n{KEY}={value}"}, {}))
                self.assertTrue(f14_preflight_problems({**self.sources, 'other.conf.dist': f"{KEY}={value}"}, {}))

    def test_wrong_source_and_malformed_assignment(self):
        self.assertTrue(f14_preflight_problems({TEMPLATE: "Other=0", 'worldserver.conf.dist': f"{KEY}=0"}, {}))
        self.assertTrue(f14_preflight_problems({TEMPLATE: f"{KEY}=0\n{KEY}"}, {}))

    def test_whitespace_and_unrelated_environment(self):
        self.assertEqual(f14_preflight_problems({TEMPLATE: f"  {KEY}   =   0  \n"}, {'AC_DATA_DIR': 'fictional'}), [])

    def test_environment_override_refused_even_zero(self):
        for value in ('0', '1', ''):
            with self.subTest(value=value):
                self.assertTrue(f14_preflight_problems(self.sources, {'AC_INDIVIDUAL_PROGRESSION_F_14__RANDOM_BOT_SOCIAL_ISOLATION': value}))


if __name__ == '__main__':
    unittest.main()
