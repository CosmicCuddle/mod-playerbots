"""Exercise the runner dispatch boundary with no real clients or database."""
import unittest
from types import SimpleNamespace
from unittest.mock import Mock, patch

from run_autogear_acceptance import Runner

BOT = {'name': 'fixture', 'guid': 7}
EXPECT = {'refusal': 5, 'ran': 0, 'unchanged': True, 'full_bags': True}
GEAR = [(0, 4, 1, 20)]
BACKPACK = [(0, slot, 100 + slot, 25) for slot in range(23, 39)]


class CasePreconditions(unittest.TestCase):
    def fixture(self, rows):
        snapshot = {'rows': rows, 'mail': 0}
        fx = Mock()
        fx.args.expected_cap = 66
        fx.inventory.return_value = snapshot
        fx.log_size.return_value = 0
        fx.result_lines.return_value = ['synthetic result']
        cmd = Mock()
        cmd.chat = []
        cmd.whispers_since.return_value = [SimpleNamespace(text='refused')]
        return SimpleNamespace(fx=fx, cmd=cmd, results=[], save_all=Mock(),
                               bag_sizes=Mock(return_value={}),
                               check=Mock(return_value={'pass': True, 'problems': []}))

    def test_invalid_setup_never_dispatches(self):
        for rows in (GEAR + BACKPACK[:-1], BACKPACK):
            with self.subTest(rows=rows):
                r = self.fixture(rows)
                prepare = Mock()
                with patch('run_autogear_acceptance.pre_save_problems') as guard:
                    verdict = Runner.run_case(r, 'R3', BOT, 'autogear reset', EXPECT, prepare)
                self.assertFalse(verdict['pass'])
                self.assertTrue(verdict['command_skipped'])
                self.assertEqual(r.results, [verdict])
                prepare.assert_called_once_with()
                r.save_all.assert_called_once_with()
                r.cmd.whisper.assert_not_called()
                r.fx.log_size.assert_not_called()
                r.check.assert_not_called()
                guard.assert_not_called()

    def test_valid_setup_reaches_final_oracle(self):
        r = self.fixture(GEAR + BACKPACK)
        with patch('run_autogear_acceptance.pre_save_problems', return_value=[]) as guard:
            verdict = Runner.run_case(r, 'R3', BOT, 'autogear reset', EXPECT)
        self.assertTrue(verdict['pass'])
        r.cmd.whisper.assert_called_once_with('fixture', 'autogear reset')
        guard.assert_called_once_with(['synthetic result'], 66)
        self.assertEqual(r.save_all.call_count, 2)
        self.assertEqual(r.fx.inventory.call_count, 2)
        r.check.assert_called_once()

    def test_uncertain_result_never_requests_second_save(self):
        r = self.fixture(GEAR + BACKPACK)
        with patch('run_autogear_acceptance.pre_save_problems', return_value=['uncertain result']):
            verdict = Runner.run_case(r, 'R3', BOT, 'autogear reset', EXPECT)
        self.assertFalse(verdict['pass'])
        self.assertTrue(verdict['post_save_skipped'])
        r.cmd.whisper.assert_called_once()
        r.save_all.assert_called_once_with()
        r.fx.inventory.assert_called_once_with(7)
        r.check.assert_not_called()


if __name__ == '__main__':
    unittest.main()
