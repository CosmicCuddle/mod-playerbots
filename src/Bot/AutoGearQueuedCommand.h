#pragma once
#include <cstdint>
#include <string_view>

namespace PB59
{
struct CommandOwner
{
    std::uint64_t guid = 0, player = 0, session = 0;
    bool Valid() const { return guid && player && session; }
    bool operator==(CommandOwner const&) const = default;
};

struct AutoGearCommandText
{
    bool recognized = false;
    bool bis = false;
    std::string_view argument;
};

inline AutoGearCommandText ParseQueuedAutoGear(std::string_view text)
{
    if (text == "autogear")
        return {true, false, {}};
    if (!text.starts_with("autogear "))
        return {};
    text.remove_prefix(9);
    while (!text.empty() && text.front() == ' ')
        text.remove_prefix(1);
    if (text == "bis")
        return {true, true, {}};
    if (text.starts_with("bis "))
    {
        text.remove_prefix(4);
        return {true, true, text};
    }
    return {true, false, text};
}

// A command accepted under the new path never falls back to destructive legacy
// gear changes if the flag is turned off while the command waits.
inline bool UsesQueuedAutoGear(bool acceptedCapped, bool enabled, std::string_view text)
{
    return acceptedCapped || (enabled && ParseQueuedAutoGear(text).recognized);
}
}
