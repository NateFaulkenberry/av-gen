// TEMPORARY DIAGNOSTIC (ADR-273). Delete with the phase it priced.
//
// ADR-267 priced a scrub at the cast size the character-AI plan wants and stopped there: 161 s at
// 100 explorers, 594 s at 250. What it did not say is *where inside one entity-step* that goes, and
// that is the question a fix has to answer before it picks a mechanism -- a shorter window, a
// cheaper step and a skipped entity are three different repairs and only a breakdown chooses
// between them.
//
// Arms, each with its control (ADR-182):
//
//   pop     What the authored scene actually contains, by behaviour kind. Without it every
//           per-kind number below is a cost for a population nobody has.
//   kind    One behaviour kind at a time, N bodies, one seek. The per-entity-step cost of each
//           behaviour in the vocabulary, measured rather than reasoned about. Its control is the
//           `none` row: N entities carrying no behaviour at all, which is the loop and the
//           bookkeeping with nothing in them.
//   window  The same seek at a range of window lengths. Cost must be linear in the window and the
//           control is that it is: a window arm whose cost does not move with the window is
//           measuring something other than the re-simulation.
//   hist    Does the answer at `target` depend on how far back the replay started? Per kind, the
//           shortest replay that lands on the same state as the full one. A kind that reports
//           "1 step" and a kind that reports "the whole window" are the two halves of the
//           classification `EntityWorld::seek` now uses, and this is where the classification gets
//           checked against a measurement rather than against a reading of the source.

#include "assets/asset_registry.hpp"
#include "core/log.hpp"
#include "entity/entity.hpp"
#include "entity/navigation.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <map>
#include <string>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

using Clock = std::chrono::steady_clock;

double msSince(Clock::time_point t) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
}

struct Population {
    std::vector<entity::EntityDesc> descs;
    std::vector<entity::NodeBinding> bindings;
};

// The behaviour list behind each label, and *settings that make it do something*.
//
// This is not decoration. `spin` with its defaults has a base rate of zero and fires on an audio
// event that a null bus never raises; `bank` leans into motion and produces none of its own;
// `liveliness` bobs in proportion to a speed nothing gives it. Measured with their defaults, all
// three report "no history" -- not because their state does not accumulate but because there is
// nothing for it to accumulate. An arm that cannot move a body cannot measure one (ADR-182, and
// ADR-267 made the same mistake in the same file's ancestor), so `bank` is paired with the `drift`
// it exists to lean into and the rest are given inputs.
//
// The arms that still cannot be made to move alone -- `lookAt` needs a named point of interest,
// `interest` needs a signal bus -- keep their defaults and are read against the travel control
// beside them, which says in the output that they did not move.
std::vector<std::pair<std::string, nlohmann::json>> behaviorsFor(const std::string& label) {
    if (label == "none") {
        return {};
    }
    if (label == "explore") {
        return {{"explore",
                 {{"speed", 1.7}, {"bodyRadius", 2.4}, {"headroom", 7.0}, {"maxRange", 160.0}}}};
    }
    if (label == "spin") {
        return {{"spin", {{"signal", "none"}, {"baseRate", 40.0}, {"damping", 1.5}}}};
    }
    if (label == "drift+bank") {
        return {{"drift", {{"radius", 6.0}, {"rate", 0.25}}}, {"bank", {{"degrees", 12.0}}}};
    }
    if (label == "wander+liveliness") {
        return {{"wander", {{"radius", 18.0}, {"speed", 1.1}}},
                {"liveliness", {{"bounce", 0.32}, {"sway", 6.0}}}};
    }
    if (label == "orbit") {
        return {{"orbit", {{"radius", 12.0}, {"rate", 8.0}}}};
    }
    return {{label, nlohmann::json::object()}};
}

