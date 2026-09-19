// Glowmere Valley 2 - Multi-Camera: the ported tree work and the ported cast (ADR-351).
//
// ADR-340 built `glowmere-valley-3` to answer two questions -- why Glowmere's hills are bare, and
// whether a `decide` character can carry a world -- and the owner then cut the scene for its
// terrain and asked for the answers to be folded back into the film they came from. The scene is
// gone; the two instruments it was built around are here, pointed at the film.
//
// **Why the control arm is `glowmere-valley-2` and not a constant.** Every probe and every guard
// below reads two scenes: `glowmere-valley-2-multicam`, which now carries eight tree layers, and
// `glowmere-valley-2`, which still carries the three it was authored with. They are generated from
// the same terrain -- the same `world` block, seed, layers, features and water table, asserted
// below -- so the difference between their columns is the layer table and nothing else. That is
// the like-for-like ADR-340 had to build a second scene (`-legacytrees`) to get, and it is free
// here because the sibling film was left alone on purpose.
//
// ADR-182: a probe that cannot fail proves nothing. The guards below are two-sided -- the counts
// must rise *and* the tree line must not move -- and the control arm is a scene where the same
// computation gives the old answer.
//
// GPU-free: the world map, the scatter placer and the JSON are all CPU.

#include "assets/asset_registry.hpp"
#include "core/time.hpp"
#include "entity/decision.hpp"
#include "entity/locomotion.hpp"
#include "entity/entity.hpp"
#include "entity/nav_grid.hpp"
#include "entity/navigation.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "signals/signal_bus.hpp"
#include "spatial/point_cloud.hpp"
#include "world/biome.hpp"
#include "world/ecology.hpp"
#include "world/world_map.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <map>
#include <set>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace avgen;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {

fs::path worldDir() { return fs::path(AVGEN_SOURCE_DIR) / "examples" / "world"; }

json readJson(const fs::path& path) {
    std::ifstream in(path);
    REQUIRE(in.good());
    json doc;
    in >> doc;
    return doc;
}

const json& terrainNode(const json& scene) {
    for (const json& n : scene.at("nodes")) {
        if (n.contains("world")) {
            return n;
        }
    }
    FAIL("no node in the scene declares a world");
    return scene;
}

world::WorldMap loadWorld(const json& doc) {
    auto map = world::worldMapFromJson(terrainNode(doc).at("world"));
    REQUIRE(map.has_value());
    map->prepare();
    return std::move(*map);
}

// The film, and the sibling it is measured against.
constexpr const char* kFilm = "glowmere-valley-2-multicam.scene.json";
constexpr const char* kControl = "glowmere-valley-2.scene.json";

// **A tree is a layer that places something at least five metres tall** -- the same structural
// definition `test_glowmere_scale.cpp` derives its tree line from, and for the same reason: the
// film has eight tree layers of which one is still called `canopy`, so a list of names is a floor
// that outlives what it counted.
constexpr float kTreeMetres = 5.0f;

struct Placement {
    std::size_t total = 0;
    std::size_t trees = 0;
    std::size_t treesOnHills = 0;
    std::vector<std::string> atCap;
    float treeLine = 0.0f;
    int treeLayers = 0;
    std::set<std::string> treeAssets;
};

