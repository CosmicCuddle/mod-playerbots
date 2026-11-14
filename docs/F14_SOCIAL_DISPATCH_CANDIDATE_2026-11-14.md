# F14 Playerbots social dispatch candidate — source and test plan

This DRAFT source candidate starts at public Playerbots `405f81196161017eebae3ee7a612d786a3ed2a9d`. Installed Core, Playerbots, IP and map-worker configuration remain UNKNOWN. It does not change Core, IP, SQL, config, DBC, client files or a server. F14/Paid remain OFF.

## Thread route and exact scope

In pinned Playerbots, AI actions run from the player update path and can execute on map workers. `InviteToGroupAction`, `TradeAction` and `AcceptBattlegroundInvitationAction` previously called `PROCESS_THREADUNSAFE` handlers directly. Their existing payload fields and order now enter `WorldSession::QueuePacket`. For headless RandomBots, `PlayerbotsWorldScript::OnUpdate` calls `sRandomPlayerbotMgr.UpdateSessions()`; for a master's bots, `PlayerbotsSessionScript::OnSessionUpdate` calls `PlayerbotMgr::UpdateSessions()`. Both reach `PlayerbotHolder::HandleBotPackets`, which drains `NextQueuedPacket()` and invokes the opcode handler. At candidate Core #25, `WorldSession::Update` calls `OnSessionUpdate` in its unsafe/world pass. Session queues own packet copies; a destroyed session loses its queue. A new session is not handed a prior session's packet by this candidate.

Group accept cannot simply queue its packet because `GetGroup`, `SetMaster`, strategies, greeting and summon depend immediately on the handler result. `GroupAcceptOperation` runs from `PlayerbotWorldThreadProcessor::Update` in the world script, resolves GUIDs at execution, checks the same pending group/leader and live bot session, runs the unchanged zero-role payload through `HandleGroupAcceptOpcode`, verifies new membership, then performs the follow-up in the old order. A duplicate operation sees the invite removed by the first successful accept and skips before the handler. A refused accept cannot run the follow-up. Queue-full returns false from the action. The operation stores no `Player*` or `WorldSession*` across ticks.

Pinned Core's group-invite handler rejects an already grouped/invited target; trade initiation returns if trade data exists; BG port requires a live queue and invitation. These are drain-time duplicate/stale guards, not a promise that the action never queues a duplicate. The group-accept operation has its own pending-invite guard. All four routes need staged checks for disconnect, relog, repeated action ticks and invite/queue cancellation.

## RandomBot personal-bot adoption

The command guard uses `RandomPlayerbotMgr::rndBotTypeAccounts`, populated from the persisted type-1 account assignment snapshot, plus active `IsRandomBot(guid)` as an additional positive signal. It does not use race or character name. `RandomBotAccounts` contains managed-prefix accounts, including type-2 AddClass accounts; a managed account in neither type list is treated as unavailable and refused, while type-2 remains eligible. Character account ID zero is refused. `addaccount` checks the account before enumerating its characters; `add`/`login` check before `AddPlayerBot`. This is a command-path restriction, not a claim that every possible direct `AddPlayerBot` caller has been gated. Source reachability and staged tests remain necessary.

## Local checks and required staged matrix

`node --test tests/f14_social_dispatch_source.test.js`: 7/7 PASS at the candidate source, covering packet fields/order, removal of the four direct calls, group-accept handler-before-follow-up, stale/duplicate/disconnect and account-classification models, and adoption guard placement. `git diff --check`: PASS. Python, clang-format, a C++ compiler, a Core build and ThreadSanitizer are unavailable here; repository instructions also prohibit configuring/building unless explicitly requested.

Before any merge or deployment, test against the owner's exact installed source tuple on a disposable realm, with map workers **>0 and =0** separately:

1. Bot→human, bot→bot and human→bot group invite; accept/decline, duplicate ticks, invite cancellation, logout/relog before drain, full group, role payload and original follow-up order.
2. Trade initiation with repeat ticks, disconnect and a pre-existing trade; blocked IP social pair must stay blocked at the world-thread hook.
3. BG invite port with repeat ticks, canceled/expired queue, logout, normal eligible entry and IP denial; compare packet fields and outcome to pre-change behavior.
4. `.playerbots bot add`, `login` and `addaccount` for offline type-1 RandomBot, active RandomBot, own-account alt, linked-account alt, type-2 AddClass and unknown/unavailable managed-account classification. Confirm a restricted RandomBot cannot reach the direct `GroupInviteOperation::AddMember` command path.
5. Run native compile/link and ThreadSanitizer if the disposable build environment permits; inspect queue lifetime and map/world handoff at exact installed refs. Keep this separate from all other direct `PROCESS_THREADUNSAFE` Playerbots calls.

No gameplay, installed-version or live safety conclusion follows from the local source/model checks.
