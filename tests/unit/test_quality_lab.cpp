// The Quality Lab beyond the §34 spatial ladder: the shared ladder as a library, the temporal
// detectors and their control arms, the AOV-gated masks, the report's refusals, and the degradation
// contract for the optional external tools.
//
// `test_quality_metrics.cpp` holds the hand-written spatial ladder, which is the human-readable
// gate. This file asserts that `avgen_quality validate --ladder` runs the SAME arms and that they
// pass -- so the tool anyone can run and the suite CI runs cannot drift apart -- and then covers
// everything the spatial ladder does not reach.
//
// ADR-182 governs the whole file: a probe must be shown capable of failing. Every arm here has a
// control where the number must NOT move, and the ladder itself has one -- `runSpatialLadder` is run
// against a deliberately broken metric and required to catch it.

#include "artifacts/masks.hpp"
#include "capture/sequence.hpp"

#include "scene/scene_types.hpp"
#include "external/vmaf.hpp"
#include "metrics/temporal.hpp"
#include "report/diagnostics.hpp"
#include "report/vector.hpp"
#include "validation/ladder.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

using namespace avgen;
using Catch::Approx;

namespace {

quality::MotionResidual residualBetween(const quality::SyntheticSequence& sequence,
                                        std::size_t index,
                                        const quality::DisocclusionPolicy& policy = {}) {
    quality::MotionInputs inputs;
    inputs.previous = &sequence.frames[index - 1];
    inputs.current = &sequence.frames[index];
    inputs.velocity = &sequence.velocity[index];
    if (!sequence.depth.empty()) {
        inputs.depthPrevious = &sequence.depth[index - 1];
        inputs.depthCurrent = &sequence.depth[index];
    }
    if (!sequence.identifier.empty()) {
        inputs.idPrevious = &sequence.identifier[index - 1];
        inputs.idCurrent = &sequence.identifier[index];
    }
    return quality::motionCompensatedResidual(inputs, policy);
}

std::filesystem::path scratch(const char* name) {
    const auto dir = std::filesystem::temp_directory_path() / "avgen-quality-tests" / name;
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    return dir;
}

} // namespace

// ---- the ladder as a library -------------------------------------------------------------------

TEST_CASE("the shared distortion ladder passes every arm and every control", "[quality][ladder]") {
    const quality::LadderResult result = quality::runSpatialLadder();
    REQUIRE_FALSE(result.arms.empty());
    for (const quality::Arm& arm : result.arms) {
        for (const quality::Check& check : arm.checks) {
            INFO(arm.name << " / " << check.description << " :: " << check.expectation
                          << " observed " << check.observed << " vs " << check.reference);
            CHECK(check.passed);
        }
    }
    // The controls are not optional garnish: without them an arm that rises for every distortion
    // looks like a detector. Assert they are actually present.
    std::size_t controls = 0;
    for (const quality::Arm& arm : result.arms) {
        controls += static_cast<std::size_t>(
            std::count_if(arm.checks.begin(), arm.checks.end(),
                          [](const quality::Check& c) { return !c.mustMove; }));
    }
    INFO("controls " << controls << " of " << result.totalChecks());
    CHECK(controls >= 6);
}

TEST_CASE("the ladder catches a metric that has been broken", "[quality][ladder][adr182]") {
    // **The meta-control.** A validator that passes whatever it is given proves nothing about the
    // metrics it runs. Replace the spatial Laplacian with a constant -- the shape of a metric that
    // is reading the wrong buffer, or a cached value, or a stub someone forgot -- and the ladder
    // must notice.
    quality::MetricBindings broken = quality::realMetrics();
    broken.spatialLaplacian = [](const quality::Frame&) { return 1.0; };
    const quality::LadderResult sabotaged = quality::runSpatialLadder(broken);
    INFO("failed " << sabotaged.failedChecks() << " of " << sabotaged.totalChecks());
    CHECK_FALSE(sabotaged.passed());
    // And specifically the arms that depend on it, not merely "something failed".
    const auto armNamed = [&](std::string_view name) {
        return std::find_if(sabotaged.arms.begin(), sabotaged.arms.end(),
                            [&](const quality::Arm& a) { return a.name.starts_with(name); });
    };
    REQUIRE(armNamed("blur") != sabotaged.arms.end());
    CHECK_FALSE(armNamed("blur")->passed());
    REQUIRE(armNamed("aliasing") != sabotaged.arms.end());
    CHECK_FALSE(armNamed("aliasing")->passed());
    // ...while an arm that does not use it still passes, which is what makes this a targeted
    // sabotage rather than a global break.
    REQUIRE(armNamed("control: identity") != sabotaged.arms.end());
    CHECK(armNamed("control: identity")->passed());
}

