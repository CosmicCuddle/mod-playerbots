const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");

const root = path.resolve(__dirname, "..");
const read = relative => fs.readFileSync(path.join(root, relative), "utf8");

test("bot guild accept leaves AI and keeps the decline path unchanged", () => {
    const source = read("src/Ai/Base/Actions/GuildAcceptAction.cpp");
    const accept = source.split("if (accept)")[1]?.split("else")[0];
    assert.ok(accept);
    assert.doesNotMatch(accept, /HandleGuildAcceptOpcode\(/);
    assert.match(accept, /invitedGuildId != guildId/);
    assert.match(accept, /CurrentLogin\(bot->GetGUID\(\)\)/);
    assert.match(accept, /std::make_unique<GuildAcceptOperation>\(bot->GetGUID\(\), invitedGuildId, botEpoch\)/);
    assert.match(accept, /QueueOperation\(std::move\(operation\)\)/);
    assert.match(source, /else\s*\{\s*WorldPackets::Guild::GuildDeclineInvitation data = WorldPacket\(CMSG_GUILD_DECLINE\);\s*bot->GetSession\(\)->HandleGuildDeclineOpcode\(data\)/);
});

test("world operation rejects stale bot or changed guild invitation before Core handler", () => {
    const source = read("src/Script/WorldThr/PlayerbotOperations.h");
    const operation = source.split("class GuildAcceptOperation : public PlayerbotOperation")[1]?.split("class GroupAcceptOperation")[0];
    assert.ok(operation);
    const handler = operation.indexOf("session->HandleGuildAcceptOpcode(data)");
    for (const guard of ["EpochMatches()", "ObjectAccessor::FindPlayer(m_botGuid)",
        "session->GetPlayer() != bot", "bot->GetGuildId()", "bot->GetGuildIdInvited() != m_guildId"]) {
        assert.ok(operation.indexOf(guard) >= 0 && operation.indexOf(guard) < handler, guard);
    }
    assert.match(operation, /CurrentLogin\(m_botGuid\) == m_botEpoch/);
    assert.match(operation, /WorldPacket\(CMSG_GUILD_ACCEPT\)/);
    assert.doesNotMatch(operation, /Player\* m_|WorldSession\* m_|Guild\* m_/);
});

test("guild accept intent model refuses relog, changed invite and existing membership", () => {
    const accepts = (intent, now) => intent.epoch !== 0 && intent.epoch === now.epoch &&
        now.session && now.guildId === 0 && now.invitedGuildId === intent.invitedGuildId;
    const intent = {epoch: 8, invitedGuildId: 42};
    const now = {epoch: 8, session: true, guildId: 0, invitedGuildId: 42};
    assert.equal(accepts(intent, now), true);
    for (const changed of [{epoch: 9}, {session: false}, {guildId: 42}, {invitedGuildId: 0},
        {invitedGuildId: 43}]) {
        assert.equal(accepts(intent, {...now, ...changed}), false);
    }
});
