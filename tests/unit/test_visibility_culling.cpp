// The Visibility / Culling Lab's core regression: the sphere the cull pass tests must contain the
// geometry it is deciding about.
//
// The bug this file was written for is the owner's standing complaint that trees pop out of shots
// in Glowmere. It is not a Glowmere parameter. shaders/cull.wgsl centres each instance's bounding
// sphere on the instance *record's position* -- which for a scatter is the point on the ground the
// thing was planted at, and for a glTF tree is the foot of its trunk -- and the renderer's mesh
// cache was handing it the *half-diagonal of the source AABB* as the radius. Those two numbers
// describe different spheres. The half-diagonal is the radius about the box's centre; about the
// origin it is only right when the geometry is centred there, and a tree standing on its origin is
// the case where it is most wrong. CommonTree_1 -- Glowmere's `canopy` layer, normalised to 14 m --
// was culled with a 7.5 m sphere about its foot when its crown reaches 14.9 m from that foot.
//
// So a tree whose foot passed a metre or two below the bottom of frame was thrown away with half
// its canopy still on screen. ADR-199 had already reached this conclusion for
// `scene::ProceduralGeometry`'s own bounds and wrote it down -- "the conservative version stays for
// culling" -- and the renderer's cache did not follow.
//
// Every arm here has a control (ADR-182). A cull test that keeps everything passes the first half
// of this file and detects nothing, so each "it must survive" case is paired with a case that must
// still be culled, and the reproduction asserts against the tree's *actual vertices* rather than
// against a second radius: the invariant is "never culled while some of its geometry is inside the
// frustum", which is a claim about the world and not about the rule being reimplemented.
#include "assets/gltf_loader.hpp"
#include "rendering/visibility.hpp"
#include "scene/camera.hpp"
#include "scene/procedural.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

// What the renderer's mesh cache used to hand the cull pass: half the AABB's diagonal.
float halfDiagonalRadius(const glm::vec3& lo, const glm::vec3& hi) {
    return std::max(0.5f * glm::length(hi - lo), 1e-4f);
}

fs::path treeAsset() {
    return fs::path(AVGEN_SOURCE_DIR) / "assets" / "quaternius" / "glTF" / "CommonTree_1.gltf";
}
fs::path rockAsset() {
    return fs::path(AVGEN_SOURCE_DIR) / "assets" / "quaternius" / "glTF" / "Rock_Medium_1.gltf";
}

// Every vertex of every primitive, in the asset's own units. The test asks questions about real
// geometry, so it needs the real vertices and not a bounding box of them.
struct Asset {
    std::vector<glm::vec3> vertices;
    glm::vec3 lo{std::numeric_limits<float>::max()};
    glm::vec3 hi{std::numeric_limits<float>::lowest()};
    // The per-part half-diagonals, the way the renderer takes them: one per primitive, maxed.
    // Deliberately not the half-diagonal of the union -- the renderer never computes that.
    float worstHalfDiagonal = 0.0f;
    bool valid = false;
};

Asset loadAsset(const fs::path& path) {
    Asset out;
    if (!fs::is_regular_file(path)) {
        return out;
    }
    scene::Scene s;
    auto loaded = assets::loadGltf(path, s, {});
    if (!loaded || s.meshes.empty()) {
        return out;
    }
    for (const scene::MeshData& mesh : s.meshes) {
        if (mesh.vertices.empty()) {
            continue;
        }
        const auto [plo, phi] = mesh.bounds();
        out.lo = glm::min(out.lo, plo);
        out.hi = glm::max(out.hi, phi);
        out.worstHalfDiagonal = std::max(out.worstHalfDiagonal, halfDiagonalRadius(plo, phi));
        for (const scene::Vertex& v : mesh.vertices) {
            out.vertices.push_back(v.position);
        }
        out.valid = true;
    }
    return out;
}

