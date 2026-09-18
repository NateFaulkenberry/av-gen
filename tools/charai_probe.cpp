// TEMPORARY DIAGNOSTIC (docs/character-ai-research.md, Phase 0 §E).
//
// The character-intelligence brief asks for a cost model at 10/25/50/100/250/500 characters. This
// measures it, on the real Glowmere world, with the real navigator, the real navigation grid and
// the real per-instance obstacle field -- rather than estimating it, because every number in that
// section has to be one somebody took.
//
// Arms, and why each one has a control (ADR-182):
//
//   nil      N entities with NO behaviours. The harness floor: the crowd rebuild, the LOD test and
//            the loop over entities, with nothing in them. Without it every other arm's number is
//            "a behaviour plus an unknown amount of bookkeeping".
//   wander   N entities carrying the farm-animal profile (liveliness + wander + ground).
//   explore  N entities carrying the alien profile (explore + liveliness + lookAt) -- the only
//            behaviour in the repository that plans a route over the navigation graph.
//
//   prims    The primitives a perception or decision layer would call, priced individually:
//            Navigator::sample, steer, requestPath, pathValid, crowdSeparation, and
//            world::heroSightline -- the nine-ray silhouette test that is the nearest thing this
//            engine has to a line-of-sight query.
//
//   det      Determinism. Does play(t) equal seek(t)? Does the coarse behaviour-LOD band change
//            the answer? Each with an arm that must disagree, so an arm that agrees means something.
//
// CPU only -- no GPU, no window, no renderer. Minima over repeats (ADR-170).
//
// DELETE THIS FILE, and its two lines in tools/CMakeLists.txt, once the plan it priced is built.

#include "assets/asset_registry.hpp"
#include "core/log.hpp"
#include "entity/entity.hpp"
#include "entity/nav_grid.hpp"
#include "entity/navigation.hpp"
#include "core/rng.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "spatial/point_grid.hpp"
#include "world/camera_clearance.hpp"
#include "world/terrain_query.hpp"
#include "world/world_map.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <tuple>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

using Clock = std::chrono::steady_clock;

double msSince(Clock::time_point t) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
}

// A behaviour list, as a scene file writes one.
nlohmann::json behaviorsFor(const char* profile) {
    nlohmann::json out = nlohmann::json::array();
    if (std::string(profile) == "wander") {
        out.push_back({{"kind", "liveliness"}});
        out.push_back({{"kind", "wander"}, {"radius", 18.0}, {"speed", 1.1}});
        out.push_back({{"kind", "ground"}});
    } else if (std::string(profile) == "explore") {
        out.push_back({{"kind", "explore"},
                       {"speed", 1.7},
                       {"bodyRadius", 2.4},
                       {"headroom", 7.0},
                       {"maxRange", 160.0}});
        out.push_back({{"kind", "liveliness"}});
        out.push_back({{"kind", "lookAt"}});
    }
    return out;
}

// N entities of one profile, spread over the walkable ground so they are not all in one cell,
// and the bindings that tell them where the scene put them.
//
// The bindings are not optional decoration. Without a `NodeBinding` an entity's anchor is the
// origin, every body in the arm starts stacked on the same point, and `explore` never travels --
// which is how the first version of this probe reported 0.000000 m for its "must not be zero"
// control (ADR-182). A harness that cannot move a character cannot measure one.
struct Population {
    std::vector<entity::EntityDesc> descs;
    std::vector<entity::NodeBinding> bindings;
};

Population population(int n, const char* profile, const entity::Navigator& nav, bool lodBands = false) {
    const glm::vec2 lo = nav.worldMin();
    const glm::vec2 hi = nav.worldMax();
    const int side = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(n))));
    Population out;
    out.descs.reserve(static_cast<std::size_t>(n));
    out.bindings.reserve(static_cast<std::size_t>(n));
    const nlohmann::json behaviors = behaviorsFor(profile);
    for (int i = 0; i < n; ++i) {
        entity::EntityDesc d;
        d.name = std::string(profile) + "-" + std::to_string(i);
        d.node = d.name;
        for (const auto& b : behaviors) {
            entity::BehaviorDesc bd;
            bd.kind = b.at("kind").get<std::string>();
            bd.settings = b;
            d.behaviors.push_back(std::move(bd));
        }
        d.gait.walkSpeed = 1.6f;
        if (lodBands) {
            // Glowmere's own numbers for its walking characters, so the LOD arm exercises the real
            // bands. Left at 0 otherwise -- 0 disables a band, and an arm whose band is disabled is
            // an arm that cannot fail (ADR-182), which is exactly what the first run of this probe
            // reported as "behaviour LOD does not change the answer".
            d.fullDetailDistance = 120.0f;
            d.coarseInterval = 0.1f;
            d.cullDistance = 340.0f;
        }
        out.descs.push_back(std::move(d));

        // Inset by a tenth of the world so no body starts on the boundary margin, which is not
        // navigable and would leave a walker with nowhere to go.
        const float u = 0.1f + 0.8f * (static_cast<float>(i % side) + 0.5f) / static_cast<float>(side);
        const float v = 0.1f + 0.8f * (static_cast<float>(i / side) + 0.5f) / static_cast<float>(side);
        const glm::vec2 p = lo + (hi - lo) * glm::vec2(u, v);
        entity::NodeBinding b;
        b.node = out.descs.back().name;
        b.exists = true;
        b.transformPrefix = "nodes/" + b.node + "/";
        b.anchor = glm::vec3(p.x, nav.groundHeight(p), p.y);
        b.facing = 0.0f;
        out.bindings.push_back(std::move(b));
    }
    return out;
}

