// AV Gen scene integration (ADR-344 Phase 5): CPU skinning and procedural scatter.
//
// Phase 5's risk is not that a feature is missing -- it is that a feature appears to work while
// putting geometry in the wrong place. A character rendered in its bind pose looks like a
// character; a scatter rendered at the origin looks like an object. Both need arms that check
// WHERE the geometry ended up, not just that some arrived.

#include "pathtrace/path_tracer.hpp"
#include "pathtrace/snapshot.hpp"
#include "scene/animation.hpp"
#include "scene/mesh_generators.hpp"
#include "scene/procedural.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <limits>

using namespace avgen;
using Catch::Approx;

namespace {

// Axis-aligned bounds of every triangle the snapshot will actually trace.
struct Bounds {
    glm::vec3 lo{std::numeric_limits<float>::max()};
    glm::vec3 hi{std::numeric_limits<float>::lowest()};
    bool empty = true;
};

Bounds snapshotBounds(const pathtrace::Snapshot& s) {
    Bounds b;
    for (const auto& m : s.meshes) {
        for (const auto& p : m.positions) {
            b.lo = glm::min(b.lo, p);
            b.hi = glm::max(b.hi, p);
            b.empty = false;
        }
    }
    return b;
}

// A one-bone rig whose single joint is translated, so "was the palette applied?" has a visible
// answer: the mesh is modelled at the origin and the joint moves it.
scene::Scene skinnedScene(glm::vec3 jointOffset, bool evaluatePalette) {
    scene::Scene s;
    scene::MeshData mesh = scene::makeCube(0.5f);
    mesh.skin.assign(mesh.vertices.size(), scene::SkinInfluence{});
    for (auto& inf : mesh.skin) {
        inf.joints[0] = 0;
        inf.weights = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
    }
    const scene::MeshId id = s.addMesh(std::move(mesh));

    scene::SkinnedRig rig;
    if (evaluatePalette) rig.palette = {glm::translate(glm::mat4(1.0f), jointOffset)};
    s.rigs.push_back(std::move(rig));

    scene::Entity& e = s.addEntity("character", id);
    e.rig = 0;
    return s;
}

} // namespace

TEST_CASE("CPU skinning puts the mesh where the joint palette says", "[unit][pathtrace][skinning]") {
    // The cube is modelled at the origin with half-extent 0.5. A joint translated by +4 in X must
    // move every vertex with it, so the bounds become [3.5, 4.5] in X.
    const pathtrace::Snapshot posed = pathtrace::buildSnapshot(skinnedScene({4.0f, 0.0f, 0.0f}, true));
    REQUIRE(posed.meshes.size() == 1);
    const Bounds b = snapshotBounds(posed);
    REQUIRE_FALSE(b.empty);
    INFO("x " << b.lo.x << ".." << b.hi.x);
    REQUIRE(b.lo.x == Approx(3.5f).margin(1e-4));
    REQUIRE(b.hi.x == Approx(4.5f).margin(1e-4));
    // The other axes are untouched by a pure X translation.
    REQUIRE(b.lo.y == Approx(-0.5f).margin(1e-4));
    REQUIRE(b.hi.z == Approx(0.5f).margin(1e-4));

    // CONTROL: the same scene with the joint at the origin must leave the cube at the origin. If
    // the palette were being ignored, BOTH arms would produce a cube at the origin and the arm
    // above would be the only one that failed -- this pins that it is the palette doing the work.
    const pathtrace::Snapshot atOrigin = pathtrace::buildSnapshot(skinnedScene({0.0f, 0.0f, 0.0f}, true));
    const Bounds o = snapshotBounds(atOrigin);
    REQUIRE(o.lo.x == Approx(-0.5f).margin(1e-4));
    REQUIRE(o.hi.x == Approx(0.5f).margin(1e-4));
}

