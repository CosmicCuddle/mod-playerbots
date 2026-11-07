#ifndef COSMIC_F14_SYNTHETIC_DB_COMMIT_RECEIPT_TEST_MODEL_H
#define COSMIC_F14_SYNTHETIC_DB_COMMIT_RECEIPT_TEST_MODEL_H

// TEST ONLY. Does NOT use CharacterDatabase or call Player::SaveToDB.
// Demonstrates fail-closed ordering for hypothetical separate per-character
// transaction receipt + readback + real PB admission. This is NOT a durable
// entitlement/creation credential and MUST NOT be included from src/.
// None of the proofs currently come from owner PB #3's runtime factory.
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace CosmicF14TestOnly
{
enum class PriorData : std::uint8_t { Unknown, Absent, Present };

class SyntheticCommitProofLedger
{
public:
    // The three PriorData proofs are HYPOTHETICAL pre-creation inspections.
    // Real DB reads must be ordered/transactionally isolated before use.
    bool Begin(std::uint32_t guid, std::uint32_t account, std::uint8_t race,
               std::uint64_t transactionNonce, PriorData characterRow,
               PriorData questRows, PriorData paidRows)
    {
        if (!guid || !account || !transactionNonce || !SelectedRace(race) ||
            characterRow != PriorData::Absent ||
            questRows != PriorData::Absent || paidRows != PriorData::Absent)
            return false;
        std::lock_guard<std::mutex> hold(_mutex);
        if (_records.size() >= MaxRecords || _records.count(guid) ||
            _usedNonces.count(transactionNonce))
            return false;
        _usedNonces.insert(transactionNonce);
        _records.emplace(guid, Record{account, race, transactionNonce, Phase::Pending});
        return true;
    }

    // Must represent the ACTUAL creation transaction callback result,
    // not queue empty, SaveToDB return (void), cache insertion or login.
    bool ConfirmCommit(std::uint32_t guid, std::uint64_t transactionNonce,
                       bool actualCommitSucceeded)
    {
        std::lock_guard<std::mutex> hold(_mutex);
        Record* r = Match(guid, transactionNonce);
        if (!r || r->phase != Phase::Pending)
            return false;
        r->phase = actualCommitSucceeded ? Phase::Committed : Phase::Retired;
        return actualCommitSucceeded;
    }

    // Separate post-commit identity and old-state readback are necessary.
    // Passing false deliberately poisons the candidate, never rewrites DB.
    bool ConfirmReadback(std::uint32_t guid, std::uint32_t account,
                         std::uint8_t race, std::uint64_t transactionNonce,
                         bool exactSavedCharacterExists, bool noQuestHistory,
                         bool noPaidReceiptHistory)
    {
        std::lock_guard<std::mutex> hold(_mutex);
        Record* r = Match(guid, transactionNonce);
        if (!r || r->phase != Phase::Committed)
            return false;
        if (r->account != account || r->race != race ||
            !exactSavedCharacterExists || !noQuestHistory || !noPaidReceiptHistory)
        {
            r->phase = Phase::Retired;
            return false;
        }
        r->phase = Phase::Readback;
        return true;
    }

    // PB #3's REAL diagnostic observation, headlessness, AI, and world state
    // are inputs; do not claim an owner/DB result from these booleans alone.
    bool ObserveAdmission(std::uint32_t guid, std::uint32_t account,
                          std::uint8_t race, std::uint64_t transactionNonce,
                          bool pb3Admitted, bool headless, bool aiPresent,
                          bool inWorld, bool trueRandomBot)
    {
        if (!pb3Admitted || !headless || !aiPresent || !inWorld || !trueRandomBot)
            return false;
        std::lock_guard<std::mutex> hold(_mutex);
        Record* r = Match(guid, transactionNonce);
        if (!r || r->phase != Phase::Readback ||
            r->account != account || r->race != race)
            return false;
        r->phase = Phase::Admitted;
        return true;
    }

    // Evidence-ready FOR HUMAN REVIEW in this synthetic model only.
    // Never consume it as permission to set stage8 or hide an NPC.
    bool EvidenceCompleteForReview(std::uint32_t guid, std::uint32_t account,
                                   std::uint8_t race,
                                   std::uint64_t transactionNonce) const
    {
        std::lock_guard<std::mutex> hold(_mutex);
        auto const it = _records.find(guid);
        return it != _records.end() && it->second.account == account &&
            it->second.race == race && it->second.nonce == transactionNonce &&
            it->second.phase == Phase::Admitted;
    }

    // Every logout, creation failure, relog-before-readback or shutdown
    // must revoke the synthetic evidence. Tombstones forbid GUID reuse.
    void Retire(std::uint32_t guid)
    {
        std::lock_guard<std::mutex> hold(_mutex);
        auto it = _records.find(guid);
        if (it != _records.end())
            it->second.phase = Phase::Retired;
    }

    std::size_t RecordCount() const
    {
        std::lock_guard<std::mutex> hold(_mutex);
        return _records.size();
    }

private:
    static constexpr std::size_t MaxRecords = 16384;
    enum class Phase : std::uint8_t
    {
        Pending, Committed, Readback, Admitted, Retired
    };
    struct Record
    {
        std::uint32_t account;
        std::uint8_t race;
        std::uint64_t nonce;
        Phase phase;
    };

    static constexpr bool SelectedRace(std::uint8_t race)
    {
        return race == 10 || race == 11;
    }

    Record* Match(std::uint32_t guid, std::uint64_t transactionNonce)
    {
        auto it = _records.find(guid);
        return it != _records.end() && it->second.nonce == transactionNonce ?
            &it->second : nullptr;
    }

    mutable std::mutex _mutex;
    std::unordered_map<std::uint32_t, Record> _records;
    std::unordered_set<std::uint64_t> _usedNonces;
};
}
#endif