// The transform parameters a Composition would have registered for each of those nodes.
void registerNodes(params::ParameterSet& params, const Population& pop) {
    for (const entity::NodeBinding& b : pop.bindings) {
        params.add(params::ParamDesc<glm::vec3>{.path = b.transformPrefix + "position",
                                                .defaultValue = b.anchor,
                                                .hardMin = glm::vec3(-1e4f),
                                                .hardMax = glm::vec3(1e4f)});
        params.add(params::ParamDesc<glm::vec3>{.path = b.transformPrefix + "rotation",
                                                .defaultValue = glm::vec3(0.0f),
                                                .hardMin = glm::vec3(-360.0f),
                                                .hardMax = glm::vec3(360.0f)});
        params.add(params::ParamDesc<glm::vec3>{.path = b.transformPrefix + "scale",
                                                .defaultValue = glm::vec3(1.0f),
                                                .hardMin = glm::vec3(0.001f),
                                                .hardMax = glm::vec3(100.0f)});
    }
}

// Builds a world of N of one profile, wired the way a Composition wires one.
void buildWorld(entity::EntityWorld& world, params::ParameterSet& params, int n,
                const char* profile, const entity::Navigator& nav, bool lodBands = false) {
    Population pop = population(n, profile, nav, lodBands);
    world.setNavigator(nav);
    world.setBindings(pop.bindings);
    registerNodes(params, pop);
    world.setEntities(std::move(pop.descs), 1234u);
    world.registerParameters(params, "entity/");
    world.bind(params, "entity/");
}

struct Timing {
    double perFrameMs = 0.0;
    // How far the furthest body actually walked. The arm's own control: a `wander` or `explore`
    // arm that reports 0 m travelled is not a cheap simulation, it is a simulation that did not
    // run, and its milliseconds mean nothing.
    double maxTravel = 0.0;
};

// One arm: build a world of N, run `frames` fixed steps, report the minimum per-frame cost over
// `repeats` runs. The world is rebuilt per repeat so no repeat inherits the previous one's warm
// route cache -- a warm cache would make the second repeat the fastest and the minimum a lie.
Timing armTiming(int n, const char* profile, const entity::Navigator& nav, int frames, int repeats) {
    Timing best;
    best.perFrameMs = std::numeric_limits<double>::max();
    for (int r = 0; r < repeats; ++r) {
        entity::EntityWorld world;
        params::ParameterSet params;
        buildWorld(world, params, n, profile, nav);
        entity::EntityUpdate u;
        u.dt = 1.0 / 60.0;
        u.distanceDetail = false; // ADR-186: price the full simulation, not the LOD band
        u.viewPosition = glm::vec3(0.0f);
        // One untimed step, so the first route plan is not charged to the measured window.
        u.time = 0.0;
        world.update(u, params);
        const auto start = Clock::now();
        for (int f = 1; f <= frames; ++f) {
            u.time = static_cast<double>(f) / 60.0;
            world.update(u, params);
        }
        const double ms = msSince(start) / static_cast<double>(frames);
        double travel = 0.0;
        for (const auto& e : world.entities()) {
            travel = std::max(travel, static_cast<double>(glm::length(e->state().travel)));
        }
        if (ms < best.perFrameMs) {
            best.perFrameMs = ms;
        }
        best.maxTravel = std::max(best.maxTravel, travel);
    }
    return best;
}

void scaling(const entity::Navigator& nav, int frames, int repeats) {
    const int counts[] = {10, 25, 50, 100, 250, 500};
    const char* profiles[] = {"nil", "wander", "explore"};
    std::printf("\n== scaling: ms per 60 Hz behaviour update, minimum of %d runs of %d frames ==\n",
                repeats, frames);
    std::printf("%-10s %9s %9s %9s %9s %9s %9s   %s\n", "profile", "10", "25", "50", "100", "250",
                "500", "max travel at N=50");
    for (const char* profile : profiles) {
        std::printf("%-10s", profile);
        double travelAt50 = 0.0;
        for (const int n : counts) {
            const Timing t = armTiming(n, profile, nav, frames, repeats);
            std::printf(" %9.4f", t.perFrameMs);
            if (n == 50) {
                travelAt50 = t.maxTravel;
            }
            std::fflush(stdout);
        }
        std::printf("   %8.2f m\n", travelAt50);
    }
    std::printf("  (the travel column is the control: `nil` must be 0, the other two must not be)\n");
}

