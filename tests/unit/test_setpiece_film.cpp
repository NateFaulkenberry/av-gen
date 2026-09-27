// The proof the set-piece brief asks for (ADR-928, 929, 930): three abductions at three places, with
// one, two and three animals, in one film flown by ONE craft -- each lift under a craft that has
// stopped, and a seek landing on the frame a play does.
//
// The film is `tests/data/setpieces/setpiece-lab`: flat ground, the craft "saucer" (the body `visitor`
// with its beam `visitor-beam` as the part "beam"), three small herds -- one cow at (-120, -60), two
// at (60, 20), three at (150, 180) -- and two strays far from all of them. No licensed asset. The plan
// is the kind GV3's generator writes: an abduction on a point at 12 s (one animal), a coloured one on
// a point at 60 s (two), and one that searches a region at 110 s (three). Everything is real: the plan
// parses, validates and compiles through `app::applyPlanDocument` -- the step `avgen --plan` runs --
// and the engine plays it frame by frame at 60 fps, as a render does.
//
// Played ONCE (a static film, as `test_abduction_fade.cpp` does) and measured from the engine's own
// state every frame: the entity world's bodies, the director's beats and retirements, and the finals
// of the parameters a renderer reads. Every claim that could be vacuous has its control beside it.

#include "app/camera_director.hpp"
#include "app/directing_apply.hpp"
#include "app/directing_context.hpp"
#include "app/directing_plan_file.hpp"
#include "app/directing_record.hpp"
#include "app/edit_system.hpp"
#include "app/engine.hpp"
#include "core/time.hpp"
#include "directing/compiler.hpp"
#include "directing/plan.hpp"
#include "entity/entity.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "stage/setpiece.hpp"
#include "stage/staging.hpp"
#include "support/project_round_trip.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace avgen;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {

constexpr double kFps = 60.0;
constexpr double kDt = 1.0 / kFps;
constexpr double kEnd = 126.0; // the last set piece's craft is let go at about 124.6 s

fs::path labProject() { return fs::path(AVGEN_SOURCE_DIR) / "tests/data/setpieces/setpiece-lab.json"; }

const std::vector<std::string> kCows = {"cow-a1", "cow-b1", "cow-b2", "cow-c1", "cow-c2", "cow-c3"};
const std::vector<std::string> kStrays = {"cow-s1", "cow-s2"};

// The plan: west (12 s, 1 animal, the default beam), field (60 s, 2 animals, a red beam), south
// (110 s, the nearest cows within 30 m of (150, 180), 3 animals). Varied on purpose, so the validator
// has no repetition to warn of: three places, three bearings, three heights, three framings.
json filmPlan() {
    return json::parse(R"({
      "schemaVersion": 1, "id": "ufo", "title": "UFO activity", "tier": "baked",
      "setPieces": [
        {"key": "west", "template": "abduction", "craft": "saucer", "at": {"seconds": 12},
         "place": {"point": [-120, -60]},
         "set": {"animals": 1, "approachSeconds": 6, "approachBearing": 270}, "framingMetres": 30},
        {"key": "field", "template": "abduction", "craft": "saucer", "at": {"seconds": 60},
         "place": {"point": [62, 22]}, "beamColor": [1.0, 0.25, 0.15],
         "set": {"animals": 2, "approachSeconds": 6, "hoverHeight": 30}, "framingMetres": 120},
        {"key": "south", "template": "abduction", "craft": "saucer", "at": {"seconds": 110},
         "place": {"region": {"center": [150, 180], "radius": 30}},
         "set": {"animals": 3, "approachSeconds": 6, "approachBearing": 90}, "framingMetres": 300}
      ]})");
}

// Two routes keyed on the second set piece's beam (ADR-930): one that answers the frame it arrives
// and forgets it (no smoothing), one that remembers it (a one-second decay). Their targets are post
// parameters nothing else in the lab writes.
constexpr const char* kInstantTarget = "post/grade/saturation";
constexpr const char* kDecayTarget = "post/halation/intensity";