// N bodies of one behaviour kind, spread over the walkable ground. The bindings are not decoration:
// without one an entity's anchor is the origin, every body starts stacked and nothing travels --
// which is how ADR-267's first probe reported 0 m for a control that must not be zero.
Population population(int n, const std::string& kind, const entity::Navigator& nav) {
    const glm::vec2 lo = nav.worldMin();
    const glm::vec2 hi = nav.worldMax();
    const int side = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(n))));
    Population out;
    for (int i = 0; i < n; ++i) {
        entity::EntityDesc d;
        d.name = kind + "-" + std::to_string(i);
        d.node = d.name;
        for (const auto& [k, settings] : behaviorsFor(kind)) {
            entity::BehaviorDesc bd;
            bd.kind = k;
            bd.settings = settings;
            bd.settings["kind"] = k;
            d.behaviors.push_back(std::move(bd));
        }
        d.gait.walkSpeed = 1.6f;
        out.descs.push_back(std::move(d));
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

void buildWorld(entity::EntityWorld& world, params::ParameterSet& params, int n,
                const std::string& kind, const entity::Navigator& nav) {
    Population pop = population(n, kind, nav);
    world.setNavigator(nav);
    world.setBindings(pop.bindings);
    registerNodes(params, pop);
    world.setEntities(std::move(pop.descs), 1234u);
    world.registerParameters(params, "entity/");
    world.bind(params, "entity/");
}

// The frame *after* the scrub, which is the only thing anybody sees.
//
// `EntityWorld::seek` deliberately writes nothing to the parameter set: the behaviours' offsets --
// hover's rise, bank's lean, spin's angle -- are folded onto the node's transform by the next
// ordinary update, from state the seek left behind. So comparing `state().position()` after a seek
// compares the half of the answer that navigation writes and none of the half that everything else
// writes, and an arm built on it reports 0.000000 m for eight behaviours that cannot move a body at
// all. It did, in the first version of this file, and that is why this exists: run one ordinary
// frame on top and read the node transforms, which is the question "does the scrubbed frame look
// like the played one" asked in the terms the renderer answers it in.
std::vector<glm::vec3> poseAfterNextFrame(entity::EntityWorld& world, params::ParameterSet& params,
                                          double target) {
    params.resetFinals();
    entity::EntityUpdate u;
    u.time = target + 1.0 / 60.0;
    u.dt = 1.0 / 60.0;
    u.distanceDetail = false;
    world.update(u, params);
    std::vector<glm::vec3> out;
    for (const auto& e : world.entities()) {
        const std::string prefix = "nodes/" + e->desc().node + "/";
        if (const auto* p = params.findAs<glm::vec3>(prefix + "position")) {
            out.push_back(p->value());
        }
        if (const auto* r = params.findAs<glm::vec3>(prefix + "rotation")) {
            out.push_back(r->value());
        }
    }
    return out;
}

double worst(const std::vector<glm::vec3>& a, const std::vector<glm::vec3>& b) {
    double d = 0.0;
    for (std::size_t i = 0; i < std::min(a.size(), b.size()); ++i) {
        d = std::max(d, static_cast<double>(glm::length(a[i] - b[i])));
    }
    return d;
}

double travelled(const entity::EntityWorld& world) {
    double d = 0.0;
    for (const auto& e : world.entities()) {
        d = std::max(d, static_cast<double>(glm::length(e->state().travel)));
    }
    return d;
}

const char* const kKinds[] = {"none",   "hover",  "drift",     "drift+bank", "spin",  "orbit",
                              "ground", "lookAt", "interest",  "wander",     "explore",
                              "wander+liveliness"};

void populationOfScene(const scene::Composition& comp) {
    std::map<std::string, int> kinds;
    int bodies = 0;
    for (const auto& e : comp.entityWorld().entities()) {
        ++bodies;
        for (const auto& b : e->desc().behaviors) {
            ++kinds[b.kind];
        }
    }
    std::printf("\n== the authored population ==\n");
    std::printf("  %d entities\n", bodies);
    for (const auto& [kind, count] : kinds) {
        std::printf("  %-12s %3d\n", kind.c_str(), count);
    }
}

void perKind(const entity::Navigator& nav, int n, double window, int repeats) {
    std::printf("\n== one EntityWorld::seek(%.0f s) by behaviour kind, %d bodies, min of %d ==\n",
                window, n, repeats);
    std::printf("  %-18s %10s %12s   %s\n", "kind", "seek ms", "us/body-step", "max travel");
    for (const char* kind : kKinds) {
        double best = std::numeric_limits<double>::max();
        double travel = 0.0;
        for (int r = 0; r < repeats; ++r) {
            entity::EntityWorld world;
            params::ParameterSet params;
            buildWorld(world, params, n, kind, nav);
            const auto start = Clock::now();
            world.seek(window, &params, nullptr, 1.0 / 60.0,
                       entity::SeekBudget{.maxSeconds = window, .maxBodySteps = 0});
            best = std::min(best, msSince(start));
            travel = std::max(travel, travelled(world));
        }
        const double bodySteps = std::floor(window * 60.0) * static_cast<double>(n);
        std::printf("  %-18s %10.1f %12.3f   %8.2f m\n", kind, best, best * 1000.0 / bodySteps,
                    travel);
        std::fflush(stdout);
    }
    std::printf("  (`none` is the control: the loop with nothing in it)\n");
}

// The shortest replay that lands where the full one does. A kind whose answer at `target` is a pure
// function of `target` needs one step; a kind that integrates needs the window.
void historyDepth(const entity::Navigator& nav, double window) {
    std::printf("\n== how much history each kind's answer at t = %.0f s actually depends on ==\n",
                window);
    std::printf("  %-18s %12s %12s %12s %11s  verdict\n", "kind", "|full-1step|", "|full-2step|",
                "|full-half|", "spread");
    for (const char* kind : kKinds) {
        const auto at = [&](double span, double target = -1.0) {
            const double t = target < 0.0 ? window : target;
            entity::EntityWorld world;
            params::ParameterSet params;
            buildWorld(world, params, 8, kind, nav);
            world.seek(t, &params, nullptr, 1.0 / 60.0,
                       entity::SeekBudget{.maxSeconds = span, .maxBodySteps = 0});
            return poseAfterNextFrame(world, params, t);
        };
        const auto full = at(window);
        const double d1 = worst(full, at(1.0 / 60.0));
        const double d2 = worst(full, at(2.0 / 60.0));
        const double dh = worst(full, at(window * 0.5));
        // The arm's own control: how far the thirty seconds of replay actually moved the node,
        // against the same scene seeked to zero. A row that reads 0 here moved nothing between the
        // two seconds, so its history verdict is a statement about an inert behaviour and not
        // about history.
        const double moved = worst(full, at(window, 0.0));
        const char* verdict = moved == 0.0 ? "INERT -- says nothing"
                              : d1 == 0.0   ? "no history"
                              : d2 == 0.0 ? "one step back"
                              : dh == 0.0 ? "half the window is enough"
                                          : "the whole window";
        std::printf("  %-18s %12.6f %12.6f %12.6f %9.2f m  %s\n", kind, d1, d2, dh, moved, verdict);
        std::fflush(stdout);
    }
}

void windowScaling(const entity::Navigator& nav, int n, const char* kind, int repeats) {
    std::printf("\n== cost against window length, %d %s bodies, min of %d ==\n", n, kind, repeats);
    for (const double window : {5.0, 15.0, 45.0, 90.0}) {
        double best = std::numeric_limits<double>::max();
        for (int r = 0; r < repeats; ++r) {
            entity::EntityWorld world;
            params::ParameterSet params;
            buildWorld(world, params, n, kind, nav);
            const auto start = Clock::now();
            world.seek(90.0, &params, nullptr, 1.0 / 60.0,
                       entity::SeekBudget{.maxSeconds = window, .maxBodySteps = 0});
            best = std::min(best, msSince(start));
        }
        std::printf("  window %5.1f s: %9.1f ms\n", window, best);
        std::fflush(stdout);
    }
    std::printf("  (the control is linearity: a cost that does not move with the window is not the "
                "re-simulation)\n");
}

// The scene's *own* entity world, seeked, at a range of price caps.
//
// The one table the whole change turns on, because it is the only arm whose population is the one an
// editor actually has. Latency in one column and fidelity in the next, so the budget is chosen from
// a trade somebody looked at rather than from a round number -- the caps are not free, they buy
// less history, and the right-hand column is how much less in metres.
//
// The reference is the uncapped ninety-second replay, which is what the editor did before. Not a
// play: this is asking "how much does the cap move the answer", not "does the replay match a play",
// which §3 of the ADR asks separately and answers exactly.
void authoredScene(scene::Composition& comp, params::ParameterSet& params, double target,
                   int repeats) {
    std::printf("\n== the authored scene's own entity world, seeked to t = %.0f s ==\n", target);
    entity::EntityWorld& world = comp.entityWorld();
    const auto run = [&](std::uint64_t cap) {
        double best = std::numeric_limits<double>::max();
        for (int r = 0; r < repeats; ++r) {
            const auto start = Clock::now();
            world.seek(target, &params, nullptr, 1.0 / 60.0,
                       entity::SeekBudget{.maxSeconds = 90.0, .maxBodySteps = cap});
            best = std::min(best, msSince(start));
        }
        std::vector<glm::vec3> pose;
        for (const auto& e : world.entities()) {
            pose.push_back(e->visualPosition());
        }
        return std::pair{best, pose};
    };
    const auto [fullMs, fullPose] = run(0);
    const entity::EntityWorld::SeekWork fullWork = world.lastSeekWork();
    std::printf("  %zu entities; %llu deep, %llu shallow; the uncapped window is %llu steps = "
                "%llu body-steps\n",
                world.size(), static_cast<unsigned long long>(fullWork.deepBodies),
                static_cast<unsigned long long>(fullWork.shallowBodies),
                static_cast<unsigned long long>(fullWork.steps),
                static_cast<unsigned long long>(fullWork.bodySteps));
    std::printf("  %12s %10s %12s %10s   %s\n", "cap", "history s", "body-steps", "seek ms",
                "worst move vs the full replay");
    std::printf("  %12s %10.1f %12llu %10.1f   %s\n", "none (before)",
                fullWork.spanSeconds, static_cast<unsigned long long>(fullWork.bodySteps), fullMs,
                "0.000 m (it is the reference)");
    for (const std::uint64_t cap : {120000ull, 60000ull, 30000ull, 15000ull, 6000ull}) {
        const auto [ms, pose] = run(cap);
        const entity::EntityWorld::SeekWork work = world.lastSeekWork();
        double moved = 0.0;
        for (std::size_t i = 0; i < pose.size() && i < fullPose.size(); ++i) {
            moved = std::max(moved, static_cast<double>(glm::length(pose[i] - fullPose[i])));
        }
        std::printf("  %12llu %10.1f %12llu %10.1f   %.3f m\n",
                    static_cast<unsigned long long>(cap), work.spanSeconds,
                    static_cast<unsigned long long>(work.bodySteps), ms, moved);
        std::fflush(stdout);
    }
    std::printf("  (the right-hand column is the control: a cap that moves nothing is a cap that is "
                "not biting)\n");
}

} // namespace

int main(int argc, char** argv) {
    const fs::path scenePath =
        argc > 1 ? fs::path(argv[1]) : fs::path("examples/world/glowmere-valley-2.scene.json");
    const int n = argc > 2 ? std::atoi(argv[2]) : 25;
    const int repeats = argc > 3 ? std::atoi(argv[3]) : 2;
    const std::string sections = argc > 4 ? std::string(argv[4]) : std::string("pakwh");
    const auto want = [&](char c) { return sections.find(c) != std::string::npos; };

    log::setLevel(log::Level::Error);
    assets::AssetRegistry registry{scenePath.parent_path()};
    auto loaded = scene::Composition::loadFile(scenePath.filename(), registry);
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
    std::printf("scene: %s\nnavigator valid: %s\n", scenePath.string().c_str(),
                nav.valid() ? "yes" : "NO");
    if (want('p')) { populationOfScene(comp); }
    if (want('k')) { perKind(nav, n, 90.0, repeats); }
    if (want('w')) { windowScaling(nav, n, "explore", repeats); }
    if (want('h')) { historyDepth(nav, 30.0); }
    if (want('a')) { authoredScene(comp, params, 90.0, repeats); }
    return 0;
}