void primitives(const entity::Navigator& nav, int repeats) {
    const glm::vec2 lo = nav.worldMin();
    const glm::vec2 hi = nav.worldMax();
    // A set of start/goal pairs spread over the world, so no measurement is one lucky cell.
    std::vector<glm::vec2> from;
    std::vector<glm::vec2> to;
    for (int i = 0; i < 64; ++i) {
        const float a = static_cast<float>(i) / 64.0f;
        const float b = static_cast<float>((i * 37) % 64) / 64.0f;
        from.push_back(lo + (hi - lo) * glm::vec2(a, b));
        to.push_back(lo + (hi - lo) * glm::vec2(b, a));
    }

    const auto best = [&](const char* name, auto&& fn, int inner) {
        double bestUs = std::numeric_limits<double>::max();
        long long acc = 0;
        for (int r = 0; r < repeats; ++r) {
            const auto start = Clock::now();
            for (int i = 0; i < inner; ++i) {
                acc += fn(i);
            }
            const double us = msSince(start) * 1000.0 / static_cast<double>(inner);
            bestUs = std::min(bestUs, us);
        }
        std::printf("  %-26s %9.3f us   (sink %lld)\n", name, bestUs, acc);
    };

    std::printf("\n== primitive cost, minimum of %d runs ==\n", repeats);
    best("Navigator::sample", [&](int i) { return nav.sample(from[i % 64]).navigable ? 1 : 0; }, 4096);
    best("Navigator::groundHeight",
         [&](int i) { return static_cast<int>(nav.groundHeight(from[i % 64])); }, 4096);
    best("Navigator::steer",
         [&](int i) { return nav.steer(from[i % 64], to[i % 64], 6.0f).x > 0.0f ? 1 : 0; }, 1024);
    best("Navigator::obstructed",
         [&](int i) { return nav.obstructed(from[i % 64], 0.0f) ? 1 : 0; }, 4096);
    best("Navigator::clearanceAt",
         [&](int i) { return static_cast<int>(nav.clearanceAt(from[i % 64], 0.0f)); }, 4096);
    std::vector<glm::vec2> route;
    best(
        "Navigator::requestPath",
        [&](int i) {
            entity::PathRequest req;
            req.from = from[i % 64];
            req.to = to[i % 64];
            req.goalTolerance = 2.0f;
            const entity::PathResult res = nav.requestPath(req);
            if (res.status == entity::PathStatus::Ok) {
                route = res.waypoints;
            }
            return static_cast<int>(res.waypoints.size());
        },
        256);
    if (!route.empty()) {
        best(
            "Navigator::pathValid",
            [&](int i) { return nav.pathValid(from[i % 64], route, 0) ? 1 : 0; }, 1024);
    }
    // The nearest thing to a line-of-sight query this engine has (ADR-080). Priced at three
    // ranges, because it marches at a fixed step in metres: its cost is linear in how far the
    // looker is looking, and "can this character see that one" is a 20-60 m question while the
    // camera's version of it is a 400 m one. One number for both would be the wrong number twice.
    const world::ClearanceField& field = nav.clearance();
    for (const float range : {20.0f, 60.0f, 200.0f}) {
        char label[64];
        std::snprintf(label, sizeof(label), "heroSightline 9 rays @ %3.0fm", static_cast<double>(range));
        best(
            label,
            [&](int i) {
                const glm::vec2 e = from[i % 64];
                const float a = static_cast<float>(i) * 0.61547f;
                const glm::vec2 p = e + glm::vec2(std::cos(a), std::sin(a)) * range;
                world::SubjectCapsule s;
                s.position = glm::vec3(p.x, nav.groundHeight(p), p.y);
                s.radius = 1.2f;
                s.height = 6.0f;
                const glm::vec3 eye(e.x, nav.groundHeight(e) + 5.0f, e.y);
                return static_cast<int>(world::heroSightline(field, eye, s).visible * 9.0f);
            },
            128);
    }
}

// What a perception candidate scan would cost. The one number the Phase 0 performance model left
// extrapolated: a sense tick is a radius query over the world's interest points plus a cheap test
// per candidate, and the radius query was the part nobody had priced.
//
// Built over the real 505 interest points of the real scene, at the cell size a 60 m sense range
// wants, and queried at three ranges -- because a query radius larger than the cell size touches
// more than 27 cells and the header says so.
void perceptionScan(const entity::EntityWorld& world, const entity::Navigator& nav, int repeats) {
    std::vector<glm::vec3> points;
    for (const entity::InterestPoint& p : world.interestPoints()) {
        points.push_back(p.position);
    }
    std::printf("\n== perception candidate scan over %zu real interest points ==\n", points.size());
    if (points.empty()) {
        std::printf("  no interest points; nothing to scan\n");
        return;
    }
    const glm::vec2 lo = nav.worldMin();
    const glm::vec2 hi = nav.worldMax();
    std::vector<glm::vec3> eyes;
    for (int i = 0; i < 64; ++i) {
        const float a = static_cast<float>(i) / 64.0f;
        const float b = static_cast<float>((i * 37) % 64) / 64.0f;
        const glm::vec2 p = lo + (hi - lo) * glm::vec2(a, b);
        eyes.emplace_back(p.x, nav.groundHeight(p), p.y);
    }
    for (const float range : {20.0f, 60.0f, 120.0f}) {
        spatial::PointGrid grid;
        const auto buildStart = Clock::now();
        grid.build(points, range);
        const double buildMs = msSince(buildStart);
        std::vector<std::uint32_t> hits;
        double bestUs = std::numeric_limits<double>::max();
        long long found = 0;
        for (int r = 0; r < repeats; ++r) {
            const auto start = Clock::now();
            long long acc = 0;
            for (int i = 0; i < 4096; ++i) {
                grid.query(eyes[static_cast<std::size_t>(i % 64)], range, hits);
                // The distance test a sense tick does per candidate. Counted, so the compiler
                // cannot drop the loop. It deliberately does **not** include the per-candidate
                // `clearanceAt`: that is priced separately in the primitives table at 0.024 us, and
                // folding it in here would hide which half of a sense tick costs what.
                for (const std::uint32_t h : hits) {
                    const glm::vec3 d = points[h] - eyes[static_cast<std::size_t>(i % 64)];
                    if (glm::dot(d, d) <= range * range) {
                        ++acc;
                    }
                }
            }
            bestUs = std::min(bestUs, msSince(start) * 1000.0 / 4096.0);
            found = acc;
        }
        std::printf("  range %3.0f m: build %5.2f ms, one scan %7.3f us, %lld candidates per 4096 scans\n",
                    static_cast<double>(range), buildMs, bestUs, found);
    }
}

