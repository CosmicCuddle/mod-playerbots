"""Test the actual case dispatcher with all game/DB calls replaced by recorders."""
import unittest
from types import SimpleNamespace
from unittest.mock import Mock

from run_autogear_acceptance import Runner


class CaseSequenceTests(unittest.TestCase):
    def fixture(self, fail=None, missing_random=False, raise_gm=False):
        runner = SimpleNamespace(alt=object(), random=None if missing_random else object(),
                                 cmd=object(), ops=object(), calls=[])
        commander = runner.cmd

        def run(name, bot, command, expected, prepare=None):
            case = name.split()[0]
            runner.calls.append((case, runner.cmd, bot))
            if prepare is not None:
                prepare()
            if case == 'G1' and raise_gm:
                raise RuntimeError('synthetic GM failure')
            return {'pass': case != fail}

        runner.run_case = Mock(side_effect=run)
        runner.prepare = Mock()
        runner.fill_bags = Mock(return_value=runner.prepare)
        return runner, commander

    def test_complete_sequence(self):
        r, commander = self.fixture()
        Runner.cases(r)
        self.assertEqual([c[0] for c in r.calls], ['A1','A2','A3','B1','B2','R1','R2','R3','G1'])
        self.assertTrue(all(c[1] is commander for c in r.calls[:-1]))
        self.assertTrue(all(c[2] is r.alt for c in r.calls[:3]))
        self.assertTrue(all(c[2] is r.random for c in r.calls[3:]))
        self.assertIs(r.calls[-1][1], r.ops)
        self.assertIs(r.cmd, commander)
        r.fill_bags.assert_called_once_with(r.random, 0)
        r.prepare.assert_called_once_with()

    def test_stop_after_every_failed_case(self):
        cases = ['A1','A2','A3','B1','B2','R1','R2','R3','G1']
        for i, case in enumerate(cases):
            with self.subTest(case=case):
                r, commander = self.fixture(fail=case)
                Runner.cases(r)
                self.assertEqual([c[0] for c in r.calls], cases[:i+1])
                self.assertIs(r.cmd, commander)
                if i < 7:
                    r.fill_bags.assert_not_called()
                    r.prepare.assert_not_called()

    def test_missing_random_remains_incomplete(self):
        r, commander = self.fixture(missing_random=True)
        Runner.cases(r)
        self.assertEqual([c[0] for c in r.calls], ['A1','A2','A3'])
        r.fill_bags.assert_not_called()
        self.assertIs(r.cmd, commander)

    def test_gm_exception_restores_commander(self):
        r, commander = self.fixture(raise_gm=True)
        with self.assertRaisesRegex(RuntimeError, 'synthetic GM failure'):
            Runner.cases(r)
        self.assertIs(r.cmd, commander)


if __name__ == '__main__':
    unittest.main()
