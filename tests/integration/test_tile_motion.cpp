// ADR-1998: the crests that throw the aliens are the crests the music draws. The ground's routed parameters are run
// over the song once, offline, and every surface -- the drawn ground, the stance, the plan, the throw -- reads that
// record at the instant it asks about, so a seek lands where a play does with an audio route swelling the ground,
// whatever the frame rate, and a louder routed swell throws more.

#include "app/engine.hpp"
#include "audio/audio_file.hpp"
#include "core/time.hpp"
#include "params/modulation.hpp"
#include "scene/composition.hpp"
#include "support/synth.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <vector>

using namespace avgen;

namespace {

constexpr std::uint32_t kRate = 44100;

// Twenty seconds of a low tone that swells in and out every ~5 s, over a 120 BPM click: audio.bass rises and falls with
// the swell, so a route from it moves the ground in phrases a body can be thrown by or not.
std::filesystem::path swellWav() {
    static const std::filesystem::path path = [] {
        const std::size_t frames = 20 * kRate;
        auto tone = testsupport::sine(55.0f, kRate, frames, 0.6f);
        auto clicks = testsupport::clickTrack(120.0f, kRate, frames);
        std::vector<float> mono(frames);
        for (std::size_t i = 0; i < frames; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(kRate);
            const float swell = std::pow(0.5f + 0.5f * std::sin(t * 1.25f - 1.2f), 3.0f);
            mono[i] = tone[i] * swell + clicks[i] * 0.2f;
        }
        auto file = audio::AudioFile::fromInterleaved(testsupport::interleave(mono, 2), 2, kRate);
        const auto p = testsupport::processTempDir() / "tile_motion_swell.wav";
        REQUIRE(file.writeWav(p).has_value());
        return p;
    }();
    return path;
}

// A moving tiles ground with three bodies that stay where they are put (a home radius of 0: they never walk), so every
// time one leaves the ground it is a crest's throw.
nlohmann::json scene(float tossThreshold) {
    nlohmann::json j = nlohmann::json::parse(R"({
        "format": "avgen-scene", "version": 1, "name": "tile-motion",
        "camera": {"position": [0, 30, 40], "target": [0, 0, 0]},
        "environment": {"tileDynamics": {
            "amplitude": 0.3,
            "waves": [ {"amplitude": 0.5, "wavelength": 36, "speed": 5, "onLand": 1},
                       {"amplitude": 0.3, "wavelength": 22, "speed": 3.5, "direction": 70, "onLand": 1} ]
        }},
        "nodes": [
            {"name": "ground", "kind": "procedural", "position": [2, 0, -1],
             "procedural": {"source": {"kind": "box", "size": [3.66, 1.2, 3.66]},
                "distribution": {"kind": "generator", "generator": {"name": "tiles", "version": 1,
                    "cellSize": 4.0, "viewDistance": 120.0, "tiles": {"seed": 3, "landThreshold": 0.2}}},
                "slabs": true}},
            {"name": "a", "kind": "orb", "position": [6, 0, 6]},
            {"name": "b", "kind": "orb", "position": [-10, 0, 3]},
            {"name": "c", "kind": "orb", "position": [14, 0, -9]}
        ],
        "entities": []})");
    int seed = 5;
    for (const char* name : {"a", "b", "c"}) {
        j["entities"].push_back({{"name", name}, {"node", name}, {"seed", seed},
                                 {"behaviors", {{{"kind", "hop"}, {"hopChance", 0.0}, {"homeRadius", 0.0},
                                                 {"tossThreshold", tossThreshold}, {"tossGain", 2.0},
                                                 {"tossCooldown", 1.5}}}}});
        seed += 4;
    }
    return j;
}

struct Rig {
    app::Engine engine{app::EngineMode::Offline};

