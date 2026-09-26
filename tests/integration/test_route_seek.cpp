// ADR-901: a seek lands a route where a play does -- its delay line, its smoothing, its envelope --
// because the seek's replay rebuilds the signals the route reads and runs its chain on them, and the
// chain states ride in the seek checkpoints. And ADR-900's depth, which is stateless, follows.
//
// The control arm is the seek as it was before ADR-901: the same engine, the same seek, and then the
// route states reset (`Modulator::resetState`, which is all the old seek did to a route). It must land
// somewhere else, or the case proves nothing.

#include "app/engine.hpp"
#include "audio/audio_file.hpp"
#include "core/time.hpp"
#include "params/modulation.hpp"
#include "signals/source.hpp"
#include "support/synth.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

using namespace avgen;

namespace {

constexpr std::uint32_t kRate = 44100;

// Ten seconds of a 120 BPM click over a low tone: onsets every half second, and bass and rms that
// move with them, so every kind of source the routes read has something to say.
std::filesystem::path clickWav() {
    static const std::filesystem::path path = [] {
        const std::size_t frames = 10 * kRate;
        auto tone = testsupport::sine(55.0f, kRate, frames, 0.5f);
        auto clicks = testsupport::clickTrack(120.0f, kRate, frames);
        std::vector<float> mono(frames);
        for (std::size_t i = 0; i < frames; ++i) {
            // The tone swells and falls every four seconds, so bass and rms are not constants.
            const float swell = 0.5f + 0.5f * std::sin(static_cast<float>(i) / static_cast<float>(kRate) * 1.57f);
            mono[i] = tone[i] * swell + clicks[i] * 0.6f;
        }
        auto file = audio::AudioFile::fromInterleaved(testsupport::interleave(mono, 2), 2, kRate);
        const auto p = testsupport::processTempDir() / "route_seek_clicks.wav";
        REQUIRE(file.writeWav(p).has_value());
        return p;
    }();
    return path;
}

// One craft on a simulated orbit: an entity, so a seek goes through the entity replay and its
// checkpoints rather than through the signal pipeline alone.
constexpr const char* kScene = R"({ "format": "avgen-scene", "version": 1, "name": "route-seek",
  "nodes": [ { "kind": "orb", "name": "craft", "position": [0, 6, 0] } ],
  "entities": [ { "name": "craft", "node": "craft", "seed": 11,
                  "behaviors": [ { "kind": "orbit", "radius": 15.0, "rate": 20.0, "authority": "simulation" } ] } ] })";

struct Target {
    const char* path;
    const char* what;
};
// Each target is written by exactly one of the routes below.
constexpr std::array<Target, 5> kTargets{{
    {"post/halation/intensity", "audio.onset, delayed 180 ms, attack 30 / decay 400"},
    {"post/grade/saturation", "timeline.kick (event mode), delayed 120 ms, decay 300"},
    {"post/grade/contrast", "audio.rms, attack 200 / decay 1500, no delay"},
    {"post/anamorphic/intensity", "audio.bass with its depth on lfo.wob"},
    {"post/bloom/threshold", "lfo.wob delayed 250 ms"},
}};

struct Rig {
    app::Engine engine{app::EngineMode::Offline};

    explicit Rig(bool withComposition, bool withAudio = true) {
        if (withComposition) {
            REQUIRE(engine.setCompositionJson(nlohmann::json::parse(kScene)).has_value());
        }
        if (withAudio) {
            REQUIRE(engine.loadAudio(clickWav()).has_value());
        }
        signals::SourceRack& rack = engine.sources();
        auto lfo = std::make_unique<signals::LfoSource>("wob");
        rack.add(std::move(lfo));
        engine.params().findAs<float>("sources/wob/rate")->setBase(0.7f);
        auto kick = std::make_unique<signals::TimelineSource>("kick");
        kick->setMode(signals::TimelineMode::Event);
        for (int k = 0; k < 24; ++k) {
            // Off the frame grid on purpose: the hits land between frames at every rate.
            kick->addKey({0.43 + 0.377 * k, 0.9f, signals::KeyInterpolation::Step});
        }
        rack.add(std::move(kick));
        rack.attach(engine.signals(), engine.params());

        auto route = [&](const char* source, const char* target) -> params::ModRoute& {
            params::ModRoute r;
            r.source = source;
            r.target = target;
            return engine.modulator().addRoute(std::move(r));
        };
        {
            params::ModRoute& r = route("audio.onset", "post/halation/intensity");
            r.chain.delayMs = 180.0f;
            r.chain.attackMs = 30.0f;
            r.chain.decayMs = 400.0f;
        }
        {
            params::ModRoute& r = route("timeline.kick", "post/grade/saturation");
            r.chain.delayMs = 120.0f;
            r.chain.decayMs = 300.0f;
        }
        {
            params::ModRoute& r = route("audio.rms", "post/grade/contrast");
            r.chain.attackMs = 200.0f;
            r.chain.decayMs = 1500.0f;
        }
        {
            params::ModRoute& r = route("audio.bass", "post/anamorphic/intensity");
            r.depthSource = "lfo.wob";
            r.depthMin = 0.2f;
        }
        {
            params::ModRoute& r = route("lfo.wob", "post/bloom/threshold");
            r.chain.delayMs = 250.0f;
        }
        engine.rebind();
        for (const Target& t : kTargets) {
            REQUIRE(engine.params().find(t.path) != nullptr);
        }
    }