TEST_CASE("the temporal control arms pass", "[quality][ladder][temporal]") {
    const quality::LadderResult result = quality::runTemporalLadder();
    REQUIRE_FALSE(result.arms.empty());
    for (const quality::Arm& arm : result.arms) {
        for (const quality::Check& check : arm.checks) {
            INFO(arm.name << " / " << check.description << " :: " << check.expectation
                          << " observed " << check.observed << " vs " << check.reference);
            CHECK(check.passed);
        }
    }
}

// ---- the motion-compensated residual -----------------------------------------------------------

TEST_CASE("the residual is zero on a static scene and stays zero under smooth motion",
          "[quality][temporal]") {
    const quality::SyntheticSequence still = quality::staticScene(4);
    const quality::SyntheticSequence smooth = quality::smoothMotion(4, 1.0);

    const quality::MotionResidual stillResidual = residualBetween(still, 1);
    REQUIRE(stillResidual.width > 0);
    CHECK(stillResidual.residual == Approx(0.0).margin(1e-6));
    CHECK(stillResidual.disocclusionFraction == Approx(0.0).margin(1e-6));
    CHECK(stillResidual.validFraction == Approx(1.0).margin(1e-6));

    const quality::MotionResidual smoothResidual = residualBetween(smooth, 1);
    // 1e-4 and not 1e-9, and the reason is a property of the instrument rather than slack: the
    // velocity is a float32 UV, so a one-pixel motion on a 96-pixel frame is 1/96 rounded to 24
    // bits and the warp lands about a millionth of a pixel off. The engine's target is RG16Float,
    // which is coarser still. A residual is never compared against zero; it is compared against its
    // control, and this is why.
    CHECK(smoothResidual.residual == Approx(0.0).margin(1e-4));

    // **The arm that is ADR-243 in miniature.** The same footage, measured by the detector that was
    // in use when a correct measurement ranked two remedies backwards: authored translation reads as
    // large temporal instability, and the motion-compensated residual reads zero. That disagreement
    // is the whole reason this detector was built, so assert it rather than describe it.
    const double alternation =
        quality::temporalAlternation(smooth.frames[0], smooth.frames[1], smooth.frames[2]).mean;
    INFO("alternation on smooth motion " << alternation << ", residual " << smoothResidual.residual);
    CHECK(alternation > 20.0);
    CHECK(smoothResidual.residual < 0.01);
}

TEST_CASE("the residual rises on shimmer the velocity buffer does not explain",
          "[quality][temporal]") {
    const quality::SyntheticSequence shimmering = quality::shimmer(4);
    const quality::MotionResidual residual = residualBetween(shimmering, 1);
    REQUIRE(residual.width > 0);
    CHECK(residual.residual > 5.0);
    // The control that stops this being a blur arm: the per-frame spatial energy is unchanged, so
    // what moved is temporal and not spatial.
    const double a = quality::spatialLaplacian(shimmering.frames[0]);
    const double b = quality::spatialLaplacian(shimmering.frames[1]);
    INFO("laplacian " << a << " vs " << b);
    CHECK(b / a == Approx(1.0).margin(0.05));
}

TEST_CASE("the disocclusion mask is what makes the residual mean anything", "[quality][temporal]") {
    const quality::SyntheticSequence occluded = quality::disocclusion(4);
    quality::DisocclusionPolicy none;
    none.useIdentifier = false;
    none.useDepth = false;

    const quality::MotionResidual masked = residualBetween(occluded, 1);
    const quality::MotionResidual unmasked = residualBetween(occluded, 1, none);
    REQUIRE(masked.width > 0);

    INFO("masked " << masked.residual << " over " << masked.validFraction << "; unmasked "
                   << unmasked.residual);
    CHECK(masked.disocclusionFraction > 0.01);
    CHECK(unmasked.residual > 2.0 * masked.residual);
    // Both tests fire, and each on its own would have caught it: the identifier test sees a
    // different object, the depth test sees a nearer surface. A mask with only one of them is one
    // silhouette away from being wrong.
    CHECK(masked.identifierFraction + masked.depthFraction > 0.0);

    // And the negative control: turn the occluder's *identifier* off and the identifier test must
    // stop firing while the depth test still does. A mask whose two halves cannot be separated is a
    // mask whose halves cannot be trusted.
    quality::DisocclusionPolicy depthOnly;
    depthOnly.useIdentifier = false;
    const quality::MotionResidual depthMasked = residualBetween(occluded, 1, depthOnly);
    CHECK(depthMasked.identifierFraction == Approx(0.0).margin(1e-9));
    CHECK(depthMasked.depthFraction > 0.0);
}

TEST_CASE("an empty or mismatched input produces an empty result, not a zero",
          "[quality][temporal]") {
    // A zero residual and an unanswerable question look identical in a report. The caller checks
    // `width`, so make sure there is something to check.
    quality::MotionInputs inputs;
    CHECK(quality::motionCompensatedResidual(inputs).width == 0);

    const quality::SyntheticSequence smooth = quality::smoothMotion(2, 1.0);
    const quality::SyntheticSequence still = quality::staticScene(2);
    quality::Plane wrongSize = still.velocity[0];
    wrongSize.width += 1;
    inputs.previous = &smooth.frames[0];
    inputs.current = &smooth.frames[1];
    inputs.velocity = &wrongSize;
    CHECK(quality::motionCompensatedResidual(inputs).width == 0);
}