void addBeamRoutes(app::Engine& engine) {
    params::ModRoute instant;
    instant.source = "setpiece/field/beam";
    instant.target = kInstantTarget;
    instant.amount = 0.5f;
    engine.modulator().addRoute(std::move(instant));
    params::ModRoute decay;
    decay.source = "setpiece/field/beam";
    decay.target = kDecayTarget;
    decay.amount = 1.0f;
    decay.chain.decayMs = 1000.0f;
    engine.modulator().addRoute(std::move(decay));
    engine.rebind();
}

std::unique_ptr<app::Engine> labWithPlan(bool routes = true) {
    auto engine = std::make_unique<app::Engine>(app::EngineMode::Offline);
    auto loaded = engine->loadProject(labProject());
    INFO((loaded ? std::string() : loaded.error().message));
    REQUIRE(loaded.has_value());
    auto applied = app::applyPlanDocument(*engine, filmPlan());
    INFO((applied ? applied->toJson().dump() : applied.error().message));
    REQUIRE(applied.has_value());
    REQUIRE(applied->blocked.empty());
    if (routes) {
        addBeamRoutes(*engine);
    }
    engine->composition()->scene().detailLimits.entityDistanceCull = false;
    return engine;
}

float finalOf(app::Engine& engine, const std::string& path, int component = 0) {
    const params::IParameter* p = engine.params().find(path);
    return p != nullptr ? p->finalComponent(component) : -1.0f;
}

glm::vec3 bodyOf(const app::Engine& engine, const std::string& name) {
    const entity::Entity* e = engine.composition()->entityWorld().find(name);
    return e != nullptr ? e->state().position() : glm::vec3(-9999.0f);
}

// One frame of what a renderer would draw and what the director did.
struct Frame {
    double t = 0.0;
    glm::vec3 craft{0.0f};
    bool craftVisible = false;
    bool beamVisible = false;
    float beamRate = 0.0f;
    float beamSize = 0.0f;
    float beamEmissive = 0.0f;
    glm::vec4 beamColour{0.0f};
    std::array<glm::vec3, 8> bodies{};
    std::array<bool, 8> visible{};
    std::array<float, 8> opacity{};
    float instant = 0.0f;
    float decay = 0.0f;
};

Frame sample(app::Engine& engine, double t) {
    Frame f;
    f.t = t;
    f.craft = bodyOf(engine, "visitor");
    f.craftVisible = finalOf(engine, "nodes/visitor/visible") > 0.5f;
    f.beamVisible = finalOf(engine, "nodes/visitor-beam/visible") > 0.5f;
    f.beamRate = finalOf(engine, "particles/visitor-beam/spawnRate");
    f.beamSize = finalOf(engine, "particles/visitor-beam/size");
    f.beamEmissive = finalOf(engine, "particles/visitor-beam/emissive");
    for (int c = 0; c < 4; ++c) {
        f.beamColour[c] = finalOf(engine, "particles/visitor-beam/colorStart", c);
    }
    for (std::size_t i = 0; i < 8; ++i) {
        const std::string& name = i < 6 ? kCows[i] : kStrays[i - 6];
        f.bodies[i] = bodyOf(engine, name);
        f.visible[i] = finalOf(engine, "nodes/" + name + "/visible") > 0.5f;
        f.opacity[i] = finalOf(engine, "nodes/" + name + "/opacity");
    }
    f.instant = finalOf(engine, kInstantTarget);
    f.decay = finalOf(engine, kDecayTarget);
    return f;
}

struct Beat {
    std::string scenario;
    std::string beat;
    double t = 0.0;
};

// The film, played once from zero at 60 fps, exactly as `avgen --render` steps a project.
struct Film {
    std::unique_ptr<app::Engine> engine;
    std::vector<Frame> frames;
    std::vector<Beat> beats;
    std::map<std::string, double> retired; // animal -> the instant the director retired it
    std::map<std::string, std::string> bound; // animal -> the set piece that bound it
    // The setpiece/* world events in the entity world's record at `kSnapshot`, as a play has them there
    // (the record keeps a minute, so the snapshot is taken at the instant a seek is compared at).
    static constexpr double kSnapshot = 115.0;
    std::vector<std::pair<std::string, double>> worldEvents;
    float instantBase = 0.0f;
    float decayBase = 0.0f;

