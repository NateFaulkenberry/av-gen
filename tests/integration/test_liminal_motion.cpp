// ADR-1041: the spring and integrate chain stages -- continuous musical modulation for the Liminal POC.
// Unit behaviour of the stages, then the property the owner's brief depends on: a seek lands them
// where a play does (ADR-091 / ADR-901), with audio and without.

#include "app/engine.hpp"
#include "audio/audio_file.hpp"
#include "core/time.hpp"
#include "params/modulation.hpp"
#include "params/processor.hpp"
#include "params/serialization.hpp"
#include "signals/source.hpp"
#include "support/synth.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <vector>

using namespace avgen;
using Catch::Matchers::WithinAbs;

namespace {

// Runs a chain over a step input (0 for a second, then 1) at `fps`, returning the output sampled at
// every 1/60 s instant (the frames of a 60 fps run; other rates sample the nearest earlier frame).
std::vector<float> stepResponse(const params::ProcessorChain& chain, double fps, double seconds) {
    params::ProcessorChain::State state;
    std::vector<float> out;
    const double dt = 1.0 / fps;
    double t = 0.0;
    double nextSample = 1.0 / 60.0; // sample i is the instant (i + 1) / 60
    float y = chain.process(0.0f, false, 0.0, state);
    while (t < seconds - 1e-9) {
        const float x = t + dt >= 1.0 - 1e-9 ? 1.0f : 0.0f;
        y = chain.process(x, false, dt, state);
        t += dt;
        while (nextSample <= t + 1e-9) {
            out.push_back(y);
            nextSample += 1.0 / 60.0;
        }
    }
    return out;
}

} // namespace

TEST_CASE("Spring: a critically damped step rises smoothly, never overshoots, and settles", "[liminal][processor][adr1041]") {
    params::ProcessorChain chain;
    chain.springHz = 0.5f;
    chain.springDamping = 1.0f;
    const auto y = stepResponse(chain, 60.0, 6.0);
    REQUIRE(y.size() >= 300);
    float maxJump = 0.0f;
    float maxAccelStep = 0.0f;
    for (std::size_t i = 1; i < y.size(); ++i) {
        CHECK(y[i] <= 1.0f + 1e-6f);        // no overshoot
        CHECK(y[i] >= y[i - 1] - 1e-7f);    // monotone
        maxJump = std::max(maxJump, y[i] - y[i - 1]);
        if (i >= 2) {
            const float v1 = y[i] - y[i - 1];
            const float v0 = y[i - 1] - y[i - 2];
            maxAccelStep = std::max(maxAccelStep, std::fabs(v1 - v0));
        }
    }
    // A one-pole would jump by (1 - exp(-dt/tau)) on the step frame; the spring starts at rest.
    CHECK(maxJump < 0.025f);        // peak speed omega / e = 1.16 units/s
    CHECK(maxAccelStep < 0.004f); // velocity changes gradually (omega^2 dt^2 = 0.0027): no corner
    CHECK(y[60] < 0.01f);          // barely moved one frame after the step
    CHECK(y[60 + 150] > 0.95f);    // 2.5 s after it: settled (critical: ~4.7 / omega = 1.5 s to 95%)
}

TEST_CASE("Spring: under-damped overshoots and rings down; the response is frame-rate independent",
          "[liminal][processor][adr1041]") {
    params::ProcessorChain chain;
    chain.springHz = 0.8f;
    chain.springDamping = 0.35f;
    const auto y60 = stepResponse(chain, 60.0, 8.0);
    const float peak = *std::max_element(y60.begin(), y60.end());
    CHECK(peak > 1.1f);
    CHECK_THAT(static_cast<double>(y60.back()), WithinAbs(1.0, 0.02));

    // A smooth input (a step's timing is quantised to each rate's frames, which would be measured
    // instead), compared at instants all three runs have a frame at.
    const auto run = [&](double fps) {
        params::ProcessorChain::State state;
        std::vector<std::pair<double, float>> out;
        const double dt = 1.0 / fps;
        const auto input = [](double t) { return 0.5f + 0.5f * static_cast<float>(std::sin(1.3 * t)); };
        (void)chain.process(input(0.0), false, 0.0, state);
        for (int k = 1; k <= static_cast<int>(fps * 6.0); ++k) {
            const double t = k * dt;
            out.emplace_back(t, chain.process(input(t), false, dt, state));
        }
        return out;
    };
    const auto a = run(60.0);
    const auto b = run(144.0);
    const auto c = run(30.0);
    float worst = 0.0f;
    for (int k = 1; k <= 36; ++k) { // every 1/6 s
        const float v60 = a[static_cast<std::size_t>(k * 10 - 1)].second;
        const float v144 = b[static_cast<std::size_t>(k * 24 - 1)].second;
        const float v30 = c[static_cast<std::size_t>(k * 5 - 1)].second;
        worst = std::max({worst, std::fabs(v60 - v144), std::fabs(v60 - v30)});
    }
    CHECK(worst < 0.02f);
}

