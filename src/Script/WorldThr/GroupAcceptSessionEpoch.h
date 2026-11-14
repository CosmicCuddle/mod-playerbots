/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_GROUPACCEPTSESSIONEPOCH_H
#define PLAYERBOTS_GROUPACCEPTSESSIONEPOCH_H

#include "ObjectGuid.h"
#include <cstdint>
#include <limits>
#include <mutex>
#include <unordered_map>

class GroupAcceptSessionEpoch
{
public:
    static GroupAcceptSessionEpoch& Instance()
    {
        static GroupAcceptSessionEpoch epochs;
        return epochs;
    }

    std::uint64_t Issue(ObjectGuid guid)
    {
        if (!guid.IsPlayer())
            return 0;
        std::lock_guard<std::mutex> lock(_mutex);
        if (_next == std::numeric_limits<std::uint64_t>::max())
            return 0;
        std::uint64_t epoch = _next++;
        _current[guid.GetRawValue()] = epoch;
        return epoch;
    }

    std::uint64_t Current(ObjectGuid guid) const
    {
        std::lock_guard<std::mutex> lock(_mutex);
        auto const it = _current.find(guid.GetRawValue());
        return it == _current.end() ? 0 : it->second;
    }

    void Retire(ObjectGuid guid)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _current.erase(guid.GetRawValue());
    }

private:
    GroupAcceptSessionEpoch() = default;
    mutable std::mutex _mutex;
    std::unordered_map<std::uint64_t, std::uint64_t> _current;
    std::uint64_t _next = 1;
};

#endif
