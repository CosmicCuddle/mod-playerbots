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

// Owner decision (21 Nov 2026): wings use their own original LFG brackets. Rows are the installed
// LFGDungeons.dbc rows; entrances are the scratch world DB's lfg_dungeon_template rows. The portal
// targets (areatrigger_teleport) for SM 45/610/612/614, Maraudon 3133/3134 and Dire Maul 3183/3186/3189
// are exactly these points; Dire Maul 3184/3185/3187 and Blackrock Depths 1466 are not.
constexpr std::uint32_t BRD = 230;
constexpr std::uint32_t MARAUDON = 349;
constexpr std::uint32_t DIRE_MAUL = 429;

std::vector<Row> const BlackrockDepths = {
    {30, BRD, 0, 47, 57, true, false, false},   // Prison
    {276, BRD, 0, 51, 61, true, false, false},  // Upper City
    {287, BRD, 0, 78, 82, true, true, false},   // Coren Direbrew, seasonal
};
std::vector<Row> const Maraudon = {
    {26, MARAUDON, 0, 41, 51, true, false, false},   // Orange
    {272, MARAUDON, 0, 39, 49, true, false, false},  // Purple
    {273, MARAUDON, 0, 43, 53, true, false, false},  // Pristine Waters
};
std::vector<Row> const DireMaul = {
    {34, DIRE_MAUL, 0, 53, 63, true, false, false},  // East
    {36, DIRE_MAUL, 0, 55, 65, true, false, false},  // West
    {38, DIRE_MAUL, 0, 55, 65, true, false, false},  // North
};
std::vector<Entrance> const Entrances = {
    {18, SM, 1688.99f, 1053.48f, 18.6775f},
    {163, SM, 1610.83f, -323.433f, 18.6738f},
    {164, SM, 855.683f, 1321.5f, 18.6709f},
    {165, SM, 255.346f, -209.09f, 18.6773f},
    {HEADLESS_HORSEMAN, SM, 1797.52f, 1347.38f, 18.8876f},
    {30, BRD, 458.32f, 26.52f, -70.67f},
    {276, BRD, 458.32f, 26.52f, -70.67f},
    {26, MARAUDON, 1019.69f, -458.31f, -43.43f},
    {272, MARAUDON, 752.91f, -616.53f, -33.11f},
    {273, MARAUDON, 495.702f, 17.3372f, -96.3128f},
    {34, DIRE_MAUL, 44.4499f, -154.822f, -2.71201f},
    {36, DIRE_MAUL, -62.9658f, 159.867f, -3.46206f},
    {38, DIRE_MAUL, 255.249f, -16.0561f, -2.58737f},
};

Destination At(float x, float y, float z) { return {true, x, y, z}; }

Decision EnterWing(std::vector<Row> const& rows, std::uint32_t map, Destination destination, std::uint32_t level,
                   std::uint32_t selected = 0, bool seasonActive = false,
                   std::vector<OverrideRange> const& overrides = {})
{
    std::array<Row, 16> scratch{};
    std::span<Row const> const view = WithSeasonActivity(std::span<Row const>(rows), selected, seasonActive, scratch);
    Decision decision = Resolve(view, map, 0, selected, level);
    decision = ApplyReviewedOverride(decision, overrides, map, 0, level);
    return ResolveWing(decision, view, Entrances, map, 0, destination, level);
}

bool Is(Decision const& d, Result result, std::uint32_t min, std::uint32_t max)
{
    return d.result == result && d.minLevel == min && d.maxLevel == max;
}

