// The LOD / Geometry Lab's device-free half: the ladder's arithmetic, and the instruments that
// measure what its thresholds mean on production geometry.
//
// Everything here is either an invariant with a control, or an instrument hidden behind `[.analysis]`
// that prints a table. Nothing here re-derives a rung from the inputs the shader uses and then
// checks it against itself: that is what `test_triangle_size_analysis.cpp` and
// `test_representation_band_analysis.cpp` do, deliberately and with a note saying so, and it is why
// the rung assertions live in `tests/rendering/test_lod_gpu.cpp` against drawn pixels instead.
//
// The two instruments:
//
//   avgen_tests "[.analysis][lod]" --success
//
// answer the two questions the threshold investigation turned on. **What does 28 px mean?** -- what
// size, on screen, an object of each production layer actually is when the ladder takes its full
// mesh away. And **what does a rung draw?** -- the bounding box of each level of the LOD chain
// against the source's, which is where the ladder's bottom rung is caught losing the trunk of a
// tree while reporting an error five times smaller than the deviation it has.
#include "assets/gltf_loader.hpp"
#include "assets/mesh_lod.hpp"
#include "rendering/visibility.hpp"
#include "scene/camera.hpp"
#include "scene/procedural.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

// Glowmere's thirteen scatter layers, as `src/scene/composition.cpp` builds them from
// `examples/world/glowmere-valley-2*.scene.json`: the asset, and the height the layer normalises it
// to. The ladder is the same 28 / 11 / 4 px for every one of them, which is the point.
struct Layer {
    const char* name;
    const char* asset;
    float height;
};
const std::vector<Layer>& glowmereLayers() {
    static const std::vector<Layer> layers{
        {"canopy", "CommonTree_1", 14.0f},          {"pines", "TwistedTree_2", 15.0f},
        {"deadwood", "DeadTree_1", 12.0f},          {"bushes", "Bush_Common", 1.1f},
        {"ferns", "Fern_1", 1.4f},                  {"grass", "Grass_Common_Short", 0.7f},
        {"fan-plants", "Plant_1_Big", 3.2f},        {"flowers", "Flower_3_Group", 0.55f},
        {"fungi", "Mushroom_Common", 0.28f},        {"shelf-fungi", "Mushroom_Laetiporus", 0.55f},
        {"boulders", "Rock_Medium_1", 1.6f},        {"pebbles", "Pebble_Round_2", 0.28f},
        {"beacons", "Mushroom_Common", 1.5f},
    };
    return layers;
}

struct Asset {
    std::vector<glm::vec3> vertices;
    scene::MeshData mesh;
    glm::vec3 lo{0.0f};
    glm::vec3 hi{0.0f};
    bool valid = false;
};

Asset loadAsset(const std::string& name) {
    Asset out;
    const fs::path path = fs::path(AVGEN_SOURCE_DIR) / "assets" / "quaternius" / "glTF" / (name + ".gltf");
    if (!fs::is_regular_file(path)) {
        return out;
    }
    scene::Scene s;
    if (!assets::loadGltf(path, s, {}) || s.meshes.empty()) {
        return out;
    }
    for (const scene::MeshData& part : s.meshes) {
        const auto base = static_cast<std::uint32_t>(out.mesh.vertices.size());
        out.mesh.vertices.insert(out.mesh.vertices.end(), part.vertices.begin(), part.vertices.end());
        for (const std::uint32_t index : part.indices) {
            out.mesh.indices.push_back(base + index);
        }
        for (const scene::Vertex& v : part.vertices) {
            out.vertices.push_back(v.position);
        }
    }
    if (out.vertices.empty()) {
        return out;
    }
    const auto [lo, hi] = out.mesh.bounds();
    out.lo = lo;
    out.hi = hi;
    out.valid = true;
    return out;
}

// What the renderer's mesh cache used to hand the cull pass, and still hands it as `CachedMesh
// ::radius`: half the AABB's diagonal, the radius of the tight sphere about the box's *centre*.
float halfDiagonal(const glm::vec3& lo, const glm::vec3& hi) {
    return std::max(0.5f * glm::length(hi - lo), 1e-4f);
}

// The projected silhouette of real geometry: every vertex through a real view-projection, in
// pixels. Not a bounding rule -- the vertices themselves, which is what the rasteriser is handed.
struct Projected {
    float radius = 0.0f; // half the larger dimension of the pixel bounding box
    float height = 0.0f;
    float width = 0.0f;
};

