import unittest
from stranger_case import (CHAT_MSG_WHISPER, CHAT_MSG_WHISPER_INFORM, expected_reply,
                           stranger_case_problems, stranger_preflight_problems)
from test_result_identity import LINE

BOT = 7


class Msg:
    def __init__(self, msg_type, sender, text):
        self.type, self.sender_guid, self.text = msg_type, sender, text


INV = {"rows": [(0, 4, 100, 25)], "mail": 0}
RND = expected_reply("random", "Agcmd")
GOOD = [Msg(CHAT_MSG_WHISPER, BOT, "I am in a group with Agcmd. You can ask him for invite."),
        Msg(CHAT_MSG_WHISPER_INFORM, BOT, "autogear")]


def run(messages=GOOD, not_found=False, lines=(), final_lines=None, before=INV, after=INV, pattern=RND):
    lines = list(lines)
    return stranger_case_problems(bot_guid=BOT, messages=messages, not_found=not_found, lines=lines,
                                  final_lines=lines if final_lines is None else final_lines,
                                  before=before, after=after, reply_pattern=pattern)


class StrangerCaseTests(unittest.TestCase):
    def test_pass(self):
        self.assertEqual(run(), ("pass", []))

    def test_alt_reply(self):
        alt = [Msg(CHAT_MSG_WHISPER, BOT, "Invite me to your group first"), GOOD[1]]
        self.assertEqual(run(alt, pattern=expected_reply("alt", "Agcmd")), ("pass", []))
        self.assertEqual(run(GOOD, pattern=expected_reply("alt", "Agcmd"))[0], "fail")

    def test_absent_reply_is_inconclusive_not_pass(self):
        self.assertEqual(run([GOOD[1]])[0], "inconclusive")
        self.assertEqual(run([])[0], "inconclusive")

    def test_not_delivered_is_inconclusive(self):
        self.assertEqual(run([], not_found=True)[0], "inconclusive")
        self.assertEqual(run(GOOD, not_found=True)[0], "inconclusive")

    def test_missing_inform_is_inconclusive(self):
        self.assertEqual(run([GOOD[0]])[0], "inconclusive")

    def test_reply_from_other_sender_ignored(self):
        self.assertEqual(run([Msg(CHAT_MSG_WHISPER, 99, GOOD[0].text), GOOD[1]])[0], "inconclusive")

    def test_wrong_or_extra_reply_fails(self):
        self.assertEqual(run([Msg(CHAT_MSG_WHISPER, BOT, "Geared up to item level 66"), GOOD[1]])[0], "fail")
        self.assertEqual(run(GOOD + [GOOD[0]])[0], "fail")

    def test_leader_name_is_literal(self):
        self.assertEqual(run([Msg(CHAT_MSG_WHISPER, BOT, "I am in a group with AgcmX. You can ask him for invite."),
                              GOOD[1]], pattern=expected_reply("random", "Agcm."))[0], "fail")

    def test_result_line_fails(self):
        self.assertEqual(run(lines=[LINE])[0], "fail")
        self.assertEqual(run(lines=[], final_lines=[LINE])[0], "fail")

    def test_custody(self):
        self.assertEqual(run(after={"rows": [], "mail": 0})[0], "fail")
        self.assertEqual(run(after={"rows": INV["rows"], "mail": 1})[0], "fail")
        self.assertEqual(run(after=None)[0], "inconclusive")

    def test_fail_outranks_inconclusive(self):
        self.assertEqual(run([], lines=[LINE])[0], "fail")


class StrangerPreflightTests(unittest.TestCase):
    CMD = {"account": 2, "guid": 2}
    OPS = {"account": 1, "guid": 1}

    def stranger(self, **kw):
        base = {"account": 3, "guid": 3, "username": "AGSTR", "race": 1}
        base.update(kw)
        return base

    def check(self, stranger=None, **kw):
        rows = {"gm_rows": [], "group_rows": [], "guild_rows": [], "random_account_names": {"RNDBOT1"}}
        rows.update(kw)
        return stranger_preflight_problems(stranger or self.stranger(), self.CMD, self.OPS, **rows)

    def test_clean(self):
        self.assertEqual(self.check(), [])

    def test_each_authority_signal(self):
        cases = [dict(stranger=self.stranger(account=2)), dict(stranger=self.stranger(guid=1)),
                 dict(gm_rows=[["3"]]), dict(group_rows=[["1"]]), dict(guild_rows=[["1"]]),
                 dict(stranger=self.stranger(username="RNDBOT9")), dict(stranger=self.stranger(username="RNDBOT1")),
                 dict(stranger=self.stranger(race=2)), dict(chat_filter_rows=[["1"]])]
        for case in cases:
            with self.subTest(case=case):
                self.assertTrue(self.check(**case))

    def test_unknown_population(self):
        with self.assertRaises(ValueError):
            expected_reply("addclass", "Agcmd")


if __name__ == "__main__":
    unittest.main()