// What a navigation query is actually made of (P7 item 1).
//
// `Navigator::sample` is 10.3 us and `Navigator::groundHeight` is 0.97 us, so the sample is ten
// height evaluations' worth of work and nobody had asked which ten. This decomposes it, because
// "steer against the grid instead" is only the right answer if the analytic query is irreducible --
// and the first thing this arm found is that it is not.
//
// The control is the bottom of the table: every composite must cost at least the sum of the parts
// it is built from. A part that reads as more expensive than the whole is a mistimed arm.
void navCost(const entity::Navigator& nav, int repeats) {
    const world::TerrainQuery query = nav.terrain();
    const world::WorldMap* map = query.map;
    if (map == nullptr) {
        std::printf("\n== navigation decomposition ==\n  this scene has no world map\n");
        return;
    }
    const glm::vec2 lo = nav.worldMin();
    const glm::vec2 hi = nav.worldMax();
    // Points on walkable ground, so the rule ladder runs to the end rather than bailing at rule one.
    // A table measured on rejected points prices the cheapest path through the function and calls it
    // the cost of the function.
    std::vector<glm::vec2> pts;
    std::vector<glm::vec2> far6;
    std::vector<glm::vec2> far30;
    Rng rng(20250918u);
    while (pts.size() < 64) {
        const glm::vec2 p = lo + (hi - lo) * glm::vec2(rng.nextFloat(), rng.nextFloat());
        if (!nav.sample(p).navigable) {
            continue;
        }
        const float a = rng.range(0.0f, 6.2831853f);
        const glm::vec2 d(std::cos(a), std::sin(a));
        pts.push_back(p);
        far6.push_back(p + d * 6.0f);
        far30.push_back(p + d * 30.0f);
    }

    const auto best = [&](const char* name, auto&& fn, int inner) {
        double bestUs = std::numeric_limits<double>::max();
        long long acc = 0;
        for (int r = 0; r < repeats; ++r) {
            const auto start = Clock::now();
            for (int i = 0; i < inner; ++i) {
                acc += fn(i);
            }
            bestUs = std::min(bestUs, msSince(start) * 1000.0 / static_cast<double>(inner));
        }
        std::printf("  %-34s %9.3f us   (sink %lld)\n", name, bestUs, acc);
    };

    std::printf("\n== navigation decomposition, minimum of %d runs, on walkable ground ==\n", repeats);
    best("WorldMap::height", [&](int i) { return static_cast<int>(map->height(pts[i % 64])); }, 4096);
    best("WorldMap::normal (4 heights)",
         [&](int i) { return static_cast<int>(map->normal(pts[i % 64], 0.5f).y * 100.0f); }, 4096);
    best("WorldMap::waterSurface",
         [&](int i) { const float w = map->waterSurface(pts[i % 64]);
                      return std::isfinite(w) ? 1 : 0; }, 4096);
    best("WorldMap::sample (the lot)",
         [&](int i) { return static_cast<int>(map->sample(pts[i % 64], 0.5f).height); }, 4096);
    best("ClearanceField::canopyHeight",
         [&](int i) { return static_cast<int>(query.clearance.canopyHeight(pts[i % 64])); }, 4096);
    best("TerrainQuery::at", [&](int i) { return query.at(pts[i % 64]).walkable ? 1 : 0; }, 4096);
    best("Navigator::sample", [&](int i) { return nav.sample(pts[i % 64]).navigable ? 1 : 0; }, 4096);
    best("ObstacleField::segmentBlocked 6 m",
         [&](int i) {
             return nav.obstacles() != nullptr &&
                            nav.obstacles()->segmentBlocked(pts[i % 64], far6[i % 64], nav.filter(0.0f))
                        ? 1
                        : 0;
         },
         4096);
    best("Navigator::pathClear 6 m",
         [&](int i) { return nav.pathClear(pts[i % 64], far6[i % 64]) ? 1 : 0; }, 2048);
    best("Navigator::pathClear 30 m",
         [&](int i) { return nav.pathClear(pts[i % 64], far30[i % 64]) ? 1 : 0; }, 1024);
    best("Navigator::steer 6 m lookahead",
         [&](int i) { return nav.steer(pts[i % 64], far30[i % 64], 6.0f).x != 0.0f ? 1 : 0; }, 1024);
    std::printf("  control: every composite must cost at least the parts under it\n");
}

