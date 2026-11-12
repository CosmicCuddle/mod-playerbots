#!/usr/bin/env python3
"""F14 creation probe: actual integration source, default-OFF, no state writes."""
from pathlib import Path
import unittest
ROOT = Path(__file__).resolve().parents[1]
def read(path):
    return (ROOT / path).read_text(encoding="utf-8")

class FirstBirthProbe(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.h = read("src/Bot/F14NewRandomBotBirthProbe.h")
        cls.factory = read("src/Bot/Factory/RandomPlayerbotFactory.cpp")
        cls.holder = read("src/Bot/PlayerbotMgr.cpp")
        cls.c_h = read("src/PlayerbotAIConfig.h")
        cls.c_cpp = read("src/PlayerbotAIConfig.cpp")
        cls.config = read("conf/playerbots.conf.dist")
        cls.test = read("tests/f14_new_randombot_birth_probe.cpp")

    def test_default_off_loaded_from_config_and_dist(self):
        self.assertIn("bool F14TrackNewRandomBotBirth = false;", self.c_h)
        self.assertIn('GetOption<bool>("Playerbots.F14TrackNewRandomBotBirth", false)', self.c_cpp)
        self.assertIn("Playerbots.F14TrackNewRandomBotBirth = 0", self.config)
        self.assertNotIn("Playerbots.F14TrackNewRandomBotBirth = 1", self.config)

    def test_factory_records_only_after_save_and_cache(self):
        start = self.factory.index("playerBot->SaveToDB(true, false);")
        end = self.factory.index("playerBot->CleanupsBeforeDelete();", start)
        segment = self.factory[start:end]
        self.assertLess(segment.index("SaveToDB(true, false);"),
                        segment.index("sCharacterCache->AddCharacterCacheEntry("))
        self.assertLess(segment.index("sCharacterCache->AddCharacterCacheEntry("),
                        segment.index("RecordCandidate("))
        self.assertIn("if (sPlayerbotAIConfig.F14TrackNewRandomBotBirth)", segment)
        self.assertIn("playerBot->GetGUID().GetCounter(), accountId, playerBot->getRace()", segment)

    def test_login_checks_ai_before_observation(self):
        bot = self.holder.split("void PlayerbotHolder::OnBotLogin(Player* const bot)",1)[1].split(
            "void PlayerbotHolder::",1)[0]
        for marker in ("OnBotLoginInternal(bot);", "PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);",
                       "if (!botAI)", "bot->GetSession()->IsHeadless()",
                       "bot->IsInWorld()", "sRandomPlayerbotMgr.IsRandomBot(bot)",
                       "ObserveAdmission("):
            self.assertIn(marker, bot)
        self.assertLess(bot.index("if (!botAI)"), bot.index("ObserveAdmission("))
        self.assertLess(bot.index("sRandomPlayerbotMgr.IsRandomBot(bot)"),
                        bot.index("ObserveAdmission("))
        self.assertIn("if (sPlayerbotAIConfig.F14TrackNewRandomBotBirth", bot)

    def test_logout_retires_before_any_player_deletion(self):
        stop = self.holder.split("void PlayerbotHolder::LogoutPlayerBot(ObjectGuid guid)",1)[1].split(
            "void PlayerbotHolder::DisablePlayerBot(ObjectGuid guid)",1)[0]
        self.assertLess(stop.index("Retire(guid.GetCounter())"),stop.index("GetPlayerBot(guid)"))
        self.assertLess(stop.index("Retire(guid.GetCounter())"),stop.index("LogoutPlayer(true)"))
        disable = self.holder.split("void PlayerbotHolder::DisablePlayerBot(ObjectGuid guid)",1)[1].split(
            "void PlayerbotHolder::RemoveFromPlayerbotsMap(ObjectGuid guid)",1)[0]
        self.assertLess(disable.index("Retire(guid.GetCounter())"),disable.index("GetPlayerBot(guid)"))
        erase = self.holder.split("void PlayerbotHolder::RemoveFromPlayerbotsMap(ObjectGuid guid)",1)[1].split(
            "Player* PlayerbotHolder::GetPlayerBot(",1)[0]
        self.assertLess(erase.index("Retire(guid.GetCounter())"),erase.index("playerBots.erase(guid);"))

    def test_identity_requires_birth_and_is_one_use(self):
        for marker in ("_records.find(guid)", "it->second.Account != account",
                       "it->second.Race != race", "it->second.Consumed",
                       "it->second.Retired", "it->second.Consumed = true",
                       "it->second.Retired = true", "MaxCandidates = 16384",
                       "race == 10 || race == 11"):
            self.assertIn(marker,self.h)
        self.assertIn("eight worker admits exactly once",self.test)

    def test_no_stage_grant_visibility_database_or_live_client(self):
        for path in ("src/Bot/F14NewRandomBotBirthProbe.h",):
            contents = read(path)
            for needle in ("ForceUpdateProgressionState(", "RewardQuest(", "SaveToDB(",
                           "GetSession(", "SetPhaseMask(", "SendUpdateToPlayer("):
                self.assertNotIn(needle, contents.split("namespace CosmicF14",1)[1])
        self.assertNotIn("ForceUpdateProgressionState(", self.holder)
        self.assertNotIn("ForceUpdateProgressionState(", self.factory)

if __name__ == "__main__":
    unittest.main()
