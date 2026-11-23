// "trainer learnone": exactly one spell or nothing (coordination #75, comment 7079993298).
#include <array>
#include <cstdint>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#include "TrainerLearnOne.h"

using namespace PlayerbotsTrainerLearnOne;

namespace
{
int g_checks = 0;
int g_failures = 0;

void Check(bool condition, std::string const& text)
{
    ++g_checks;
    if (!condition)
    {
        ++g_failures;
        std::cerr << "FAIL " << text << "\n";
    }
}

void Expect(std::string const& param, Parse status, std::uint32_t spell, std::string const& text)
{
    Result const r = ParseLearnOne(param);
    Check(r.status == status && r.spellId == spell, text + " [" + param + "]");
}

constexpr char const* Mining = "|cff71d5ff|Hspell:2575|h[Mining]|h|r";
}  // namespace

int main()
{
    // Other trainer forms keep the existing behaviour.
    Expect("", Parse::NotLearnOne, 0, "an empty parameter is not learnone");
    Expect("learn", Parse::NotLearnOne, 0, "learn stays the existing command");
    Expect(std::string("learn ") + Mining, Parse::NotLearnOne, 0, "learn with a link stays the existing command");
    Expect("learnonex 2575", Parse::NotLearnOne, 0, "a longer word is not learnone");
    Expect(Mining, Parse::NotLearnOne, 0, "a bare link (listing) is not learnone");

    // Exactly one spell.
    Expect(std::string("learnone ") + Mining, Parse::Spell, 2575, "one coloured spell link");
    Expect("learnone |Hspell:3273|h[First Aid]|h", Parse::Spell, 3273, "a link without colour or reset");
    Expect("learnone |cffffd000|Htrade:2575:300:300:1:abc|h[Mining]|h|r", Parse::Spell, 2575, "a trade link");
    Expect("learnone |cffffffff|Henchant:7418|h[Enchanting: Enchant Bracer - Minor Health]|h|r", Parse::Spell, 7418,
           "an enchant link with spaces and punctuation in its name");
    Expect("learnone 2575", Parse::Spell, 2575, "one number");
    Expect("  learnone   2575  ", Parse::Spell, 2575, "surrounding spaces");
    Expect("learnone 4294967295", Parse::Spell, 4294967295u, "the largest id");

    // Fail closed.
    Expect("learnone", Parse::Missing, 0, "nothing after learnone");
    Expect("learnone   ", Parse::Missing, 0, "only spaces after learnone");
    Expect("learnone 0", Parse::Malformed, 0, "zero");
    Expect("learnone -2575", Parse::Malformed, 0, "a negative number");
    Expect("learnone 4294967296", Parse::Malformed, 0, "a number above uint32");
    Expect("learnone 99999999999", Parse::Malformed, 0, "too many digits");
    Expect("learnone 25x75", Parse::Malformed, 0, "a number with letters");
    Expect("learnone mining", Parse::Malformed, 0, "a spell name");
    Expect("learnone |cff9d9d9d|Hitem:2901:0:0:0:0:0:0:0:0|h[Mining Pick]|h|r", Parse::Malformed, 0, "an item link");
    Expect("learnone |cff71d5ff|Htalent:1:0|h[Talent]|h|r", Parse::Malformed, 0, "a talent link");
    Expect("learnone |cff71d5ff|Hspell:2575|h[Mining", Parse::Malformed, 0, "a link that never closes");
    Expect("learnone |cff71d5ff|Hspell:|h[Mining]|h|r", Parse::Malformed, 0, "a link without an id");
    Expect("learnone |cff71d5ff|Hspell:0|h[Nothing]|h|r", Parse::Malformed, 0, "a link with id 0");
    Expect(std::string("learnone ") + Mining + " please", Parse::Malformed, 0, "text after the link");
    Expect(std::string("learnone please ") + Mining, Parse::Malformed, 0, "text before the link");
    Expect(std::string("learnone ") + Mining + Mining, Parse::Ambiguous, 0, "two links");
    Expect(std::string("learnone ") + Mining + " |cff71d5ff|Hspell:3273|h[First Aid]|h|r", Parse::Ambiguous, 0,
           "two links with a space");
    Expect("learnone 2575 3273", Parse::Ambiguous, 0, "two numbers");
    Expect("learnone 2575 all", Parse::Ambiguous, 0, "a number and a word");

    static_assert(ParseLearnOne("learnone 2575").spellId == 2575, "usable at compile time");

    // What must be known afterwards (#68 comment 7083535451): the taught spell, not the trainer spell.
    using Effects = std::array<LearnEffect, 3>;
    Check(SpellsTaught(3273, Effects{}) == std::vector<std::uint32_t>{3273},
          "a direct trainer spell must itself be known");
    Check(SpellsTaught(2580, Effects{{{true, 2575}, {}, {}}}) == std::vector<std::uint32_t>{2575},
          "a learn-spell trainer spell must make its taught spell known, not itself");
    Check(SpellsTaught(1, Effects{{{true, 10}, {false, 99}, {true, 11}}}) == std::vector<std::uint32_t>{10, 11},
          "every learn effect counts; other effects do not");
    Check(SpellsTaught(1, Effects{{{true, 10}, {true, 0}, {}}}).empty(),
          "a learn effect without a spell makes the result unverifiable");

    std::set<std::uint32_t> known{10, 2575};
    auto knows = [&](std::uint32_t spell) { return known.count(spell) != 0; };
    Check(AllKnown({2575}, knows), "learned when the taught spell is known");
    Check(!AllKnown({10, 11}, knows), "not learned while any taught spell is missing");
    Check(!AllKnown({}, knows), "nothing to check is never success");

    std::cout << "checks=" << g_checks << " failures=" << g_failures << "\n";
    return g_failures == 0 ? 0 : 1;
}