void TestScarletMonasteryWings()
{
    Destination const graveyard = At(1688.99f, 1053.48f, 18.6775f);
    Destination const cathedral = At(855.683f, 1321.5f, 18.6709f);
    Destination const library = At(255.346f, -209.09f, 18.6773f);
    Destination const armory = At(1610.83f, -323.433f, 18.6738f);
    Check(Is(EnterWing(ScarletMonastery, SM, graveyard, 30), Result::Allowed, 27, 37), "SM Graveyard portal, 30");
    Check(Is(EnterWing(ScarletMonastery, SM, graveyard, 40), Result::AboveMaximum, 27, 37),
          "SM Graveyard portal, 40 is over the Graveyard bracket");
    Check(Is(EnterWing(ScarletMonastery, SM, cathedral, 30), Result::BelowMinimum, 35, 45),
          "SM Cathedral portal, 30 is under the Cathedral bracket");
    Check(Is(EnterWing(ScarletMonastery, SM, cathedral, 45), Result::Allowed, 35, 45), "SM Cathedral, 45 edge");
    Check(Is(EnterWing(ScarletMonastery, SM, cathedral, 46), Result::AboveMaximum, 35, 45), "SM Cathedral, 46");
    Check(Is(EnterWing(ScarletMonastery, SM, library, 30), Result::Allowed, 30, 40), "SM Library, 30 edge");
    Check(Is(EnterWing(ScarletMonastery, SM, library, 29), Result::BelowMinimum, 30, 40), "SM Library, 29");
    Check(Is(EnterWing(ScarletMonastery, SM, armory, 42), Result::Allowed, 32, 42), "SM Armory, 42 edge");
    Check(Is(EnterWing(ScarletMonastery, SM, At(1695.0f, 1050.0f, 19.0f), 30), Result::Allowed, 27, 37),
          "a few yards off the entrance is still the Graveyard");
    Check(Is(EnterWing(ScarletMonastery, SM, At(1700.0f, 1053.48f, 18.6775f), 40), Result::Allowed, 27, 45),
          "11 yards off is no longer an entrance: any wing");

    // A summon into the middle of the instance, or Core's pre-check without a destination.
    Destination const inside = At(1000.0f, 1000.0f, 20.0f);
    Check(Is(EnterWing(ScarletMonastery, SM, inside, 27), Result::Allowed, 27, 45), "SM inside, 27 fits a wing");
    Check(Is(EnterWing(ScarletMonastery, SM, inside, 45), Result::Allowed, 27, 45), "SM inside, 45 fits a wing");
    Check(Is(EnterWing(ScarletMonastery, SM, inside, 26), Result::BelowMinimum, 27, 45), "SM inside, 26");
    Check(Is(EnterWing(ScarletMonastery, SM, inside, 46), Result::AboveMaximum, 27, 45), "SM inside, 46");
    Check(Is(EnterWing(ScarletMonastery, SM, Destination{}, 40), Result::Allowed, 27, 45), "no destination: any wing");
    Check(Is(EnterWing(ScarletMonastery, SM, Destination{}, 80), Result::AboveMaximum, 27, 45),
          "no destination never lets a level-80 bot in");

    // The Headless Horseman entrance is seasonal: it never names a wing and never widens the range.
    Destination const horseman = At(1797.52f, 1347.38f, 18.8876f);
    Check(Is(EnterWing(ScarletMonastery, SM, horseman, 80), Result::AboveMaximum, 27, 45),
          "a level-80 bot at the holiday entrance without the LFG selection is denied");
    Check(EnterWing(ScarletMonastery, SM, horseman, 80, HEADLESS_HORSEMAN, true).result == Result::SeasonalException,
          "the LFG holiday selection still admits it");
    Check(EnterWing(ScarletMonastery, SM, horseman, 80, HEADLESS_HORSEMAN, false).result == Result::SeasonalInactive,
          "the holiday selection outside Hallow's End is still refused");

    // An exact LFG wing selection is decided before the destination is looked at.
    Check(Is(EnterWing(ScarletMonastery, SM, graveyard, 40, 164), Result::Allowed, 35, 45),
          "LFG Cathedral selection wins over the destination");
    // A reviewed override still takes precedence over the automatic wing rule.
    Check(Is(EnterWing(ScarletMonastery, SM, graveyard, 40, 0, false, {{SM, 0, 27, 45}}), Result::Allowed, 27, 45),
          "a reviewed override is applied before the wing rule");
    // Another map's entrance at the same point never names a wing here.
    Check(Is(EnterWing(Maraudon, MARAUDON, graveyard, 45), Result::Allowed, 39, 53),
          "entrances are matched on the same map only");
}

