# F14 Playerbots guild-chat world dispatch candidate

This DRAFT child change depends on Playerbots social dispatch PR #21 at
`1cf279183a35721b997033d73270e2d69becad75`. It has no SQL, config,
DBC, client, or live-server change. F14 and Paid remain OFF. The installed
Core, IP, Playerbots, and guild-policy revisions are not yet verified.

`PlayerbotAI::SayToGuild` previously called `Guild::BroadcastToGuild` from
bot AI. AI can run on a map worker; Core's guild-recipient policy in Core
PR #28 v2 (`2b356ff99ea93a1c8da1323aa1a07a6bb96384f0`) belongs on the world-thread send path. The call now queues one
value-only `GuildChatOperation` with the bot GUID, guild ID, login generation,
and text. The operation runs in the existing world-script queue after Core
waits for map workers, resolves the current player/session/guild, and checks
the same login generation, guild membership, and speaking right. It runs
Core's `OnPlayerCanUseChat` preflight and then calls `BroadcastToGuild` with
the original `LANG_UNIVERSAL` behavior; Core PR #28 v2 applies its narrow
per-recipient F14 filter inside that broadcast. No incoming
`CMSG_MESSAGECHAT` packet is fabricated: the normal client parser rejects
`LANG_UNIVERSAL` for that opcode. Addon and officer chat are outside scope.

`SayToGuild` now returns whether the operation was queued. A later logout,
guild change, rights change, script veto, empty rewritten text, or full queue
can prevent delivery. The former return value signaled an immediate send.
`BroadcastHelper` can therefore stop at a queued guild attempt even if the
world-thread operation later drops it; this is an asynchronous behavior
change to check in gameplay. The value-only operation holds no player,
session, or guild pointer across updates.

Local checks: `node --test tests/f14_guild_chat_source.test.js
tests/f14_social_dispatch_source.test.js` passes 20/20, and `git diff
--check` passes. Python and clang-format are unavailable in this workspace.
No native compilation, ThreadSanitizer, or gameplay check has run.

On a disposable realm at the exact installed source tuple, with map workers
enabled and then disabled, verify normal guild chat from RandomBots and
personal bots, script vetoes, a bot logging out and back in before drain,
guild removal/rejoin before drain, rank-right removal, queue saturation,
and Core #28 v2 delivery to eligible and ineligible human recipients. Also
check BroadcastHelper's channel fallback after a queued guild attempt that
later fails. Keep the owner's narrow policy: mixed guilds remain allowed;
only the restricted BE/Draenei RNDBOT and still-Vanilla human pair is
isolated. Do not merge or deploy this candidate on source/model tests alone.