// Is this world-space point inside the six planes? The same inward-normal convention the cull uses.
bool pointInside(const rendering::FrustumPlanes& planes, const glm::vec3& p) {
    for (const glm::vec4& plane : planes) {
        if (glm::dot(glm::vec3(plane), p) + plane.w < 0.0f) {
            return false;
        }
    }
    return true;
}

// Glowmere's `canopy` layer: CommonTree_1 normalised to 14 m tall (examples/world/
// glowmere-valley-2-multicam.scene.json, nodes[0].scatter[0]).
constexpr float kCanopyHeight = 14.0f;
constexpr float kCanopyViewDistance = 520.0f;
constexpr float kCanopyMinScreenRadius = 1.0f;

scene::LodSettings canopyLod() {
    scene::LodSettings lod;
    lod.cull = true;
    lod.maxDistance = kCanopyViewDistance;
    lod.minScreenRadius = kCanopyMinScreenRadius;
    lod.lodCount = scene::kMaxLodLevels;
    lod.lodByScreenSize = true;
    lod.lodDistances[0] = 28.0f;
    lod.lodDistances[1] = 11.0f;
    lod.lodDistances[2] = 4.0f;
    return lod;
}

} // namespace

TEST_CASE("A cull sphere about the instance origin contains the source geometry", "[culling][visibility]") {
    const Asset tree = loadAsset(treeAsset());
    if (!tree.valid) {
        SKIP("assets/quaternius is not present in this checkout");
    }
    const float fixed = rendering::sourceCullRadius(tree.lo, tree.hi);

    SECTION("the rule the cull pass now uses contains every vertex") {
        float furthest = 0.0f;
        for (const glm::vec3& v : tree.vertices) {
            furthest = std::max(furthest, glm::length(v));
        }
        CHECK(furthest <= fixed);
        // Tight about the bound it is derived from, so it is not a margin that could be quietly
        // enlarged to make a test pass (§28): the radius *is* the distance to the furthest corner
        // of the AABB. It exceeds the furthest actual vertex only because no vertex sits exactly on
        // that corner, which is the ordinary slack of any box bound and not padding added here.
        glm::vec3 worst(0.0f);
        for (int corner = 0; corner < 8; ++corner) {
            const glm::vec3 c((corner & 1) ? tree.hi.x : tree.lo.x, (corner & 2) ? tree.hi.y : tree.lo.y,
                              (corner & 4) ? tree.hi.z : tree.lo.z);
            if (glm::length(c) > glm::length(worst)) {
                worst = c;
            }
        }
        CHECK(fixed == Catch::Approx(glm::length(worst)).margin(1e-3));
    }

    SECTION("the rule it used before did not -- the control that makes this file able to fail") {
        float furthest = 0.0f;
        for (const glm::vec3& v : tree.vertices) {
            furthest = std::max(furthest, glm::length(v));
        }
        // If this ever stops holding, the asset changed and the reproduction below is measuring
        // something else. It is asserted rather than assumed for exactly that reason.
        CHECK(tree.worstHalfDiagonal < furthest);
        INFO("half-diagonal " << tree.worstHalfDiagonal << " vs furthest vertex " << furthest);
        CHECK(tree.worstHalfDiagonal < fixed);
    }
}

TEST_CASE("Geometry centred on its own origin is unaffected by the corrected radius",
          "[culling][visibility]") {
    // The control for the fix itself: it must not inflate every bound in the scene. For a source
    // whose box straddles its origin the two rules are the same number, so nothing centred moves --
    // which is what makes this a targeted correction rather than a global margin (§28).
    SECTION("a symmetric box: the old rule and the new one agree exactly") {
        for (const glm::vec3 half : {glm::vec3(1.0f), glm::vec3(0.4f, 9.0f, 0.4f), glm::vec3(3.0f, 0.1f, 7.0f)}) {
            CHECK(rendering::sourceCullRadius(-half, half) == Catch::Approx(halfDiagonalRadius(-half, half)));
        }
    }
    SECTION("an asset that stands on its origin grows, and by how much it is off centre") {
        // Rock_Medium_1 is not centred either -- it sits on the ground like everything scattered
        // onto terrain does -- so it grows too. Recorded rather than asserted away: the size of the
        // correction is a property of the asset, and the lab's job is to say what it is.
        const Asset rock = loadAsset(rockAsset());
        if (!rock.valid) {
            SKIP("assets/quaternius is not present in this checkout");
        }
        const float fixed = rendering::sourceCullRadius(rock.lo, rock.hi);
        const float old = halfDiagonalRadius(rock.lo, rock.hi);
        INFO("boulder cull sphere " << old << " m -> " << fixed << " m (x" << fixed / old << ")");
        CHECK(fixed > old);
        for (const glm::vec3& v : rock.vertices) {
            CHECK(glm::length(v) <= fixed);
        }
    }
}

