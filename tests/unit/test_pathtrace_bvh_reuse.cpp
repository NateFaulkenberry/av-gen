// BVH reuse across frames (ADR-583).
//
// Reuse is only worth having if it can never be wrong: a stale acceleration structure puts rays on
// geometry where it USED to be, and the picture still looks like a picture. So every arm here moves
// something the way a real sequence does -- an entity transform, an object-space deformation, a
// rig's palette, one scatter instance -- and asserts the ray lands at the NEW place.
//
// Each has a CONTROL that runs the same change through `BvhReuse::TrustStructure`, which is change
// detection switched off, and asserts the ray lands at the OLD place. That is the half that makes
// the first half mean something (ADR-182): a scenario the stale structure would also have passed
// could not have caught anything.
//
// And every arm checks, over a grid of rays, that the reused structure answers exactly what a
// from-scratch build of the same snapshot answers -- the same triangle, the same t to the bit.

#include "pathtrace/embree_scene.hpp"
#include "pathtrace/path_tracer.hpp"
#include "pathtrace/snapshot.hpp"
#include "scene/animation.hpp"
#include "scene/mesh_generators.hpp"
#include "scene/procedural.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_test_macros.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <cstring>

using namespace avgen;
using pathtrace::BvhReuse;
using pathtrace::EmbreeScene;
using pathtrace::Snapshot;

namespace {

// A ray straight down onto (x, z) from above everything in these scenes.
pathtrace::SurfaceHit dropAt(const EmbreeScene& bvh, const Snapshot& snap, float x, float z = 0.0f) {
    return bvh.intersect(snap, glm::vec3(x, 50.0f, z), glm::vec3(0.0f, -1.0f, 0.0f), 0.0f, 1e30f);
}

// Which entity (or procedural) a hit landed on, by name, so an assertion reads as the scene does.
std::string hitName(const Snapshot& snap, const pathtrace::SurfaceHit& h) {
    if (!h.hit) return "(miss)";
    return h.instanced ? snap.instanced[h.meshIndex].name : snap.meshes[h.meshIndex].entityName;
}

// The reused structure must answer exactly what a from-scratch build of the same snapshot answers.
// Compared over a grid of rays that crosses every object in these scenes, field by field, with t
// compared as BITS: "close" would be the loose tolerance that hides a stale BVH.
void requireSameAsFreshBuild(const EmbreeScene& reused, const Snapshot& snap) {
    EmbreeScene fresh;
    REQUIRE(fresh.build(snap, 1).has_value());
    int hits = 0;
    for (int ix = -40; ix <= 40; ++ix) {
        for (int iz = -8; iz <= 8; ++iz) {
            const float x = static_cast<float>(ix) * 0.37f;
            const float z = static_cast<float>(iz) * 0.23f;
            // Slanted as well as vertical, so side faces are exercised too.
            for (const glm::vec3 dir : {glm::vec3(0.0f, -1.0f, 0.0f), glm::normalize(glm::vec3(0.3f, -1.0f, 0.2f))}) {
                const glm::vec3 o(x, 50.0f, z);
                const auto a = reused.intersect(snap, o, dir, 0.0f, 1e30f);
                const auto b = fresh.intersect(snap, o, dir, 0.0f, 1e30f);
                INFO("ray at x " << x << " z " << z);
                REQUIRE(a.hit == b.hit);
                if (!a.hit) continue;
                ++hits;
                REQUIRE(a.instanced == b.instanced);
                REQUIRE(a.meshIndex == b.meshIndex);
                REQUIRE(a.instanceIndex == b.instanceIndex);
                REQUIRE(a.primIndex == b.primIndex);
                REQUIRE(std::memcmp(&a.t, &b.t, sizeof(float)) == 0);
                REQUIRE(std::memcmp(&a.baryU, &b.baryU, sizeof(float)) == 0);
                REQUIRE(std::memcmp(&a.baryV, &b.baryV, sizeof(float)) == 0);
            }
        }
    }
    REQUIRE(hits > 20);   // the grid really crossed the geometry
}

scene::Entity& addCube(scene::Scene& s, const std::string& name, glm::vec3 at, float half = 0.5f) {
    const scene::MeshId id = s.addMesh(scene::makeCube(half));
    scene::Entity& e = s.addEntity(name, id);
    e.transform.position = at;
    return e;
}

// Runs frame A then frame B through one EmbreeScene in `mode`, and returns it holding frame B.
struct TwoFrames {
    Snapshot a;
    Snapshot b;
    EmbreeScene bvh;
};
void runTwoFrames(TwoFrames& f, BvhReuse mode) {
    REQUIRE(f.bvh.update(f.a, 1, BvhReuse::Detect).has_value());
    REQUIRE(f.bvh.update(f.b, 1, mode).has_value());
}

} // namespace

