// ADR-1664: the ribbons' churn offset -- a phase, so routable without the jump a routed speed makes (E4). Packed in a
// free lane (audio3.z) only while ribbons are on; written to the file only when set, so no file without it changes.
// tests/rendering/test_aurora_ribbons_gpu.cpp holds 0 to the ribbons' hash.
#include "world/atmospherics.hpp"
#include "world/effects/effect_instance.hpp"
#include "world/effects/effect_registry.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <array>
#include <limits>

using namespace avgen;

namespace {
world::AtmosphericFrame frameOf(const world::EffectInstance& e) {
    const std::array<world::EffectInstance, 1> set{e};
    world::EffectContext ctx;
    ctx.seconds = 3.0;
    world::AtmosphericFrame frame;
    world::buildAtmosphericFrame(set, ctx, frame);
    return frame;
}
} // namespace

TEST_CASE("ADR-1664: the ribbons' churn offset packs in its lane, validates and is written only when set",
          "[world][atmospherics][aurora][adr1664]") {
    world::EffectInstance e = world::glowmereAurora("sky");
    e.timing.fadeIn = 0.0;
    e.aurora.shape.ribbons = true;
    REQUIRE(frameOf(e).auroras[0].audio3.z == 0.0f);
    const nlohmann::json plain = e.toJson();
    CHECK_FALSE(plain.at("parameters").at("shape").contains("ribbonChurn"));

    e.aurora.shape.ribbonChurn = 7.5f;
    REQUIRE(e.validate().has_value());
    CHECK(frameOf(e).auroras[0].audio3.z == 7.5f);
    const nlohmann::json set = e.toJson();
    CHECK(set.at("parameters").at("shape").at("ribbonChurn").get<float>() == 7.5f);
    const auto back = world::EffectInstance::fromJson(set);
    REQUIRE(back.has_value());
    CHECK(back->aurora.shape.ribbonChurn == 7.5f);

    // the curtains (ribbons off) never read it: the lane stays zero
    e.aurora.shape.ribbons = false;
    CHECK(frameOf(e).auroras[0].audio3.z == 0.0f);

    e.aurora.shape.ribbons = true;
    e.aurora.shape.ribbonChurn = std::numeric_limits<float>::infinity();
    CHECK_FALSE(e.validate().has_value());
}