TEST_CASE("A Glowmere canopy tree is not culled while its crown is in frame", "[culling][visibility]") {
    const Asset tree = loadAsset(treeAsset());
    if (!tree.valid) {
        SKIP("assets/quaternius is not present in this checkout");
    }
    // The scatter's own normalisation (composition.cpp: sourceTransform.scale = height / authored).
    const float authored = tree.hi.y - tree.lo.y;
    REQUIRE(authored > 1e-4f);
    const float sourceScale = kCanopyHeight / authored;
    const float fixedRadius = rendering::sourceCullRadius(tree.lo, tree.hi) * sourceScale;
    const float oldRadius = tree.worstHalfDiagonal * sourceScale;
    REQUIRE(oldRadius < fixedRadius);
    // The reproduction below is calibrated from these two radii, so it would still describe *some*
    // pop if the shipped rule regressed -- it is relative by construction. This pins it to the
    // absolute claim as well: the radius the renderer ships must contain the tree it is culling.
    for (const glm::vec3& v : tree.vertices) {
        REQUIRE(glm::length(v * sourceScale) <= fixedRadius);
    }

    scene::Camera camera;
    camera.fovYRadians = glm::radians(50.0f);
    camera.lens.useExplicitFov = true;
    camera.nearPlane = 0.1f;
    camera.farPlane = 2000.0f;
    constexpr float kAspect = 16.0f / 9.0f;
    constexpr std::uint32_t kViewportHeight = 1080;

    // The tree stands at the world origin, its foot on the ground. The camera looks level from
    // `range` metres away and `height` metres up -- an ordinary valley framing, a camera on a
    // hillside looking across at what is growing below it.
    const float range = 40.0f;
    const float halfFovTan = std::tan(camera.fovYRadians * 0.5f);
    // Perpendicular distance from the bottom plane down to the tree's foot, as a function of the
    // camera height: the plane through the eye descending at the half-angle.
    const auto footOutsideBy = [&](float height) {
        return (height - range * halfFovTan) * std::cos(camera.fovYRadians * 0.5f);
    };
    // Put the foot outside by a distance the old sphere could not reach but the corrected one can.
    // Solved from the asset rather than written down, so the case follows the geometry.
    const float wantOutside = 0.5f * (oldRadius + fixedRadius);
    const float height = range * halfFovTan + wantOutside / std::cos(camera.fovYRadians * 0.5f);
    camera.position = glm::vec3(0.0f, height, range);
    camera.target = glm::vec3(0.0f, height, 0.0f);
    const rendering::FrustumPlanes planes =
        rendering::frustumPlanes(camera.projection(kAspect) * camera.view());
    rendering::CullCamera cull;
    cull.position = camera.position;
    cull.projScale = rendering::cullProjScale(camera.effectiveFovY(), kViewportHeight);

    INFO("camera height " << height << " m, foot outside the bottom plane by " << footOutsideBy(height)
                          << " m; old sphere " << oldRadius << " m, corrected " << fixedRadius << " m");

    SECTION("the tree really is on screen: some of its geometry is inside the frustum") {
        // The claim the whole case rests on, tested against the asset's vertices and nothing else.
        std::size_t inside = 0;
        for (const glm::vec3& v : tree.vertices) {
            if (pointInside(planes, v * sourceScale)) {
                ++inside;
            }
        }
        INFO(inside << " of " << tree.vertices.size() << " vertices inside the frustum");
        CHECK(inside > 0);
    }

    SECTION("the corrected sphere keeps it") {
        const rendering::InstanceVisibility v =
            rendering::instanceVisibility(canopyLod(), planes, cull, glm::vec3(0.0f), fixedRadius);
        CHECK(v.reason == rendering::VisibilityReason::Visible);
        CHECK(v.lodLevel >= 0);
    }

    SECTION("the sphere the renderer used before threw it away -- the failure, reproduced") {
        // This is the bug. Before the fix the renderer handed the cull pass exactly `oldRadius`,
        // and the pass rejected an instance whose crown was in shot. If this section ever starts
        // reporting VISIBLE, the reproduction has stopped reproducing anything and the sections
        // above prove nothing.
        const rendering::InstanceVisibility v =
            rendering::instanceVisibility(canopyLod(), planes, cull, glm::vec3(0.0f), oldRadius);
        CHECK(v.reason == rendering::VisibilityReason::FrustumCulled);
        CHECK(v.lodLevel == -1);
    }

    SECTION("a tree genuinely out of frame is still culled") {
        // The control for the fix. Widening a cull sphere until nothing is ever culled would pass
        // every section above; this one fails the moment the frustum stage stops working.
        const float clearOutside = 2.5f * fixedRadius;
        const float highCamera = range * halfFovTan + clearOutside / std::cos(camera.fovYRadians * 0.5f);
        scene::Camera far = camera;
        far.position = glm::vec3(0.0f, highCamera, range);
        far.target = glm::vec3(0.0f, highCamera, 0.0f);
        const rendering::FrustumPlanes farPlanes =
            rendering::frustumPlanes(far.projection(kAspect) * far.view());
        rendering::CullCamera farCull;
        farCull.position = far.position;
        farCull.projScale = cull.projScale;
        for (const glm::vec3& v : tree.vertices) {
            REQUIRE_FALSE(pointInside(farPlanes, v * sourceScale));
        }
        const rendering::InstanceVisibility v =
            rendering::instanceVisibility(canopyLod(), farPlanes, farCull, glm::vec3(0.0f), fixedRadius);
        CHECK(v.reason == rendering::VisibilityReason::FrustumCulled);
    }
}

