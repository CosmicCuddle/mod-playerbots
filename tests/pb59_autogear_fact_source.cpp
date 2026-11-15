#include "AutoGearConstraintFill.h"
#include "AutoGearVariantSource.h"
#include <array>
#include <iostream>

using namespace PlayerbotsAutoGearSelection;
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

ItemFact KnownChest()
{
    ItemFact fact;
    fact.id = 10;
    fact.itemLevel = 60;
    fact.requiredLevel = 1;
    fact.slot = 4;
    fact.armorSubtype = 1;
    fact.sourceKnown = true;
    fact.metadataKnown = true;
    fact.equipKnown = true;
    fact.equipAllowed = true;
    return fact;
}

BotView KnownMage()
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
    bot.armorMask = 1u << 1;
    bot.weaponsKnown = true;
    return bot;
}
}

int main()
{
    bool ok = true;
    SourceRow loaded;
    loaded.entryId = 100;
    loaded.itemLevel = 60;
    loaded.templateLoaded = true;
    loaded.sourceEligible = false;
    loaded.staticCapabilityKnown = true;
    loaded.staticCapable = true;
    std::array<SourceRow, 1> onlyLoaded{{loaded}};
    CopiedSource const notEligible = CopyBoundedSource(onlyLoaded, 4);
    ok = Check(notEligible.rows.empty(), "a loaded template is not an eligible source") && ok;

    SourceRow propA = loaded;
    propA.sourceEligible = true;
    propA.randomProperty = 1;
    SourceRow propB = propA;
    propB.randomProperty = 2;
    std::array<SourceRow, 2> variants{{propA, propB}};
    CopiedSource const both = CopyBoundedSource(variants, 4);
    ok = Check(both.rows.size() == 2 && !both.sourceComplete && both.status == CopyStatus::IncompleteVariants,
            "distinct random properties are not dropped or called complete") &&
         ok;
    std::array<SourceRow, 1> one{{propA}};
    ok = Check(CopyBoundedSource(one, 4).sourceComplete && CopyBoundedSource(one, 4).rows.size() == 1,
            "one variant can be complete") &&
         ok;

    std::array<SourceRow, 5> many{};
    for (std::size_t i = 0; i < many.size(); ++i)
    {
        many[i] = propA;
        many[i].entryId = static_cast<std::uint32_t>(200 + i);
    }
    CopiedSource const bounded = CopyBoundedSource(many, 2);
    ok = Check(bounded.status == CopyStatus::Bounded && bounded.rows.empty() && !bounded.sourceComplete,
            "native rows are bounded before they are copied") &&
         ok;

    ItemFact wornUnique = KnownChest();
    wornUnique.equipAllowed = false;
    wornUnique.blockedOnlyByCurrentGear = true;
    ok = Check(Classify(wornUnique, KnownMage(), 66) == FactDecision::Accept,
            "a current-gear block is not a static miss") &&
         ok;
    wornUnique.blockedOnlyByCurrentGear = false;
    ok = Check(Classify(wornUnique, KnownMage(), 66) == FactDecision::Unavailable,
            "a known equip miss stays unavailable") &&
         ok;

    OwnerThreadCopy published{11, 22, 33, true};
    OwnerThreadCopy same = published;
    OwnerThreadCopy otherBot = published;
    otherBot.botEpoch = 99;
    ok = Check(SameFinalCopy(published, same) && !SameFinalCopy(published, otherBot),
            "final B compares sender and bot epochs on the copy") &&
         ok;

    if (!ok)
        return 1;
    std::cout << "checks=" << g_checks << "\n";
    return 0;
}
