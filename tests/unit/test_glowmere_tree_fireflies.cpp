// Fireflies on one tree in three in `glowmere-valley-2-multicam`, as two representations of one
// effect: orbiting swarms near the camera (the `tree-fireflies` particle node, scene::ScatterAnchor)
// and firefly points in the foliage further out (the glowmereFireflies* material programs).
//
// What is checked, and why each is a thing that could be quietly wrong:
//
//   1. The fraction. "One in three" is the owner's number, and it is produced by two gates in
//      series -- instanceRandom.w below 0.3876, and a specimen `emissiveSparsity` left lit -- so it
//      is measured on the forest the film actually grows, not argued from the constants.
//   2. The material program lights exactly the swarm trees and does NOTHING to any other tree:
//      two trees in three must look as they did before this existed. That is checked bit for bit
//      on the CPU reference of the interpreter, over every instance and real leaf vertices.
//   3. No firefly on a trunk: bark below the lowest leaf is left alone on every tree.
//   4. The swarm centres are on real trees and the per-frame table is a pure function of the
//      camera, and the film does bring the camera within reach of them.
//   5. `scatterAnchor` survives a save, and the configurations that would silently empty the
//      system are refused.
//
// Every count is taken on a forest that is required to exist first. A world whose trees failed to
// load has no instances, and "none of the zero trees has fireflies on its trunk" is true of it --
// which is how an empty world passes a test written against a full one.
//
// GPU-free: `EngineMode::Offline` builds no device; the program is evaluated by the CPU reference
// `shaders/material.wgsl` is a transliteration of.

#include "app/engine.hpp"
#include "assets/asset_registry.hpp"
#include "scene/composition.hpp"
#include "scene/material_program.hpp"
#include "scene/particles.hpp"
#include "scene/scatter_anchors.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <glm/gtc/quaternion.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

fs::path filmProject() {
    return fs::path(AVGEN_SOURCE_DIR) / "examples" / "world" / "glowmere-valley-2-multicam.json";
}

const std::array<const char*, 8> kTreeLayers{{"canopy", "canopy-broad", "twisted", "twisted-low", "pine-upper",
                                              "pine-rim", "deadwood", "deadwood-rim"}};

// The film, loaded the way an offline render loads it (the project over its scene, ADR-264), and
// stepped once so every procedural object has built its instances.
struct Film {
    std::unique_ptr<app::Engine> engine;
    scene::Composition* comp = nullptr;
};

Film loadFilm() {
    Film f;
    f.engine = std::make_unique<app::Engine>(app::EngineMode::Offline);
    auto loaded = f.engine->loadProject(filmProject());
    INFO((loaded.has_value() ? std::string() : loaded.error().message));
    REQUIRE(loaded.has_value());
    f.comp = f.engine->composition();
    REQUIRE(f.comp != nullptr);
    FrameTime t;
    t.renderTime = 0.0;
    t.deltaTime = 1.0 / 60.0;
    f.engine->update(t);
    return f;
}

const scene::ParticleSystem* fireflies(const scene::Scene& s) {
    for (const scene::ParticleSystem& ps : s.particles) {
        if (ps.name == "tree-fireflies") {
            return &ps;
        }
    }
    return nullptr;
}

const scene::ProceduralGeometry* object(const scene::Scene& s, const std::string& name) {
    for (const scene::ProceduralGeometry& g : s.procedurals) {
        if (g.name == name) {
            return &g;
        }
    }
    return nullptr;
}

std::vector<const scene::ProceduralGeometry*> parts(const scene::Scene& s, const scene::ProceduralGeometry& lead) {
    std::vector<const scene::ProceduralGeometry*> out{&lead};
    for (const scene::ProceduralGeometry& g : s.procedurals) {
        if (g.partOf == lead.name) {
            out.push_back(&g);
        }
    }
    return out;
}

// Up to `n` vertices of a part's own mesh, evenly strided -- real surface points, in the asset's
// own units, which is the space `localPosition` is in.
std::vector<glm::vec3> vertices(const scene::ProceduralGeometry& part, std::size_t n) {
    std::vector<glm::vec3> out;
    if (!part.source.assetMesh) {
        return out;
    }
    const auto& v = part.source.assetMesh->vertices;
    const std::size_t stride = std::max<std::size_t>(1, v.size() / n);
    for (std::size_t i = 0; i < v.size(); i += stride) {
        out.push_back(v[i].position);
    }
    return out;
}

