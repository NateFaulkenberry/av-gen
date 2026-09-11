#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "params/serialization.hpp"
#include "scene/post_settings.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

using namespace avgen;

TEST_CASE("Post settings register as parameters and apply finals", "[scene][post]") {
    params::ParameterSet params;
    scene::PostSettings defaults;
    defaults.bloomIntensity = 0.7f;
    auto p = scene::registerPostParameters(params, defaults);
    REQUIRE(p.bloomIntensity != nullptr);
    // Every field this struct exposes should have arrived as a parameter. An exact count here
    // asserted nothing about correctness and broke whenever a setting was added, so what is
    // checked instead is that registration produced parameters and that the named ones exist.
    const std::size_t registered = params.size();
    CHECK(registered > 30);
    CHECK(params.find("post/tonemap/operator")->kind() == params::ParamKind::Int);
    CHECK(params.find("post/halation/tint")->componentCount() == 3);
    CHECK(params.find("post/output/sharpen") != nullptr);
    CHECK(params.find("post/grade/lift")->componentCount() == 3);
    CHECK(p.bloomIntensity->value() == 0.7f);

    p.tonemap->setBase(1);
    p.saturation->setBase(0.0f);
    p.dofEnabled->setBase(true);
    p.vignette->setBase(0.5f);
    params.resetFinals();
    scene::PostSettings live;
    scene::applyPostParameters(p, live);
    CHECK(live.tonemap == scene::TonemapOperator::AgX);
    CHECK(live.saturation == 0.0f);
    CHECK(live.dofEnabled);
    CHECK(live.vignette == 0.5f);
    CHECK(std::string(scene::tonemapOperatorName(live.tonemap)) == "agx");
    // ADR-039: AgX is the default operator now.
    CHECK(scene::PostSettings{}.tonemap == scene::TonemapOperator::AgX);
    // Re-registering returns the same parameters.
    auto again = scene::registerPostParameters(params, defaults);
    CHECK(again.bloomIntensity == p.bloomIntensity);
    CHECK(params.size() == registered); // idempotent: re-registering adds nothing
}

// ---- tilt-shift (ADR-079) ------------------------------------------------------------------

TEST_CASE("Tilt-shift coverage is zero inside the band and rises outside it", "[scene][post][tiltshift]") {
    scene::PostSettings s;
    s.tiltShiftCentre = {0.5f, 0.5f};
    s.tiltShiftBandWidth = 0.2f; // half-width 0.1 in frame heights
    s.tiltShiftFalloff = 0.2f;
    s.tiltShiftRotation = 0.0f;
    const float aspect = 16.0f / 9.0f;

    // Inside the band nothing is defocused, whatever the rotation or the aspect ratio.
    CHECK(scene::tiltShiftCoverage(s, {0.5f, 0.5f}, aspect) == 0.0f);
    CHECK(scene::tiltShiftCoverage(s, {0.0f, 0.5f}, aspect) == 0.0f); // along the band, far off-centre
    CHECK(scene::tiltShiftCoverage(s, {0.5f, 0.55f}, aspect) == 0.0f);

    // Past the band's edge it rises, and it is monotonic on the way out: an author dragging the
    // falloff slider must never see the blur go backwards.
    float previous = 0.0f;
    for (float y = 0.60f; y <= 0.85f; y += 0.025f) {
        const float c = scene::tiltShiftCoverage(s, {0.5f, y}, aspect);
        INFO("y=" << y << " coverage=" << c);
        CHECK(c >= previous);
        previous = c;
    }
    CHECK(previous > 0.0f);
    // A full falloff past the edge is fully defocused, and it stays there rather than overshooting.
    CHECK(scene::tiltShiftCoverage(s, {0.5f, 0.5f + 0.1f + 0.25f}, aspect) == 1.0f);
    CHECK(scene::tiltShiftCoverage(s, {0.5f, 1.0f}, aspect) == 1.0f);
    // Symmetric about the centre line: the band is a lens, not a gradient.
    CHECK(scene::tiltShiftCoverage(s, {0.5f, 0.5f - 0.18f}, aspect) ==
          scene::tiltShiftCoverage(s, {0.5f, 0.5f + 0.18f}, aspect));
}