    void frame(long long k) {
        engine.update(FrameTime{static_cast<double>(k) / 60.0, k == 0 ? 0.0 : 1.0 / 60.0, static_cast<std::uint64_t>(k)});
    }
    // A seek's landing frame, as a render's first tick after its seek has it: the landed instant, dt 0.
    void landing(long long k) { engine.update(FrameTime{static_cast<double>(k) / 60.0, 0.0, 0}); }

    [[nodiscard]] std::array<float, kTargets.size()> values() {
        std::array<float, kTargets.size()> out{};
        for (std::size_t i = 0; i < kTargets.size(); ++i) {
            out[i] = engine.params().find(kTargets[i].path)->finalComponent(0);
        }
        return out;
    }
};

using Values = std::array<float, kTargets.size()>;

// A play from zero, recording each frame's values at the frames asked for.
std::vector<Values> play(bool withComposition, const std::vector<long long>& frames, bool withAudio = true) {
    Rig rig(withComposition, withAudio);
    std::vector<Values> out;
    long long k = 0;
    for (const long long f : frames) {
        for (; k <= f; ++k) {
            rig.frame(k);
        }
        out.push_back(rig.values());
    }
    return out;
}

void requireEqual(const Values& played, const Values& seeked, long long frame, const char* when) {
    for (std::size_t i = 0; i < kTargets.size(); ++i) {
        INFO(when << " frame " << frame << " (" << frame / 60.0 << " s): " << kTargets[i].path << " -- "
                  << kTargets[i].what);
        CHECK(seeked[i] == played[i]);
    }
}

} // namespace

TEST_CASE("A seek lands delayed, smoothed and depth-scaled routes where a play does (signal pipeline alone)",
          "[seek][modulation][adr901][adr900]") {
    const long long t = 187; // 3.117 s
    const auto played = play(false, {t, t + 1, t + 2});
    Rig rig(false);
    rig.engine.seekSeconds(static_cast<double>(t) / 60.0);
    rig.landing(t);
    requireEqual(played[0], rig.values(), t, "landing");
    rig.frame(t + 1);
    requireEqual(played[1], rig.values(), t + 1, "played on");
    rig.frame(t + 2);
    requireEqual(played[2], rig.values(), t + 2, "played on");

    // Control: the seek as it was. The routes' histories are gone, and the delayed and smoothed ones
    // land elsewhere; the depth route and the undelayed LFO do not depend on history and still agree.
    Rig old(false);
    old.engine.seekSeconds(static_cast<double>(t) / 60.0);
    old.engine.modulator().resetState();
    old.landing(t);
    const Values before = old.values();
    CHECK(before[0] != played[0][0]); // the delayed onset route
    CHECK(before[2] != played[0][2]); // the 1.5 s decay
    CHECK(before[3] == played[0][3]); // depth is stateless
}

TEST_CASE("A seek through the entity replay and its checkpoints lands routes where a play does",
          "[seek][modulation][checkpoint][adr901]") {
    // 4.5 s first (a replay from zero that records a checkpoint every second), then 7.25 s (resumed
    // from the 4 s checkpoint the first seek recorded, and replayed forward), then back to 2 s
    // (resumed from the 1 s checkpoint, which carries the delay lines' history across it).
    const std::vector<long long> targets{270, 435, 120};
    std::vector<long long> sorted = targets;
    std::sort(sorted.begin(), sorted.end());
    std::vector<long long> frames;
    for (const long long f : sorted) {
        frames.push_back(f);
        frames.push_back(f + 1);
    }
    const auto played = play(true, frames);
    const auto playedAt = [&](long long f) {
        const auto it = std::find(frames.begin(), frames.end(), f);
        REQUIRE(it != frames.end());
        return played[static_cast<std::size_t>(it - frames.begin())];
    };
    Rig rig(true);
    for (const long long t : targets) {
        rig.engine.seekSeconds(static_cast<double>(t) / 60.0);
        rig.landing(t);
        requireEqual(playedAt(t), rig.values(), t, "landing");
        rig.frame(t + 1);
        requireEqual(playedAt(t + 1), rig.values(), t + 1, "played on");
    }
}

