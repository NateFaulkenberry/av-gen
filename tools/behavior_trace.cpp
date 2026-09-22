// avgen_behavior_trace -- Phase D §58 / §64: "simulate scene for N seconds and produce a behaviour
// trace". The offline half of "why is this character doing that": no window, no GPU, the same
// entity update the application runs, and one line per decision any character makes.
//
//   avgen_behavior_trace <scene.json> [--seconds N] [--hz H] [--explain T] [--entity NAME]
//                        [--repeat] [--seek T]
//
//   --seconds N   simulate N seconds of timeline (default 120)
//   --hz H        at a fixed step of 1/H seconds (default 60)
//   --explain T   print the §41 "why" report for every deciding character at second T
//   --entity NAME only print lines for this character
//   --repeat      run the whole simulation twice and report whether the traces are identical
//                 (§64: "running the same simulation twice should produce the same trace")
//   --seek T      after playing, seek to T and compare every deciding character's committed
//                 option with the played run at T (ADR-360: a scrubbed frame equals a played one)
//
// Exit status is non-zero when --repeat or --seek finds a difference, so it can gate a script.

#include "assets/asset_registry.hpp"
#include "core/log.hpp"
#include "core/time.hpp"
#include "entity/behavior_trace.hpp"
#include "entity/entity.hpp"
#include "organism/mushroom.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "scene/tree_generated.hpp"
#include "signals/signal_bus.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <set>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

struct Options {
    fs::path scene;
    double seconds = 120.0;
    double hz = 60.0;
    double explainAt = -1.0;
    std::string entity;
    bool repeat = false;
    double seekTo = -1.0;
    // --options FROM TO: every decision tick's scored options for --entity, in that window. The
    // instrument for "why did it switch there".
    double optionsFrom = -1.0;
    double optionsTo = -1.0;
    // --crowd N [REPEATS]: §39/§44. Clone the scene's first deciding character to N bodies with
    // varied personalities, simulate, and report cost (minimum over repeats, ADR-170) and
    // structural counts. No trace printed.
    int crowd = 0;
    int repeats = 3;
    // --scrub T: the application's scrub (EntityWorld::seek with the engine's budget, 90 s and
    // 180,000 body-steps) timed as a minimum over --repeats, and each deciding character's position
    // and choice after it compared with a play to the same second. Exit 0; it reports, it does not
    // judge -- the director is reset by a real scrub, so a scene with staged bodies can differ for
    // reasons that are not the decider's.
    double scrub = -1.0;
    // --legacy-scrub: the pre-ADR-671 scrub (director reset, entities replayed alone), for a
    // before/after cost comparison on one binary and one machine state.
    bool legacyScrub = false;
    // --seek-mode checkpointed|window|full (ADR-700): which replay the scrub uses. Default is the
    // product's, checkpointed.
    entity::SeekMode seekMode = entity::SeekMode::Checkpointed;
    // --warm SECONDS (ADR-700): before timing, seek once to SECONDS on the same composition so the
    // checkpoints up to there exist, then time scrubs to T on that composition -- the cost after the
    // first scrub, which is the one an owner pays on every click after it. Without --warm each
    // repeat is a fresh load, which is the first-time cost.
    double warm = -1.0;
    // --interval SECONDS: the checkpoint interval (ADR-700), for choosing it.
    double interval = -1.0;
};