void TestOtherWingMaps()
{
    // Blackrock Depths: Prison and Upper City share one entrance, so the bot must fit either wing.
    Destination const brd = At(458.32f, 26.52f, -70.67f);
    Check(Is(EnterWing(BlackrockDepths, BRD, brd, 47), Result::Allowed, 47, 61), "BRD, 47 fits the Prison");
    Check(Is(EnterWing(BlackrockDepths, BRD, brd, 61), Result::Allowed, 47, 61), "BRD, 61 fits the Upper City");
    Check(Is(EnterWing(BlackrockDepths, BRD, brd, 46), Result::BelowMinimum, 47, 61), "BRD, 46");
    Check(Is(EnterWing(BlackrockDepths, BRD, brd, 62), Result::AboveMaximum, 47, 61), "BRD, 62");
    Check(Is(EnterWing(BlackrockDepths, BRD, brd, 80), Result::AboveMaximum, 47, 61),
          "Coren Direbrew's seasonal row never widens BRD");
    // The areatrigger 1466 target is 7.6 yards from the LFG entrance: same result.
    Check(Is(EnterWing(BlackrockDepths, BRD, At(456.929f, 34.0923f, -68.0896f), 50), Result::Allowed, 47, 61),
          "BRD portal target");

    Destination const orange = At(1019.69f, -458.31f, -43.43f);
    Destination const purple = At(752.91f, -616.53f, -33.11f);
    Check(Is(EnterWing(Maraudon, MARAUDON, orange, 40), Result::BelowMinimum, 41, 51), "Maraudon Orange, 40");
    Check(Is(EnterWing(Maraudon, MARAUDON, purple, 40), Result::Allowed, 39, 49), "Maraudon Purple, 40");
    Check(Is(EnterWing(Maraudon, MARAUDON, purple, 50), Result::AboveMaximum, 39, 49), "Maraudon Purple, 50");
    Check(Is(EnterWing(Maraudon, MARAUDON, Destination{}, 53), Result::Allowed, 39, 53),
          "Maraudon without a destination, 53 fits Pristine Waters");

    Destination const east = At(44.4499f, -154.822f, -2.71201f);
    Destination const west = At(-62.9658f, 159.867f, -3.46206f);
    Destination const north = At(255.249f, -16.0561f, -2.58737f);
    Check(Is(EnterWing(DireMaul, DIRE_MAUL, east, 54), Result::Allowed, 53, 63), "Dire Maul East, 54");
    Check(Is(EnterWing(DireMaul, DIRE_MAUL, west, 54), Result::BelowMinimum, 55, 65), "Dire Maul West, 54");
    Check(Is(EnterWing(DireMaul, DIRE_MAUL, north, 65), Result::Allowed, 55, 65), "Dire Maul North, 65");
    Check(Is(EnterWing(DireMaul, DIRE_MAUL, east, 64), Result::AboveMaximum, 53, 63), "Dire Maul East, 64");
    // Areatrigger 3184 (East, south door) is not an LFG entrance: any wing.
    Check(Is(EnterWing(DireMaul, DIRE_MAUL, At(-201.11f, -328.66f, -2.72f), 64), Result::Allowed, 53, 65),
          "Dire Maul side door: any wing");
}

void TestWingRuleBoundaries()
{
    // Resolved maps are never touched by the wing rule.
    Check(Is(EnterWing(ShadowfangKeep, SFK, At(0, 0, 0), 20), Result::Allowed, 16, 26), "SFK unchanged");
    Check(EnterWing(ShadowfangKeep, SFK, Destination{}, 60).result == Result::AboveMaximum, "SFK still denies 60");

    // Wings with a gap between them: a level in the gap is below the next wing.
    std::vector<Row> const gapped = {{1, 900, 0, 10, 20, true, false, false}, {2, 900, 0, 30, 40, true, false, false}};
    Check(Is(EnterWing(gapped, 900, Destination{}, 25), Result::BelowMinimum, 30, 40),
          "a level between two wings is below the next wing");
    Check(Is(EnterWing(gapped, 900, Destination{}, 15), Result::Allowed, 10, 40), "the lower wing fits");
    Check(Is(EnterWing(gapped, 900, Destination{}, 5), Result::BelowMinimum, 10, 40), "below every wing");

    // A wing row with no usable bracket makes the map missing, never silently allowed.
    std::vector<Row> const broken = {{1, 901, 0, 10, 20, true, false, false}, {2, 901, 0, 0, 0, true, false, false}};
    Check(EnterWing(broken, 901, Destination{}, 15).result == Result::MissingBracket, "unusable wing row");

    // Another difficulty's rows are not candidates.
    std::vector<Row> const heroic = {{1, 902, 1, 10, 20, true, false, false}, {2, 902, 1, 30, 40, true, false, false}};
    Check(Resolve(heroic, 902, 0, 0, 15).result == Result::MissingBracket &&
              ResolveWing(Resolve(heroic, 902, 0, 0, 15), heroic, Entrances, 902, 0, Destination{}, 15).result ==
                  Result::MissingBracket,
          "rows of another difficulty never resolve the wing");

    // Access requirement limits still tighten each wing, as in Resolve.
    Destination const graveyard = At(1688.99f, 1053.48f, 18.6775f);
    Check(Is(ResolveWing(Decision{Result::AmbiguousBracket}, ScarletMonastery, Entrances, SM, 0, graveyard, 36, 0, 35),
             Result::AboveMaximum, 27, 35),
          "a dungeon_access_template maximum tightens the wing");
    Check(Is(ResolveWing(Decision{Result::AmbiguousBracket}, ScarletMonastery, Entrances, SM, 0, graveyard, 30, 0, 35),
             Result::Allowed, 27, 35),
          "inside the tightened wing");
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
    TestScarletMonasteryWings();
    TestOtherWingMaps();
    TestWingRuleBoundaries();
    std::cout << "checks=" << g_checks << " failures=" << g_failures << "\n";
    return g_failures == 0 ? 0 : 1;
}
