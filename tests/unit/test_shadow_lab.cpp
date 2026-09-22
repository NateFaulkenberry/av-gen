// The Shadow Lab's CPU half (§15): the caster rule, the cascade a depth belongs to, the bias a view
// reports, and the basis a camera-facing impostor is built from inside a shadow pass.
//
// None of it needs a device. The fit, the splits and the caster rule are arithmetic over a camera,
// a light and some bounds, and a decision that can only be checked by rendering something is a
// decision nobody checks (rendering/visibility.hpp's reason for existing, applied to shadows).
//
// **The fixtures here are deliberately badly behaved** (ADR-182). The visibility bug of 2026-09-16
// survived every test in the tree because every fixture was a centred box, for which the correct
// and incorrect radius rules are bit-identical -- the bug was in the shape of the fixtures. So the
// entities below are thin, off-origin, oversized, rotated and scaled, and the one centred cube is
// there as the control that says the others are being measured against something.

#include "rendering/shadow_math.hpp"
#include "rendering/visibility.hpp"
#include "scene/mesh_generators.hpp"
#include "scene/procedural.hpp"
#include "scene/scene.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <vector>

using namespace avgen;
using rendering::CasterState;

namespace {

// A box mesh whose local bounds are `lo..hi` -- NOT centred on the origin unless you ask for it.
// `scene::makeCube` is centred, which is exactly the shape that hid the last cull bug, so this
// takes both corners.
scene::MeshData boxBetween(const glm::vec3& lo, const glm::vec3& hi) {
    scene::MeshData m;
    for (int c = 0; c < 8; ++c) {
        m.vertices.push_back({glm::vec3((c & 1) ? hi.x : lo.x, (c & 2) ? hi.y : lo.y, (c & 4) ? hi.z : lo.z),
                              glm::vec3(0.0f, 1.0f, 0.0f), glm::vec2(0.0f)});
    }
    // Two triangles are enough for a bounds test; the mesh is never rasterised here.
    m.indices = {0, 1, 2, 1, 3, 2};
    return m;
}

// A tree-shaped caster: tall, standing on its own origin, wider at the top. The shape the cull
// radius fix was about, and the shape a bounding sphere about the *centre* describes worst.
constexpr glm::vec3 kTreeLo{-2.194f, -0.243f, -2.225f};
constexpr glm::vec3 kTreeHi{2.117f, 7.022f, 2.353f};

// A camera looking down -Z from +Z, and the frustum planes of it.
glm::mat4 cameraViewProj(const glm::vec3& eye, const glm::vec3& at, float fovY = 0.96f,
                         float aspect = 16.0f / 9.0f, float near = 0.5f, float far = 200.0f) {
    return glm::perspective(fovY, aspect, near, far) * glm::lookAt(eye, at, glm::vec3(0.0f, 1.0f, 0.0f));
}

std::vector<rendering::FrustumPlanes> planesOf(const std::vector<rendering::ShadowView>& views) {
    std::vector<rendering::FrustumPlanes> out;
    out.reserve(views.size());
    for (const rendering::ShadowView& v : views) {
        out.push_back(rendering::frustumPlanes(v.viewProj));
    }
    return out;
}

// Three cascades fitted to a camera, exactly as `ShadowRenderer::update` fits them.
std::vector<rendering::ShadowView> cascadesFor(const glm::mat4& viewProj, const glm::vec3& lightDir,
                                               float near = 0.5f, float far = 200.0f,
                                               float sceneRadius = 120.0f, std::uint32_t count = 3) {
    const float range = rendering::directionalShadowRange(near, far, sceneRadius,
                                                          rendering::kShadowRangeReference, 0.08f);
    const std::vector<float> splits = rendering::cascadeSplits(near, range, count);
    const glm::mat4 inverse = glm::inverse(viewProj);
    std::vector<rendering::ShadowView> views;
    float nearDepth = near;
    for (std::uint32_t c = 0; c < count; ++c) {
        views.push_back(rendering::fitDirectionalCascade(inverse, near, far, nearDepth, splits[c],
                                                         lightDir, 2048, sceneRadius));
        nearDepth = splits[c];
    }
    return views;
}

} // namespace

// ================================================================================================
// The caster rule
// ================================================================================================

