#include "AutoGearCommandRoute.h"
#include <iostream>
#include <vector>

using namespace PlayerbotsAutoGearSelection;
using PlayerbotsAutoGearWeapons::Bit;
using PlayerbotsAutoGearWeapons::Kind;

namespace
{
int g_checks = 0;

bool Check(bool condition, char const* text)
{
    ++g_checks;
    if (!condition)
    {
        std::cerr << "FAIL " << text << "\n";
        return false;
    }
    return true;
}

std::uint32_t ScoreOf(std::uint32_t id, std::int32_t, std::uint32_t)
{
    if (id == 66)
        return 40;
    if (id == 910001)
        return 10;
    return 1;
}

ItemFact Fact(std::uint32_t id, std::uint32_t itemLevel)
{
    ItemFact fact;
    fact.id = id;
    fact.itemLevel = itemLevel;
    fact.requiredLevel = 1;
    fact.slot = 4;
    fact.randomProperty = 4;
    fact.armorSubtype = 1;
    fact.sourceKnown = true;
    fact.metadataKnown = true;
    fact.equipKnown = true;
    fact.equipAllowed = true;
    fact.scoreKnown = true;
    return fact;
}

BotView Mage()
{
    BotView bot;
    bot.levelKnown = true;
    bot.level = 60;
    bot.classKnown = true;
    bot.classId = 8;
    bot.specKnown = true;
    bot.specTab = 1;
    bot.roleKnown = true;
    bot.role = Role::Caster;
    bot.armorKnown = true;
    bot.armorMask = std::uint32_t{1} << 1;
    bot.weaponsKnown = true;
    bot.weapons.mainhand = Bit(Kind::OneHand) | Bit(Kind::TwoHand);
    return bot;
}

RouteRequest Enabled(std::uint8_t stage)
{
    RouteRequest request;
    request.commandEnabled = true;
    request.ipCapEnabled = true;
    request.population = Population::RandomBot;
    request.rawStage = stage;
    request.effectiveStage = stage;
    request.qualityLimit = 4;
    request.selection.bot = Mage();
    request.selection.authorityValid = true;
    request.selection.bags = BagSpace::Enough;
    request.selection.eligibleMask = 1u << 4;
    request.selection.requiredMask = 1u << 4;
    request.selection.completeMask = 1u << 4;
    return request;
}
}