TEST_CASE("an unposed rig is refused and reported, not drawn in its bind pose",
          "[unit][pathtrace][skinning]") {
    // Spec section 87: detect, report, do not silently alter. A rig frozen by `cullDistance` has an
    // empty palette, and its bind pose is not where the character is.
    const pathtrace::Snapshot snap = pathtrace::buildSnapshot(skinnedScene({4.0f, 0.0f, 0.0f}, false));
    REQUIRE(snap.meshes.empty());

    const auto* cap = [&]() -> const pathtrace::Capability* {
        for (const auto& c : snap.capabilities.entries) {
            if (c.feature == "unposed rig") return &c;
        }
        return nullptr;
    }();
    REQUIRE(cap != nullptr);
    REQUIRE(cap->support == pathtrace::Support::Unsupported);
    REQUIRE(cap->count == 1);
    REQUIRE(cap->detail.find("cullDistance") != std::string::npos);

    // CONTROL: the posed version of the same scene reports nothing of the kind.
    const pathtrace::Snapshot ok = pathtrace::buildSnapshot(skinnedScene({4.0f, 0.0f, 0.0f}, true));
    REQUIRE_FALSE(ok.capabilities.anyUnsupported());
}

TEST_CASE("skinning transforms normals by the inverse transpose, not the matrix",
          "[unit][pathtrace][skinning]") {
    // A non-uniform joint scale is the case where the two differ. Scale x by 4: a face whose normal
    // is +X keeps it, but a face whose normal is +Y must STAY +Y -- under the plain matrix it would
    // be stretched and, after normalising, tilt.
    scene::Scene s = skinnedScene({0.0f, 0.0f, 0.0f}, true);
    s.rigs[0].palette = {glm::scale(glm::mat4(1.0f), glm::vec3(4.0f, 1.0f, 1.0f))};
    const pathtrace::Snapshot snap = pathtrace::buildSnapshot(s);
    REQUIRE(snap.meshes.size() == 1);

    for (const auto& n : snap.meshes[0].normals) {
        REQUIRE(glm::length(n) == Approx(1.0f).margin(1e-4));
        // Every cube normal is axis-aligned and must remain so.
        const float m = std::max({std::abs(n.x), std::abs(n.y), std::abs(n.z)});
        INFO("normal " << n.x << "," << n.y << "," << n.z);
        REQUIRE(m == Approx(1.0f).margin(1e-3));
    }
    // And the geometry really was stretched, so the arm is live.
    const Bounds b = snapshotBounds(snap);
    REQUIRE(b.hi.x == Approx(2.0f).margin(1e-4));
    REQUIRE(b.hi.y == Approx(0.5f).margin(1e-4));
}

TEST_CASE("procedural scatter reaches the tracer at its instance transforms",
          "[unit][pathtrace][procedural]") {
    // A scatter that arrived but ignored its instance matrices would put every copy at the origin:
    // the right triangle count, the wrong picture. So this checks the BOUNDS, which only widen if
    // the instances landed where they were placed.
    scene::Scene s;
    scene::ProceduralGeometry proc;
    proc.source.kind = scene::PrimitiveKind::Box;
    proc.source.size = glm::vec3(0.4f);
    proc.material.baseColor = glm::vec3(0.4f, 0.7f, 0.3f);

    const int n = 12;
    for (int i = 0; i < n; ++i) {
        spatial::InstanceRecord r;
        r.position = glm::vec4(static_cast<float>(i) * 2.0f - 11.0f, 0.0f, 0.0f, 1.0f);
        r.rotation = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        r.scale = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);
        proc.instances.push_back(r);
    }
    s.procedurals.push_back(std::move(proc));

    const pathtrace::Snapshot snap = pathtrace::buildSnapshot(s);
    INFO("meshes " << snap.meshes.size() << " triangles " << snap.triangleCount());
    REQUIRE(snap.meshes.size() == static_cast<std::size_t>(n));
    REQUIRE(snap.triangleCount() == 12u * static_cast<std::size_t>(n)); // a cube is 12 triangles

    const Bounds b = snapshotBounds(snap);
    REQUIRE_FALSE(b.empty);
    // Instances span x = -11 .. +11, each a cube of half-extent 0.2.
    REQUIRE(b.lo.x == Approx(-11.2f).margin(0.05));
    REQUIRE(b.hi.x == Approx(11.2f).margin(0.05));
    // CONTROL: had the transforms been ignored, every copy would sit at the origin and the bounds
    // would be the single cube's.
    REQUIRE(b.hi.x - b.lo.x > 20.0f);

    // And it is reported honestly as a degraded representation, not as full support.
    const auto* cap = [&]() -> const pathtrace::Capability* {
        for (const auto& c : snap.capabilities.entries) {
            if (c.feature == "procedural instance") return &c;
        }
        return nullptr;
    }();
    REQUIRE(cap != nullptr);
    REQUIRE(cap->support == pathtrace::Support::Degraded);
    REQUIRE(cap->count == n);
}