Placement placeAll(const json& doc, const world::WorldMap& map, bool print, const char* label) {
    auto ecology = world::ecologyFromJson(terrainNode(doc).at("scatter"));
    REQUIRE(ecology.has_value());
    std::vector<world::ScatterClearance> clearances;
    if (terrainNode(doc).contains("clearings")) {
        auto c = world::clearancesFromJson(terrainNode(doc).at("clearings"));
        REQUIRE(c.has_value());
        clearances = std::move(*c);
    }
    // "The hills" is the upper half of the sampled height range -- the same split ADR-340 used, so
    // the numbers in that ADR and the numbers here are the same measurement.
    const float mid = 0.5f * (map.sampledMinHeight + map.sampledMaxHeight);
    Placement out;
    if (print) {
        std::printf("\n===== %s: placed instances =====\n", label);
        std::printf("  %-13s %9s %9s %10s %9s %8s\n", "layer", "placed", "maxInst", "meshBudget",
                    "onHills", "capped");
    }
    for (const world::ScatterLayer& layer : ecology->layers) {
        const spatial::PointCloud cloud = world::scatter(map, layer, {}, clearances);
        const std::size_t placed = cloud.positions().size();
        std::size_t hills = 0;
        for (const glm::vec3& p : cloud.positions()) {
            hills += p.y > mid ? 1 : 0;
        }
        const bool capped = static_cast<int>(placed) >= layer.maxInstances;
        out.total += placed;
        if (layer.height >= kTreeMetres) {
            out.trees += placed;
            out.treesOnHills += hills;
            out.treeLine = std::max(out.treeLine, layer.height * layer.maxScale);
            ++out.treeLayers;
            out.treeAssets.insert(layer.asset);
            if (capped) {
                out.atCap.push_back(layer.name);
            }
        }
        if (print) {
            std::printf("  %-13s %9zu %9d %10d %9zu %8s\n", layer.name.c_str(), placed,
                        layer.maxInstances, layer.meshBudget, hills, capped ? "AT CAP" : "");
        }
    }
    if (print) {
        std::printf("  %-13s %9zu   trees %zu, of which %zu on the hills; tree line %.2f m\n",
                    "TOTAL", out.total, out.trees, out.treesOnHills, out.treeLine);
        std::fflush(stdout);
    }
    return out;
}

} // namespace

// ---------------------------------------------------------------------------------------------
// The premise the whole like-for-like rests on: the two scenes are the same terrain.
//
// If this ever fails, every count below stops being a comparison and becomes two unrelated
// numbers -- which is the failure mode ADR-340 built `-legacytrees` to avoid and the reason it is
// asserted rather than assumed.
TEST_CASE("The multicam and its sibling are generated from one terrain", "[glowmere][multicam]") {
    const json film = readJson(worldDir() / kFilm);
    const json control = readJson(worldDir() / kControl);
    CHECK(terrainNode(film).at("world") == terrainNode(control).at("world"));
    CHECK(terrainNode(film).at("terrain") == terrainNode(control).at("terrain"));
    // The glades are composition and they are shared. A clearing in one and not the other would
    // move instance counts without moving a layer.
    CHECK(terrainNode(film).at("clearings") == terrainNode(control).at("clearings"));
}

// ---------------------------------------------------------------------------------------------
// ADR-351 §2. Eight layers over eight models where there were three over three, and the counts
// that buys, and the ceiling it does not move.
TEST_CASE("The multicam's hills are wooded, and the tree line has not moved",
          "[glowmere][multicam]") {
    const json film = readJson(worldDir() / kFilm);
    const json control = readJson(worldDir() / kControl);
    const world::WorldMap map = loadWorld(film);
    const Placement after = placeAll(film, map, false, kFilm);
    const Placement before = placeAll(control, map, false, kControl);

    INFO("film " << after.treeLayers << " tree layers / " << after.treeAssets.size()
                 << " models, " << after.trees << " trees, " << after.treesOnHills
                 << " on the hills; control " << before.treeLayers << " / "
                 << before.treeAssets.size() << ", " << before.trees << " / "
                 << before.treesOnHills);

    // Variety, and it is variety of *species* and not of layer: two layers on one model would
    // satisfy a layer count and would not answer the owner's request.
    CHECK(after.treeLayers == 8);
    CHECK(after.treeAssets.size() == 8);
    // The control arm. This is the number the arm above would give on the world the film had
    // yesterday, and it must not pass the claim being made.
    CHECK(before.treeLayers == 3);
    CHECK(before.treeAssets.size() == 3);

    // A band, not a floor (ADR-182). The lower end is "the hills carry several times what they
    // did"; the upper is "no layer is resting on its cap", which is asserted directly below and is
    // what would turn a density change into a no-op.
    CHECK(after.trees > before.trees * 3);
    CHECK(after.treesOnHills > before.treesOnHills * 5);
    CHECK(after.atCap.empty());

    // ADR-334 and ADR-335, and the reason every row of the ported table is authored under 11.2 m:
    // the 16 m elder has to stand a fifth again above the tallest instance any tree layer places,
    // and the cast's 1.94x was re-derived from this exact number.
    CHECK(after.treeLine == Catch::Approx(before.treeLine).epsilon(1e-4));
    CHECK(after.treeLine == Catch::Approx(11.2f).epsilon(1e-3));
    CHECK(16.0f >= after.treeLine * 1.2f);
}