// Does the grid answer the same question the world does, and do characters walk the same line?
// (P7 item 1, ADR-295.)
//
// A faster route that is a different route is not a faster route. `Navigator::pathClear` may now
// accept a segment on the grid's word instead of the world's, and the only thing that makes that
// legitimate is that it never accepts one the world would have refused. So this counts, over a
// great many segments of the two shapes a walker actually asks about:
//
//   * how often the grid can answer at all -- the hit rate, which is what the speed-up is worth;
//   * how often it answers **clear** where the world says **blocked** -- the only dangerous
//     direction, and the one that must be zero;
//   * and, as the control that must fail (ADR-182), the naive substitution the seek agent looked at
//     and refused: trust any walkable cell, no room margin, no gradient test. If that row reports
//     zero unsafe answers too, this arm is measuring nothing.
void gridAgreement(const entity::Navigator& nav, int samples) {
    const entity::NavGrid* grid = nav.grid();
    if (grid == nullptr || !grid->valid()) {
        std::printf("\n== grid vs world ==\n  this scene has no navigation graph\n");
        return;
    }
    entity::Navigator world = nav; // the control: the analytic path, exactly as it was
    entity::NavSettings off = world.settings();
    off.gridTrustMetres = 0.0f;
    world.setSettings(off);

    const glm::vec2 lo = nav.worldMin();
    const glm::vec2 hi = nav.worldMax();
    std::printf("\n== grid vs world: %d segments of each shape ==\n", samples);
    std::printf("  the grid %s for this world (build-time self-check)\n",
                grid->vouches() ? "vouches" : "does NOT vouch");
    std::printf("  the shipped rule is trust %d, slope gate %u/255, rise %.3f m/m\n",
                nav.gridTrustCells(*grid), world.gridSlopeGate(2.5f),
                static_cast<double>(world.settings().stepHeight / 2.5f * 0.667f));
    std::printf("  %-8s %-18s %8s %8s %9s %9s\n", "reach", "rule", "grid ok", "world ok", "unsafe",
                "missed");
    for (const float reach : {6.0f, 40.0f}) {
        struct Rule {
            const char* name;
            int trust;
            float rise;     // 0 = the shipped rule, derived from the step height
            int slopeGate;  // -1 = the shipped gate; 255 = no gate at all
        };
        // The last row is the control. `1e9` disables the gradient test and a trust of 1 asks only
        // that the cell itself and its neighbours be standable -- which is exactly "use the grid
        // instead of the world", the thing that was refused, measured rather than asserted.
        const Rule rules[] = { // NOLINT{"trust 1", 1, 0.0f, -1},
                              {"trust 2", 2, 0.0f, -1},
                              {"trust 3", 3, 0.0f, -1},
                              {"trust 2, no slope", 2, 0.0f, 255},
                              {"naive (control)", 1, 1e9f, 255}};
        for (const Rule& rule : rules) {
            Rng rng(77000u + static_cast<std::uint32_t>(reach)); // the same segments for every rule
            const float rise = rule.rise > 0.0f ? rule.rise
                                                : world.settings().stepHeight / 2.5f * 0.667f;
            const auto gate = rule.slopeGate < 0
                                  ? world.gridSlopeGate(2.5f)
                                  : static_cast<std::uint8_t>(rule.slopeGate);
            long long gridOk = 0;
            long long worldOk = 0;
            long long unsafe = 0;
            long long missed = 0;
            long long reasons[9] = {};
            int taken = 0;
            while (taken < samples) {
                const glm::vec2 a = lo + (hi - lo) * glm::vec2(rng.nextFloat(), rng.nextFloat());
                if (!world.sample(a).navigable) {
                    continue; // a walker asks from where it is standing, and it is standing somewhere
                }
                ++taken;
                const float angle = rng.range(0.0f, 6.2831853f);
                const glm::vec2 b = a + glm::vec2(std::cos(angle), std::sin(angle)) * reach;
                const bool truth = world.pathClear(a, b);
                // The grid's half of the answer, under this rule, with the solids settled the way
                // `pathClear` settles them -- swept, exact, and never asked of the grid.
                const bool solid = nav.obstacles() != nullptr &&
                                   nav.obstacles()->segmentBlocked(a, b, nav.filter(nav.groundHeight(a)));
                bool fast = !solid && grid->segmentTerrainClear(a, b, rule.trust, rise, gate);
                if (fast && rule.rise <= 0.0f) {
                    // The point obstacle test the fast path also runs. Off for the control row, so
                    // the control stays the naive substitution it is there to represent.
                    const int steps = std::max(1, static_cast<int>(std::ceil(reach / 2.5f)));
                    for (int k = 1; k <= steps && fast; ++k) {
                        const glm::vec2 q =
                            a + (b - a) * (static_cast<float>(k) / static_cast<float>(steps));
                        fast = !nav.obstructed(q, grid->groundAt(q));
                    }
                }
                worldOk += truth ? 1 : 0;
                gridOk += fast ? 1 : 0;
                if (fast && !truth) {
                    ++unsafe; // the grid said walk and the world said no. Must be 0.
                    // *Why* the world said no, walked exactly the way `pathClear` walks it. A count
                    // of disagreements says the rule is wrong; this says which rule, and the two
                    // lead to completely different fixes.
                    const float stepLen = 2.5f;
                    const int steps = std::max(1, static_cast<int>(std::ceil(reach / stepLen)));
                    float previous = world.sample(a).ground;
                    for (int k = 1; k <= steps; ++k) {
                        const float t = static_cast<float>(k) / static_cast<float>(steps);
                        const entity::NavSample sm = world.sample(a + (b - a) * t);
                        if (!sm.navigable) {
                            ++reasons[static_cast<int>(sm.reject)];
                            break;
                        }
                        if (std::abs(sm.ground - previous) > world.settings().stepHeight) {
                            ++reasons[8]; // the step test
                            break;
                        }
                        previous = sm.ground;
                    }
                }
                if (!fast && truth) {
                    ++missed; // a clear line the grid could not settle: work, not a wrong answer.
                }
            }
            std::printf("  %-8.0f %-18s %8lld %8lld %9lld %9lld", static_cast<double>(reach),
                        rule.name, gridOk, worldOk, unsafe, missed);
            if (unsafe > 0) {
                std::printf("   why:");
                for (int k = 0; k < 8; ++k) {
                    if (reasons[k] > 0) {
                        std::printf(" %s=%lld",
                                    entity::navRejectName(static_cast<entity::NavReject>(k)),
                                    reasons[k]);
                    }
                }
                if (reasons[8] > 0) {
                    std::printf(" step=%lld", reasons[8]);
                }
            }
            std::printf("\n");
        }
    }
    std::printf("  unsafe must be 0 for every rule but the control, and the control must not be\n");
    std::printf("  grid ok / world ok is the hit rate: how much of the analytic work it removes\n");
}

