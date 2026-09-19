// Phase C's two CPU selectors: how much a drawable is worth this frame, and what geometry it is
// therefore drawn as (ADR-122, ADR-123, ADR-124, ADR-125).
//
// Everything here runs without a device, which is the point of the split: the flicker risk in a
// representation system lives in the hysteresis, and a mechanism that can only be checked by
// looking at a moving picture is one nobody checks. The oscillation case is stated twice over --
// once proving that without a dead zone the choice *does* flip every frame, and once proving that
// with one it does not -- because a stability test that would pass with the mechanism removed is
// not a test of the mechanism.

#include "rendering/importance.hpp"
#include "rendering/representation.hpp"
#include "scene/mesh_metrics.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <vector>

using namespace avgen;
using Catch::Approx;

namespace {

constexpr std::uint32_t kWidth = 1280;
constexpr std::uint32_t kHeight = 800;

scene::Camera cameraAtOrigin() {
    scene::Camera c;
    c.position = glm::vec3(0.0f, 0.0f, 0.0f);
    c.target = glm::vec3(0.0f, 0.0f, -1.0f);
    c.fovYRadians = 0.87f;
    c.lens.useExplicitFov = true;
    return c;
}

// A drawable `distance` in front of the camera, `radius` across, with a stated triangle budget and
// surface area. Everything the selector reads is here and nothing else is.
rendering::ImportanceInput sphereAt(float distance, float radius, float area, std::uint32_t tris) {
    rendering::ImportanceInput in;
    in.center = glm::vec3(0.0f, 0.0f, -distance);
    in.radius = radius;
    in.surfaceArea = area;
    in.triangles = tris;
    return in;
}

// A record with a chosen projected radius and nothing else that matters, for the band tests. Built
// by hand rather than by projection so the test states the input to the decision directly.
rendering::ImportanceRecord recordWithRadius(float projectedRadius, std::uint32_t index = 0) {
    rendering::ImportanceRecord r;
    r.index = index;
    r.projectedRadius = projectedRadius;
    r.distance = 10.0f;
    r.pixelsPerUnit = 100.0f;
    r.triangles = 1;
    r.projectedArea = projectedRadius * projectedRadius;
    r.pixelsPerTriangle = r.projectedArea;
    return r;
}

} // namespace

// ---- mesh metrics ------------------------------------------------------------------------------