// ---------------------------------------------------------------------------------------------
// ADR-340 §3, re-measured on the terrain the film actually has.
//
// The claim ported forward is that the hills were empty because of ADR-174's riparian ladder and
// not because of biome or `maxSlope`. That was measured on valley 3's scene, which had terrain
// edits the film does not have, so it is measured again here rather than assumed. The instrument
// is the joint gate: for each tree layer, what fraction of hill samples each of the three gates
// refuses.
TEST_CASE("probe: what gates a tree off the multicam's hills", "[.probe][glowmere-multicam]") {
    for (const char* file : {kControl, kFilm}) {
        const json doc = readJson(worldDir() / file);
        const world::WorldMap map = loadWorld(doc);
        auto ecology = world::ecologyFromJson(terrainNode(doc).at("scatter"));
        REQUIRE(ecology.has_value());
        std::printf("\n===== %s: what refuses a tree on the hills =====\n", file);
        std::printf("  %-13s %8s %8s %10s %10s   %s\n", "layer", "maxSlope", "height", "all(dens)",
                    "hills(dens)", "hills refused by");

        const int kN = 160;
        const float mid = 0.5f * (map.sampledMinHeight + map.sampledMaxHeight);
        for (const world::ScatterLayer& layer : ecology->layers) {
            if (layer.height < kTreeMetres) {
                continue;
            }
            double all = 0.0;
            double hills = 0.0;
            int allN = 0;
            int hillN = 0;
            int hillSlopeOut = 0;
            int hillHarOut = 0;
            int hillNoBiome = 0;
            for (int j = 0; j < kN; ++j) {
                for (int i = 0; i < kN; ++i) {
                    const glm::vec2 q((static_cast<float>(i) / (kN - 1) - 0.5f) * map.size.x,
                                      (static_cast<float>(j) / (kN - 1) - 0.5f) * map.size.y);
                    const world::Sample sm = map.sample(q, 1.0f);
                    if (sm.submerged) {
                        continue;
                    }
                    const bool hill = sm.height > mid;
                    ++allN;
                    hillN += hill ? 1 : 0;
                    if (sm.slope < layer.minSlope || sm.slope > layer.maxSlope) {
                        hillSlopeOut += hill ? 1 : 0;
                        continue;
                    }
                    if (layer.constrainsHeightAboveWater()) {
                        const float har = map.heightAboveWater(q);
                        if (har < layer.minHeightAboveWater - layer.heightAboveWaterFeather ||
                            har > layer.maxHeightAboveWater + layer.heightAboveWaterFeather) {
                            hillHarOut += hill ? 1 : 0;
                            continue;
                        }
                    }
                    const world::BiomeWeights bw =
                        map.biomes.at(sm.altitude, sm.slope, sm.moisture, q);
                    double d = 0.0;
                    for (const world::BiomeDensity& bd : layer.densities) {
                        for (std::size_t b = 0; b < map.biomes.biomes.size(); ++b) {
                            if (map.biomes.biomes[b].name == bd.biome) {
                                d += static_cast<double>(bd.density) *
                                     static_cast<double>(bw.weights[b]);
                            }
                        }
                    }
                    all += d;
                    if (hill) {
                        hills += d;
                        hillNoBiome += d <= 0.0 ? 1 : 0;
                    }
                }
            }
            std::printf("  %-13s %8.2f %8.2f %10.5f %10.5f   slope %4.1f%%  HAR %4.1f%%  "
                        "no-biome %4.1f%%\n",
                        layer.name.c_str(), layer.maxSlope, layer.height,
                        all / std::max(1, allN), hills / std::max(1, hillN),
                        100.0 * hillSlopeOut / std::max(1, hillN),
                        100.0 * hillHarOut / std::max(1, hillN),
                        100.0 * hillNoBiome / std::max(1, hillN));
        }
        std::fflush(stdout);
    }
}