    // `amount` scales the route from the bass onto the swell (0: no route).
    explicit Rig(float amount, float tossThreshold = 0.3f) {
        REQUIRE(engine.setCompositionJson(scene(tossThreshold)).has_value());
        REQUIRE(engine.loadAudio(swellWav()).has_value());
        for (params::ModRoute& r : engine.modulator().routes()) {
            // the composition's default routes turn and scale the whole scene with the music; a tiles world is not
            // turned (its ground is a translation, ADR-1955), so they are off here as a tiles scene turns them off
            if (r.target.starts_with("root/")) r.enabled = false;
        }
        if (amount != 0.0f) {
            params::ModRoute r;
            r.source = "audio.bass";
            r.target = "env/tiles/amplitude";
            r.amount = amount;
            r.chain.attackMs = 300.0f; // smoothed, so the swell's value is history a seek must not lose
            r.chain.decayMs = 1500.0f;
            r.chain.springHz = 0.6f;
            r.chain.springDamping = 1.0f;
            engine.modulator().addRoute(std::move(r));
        }
        engine.rebind();
        REQUIRE(engine.params().find("env/tiles/amplitude") != nullptr);
    }
    void frame(double t, double dt, std::uint64_t k) { engine.update(FrameTime{t, dt, k}); }
    void playTo(double seconds, double fps) {
        const auto last = static_cast<long long>(std::llround(seconds * fps));
        for (long long k = 0; k <= last; ++k) {
            frame(static_cast<double>(k) / fps, k == 0 ? 0.0 : 1.0 / fps, static_cast<std::uint64_t>(k));
        }
    }
    [[nodiscard]] scene::Composition& comp() { return *engine.composition(); }
    [[nodiscard]] std::vector<glm::vec3> bodies() {
        std::vector<glm::vec3> out;
        for (const auto& e : comp().entityWorld().entities()) out.push_back(e->state().position());
        return out;
    }
};

struct Throws {
    int count = 0;
    float highest = 0.0f; // the highest a body got above the drawn ground under it (m)
};

Throws countThrows(Rig& rig, double seconds, double fps) {
    Throws out;
    std::vector<bool> was(3, false);
    const auto last = static_cast<long long>(std::llround(seconds * fps));
    for (long long k = 0; k <= last; ++k) {
        const double t = static_cast<double>(k) / fps;
        rig.frame(t, k == 0 ? 0.0 : 1.0 / fps, static_cast<std::uint64_t>(k));
        const auto& es = rig.comp().entityWorld().entities();
        const world::GroundSource* g = rig.comp().entityWorld().ground();
        REQUIRE(g != nullptr);
        for (std::size_t i = 0; i < es.size(); ++i) {
            const entity::EntityState& st = es[i]->state();
            if (st.airborne && !was[i]) ++out.count;
            if (st.airborne) {
                const glm::vec3 p = st.position();
                out.highest = std::max(out.highest, p.y - g->at(p.x, p.z, t).height);
            }
            was[i] = st.airborne;
        }
    }
    return out;
}

} // namespace

TEST_CASE("the drawn ground and the bodies read the song's ground: a seek lands where a play does, at any frame rate",
          "[tiles][hop][geography][seek][audio][adr1998]") {
    const double T = 14.0;
    Rig played(2.5f);
    played.playTo(T, 30.0);
    const auto want = played.bodies();
    const scene::TileDynamics& drawn = played.comp().scene().environment.tileDynamics;
    const float wantAmplitude = drawn.amplitude;
    REQUIRE(played.comp().tileMotion() != nullptr);
    // the route reaches the drawn swell: the bass's output on the 0.3 rest
    CAPTURE(wantAmplitude);
    CHECK(wantAmplitude > 0.35f);

    // a cold seek there, and the frame it lands on
    Rig sought(2.5f);
    sought.engine.seekSeconds(T);
    sought.frame(T, 0.0, 0);
    const auto got = sought.bodies();
    CHECK(sought.comp().scene().environment.tileDynamics.amplitude == wantAmplitude);
    REQUIRE(got.size() == want.size());
    for (std::size_t i = 0; i < want.size(); ++i) {
        CAPTURE(i, want[i].x, want[i].y, want[i].z, got[i].x, got[i].y, got[i].z);
        CHECK(glm::length(want[i] - got[i]) < 1e-3f);
    }

    // and 60 fps throws where 30 does (the record is on its own grid, the bodies on theirs)
    Rig sixty(2.5f);
    sixty.playTo(T, 60.0);
    const auto at60 = sixty.bodies();
    CHECK(sixty.comp().scene().environment.tileDynamics.amplitude == wantAmplitude);
    for (std::size_t i = 0; i < want.size(); ++i) {
        CAPTURE(i, want[i].y, at60[i].y);
        CHECK(glm::length(want[i] - at60[i]) < 1e-3f);
    }

    // recording leaves the play's own route state alone: a frame after the seek plays on exactly as the play does
    played.frame(T + 1.0 / 30.0, 1.0 / 30.0, 1);
    sought.frame(T + 1.0 / 30.0, 1.0 / 30.0, 1);
    CHECK(sought.comp().scene().environment.tileDynamics.amplitude ==
          played.comp().scene().environment.tileDynamics.amplitude);
}