Projected projectedSilhouette(const Asset& asset, float sourceScale, float distance, std::uint32_t width,
                              std::uint32_t height, float fovY) {
    scene::Camera cam;
    cam.fovYRadians = fovY;
    cam.lens.useExplicitFov = true;
    cam.nearPlane = 0.1f;
    cam.farPlane = 1.0e5f;
    const float mid = 0.5f * (asset.hi.y + asset.lo.y) * sourceScale;
    cam.position = {0.0f, mid, distance};
    cam.target = {0.0f, mid, 0.0f};
    const glm::mat4 vp =
        cam.projection(static_cast<float>(width) / static_cast<float>(height)) * cam.view();
    glm::vec2 lo(std::numeric_limits<float>::max());
    glm::vec2 hi(std::numeric_limits<float>::lowest());
    for (const glm::vec3& v : asset.vertices) {
        const glm::vec4 c = vp * glm::vec4(v * sourceScale, 1.0f);
        if (c.w <= 1e-6f) {
            continue;
        }
        const glm::vec2 px(0.5f * (c.x / c.w + 1.0f) * static_cast<float>(width),
                           0.5f * (1.0f - c.y / c.w) * static_cast<float>(height));
        lo = glm::min(lo, px);
        hi = glm::max(hi, px);
    }
    Projected out;
    out.width = hi.x - lo.x;
    out.height = hi.y - lo.y;
    out.radius = 0.5f * std::max(out.width, out.height);
    return out;
}

} // namespace

// -------------------------------------------------------------------------------------------------
TEST_CASE("The ladder's size measure is a bound on what is drawn, and the two radius rules are not "
          "the same bound",
          "[lod][ladder]") {
    // The invariant that makes the rest of the investigation meaningful, and its control.
    //
    // shaders/cull.wgsl forms one sphere, centred on the instance **record position**, and uses its
    // projected radius for two different decisions: whether to keep the instance at all, and which
    // rung to draw it at. A sphere about that centre has to reach the furthest corner of the source
    // from the *origin*, which is `sourceCullRadius`. The half-diagonal -- the radius the mesh cache
    // handed it until the Visibility Lab's fix -- is the radius about the box's centre, and about
    // the origin it does not contain the geometry at all.
    //
    // Both halves are asserted, because the two are the same number for centred geometry and it is
    // exactly that coincidence which hid the last bug (ADR-182).
    const Asset tree = loadAsset("CommonTree_1");
    const Asset rock = loadAsset("Rock_Medium_1");
    if (!tree.valid || !rock.valid) {
        SKIP("assets/quaternius is not present in this checkout");
    }

    SECTION("the cull radius contains the source about its own origin and the half-diagonal does not") {
        float furthest = 0.0f;
        for (const glm::vec3& v : tree.vertices) {
            furthest = std::max(furthest, glm::length(v));
        }
        CHECK(furthest <= rendering::sourceCullRadius(tree.lo, tree.hi));
        CHECK(furthest > halfDiagonal(tree.lo, tree.hi));
    }

    SECTION("and the gap between them is an authoring accident, not a size") {
        // It is the offset of the geometry from its own origin, so it varies from layer to layer
        // by a factor the ladder cannot mean anything by. Both arms: a layer where the two rules
        // nearly agree, and one where they differ by three quarters.
        float lowest = std::numeric_limits<float>::max();
        float highest = 0.0f;
        std::string lowestName;
        std::string highestName;
        for (const Layer& layer : glowmereLayers()) {
            const Asset a = loadAsset(layer.asset);
            if (!a.valid) {
                continue;
            }
            const float ratio = rendering::sourceCullRadius(a.lo, a.hi) / halfDiagonal(a.lo, a.hi);
            if (ratio < lowest) {
                lowest = ratio;
                lowestName = layer.name;
            }
            if (ratio > highest) {
                highest = ratio;
                highestName = layer.name;
            }
        }
        INFO("the two rules differ least on '" << lowestName << "' (" << lowest << "x) and most on '"
                                               << highestName << "' (" << highest << "x)");
        REQUIRE(highest > 0.0f);
        CHECK(lowest < 1.15f);  // ferns: near enough the same number
        CHECK(highest > 1.60f); // grass and the trees: three quarters as much again
    }
}