// ---------------------------------------------------------------------------------------------
// The counts, printed. `maxInstances` and `meshBudget` are caps, and a cap that binds is invisible
// in the density -- so the placer is run rather than the table read.
//
//   ./build/release/tests/avgen_tests "[.probe][glowmere-multicam]"
TEST_CASE("probe: Glowmere multicam scatter instance counts", "[.probe][glowmere-multicam]") {
    const json film = readJson(worldDir() / kFilm);
    const world::WorldMap map = loadWorld(film);
    placeAll(readJson(worldDir() / kControl), map, true, kControl);
    placeAll(film, map, true, kFilm);
}

// ---------------------------------------------------------------------------------------------
// ADR-340 §2, re-measured on the terrain the film actually has -- and this is the probe whose
// answer decides what the ported cast can be.
//
// Valley 3 authored a ford and a backwater into its terrain so that `RouteConsiderer` had two ways
// to price. **The film never got those edits**, so the river it has is the one ADR-340 measured
// before them: a channel that leaves the map at both ends, 3.5-3.65 m deep against a wade depth of
// 0.85 m. A route considerer needs a ford *or* a way round; this prints whether either exists, per
// alien, so "the crossing demonstration does not port" is a measurement and not an assumption.
//
//   ./build/release/tests/avgen_tests "[.probe][glowmere-multicam]"
TEST_CASE("probe: the multicam's river, and where each of the cast can get to",
          "[.probe][glowmere-multicam]") {
    const fs::path path = worldDir() / kFilm;
    assets::AssetRegistry registry(path.parent_path());
    params::ParameterSet params;
    params::Modulator modulator;
    auto loaded = scene::Composition::loadFile(path, registry);
    INFO((loaded.has_value() ? std::string() : loaded.error().message));
    REQUIRE(loaded.has_value());
    std::unique_ptr<scene::Composition> comp = std::move(*loaded);
    comp->attach(params, modulator);
    const entity::Navigator& nav = comp->entityWorld().navigator();
    const entity::NavGrid* grid = nav.grid();

    std::printf("\n===== %s: the river as a route =====\n", kFilm);
    std::printf("navWadeDepth %.4f  navBodyRadius %.4f  grid %s\n", nav.settings().wadeDepth,
                nav.settings().bodyRadius,
                grid == nullptr ? "ABSENT" : (grid->valid() ? "present and valid" : "INVALID"));
    if (grid != nullptr) {
        std::printf("grid %dx%d at %.1f m; %lld walkable, %lld water, %lld blocked; %lld regions, "
                    "largest %lld, stranded %lld; terrain-trusted %s\n",
                    grid->stats().width, grid->stats().height,
                    static_cast<double>(grid->stats().cellSize),
                    static_cast<long long>(grid->stats().walkable),
                    static_cast<long long>(grid->stats().water),
                    static_cast<long long>(grid->stats().blocked),
                    static_cast<long long>(grid->stats().regions),
                    static_cast<long long>(grid->stats().largestRegion),
                    static_cast<long long>(grid->stats().stranded),
                    grid->stats().trusted ? "yes" : "NO");
    }

    // The channel, sampled across its own course. The centreline is
    // `tools/make_glowmere_valley_2.py`'s RIVER, every other control point.
    const std::array<glm::vec2, 8> kCourse{{{-33.0f, -220.0f},
                                            {27.0f, -92.0f},
                                            {-18.3f, 0.0f},
                                            {-32.0f, 20.0f},
                                            {-31.0f, 96.0f},
                                            {-13.0f, 132.0f},
                                            {45.0f, 244.0f},
                                            {11.0f, 352.0f}}};
    std::printf("\n  channel depth across the course (m of water, 0 = dry):\n");
    for (const glm::vec2& c : kCourse) {
        std::printf("    z %7.1f :", c.y);
        for (float off = -24.0f; off <= 24.0f; off += 6.0f) {
            std::printf(" %6.2f", nav.sample({c.x + off, c.y}).waterDepth);
        }
        std::printf("\n");
    }

    // Where the five bodies stand, and what each can walk to. The destinations are the film's own
    // hero fungi -- the things a curious character is actually going to want to go and look at --
    // so a row of failures here is the itinerary the deciders cannot have.
    struct Site {
        const char* name;
        float x;
        float z;
    };
    const std::array<Site, 5> kCast{{{"rook", -74.0f, -18.0f},
                                     {"tide", -28.0f, -46.0f},
                                     {"sage", -112.0f, 16.0f},
                                     {"ember", 12.0f, 34.0f},
                                     {"vane", 20.0f, -10.0f}}};
    std::vector<std::pair<std::string, glm::vec2>> goals;
    for (const world::HeroPoint& h : comp->heroes()) {
        goals.emplace_back(h.name, glm::vec2(h.position.x, h.position.z));
    }
    std::printf("\n  who can reach what (path length in m, or the refusal):\n");
    std::printf("    %-8s %8s %8s  ", "body", "ground", "depth");
    for (const auto& [name, p] : goals) {
        std::printf("%14s", name.c_str());
    }
    std::printf("\n");
    for (const Site& s : kCast) {
        const entity::NavSample sm = nav.sample({s.x, s.z});
        std::printf("    %-8s %8.2f %8.2f  ", s.name, sm.ground, sm.waterDepth);
        for (const auto& [name, p] : goals) {
            entity::PathRequest req;
            req.from = {s.x, s.z};
            req.to = p;
            req.goalTolerance = 8.0f;
            const entity::PathResult r = nav.requestPath(req);
            if (r.ok()) {
                std::printf("%14.1f", r.length);
            } else {
                std::printf("%14s", entity::pathStatusName(r.status));
            }
        }
        std::printf("\n");
    }
    std::fflush(stdout);
}