TEST_CASE("the warp floor is measured rather than assumed", "[quality][temporal]") {
    const quality::SyntheticSequence integer = quality::smoothMotion(2, 1.0);
    const quality::SyntheticSequence subPixel = quality::smoothMotion(2, 0.5);
    const double integerFloor = quality::warpFloor(integer.frames[0], integer.velocity[0]);
    const double subPixelFloor = quality::warpFloor(subPixel.frames[0], subPixel.velocity[0]);
    INFO("integer floor " << integerFloor << ", sub-pixel floor " << subPixelFloor);
    // A whole-pixel warp is a copy and costs nothing; a half-pixel warp is a resample and costs
    // something. That difference is the floor, and it is why a residual is compared against its
    // control rather than against zero.
    CHECK(integerFloor == Approx(0.0).margin(1e-4)); // float32 UV; see the smooth-motion arm
    CHECK(subPixelFloor > 1.0);
}

// ---- the AOV-gated masks -----------------------------------------------------------------------

TEST_CASE("an identifier plane is surveyed before anything is gated on it", "[quality][aov]") {
    // ADR-242's lesson: without asserting that `id` has more than one distinct value, every
    // mask-based number is vacuous -- a constant plane produces a mask of everything or a mask of
    // nothing, and both look like a working detector.
    const quality::SyntheticSequence occluded = quality::disocclusion(2);
    const quality::IdentifierSurvey survey = quality::surveyIdentifiers(occluded.identifier[0]);
    CHECK(survey.distinctValues == 2);
    CHECK(survey.usable());
    CHECK_FALSE(survey.exceedsExactFloatRange);

    const quality::SyntheticSequence still = quality::staticScene(2);
    const quality::IdentifierSurvey flat = quality::surveyIdentifiers(still.identifier[0]);
    CHECK(flat.distinctValues == 1);
    CHECK_FALSE(flat.usable());
}

TEST_CASE("the identifier's float representation is checked, not assumed", "[quality][aov]") {
    // The id target is R32Uint widened to float on the way into the EXR. A float32 is exact to 2^24,
    // and the identifier packs a material id into the high 16 bits of a 32-bit word -- so a large
    // enough material id is not exactly representable and an equality test on it is approximate.
    quality::Plane plane;
    plane.width = 2;
    plane.height = 1;
    plane.rgba.assign(8, 0.0f);
    plane.rgba[0] = 1.0f;
    plane.rgba[4] = 20000000.0f; // above 2^24
    const quality::IdentifierSurvey survey = quality::surveyIdentifiers(plane);
    CHECK(survey.exceedsExactFloatRange);
}

TEST_CASE("the specular mask selects smooth and emissive pixels and nothing else",
          "[quality][aov]") {
    const quality::SyntheticSequence flicker = quality::shadingFlicker(2);
    REQUIRE_FALSE(flicker.normal.empty());
    const quality::Mask mask = quality::specularMask(&flicker.emission[0], &flicker.normal[0]);
    REQUIRE_FALSE(mask.empty());
    const double share = quality::coverage(mask);
    INFO("specular coverage " << share);
    // The flickering strip is 30 of 96 columns.
    CHECK(share == Approx(30.0 / 96.0).margin(0.02));

    // The control: with neither plane, the mask is EMPTY rather than everything. A mask that
    // defaults to the whole frame turns "specular instability" into "instability", silently.
    CHECK(quality::specularMask(nullptr, nullptr).empty());

    // And the gated residual over that mask finds the flicker while the ungated one dilutes it.
    const quality::MotionResidual residual = residualBetween(flicker, 1);
    REQUIRE(residual.width > 0);
    const auto gated = residual.over(mask);
    INFO("gated " << gated.residual << " over " << gated.coverage << "; ungated "
                  << residual.residual);
    CHECK(gated.residual > residual.residual);
    CHECK(gated.coverage == Approx(share).margin(0.02));

    // The control for the gating machinery itself: a residual gated over every pixel must equal the
    // ungated residual, or `over()` is doing something other than gating.
    const auto all = residual.over(quality::everything(residual.width, residual.height));
    CHECK(all.residual == Approx(residual.residual).epsilon(1e-9));
}