// -------------------------------------------------------------------------------------------------
TEST_CASE("A level range is a proof: it never omits a rung a record is on", "[lod][ladder]") {
    // `rendering::objectLevelRange` is what lets the renderer stop recording an indirect draw for a
    // level, and it replaced a rule that guessed from a stale readback and was wrong at every rung
    // change. A proof that is wrong loses geometry, so this is the case that has to have a control:
    // the range is checked against the rung `cullLodLevel` gives **every record**, over cameras that
    // reach every rung, and the sweep asserts that both a narrow range and a wide one occurred.
    scene::LodSettings lod;
    lod.cull = true;
    lod.lodCount = scene::kMaxLodLevels;
    lod.lodByScreenSize = true;
    lod.lodDistances[0] = 28.0f;
    lod.lodDistances[1] = 11.0f;
    lod.lodDistances[2] = 4.0f;
    lod.lodSpread = 0.0f;
    lod.lodHysteresis = 0.0f;

    // A cloud that is deliberately not a centred box (the shape that hid the last bug): records
    // spread over 300 m of depth with scales from 0.35 to 2.4, which is Glowmere's own `scaled`
    // range.
    std::vector<scene::InstanceRecord> records;
    for (int i = 0; i < 64; ++i) {
        scene::InstanceRecord r{};
        const float u = static_cast<float>(i) / 63.0f;
        r.position = glm::vec4(-40.0f + 80.0f * u, 0.0f, -300.0f * u, 1.0f);
        const float s = 0.35f + 2.05f * std::fabs(std::sin(static_cast<float>(i) * 1.7f));
        r.scale = {s, s, s, 0.0f};
        r.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
        records.push_back(r);
    }
    const rendering::InstanceBounds bounds = rendering::instanceBounds(records);
    REQUIRE(bounds.valid);
    CHECK(bounds.minAbsScale < bounds.maxAbsScale);

    const float sourceRadius = 14.885f; // CommonTree_1 normalised to 14 m, about its own origin
    int narrow = 0;
    int wide = 0;
    int checked = 0;
    for (const float back : {30.0f, 90.0f, 240.0f, 700.0f, 1800.0f}) {
        for (const float side : {0.0f, 120.0f}) {
            rendering::CullCamera camera;
            camera.position = {side, 18.0f, back};
            camera.projScale = rendering::cullProjScale(glm::radians(50.0f), 720);
            const glm::mat4 identity(1.0f);
            const rendering::LevelRange range =
                rendering::objectLevelRange(lod, camera, identity, bounds, sourceRadius, 1.0f, 1.0f, false);
            (range.highest - range.lowest <= 1 ? narrow : wide) += 1;
            // Every record's own rung has to be inside the range. `cullLodLevel` is the function the
            // GPU is pinned against (tests/rendering/test_culling_gpu.cpp), so this compares the
            // proof with the decision rather than with a second copy of itself.
            const scene::Camera dummy;
            const rendering::FrustumPlanes planes =
                rendering::frustumPlanes(glm::mat4(1.0f)); // unused: the ladder does not read them
            for (const scene::InstanceRecord& r : records) {
                const float radius = sourceRadius * std::max({std::fabs(r.scale.x), std::fabs(r.scale.y),
                                                              std::fabs(r.scale.z)});
                scene::LodSettings noCull = lod;
                noCull.cull = false; // the rung, with the rejection tests out of the way
                const int level =
                    rendering::cullLodLevel(noCull, planes, camera, glm::vec3(r.position), radius);
                INFO("camera (" << side << ", 18, " << back << "): a record at " << r.position.z
                                << " m, scale " << r.scale.x << ", is on rung " << level
                                << " and the range is " << range.lowest << ".." << range.highest);
                REQUIRE(range.contains(level));
                ++checked;
            }
            (void)dummy;
        }
    }
    // Both arms: a range that pinned the population to one or two rungs, and one that could not.
    // Without these the proof could be "every level, always" and would pass vacuously.
    INFO(narrow << " cameras gave a range of one or two rungs, " << wide << " gave a wider one");
    CHECK(checked == 64 * 10);
    CHECK(narrow > 0);
    CHECK(wide > 0);
}