TEST_CASE("the shadow caster rule answers for every shape, not only for a centred cube",
          "[shadows][lab][casters]") {
    scene::Scene s;
    const auto tree = s.addMesh(boxBetween(kTreeLo, kTreeHi));
    const auto plank = s.addMesh(boxBetween({-3.0f, 0.0f, -0.02f}, {3.0f, 6.0f, 0.02f}));
    const auto cube = s.addMesh(scene::makeCube(0.5f));

    // Indices, not references. `addEntity` returns a reference into the scene's own vector and the
    // next call may reallocate it -- which is how the first version of this test asked about a
    // dangling entity and got `NotDrawable` for a perfectly drawable plank.
    s.addEntity("standing-tree", tree);
    s.addEntity("thin-plank", plank);
    s.addEntity("centred-cube", cube);
    scene::Entity& standing = s.entities[0];
    scene::Entity& thin = s.entities[1];
    scene::Entity& control = s.entities[2];
    standing.transform.position = {0.0f, 0.0f, -30.0f};
    standing.transform.scale = glm::vec3(1.9f);
    thin.transform.position = {6.0f, 0.0f, -30.0f};
    control.transform.position = {-6.0f, 1.0f, -30.0f};

    const glm::mat4 viewProj = cameraViewProj({0.0f, 6.0f, 14.0f}, {0.0f, 3.0f, -80.0f});
    const auto views = cascadesFor(viewProj, glm::normalize(glm::vec3(0.829f, -0.559f, 0.0f)));
    const auto planes = planesOf(views);

    SECTION("an entity the camera kept is a caster, whatever its shape") {
        for (const scene::Entity& e : s.entities) {
            INFO(e.name);
            CHECK(rendering::casterState(e, s.meshBounds(e.mesh), planes) == CasterState::Caster);
        }
    }

    SECTION("castsShadow off is reported as itself, not as a cull") {
        standing.castsShadow = false;
        CHECK(rendering::casterState(standing, s.meshBounds(standing.mesh), planes) ==
              CasterState::ShadowDisabled);
    }

    SECTION("the styles the shadow passes skip are reported as one reason") {
        thin.style = scene::MeshStyle::Water;
        CHECK(rendering::casterState(thin, s.meshBounds(thin.mesh), planes) == CasterState::StyleExcluded);
        thin.style = scene::MeshStyle::Grid;
        CHECK(rendering::casterState(thin, s.meshBounds(thin.mesh), planes) == CasterState::StyleExcluded);
    }

    // ADR-701. This section used to be "the *three* styles the shadow passes skip", and the third
    // was any blended material at all. That stopped being right the day ADR-385 gave the engine a
    // fade: a fade PROMOTES a node under 1.0 opacity to Blend for exactly the frames it is fading,
    // so the exclusion deleted the abducted animal's shadow at an opacity of 0.998 and the animal
    // was not gone until 1.13 s later. `fs_depth` discards an ordered fraction of a blended
    // caster's depth texels instead, so what it casts is proportional to its alpha -- and the only
    // blended body that casts nothing is one nobody can see.
    SECTION("a blended body casts in proportion to its alpha, and only zero casts nothing") {
        thin.style = scene::MeshStyle::Lit;
        thin.material.alphaMode = scene::AlphaMode::Blend;
        for (const float opacity : {1.0f, 0.5f, 0.002f}) {
            thin.material.opacity = opacity;
            INFO("a blended caster at opacity " << opacity);
            CHECK(rendering::casterState(thin, s.meshBounds(thin.mesh), planes) ==
                  CasterState::Caster);
        }
        thin.material.opacity = 0.0f;
        CHECK(rendering::casterState(thin, s.meshBounds(thin.mesh), planes) ==
              CasterState::StyleExcluded);
    }

    SECTION("an invisible entity is not drawable rather than not casting") {
        control.visible = false;
        CHECK(rendering::casterState(control, s.meshBounds(control.mesh), planes) == CasterState::NotDrawable);
    }

    SECTION("ADR-046: off screen is not a reason to stop casting") {
        // Straight up, well above the camera frustum, and still inside the cascades -- which is
        // exactly the hill-behind-the-camera case the second pass exists for.
        standing.transform.position = {0.0f, 0.0f, 6.0f};
        standing.cameraCulled = true;
        CHECK(rendering::casterState(standing, s.meshBounds(standing.mesh), planes) ==
              CasterState::CasterOffScreen);
    }

    SECTION("a camera-culled entity outside every view is reported against the view, not the camera") {
        standing.transform.position = {0.0f, 0.0f, 4000.0f};
        standing.cameraCulled = true;
        CHECK(rendering::casterState(standing, s.meshBounds(standing.mesh), planes) ==
              CasterState::OutsideEveryView);
    }

    SECTION("with no shadow views the answer is about the frame, not the object") {
        standing.cameraCulled = true;
        CHECK(rendering::casterState(standing, s.meshBounds(standing.mesh), {}) == CasterState::NoShadowViews);
        // ...and an entity the camera kept still casts, because the first pass never asks a view.
        CHECK(rendering::casterState(thin, s.meshBounds(thin.mesh), {}) == CasterState::Caster);
    }
}

