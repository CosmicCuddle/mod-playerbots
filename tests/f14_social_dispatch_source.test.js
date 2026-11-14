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