int main()
{
    bool ok = true;
    ok = Check(ApprovedVanillaEraCeiling() == 92, "the era ceiling is the approved table, not 78") && ok;
    std::uint32_t const caps[] = {66, 78, 78, 83, 83, 83, 88, 92};
    for (std::uint8_t stage = 0; stage <= 7; ++stage)
    {
        RouteRequest request = Enabled(stage);
        request.bis = true;
        RouteDecision const decision = ResolveAutoGearRoute(request, ScoreOf);
        ok = Check(decision.ranNewPath && !decision.preserve && decision.cap == caps[stage] && !decision.deletesGear,
                "a zero score limit uses the stage cap") &&
             ok;
    }
    RouteRequest lowered = Enabled(7);
    lowered.bis = true;
    lowered.scoreLimit = 80;
    ok = Check(ResolveAutoGearRoute(lowered, ScoreOf).cap == 80, "a nonzero score limit only lowers the cap") && ok;
    RouteRequest raised = Enabled(0);
    raised.bis = true;
    raised.scoreLimit = 100;
    ok = Check(ResolveAutoGearRoute(raised, ScoreOf).cap == 66, "a score limit cannot raise the stage") && ok;

    std::array<std::uint32_t, 3> occupied{{60, 0, 80}};
    RouteRequest post = Enabled(1);
    post.firstStageAfterLeveling = true;
    post.occupiedSlots = occupied;
    char const* normal[] = {"", "reset", "green", "200"};
    for (char const* text : normal)
    {
        post.text = text;
        post.bis = false;
        RouteDecision const decision = ResolveAutoGearRoute(post, ScoreOf);
        ok = Check(decision.mode == CapMode::Leveling && decision.cap == 70 && !decision.deletesGear,
                "a normal form keeps the occupied average") &&
             ok;
    }
    post.bis = true;
    post.text = "";
    ok = Check(ResolveAutoGearRoute(post, ScoreOf).cap == 78, "bis at the first post-level stage uses the stage") && ok;
    post.text = "50";
    ok = Check(ResolveAutoGearRoute(post, ScoreOf).cap == 50, "bis numeric can only lower the stage cap") && ok;
    post.bis = false;
    post.text = "match";
    ok = Check(ResolveAutoGearRoute(post, ScoreOf).mode == CapMode::Match &&
               ResolveAutoGearRoute(post, ScoreOf).cap == 70,
            "match ignores the empty slot and stays inside the stage") &&
         ok;

    RouteRequest leveling = Enabled(0);
    leveling.senderIsLeveling = true;
    leveling.senderFixedKnown = true;
    leveling.senderFixedAverage = 40;
    leveling.occupiedSlots = occupied;
    ok = Check(ResolveAutoGearRoute(leveling, ScoreOf).cap == 40, "leveling still uses the fixed sender average") && ok;

    RouteRequest off = Enabled(7);
    off.ipCapEnabled = false;
    off.scoreLimit = 0;
    RouteDecision const legacy = ResolveAutoGearRoute(off, ScoreOf);
    ok = Check(!legacy.ranNewPath && legacy.legacyUnchanged && legacy.cap == 0, "the flag off keeps the legacy path") &&
         ok;
    RouteRequest disabled = Enabled(0);
    disabled.commandEnabled = false;
    ok = Check(ResolveAutoGearRoute(disabled, ScoreOf).rejected, "a disabled command does not gear") && ok;

    RouteRequest rndbot = Enabled(6);
    rndbot.tierSync = true;
    RouteDecision const synced = ResolveAutoGearRoute(rndbot, ScoreOf);
    ok = Check(synced.ranNewPath && synced.cap == 88 && !synced.writesProgression && !synced.deletesGear,
            "only a random bot is tier synced, and its stage is not written") &&
         ok;
    RouteRequest alt = synced.cap == 88 ? Enabled(6) : Enabled(6);
    alt.tierSync = true;
    alt.population = Population::AltBot;
    RouteDecision const altSync = ResolveAutoGearRoute(alt, ScoreOf);
    ok = Check(altSync.rejected && !altSync.writesProgression && altSync.action == GearAction::Preserve,
            "an alt bot is not tier synced") &&
         ok;
    RouteRequest altCommand = Enabled(0);
    altCommand.population = Population::AltBot;
    altCommand.altCommandAllowed = false;
    ok = Check(ResolveAutoGearRoute(altCommand, ScoreOf).rejected, "a refused alt command does not change gear") && ok;

    std::vector<ItemFact> legal{Fact(66, 60)};
    RouteRequest commit = Enabled(0);
    commit.selection.facts = legal;
    RouteDecision const chosen = ResolveAutoGearRoute(commit, ScoreOf);
    ok = Check(chosen.selectionRan && chosen.action == GearAction::CommitSet && chosen.plan.selected[4].entryId == 66 &&
               !chosen.deletesGear,
            "a legal set is committed and nothing is deleted") &&
         ok;

    std::vector<ItemFact> tooHard{Fact(66, 60)};
    tooHard[0].requiredLevel = 61;
    RouteRequest strip = Enabled(0);
    strip.selection.facts = tooHard;
    RouteDecision const stripped = ResolveAutoGearRoute(strip, ScoreOf);
    ok = Check(stripped.action == GearAction::StripToBags && !stripped.deletesGear,
            "a confirmed empty set moves gear to bags") &&
         ok;

    RouteRequest unknown = Enabled(0);
    unknown.selection.authorityValid = false;
    unknown.selection.facts = legal;
    ok = Check(ResolveAutoGearRoute(unknown, ScoreOf).action == GearAction::Preserve,
            "unknown authority preserves") &&
         ok;

    ItemFact card = Fact(910001, 60);
    card.darkmoonCard = true;
    card.requestForceBis = true;
    std::vector<ItemFact> forced{card, Fact(66, 60)};
    RouteRequest darkmoon = Enabled(0);
    darkmoon.selection.facts = forced;
    ok = Check(ResolveAutoGearRoute(darkmoon, ScoreOf).action == GearAction::Preserve,
            "an unknown Darkmoon card is not forced") &&
         ok;
    forced[0].darkmoonAvailabilityKnown = true;
    forced[0].darkmoonProcKnown = true;
    darkmoon.selection.facts = forced;
    RouteDecision const scored = ResolveAutoGearRoute(darkmoon, ScoreOf);
    ok = Check(scored.action == GearAction::CommitSet && scored.plan.selected[4].entryId == 66,
            "a known Darkmoon card loses to the higher real score") &&
         ok;

    if (!ok)
        return 1;
    std::cout << "checks=" << g_checks << "\n";
    return 0;
}
