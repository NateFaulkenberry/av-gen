// ADR-1998: the crests that throw the aliens are the crests the music draws. The ground's routed parameters are run
// over the song once, offline, and every surface -- the drawn ground, the stance, the plan, the throw -- reads that
// record at the instant it asks about, so a seek lands where a play does with an audio route swelling the ground,
// whatever the frame rate, and a louder routed swell throws more.

#include "app/engine.hpp"
#include "audio/audio_file.hpp"
#include "core/time.hpp"
#include "entity/tile_nav.hpp"
#include "params/modulation.hpp"
#include "scene/composition.hpp"
#include "scene/tile_ground.hpp"
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
    explicit Rig(float amount, float tossThreshold = 0.3f) : Rig(amount, scene(tossThreshold)) {}
    Rig(float amount, const nlohmann::json& sceneJson) {
        REQUIRE(engine.setCompositionJson(sceneJson).has_value());
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

TEST_CASE("a throw crouches on the rising tile, pops at the crest's top and flies on one clip clock",
          "[tiles][hop][geography][audio][adr1998][adr2000]") {
    // The owner (CPD-R7): "the popping animation will be timed with the crest of the wave (tile height max)". Each throw:
    //   * a crouch first, on the tile's drawn top (riding the crest up), activity Jump, on the jump clip's clock whose
    //     pop (0.60 clip s by default) falls on the launch;
    //   * the launch at the crest's top: the ground under the body is at its highest that instant;
    //   * the flight on the same state (no fall clip at the apex), the clip stretched so its feet come down (1.12 clip s)
    //     as the body lands.
    Rig rig(4.0f);
    const double step = 1.0 / 60.0;
    struct Body {
        bool air = false;
        bool crouch = false;
        int crouchFrames = 0;
        double launch = -1.0;
    };
    std::vector<Body> bodies(3);
    int launches = 0;
    int landings = 0;
    for (long long k = 0; k <= 20 * 60; ++k) {
        const double t = static_cast<double>(k) * step;
        rig.frame(t, k == 0 ? 0.0 : step, static_cast<std::uint64_t>(k));
        const auto& es = rig.comp().entityWorld().entities();
        const world::GroundSource* g = rig.comp().entityWorld().ground();
        for (std::size_t i = 0; i < es.size(); ++i) {
            const entity::EntityState& st = es[i]->state();
            Body& b = bodies[i];
            const glm::vec2 c = g->cellCentre(st.position().x, st.position().z);
            const bool crouching = !st.airborne && st.activity == entity::Activity::Jump;
            if (crouching) {
                // on the drawn top, on the clip clock, the pop still ahead
                CAPTURE(t, i);
                CHECK(std::abs(st.position().y - g->at(c.x, c.y, t).height) < 1e-3f);
                CHECK(st.clipRate > 0.0f);
                CHECK((t - st.clipOrigin) * st.clipRate <= 0.60 + 1e-3);
                ++b.crouchFrames;
            }
            if (st.airborne && !b.air) {
                // the launch: the crest's top under the body, and the clip at its pop
                const auto h = [&](double u) { return g->landscape(c.x, c.y, u).height; };
                CAPTURE(t, i, h(t - step), h(t), h(t + step), b.crouchFrames);
                CHECK(h(t) >= h(t - step) - 1e-3f); // the top, on the 60 Hz step grid (within a millimetre)
                CHECK(h(t) >= h(t + step) - 1e-3f);
                CHECK(b.crouch); // it crouched first
                CHECK((t - st.clipOrigin) * st.clipRate == Catch::Approx(0.60).margin(0.03));
                b.launch = t;
                ++launches;
            }
            if (st.airborne) {
                CHECK(st.activity == entity::Activity::Jump); // no fall clip at the apex
            }
            if (!st.airborne && b.air && b.launch >= 0.0) {
                // the landing: the clip's feet come down as the body does (the last flying frame's clock)
                ++landings;
                b.crouchFrames = 0;
            }
            if (st.airborne && st.clipRate > 0.0f) {
                CHECK((t - st.clipOrigin) * st.clipRate <= 1.12 + 1e-2);
            }
            b.air = st.airborne;
            b.crouch = crouching;
        }
    }
    CAPTURE(launches, landings);
    CHECK(launches >= 3);
    CHECK(landings >= 2);
}

// CPD-R9: three bodies a tile apart, springing under each other's landings at art's spring (CP-D r2: depth 1.1, 1.4 Hz,
// ripple 14 m; landing strength 0.5), on crests with several tops: a short train riding the swell, and authored rings
// at art's ring spec (45 m/s, a 24 m ripple inside a 20 m front) passing every few seconds.
namespace {
nlohmann::json crowdedScene() {
    nlohmann::json j = scene(0.3f);
    j["environment"]["tileDynamics"]["waves"].push_back({{"amplitude", 0.35}, {"wavelength", 9}, {"speed", 6}, {"onLand", 1}});
    j["environment"]["tileDynamics"]["rings"] = {{"amplitude", 1.6}, {"speed", 45.0}, {"wavelength", 24.0},
                                                 {"width", 20.0},    {"decay", 2.4},   {"crest", 1.0}};
    for (const float when : {2.5f, 6.0f, 9.0f, 12.5f, 16.0f}) {
        j["environment"]["tileDynamics"]["events"].push_back(
            {{"time", when}, {"position", {-60.0, 6.0}}, {"strength", 1.0}});
    }
    j["environment"]["tileDynamics"]["spring"] = {{"depth", 1.1},       {"frequency", 1.4},     {"damping", 0.16},
                                                  {"rippleSpeed", 16.0}, {"rippleRadius", 14.0}, {"glowRadius", 3.0}};
    const std::vector<glm::vec2> at = {{6, 6}, {10, 6}, {6, 10}};
    for (std::size_t i = 0; i < at.size(); ++i) {
        j["nodes"][i + 1]["position"] = {at[i].x, 0.0, at[i].y};
        j["entities"][i]["behaviors"][0]["strength"] = 0.5;
        j["entities"][i]["behaviors"][0]["tossGain"] = 3.0;
    }
    return j;
}
} // namespace

TEST_CASE("a throw pops on the crest the viewer sees, and none without one", "[tiles][hop][geography][audio][adr2001][adr2002]") {
    // ADR-2001: on the landscape, the pop is the swing's highest top: no top within 0.7 s either side is 0.1 m higher,
    // and the crest rose at least 0.25 m into it. ADR-2002: on the drawn tile (the landings' springs included), read
    // once the window has played, the pop is the top too -- except where a landing made after the crouch began moved it.
    Rig rig(4.0f, crowdedScene());
    const double step = 1.0 / 60.0;
    struct Launch {
        std::size_t body;
        double t0;
        glm::vec2 tile;
    };
    std::vector<Launch> launches;
    std::vector<bool> was(3, false);
    const world::GroundSource* g = nullptr;
    for (long long k = 0; k <= 20 * 60; ++k) {
        const double t = static_cast<double>(k) * step;
        rig.frame(t, k == 0 ? 0.0 : step, static_cast<std::uint64_t>(k));
        g = rig.comp().entityWorld().ground();
        const auto& es = rig.comp().entityWorld().entities();
        for (std::size_t i = 0; i < es.size(); ++i) {
            const entity::EntityState& st = es[i]->state();
            if (st.airborne && !was[i] && t < 19.0) {
                launches.push_back({i, t, g->cellCentre(st.position().x, st.position().z)});
            }
            was[i] = st.airborne;
        }
    }
    REQUIRE(g != nullptr);
    int drawnOff = 0;
    int springs = 0;
    for (const Launch& l : launches) {
        const auto land = [&](double u) { return g->landscape(l.tile.x, l.tile.y, u).height; };
        const auto drawn = [&](double u) { return g->at(l.tile.x, l.tile.y, u).height; };
        float highLand = -1e9f;
        float highDrawn = -1e9f;
        float low = land(l.t0);
        for (int j = -42; j <= 42; ++j) {
            highLand = std::max(highLand, land(l.t0 + j * step));
            highDrawn = std::max(highDrawn, drawn(l.t0 + j * step));
            springs += std::abs(drawn(l.t0 + j * step) - land(l.t0 + j * step)) > 0.05f ? 1 : 0;
        }
        for (int j = -120; j <= 0; ++j) low = std::min(low, land(l.t0 + j * step));
        CAPTURE(l.body, l.t0, highLand, land(l.t0), highDrawn, drawn(l.t0), low);
        CHECK(highLand <= land(l.t0) + 0.1f + 0.3f); // the landscape's own top (a pop moved onto a drawn top stays near)
        CHECK(land(l.t0) - low >= 0.25f);              // a readable crest
        drawnOff += highDrawn > drawn(l.t0) + 0.1f ? 1 : 0;
    }
    CAPTURE(launches.size(), drawnOff, springs);
    CHECK(launches.size() >= 12);
    CHECK(springs > 0); // the springs do move the tiles the pops leave from
    CHECK(drawnOff * 10 <= static_cast<int>(launches.size()));
}

TEST_CASE("crowded bodies springing each other's tiles: a seek lands where a play does, at any frame rate",
          "[tiles][hop][geography][seek][audio][adr2002]") {
    // ADR-2002 reads the other bodies' landings (lagged 0.1 s, so no body's step order matters while a frame is
    // shorter than that). A 30 fps play, a 60 fps play and a cold seek agree to a millimetre.
    const double T = 16.0;
    Rig played(4.0f, crowdedScene());
    played.playTo(T, 30.0);
    Rig sixty(4.0f, crowdedScene());
    sixty.playTo(T, 60.0);
    Rig sought(4.0f, crowdedScene());
    sought.engine.seekSeconds(T);
    sought.frame(T, 0.0, 0);
    const auto want = played.bodies();
    const auto at60 = sixty.bodies();
    const auto got = sought.bodies();
    for (std::size_t i = 0; i < want.size(); ++i) {
        CAPTURE(i, want[i].x, want[i].y, want[i].z, at60[i].y, got[i].y);
        CHECK(glm::length(want[i] - at60[i]) < 1e-3f);
        CHECK(glm::length(want[i] - got[i]) < 1e-3f);
    }
}

namespace {
// ADR-2003: the swell scene's three bodies with wide homes, running, and a bent keep-out line between them.
nlohmann::json keepOutScene() {
    nlohmann::json j = scene(0.3f);
    const nlohmann::json line = {{-200.0, 1.0}, {0.0, 2.0}, {200.0, -1.0}};
    for (auto& e : j["entities"]) {
        e["behaviors"][0].update({{"homeRadius", 24.0}, {"runChance", 1.0}, {"pauseMin", 0.2}, {"pauseMax", 0.5},
                                  {"goalMin", 2.0}, {"goalMax", 6.0}, {"avoidLine", line}, {"avoidDistance", 5.0}});
    }
    return j;
}
} // namespace

TEST_CASE("a keep-out line: bodies roam and are thrown, and never come within its distance",
          "[tiles][hop][geography][seek][nav][adr2003]") {
    // ADR-2003 (CPD-R10): the flight's route as `avoidLine`, kept `avoidDistance` off. Bodies with wide homes on both
    // sides run about for 20 s; none is ever nearer the line than the distance (grounded or in the air), they do run,
    // the crests still throw them, and a cold seek lands every body where the play does.
    const nlohmann::json j = keepOutScene();
    const auto distance = [](glm::vec2 p) {
        const std::vector<glm::vec2> pts = {{-200.0f, 1.0f}, {0.0f, 2.0f}, {200.0f, -1.0f}};
        float best = 1e9f;
        for (std::size_t i = 1; i < pts.size(); ++i) {
            const glm::vec2 ab = pts[i] - pts[i - 1];
            const float u = std::clamp(glm::dot(p - pts[i - 1], ab) / glm::dot(ab, ab), 0.0f, 1.0f);
            best = std::min(best, glm::length(p - (pts[i - 1] + ab * u)));
        }
        return best;
    };
    Rig rig(4.0f, j);
    float nearest = 1e9f;
    int running = 0;
    int throws = 0;
    std::vector<bool> was(3, false);
    for (long long k = 0; k <= 20 * 30; ++k) {
        const double t = static_cast<double>(k) / 30.0;
        rig.frame(t, k == 0 ? 0.0 : 1.0 / 30.0, static_cast<std::uint64_t>(k));
        const auto& es = rig.comp().entityWorld().entities();
        for (std::size_t i = 0; i < es.size(); ++i) {
            const entity::EntityState& st = es[i]->state();
            nearest = std::min(nearest, distance(glm::vec2(st.position().x, st.position().z)));
            running += st.activity == entity::Activity::Run ? 1 : 0;
            throws += st.airborne && !was[i] ? 1 : 0;
            was[i] = st.airborne;
        }
    }
    const auto want = rig.bodies();
    Rig sought(4.0f, j);
    sought.engine.seekSeconds(20.0);
    sought.frame(20.0, 0.0, 0);
    const auto got = sought.bodies();
    CAPTURE(nearest, running, throws);
    CHECK(nearest >= 5.0f - 1e-3f);
    CHECK(running > 30 * 3); // they run (more than three body-seconds of it)
    CHECK(throws >= 3);
    for (std::size_t i = 0; i < want.size(); ++i) {
        CAPTURE(i, want[i].x, want[i].z, got[i].x, got[i].z);
        CHECK(glm::length(want[i] - got[i]) < 1e-3f);
    }
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

TEST_CASE("every body's position and the water under it, on a real project (measure)",
          "[.][tiles][hop][measure][cpdr6]") {
    // CPD-R6. AVGEN_TRACE_PROJECT: a project file; AVGEN_TRACE_CSV: where to write; AVGEN_TRACE_FROM / _TO (s, default
    // 0 / the song's end) and AVGEN_TRACE_FPS (default 30). One row per frame per body:
    //   t, body (index), name, x, y, z, water (0 dry, 1 sea, 2 river, 3 pond: tileWaterBody on the sample the walker
    //   and landOnWater read, ground.planned at the tile under it), bank (that sample's bank channel), airborne,
    //   landed (1 on the first grounded frame after a flight), activity (entity::Activity), and the drawn camera's
    //   x, y, z that frame.
    const char* projectPath = std::getenv("AVGEN_TRACE_PROJECT");
    const char* csvPath = std::getenv("AVGEN_TRACE_CSV");
    if (projectPath == nullptr || csvPath == nullptr) SKIP("set AVGEN_TRACE_PROJECT and AVGEN_TRACE_CSV");
    const auto env = [](const char* k, double d) { return std::getenv(k) != nullptr ? std::atof(std::getenv(k)) : d; };
    const double fps = env("AVGEN_TRACE_FPS", 30.0);
    const double t0 = env("AVGEN_TRACE_FROM", 0.0);
    app::Engine engine{app::EngineMode::Offline};
    REQUIRE(engine.loadProject(projectPath).has_value());
    const double t1 = env("AVGEN_TRACE_TO", engine.durationSeconds());
    std::ofstream out(csvPath);
    out << "t,body,name,x,y,z,water,bank,airborne,landed,activity,camX,camY,camZ\n";
    const entity::TileNavRules rules; // the walker's own threshold
    std::vector<bool> was;
    for (long long k = 0; k <= static_cast<long long>(t1 * fps); ++k) {
        const double t = static_cast<double>(k) / fps;
        engine.update(FrameTime{t, k == 0 ? 0.0 : 1.0 / fps, static_cast<std::uint64_t>(k)});
        scene::Composition* comp = engine.composition();
        REQUIRE(comp != nullptr);
        const world::GroundSource* g = comp->entityWorld().ground();
        REQUIRE(g != nullptr);
        const auto& es = comp->entityWorld().entities();
        was.resize(es.size(), false);
        const glm::vec3 cam = comp->scene().camera.position;
        for (std::size_t i = 0; i < es.size(); ++i) {
            const entity::EntityState& st = es[i]->state();
            const bool landed = was[i] && !st.airborne;
            was[i] = st.airborne;
            if (t < t0) continue;
            const glm::vec3 p = st.position();
            const world::GroundSample s = g->planned(p.x, p.z, t);
            out << t << "," << i << "," << es[i]->name() << "," << p.x << "," << p.y << "," << p.z << ","
                << entity::tileWaterBody(s, rules) << "," << s.bank << "," << (st.airborne ? 1 : 0) << ","
                << (landed ? 1 : 0) << "," << static_cast<int>(st.activity) << "," << cam.x << "," << cam.y << ","
                << cam.z << "\n";
        }
    }
}

TEST_CASE("every launch against the drawn tile and the landscape, on a real project (measure)",
          "[.][tiles][hop][measure][cpdr9]") {
    // CPD-R9. AVGEN_LAUNCH_PROJECT: a project file; AVGEN_LAUNCH_DIR: where to write; AVGEN_LAUNCH_TO (s, default the
    // song's end); AVGEN_LAUNCH_FPS (default 60). Every body, one run. Two files:
    //   frames.csv: per frame per body -- t, body, x, y, z, airborne, activity, drawn (the drawn top under the body, the
    //     surface art's r6 audit reads);
    //   launches.csv: per launch, the tile it left from sampled at 60 Hz over [launch - 2, launch + 1] s once that span
    //     has played (so every landing in it has happened) -- event, body, t0, crouched (the frame before was a crouch:
    //     a crest throw), u, drawn (all springs), land (the landscape: no springs, what the throw reads), own (the
    //     body's own landings' part of the drawn top), others (the other bodies' part), known (the springs of the
    //     landings made by the crouch's start, launch - 0.8 s - 0.1 s: what a throw could have known when it committed).
    const char* projectPath = std::getenv("AVGEN_LAUNCH_PROJECT");
    const char* dirPath = std::getenv("AVGEN_LAUNCH_DIR");
    if (projectPath == nullptr || dirPath == nullptr) SKIP("set AVGEN_LAUNCH_PROJECT and AVGEN_LAUNCH_DIR");
    const auto env = [](const char* k, double d) { return std::getenv(k) != nullptr ? std::atof(std::getenv(k)) : d; };
    const double fps = env("AVGEN_LAUNCH_FPS", 60.0);
    app::Engine engine{app::EngineMode::Offline};
    REQUIRE(engine.loadProject(projectPath).has_value());
    const double t1 = env("AVGEN_LAUNCH_TO", engine.durationSeconds());
    const std::filesystem::path dir(dirPath);
    std::ofstream frames(dir / "frames.csv");
    std::ofstream launches(dir / "launches.csv");
    frames << "t,body,x,y,z,airborne,activity,drawn\n";
    launches << "event,body,t0,crouched,u,drawn,land,own,others,known\n";
    struct Launch {
        int event;
        std::size_t body;
        double t0;
        glm::vec2 tile;
        bool crouched;
    };
    struct Body {
        bool air = false;
        bool crouch = false;
        std::vector<scene::TileEvent> landed; // its own landings (world space, as the ground keeps them)
    };
    std::vector<Body> bodies;
    std::vector<Launch> open;
    int events = 0;
    const auto own = [](const scene::TileEvent& e, const Body& b) {
        for (const scene::TileEvent& l : b.landed) {
            if (std::abs(l.time - e.time) < 1e-3f && glm::length(l.position - e.position) < 0.05f) return true;
        }
        return false;
    };
    for (long long k = 0; k <= static_cast<long long>(t1 * fps); ++k) {
        const double t = static_cast<double>(k) / fps;
        engine.update(FrameTime{t, k == 0 ? 0.0 : 1.0 / fps, static_cast<std::uint64_t>(k)});
        scene::Composition* comp = engine.composition();
        REQUIRE(comp != nullptr);
        const auto* g = dynamic_cast<const scene::TileGround*>(comp->entityWorld().ground());
        REQUIRE(g != nullptr);
        const auto& es = comp->entityWorld().entities();
        bodies.resize(es.size());
        for (std::size_t i = 0; i < es.size(); ++i) {
            const entity::EntityState& st = es[i]->state();
            const glm::vec3 p = st.position();
            Body& b = bodies[i];
            if (st.airborne && !b.air) {
                open.push_back(Launch{events++, i, t, g->cellCentre(p.x, p.z), b.crouch});
            }
            if (!st.airborne && b.air) {
                // the landing the hop just registered: the newest landing at this body's tile
                const glm::vec2 c = g->cellCentre(p.x, p.z);
                const scene::TileEvent* best = nullptr;
                for (const scene::TileEvent& e : g->landings()) {
                    if (glm::length(e.position - c) < 0.75f * g->pitch() && (best == nullptr || e.time > best->time)) {
                        best = &e;
                    }
                }
                if (best != nullptr) b.landed.push_back(*best);
            }
            b.air = st.airborne;
            b.crouch = !st.airborne && st.activity == entity::Activity::Jump;
            frames << t << "," << i << "," << p.x << "," << p.y << "," << p.z << "," << (st.airborne ? 1 : 0) << ","
                   << static_cast<int>(st.activity) << "," << g->at(p.x, p.z, t).height << "\n";
        }
        // the launches whose span has played: the tile they left, with and without each body's landings
        const bool last = k == static_cast<long long>(t1 * fps);
        for (auto it = open.begin(); it != open.end();) {
            if (t < it->t0 + 1.0 && !last) {
                ++it;
                continue;
            }
            std::vector<scene::TileEvent> others;
            for (const scene::TileEvent& e : g->landings()) {
                if (!own(e, bodies[it->body])) others.push_back(e);
            }
            scene::TileGround withoutOwn = *g;
            withoutOwn.restoreLandings(others);
            std::vector<scene::TileEvent> early;
            for (const scene::TileEvent& e : g->landings()) {
                if (e.time <= it->t0 - 0.9) early.push_back(e);
            }
            scene::TileGround known = *g;
            known.restoreLandings(early);
            for (int j = -120; j <= 60; ++j) {
                const double u = it->t0 + j / 60.0;
                const float drawn = g->at(it->tile.x, it->tile.y, u).height;
                const float land = g->landscape(it->tile.x, it->tile.y, u).height;
                const float noOwn = withoutOwn.at(it->tile.x, it->tile.y, u).height;
                launches << it->event << "," << it->body << "," << it->t0 << "," << (it->crouched ? 1 : 0) << "," << u
                         << "," << drawn << "," << land << "," << (drawn - noOwn) << "," << (noOwn - land) << ","
                         << (known.at(it->tile.x, it->tile.y, u).height - land) << "\n";
            }
            it = open.erase(it);
        }
    }
}