TEST_CASE("Integrate: a rate becomes a position that never jumps and holds at zero rate", "[liminal][processor][adr1041]") {
    params::ProcessorChain chain;
    chain.integrate = true;
    chain.remapEnabled = true; // pace: 0.4 m/s floor, 1.6 m/s at full input
    chain.remapInMin = 0.0f;
    chain.remapInMax = 1.0f;
    chain.remapOutMin = 0.4f;
    chain.remapOutMax = 1.6f;
    params::ProcessorChain::State s;
    CHECK(chain.process(0.0f, false, 0.0, s) == 0.0f); // the first frame adds nothing
    float last = 0.0f;
    for (int k = 1; k <= 600; ++k) {
        const float input = (k / 60) % 2 == 0 ? 0.0f : 1.0f; // a square wave of loudness
        const float y = chain.process(input, false, 1.0 / 60.0, s);
        CHECK(y > last);                    // always moving forward (the floor)
        CHECK(y - last <= 1.6f / 60.0f + 1e-5f);
        last = y;
    }
    CHECK_THAT(static_cast<double>(last), WithinAbs(5.0 * 0.4 + 5.0 * 1.6, 0.05));

    params::ProcessorChain hold;
    hold.integrate = true;
    params::ProcessorChain::State h;
    (void)hold.process(1.0f, false, 0.0, h);
    const float at1 = [&] {
        float y = 0.0f;
        for (int k = 0; k < 60; ++k) {
            y = hold.process(1.0f, false, 1.0 / 60.0, h);
        }
        return y;
    }();
    CHECK_THAT(static_cast<double>(at1), WithinAbs(1.0, 1e-5));
    for (int k = 0; k < 60; ++k) {
        CHECK(hold.process(0.0f, false, 1.0 / 60.0, h) == at1); // silence: the journey pauses
    }
}

TEST_CASE("Spring and integrate round-trip through JSON and are omitted when unused", "[liminal][processor][adr1041]") {
    params::ProcessorChain chain;
    CHECK_FALSE(params::chainToJson(chain).contains("springHz"));
    CHECK_FALSE(params::chainToJson(chain).contains("integrate"));
    chain.springHz = 0.3f;
    chain.springDamping = 0.6f;
    chain.integrate = true;
    const auto back = params::chainFromJson(params::chainToJson(chain));
    REQUIRE(back.has_value());
    CHECK(back->springHz == 0.3f);
    CHECK(back->springDamping == 0.6f);
    CHECK(back->integrate);
    CHECK_FALSE(params::chainFromJson(nlohmann::json{{"springHz", -1.0}}).has_value());
    CHECK_FALSE(params::chainFromJson(nlohmann::json{{"springHz", 500.0}}).has_value());
}

