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

test("personal-bot commands reject authoritative RandomBot account types before login", () => {
    const manager = read("src/Bot/PlayerbotMgr.cpp");
    const helper = manager.split("static std::string PersonalBotAccountRejection")[1]?.split("std::string const PlayerbotHolder::ProcessBotCommand")[0];
    assert.ok(helper);
    assert.match(helper, /rndBotTypeAccounts/);
    assert.match(helper, /IsRandomBot\(guid.GetCounter\(\)\)/);
    assert.match(helper, /addClassTypeAccounts/);
    assert.match(helper, /bot account classification unavailable/);
    assert.ok(!/getRace\(|GetRace\(/.test(helper));
    const command = manager.split("std::string const PlayerbotHolder::ProcessBotCommand")[1].split("std::vector<std::string> PlayerbotHolder::HandlePlayerbotCommand")[0];
    assert.ok(command.indexOf("PersonalBotAccountRejection(accountId, guid)") < command.indexOf("AddPlayerBot(guid, masterAccountId)"));
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

test("group accept captures and rechecks both non-reusing login epochs", () => {
    const action = read("src/Ai/Base/Actions/AcceptInvitationAction.cpp");
    const operations = read("src/Script/WorldThr/PlayerbotOperations.h");
    const playerScript = read("src/Script/Playerbots.cpp");
    const holder = read("src/Bot/PlayerbotMgr.cpp");
    const epochs = read("src/Script/WorldThr/GroupAcceptSessionEpoch.h");
    assert.match(action, /botEpoch = epochs\.Current\(bot->GetGUID\(\)\)/);
    assert.match(action, /inviterEpoch = epochs\.Current\(grp->GetLeaderGUID\(\)\)/);
    assert.match(operations, /bool IsValid\(\) const override\s*\{\s*if \(!EpochsMatch\(\)\)/);
    assert.match(operations, /bool Execute\(\) override\s*\{\s*if \(!EpochsMatch\(\)\)/);
    assert.match(operations, /epochs\.Current\(m_botGuid\) == m_botEpoch[\s\S]*epochs\.Current\(m_inviterGuid\) == m_inviterEpoch/);
    assert.match(playerScript, /OnPlayerLogin\(Player\* player\)[\s\S]*?Issue\(player->GetGUID\(\)\)/);
    assert.match(playerScript, /OnPlayerBeforeLogout\(Player\* player\)[\s\S]*?Retire\(player->GetGUID\(\)\)/);
    assert.match(holder, /LogoutPlayerBot\(ObjectGuid guid\)[\s\S]*?Retire\(guid\)/);
    assert.match(holder, /DisablePlayerBot\(ObjectGuid guid\)[\s\S]*?Retire\(guid\)/);
    assert.match(holder, /OnBotLogin\(Player\* const bot\)[\s\S]*?Issue\(bot->GetGUID\(\)\)/);
    assert.match(epochs, /if \(_next == std::numeric_limits<std::uint64_t>::max\(\)\)\s*return 0/);
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
