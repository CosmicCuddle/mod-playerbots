"""Strict PB56 command-result identity; not a unique transaction receipt."""
import re

NUM = r'(?:0|[1-9][0-9]{0,9})'
BOOL = r'(?:true|false)'
GUID = r'GUID Full: 0x([0-9a-f]{16}) Type: Player Low: (' + NUM + r')'
RESULT = re.compile(
    r'PB59 AutoGear bot=' + GUID + r' sender=' + GUID
    + r' raw=(' + NUM + r') effective=(' + NUM + r') authority=(' + BOOL + r')'
    + r' cap=' + NUM + r' form=(' + NUM + r') refusal=' + NUM + r' ran=' + BOOL
    + r' stripped=' + NUM + r' new=' + NUM + r' revoked=' + NUM + r' capHeld=' + BOOL
    + r' bisRestoreFailed=' + BOOL + r' ownedMissing=' + NUM
)


def identity_problems(lines, bot, sender, *, form=None, require_authority=True):
    """Reject missing, duplicate, malformed or unexpected observed bot lines."""
    if len(lines) != 1:
        return ['expected exactly one observed bot event; extra or missing log evidence']
    match = RESULT.fullmatch(lines[0])
    if not match:
        return ['unexpected or malformed PB56 bot event']
    bot_hex, bot_low, sender_hex, sender_low, raw, effective, authority, logged_form = match.groups()
    problems = []
    if type(bot) is not int or not 0 < bot <= 0xffffffff or int(bot_hex, 16) != bot or int(bot_low) != bot:
        problems.append('bot GUID does not match case')
    if type(sender) is not int or not 0 < sender <= 0xffffffff or int(sender_hex, 16) != sender or int(sender_low) != sender:
        problems.append('sender GUID does not match case')
    if not 0 <= int(effective) <= int(raw) <= 18:
        problems.append('invalid progression stage evidence')
    if require_authority and authority != 'true':
        problems.append('commander progression authority not verified')
    if int(logged_form) > 6 or (form is not None and int(logged_form) != form):
        problems.append('command form does not match case')
    return problems
