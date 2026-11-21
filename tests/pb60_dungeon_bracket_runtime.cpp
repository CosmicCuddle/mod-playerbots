// Runtime pieces of the bot dungeon bracket (coordination #60 / C3): the owner-reviewed override for
// ambiguous maps, holiday activity for an exact LFG selection, and the override list parser.
// Rows are the installed LFGDungeons.dbc rows for Scarlet Monastery (map 189) and Shadowfang Keep (33).
#include <iostream>
#include <string>
#include <vector>

#include "DungeonLevelBracket.h"

namespace
{
using namespace PlayerbotsDungeonBracket;

int g_checks = 0;
int g_failures = 0;

void Check(bool ok, std::string const& what)
{
    ++g_checks;
    if (!ok)
    {
        ++g_failures;
        std::cerr << "FAIL: " << what << "\n";
    }
}

constexpr std::uint32_t SM = 189;
constexpr std::uint32_t SFK = 33;
constexpr std::uint32_t HEADLESS_HORSEMAN = 285;
constexpr std::uint32_t CROWN_CHEMICAL = 288;

std::vector<Row> const ScarletMonastery = {
    {18, SM, 0, 27, 37, true, false, false},                // Graveyard
    {163, SM, 0, 32, 42, true, false, false},               // Armory
    {164, SM, 0, 35, 45, true, false, false},               // Cathedral
    {165, SM, 0, 30, 40, true, false, false},               // Library
    {HEADLESS_HORSEMAN, SM, 0, 78, 82, true, true, false},  // seasonal
};
std::vector<Row> const ShadowfangKeep = {
    {8, SFK, 0, 16, 26, true, false, false},
    {CROWN_CHEMICAL, SFK, 0, 78, 82, true, true, false},
};

Decision Enter(std::vector<Row> const& rows, std::uint32_t map, std::uint32_t selected, bool seasonActive,
               std::uint32_t level, std::vector<OverrideRange> const& overrides = {})
{
    std::array<Row, 16> scratch{};
    std::span<Row const> const view = WithSeasonActivity(std::span<Row const>(rows), selected, seasonActive, scratch);
    Decision const decision = Resolve(view, map, 0, selected, level);
    return ApplyReviewedOverride(decision, overrides, map, 0, level);
}

void TestOrdinaryEntrance()
{
    Check(Enter(ShadowfangKeep, SFK, 0, false, 20).result == Result::Allowed, "SFK portal, level 20 allowed");
    Check(Enter(ShadowfangKeep, SFK, 0, false, 15).result == Result::BelowMinimum, "SFK portal, level 15 denied");
    Check(Enter(ShadowfangKeep, SFK, 0, false, 60).result == Result::AboveMaximum, "SFK portal, level 60 denied");
    Check(Enter(ShadowfangKeep, SFK, 0, true, 80).result == Result::AboveMaximum,
          "an active holiday without an exact selection never exempts the ordinary map");
    Check(Enter(ScarletMonastery, SM, 0, false, 35).result == Result::AmbiguousBracket,
          "SM portal without selection or override is ambiguous");
}

void TestLfgSelection()
{
    Check(Enter(ScarletMonastery, SM, 165, false, 35).result == Result::Allowed, "LFG Library selection, level 35");
    Check(Enter(ScarletMonastery, SM, 18, false, 40).result == Result::AboveMaximum,
          "LFG Graveyard selection, level 40 is over 37");
    Check(Enter(ScarletMonastery, SM, HEADLESS_HORSEMAN, true, 80).result == Result::SeasonalException,
          "Headless Horseman selected during Hallow's End");
    Check(Enter(ScarletMonastery, SM, HEADLESS_HORSEMAN, false, 80).result == Result::SeasonalInactive,
          "Headless Horseman selected outside the event");
    Check(Enter(ShadowfangKeep, SFK, CROWN_CHEMICAL, true, 80).result == Result::SeasonalException,
          "Crown Chemical Co. selected during Love is in the Air");
    Check(Enter(ShadowfangKeep, SFK, 8, true, 80).result == Result::AboveMaximum,
          "an active holiday does not exempt the ordinary SFK selection");
    Check(Enter(ShadowfangKeep, SFK, 999, true, 80).result == Result::AboveMaximum,
          "an unmatched selection falls back to the ordinary rows");
}

void TestSeasonActivityCopy()
{
    std::array<Row, 16> scratch{};
    auto const none = WithSeasonActivity(std::span<Row const>(ScarletMonastery), 0, true, scratch);
    bool anyActive = false;
    for (Row const& row : none)
        anyActive = anyActive || row.seasonActive;
    Check(!anyActive, "no selection leaves every row inactive");
    auto const selected = WithSeasonActivity(std::span<Row const>(ScarletMonastery), HEADLESS_HORSEMAN, true, scratch);
    int active = 0;
    for (Row const& row : selected)
        active += row.seasonActive ? 1 : 0;
    Check(active == 1 && selected.size() == ScarletMonastery.size(), "only the selected seasonal row is active");
    auto const ordinary = WithSeasonActivity(std::span<Row const>(ScarletMonastery), 165, true, scratch);
    active = 0;
    for (Row const& row : ordinary)
        active += row.seasonActive ? 1 : 0;
    Check(active == 0, "selecting an ordinary row never activates a seasonal one");
    std::array<Row, 2> tiny{};
    auto const tooMany = WithSeasonActivity(std::span<Row const>(ScarletMonastery), HEADLESS_HORSEMAN, true, tiny);
    Check(tooMany.data() == ScarletMonastery.data(), "too many rows to copy keeps the inactive originals");
}

void TestReviewedOverride()
{
    std::vector<OverrideRange> const overrides = {{SM, 0, 27, 45}, {SFK, 0, 1, 80}};
    Check(Enter(ScarletMonastery, SM, 0, false, 35, overrides).result == Result::Allowed,
          "SM override allows level 35");
    Check(Enter(ScarletMonastery, SM, 0, false, 26, overrides).result == Result::BelowMinimum,
          "SM override still denies level 26");
    Decision const over = Enter(ScarletMonastery, SM, 0, false, 46, overrides);
    Check(over.result == Result::AboveMaximum && over.minLevel == 27 && over.maxLevel == 45,
          "SM override denies level 46 and reports 27-45");
    Check(Enter(ShadowfangKeep, SFK, 0, false, 60, overrides).result == Result::AboveMaximum,
          "an override never widens a map the data already resolves");
    Check(Enter(ScarletMonastery, SM, 18, false, 40, overrides).result == Result::AboveMaximum,
          "an exact LFG selection is not replaced by the override");
    Check(Enter(ScarletMonastery, SM, 0, false, 35, {{SM, 1, 27, 45}}).result == Result::AmbiguousBracket,
          "an override for another difficulty does not apply");
    Check(Enter(ScarletMonastery, SM, 0, false, 35, {{SM, 0, 45, 27}}).result == Result::AmbiguousBracket,
          "an inverted override is ignored");
    Check(
        Resolve({}, 999, 0, 0, 30).result == Result::MissingBracket &&
            ApplyReviewedOverride(Resolve({}, 999, 0, 0, 30), std::vector<OverrideRange>{{999, 0, 20, 40}}, 999, 0, 30)
                    .result == Result::Allowed,
        "a map with no rows can be covered by a reviewed override");
}

void TestParser()
{
    std::vector<OverrideRange> out;
    Check(ParseOverrides("", out) && out.empty(), "empty list parses to nothing");
    Check(ParseOverrides("189:0:27:45, 230:0:47:61 ,349:0:39:53,429:0:53:65,", out) && out.size() == 4 &&
              out[1].mapId == 230 && out[1].minLevel == 47 && out[3].maxLevel == 65,
          "four reviewed maps parse with spaces and a trailing comma");
    Check(!ParseOverrides("189:0:27", out) && out.empty(), "missing field rejects the list");
    Check(!ParseOverrides("189:0:27:45:1", out) && out.empty(), "extra field rejects the list");
    Check(!ParseOverrides("189:0:0:45", out) && out.empty(), "zero minimum rejects the list");
    Check(!ParseOverrides("189:0:45:27", out) && out.empty(), "inverted range rejects the list");
    Check(!ParseOverrides("189:0:27:45,189:0:30:40", out) && out.empty(), "duplicate map/difficulty rejects");
    Check(!ParseOverrides("189:0:-1:45", out) && out.empty(), "negative number rejects the list");
    Check(!ParseOverrides("189:0:27:4500000", out) && out.empty(), "absurd number rejects the list");
    Check(!ParseOverrides("189:0:27:45,abc", out) && out.empty(), "one bad entry rejects the whole list");
    Check(ParseOverrides("189:0:27:45,189:1:27:45", out) && out.size() == 2, "same map, other difficulty is fine");
}

void TestTeleportSeam()
{
    Check(!TeleportNeedsBracketCheck(false, false), "normal cross-map teleport is checked by CanEnterMap instead");
    Check(TeleportNeedsBracketCheck(false, true), "GM-mode cross-map teleport skips Core, so it is checked here");
    Check(!TeleportNeedsBracketCheck(true, false),
          "teleport inside the dungeon the bot already entered is not re-judged");
    Check(!TeleportNeedsBracketCheck(true, true), "same-map GM teleport inside the dungeon is not re-judged");
}
}  // namespace

int main()
{
    TestOrdinaryEntrance();
    TestLfgSelection();
    TestSeasonActivityCopy();
    TestReviewedOverride();
    TestParser();
    TestTeleportSeam();
    std::cout << "checks=" << g_checks << " failures=" << g_failures << "\n";
    return g_failures == 0 ? 0 : 1;
}
