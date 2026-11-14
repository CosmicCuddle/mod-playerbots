const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");

const root = path.resolve(__dirname, "..");
const read = relative => fs.readFileSync(path.join(root, relative), "utf8");

test("bot guild chat queues values instead of broadcasting on a map worker", () => {
    const source = read("src/Bot/PlayerbotAI.cpp");
    const send = source.split("bool PlayerbotAI::SayToGuild(")[1]?.split("bool PlayerbotAI::SayToWorld(")[0];
    assert.ok(send);
    assert.doesNotMatch(send, /BroadcastToGuild\(/);
    assert.doesNotMatch(send, /CMSG_MESSAGECHAT|QueuePacket/);
    assert.match(send, /CurrentLogin\(botGuid\)/);
    assert.match(send, /std::make_unique<GuildChatOperation>\(botGuid, bot->GetGuildId\(\), botEpoch, msg\)/);
    assert.match(send, /QueueOperation\(std::move\(operation\)\)/);
});

test("guild chat operation rechecks the live identity and guild before Core filtering", () => {
    const source = read("src/Script/WorldThr/PlayerbotOperations.h");
    const operation = source.split("class GuildChatOperation : public PlayerbotOperation")[1]?.split("class GroupAcceptOperation")[0];
    assert.ok(operation);
    for (const guard of ["EpochMatches()", "ObjectAccessor::FindPlayer(m_botGuid)",
        "session->GetPlayer() != bot", "bot->GetGuildId() != m_guildId",
        "guild->GetMember(m_botGuid)", "guild->HasRankRight(bot, GR_RIGHT_GCHATSPEAK)"]) {
        assert.ok(operation.includes(guard), guard);
    }
    const preflight = operation.indexOf("OnPlayerCanUseChat(bot, CHAT_MSG_GUILD, LANG_UNIVERSAL, message, guild)");
    const broadcast = operation.indexOf("guild->BroadcastToGuild(session, false, message.c_str(), LANG_UNIVERSAL)");
    assert.ok(preflight >= 0 && preflight < broadcast);
    assert.doesNotMatch(operation, /CHAT_MSG_OFFICER|LANG_ADDON|CMSG_MESSAGECHAT|QueuePacket/,
        "guild operation must not become an officer, addon, or fake client transport");
    assert.match(operation, /OnPlayerCanUseChat[^;]+\|\| message\.empty\(\)\)/);
    assert.doesNotMatch(operation, /Player\* m_|WorldSession\* m_|Guild\* m_/);
});

test("guild chat lifetime model rejects relog, guild change and veto before delivery", () => {
    const deliver = (queued, live) => queued.epoch !== 0 && queued.epoch === live.epoch &&
        live.session && live.guildId === queued.guildId && live.member && live.speak && live.allow &&
        live.message.length > 0;
    const queued = {epoch: 3, guildId: 17};
    const good = {epoch: 3, session: true, guildId: 17, member: true, speak: true, allow: true, message: "hi"};
    assert.equal(deliver(queued, good), true);
    for (const changed of [{epoch: 4}, {session: false}, {guildId: 18}, {member: false},
        {speak: false}, {allow: false}, {message: ""}]) {
        assert.equal(deliver(queued, {...good, ...changed}), false);
    }
});
