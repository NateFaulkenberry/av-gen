// Stars (Effect Library Wave 2): the star field's builder. One field per sky, a named drop for a
// second, the gate, the wrapped second, and the default routes.

#include "world/atmospherics.hpp"
#include "world/effects/effect_instance.hpp"
#include "world/effects/effect_registry.hpp"
#include "world/effects/effect_stack.hpp"
#include "world/effects/star_field.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string>
#include <vector>

using namespace avgen;

namespace {

world::EffectInstance stars(const char* name, const char* style = nullptr) {
    world::EffectInstance e = world::makeEffect(world::EffectKind::Stars, name);
    if (style != nullptr) {
        REQUIRE(world::applyEffectStyle(e, world::EffectKind::Stars, style));
    }
    return e;
}

struct Built {
    world::StarField field;
    std::vector<world::EffectStatus> status;
    std::vector<std::string> reasons;
};

Built build(const std::vector<world::EffectInstance>& list, double seconds) {
    Built b;
    b.status.assign(list.size(), world::EffectStatus::Dormant);
    b.reasons.assign(list.size(), std::string());
    world::EffectContext ctx;
    ctx.seconds = seconds;
    world::buildStarField(list, ctx, b.field, {}, b.status, b.reasons);
    return b;
}

} // namespace

TEST_CASE("Stars: no instance is the background's own field; one draws its settings", "[stars][effects]") {
    const Built none = build({}, 10.0);
    CHECK_FALSE(none.field.on);

    const Built one = build({stars("Night", "Deep Space")}, 10.0);
    REQUIRE(one.field.on);
    CHECK(one.status[0] == world::EffectStatus::Drawn);
    CHECK(one.field.density == Catch::Approx(0.04f));
    CHECK(one.field.band == Catch::Approx(1.2f));
    CHECK(one.field.envelope == Catch::Approx(1.0f));

    world::EffectInstance off = stars("Off");
    off.enabled = false;
    const Built disabled = build({off}, 10.0);
    CHECK_FALSE(disabled.field.on);
    CHECK(disabled.status[0] == world::EffectStatus::Disabled);
}

TEST_CASE("Stars: a sky has one field -- the second is dropped and told which one drew",
          "[stars][effects]") {
    const Built b = build({stars("First"), stars("Second", "Deep Space")}, 5.0);
    REQUIRE(b.field.on);
    CHECK(b.field.density == Catch::Approx(0.015f)); // the first's, not a blend
    CHECK(b.status[0] == world::EffectStatus::Drawn);
    CHECK(b.status[1] == world::EffectStatus::Dropped);
    CHECK(b.reasons[1].find("'First'") != std::string::npos);
}

TEST_CASE("Stars: the second the shader is handed wraps at the twinkle period", "[stars][effects]") {
    const Built early = build({stars("S")}, 3.25);
    const Built late = build({stars("S")}, 3.25 + 4.0 * world::kTwinklePeriod);
    CHECK(early.field.seconds == Catch::Approx(3.25f));
    CHECK(late.field.seconds == Catch::Approx(3.25f).margin(1e-3));
    CHECK(late.field.seconds < static_cast<float>(world::kTwinklePeriod));
}

TEST_CASE("Stars: an Entity owner draws nothing", "[stars][effects]") {
    world::EffectInstance e = stars("Owned");
    e.owner = world::EffectOwner::entity("rock");
    const Built b = build({e}, 1.0);
    CHECK_FALSE(b.field.on);
    world::EffectContext ctx;
    ctx.seconds = 1.0;
    CHECK(world::starFieldRecords(e, ctx) == 0u);
    CHECK(world::starFieldRecords(stars("World"), ctx) == 1u);
}