TEST_CASE("the normal-unchanged mask separates shading from geometry", "[quality][aov]") {
    const quality::SyntheticSequence flicker = quality::shadingFlicker(2);
    const quality::Mask unchanged = quality::normalUnchangedMask(flicker.normal[0], flicker.normal[1]);
    REQUIRE_FALSE(unchanged.empty());
    // Nothing moved, so every surfaced pixel's normal is unchanged.
    CHECK(quality::coverage(unchanged) == Approx(1.0).margin(1e-6));

    // The control: rotate every normal and the mask must empty. Without this the mask could be
    // returning "all ones" for a reason that has nothing to do with normals.
    quality::Plane rotated = flicker.normal[1];
    for (std::size_t i = 0; i < rotated.rgba.size(); i += 4) {
        rotated.rgba[i] = 1.0f;
        rotated.rgba[i + 1] = 0.0f;
        rotated.rgba[i + 2] = 0.0f;
    }
    CHECK(quality::coverage(quality::normalUnchangedMask(flicker.normal[0], rotated)) ==
          Approx(0.0).margin(1e-6));

    // And the sky control: a pixel with no surface has no normal, so it is in neither mask.
    quality::Plane sky = flicker.normal[0];
    std::fill(sky.rgba.begin(), sky.rgba.end(), 0.0f);
    CHECK(quality::coverage(quality::normalUnchangedMask(sky, sky)) == Approx(0.0).margin(1e-6));
}

TEST_CASE("identifier churn is found where nothing moved, and not where something did",
          "[quality][aov]") {
    const quality::SyntheticSequence pop = quality::identifierPop(3);
    const quality::Mask churn =
        quality::identifierChurnMask(pop.identifier[0], pop.identifier[1], pop.velocity[1],
                                     &pop.depth[0], &pop.depth[1]);
    REQUIRE_FALSE(churn.empty());
    INFO("churn coverage " << quality::coverage(churn));
    CHECK(quality::coverage(churn) == Approx(12.0 / 96.0).margin(0.02));

    // **The control that makes it a pop detector rather than an id-difference detector.** The same
    // identifier change, with a velocity that says the surface moved, must NOT be reported: that is
    // ordinary occlusion, and a detector that cannot tell them apart is reporting camera motion as
    // a rendering defect -- which is exactly ADR-243's mistake.
    quality::Plane moving = pop.velocity[1];
    for (std::size_t i = 0; i < moving.rgba.size(); i += 4) {
        moving.rgba[i] = 4.0f / 96.0f;
    }
    const quality::Mask moved = quality::identifierChurnMask(
        pop.identifier[0], pop.identifier[1], moving, &pop.depth[0], &pop.depth[1]);
    CHECK(quality::coverage(moved) == Approx(0.0).margin(1e-6));

    // And the same again for depth: a changed identifier at a changed depth is a new surface, not a
    // level swap.
    quality::Plane nearer = pop.depth[1];
    for (std::size_t i = 0; i < nearer.rgba.size(); i += 4) {
        nearer.rgba[i] = 1.0f;
    }
    const quality::Mask different = quality::identifierChurnMask(
        pop.identifier[0], pop.identifier[1], pop.velocity[1], &pop.depth[0], &nearer);
    CHECK(quality::coverage(different) == Approx(0.0).margin(1e-6));
}

TEST_CASE("a material-class mask selects by the identifier's material half", "[quality][aov]") {
    // The mechanism for `vegetationResidual`. The MAPPING from material id to class does not exist
    // and the report says so; this is the machinery that mapping would drive, tested so it is not
    // dead code waiting to be wrong on the day somebody writes the mapping down.
    quality::Plane id;
    id.width = 4;
    id.height = 1;
    id.rgba.assign(16, 0.0f);
    const auto pack = [](std::uint32_t material, std::uint32_t object) {
        return static_cast<float>((material << 16) | object);
    };
    id.rgba[0] = pack(3, 11);
    id.rgba[4] = pack(3, 12);
    id.rgba[8] = pack(9, 11);
    id.rgba[12] = pack(0, 0);
    CHECK(quality::materialIdOf(id.rgba[0]) == 3);
    CHECK(quality::objectIdOf(id.rgba[0]) == 11);
    CHECK(quality::coverage(quality::materialClassMask(id, {3})) == Approx(0.5));
    CHECK(quality::coverage(quality::materialClassMask(id, {9})) == Approx(0.25));
    // The control: an empty class list selects nothing, not everything.
    CHECK(quality::materialClassMask(id, {}).empty());
}