TEST_CASE("a mesh's triangle count and surface area are measured, not estimated", "[repr][metrics]") {
    // A unit square in the xy plane as two triangles: area 1, two triangles, bounding radius
    // sqrt(2)/2 about the centre.
    scene::MeshData mesh;
    mesh.name = "quad";
    mesh.vertices = {
        {{-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
        {{0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
        {{0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
        {{-0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
    };
    mesh.indices = {0, 1, 2, 0, 2, 3};

    const scene::MeshMetrics m = scene::meshMetrics(mesh);
    CHECK(m.triangles == 2);
    CHECK(m.surfaceArea == Approx(1.0f));
    CHECK(m.meanTriangleArea() == Approx(0.5f));
    CHECK(m.boundsRadius == Approx(std::sqrt(0.5f)));
    CHECK(m.boundsCenter.x == Approx(0.0f));
}

TEST_CASE("a degenerate triangle is still a triangle", "[repr][metrics]") {
    // It is submitted, binned and killed, and a metric that dropped it would flatter every estimate
    // built on it -- the count is what the GPU is asked to rasterise.
    scene::MeshData mesh;
    mesh.vertices = {
        {{0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
        {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
        {{2.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
    };
    mesh.indices = {0, 1, 2};
    const scene::MeshMetrics m = scene::meshMetrics(mesh);
    CHECK(m.triangles == 1);
    CHECK(m.surfaceArea == Approx(0.0f).margin(1e-6));
}

TEST_CASE("area scales by the square of a uniform scale", "[repr][metrics]") {
    CHECK(scene::MeshMetrics::areaScale(glm::vec3(1.0f)) == Approx(1.0f));
    CHECK(scene::MeshMetrics::areaScale(glm::vec3(3.0f)) == Approx(9.0f));
    CHECK(scene::MeshMetrics::areaScale(glm::vec3(-2.0f)) == Approx(4.0f)); // a mirrored instance
}

TEST_CASE("the metrics cache rebuilds on a version move and not otherwise", "[repr][metrics]") {
    scene::Scene scene;
    scene::MeshData mesh;
    mesh.vertices = {
        {{0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
        {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
        {{0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
    };
    mesh.indices = {0, 1, 2};
    const scene::MeshId id = scene.addMesh(mesh);

    scene::MeshMetricsCache cache;
    CHECK(cache.metrics(scene, id).triangles == 1);
    const std::uint64_t after = cache.rebuilds();
    CHECK(after == 1);
    CHECK(cache.metrics(scene, id).triangles == 1);
    CHECK(cache.rebuilds() == after); // asked twice, answered once

    ++scene.meshVersion;
    CHECK(cache.metrics(scene, id).triangles == 1);
    CHECK(cache.rebuilds() == after + 1);

    // A different scene at the same version is a different scene. This is the trap Scene::identity
    // was minted to close and the cache has to close it too: every fresh Scene starts its mesh
    // version at the same value.
    scene::Scene other;
    other.addMesh(mesh);
    CHECK_FALSE(cache.matches(other));
}

TEST_CASE("an unknown mesh yields an invalid metric rather than a plausible one", "[repr][metrics]") {
    scene::Scene scene;
    scene::MeshMetricsCache cache;
    CHECK_FALSE(cache.metrics(scene, scene::kInvalidMesh).valid());
    CHECK_FALSE(cache.metrics(scene, 7).valid());
}

// ---- importance --------------------------------------------------------------------------------

TEST_CASE("projected radius is the same arithmetic the GPU ladder uses", "[repr][importance]") {
    // shaders/cull.wgsl: screenRadius = radius / distance * (height / (2 tan(fovY/2))). An entity
    // and a scattered copy of the same asset must not disagree about how big they are.
    const scene::Camera camera = cameraAtOrigin();
    const auto view = rendering::ViewContext::fromCamera(camera, kWidth, kHeight);
    const float expectedPpu =
        static_cast<float>(kHeight) / (2.0f * std::tan(camera.effectiveFovY() * 0.5f));
    CHECK(view.pixelsPerUnit == Approx(expectedPpu));

    const auto r = rendering::ImportanceEvaluator::evaluate(view, sphereAt(10.0f, 1.0f, 0.0f, 0));
    CHECK(r.distance == Approx(10.0f));
    CHECK(r.projectedRadius == Approx(expectedPpu / 10.0f));
    CHECK(r.inFront);
}

TEST_CASE("pixels per triangle falls with the square of distance", "[repr][importance]") {
    const auto view = rendering::ViewContext::fromCamera(cameraAtOrigin(), kWidth, kHeight);
    const auto near = rendering::ImportanceEvaluator::evaluate(view, sphereAt(10.0f, 1.0f, 12.0f, 1000));
    const auto far = rendering::ImportanceEvaluator::evaluate(view, sphereAt(20.0f, 1.0f, 12.0f, 1000));
    CHECK(far.pixelsPerTriangle == Approx(near.pixelsPerTriangle * 0.25f).epsilon(1e-4));
    CHECK(far.projectedRadius == Approx(near.projectedRadius * 0.5f).epsilon(1e-4));
}

TEST_CASE("the quad threshold is where §4.5 put the knee", "[repr][importance]") {
    // The sweep's knee sits between 3.95 and 7.8 px/triangle; a 2x2 quad is 4 px. `quadOverdrawn`
    // claims "certainly past the knee", so it must be false at the upper end of that bracket.
    rendering::ImportanceRecord r;
    r.triangles = 100;
    r.pixelsPerTriangle = 3.9f;
    CHECK(r.quadOverdrawn());
    r.pixelsPerTriangle = 7.8f;
    CHECK_FALSE(r.quadOverdrawn());
    r.triangles = 0;
    r.pixelsPerTriangle = 0.1f;
    CHECK_FALSE(r.quadOverdrawn()); // nothing drawn is not overdrawn
}

TEST_CASE("a drawable with no area falls back to the bounding disc, which is never the smaller claim",
          "[repr][importance]") {
    // The fallback has to bias towards keeping geometry: an estimate that under-states px/triangle
    // accepts a coarser rung than the object deserves, and does it silently.
    const auto view = rendering::ViewContext::fromCamera(cameraAtOrigin(), kWidth, kHeight);

    // A sphere is the case where the two agree exactly: Cauchy's quarter of 4(pi)r^2 IS the disc.
    const float sphereArea = 4.0f * 3.14159265f;
    const auto sphereKnown = rendering::ImportanceEvaluator::evaluate(view, sphereAt(10.0f, 1.0f, sphereArea, 100));
    const auto sphereUnknown = rendering::ImportanceEvaluator::evaluate(view, sphereAt(10.0f, 1.0f, 0.0f, 100));
    CHECK(sphereUnknown.pixelsPerTriangle == Approx(sphereKnown.pixelsPerTriangle).epsilon(1e-4));

    // Anything that does not fill its bounding sphere -- a leaf card, a fern -- gets a larger
    // estimate from the fallback, which is the direction that keeps detail.
    const auto flatKnown = rendering::ImportanceEvaluator::evaluate(view, sphereAt(10.0f, 1.0f, 0.4f, 100));
    CHECK(sphereUnknown.pixelsPerTriangle > flatKnown.pixelsPerTriangle);
}

TEST_CASE("screen velocity is measured in pixels, and is zero without a previous position",
          "[repr][importance]") {
    const auto view = rendering::ViewContext::fromCamera(cameraAtOrigin(), kWidth, kHeight);
    auto in = sphereAt(10.0f, 1.0f, 1.0f, 10);
    CHECK(rendering::ImportanceEvaluator::evaluate(view, in).screenVelocity == Approx(0.0f));

    in.hasPrevious = true;
    in.previousCenter = in.center + glm::vec3(0.1f, 0.0f, 0.0f);
    const auto moved = rendering::ImportanceEvaluator::evaluate(view, in);
    CHECK(moved.screenVelocity > 0.0f);
    // 0.1 world units at 10 units of distance, projected.
    CHECK(moved.screenVelocity == Approx(0.1f * view.pixelsPerUnit / 10.0f).epsilon(1e-3));
}

TEST_CASE("a drawable behind the camera says so rather than reporting a visibility answer",
          "[repr][importance]") {
    const auto view = rendering::ViewContext::fromCamera(cameraAtOrigin(), kWidth, kHeight);
    auto in = sphereAt(10.0f, 1.0f, 1.0f, 10);
    in.center = glm::vec3(0.0f, 0.0f, 10.0f); // behind
    const auto r = rendering::ImportanceEvaluator::evaluate(view, in);
    CHECK_FALSE(r.inFront);
    CHECK(r.distance == Approx(10.0f)); // still measured: culling is somebody else's decision
}

TEST_CASE("a zero viewport measures nothing rather than dividing by it", "[repr][importance]") {
    const auto view = rendering::ViewContext::fromCamera(cameraAtOrigin(), 0, 0);
    const auto r = rendering::ImportanceEvaluator::evaluate(view, sphereAt(10.0f, 1.0f, 1.0f, 10));
    CHECK(view.pixelsPerUnit == Approx(0.0f));
    CHECK(r.projectedRadius == Approx(0.0f));
    CHECK(std::isfinite(r.pixelsPerTriangle));
}

// ---- representation: the rollback and the offline promise ----------------------------------------

TEST_CASE("a disabled policy reproduces today's renderer exactly", "[repr][selector]") {
    // §6's stated rollback: the selector returns "full mesh" for everything. The tests are asked to
    // assert it, so they do.
    rendering::RepresentationPolicy policy;
    policy.enabled = false;
    const std::array<rendering::LodRung, 4> rungs{{{10.0f, 4000}, {10.0f, 1000}, {10.0f, 200}, {10.0f, 40}}};
    rendering::RepresentationSelector selector;
    for (float radius : {0.1f, 1.0f, 5.0f, 30.0f, 400.0f}) {
        const auto choice = selector.select(recordWithRadius(radius), rungs, policy);
        CHECK(choice.kind == rendering::Representation::FullMesh);
        CHECK(choice.lodLevel == 0);
    }
}

TEST_CASE("offline forces the top representation for every object", "[repr][selector][offline]") {
    // §5.9: a render is a deliverable and must not be a silently lower-fidelity version of the
    // preview it was approved from.
    const auto policy = rendering::RepresentationPolicy::forTier(rendering::QualityTier::Offline);
    CHECK(policy.forceTopRepresentation);
    CHECK(policy.hysteresis == 0.0f);
    const std::array<rendering::LodRung, 4> rungs{{{10.0f, 4000}, {10.0f, 1000}, {10.0f, 200}, {10.0f, 40}}};
    rendering::RepresentationSelector selector;
    for (float radius : {0.01f, 0.5f, 3.0f, 20.0f, 900.0f}) {
        const auto choice = selector.select(recordWithRadius(radius), rungs, policy);
        CHECK(choice.kind == rendering::Representation::FullMesh);
        CHECK(choice.lodLevel == 0);
    }
}

TEST_CASE("no tier turns hysteresis on by itself", "[repr][selector][offline]") {
    // ADR-082's rule, inherited: reading the previous frame makes the image depend on how the
    // camera arrived, so it is offered rather than assumed.
    for (auto tier : {rendering::QualityTier::Preview, rendering::QualityTier::Realtime,
                      rendering::QualityTier::High, rendering::QualityTier::Offline}) {
        CHECK(rendering::RepresentationPolicy::forTier(tier).hysteresis == 0.0f);
    }
}

// ---- representation: the kind bands --------------------------------------------------------------

TEST_CASE("projected radius decides the kind of representation", "[repr][selector]") {
    rendering::RepresentationPolicy policy; // realtime defaults: 40 / 8 / 2 px
    const std::array<rendering::LodRung, 1> one{{{10.0f, 100}}};
    rendering::RepresentationSelector selector;

    auto kindAt = [&](float radius) {
        selector.reset();
        return selector.select(recordWithRadius(radius), one, policy).kind;
    };
    CHECK(kindAt(1.0f) == rendering::Representation::Culled);
    CHECK(kindAt(5.0f) == rendering::Representation::Impostor);
    CHECK(kindAt(20.0f) == rendering::Representation::HlodProxy);
    CHECK(kindAt(200.0f) == rendering::Representation::FullMesh);
}

TEST_CASE("a hero is never demoted past its floor", "[repr][selector][hero]") {
    rendering::RepresentationPolicy policy;
    const std::array<rendering::LodRung, 1> one{{{10.0f, 100}}};
    rendering::RepresentationSelector selector;

    auto record = recordWithRadius(0.5f); // well inside the cull band
    CHECK(selector.select(record, one, policy).kind == rendering::Representation::Culled);

    selector.reset();
    record.hero = true;
    const auto choice = selector.select(record, one, policy);
    // The floor is `MeshLod`: never *coarser* than a mesh. A one-rung ladder then resolves to LOD0,
    // which is finer still and therefore allowed -- a floor is a bound, not an assignment.
    CHECK(choice.kind == rendering::Representation::FullMesh);
    CHECK(choice.lodLevel == 0);
}

// ---- representation: the rung, chosen by pixels per triangle ---------------------------------------

TEST_CASE("the rung is chosen by pixels per triangle, not by radius", "[repr][selector][calibration]") {
    // Two objects at the same projected radius, one a fern and one a terrain chunk, differ in
    // tessellation by orders of magnitude. A radius ladder gives them the same rung; the measured
    // cost model does not, and §4.5 says the measured one is right.
    rendering::RepresentationPolicy policy;
    rendering::ImportanceRecord r = recordWithRadius(120.0f);
    r.pixelsPerUnit = 80.0f;

    // Ladder: 8,000 / 2,000 / 500 / 100 triangles over 40 world units^2 of surface.
    const std::array<rendering::LodRung, 4> dense{{{40.0f, 8000}, {40.0f, 2000}, {40.0f, 500}, {40.0f, 100}}};
    // A coarse object of the same size: even LOD0 is over the target already.
    const std::array<rendering::LodRung, 4> coarse{{{40.0f, 20}, {40.0f, 10}, {40.0f, 5}, {40.0f, 2}}};

    rendering::RepresentationSelector selector;
    const auto dChoice = selector.decide(r, dense, policy, {});
    const auto cChoice = selector.decide(r, coarse, policy, {});
    CHECK(dChoice.lodLevel > 0);
    CHECK(cChoice.lodLevel == 0);
    CHECK(cChoice.kind == rendering::Representation::FullMesh);
}

TEST_CASE("the chosen rung lands in the measured band and never past its cheap end",
          "[repr][selector][calibration]") {
    // §4.5's caveat, asserted: two screen-filling triangles cost 3.02 ms against 1.57 ms for 2,048,
    // so "fewest triangles" is wrong by measurement. Whatever rung is chosen, its pixels per
    // triangle must not exceed the target -- the selector must not overshoot into that regime.
    rendering::RepresentationPolicy policy;
    const std::array<rendering::LodRung, 5> rungs{
        {{40.0f, 20000}, {40.0f, 5000}, {40.0f, 1000}, {40.0f, 200}, {40.0f, 4}}};

    for (float ppu : {400.0f, 200.0f, 80.0f, 30.0f, 10.0f, 3.0f}) {
        rendering::ImportanceRecord r = recordWithRadius(300.0f);
        r.pixelsPerUnit = ppu;
        const auto choice = rendering::RepresentationSelector::decide(r, rungs, policy, {});
        const auto& rung = rungs[choice.lodLevel];
        const float ppt =
            rendering::ImportanceEvaluator::pixelsPerTriangleFor(r, rung.surfaceArea, rung.triangles);
        const float finest = rendering::ImportanceEvaluator::pixelsPerTriangleFor(
            r, rungs[0].surfaceArea, rungs[0].triangles);
        if (finest <= policy.targetPixelsPerTriangle) {
            CHECK(ppt <= policy.targetPixelsPerTriangle);
        } else {
            // Everything is already coarser than the target: the finest rung is the closest to it,
            // and there is nothing a selector can do about the upper end -- you cannot add
            // triangles that were never authored.
            CHECK(choice.lodLevel == 0);
        }
    }
}

TEST_CASE("a ladder with one rung has one answer", "[repr][selector]") {
    rendering::RepresentationPolicy policy;
    const std::array<rendering::LodRung, 1> one{{{40.0f, 100000}}};
    rendering::ImportanceRecord r = recordWithRadius(300.0f);
    r.pixelsPerUnit = 5.0f;
    const auto choice = rendering::RepresentationSelector::decide(r, one, policy, {});
    CHECK(choice.kind == rendering::Representation::FullMesh);
    CHECK(choice.lodLevel == 0);
}

// ---- representation: hysteresis, both ways ---------------------------------------------------------

namespace {

// A camera breathing across a threshold: the projected radius jitters either side of the proxy
// band's edge. This is the exact shape of the artefact -- a drawable sitting on a threshold with no
// dead zone flips state on every frame.
std::vector<rendering::RepresentationChoice> runJitter(float hysteresis, int frames) {
    rendering::RepresentationPolicy policy;
    policy.hysteresis = hysteresis;
    const std::array<rendering::LodRung, 1> one{{{10.0f, 100}}};
    rendering::RepresentationSelector selector;
    std::vector<rendering::RepresentationChoice> out;
    for (int f = 0; f < frames; ++f) {
        const float radius = policy.proxyRadius + ((f % 2 == 0) ? -0.2f : 0.2f);
        out.push_back(selector.select(recordWithRadius(radius), one, policy));
    }
    return out;
}

int transitions(const std::vector<rendering::RepresentationChoice>& choices) {
    int n = 0;
    for (std::size_t i = 1; i < choices.size(); ++i) {
        if (!choices[i].sameAs(choices[i - 1])) {
            ++n;
        }
    }
    return n;
}

} // namespace

TEST_CASE("without a dead zone a drawable on a threshold oscillates every frame",
          "[repr][selector][hysteresis]") {
    // Stated first and deliberately: a stability test that would pass with the mechanism removed
    // is not a test of the mechanism. This is the artefact.
    const auto choices = runJitter(0.0f, 20);
    CHECK(transitions(choices) == 19);
}

TEST_CASE("a dead zone holds a drawable that is sitting on a threshold", "[repr][selector][hysteresis]") {
    const auto choices = runJitter(0.1f, 20);
    CHECK(transitions(choices) == 0);
    // And it says that it is doing so, rather than leaving it to be inferred.
    bool anyHeld = false;
    for (const auto& c : choices) {
        anyHeld = anyHeld || c.held;
    }
    CHECK(anyHeld);
}

TEST_CASE("a dead zone does not prevent a real crossing, it delays it once",
          "[repr][selector][hysteresis]") {
    // A slow camera move through the band: the choice must change exactly once, and it must end up
    // where a dead-zone-free selector would have put it. Hysteresis that never lets go is a system
    // that never reaches the representation it should.
    rendering::RepresentationPolicy policy;
    policy.hysteresis = 0.1f;
    const std::array<rendering::LodRung, 1> one{{{10.0f, 100}}};
    rendering::RepresentationSelector selector;

    std::vector<rendering::RepresentationChoice> choices;
    for (int f = 0; f < 60; ++f) {
        const float radius = 60.0f - static_cast<float>(f); // 60 px down to 1 px
        choices.push_back(selector.select(recordWithRadius(radius), one, policy));
    }
    CHECK(choices.front().kind == rendering::Representation::FullMesh);
    CHECK(choices.back().kind == rendering::Representation::Culled);
    // Full -> proxy -> impostor -> culled, each exactly once.
    CHECK(transitions(choices) == 3);
}

TEST_CASE("hysteresis is one-sided: it is harder to come back than it was to leave",
          "[repr][selector][hysteresis]") {
    // The same construction cull.wgsl uses. A drawable that dropped to a proxy at 40 px does not
    // return to a mesh the moment it reaches 40 px again.
    rendering::RepresentationPolicy policy;
    policy.hysteresis = 0.2f;
    const std::array<rendering::LodRung, 1> one{{{10.0f, 100}}};

    rendering::RepresentationChoice proxy;
    proxy.kind = rendering::Representation::HlodProxy;
    const auto back = rendering::RepresentationSelector::decide(recordWithRadius(44.0f), one, policy, proxy);
    CHECK(back.kind == rendering::Representation::HlodProxy); // 44 <= 40 * 1.2, still held

    const auto out = rendering::RepresentationSelector::decide(recordWithRadius(49.0f), one, policy, proxy);
    CHECK(out.kind == rendering::Representation::FullMesh); // 49 > 48, it comes back
}

TEST_CASE("the spread offsets a population without moving any one object twice",
          "[repr][selector][hysteresis]") {
    // ADR-082's second stabiliser: a band of the world must not change representation on one frame
    // together. It is a pure function of the index, so it is exactly as deterministic as the hard
    // comparison it replaces -- which is what makes it safe to leave on.
    rendering::RepresentationPolicy policy;
    policy.spread = 0.3f;
    const std::array<rendering::LodRung, 1> one{{{10.0f, 100}}};

    int proxies = 0;
    for (std::uint32_t i = 0; i < 200; ++i) {
        const auto choice =
            rendering::RepresentationSelector::decide(recordWithRadius(40.0f, i), one, policy, {});
        if (choice.kind == rendering::Representation::HlodProxy) {
            ++proxies;
        }
    }
    CHECK(proxies > 0);
    CHECK(proxies < 200); // the population disagrees with itself, which is the whole point

    // Determinism: the same index gives the same answer, always.
    const auto a = rendering::RepresentationSelector::decide(recordWithRadius(40.0f, 17), one, policy, {});
    const auto b = rendering::RepresentationSelector::decide(recordWithRadius(40.0f, 17), one, policy, {});
    CHECK(a.sameAs(b));
}

TEST_CASE("the selector's memory is reset rather than reinterpreted", "[repr][selector]") {
    // A previous choice read against a different object is the derived-copy defect this engine has
    // nine of. `reset` is the invalidation rule, and it has to actually forget.
    rendering::RepresentationPolicy policy;
    policy.hysteresis = 0.2f;
    const std::array<rendering::LodRung, 1> one{{{10.0f, 100}}};
    rendering::RepresentationSelector selector;

    CHECK(selector.select(recordWithRadius(5.0f), one, policy).kind == rendering::Representation::Impostor);
    CHECK(selector.previous(0).kind == rendering::Representation::Impostor);
    CHECK(selector.previous(0).changed == false); // last frame's flags are not this frame's
    selector.reset();
    CHECK(selector.remembered() == 0);
    CHECK(selector.previous(0).kind == rendering::Representation::FullMesh);
}

TEST_CASE("a change is reported on the frame it happens and not afterwards", "[repr][selector]") {
    // The hook a transition mechanism reads. Whether that mechanism is a cross-fade, a dither or
    // something that needs temporal rendering is the open C2 question and is not decided here.
    rendering::RepresentationPolicy policy;
    const std::array<rendering::LodRung, 1> one{{{10.0f, 100}}};
    rendering::RepresentationSelector selector;

    CHECK(selector.select(recordWithRadius(200.0f), one, policy).changed == false); // already full
    CHECK(selector.select(recordWithRadius(20.0f), one, policy).changed == true);
    CHECK(selector.select(recordWithRadius(20.0f), one, policy).changed == false);
}

// ---- the quality floor, and a dolly across it (ADR-351) ------------------------------------------
//
// `maxScreenError` is a second family of thresholds on the rung ladder, and it needs what the kind
// bands already have: an arm that shows it acts, a control that shows the arm is not a tautology,
// and a moving camera that shows the boundaries do not strobe.

namespace {

// The Tree of Life's foliage layer, as the renderer builds its rungs: the triangle counts and the
// measured errors of its largest part, with a surface area that barely moves between rungs because
// shell thinning grows what it keeps. Real numbers, so a threshold tuned against frames is tested
// against the ladder those frames were rendered from.
const std::array<rendering::LodRung, 5> kFoliageLadder{{
    {3000.0f, 250824, 0.00f},
    {3000.0f, 125418, 0.77f},
    {3000.0f, 50166, 2.50f},
    {3000.0f, 17562, 8.80f},
    {3000.0f, 5022, 14.24f},
}};

// One frame of a dolly: the object at `distance` world units, seen through the study's camera
// (1080 tall, 36 degrees, so 1662 pixels per world unit at one unit) with a 62-unit radius.
rendering::ImportanceRecord recordAtDistance(float distance, std::uint32_t index = 0) {
    rendering::ImportanceRecord r;
    r.index = index;
    r.distance = distance;
    r.pixelsPerUnit = 1662.0f / std::max(distance, 1e-3f);
    r.projectedRadius = r.pixelsPerUnit * 62.0f;
    r.projectedArea = 3.14159265f * r.projectedRadius * r.projectedRadius;
    r.triangles = kFoliageLadder.front().triangles;
    r.pixelsPerTriangle = r.projectedArea / static_cast<float>(r.triangles / 2);
    return r;
}

struct DollyResult {
    std::vector<std::uint8_t> levels;
    int transitions = 0;
    int skips = 0; // transitions that moved more than one rung
    // The largest frame-to-frame change in submitted triangles, as a ratio of the smaller. This is
    // the pop itself, in the units the eye reacts to: a rung change from 250,824 triangles to
    // 125,418 is a jump of 2.0, and a change from 250,824 to 17,562 -- a skipped rung -- is 14.3.
    // A transition count cannot tell those apart and both are "one transition".
    float worstJump = 1.0f;
};

// A camera dollying from `far` to `near` over `frames`, breathing by `jitter` of its *distance* as
// it goes. The breath is what makes this a test rather than a monotone ramp: a threshold with no
// dead zone is crossed several times by a camera approaching it unevenly, which is what a hand on a
// gimbal does and what an orbit around an off-centre hero does. A fraction of the distance rather
// than of the step, because that is the shape a real wobble has and because a fraction of the step
// is whatever the frame count happens to make it -- at 240 frames it was 0.25% of the distance,
// far too small to re-cross anything, and the control duly reported the ladder as perfectly stable
// with the mechanism switched off.
DollyResult dolly(float hysteresis, float farDistance, float nearDistance, int frames, float jitter) {
    rendering::RepresentationPolicy policy;
    policy.hysteresis = hysteresis;
    policy.proxyRadius = -1.0f;
    policy.impostorRadius = -1.0f;
    policy.cullRadius = -1.0f;
    rendering::RepresentationSelector selector;
    DollyResult out;
    for (int f = 0; f < frames; ++f) {
        const float t = static_cast<float>(f) / static_cast<float>(frames - 1);
        const float step = (farDistance - nearDistance) / static_cast<float>(frames - 1);
        (void)step;
        const float ramp = farDistance + (nearDistance - farDistance) * t;
        const float distance = ramp * (1.0f + (f % 2 == 0 ? -jitter : jitter));
        const auto choice = selector.select(recordAtDistance(distance), kFoliageLadder, policy);
        const auto level = choice.kind == rendering::Representation::FullMesh ? 0 : choice.lodLevel;
        if (!out.levels.empty() && level != out.levels.back()) {
            ++out.transitions;
            out.skips += std::abs(static_cast<int>(level) - static_cast<int>(out.levels.back())) > 1 ? 1 : 0;
            const float a = static_cast<float>(kFoliageLadder[out.levels.back()].triangles);
            const float b = static_cast<float>(kFoliageLadder[level].triangles);
            out.worstJump = std::max(out.worstJump, std::max(a, b) / std::max(std::min(a, b), 1.0f));
        }
        out.levels.push_back(static_cast<std::uint8_t>(level));
    }
    return out;
}

} // namespace

TEST_CASE("the quality floor refuses a rung whose error is large on screen", "[repr][selector][floor]") {
    rendering::RepresentationPolicy policy;
    policy.proxyRadius = -1.0f;
    policy.impostorRadius = -1.0f;
    policy.cullRadius = -1.0f;
    // At the hero camera every rung is far under the px/triangle target, so the cost rule alone
    // would take the last one. What it takes instead is rung 1, whose 0.77 units of error project
    // to 5.8 px against the floor's 8.
    const auto hero = rendering::RepresentationSelector::decide(recordAtDistance(219.4f), kFoliageLadder,
                                                                policy, {});
    CHECK(hero.lodLevel == 1);

    // The control. Lift the floor and the same record takes the bottom rung, which is the behaviour
    // the frames rejected -- so the arm above is measuring the floor and not the cost rule quietly
    // agreeing with it.
    rendering::RepresentationPolicy unfloored = policy;
    unfloored.maxScreenError = std::numeric_limits<float>::infinity();
    const auto unboundedHero =
        rendering::RepresentationSelector::decide(recordAtDistance(219.4f), kFoliageLadder, unfloored, {});
    CHECK(unboundedHero.lodLevel == 4);

    // And the floor is a *screen-space* rule, not a distance one: the same object further away
    // takes coarser rungs, in the order the errors say.
    CHECK(rendering::RepresentationSelector::decide(recordAtDistance(600.0f), kFoliageLadder, policy, {})
              .lodLevel == 2);
    CHECK(rendering::RepresentationSelector::decide(recordAtDistance(2200.0f), kFoliageLadder, policy, {})
              .lodLevel == 3);
    CHECK(rendering::RepresentationSelector::decide(recordAtDistance(4000.0f), kFoliageLadder, policy, {})
              .lodLevel == 4);
}

TEST_CASE("a rung with no measured error is admitted whatever its size", "[repr][selector][floor]") {
    // Every LodRung built anywhere else in this repository leaves `error` at zero, and the floor
    // must not quietly hold those callers at LOD0. This is the compatibility claim, asserted rather
    // than assumed.
    std::array<rendering::LodRung, 5> unmeasured = kFoliageLadder;
    for (rendering::LodRung& rung : unmeasured) {
        rung.error = 0.0f;
    }
    rendering::RepresentationPolicy policy;
    policy.proxyRadius = -1.0f;
    policy.impostorRadius = -1.0f;
    policy.cullRadius = -1.0f;
    const auto choice =
        rendering::RepresentationSelector::decide(recordAtDistance(219.4f), unmeasured, policy, {});
    CHECK(choice.lodLevel == 4);
}

TEST_CASE("a camera dollying through the ladder does not strobe", "[repr][selector][hysteresis][floor]") {
    // §6 of the asset-LOD brief, and ADR-182's shape: an arm, a control, and a band rather than a
    // floor. The dolly runs from 4 km to 120 m over 240 frames -- every threshold in the ladder --
    // with the camera breathing 60% of a step either side as it comes.
    const DollyResult armed = dolly(0.12f, 4000.0f, 120.0f, 240, 0.03f);
    const DollyResult bare = dolly(0.0f, 4000.0f, 120.0f, 240, 0.03f);

    // The control first, so the arm below cannot be a test that would pass with the mechanism
    // removed. With no dead zone the breathing camera re-crosses every threshold it approaches.
    CHECK(bare.transitions > 8);

    // The arm. Four thresholds in the ladder, so four transitions is the floor of what a dolly
    // through all of them can have; the band allows a couple more rather than pinning it, because
    // which frame a threshold falls on is arithmetic nobody should be able to break by changing a
    // ratio. What it must not do is cross the same threshold twice.
    CHECK(armed.transitions >= 4);
    CHECK(armed.transitions <= 6);
    CHECK(armed.transitions < bare.transitions);

    // No rung is skipped, and no single frame boundary changes the geometry by more than the
    // ladder's own largest step. This is the popping assertion proper: a transition count says how
    // *often* the geometry changed and this says how *far* it changed at once, and a system that
    // pops visibly passes the first and fails the second. The ladder's steps are 2.0x, 2.5x, 2.86x
    // and 3.5x, so 3.6 is one adjacent step plus a margin; the smallest skip available is 5.0x.
    CHECK(armed.skips == 0);
    CHECK(armed.worstJump < 3.6f);
    CHECK(armed.worstJump > 1.0f); // ...and it did move, so the band above is not measuring a flat line

    // The control for that band, and the one that matters most: with the dead zone removed the
    // camera's breath re-crosses thresholds, and the same jump happens over and over. The magnitude
    // of a single jump is a property of the ladder and is the same either way -- what hysteresis
    // buys is that it happens four times instead of a dozen -- so the control is stated as the
    // count, which is the quantity the mechanism actually moves.
    CHECK(bare.transitions >= armed.transitions * 2);

    // Monotone: a camera that only ever approaches must only ever refine. Stated separately from
    // the transition count because a ladder that went 4-3-4-3-2 has the same count as one that went
    // 4-3-2-1-0 and is the artefact.
    for (std::size_t i = 1; i < armed.levels.size(); ++i) {
        INFO("frame " << i << ": " << int(armed.levels[i - 1]) << " -> " << int(armed.levels[i]));
        REQUIRE(armed.levels[i] <= armed.levels[i - 1]);
    }
}

TEST_CASE("a camera dollying away is the same ladder in reverse", "[repr][selector][hysteresis][floor]") {
    // The other direction, because hysteresis is deliberately one-sided and a mechanism that is
    // stable approaching and strobes receding is half a mechanism.
    const DollyResult armed = dolly(0.12f, 120.0f, 4000.0f, 240, 0.03f);
    CHECK(armed.skips == 0);
    for (std::size_t i = 1; i < armed.levels.size(); ++i) {
        INFO("frame " << i << ": " << int(armed.levels[i - 1]) << " -> " << int(armed.levels[i]));
        REQUIRE(armed.levels[i] >= armed.levels[i - 1]);
    }
    CHECK(armed.levels.front() < armed.levels.back()); // it did move; this is not a flat line
}