// N deciding bodies from the scene's first one: a grid 6 m apart around it, each with its own seed
// and a personality drawn from a hash of its index (D2: a seed and an index, never a stream).
nlohmann::json crowdScene(nlohmann::json doc, int n) {
    nlohmann::json* proto = nullptr;
    for (auto& e : doc["entities"]) {
        if (e.contains("behaviors")) {
            for (const auto& b : e["behaviors"]) {
                if (b.value("kind", "") == "decide") {
                    proto = &e;
                }
            }
        }
        if (proto != nullptr) {
            break;
        }
    }
    if (proto == nullptr || n < 1) {
        return doc;
    }
    const nlohmann::json protoEntity = *proto;
    nlohmann::json protoNode;
    for (const auto& node : doc["nodes"]) {
        if (node["name"] == protoEntity.value("node", protoEntity["name"].get<std::string>())) {
            protoNode = node;
        }
    }
    const auto hash = [](std::uint32_t x) {
        x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
        return static_cast<float>(x) / 4294967296.0f;
    };
    const int side = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(n))));
    for (int i = 1; i < n; ++i) {
        const std::string name = "crowd-" + std::to_string(i);
        nlohmann::json node = protoNode;
        node["name"] = name;
        node["position"][0] = protoNode["position"][0].get<float>() + 6.0f * static_cast<float>(i % side);
        node["position"][2] = protoNode["position"][2].get<float>() + 6.0f * static_cast<float>(i / side);
        doc["nodes"].push_back(node);
        nlohmann::json e = protoEntity;
        e["name"] = name;
        e["node"] = name;
        e["seed"] = 9000 + i;
        const auto u = static_cast<std::uint32_t>(i);
        e["personality"] = {{"curiosity", hash(u * 3 + 1)},
                            {"caution", hash(u * 3 + 2)},
                            {"sociability", hash(u * 3 + 3)},
                            {"eventSensitivity", 0.5},
                            {"attentionSpan", 0.5}};
        doc["entities"].push_back(e);
    }
    return doc;
}

struct Run {
    std::vector<std::string> lines;
    std::vector<std::string> explained;
    // At the end of the run: every deciding character's committed option and position.
    std::vector<std::string> finalState;
    int exit = 0;
    double wallSeconds = 0.0;
    long long steps = 0;
    std::size_t deciders = 0;
    std::size_t decisions = 0;
    std::size_t distinctChoices = 0;
    double behaviourSeconds = 0.0; // inside Composition::updateBehaviour: director + entity step
    std::vector<float> reach; // per entity: furthest it got from its anchor, on the ground
    std::vector<std::string> reachLines;
};

std::string stateLine(const entity::Entity& e) {
    entity::DecisionDebug d;
    for (const auto& b : e.behaviors()) {
        if (b->decisionDebug(d)) {
            const glm::vec3 p = e.state().position();
            return std::string(e.name()) + " " + std::string(d.chosen) + " [" +
                   std::string(d.subject) + "] at " + std::to_string(p.x) + "," +
                   std::to_string(p.z);
        }
    }
    return {};
}

