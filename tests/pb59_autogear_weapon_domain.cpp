#include "AutoGearWeaponDomain.h"

using namespace PlayerbotsAutoGearWeapons;

// Synthetic capability sets; production must derive masks from actual Core eligibility.
constexpr auto one = Bit(Kind::OneHand);
constexpr auto two = Bit(Kind::TwoHand);
constexpr Capabilities twoHander{one | two, one | two, Bit(Kind::Thrown), true, false, true, true, false};
constexpr auto twoHandPlan = Build(twoHander, Kind::TwoHand);
static_assert(twoHandPlan.valid);
static_assert(twoHandPlan.offhand.Accepts(false, Kind::Count));
static_assert(!twoHandPlan.offhand.Accepts(true, Kind::OneHand));
constexpr Capabilities titan{one | two, one | two, 0, true, true, true, true, false};
constexpr auto titanPlan = Build(titan, Kind::TwoHand);
static_assert(titanPlan.valid && titanPlan.offhand.Accepts(true, Kind::TwoHand));
static_assert(!titanPlan.offhand.Accepts(false, Kind::Count));
constexpr Capabilities dual{one, one, Bit(Kind::Thrown), true, false, true, true, false};
static_assert(Build(dual, Kind::OneHand).offhand.Accepts(true, Kind::OneHand));
static_assert(!Build(dual, Kind::TwoHand).valid);
constexpr Capabilities caster{one | two, Bit(Kind::Holdable), Bit(Kind::Wand), false, false, true, false, false};
static_assert(Build(caster, Kind::OneHand).offhand.Accepts(true, Kind::Holdable));
static_assert(Build(caster, Kind::OneHand).ranged.Accepts(true, Kind::Wand));
static_assert(!Build(caster, Kind::OneHand).ranged.Accepts(true, Kind::Gun));
static_assert(!Build(caster, Kind::OneHand).ranged.Accepts(true, Kind::Relic));
constexpr Capabilities relicUser{one | two, 0, Bit(Kind::Relic), false, false, true, false, false};
static_assert(Build(relicUser, Kind::TwoHand).ranged.Accepts(true, Kind::Relic));
static_assert(Build(relicUser, Kind::TwoHand).ranged.Accepts(false, Kind::Count));
constexpr Capabilities requiredRanged{one, 0, 0, false, false, true, false, true};
static_assert(!Build(requiredRanged, Kind::OneHand).valid);
constexpr Capabilities optionalEmpty{};
static_assert(Build(optionalEmpty, Kind::Count).valid);
static_assert(!Build(optionalEmpty, Kind::Count).mainhand.Accepts(true, Kind::OneHand));
constexpr Capabilities noDual{one, one, 0, false, false, true, true, false};
static_assert(!Build(noDual, Kind::OneHand).valid);
static_assert(Bit(static_cast<Kind>(255)) == 0);

int main() { return 0; }
