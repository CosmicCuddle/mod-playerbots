// PURE executable oracle. Does NOT instantiate, modify or save any Player.
#include <cstdint>
#include <cstdlib>
#include <cstdio>

static bool KeepOldRewarded(uint32_t id, bool isVerifiedRandomBot,
                            bool rewarded, uint32_t configuredMask)
{
    if (!isVerifiedRandomBot || !rewarded || id < 66001 || id > 66018)
        return false;
    return (configuredMask & (uint32_t(1) << (id - 66001))) != 0;
}

int main()
{
    unsigned long long checks = 0;
    // All 18 in-range marker IDs + two out-of-range controls;
    // Altbot/human/unknown must never preserve (false identity).
    for (uint32_t mask = 0; mask < 4096; ++mask)
        for (int bot = 0; bot < 2; ++bot)
            for (int rewarded = 0; rewarded < 2; ++rewarded)
                for (uint32_t id = 66000; id <= 66019; ++id)
                {
                    bool actual = KeepOldRewarded(id, bot != 0, rewarded != 0, mask);
                    bool expected = bot && rewarded && id >= 66001 && id <= 66018 &&
                        ((mask >> (id - 66001)) & 1u);
                    if (actual != expected)
                    {
                        std::fprintf(stderr, "FAIL: pure marker preservation policy\n");
                        return EXIT_FAILURE;
                    }
                    ++checks;
                }
    if (KeepOldRewarded(66008, true, true, 0) ||
        KeepOldRewarded(66008, false, true, UINT32_MAX) ||
        KeepOldRewarded(66008, true, false, UINT32_MAX) ||
        !KeepOldRewarded(66008, true, true, uint32_t(1) << 7))
        return EXIT_FAILURE;
    std::printf("PASS: %llu pure marker reset decisions, no grant or database action\n", checks);
    return EXIT_SUCCESS;
}
