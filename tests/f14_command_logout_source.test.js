const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");

const root = path.resolve(__dirname, "..");
const read = relative => fs.readFileSync(path.join(root, relative), "utf8");

test("chat logout queues before the caller can destroy its own AI", () => {
    const source = read("src/Bot/PlayerbotAI.cpp");
    const command = source.split('else if (filtered == "logout")').at(-1)?.split('else if (filtered == "logout cancel")')[0];
    assert.ok(command);
    assert.doesNotMatch(command, /masterBotMgr->LogoutPlayerBot\(/);
    assert.match(command, /CurrentLogin\(botGuid\)/);
    assert.match(command, /CurrentLogin\(masterGuid\)/);
    assert.match(command, /std::make_unique<BotCommandLogoutOperation>\(botGuid, masterGuid, botEpoch, masterEpoch\)/);
    assert.match(command, /QueueOperation\(std::move\(operation\)\)/);
});

test("world operation rechecks the exact bot, master and holder before teardown", () => {
    const source = read("src/Script/WorldThr/PlayerbotOperations.h");
    const operation = source.split("class BotCommandLogoutOperation : public PlayerbotOperation")[1]?.split("// Bot logout group cleanup operation")[0];
    assert.ok(operation);
    const checks = ["EpochsMatch()", "ObjectAccessor::FindPlayer(m_botGuid)",
        "ObjectAccessor::FindPlayer(m_masterGuid)", "session->GetPlayer() != bot", "session->IsLoggingOut()",
        "holder->GetPlayerBot(m_botGuid) != bot", "botAI->GetMaster() != master"];
    const logout = operation.indexOf("holder->LogoutPlayerBot(m_botGuid)");
    for (const check of checks) {
        assert.ok(operation.indexOf(check) >= 0 && operation.indexOf(check) < logout, check);
    }
    assert.doesNotMatch(operation, /Player\* m_|WorldSession\* m_|PlayerbotMgr\* m_/);
    assert.match(operation, /CurrentLogin\(m_botGuid\) == m_botEpoch/);
    assert.match(operation, /CurrentLogin\(m_masterGuid\) == m_masterEpoch/);
});

test("logout intent model refuses stale relog and holder transfer", () => {
    const accepts = (intent, now) => intent.botEpoch && intent.masterEpoch &&
        intent.botEpoch === now.botEpoch && intent.masterEpoch === now.masterEpoch &&
        now.session && !now.loggingOut && now.holderOwnsBot && now.sameMaster;
    const intent = {botEpoch: 8, masterEpoch: 3};
    const now = {botEpoch: 8, masterEpoch: 3, session: true, loggingOut: false,
        holderOwnsBot: true, sameMaster: true};
    assert.equal(accepts(intent, now), true);
    for (const changed of [{botEpoch: 9}, {masterEpoch: 4}, {session: false}, {loggingOut: true},
        {holderOwnsBot: false}, {sameMaster: false}]) {
        assert.equal(accepts(intent, {...now, ...changed}), false);
    }
});
