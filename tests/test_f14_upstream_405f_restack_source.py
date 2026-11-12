"""Source contract for the Playerbots upstream-405f F14 preservation restack.

This test deliberately avoids historical branch/blob pins. It proves the
current restack keeps upstream count-mode/downsize architecture while carrying
only the registered CosmicCuddle F14 layer.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def read(path):
    return (ROOT / path).read_text()

def require(ok, message):
    if not ok:
        raise SystemExit(message)

conf = read("conf/playerbots.conf.dist")
cfg_cpp = read("src/PlayerbotAIConfig.cpp")
cfg_h = read("src/PlayerbotAIConfig.h")
mgr = read("src/Bot/RandomPlayerbotMgr.cpp")
mgr_h = read("src/Bot/RandomPlayerbotMgr.h")
factory = read("src/Bot/Factory/PlayerbotFactory.cpp")
random_factory = read("src/Bot/Factory/RandomPlayerbotFactory.cpp")
holder = read("src/Bot/PlayerbotMgr.cpp")

for needle in (
    "Playerbots.RandomBotCountMode = 0",
    "RandomBotCountMode",
    "RandomBotCountChangeMinInterval",
    "RandomBotCountChangeMaxInterval",
    "RandomBotCountMinTime",
    "RandomBotCountMaxTime",
    "RemoveRandomBots()",
    "GetScheduledBotCount()",
    "IsRemovableBot(Player* bot)",
):
    require(needle in conf + cfg_cpp + cfg_h + mgr + mgr_h,
            "missing upstream count-mode/downsize contract: " + needle)

for forbidden in (
    "EnablePeriodicOnlineOffline",
    "PeriodicOnlineOfflineRatio",
    "MinRandomBotInWorldTime",
    "MaxRandomBotInWorldTime",
    "PermanentlyInWorldTime",
):
    require(forbidden not in conf + cfg_cpp + cfg_h + mgr + mgr_h,
            "obsolete population knob was reintroduced: " + forbidden)

for needle in (
    "Playerbots.F14TrackNewRandomBotBirth = 0",
    "Playerbots.F14AuditNewBirthCommitReadback = 0",
    "Playerbots.F14HoldTbcRaceRandomBotsDuringVanilla = 0",
    "Playerbots.ResetQuestsPreserveQuestIds =",
    'GetOption<bool>("Playerbots.F14TrackNewRandomBotBirth", false)',
    'GetOption<bool>("Playerbots.F14AuditNewBirthCommitReadback", false)',
    'GetOption<bool>("Playerbots.F14HoldTbcRaceRandomBotsDuringVanilla", false)',
):
    require(needle in conf + cfg_cpp, "F14 default-OFF contract missing: " + needle)

require(mgr.count("CountF14ScheduledRandomBotsForQuota(") >= 4,
        "effective scheduled count is not shared across scheduler/add/remove")
require("uint32 const availableBotCount = static_cast<uint32>(" in mgr,
        "outer scheduler does not use effective count")
require(mgr.count("uint32 const eligibleScheduledBotCount = static_cast<uint32>(") == 2,
        "Add/Remove do not each compute the effective eligible count")

require('SetEventValue(charInfo.guid, "add", 1, YEAR);' in mgr,
        "current upstream YEAR add-event lifetime was not preserved")

held = mgr.find("if (manualVanillaHold &&\n            IsF14PositivelyHeldScheduledRandomBot(")
alliance = mgr.find("bool isAlliance = IsAlliance(bot->getRace());", held)
require(held >= 0 and alliance > held,
        "held-bot exclusion is not before faction downsize accounting")

for needle in (
    "bot->IsInCombat()",
    "UNIT_STATE_IN_FLIGHT",
    "bot->IsBeingTeleported()",
    "bot->InBattleground()",
    "bot->InBattlefield()",
    "bot->InArena()",
    "bot->InBattlegroundQueue()",
    "bot->GetMap()->IsDungeon()",
    "botAI->HasGameClientMaster()",
    "IsRealPlayer(member) || IsSelfBot(member)",
    'SetEventValue(bot, "add", 0, 0);',
    "currentBots.erase(bot);",
    "LogoutPlayerBot(guid);",
):
    require(needle in mgr, "upstream downsize safety was lost: " + needle)

for needle in (
    "IsF14PositivelyHeldScheduledRandomBot",
    "ShouldHoldF14PendingRandomBotLogin",
    "CosmicF14VanillaTbcPopulationHold::Decide",
    "CosmicF14VanillaPendingRandomBotLogin::ShouldHold",
    "fromAssignedRandomBotPool",
    "currentBots.contains(guid.GetCounter())",
):
    require(needle in mgr + mgr_h, "F14 hold identity boundary missing: " + needle)

for needle in (
    "ResetQuestsPreserveMask.load(std::memory_order_acquire)",
    "QUEST_STATUS_REWARDED",
    "F14NewRandomBotBirthProbe.h",
    "F14AuditNewBirthCommitReadback",
    "AsyncCommitTransaction",
    "MarkCommittedRowReadback",
    "RecordCandidate",
    "ObserveAdmission",
    "NewRandomBotBirthProbe::Instance().Retire",
):
    require(needle in factory + random_factory + holder,
            "registered custom behavior missing after restack: " + needle)

for forbidden in (
    "Store::Reserve",
    "Store::Commit",
    "DestroyForPlayer",
    "SetPhaseMask",
):
    require(forbidden not in mgr + factory + random_factory + holder,
            "positive/visibility authority appeared in Playerbots runtime: " + forbidden)

print("PASS Playerbots upstream-405f F14 preservation restack source contract")