TEST_CASE("the caster bound is the transformed corners, not the transformed bound",
          "[shadows][lab][casters]") {
    // The failure a centred cube cannot show. A thin plank rotated 45 degrees about Y has a world
    // AABB much wider than its local one; taking the local extent and translating it -- the
    // plausible shortcut -- understates it by 2.1 m on each side, which is a caster that a cascade
    // at the edge of its frustum would drop while its shadow was in shot.
    scene::Entity e;
    e.name = "plank";
    e.transform.position = {10.0f, 0.0f, -20.0f};
    e.transform.rotation = glm::angleAxis(glm::radians(45.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    const std::pair<glm::vec3, glm::vec3> local{{-3.0f, 0.0f, -0.02f}, {3.0f, 6.0f, 0.02f}};

    const auto [lo, hi] = rendering::entityWorldBounds(e, local);
    const float halfX = (hi.x - lo.x) * 0.5f;
    const float halfZ = (hi.z - lo.z) * 0.5f;
    INFO("world half extents " << halfX << ", " << halfZ);
    // 3 m of plank at 45 degrees reaches 3 / sqrt(2) = 2.121 m along each of x and z.
    CHECK_THAT(halfX, Catch::Matchers::WithinAbs(2.136f, 0.02f));
    CHECK_THAT(halfZ, Catch::Matchers::WithinAbs(2.136f, 0.02f));
    // The shortcut's answer, for contrast: the untransformed half extent is 3.0 and 0.02.
    CHECK(halfZ > 0.02f * 50.0f);
    // Height is unchanged by a yaw, and the box sits on its own origin: y runs 0..6, not -3..3.
    CHECK_THAT(lo.y, Catch::Matchers::WithinAbs(0.0f, 1e-4f));
    CHECK_THAT(hi.y, Catch::Matchers::WithinAbs(6.0f, 1e-4f));

    // Scale, too: the caster rule is applied after the entity's own scale, and a tree normalised to
    // 1.9x is 13.8 m tall rather than 7.3.
    scene::Entity tall;
    tall.transform.scale = glm::vec3(1.9f);
    const auto [tlo, thi] = rendering::entityWorldBounds(tall, {kTreeLo, kTreeHi});
    CHECK_THAT(thi.y - tlo.y, Catch::Matchers::WithinAbs((kTreeHi.y - kTreeLo.y) * 1.9f, 1e-3f));
}

// ================================================================================================
// Cascade selection
// ================================================================================================

TEST_CASE("a depth exactly on a split belongs to the nearer cascade", "[shadows][lab][cascades]") {
    // The CPU mirror of `cascadeFor` in shaders/shadows.wgsl. Both compare `<=`, and which side of
    // the boundary wins is not cosmetic: the crossfade band in `shadowFactor` is measured from the
    // far edge of the cascade a fragment is *in*, so a fragment that changed cascade at the split
    // would cross the band twice.
    const std::array<float, 3> splits{6.19f, 19.99f, 77.28f};
    const std::span<const float> s(splits);

    CHECK(rendering::cascadeForDepth(0.0f, s) == 0);
    CHECK(rendering::cascadeForDepth(6.18f, s) == 0);
    CHECK(rendering::cascadeForDepth(6.19f, s) == 0);  // exactly on it: the nearer one
    CHECK(rendering::cascadeForDepth(6.1901f, s) == 1);
    CHECK(rendering::cascadeForDepth(19.99f, s) == 1);
    CHECK(rendering::cascadeForDepth(20.0f, s) == 2);
    CHECK(rendering::cascadeForDepth(77.28f, s) == 2);
    // Past the last split there is no cascade; the shader clamps to the last one and
    // `shadowLookup` then reports the sample invalid, which the range fade has already faded out.
    CHECK(rendering::cascadeForDepth(1000.0f, s) == 2);
    // A frame with no splits at all answers 0 rather than indexing nothing.
    CHECK(rendering::cascadeForDepth(5.0f, {}) == 0);
}

TEST_CASE("every cascade's own depth interval is contiguous with its neighbours",
          "[shadows][lab][cascades]") {
    const glm::mat4 viewProj = cameraViewProj({0.0f, 6.0f, 14.0f}, {0.0f, 3.0f, -80.0f});
    const auto views = cascadesFor(viewProj, glm::normalize(glm::vec3(0.829f, -0.559f, 0.0f)));
    REQUIRE(views.size() == 3);
    CHECK_THAT(views[0].nearDistance, Catch::Matchers::WithinAbs(0.5f, 1e-4f));
    for (std::size_t i = 1; i < views.size(); ++i) {
        INFO("cascade " << i);
        // No gap and no overlap: a gap is a band of depth with no shadow map and an overlap is two
        // cascades claiming one fragment, and `cascadeFor` would silently pick the nearer.
        CHECK_THAT(views[i].nearDistance, Catch::Matchers::WithinAbs(views[i - 1].farDistance, 1e-4f));
        // Each cascade is coarser than the one in front of it, which is the whole point of the
        // ladder. A scheme that produced a finer far cascade would be spending its texels backwards.
        CHECK(views[i].texelWorldSize > views[i - 1].texelWorldSize);
    }
}

// ================================================================================================
// Bias
// ================================================================================================

TEST_CASE("the same visual bias is a different number in every cascade", "[shadows][lab][bias]") {
    const glm::mat4 viewProj = cameraViewProj({0.0f, 6.0f, 14.0f}, {0.0f, 3.0f, -80.0f});
    const auto views = cascadesFor(viewProj, glm::normalize(glm::vec3(0.829f, -0.559f, 0.0f)));
    REQUIRE(views.size() == 3);

    std::array<rendering::ShadowViewReport, 3> report{};
    for (std::size_t i = 0; i < views.size(); ++i) {
        report[i] = rendering::reportView(views[i], static_cast<std::uint32_t>(i));
        INFO("cascade " << i << " world bias " << report[i].worldBias << " normalised "
                        << report[i].depthBias);
        // The arithmetic the renderer used to do inline, now stated once. Two of the view's own
        // texels plus a 5 mm floor, converted into the normalised depth the comparison happens in.
        CHECK_THAT(report[i].worldBias,
                   Catch::Matchers::WithinAbs(views[i].texelWorldSize * 2.0f + 0.005f, 1e-7f));
        CHECK_THAT(report[i].depthBias,
                   Catch::Matchers::WithinRel(report[i].worldBias / views[i].depthRange, 1e-5f));
        CHECK(report[i].depthBias > 0.0f);
    }

    // The point of expressing the constant in texels: in metres the three cascades differ by an
    // order of magnitude, and once normalised they agree to within a third. A bias authored as a
    // normalised constant would be either useless in cascade 0 or acne in cascade 2.
    const float worldSpread = report[2].worldBias / report[0].worldBias;
    const float normalisedSpread = report[0].depthBias / report[2].depthBias;
    INFO("world spread " << worldSpread << ", normalised spread " << normalisedSpread);
    CHECK(worldSpread > 5.0f);
    CHECK(normalisedSpread < 2.0f);

    // The normal offset is the shader's floor at normal incidence: one texel times 1.4.
    for (std::size_t i = 0; i < views.size(); ++i) {
        CHECK_THAT(report[i].normalOffset,
                   Catch::Matchers::WithinRel(views[i].texelWorldSize * 1.4f, 1e-5f));
    }
}

// ================================================================================================
// The billboard basis a shadow pass hands the impostor rungs
// ================================================================================================

TEST_CASE("a shadow view's billboard basis is its own, not the camera's", "[shadows][lab][impostor]") {
    // Rungs 2 and 3 of the LOD ladder are camera-facing quads built in the vertex shader from
    // `frame.cameraRight` / `frame.cameraUp`. A shadow pass runs that shader against a copy of the
    // frame block, and until this basis existed the copy carried the camera's two axes -- so the
    // quad faced the viewer and was then rasterised from the light. A stationary tree under a
    // stationary sun cast a shadow whose size depended on where the camera stood.
    //
    // The invariant that cannot be faked: the basis is orthonormal and perpendicular to the
    // direction the view looks along. `up x right` is that direction up to sign.
    const glm::vec3 light = glm::normalize(glm::vec3(0.829f, -0.559f, 0.0f));
    const glm::mat4 viewProj = cameraViewProj({0.0f, 6.0f, 14.0f}, {0.0f, 3.0f, -80.0f});
    const auto views = cascadesFor(viewProj, light);
    REQUIRE(views.size() == 3);

    for (const rendering::ShadowView& v : views) {
        CHECK_THAT(glm::length(v.right), Catch::Matchers::WithinAbs(1.0f, 1e-5f));
        CHECK_THAT(glm::length(v.up), Catch::Matchers::WithinAbs(1.0f, 1e-5f));
        CHECK_THAT(glm::dot(v.right, v.up), Catch::Matchers::WithinAbs(0.0f, 1e-5f));
        // A quad spanned by these two axes presents its full area to the light.
        CHECK_THAT(glm::dot(v.right, light), Catch::Matchers::WithinAbs(0.0f, 1e-5f));
        CHECK_THAT(glm::dot(v.up, light), Catch::Matchers::WithinAbs(1e-7f, 1e-5f));
    }

    // And the control that makes the check mean something (ADR-182): with this light at right
    // angles to this camera, the camera's own right axis is NOT perpendicular to the light -- so a
    // quad built from the camera's basis presents almost nothing to it. That is the number the
    // frame used to be drawn with.
    const glm::mat3 cameraBasis = glm::transpose(glm::mat3(glm::lookAt(
        glm::vec3(0.0f, 6.0f, 14.0f), glm::vec3(0.0f, 3.0f, -80.0f), glm::vec3(0.0f, 1.0f, 0.0f))));
    const glm::vec3 cameraRight = cameraBasis[0];
    const glm::vec3 cameraUp = cameraBasis[1];
    const glm::vec3 cameraQuadNormal = glm::normalize(glm::cross(cameraRight, cameraUp));
    const float presentedByCamera = std::abs(glm::dot(cameraQuadNormal, light));
    const float presentedByView = std::abs(glm::dot(glm::normalize(glm::cross(views[0].right, views[0].up)), light));
    INFO("a camera-facing quad presents " << presentedByCamera << " of its area to this light; "
                                          << "a view-facing quad presents " << presentedByView);
    CHECK(presentedByCamera < 0.05f);
    CHECK(presentedByView > 0.99f);
}

TEST_CASE("a spot and a cube face carry a billboard basis too", "[shadows][lab][impostor]") {
    scene::PunctualLight spot;
    spot.type = scene::PunctualLight::Type::Spot;
    spot.position = {0.0f, 12.0f, 0.0f};
    spot.direction = glm::normalize(glm::vec3(0.2f, -1.0f, 0.1f));
    spot.outerConeAngle = 0.6f;
    const rendering::ShadowView view = rendering::fitSpotShadow(spot, 2048, 40.0f);
    CHECK_THAT(glm::dot(view.right, spot.direction), Catch::Matchers::WithinAbs(0.0f, 1e-5f));
    CHECK_THAT(glm::dot(view.up, spot.direction), Catch::Matchers::WithinAbs(0.0f, 1e-5f));

    scene::PunctualLight point;
    point.type = scene::PunctualLight::Type::Point;
    point.position = {3.0f, 4.0f, -5.0f};
    const auto faces = rendering::fitPointShadow(point, 1024, 20.0f);
    static constexpr glm::vec3 kForward[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (std::size_t f = 0; f < faces.size(); ++f) {
        INFO("face " << f);
        CHECK_THAT(glm::dot(faces[f].right, kForward[f]), Catch::Matchers::WithinAbs(0.0f, 1e-5f));
        CHECK_THAT(glm::dot(faces[f].up, kForward[f]), Catch::Matchers::WithinAbs(0.0f, 1e-5f));
    }
}

// ================================================================================================
// The two invariants this lab could not repair, repaired (ADR-285, ADR-287)
// ================================================================================================

TEST_CASE("a procedural instance a cascade can see is submitted to the shadow passes",
          "[shadows][lab][casters]") {
    // Was `[!shouldfail]`. `ProceduralRenderer` ran ONE cull, against the camera frustum, and
    // `drawShadow` issued its indirect draws against the args that cull wrote -- so an instance the
    // camera rejected cast nothing, however plainly its shadow fell into shot. Entities have had a
    // second pass over exactly this case since ADR-046; the ecology had nothing. On Glowmere
    // multicam that read as 62,284 procedural instances, 496 surviving the camera cull, and 496
    // drawn into the shadow maps: one number where there should be two.
    //
    // The invariant, stated as the decision rather than as the symptom: which set an instance
    // belongs to for the shadow passes is decided by a **shadow view**. `rendering::shadowCullLodLevel`
    // is that decision and shaders/cull.wgsl mirrors it; the rendered half is in
    // tests/rendering/test_shadow_lab_gpu.cpp, because a decision function agreeing with itself
    // proves nothing about a pixel (§37).
    const glm::vec3 light = glm::normalize(glm::vec3(0.829f, -0.559f, 0.0f));
    const glm::mat4 viewProj = cameraViewProj({0.0f, 6.0f, 14.0f}, {0.0f, 3.0f, -80.0f});
    const rendering::FrustumPlanes cameraPlanes = rendering::frustumPlanes(viewProj);
    const auto views = cascadesFor(viewProj, light);
    const auto viewPlanes = planesOf(views);
    const std::span<const rendering::FrustumPlanes> shadowViews(viewPlanes);

    scene::LodSettings lod;
    lod.cull = true;
    lod.maxDistance = 400.0f;
    lod.minScreenRadius = 1.0f;
    rendering::CullCamera cullCamera;
    cullCamera.position = {0.0f, 6.0f, 14.0f};
    cullCamera.projScale = rendering::cullProjScale(0.96f, 720);

    // **Not a centred cube anywhere in this list** (ADR-182). The cull sphere is centred on the
    // record position, so a shape that stands on its own origin is the one the radius rules
    // disagree about, and a thin one is the one a plane test can slip past. Each case names what it
    // is for, and the last two are controls: things that must NOT become casters.
    struct Case {
        const char* what;
        glm::vec3 center;
        glm::vec3 lo;        // source bounds, in the source's own space
        glm::vec3 hi;
        glm::vec3 scale;     // record scale, non-uniform where it matters
        bool cameraSees;     // what the camera frustum should say
        bool castsExpected;  // what the shadow list should say
    };
    const std::vector<Case> cases{
        // The lab's own arm: a Glowmere canopy tree 80 m off to the left, outside the camera
        // frustum, inside cascade 2, its shadow running 20 m to +X -- into shot.
        {"a canopy tree off the left of frame", {-80.0f, 3.5f, -40.0f}, kTreeLo, kTreeHi,
         glm::vec3(1.9f), false, true},
        // Above the top of the frame, which is the case ADR-046 was written for and the ecology
        // never got: the thing itself is out of shot and the thing it throws is not.
        {"a tree above the top of frame", {0.0f, 50.0f, -20.0f}, kTreeLo, kTreeHi, glm::vec3(1.9f),
         false, true},
        // Thin: 4 cm of one axis, which is under cascade 2's own texel. A plane test with a radius
        // taken from the wrong sphere loses this one first.
        {"a thin upright panel off frame", {-46.0f, 3.0f, -18.0f}, {-3.0f, 0.0f, -0.02f},
         {3.0f, 6.0f, 0.02f}, glm::vec3(1.0f), false, true},
        // Off-origin AND non-uniformly scaled: the geometry is authored six metres up its own stem
        // and then squashed, so the sphere the shader forms is `sourceCullRadius * max|scale|` and
        // no single axis predicts it.
        {"an off-origin cap on a non-uniform scale", {-60.0f, 0.0f, -22.0f}, {-0.5f, 5.0f, -0.5f},
         {0.5f, 7.0f, 0.5f}, {0.4f, 1.6f, 0.4f}, false, true},
        // The control that says the camera list is not simply being reused: something the camera
        // DOES see, in the same frame, at the same rung.
        {"a tree in the middle of frame", {0.0f, 0.0f, -18.0f}, kTreeLo, kTreeHi, glm::vec3(1.9f), true, true},
        // The two controls that must come back rejected, or "casts" would mean nothing. Past the
        // shadow range the cascades reach (ADR-112, 77 m here) there is no map to be drawn into --
        // this one is off frame as well, so neither list keeps it...
        {"a tree past the cascades, off frame", {-220.0f, 0.0f, -260.0f}, kTreeLo, kTreeHi,
         glm::vec3(1.9f), false, false},
        // ...and this one is straight down the lens at 150 m: inside the camera's far plane, past
        // the 77 m the cascades reach. The camera keeps it and no view can, which is the direction
        // of asymmetry nobody expects and the reason the caster list is not a superset.
        {"a tree the camera sees at 150 m", {0.0f, 0.0f, -150.0f}, kTreeLo, kTreeHi, glm::vec3(1.9f),
         true, false},
    };

    for (const Case& c : cases) {
        INFO(c.what);
        const float radius =
            rendering::sourceCullRadius(c.lo, c.hi) * std::max({c.scale.x, c.scale.y, c.scale.z});
        const int cameraLevel = rendering::cullLodLevel(lod, cameraPlanes, cullCamera, c.center, radius);
        // The control on the fixture itself: the camera really does say what the case claims, or
        // the arm below is measuring nothing.
        CHECK((cameraLevel >= 0) == c.cameraSees);

        // And a cascade really can (or cannot) see it, written out rather than taken on trust.
        bool someViewSees = false;
        for (const auto& planes : viewPlanes) {
            bool inside = true;
            for (const glm::vec4& plane : planes) {
                if (glm::dot(glm::vec3(plane), c.center) + plane.w < -radius) {
                    inside = false;
                    break;
                }
            }
            someViewSees = someViewSees || inside;
        }
        CHECK(someViewSees == c.castsExpected);

        // The invariant.
        const int shadowLevel =
            rendering::shadowCullLodLevel(lod, shadowViews, cullCamera, c.center, radius);
        CHECK((shadowLevel >= 0) == c.castsExpected);
        // ...and an instance in both lists is on the SAME rung in both, so a caster is rasterised
        // at the mesh the frame draws. A shadow at a different level of detail from its object is a
        // shadow that does not line up with it.
        if (cameraLevel >= 0 && shadowLevel >= 0) {
            CHECK(shadowLevel == cameraLevel);
        }
    }

    SECTION("no shadow views is no caster list") {
        // How a frame with shadows switched off says so. Every instance is rejected, which is what
        // keeps the frame this renderer produced before the second list existed.
        const float radius = rendering::sourceCullRadius(kTreeLo, kTreeHi) * 1.9f;
        CHECK(rendering::shadowCullLodLevel(lod, {}, cullCamera, {0.0f, 0.0f, -18.0f}, radius) < 0);
    }

    SECTION("the whole-object proof agrees with the per-instance one") {
        // `objectFullyCulledForShadows` is what lets the renderer skip an object's shadow dispatches
        // and draws before encoding them, so it must never claim "nothing casts" about a set that
        // contains a caster. Checked against the per-instance decision over the same records rather
        // than against a second copy of its own arithmetic.
        const auto proofAgrees = [&](const std::vector<glm::vec3>& positions, float scale) {
            std::vector<scene::InstanceRecord> records;
            for (const glm::vec3& p : positions) {
                scene::InstanceRecord r{};
                r.position = glm::vec4(p, 1.0f);
                r.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
                r.scale = {scale, scale, scale, 0.0f};
                records.push_back(r);
            }
            const rendering::InstanceBounds bounds = rendering::instanceBounds(records);
            const float radius = rendering::sourceCullRadius(kTreeLo, kTreeHi) * scale;
            bool anyCasts = false;
            for (const glm::vec3& p : positions) {
                anyCasts = anyCasts ||
                           rendering::shadowCullLodLevel(lod, shadowViews, cullCamera, p, radius) >= 0;
            }
            const bool proof = rendering::objectFullyCulledForShadows(lod, shadowViews, cullCamera,
                                                                      glm::mat4(1.0f), bounds,
                                                                      rendering::sourceCullRadius(kTreeLo, kTreeHi));
            INFO("proof says fully culled: " << proof << ", instances that cast: " << anyCasts);
            // The proof may be conservative; it may never be wrong.
            CHECK_FALSE((proof && anyCasts));
            return proof;
        };
        // An arm the proof must NOT reject: one instance off frame but inside a cascade.
        CHECK_FALSE(proofAgrees({{-80.0f, 3.5f, -40.0f}}, 1.9f));
        // And a control it must reject, or it is not proving anything: a cloud 600 m away.
        CHECK(proofAgrees({{-600.0f, 0.0f, -620.0f}, {-610.0f, 0.0f, -630.0f}}, 1.9f));
    }
}

TEST_CASE("a LOD impostor is the size of the object it stands in for",
          "[shadows][lab][impostor]") {
    // Was `[!shouldfail]`. `makeLodMesh` sized the rung-2 quad from `sourceBoundingRadius(spec)` --
    // the RAW asset -- and the billboard branch of shaders/procedural.wgsl scales the quad by
    // `inst.scale` alone and never applies `proc.sourceMatrix`. Rungs 0 and 1 do go through
    // `sourceMatrix`. Every terrain scatter layer normalises its asset's height onto
    // `sourceTransform` ("the layer says how tall the thing should be; the asset says how tall it
    // is"), so the impostor drew at the asset's authored size in the camera pass as well as in the
    // shadows: a 0.5 m shrub grid drew as a hedge of 7.3 m trees.
    //
    // The size is now taken through `sourceTransform.scale`, which is the one step of the chain a
    // quad built in the camera's basis cannot pick up for itself.
    //
    // Three arms and a control, because the wrong rule and the right one agree on a uniformly
    // scaled cube and this file exists to stop that happening again (ADR-182).
    const auto halfExtentOf = [](const scene::MeshData& quad) {
        float half = 0.0f;
        for (const scene::Vertex& v : quad.vertices) {
            half = std::max(half, std::max(std::abs(v.position.x), std::abs(v.position.y)));
        }
        return half;
    };
    scene::SourceSpec box;
    box.kind = scene::PrimitiveKind::Box;
    box.size = glm::vec3(2.0f);

    SECTION("the control: an unscaled source is the size it always was") {
        // The half that says the fix is not a blanket shrink. A layer that does not normalise its
        // source gets byte-identical geometry to the one this code shipped with.
        const auto quad = scene::makeLodMesh(box, 2, 1.0f, glm::vec3(1.0f));
        REQUIRE(quad.has_value());
        CHECK_THAT(halfExtentOf(*quad),
                   Catch::Matchers::WithinRel(scene::sourceBoundingRadius(box), 1e-5f));
    }

    SECTION("a scatter layer's normalisation reaches the quad") {
        scene::ProceduralGeometry g;
        g.name = "shrub";
        g.source = box;
        g.sourceTransform.scale = glm::vec3(0.1f);
        const auto quad = scene::makeLodMesh(g.source, 2, g.lod.impostorSize, g.sourceTransform.scale);
        REQUIRE(quad.has_value());
        const float wanted = scene::sourceBoundingRadius(g.source) * g.sourceTransform.scale.x;
        INFO("impostor half extent " << halfExtentOf(*quad) << ", the scaled source's radius " << wanted);
        CHECK_THAT(halfExtentOf(*quad), Catch::Matchers::WithinRel(wanted, 0.01f));
    }

    SECTION("a non-uniform normalisation is not read off one axis") {
        // The shape that separates "multiply the radius by scale.x" from "measure the scaled box".
        // A source squashed in y has a smaller sphere than scale.x alone would give it, and a
        // source stretched in y has a larger one, so a rule that picks an axis is wrong in both
        // directions here and right on the uniform fixture above.
        scene::SourceSpec slab;
        slab.kind = scene::PrimitiveKind::Box;
        slab.size = {2.0f, 2.0f, 2.0f};
        for (const glm::vec3 scale : {glm::vec3(0.1f, 0.5f, 0.1f), glm::vec3(0.5f, 0.1f, 0.5f)}) {
            INFO("source scale (" << scale.x << ", " << scale.y << ", " << scale.z << ")");
            const auto quad = scene::makeLodMesh(slab, 2, 1.0f, scale);
            REQUIRE(quad.has_value());
            const float wanted = scene::sourceBoundingRadius(slab, scale);
            CHECK_THAT(halfExtentOf(*quad), Catch::Matchers::WithinRel(wanted, 1e-4f));
            // ...and it really is a different number from the one-axis rule, or the check above
            // would pass whichever rule the code used.
            CHECK(std::abs(wanted - scene::sourceBoundingRadius(slab) * scale.x) > 1e-3f);
        }
    }

    SECTION("a thin source, where the sphere and the tallest axis disagree most") {
        scene::SourceSpec post;
        post.kind = scene::PrimitiveKind::Box;
        post.size = {0.08f, 6.0f, 0.08f};
        const glm::vec3 scale(1.0f, 0.25f, 1.0f);
        const auto quad = scene::makeLodMesh(post, 2, 1.0f, scale);
        REQUIRE(quad.has_value());
        CHECK_THAT(halfExtentOf(*quad),
                   Catch::Matchers::WithinRel(scene::sourceBoundingRadius(post, scale), 1e-4f));
    }

    SECTION("rung 3 is an eighth of rung 2, at the scaled size too") {
        const glm::vec3 scale(0.1f);
        const auto quad = scene::makeLodMesh(box, 2, 1.0f, scale);
        const auto dot = scene::makeLodMesh(box, 3, 1.0f, scale);
        REQUIRE(quad.has_value());
        REQUIRE(dot.has_value());
        CHECK_THAT(halfExtentOf(*dot), Catch::Matchers::WithinRel(halfExtentOf(*quad) * 0.125f, 1e-4f));
    }

    SECTION("the world size the quad is built at is the world size rung 0 draws at") {
        // The invariant behind the arithmetic, stated without reference to which function holds the
        // scale: rung 0 is source geometry through `sourceMatrix`, rung 2 is a quad circumscribing
        // it, and they must describe one object. Measured off the meshes, not off a formula.
        const glm::vec3 scale(0.1f, 0.35f, 0.1f);
        const auto rung0 = scene::makeLodMesh(box, 0);
        const auto rung2 = scene::makeLodMesh(box, 2, 1.0f, scale);
        REQUIRE(rung0.has_value());
        REQUIRE(rung2.has_value());
        float rung0Radius = 0.0f;
        for (const scene::Vertex& v : rung0->vertices) {
            rung0Radius = std::max(rung0Radius, glm::length(v.position * scale));
        }
        INFO("rung 0 scaled radius " << rung0Radius << ", rung 2 half extent " << halfExtentOf(*rung2));
        CHECK_THAT(halfExtentOf(*rung2), Catch::Matchers::WithinRel(rung0Radius, 0.01f));
    }
}