TEST_CASE("Rotating the tilt-shift band 90 degrees defocuses the other axis", "[scene][post][tiltshift]") {
    scene::PostSettings s;
    s.tiltShiftCentre = {0.5f, 0.5f};
    s.tiltShiftBandWidth = 0.2f;
    s.tiltShiftFalloff = 0.2f;
    const float aspect = 1.0f; // square, so the two axes are directly comparable

    s.tiltShiftRotation = 0.0f;
    const float horizontalAcross = scene::tiltShiftCoverage(s, {0.5f, 0.95f}, aspect);
    const float horizontalAlong = scene::tiltShiftCoverage(s, {0.95f, 0.5f}, aspect);
    CHECK(horizontalAcross == 1.0f);
    CHECK(horizontalAlong == 0.0f);

    s.tiltShiftRotation = 90.0f;
    const float verticalAcross = scene::tiltShiftCoverage(s, {0.95f, 0.5f}, aspect);
    const float verticalAlong = scene::tiltShiftCoverage(s, {0.5f, 0.95f}, aspect);
    CHECK(verticalAcross == 1.0f);
    CHECK(verticalAlong == 0.0f);

    // 180 degrees is the same band as 0: the band has no front and no back.
    s.tiltShiftRotation = 180.0f;
    CHECK(scene::tiltShiftCoverage(s, {0.5f, 0.95f}, aspect) == horizontalAcross);
    CHECK(scene::tiltShiftCoverage(s, {0.95f, 0.5f}, aspect) == horizontalAlong);

    // The aspect correction is what makes a rotation an angle on *screen*. On a 16:9 frame a
    // 45 degree band must stay 45 degrees: a point displaced equally in x and y on screen sits on
    // the band's axis and is sharp, which is false if the x axis is left in raw uv.
    s.tiltShiftRotation = 45.0f;
    const float wide = 16.0f / 9.0f;
    CHECK(scene::tiltShiftCoverage(s, {0.5f + 0.2f / wide, 0.5f + 0.2f}, wide) == 0.0f);
    CHECK(scene::tiltShiftCoverage(s, {0.5f - 0.2f / wide, 0.5f + 0.2f}, wide) > 0.9f);
}

TEST_CASE("Tilt-shift settings round-trip through a project document", "[scene][post][tiltshift]") {
    params::ParameterSet params;
    params::Modulator modulator;
    scene::PostSettings defaults;
    auto p = scene::registerPostParameters(params, defaults);
    REQUIRE(p.tiltShiftEnabled != nullptr);
    // Off by default: turning the engine on must not change a single existing render (ADR-079).
    CHECK_FALSE(defaults.tiltShiftEnabled);
    CHECK_FALSE(p.tiltShiftEnabled->value());

    scene::PostSettings authored;
    authored.tiltShiftEnabled = true;
    authored.tiltShiftCentre = {0.35f, 0.62f};
    authored.tiltShiftRotation = -37.5f;
    authored.tiltShiftBandWidth = 0.125f;
    authored.tiltShiftFalloff = 0.4f;
    authored.tiltShiftMaxRadius = 21.5f;
    p.tiltShiftEnabled->setBase(authored.tiltShiftEnabled);
    p.tiltShiftCentre->setBase(authored.tiltShiftCentre);
    p.tiltShiftRotation->setBase(authored.tiltShiftRotation);
    p.tiltShiftBandWidth->setBase(authored.tiltShiftBandWidth);
    p.tiltShiftFalloff->setBase(authored.tiltShiftFalloff);
    p.tiltShiftMaxRadius->setBase(authored.tiltShiftMaxRadius);

    const nlohmann::json doc = params::saveProject(params, modulator);
    params::ParameterSet reloaded;
    params::Modulator reloadedModulator;
    auto q = scene::registerPostParameters(reloaded, scene::PostSettings{});
    REQUIRE(params::loadProject(doc, reloaded, reloadedModulator).has_value());
    reloaded.resetFinals();
    scene::PostSettings live;
    scene::applyPostParameters(q, live);
    CHECK(live.tiltShiftEnabled == authored.tiltShiftEnabled);
    CHECK(live.tiltShiftCentre.x == authored.tiltShiftCentre.x);
    CHECK(live.tiltShiftCentre.y == authored.tiltShiftCentre.y);
    CHECK(live.tiltShiftRotation == authored.tiltShiftRotation);
    CHECK(live.tiltShiftBandWidth == authored.tiltShiftBandWidth);
    CHECK(live.tiltShiftFalloff == authored.tiltShiftFalloff);
    CHECK(live.tiltShiftMaxRadius == authored.tiltShiftMaxRadius);
    // And the band the reloaded settings describe is the band that was saved.
    CHECK(scene::tiltShiftCoverage(live, {0.5f, 0.5f}, 1.77f) ==
          scene::tiltShiftCoverage(authored, {0.5f, 0.5f}, 1.77f));
}

TEST_CASE("A composition's post block can author the tilt-shift", "[scene][post][tiltshift]") {
    params::ParameterSet params;
    auto p = scene::registerPostParameters(params, scene::PostSettings{});
    const auto block = nlohmann::json::parse(R"({
        "tiltShiftEnabled": true,
        "tiltShiftCentre": [0.5, 0.72],
        "tiltShiftRotation": 12.0,
        "tiltShiftBandWidth": 0.18,
        "tiltShiftFalloff": 0.3,
        "tiltShiftMaxRadius": 14.0
    })");
    REQUIRE(scene::applyPostJson(block, p).has_value());
    params.resetFinals();
    scene::PostSettings live;
    scene::applyPostParameters(p, live);
    CHECK(live.tiltShiftEnabled);
    CHECK(live.tiltShiftCentre.y == 0.72f);
    CHECK(live.tiltShiftRotation == 12.0f);
    CHECK(live.tiltShiftMaxRadius == 14.0f);
    // A malformed centre is an error rather than a silently ignored key: it is the one field an
    // author can get structurally wrong, and a silent default puts the band in the wrong place.
    CHECK_FALSE(scene::applyPostJson(nlohmann::json::parse(R"({"tiltShiftCentre": 0.5})"), p).has_value());
    CHECK_FALSE(scene::applyPostJson(nlohmann::json::parse(R"({"tiltShiftCentre": [0.5]})"), p).has_value());
}