namespace {

constexpr std::uint32_t kRate = 44100;

std::filesystem::path swellWav() {
    static const std::filesystem::path path = [] {
        const std::size_t frames = 10 * kRate;
        auto tone = testsupport::sine(55.0f, kRate, frames, 0.5f);
        auto clicks = testsupport::clickTrack(120.0f, kRate, frames);
        std::vector<float> mono(frames);
        for (std::size_t i = 0; i < frames; ++i) {
            const float swell = 0.5f + 0.5f * std::sin(static_cast<float>(i) / static_cast<float>(kRate) * 1.57f);
            mono[i] = tone[i] * swell + clicks[i] * 0.6f;
        }
        auto file = audio::AudioFile::fromInterleaved(testsupport::interleave(mono, 2), 2, kRate);
        const auto p = testsupport::processTempDir() / "liminal_motion_swell.wav";
        REQUIRE(file.writeWav(p).has_value());
        return p;
    }();
    return path;
}

constexpr std::array<const char*, 3> kTargets{"post/grade/contrast", "post/bloom/threshold", "post/anamorphic/intensity"};

struct Rig {
    app::Engine engine{app::EngineMode::Offline};
    explicit Rig(bool withAudio) {
        if (withAudio) {
            REQUIRE(engine.loadAudio(swellWav()).has_value());
        }
        auto lfo = std::make_unique<signals::LfoSource>("drift");
        engine.sources().add(std::move(lfo));
        engine.params().findAs<float>("sources/drift/rate")->setBase(0.13f);
        engine.sources().attach(engine.signals(), engine.params());
        auto route = [&](const char* source, const char* target) -> params::ModRoute& {
            params::ModRoute r;
            r.source = source;
            r.target = target;
            return engine.modulator().addRoute(std::move(r));
        };
        {   // bass: the world breathing under the weight of the low end
            params::ModRoute& r = route("audio.bass", kTargets[0]);
            r.chain.attackMs = 120.0f;
            r.chain.decayMs = 600.0f;
            r.chain.springHz = 0.4f;
            r.chain.springDamping = 0.5f;
        }
        {   // a pace integrated into a distance: loud passages travel faster, never zero
            params::ModRoute& r = route("audio.rms", kTargets[1]);
            r.chain.attackMs = 300.0f;
            r.chain.decayMs = 1200.0f;
            r.chain.remapEnabled = true;
            r.chain.remapOutMin = 0.3f;
            r.chain.remapOutMax = 1.2f;
            r.chain.integrate = true;
            r.amount = 0.01f;
        }
        {   // an autonomous trajectory through a spring (a pure source, replayed by a seek)
            params::ModRoute& r = route("lfo.drift", kTargets[2]);
            r.chain.springHz = 0.25f;
            r.chain.integrate = true;
        }
        engine.rebind();
    }
    void frame(long long k) {
        engine.update(FrameTime{static_cast<double>(k) / 60.0, k == 0 ? 0.0 : 1.0 / 60.0, static_cast<std::uint64_t>(k)});
    }
    void landing(long long k) { engine.update(FrameTime{static_cast<double>(k) / 60.0, 0.0, 0}); }
    [[nodiscard]] std::array<float, 3> values() {
        std::array<float, 3> out{};
        for (std::size_t i = 0; i < kTargets.size(); ++i) {
            out[i] = engine.params().find(kTargets[i])->finalComponent(0);
        }
        return out;
    }
};

void seekEqualsPlay(bool withAudio) {
    const long long t = 331; // 5.517 s
    Rig played(withAudio);
    std::vector<std::array<float, 3>> ref;
    for (long long k = 0; k <= t + 3; ++k) {
        played.frame(k);
        if (k >= t) {
            ref.push_back(played.values());
        }
    }
    Rig seeked(withAudio);
    seeked.engine.seekSeconds(static_cast<double>(t) / 60.0);
    seeked.landing(t);
    for (long long k = t; k <= t + 3; ++k) {
        if (k > t) {
            seeked.frame(k);
        }
        const auto v = seeked.values();
        for (std::size_t i = 0; i < kTargets.size(); ++i) {
            INFO("frame " << k << " target " << kTargets[i] << (withAudio ? " (audio)" : " (no audio)"));
            CHECK(v[i] == ref[static_cast<std::size_t>(k - t)][i]);
        }
    }
    // The integrated routes moved: something was actually carried.
    CHECK(ref.front()[1] > 0.3f * 0.01f * 5.0f);
    CHECK(ref.front()[2] != 0.0f);
}

} // namespace

TEST_CASE("Spring and integrate routes land a seek where a play does (ADR-901), with and without audio",
          "[liminal][seek][modulation][adr1041]") {
    SECTION("audio") { seekEqualsPlay(true); }
    SECTION("no audio") { seekEqualsPlay(false); }
}