Run simulate(const Options& o, bool withExplain) {
    Run run;
    assets::AssetRegistry registry{o.scene.parent_path()};
    std::ifstream in(o.scene);
    nlohmann::json doc = nlohmann::json::parse(in, nullptr, false);
    if (o.crowd > 0) {
        doc = crowdScene(std::move(doc), o.crowd);
    }
    // A file load when nothing was changed, so relative profile references resolve against the
    // file (a document built by `fromJson` carries no path -- the ADR-618 note).
    auto loaded = o.crowd > 0 ? scene::Composition::fromJson(doc, registry)
                              : scene::Composition::loadFile(o.scene, registry);
    if (!loaded) {
        std::fprintf(stderr, "could not load '%s': %s\n", o.scene.string().c_str(),
                     loaded.error().message.c_str());
        run.exit = 2;
        return run;
    }
    scene::Composition& comp = **loaded;
    params::ParameterSet params;
    params::Modulator modulator;
    signals::SignalBus bus;
    comp.attach(params, modulator);
    comp.setViewport(1280, 720);
    comp.scene().detailLimits.entityDistanceCull = false;

    entity::BehaviorTraceRecorder recorder;
    const double step = 1.0 / o.hz;
    const auto started = std::chrono::steady_clock::now();
    const auto frames = static_cast<long long>(std::llround(o.seconds * o.hz));
    const auto explainFrame =
        o.explainAt >= 0.0 ? static_cast<long long>(std::llround(o.explainAt * o.hz)) : -1;
    FrameTime time;
    for (long long i = 0; i <= frames; ++i) {
        time.renderTime = static_cast<double>(i) * step;
        time.deltaTime = i == 0 ? 0.0 : step;
        time.frameIndex = static_cast<std::uint64_t>(i);
        params.resetFinals();
        comp.updateFields(time, bus, modulator);
        modulator.applyRoutes(bus, params, time.deltaTime);
        const auto behaviourBegin = std::chrono::steady_clock::now();
        comp.updateBehaviour(time, bus);
        run.behaviourSeconds +=
            std::chrono::duration<double>(std::chrono::steady_clock::now() - behaviourBegin).count();
        comp.update(time);
        if (o.crowd == 0) {
            recorder.sample(comp.entityWorld());
            const auto& all = comp.entityWorld().entities();
            if (run.reach.size() != all.size()) {
                run.reach.assign(all.size(), 0.0f);
            }
            for (std::size_t e = 0; e < all.size(); ++e) {
                const glm::vec3 d = all[e]->state().position() - all[e]->state().anchor;
                run.reach[e] = std::max(run.reach[e], std::sqrt(d.x * d.x + d.z * d.z));
            }
        }
        if (withExplain && o.optionsFrom >= 0.0 && time.renderTime >= o.optionsFrom &&
            time.renderTime <= o.optionsTo) {
            for (const auto& e : comp.entityWorld().entities()) {
                if (!o.entity.empty() && e->name() != o.entity) {
                    continue;
                }
                entity::DecisionDebug d;
                for (const auto& b : e->behaviors()) {
                    if (!b->decisionDebug(d)) {
                        continue;
                    }
                    static std::uint64_t lastTick = ~0ull;
                    if (d.tick == lastTick) {
                        break;
                    }
                    lastTick = d.tick;
                    std::string line = std::to_string(time.renderTime) + " " + e->name() + " tick " +
                                       std::to_string(d.tick) + " chosen=" + std::string(d.chosen) +
                                       " holdRej? plan=" + (d.planActive ? "on" : "off") + " :";
                    for (const auto& opt : d.options) {
                        line += " " + std::string(opt.name) + "=" + std::to_string(opt.score) +
                                (opt.chosen ? "*" : "");
                    }
                    const glm::vec3 p = e->state().position();
                    line += "  pos " + std::to_string(p.x) + "," + std::to_string(p.z) + " percepts=" +
                            std::to_string(e->percepts().size());
                    std::printf("%s\n", line.c_str());
                }
            }
        }
        if (withExplain && i == explainFrame) {
            const auto& world = comp.entityWorld();
            for (std::size_t e = 0; e < world.size(); ++e) {
                if (!o.entity.empty() && world.entities()[e]->name() != o.entity) {
                    continue;
                }
                const std::string text = entity::explainCharacter(world, e);
                if (!text.empty()) {
                    run.explained.push_back(text);
                }
            }
        }
    }
    run.wallSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    run.steps = frames + 1;
    {
        std::set<std::string> subjects;
        for (const auto& e : comp.entityWorld().entities()) {
            entity::DecisionDebug d;
            for (const auto& b : e->behaviors()) {
                if (b->decisionDebug(d)) {
                    ++run.deciders;
                    run.decisions += d.historyTotal;
                    for (const auto& h : d.history) {
                        subjects.insert(h.option + "|" + h.subject);
                    }
                }
            }
        }
        run.distinctChoices = subjects.size();
    }
    run.lines = recorder.lines();
    for (std::size_t e = 0; e < run.reach.size(); ++e) {
        const auto& ent = *comp.entityWorld().entities()[e];
        if (!stateLine(ent).empty()) {
            run.reachLines.push_back(fmt::format("{:<10} furthest from its anchor: {:.1f} m", ent.name(), run.reach[e]));
        }
    }
    for (const auto& e : comp.entityWorld().entities()) {
        const std::string line = stateLine(*e);
        if (!line.empty()) {
            run.finalState.push_back(line);
        }
    }
    if (o.seekTo >= 0.0) {
        // Play to `seekTo` recorded above only when seekTo == seconds; otherwise compare against a
        // fresh play to the same instant, which is what a person scrubbing would see.
        comp.entityWorld().seek(o.seekTo, &params, &bus, step);
        std::vector<std::string> scrubbed;
        for (const auto& e : comp.entityWorld().entities()) {
            const std::string line = stateLine(*e);
            if (!line.empty()) {
                scrubbed.push_back(line);
            }
        }
        const bool same = std::abs(o.seekTo - o.seconds) < 1e-9 && scrubbed == run.finalState;
        std::printf("\n-- seek to %.2f --\n", o.seekTo);
        for (std::size_t k = 0; k < scrubbed.size(); ++k) {
            std::printf("  seek: %s\n", scrubbed[k].c_str());
            if (k < run.finalState.size()) {
                std::printf("  play: %s\n", run.finalState[k].c_str());
            }
        }
        if (std::abs(o.seekTo - o.seconds) < 1e-9) {
            std::printf("seek == play: %s\n", same ? "yes" : "NO");
            if (!same) {
                run.exit = 1;
            }
        }
    }
    return run;
}

