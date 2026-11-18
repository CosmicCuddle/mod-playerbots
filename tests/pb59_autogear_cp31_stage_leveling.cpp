// CP31: "still leveling" follows the commander's IP stage (owner decision
// 18 Nov 2026 13:06 UK). Stages 0-7 below 60, TBC 8-12 below 70, Wrath 13-18
// below 80. Stage 11 stays refused. Unknown level or stage preserves.
#include "AutoGearCommandRoute.h"
#include <iostream>
#include <string>
#include <vector>

using namespace PlayerbotsAutoGearSelection;
using PlayerbotsAutoGearWeapons::Bit;
using PlayerbotsAutoGearWeapons::Kind;

namespace
{
int g_checks = 0;

bool Check(bool condition, std::string const& text)
{
    ++g_checks;
    if (!condition)
    {
        std::cerr << "FAIL " << text << "\n";
        return false;
    }
    return true;
}

std::uint32_t ScoreOf(std::uint32_t id, std::int32_t, std::uint32_t) { return id; }

ItemFact Chest(std::uint32_t id, std::uint32_t itemLevel)
{
    ItemFact fact;
    fact.id = id;
    fact.itemLevel = itemLevel;
    fact.requiredLevel = 1;
    fact.slot = 4;
    fact.armorSubtype = 1;
    fact.sourceKnown = true;
    fact.metadataKnown = true;
    fact.equipKnown = true;
    fact.equipAllowed = true;
    fact.scoreKnown = true;
    fact.setEvidenceKnown = true;
    return fact;
}

RouteRequest Command(std::uint8_t stage, std::uint32_t level, bool bis, std::vector<ItemFact> const& facts)
{
    RouteRequest request;
    request.commandEnabled = true;
    request.ipCapEnabled = true;
    request.bis = bis;
    request.populationKnown = true;
    request.population = Population::RandomBot;
    request.playerContextVerified = true;
    request.progressionContextVerified = true;
    request.rawStage = stage;
    request.effectiveStage = stage;
    request.qualityLimit = 4;
    request.senderLevelKnown = true;
    request.senderLevel = level;
    // A caller's old level < 60 claim must not matter once the level is known.
    request.senderIsLeveling = level < 60;
    request.senderFixedKnown = true;
    request.senderFixedAverage = 100;
    BotView bot;
    bot.levelKnown = true;
    bot.level = 80;
    bot.classKnown = true;
    bot.classId = 8;
    bot.specKnown = true;
    bot.roleKnown = true;
    bot.role = Role::Caster;
    bot.armorKnown = true;
    bot.armorMask = std::uint32_t{1} << 1;
    bot.weaponsKnown = true;
    bot.weapons.mainhand = Bit(Kind::OneHand);
    request.selection.bot = bot;
    request.selection.facts = facts;
    request.selection.eligibleMask = 1u << 4;
    request.selection.requiredMask = 1u << 4;
    request.selection.completeMask = 1u << 4;
    request.selection.authorityValid = true;
    request.selection.bags = BagSpace::Enough;
    return request;
}

struct Case
{
    std::uint8_t stage;
    std::uint32_t level;
    SenderPhase phase;
};
}  // namespace

static_assert(StageMaxLevel(7) == 60 && StageMaxLevel(8) == 70 && StageMaxLevel(12) == 70);
static_assert(StageMaxLevel(11) == 0 && StageMaxLevel(13) == 80 && StageMaxLevel(18) == 80 && StageMaxLevel(19) == 0);
static_assert(SenderPhaseForStage(true, 65, true, 8) == SenderPhase::Leveling);

