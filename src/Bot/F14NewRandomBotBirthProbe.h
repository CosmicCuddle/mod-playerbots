#ifndef COSMIC_F14_NEW_RANDOM_BOT_BIRTH_PROBE_H
#define COSMIC_F14_NEW_RANDOM_BOT_BIRTH_PROBE_H

// CosmicCuddle F14: default-OFF, single-process, diagnostic creation-to-login
// handoff. Never an IP stage grant, Core visibility decision or persistent
// proof of a committed character DB row. No Player/Session pointers retained.
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace CosmicF14
{
class NewRandomBotBirthProbe
{
public:
    static NewRandomBotBirthProbe& Instance()
    {
        static NewRandomBotBirthProbe probe;
        return probe;
    }

    // Called only by the actual RandomPlayerbotFactory creation path, after
    // queuing SaveToDB and inserting the character cache entry. This is only
    // creation CANDIDACY; the asynchronous DB transaction is not proved.
    bool RecordCandidate(std::uint32_t guid, std::uint32_t account, std::uint8_t race)
    {
        if (!guid || !account || !IsSelectedRace(race))
            return false;

        std::lock_guard<std::mutex> lock(_lock);
        if (_records.size() >= MaxCandidates || _records.find(guid) != _records.end())
            return false;

        _records.emplace(guid, Record{account, race, false, false, false});
        return true;
    }

    // A positive world-thread login observation requires every caller flag.
    // This only correlates an actual in-process new creation with a later
    // admission; it does NOT issue a trusted progression/visibility permit.
    bool ObserveAdmission(std::uint32_t guid, std::uint32_t account,
                          std::uint8_t race, bool verifiedRandomBot,
                          bool headless, bool worldPresent, bool hasAI)
    {
        if (!guid || !account || !IsSelectedRace(race) ||
            !verifiedRandomBot || !headless || !worldPresent || !hasAI)
            return false;

        std::lock_guard<std::mutex> lock(_lock);
        auto it = _records.find(guid);
        if (it == _records.end() || it->second.Account != account ||
            it->second.Race != race || it->second.Consumed || it->second.Retired)
            return false;

        it->second.Consumed = true;
        return true;
    }

    // Separate database observation, NOT a first-born/stage entitlement.
    // The factory may set this only after the exact per-character transaction
    // callback reported success AND an independent row readback matched
    // GUID, account, race and name. Old orphan quest/paid rows are NOT ruled
    // out by this observation. The audit is optional/OFF by default.
    bool MarkCommittedRowReadback(std::uint32_t guid, std::uint32_t account,
                                  std::uint8_t race)
    {
        std::lock_guard<std::mutex> lock(_lock);
        auto it = _records.find(guid);
        if (it == _records.end() || it->second.Account != account ||
            it->second.Race != race || it->second.Retired ||
            it->second.CommittedRowReadback)
            return false;
        it->second.CommittedRowReadback = true;
        return true;
    }

    // Stronger than same-process admission alone, but STILL not permission
    // for stage8, WHO suppression, human/NPC concealment or paid entitlement.
    // Survives neither process restart nor retirement; no DB writes.
    bool HasAuditedRowAndUnretiredAdmission(std::uint32_t guid,
                                             std::uint32_t account,
                                             std::uint8_t race) const
    {
        std::lock_guard<std::mutex> lock(_lock);
        auto it = _records.find(guid);
        return it != _records.end() && it->second.Account == account &&
            it->second.Race == race && it->second.CommittedRowReadback &&
            it->second.Consumed && !it->second.Retired;
    }

    // Idempotent and fail-closed: old login callbacks cannot revive a retired
    // entry. Retain the consumed/retired tombstone for process lifetime.
    void Retire(std::uint32_t guid)
    {
        std::lock_guard<std::mutex> lock(_lock);
        auto it = _records.find(guid);
        if (it != _records.end())
            it->second.Retired = true;
    }

    bool HasUnretiredAdmission(std::uint32_t guid, std::uint32_t account,
                               std::uint8_t race) const
    {
        std::lock_guard<std::mutex> lock(_lock);
        auto it = _records.find(guid);
        return it != _records.end() && it->second.Account == account &&
               it->second.Race == race && it->second.Consumed && !it->second.Retired;
    }

    std::size_t CandidateCount() const
    {
        std::lock_guard<std::mutex> lock(_lock);
        return _records.size();
    }

private:
    struct Record
    {
        std::uint32_t Account;
        std::uint8_t Race;
        bool Consumed;
        bool Retired;
        bool CommittedRowReadback;
    };

    static constexpr std::size_t MaxCandidates = 16384;
    static constexpr bool IsSelectedRace(std::uint8_t race)
    {
        return race == 10 || race == 11; // 3.3.5a Blood Elf / Draenei
    }

    NewRandomBotBirthProbe() = default;
    NewRandomBotBirthProbe(NewRandomBotBirthProbe const&) = delete;
    NewRandomBotBirthProbe& operator=(NewRandomBotBirthProbe const&) = delete;

    mutable std::mutex _lock;
    std::unordered_map<std::uint32_t, Record> _records;
};
}
#endif
