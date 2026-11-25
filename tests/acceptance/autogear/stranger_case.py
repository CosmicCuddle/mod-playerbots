"""Pure oracle for the optional stranger-sender whisper cases (S1/S2).

A same-faction, non-GM, non-master, ungrouped player whispers a command to a bot. The reviewed
PlayerbotSecurity source refuses it at the first HandleCommand gate and whispers one denial reply.
This module only classifies observations; it proves nothing about the silent party/channel drop,
command nonce, save durability or release acceptance. An absent observation is never a pass.
"""
import re

CHAT_MSG_WHISPER = 0x07
CHAT_MSG_WHISPER_INFORM = 0x09

# Exact reviewed PlayerbotSecurity::CheckLevelFor replies for the two fixture populations.
RANDOM_REPLY = r"I am in a group with {leader}\. You can ask him for invite\."
ALT_REPLY = r"Invite me to your group first"


def expected_reply(population, leader):
    if population == "random":
        return RANDOM_REPLY.format(leader=re.escape(leader))
    if population == "alt":
        return ALT_REPLY
    raise ValueError(f"unknown population {population!r}")


def stranger_preflight_problems(stranger, commander, operator, *, gm_rows, group_rows, guild_rows,
                                random_account_names, chat_filter_rows=()):
    """Confirm the stranger is really unauthorized before any whisper is sent."""
    problems = []
    if stranger["account"] in (commander["account"], operator["account"]):
        problems.append("stranger shares an account with the commander or operator")
    if stranger["guid"] in (commander["guid"], operator["guid"]):
        problems.append("stranger is the commander or operator character")
    if gm_rows:
        problems.append("stranger account has account_access (GM) rows")
    if group_rows:
        problems.append("stranger is a group member")
    if guild_rows:
        problems.append("stranger is a guild member")
    if stranger["username"].upper().startswith("RNDBOT") or stranger["username"] in random_account_names:
        problems.append("stranger account looks like a random-bot account")
    if chat_filter_rows:
        # ChatFilter.Whisper defaults on; any `chat_filter` word could turn the echo into CHAT_MSG_FILTERED
        # and suppress the whisper to its target, so a fresh fixture must have an empty table.
        problems.append("chat_filter table is not empty")
    if stranger.get("race") not in (1, 3, 4, 7):
        problems.append("stranger is not Alliance (same-faction case only)")
    return problems


def stranger_case_problems(*, bot_guid, messages, not_found, lines, final_lines, before, after, reply_pattern):
    """Return (status, problems); status is 'pass', 'fail' or 'inconclusive'.

    messages: ChatMessage-like objects received by the stranger since the whisper was sent.
    not_found: True if SMSG_CHAT_PLAYER_NOT_FOUND arrived (whisper never reached the bot).
    lines / final_lines: PB59 result lines for the bot before and after the explicit save.
    before / after: inventory snapshots {"rows": [...], "mail": n}.
    """
    problems, unsure = [], []
    if not_found:
        unsure.append("whisper not delivered: SMSG_CHAT_PLAYER_NOT_FOUND")
    informs = [m for m in messages if m.type == CHAT_MSG_WHISPER_INFORM and m.sender_guid == bot_guid]
    if len(informs) != 1:
        unsure.append(f"expected one whisper-inform echo from the bot, saw {len(informs)}")
    replies = [m for m in messages if m.type == CHAT_MSG_WHISPER and m.sender_guid == bot_guid]
    if not replies:
        unsure.append("no denial reply observed (absent reply is not proof of denial)")
    elif len(replies) != 1:
        problems.append(f"expected exactly one bot whisper, saw {len(replies)}")
    elif not re.fullmatch(reply_pattern, replies[0].text):
        problems.append(f"denial reply {replies[0].text!r} does not match {reply_pattern!r}")
    if lines:
        problems.append(f"stranger command produced result lines: {lines}")
    if final_lines != lines:
        problems.append("observed bot event window changed after explicit save")
    if after is None:
        unsure.append("no post-save inventory snapshot")
    else:
        if before["rows"] != after["rows"]:
            problems.append("inventory changed on a stranger command")
        if before["mail"] != after["mail"]:
            problems.append(f"mail count changed {before['mail']} -> {after['mail']}")
    if problems:
        return "fail", problems + unsure
    if unsure:
        return "inconclusive", unsure
    return "pass", []
