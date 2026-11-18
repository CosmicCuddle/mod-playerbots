// CP30: owner-approved stage caps 8-18 and TBC/Wrath era ceilings (18 Nov
// 2026). Source: item-database research. Wrath Naxx 25 / OS / EoE 25 top out
// at 226; ICC 25 Heroic and Ruby Sanctum 25 Heroic at 284; nothing lootable
// reaches 290. Stages 0-7 are unchanged.
#include "AutoGearCommandRoute.h"
#include "AutoGearMasterTier.h"
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

constexpr std::uint32_t kExpected[19] = {
    66, 78, 78, 83, 83, 83, 88, 92, 125, 125, 141, 0, 156, 226, 226, 245, 258, 284, 284};

constexpr CapInput Endgame(std::uint8_t raw, std::uint8_t effective)
{
    CapInput in;
    in.playerContext = true;
    in.progressionContext = true;
    in.mode = CapMode::Endgame;
    in.rawStage = raw;
    in.effectiveStage = effective;
    in.policy = ApprovedCapPolicy();
    return in;
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

RouteRequest Bis(std::uint8_t stage, std::vector<ItemFact> const& facts)
{
    RouteRequest request;
    request.commandEnabled = true;
    request.ipCapEnabled = true;
    request.bis = true;
    request.populationKnown = true;
    request.population = Population::RandomBot;
    request.playerContextVerified = true;
    request.progressionContextVerified = true;
    request.rawStage = stage;
    request.effectiveStage = stage;
    request.qualityLimit = 4;
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
}  // namespace

static_assert(ApprovedVanillaCompletion[7] == 92);
static_assert(ApprovedVanillaCompletion[13] == 226);
static_assert(ApprovedVanillaCompletion[18] == 284);
static_assert(ApprovedEraCeilings[0] == 92 && ApprovedEraCeilings[1] == 159 && ApprovedEraCeilings[2] == 284);
static_assert(ComposeCap(Endgame(8, 8)).cap == 125);
static_assert(ComposeCap(Endgame(11, 11)).preserve);

int main()
{
    bool ok = true;
    for (std::uint8_t stage = 0; stage <= 18; ++stage)
    {
        std::string const tag = "stage " + std::to_string(stage);
        ok = Check(ApprovedVanillaCompletion[stage] == kExpected[stage], tag + " table value") && ok;
        CapResult const result = ComposeCap(Endgame(stage, stage));
        if (kExpected[stage] == 0)
            ok = Check(result.preserve && result.cap == 0, tag + " (disabled) refuses") && ok;
        else
            ok = Check(!result.preserve && result.cap == kExpected[stage], tag + " resolves its approved cap") && ok;
    }
    ok = Check(ApprovedVanillaEraCeiling() == 92, "later stages do not raise the Vanilla ceiling") && ok;
    for (std::uint8_t stage = 0; stage < PlayerbotsAutoGearCap::TbcEntryStage; ++stage)
        ok = Check(ComposeCap(Endgame(stage, stage)).cap <= 92, "Vanilla stages stay at or below 92") && ok;
    for (std::uint8_t stage = PlayerbotsAutoGearCap::TbcEntryStage; stage < PlayerbotsAutoGearCap::WrathEntryStage; ++stage)
        ok = Check(ComposeCap(Endgame(stage, stage)).cap <= 159, "TBC stages stay at or below 159") && ok;
    for (std::uint8_t stage = PlayerbotsAutoGearCap::WrathEntryStage; stage <= 18; ++stage)
        ok = Check(ComposeCap(Endgame(stage, stage)).cap <= 284, "Wrath stages stay at or below 284") && ok;

    // A lagging effective stage is clamped by its own era ceiling.
    ok = Check(ComposeCap(Endgame(13, 7)).cap == 92, "raw 13 at effective Vanilla stage stays at 92") && ok;
    ok = Check(ComposeCap(Endgame(17, 10)).cap == 159, "raw 17 at effective TBC stage stays at 159") && ok;
    for (std::uint8_t raw = PlayerbotsAutoGearCap::WrathEntryStage; raw <= 18; ++raw)
    {
        for (std::uint8_t effective = PlayerbotsAutoGearCap::TbcEntryStage; effective < PlayerbotsAutoGearCap::WrathEntryStage; ++effective)
            ok = Check(ComposeCap(Endgame(raw, effective)).cap == 159, "a Wrath raw stage at a TBC effective stage is held at 159") && ok;
    }
    ok = Check(ApprovedTbcEraCeiling == PlayerbotsAutoGearSelection::RaidPointCeiling(
                   PlayerbotsAutoGearSelection::RaidPoint::TbcSunwell, PlayerbotsAutoGearSelection::RaidSize::Unknown),
             "TBC era ceiling equals the Sunwell tier ceiling") && ok;
    ok = Check(ComposeCap(Endgame(9, 10)).preserve, "effective above raw still refuses") && ok;
    ok = Check(ComposeCap(Endgame(19, 19)).preserve, "stage 19 still refuses") && ok;
    CapInput limited = Endgame(18, 18);
    limited.scoreLimit = 200;
    ok = Check(ComposeCap(limited).cap == 200, "a nonzero score limit only lowers the cap") && ok;
    CapInput leveling = Endgame(13, 13);
    leveling.mode = CapMode::Leveling;
    leveling.senderSampleKnown = true;
    leveling.senderAverage = 180;
    ok = Check(ComposeCap(leveling).cap == 180, "leveling still uses the lower sender average") && ok;

    // Through the command route: 290 is never allowed; 284 is, at stage 17+.
    std::vector<ItemFact> top{Chest(284, 284), Chest(290, 290)};
    RouteDecision const s18 = ResolveAutoGearRoute(Bis(18, top), ScoreOf);
    ok = Check(s18.cap == 284 && s18.action == GearAction::CommitSet && s18.plan.selected[4].entryId == 284,
             "stage 18 BiS takes the 284 item and never a 290 one") &&
         ok;
    std::vector<ItemFact> naxx{Chest(226, 226), Chest(232, 232)};
    RouteDecision const s13 = ResolveAutoGearRoute(Bis(13, naxx), ScoreOf);
    ok = Check(s13.cap == 226 && s13.plan.selected[4].entryId == 226, "stage 13 BiS stops at Naxx 25 226") && ok;
    std::vector<ItemFact> kara{Chest(125, 125), Chest(141, 141)};
    RouteDecision const s8 = ResolveAutoGearRoute(Bis(8, kara), ScoreOf);
    ok = Check(s8.cap == 125 && s8.plan.selected[4].entryId == 125, "stage 8 BiS stops at Karazhan 125") && ok;
    RouteDecision const s11 = ResolveAutoGearRoute(Bis(11, kara), ScoreOf);
    ok = Check(s11.preserve && s11.cap == 0 && !s11.selectionRan, "disabled stage 11 preserves") && ok;

    std::cout << "checks=" << g_checks << "\n";
    return ok ? 0 : 1;
}