// The acceptance bar, stated as a measurement: do the characters walk the same line? (ADR-295.)
//
// One thing this arm does *not* have to prove, because the code makes it true by construction: the
// planned routes are identical. `NavGrid::findPath` -- A*, the string pull, the tie-break on cell
// index -- never calls `pathClear`. Only `steer` and `pathValid` do. So what a grid-assisted walker
// can differ in is the metre in front of it and the moment it decides its held route has gone
// stale, and those are what this measures.
//
// Reported over a rising horizon rather than at one time, because a walking simulation is chaotic:
// one differing steering decision at second three puts a body somewhere else at second ninety, and
// a single ninety-second number cannot tell "the line past that trunk moved by a centimetre" from
// "it took the other side of the lake". The control is the same configuration run twice, which must
// be exactly zero at every horizon -- without it a harness that quietly ran one configuration twice
// would report perfect agreement and mean nothing (ADR-182).
void routeDivergence(const entity::Navigator& nav, int count, const char* label) {
    constexpr double kHorizons[] = {1.0, 5.0, 15.0, 30.0, 60.0, 90.0};
    constexpr std::size_t kCount = sizeof(kHorizons) / sizeof(kHorizons[0]);
    struct Run {
        std::vector<std::vector<glm::vec3>> at; // positions at each horizon
        double ms = 0.0;
        double travel = 0.0;
    };
    const auto walk = [&](float trust) {
        entity::Navigator tuned = nav;
        entity::NavSettings settings = tuned.settings();
        settings.gridTrustMetres = trust;
        tuned.setSettings(settings);
        entity::EntityWorld world;
        params::ParameterSet params;
        buildWorld(world, params, count, "explore", tuned);
        entity::EntityUpdate u;
        u.dt = 1.0 / 60.0;
        u.distanceDetail = false;
        u.viewPosition = glm::vec3(0.0f);
        Run run;
        run.at.resize(kCount);
        std::size_t next = 0;
        const auto steps = static_cast<int>(kHorizons[kCount - 1] * 60.0);
        const auto start = Clock::now();
        for (int f = 1; f <= steps; ++f) {
            u.time = static_cast<double>(f) / 60.0;
            world.update(u, params);
            while (next < kCount && u.time >= kHorizons[next] - 1e-9) {
                for (const auto& e : world.entities()) {
                    run.at[next].push_back(e->state().position());
                }
                ++next;
            }
        }
        run.ms = msSince(start);
        for (const auto& e : world.entities()) {
            run.travel = std::max(run.travel, static_cast<double>(glm::length(e->state().travel)));
        }
        return run;
    };
    const auto worst = [](const std::vector<glm::vec3>& a, const std::vector<glm::vec3>& b) {
        double d = 0.0;
        for (std::size_t i = 0; i < std::min(a.size(), b.size()); ++i) {
            d = std::max(d, static_cast<double>(glm::length(a[i] - b[i])));
        }
        return d;
    };
    const auto moved = [](const std::vector<glm::vec3>& a, const std::vector<glm::vec3>& b) {
        int n = 0;
        for (std::size_t i = 0; i < std::min(a.size(), b.size()); ++i) {
            n += glm::length(a[i] - b[i]) > 1e-4f ? 1 : 0;
        }
        return n;
    };

    std::printf("\n== do the routes change: %d explorers on %s ==\n", count, label);
    const Run off = walk(0.0f);
    const Run offAgain = walk(0.0f);
    const Run on = walk(nav.settings().gridTrustMetres);
    std::printf("  analytic only   %8.0f ms of simulation, furthest body %7.2f m\n", off.ms, off.travel);
    std::printf("  grid-assisted   %8.0f ms of simulation, furthest body %7.2f m   (%.2fx)\n", on.ms,
                on.travel, on.ms > 0.0 ? off.ms / on.ms : 0.0);
    std::printf("  %6s %14s %14s %10s\n", "t", "control |dx|", "grid |dx|", "bodies moved");
    for (std::size_t i = 0; i < kCount; ++i) {
        std::printf("  %5.0fs %13.6f m %13.6f m %6d / %zu\n", kHorizons[i],
                    worst(off.at[i], offAgain.at[i]), worst(off.at[i], on.at[i]),
                    moved(off.at[i], on.at[i]), off.at[i].size());
    }
    std::printf("  control must be 0.000000 m at every horizon; the furthest-body column must not\n"
                "  be 0 m, or the arm simulated nothing and two of nothing agree perfectly\n");
}