int main()
{
    bool ok = true;
    Case const cases[] = {
        {0, 59, SenderPhase::Leveling},  {0, 60, SenderPhase::MaxLevel},  {7, 59, SenderPhase::Leveling},
        {7, 60, SenderPhase::MaxLevel},  {7, 70, SenderPhase::MaxLevel},  {8, 59, SenderPhase::Leveling},
        {8, 60, SenderPhase::Leveling},  {8, 65, SenderPhase::Leveling},  {8, 69, SenderPhase::Leveling},
        {8, 70, SenderPhase::MaxLevel},  {12, 69, SenderPhase::Leveling}, {12, 70, SenderPhase::MaxLevel},
        {13, 69, SenderPhase::Leveling}, {13, 70, SenderPhase::Leveling}, {13, 79, SenderPhase::Leveling},
        {13, 80, SenderPhase::MaxLevel}, {18, 79, SenderPhase::Leveling}, {18, 80, SenderPhase::MaxLevel},
        {11, 70, SenderPhase::Unknown},  {11, 65, SenderPhase::Unknown},  {19, 80, SenderPhase::Unknown},
        {8, 0, SenderPhase::Unknown}};
    for (Case const& c : cases)
    {
        std::string const tag = "stage " + std::to_string(c.stage) + " level " + std::to_string(c.level);
        ok = Check(SenderPhaseForStage(true, c.level, true, c.stage) == c.phase, tag + " phase") && ok;
    }
    ok = Check(SenderPhaseForStage(false, 70, true, 8) == SenderPhase::Unknown, "unknown level is Unknown") && ok;
    ok = Check(SenderPhaseForStage(true, 70, false, 8) == SenderPhase::Unknown, "unknown stage is Unknown") && ok;

    // Route: leveling masters use the leveling cap (min of stage cap and their
    // average); max-level masters get the stage cap; Unknown preserves.
    std::vector<ItemFact> facts{Chest(60, 60), Chest(92, 92), Chest(100, 100), Chest(125, 125), Chest(156, 156), Chest(226, 226),
        Chest(284, 284)};
    struct RouteCase
    {
        std::uint8_t stage;
        std::uint32_t level;
        bool leveling;
        std::uint32_t stageCap;
    };
    RouteCase const routes[] = {{7, 59, true, 92}, {7, 60, false, 92}, {8, 59, true, 125}, {8, 65, true, 125},
        {8, 69, true, 125}, {8, 70, false, 125}, {12, 69, true, 156}, {12, 70, false, 156}, {13, 70, true, 226},
        {13, 79, true, 226}, {13, 80, false, 226}, {18, 79, true, 284}, {18, 80, false, 284}};
    for (RouteCase const& c : routes)
    {
        std::string const tag = "route stage " + std::to_string(c.stage) + " level " + std::to_string(c.level);
        for (bool bis : {false, true})
        {
            RouteDecision const d = ResolveAutoGearRoute(Command(c.stage, c.level, bis, facts), ScoreOf);
            std::uint32_t const want = c.leveling ? (c.stageCap < 100 ? c.stageCap : 100) : c.stageCap;
            CapMode const mode = c.leveling ? CapMode::Leveling : CapMode::Endgame;
            ok = Check(d.cap == want && d.mode == mode && d.action == GearAction::CommitSet &&
                           d.plan.selected[4].entryId == want,
                     tag + (bis ? " bis" : " plain")) &&
                 ok;
        }
        RouteRequest noAverage = Command(c.stage, c.level, false, facts);
        noAverage.senderFixedKnown = false;
        RouteDecision const d = ResolveAutoGearRoute(noAverage, ScoreOf);
        if (c.leveling)
            ok = Check(d.preserve && d.cap == 0, tag + " leveling without an average preserves") && ok;
        else
            ok = Check(d.cap == c.stageCap, tag + " max level does not need the leveling average") && ok;
    }
    for (std::uint32_t level : {0u, 65u, 70u})
    {
        RouteDecision const s11 = ResolveAutoGearRoute(Command(11, level, true, facts), ScoreOf);
        ok = Check(s11.preserve && s11.cap == 0 && !s11.selectionRan, "stage 11 refuses at level " + std::to_string(level)) &&
             ok;
    }
    RouteDecision const zero = ResolveAutoGearRoute(Command(8, 0, true, facts), ScoreOf);
    ok = Check(zero.preserve && zero.cap == 0 && !zero.selectionRan, "unknown (zero) level preserves") && ok;
    RouteRequest unverified = Command(8, 70, true, facts);
    unverified.progressionContextVerified = false;
    RouteDecision const noStage = ResolveAutoGearRoute(unverified, ScoreOf);
    ok = Check(noStage.preserve && noStage.cap == 0 && !noStage.selectionRan, "unverified stage preserves") && ok;
    RouteDecision const past = ResolveAutoGearRoute(Command(19, 80, true, facts), ScoreOf);
    ok = Check(past.preserve && past.cap == 0, "stage 19 preserves") && ok;
    // The old level < 60 claim cannot raise a TBC leveler to the stage cap.
    RouteRequest stale = Command(8, 65, false, facts);
    stale.senderIsLeveling = false;
    RouteDecision const staleDecision = ResolveAutoGearRoute(stale, ScoreOf);
    ok = Check(staleDecision.cap == 100 && staleDecision.mode == CapMode::Leveling,
             "a known level overrides a stale not-leveling flag") &&
         ok;
    // Without a known level the existing caller flag path is unchanged.
    RouteRequest legacy = Command(7, 60, false, facts);
    legacy.senderLevelKnown = false;
    legacy.senderIsLeveling = false;
    ok = Check(ResolveAutoGearRoute(legacy, ScoreOf).cap == 92, "legacy flag path is unchanged") && ok;
    // Match keeps its own master-average target while leveling.
    std::vector<std::uint32_t> master{50, 50};
    RouteRequest match = Command(8, 65, false, facts);
    match.text = "match";
    match.masterSampleVerified = true;
    match.masterSlots = master;
    RouteDecision const matched = ResolveAutoGearRoute(match, ScoreOf);
    ok = Check(matched.mode == CapMode::Match && matched.cap <= 50, "match keeps the master average while leveling") &&
         ok;

    std::cout << "checks=" << g_checks << "\n";
    return ok ? 0 : 1;
}
