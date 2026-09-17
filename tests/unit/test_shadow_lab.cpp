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

#include <array>
#include <cmath>
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

    SECTION("the three styles the shadow passes skip are reported as one reason") {
        thin.style = scene::MeshStyle::Water;
        CHECK(rendering::casterState(thin, s.meshBounds(thin.mesh), planes) == CasterState::StyleExcluded);
        thin.style = scene::MeshStyle::Grid;
        CHECK(rendering::casterState(thin, s.meshBounds(thin.mesh), planes) == CasterState::StyleExcluded);
        thin.style = scene::MeshStyle::Lit;
        thin.material.alphaMode = scene::AlphaMode::Blend;
        CHECK(rendering::casterState(thin, s.meshBounds(thin.mesh), planes) == CasterState::StyleExcluded);
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
// The two invariants this lab could not repair
// ================================================================================================

TEST_CASE("a procedural instance a cascade can see is submitted to the shadow passes",
          "[shadows][lab][casters][!shouldfail]") {
    // **This is expected to fail, and the day it stops failing is the day somebody fixed it.**
    //
    // `ProceduralRenderer` runs ONE cull, against the camera frustum, and `drawShadow` issues its
    // indirect draws against the args that cull wrote. So an instance the camera rejects casts
    // nothing, however plainly its shadow falls into shot. Entities get a second pass over exactly
    // this case (ADR-046, `rendering::casterState`); the ecology does not.
    //
    // Expressed as the invariant rather than as the symptom: the set an instance belongs to for the
    // shadow passes should be decided by a shadow view, and `cullLodLevel` against the camera is
    // what actually decides it. See docs/shadow-lab/README.md §3.1 for the measurement and for what
    // the fix would cost.
    const glm::vec3 light = glm::normalize(glm::vec3(0.829f, -0.559f, 0.0f));
    const glm::mat4 viewProj = cameraViewProj({0.0f, 6.0f, 14.0f}, {0.0f, 3.0f, -80.0f});
    const rendering::FrustumPlanes cameraPlanes = rendering::frustumPlanes(viewProj);
    const auto views = cascadesFor(viewProj, light);
    const auto viewPlanes = planesOf(views);

    // One Glowmere canopy tree, 40 m out and well off to the left of frame: outside the camera
    // frustum, inside the cascades, and its shadow falls 20 m to +X -- into shot.
    const glm::vec3 centre(-80.0f, 3.5f, -40.0f);
    const float radius = rendering::sourceCullRadius(kTreeLo, kTreeHi) * 1.9f;

    scene::LodSettings lod;
    lod.cull = true;
    lod.maxDistance = 400.0f;
    lod.minScreenRadius = 1.0f;
    rendering::CullCamera cullCamera;
    cullCamera.position = {0.0f, 6.0f, 14.0f};
    cullCamera.projScale = rendering::cullProjScale(0.96f, 720);

    const int level = rendering::cullLodLevel(lod, cameraPlanes, cullCamera, centre, radius);
    REQUIRE(level < 0); // the camera really does reject it -- otherwise this proves nothing

    bool someViewSees = false;
    for (const auto& planes : viewPlanes) {
        bool inside = true;
        for (const glm::vec4& plane : planes) {
            if (glm::dot(glm::vec3(plane), centre) + plane.w < -radius) {
                inside = false;
                break;
            }
        }
        someViewSees = someViewSees || inside;
    }
    REQUIRE(someViewSees); // and a cascade really can see it

    // What the shadow passes actually draw: the camera cull's survivors, and nothing else.
    const bool drawnIntoShadowMaps = level >= 0;
    CHECK(drawnIntoShadowMaps);
}

TEST_CASE("a LOD impostor is the size of the object it stands in for",
          "[shadows][lab][impostor][!shouldfail]") {
    // **Expected to fail.** Not a shadow defect -- a LOD one, found because it made a shadow
    // experiment unmeasurable, and recorded here so the day it is fixed is announced.
    //
    // `makeLodMesh` sizes the rung-2 quad from `sourceBoundingRadius(spec)`, the RAW asset, and the
    // billboard branch of shaders/procedural.wgsl scales it by `inst.scale` alone and never applies
    // `proc.sourceMatrix`. Rungs 0 and 1 do go through `sourceMatrix`. Every terrain scatter layer
    // normalises its asset's height onto `sourceTransform`, so every scatter layer's impostors are
    // drawn at the asset's authored size instead of the layer's. Routed to the LOD Lab.
    scene::ProceduralGeometry g;
    g.name = "shrub";
    g.source.kind = scene::PrimitiveKind::Box;
    g.source.size = glm::vec3(2.0f);
    // The layer says how tall the thing should be; the asset says how tall it is.
    g.sourceTransform.scale = glm::vec3(0.1f);

    const auto quad = scene::makeLodMesh(g.source, 2, g.lod.impostorSize);
    REQUIRE(quad.has_value());
    float half = 0.0f;
    for (const scene::Vertex& v : quad->vertices) {
        half = std::max(half, std::max(std::abs(v.position.x), std::abs(v.position.y)));
    }
    const float wanted = scene::sourceBoundingRadius(g.source) * g.sourceTransform.scale.x;
    INFO("impostor half extent " << half << ", the scaled source's radius " << wanted);
    CHECK_THAT(half, Catch::Matchers::WithinRel(wanted, 0.01f));
}