    Film() {
        engine = labWithPlan();
        instantBase = engine->params().find(kInstantTarget)->baseComponent(0);
        decayBase = engine->params().find(kDecayTarget)->baseComponent(0);
        engine->seekSeconds(0.0);
        const auto n = static_cast<int>(std::llround(kEnd * kFps));
        frames.reserve(static_cast<std::size_t>(n) + 1);
        for (int i = 0; i <= n; ++i) {
            FrameTime time;
            time.renderTime = static_cast<double>(i) * kDt;
            time.deltaTime = i == 0 ? 0.0 : kDt;
            time.frameIndex = static_cast<std::uint64_t>(i);
            engine->update(time);
            frames.push_back(sample(*engine, time.renderTime));
            if (i == static_cast<int>(std::llround(kSnapshot * kFps))) {
                const entity::EntityWorld& world = engine->composition()->entityWorld();
                for (const entity::WorldEvent& e : world.worldEvents()) {
                    const std::string name(world.eventName(e.type));
                    if (name.rfind("setpiece/", 0) == 0) {
                        worldEvents.emplace_back(name, e.time);
                    }
                }
            }
            for (const stage::StageEvent& e : engine->composition()->director().events()) {
                if (e.kind == stage::StageEventKind::Beat) {
                    beats.push_back({e.scenario, e.beat, time.renderTime});
                } else if (e.kind == stage::StageEventKind::Retired) {
                    retired.emplace(e.detail, time.renderTime);
                } else if (e.kind == stage::StageEventKind::Bound && e.role.rfind("target", 0) == 0) {
                    bound.emplace(e.detail, e.scenario);
                }
            }
        }
    }

    [[nodiscard]] std::optional<double> beat(const std::string& scenario, const std::string& name) const {
        for (const Beat& b : beats) {
            if (b.scenario == scenario && b.beat == name) {
                return b.t;
            }
        }
        return std::nullopt;
    }
    [[nodiscard]] const Frame& at(double t) const {
        const auto i = static_cast<std::size_t>(std::llround(t * kFps));
        return frames.at(std::min(i, frames.size() - 1));
    }
};

const Film& film() {
    static const Film f;
    return f;
}

} // namespace

TEST_CASE("three abductions, one craft: every animal is taken, the strays are not, each beam lights on its second",
          "[setpiece][film][adr928][adr929]") {
    const Film& f = film();
    // Each set piece ran start to finish, in order, on its own clock.
    const std::array<std::pair<const char*, double>, 3> pieces{{{"setpiece/west", 12.0}, {"setpiece/field", 60.0},
                                                                {"setpiece/south", 110.0}}};
    for (const auto& [scenario, at] : pieces) {
        INFO(scenario);
        const auto beam = f.beat(scenario, "beam");
        REQUIRE(beam.has_value());
        // The placed moment is entered on the first frame at or after its second.
        CHECK(*beam >= at - 1e-9);
        CHECK(*beam < at + kDt + 1e-9);
        for (const char* b : {"approach", "hover", "lift", "depart"}) {
            CHECK(f.beat(scenario, b).has_value());
        }
        CHECK(*f.beat(scenario, "approach") < *f.beat(scenario, "hover"));
        CHECK(*f.beat(scenario, "hover") < *beam);
        CHECK(*beam < *f.beat(scenario, "lift"));
        CHECK(*f.beat(scenario, "lift") < *f.beat(scenario, "depart"));
    }
    // Every cow of the three herds was lifted by the set piece over its herd, and retired.
    const std::map<std::string, std::string> herd = {{"cow-a1", "setpiece/west"}, {"cow-b1", "setpiece/field"},
                                                     {"cow-b2", "setpiece/field"}, {"cow-c1", "setpiece/south"},
                                                     {"cow-c2", "setpiece/south"}, {"cow-c3", "setpiece/south"}};
    for (const auto& [cow, scenario] : herd) {
        INFO(cow);
        REQUIRE(f.bound.contains(cow));
        CHECK(f.bound.at(cow) == scenario);
        REQUIRE(f.retired.contains(cow));
        CHECK(f.retired.at(cow) > *f.beat(scenario, "lift"));
        CHECK(f.retired.at(cow) <= *f.beat(scenario, "depart") + kDt + 1e-9);
    }
    CHECK(f.retired.size() == 6);
    // ...and the count per set piece is the plan's: 1, 2, 3.
    std::map<std::string, int> perPiece;
    for (const auto& [cow, scenario] : f.bound) {
        ++perPiece[scenario];
    }
    CHECK(perPiece["setpiece/west"] == 1);
    CHECK(perPiece["setpiece/field"] == 2);
    CHECK(perPiece["setpiece/south"] == 3);
    // The strays: never bound, never moved, never hidden. The control that the herds' checks above are
    // not true of every animal in the lab.
    const Frame& first = f.frames.front();
    const Frame& last = f.frames.back();
    for (std::size_t s = 6; s < 8; ++s) {
        INFO(kStrays[s - 6]);
        CHECK_FALSE(f.bound.contains(kStrays[s - 6]));
        CHECK(glm::length(last.bodies[s] - first.bodies[s]) < 1e-4f);
        CHECK(last.visible[s]);
    }
    for (std::size_t c = 0; c < 6; ++c) {
        INFO(kCows[c]);
        CHECK_FALSE(last.visible[c]);
        CHECK(first.visible[c]);
    }
}