bool lit(const spatial::InstanceRecord& r) {
    return r.emissive.x > 0.0f || r.emissive.y > 0.0f || r.emissive.z > 0.0f;
}

scene::MaterialResult treeBase() {
    scene::MaterialResult base;
    base.baseColor = glm::vec3(0.21f, 0.33f, 0.17f);
    base.roughness = 0.8f;
    base.emission = glm::vec3(1.0f, 0.82f, 0.45f) * 0.035f; // the layers' own glow
    return base;
}

scene::MaterialContext contextFor(const spatial::InstanceRecord& r, const glm::vec3& local, float time) {
    scene::MaterialContext ctx;
    ctx.localPosition = local;
    ctx.worldPosition = glm::vec3(r.position) + local;
    ctx.instanceRandom = r.random;
    ctx.instanceEmissive = glm::vec4(glm::vec3(r.emissive), 0.0f);
    ctx.time = time;
    ctx.depth = 100.0f; // inside the band where the points are at full strength
    return ctx;
}

bool sameSurface(const scene::MaterialResult& a, const scene::MaterialResult& b) {
    return a.emission == b.emission && a.baseColor == b.baseColor && a.metallic == b.metallic &&
           a.roughness == b.roughness && a.opacity == b.opacity && a.normal == b.normal &&
           a.occlusion == b.occlusion;
}

} // namespace

TEST_CASE("Fireflies land on one tree in three of the multicam's forest", "[glowmere][multicam][fireflies]") {
    Film film = loadFilm();
    const scene::Scene& s = film.comp->scene();
    const scene::ParticleSystem* ps = fireflies(s);
    REQUIRE(ps != nullptr);
    REQUIRE(ps->enabled);
    REQUIRE(ps->scatterAnchor.active());

    // The forest has to be there before anything is counted on it: every tree layer is read, and
    // each is a real stand, not a handful.
    std::size_t trees = 0;
    for (const char* layer : kTreeLayers) {
        INFO("layer " << layer);
        REQUIRE(std::find(ps->scatterAnchor.layers.begin(), ps->scatterAnchor.layers.end(), layer) !=
                ps->scatterAnchor.layers.end());
        const scene::ProceduralGeometry* g = object(s, scene::scatterObjectName("valley", layer));
        REQUIRE(g != nullptr);
        CHECK(g->instances.size() >= 100);
        trees += g->instances.size();
    }
    REQUIRE(trees >= 2000);

    const scene::ScatterAnchorSet set = scene::scatterAnchorPoints(s.procedurals, ps->scatterAnchor);
    CHECK(set.missing.empty());
    REQUIRE(set.considered == trees);
    const double fraction = static_cast<double>(set.points.size()) / static_cast<double>(set.considered);
    const double litShare = static_cast<double>(set.lit) / static_cast<double>(set.considered);
    std::printf("tree fireflies: %zu of %zu trees (%.2f%%), %zu lit (%.2f%%)\n", set.points.size(),
                set.considered, 100.0 * fraction, set.lit, 100.0 * litShare);
    // One in three. The binomial standard deviation over ~2,900 trees is under 0.9 points, so a
    // band of +-3 points is more than three sigma and still excludes a quarter and a half.
    CHECK(fraction > 0.3033);
    CHECK(fraction < 0.3633);
    // And the glow the other trees keep is the glow they had: emissiveSparsity 0.14 leaves 86% lit.
    CHECK(litShare > 0.83);
    CHECK(litShare < 0.89);
}