TEST_CASE("an object splits into an interior that cannot alias and a silhouette that can",
          "[quality][aov][adr257]") {
    // ADR-257. `examples/quality/aliasing*.scene.json` called its large smooth orb a control -- "it
    // cannot alias, so a metric that moves on this scene's arms must not be moving here" -- and a
    // reviewer handed it as one correctly reported that it differed. A sphere's INTERIOR shading is
    // unaffected by anti-aliasing; its SILHOUETTE is a curved edge like any other. This is the
    // machinery that separates the two claims, and the arms below are what stop it being believed
    // when it is empty.
    const std::uint32_t width = 32;
    const std::uint32_t height = 32;
    const auto pack = [](std::uint32_t material, std::uint32_t object) {
        return static_cast<float>((material << 16) | object);
    };
    quality::Plane id;
    id.width = width;
    id.height = height;
    id.rgba.assign(static_cast<std::size_t>(width) * height * 4, 0.0f);
    // A 12x12 square of object 7 in the middle of a frame of object 9. A square rather than a disc
    // so the geometry of the split is arithmetic and not an approximation.
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const bool inside = x >= 10 && x < 22 && y >= 10 && y < 22;
            id.rgba[(static_cast<std::size_t>(y) * width + x) * 4] = pack(1, inside ? 7 : 9);
        }
    }

    const quality::SilhouetteSplit split = quality::splitSilhouette(id, 7, 2);
    const double pixels = static_cast<double>(width) * height;
    CHECK(quality::coverage(split.object) == Approx(144.0 / pixels));
    // eroded by 2: 8x8. dilated by 2: 16x16. The band is what lies between them.
    CHECK(quality::coverage(split.interior) == Approx(64.0 / pixels));
    CHECK(quality::coverage(split.band) == Approx((256.0 - 64.0) / pixels));
    CHECK(quality::coverage(split.elsewhere) == Approx((pixels - 256.0) / pixels));
    // The masks partition the frame exactly: nothing counted twice, nothing dropped.
    bool partitions = true;
    for (std::size_t i = 0; i < split.object.size(); ++i) {
        partitions = partitions && (split.interior[i] + split.band[i] + split.elsewhere[i] == 1);
    }
    CHECK(partitions);
    // **The vacuity arms.** Neither region may be empty and neither may be the whole frame -- both
    // look like a working detector and neither is (ADR-242's identifier survey, same reason).
    CHECK(quality::coverage(split.interior) > 0.0);
    CHECK(quality::coverage(split.band) > 0.0);
    CHECK(quality::coverage(split.interior) < 1.0);
    CHECK(quality::coverage(split.band) < 1.0);
    // An object that is not in the frame produces nothing rather than everything.
    CHECK(quality::coverage(quality::splitSilhouette(id, 4242, 2).object) == Approx(0.0));
    // An object thinner than the erosion has NO interior, which is the whole reason a fence cannot
    // be measured this way and the orb can.
    CHECK(quality::coverage(quality::splitSilhouette(id, 7, 8).interior) == Approx(0.0));

    // Now the measurement. Two frames identical except for a one-pixel ring on the object's
    // boundary -- which is what an anti-aliasing filter does to a resolved shape.
    quality::Frame base;
    base.width = width;
    base.height = height;
    base.rgba.assign(static_cast<std::size_t>(width) * height * 4, 0);
    for (std::size_t i = 0; i < static_cast<std::size_t>(width) * height; ++i) {
        const bool inside = split.object[i] != 0;
        const std::uint8_t value = inside ? 200 : 40;
        base.rgba[i * 4 + 0] = value;
        base.rgba[i * 4 + 1] = value;
        base.rgba[i * 4 + 2] = value;
        base.rgba[i * 4 + 3] = 255;
    }
    quality::Frame filtered = base;
    const quality::Mask ring = quality::splitSilhouette(id, 7, 1).band;
    for (std::size_t i = 0; i < ring.size(); ++i) {
        if (ring[i] != 0) {
            filtered.rgba[i * 4 + 0] = 120;
            filtered.rgba[i * 4 + 1] = 120;
            filtered.rgba[i * 4 + 2] = 120;
        }
    }

    const quality::MaskedDifference interior =
        quality::maskedLumaDifference(filtered, base, split.interior);
    const quality::MaskedDifference band =
        quality::maskedLumaDifference(filtered, base, split.band);
    CHECK(interior.pixels == 64);
    CHECK(interior.mean == Approx(0.0));
    CHECK(interior.max == Approx(0.0));
    CHECK(band.pixels > 0);
    CHECK(band.mean > 1.0);
    CHECK(band.max > 50.0);

    // **The arm that makes the interior zero mean something** (ADR-182): the same interior mask over
    // a pair that differs everywhere must NOT report zero. Without it, "the interior did not change"
    // is indistinguishable from an instrument that cannot see the interior at all.
    quality::Frame shifted = base;
    for (std::size_t i = 0; i < shifted.rgba.size(); i += 4) {
        shifted.rgba[i + 0] = static_cast<std::uint8_t>(shifted.rgba[i + 0] / 2);
        shifted.rgba[i + 1] = static_cast<std::uint8_t>(shifted.rgba[i + 1] / 2);
        shifted.rgba[i + 2] = static_cast<std::uint8_t>(shifted.rgba[i + 2] / 2);
    }
    const quality::MaskedDifference moved =
        quality::maskedLumaDifference(shifted, base, split.interior);
    CHECK(moved.mean > 50.0);
    // And a mask with nothing in it reports no pixels rather than a mean of zero that reads as
    // "no difference".
    const quality::MaskedDifference empty = quality::maskedLumaDifference(
        filtered, base, quality::Mask(static_cast<std::size_t>(width) * height, 0));
    CHECK(empty.pixels == 0);
}

