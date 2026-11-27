"""Adversarial evidence cases for the disposable fixture (CS01 Part 41 Task 4).

Each case feeds forged, stale, duplicated or replayed server evidence to the fixture's own oracle and
requires a refusal. Cases that need a durable per-command receipt, a database readback or a shutdown
hook are listed in BLOCKED and asserted as not executed, never faked.
"""
import unittest

from acceptance_oracle import pre_save_problems
from result_identity import identity_problems

BOT, SENDER = 0x2A, 0x15


def line(bot=BOT, sender=SENDER, raw=1, effective=1, authority="true", cap=78, form=0, refusal=0, ran="true",
         new=3, owned_missing=0):
    def guid(low):
        return f"GUID Full: 0x{low:016x} Type: Player Low: {low}"
    return (f"PB59 AutoGear bot={guid(bot)} sender={guid(sender)} raw={raw} effective={effective} "
            f"authority={authority} cap={cap} form={form} refusal={refusal} ran={ran} stripped=0 new={new} "
            f"revoked=0 capHeld=true bisRestoreFailed=false ownedMissing={owned_missing}")


BLOCKED = (
    "durable per-command receipt (needs a Core/PB receipt interface; none exists in R1 + PB68)",
    "database readback failure after a run (Core114+ readback holder, owned by CS01)",
    "unexpected worldserver shutdown mid-command (needs native fixture and Core recovery persistence)",
    "duplicate completion of one native transaction (needs a command nonce in the result line)",
)


class AdversarialEvidenceTest(unittest.TestCase):
    def test_honest_line_passes(self):
        self.assertEqual(identity_problems([line()], BOT, SENDER), [])

    def test_duplicate_completion_lines_refused(self):
        self.assertTrue(identity_problems([line(), line()], BOT, SENDER))

    def test_replayed_line_for_other_bot_or_sender_refused(self):
        self.assertTrue(identity_problems([line(bot=0x2B)], BOT, SENDER))
        self.assertTrue(identity_problems([line(sender=0x16)], BOT, SENDER))

    def test_forged_guid_text_refused(self):
        forged = line().replace(f"0x{BOT:016x}", f"0x{BOT + 1:016x}", 1)
        self.assertTrue(identity_problems([forged], BOT, SENDER))

    def test_missing_line_refused(self):
        self.assertTrue(identity_problems([], BOT, SENDER))

    def test_unverified_authority_or_bad_stage_refused(self):
        self.assertTrue(identity_problems([line(authority="false")], BOT, SENDER))
        self.assertTrue(identity_problems([line(raw=1, effective=2)], BOT, SENDER))
        self.assertTrue(identity_problems([line(raw=19, effective=19)], BOT, SENDER))

    def test_wrong_form_refused(self):
        self.assertTrue(identity_problems([line(form=2)], BOT, SENDER, form=0))

    def test_trailing_injection_refused(self):
        self.assertTrue(identity_problems([line() + " ownedMissing=0"], BOT, SENDER))

    def test_cap_mismatch_refused_before_save(self):
        self.assertTrue(pre_save_problems([line(cap=92)], expected_cap=78))
        self.assertTrue(pre_save_problems([line(), line()], expected_cap=78))

    def test_blocked_cases_are_declared_not_executed(self):
        # Listed, never asserted as passing: each needs an interface or native run that does not exist yet.
        self.assertEqual(len(BLOCKED), 4)
        self.assertTrue(all("(" in case for case in BLOCKED), "each blocked case names its missing interface")


if __name__ == "__main__":
    for case in BLOCKED:
        print("NOT EXECUTED / BLOCKED:", case)
    unittest.main()
