"""Fresh account setup must never delete or reuse existing identities."""
import unittest
from types import SimpleNamespace
from unittest.mock import Mock, patch

from run_autogear_acceptance import Fixture


class AccountSetupTests(unittest.TestCase):
    def test_existing_accounts_stop_before_mutation(self):
        fx = SimpleNamespace(names={'auth': 'fixture_auth'}, args=SimpleNamespace(world_port=18085),
                             sql=Mock(return_value=[['42']]))
        with self.assertRaisesRegex(SystemExit, 'fixture accounts already exist'):
            Fixture.accounts(fx)
        fx.sql.assert_called_once_with('fixture_auth', "SELECT id FROM account WHERE username IN ('AGOPS', 'AGCMD')")

    def test_fresh_setup_inserts_without_delete_or_replace(self):
        fx = SimpleNamespace(names={'auth': 'fixture_auth'}, args=SimpleNamespace(world_port=18085),
                             sql=Mock(side_effect=[[], [], [], [['42']], [], []]))
        with patch('run_autogear_acceptance.wc.make_registration', return_value=(b'salt', b'verifier')):
            Fixture.accounts(fx)
        statements = [call.args[1] for call in fx.sql.call_args_list]
        self.assertEqual(len(statements), 6)
        self.assertTrue(statements[0].startswith('SELECT id'))
        self.assertTrue(statements[1].startswith('UPDATE realmlist'))
        self.assertEqual(sum(s.startswith('INSERT INTO account (') for s in statements), 2)
        self.assertEqual(sum(s.startswith('INSERT INTO account_access (') for s in statements), 1)
        self.assertFalse(any(s.startswith(('DELETE', 'REPLACE', 'DROP')) for s in statements))


if __name__ == '__main__':
    unittest.main()