TEST_CASE("the shadow mask finds shadowed surfaces and never the sky", "[quality][aov][adr255]") {
    // ADR-255's AOV: rgb = the shadow-map visibility of directional lights 0/1/2, a = the view
    // depth it was computed at. The sky is the arm that matters here -- the pass writes UNSHADOWED
    // white there, so a threshold on visibility alone would be correct about the sky by accident
    // and wrong the day the pass writes anything else. The depth is what excludes it.
    quality::Plane shadow;
    shadow.width = 4;
    shadow.height = 1;
    shadow.rgba.assign(16, 0.0f);
    const auto set = [&](std::uint32_t x, float visibility, float depth) {
        shadow.rgba[x * 4 + 0] = visibility;
        shadow.rgba[x * 4 + 1] = visibility;
        shadow.rgba[x * 4 + 2] = visibility;
        shadow.rgba[x * 4 + 3] = depth;
    };
    set(0, 0.0f, 12.0f);  // fully shadowed surface
    set(1, 0.9f, 12.0f);  // lit surface
    set(2, 1.0f, 0.0f);   // sky: unshadowed, and no depth
    set(3, 0.2f, 0.0f);   // sky that somehow reads dark -- still not a surface
    CHECK(quality::coverage(quality::shadowedMask(shadow)) == Approx(0.25));
    // The threshold is a decision and the mask moves with it, which is what makes it a decision.
    CHECK(quality::coverage(quality::shadowedMask(shadow, 0.95)) == Approx(0.5));
    // The controls: at the extremes it is empty and it is every SURFACE -- never every pixel,
    // because the sky is not a surface however the threshold is set.
    CHECK(quality::coverage(quality::shadowedMask(shadow, 0.0)) == Approx(0.0));
    CHECK(quality::coverage(quality::shadowedMask(shadow, 2.0)) == Approx(0.5));
}

TEST_CASE("a class mask keys on the identifier half that is actually unique",
          "[quality][aov][adr256]") {
    // ADR-256 assumed the identifier's high 16 bits were a material index. They are not: every
    // renderer that writes this target puts the object's own ordinal within its pick space plus
    // one there. So an entity and a procedural that share NOTHING share that number, and the mask
    // that keys on it selects both.
    quality::Plane id;
    id.width = 4;
    id.height = 1;
    id.rgba.assign(16, 0.0f);
    const auto pack = [](std::uint32_t high, std::uint32_t low) {
        return static_cast<float>((high << 16) | low);
    };
    const std::uint32_t entity0 = avgen::scene::packPickId(avgen::scene::PickSpace::Entity, 0);
    const std::uint32_t procedural0 =
        avgen::scene::packPickId(avgen::scene::PickSpace::Procedural, 0);
    REQUIRE(entity0 != procedural0);
    id.rgba[0] = pack(1, entity0);      // entity 0: its "material id" is 1
    id.rgba[4] = pack(1, procedural0);  // procedural 0: ALSO 1, and nothing to do with the entity
    id.rgba[8] = pack(2, avgen::scene::packPickId(avgen::scene::PickSpace::Entity, 1));
    id.rgba[12] = 0.0f;                 // nothing wrote here

    // The failure the object-keyed mask exists to avoid, demonstrated rather than asserted: keyed
    // on the high half, one class selects two unrelated objects.
    CHECK(quality::coverage(quality::materialClassMask(id, {1})) == Approx(0.5));
    // Keyed on the low half, it selects the one object it was given.
    CHECK(quality::coverage(quality::objectClassMask(id, {entity0})) == Approx(0.25));
    CHECK(quality::coverage(quality::objectClassMask(id, {entity0, procedural0})) == Approx(0.5));
    // The controls: an empty class list selects nothing rather than everything, and an id that is
    // in no class selects nothing rather than the background.
    CHECK(quality::objectClassMask(id, {}).empty());
    CHECK(quality::coverage(quality::objectClassMask(id, {4242})) == Approx(0.0));
}

// ---- capture -----------------------------------------------------------------------------------

TEST_CASE("a frame survives a PNG round trip bit for bit", "[quality][capture]") {
    const auto dir = scratch("roundtrip");
    const quality::Frame source = quality::ladderBase();
    REQUIRE(quality::writeFrame(dir / "frame_000000.png", source).has_value());
    const auto read = quality::readFrame(dir / "frame_000000.png");
    REQUIRE(read.has_value());
    CHECK(read->width == source.width);
    CHECK(read->height == source.height);
    CHECK(read->rgba == source.rgba);
    // Which makes the identity arm meaningful on real files and not only in memory.
    CHECK(std::isinf(quality::psnr(*read, source)));
}