// What a door that closes costs (ADR-297). A rect rebuild against a whole one, on the real grid.
void dynamicRebuild(const entity::Navigator& nav, int repeats) {
    const entity::NavGrid* grid = nav.grid();
    std::printf("\n== a partial rebuild against a whole one ==\n");
    if (grid == nullptr || !grid->valid()) {
        std::printf("  this scene has no navigation graph\n");
        return;
    }
    auto& mutable_grid = const_cast<entity::NavGrid&>(*grid);
    const glm::vec2 middle = (nav.worldMin() + nav.worldMax()) * 0.5f;
    for (const float half : {4.0f, 12.0f, 40.0f}) {
        double best = std::numeric_limits<double>::max();
        for (int r = 0; r < repeats; ++r) {
            best = std::min(best, mutable_grid.rebuildRect(nav, middle - half, middle + half));
        }
        std::printf("  %5.0f m rect: %8.3f ms   (a whole build is %.1f ms)\n",
                    static_cast<double>(half * 2.0f), best, grid->stats().buildMs);
    }
    std::printf("  the control: every rebuild above added no solids, so the graph must be unchanged"
                " -- %zu walkable, %zu region(s)\n",
                grid->stats().walkable, grid->stats().regions);
}

void gridStats(const entity::Navigator& nav) {
    const entity::NavGrid* grid = nav.grid();
    std::printf("\n== navigation graph ==\n");
    if (grid == nullptr) {
        std::printf("  no grid built\n");
        return;
    }
    const entity::NavGridStats s = grid->stats();
    std::printf("  %d x %d cells at %.2f m = %zu cells; walkable %zu, water %zu, blocked %zu\n",
                s.width, s.height, s.cellSize, s.cells, s.walkable, s.water, s.blocked);
    std::printf("  regions %zu, largest %zu, stranded %zu; terrain-standable %zu; build %.1f ms\n",
                s.regions, s.largestRegion, s.stranded, s.terrain, s.buildMs);
    std::printf("  grid vouches for this world's terrain: %s (%zu sampled walks checked, %zu wrong)\n",
                s.trusted ? "yes" : "NO", s.trustChecks, s.trustFailures);
}

// Does play(t) equal seek(t)? And does the coarse LOD band change the answer?
// What a timeline click costs. `Engine::seekSeconds` re-simulates the whole entity world forward
// at a fixed 1/60 from up to 90 s back, synchronously, on the main thread: this is that, priced
// against the cast size the brief wants.
void seekCost(const entity::Navigator& nav, int repeats) {
    std::printf("\n== scrub cost: one EntityWorld::seek(90 s) on the main thread ==\n");
    std::printf("%-10s %9s %9s %9s %9s\n", "profile", "10", "50", "100", "250");
    for (const char* profile : {"wander", "explore"}) {
        std::printf("%-10s", profile);
        for (const int n : {10, 50, 100, 250}) {
            double best = std::numeric_limits<double>::max();
            for (int r = 0; r < repeats; ++r) {
                entity::EntityWorld world;
                params::ParameterSet params;
                buildWorld(world, params, n, profile, nav);
                const auto start = Clock::now();
                world.seek(90.0, &params, nullptr, 1.0 / 60.0, entity::SeekBudget{});
                best = std::min(best, msSince(start));
            }
            std::printf(" %8.0fms", best);
            std::fflush(stdout);
        }
        std::printf("\n");
    }
}

