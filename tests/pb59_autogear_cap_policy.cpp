#include "AutoGearProgressionCap.h"
#include <array>
#include <span>

using namespace PlayerbotsAutoGearCap;

// Synthetic later-era values exercise the resolver; they are not production defaults.
constexpr std::array<std::uint32_t, 19> completion = {
    66, 78, 78, 83, 83, 83, 88, 92, 92, 125, 141, 141, 156, 164, 224, 245, 258, 290, 290
};
constexpr Policy policy{std::span<std::uint32_t const>(completion), {0, 115, 200}, {92, 164, 290}};

static_assert(Resolve(0, 0, policy, 0).itemLevelCap == 66);
static_assert(Resolve(1, 1, policy, 0).itemLevelCap == 78);
static_assert(Resolve(13, 12, policy, 0).itemLevelCap == 164);
static_assert(Resolve(13, 13, policy, 0).itemLevelCap == 200);
static_assert(Resolve(13, 13, policy, 180).itemLevelCap == 180);
static_assert(Resolve(13, 13, policy, 0, 150).itemLevelCap == 150);
static_assert(Resolve(13, 13, policy, 180, 150).itemLevelCap == 150);
static_assert(Resolve(17, 17, policy, 0).itemLevelCap == 290);

constexpr std::array<std::uint32_t, 2> approvedOnly = {66, 78};
constexpr Policy incomplete{std::span<std::uint32_t const>(approvedOnly), {0, 0, 0}, {92, 0, 0}};
static_assert(!Resolve(8, 8, incomplete, 0).valid);
static_assert(!Resolve(0, 13, incomplete, 0).valid);
static_assert(!Resolve(13, 19, policy, 0).valid);
static_assert(!Resolve(0, 8, policy, 0).valid);
static_assert(!Resolve(12, 13, policy, 0).valid);
static_assert(Resolve(13, 12, policy, 0).valid);
constexpr Policy inverted{std::span<std::uint32_t const>(completion), {0, 200, 200}, {92, 164, 290}};
static_assert(!Resolve(13, 12, inverted, 0).valid);
static_assert(AllowsRawItemLevel(66, 66));
static_assert(!AllowsRawItemLevel(67, 66));
static_assert(AllowsRawItemLevel(290, 0));

// Exercise supported stages and the first unsupported stage. Caps
// must remain restrictive regardless of the configured/requested limit order.
constexpr bool VerifyStageAndLimitProperties()
{
    for (std::uint8_t raw = 0; raw <= LastSupportedStage + 1; ++raw)
        for (std::uint8_t effective = 0; effective <= LastSupportedStage + 1; ++effective)
        {
            auto const decision = Resolve(raw, effective, policy, 0);
            bool const expectedValid = raw <= LastSupportedStage && effective <= raw;
            if (decision.valid != expectedValid)
                return false;
            if (!decision.valid)
            {
                if (decision.itemLevelCap != 0)
                    return false;
                continue;
            }

            auto const configured = Resolve(raw, effective, policy, 70);
            auto const requested = Resolve(raw, effective, policy, 0, 70);
            auto const both = Resolve(raw, effective, policy, 80, 70);
            auto const reversed = Resolve(raw, effective, policy, 70, 80);
            if (!decision.itemLevelCap || !configured.valid || !requested.valid || !both.valid || !reversed.valid ||
                configured.itemLevelCap > decision.itemLevelCap || configured.itemLevelCap > 70 ||
                configured.itemLevelCap != requested.itemLevelCap ||
                both.itemLevelCap != configured.itemLevelCap || reversed.itemLevelCap != both.itemLevelCap)
                return false;
        }
    return true;
}

static_assert(VerifyStageAndLimitProperties());
static_assert(!Resolve(255, 0, policy, 0).valid);
static_assert(!Resolve(18, 255, policy, 0).valid);

int main() { return 0; }
