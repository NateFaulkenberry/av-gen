// The imported-asset LOD instruments: what an imported glTF actually contains, and what the
// existing chain builder can do with it.
//
//   avgen_tests "[.analysis][assetlod]" --success
//
// Two questions, both of which had to be answered before any renderer wiring was written, and both
// of which produced a number that contradicts an assumption the work started from.
//
// **What is in the asset?** (§2 of the brief.) Not "how many triangles" -- that number was already
// known -- but how many *vertices*, which nobody had counted. The Tree of Life's foliage layer is a
// glTF whose 22 primitives all reference one shared POSITION accessor, and `convertPrimitive` in
// assets/gltf_loader.cpp copies the accessor per primitive because it has no way to express "a
// slice of a buffer somebody else owns". So an 87 MB file becomes 34.5 million vertices of
// scene::MeshData, of which 3.1 million are distinct. The multiplier is not a property of the tree:
// it is a property of every multi-material glTF this engine imports, and it is 33x on this one.
//
// **Can the chain builder reach the rungs?** assets/mesh_lod.hpp's header warns that a preserving
// simplifier hands back 91% when asked for 7%, because it will not move a vertex where three
// surfaces meet. 122,767 separate leaf instances is that failure mode at its worst. The instrument
// prints the achieved ratio against the requested one for every rung of every layer, so the answer
// is a table rather than a hope.
#include "assets/gltf_loader.hpp"
#include "assets/mesh_lod.hpp"
#include "scene/mesh_metrics.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

const char* const kLayers[] = {"wood", "twigs", "tracery", "foliage", "lumens"};

fs::path layerPath(const std::string& layer) {
    return fs::path(AVGEN_SOURCE_DIR) / "assets" / "treeisle" / ("tree-glowmere-" + layer + ".glb");
}

bool haveLayers() {
    for (const char* layer : kLayers) {
        if (!fs::is_regular_file(layerPath(layer))) {
            return false;
        }
    }
    return true;
}

double millisSince(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

} // namespace

TEST_CASE("what an imported Tree of Life layer contains", "[.analysis][assetlod]") {
    if (!haveLayers()) {
        SKIP("assets/treeisle is not present in this checkout");
    }
    std::printf("\n%-10s %10s %12s %12s %8s %6s %6s %6s %6s %6s\n", "layer", "parts", "triangles",
                "vertices", "MB", "uv", "skin", "unique", "dup", "load");
    std::uint64_t totalTris = 0;
    std::uint64_t totalVerts = 0;
    std::uint64_t totalUnique = 0;
    for (const char* layer : kLayers) {
        scene::Scene s;
        const auto started = std::chrono::steady_clock::now();
        const auto loaded = assets::loadGltf(layerPath(layer), s, {});
        const double loadMs = millisSince(started);
        REQUIRE(loaded);
        std::uint64_t tris = 0;
        std::uint64_t verts = 0;
        std::uint64_t unique = 0;
        bool anyUv = false;
        bool anySkin = false;
        for (const scene::MeshData& mesh : s.meshes) {
            tris += mesh.indices.size() / 3;
            verts += mesh.vertices.size();
            anySkin = anySkin || mesh.skinned();
            for (const scene::Vertex& v : mesh.vertices) {
                anyUv = anyUv || v.uv.x != 0.0f || v.uv.y != 0.0f;
            }
            // What the mesh would carry if the vertices it does not index were dropped. This is
            // exactly what `optimiseMesh`'s vertex-fetch pass produces, so it is a measurement of
            // an available saving rather than a hypothetical one.
            unique += assets::optimiseMesh(mesh).vertices.size();
        }
        totalTris += tris;
        totalVerts += verts;
        totalUnique += unique;
        const double mb =
            static_cast<double>(verts * sizeof(scene::Vertex) + tris * 3 * sizeof(std::uint32_t)) / 1.0e6;
        std::printf("%-10s %10zu %12llu %12llu %8.1f %6s %6s %12llu %5.1fx %5.0f\n", layer,
                    s.meshes.size(), static_cast<unsigned long long>(tris),
                    static_cast<unsigned long long>(verts), mb, anyUv ? "yes" : "no",
                    anySkin ? "yes" : "no", static_cast<unsigned long long>(unique),
                    unique == 0 ? 0.0 : static_cast<double>(verts) / static_cast<double>(unique), loadMs);
        // Everything downstream of this file assumes these three, and an asset re-export that
        // breaks one of them should break here rather than in a frame.
        CHECK(tris > 0);
        CHECK_FALSE(anySkin);
        CHECK(s.rigs.empty());
    }
    std::printf("%-10s %10s %12llu %12llu %8.1f %6s %6s %12llu %5.1fx\n", "TOTAL", "",
                static_cast<unsigned long long>(totalTris), static_cast<unsigned long long>(totalVerts),
                static_cast<double>(totalVerts * sizeof(scene::Vertex)) / 1.0e6, "", "",
                static_cast<unsigned long long>(totalUnique),
                static_cast<double>(totalVerts) / static_cast<double>(totalUnique));
    std::printf("\nvertex buffer as imported %.0f MB, after vertex-fetch compaction %.0f MB\n",
                static_cast<double>(totalVerts * sizeof(scene::Vertex)) / 1.0e6,
                static_cast<double>(totalUnique * sizeof(scene::Vertex)) / 1.0e6);
}

