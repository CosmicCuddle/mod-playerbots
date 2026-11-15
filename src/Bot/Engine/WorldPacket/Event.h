/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_EVENT_H
#define PLAYERBOTS_EVENT_H

#include "WorldPacket.h"
#include "ObjectGuid.h"

class Player;

class Event
{
public:
    Event(Event const& other) = default;
    Event& operator=(Event const& other) = default;
    Event() {}
    Event(std::string const source) : source(source) {}
    Event(std::string const source, std::string const param, Player* owner = nullptr)
        : source(source), param(param), owner(owner)
    {
    }
    Event(std::string const source, std::string const param, Player* owner, ObjectGuid ownerGuid)
        : source(source), param(param), owner(owner), ownerGuid(ownerGuid)
    {
    }
    Event(std::string const source, WorldPacket& packet, Player* owner = nullptr)
        : source(source), packet(packet), owner(owner)
    {
    }
    Event(std::string const source, ObjectGuid object, Player* owner = nullptr);
    virtual ~Event() {}

    std::string const GetSource() { return source; }
    std::string const getParam() { return param; }
    WorldPacket& getPacket() { return packet; }
    ObjectGuid getObject();
    Player* getOwner() { return owner; }
    ObjectGuid GetOwnerGuid() const { return ownerGuid; }
    bool operator!() const { return source.empty(); }

protected:
    std::string source;
    std::string param;
    WorldPacket packet;
    Player* owner = nullptr;
    ObjectGuid ownerGuid;
};

#endif