TEST_CASE("The firefly material lights exactly the swarm trees and leaves every other tree as it was",
          "[glowmere][multicam][fireflies]") {
    Film film = loadFilm();
    const scene::Scene& s = film.comp->scene();
    const scene::ParticleSystem* ps = fireflies(s);
    REQUIRE(ps != nullptr);
    const scene::MaterialResult base = treeBase();

    std::size_t instances = 0;
    std::size_t carriers = 0;
    std::size_t leafSamples = 0;
    std::size_t trunkSamples = 0;
    for (const char* layer : kTreeLayers) {
        INFO("layer " << layer);
        const scene::ProceduralGeometry* lead = object(s, scene::scatterObjectName("valley", layer));
        REQUIRE(lead != nullptr);
        REQUIRE_FALSE(lead->instances.empty());
        // The program is attached, it resolves, and it is inside the GPU's table -- a program past
        // the eighth is dropped with one log line and the layer renders with none at all.
        REQUIRE_FALSE(lead->material.program.empty());
        const auto found = std::find_if(s.materialPrograms.begin(), s.materialPrograms.end(),
                                        [&](const scene::MaterialProgram& p) { return p.name == lead->material.program; });
        REQUIRE(found != s.materialPrograms.end());
        // rendering::kMaxGpuMaterialPrograms, which this CPU suite cannot include (it pulls in
        // webgpu); shaders/material.wgsl's MAT_MAX_PROGRAMS says the same 8.
        CHECK(found - s.materialPrograms.begin() < 8);
        const scene::MaterialProgram& program = *found;
        REQUIRE(program.validate().has_value());

        const std::vector<const scene::ProceduralGeometry*> ps2 = parts(s, *lead);
        for (const scene::ProceduralGeometry* p : ps2) {
            CHECK(p->material.program == lead->material.program); // bark and leaves alike
        }
        // Where the leaves are, and the bark below the lowest of them. A tree with one part has no
        // leaves; its whole mesh is sampled and the trunk check does not apply.
        // The leaves are the part that does not reach the ground: of the parts, the one whose lowest
        // vertex is highest.
        const auto lowest = [](const scene::ProceduralGeometry& p) {
            float y = std::numeric_limits<float>::max();
            for (const auto& v : p.source.assetMesh->vertices) {
                y = std::min(y, v.position.y);
            }
            return y;
        };
        const scene::ProceduralGeometry* leaves = ps2.front();
        const scene::ProceduralGeometry* bark = ps2.front();
        for (const scene::ProceduralGeometry* p : ps2) {
            REQUIRE(p->source.assetMesh);
            if (lowest(*p) > lowest(*leaves)) {
                leaves = p;
            }
            if (lowest(*p) < lowest(*bark)) {
                bark = p;
            }
        }
        std::vector<glm::vec3> crown = vertices(*leaves, 512);
        REQUIRE(crown.size() >= 64);
        const float lowestLeaf = lowest(*leaves);
        std::vector<glm::vec3> trunk;
        if (ps2.size() > 1) {
            REQUIRE(leaves != bark);
            for (const glm::vec3& v : vertices(*bark, 4096)) {
                if (v.y < lowestLeaf) {
                    trunk.push_back(v);
                }
            }
            REQUIRE(trunk.size() >= 16);
        }

        for (const spatial::InstanceRecord& r : lead->instances) {
            ++instances;
            bool carries = false;
            for (std::size_t k = 0; k < crown.size(); ++k) {
                const float time = 0.37f * static_cast<float>(k % 7);
                const scene::MaterialResult out =
                    scene::evaluateMaterialProgram(program, contextFor(r, crown[k], time), base);
                ++leafSamples;
                if (!sameSurface(out, base)) {
                    carries = true;
                    // Nothing but the emission may move, even on a carrier.
                    REQUIRE(out.baseColor == base.baseColor);
                    REQUIRE(out.roughness == base.roughness);
                    REQUIRE(out.normal == base.normal);
                    REQUIRE(out.occlusion == base.occlusion);
                }
            }
            for (const glm::vec3& v : trunk) {
                ++trunkSamples;
                const scene::MaterialResult out = scene::evaluateMaterialProgram(program, contextFor(r, v, 0.0f), base);
                INFO("trunk vertex y " << v.y << " below the lowest leaf " << lowestLeaf);
                REQUIRE(sameSurface(out, base));
            }
            // Outside the distance band the program is not run at all, carrier or not.
            if (!crown.empty()) {
                scene::MaterialContext nearCtx = contextFor(r, crown.front(), 0.0f);
                nearCtx.depth = 10.0f;
                scene::MaterialContext farCtx = nearCtx;
                farCtx.depth = 360.0f;
                REQUIRE(sameSurface(scene::evaluateMaterialProgram(program, nearCtx, base), base));
                REQUIRE(sameSurface(scene::evaluateMaterialProgram(program, farCtx, base), base));
            }
            // The material's trees are the swarm's trees: the same lane, the same threshold. The
            // swarm additionally skips a specimen emissiveSparsity darkened, and on those the
            // renderer multiplies the program's emission by the zero instance emissive anyway.
            const bool gate = r.random.w < ps->scatterAnchor.randomBelow;
            INFO("instanceRandom.w " << r.random.w);
            REQUIRE(carries == gate);
            REQUIRE(scene::scatterAnchorGate(r, ps->scatterAnchor) == (carries && lit(r)));
            carriers += carries ? 1 : 0;
        }
    }
    std::printf("firefly material: %zu of %zu instances carry points; %zu leaf and %zu trunk samples\n", carriers,
                instances, leafSamples, trunkSamples);
    REQUIRE(instances >= 2000);
    CHECK(carriers > instances / 4);
    CHECK(carriers < instances / 2);
}