TEST_CASE("one craft plays them in sequence: hidden between set pieces, seen during each", "[setpiece][film][adr928]") {
    const Film& f = film();
    // Between the pieces (west is let go by ~26.6 s, field taken at ~52 s; field let go by ~74.6 s,
    // south taken at ~102 s) and before the first, the craft is not in the picture.
    for (const double t : {1.0, 3.5, 30.0, 45.0, 80.0, 95.0}) {
        INFO(fmt::format("t = {}", t));
        CHECK_FALSE(f.at(t).craftVisible);
        CHECK_FALSE(f.at(t).beamVisible);
    }
    // During each beam and lift it is, with its beam lit.
    for (const double t : {12.5, 14.0, 60.5, 62.0, 110.5, 112.0}) {
        INFO(fmt::format("t = {}", t));
        CHECK(f.at(t).craftVisible);
        CHECK(f.at(t).beamVisible);
        CHECK(f.at(t).beamRate > 0.0f);
    }
}

TEST_CASE("each lift happens under a still craft", "[setpiece][film][adr928]") {
    const Film& f = film();
    // From the frame the beam beat is entered to the frame the depart beat is, the craft holds its
    // station: its only motion is the hover sway (`craftWobble`, 0.3 m by default).
    for (const char* scenario : {"setpiece/west", "setpiece/field", "setpiece/south"}) {
        INFO(scenario);
        const double from = *f.beat(scenario, "beam");
        const double until = *f.beat(scenario, "depart");
        const glm::vec3 station = f.at(from).craft;
        float drift = 0.0f;
        int frames = 0;
        for (const Frame& fr : f.frames) {
            if (fr.t < from - 1e-9 || fr.t >= until - 1e-9) {
                continue;
            }
            drift = std::max(drift, glm::length(glm::vec2(fr.craft.x - station.x, fr.craft.z - station.z)));
            ++frames;
        }
        INFO(fmt::format("{} frames, farthest {:.4f} m from the station", frames, drift));
        CHECK(frames > 300); // beam 1.1 s + lift 4.9 s at 60 fps: the window is not empty
        CHECK(drift <= 0.3f * 2.0f + 1e-3f); // the sway's reach either side of its centre
    }
    // The control, and the proof the sway is the only motion: the same lift with `craftWobble` 0
    // holds the craft to the millimetre -- frame to frame it does not move at all.
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(labProject()).has_value());
    json plan = filmPlan();
    plan["setPieces"] = json::array({plan["setPieces"][0]});
    plan["setPieces"][0]["set"]["craftWobble"] = 0.0;
    auto applied = app::applyPlanDocument(engine, plan);
    REQUIRE(applied.has_value());
    engine.composition()->scene().detailLimits.entityDistanceCull = false;
    engine.seekSeconds(0.0);
    std::optional<double> beam;
    std::optional<double> depart;
    glm::vec3 last(0.0f);
    float fastest = -1.0f;
    for (int i = 0; i <= static_cast<int>(20.0 * kFps); ++i) {
        FrameTime time;
        time.renderTime = static_cast<double>(i) * kDt;
        time.deltaTime = i == 0 ? 0.0 : kDt;
        time.frameIndex = static_cast<std::uint64_t>(i);
        engine.update(time);
        for (const stage::StageEvent& e : engine.composition()->director().events()) {
            if (e.kind == stage::StageEventKind::Beat && e.beat == "beam") beam = time.renderTime;
            if (e.kind == stage::StageEventKind::Beat && e.beat == "depart") depart = time.renderTime;
        }
        const glm::vec3 now = bodyOf(engine, "visitor");
        if (beam && !depart && time.renderTime > *beam + 1e-9) {
            fastest = std::max(fastest, glm::length(now - last) / static_cast<float>(kDt));
        }
        last = now;
    }
    REQUIRE(beam.has_value());
    REQUIRE(depart.has_value());
    CHECK(fastest == 0.0f);
}

