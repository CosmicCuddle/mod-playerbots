// CP30 G3: only a proven RNDBOT may have equipped gear moved to bags or
// enter tier sync. The cap comes only from the commanding player's verified
// Individual Progression stage; nothing is invented when that is missing.
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

RouteRequest Ready(Population population, std::uint8_t stage)
{
    RouteRequest request;
    request.commandEnabled = true;
    request.ipCapEnabled = true;
    request.populationKnown = true;
    request.population = population;
    request.playerContextVerified = true;
    request.progressionContextVerified = true;
    request.rawStage = stage;
    request.effectiveStage = stage;
    request.qualityLimit = 4;
    request.selection.bot = Mage();
    request.selection.eligibleMask = 1u << 4;
    request.selection.requiredMask = 1u << 4;
    request.selection.completeMask = 1u << 4;
    request.selection.authorityValid = true;
    request.selection.bags = BagSpace::Enough;
    return request;
}

std::string Name(Population population)
{
    switch (population)
    {
        case Population::RandomBot:
            return "RNDBOT";
        case Population::AltBot:
            return "altbot/selfbot";
        case Population::AddClass:
            return "AddClass";
        case Population::Unknown:
            return "unknown";
    }
    return "?";
}
}  // namespace

int main()
{
    bool ok = true;
    Population const all[] = {Population::RandomBot, Population::AltBot, Population::AddClass, Population::Unknown};

    // Tier sync: RNDBOT only, and only with a verified stage.
    for (Population population : all)
    {
        RouteRequest sync = Ready(population, 1);
        sync.tierSync = true;
        RouteDecision const decision = ResolveAutoGearRoute(sync, ScoreOf);
        bool const rnd = population == Population::RandomBot;
        ok = Check(decision.preserve && !decision.deletesGear && !decision.writesProgression,
                 Name(population) + " tier sync never mutates or writes a stage") &&
             ok;
        ok = Check(decision.ranNewPath == rnd && decision.rejected == !rnd,
                 Name(population) + " tier sync runs only for a proven RNDBOT") &&
             ok;
        if (rnd)
            ok = Check(decision.cap == 78, "RNDBOT tier sync uses the verified stage 1 cap") && ok;
        sync.populationKnown = false;
        RouteDecision const unproven = ResolveAutoGearRoute(sync, ScoreOf);
        ok = Check(unproven.rejected && !unproven.ranNewPath, Name(population) + " unproven population is rejected") &&
             ok;
    }
    RouteRequest syncNoStage = Ready(Population::RandomBot, 1);
    syncNoStage.tierSync = true;
    syncNoStage.progressionContextVerified = false;
    RouteDecision const syncHeld = ResolveAutoGearRoute(syncNoStage, ScoreOf);
    ok = Check(syncHeld.preserve && !syncHeld.ranNewPath && syncHeld.cap == 0,
             "tier sync without a verified stage resolves no cap") &&
         ok;

    // Strip to bags: a proven empty domain only strips a proven RNDBOT.
    for (Population population : all)
    {
        RouteRequest empty = Ready(population, 0);
        empty.domainProvenEmpty = true;
        RouteDecision const decision = ResolveAutoGearRoute(empty, ScoreOf);
        bool const rnd = population == Population::RandomBot;
        ok = Check((decision.action == GearAction::StripToBags) == rnd && !decision.deletesGear,
                 Name(population) + " empty-domain strip is RNDBOT-only") &&
             ok;
        empty.populationKnown = false;
        RouteDecision const unproven = ResolveAutoGearRoute(empty, ScoreOf);
        ok = Check(unproven.action == GearAction::Preserve, Name(population) + " unproven owner never strips") && ok;

        // Solver-produced empty result (caller-claimed complete list with only
        // an over-cap item) also strips only a proven RNDBOT.
        std::vector<ItemFact> overCap{Chest(500, 90)};
        RouteRequest solved = Ready(population, 0);
        solved.selection.facts = overCap;
        RouteDecision const viaSolver = ResolveAutoGearRoute(solved, ScoreOf);
        if (population == Population::Unknown)
            ok = Check(viaSolver.preserve && !viaSolver.selectionRan, "unknown owner never reaches the solver") && ok;
        else
            ok = Check((viaSolver.action == GearAction::StripToBags) == rnd && !viaSolver.deletesGear,
                     Name(population) + " solver empty result strips only an RNDBOT") &&
                 ok;
    }

    // Alt bots: refusal is unconditional, even with the legacy setting enabled.
    RouteRequest altRefused = Ready(Population::AltBot, 0);
    altRefused.altCommandAllowed = false;
    std::vector<ItemFact> legal{Chest(60, 60)};
    altRefused.selection.facts = legal;
    RouteDecision const refused = ResolveAutoGearRoute(altRefused, ScoreOf);
    ok = Check(refused.rejected && refused.preserve, "AutoGearCommandAltBots=0 refuses alt bots") && ok;
    RouteRequest altAllowed = Ready(Population::AltBot, 0);
    altAllowed.selection.facts = legal;
    RouteDecision const altCommit = ResolveAutoGearRoute(altAllowed, ScoreOf);
    ok = Check(altCommit.rejected && altCommit.preserve && !altCommit.selectionRan,
             "alt command is refused even with historical config enabled") &&
         ok;

    // Cap provenance: only the verified sender stage, nothing invented.
    std::uint32_t const vanilla[] = {66, 78, 78, 83, 83, 83, 88, 92};
    for (std::uint8_t stage = 0; stage <= 7; ++stage)
    {
        RouteRequest bis = Ready(Population::RandomBot, stage);
        bis.bis = true;
        bis.selection.facts = legal;
        RouteDecision const decision = ResolveAutoGearRoute(bis, ScoreOf);
        ok = Check(decision.cap == vanilla[stage], "stage " + std::to_string(stage) + " BiS uses its approved cap") &&
             ok;
    }
    RouteRequest unverified = Ready(Population::RandomBot, 7);
    unverified.progressionContextVerified = false;
    unverified.selection.facts = legal;
    RouteDecision const noStage = ResolveAutoGearRoute(unverified, ScoreOf);
    ok = Check(noStage.preserve && noStage.cap == 0 && !noStage.selectionRan,
             "an unverified IP stage resolves no cap and preserves") &&
         ok;
    RouteRequest noPlayer = Ready(Population::RandomBot, 7);
    noPlayer.playerContextVerified = false;
    noPlayer.selection.facts = legal;
    ok = Check(ResolveAutoGearRoute(noPlayer, ScoreOf).preserve, "an unverified player context preserves") && ok;
    RouteRequest shifted = Ready(Population::RandomBot, 3);
    shifted.effectiveStage = 4;
    shifted.selection.facts = legal;
    RouteDecision const ahead = ResolveAutoGearRoute(shifted, ScoreOf);
    ok = Check(ahead.preserve && ahead.cap == 0, "an effective stage above the raw stage preserves") && ok;
    RouteRequest past = Ready(Population::RandomBot, 19);
    past.selection.facts = legal;
    ok = Check(ResolveAutoGearRoute(past, ScoreOf).preserve, "a stage past 18 preserves") && ok;
    RouteRequest limited = Ready(Population::RandomBot, 7);
    limited.bis = true;
    limited.scoreLimit = 70;
    limited.selection.facts = legal;
    ok = Check(ResolveAutoGearRoute(limited, ScoreOf).cap == 70, "a nonzero score limit only lowers the cap") && ok;

    // Leveling (cap 60 Vanilla): the sender's fixed average bounds the stage cap.
    RouteRequest leveling = Ready(Population::RandomBot, 0);
    leveling.senderIsLeveling = true;
    leveling.selection.facts = legal;
    RouteDecision const noAverage = ResolveAutoGearRoute(leveling, ScoreOf);
    ok = Check(noAverage.preserve && noAverage.cap == 0, "leveling without a sender average preserves") && ok;
    leveling.senderFixedKnown = true;
    leveling.senderFixedAverage = 40;
    std::vector<ItemFact> low{Chest(40, 40)};
    leveling.selection.facts = low;
    RouteDecision const levelCap = ResolveAutoGearRoute(leveling, ScoreOf);
    ok = Check(levelCap.cap == 40 && levelCap.mode == CapMode::Leveling, "leveling uses min(stage cap, sender average)") &&
         ok;

    std::cout << "checks=" << g_checks << "\n";
    return ok ? 0 : 1;
}
