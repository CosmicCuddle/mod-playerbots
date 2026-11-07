#ifndef COSMIC_F14_TEST_ONLY_IMMUTABLE_RANDOMBOT_SNAPSHOT_H
#define COSMIC_F14_TEST_ONLY_IMMUTABLE_RANDOMBOT_SNAPSHOT_H

// TEST-ONLY proof-of-concept. Never called by Core, IP or Playerbots runtime.
// In a future implementation, verified facts must be minted on an authorized
// world-thread/lifecycle path; map workers may READ ONLY the published copy.
// No Player*, WorldSession*, mutable currentBots, database or game API here.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace CosmicF14Prototype
{
struct Candidate
{
    std::uint32_t Guid = 0;
    std::uint64_t Incarnation = 0; // external unique monotonic incarnation, never GUID alone
    std::uint32_t Account = 0;
    std::uint8_t Race = 0;
    bool Headless = false;
    bool PlayerbotAI = false;
    bool SelfBot = false;
    bool InRandomAccountList = false;
    bool InActiveRandomBotList = false;
};

struct Stamp
{
    std::uint64_t Incarnation;
    std::uint32_t Account;
    std::uint8_t Race;
};

class ImmutableVerifiedRandomBotSnapshot
{
public:
    // Important: positive provenance must be established by caller before
    // invocation, on a safe producer thread. The booleans aren't a trust
    // boundary or proof of account/player lifetime.
    bool Publish(Candidate const& c)
    {
        if (c.Guid == 0 || c.Incarnation == 0 || c.Account == 0 ||
            (c.Race != 10 && c.Race != 11) || !c.Headless ||
            !c.PlayerbotAI || c.SelfBot || !c.InRandomAccountList ||
            !c.InActiveRandomBotList)
            return false;

        std::lock_guard<std::mutex> guard(_writer);
        // A retired entry is absent from the visible snapshot. Keep the last
        // accepted incarnation outside that view, or a delayed login event
        // could re-publish the *same* already retired bot identity.
        auto high = _lastAcceptedIncarnation.find(c.Guid);
        if (high != _lastAcceptedIncarnation.end() &&
            c.Incarnation <= high->second)
            return false;

        auto old = LoadView();
        auto it = old->find(c.Guid);
        if (it != old->end() && it->second.Incarnation >= c.Incarnation)
            return false; // no duplicate, no stale generation overwrite

        auto next = std::make_shared<Table>(*old);
        (*next)[c.Guid] = {c.Incarnation, c.Account, c.Race};
        // Under the writer mutex, retain this high-water mark even after a
        // later Retire() removes the actor from readers' immutable view.
        // If bookkeeping fails, no new snapshot has been published yet.
        _lastAcceptedIncarnation[c.Guid] = c.Incarnation;
        std::shared_ptr<const Table> published = std::move(next);
        PublishView(std::move(published));
        return true;
    }

    bool Retire(std::uint32_t guid, std::uint64_t incarnation)
    {
        if (!guid || !incarnation)
            return false;
        std::lock_guard<std::mutex> guard(_writer);
        auto old = LoadView();
        auto it = old->find(guid);
        if (it == old->end() || it->second.Incarnation != incarnation)
            return false; // stale logout cannot evict a newer incarnation

        auto next = std::make_shared<Table>(*old);
        next->erase(guid);
        std::shared_ptr<const Table> published = std::move(next);
        PublishView(std::move(published));
        return true;
    }

    // No game object or manager mutation is accessed on reader threads.
    // Even TRUE must be independently revalidated against a current
    // living Player/session/incarnation in a serialized Core map pass.
    bool IsVerified(std::uint32_t guid, std::uint64_t incarnation,
                    std::uint32_t account, std::uint8_t race) const
    {
        if (!guid || !incarnation || !account || (race != 10 && race != 11))
            return false;
        auto view = LoadView();
        auto it = view->find(guid);
        return it != view->end() &&
               it->second.Incarnation == incarnation &&
               it->second.Account == account && it->second.Race == race;
    }

    std::size_t Size() const
    {
        return LoadView()->size();
    }

private:
    using Table = std::unordered_map<std::uint32_t, Stamp>;
    // The publication mutex is held only to copy/swap the shared_ptr.
    // Reader traversal happens after releasing it, on a CONST table.
    // This avoids deprecated shared_ptr atomic free functions in C++20,
    // while supporting the same immutable snapshot semantics in C++17.
    std::shared_ptr<const Table> LoadView() const
    {
        std::lock_guard<std::mutex> lock(_publication);
        return _snapshot;
    }

    void PublishView(std::shared_ptr<const Table> published)
    {
        std::lock_guard<std::mutex> lock(_publication);
        _snapshot = std::move(published);
    }

    mutable std::mutex _writer;
    // Test-only, writer-guarded per-GUID replay fence. Not production ready:
    // unbounded over all distinct GUIDs; later design needs safe GC/persistence
    // and a non-recycled unique epoch across restarts.
    std::unordered_map<std::uint32_t, std::uint64_t> _lastAcceptedIncarnation;
    mutable std::mutex _publication;
    std::shared_ptr<const Table> _snapshot = std::make_shared<Table>();
};
} // namespace CosmicF14Prototype
#endif