TEST_CASE("a scattered scene renders, and the scatter is visible in the picture",
          "[unit][pathtrace][procedural]") {
    // End to end. Numbers above say the geometry is in the right place; this says it reaches pixels.
    scene::Scene s;
    s.environment.sky.enabled = false;
    s.environment.backgroundColor = glm::vec3(0.0f);
    s.camera.position = glm::vec3(0.0f, 3.0f, 9.0f);
    s.camera.target = glm::vec3(0.0f, 0.5f, 0.0f);
    s.camera.lens.useExplicitFov = true;
    s.camera.fovYRadians = 0.9f;

    scene::PunctualLight key;
    key.type = scene::PunctualLight::Type::Directional;
    key.direction = glm::vec3(-0.3f, -1.0f, -0.4f);
    key.color = glm::vec3(1.0f);
    key.intensity = 3.0f;
    key.temperature = 6500.0f;
    s.addLight(key);

    scene::ProceduralGeometry proc;
    proc.source.kind = scene::PrimitiveKind::Box;
    proc.source.size = glm::vec3(0.8f);
    proc.material.baseColor = glm::vec3(0.2f, 0.8f, 0.3f);
    for (int i = 0; i < 9; ++i) {
        spatial::InstanceRecord r;
        r.position = glm::vec4(static_cast<float>(i) * 1.6f - 6.4f, 0.4f, 0.0f, 1.0f);
        r.rotation = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        r.scale = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);
        proc.instances.push_back(r);
    }
    s.procedurals.push_back(std::move(proc));

    pathtrace::TraceSettings t;
    t.width = 160;
    t.height = 100;
    t.samplesPerPixel = 24;
    t.maxDepth = 1;
    pathtrace::PathTracer tr;
    pathtrace::Framebuffer fb;
    REQUIRE(tr.render(pathtrace::buildSnapshot(s), t, fb).has_value());
    REQUIRE_FALSE(fb.isBlack());

    // The row through the cubes must alternate lit-green and black background: count how many
    // separate runs of lit pixels there are. Nine cubes, seen head on, must not read as one blob
    // and must not read as none.
    // Scan every row and take the one the cubes actually occupy, rather than assuming a row and
    // measuring the gap between two of them. Which scanline a 0.8 m cube lands on is a property of
    // the camera, not of the thing under test.
    int runs = 0;
    int litPixels = 0;
    std::uint32_t bestRow = 0;
    for (std::uint32_t y = 0; y < fb.height; ++y) {
        int lit = 0;
        int r = 0;
        bool inRun = false;
        for (std::uint32_t x = 0; x < fb.width; ++x) {
            const bool on = fb.pixel(x, y).y > 0.02f;
            if (on) ++lit;
            if (on && !inRun) ++r;
            inRun = on;
        }
        if (lit > litPixels) {
            litPixels = lit;
            runs = r;
            bestRow = y;
        }
    }
    const std::uint32_t row = bestRow;
    INFO("best row " << row << " runs " << runs << " lit pixels " << litPixels);
    REQUIRE(litPixels > 10);      // live arm: something is there
    REQUIRE(runs >= 5);           // and it is SEVERAL objects, not one smear at the origin
    // Green, because that is the scatter's albedo -- not the background, not white.
    // The brightest lit pixel on that row, so the colour check lands on a cube rather than a gap.
    glm::vec3 mid{0.0f};
    for (std::uint32_t x = 0; x < fb.width; ++x) {
        const glm::vec3 c = fb.pixel(x, row);
        if (c.y > mid.y) mid = c;
    }
    REQUIRE(mid.y > mid.x * 1.5f);
    REQUIRE(mid.y > mid.z * 1.5f);
}
