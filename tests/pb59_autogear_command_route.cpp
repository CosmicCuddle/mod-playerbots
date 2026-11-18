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
    request.populationKnown = true;
    request.playerContextVerified = true;
    request.progressionContextVerified = true;
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
        ok = Check(decision.ranNewPath && decision.preserve && decision.action == GearAction::Preserve &&
                   decision.cap == caps[stage] && !decision.deletesGear,
                "a zero score limit uses the stage cap and does not commit without a set") &&
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

    std::array<std::uint32_t, 3> senderSlots{{40, 0, 40}};
    std::array<std::uint32_t, 3> masterSlots{{60, 0, 80}};
    RouteRequest post = Enabled(1);
    post.firstStageAfterLeveling = true;
    post.commandingSenderVerified = true;
    post.commandingSenderSlots = senderSlots;
    post.masterSampleVerified = true;
    post.masterSlots = masterSlots;
    char const* normal[] = {"", "reset", "green", "200"};
    for (char const* text : normal)
    {
        post.text = text;
        post.bis = false;
        RouteDecision const decision = ResolveAutoGearRoute(post, ScoreOf);
        ok = Check(decision.mode == CapMode::Leveling && decision.cap == 40 && !decision.deletesGear,
                "a normal form uses the commanding sender average") &&
             ok;
    }
    post.bis = true;
    post.text = "";
    ok = Check(ResolveAutoGearRoute(post, ScoreOf).cap == 78, "bis at the first post-level stage uses the stage") && ok;
    post.text = "50";
    ok = Check(ResolveAutoGearRoute(post, ScoreOf).cap == 50, "bis numeric can only lower the stage cap") && ok;
    post.bis = false;
    post.text = "match";
    RouteDecision const matched = ResolveAutoGearRoute(post, ScoreOf);
    ok = Check(matched.mode == CapMode::Match && matched.cap == 70,
            "match uses the bot master average and ignores the empty slot") &&
         ok;
    post.masterSampleVerified = false;
    ok = Check(ResolveAutoGearRoute(post, ScoreOf).preserve, "an unverified master sample preserves match") && ok;

    RouteRequest leveling = Enabled(0);
    leveling.senderIsLeveling = true;
    std::array<std::uint32_t, 3> lowSender{{10, 0, 10}};
    leveling.senderFixedKnown = true;
    leveling.senderFixedAverage = 40;
    leveling.commandingSenderVerified = true;
    leveling.commandingSenderSlots = lowSender;
    leveling.masterSampleVerified = true;
    leveling.masterSlots = masterSlots;
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
    ok = Check(synced.ranNewPath && synced.cap == 88 && synced.action == GearAction::Preserve &&
               !synced.writesProgression && !synced.deletesGear,
            "tier sync records the cap and does not commit without a set") &&
         ok;
    RouteRequest syncOff = Enabled(6);
    syncOff.tierSync = true;
    syncOff.ipCapEnabled = false;
    RouteDecision const syncDisabled = ResolveAutoGearRoute(syncOff, ScoreOf);
    ok = Check(!syncDisabled.ranNewPath && syncDisabled.action == GearAction::Preserve && syncDisabled.cap == 0,
            "tier sync stays off with the feature") &&
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
    ok = Check(stripped.action == GearAction::Preserve && stripped.preserve &&
               !stripped.plan.sourceComplete && !stripped.deletesGear,
            "a facts-only NoSet cannot strip an RNDBOT without native proof") &&
         ok;

    RouteRequest unknown = Enabled(0);
    unknown.selection.authorityValid = false;
    unknown.selection.facts = legal;
    RouteDecision const missingAuthority = ResolveAutoGearRoute(unknown, ScoreOf);
    ok = Check(missingAuthority.action == GearAction::Preserve && missingAuthority.preserve,
            "unknown authority preserves") &&
         ok;
    RouteRequest unverified = Enabled(0);
    unverified.playerContextVerified = false;
    unverified.selection.facts = legal;
    ok = Check(ResolveAutoGearRoute(unverified, ScoreOf).preserve, "unverified player context preserves") && ok;
    RouteRequest noPopulation = Enabled(0);
    noPopulation.populationKnown = false;
    ok = Check(ResolveAutoGearRoute(noPopulation, ScoreOf).preserve, "unknown population preserves") && ok;
    RouteRequest emptyFacts = Enabled(0);
    RouteDecision const emptyUnknown = ResolveAutoGearRoute(emptyFacts, ScoreOf);
    ok = Check(emptyUnknown.action == GearAction::Preserve && emptyUnknown.preserve && !emptyUnknown.selectionRan,
            "empty facts without a proven domain preserve") &&
         ok;
    emptyFacts.domainProvenEmpty = true;
    RouteDecision const emptyProven = ResolveAutoGearRoute(emptyFacts, ScoreOf);
    ok = Check(emptyProven.action == GearAction::Preserve && emptyProven.preserve &&
               !emptyProven.selectionRan && !emptyProven.deletesGear,
            "caller domainProvenEmpty flag cannot authorize an empty-domain strip") &&
         ok;

    // A synthetic but internally signed/exhaustive native empty list CAN
    // still request StripToBags for a verified RNDBOT. The native collector
    // in the actual server does not currently prove an exhaustive universe.
    SourceRow unavailable;
    unavailable.entryId = 100;
    unavailable.itemLevel = 60;
    unavailable.slot = 4; // same required chest slot as the empty native proof
    unavailable.templateLoaded = true;
    unavailable.eligibility = SourceEligibility::KnownUnavailable;
    unavailable.staticCapabilityKnown = true;
    unavailable.staticCapable = true;
    std::array<SourceRow, 1> unavailableOnly{{unavailable}};
    RouteRequest signedEmpty = Enabled(0);
    signedEmpty.domainProvenEmpty = true;
    signedEmpty.selection.hasNativeSource = true;
    signedEmpty.selection.nativeRows = unavailableOnly;
    signedEmpty.selection.sourceBudget = 4;
    signedEmpty.selection.sourceRevision = 7;
    ProvenanceRow unavailableProof;
    unavailableProof.entryId = unavailable.entryId;
    unavailableProof.randomProperty = unavailable.randomProperty;
    unavailableProof.itemLevel = unavailable.itemLevel;
    unavailableProof.state = RowState::KnownUnavailable;
    unavailableProof.revision = 7;
    std::array<ProvenanceRow, 1> proofRows{{unavailableProof}};
    signedEmpty.selection.nativeProofs[4].authoritative = true;
    signedEmpty.selection.nativeProofs[4].rowsDeclared = true;
    signedEmpty.selection.nativeProofs[4].declaredRows = proofRows.size();
    signedEmpty.selection.nativeProofs[4].rows = proofRows;
    RouteDecision const signedStrip = ResolveAutoGearRoute(signedEmpty, ScoreOf);
    ok = Check(signedStrip.action == GearAction::StripToBags && !signedStrip.preserve &&
               signedStrip.selectionRan && signedStrip.plan.sourceComplete && !signedStrip.deletesGear,
            "verified RNDBOT with declared matching native ProvenEmpty may strip to bags") && ok;
    signedEmpty.selection.nativeProofs[4].rowsDeclared = false;
    RouteDecision const missingDeclaration = ResolveAutoGearRoute(signedEmpty, ScoreOf);
    ok = Check(missingDeclaration.preserve && !missingDeclaration.plan.sourceComplete,
            "authoritative row span without declared count must preserve") && ok;
    signedEmpty.selection.nativeProofs[4].rowsDeclared = true;
    signedEmpty.selection.nativeProofs[4].rows = std::span<ProvenanceRow const>{};
    RouteDecision const droppedUnavailable = ResolveAutoGearRoute(signedEmpty, ScoreOf);
    ok = Check(droppedUnavailable.preserve && !droppedUnavailable.plan.sourceComplete,
            "omitting a known-unavailable source from empty proof preserves") && ok;
    signedEmpty.selection.nativeProofs[4].rows = proofRows;
    proofRows[0].entryId = 101;
    RouteDecision const wrongProofIdentity = ResolveAutoGearRoute(signedEmpty, ScoreOf);
    ok = Check(wrongProofIdentity.preserve && !wrongProofIdentity.plan.sourceComplete,
            "mismatched unavailable source and proof identity preserves") && ok;
    proofRows[0].entryId = unavailable.entryId;
    unavailableOnly[0].slot = 5;
    RouteDecision const wrongSourceSlot = ResolveAutoGearRoute(signedEmpty, ScoreOf);
    ok = Check(wrongSourceSlot.preserve && !wrongSourceSlot.plan.sourceComplete,
            "unavailable source for another slot cannot prove chest empty") && ok;
    unavailableOnly[0].slot = 4;
    signedEmpty.selection.sourceRevision = 0;
    RouteDecision const staleSigned = ResolveAutoGearRoute(signedEmpty, ScoreOf);
    ok = Check(staleSigned.action == GearAction::Preserve && !staleSigned.plan.sourceComplete,
            "zero source revision is not an empty certificate") && ok;
    signedEmpty.selection.sourceRevision = 7;
    signedEmpty.selection.nativeProofs[4].authoritative = false;
    RouteDecision const nonAuthoritative = ResolveAutoGearRoute(signedEmpty, ScoreOf);
    ok = Check(nonAuthoritative.action == GearAction::Preserve &&
               !nonAuthoritative.plan.sourceComplete,
            "unverified native slot proof never strips") && ok;
    signedEmpty.selection.nativeProofs[4].authoritative = true;
    signedEmpty.population = Population::AltBot;
    RouteDecision const altSigned = ResolveAutoGearRoute(signedEmpty, ScoreOf);
    ok = Check(altSigned.action == GearAction::Preserve && altSigned.preserve,
            "even a signed empty source cannot strip an AltBot") && ok;
    signedEmpty.population = Population::RandomBot;
    signedEmpty.domainProvenEmpty = false;
    RouteDecision const unsignedHint = ResolveAutoGearRoute(signedEmpty, ScoreOf);
    ok = Check(unsignedHint.action == GearAction::Preserve && !unsignedHint.selectionRan,
            "signed proof without an independently asserted empty-domain hint preserves") && ok;
    signedEmpty.domainProvenEmpty = true;
    unavailableOnly[0].eligibility = SourceEligibility::Unknown;
    RouteDecision const unknownEligibility = ResolveAutoGearRoute(signedEmpty, ScoreOf);
    ok = Check(unknownEligibility.action == GearAction::Preserve &&
               !unknownEligibility.plan.sourceComplete,
            "unknown template eligibility prevents native empty certification") && ok;

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
