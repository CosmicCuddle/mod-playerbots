# Playerbots follow-up audit — source only

Pin: parent `405f81196161017eebae3ee7a612d786a3ed2a9d`, candidate branch `feature/codex-pb-f14-social-dispatch-20261114`. This inventory is not native or gameplay evidence. It proposes no changes outside draft PR #21.

## Personal-bot adoption reachability

`PlayerbotHolder::AddPlayerBot` is public. Its in-repository callers are `ProcessBotCommand` (`add`, `login`, `addaccount`), the `addclass` command, RandomPlayerbotMgr's population loop, arena member login in `BattleGroundJoinAction`, and the currently unreferenced `AddPlayerBotOperation`. The three ordinary commands check persisted type-1 accounts before calling it. `addclass` chooses from `addclassCache`, populated from type-2 `addClassTypeAccounts`; its selection is distinct from type-1 RandomBots. The RandomPlayerbotMgr and arena paths pass master account ID zero and are RandomBot or arena population, not personal-bot adoption. `OnBotLoginOperation` is queued from the async login completion path; it selects the master's holder only when a master account ID and live master session are available. `GroupInviteOperation::AddMember` is queued only from `OnBotLogin` in this tree. No other in-repository caller of `AddPlayerBotOperation` was found.

`AddPlayerBot` itself does not apply the type-1 refusal for a nonzero master account ID. An external module or future in-repository direct caller could bypass the three command checks; this is an API-level policy gap, not a proven current in-repository bypass. Review that API boundary separately if the owner wants the policy to cover all future direct callers. Draft #21 now preflights the currently reachable direct group add with the standard invite veto and login epochs. That preflight enforces ordinary group rules for eligible personal/Altbots; it does not classify an Altbot as an F14 social RandomBot.

## Account assignment lifecycle

`PlayerbotAIConfig::Initialize` calls `RandomPlayerbotMgr::AssignAccountTypes` after random-account creation during world-script startup. The `rndbot reload` console path also calls `Initialize`. `AssignAccountTypes` queries login `account` rows by managed prefix, reads persisted account-type assignments, creates type 0 rows for newly found managed accounts, assigns type 1 and type 2 from their configured pools, then rebuilds `rndBotTypeAccounts` and `addClassTypeAccounts`. The command refusal uses the type-1 snapshot plus active `IsRandomBot` as positive evidence. A managed-prefix account missing from both lists is refused as unresolved; an ordinary account missing from both lists is not refused. A DB/account change after the last initialization may leave the snapshot stale until reload; no automatic refresh was found in this tree. Whether reload can race a concurrent command read needs a native/thread-ownership check. No DB operation was performed for this audit.

## Existing world-operation queue

`PlayerbotWorldThreadProcessor` has a global queue. Per-bot logout does not clear it, and a later login can reuse a GUID. PR #21 adds epoch checks only to new `GroupAcceptOperation` and the existing, currently reachable `GroupInviteOperation`.

| Operation | Current pinned-tree reachability | State effect and follow-up priority |
| --- | --- | --- |
| `GroupInviteOperation` | `OnBotLogin` | Direct group create/convert/add. In PR #21: invite veto plus both participant epochs. Staged proof still needed. |
| `GroupConvertToRaidOperation` | Four `InviteToGroupAction` call sites | Converts whatever group the same GUID has at drain. GUID-only validity; add an intent/group-generation check in a separate hardening slice. |
| `GroupSetLeaderOperation` | `PassLeadershipToMasterAction` | Changes leader in the bot's current group if target is a member. GUID-only validity; review session and group intent separately. |
| `BotLogoutGroupCleanupOperation` | `LogoutPlayerBot` | Can save AI/group repository state. A later session with same GUID could receive old cleanup; review epoch before broad rollout. |
| `OnBotLoginOperation` | Async login completion | Resolves connected bot by GUID and holder by account; stale completion could target a later session. Review pending-login identity. |
| `ArenaTeamAssignOperation` | `RandomPlayerbotFactory` | Mutates arena assignment for a resolved bot; review later-session intent. |
| `ArenaTeamDropMemberOperation` | `BattleGroundJoinAction` | Checks current team membership/bracket before removal, but GUID-only bot validity; review race and intent. |
| `GroupRemoveMemberOperation`, `ArenaGroupFormationOperation`, `AddPlayerBotOperation` | No constructor call found outside declarations | Not established as reachable in this pinned tree; keep on design inventory, do not change in PR #21. |

## Direct handler calls still outside PR #21

Search of `Handle*Opcode(` in `src/Ai`, `src/Bot` and `src/Script` found direct calls in guild accept/decline/leave/petition, arena accept/decline, trade status/accept, duel, quest/gossip/item, battleground leave/status, logout cancel, emote and other gameplay paths. Their exact Core opcode thread classification and whether each caller runs on a map worker remain to be verified against the installed Core. Highest next F14 social candidates are `GuildAcceptAction`, `TradeStatusAction`, `RpgSubActions` trade accept, `ArenaTeamActions` and direct group conversion/leadership. Do not transfer the four-route queue treatment mechanically: some handlers may be map safe or depend on immediate results. This is audit/design only; no other handler is changed by PR #21.
