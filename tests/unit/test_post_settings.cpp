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

// ================================================================================================
// Cinematic integration (Image/Look §52.1, §68) -- the ADR-350 round trip and the §60 default.
// ================================================================================================

TEST_CASE("Cinematic integration defaults to doing nothing", "[scene][post][look]") {
    // §60/§87 at the level of the state object: this is the precondition for the byte-identity
    // proof in tests/rendering/test_image_look_gpu.cpp, and it is asserted separately because a
    // defaults regression here would make that GPU test pass for the wrong reason -- it would be
    // comparing two arms that are both "off".
    const scene::PostSettings defaults;
    CHECK(defaults.look.atmospheric == 0.0f);
    CHECK(defaults.look.colour == 0.0f);
    CHECK(defaults.look.localContrast == 0.0f);
    CHECK(defaults.look.lightWrap == 0.0f);
    CHECK_FALSE(defaults.look.active());
    CHECK_FALSE(defaults.look.atmosphericActive());
    CHECK_FALSE(defaults.look.lookActive());

    // `active()` is the single place the chain decides whether to encode anything, so each of the
    // four amounts has to be able to trip it on its own. A control that cannot turn the system on
    // by itself is a control that appears to do nothing (ADR-350's wind writer, one layer up).
    for (int which = 0; which < 4; ++which) {
        scene::PostSettings s;
        switch (which) {
        case 0: s.look.atmospheric = 0.5f; break;
        case 1: s.look.colour = 0.5f; break;
        case 2: s.look.localContrast = 0.5f; break;
        default: s.look.lightWrap = 0.5f; break;
        }
        INFO("amount index " << which);
        CHECK(s.look.active());
        // ... and it has to trip the *right* pass, or the pass that carries it is never encoded.
        CHECK(s.look.atmosphericActive() == (which == 0));
        CHECK(s.look.lookActive() == (which != 0));
    }

    // The two shape parameters have non-zero defaults because a shape has no meaningful zero. They
    // must not be able to switch the system on by themselves.
    scene::PostSettings shape;
    shape.look.atmosphericDistance = 42.0f;
    shape.look.localContrastRadius = 99.0f;
    shape.look.atmosphericTint = glm::vec3(1.0f, 0.0f, 0.0f);
    CHECK_FALSE(shape.look.active());
}

TEST_CASE("Cinematic integration survives save, load, save (ADR-350)", "[scene][post][look]") {
    // ADR-350's prescribed round trip: set a non-default value, save, load, save, assert it
    // survived -- and check *named keys*, not file size. The camera-bake loss shrank a file by
    // 10,000 lines while its parameter count went up, so every cheap check said healthy.
    params::ParameterSet params;
    params::Modulator modulator;
    auto p = scene::registerPostParameters(params, scene::PostSettings{});
    REQUIRE(p.lookAtmospheric != nullptr);
    REQUIRE(p.lookAtmosphericTint != nullptr);

    scene::PostSettings authored;
    authored.look.atmospheric = 0.42f;
    authored.look.atmosphericDistance = 137.5f;
    authored.look.atmosphericTint = {0.21f, 0.34f, 0.55f};
    authored.look.colour = 0.27f;
    authored.look.localContrast = 0.63f;
    authored.look.localContrastRadius = 37.0f;
    authored.look.lightWrap = 0.18f;
    p.lookAtmospheric->setBase(authored.look.atmospheric);
    p.lookAtmosphericDistance->setBase(authored.look.atmosphericDistance);
    p.lookAtmosphericTint->setBase(authored.look.atmosphericTint);
    p.lookColour->setBase(authored.look.colour);
    p.lookLocalContrast->setBase(authored.look.localContrast);
    p.lookLocalContrastRadius->setBase(authored.look.localContrastRadius);
    p.lookLightWrap->setBase(authored.look.lightWrap);

    // ---- save 1 -------------------------------------------------------------------------------
    const nlohmann::json first = params::saveProject(params, modulator);
    REQUIRE(first.contains("parameters"));
    const char* kPaths[] = {
        "post/look/atmospheric",  "post/look/atmosphericDistance", "post/look/atmosphericTint",
        "post/look/colour",       "post/look/localContrast",       "post/look/localContrastRadius",
        "post/look/lightWrap",
    };
    for (const char* path : kPaths) {
        INFO("named key " << path);
        CHECK(first["parameters"].contains(path));
    }

    // ---- load ----------------------------------------------------------------------------------
    params::ParameterSet reloaded;
    params::Modulator reloadedModulator;
    auto q = scene::registerPostParameters(reloaded, scene::PostSettings{});
    REQUIRE(params::loadProject(first, reloaded, reloadedModulator).has_value());
    reloaded.resetFinals();
    scene::PostSettings live;
    scene::applyPostParameters(q, live);
    CHECK(live.look.atmospheric == authored.look.atmospheric);
    CHECK(live.look.atmosphericDistance == authored.look.atmosphericDistance);
    CHECK(live.look.atmosphericTint.r == authored.look.atmosphericTint.r);
    CHECK(live.look.atmosphericTint.g == authored.look.atmosphericTint.g);
    CHECK(live.look.atmosphericTint.b == authored.look.atmosphericTint.b);
    CHECK(live.look.colour == authored.look.colour);
    CHECK(live.look.localContrast == authored.look.localContrast);
    CHECK(live.look.localContrastRadius == authored.look.localContrastRadius);
    CHECK(live.look.lightWrap == authored.look.lightWrap);

    // ---- save 2: the second save is the one ADR-350 exists for. A block with a reader and no
    // writer survives the first round trip in memory and is destroyed the moment the thing that
    // read it writes the file back.
    const nlohmann::json second = params::saveProject(reloaded, reloadedModulator);
    for (const char* path : kPaths) {
        INFO("named key after re-save " << path);
        REQUIRE(second["parameters"].contains(path));
        CHECK(second["parameters"][path] == first["parameters"][path]);
    }
}

