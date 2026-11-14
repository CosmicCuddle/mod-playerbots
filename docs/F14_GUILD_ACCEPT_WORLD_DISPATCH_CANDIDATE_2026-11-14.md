# F14 bot guild-accept world dispatch candidate

This DRAFT child change starts at Playerbots PR #21 head
`1cf279183a35721b997033d73270e2d69becad75`. Claude's F-5 source
finding in coordination Issue #13 identified bot `GuildAcceptAction` calling
Core's `HandleGuildAcceptOpcode` directly from map-worker AI. At Core #28 v2
`2b356ff99ea93a1c8da1323aa1a07a6bb96384f0`, guild admission from an
off-world thread relies on IP late recheck after persistence. This change
moves just the accept handler to the existing world-operation queue so the
Core/IP veto can run before persistence. Decline/leave remain outside scope;
IP late remediation remains the safety fallback.

The AI action retains its inviter, guild and security checks, then captures
the bot GUID, current invited guild ID and login generation. Queue-full
returns false. At drain, `GuildAcceptOperation` rechecks generation, current
Player/session identity, no existing guild, and unchanged invited guild ID
before invoking the same empty `CMSG_GUILD_ACCEPT` handler on the world
thread. Pinned Core's `AcceptGuildInvite::Read` is empty and its handler uses
the Player's current invited guild ID. The operation reports successful
dispatch, not a promise that admission succeeded. It stores no Player,
session or Guild pointer across ticks.

This prevents a queued accept for guild A from accepting a later invite to
guild B. A cancel/reinvite cycle to the **same** guild ID is not
distinguishable by this state alone; no separate guild invitation generation
hook exists in this candidate. Test that cycle and retain IP's late recheck
as a fallback. The original decline handler remains direct and thread-unsafe
per the existing inventory; it is not part of this release slice.

Local checks: `node --test tests/f14_guild_accept_source.test.js
tests/f14_social_dispatch_source.test.js` passes 20/20 and `git diff
--check` passes. Python, clang-format, native Core build, TSAN and gameplay
are unavailable or unrun here. The installed source tuple is UNKNOWN.

On a disposable realm with map workers enabled and disabled, test eligible
and F14-restricted guild accept; cancel/change guild/reinvite before drain;
same-guild reinvite; logout/relog; existing membership; security refusal;
queue full; and declined invitation behavior. Verify restricted BE/Draenei
RandomBot admission is vetoed before persistence when the guild contains a
still-Vanilla human. Do not merge or deploy on source checks alone. No SQL,
config, DBC, client or live-server change was made. F14/Paid remain OFF.
