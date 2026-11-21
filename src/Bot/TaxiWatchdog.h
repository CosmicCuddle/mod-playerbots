/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_TAXI_WATCHDOG_H
#define PLAYERBOTS_TAXI_WATCHDOG_H

#include <cstdint>

// Detects a bot that is still in taxi state but has made no flight progress for a configured time.
// Progress means the flight generator is active, its spline is moving, and the route node changed or
// the bot moved; a bot walking on the ground with stale taxi flags is not progress. Pure: the caller
// samples the Player and performs the recovery.
namespace PlayerbotsTaxiWatchdog
{
struct Config
{
    bool enabled = false;
    std::uint64_t stuckMs = 900000;     // time without progress before recovery
    std::uint64_t cooldownMs = 600000;  // minimum gap between two recoveries of one bot
    std::uint32_t maxRecoveries = 3;    // per window; then give up and log once
    std::uint64_t windowMs = 3600000;
    float progressYards = 5.0f;
};

struct Sample
{
    std::uint64_t nowMs = 0;
    bool eligible = false;      // a bot (not a human or self-bot), alive and in world
    bool transitional = false;  // being teleported or changing map
    bool taxiState = false;     // in-flight unit state, taxi unit flag, or queued taxi destinations
    bool flightGenerator = false;
    bool splineMoving = false;
    std::uint32_t mapId = 0;
    std::uint32_t node = 0;  // flight generator current node
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

enum class Action : std::uint8_t
{
    None,
    Recover,
    GiveUp  // returned once when the recovery budget is spent; logging only
};

class Watchdog
{
public:
    Action Observe(Sample const& sample, Config const& config)
    {
        if (!config.enabled || !sample.eligible || !sample.taxiState)
        {
            // Route completed, feature off, or not a bot we act on: forget this flight.
            _tracking = false;
            _gaveUp = false;
            return Action::None;
        }
        if (!_tracking || sample.transitional || sample.mapId != _mapId)
        {
            // A new flight, a teleport or a cross-map hop restarts the clock.
            Start(sample);
            return Action::None;
        }
        if (Progressed(sample, config))
        {
            Start(sample);
            return Action::None;
        }
        if (sample.nowMs - _lastProgressMs < config.stuckMs)
            return Action::None;
        if (_gaveUp)
            return Action::None;

        ForgetOldRecoveries(sample.nowMs, config);
        if (_recoveries >= config.maxRecoveries)
        {
            _gaveUp = true;
            return Action::GiveUp;
        }
        if (_recoveries && sample.nowMs - _lastRecoveryMs < config.cooldownMs)
            return Action::None;
        return Action::Recover;
    }

    // The caller ran a recovery attempt; the next one needs another full stuck period.
    void Recovered(std::uint64_t nowMs)
    {
        if (!_recoveries)
            _windowStartMs = nowMs;
        ++_recoveries;
        _lastRecoveryMs = nowMs;
        _lastProgressMs = nowMs;
    }

    bool Tracking() const { return _tracking; }
    std::uint64_t LastProgressMs() const { return _lastProgressMs; }
    std::uint32_t Recoveries() const { return _recoveries; }

private:
    void Start(Sample const& sample)
    {
        _tracking = true;
        _lastProgressMs = sample.nowMs;
        _mapId = sample.mapId;
        _node = sample.node;
        _x = sample.x;
        _y = sample.y;
        _z = sample.z;
    }

    bool Progressed(Sample const& sample, Config const& config) const
    {
        if (!sample.flightGenerator || !sample.splineMoving)
            return false;
        if (sample.node != _node)
            return true;
        float const dx = sample.x - _x;
        float const dy = sample.y - _y;
        float const dz = sample.z - _z;
        return dx * dx + dy * dy + dz * dz >= config.progressYards * config.progressYards;
    }

    void ForgetOldRecoveries(std::uint64_t nowMs, Config const& config)
    {
        if (_recoveries && nowMs - _windowStartMs >= config.windowMs)
            _recoveries = 0;
    }

    bool _tracking = false;
    bool _gaveUp = false;
    std::uint64_t _lastProgressMs = 0;
    std::uint32_t _mapId = 0;
    std::uint32_t _node = 0;
    float _x = 0.0f;
    float _y = 0.0f;
    float _z = 0.0f;
    std::uint32_t _recoveries = 0;
    std::uint64_t _windowStartMs = 0;
    std::uint64_t _lastRecoveryMs = 0;
};
}  // namespace PlayerbotsTaxiWatchdog

#endif