TEST_CASE("Swarm centres sit in real crowns, and the table is a function of the camera alone",
          "[glowmere][multicam][fireflies]") {
    Film film = loadFilm();
    const scene::Scene& s = film.comp->scene();
    const scene::ParticleSystem* ps = fireflies(s);
    REQUIRE(ps != nullptr);
    const scene::ScatterAnchorSet set = scene::scatterAnchorPoints(s.procedurals, ps->scatterAnchor);
    REQUIRE(set.points.size() >= 600);

    // Every centre is up in its own tree: above the root by a sensible share of the tree and not
    // off to one side of it by more than a crown's width.
    for (const scene::ScatterAnchorPoint& p : set.points) {
        const std::size_t layer = static_cast<std::size_t>(p.key >> 32);
        const std::size_t index = static_cast<std::size_t>(p.key & 0xFFFFFFFFull);
        REQUIRE(layer < ps->scatterAnchor.layers.size());
        const scene::ProceduralGeometry* g =
            object(s, scene::scatterObjectName("valley", ps->scatterAnchor.layers[layer]));
        REQUIRE(g != nullptr);
        REQUIRE(index < g->instances.size());
        const glm::vec3 root(g->instances[index].position);
        const glm::vec3 d = p.centre - root;
        INFO(ps->scatterAnchor.layers[layer] << " #" << index);
        CHECK(d.y > 2.0f);
        CHECK(d.y < 12.0f);
        CHECK(glm::length(glm::vec2(d.x, d.z)) < 5.0f);
    }

    // Built twice, the same set; asked twice from the same eye, the same table.
    const scene::ScatterAnchorSet again = scene::scatterAnchorPoints(s.procedurals, ps->scatterAnchor);
    REQUIRE(again.points.size() == set.points.size());
    for (std::size_t i = 0; i < set.points.size(); ++i) {
        REQUIRE(again.points[i].centre == set.points[i].centre);
        REQUIRE(again.points[i].key == set.points[i].key);
    }
    const glm::vec3 eye = set.points[set.points.size() / 2].centre + glm::vec3(3.0f, -2.0f, 4.0f);
    const auto table = scene::nearestScatterAnchors(set.points, eye, ps->scatterAnchor.viewDistance, ps->clusterCount);
    REQUIRE_FALSE(table.empty());
    REQUIRE(table.size() <= ps->clusterCount);
    float last = 0.0f;
    for (const glm::vec3& c : table) {
        const float d = glm::length(c - eye);
        CHECK(d <= ps->scatterAnchor.viewDistance);
        CHECK(d >= last);
        last = d;
    }
    REQUIRE(scene::nearestScatterAnchors(set.points, eye, ps->scatterAnchor.viewDistance, ps->clusterCount) == table);

    // And the film brings the camera within reach of them. Stepped in two-second strides: the
    // table is a function of where the camera is, and the camera's pose is the timeline's at that
    // second however the engine got there.
    const double duration = film.engine->durationSeconds();
    REQUIRE(duration > 10.0);
    int sampled = 0;
    int inReach = 0;
    std::size_t most = 0;
    double bestSecond = 0.0;
    for (double t = 0.0; t < duration; t += 2.0) {
        FrameTime ft;
        ft.renderTime = t;
        ft.deltaTime = t == 0.0 ? 0.0 : 2.0;
        film.engine->update(ft);
        const auto here = scene::nearestScatterAnchors(set.points, film.comp->scene().camera.position,
                                                       ps->scatterAnchor.viewDistance, ps->clusterCount);
        ++sampled;
        inReach += here.empty() ? 0 : 1;
        if (here.size() > most) {
            most = here.size();
            bestSecond = t;
        }
    }
    std::printf("tree fireflies in reach at %d of %d sampled seconds (of %.0f s); at most %zu swarms at once, "
                "at %.0f s\n",
                inReach, sampled, duration, most, bestSecond);
    CHECK(inReach > 0);
}