TEST_CASE("what the chain builder achieves on Tree of Life geometry", "[.analysis][assetlod]") {
    if (!haveLayers()) {
        SKIP("assets/treeisle is not present in this checkout");
    }
    assets::LodChainSettings settings = assets::heroLodSettings();
    settings.ratios = {1.0f, 0.5f, 0.2f, 0.07f, 0.02f};
    std::printf("\n%-10s %4s %12s %8s %8s %10s %10s %7s %7s\n", "layer", "rung", "triangles", "want",
                "got", "error", "bounds", "sloppy", "reached");
    for (const char* layer : kLayers) {
        scene::Scene s;
        REQUIRE(assets::loadGltf(layerPath(layer), s, {}));
        // One part per material, which is how the renderer draws it: the chain is per mesh, so the
        // instrument has to be per mesh too. Printing only the largest part of each layer keeps the
        // table readable; the aggregate ratio below is over all of them.
        std::size_t biggest = 0;
        for (std::size_t i = 1; i < s.meshes.size(); ++i) {
            if (s.meshes[i].indices.size() > s.meshes[biggest].indices.size()) {
                biggest = i;
            }
        }
        std::uint64_t sourceTotal = 0;
        std::vector<std::uint64_t> rungTotals(settings.ratios.size(), 0);
        double buildMs = 0.0;
        for (std::size_t i = 0; i < s.meshes.size(); ++i) {
            const auto started = std::chrono::steady_clock::now();
            const auto chain = assets::buildLodChain(s.meshes[i], settings);
            buildMs += millisSince(started);
            REQUIRE(chain);
            sourceTotal += chain->sourceTriangles;
            for (std::size_t r = 0; r < chain->levels.size() && r < rungTotals.size(); ++r) {
                rungTotals[r] += chain->levels[r].mesh.indices.size() / 3;
            }
            if (i != biggest) {
                continue;
            }
            for (std::size_t r = 0; r < chain->levels.size(); ++r) {
                const assets::LodLevel& level = chain->levels[r];
                std::printf("%-10s %4zu %12zu %8.3f %8.3f %10.4f %10.4f %7s %7s\n", layer, r,
                            level.mesh.indices.size() / 3, static_cast<double>(level.targetRatio),
                            static_cast<double>(level.achievedRatio), static_cast<double>(level.error),
                            static_cast<double>(level.boundsError), level.sloppy ? "yes" : "",
                            level.reachedTarget ? "yes" : "NO");
            }
        }
        std::printf("%-10s  all %12llu", layer, static_cast<unsigned long long>(sourceTotal));
        for (std::size_t r = 0; r < rungTotals.size(); ++r) {
            std::printf("  %5.3f", static_cast<double>(rungTotals[r]) / static_cast<double>(sourceTotal));
        }
        std::printf("   (%.0f ms to build)\n", buildMs);
    }
}