// ---- the reason codes (§9) ----------------------------------------------------------------------
//
// The test that matters for a diagnostic is not that it produces a string. It is that the string
// can be *wrong*, and is checked against the decision rather than against itself. So each arm below
// drives a case into one stage and asserts both the code and the verdict `cullLodLevel` reaches,
// and the last two arms are the controls: a reason that never says VISIBLE and a reason that never
// says anything else would both pass a one-sided test.

TEST_CASE("Every reason code corresponds to a decision the cull pass actually makes",
          "[culling][visibility]") {
    // A camera at the origin looking down -Z, so the cases below can be written in metres.
    scene::Camera camera;
    camera.fovYRadians = glm::radians(50.0f);
    camera.lens.useExplicitFov = true;
    camera.nearPlane = 0.1f;
    camera.farPlane = 4000.0f;
    camera.position = glm::vec3(0.0f);
    camera.target = glm::vec3(0.0f, 0.0f, -1.0f);
    const rendering::FrustumPlanes planes =
        rendering::frustumPlanes(camera.projection(16.0f / 9.0f) * camera.view());
    rendering::CullCamera cull;
    cull.position = camera.position;
    cull.projScale = rendering::cullProjScale(camera.effectiveFovY(), 1080);

    const scene::LodSettings lod = canopyLod();

    SECTION("VISIBLE: a tree-sized sphere 100 m down the view axis") {
        const auto v = rendering::instanceVisibility(lod, planes, cull, glm::vec3(0.0f, 0.0f, -100.0f), 7.5f);
        CHECK(v.reason == rendering::VisibilityReason::Visible);
        CHECK(v.lodLevel >= 0);
        CHECK(v.distance == Catch::Approx(100.0f));
        CHECK(v.screenRadius > 0.0f);
        // Every plane margin is positive for something wholly inside.
        for (const float m : v.planeMargins) {
            CHECK(m > 0.0f);
        }
    }

    SECTION("FRUSTUM_CULLED: the same sphere behind the camera") {
        const auto v = rendering::instanceVisibility(lod, planes, cull, glm::vec3(0.0f, 0.0f, 100.0f), 7.5f);
        CHECK(v.reason == rendering::VisibilityReason::FrustumCulled);
        CHECK(v.lodLevel == -1);
        // The named plane is the one that rejected it, and the margin says by how much: 100 m
        // behind the eye, less the sphere's own radius.
        // 100 m behind the eye and the near plane itself 0.1 m in front of it, less the sphere's
        // own radius: the number says how far outside the sphere was, and which plane said so.
        CHECK(*std::min_element(v.planeMargins.begin(), v.planeMargins.end()) ==
              Catch::Approx(-100.0f - camera.nearPlane + 7.5f).margin(0.01));
    }

    SECTION("DISTANCE_CULLED: down the view axis, past maxDistance") {
        const float beyond = lod.maxDistance + 50.0f;
        const auto v = rendering::instanceVisibility(lod, planes, cull, glm::vec3(0.0f, 0.0f, -beyond), 7.5f);
        CHECK(v.reason == rendering::VisibilityReason::DistanceCulled);
        CHECK(v.lodLevel == -1);
        // In frame, not off it: this is the distinction the code exists to make.
        for (const float m : v.planeMargins) {
            CHECK(m > 0.0f);
        }
    }

    SECTION("SCREEN_SIZE_CULLED: in frame and inside maxDistance, but too small to draw") {
        // A pebble at 300 m: 0.02 m of radius is well under one pixel at this lens.
        const auto v = rendering::instanceVisibility(lod, planes, cull, glm::vec3(0.0f, 0.0f, -300.0f), 0.02f);
        CHECK(v.reason == rendering::VisibilityReason::ScreenSizeCulled);
        CHECK(v.screenRadius < lod.minScreenRadius);
        CHECK(v.distance < lod.maxDistance);
    }

    SECTION("INVALID_BOUNDS: a sphere with no radius") {
        const auto v = rendering::instanceVisibility(lod, planes, cull, glm::vec3(0.0f, 0.0f, -100.0f), 0.0f);
        CHECK(v.reason == rendering::VisibilityReason::InvalidBounds);
    }

    SECTION("control: the reason never disagrees with the verdict the cull pass reaches") {
        // Sweep a sphere through every stage -- in frame, off the edge, past the bar, too small --
        // and check the one property a diagnostic may never break (§37): it says VISIBLE exactly
        // when the decision it is describing kept the instance.
        std::size_t visible = 0;
        std::size_t culled = 0;
        for (int x = -40; x <= 40; ++x) {
            for (int z = 1; z <= 60; ++z) {
                const glm::vec3 centre(static_cast<float>(x) * 12.0f, 0.0f, -static_cast<float>(z) * 12.0f);
                for (const float radius : {0.05f, 1.0f, 9.0f}) {
                    const auto v = rendering::instanceVisibility(lod, planes, cull, centre, radius);
                    const int level = rendering::cullLodLevel(lod, planes, cull, centre, radius);
                    REQUIRE((v.reason == rendering::VisibilityReason::Visible) == (level >= 0));
                    REQUIRE(v.lodLevel == level);
                    if (level >= 0) {
                        ++visible;
                    } else {
                        ++culled;
                    }
                }
            }
        }
        // The probe reached both answers. A sweep that only ever saw one of them would have
        // asserted nothing (ADR-182).
        CHECK(visible > 0);
        CHECK(culled > 0);
    }

    SECTION("control: every reason code has a name, and no two share one") {
        using R = rendering::VisibilityReason;
        const R all[] = {R::Visible,           R::FrustumCulled,        R::DistanceCulled,
                         R::ScreenSizeCulled,  R::DepthBandThinned,     R::ObjectFullyCulled,
                         R::ObjectNotDrawable, R::ObjectBudgetExceeded, R::ShadowOnlyRejected,
                         R::InvalidBounds};
        std::vector<std::string_view> names;
        for (const R r : all) {
            const std::string_view name = rendering::visibilityReasonName(r);
            CHECK_FALSE(name.empty());
            names.push_back(name);
        }
        std::sort(names.begin(), names.end());
        CHECK(std::adjacent_find(names.begin(), names.end()) == names.end());
    }
}