// -------------------------------------------------------------------------------------------------
TEST_CASE("A generated primitive's far rungs are impostors and an imported mesh's are not",
          "[lod][ladder]") {
    // The distinction the renderer has to pick a vertex path by. It used to pick by level index,
    // which was right until ADR-085 gave a Mesh source a simplified mesh at every level -- after
    // which rungs 2 and 3 of every scatter layer were drawn through the impostor path: their
    // vertices taken as offsets in the camera's basis, at the asset's authored size instead of the
    // layer's, centred on the instance record.
    //
    // Both arms, because a predicate that answered "never an impostor" would remove the pop and
    // silently disable the billboard levels the primitive sources still use.
    scene::SourceSpec cylinder;
    cylinder.kind = scene::PrimitiveKind::Cylinder;
    cylinder.radius = 0.5f;
    cylinder.height = 2.0f;
    cylinder.radialSegments = 24;
    for (int level = 0; level <= 3; ++level) {
        CHECK(scene::lodLevelIsImpostor(cylinder, level) == (level >= 2));
    }

    const Asset tree = loadAsset("CommonTree_1");
    if (!tree.valid) {
        SKIP("assets/quaternius is not present in this checkout");
    }
    scene::SourceSpec mesh;
    mesh.kind = scene::PrimitiveKind::Mesh;
    mesh.asset = "CommonTree_1.gltf";
    mesh.assetMesh = std::make_shared<scene::MeshData>(tree.mesh);
    for (int level = 0; level <= 3; ++level) {
        CHECK_FALSE(scene::lodLevelIsImpostor(mesh, level));
    }

    // And the predicate agrees with what `makeLodMesh` actually builds, which is the only thing
    // that makes it worth asking. An impostor is two triangles; a simplified tree is not.
    for (int level = 2; level <= 3; ++level) {
        auto quad = scene::makeLodMesh(cylinder, level);
        REQUIRE(quad.has_value());
        CHECK(quad->indices.size() == 6);
        auto reduced = scene::makeLodMesh(mesh, level);
        REQUIRE(reduced.has_value());
        CHECK(reduced->indices.size() > 6);
    }
}

// -------------------------------------------------------------------------------------------------
// The instruments. Hidden behind `.` because they load and simplify every production asset.
// -------------------------------------------------------------------------------------------------
TEST_CASE("What 28 px means, per production layer", "[.analysis][lod]") {
    // The threshold question, in the only terms that answer it: for each of Glowmere's thirteen
    // scatter layers, how big the thing actually is on screen at the moment the ladder takes its
    // full mesh away -- under the radius the cull uses now, and under the one the ladder was
    // authored against.
    //
    // The silhouette is measured from the asset's own vertices through a real view-projection, at a
    // distance far enough that perspective within the object is negligible, so the number is a fact
    // about the geometry and not about either rule.
    constexpr std::uint32_t kW = 1280;
    constexpr std::uint32_t kH = 800;
    const float fovY = glm::radians(50.0f);
    const float projScale = rendering::cullProjScale(fovY, kH);
    std::printf("\n%-12s %8s %8s %6s | %9s %9s | %9s %9s\n", "layer", "tightR", "originR", "ratio",
                "silo/tight", "px @ 28", "silo/orig", "px @ 28");
    float tightLo = 1e30f, tightHi = 0.0f, originLo = 1e30f, originHi = 0.0f;
    for (const Layer& layer : glowmereLayers()) {
        const Asset a = loadAsset(layer.asset);
        if (!a.valid) {
            continue;
        }
        const float sourceScale = layer.height / std::max(a.hi.y - a.lo.y, 1e-4f);
        const float tight = halfDiagonal(a.lo, a.hi) * sourceScale;
        const float origin = rendering::sourceCullRadius(a.lo, a.hi) * sourceScale;
        const float distance = layer.height * 200.0f;
        const Projected p = projectedSilhouette(a, sourceScale, distance, kW, kH, fovY);
        // The cull's sphere is centred on the record, so its projected radius at this distance is
        // radius / distance * projScale. Divided by the silhouette it is how loose the bound is.
        const float tightRatio = (tight / distance * projScale) / std::max(p.radius, 1e-4f);
        const float originRatio = (origin / distance * projScale) / std::max(p.radius, 1e-4f);
        tightLo = std::min(tightLo, tightRatio);
        tightHi = std::max(tightHi, tightRatio);
        originLo = std::min(originLo, originRatio);
        originHi = std::max(originHi, originRatio);
        std::printf("%-12s %8.3f %8.3f %6.3f | %9.2f %9.1f | %9.2f %9.1f\n", layer.name, tight, origin,
                    origin / tight, tightRatio, 28.0f / tightRatio, originRatio, 28.0f / originRatio);
    }
    std::printf("\n  the threshold '28 px' is a drawn radius of %.1f..%.1f px under the half-diagonal "
                "(%.2fx spread)\n  and %.1f..%.1f px under the origin-centred radius (%.2fx spread)\n",
                28.0f / tightHi, 28.0f / tightLo, tightHi / tightLo, 28.0f / originHi, 28.0f / originLo,
                originHi / originLo);
    CHECK(tightHi > tightLo);
}