TEST_CASE("a seek lands on the frame a play does, mid-lift of each set piece", "[setpiece][film][seek][adr930]") {
    const Film& f = film();
    auto engine = labWithPlan();
    int compared = 0;
    float worstBody = 0.0f;
    for (const char* scenario : {"setpiece/west", "setpiece/field", "setpiece/south"}) {
        // Two seconds into the lift: every animal is partway up the column, spinning, still visible.
        const double lift = *f.beat(scenario, "lift");
        const double t = std::round((lift + 2.0) * kFps) / kFps;
        const Frame& played = f.at(t);
        engine->seekSeconds(t);
        FrameTime time;
        time.renderTime = t;
        time.deltaTime = 0.0; // the landing frame, as a render that starts here opens (`RenderJob`)
        time.frameIndex = 0;
        engine->update(time);
        const Frame seeked = sample(*engine, t);
        INFO(fmt::format("{} at t = {:.4f}", scenario, t));
        CHECK(glm::length(seeked.craft - played.craft) < 1e-4f);
        CHECK(seeked.craftVisible == played.craftVisible);
        CHECK(seeked.beamVisible == played.beamVisible);
        CHECK(seeked.beamRate == played.beamRate);
        CHECK(seeked.beamSize == played.beamSize);
        CHECK(seeked.beamEmissive == played.beamEmissive);
        CHECK(seeked.beamColour == played.beamColour);
        int rising = 0;
        for (std::size_t i = 0; i < 8; ++i) {
            INFO((i < 6 ? kCows[i] : kStrays[i - 6]));
            const float d = glm::length(seeked.bodies[i] - played.bodies[i]);
            worstBody = std::max(worstBody, d);
            CHECK(d < 1e-4f);
            CHECK(seeked.visible[i] == played.visible[i]);
            CHECK(std::abs(seeked.opacity[i] - played.opacity[i]) < 1e-5f);
            // Partway up: the check is about bodies in the air, not bodies standing still.
            if (i < 6 && played.visible[i] && played.bodies[i].y > 2.0f) {
                ++rising;
            }
        }
        CHECK(rising >= 1);
        // And the frames after it continue as the play does.
        for (int k = 1; k <= 30; ++k) {
            time.renderTime = t + (static_cast<double>(k) * kDt);
            time.deltaTime = kDt;
            time.frameIndex = static_cast<std::uint64_t>(k);
            engine->update(time);
            const Frame next = sample(*engine, time.renderTime);
            const Frame& playedNext = f.at(time.renderTime);
            for (std::size_t i = 0; i < 8; ++i) {
                worstBody = std::max(worstBody, glm::length(next.bodies[i] - playedNext.bodies[i]));
            }
            CHECK(glm::length(next.craft - playedNext.craft) < 1e-4f);
        }
        ++compared;
        // The beam colour differs between the pieces (field is red), so an agreement on it is not two
        // engines agreeing that nothing changed.
        if (std::string(scenario) == "setpiece/field") {
            CHECK(played.beamColour.r > played.beamColour.g * 2.0f);
        }
    }
    INFO(fmt::format("worst body disagreement {:.6f} m", worstBody));
    CHECK(compared == 3);
    CHECK(worstBody < 1e-4f);
}