void determinism(const entity::Navigator& nav, double target) {
    std::printf("\n== determinism ==\n");
    const auto positionsAfterPlay = [&](double dt, bool detail, float viewDistance) {
        entity::EntityWorld world;
        params::ParameterSet params;
        buildWorld(world, params, 8, "explore", nav, detail);
        entity::EntityUpdate u;
        u.dt = dt;
        u.distanceDetail = detail;
        u.viewPosition = glm::vec3(viewDistance, 0.0f, 0.0f);
        const auto steps = static_cast<int>(target / dt);
        for (int f = 1; f <= steps; ++f) {
            u.time = static_cast<double>(f) * dt;
            world.update(u, params);
        }
        std::vector<glm::vec3> out;
        for (const auto& e : world.entities()) {
            out.push_back(e->state().position());
        }
        return out;
    };
    const auto positionsAfterSeek = [&](double step) {
        entity::EntityWorld world;
        params::ParameterSet params;
        buildWorld(world, params, 8, "explore", nav);
        world.seek(target, &params, nullptr, step, entity::SeekBudget{});
        std::vector<glm::vec3> out;
        for (const auto& e : world.entities()) {
            out.push_back(e->state().position());
        }
        return out;
    };
    const auto worst = [](const std::vector<glm::vec3>& a, const std::vector<glm::vec3>& b) {
        double d = 0.0;
        for (std::size_t i = 0; i < std::min(a.size(), b.size()); ++i) {
            d = std::max(d, static_cast<double>(glm::length(a[i] - b[i])));
        }
        return d;
    };

    // A jittered frame time, which is what an interactive session actually delivers.
    const auto positionsAfterJitteredPlay = [&]() {
        entity::EntityWorld world;
        params::ParameterSet params;
        buildWorld(world, params, 8, "explore", nav);
        entity::EntityUpdate u;
        u.distanceDetail = false;
        u.viewPosition = glm::vec3(0.0f);
        double t = 0.0;
        int i = 0;
        while (t < target) {
            const double dt = (i % 3 == 0) ? 1.0 / 90.0 : (i % 3 == 1) ? 1.0 / 45.0 : 1.0 / 60.0;
            t = std::min(t + dt, target);
            u.dt = dt;
            u.time = t;
            world.update(u, params);
            ++i;
        }
        std::vector<glm::vec3> out;
        for (const auto& e : world.entities()) {
            out.push_back(e->state().position());
        }
        return out;
    };

    const auto play60 = positionsAfterPlay(1.0 / 60.0, false, 0.0f);
    const auto play60again = positionsAfterPlay(1.0 / 60.0, false, 0.0f);
    const auto play30 = positionsAfterPlay(1.0 / 30.0, false, 0.0f);
    const auto playJitter = positionsAfterJitteredPlay();
    const auto seek60 = positionsAfterSeek(1.0 / 60.0);
    const auto seek60again = positionsAfterSeek(1.0 / 60.0);
    const auto seek30 = positionsAfterSeek(1.0 / 30.0);
    // Behaviour LOD on, with the camera near and far. If the coarse band is honest, these agree.
    const auto lodNear = positionsAfterPlay(1.0 / 60.0, true, 0.0f);
    const auto lodFar = positionsAfterPlay(1.0 / 60.0, true, 200.0f);   // inside cull, past full detail
    const auto lodCulled = positionsAfterPlay(1.0 / 60.0, true, 2000.0f); // past the cull band

    std::printf("  worst |dx| over 8 explorers at t = %.1f s\n", target);
    std::printf("  %-44s %10.6f m   <- control: must be 0\n",
                "play(60Hz) vs play(60Hz) again", worst(play60, play60again));
    std::printf("  %-44s %10.6f m   <- control: must NOT be 0\n",
                "play(60Hz) vs play(30Hz)", worst(play60, play30));
    std::printf("  %-44s %10.6f m\n", "seek(60Hz step) vs seek(60Hz step) again",
                worst(seek60, seek60again));
    std::printf("  %-44s %10.6f m   <- the claim under test\n", "play(60Hz) vs seek(60Hz step)",
                worst(play60, seek60));
    std::printf("  %-44s %10.6f m   <- what a real session plays\n",
                "play(jittered 45-90Hz) vs seek(60Hz step)", worst(playJitter, seek60));
    std::printf("  %-44s %10.6f m\n", "seek(60Hz step) vs seek(30Hz step)", worst(seek60, seek30));
    std::printf("  %-44s %10.6f m   <- does behaviour LOD change the answer\n",
                "play(LOD, camera near) vs play(LOD, far)", worst(lodNear, lodFar));
    std::printf("  %-44s %10.6f m\n", "play(LOD off) vs play(LOD on, camera near)",
                worst(play60, lodNear));
    std::printf("  %-44s %10.6f m   <- a culled body never moves\n",
                "play(LOD on, near) vs play(LOD on, culled)", worst(lodNear, lodCulled));
}

} // namespace

int main(int argc, char** argv) {
    const fs::path scenePath =
        argc > 1 ? fs::path(argv[1]) : fs::path("examples/world/glowmere-valley-2.scene.json");
    const int frames = argc > 2 ? std::atoi(argv[2]) : 120;
    const int repeats = argc > 3 ? std::atoi(argv[3]) : 3;
    // Which sections to run, as letters: g grid, n navigation decomposition, a grid-vs-world
    // agreement, v route divergence, p perception scan, r primitives, s scaling,
    // k scrub cost, d determinism. A selector rather than an all-or-nothing run because the scrub
    // arm at 250 explorers takes ten minutes by itself and nobody wants to pay that to re-check a
    // microsecond.
    const std::string sections = argc > 4 ? std::string(argv[4]) : std::string("gprskd");
    const auto want = [&](char c) { return sections.find(c) != std::string::npos; };

    log::setLevel(log::Level::Error);
    assets::AssetRegistry registry{scenePath.parent_path()};
    auto loaded = scene::Composition::loadFile(scenePath, registry);
    if (!loaded) {
        std::fprintf(stderr, "could not load '%s': %s\n", scenePath.string().c_str(),
                     loaded.error().message.c_str());
        return 1;
    }
    scene::Composition& comp = **loaded;
    params::ParameterSet params;
    params::Modulator modulator;
    comp.attach(params, modulator);
    (void)comp.nodeCorners(comp.nodes().empty() ? std::string{} : comp.nodes().front()->name);

    const entity::Navigator nav = comp.entityWorld().navigator();
    std::printf("scene: %s\n", scenePath.string().c_str());
    std::printf("entities in the scene as authored: %zu\n", comp.entityWorld().size());
    std::printf("navigator valid: %s; obstacles recorded: %s\n", nav.valid() ? "yes" : "NO",
                nav.obstacles() != nullptr ? "yes" : "NO");
    if (nav.obstacles() != nullptr) {
        std::printf("obstacle field: %zu solids, %zu blocking\n", nav.obstacles()->size(),
                    nav.obstacles()->blockingCount());
    }
    std::printf("interest points: %zu\n", comp.entityWorld().interestPoints().size());
    if (want('g')) { gridStats(nav); }
    if (want('n')) { navCost(nav, repeats); }
    if (want('y')) { dynamicRebuild(nav, repeats); }
    if (want('a')) { gridAgreement(nav, 20000); }
    if (want('v')) { routeDivergence(nav, 24, scenePath.filename().string().c_str()); }
    if (want('p')) { perceptionScan(comp.entityWorld(), nav, repeats); }
    if (want('r')) { primitives(nav, repeats); }
    if (want('s')) { scaling(nav, frames, repeats); }
    if (want('k')) { seekCost(nav, repeats); }
    if (want('d')) { determinism(nav, 30.0); }
    return 0;
}
