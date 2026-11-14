const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");

const root = path.resolve(__dirname, "..");
const read = relative => fs.readFileSync(path.join(root, relative), "utf8");

const actions = [
    ["src/Ai/Base/Actions/InviteToGroupAction.cpp", "HandleGroupInviteOpcode", "CMSG_GROUP_INVITE"],
    ["src/Ai/Base/Actions/TradeAction.cpp", "HandleInitiateTradeOpcode", "CMSG_INITIATE_TRADE"],
    ["src/Ai/Base/Actions/AcceptBattlegroundInvitationAction.cpp", "HandleBattleFieldPortOpcode", "CMSG_BATTLEFIELD_PORT"]
];

test("the three packet routes retain their payload and queue to a session", () => {
    for (const [file, directHandler, opcode] of actions) {
        const source = read(file);
        assert.ok(source.includes(`WorldPacket ${file.includes("InviteToGroup") ? "p" : "packet"}(${opcode}`), file);
        assert.ok(!source.includes(`->${directHandler}(`), file);
        assert.match(source, /->GetSession\(\)->QueuePacket\(new WorldPacket\((?:p|packet)\)\)/, file);
    }
    const invite = read(actions[0][0]);
    assert.match(invite, /p << player->GetName\(\);\s*p << roles_mask;\s*inviter->GetSession\(\)->QueuePacket/, "group invite name and roles order");
    const trade = read(actions[1][0]);
    assert.match(trade, /packet << player->GetGUID\(\);\s*bot->GetSession\(\)->QueuePacket/, "trade target GUID");
    const bg = read(actions[2][0]);
    assert.match(bg, /packet << type << unk2 << \(uint32\)bgTypeId_ << unk << action;\s*bot->GetSession\(\)->QueuePacket/, "BG-port fields and order");
});

test("group accept is one world-thread operation with guarded post-handler follow-up", () => {
    const action = read("src/Ai/Base/Actions/AcceptInvitationAction.cpp");
    const operations = read("src/Script/WorldThr/PlayerbotOperations.h");
    const section = operations.split("class GroupAcceptOperation : public PlayerbotOperation")[1]?.split("// Group invite operation")[0];
    assert.ok(section, "operation exists");
    assert.ok(!action.includes("HandleGroupAcceptOpcode("), "AI action has no direct handler call");
    assert.match(action, /QueueOperation\(std::move\(operation\)\)/);
    assert.match(section, /bot->GetSession\(\)->GetPlayer\(\) != bot/, "reject disconnected or replaced session");
    assert.match(section, /invite->GetGUID\(\) != m_inviteGroupGuid/, "reject stale invite");
    assert.match(section, /invite->GetLeaderGUID\(\) != m_inviterGuid/, "reject changed inviter");
    const handler = section.indexOf("HandleGroupAcceptOpcode(packet)");
    const membership = section.indexOf("group->IsMember(m_inviterGuid)");
    const master = section.indexOf("botAI->SetMaster(inviter)");
    const strategy = section.indexOf("botAI->ResetStrategies()");
    const summon = section.indexOf("summonAction.Teleport(inviter, bot, true)");
    assert.ok(handler >= 0 && handler < membership && membership < master && master < strategy && strategy < summon);
    assert.match(section, /IsValid\(\) const override[\s\S]*invite->GetGUID\(\) == m_inviteGroupGuid/);
});