TEST_CASE("An edited route drops the seek checkpoints its chain states were recorded under",
          "[seek][modulation][checkpoint][adr901]") {
    Rig rig(true);
    const long long t = 300;
    rig.engine.seekSeconds(static_cast<double>(t) / 60.0); // records checkpoints under this chain
    // Change the delayed route's chain. A checkpoint that kept the old chain's history would land the
    // new chain where the old one was.
    for (params::ModRoute& r : rig.engine.modulator().routes()) {
        if (r.target == "post/halation/intensity") {
            r.chain.delayMs = 60.0f;
            r.chain.decayMs = 150.0f;
        }
    }
    rig.engine.seekSeconds(static_cast<double>(t) / 60.0);
    rig.landing(t);
    const float seeked = rig.engine.params().find("post/halation/intensity")->finalComponent(0);

    Rig reference(true);
    for (params::ModRoute& r : reference.engine.modulator().routes()) {
        if (r.target == "post/halation/intensity") {
            r.chain.delayMs = 60.0f;
            r.chain.decayMs = 150.0f;
        }
    }
    for (long long k = 0; k <= t; ++k) {
        reference.frame(k);
    }
    CHECK(seeked == reference.engine.params().find("post/halation/intensity")->finalComponent(0));
}

TEST_CASE("An offline seek with no audio replays the routes on the pipeline alone", "[seek][modulation][adr901]") {
    // No analysed track: no signal replay (ADR-870's scope). The LFO and the scored timeline are still
    // functions of time, and their delayed routes land where the play puts them.
    const long long t = 250;
    const auto played = play(false, {t, t + 1}, false);
    Rig rig(false, false);
    rig.engine.seekSeconds(static_cast<double>(t) / 60.0);
    rig.landing(t);
    requireEqual(played[0], rig.values(), t, "landing");
    rig.frame(t + 1);
    requireEqual(played[1], rig.values(), t + 1, "played on");

    // Control: without the replay the timeline's and the LFO's delay lines are empty after the seek.
    Rig old(false, false);
    old.engine.seekSeconds(static_cast<double>(t) / 60.0);
    old.engine.modulator().resetState();
    old.landing(t);
    const Values before = old.values();
    CHECK((before[1] != played[0][1] || before[4] != played[0][4]));
}

// ADR-901's cost, on the Glowmere multicam film: a cold seek to 150 s (a replay from zero) and a warm
// one (from the checkpoint a second before), each with the project's routes and with them removed,
// interleaved, minima of three. Hidden: it is a measurement, and it is slow.
//   ./build/release/tests/avgen_tests "[.bench][adr901]" -s
TEST_CASE("the cost of replaying a film's routes in a seek", "[.bench][seek][modulation][adr901]") {
    const auto project =
        std::filesystem::path(AVGEN_SOURCE_DIR) / "examples" / "world" / "glowmere-valley-2-multicam.json";
    if (!std::filesystem::exists(std::filesystem::path(AVGEN_SOURCE_DIR) / "assets" / "farm" / "cow.glb")) {
        SKIP("farm assets missing");
    }
    const auto ms = [](auto start) {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    };
    std::array<double, 2> cold{1e18, 1e18};
    std::array<double, 2> warm{1e18, 1e18};
    for (int repeat = 0; repeat < 3; ++repeat) {
        for (int arm = 0; arm < 2; ++arm) {
            app::Engine engine(app::EngineMode::Offline);
            engine.setLiveControl(false);
            REQUIRE(engine.loadProject(project).has_value());
            if (arm == 1) {
                // The control: no project routes to replay (the reactions stay, as ADR-870 replays them).
                auto& routes = engine.modulator().routes();
                std::erase_if(routes, [](const params::ModRoute& r) { return !r.fromEntity; });
                engine.rebind();
            }
            auto start = std::chrono::steady_clock::now();
            engine.seekSeconds(150.0);
            cold[static_cast<std::size_t>(arm)] = std::min(cold[static_cast<std::size_t>(arm)], ms(start));
            start = std::chrono::steady_clock::now();
            engine.seekSeconds(149.5);
            warm[static_cast<std::size_t>(arm)] = std::min(warm[static_cast<std::size_t>(arm)], ms(start));
        }
    }
    WARN("cold seek to 150 s: " << cold[0] << " ms with the project's routes, " << cold[1] << " ms without");
    WARN("warm seek to 149.5 s: " << warm[0] << " ms with the project's routes, " << warm[1] << " ms without");
    CHECK(cold[0] > 0.0);
}
