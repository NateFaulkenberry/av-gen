// Mesh optimisation and LOD chains (ADR-078). These tests are about the two failures a LOD
// generator hides best: a level that did not actually shrink, and a level that shrank by
// destroying the shape. Both pass a triangle count.

#include "assets/gltf_loader.hpp"
#include "assets/mesh_lod.hpp"
#include "scene/procedural.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace avgen;
using Catch::Matchers::WithinAbs;

namespace {

std::uint32_t triangles(const scene::MeshData& m) {
    return static_cast<std::uint32_t>(m.indices.size() / 3);
}

float diagonal(const scene::MeshData& m) {
    const auto [lo, hi] = m.bounds();
    return glm::length(hi - lo);
}

// How far the simplified mesh's box drifts from the source's, as a fraction of the source's
// diagonal. A simplifier that collapsed a tree into a point keeps every triangle-count promise and
// fails this.
float boxDrift(const scene::MeshData& source, const scene::MeshData& lod) {
    const auto [slo, shi] = source.bounds();
    const auto [llo, lhi] = lod.bounds();
    const float d = std::max(glm::length(shi - slo), 1e-6f);
    return std::max(glm::length(llo - slo), glm::length(lhi - shi)) / d;
}

float pointTriangleDistance(glm::vec3 p, glm::vec3 a, glm::vec3 b, glm::vec3 c) {
    const glm::vec3 ab = b - a;
    const glm::vec3 ac = c - a;
    const glm::vec3 n = glm::cross(ab, ac);
    const float nn = glm::dot(n, n);
    if (nn > 1e-20f) {
        const glm::vec3 proj = p - n * (glm::dot(n, p - a) / nn);
        // Barycentrics of the projection; inside means the plane distance is the answer.
        const glm::vec3 ap = proj - a;
        const float d00 = glm::dot(ab, ab);
        const float d01 = glm::dot(ab, ac);
        const float d11 = glm::dot(ac, ac);
        const float denom = d00 * d11 - d01 * d01;
        if (std::abs(denom) > 1e-20f) {
            const float d20 = glm::dot(ap, ab);
            const float d21 = glm::dot(ap, ac);
            const float v = (d11 * d20 - d01 * d21) / denom;
            const float w = (d00 * d21 - d01 * d20) / denom;
            if (v >= 0.0f && w >= 0.0f && v + w <= 1.0f) {
                return glm::length(p - proj);
            }
        }
    }
    const auto edge = [p](glm::vec3 s, glm::vec3 e) {
        const glm::vec3 d = e - s;
        const float dd = glm::dot(d, d);
        const float t = dd > 1e-20f ? std::clamp(glm::dot(p - s, d) / dd, 0.0f, 1.0f) : 0.0f;
        return glm::length(p - (s + d * t));
    };
    return std::min({edge(a, b), edge(b, c), edge(c, a)});
}

struct SurfaceError {
    float mean = 0.0f;
    float max = 0.0f;
};

// One-sided geometric error: for every source vertex, the distance to the nearest triangle of the
// simplified mesh, as a fraction of the source's bounding diagonal. Brute force -- these meshes are
// a few thousand triangles and this is a test, not a shipping metric.
SurfaceError surfaceError(const scene::MeshData& source, const scene::MeshData& lod) {
    if (lod.indices.empty() || source.vertices.empty()) {
        return {1.0f, 1.0f};
    }
    const float scale = std::max(diagonal(source), 1e-6f);
    double total = 0.0;
    float worst = 0.0f;
    for (const scene::Vertex& v : source.vertices) {
        float best = std::numeric_limits<float>::max();
        for (std::size_t i = 0; i + 2 < lod.indices.size(); i += 3) {
            best = std::min(best, pointTriangleDistance(v.position, lod.vertices[lod.indices[i]].position,
                                                        lod.vertices[lod.indices[i + 1]].position,
                                                        lod.vertices[lod.indices[i + 2]].position));
        }
        total += static_cast<double>(best);
        worst = std::max(worst, best);
    }
    return {static_cast<float>(total / static_cast<double>(source.vertices.size())) / scale, worst / scale};
}

// The geometric triangles of a mesh, each canonicalised so index order and winding-preserving
// rotation do not matter, and the whole set sorted. Two meshes with the same set describe the same
// surface however their buffers are arranged.
std::vector<std::array<float, 9>> triangleSet(const scene::MeshData& m) {
    std::vector<std::array<float, 9>> out;
    out.reserve(m.indices.size() / 3);
    for (std::size_t i = 0; i + 2 < m.indices.size(); i += 3) {
        std::array<glm::vec3, 3> p{m.vertices[m.indices[i]].position, m.vertices[m.indices[i + 1]].position,
                                   m.vertices[m.indices[i + 2]].position};
        // Rotate to the lexicographically smallest corner; rotation preserves winding, so a
        // flipped triangle still compares different.
        const auto less = [](glm::vec3 a, glm::vec3 b) {
            return std::tie(a.x, a.y, a.z) < std::tie(b.x, b.y, b.z);
        };
        const std::size_t first =
            static_cast<std::size_t>(std::distance(p.begin(), std::min_element(p.begin(), p.end(), less)));
        std::array<float, 9> t{};
        for (std::size_t k = 0; k < 3; ++k) {
            const glm::vec3 v = p[(first + k) % 3];
            t[k * 3] = v.x;
            t[k * 3 + 1] = v.y;
            t[k * 3 + 2] = v.z;
        }
        out.push_back(t);
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool sameBuffers(const scene::MeshData& a, const scene::MeshData& b) {
    if (a.indices != b.indices || a.vertices.size() != b.vertices.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.vertices.size(); ++i) {
        if (a.vertices[i].position != b.vertices[i].position ||
            a.vertices[i].normal != b.vertices[i].normal || a.vertices[i].uv != b.vertices[i].uv) {
            return false;
        }
    }
    return true;
}

scene::MeshData singleTriangle() {
    scene::MeshData m;
    m.name = "tri";
    m.vertices = {{{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
                  {{1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
                  {{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}}};
    m.indices = {0, 1, 2};
    return m;
}

// A row of three-bladed fins whose ends meet: every spine edge is shared by three faces, and the
// two vertices at each joint hold the same position with different UVs. Non-manifold geometry with
// attribute seams through it, which is what vegetation is made of and what actually stalls the
// preserving simplifier -- it will not move a vertex when doing so would change which surfaces
// meet there or tear a seam, so past a point it stops. Measured on the real assets: CommonTree_1's
// trunk and branches will not go below 90% of their triangles at any ratio they are asked for
// (ADR-078). This fixture stalls at 25%, for the same reason and in the same way.
scene::MeshData nonManifoldFins(int count) {
    scene::MeshData m;
    m.name = "fins";
    for (int i = 0; i < count; ++i) {
        const glm::vec3 at{static_cast<float>(i) * 0.3f, 0.0f, 0.0f};
        const auto b = static_cast<std::uint32_t>(m.vertices.size());
        m.vertices.push_back({at, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}});
        m.vertices.push_back({at + glm::vec3(0.0f, 1.0f, 0.0f), {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}});
        m.vertices.push_back({at + glm::vec3(0.30f, 0.0f, 0.00f), {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}});
        m.vertices.push_back({at + glm::vec3(-0.15f, 0.0f, 0.26f), {0.87f, 0.0f, -0.5f}, {1.0f, 0.0f}});
        m.vertices.push_back({at + glm::vec3(-0.15f, 0.0f, -0.26f), {-0.87f, 0.0f, -0.5f}, {1.0f, 0.0f}});
        m.indices.insert(m.indices.end(), {b, b + 1, b + 2, b, b + 1, b + 3, b, b + 1, b + 4});
    }
    return m;
}

// How far a level's bounding box has receded from the source's, face by face, in mesh units. The
// quantity `buildLodChain`'s guard compares against `boundsTolerance`, written out here rather than
// read off `LodLevel::boundsError` -- a test that asks the implementation what it measured is a
// test of nothing (§37).
float boundsRecession(const scene::MeshData& source, const scene::MeshData& level) {
    const auto [slo, shi] = source.bounds();
    const auto [llo, lhi] = level.bounds();
    float worst = 0.0f;
    for (int axis = 0; axis < 3; ++axis) {
        worst = std::max(worst, llo[axis] - slo[axis]);
        worst = std::max(worst, shi[axis] - lhi[axis]);
    }
    return worst;
}

// A dense canopy standing on a thin trunk, optionally scaled non-uniformly.
//
// The shape the defect lives in, and none of its three properties is decoration (ADR-182). It
// stands **on its own origin**, so its bounds are asymmetric and a level that loses the bottom
// loses it in one direction only. Its trunk is **thin** -- 8 cm of radius against a 5 m height --
// so a simplifier quantising onto a grid swallows it whole while the canopy, which carries 97% of
// the triangles and all of the error, barely moves. And the scale is **non-uniform** in two of the
// three cases, because a tolerance taken as a fraction of one axis and a tolerance taken as a
// fraction of the diagonal are the same number on a cube and different numbers here.
scene::MeshData canopyOnTrunk(glm::vec3 scale) {
    scene::MeshData m;
    m.name = "canopy-on-trunk";
    const auto append = [&m, scale](const scene::MeshData& part, glm::vec3 offset) {
        const auto base = static_cast<std::uint32_t>(m.vertices.size());
        for (const scene::Vertex& v : part.vertices) {
            scene::Vertex moved = v;
            moved.position = (v.position + offset) * scale;
            m.vertices.push_back(moved);
        }
        for (const std::uint32_t i : part.indices) {
            m.indices.push_back(base + i);
        }
    };
    append(scene::makeUvSphere(1.2f, 40, 26), {0.0f, 5.0f, 0.0f});
    append(scene::makeCylinder(0.08f, 5.0f, 8, 3, true), {0.0f, 2.5f, 0.0f});
    return m;
}

} // namespace

TEST_CASE("optimiseMesh reorders a mesh without changing its surface", "[assets][lod]") {
    const scene::MeshData sphere = scene::makeUvSphere(1.0f, 48, 32);
    const scene::MeshData optimised = assets::optimiseMesh(sphere);

    // The set of triangles is identical; only the order of the indices and the layout of the
    // vertex buffer moved. This is the whole contract of a cache/fetch pass, and it is the one a
    // caller would never notice breaking until shading went wrong somewhere far away.
    CHECK(triangleSet(optimised) == triangleSet(sphere));
    CHECK(triangles(optimised) == triangles(sphere));
    CHECK(optimised.indices != sphere.indices); // it did do something
    CHECK(optimised.vertices.size() <= sphere.vertices.size());
    CHECK_THAT(static_cast<double>(boxDrift(sphere, optimised)), WithinAbs(0.0, 1e-6));
    CHECK(sameBuffers(optimised, assets::optimiseMesh(sphere)));

    // And it is worth having done: the point of the pass is the cache behaviour, so that is what
    // is asserted rather than the fact that the indices moved.
    const assets::MeshCacheStats before = assets::analyzeMesh(sphere);
    const assets::MeshCacheStats after = assets::analyzeMesh(optimised);
    INFO("acmr " << before.acmr << " -> " << after.acmr << ", overfetch " << before.overfetch << " -> "
                 << after.overfetch);
    CHECK(after.acmr < before.acmr);
    CHECK(after.overfetch <= before.overfetch);
    CHECK(after.acmr < 1.0f);
}

TEST_CASE("optimiseMesh welds the duplicate corners an exporter leaves behind", "[assets][lod]") {
    // An unindexed mesh: every triangle carries its own three vertices, which is what a naive
    // exporter and several of the Quaternius files produce.
    scene::MeshData split;
    split.name = "split";
    const scene::MeshData sphere = scene::makeUvSphere(1.0f, 24, 16);
    for (const std::uint32_t i : sphere.indices) {
        split.indices.push_back(static_cast<std::uint32_t>(split.vertices.size()));
        split.vertices.push_back(sphere.vertices[i]);
    }
    REQUIRE(split.vertices.size() == split.indices.size());

    const scene::MeshData welded = assets::optimiseMesh(split);
    CHECK(welded.vertices.size() < split.vertices.size() / 2);
    CHECK(triangleSet(welded) == triangleSet(split));
}

TEST_CASE("A LOD chain actually reduces, and roughly where it was asked to", "[assets][lod]") {
    const scene::MeshData sphere = scene::makeUvSphere(1.0f, 96, 64);
    const auto sourceTris = triangles(sphere);
    REQUIRE(sourceTris > 8000);

    const auto chain = assets::buildLodChain(sphere, assets::heroLodSettings());
    REQUIRE(chain.has_value());
    REQUIRE(chain->levels.size() == 4);
    CHECK(chain->sourceTriangles == triangles(assets::optimiseMesh(sphere)));

    std::uint32_t previous = std::numeric_limits<std::uint32_t>::max();
    for (const assets::LodLevel& level : chain->levels) {
        const auto tris = triangles(level.mesh);
        INFO("lod target " << level.targetRatio << " -> " << tris << " triangles, error " << level.error);
        CHECK(tris < previous);
        previous = tris;
        // Closed manifold geometry is exactly the case the preserving simplifier is good at, so
        // every level should land close to its target rather than stopping short.
        CHECK(level.reachedTarget);
        CHECK(level.achievedRatio <= level.targetRatio + 0.02f);
        CHECK(level.achievedRatio > level.targetRatio * 0.6f);
        // A LOD that lost the object's extent is a LOD that pops. 3% of the diagonal at 7% of the
        // triangles is generous; a collapse to a point would read 50%.
        CHECK(boxDrift(sphere, level.mesh) < 0.03f);
        CHECK(level.mesh.valid());
    }
}

TEST_CASE("A LOD level reports the error it achieved, not the ratio it was asked for", "[assets][lod]") {
    const scene::MeshData sphere = scene::makeUvSphere(1.0f, 96, 64);
    const auto chain = assets::buildLodChain(sphere, assets::heroLodSettings());
    REQUIRE(chain.has_value());

    const assets::LodLevel& lod0 = chain->levels[0];
    const assets::LodLevel& gentle = chain->levels[1];
    const assets::LodLevel& aggressive = chain->levels.back();

    CHECK(lod0.error == 0.0f); // nothing was removed, so nothing was lost
    CHECK(gentle.error > 0.0f);
    CHECK(aggressive.error > gentle.error);
    // The gentle level is close enough to the source that a caller could use it anywhere; the
    // aggressive one is not, and the number is what says so.
    CHECK(gentle.relativeError < 0.01f);
    CHECK(aggressive.relativeError > gentle.relativeError * 2.0f);
    // Absolute error is the relative error in the mesh's own units, so a caller can compare it
    // against a projected pixel size. A unit sphere's simplification scale is about 2.
    CHECK_THAT(static_cast<double>(aggressive.error / aggressive.relativeError), WithinAbs(2.0, 0.5));

    // The reported error tracks what the surface actually did, which is the claim that matters:
    // an error number that does not correspond to geometry is worse than no error number.
    const SurfaceError measured = surfaceError(sphere, aggressive.mesh);
    INFO("reported " << aggressive.relativeError << " measured mean " << measured.mean << " max "
                     << measured.max);
    CHECK(measured.max < aggressive.relativeError * 6.0f + 0.01f);
    CHECK(measured.mean < aggressive.relativeError);
}

TEST_CASE("A simplification that stops short says so instead of pretending", "[assets][lod]") {
    const scene::MeshData fins = nonManifoldFins(200);
    const auto hero = assets::buildLodChain(fins, assets::heroLodSettings());
    REQUIRE(hero.has_value());
    const assets::LodLevel& aggressive = hero->levels.back();
    INFO("asked " << aggressive.targetRatio << ", got " << aggressive.achievedRatio);
    CHECK_FALSE(aggressive.reachedTarget);
    CHECK(aggressive.achievedRatio > aggressive.targetRatio * 2.0f);
    // A hero is never quietly swapped for a sloppy approximation; it stalls and says so.
    CHECK_FALSE(aggressive.sloppy);

    // The vegetation calibration is the answer to it. The fallback fires only on a level that
    // stalled, and the sloppy simplifier ignores topology and gets past it.
    const auto vegetation = assets::buildLodChain(fins, assets::vegetationLodSettings());
    REQUIRE(vegetation.has_value());
    const auto sloppyLevels = std::count_if(vegetation->levels.begin(), vegetation->levels.end(),
                                            [](const assets::LodLevel& l) { return l.sloppy; });
    CHECK(sloppyLevels > 0);
    for (const assets::LodLevel& level : vegetation->levels) {
        INFO("vegetation asked " << level.targetRatio << ", got " << level.achievedRatio
                                 << (level.sloppy ? " (sloppy)" : ""));
        if (level.sloppy) {
            // It got past the stall without throwing the shape away.
            CHECK(level.achievedRatio < 0.2f);
            CHECK(boxDrift(fins, level.mesh) < 0.1f);
            CHECK(level.error > 0.0f);
        }
        CHECK(triangles(level.mesh) >= 1);
    }

    // The fallback does not fire on geometry that never stalls: a closed sphere reaches every
    // ratio the preserving simplifier is given, so arming the fallback changes nothing about it.
    const scene::MeshData sphere = scene::makeUvSphere(1.0f, 64, 40);
    const auto armed = assets::buildLodChain(sphere, assets::vegetationLodSettings());
    REQUIRE(armed.has_value());
    for (const assets::LodLevel& level : armed->levels) {
        INFO("sphere level " << level.targetRatio);
        CHECK_FALSE(level.sloppy);
        CHECK(level.reachedTarget);
    }
}

TEST_CASE("No level is ever empty, however badly the simplifier does", "[assets][lod]") {
    // Pruning is what deletes whole components, and on the real assets it was measured deleting
    // all of them (ADR-078). Whatever the settings, a level with no triangles in it is an object
    // that vanishes at a distance, so the chain returns the source and says it fell short instead.
    assets::LodChainSettings destructive;
    destructive.ratios = {1.0f, 0.5f, 0.2f, 0.02f};
    destructive.prune = true;
    destructive.maxError = 1.0f;
    for (const scene::MeshData& mesh :
         {nonManifoldFins(160), scene::makeUvSphere(0.6f, 20, 14), singleTriangle()}) {
        const auto chain = assets::buildLodChain(mesh, destructive);
        REQUIRE(chain.has_value());
        for (const assets::LodLevel& level : chain->levels) {
            INFO(mesh.name << " at " << level.targetRatio << ": " << triangles(level.mesh) << " triangles");
            CHECK(triangles(level.mesh) >= 1);
            CHECK(level.mesh.valid());
        }
    }
}

TEST_CASE("LOD generation is deterministic", "[assets][lod]") {
    const scene::MeshData sphere = scene::makeUvSphere(1.0f, 64, 40);
    const scene::MeshData fins = nonManifoldFins(120);
    for (const scene::MeshData* mesh : {&sphere, &fins}) {
        for (const assets::LodChainSettings& settings :
             {assets::heroLodSettings(), assets::vegetationLodSettings()}) {
            const auto a = assets::buildLodChain(*mesh, settings);
            const auto b = assets::buildLodChain(*mesh, settings);
            REQUIRE(a.has_value());
            REQUIRE(b.has_value());
            REQUIRE(a->levels.size() == b->levels.size());
            for (std::size_t i = 0; i < a->levels.size(); ++i) {
                INFO(mesh->name << " level " << i);
                CHECK(sameBuffers(a->levels[i].mesh, b->levels[i].mesh));
                CHECK(a->levels[i].error == b->levels[i].error);
                CHECK(a->levels[i].achievedRatio == b->levels[i].achievedRatio);
            }
        }
    }
}

TEST_CASE("Degenerate meshes are refused with a reason, not a crash", "[assets][lod]") {
    CHECK_FALSE(assets::buildLodChain(scene::MeshData{}).has_value());

    scene::MeshData noIndices;
    noIndices.vertices = singleTriangle().vertices;
    CHECK_FALSE(assets::buildLodChain(noIndices).has_value());

    scene::MeshData notTriangles = singleTriangle();
    notTriangles.indices = {0, 1};
    CHECK_FALSE(assets::buildLodChain(notTriangles).has_value());

    scene::MeshData outOfRange = singleTriangle();
    outOfRange.indices = {0, 1, 7};
    const auto bad = assets::buildLodChain(outOfRange);
    REQUIRE_FALSE(bad.has_value());
    CHECK(bad.error().message.find("index past") != std::string::npos);

    scene::MeshData nan = singleTriangle();
    nan.vertices[1].position.y = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(assets::buildLodChain(nan).has_value());

    // A single triangle has nothing to give up. It must come back whole rather than empty, and
    // every level must still be a drawable mesh.
    const auto one = assets::buildLodChain(singleTriangle());
    REQUIRE(one.has_value());
    for (const assets::LodLevel& level : one->levels) {
        CHECK(triangles(level.mesh) == 1);
        CHECK(level.mesh.valid());
        CHECK(level.error == 0.0f);
    }

    // No normals at all: every attribute weight has nothing to say, and the simplifier must not
    // divide by the zero length. glTF primitives without NORMAL arrive exactly like this.
    scene::MeshData flat = scene::makeUvSphere(1.0f, 32, 24);
    for (scene::Vertex& v : flat.vertices) {
        v.normal = glm::vec3(0.0f);
    }
    const auto noNormals = assets::buildLodChain(flat);
    REQUIRE(noNormals.has_value());
    CHECK(triangles(noNormals->levels.back().mesh) < triangles(flat) / 4);
    CHECK(boxDrift(flat, noNormals->levels.back().mesh) < 0.05f);

    // optimiseMesh is the permissive half of the pair: it is called on anything and returns the
    // input untouched rather than half-processing a mesh it cannot read.
    CHECK(assets::optimiseMesh(scene::MeshData{}).indices.empty());
    CHECK(sameBuffers(assets::optimiseMesh(outOfRange), outOfRange));
}

TEST_CASE("Chain settings that do not describe a chain are refused", "[assets][lod]") {
    const scene::MeshData sphere = scene::makeUvSphere(1.0f, 24, 16);
    const auto refuses = [&sphere](auto mutate) {
        assets::LodChainSettings s = assets::heroLodSettings();
        mutate(s);
        return !assets::buildLodChain(sphere, s).has_value();
    };
    CHECK(refuses([](auto& s) { s.ratios.clear(); }));
    CHECK(refuses([](auto& s) { s.ratios = {1.0f, 0.2f, 0.5f}; })); // not descending
    CHECK(refuses([](auto& s) { s.ratios = {1.0f, 0.0f}; }));       // nothing left
    CHECK(refuses([](auto& s) { s.ratios = {1.5f}; }));             // more than the source
    CHECK(refuses([](auto& s) { s.maxError = -1.0f; }));
    CHECK(refuses([](auto& s) { s.sloppyFallback = -1.0f; }));
    CHECK(refuses([](auto& s) { s.sloppyFallback = 0.5f; })); // would fire on a level that hit its target
}

TEST_CASE("The shadow index buffer describes the same surface with fewer corners", "[assets][lod]") {
    // A cube from the generator has split normals at every edge, so three vertices sit at each
    // corner and a depth-only pass transforms all three for no reason.
    const scene::MeshData box = scene::makeBeveledBox(glm::vec3(1.0f), 1, 0.0f, 1);
    assets::LodChainSettings settings = assets::heroLodSettings();
    settings.generateShadowIndices = true;

    const auto chain = assets::buildLodChain(box, settings);
    REQUIRE(chain.has_value());
    const scene::MeshData& lod0 = chain->levels[0].mesh;
    REQUIRE(chain->shadowIndices.size() == lod0.indices.size());

    scene::MeshData shadow = lod0;
    shadow.indices = chain->shadowIndices;
    CHECK(shadow.valid());
    // The same triangles, in the same places, referencing fewer distinct vertices.
    CHECK(triangleSet(shadow) == triangleSet(lod0));
    const auto distinct = [](const std::vector<std::uint32_t>& idx) {
        std::vector<std::uint32_t> s(idx);
        std::sort(s.begin(), s.end());
        s.erase(std::unique(s.begin(), s.end()), s.end());
        return s.size();
    };
    INFO("lit " << distinct(lod0.indices) << " shadow " << distinct(chain->shadowIndices));
    CHECK(distinct(chain->shadowIndices) < distinct(lod0.indices));
}

// ---------------------------------------------------------------------------------------------
// Calibration. Hidden (the leading dot): it needs the Quaternius pack, which is gitignored, and it
// prints a table rather than asserting. Run it with `avgen_tests "[.lodmeasure]"` when the ratios
// or the settings are questioned -- that is what produced the numbers in ADR-078.

TEST_CASE("Measure: meshoptimizer against the existing decimator", "[.lodmeasure]") {
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    namespace fs = std::filesystem;
    const fs::path root = fs::path(AVGEN_SOURCE_DIR) / "assets" / "quaternius" / "glTF";
    if (!fs::exists(root)) {
        SKIP("the Quaternius pack is not present");
    }
    const std::array<const char*, 8> files{
        "CommonTree_1.gltf", "TwistedTree_2.gltf", "DeadTree_1.gltf",      "Bush_Common.gltf",
        "Fern_1.gltf",       "Rock_Medium_1.gltf", "Mushroom_Common.gltf", "Grass_Common_Short.gltf"};
    std::printf("\n%-20s %6s %-12s %5s %6s %8s %8s %8s %8s %6s\n", "asset", "tris", "preset", "ask", "kept",
                "meanErr", "maxErr", "boxDrift", "reported", "how");
    for (const char* file : files) {
        const fs::path path = root / file;
        if (!fs::exists(path)) {
            continue;
        }
        scene::Scene scn;
        assets::GltfLoadOptions options;
        options.loadImages = false;
        if (!assets::loadGltf(path, scn, options) || scn.meshes.empty()) {
            std::printf("%-20s LOAD FAILED\n", file);
            continue;
        }
        const scene::MeshData& raw = *std::max_element(
            scn.meshes.begin(), scn.meshes.end(), [](const scene::MeshData& a, const scene::MeshData& b) {
                return a.indices.size() < b.indices.size();
            });
        const scene::MeshData source = assets::optimiseMesh(raw);
        const auto sourceTris = triangles(source);
        const std::string name = std::string(file).substr(0, std::string(file).find('.'));

        const auto report = [&](const char* preset, const assets::LodLevel& l) {
            const SurfaceError e = surfaceError(source, l.mesh);
            std::printf("%-20s %6u %-12s %5.2f %6.3f %8.4f %8.4f %8.4f %8.4f %6s%s\n", name.c_str(),
                        sourceTris, preset, static_cast<double>(l.targetRatio),
                        static_cast<double>(l.achievedRatio), static_cast<double>(e.mean),
                        static_cast<double>(e.max), static_cast<double>(boxDrift(source, l.mesh)),
                        static_cast<double>(l.relativeError), l.sloppy ? "sloppy" : "keep",
                        l.reachedTarget ? "" : "  (short)");
            // The existing decimator at the *same triangle count*, so the two are compared on
            // quality rather than on how far each happened to get.
            const scene::MeshData d = scene::decimateMesh(source, static_cast<int>(triangles(l.mesh)));
            const SurfaceError ed = surfaceError(source, d);
            std::printf("%-20s %6u %-12s %5.2f %6.3f %8.4f %8.4f %8.4f %8s %6s\n", name.c_str(), sourceTris,
                        "  decimate", static_cast<double>(l.achievedRatio),
                        static_cast<double>(triangles(d)) / static_cast<double>(sourceTris),
                        static_cast<double>(ed.mean), static_cast<double>(ed.max),
                        static_cast<double>(boxDrift(source, d)), "-", "grid");
        };

        for (const auto& [preset, settings] : {std::pair{"hero", assets::heroLodSettings()},
                                               std::pair{"vegetation", assets::vegetationLodSettings()}}) {
            const auto t0 = std::chrono::steady_clock::now();
            const auto chain = assets::buildLodChain(source, settings);
            const auto ms =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            if (!chain) {
                std::printf("%-20s %-12s FAILED: %s\n", name.c_str(), preset, chain.error().message.c_str());
                continue;
            }
            for (std::size_t i = 1; i < chain->levels.size(); ++i) {
                report(preset, chain->levels[i]);
            }
            // The same three levels through the existing decimator, for cost as well as quality.
            const auto d0 = std::chrono::steady_clock::now();
            for (std::size_t i = 1; i < chain->levels.size(); ++i) {
                const scene::MeshData d = scene::decimateMesh(
                    source, static_cast<int>(static_cast<float>(sourceTris) * chain->levels[i].targetRatio));
                (void)d.indices.size();
            }
            const auto dms =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - d0).count();
            std::printf("%-20s %6u %-12s chain built in %.2f ms (decimateMesh: %.2f ms)\n", name.c_str(),
                        sourceTris, preset, ms, dms);
        }
    }
#endif
}

// ---- ADR-110: LOD0 goes through meshoptimizer too ----------------------------------------------

TEST_CASE("LOD0 reaches a triangle budget a grid clustering cannot", "[assets][lod]") {
    // The fins stall the preserving simplifier the way a Quaternius tree does, and a uniform grid
    // has no way to reach a triangle count on them at all: it snaps vertices into cells and keeps
    // whatever triangles survive. This is the geometry a scatter's budget is written for.
    const scene::MeshData fins = nonManifoldFins(400);
    const auto sourceTris = triangles(fins);
    REQUIRE(sourceTris == 1200u);
    const int budget = 120;

    const scene::MeshData lod0 = assets::sourceLodMesh(fins, budget, assets::lod0Settings());
    CHECK(lod0.valid());
    CHECK(triangles(lod0) <= static_cast<std::uint32_t>(budget));
    // It reached the number by simplifying, not by deleting the mesh: the shape is still there.
    CHECK(triangles(lod0) > 0u);
    CHECK(boxDrift(fins, lod0) < 0.3f);

    // The negative control, and the reason this exists: the path LOD0 used to take, on the same
    // mesh, with the same budget.
    const scene::MeshData clustered = scene::decimateMesh(fins, budget);
    INFO("grid clustering left " << triangles(clustered) << " triangles of a " << budget << " budget");
    CHECK(triangles(clustered) > static_cast<std::uint32_t>(budget));

    // A second control: with no budget, nothing is simplified. A function that reduced here would
    // be passing the check above for the wrong reason.
    const scene::MeshData unbudgeted = assets::sourceLodMesh(fins, 0, assets::lod0Settings());
    CHECK(triangles(unbudgeted) == sourceTris);
}

TEST_CASE("LOD0 comes back ordered for the GPU", "[assets][lod]") {
    // Deliberately bad triangle order, which is what an unordered source mesh looks like to the
    // vertex cache. LOD0 never went through any of this before: only LOD1 and below did.
    scene::MeshData sphere = scene::makeUvSphere(1.0f, 64, 48);
    for (std::size_t t = 0; t * 3 + 2 < sphere.indices.size(); ++t) {
        const std::size_t other = (t * 7919u) % (sphere.indices.size() / 3);
        for (std::size_t k = 0; k < 3; ++k) {
            std::swap(sphere.indices[t * 3 + k], sphere.indices[other * 3 + k]);
        }
    }
    const assets::MeshCacheStats before = assets::analyzeMesh(sphere);
    const scene::MeshData ordered = assets::sourceLodMesh(sphere, 0, assets::lod0Settings());
    const assets::MeshCacheStats after = assets::analyzeMesh(ordered);
    INFO("acmr " << before.acmr << " -> " << after.acmr << ", overfetch " << before.overfetch << " -> "
                 << after.overfetch);
    CHECK(triangles(ordered) == triangles(sphere)); // no geometry was traded for the ordering
    CHECK(after.acmr < before.acmr);
    // A sphere's vertices are each used by six triangles however they are ordered, so overfetch
    // starts at ~1.0 and there is nothing for the fetch pass to win; it must not lose either.
    CHECK(after.overfetch <= before.overfetch);
    CHECK(ordered.valid());

    // The control: the shuffled mesh must actually show the bad number this claims to fix, or the
    // check above is empty. 0.5 is the floor of ACMR and 3.0 is one transform per corner.
    CHECK(before.acmr > 2.5f);
    CHECK(after.acmr < 1.0f);
}

TEST_CASE("A skinned mesh is not reordered under its skin", "[assets][lod]") {
    // Vertex-fetch optimisation permutes the vertex buffer; MeshData::skin is parallel to it and
    // is not carried through. Reordering one without the other silently detaches every joint.
    scene::MeshData skinned = scene::makeUvSphere(1.0f, 16, 12);
    skinned.skin.assign(skinned.vertices.size(), scene::SkinInfluence{});
    REQUIRE(skinned.skinned());
    const scene::MeshData out = assets::sourceLodMesh(skinned, 8, assets::lod0Settings());
    CHECK(sameBuffers(out, skinned));
    CHECK(out.skin.size() == skinned.skin.size());
}

TEST_CASE("makeSourceMesh applies a mesh budget through the simplifier", "[assets][lod][procedural]") {
    // The wiring, not the algorithm: the budget is applied where the source mesh is resolved, so a
    // scene that writes `meshBudget` gets the mesh this test's siblings describe. Reverting the
    // call site to decimateMesh fails here and nowhere else.
    scene::SourceSpec spec;
    spec.kind = scene::PrimitiveKind::Mesh;
    spec.asset = "fins";
    spec.assetMesh = std::make_shared<const scene::MeshData>(nonManifoldFins(400));
    spec.meshBudget = 120;
    const auto built = scene::makeSourceMesh(spec);
    REQUIRE(built.has_value());
    CHECK(triangles(*built) <= 120u);

    // The control: no budget, and the mesh keeps every triangle it arrived with.
    spec.meshBudget = 0;
    const auto whole = scene::makeSourceMesh(spec);
    REQUIRE(whole.has_value());
    CHECK(triangles(*whole) == triangles(*spec.assetMesh));
}

TEST_CASE("A level that lost part of the object is refused, and never understates what it moved",
          "[assets][lod]") {
    // The defect: at 2.9% of its triangles CommonTree_1 came back with the bottom 31% of its
    // bounding box gone -- the trunk -- while reporting a relative error 5.3x smaller than the
    // deviation it actually had. The header's claim that the error "rises monotonically with
    // aggressiveness and never understates" was false for exactly that level, and a selector
    // choosing a rung by projected error would have taken it far too early.
    //
    // Two things now stop it, and they are separate on purpose. `boundsTolerance` refuses a level
    // that has lost a part of the object, the same way ADR-085's guard refuses one larger than its
    // predecessor. And `LodLevel::error` is floored by the recession, which is a *measured* lower
    // bound on the deviation -- the source has a vertex out past the level's box and the level's
    // surface lies inside it -- so the claim above is a property of the number rather than a hope
    // about the simplifier's estimate.
    //
    // Arm and control are the same call with one field different: `boundsTolerance = 0` is the
    // chain this code built before the guard existed. Same fixture, same settings, same process.
    assets::LodChainSettings guarded = assets::vegetationLodSettings();
    // Aggressive enough that the sloppy simplifier is reached, which is what eats the trunk.
    guarded.ratios = {1.0f, 0.35f, 0.12f, 0.029f};
    assets::LodChainSettings unguarded = guarded;
    unguarded.boundsTolerance = 0.0f;

    struct Fixture {
        const char* what;
        glm::vec3 scale;
    };
    // Upright, squashed flat, and stretched tall: the same topology at three aspect ratios, because
    // a rule read off one axis and a rule read off the diagonal agree on the first and disagree on
    // the other two.
    const std::array<Fixture, 3> fixtures{Fixture{"upright", {1.0f, 1.0f, 1.0f}},
                                          Fixture{"squashed", {1.0f, 0.25f, 1.0f}},
                                          Fixture{"stretched", {0.35f, 2.0f, 0.35f}}};

    bool anyControlLostGeometry = false;
    bool anyControlUnderstated = false;
    for (const Fixture& f : fixtures) {
        INFO(f.what);
        const scene::MeshData source = assets::optimiseMesh(canopyOnTrunk(f.scale));
        const float diag = diagonal(source);
        const float limit = guarded.boundsTolerance * diag;

        const auto before = assets::buildLodChain(source, unguarded);
        const auto after = assets::buildLodChain(source, guarded);
        REQUIRE(before.has_value());
        REQUIRE(after.has_value());
        REQUIRE(before->levels.size() == after->levels.size());

        for (std::size_t level = 0; level < after->levels.size(); ++level) {
            INFO("level " << level);
            const assets::LodLevel& B = (*before).levels[level];
            const assets::LodLevel& A = (*after).levels[level];
            const float wentBefore = boundsRecession(source, B.mesh);
            const float wentAfter = boundsRecession(source, A.mesh);
            INFO("recession before " << wentBefore << ", after " << wentAfter << ", limit " << limit
                                     << "; error before " << B.error << ", after " << A.error);
            anyControlLostGeometry = anyControlLostGeometry || wentBefore > limit;
            anyControlUnderstated = anyControlUnderstated || wentBefore > B.error * 1.001f + 1e-6f;

            // The invariant: no level of the guarded chain has lost a part of the object...
            CHECK(wentAfter <= limit * 1.001f + 1e-6f);
            // ...and none of them understates how far it moved.
            CHECK(A.error >= wentAfter * 0.999f - 1e-6f);
            // The guard must not have broken the property ADR-085 added: a chain descends.
            if (level > 0) {
                CHECK(triangles(A.mesh) <= triangles((*after).levels[level - 1].mesh));
            }
            // And no level is ever empty, whatever the guard decided.
            CHECK(triangles(A.mesh) >= 1);
        }
    }

    // The controls. Without them the two CHECKs above would pass on a simplifier that never lost
    // anything in the first place, which is the failure mode ADR-182 is about: the fixtures have to
    // be shapes where the wrong answer is actually reachable.
    CHECK(anyControlLostGeometry);
    CHECK(anyControlUnderstated);
}

TEST_CASE("The production tree keeps its trunk at the bottom rung", "[assets][lod]") {
    // The asset the defect was found on, at the ratios the scatter layers actually ask for. A
    // synthetic fixture can be tuned until it says what you want; this one cannot (§29).
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    namespace fs = std::filesystem;
    const fs::path path = fs::path(AVGEN_SOURCE_DIR) / "assets" / "quaternius" / "glTF" / "CommonTree_1.gltf";
    if (!fs::exists(path)) {
        SKIP("the Quaternius pack is not present");
    }
    scene::Scene scn;
    assets::GltfLoadOptions options;
    options.loadImages = false;
    REQUIRE(assets::loadGltf(path, scn, options));
    REQUIRE_FALSE(scn.meshes.empty());
    // The whole tree, not its largest part: the trunk and the canopy are separate meshes and the
    // thing that went missing was one of them.
    scene::MeshData whole;
    whole.name = "CommonTree_1";
    for (const scene::MeshData& part : scn.meshes) {
        const auto base = static_cast<std::uint32_t>(whole.vertices.size());
        whole.vertices.insert(whole.vertices.end(), part.vertices.begin(), part.vertices.end());
        for (const std::uint32_t i : part.indices) {
            whole.indices.push_back(base + i);
        }
    }
    const scene::MeshData source = assets::optimiseMesh(whole);
    const auto [lo, hi] = source.bounds();
    const float height = hi.y - lo.y;

    assets::LodChainSettings guarded = assets::vegetationLodSettings();
    assets::LodChainSettings unguarded = guarded;
    unguarded.boundsTolerance = 0.0f;
    const auto before = assets::buildLodChain(source, unguarded);
    const auto after = assets::buildLodChain(source, guarded);
    REQUIRE(before.has_value());
    REQUIRE(after.has_value());

    const auto bottomLost = [&](const scene::MeshData& m) {
        const auto [mlo, mhi] = m.bounds();
        return (mlo.y - lo.y) / height;
    };
    const assets::LodLevel& lastBefore = before->levels.back();
    const assets::LodLevel& lastAfter = after->levels.back();
    INFO("bottom lost: before " << 100.0f * bottomLost(lastBefore.mesh) << "%, after "
                                << 100.0f * bottomLost(lastAfter.mesh) << "%");
    INFO("triangles: before " << triangles(lastBefore.mesh) << ", after " << triangles(lastAfter.mesh));
    // The control: the unguarded chain really does lose the trunk, so the arm below is measuring
    // this fix and not a simplifier that behaves on this asset anyway.
    CHECK(bottomLost(lastBefore.mesh) > 0.25f);
    CHECK(lastBefore.error < boundsRecession(source, lastBefore.mesh)); // and it understated it
    // The arm.
    CHECK(bottomLost(lastAfter.mesh) < 0.15f);
    CHECK(lastAfter.error >= boundsRecession(source, lastAfter.mesh) * 0.999f);
    // It is still a bottom rung: the guard is allowed to cost triangles and is not allowed to cost
    // the whole ladder.
    CHECK(triangles(lastAfter.mesh) < triangles(after->levels.front().mesh) / 4);
#endif
}

// ---- shell thinning (ADR-344) -------------------------------------------------------------------
//
// Every arm here has a control, because the failure this machinery is most likely to have is the
// silent one: a setting that does nothing, on geometry where doing nothing and succeeding look
// identical in a triangle count. The control in each case is the *same mesh* built with thinning
// disarmed, and each control is asserted to fail the thing its arm passes.

namespace {

// `count` flat two-triangle cards, each too small for any simplifier to touch, on a tight lattice.
// This is the Tree of Life's foliage in miniature and by the same mechanism: a leaf there is 7.6
// triangles and a card here is 2, and in both cases there is no edge whose collapse leaves the
// piece recognisable. The lattice is tight so that the cloud's bounding box barely moves when
// shells are removed from it, which is the property that makes this a canopy rather than a handful
// of objects, and it is spaced so that no two cards ever share a vertex position, so the shell
// count is the card count exactly. (`fmod` over a coprime stride was the first attempt at that
// spread, and it silently collided 200 shells down to 100.)
scene::MeshData scatteredShells(int count, float size = 1.0f) {
    scene::MeshData m;
    m.name = "scattered-shells";
    const int side = static_cast<int>(std::ceil(std::cbrt(static_cast<double>(count))));
    const float step = size * 1.6f;
    for (int i = 0; i < count; ++i) {
        const auto base = static_cast<std::uint32_t>(m.vertices.size());
        const glm::vec3 at{static_cast<float>(i % side) * step,
                           static_cast<float>((i / side) % side) * step,
                           static_cast<float>(i / (side * side)) * step};
        const std::array<glm::vec3, 4> corners{glm::vec3{0, 0, 0}, glm::vec3{size, 0, 0},
                                               glm::vec3{size, 0, size}, glm::vec3{0, 0, size}};
        for (const glm::vec3& c : corners) {
            scene::Vertex v;
            v.position = at + c;
            v.normal = glm::vec3(0.0f, 1.0f, 0.0f);
            m.vertices.push_back(v);
        }
        for (const std::uint32_t index : {0u, 1u, 2u, 0u, 2u, 3u}) {
            m.indices.push_back(base + index);
        }
    }
    return m;
}

// `count` spheres, each far too large for `maxShellTriangles`. Shell-structured and simplifiable:
// the case thinning must decline.
scene::MeshData scatteredSpheres(int count) {
    scene::MeshData m;
    m.name = "scattered-spheres";
    const scene::MeshData unit = scene::makeUvSphere(1.0f, 24, 16);
    for (int i = 0; i < count; ++i) {
        const auto base = static_cast<std::uint32_t>(m.vertices.size());
        const auto fi = static_cast<float>(i);
        const glm::vec3 at{fi * 5.0f, std::fmod(fi * 11.0f, 50.0f), std::fmod(fi * 17.0f, 50.0f)};
        for (const scene::Vertex& v : unit.vertices) {
            scene::Vertex moved = v;
            moved.position = v.position + at;
            m.vertices.push_back(moved);
        }
        for (const std::uint32_t i2 : unit.indices) {
            m.indices.push_back(base + i2);
        }
    }
    return m;
}

double surfaceArea(const scene::MeshData& m) {
    double total = 0.0;
    for (std::size_t t = 0; t + 2 < m.indices.size(); t += 3) {
        const glm::vec3 a = m.vertices[m.indices[t]].position;
        const glm::vec3 b = m.vertices[m.indices[t + 1]].position;
        const glm::vec3 c = m.vertices[m.indices[t + 2]].position;
        total += 0.5 * static_cast<double>(glm::length(glm::cross(b - a, c - a)));
    }
    return total;
}

} // namespace

TEST_CASE("countShells finds the disconnected pieces of a mesh", "[assets][lod][thinning]") {
    CHECK(assets::countShells(scene::makeUvSphere(1.0f, 16, 12)) == 1);
    CHECK(assets::countShells(scatteredShells(200)) == 200);
    CHECK(assets::countShells(scatteredSpheres(7)) == 7);
    // Not a claim about an empty mesh's topology; a claim that asking is safe.
    CHECK(assets::countShells(scene::MeshData{}) == 0);
    // A hard-shaded shell carries several vertices per corner and is still one shell: the union is
    // over positions, not indices. Splitting every vertex of one tetrahedron per triangle is the
    // worst case of that, and an index-space union would call it four shells.
    scene::MeshData split;
    const scene::MeshData one = scatteredShells(1);
    for (const std::uint32_t index : one.indices) {
        split.indices.push_back(static_cast<std::uint32_t>(split.vertices.size()));
        scene::Vertex v = one.vertices[index];
        v.normal = glm::vec3(0.0f, 1.0f, 0.0f); // a different normal per copy, so no byte-weld helps
        split.vertices.push_back(v);
    }
    CHECK(assets::countShells(split) == 1);
}

// A note that belongs in one place rather than in five test bodies.
//
// **No synthetic fixture in this file reproduces the refusal that motivates thinning.** Every small
// shell tried here -- 4-triangle tetrahedra, 2-triangle cards -- meshoptimizer takes to 25% of its
// triangles quite happily, and on the cards it does so by deleting 300 of the 400 shells outright,
// which is thinning without the hashing or the area compensation and with no record that it
// happened. A real leaf stops it and a synthetic one does not, and several rounds of trying to
// build one that does produced only fixtures that were unrealistic in a different direction.
//
// So the synthetic tests below stop the simplifier by the means the settings already provide: a
// `maxError` of a thousandth of the extent, which is a documented way to say "reduce only what you
// can do almost exactly", and which the header already warns will leave levels short of their
// ratio. That is the condition thinning exists for, reached honestly. What these tests then measure
// is the thinning itself -- which shells it keeps, what it does to the area, whether it is
// deterministic, what it reports -- and every one of them has a control with thinning disarmed.
//
// The *refusal* is tested where it actually happens, on the Tree of Life's foliage, in the last
// test in this section. That one is the load-bearing claim; these are the mechanism.
constexpr float kForceThinning = 0.5f;
constexpr float kStallTheSimplifier = 0.001f;

// The synthetic arms' settings: shells small enough to qualify, and a simplifier held still.
assets::LodChainSettings thinningTestSettings() {
    assets::LodChainSettings s = assets::foliageLodSettings();
    s.thinning.fallback = kForceThinning;
    s.maxError = kStallTheSimplifier;
    return s;
}

TEST_CASE("thinning removes whole shells and reports that it did", "[assets][lod][thinning]") {
    const scene::MeshData source = scatteredShells(400); // 800 triangles over 400 shells
    assets::LodChainSettings armed = thinningTestSettings();
    armed.ratios = {1.0f, 0.25f, 0.08f};
    assets::LodChainSettings control = armed;
    control.thinning.fallback = 0.0f;
    control.sloppyFallback = 0.0f;

    const auto thinned = assets::buildLodChain(source, armed);
    const auto unthinned = assets::buildLodChain(source, control);
    REQUIRE(thinned);
    REQUIRE(unthinned);
    REQUIRE(thinned->levels.size() == 3);
    REQUIRE(unthinned->levels.size() == 3);
    CHECK(thinned->sourceShells == 400);
    // The control counts nothing, because nothing asked it to.
    CHECK(unthinned->sourceShells == 0);

    for (std::size_t level = 1; level < 3; ++level) {
        const assets::LodLevel& arm = thinned->levels[level];
        const assets::LodLevel& ctl = unthinned->levels[level];
        INFO("level " << level << " asked for " << arm.targetRatio);
        // The arm reaches the ratio...
        CHECK(arm.thinned);
        CHECK(arm.achievedRatio <= arm.targetRatio * 1.05f);
        CHECK(arm.reachedTarget);
        CHECK(arm.shells > 0);
        CHECK(arm.shells < 400);
        // ...and the control does not, which is what makes the arm a measurement of the code path
        // rather than of the settings struct. Its levels are simplified, not thinned: same ratio,
        // and every shell still present.
        CHECK_FALSE(ctl.thinned);
        CHECK(ctl.shells == 0);
        CHECK(assets::countShells(ctl.mesh) == 400);
        CHECK(assets::countShells(arm.mesh) < 400);
    }
}

TEST_CASE("a thinned level keeps the shell area it removed", "[assets][lod][thinning]") {
    const scene::MeshData source = scatteredShells(400);
    const double sourceArea = surfaceArea(source);

    assets::LodChainSettings compensated = thinningTestSettings();
    compensated.ratios = {1.0f, 0.25f};
    assets::LodChainSettings bare = compensated;
    bare.thinning.areaCompensation = 0.0f;

    const auto grown = assets::buildLodChain(source, compensated);
    const auto flat = assets::buildLodChain(source, bare);
    REQUIRE(grown);
    REQUIRE(flat);
    const double grownArea = surfaceArea(grown->levels[1].mesh);
    const double flatArea = surfaceArea(flat->levels[1].mesh);

    // The arm: a quarter of the leaves, grown by 4^0.5 = 2, is the same total leaf area. A band,
    // not a floor -- the thin stops at the first shell past the budget, so the ratio is a quarter
    // give or take one shell.
    CHECK(grownArea > sourceArea * 0.9);
    CHECK(grownArea < sourceArea * 1.1);
    CHECK(grown->levels[1].shellScale > 1.9f);
    CHECK(grown->levels[1].shellScale < 2.1f);
    // The control: the same thin with no compensation keeps a quarter of the area, and *that* is
    // what a canopy with holes in it looks like. Without this arm the one above would pass on a
    // mesh that was not thinned at all.
    CHECK(flatArea < sourceArea * 0.3);
    CHECK(flat->levels[1].shellScale == 1.0f);
    CHECK(triangles(flat->levels[1].mesh) == triangles(grown->levels[1].mesh));
}

TEST_CASE("thinning declines geometry whose shells can be simplified", "[assets][lod][thinning]") {
    assets::LodChainSettings settings = thinningTestSettings();
    settings.ratios = {1.0f, 0.25f};

    // The arm. 80 spheres of 704 triangles each: shell-structured, well past `maxShellTriangles`,
    // and perfectly simplifiable. Deleting 60 of the 80 to reach 25% would be a far worse answer
    // than collapsing edges on all 80, and this says the strategy switch knows the difference.
    const scene::MeshData spheres = scatteredSpheres(80);
    REQUIRE(assets::countShells(spheres) == 80);
    const auto simplified = assets::buildLodChain(spheres, settings);
    REQUIRE(simplified);
    CHECK(simplified->sourceShells == 80);
    CHECK_FALSE(simplified->levels[1].thinned);
    CHECK(simplified->levels[1].shells == 0);
    CHECK(simplified->levels[1].achievedRatio <= 0.26f); // and it reached the ratio anyway

    // The contrast. The same settings on shells small enough to qualify *do* thin, so the arm above
    // is a decision about the geometry and not a code path that never runs.
    const scene::MeshData leaves = scatteredShells(400);
    const auto thinned = assets::buildLodChain(leaves, settings);
    REQUIRE(thinned);
    CHECK(thinned->levels[1].thinned);

    // The control. `maxShellTriangles` is what separated them: drop it below the leaves' four
    // triangles and they stop qualifying too, and stall at the half a tetrahedron can reach.
    assets::LodChainSettings strict = settings;
    strict.thinning.maxShellTriangles = 1;
    strict.sloppyFallback = 0.0f;
    const auto stalled = assets::buildLodChain(leaves, strict);
    REQUIRE(stalled);
    CHECK_FALSE(stalled->levels[1].thinned);
    CHECK(stalled->levels[1].achievedRatio >= 0.9f);
}

TEST_CASE("a thinned chain is the same chain every time", "[assets][lod][thinning]") {
    const scene::MeshData source = scatteredShells(300);
    assets::LodChainSettings settings = thinningTestSettings();
    settings.ratios = {1.0f, 0.25f, 0.1f};
    const auto a = assets::buildLodChain(source, settings);
    const auto b = assets::buildLodChain(source, settings);
    REQUIRE(a);
    REQUIRE(b);
    REQUIRE(a->levels[1].thinned); // or the rest of this proves only that nothing happened twice
    REQUIRE(a->levels.size() == b->levels.size());
    for (std::size_t i = 0; i < a->levels.size(); ++i) {
        INFO("level " << i);
        REQUIRE(a->levels[i].mesh.indices == b->levels[i].mesh.indices);
        REQUIRE(a->levels[i].mesh.vertices.size() == b->levels[i].mesh.vertices.size());
        for (std::size_t v = 0; v < a->levels[i].mesh.vertices.size(); ++v) {
            REQUIRE(a->levels[i].mesh.vertices[v].position == b->levels[i].mesh.vertices[v].position);
        }
        CHECK(a->levels[i].shells == b->levels[i].shells);
    }
    // And a different seed is a different chain, so "deterministic" above is not "the seed is
    // ignored". Same triangle count, different leaves.
    assets::LodChainSettings reseeded = settings;
    reseeded.thinning.seed = settings.thinning.seed + 1u;
    const auto c = assets::buildLodChain(source, reseeded);
    REQUIRE(c);
    CHECK(triangles(c->levels[1].mesh) == triangles(a->levels[1].mesh));
    bool anyMoved = false;
    for (std::size_t v = 0; v < c->levels[1].mesh.vertices.size() && !anyMoved; ++v) {
        anyMoved = c->levels[1].mesh.vertices[v].position != a->levels[1].mesh.vertices[v].position;
    }
    CHECK(anyMoved);
}

TEST_CASE("a thinned level reports an error a selector will respect", "[assets][lod][thinning]") {
    // The failure this guards: thinning does not move the bounding box -- a canopy thinned to a
    // fifth still reaches exactly as far -- so `boundsError` says nothing about it, and a level
    // reporting a near-zero error would be taken by the selector while the object filled the
    // screen. The error must instead be of the order of a shell.
    const scene::MeshData source = scatteredShells(400);
    assets::LodChainSettings settings = thinningTestSettings();
    settings.ratios = {1.0f, 0.25f, 0.08f};
    const auto chain = assets::buildLodChain(source, settings);
    REQUIRE(chain);
    REQUIRE(chain->levels[1].thinned);
    REQUIRE(chain->levels[2].thinned);
    // A card of side 1 has an area of 1, so a disc of that area has a radius of about 0.56. The
    // grown cards are larger than that, so the estimate rises with the thinning.
    CHECK(chain->levels[1].error > 0.3f);
    CHECK(chain->levels[2].error > 0.3f);
    // Rising with aggressiveness, which is the property the selector's threshold depends on.
    CHECK(chain->levels[2].error > chain->levels[1].error);
    // The invariant the floor exists to keep: whatever the shell estimate says, the error is never
    // below the measured bounding-box recession either. `error` is the larger of the two claims,
    // always, so a selector thresholding on it is conservative rather than wrong.
    CHECK(chain->levels[1].error >= chain->levels[1].boundsError);
    CHECK(chain->levels[2].error >= chain->levels[2].boundsError);
    // The control. LOD0 is not thinned and reports no error at all, so the numbers above are the
    // thinning's and not a constant this function would print for anything.
    CHECK_FALSE(chain->levels[0].thinned);
    CHECK(chain->levels[0].error == 0.0f);
}

TEST_CASE("the Tree of Life's foliage cannot be simplified and can be thinned", "[assets][lod][thinning]") {
    // The claim the whole mechanism rests on, tested against the geometry it is a claim about,
    // because no synthetic shell in this file reproduces it. Skipped rather than faked when the
    // asset is not in the checkout: assets/treeisle is gitignored.
    const std::filesystem::path path =
        std::filesystem::path(AVGEN_SOURCE_DIR) / "assets" / "treeisle" / "tree-glowmere-foliage.glb";
    if (!std::filesystem::is_regular_file(path)) {
        SKIP("assets/treeisle/tree-glowmere-foliage.glb is not present in this checkout");
    }
    scene::Scene s;
    REQUIRE(assets::loadGltf(path, s, {}));
    REQUIRE_FALSE(s.meshes.empty());
    // The largest part, which is where the argument has to hold.
    const scene::MeshData* biggest = &s.meshes.front();
    for (const scene::MeshData& mesh : s.meshes) {
        if (mesh.indices.size() > biggest->indices.size()) {
            biggest = &mesh;
        }
    }
    const std::uint32_t shells = assets::countShells(*biggest);
    CHECK(shells > 10000);
    // Around eight triangles a leaf. The band is wide because this is a property of the asset and
    // an asset re-export may move it; what must not happen is it quietly becoming large enough that
    // `maxShellTriangles` stops firing and the canopy silently stops having a LOD at all.
    const auto perShell = static_cast<float>(biggest->indices.size() / 3) / static_cast<float>(shells);
    CHECK(perShell > 2.0f);
    CHECK(perShell < 24.0f);

    assets::LodChainSettings preserving = assets::heroLodSettings();
    preserving.ratios = {1.0f, 0.2f};
    const auto refused = assets::buildLodChain(*biggest, preserving);
    REQUIRE(refused);
    // The control, and the reason any of this exists: asked for a fifth, handed back all of it.
    CHECK(refused->levels[1].achievedRatio > 0.95f);
    CHECK_FALSE(refused->levels[1].reachedTarget);

    assets::LodChainSettings thinning = assets::foliageLodSettings();
    thinning.ratios = {1.0f, 0.2f};
    const auto reached = assets::buildLodChain(*biggest, thinning);
    REQUIRE(reached);
    CHECK(reached->levels[1].thinned);
    CHECK(reached->levels[1].achievedRatio <= 0.21f);
    CHECK(reached->levels[1].reachedTarget);
    CHECK(reached->levels[1].shells > 0);
    CHECK(reached->levels[1].shells < shells);
    // Grown to hold the canopy's area, and not past the cap.
    CHECK(reached->levels[1].shellScale > 1.0f);
    CHECK(reached->levels[1].shellScale <= thinning.thinning.maxScale);
    // The silhouette is still roughly the canopy's. Thinning removes leaves from inside the
    // envelope as well as from its edge, and the edge is where the loss shows: measured at 0.110 of
    // the diagonal at a fifth of the leaves, against the 0.12 `boundsTolerance` allows a simplified
    // level. The band is 0.15 rather than 0.12 because this number is not what `boundsTolerance`
    // measures -- a canopy's box is set by its outermost leaf and losing that leaf moves it by a
    // leaf, which is a different event from a trunk going missing -- and pinning it at the measured
    // value would make an asset re-export look like a regression.
    CHECK(boxDrift(*biggest, reached->levels[1].mesh) < 0.15f);
}