// ---------------------------------------------------------------------------------------------
// The cast, run. ADR-351 part 3.
//
// The one failure mode a `decide` cast has that an `explore` cast does not is standing still: the
// selector only hands out new actions when the committed option *changes*, so a body whose goal
// became unreachable used to freeze for good (ADR-340 §4, four of five bodies stopped by t = 90 s).
// On this terrain that is not a hypothetical -- perception reaches 85-120 m, the river is 50 m from
// `rook`, and a percept across the water is a goal no path reaches. So the cast is simulated and
// the distances are asserted, because a film whose characters are statues renders perfectly.

namespace {

struct Track {
    glm::vec3 start{0.0f};
    glm::vec3 end{0.0f};
    float travelled = 0.0f;
    float deepest = 0.0f;
    std::size_t decisions = 0;
    std::size_t percepts = 0;
    float nearest = 1e9f;          // closest approach to any other body
    std::map<std::string, int> chosen;
    std::map<std::string, int> activity;
};

const std::array<const char*, 5> kCast{{"rook", "tide", "sage", "ember", "vane"}};

std::map<std::string, Track> play(double seconds, std::uint32_t seedShift, double hz = 40.0) {
    std::ifstream in(worldDir() / kFilm);
    REQUIRE(in.good());
    json doc;
    in >> doc;
    if (seedShift != 0) {
        for (json& e : doc.at("entities")) {
            e["seed"] = e.at("seed").get<std::uint64_t>() + seedShift;
        }
    }
    // Through a file, and in the film's own directory, because the scene names two entity
    // profiles and an environment by relative path and `fromJson` has no base to resolve them
    // against -- the first cut of this used it and died on `../entities/craft-lights.profile.json`.
    const fs::path scratch =
        worldDir() / fmt::format("_probe-cast-{}.scene.json", seedShift);
    {
        std::ofstream out(scratch);
        REQUIRE(out.good());
        out << doc.dump(1);
    }
    struct Remove {
        fs::path p;
        ~Remove() { std::error_code ec; fs::remove(p, ec); }
    } remove{scratch};

    assets::AssetRegistry registry(worldDir());
    params::ParameterSet params;
    params::Modulator modulator;
    signals::SignalBus bus;
    auto loaded = scene::Composition::loadFile(scratch, registry);
    INFO((loaded.has_value() ? std::string() : loaded.error().message));
    REQUIRE(loaded.has_value());
    std::unique_ptr<scene::Composition> comp = std::move(*loaded);
    comp->attach(params, modulator);
    comp->setViewport(1600, 900);
    // ADR-186's offline setting. With the distance cull on, "it walked 40 m" would be a measurement
    // of which level-of-detail band the camera happened to put the body in.
    comp->scene().detailLimits.entityDistanceCull = false;

    std::map<std::string, Track> out;
    const entity::Navigator& nav = comp->entityWorld().navigator();
    const double step = 1.0 / hz;
    const auto frames = static_cast<int>(std::llround(seconds * hz));
    FrameTime time;
    std::map<std::string, glm::vec3> last;
    for (int i = 0; i < frames; ++i) {
        time.renderTime = static_cast<double>(i) * step;
        time.deltaTime = i == 0 ? 0.0 : step;
        time.frameIndex = static_cast<std::uint64_t>(i);
        params.resetFinals();
        comp->updateFields(time, bus, modulator);
        modulator.applyRoutes(bus, params, time.deltaTime);
        comp->updateBehaviour(time, bus);
        comp->update(time);
        for (const auto& e : comp->entityWorld().entities()) {
            const std::string name(e->name());
            Track& t = out[name];
            const glm::vec3 p = e->state().position();
            if (i == 0) {
                t.start = p;
                last[name] = p;
            }
            t.travelled += glm::length(glm::vec2(p.x - last[name].x, p.z - last[name].z));
            last[name] = p;
            t.end = p;
            t.deepest = std::max(t.deepest, nav.sample(glm::vec2(p.x, p.z)).waterDepth);
            t.activity[entity::activityName(e->locomotion().activity)] += 1;
            t.percepts = std::max(t.percepts, e->percepts().size());
            for (const auto& behavior : e->behaviors()) {
                entity::DecisionDebug dbg;
                if (!behavior->decisionDebug(dbg)) {
                    continue;
                }
                t.decisions = dbg.decisions;
                if (!dbg.chosen.empty()) {
                    t.chosen[std::string(dbg.chosen)] += 1;
                }
            }
        }
        // ADR-340's other engine fix, checked where it applies: `ground`'s `bodyRadius` is what
        // puts a `decide` character into the crowd field, and without it two aliens closed to
        // 0.238 m. A minimum over the run, not a final position.
        for (const char* a : kCast) {
            const entity::Entity* ea = comp->entityWorld().find(a);
            if (ea == nullptr) {
                continue;
            }
            for (const char* b : kCast) {
                if (std::string(a) >= std::string(b)) {
                    continue;
                }
                const entity::Entity* eb = comp->entityWorld().find(b);
                if (eb == nullptr) {
                    continue;
                }
                const glm::vec3 pa = ea->state().position();
                const glm::vec3 pb = eb->state().position();
                const float d = glm::length(glm::vec2(pa.x - pb.x, pa.z - pb.z));
                out[a].nearest = std::min(out[a].nearest, d);
                out[b].nearest = std::min(out[b].nearest, d);
            }
        }
    }
    return out;
}

constexpr std::uint32_t kSeedShift = 900001u;

} // namespace

