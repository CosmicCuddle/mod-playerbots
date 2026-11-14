# Playerbots command logout lifecycle candidate

This DRAFT child change starts from Playerbots social dispatch PR #21 at
`1cf279183a35721b997033d73270e2d69becad75`. The installed Core and
Playerbots tuple remains UNKNOWN. It does not change SQL, config, DBC,
client files, or a server. F14/Paid remain OFF.

The personal bot's pointer-form `HandleCommand` previously invoked
`PlayerbotMgr::LogoutPlayerBot` directly for its master's `logout` command.
That holder removes the bot from its map, logs out and deletes the Player,
and destroys its `PlayerbotAI`. The guild-chat script iterates that holder's
bot map while invoking `HandleCommand`; direct teardown can invalidate its
active iterator and the callee's AI stack. The whisper script already has a
special case labeled a logout-crash hotfix. This candidate makes only the
pointer-form command path asynchronous; it does not claim to fix other
direct logout or hire paths.

The command captures bot/master GUIDs and current login generations, queues
`BotCommandLogoutOperation`, and returns while the chat callback is still
active. The world operation checks both generations, resolves current
players and the bot session, rejects a session already logging out, then
checks the same master's holder still owns that exact bot and AI before
calling the existing `LogoutPlayerBot`. The operation does not use the bot
or AI after that call. A full queue leaves the bot logged in and suppresses
the previous whisper acknowledgement. The existing whisper chat-script
suppression remains in place. Other queued requests for the same bot will
fail their generation checks once the first logout retires it.

Local checks: `node --test tests/f14_command_logout_source.test.js
tests/f14_social_dispatch_source.test.js` passes 20/20; `git diff --check`
passes. Python and clang-format are unavailable here. Native compile,
ThreadSanitizer, hosted Actions, and gameplay remain unrun.

On a disposable realm with map workers on and off, test master-issued
whisper, guild, and group `logout`; command separators and repeated commands;
bot/master logout and relog before drain; ownership transfer before drain;
queue saturation; and the existing save/group cleanup/message order. Watch
for bot-map iterator invalidation, callbacks after Player deletion, and
stale queued operations. The separate `HireAction` teardown issue and AI
timer logout remain outside this change and need their own review. Do not
merge or deploy this candidate from source tests alone.