TEST_CASE("a sequence is discovered in numeric order and AOV files are not mistaken for frames",
          "[quality][capture]") {
    const auto dir = scratch("sequence");
    const quality::Frame frame = quality::ladderBase();
    // Deliberately unpadded, in an order that lexicographic sorting gets wrong. A temporal metric
    // fed a mis-ordered sequence reports alternation that is an artifact of the sort.
    for (const int index : {0, 2, 9, 10, 11}) {
        REQUIRE(quality::writeFrame(dir / ("frame_" + std::to_string(index) + ".png"), frame)
                    .has_value());
    }
    // An AOV beside one of them, and a file that is not a frame at all.
    std::ofstream(dir / "frame_2.normal.exr") << "not an exr";
    std::ofstream(dir / "notes.png") << "not a frame either";

    const auto sequence = quality::discoverSequence(dir);
    REQUIRE(sequence.has_value());
    REQUIRE(sequence->size() == 5);
    CHECK(sequence->frames[0].filename() == "frame_0.png");
    CHECK(sequence->frames[1].filename() == "frame_2.png");
    CHECK(sequence->frames[2].filename() == "frame_9.png");
    CHECK(sequence->frames[3].filename() == "frame_10.png"); // after 9, not after 1
    CHECK(sequence->frames[4].filename() == "frame_11.png");

    CHECK(quality::aovPathFor(dir / "frame_2.png", "normal").filename() == "frame_2.normal.exr");
    CHECK(quality::hasAov(dir / "frame_2.png", "normal"));
    CHECK_FALSE(quality::hasAov(dir / "frame_2.png", "velocity"));

    // And an empty directory is an error rather than an empty success: "no frames" and "no
    // difference" must not be the same result.
    CHECK_FALSE(quality::discoverSequence(scratch("empty")).has_value());
}

// ---- the report --------------------------------------------------------------------------------

TEST_CASE("the report refuses to emit a temporal measure without a spatial one beside it",
          "[quality][report][adr243]") {
    quality::Report report;
    quality::Metric alternation;
    alternation.name = "temporal.temporalAlternation";
    alternation.value = 3.5;
    report.add(alternation);
    // ADR-250 §3 makes this a harness rule rather than advice, and this is the enforcement.
    CHECK_FALSE(report.pairingViolation().empty());
    CHECK(report.toJson().empty());

    quality::Metric laplacian;
    laplacian.name = "detail.spatialLaplacian";
    laplacian.value = 12.0;
    report.add(laplacian);
    CHECK(report.pairingViolation().empty());
    CHECK_FALSE(report.toJson().empty());

    // The control: an UNAVAILABLE spatial measure does not satisfy the pairing, because a caveat
    // beside a number is not the same as a number beside a number.
    quality::Report second;
    second.add(alternation);
    second.add(quality::Metric::unavailable("detail.spatialLaplacian", "no candidate frames"));
    CHECK_FALSE(second.pairingViolation().empty());
}

TEST_CASE("a measurement and a hypothesis cannot be confused in the JSON", "[quality][report]") {
    quality::Report report;
    report.findings.push_back({"detail.spatialLaplacianRatio", "the candidate carries 1.31x", 1.31,
                               std::size_t{7}});
    report.hypotheses.push_back({"FXAA's threshold is flipping on the grass",
                                {"spatialLaplacianRatio 1.31", "the reviewer said 'more distracting'"},
                                0.4});
    const nlohmann::json json = nlohmann::json::parse(report.toJson());
    REQUIRE(json["findings"].size() == 1);
    REQUIRE(json["hypotheses"].size() == 1);
    CHECK(json["findings"][0]["kind"] == "measurement");
    CHECK(json["hypotheses"][0]["kind"] == "hypothesis");
    CHECK(json["hypotheses"][0].contains("confidence"));
    // A finding has no confidence and a hypothesis has no metric: the two shapes are different, so
    // nothing can be moved from one array to the other without being rewritten by a person.
    CHECK_FALSE(json["findings"][0].contains("confidence"));
    CHECK_FALSE(json["hypotheses"][0].contains("metric"));
}

TEST_CASE("an unavailable metric carries its reason and no value", "[quality][report]") {
    quality::Report report;
    report.add(quality::Metric::unavailable("perClass.shadowStability", "no shadow AOV"));
    const nlohmann::json json = nlohmann::json::parse(report.toJson());
    REQUIRE(json["metrics"].size() == 1);
    CHECK(json["metrics"][0]["available"] == false);
    CHECK(json["metrics"][0]["reason"] == "no shadow AOV");
    // Not zero. A zero in a quality column is a claim, and this one would be the claim that shadows
    // are perfectly stable.
    CHECK(json["metrics"][0]["value"].is_null());
}

TEST_CASE("an infinite PSNR is named rather than silently becoming null", "[quality][report]") {
    quality::Report report;
    quality::Metric metric;
    metric.name = "spatial.psnr";
    metric.value = std::numeric_limits<double>::infinity();
    report.add(metric);
    const nlohmann::json json = nlohmann::json::parse(report.toJson());
    CHECK(json["metrics"][0]["value"].is_null());
    CHECK(json["metrics"][0]["valueNote"] == "infinite");
}

