/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_TRAINER_LEARN_ONE_H
#define PLAYERBOTS_TRAINER_LEARN_ONE_H

#include <cstdint>
#include <limits>
#include <string_view>
#include <vector>

// "trainer learnone <spell>": teach exactly one trainer spell, or nothing.
//
// The older "trainer learn [link]" form reads its spell with Core's extractSpellIdFromLink, which takes
// the first word ("learn") when the text does not start with '|'. That gives 0, and 0 means "no
// filter", so the bot learns every spell the trainer can teach it (coordination #75, comment
// 7079993298). learnone parses its own argument and refuses anything but one spell: one spell, trade
// or enchant link, or one positive number, and nothing else around it.
namespace PlayerbotsTrainerLearnOne
{
enum class Parse : std::uint8_t
{
    NotLearnOne,  // the parameter is not a learnone command: the existing trainer behaviour applies
    Spell,        // exactly one spell id
    Missing,      // "learnone" with nothing after it
    Malformed,    // not one spell link or number (an item link, text, zero, a link that does not close)
    Ambiguous     // more than one spell
};

struct Result
{
    Parse status = Parse::NotLearnOne;
    std::uint32_t spellId = 0;
};

namespace Detail
{
constexpr bool Space(char c) { return c == ' ' || c == '\t'; }

constexpr std::string_view Trim(std::string_view text)
{
    while (!text.empty() && Space(text.front()))
        text.remove_prefix(1);
    while (!text.empty() && Space(text.back()))
        text.remove_suffix(1);
    return text;
}

// A positive decimal that fits uint32; 0 on anything else.
constexpr std::uint32_t Number(std::string_view digits)
{
    if (digits.empty() || digits.size() > 10)
        return 0;
    std::uint64_t value = 0;
    for (char c : digits)
    {
        if (c < '0' || c > '9')
            return 0;
        value = value * 10 + static_cast<std::uint64_t>(c - '0');
    }
    return value > std::numeric_limits<std::uint32_t>::max() ? 0 : static_cast<std::uint32_t>(value);
}

constexpr bool StartsWith(std::string_view text, std::string_view prefix)
{
    return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

// One whole chat link: [|cAARRGGBB]|H<type>:<id>[:...]|h[<name>]|h[|r]. Returns its spell id, or 0 when
// the text is not exactly one spell, trade or enchant link.
constexpr std::uint32_t Link(std::string_view text)
{
    if (StartsWith(text, "|c"))
    {
        if (text.size() < 10)
            return 0;
        text.remove_prefix(10);
    }
    std::string_view type;
    for (std::string_view candidate :
         {std::string_view("|Hspell:"), std::string_view("|Htrade:"), std::string_view("|Henchant:")})
        if (StartsWith(text, candidate))
            type = candidate;
    if (type.empty())
        return 0;
    text.remove_prefix(type.size());
    std::size_t const idEnd = text.find_first_of(":|");
    if (idEnd == std::string_view::npos)
        return 0;
    std::uint32_t const id = Number(text.substr(0, idEnd));
    std::size_t const nameStart = text.find("|h[");
    if (!id || nameStart == std::string_view::npos)
        return 0;
    std::size_t const nameEnd = text.find("]|h", nameStart + 3);
    if (nameEnd == std::string_view::npos)
        return 0;
    std::string_view rest = text.substr(nameEnd + 3);
    if (StartsWith(rest, "|r"))
        rest.remove_prefix(2);
    return rest.empty() ? id : 0;  // anything after the link (another link, text) is refused
}
}  // namespace Detail

// param: what follows "trainer" in the command.
constexpr Result ParseLearnOne(std::string_view param)
{
    using namespace Detail;
    constexpr std::string_view Keyword = "learnone";
    std::string_view text = Trim(param);
    if (!StartsWith(text, Keyword) || (text.size() > Keyword.size() && !Space(text[Keyword.size()])))
        return {};
    text = Trim(text.substr(Keyword.size()));
    if (text.empty())
        return {Parse::Missing, 0};
    std::size_t links = 0;
    for (std::size_t at = text.find("|H"); at != std::string_view::npos; at = text.find("|H", at + 2))
        ++links;
    if (links > 1)
        return {Parse::Ambiguous, 0};
    if (links == 0 && text.find_first_of(" \t") != std::string_view::npos)
        return {Parse::Ambiguous, 0};  // several words: two numbers, or a number and text
    std::uint32_t const id = links ? Link(text) : Number(text);
    if (!id)
        return {Parse::Malformed, 0};
    return {Parse::Spell, id};
}
// One effect of the trainer spell: does it teach another spell (SPELL_EFFECT_LEARN_SPELL), and which.
struct LearnEffect
{
    bool learnsSpell = false;
    std::uint32_t taughtSpell = 0;
};

// The spells the bot must know once the trainer spell is learned, so success is checked rather than
// assumed: the spells its learn effects teach (a profession rank, for example, teaches a different id),
// or the trainer spell itself when it has none. Empty when a learn effect names no spell: the result
// could not be verified, so the caller learns nothing.
template <class Effects>
std::vector<std::uint32_t> SpellsTaught(std::uint32_t trainerSpell, Effects const& effects)
{
    std::vector<std::uint32_t> taught;
    bool learnEffect = false;
    for (LearnEffect const& effect : effects)
    {
        if (!effect.learnsSpell)
            continue;
        learnEffect = true;
        if (!effect.taughtSpell)
            return {};
        taught.push_back(effect.taughtSpell);
    }
    if (!learnEffect)
        taught.push_back(trainerSpell);
    return taught;
}

// True when every taught spell is known; Known(spellId) asks the bot.
template <class Known>
bool AllKnown(std::vector<std::uint32_t> const& taught, Known&& known)
{
    if (taught.empty())
        return false;
    for (std::uint32_t spell : taught)
        if (!known(spell))
            return false;
    return true;
}
}  // namespace PlayerbotsTrainerLearnOne

#endif