TEST_CASE("scatterAnchor survives a save and load", "[particles][fireflies][json]") {
    scene::ParticleSystem src;
    src.name = "swarm";
    src.clusterCount = 23;
    src.clusterRadius = 1.3f;
    src.scatterAnchor.terrain = "hills";
    src.scatterAnchor.layers = {"oak", "birch"};
    src.scatterAnchor.randomBelow = 0.41f;
    src.scatterAnchor.litOnly = true;
    src.scatterAnchor.viewDistance = 37.5f;
    REQUIRE(scene::validateParticleSystem(src).has_value());

    assets::AssetRegistry registry(testsupport::processTempDir());
    scene::Composition comp(registry, "anchor-roundtrip");
    scene::CompositionNode node;
    node.kind = scene::NodeKind::Particles;
    node.name = src.name;
    node.particles = src;
    REQUIRE(comp.addNode(std::move(node)).has_value());
    const auto path = testsupport::processTempDir() / "avgen_anchor_roundtrip.scene.json";
    REQUIRE(comp.saveFile(path).has_value());
    auto loaded = scene::Composition::loadFile(path.filename(), registry);
    REQUIRE(loaded.has_value());
    const scene::CompositionNode* back = (*loaded)->findNode(src.name);
    REQUIRE(back != nullptr);
    const scene::ScatterAnchor& a = back->particles.scatterAnchor;
    CHECK(a.terrain == src.scatterAnchor.terrain);
    CHECK(a.layers == src.scatterAnchor.layers);
    CHECK(a.randomBelow == src.scatterAnchor.randomBelow);
    CHECK(a.litOnly == src.scatterAnchor.litOnly);
    CHECK(a.viewDistance == src.scatterAnchor.viewDistance);
    CHECK(back->particles.clusterCount == src.clusterCount);
    std::filesystem::remove(path);

    // A system that does not anchor writes no key for it.
    scene::Composition plain(registry, "anchor-plain");
    scene::CompositionNode p;
    p.kind = scene::NodeKind::Particles;
    p.name = "plain";
    REQUIRE(plain.addNode(std::move(p)).has_value());
    CHECK_FALSE(plain.toJson()["nodes"][0]["particles"].contains("scatterAnchor"));
}

TEST_CASE("An anchored system that could never draw is refused", "[particles][fireflies]") {
    scene::ParticleSystem s;
    s.name = "swarm";
    s.clusterCount = 8;
    s.scatterAnchor.terrain = "hills";
    s.scatterAnchor.layers = {"oak"};
    REQUIRE(scene::validateParticleSystem(s).has_value()); // the valid neighbour (ADR-182)

    scene::ParticleSystem noClusters = s;
    noClusters.clusterCount = 0;
    CHECK_FALSE(scene::validateParticleSystem(noClusters).has_value());
    scene::ParticleSystem tooMany = s;
    tooMany.clusterCount = scene::kMaxScatterAnchors + 1;
    CHECK_FALSE(scene::validateParticleSystem(tooMany).has_value());
    scene::ParticleSystem noTree = s;
    noTree.scatterAnchor.randomBelow = 0.0f;
    CHECK_FALSE(scene::validateParticleSystem(noTree).has_value());
    scene::ParticleSystem blind = s;
    blind.scatterAnchor.viewDistance = 0.0f;
    CHECK_FALSE(scene::validateParticleSystem(blind).has_value());
}