TEST_CASE("A composition's post block can author the cinematic integration", "[scene][post][look]") {
    // The scene-file side of the same question. `applyPostJson` is the reader and
    // `Composition::toJson` echoes the block back, so a key this reader does not name is a key a
    // scene cannot carry -- which is why every one of them is exercised here rather than sampled.
    params::ParameterSet params;
    auto p = scene::registerPostParameters(params, scene::PostSettings{});
    const auto block = nlohmann::json::parse(R"({
        "lookAtmospheric": 0.33,
        "lookAtmosphericDistance": 450.0,
        "lookAtmosphericTint": [0.4, 0.5, 0.7],
        "lookColour": 0.25,
        "lookLocalContrast": 0.5,
        "lookLocalContrastRadius": 18.0,
        "lookLightWrap": 0.4
    })");
    REQUIRE(scene::applyPostJson(block, p).has_value());
    params.resetFinals();
    scene::PostSettings live;
    scene::applyPostParameters(p, live);
    CHECK(live.look.atmospheric == 0.33f);
    CHECK(live.look.atmosphericDistance == 450.0f);
    CHECK(live.look.atmosphericTint.b == 0.7f);
    CHECK(live.look.colour == 0.25f);
    CHECK(live.look.localContrast == 0.5f);
    CHECK(live.look.localContrastRadius == 18.0f);
    CHECK(live.look.lightWrap == 0.4f);
    CHECK(live.look.active());

    // A malformed tint is an error, not a silently ignored key.
    CHECK_FALSE(scene::applyPostJson(nlohmann::json::parse(R"({"lookAtmosphericTint": 0.5})"), p).has_value());
    CHECK_FALSE(scene::applyPostJson(nlohmann::json::parse(R"({"lookAtmosphericTint": [0.5, 0.5]})"), p).has_value());
}

TEST_CASE("A composition's post block can author the colour grade's vectors", "[scene][post]") {
    // Before the Image/Look work `applyPostJson` handled no vec3 at all, so `lift`, `gamma`, `gain`
    // and both tints were registered parameters a scene's own `post` block could not set -- it got
    // "unknown key, ignored". Adding `lookAtmosphericTint` needed the type, and the five that were
    // already stranded got it too. This is the test that stops them being stranded again.
    params::ParameterSet params;
    auto p = scene::registerPostParameters(params, scene::PostSettings{});
    const auto block = nlohmann::json::parse(R"({
        "lift": [0.01, 0.02, 0.03],
        "gamma": [1.1, 1.0, 0.9],
        "gain": [1.2, 1.0, 0.8],
        "halationTint": [1.0, 0.2, 0.1],
        "anamorphicTint": [0.2, 0.4, 1.0]
    })");
    REQUIRE(scene::applyPostJson(block, p).has_value());
    params.resetFinals();
    scene::PostSettings live;
    scene::applyPostParameters(p, live);
    CHECK(live.lift.r == 0.01f);
    CHECK(live.gamma.b == 0.9f);
    CHECK(live.gain.r == 1.2f);
    CHECK(live.halationTint.g == 0.2f);
    CHECK(live.anamorphicTint.b == 1.0f);
}