struct Loaded {
    assets::AssetRegistry registry;
    params::ParameterSet params;
    params::Modulator modulator;
    signals::SignalBus bus;
    std::unique_ptr<scene::Composition> comp;
    explicit Loaded(const fs::path& scene) : registry(scene.parent_path()) {
        auto loaded = scene::Composition::loadFile(scene, registry);
        if (loaded) {
            comp = std::move(*loaded);
            comp->attach(params, modulator);
            comp->setViewport(1280, 720);
            comp->scene().detailLimits.entityDistanceCull = false;
        }
    }
};

int scrubReport(const Options& o) {
    const double t = o.scrub;
    // The play.
    Loaded played(o.scene);
    if (!played.comp) {
        return 2;
    }
    const double step = 1.0 / 60.0;
    FrameTime time;
    const auto frames = static_cast<long long>(std::llround(t * 60.0));
    for (long long i = 0; i <= frames; ++i) {
        time.renderTime = static_cast<double>(i) * step;
        time.deltaTime = i == 0 ? 0.0 : step;
        time.frameIndex = static_cast<std::uint64_t>(i);
        played.params.resetFinals();
        played.comp->updateFields(time, played.bus, played.modulator);
        played.modulator.applyRoutes(played.bus, played.params, time.deltaTime);
        played.comp->updateBehaviour(time, played.bus);
        played.comp->update(time);
    }
    // The scrub, timed, as the engine does it (minus the renderer).
    double best = 1e30;
    std::unique_ptr<Loaded> scrubbed;
    const entity::SeekBudget budget{.maxSeconds = 90.0, .maxBodySteps = entity::SeekBudget::kEditorBodySteps,
                                    .mode = o.seekMode};
    std::unique_ptr<Loaded> warmed;
    const auto configure = [&](Loaded& l) {
        if (o.interval > 0.0) {
            entity::CheckpointSettings cs = l.comp->entityWorld().checkpointSettings();
            cs.intervalSeconds = o.interval;
            l.comp->entityWorld().setCheckpointSettings(cs);
        }
    };
    if (o.warm >= 0.0) {
        warmed = std::make_unique<Loaded>(o.scene);
        configure(*warmed);
        const auto begin = std::chrono::steady_clock::now();
        warmed->comp->seekWithDirector(o.warm, warmed->params, budget, step);
        const auto stats = warmed->comp->entityWorld().checkpointStats();
        std::printf("warm: seek to %.2f s took %.1f ms and left %zu checkpoints, %.2f MB, last %.1f KB, "
                    "interval %.1f s\n",
                    o.warm, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count(),
                    stats.count, static_cast<double>(stats.bytes) / 1048576.0,
                    static_cast<double>(stats.lastBytes) / 1024.0, stats.intervalSeconds);
    }
    for (int r = 0; r < o.repeats; ++r) {
        auto s = warmed ? std::unique_ptr<Loaded>() : std::make_unique<Loaded>(o.scene);
        Loaded& target = warmed ? *warmed : *s;
        if (!warmed) {
            configure(target);
        }
        const auto begin = std::chrono::steady_clock::now();
        if (o.legacyScrub) {
            target.comp->director().reset(&target.comp->entityWorld(), &target.params);
            target.comp->entityWorld().seek(t, &target.params, nullptr, step,
                                            entity::SeekBudget{.maxSeconds = 90.0, .maxBodySteps = 180000,
                                                               .mode = entity::SeekMode::Window});
        } else {
            target.comp->seekWithDirector(t, target.params, budget, step);
        }
        best = std::min(best, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count());
        if (!warmed) {
            scrubbed = std::move(s);
        }
    }
    if (warmed) {
        scrubbed = std::move(warmed);
    }
    const auto work = scrubbed->comp->entityWorld().lastSeekWork();
    const auto stats = scrubbed->comp->entityWorld().checkpointStats();
    std::printf("scrub to %.2f s: %.1f ms (min of %d); mode %s%s; %llu body-steps replayed from %.2f s; "
                "exact %s; checkpoints %zu (%.2f MB), restore %.3f ms, capture %.3f ms\n",
                t, best, o.repeats, std::string(entity::seekModeName(work.mode)).c_str(),
                work.fellBack ? " (fell back)" : "",
                static_cast<unsigned long long>(work.bodySteps), work.restoredFrom < 0.0 ? 0.0 : work.restoredFrom,
                work.exact ? "yes" : "no", stats.count, static_cast<double>(stats.bytes) / 1048576.0,
                stats.lastRestoreMs, stats.lastCaptureMs);
    float worst = 0.0f;
    std::size_t differing = 0;
    for (const auto& e : played.comp->entityWorld().entities()) {
        entity::DecisionDebug pd;
        bool decides = false;
        for (const auto& b : e->behaviors()) {
            decides = decides || b->decisionDebug(pd);
        }
        if (!decides) {
            continue;
        }
        const entity::Entity* other = scrubbed->comp->entityWorld().find(e->name());
        entity::DecisionDebug sd;
        for (const auto& b : other->behaviors()) {
            (void)b->decisionDebug(sd);
        }
        const float d = glm::length(e->state().position() - other->state().position());
        worst = std::max(worst, d);
        const bool same = pd.chosen == sd.chosen && pd.subject == sd.subject;
        differing += same && d < 1e-3f ? 0 : 1;
        std::printf("  %-10s play %-22s scrub %-22s  %.4f m apart\n", e->name().c_str(),
                    std::string(pd.chosen).c_str(), std::string(sd.chosen).c_str(), d);
    }
    std::printf("deciders differing: %zu; worst %.4f m\n", differing, worst);
    // Every body, deciding or not: the craft and the animals the director moves.
    float worstAny = 0.0f;
    std::string worstName;
    std::size_t bodies = 0;
    for (const auto& e : played.comp->entityWorld().entities()) {
        const entity::Entity* other = scrubbed->comp->entityWorld().find(e->name());
        const float d = glm::length(e->visualPosition() - other->visualPosition());
        ++bodies;
        if (d > worstAny) {
            worstAny = d;
            worstName = e->name();
        }
    }
    std::printf("all %zu bodies (drawn positions): worst %.6f m (%s)\n", bodies, worstAny,
                worstName.empty() ? "-" : worstName.c_str());
    if (!worstName.empty()) {
        const entity::Entity* a = played.comp->entityWorld().find(worstName);
        const entity::Entity* b = scrubbed->comp->entityWorld().find(worstName);
        const glm::vec3 ma = a->visualPosition() - a->state().position();
        const glm::vec3 mb = b->visualPosition() - b->state().position();
        std::printf("  %s: offset play (%.4f %.4f %.4f) scrub (%.4f %.4f %.4f); speed %.4f / %.4f; "
                    "activity %s / %s; reaction %.3f / %.3f; yaw %.5f / %.5f\n",
                    worstName.c_str(), ma.x, ma.y, ma.z, mb.x, mb.y, mb.z, a->state().speed,
                    b->state().speed, entity::activityName(a->locomotion().activity),
                    entity::activityName(b->locomotion().activity), a->state().reaction,
                    b->state().reaction, a->state().yaw, b->state().yaw);
    }
    // The director's beat, as a check that the scrub landed inside the same moment of the cycle.
    std::string pb;
    std::string sb;
    for (const auto& e : played.comp->director().log()) {
        if (e.kind == stage::StageEventKind::Beat) pb = e.beat;
    }
    for (const auto& e : scrubbed->comp->director().log()) {
        if (e.kind == stage::StageEventKind::Beat) sb = e.beat;
    }
    std::printf("director beat: play '%s', scrub '%s'\n", pb.c_str(), sb.c_str());
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--seconds") {
            o.seconds = std::atof(next());
        } else if (a == "--hz") {
            o.hz = std::atof(next());
        } else if (a == "--explain") {
            o.explainAt = std::atof(next());
        } else if (a == "--entity") {
            o.entity = next();
        } else if (a == "--repeat") {
            o.repeat = true;
        } else if (a == "--legacy-scrub") {
            o.legacyScrub = true;
        } else if (a == "--interval") {
            o.interval = std::atof(next());
        } else if (a == "--warm") {
            o.warm = std::atof(next());
        } else if (a == "--seek-mode") {
            const std::string m = next();
            o.seekMode = m == "window" ? entity::SeekMode::Window
                         : m == "full" ? entity::SeekMode::FullHistory
                                       : entity::SeekMode::Checkpointed;
        } else if (a == "--scrub") {
            o.scrub = std::atof(next());
        } else if (a == "--crowd") {
            o.crowd = std::atoi(next());
        } else if (a == "--repeats") {
            o.repeats = std::max(1, std::atoi(next()));
        } else if (a == "--options") {
            o.optionsFrom = std::atof(next());
            o.optionsTo = std::atof(next());
        } else if (a == "--seek") {
            o.seekTo = std::atof(next());
        } else if (o.scene.empty()) {
            o.scene = fs::absolute(fs::path(a));
        }
    }
    if (o.scene.empty() || o.hz <= 0.0) {
        std::fprintf(stderr, "usage: avgen_behavior_trace <scene.json> [--seconds N] [--hz H] "
                             "[--explain T] [--entity NAME] [--repeat] [--seek T]\n");
        return 2;
    }
    log::setLevel(log::Level::Error);
    organism::registerMushroomGenerator();
    scene::registerTreeGenerator();

    if (o.scrub >= 0.0) {
        return scrubReport(o);
    }
    if (o.crowd > 0) {
        double best = 1e30;
        Run kept;
        for (int r = 0; r < o.repeats; ++r) {
            Run run = simulate(o, false);
            if (run.exit != 0) {
                return run.exit;
            }
            if (run.wallSeconds < best) {
                best = run.wallSeconds;
            }
            if (kept.steps == 0 || run.behaviourSeconds < kept.behaviourSeconds) {
                kept = std::move(run);
            }
        }
        std::printf("crowd %d: %zu deciders, %.1f s at %.0f Hz, %lld steps; wall %.3f s (min of %d) = "
                    "%.3f ms/step = %.4f ms/step/decider; %zu decisions, %zu distinct (option,subject)\n",
                    o.crowd, kept.deciders, o.seconds, o.hz, kept.steps, best, o.repeats,
                    1000.0 * best / static_cast<double>(kept.steps),
                    1000.0 * best / static_cast<double>(kept.steps) /
                        static_cast<double>(std::max<std::size_t>(kept.deciders, 1)),
                    kept.decisions, kept.distinctChoices);
        std::printf("  entity step alone (director + perception + behaviours + actions): %.3f ms/step "
                    "(min of %d) = %.4f ms/step/decider\n",
                    1000.0 * kept.behaviourSeconds / static_cast<double>(kept.steps), o.repeats,
                    1000.0 * kept.behaviourSeconds / static_cast<double>(kept.steps) /
                        static_cast<double>(std::max<std::size_t>(kept.deciders, 1)));
        return 0;
    }
    const Run first = simulate(o, true);
    if (first.exit == 2) {
        return 2;
    }
    std::printf("-- behaviour trace: %s, %.1f s at %.0f Hz --\n", o.scene.string().c_str(),
                o.seconds, o.hz);
    for (const std::string& line : first.lines) {
        if (!o.entity.empty() && line.find(o.entity) == std::string::npos) {
            continue;
        }
        std::printf("%s\n", line.c_str());
    }
    std::printf("\n");
    for (const std::string& line : first.reachLines) {
        std::printf("%s\n", line.c_str());
    }
    for (const std::string& text : first.explained) {
        std::printf("\n-- why, at %.2f s --\n%s", o.explainAt, text.c_str());
    }
    int exit = first.exit;
    if (o.repeat) {
        Options again = o;
        again.seekTo = -1.0;
        const Run second = simulate(again, false);
        const bool same = second.lines == first.lines && second.finalState == first.finalState;
        std::printf("\nrepeat: %zu lines vs %zu; identical: %s\n", first.lines.size(),
                    second.lines.size(), same ? "yes" : "NO");
        if (!same) {
            exit = 1;
        }
    }
    return exit;
}