TEST_CASE("a seek rebuilds the set pieces' world events a play raised", "[setpiece][film][seek][adr930]") {
    const Film& f = film();
    auto engine = labWithPlan();
    const double t = Film::kSnapshot;
    engine->seekSeconds(t);
    FrameTime time;
    time.renderTime = t;
    engine->update(time);
    const entity::EntityWorld& world = engine->composition()->entityWorld();
    std::vector<std::pair<std::string, double>> seeked;
    for (const entity::WorldEvent& e : world.worldEvents()) {
        const std::string name(world.eventName(e.type));
        if (name.rfind("setpiece/", 0) == 0) {
            seeked.emplace_back(name, e.time);
        }
    }
    // The play's record at the same instant: the same events, at the same instants, in the same order.
    REQUIRE_FALSE(seeked.empty());
    REQUIRE(seeked.size() == f.worldEvents.size());
    for (std::size_t i = 0; i < seeked.size(); ++i) {
        INFO(fmt::format("{} at {:.17g} (seek) / {} at {:.17g} (play)", seeked[i].first, seeked[i].second,
                         f.worldEvents[i].first, f.worldEvents[i].second));
        CHECK(seeked[i].first == f.worldEvents[i].first);
        CHECK(std::abs(seeked[i].second - f.worldEvents[i].second) < 1e-9);
    }
    // Among them the moments a viewer names, raised at the craft.
    const auto has = [&](const char* name) {
        return std::any_of(seeked.begin(), seeked.end(), [&](const auto& e) { return e.first == name; });
    };
    CHECK(has("setpiece/south/beam"));
    CHECK(has("setpiece/south/lift"));
    CHECK(has("setpiece/field/depart"));
}

TEST_CASE("a route keyed on a set piece's beam answers the frame after it, in a play and after a seek",
          "[setpiece][film][seek][routes][adr930]") {
    const Film& f = film();
    const double beam = *f.beat("setpiece/field", "beam");
    const double after = beam + kDt;
    // In the play: nothing before the beam, the instant route's whole amount on the frame after it,
    // and the decay route falling from there.
    CHECK(f.at(beam).instant == f.instantBase);
    CHECK(f.at(after).instant == f.instantBase + 0.5f);
    CHECK(f.at(after + kDt).instant == f.instantBase);
    CHECK(f.at(after).decay > f.decayBase + 0.99f);
    const Frame& half = f.at(after + 0.5);
    CHECK(half.decay > f.decayBase + 0.4f);
    CHECK(half.decay < f.decayBase + 0.9f);
    // The other set pieces' beams do not fire it: the route is keyed on 'field', not on any beam.
    CHECK(f.at(*f.beat("setpiece/west", "beam") + kDt).instant == f.instantBase);

    auto engine = labWithPlan();
    const auto land = [&](double t) {
        engine->seekSeconds(t);
        FrameTime time;
        time.renderTime = t;
        time.deltaTime = 0.0;
        engine->update(time);
        return sample(*engine, t);
    };
    // A seek that lands ON the frame after the beam carries the event, as the play did (the landing
    // frame has no delta; before ADR-930's fix it carried nothing).
    const Frame onAfter = land(after);
    CHECK(onAfter.instant == f.at(after).instant);
    CHECK(onAfter.decay == f.at(after).decay);
    // A seek half a second into the decay lands where the play is: the route's history is replayed.
    const Frame intoDecay = land(half.t);
    CHECK(std::abs(intoDecay.decay - half.decay) < 1e-6f);
    CHECK(intoDecay.instant == half.instant);
    // Control: the old seek, which reset the route and replayed nothing, lands the decay elsewhere.
    engine->seekSeconds(half.t);
    engine->modulator().resetState();
    FrameTime time;
    time.renderTime = half.t;
    engine->update(time);
    CHECK(std::abs(finalOf(*engine, kDecayTarget) - half.decay) > 0.3f);
}