TEST_CASE("BVH reuse: an entity that moves is hit where it went, not where it was",
          "[unit][pathtrace][bvh-reuse]") {
    scene::Scene s;
    addCube(s, "mover", {0.0f, 0.0f, 0.0f});
    addCube(s, "anchor", {10.0f, 0.0f, 0.0f});
    TwoFrames f;
    f.a = pathtrace::buildSnapshot(s);
    s.entities[0].transform.position = {5.0f, 0.0f, 0.0f};
    f.b = pathtrace::buildSnapshot(s);

    SECTION("with change detection") {
        runTwoFrames(f, BvhReuse::Detect);
        CHECK(hitName(f.b, dropAt(f.bvh, f.b, 5.0f)) == "mover");
        CHECK(hitName(f.b, dropAt(f.bvh, f.b, 0.0f)) == "(miss)");
        CHECK(hitName(f.b, dropAt(f.bvh, f.b, 10.0f)) == "anchor");
        // A rigid move is a transform: no triangles are rebuilt, only the top level.
        const auto& st = f.bvh.lastUpdate();
        CHECK(st.objectsBuilt == 0);
        CHECK(st.trianglesBuilt == 0);
        CHECK(st.topLevelBuilt);
        CHECK(st.transformsChanged == 1);
        requireSameAsFreshBuild(f.bvh, f.b);
    }
    SECTION("CONTROL: with detection off the stale structure puts the mover where it was") {
        runTwoFrames(f, BvhReuse::TrustStructure);
        CHECK(hitName(f.b, dropAt(f.bvh, f.b, 5.0f)) == "(miss)");
        CHECK(hitName(f.b, dropAt(f.bvh, f.b, 0.0f)) == "mover");
    }
}

TEST_CASE("BVH reuse: a mesh deformed in object space is rebuilt, and only that group",
          "[unit][pathtrace][bvh-reuse]") {
    // Both cubes sit under the identity transform, so they share ONE child scene. Moving the
    // vertices of one -- same counts, same transform, different positions -- is exactly the change
    // a transform comparison cannot see and a vertex comparison must.
    scene::Scene s;
    addCube(s, "bent", {0.0f, 0.0f, 0.0f});
    addCube(s, "still", {0.0f, 0.0f, 0.0f});
    for (auto& v : s.meshes[s.entities[1].mesh].vertices) v.position.x += 10.0f;
    // A third cube under a different transform: its own child, which must be KEPT.
    addCube(s, "elsewhere", {-10.0f, 0.0f, 0.0f});

    TwoFrames f;
    f.a = pathtrace::buildSnapshot(s);
    for (auto& v : s.meshes[s.entities[0].mesh].vertices) v.position.x += 5.0f;
    f.b = pathtrace::buildSnapshot(s);
    REQUIRE(f.a.meshes[0].objectToWorld == f.b.meshes[0].objectToWorld);

    SECTION("with change detection") {
        runTwoFrames(f, BvhReuse::Detect);
        CHECK(hitName(f.b, dropAt(f.bvh, f.b, 5.0f)) == "bent");
        CHECK(hitName(f.b, dropAt(f.bvh, f.b, 0.0f)) == "(miss)");
        CHECK(hitName(f.b, dropAt(f.bvh, f.b, 10.0f)) == "still");
        CHECK(hitName(f.b, dropAt(f.bvh, f.b, -10.0f)) == "elsewhere");
        const auto& st = f.bvh.lastUpdate();
        CHECK(st.objects == 2);
        CHECK(st.objectsBuilt == 1);          // the shared group, not "elsewhere"
        CHECK(st.trianglesBuilt == 24);       // two cubes
        CHECK(st.trianglesReused == 12);
        requireSameAsFreshBuild(f.bvh, f.b);
    }
    SECTION("CONTROL: with detection off the stale structure keeps the old vertices") {
        runTwoFrames(f, BvhReuse::TrustStructure);
        CHECK(hitName(f.b, dropAt(f.bvh, f.b, 5.0f)) == "(miss)");
        CHECK(hitName(f.b, dropAt(f.bvh, f.b, 0.0f)) == "bent");
    }
}