TEST_CASE("What each rung of the chain draws, against the source", "[.analysis][lod]") {
    // `assets/mesh_lod.hpp` says the simplifier's reported error "rises monotonically with
    // aggressiveness and never understates". This prints the reported error beside a deviation that
    // cannot be argued with -- how far the level's bounding box has receded from the source's --
    // because a level that has lost a third of the object's height while reporting six per cent is
    // a level a selector choosing by projected error would choose far too early.
    //
    // Both arms, in one process: `boundsTolerance = 0` is the chain this code built before the
    // guard existed, and the default is the chain it builds now. Two columns of triangles, so what
    // the guard costs is a number on this page rather than a claim about one.
    assets::LodChainSettings guarded = assets::vegetationLodSettings();
    assets::LodChainSettings unguarded = guarded;
    unguarded.boundsTolerance = 0.0f;

    std::printf("\n%-20s %4s | %8s %8s %8s %7s | %8s %8s %8s %7s | %s\n", "asset", "lod",
                "tris", "relErr", "bnd/dia", "height", "tris", "relErr", "bnd/dia", "height", "guard");
    std::printf("%-20s %4s | %35s | %35s |\n", "", "", "  ---- before the guard ----",
                "  ---- with the guard ----");
    std::size_t fired = 0;
    std::size_t understated = 0;
    long long trisBefore = 0;
    long long trisAfter = 0;
    for (const Layer& layer : glowmereLayers()) {
        const Asset a = loadAsset(layer.asset);
        if (!a.valid) {
            continue;
        }
        auto before = assets::buildLodChain(a.mesh, unguarded);
        auto after = assets::buildLodChain(a.mesh, guarded);
        if (!before || !after) {
            continue;
        }
        const float sourceHeight = std::max(a.hi.y - a.lo.y, 1e-6f);
        const float diagonal = std::max(glm::length(a.hi - a.lo), 1e-6f);
        const auto recession = [&](const scene::MeshData& m) {
            const auto [lo, hi] = m.bounds();
            float worst = 0.0f;
            for (int axis = 0; axis < 3; ++axis) {
                worst = std::max(worst, lo[axis] - a.lo[axis]);
                worst = std::max(worst, a.hi[axis] - hi[axis]);
            }
            return worst;
        };
        for (std::size_t level = 1; level < after->levels.size(); ++level) {
            const assets::LodLevel& B = before->levels[level];
            const assets::LodLevel& A = after->levels[level];
            const auto [blo, bhi] = B.mesh.bounds();
            const auto [alo, ahi] = A.mesh.bounds();
            const bool guardFired = B.mesh.indices.size() != A.mesh.indices.size();
            fired += guardFired ? 1 : 0;
            // The claim the header makes, checked against the deviation that cannot be argued with.
            understated += recession(B.mesh) > B.error * 1.001f ? 1 : 0;
            trisBefore += static_cast<long long>(B.mesh.indices.size() / 3);
            trisAfter += static_cast<long long>(A.mesh.indices.size() / 3);
            std::printf("%-20s %4zu | %8zu %8.4f %8.4f %6.1f%% | %8zu %8.4f %8.4f %6.1f%% | %s\n",
                        layer.asset, level, B.mesh.indices.size() / 3,
                        static_cast<double>(B.relativeError), static_cast<double>(recession(B.mesh) / diagonal),
                        100.0 * static_cast<double>((bhi.y - blo.y) / sourceHeight),
                        A.mesh.indices.size() / 3, static_cast<double>(A.relativeError),
                        static_cast<double>(A.boundsError / diagonal),
                        100.0 * static_cast<double>((ahi.y - alo.y) / sourceHeight),
                        guardFired ? "fired" : "");
        }
    }
    std::printf("\n  the guard replaced %zu of the rungs printed above; %zu of the unguarded levels\n"
                "  deviate further than the error they report. Triangles over every rung below 0:\n"
                "  %lld before, %lld after (%+.1f%%)\n",
                fired, understated, trisBefore, trisAfter,
                trisBefore > 0 ? 100.0 * static_cast<double>(trisAfter - trisBefore) /
                                     static_cast<double>(trisBefore)
                               : 0.0);
    // Not `CHECK(true)`: an instrument whose only assertion cannot fail is the thing ADR-182 is
    // about. The chains have to have been built for any of the above to mean anything.
    CHECK(trisBefore > 0);
    CHECK(trisAfter > 0);
}