TEST_CASE("the film saves, reloads and plays the same", "[setpiece][film][persistence][adr929]") {
    const Film& f = film();
    auto engine = labWithPlan(false);
    testsupport::ScratchDir dir("setpiece_film");
    auto trip = testsupport::saveAndReload(*engine, dir / "film.json");
    REQUIRE(trip.has_value());
    CHECK(trip->saved.contains("staging"));
    CHECK(trip->saved.contains("directingPlans"));
    CHECK(trip->warnings.empty());
    app::Engine& reloaded = *trip->reloaded;
    std::vector<std::string> names;
    for (const stage::ScenarioDesc& s : reloaded.composition()->staging().scenarios) {
        names.push_back(s.name);
    }
    CHECK(names == std::vector<std::string>{"setpiece/west", "setpiece/field", "setpiece/south"});
    // The reloaded film, played through the first set piece, is the same film.
    reloaded.composition()->scene().detailLimits.entityDistanceCull = false;
    reloaded.seekSeconds(0.0);
    float worst = 0.0f;
    for (int i = 0; i <= static_cast<int>(20.0 * kFps); ++i) {
        FrameTime time;
        time.renderTime = static_cast<double>(i) * kDt;
        time.deltaTime = i == 0 ? 0.0 : kDt;
        time.frameIndex = static_cast<std::uint64_t>(i);
        reloaded.update(time);
        const Frame now = sample(reloaded, time.renderTime);
        const Frame& played = f.frames[static_cast<std::size_t>(i)];
        worst = std::max(worst, glm::length(now.craft - played.craft));
        worst = std::max(worst, glm::length(now.bodies[0] - played.bodies[0]));
        CHECK(now.beamVisible == played.beamVisible);
    }
    INFO(fmt::format("worst disagreement {:.6f} m", worst));
    CHECK(worst < 1e-4f);
}

TEST_CASE("the plan's install is one undo, and redo puts the set pieces back", "[setpiece][film][undo][adr929]") {
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(labProject()).has_value());
    app::EditSystem edits;
    directing::PlanParse parsed = directing::parsePlan(filmPlan());
    REQUIRE(parsed.plan);
    const directing::Compilation compiled = directing::compilePlan(*parsed.plan, app::sceneFactsFor(engine));
    REQUIRE(compiled.validation.blocked.empty());
    REQUIRE(app::applyCompilation(engine, edits.history(), compiled).has_value());
    CHECK(edits.history().undoSize() == 1);
    const auto count = [&] {
        return std::count_if(engine.composition()->staging().scenarios.begin(),
                             engine.composition()->staging().scenarios.end(),
                             [](const stage::ScenarioDesc& s) { return stage::isSetPieceScenario(s.name); });
    };
    CHECK(count() == 3);
    CHECK(engine.params().find("staging/setpiece/field/beamRed") != nullptr);
    REQUIRE(edits.execute(app::EditAction::Undo, engine));
    CHECK(count() == 0);
    CHECK(engine.directingPlans().empty());
    CHECK(engine.params().find("staging/setpiece/field/beamRed") == nullptr);
    REQUIRE(edits.execute(app::EditAction::Redo, engine));
    CHECK(count() == 3);
    CHECK(engine.directingPlans().size() == 1);
    CHECK(engine.params().find("staging/setpiece/field/beamRed") != nullptr);
}

TEST_CASE("a watched play reports the set pieces' moments, and Song Mode's peak events read them",
          "[setpiece][film][events][adr930]") {
    // ADR-922 reads events generically -- a name, a subject, a time -- from a watched play's
    // observation on any directing plan. The set pieces' moments reach it by that road with nothing
    // set-piece-specific in Song Mode.
    const Film& f = film();
    auto engine = labWithPlan(false);
    auto watched = app::watchWorldEvents(*engine, 64.0);
    REQUIRE(watched.has_value());
    REQUIRE(engine->directingPlans().size() == 1);
    directing::Plan& plan = engine->directingPlans()[0];
    plan.observation = std::make_pair(watched->events, watched->untilSeconds);
    const std::vector<app::SongEvent> events = app::songEventsForEngine(*engine);
    const auto beam = std::find_if(events.begin(), events.end(),
                                   [](const app::SongEvent& e) { return e.name == "setpiece/field/beam"; });
    REQUIRE(beam != events.end());
    CHECK(beam->subject == "visitor"); // raised at the craft: the subject a peak is given to
    CHECK(std::abs(beam->seconds - *f.beat("setpiece/field", "beam")) < 1e-6);
    const auto lift = std::find_if(events.begin(), events.end(),
                                   [](const app::SongEvent& e) { return e.name == "setpiece/west/lift"; });
    REQUIRE(lift != events.end());
    CHECK(std::abs(lift->seconds - *f.beat("setpiece/west", "lift")) < 1e-6);
}