TEST_CASE("BVH reuse: a skinned mesh re-posed by its rig is picked up",
          "[unit][pathtrace][bvh-reuse]") {
    // A character: the entity transform never changes, the palette does, so only the skinned
    // vertices say it moved. It is its own child scene, so the static cube next to it is kept.
    scene::Scene s;
    scene::MeshData mesh = scene::makeCube(0.5f);
    mesh.skin.assign(mesh.vertices.size(), scene::SkinInfluence{});
    for (auto& inf : mesh.skin) {
        inf.joints[0] = 0;
        inf.weights = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
    }
    const scene::MeshId id = s.addMesh(std::move(mesh));
    scene::SkinnedRig rig;
    rig.palette = {glm::mat4(1.0f)};
    s.rigs.push_back(std::move(rig));
    s.addEntity("walker", id).rig = 0;
    addCube(s, "prop", {0.0f, 0.0f, 0.0f}).transform.position = {-10.0f, 0.0f, 0.0f};

    TwoFrames f;
    f.a = pathtrace::buildSnapshot(s);
    s.rigs[0].palette = {glm::translate(glm::mat4(1.0f), glm::vec3(5.0f, 0.0f, 0.0f))};
    f.b = pathtrace::buildSnapshot(s);
    REQUIRE(f.b.meshes[0].deforming);

    SECTION("with change detection") {
        runTwoFrames(f, BvhReuse::Detect);
        CHECK(hitName(f.b, dropAt(f.bvh, f.b, 5.0f)) == "walker");
        CHECK(hitName(f.b, dropAt(f.bvh, f.b, 0.0f)) == "(miss)");
        CHECK(hitName(f.b, dropAt(f.bvh, f.b, -10.0f)) == "prop");
        const auto& st = f.bvh.lastUpdate();
        CHECK(st.objectsBuilt == 1);
        CHECK(st.trianglesBuilt == 12);
        CHECK(st.trianglesReused == 12);
        requireSameAsFreshBuild(f.bvh, f.b);
    }
    SECTION("CONTROL: with detection off the walker is hit in its old pose") {
        runTwoFrames(f, BvhReuse::TrustStructure);
        CHECK(hitName(f.b, dropAt(f.bvh, f.b, 5.0f)) == "(miss)");
        CHECK(hitName(f.b, dropAt(f.bvh, f.b, 0.0f)) == "walker");
    }
}

TEST_CASE("BVH reuse: one scatter instance that moves is hit where it went",
          "[unit][pathtrace][bvh-reuse]") {
    scene::Scene s;
    scene::ProceduralGeometry proc;
    proc.name = "lilies";
    proc.source.kind = scene::PrimitiveKind::Box;
    proc.source.size = glm::vec3(1.0f);
    for (int i = 0; i < 3; ++i) {
        spatial::InstanceRecord r;
        r.position = glm::vec4(static_cast<float>(i) * 4.0f - 4.0f, 0.0f, 0.0f, 1.0f);   // -4, 0, 4
        r.rotation = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        r.scale = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);
        proc.instances.push_back(r);
    }
    s.procedurals.push_back(std::move(proc));

    TwoFrames f;
    f.a = pathtrace::buildSnapshot(s);
    s.procedurals[0].instances[1].position = glm::vec4(0.0f, 0.0f, 2.0f, 1.0f);   // drifts in z
    f.b = pathtrace::buildSnapshot(s);

    SECTION("with change detection") {
        runTwoFrames(f, BvhReuse::Detect);
        const auto moved = dropAt(f.bvh, f.b, 0.0f, 2.0f);
        CHECK(hitName(f.b, moved) == "lilies");
        CHECK(moved.instanceIndex == 1);
        CHECK(hitName(f.b, dropAt(f.bvh, f.b, 0.0f, 0.0f)) == "(miss)");
        CHECK(dropAt(f.bvh, f.b, 4.0f).instanceIndex == 2);
        const auto& st = f.bvh.lastUpdate();
        CHECK(st.objectsBuilt == 0);          // the box itself did not change
        CHECK(st.topLevelBuilt);
        CHECK(st.transformsChanged == 1);
        requireSameAsFreshBuild(f.bvh, f.b);
    }
    SECTION("CONTROL: with detection off the instance is hit where it was") {
        runTwoFrames(f, BvhReuse::TrustStructure);
        CHECK(hitName(f.b, dropAt(f.bvh, f.b, 0.0f, 2.0f)) == "(miss)");
        CHECK(hitName(f.b, dropAt(f.bvh, f.b, 0.0f, 0.0f)) == "lilies");
    }
}

TEST_CASE("BVH reuse: an unchanged frame rebuilds nothing, and the answer is the same",
          "[unit][pathtrace][bvh-reuse]") {
    scene::Scene s;
    addCube(s, "a", {0.0f, 0.0f, 0.0f});
    addCube(s, "b", {3.0f, 0.0f, 0.0f});
    const Snapshot snap = pathtrace::buildSnapshot(s);
    EmbreeScene bvh;
    REQUIRE(bvh.update(snap, 1).has_value());
    CHECK(bvh.lastUpdate().objectsBuilt == 2);
    CHECK(bvh.lastUpdate().topLevelBuilt);

    const Snapshot again = pathtrace::buildSnapshot(s);
    REQUIRE(bvh.update(again, 1).has_value());
    CHECK(bvh.lastUpdate().objectsBuilt == 0);
    CHECK(bvh.lastUpdate().trianglesReused == 24);
    CHECK_FALSE(bvh.lastUpdate().topLevelBuilt);
    requireSameAsFreshBuild(bvh, again);

    // CONTROL: Rebuild really does rebuild -- otherwise the arm above could pass on a structure
    // that never counted anything.
    REQUIRE(bvh.update(again, 1, BvhReuse::Rebuild).has_value());
    CHECK(bvh.lastUpdate().objectsBuilt == 2);
    CHECK(bvh.lastUpdate().deviceCreated);
}

