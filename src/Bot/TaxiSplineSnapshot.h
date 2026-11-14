/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_TAXI_SPLINE_SNAPSHOT_H
#define PLAYERBOTS_TAXI_SPLINE_SNAPSHOT_H

#include "Log.h"
#include "MotionMaster.h"
#include "MoveSpline.h"
#include "Player.h"
#include <string>
#include <utility>

// Records only a transition away from an active taxi spline during a PB call.
// It does not change movement state or attempt to recover the flight.
class TaxiSplineSnapshot
{
public:
    TaxiSplineSnapshot(Player* player, std::string context) : _bot(player), _context(std::move(context))
    {
        if (!_bot || !_bot->IsInFlight() || !_bot->movespline || _bot->movespline->Finalized())
            return;

        _mapId = _bot->GetMapId();
        _splineId = _bot->movespline->GetId();
        _motionType = _bot->GetMotionMaster()
            ? _bot->GetMotionMaster()->GetCurrentMovementGeneratorType()
            : IDLE_MOTION_TYPE;
        _active = true;
    }

    ~TaxiSplineSnapshot()
    {
        if (!_active || !_bot->IsInWorld() || _bot->GetMapId() != _mapId)
            return;

        bool const finalized = !_bot->movespline || _bot->movespline->Finalized();
        uint32 const currentSplineId = _bot->movespline ? _bot->movespline->GetId() : 0;
        MovementGeneratorType const currentMotionType = _bot->GetMotionMaster()
            ? _bot->GetMotionMaster()->GetCurrentMovementGeneratorType()
            : IDLE_MOTION_TYPE;

        if (!finalized && currentSplineId == _splineId && currentMotionType == _motionType)
            return;

        LOG_WARN("playerbots", "Taxi spline transition: bot={} context={} map={} spline={}->{} finalized={} "
            "motion={}->{} inFlight={}",
            _bot->GetGUID().ToString(), _context, _mapId, _splineId, currentSplineId, finalized,
            static_cast<uint32>(_motionType), static_cast<uint32>(currentMotionType), _bot->IsInFlight());
    }

    TaxiSplineSnapshot(TaxiSplineSnapshot const&) = delete;
    TaxiSplineSnapshot& operator=(TaxiSplineSnapshot const&) = delete;

private:
    Player* _bot;
    std::string _context;
    uint32 _mapId = 0;
    uint32 _splineId = 0;
    MovementGeneratorType _motionType = IDLE_MOTION_TYPE;
    bool _active = false;
};

#endif
