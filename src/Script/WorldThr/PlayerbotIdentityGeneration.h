/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_PLAYERBOTIDENTITYGENERATION_H
#define PLAYERBOTS_PLAYERBOTIDENTITYGENERATION_H

#include "ObjectGuid.h"
#include <cstdint>
#include <limits>
#include <mutex>
#include <unordered_map>

class PlayerbotIdentityGeneration
{
public:
    static PlayerbotIdentityGeneration& Instance()
    {
        static PlayerbotIdentityGeneration generations;
        return generations;
    }

    std::uint64_t IssueLogin(ObjectGuid guid)
    {
        if (!guid.IsPlayer())
            return 0;
        std::lock_guard<std::mutex> lock(_mutex);
        std::uint64_t const key = guid.GetRawValue();
        _invitations.erase(key);
        if (_nextLogin == std::numeric_limits<std::uint64_t>::max())
        {
            _logins.erase(key);
            return 0;
        }
        std::uint64_t generation = _nextLogin++;
        _logins[key] = generation;
        return generation;
    }

    std::uint64_t CurrentLogin(ObjectGuid guid) const
    {
        std::lock_guard<std::mutex> lock(_mutex);
        auto const it = _logins.find(guid.GetRawValue());
        return it == _logins.end() ? 0 : it->second;
    }

    void RetireLogin(ObjectGuid guid)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        std::uint64_t const key = guid.GetRawValue();
        _logins.erase(key);
        _invitations.erase(key);
    }

    std::uint64_t IssueInvitation(ObjectGuid guid)
    {
        if (!guid.IsPlayer())
            return 0;
        std::lock_guard<std::mutex> lock(_mutex);
        std::uint64_t const key = guid.GetRawValue();
        if (_nextInvitation == std::numeric_limits<std::uint64_t>::max())
        {
            _invitations.erase(key);
            return 0;
        }
        std::uint64_t generation = _nextInvitation++;
        _invitations[key] = generation;
        return generation;
    }

    std::uint64_t CurrentInvitation(ObjectGuid guid) const
    {
        std::lock_guard<std::mutex> lock(_mutex);
        auto const it = _invitations.find(guid.GetRawValue());
        return it == _invitations.end() ? 0 : it->second;
    }

private:
    PlayerbotIdentityGeneration() = default;
    mutable std::mutex _mutex;
    std::unordered_map<std::uint64_t, std::uint64_t> _logins;
    std::unordered_map<std::uint64_t, std::uint64_t> _invitations;
    std::uint64_t _nextLogin = 1;
    std::uint64_t _nextInvitation = 1;
};

#endif