TEST_CASE("A gated material program is the program-less surface wherever it refuses",
          "[materials][fireflies]") {
    // A program that would change everything it touches: emission, colour and roughness all written.
    scene::MaterialProgram p;
    p.name = "gated";
    scene::MaterialOp c;
    c.kind = scene::MaterialOpKind::Constant;
    c.dst = 0;
    c.constant = glm::vec4(0.9f, 0.1f, 0.3f, 1.0f);
    p.ops.push_back(c);
    p.emissionRegister = 0;
    p.baseColorRegister = 0;
    p.roughnessRegister = 0;
    p.emissionIntensity = 7.0f;
    scene::MaterialResult base = treeBase();
    scene::MaterialContext in;
    in.instanceRandom = glm::vec4(0.9f, 0.9f, 0.9f, 0.2f);
    scene::MaterialContext out = in;
    out.instanceRandom.w = 0.8f;

    // Ungated, it changes both instances: the premise that makes the gate observable (ADR-182).
    REQUIRE_FALSE(sameSurface(scene::evaluateMaterialProgram(p, in, base), base));
    REQUIRE_FALSE(sameSurface(scene::evaluateMaterialProgram(p, out, base), base));
    const std::uint64_t ungatedHash = p.structuralHash();
    const auto ungatedGpu = scene::packMaterialProgram(p, [](const std::string&) { return -1; });
    CHECK(ungatedGpu.opacityCountPad.w == 0); // what every program packed before the gate wrote

    p.gate.lane = 3;
    p.gate.below = 0.5f;
    REQUIRE(p.validate().has_value());
    CHECK_FALSE(sameSurface(scene::evaluateMaterialProgram(p, in, base), base));
    CHECK(sameSurface(scene::evaluateMaterialProgram(p, out, base), base));
    CHECK(p.structuralHash() != ungatedHash);

    // It packs into the header's spare lanes and survives JSON.
    const auto gpu = scene::packMaterialProgram(p, [](const std::string&) { return -1; });
    CHECK(gpu.opacityCountPad.w == 4);
    CHECK(gpu.emissionIntensityPad.y == 0.5f);
    CHECK(gpu.emissionIntensityPad.x == p.emissionIntensity);
    auto back = scene::MaterialProgram::fromJson(p.toJson());
    REQUIRE(back.has_value());
    CHECK(back->gate.lane == 3);
    CHECK(back->gate.below == 0.5f);
    CHECK_FALSE(scene::MaterialProgram{}.toJson().contains("gate"));

    // A lane that is not an instanceRandom component is refused, not silently ignored.
    nlohmann::json bad = p.toJson();
    bad["gate"]["lane"] = 4;
    CHECK_FALSE(scene::MaterialProgram::fromJson(bad).has_value());

    // The distance band: the admitted instance is refused outside it, and an empty band is refused.
    in.depth = 100.0f;
    p.gate.near = 30.0f;
    p.gate.far = 340.0f;
    REQUIRE(p.validate().has_value());
    CHECK_FALSE(sameSurface(scene::evaluateMaterialProgram(p, in, base), base));
    scene::MaterialContext tooNear = in;
    tooNear.depth = 12.0f;
    scene::MaterialContext tooFar = in;
    tooFar.depth = 400.0f;
    CHECK(sameSurface(scene::evaluateMaterialProgram(p, tooNear, base), base));
    CHECK(sameSurface(scene::evaluateMaterialProgram(p, tooFar, base), base));
    const auto banded = scene::packMaterialProgram(p, [](const std::string&) { return -1; });
    CHECK(banded.emissionIntensityPad.z == 30.0f);
    CHECK(banded.emissionIntensityPad.w == 340.0f);
    auto bandBack = scene::MaterialProgram::fromJson(p.toJson());
    REQUIRE(bandBack.has_value());
    CHECK(bandBack->gate.near == 30.0f);
    CHECK(bandBack->gate.far == 340.0f);
    scene::MaterialProgram empty = p;
    empty.gate.far = 20.0f;
    CHECK_FALSE(empty.validate().has_value());
}
