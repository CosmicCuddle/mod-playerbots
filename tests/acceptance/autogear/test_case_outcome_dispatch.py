"""Exercise case-specific pre-save refusal without live services."""
import unittest
from types import SimpleNamespace
from unittest.mock import Mock, patch

from run_autogear_acceptance import Runner
from test_result_identity import LINE


class CaseOutcomeDispatchTests(unittest.TestCase):
    def fixture(self, line, operator=False):
        fx = Mock()
        fx.args.expected_cap = 66
        fx.inventory.return_value = {'rows': [(0, 4, 1, 20)], 'mail': 0}
        fx.log_size.return_value = 0
        fx.result_lines.return_value = [line]
        cmd = Mock()
        cmd.chat = []
        cmd.whispers_since.return_value = [SimpleNamespace(text='refused')]
        return SimpleNamespace(fx=fx, cmd=cmd, ops=cmd if operator else Mock(),
                               commander={'guid': 2}, operator_guid=2,
                               results=[], save_all=Mock(),
                               check=Mock(return_value={'pass': True, 'problems': []}))

    def dispatch(self, line, expect, operator=False):
        r = self.fixture(line, operator)
        with patch('run_autogear_acceptance.pre_save_problems', return_value=[]):
            verdict = Runner.run_case(r, 'case', {'name': 'fixture', 'guid': 7}, 'autogear', expect)
        return r, verdict

    def test_wrong_case_result_never_requests_second_save(self):
        for line, expect in ((LINE, {'refusal': 2}), (LINE, {'ran': 1}),
                             (LINE.replace('new=0', 'new=1'), {'unchanged': True})):
            with self.subTest(expect=expect):
                r, verdict = self.dispatch(line, expect)
                self.assertFalse(verdict['pass'])
                self.assertTrue(verdict['post_save_skipped'])
                r.save_all.assert_called_once_with()
                r.check.assert_not_called()

    def test_gm_authority_true_is_not_gm_refusal_evidence(self):
        line = LINE.replace('refusal=5', 'refusal=2').replace('form=1', 'form=0')
        expect = {'refusal': 2, 'ran': 0, 'unchanged': True, 'authority': False, 'form': 0}
        r, verdict = self.dispatch(line, expect, operator=True)
        self.assertFalse(verdict['pass'])
        r.save_all.assert_called_once_with()
        r.check.assert_not_called()
        r, verdict = self.dispatch(line.replace('authority=true', 'authority=false'), expect, operator=True)
        self.assertTrue(verdict['pass'])
        self.assertEqual(r.save_all.call_count, 2)

    def test_matching_refusal_reaches_inventory_oracle(self):
        r, verdict = self.dispatch(LINE, {'refusal': 5, 'ran': 0, 'unchanged': True})
        self.assertTrue(verdict['pass'])
        self.assertEqual(r.save_all.call_count, 2)
        r.check.assert_called_once()


if __name__ == '__main__':
    unittest.main()