TEST_CASE("a louder routed swell throws the bodies more often and higher", "[tiles][hop][geography][audio][adr1998]") {
    // The bodies feel only what the music adds: the rest swell (0.3) rises at ~0.2 m/s under them, under the
    // threshold, so with no route nothing is thrown at all.
    Rig none(0.0f);
    const Throws silent = countThrows(none, 20.0, 30.0);
    Rig quiet(1.0f);
    const Throws soft = countThrows(quiet, 20.0, 30.0);
    Rig loud(4.0f);
    const Throws hard = countThrows(loud, 20.0, 30.0);
    CAPTURE(silent.count, soft.count, hard.count, soft.highest, hard.highest);
    CHECK(silent.count == 0);
    CHECK(hard.count > soft.count);
    CHECK(hard.highest > soft.highest);
    CHECK(hard.count >= 3);
}

TEST_CASE("the ground a body is thrown by is the ground the renderer draws", "[tiles][hop][geography][audio][adr1998]") {
    // The renderer is handed `environment.tileDynamics` each frame. The landscape a body feels (the throw's surface)
    // must be that ground, music and all, at the frame's instant -- not the bases (ADR-1964's planning surface).
    Rig rig(4.0f);
    const scene::ProceduralGeometry* ground = nullptr;
    for (const scene::ProceduralGeometry& p : rig.comp().scene().procedurals) {
        if (p.distribution.generator.isTiles() && ground == nullptr) ground = &p; // the one the bodies stand on
    }
    REQUIRE(ground != nullptr);
    const glm::vec3 offset(ground->distributionTransform.matrix()[3]);
    const float pitch = ground->distribution.generator.cellSize;
    float worst = 0.0f;
    for (long long k = 0; k <= 20 * 30; ++k) {
        const double t = static_cast<double>(k) / 30.0;
        rig.frame(t, k == 0 ? 0.0 : 1.0 / 30.0, static_cast<std::uint64_t>(k));
        scene::TileDynamics drawn = rig.comp().scene().environment.tileDynamics;
        drawn.springs.clear();
        const world::GroundSource* g = rig.comp().entityWorld().ground();
        REQUIRE(g != nullptr);
        for (const glm::vec2 probe : {glm::vec2(6.0f, 6.0f), glm::vec2(-10.0f, 3.0f), glm::vec2(30.0f, -22.0f)}) {
            const glm::vec2 c = g->cellCentre(probe.x, probe.y);
            const float want = scene::tileSampleMoving(ground->distribution.generator.tiles, drawn, c.x - offset.x,
                                                       c.y - offset.z, static_cast<float>(t), pitch)
                                   .height +
                               offset.y;
            worst = std::max(worst, std::abs(g->landscape(c.x, c.y, t).height - want));
        }
    }
    CAPTURE(worst);
    CHECK(worst < 1e-4f);
}

TEST_CASE("a throw comes at the fastest rise of the ground under the body", "[tiles][hop][geography][audio][adr1998]") {
    // The owner: "it should look like the wave is throwing the alien upwards as it passes through". Each throw leaves
    // at the instant the drawn ground under the body rises fastest on that swing: the rise there is at least the rise a
    // step either side, and above the threshold. (ADR-1997 threw once the rise had fallen 10% under its best: after it.)
    Rig rig(4.0f);
    std::vector<bool> was(3, false);
    int checked = 0;
    const double step = 1.0 / 60.0;
    for (long long k = 0; k <= 20 * 60; ++k) {
        const double t = static_cast<double>(k) * step;
        rig.frame(t, k == 0 ? 0.0 : step, static_cast<std::uint64_t>(k));
        const auto& es = rig.comp().entityWorld().entities();
        const world::GroundSource* g = rig.comp().entityWorld().ground();
        for (std::size_t i = 0; i < es.size(); ++i) {
            const entity::EntityState& st = es[i]->state();
            if (st.airborne && !was[i]) {
                const glm::vec2 c = g->cellCentre(st.position().x, st.position().z);
                const auto rise = [&](double u) {
                    return (g->landscape(c.x, c.y, u + 0.5 * step).height - g->landscape(c.x, c.y, u - 0.5 * step).height) /
                           step;
                };
                CAPTURE(t, i, rise(t - step), rise(t), rise(t + step));
                CHECK(rise(t) >= 0.3f);
                CHECK(rise(t) >= rise(t - step) - 1e-4f);
                CHECK(rise(t) >= rise(t + step) - 1e-4f);
                ++checked;
            }
            was[i] = st.airborne;
        }
    }
    CHECK(checked >= 3);
}