TEST_CASE("The motion-blur shape parameters survive save, load, save (ADR-350, ADR-372)",
          "[scene][post][motionblur]") {
    // Before ADR-372 these three were read by `fs_motion_blur` on every frame that blurs and
    // registered nowhere, so no scene and no project could set them: `post/motionBlur/amount` was
    // the only reachable motion-blur control. Built, and unreachable -- the same family as the
    // unwired identifier target, one layer along.
    params::ParameterSet params;
    params::Modulator modulator;
    auto p = scene::registerPostParameters(params, scene::PostSettings{});
    REQUIRE(p.motionBlurSamples != nullptr);
    REQUIRE(p.motionBlurMaxRadius != nullptr);
    REQUIRE(p.motionBlurTileSize != nullptr);

    // The defaults must still be the defaults, or registering them changes every existing render.
    scene::PostSettings defaults;
    CHECK(p.motionBlurSamples->value() == static_cast<int>(defaults.motionBlurSamples));
    CHECK(p.motionBlurMaxRadius->value() == defaults.motionBlurMaxRadius);
    CHECK(p.motionBlurTileSize->value() == static_cast<int>(defaults.motionBlurTileSize));

    p.motionBlurSamples->setBase(24);
    p.motionBlurMaxRadius->setBase(55.5f);
    p.motionBlurTileSize->setBase(12);

    const nlohmann::json first = params::saveProject(params, modulator);
    const char* kPaths[] = {"post/motionBlur/samples", "post/motionBlur/maxRadius",
                            "post/motionBlur/tileSize"};
    for (const char* path : kPaths) {
        INFO("named key " << path);
        REQUIRE(first["parameters"].contains(path));
    }

    params::ParameterSet reloaded;
    params::Modulator reloadedModulator;
    auto q = scene::registerPostParameters(reloaded, scene::PostSettings{});
    REQUIRE(params::loadProject(first, reloaded, reloadedModulator).has_value());
    reloaded.resetFinals();
    scene::PostSettings live;
    scene::applyPostParameters(q, live);
    CHECK(live.motionBlurSamples == 24u);
    CHECK(live.motionBlurMaxRadius == 55.5f);
    CHECK(live.motionBlurTileSize == 12u);

    // The second save is the one ADR-350 exists for.
    const nlohmann::json second = params::saveProject(reloaded, reloadedModulator);
    for (const char* path : kPaths) {
        INFO("named key after re-save " << path);
        REQUIRE(second["parameters"].contains(path));
        CHECK(second["parameters"][path] == first["parameters"][path]);
    }

    // The declared ranges are the clamps `PostProcessor::run` already applies -- samples to [2,32]
    // and tileSize to [4,40]. A parameter that can express a value the chain silently alters is a
    // slider whose top half does nothing, which is the defect this fix exists to remove wearing a
    // different hat. Asserted here so the two cannot drift apart.
    // `value()` reads the *final* value, which `setBase` does not touch until the set is resolved.
    // Reading the base directly is what asks the question being asked here -- did the hard range
    // clamp the authored value -- without depending on resolution order.
    p.motionBlurSamples->setBase(1000);
    CHECK(p.motionBlurSamples->base() == 32);
    p.motionBlurSamples->setBase(-5);
    CHECK(p.motionBlurSamples->base() == 2);
    p.motionBlurTileSize->setBase(1000);
    CHECK(p.motionBlurTileSize->base() == 40);
    p.motionBlurTileSize->setBase(0);
    CHECK(p.motionBlurTileSize->base() == 4);
}

TEST_CASE("bloomLevels is a live control and now round-trips (ADR-385)", "[scene][post][bloom]") {
    // It was accepted by `applyPostJson` and dropped, on the stated grounds that the pyramid is
    // "fixed when the bloom pyramid is created, so there is no parameter to move". That was false:
    // `PostProcessor::run` clamps and reads it every frame for both the bloom and halation
    // pyramids. So it was a working control that no scene and no project could reach -- the sixth
    // instance of that family in this project, and the one that hid behind a plausible reason.
    params::ParameterSet params;
    params::Modulator modulator;
    auto p = scene::registerPostParameters(params, scene::PostSettings{});
    REQUIRE(p.bloomLevels != nullptr);
    CHECK(p.bloomLevels->value() == static_cast<int>(scene::PostSettings{}.bloomLevels));

    // The scene block: the key a dozen committed scenes already carry now does what they meant.
    REQUIRE(scene::applyPostJson(nlohmann::json::parse(R"({"bloomLevels": 3})"), p).has_value());
    params.resetFinals();
    scene::PostSettings live;
    scene::applyPostParameters(p, live);
    CHECK(live.bloomLevels == 3u);
    // ... and a malformed one is an error rather than a silent default.
    CHECK_FALSE(scene::applyPostJson(nlohmann::json::parse(R"({"bloomLevels": "six"})"), p).has_value());

    // ADR-350 round trip: set, save, load, save, assert by named key.
    p.bloomLevels->setBase(4);
    const nlohmann::json first = params::saveProject(params, modulator);
    REQUIRE(first["parameters"].contains("post/bloom/levels"));
    params::ParameterSet reloaded;
    params::Modulator reloadedModulator;
    auto q = scene::registerPostParameters(reloaded, scene::PostSettings{});
    REQUIRE(params::loadProject(first, reloaded, reloadedModulator).has_value());
    reloaded.resetFinals();
    scene::PostSettings back;
    scene::applyPostParameters(q, back);
    CHECK(back.bloomLevels == 4u);
    const nlohmann::json second = params::saveProject(reloaded, reloadedModulator);
    REQUIRE(second["parameters"].contains("post/bloom/levels"));
    CHECK(second["parameters"]["post/bloom/levels"] == first["parameters"]["post/bloom/levels"]);

    // The declared range is the clamp `run()` already applies, so the parameter cannot express a
    // value the chain would silently alter.
    p.bloomLevels->setBase(99);
    CHECK(p.bloomLevels->base() == 8);
    p.bloomLevels->setBase(0);
    CHECK(p.bloomLevels->base() == 1);
}