TEST_CASE("The object-level verdict uses the same vocabulary as the instance one",
          "[culling][visibility]") {
    scene::Camera camera;
    camera.fovYRadians = glm::radians(50.0f);
    camera.lens.useExplicitFov = true;
    camera.position = glm::vec3(0.0f);
    camera.target = glm::vec3(0.0f, 0.0f, -1.0f);
    const rendering::FrustumPlanes planes =
        rendering::frustumPlanes(camera.projection(16.0f / 9.0f) * camera.view());
    rendering::CullCamera cull;
    cull.position = camera.position;
    cull.projScale = rendering::cullProjScale(camera.effectiveFovY(), 1080);

    scene::ProceduralGeometry object;
    object.name = "grove";
    object.meshHash = 1234;
    object.lod = canopyLod();
    object.castsShadow = true;
    // Three trees in a row, 100 m ahead.
    for (int i = -1; i <= 1; ++i) {
        scene::InstanceRecord r;
        r.position = glm::vec4(static_cast<float>(i) * 10.0f, 0.0f, -100.0f, 1.0f);
        r.scale = glm::vec4(1.0f);
        object.instances.push_back(r);
    }
    const rendering::InstanceBounds bounds = rendering::instanceBounds(object.instances);
    const glm::mat4 identity(1.0f);
    constexpr float kRadius = 7.5f;

    SECTION("VISIBLE when the object reaches the cull pass") {
        CHECK(rendering::proceduralVisibility(object, planes, cull, identity, bounds, kRadius, true, false,
                                              true) == rendering::VisibilityReason::Visible);
    }
    SECTION("OBJECT_NOT_DRAWABLE when the object is hidden") {
        object.visible = false;
        CHECK(rendering::proceduralVisibility(object, planes, cull, identity, bounds, kRadius, true, false,
                                              true) == rendering::VisibilityReason::ObjectNotDrawable);
    }
    SECTION("OBJECT_NOT_DRAWABLE when it has no records") {
        object.instances.clear();
        CHECK(rendering::proceduralVisibility(object, planes, cull, identity, bounds, kRadius, true, false,
                                              true) == rendering::VisibilityReason::ObjectNotDrawable);
    }
    SECTION("OBJECT_BUDGET_EXCEEDED when there is no slot left") {
        CHECK(rendering::proceduralVisibility(object, planes, cull, identity, bounds, kRadius, true, false,
                                              false) == rendering::VisibilityReason::ObjectBudgetExceeded);
    }
    SECTION("SHADOW_ONLY_REJECTED in a shadow pass for a non-caster") {
        object.castsShadow = false;
        CHECK(rendering::proceduralVisibility(object, planes, cull, identity, bounds, kRadius, true, true,
                                              true) == rendering::VisibilityReason::ShadowOnlyRejected);
        // The control: the same object in the camera pass is not rejected, so the code names the
        // pass and not the object.
        CHECK(rendering::proceduralVisibility(object, planes, cull, identity, bounds, kRadius, true, false,
                                              true) == rendering::VisibilityReason::Visible);
    }
    SECTION("OBJECT_FULLY_CULLED when every record provably fails") {
        const glm::mat4 behind = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 400.0f));
        CHECK(rendering::proceduralVisibility(object, planes, cull, behind, bounds, kRadius, true, false,
                                              true) == rendering::VisibilityReason::ObjectFullyCulled);
    }
}