TEST_CASE("crest throws on the music, on a real project (measure)", "[.][tiles][hop][measure][adr1998]") {
    // AVGEN_TOSS_PROJECT: a project file; AVGEN_TOSS_CSV: where to write; AVGEN_TOSS_ENTITY: which body (index).
    // Per frame at 30 fps: the drawn swell, the ground's upward speed under the body (what throws it), and its throws.
    const char* projectPath = std::getenv("AVGEN_TOSS_PROJECT");
    const char* csvPath = std::getenv("AVGEN_TOSS_CSV");
    if (projectPath == nullptr || csvPath == nullptr) SKIP("set AVGEN_TOSS_PROJECT and AVGEN_TOSS_CSV");
    const int who = std::getenv("AVGEN_TOSS_ENTITY") != nullptr ? std::atoi(std::getenv("AVGEN_TOSS_ENTITY")) : 0;
    const double t1 = std::getenv("AVGEN_TOSS_TO") != nullptr ? std::atof(std::getenv("AVGEN_TOSS_TO")) : 60.0;
    const double t0 = std::getenv("AVGEN_TOSS_FROM") != nullptr ? std::atof(std::getenv("AVGEN_TOSS_FROM")) : 0.0;
    const double fps = std::getenv("AVGEN_TOSS_FPS") != nullptr ? std::atof(std::getenv("AVGEN_TOSS_FPS")) : 30.0;
    app::Engine engine{app::EngineMode::Offline};
    REQUIRE(engine.loadProject(projectPath).has_value());
    std::ofstream out(csvPath);
    // drawn: the drawn top of the tile under the body (what the viewer sees) and its upward speed; rise: the surface
    // the throw reads (landscape); y: the body
    out << "t,amplitude,rise,airborne,throw,above,drawn,drawnRise,y\n";
    std::vector<bool> was;
    for (long long k = 0; k <= static_cast<long long>(t1 * fps); ++k) {
        const double t = static_cast<double>(k) / fps;
        engine.update(FrameTime{t, k == 0 ? 0.0 : 1.0 / fps, static_cast<std::uint64_t>(k)});
        scene::Composition* comp = engine.composition();
        REQUIRE(comp != nullptr);
        const auto& es = comp->entityWorld().entities();
        REQUIRE(static_cast<std::size_t>(who) < es.size());
        was.resize(es.size(), false);
        const entity::EntityState& st = es[static_cast<std::size_t>(who)]->state();
        const glm::vec3 p = st.position();
        const world::GroundSource* g = comp->entityWorld().ground();
        REQUIRE(g != nullptr);
        const double h = 1.0 / 120.0;
        const double rise = (g->landscape(p.x, p.z, t + h).height - g->landscape(p.x, p.z, t - h).height) / (2.0 * h);
        const double drawn = g->at(p.x, p.z, t).height;
        const double drawnRise = (g->at(p.x, p.z, t + h).height - g->at(p.x, p.z, t - h).height) / (2.0 * h);
        const bool thrown = st.airborne && !was[static_cast<std::size_t>(who)];
        if (t >= t0) {
            out << t << "," << comp->scene().environment.tileDynamics.amplitude << "," << rise << ","
                << (st.airborne ? 1 : 0) << "," << (thrown ? 1 : 0) << "," << (p.y - drawn) << "," << drawn << ","
                << drawnRise << "," << p.y << "\n";
        }
        was[static_cast<std::size_t>(who)] = st.airborne;
    }
}