TEST_CASE("pooling reports the tail and the worst frame, and knows which end is bad",
          "[quality][report]") {
    const std::vector<double> values = {0.99, 0.98, 0.40, 0.97, 0.99};
    const quality::Pooled lower = quality::pool(values, /*higherIsWorse=*/false);
    CHECK(lower.worstFrameIndex == 2);
    CHECK(lower.min == Approx(0.40));
    const quality::Pooled higher = quality::pool(values, /*higherIsWorse=*/true);
    // The same data, the other convention. Getting this backwards points every diagnostic at the
    // best frame in the run, which is a failure that looks exactly like a clean result.
    CHECK(higher.worstFrameIndex == 0);
    CHECK(quality::pool({}, true).frames == 0);
}

TEST_CASE("the diagnostics localise a finding rather than decorate it", "[quality][report]") {
    const quality::Frame filtered = quality::ladderDiagonalEdge(true);
    const quality::Frame jagged = quality::ladderDiagonalEdge(false);
    const quality::Frame difference = quality::differenceImage(filtered, jagged, 8.0);
    REQUIRE(difference.valid());
    // The identity control: a difference image of a frame against itself is black. Amplified by
    // eight, anything else would be obvious -- and a difference map that is never black is a map
    // that is showing something other than the difference.
    const quality::Frame self = quality::differenceImage(filtered, filtered, 8.0);
    REQUIRE(self.valid());
    CHECK(std::all_of(self.rgba.begin(), self.rgba.end(), [i = 0](std::uint8_t v) mutable {
        return (i++ % 4) == 3 ? v == 255 : v == 0;
    }));

    const std::vector<float> lap = quality::laplacianPlane(jagged);
    const quality::TileMap tiles = quality::tileMap(lap, jagged.width, jagged.height);
    CHECK(tiles.tilesX == jagged.width / 8);
    CHECK(tiles.worstTileMean > 0.0);
    // The worst tile is on the edge, which runs from y = 12 at x = 0 to y = 56 at x = 127. A tile
    // map whose hottest tile is in an empty corner is a tile map that has lost its indexing.
    const double expectedY = 0.35 * tiles.worstTileX + 12.0;
    INFO("worst tile (" << tiles.worstTileX << ", " << tiles.worstTileY << "), the edge is near "
                        << expectedY);
    CHECK(std::abs(static_cast<double>(tiles.worstTileY) - expectedY) < 12.0);
}

// ---- the degradation contract ------------------------------------------------------------------

TEST_CASE("an absent external tool degrades with a reason rather than failing",
          "[quality][external]") {
    // ADR-250 §2: a metric whose tool is absent reports `available: false` with a reason. The run
    // succeeds and the vector is shorter. Forced here by pointing the search at nothing.
    quality::ExternalTool missing;
    missing.available = false;
    missing.reason = "ffmpeg not found; set AVGEN_FFMPEG or install ffmpeg on PATH";
    quality::VmafRequest request;
    request.candidateFrames.emplace_back("a.png");
    request.referenceFrames.emplace_back("b.png");
    request.workDirectory = scratch("vmaf");
    const auto result = quality::runVmaf(missing, request);
    REQUIRE(result.has_value()); // NOT an error: a missing optional tool is not a failed run
    CHECK_FALSE(result->available);
    CHECK(result->reason == missing.reason);
    CHECK_FALSE(result->vmaf.available);
}

TEST_CASE("a mismatched frame count is refused rather than silently mis-paired",
          "[quality][external]") {
    quality::ExternalTool present;
    present.available = true;
    present.path = "/nonexistent/ffmpeg";
    quality::VmafRequest request;
    request.candidateFrames.assign(3, "a.png");
    request.referenceFrames.assign(2, "b.png");
    request.workDirectory = scratch("vmaf-mismatch");
    const auto result = quality::runVmaf(present, request);
    REQUIRE(result.has_value());
    CHECK_FALSE(result->available);
    // libvmaf pairs frames by position, so a length mismatch compares different moments and reports
    // a plausible number for it.
    CHECK(result->reason.find("pairs frames by position") != std::string::npos);
}

TEST_CASE("the external ladder runs its arms where libvmaf exists and is skipped where it does not",
          "[quality][external]") {
    const quality::ExternalTool tool = quality::findVmafTool();
    const quality::LadderResult result = quality::runExternalLadder(scratch("external-ladder"));
    if (!tool.available) {
        // The degradation contract: no arms, no failure, and a reason the caller can print.
        CHECK(result.arms.empty());
        CHECK_FALSE(tool.reason.empty());
        SUCCEED("libvmaf unavailable: " << tool.reason);
        return;
    }
    REQUIRE_FALSE(result.arms.empty());
    for (const quality::Arm& arm : result.arms) {
        for (const quality::Check& check : arm.checks) {
            INFO(arm.name << " / " << check.description << " :: " << check.expectation
                          << " observed " << check.observed << " vs " << check.reference);
            CHECK(check.passed);
        }
    }
}
