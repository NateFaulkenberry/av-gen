// ADR-1573: a seek lands a route that reads a macro where a play does, when other routes feed the macro.
//
// A macro source publishes `macro.<k>` from its knob `macros/<k>`, and a route may write that knob -- a scene's
// "musical control" layer (Rainbow World's swell, synth light, world scan). Only the control replay (ADR-1168) steps
// the sources, and it ran only for projects with scene states; without it, the landing frame of a seek read the knob's
// reset value, so everything downstream of the macro was a frame (or, through a chain, a whole envelope) wrong.

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

std::filesystem::path clicks() {
    static const std::filesystem::path path = [] {
        const std::size_t frames = 8 * kRate;
        auto tone = testsupport::sine(55.0f, kRate, frames, 0.4f);
        auto click = testsupport::clickTrack(120.0f, kRate, frames);
        std::vector<float> mono(frames);
        for (std::size_t i = 0; i < frames; ++i) {
            mono[i] = tone[i] * (0.5f + 0.5f * std::sin(static_cast<float>(i) / kRate * 1.3f)) + click[i] * 0.6f;
        }
        auto file = audio::AudioFile::fromInterleaved(testsupport::interleave(mono, 2), 2, kRate);
        const auto p = testsupport::processTempDir() / "macro_seek_clicks.wav";
        REQUIRE(file.writeWav(p).has_value());
        return p;
    }();
    return path;
}

struct Rig {
    app::Engine engine{app::EngineMode::Offline};
    explicit Rig(bool feedTheMacro) {
        REQUIRE(engine.loadAudio(clicks()).has_value());
        auto macros = std::make_unique<signals::MacroSource>("macros");
        macros->addKnob("k", 0.0f);
        engine.sources().add(std::move(macros));
        engine.sources().attach(engine.signals(), engine.params());
        if (feedTheMacro) {
            params::ModRoute in; // a stateful route INTO the knob: each onset, falling over a second
            in.source = "audio.onset";
            in.target = "macros/k";
            in.chain.envelope = params::EnvelopeMode::LinearFall;
            in.chain.envelopeFallPerSecond = 1.0f;
            engine.modulator().addRoute(std::move(in));
        }
        params::ModRoute out; // a stateless route OUT of the knob
        out.source = "macro.k";
        out.target = "post/grade/saturation";
        out.amount = 2.0f;
        engine.modulator().addRoute(std::move(out));
        engine.rebind();
    }
    void frame(long long k) {
        engine.update(FrameTime{static_cast<double>(k) / 60.0, k == 0 ? 0.0 : 1.0 / 60.0, static_cast<std::uint64_t>(k)});
    }
    void landing(long long k) { engine.update(FrameTime{static_cast<double>(k) / 60.0, 0.0, 0}); }
    [[nodiscard]] float value() { return engine.params().find("post/grade/saturation")->finalComponent(0); }
};

} // namespace

TEST_CASE("A seek lands a route reading a macro, fed by another route, where a play does", "[seek][modulation][adr1573]") {
    for (const long long t : {155LL, 211LL, 300LL}) {
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
        CHECK(seeked.value() == want[0]);
        seeked.frame(t + 1);
        CHECK(seeked.value() == want[1]);
        seeked.frame(t + 2);
        CHECK(seeked.value() == want[2]);
    }
    // Some instant in the run is mid-envelope, or the case proves nothing about the replay.
    Rig probe(true);
    float peak = 0.0f;
    for (long long k = 0; k <= 300; ++k) {
        probe.frame(k);
        peak = std::max(peak, probe.value());
    }
    CHECK(peak > 0.5f);
}

TEST_CASE("Without a route into a source, a seek does not pay for the control replay", "[seek][modulation][adr1573]") {
    Rig rig(false);
    CHECK_FALSE(rig.engine.controlReplayNeeded());
}