TEST_CASE("probe: the multicam's cast, ninety seconds of it", "[.probe][glowmere-multicam]") {
    for (std::uint32_t shift : {0u, kSeedShift}) {
        const auto run = play(90.0, shift);
        std::printf("\n--- the cast over 90 s, seed shift %u ---\n", shift);
        for (const char* name : kCast) {
            const Track& t = run.at(name);
            std::string top;
            int best = 0;
            for (const auto& [option, n] : t.chosen) {
                if (n > best) {
                    best = n;
                    top = option;
                }
            }
            std::string acts;
            for (const auto& [a, n] : t.activity) {
                acts += fmt::format(" {}:{}", a, n);
            }
            std::printf("  %-6s travelled %7.1f m  net %6.1f m  deepest %4.2f m  decisions %3zu  "
                        "percepts<=%zu  nearest body %5.2f m  mostly '%s'\n           act:%s\n",
                        name, static_cast<double>(t.travelled),
                        static_cast<double>(
                            glm::length(glm::vec2(t.end.x - t.start.x, t.end.z - t.start.z))),
                        static_cast<double>(t.deepest), t.decisions, t.percepts,
                        static_cast<double>(t.nearest), top.c_str(), acts.c_str());
        }
        std::fflush(stdout);
    }
}

TEST_CASE("The multicam's five deciders move, and they do not walk through each other",
          "[glowmere][multicam][entity]") {
    const auto run = play(90.0, 0);
    const auto varied = play(90.0, kSeedShift);

    float totalA = 0.0f;
    float totalB = 0.0f;
    for (const char* name : kCast) {
        const Track& t = run.at(name);
        INFO(name << " travelled " << t.travelled << " m in 90 s, made " << t.decisions
                  << " decisions, saw <= " << t.percepts << " percepts, came within " << t.nearest
                  << " m of another body");
        // **Not a statue.** The band's floor is deliberately low -- `sage` is a `holdPost`
        // character and is *supposed* to stay near its grove -- but 12 m over 90 s is under
        // 0.14 m/s against a walk speed of 3.07, which no body that is deciding anything can
        // stay below.
        CHECK(t.travelled > 12.0f);
        // And not a bolted-on treadmill either: 90 s at the run speed is 665 m, and a body that
        // covered it never stopped to look at anything.
        CHECK(t.travelled < 500.0f);
        // It is changing its mind. `Selector::Counts::decisions` counts the times the *committed
        // option changed*, not the ticks that fired -- a body that re-scores at 1.2 Hz and keeps
        // picking the same errand for eight seconds counts one. So the floor is an errand count,
        // not a tick count: ten different things attempted in ninety seconds. (The first cut of
        // this arm asserted 40 on the tick reading and failed on all five, which is the arm
        // measuring the wrong quantity rather than the cast misbehaving.)
        //
        // It is deliberately *not* the anti-statue guard: a frozen `sage` sat at 8 decisions while
        // travelling 0.0 m, because the option it was committed to never stopped winning. Distance
        // is what catches that, and distance is asserted above.
        CHECK(t.decisions >= 10);
        // And not twitching: 90 s of errands changing more than twice a second is a body that
        // never gets anywhere, which is what `dwellTicks` and `margin` exist to prevent.
        CHECK(t.decisions < 180);
        // And it is deciding about something it saw, which is what `source: "perceived"` means.
        CHECK(t.percepts > 0);
        // ADR-340's `bodyRadius`, and this is the arm it earns: 0.9 m each, so two of them stand
        // 1.8 m apart. The pre-fix measurement was 0.238 m.
        CHECK(t.nearest > 1.2f);
        // Nobody drowns. The wade depth is 0.8536 m and the channel is 2.6-3.65 m deep, so a body
        // in the river is a body the navigator let walk into a wall.
        CHECK(t.deepest < 0.8536f);
        totalA += t.travelled;
        totalB += varied.at(name).travelled;
    }

    // ADR-182's control, and it is the one that matters for a decider: if the same world at a
    // different set of per-body seeds produced the same itinerary to the metre, the bodies would
    // be following the geometry and the word "decide" would be decoration. Different, but the same
    // *kind* of thing -- so a band on both sides.
    const float ratio = totalA / std::max(1.0f, totalB);
    INFO("total travel " << totalA << " m at seed+0 against " << totalB << " m at seed+"
                         << kSeedShift << " (ratio " << ratio << ")");
    CHECK(std::abs(totalA - totalB) > 1.0f);
    CHECK(ratio > 0.4f);
    CHECK(ratio < 2.5f);
}