TEST_CASE("BVH reuse: objects appearing and disappearing change the structure safely",
          "[unit][pathtrace][bvh-reuse]") {
    // Hiding an entity shifts every later mesh's index in the snapshot. A table that kept last
    // frame's indices would resolve hits to the wrong mesh -- the right geometry, the wrong material.
    scene::Scene s;
    addCube(s, "first", {-4.0f, 0.0f, 0.0f});
    addCube(s, "second", {0.0f, 0.0f, 0.0f});
    addCube(s, "third", {4.0f, 0.0f, 0.0f});
    EmbreeScene bvh;
    const Snapshot all = pathtrace::buildSnapshot(s);
    REQUIRE(bvh.update(all, 1).has_value());

    s.entities[0].visible = false;
    const Snapshot two = pathtrace::buildSnapshot(s);
    REQUIRE(two.meshes.size() == 2);
    REQUIRE(bvh.update(two, 1).has_value());
    CHECK(hitName(two, dropAt(bvh, two, -4.0f)) == "(miss)");
    CHECK(hitName(two, dropAt(bvh, two, 0.0f)) == "second");
    CHECK(hitName(two, dropAt(bvh, two, 4.0f)) == "third");
    CHECK(bvh.lastUpdate().objectsBuilt == 0);   // the survivors are kept
    requireSameAsFreshBuild(bvh, two);

    s.entities[0].visible = true;
    const Snapshot back = pathtrace::buildSnapshot(s);
    REQUIRE(bvh.update(back, 1).has_value());
    CHECK(hitName(back, dropAt(bvh, back, -4.0f)) == "first");
    CHECK(bvh.lastUpdate().objectsBuilt == 1);
    requireSameAsFreshBuild(bvh, back);
}

TEST_CASE("BVH reuse: a PathTracer renders the same pixels reusing as rebuilding",
          "[unit][pathtrace][bvh-reuse]") {
    // The integration arm at unit scale: one tracer carried across three frames of a moving and a
    // deforming object, against a tracer told to rebuild every time. Bit-identical radiance.
    scene::Scene s;
    addCube(s, "floor", {0.0f, -1.0f, 0.0f}, 3.0f);
    addCube(s, "mover", {0.0f, 0.5f, 0.0f}, 0.5f);
    scene::PunctualLight sun;
    sun.type = scene::PunctualLight::Type::Directional;
    sun.direction = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f));
    sun.intensity = 3.0f;
    s.lights.push_back(sun);
    s.camera.position = {0.0f, 4.0f, 8.0f};
    s.camera.target = {0.0f, 0.0f, 0.0f};

    pathtrace::TraceSettings reuse;
    reuse.width = 48;
    reuse.height = 32;
    reuse.samplesPerPixel = 4;
    reuse.maxDepth = 3;
    reuse.threads = 2;
    pathtrace::TraceSettings rebuild = reuse;
    rebuild.reuseAcceleration = false;

    pathtrace::PathTracer carried;
    pathtrace::PathTracer control;
    for (int frame = 0; frame < 3; ++frame) {
        s.entities[1].transform.position.x = 0.7f * static_cast<float>(frame);
        for (auto& v : s.meshes[s.entities[0].mesh].vertices) v.position.y += 0.01f * frame;
        const Snapshot snap = pathtrace::buildSnapshot(s);
        pathtrace::Framebuffer a;
        pathtrace::Framebuffer b;
        REQUIRE(carried.render(snap, reuse, a).has_value());
        REQUIRE(control.render(snap, rebuild, b).has_value());
        INFO("frame " << frame);
        REQUIRE_FALSE(a.isBlack());
        REQUIRE(a.radiance.size() == b.radiance.size());
        REQUIRE(std::memcmp(a.radiance.data(), b.radiance.data(),
                            a.radiance.size() * sizeof(glm::vec3)) == 0);
        if (frame > 0) {
            CHECK(carried.stats().bvh.trianglesReused == 12);  // floor deformed, mover moved:
            CHECK(carried.stats().bvh.objectsBuilt == 1);      // only the floor's child rebuilt
            CHECK(control.stats().bvh.objectsBuilt == 2);
        }
    }
}