test("deferred group accept preserves decline and successful follow-up order", () => {
    const operations = read("src/Script/WorldThr/PlayerbotOperations.h");
    const accept = operations.split("class GroupAcceptOperation")[1]?.split("// Group invite operation")[0];
    assert.ok(accept);
    const ordered = ["CheckLevelFor(PLAYERBOT_SECURITY_INVITE", "SMSG_GROUP_DECLINE",
        "inviter->SendDirectMessage(&data)", "bot->UninviteFromGroup()", "bot->ToggleAFK()",
        "HandleGroupAcceptOpcode(packet)", "group->IsMember(m_inviterGuid)", "botAI->SetMaster(inviter)",
        "botAI->ResetStrategies()", "botAI->ChangeStrategy(", "botAI->Reset()", "botAI->TellMaster(",
        "summonAction.Teleport(inviter, bot, true)"];
    let previous = -1;
    for (const step of ordered) {
        const at = accept.indexOf(step);
        assert.ok(at > previous, `${step} follows previous step`);
        previous = at;
    }
    assert.match(accept, /if \(!botAI->GetSecurity\(\)->CheckLevelFor[\s\S]*?bot->UninviteFromGroup\(\);\s*return false;/);
    assert.match(accept, /if \(!group \|\| !group->IsMember\(m_inviterGuid\)\)\s*return false;/);
});

test("personal-bot commands reject authoritative RandomBot account types before login", () => {
    const manager = read("src/Bot/PlayerbotMgr.cpp");
    const helper = manager.split("static std::string PersonalBotAccountRejection").at(-1)?.split("std::string const PlayerbotHolder::ProcessBotCommand")[0];
    assert.ok(helper);
    assert.match(helper, /rndBotTypeAccounts/);
    assert.match(helper, /IsRandomBot\(guid.GetCounter\(\)\)/);
    assert.match(helper, /addClassTypeAccounts/);
    assert.match(helper, /bot account classification unavailable/);
    assert.ok(!/getRace\(|GetRace\(/.test(helper));
    const command = manager.split("std::string const PlayerbotHolder::ProcessBotCommand")[1].split("std::vector<std::string> PlayerbotHolder::HandlePlayerbotCommand")[0];
    assert.ok(command.indexOf("PersonalBotAccountRejection(accountId, guid)") < command.indexOf("AddPlayerBot(guid, masterAccountId)"));
    const entry = manager.split("void PlayerbotHolder::AddPlayerBot(ObjectGuid playerGuid, uint32 masterAccountId)")[1]?.split("void PlayerbotHolder::AbandonPendingLogin")[0];
    assert.ok(entry);
    assert.match(entry, /if \(masterAccountId\)[\s\S]*?PersonalBotAccountRejection\(accountId, playerGuid\)[\s\S]*?return;/);
    assert.ok(entry.indexOf("PersonalBotAccountRejection(accountId, playerGuid)") < entry.indexOf("new WorldSession("));
    const addAccount = manager.split("if (!strcmp(cmd, \"addaccount\"))")[1];
    assert.ok(addAccount.indexOf("PersonalBotAccountRejection(accountId, ObjectGuid::Empty)") < addAccount.indexOf("SELECT name FROM characters WHERE account"));
});

test("group accept model runs the handler once and follows up only after membership", () => {
    const run = (state, handler) => {
        if (!state.online || !state.invite || state.invite !== state.expectedInvite)
            return false;
        handler(state);
        if (!state.member)
            return false;
        state.followUps++;
        return true;
    };
    const accepted = {online: true, invite: 7, expectedInvite: 7, member: false, followUps: 0};
    assert.equal(run(accepted, state => { state.member = true; state.invite = null; }), true);
    assert.equal(run(accepted, () => { throw new Error("duplicate handler"); }), false);
    assert.equal(accepted.followUps, 1);
    const refused = {online: true, invite: 7, expectedInvite: 7, member: false, followUps: 0};
    assert.equal(run(refused, state => { state.invite = null; }), false);
    assert.equal(refused.followUps, 0);
    const disconnected = {online: false, invite: 7, expectedInvite: 7, member: false, followUps: 0};
    assert.equal(run(disconnected, () => { throw new Error("disconnected handler"); }), false);
});

test("account classification model keeps ordinary and AddClass alts distinct from RandomBots", () => {
    const classify = ({accountId, type1, type2, managed, current}) => {
        if (!accountId) return "unavailable";
        if (type1 || current) return "random";
        if (managed && !type2) return "unavailable";
        return "allowed";
    };
    assert.equal(classify({accountId: 11, type1: true, managed: true}), "random"); // offline RNDBOT
    assert.equal(classify({accountId: 11, type1: false, managed: true, current: true}), "random");
    assert.equal(classify({accountId: 12, type1: false, type2: true, managed: true}), "allowed");
    assert.equal(classify({accountId: 13, type1: false, type2: false, managed: false}), "allowed");
    assert.equal(classify({accountId: 14, type1: false, type2: false, managed: true}), "unavailable");
    assert.equal(classify({accountId: 0}), "unavailable");
});

test("owned and random bot packet queues drain through existing world-thread callbacks", () => {
    const scripts = read("src/Script/Playerbots.cpp");
    const holder = read("src/Bot/PlayerbotMgr.cpp");
    const processor = read("src/Script/WorldThr/PlayerbotWorldThreadProcessor.cpp");
    assert.match(scripts, /void OnSessionUpdate\(WorldSession\* session, uint32[\s\S]*?playerbotMgr->UpdateSessions\(\)/);
    assert.match(scripts, /void OnUpdate\(uint32 diff\) override[\s\S]*?sRandomPlayerbotMgr\.UpdateSessions\(\)[\s\S]*?PlayerbotWorldThreadProcessor::instance\(\)\.Update\(diff\)/);
    assert.match(holder, /void PlayerbotHolder::UpdateSessions\(\)[\s\S]*?HandleBotPackets\(bot->GetSession\(\)\)/);
    assert.match(holder, /void PlayerbotHolder::HandleBotPackets\(WorldSession\* session\)[\s\S]*?session->NextQueuedPacket\(\)[\s\S]*?opHandle->Call\(session, \*packet\)/);
    assert.match(processor, /void PlayerbotWorldThreadProcessor::ProcessBatch\(\)[\s\S]*?operation->IsValid\(\)[\s\S]*?operation->Execute\(\)/);
});

test("offline RandomBot account classification comes from assigned type 1", () => {
    const manager = read("src/Bot/RandomPlayerbotMgr.cpp");
    const assignment = manager.split("void RandomPlayerbotMgr::AssignAccountTypes()")[1]?.split("bool RandomPlayerbotMgr::IsAccountType")[0];
    assert.ok(assignment);
    assert.match(assignment, /PLAYERBOTS_SEL_ACCOUNT_TYPE/);
    assert.match(assignment, /if \(accountType == 1\)\s*rndBotTypeAccounts\.push_back\(accountId\)/);
    assert.match(assignment, /else if \(accountType == 2\)\s*addClassTypeAccounts\.push_back\(accountId\)/);
    const runtime = manager.split("bool RandomPlayerbotMgr::IsRandomBot\(ObjectGuid::LowType bot\)")[1]?.split("bool RandomPlayerbotMgr::IsAddclassBot")[0];
    assert.match(runtime, /currentBots\.contains\(bot\)/, "active identity alone misses offline targets");
});

test("account pool disable unassigns before rebuilding guarded type lists", () => {
    const config = read("src/PlayerbotAIConfig.cpp");
    const factory = read("src/Bot/Factory/RandomPlayerbotFactory.cpp");
    const manager = read("src/Bot/RandomPlayerbotMgr.cpp");
    assert.ok(config.indexOf("RandomPlayerbotFactory::CreateRandomBots()") <
        config.indexOf("sRandomPlayerbotMgr.AssignAccountTypes()"));
    const calculate = factory.split("uint32 RandomPlayerbotFactory::CalculateTotalAccountCount()")[1]?.split("uint32 RandomPlayerbotFactory::")[0];
    assert.match(calculate, /MaxRandomBots == 0[\s\S]*?PLAYERBOTS_UPD_ACCOUNT_TYPE_UNASSIGN/);
    assert.match(calculate, /AddClassAccountPoolSize == 0[\s\S]*?PLAYERBOTS_UPD_ACCOUNT_TYPE_UNASSIGN/);
    assert.match(manager, /void RandomPlayerbotMgr::AssignAccountTypes\(\)[\s\S]*?rndBotTypeAccounts.clear\(\)[\s\S]*?addClassTypeAccounts.clear\(\)/);
    const classify = ({managed, type1, type2}) => type1 ? "refuse random" :
        managed && !type2 ? "refuse unresolved" : "allow";
    assert.equal(classify({managed: true, type1: false, type2: false}), "refuse unresolved");
    assert.equal(classify({managed: true, type1: false, type2: true}), "allow");
    assert.equal(classify({managed: false, type1: false, type2: false}), "allow");
});

test("group accept captures and rechecks both non-reusing login epochs", () => {
    const action = read("src/Ai/Base/Actions/AcceptInvitationAction.cpp");
    const operations = read("src/Script/WorldThr/PlayerbotOperations.h");
    const playerScript = read("src/Script/Playerbots.cpp");
    const holder = read("src/Bot/PlayerbotMgr.cpp");
    const epochs = read("src/Script/WorldThr/PlayerbotIdentityGeneration.h");
    assert.match(action, /botEpoch = epochs\.CurrentLogin\(bot->GetGUID\(\)\)/);
    assert.match(action, /inviterEpoch = epochs\.CurrentLogin\(grp->GetLeaderGUID\(\)\)/);
    assert.match(action, /invitationGeneration = epochs\.CurrentInvitation\(bot->GetGUID\(\)\)/);
    assert.match(operations, /bool IsValid\(\) const override\s*\{\s*if \(!EpochsMatch\(\)\)/);
    assert.match(operations, /bool Execute\(\) override\s*\{\s*if \(!EpochsMatch\(\)\)/);
    assert.match(operations, /epochs\.CurrentLogin\(m_botGuid\) == m_botEpoch[\s\S]*epochs\.CurrentLogin\(m_inviterGuid\) == m_inviterEpoch[\s\S]*epochs\.CurrentInvitation\(m_botGuid\) == m_invitationGeneration/);
    assert.match(playerScript, /OnPlayerLogin\(Player\* player\)[\s\S]*?if \(!player->GetSession\(\)->IsHeadless\(\)\)[\s\S]*?IssueLogin\(player->GetGUID\(\)\)/);
    assert.match(playerScript, /OnPlayerBeforeLogout\(Player\* player\)[\s\S]*?RetireLogin\(player->GetGUID\(\)\)/);
    assert.match(playerScript, /OnDestructPlayer\(Player\* player\) override\s*\{\s*PlayerbotIdentityGeneration::Instance\(\)\.RetireLogin\(player->GetGUID\(\)\)/);
    assert.match(holder, /LogoutPlayerBot\(ObjectGuid guid\)[\s\S]*?RetireLogin\(guid\)/);
    assert.match(holder, /DisablePlayerBot\(ObjectGuid guid\)[\s\S]*?RetireLogin\(guid\)/);
    assert.match(holder, /OnBotLogin\(Player\* const bot\)[\s\S]*?IssueLogin\(bot->GetGUID\(\)\)/);
    assert.match(epochs, /if \(_nextLogin == std::numeric_limits<std::uint64_t>::max\(\)\)\s*\{\s*_logins.erase\(key\);\s*return 0/);
    assert.match(epochs, /if \(_nextInvitation == std::numeric_limits<std::uint64_t>::max\(\)\)\s*\{\s*_invitations.erase\(key\);\s*return 0/);
    assert.match(epochs, /void RetireLogin\(ObjectGuid guid\)[\s\S]*?_logins.erase\(key\);\s*_invitations.erase\(key\)/);
    assert.match(playerScript, /GROUPHOOK_ON_INVITE_MEMBER[\s\S]*?OnInviteMember\(Group\* \/\*group\*\/, ObjectGuid guid\)[\s\S]*?IssueInvitation\(guid\)/);
    assert.match(playerScript, /new PlayerbotsGroupScript\(\)/);
    const login = playerScript.split("void OnPlayerLogin(Player* player) override")[1]?.split("void OnPlayerBeforeLogout")[0];
    assert.equal((login.match(/IssueLogin\(player->GetGUID\(\)\)/g) || []).length, 1);
    assert.ok(login.indexOf("if (!player->GetSession()->IsHeadless())") < login.indexOf("IssueLogin(player->GetGUID())"));
    const accept = operations.split("class GroupAcceptOperation")[1]?.split("// Group invite operation")[0];
    assert.match(accept, /m_invitationGeneration &&[\s\S]*CurrentInvitation\(m_botGuid\) == m_invitationGeneration/);
    assert.doesNotMatch(accept.replace("epochs.CurrentInvitation(m_botGuid) == m_invitationGeneration", "true"),
        /CurrentInvitation\(m_botGuid\) == m_invitationGeneration/);
});

test("epoch model refuses old bot or inviter sessions even with matching GUID and group", () => {
    let next = 1;
    const current = new Map();
    const issue = guid => { current.set(guid, next++); return current.get(guid); };
    const retire = guid => current.delete(guid);
    const eligible = op => current.get(op.bot) === op.botEpoch && current.get(op.inviter) === op.inviterEpoch;
    issue("bot"); issue("human");
    const old = {bot: "bot", inviter: "human", botEpoch: current.get("bot"), inviterEpoch: current.get("human"), group: 8};
    assert.equal(eligible(old), true);
    retire("bot");
    assert.equal(eligible(old), false); // logout before drain
    issue("bot");
    assert.equal(eligible(old), false); // same GUID and group after relog
    const fresh = {...old, botEpoch: current.get("bot")};
    assert.equal(eligible(fresh), true);
    retire("human"); issue("human");
    assert.equal(eligible(fresh), false); // inviter relog
    assert.notEqual(old.botEpoch, fresh.botEpoch); // no reuse in process lifetime
});

test("group accept model rejects canceled, duplicate, disconnected and removed-bot work", () => {
    let next = 1;
    const epochs = new Map();
    const issue = guid => epochs.set(guid, next++);
    const retire = guid => epochs.delete(guid);
    const state = {bot: "bot", inviter: "human", group: 8, connected: true, hasAI: true, member: false};
    issue(state.bot); issue(state.inviter);
    const capture = () => ({...state, botEpoch: epochs.get(state.bot), inviterEpoch: epochs.get(state.inviter)});
    const eligible = op => op.botEpoch === epochs.get(state.bot) && op.inviterEpoch === epochs.get(state.inviter) &&
        state.connected && state.hasAI && !state.member && state.group === op.group;
    const accept = op => { if (!eligible(op)) return false; state.group = null; state.member = true; return true; };

    const canceled = capture();
    state.group = null;
    assert.equal(eligible(canceled), false);
    state.group = 9;
    assert.equal(eligible(canceled), false); // a different invite cycle

    const first = capture();
    assert.equal(accept(first), true);
    assert.equal(accept(first), false); // duplicate after membership

    state.member = false; state.group = 10;
    const disconnected = capture();
    state.connected = false;
    assert.equal(eligible(disconnected), false);
    state.connected = true;
    state.hasAI = false;
    assert.equal(eligible(disconnected), false); // holder removed the bot AI
    retire(state.bot);
    assert.equal(eligible(disconnected), false); // logout before queue drain
    issue(state.bot); state.hasAI = true;
    assert.equal(eligible(disconnected), false); // same GUID after relog
});

test("direct group add checks session epochs and invite veto before mutation", () => {
    const operations = read("src/Script/WorldThr/PlayerbotOperations.h");
    const invite = operations.split("class GroupInviteOperation")[1]?.split("// Remove member from group")[0];
    assert.ok(invite);
    assert.match(invite, /m_botEpoch\(PlayerbotIdentityGeneration::Instance\(\)\.CurrentLogin\(botGuid\)\)/);
    assert.match(invite, /m_targetEpoch\(PlayerbotIdentityGeneration::Instance\(\)\.CurrentLogin\(targetGuid\)\)/);
    assert.match(invite, /bool Execute\(\) override\s*\{\s*if \(!EpochsMatch\(\)\)/);
    assert.match(invite, /bool IsValid\(\) const override\s*\{\s*if \(!EpochsMatch\(\)\)/);
    const veto = invite.indexOf("OnPlayerCanGroupInvite(bot, targetName)");
    assert.ok(veto > invite.indexOf("if (target->GetGroup())"));
    for (const mutation of ["group = new Group", "group->Create(bot)", "sGroupMgr->AddGroup(group)",
        "group->ConvertToRaid()", "group->AddMember(target)"])
        assert.ok(veto < invite.indexOf(mutation), `${mutation} must follow veto`);
    assert.doesNotMatch(invite, /OnPlayerCanGroupAccept/);
    const hasPreMutationVeto = source => source.indexOf("OnPlayerCanGroupInvite(bot, targetName)") >= 0 &&
        source.indexOf("OnPlayerCanGroupInvite(bot, targetName)") < source.indexOf("group = new Group");
    assert.equal(hasPreMutationVeto(invite), true);
    assert.equal(hasPreMutationVeto(invite.replace("OnPlayerCanGroupInvite(bot, targetName)", "true")), false);
});

test("invite preflight model delegates ordinary group policy without PB race inference", () => {
    const mayGroup = (memberEra, targetEra) => memberEra === targetEra;
    const preflight = ({enabled, enforce, targetGrouped, memberEras, targetEra}) =>
        !targetGrouped && (!enabled || !enforce || memberEras.every(era => mayGroup(era, targetEra)));
    const base = {enabled: true, enforce: true, targetGrouped: false, memberEras: [1, 1], targetEra: 1};
    assert.equal(preflight({...base, enabled: false, targetEra: 2}), true);
    assert.equal(preflight({...base, enforce: false, targetEra: 2}), true);
    assert.equal(preflight(base), true); // same era, including an ordinary personal bot
    assert.equal(preflight({...base, targetEra: 2}), false); // Vanilla to TBC
    assert.equal(preflight({...base, memberEras: [2, 3], targetEra: 2}), false); // existing member mismatch
    assert.equal(preflight({...base, targetGrouped: true}), false); // stock refusal
});

test("invitation generation model rejects same-group reinvite and later cycles", () => {
    let nextLogin = 1;
    let nextInvitation = 1;
    const logins = new Map();
    const invitations = new Map();
    const login = guid => { invitations.delete(guid); logins.set(guid, nextLogin++); };
    const retire = guid => { logins.delete(guid); invitations.delete(guid); };
    const invite = guid => invitations.set(guid, nextInvitation++);
    const capture = group => ({bot: logins.get("bot"), inviter: logins.get("human"),
        invitation: invitations.get("bot"), group});
    const valid = op => !!op.bot && !!op.inviter && !!op.invitation && op.bot === logins.get("bot") &&
        op.inviter === logins.get("human") && op.invitation === invitations.get("bot");

    login("bot"); login("human"); invite("bot");
    const first = capture("G1");
    assert.equal(valid(first), true);
    invite("bot"); // cancel followed by a successful fresh AddInvite to the same Group
    assert.equal(valid(first), false);
    assert.equal(valid(capture("G1")), true);
    const second = capture("G1");
    invite("bot"); // G1 -> G2
    assert.equal(valid(second), false);
    const third = capture("G2");
    invite("bot"); // G2 -> G1
    assert.equal(valid(third), false);
    assert.equal(valid(capture("G1")), true);
    retire("human"); login("human");
    assert.equal(valid(capture("G1")), true);
    assert.equal(valid(first), false);
    retire("bot"); login("bot");
    assert.equal(valid(capture("G1")), false); // no invitation in new login
});

test("overflow invalidates the previous value for that subject", () => {
    const max = 3;
    let next = 1;
    const current = new Map();
    const issue = guid => {
        if (next === max) { current.delete(guid); return 0; }
        const value = next++;
        current.set(guid, value);
        return value;
    };
    const first = issue("bot");
    assert.equal(first, 1);
    assert.equal(issue("bot"), 2);
    assert.equal(issue("bot"), 0);
    assert.equal(current.has("bot"), false);
    const oldValueSurvives = source => /if \(_nextLogin == std::numeric_limits<std::uint64_t>::max\(\)\)\s*\{\s*_logins.erase\(key\)/.test(source);
    const header = read("src/Script/WorldThr/PlayerbotIdentityGeneration.h");
    assert.equal(oldValueSurvives(header), true);
    assert.equal(oldValueSurvives(header.replace("_logins.erase(key);", "// erased removed")), false);
});

test("teardown audit keeps the direct AI logout path visible", () => {
    const script = read("src/Script/Playerbots.cpp");
    const ai = read("src/Bot/PlayerbotAI.cpp");
    const holder = read("src/Bot/PlayerbotMgr.cpp");
    const world = script.split("void OnUpdate(uint32 diff) override")[1];
    assert.match(script, /OnPlayerAfterUpdate\(Player\* player, uint32 diff\) override/);
    assert.match(ai, /UpdateAIInternal[\s\S]*?LogoutPlayerBot\(bot->GetGUID\(\)\)/);
    assert.match(holder, /LogoutPlayerBot\(ObjectGuid guid\)[\s\S]*?RetireLogin\(guid\)[\s\S]*?LogoutPlayer\(true\)/);
    assert.match(world, /PlayerbotWorldThreadProcessor::instance\(\)\.Update\(diff\)/);
});
