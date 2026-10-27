// ADR-1801: a seek lands an integrating route where a play does.
//
// An integrating route (ADR-1041, `chain.integrate`) carries a running total for the whole film: the bands that drift
// with the level, a phase pushed on by each onset. The seek's signal replay (ADR-901) rebuilds a route's chain only when
// the signal it reads is one the replay rebuilds (`audio.*`, the clock, the stems, pure sources); a route on any other
// -- `response.*`, a stateful source -- relied on the control replay (ADR-1168), which ran only for scene states,
// conditioned signals and routes into a source. Without it a seek with an analysed track restarted that total at 0, so a render
// started mid-track (`--start`), and every scrub, drew a different picture from the play that reached the same second.

#include "app/engine.hpp"
#include "audio/audio_file.hpp"
#include "core/time.hpp"
#include "params/modulation.hpp"
#include "signals/source.hpp"
#include "support/synth.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <vector>

using namespace avgen;

namespace {

constexpr std::uint32_t kRate = 44100;

std::filesystem::path pulses() {
    static const std::filesystem::path path = [] {
        const std::size_t frames = 8 * kRate;
        auto tone = testsupport::sine(55.0f, kRate, frames, 0.4f);
        auto click = testsupport::clickTrack(120.0f, kRate, frames);
        std::vector<float> mono(frames);
        for (std::size_t i = 0; i < frames; ++i) {
            mono[i] = tone[i] * (0.5f + 0.5f * std::sin(static_cast<float>(i) / kRate * 1.3f)) + click[i] * 0.6f;
        }
        auto file = audio::AudioFile::fromInterleaved(testsupport::interleave(mono, 2), 2, kRate);
        const auto p = testsupport::processTempDir() / "integrate_seek_pulses.wav";
        REQUIRE(file.writeWav(p).has_value());
        return p;
    }();
    return path;
}

struct Rig {
    app::Engine engine{app::EngineMode::Offline};
    double fps = 60.0;
    explicit Rig(bool integrate, double framesPerSecond = 60.0) : fps(framesPerSecond) {
        REQUIRE(engine.loadAudio(pulses()).has_value());
        engine.renderSettings().fps = fps;
        // A source the seek's signal replay does not rebuild (a sample-and-hold on the onsets: not pure in time), read
        // by an integrating route. Neon Vector drifts its noise this way on `response.low`: before ADR-1801 a play
        // reached 978 m at 60 s and a seek 964 m.
        engine.sources().add(std::make_unique<signals::RandomSource>("held", "audio.onset", 7u));
        engine.sources().attach(engine.signals(), engine.params());
        params::ModRoute r;
        r.source = "random.held";
        r.target = "post/grade/saturation";
        r.amount = 0.02f; // small enough that the total never reaches the target's clamp
        r.chain.integrate = integrate;
        engine.modulator().addRoute(std::move(r));
        engine.rebind();
    }
    void frame(long long k) {
        engine.update(FrameTime{static_cast<double>(k) / fps, k == 0 ? 0.0 : 1.0 / fps, static_cast<std::uint64_t>(k)});
    }
    void landing(long long k) { engine.update(FrameTime{static_cast<double>(k) / fps, 0.0, 0}); }
    [[nodiscard]] float value() { return engine.params().find("post/grade/saturation")->finalComponent(0); }
};

} // namespace

TEST_CASE("A seek lands an integrating route where a play does", "[seek][modulation][adr1801]") {
    for (const long long t : {97LL, 240LL, 401LL}) {
        INFO("frame " << t);
        Rig played(true);
        std::vector<float> want;
        for (long long k = 0; k <= t + 2; ++k) {
            played.frame(k);
            if (k >= t) {
                want.push_back(played.value());
            }
        }
        Rig seeked(true);
        CHECK(seeked.engine.controlReplayNeeded());
        seeked.engine.seekSeconds(static_cast<double>(t) / 60.0);
        seeked.landing(t);
        INFO("played " << want[0] << ", seeked " << seeked.value());
        CHECK(seeked.value() == want[0]);
        seeked.frame(t + 1);
        CHECK(seeked.value() == want[1]);
        seeked.frame(t + 2);
        CHECK(seeked.value() == want[2]);
    }
    // The total must have moved well off its base by then, or the case proves nothing about the replay.
    Rig probe(true);
    const float base = probe.value();
    for (long long k = 0; k <= 240; ++k) {
        probe.frame(k);
    }
    INFO("base " << base << ", after 4 s " << probe.value());
    CHECK(std::abs(probe.value() - base) > 0.02f);
}

TEST_CASE("A sample-and-hold source alone makes a seek replay the control layer", "[seek][modulation][adr1801]") {
    // The k-th trigger since a reset yields the k-th value: a seek that reset it drew the first value again.
    Rig held(false);
    CHECK(held.engine.controlReplayNeeded());
    for (const long long t : {97LL, 240LL}) {
        Rig played(false);
        for (long long k = 0; k <= t; ++k) {
            played.frame(k);
        }
        Rig seeked(false);
        seeked.engine.seekSeconds(static_cast<double>(t) / 60.0);
        seeked.landing(t);
        INFO("frame " << t);
        CHECK(seeked.value() == played.value());
    }
}

TEST_CASE("A route on a replayed signal that does not integrate does not make a seek pay for the control replay",
          "[seek][modulation][adr1801]") {
    app::Engine engine{app::EngineMode::Offline};
    REQUIRE(engine.loadAudio(pulses()).has_value());
    params::ModRoute r;
    r.source = "audio.bass";
    r.target = "post/grade/saturation";
    r.chain.attackMs = 20.0f;
    r.chain.decayMs = 200.0f;
    engine.modulator().addRoute(std::move(r));
    engine.rebind();
    CHECK_FALSE(engine.controlReplayNeeded());
}

TEST_CASE("A seek lands an integral of a replayed signal where a play at 30 fps does", "[seek][modulation][adr1801]") {
    // `audio.bass` is a signal the seek's signal replay rebuilds, but on its own 60 Hz grid, and an integral summed
    // over 60 Hz steps is not the one a 30 fps render sums (Neon Vector's noise: 978 m against 964 m at 60 s).
    struct Bass : Rig {
        Bass() : Rig(true, 30.0) {
            engine.modulator().routes().clear();
            params::ModRoute r;
            r.source = "audio.bass";
            r.target = "post/grade/saturation";
            r.amount = 0.02f;
            r.chain.integrate = true;
            engine.modulator().addRoute(std::move(r));
            engine.rebind();
        }
    };
    for (const long long t : {61LL, 180LL}) {
        INFO("frame " << t);
        Bass played;
        for (long long k = 0; k <= t; ++k) {
            played.frame(k);
        }
        Bass seeked;
        seeked.engine.seekSeconds(static_cast<double>(t) / 30.0);
        seeked.landing(t);
        INFO("played " << played.value() << ", seeked " << seeked.value());
        CHECK(seeked.value() == played.value());
    }
}
