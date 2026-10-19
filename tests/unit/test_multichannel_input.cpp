// stems-multichannel M5 (ADR-1521): live input sources, through the same AudioInput, ChannelAnalysis and engine code a
// real device uses, driven by the synthetic 8-channel backend -- no hardware. Tagged [threading] so the TSan job runs
// them (tools/ci/run-suite.sh, TSAN_FILTER).

#include "analysis/channel_analysis.hpp"
#include "app/engine.hpp"
#include "audio/audio_input.hpp"
#include "audio/synthetic_capture.hpp"
#include "core/time.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>
#include <string>
#include <thread>
#include <vector>

using namespace avgen;
using audio::InputSource;
using audio::SyntheticCapture;
using audio::SyntheticCaptureConfig;

namespace {

constexpr std::uint32_t kRate = 48000;
constexpr std::uint32_t kBlock = 128;

std::vector<InputSource> monoSources(std::uint32_t n) {
    std::vector<InputSource> out;
    for (std::uint32_t c = 0; c < n; ++c) {
        out.push_back({.key = "in" + std::to_string(c + 1), .label = "Input " + std::to_string(c + 1), .channel = c});
    }
    return out;
}

// An AudioInput on a manual synthetic backend; `backend` stays valid while `input` is open.
SyntheticCapture* openSynthetic(audio::AudioInput& input, SyntheticCaptureConfig config = {}) {
    config.realtime = false;
    auto backend = std::make_unique<SyntheticCapture>(config);
    SyntheticCapture* raw = backend.get();
    REQUIRE(input.openBackend(std::move(backend)).has_value());
    return raw;
}

std::vector<float> drain(audio::AnalysisStream& s) {
    std::vector<float> out;
    std::vector<float> buf(4096);
    for (;;) {
        const auto r = s.read(buf);
        if (r.count == 0) {
            break;
        }
        out.insert(out.end(), buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(r.count));
    }
    return out;
}

// Power of `x` at `hz` (a single-bin DFT).
double powerAt(const std::vector<float>& x, double hz) {
    double re = 0.0, im = 0.0;
    const double w = 2.0 * std::numbers::pi * hz / kRate;
    for (std::size_t n = 0; n < x.size(); ++n) {
        re += x[n] * std::cos(w * static_cast<double>(n));
        im += x[n] * std::sin(w * static_cast<double>(n));
    }
    return re * re + im * im;
}

} // namespace

TEST_CASE("multichannel: each of 8 channels reaches its own source and no other", "[multichannel][threading]") {
    audio::AudioInput input;
    input.setSources(monoSources(8));
    SyntheticCaptureConfig cfg;
    cfg.impulses = false;
    SyntheticCapture* device = openSynthetic(input, cfg);
    CHECK(input.channels() == 8);
    CHECK(input.deviceId() == "synthetic:8");
    std::vector<std::vector<float>> got(8);
    for (int step = 0; step < 5; ++step) { // 1 s, drained as it goes (a source's ring holds 341 ms)
        device->pump(75);
        for (std::uint32_t c = 0; c < 8; ++c) {
            REQUIRE(input.sourceStream(c) != nullptr);
            const auto part = drain(*input.sourceStream(c));
            got[c].insert(got[c].end(), part.begin(), part.end());
        }
    }
    CHECK(input.stats().droppedBlocks == 0u);
    for (std::uint32_t c = 0; c < 8; ++c) {
        INFO("source on channel " << c + 1);
        const auto& x = got[c];
        REQUIRE(x.size() == 375u * kBlock);
        // The strongest of the eight test tones is this channel's: nothing swapped, nothing summed in.
        std::uint32_t best = 0;
        double bestPower = -1.0, second = 0.0;
        for (std::uint32_t k = 0; k < 8; ++k) {
            const double p = powerAt(x, SyntheticCaptureConfig::toneHz(k));
            if (p > bestPower) {
                second = std::max(second, bestPower);
                bestPower = p;
                best = k;
            } else {
                second = std::max(second, p);
            }
        }
        CHECK(best == c);
        CHECK(second < bestPower * 1e-6); // the other tones are absent, not merely weaker
        // And sample-exact: the source holds the generator's own samples.
        const double w = 2.0 * std::numbers::pi * SyntheticCaptureConfig::toneHz(c) / kRate;
        CHECK(x[1000] == cfg.amplitude * static_cast<float>(std::sin(w * 1000.0)));
    }
}

TEST_CASE("multichannel: a linked pair is (L + R) / 2 with its side, and the main stream is still every channel's average",
          "[multichannel][threading]") {
    audio::AudioInput input;
    auto sources = monoSources(8);
    sources.push_back({.key = "bus", .label = "Bus 3/4", .channel = 2, .pair = true});
    input.setSources(sources);
    SyntheticCapture* device = openSynthetic(input);
    device->pump(100);
    std::vector<std::vector<float>> mono(8);
    for (std::uint32_t c = 0; c < 8; ++c) {
        mono[c] = drain(*input.sourceStream(c));
    }
    const auto pair = drain(*input.sourceStream(8));
    REQUIRE(pair.size() == 2 * mono[2].size());
    for (std::size_t i = 0; i < mono[2].size(); ++i) {
        REQUIRE(pair[2 * i] == 0.5f * (mono[2][i] + mono[3][i]));
        REQUIRE(pair[2 * i + 1] == 0.5f * (mono[2][i] - mono[3][i]));
    }
    // The main stream, what audio.* analyses, is the average of all eight -- as the stereo path has always made it.
    const auto main = drain(input.analysisStream());
    REQUIRE(main.size() == mono[0].size());
    for (std::size_t i = 0; i < main.size(); ++i) {
        float sum = 0.0f;
        for (std::uint32_t c = 0; c < 8; ++c) {
            sum += mono[c][i];
        }
        REQUIRE(main[i] == sum * (1.0f / 8.0f));
    }
}

TEST_CASE("multichannel: an absent channel gets no stream; removing a source leaves the others' samples contiguous",
          "[multichannel][threading]") {
    audio::AudioInput input;
    auto sources = monoSources(3);
    sources.push_back({.key = "nine", .label = "Input 9", .channel = 8});
    sources.push_back({.key = "edge", .label = "8/9", .channel = 7, .pair = true});
    input.setSources(sources);
    SyntheticCapture* device = openSynthetic(input);
    CHECK(input.sourceStream(3) == nullptr);
    CHECK(input.sourceStream(4) == nullptr); // a pair needs both channels
    device->pump(10);
    audio::AnalysisStream* first = input.sourceStream(0);
    const auto before = drain(*first);
    sources.erase(sources.begin() + 1); // take Input 2 away
    input.setSources(sources);
    CHECK(input.sourceStream(0) == first); // the same stream: its analysis is not reset
    device->pump(10);
    std::vector<float> buf(8192);
    const auto r = first->read(buf);
    CHECK_FALSE(r.discontinuity);
    CHECK(r.count == 10u * kBlock);
    CHECK(input.sourceStream(1) != nullptr); // Input 3, now second
}

TEST_CASE("multichannel: a consumer that falls behind drops whole blocks and the stream says where it resumes",
          "[multichannel][threading]") {
    audio::AudioInput input;
    input.setSources(monoSources(2));
    SyntheticCapture* device = openSynthetic(input);
    device->pump(200); // 25600 frames into a 16384-sample ring: 128 blocks fit, 72 are dropped
    auto stats = input.stats();
    CHECK(stats.droppedBlocks == 2u * 72u);
    CHECK(stats.blocks == 200u);
    auto* s = input.sourceStream(0);
    const auto kept = drain(*s);
    CHECK(kept.size() == 128u * kBlock);
    device->pump(1);
    std::vector<float> buf(1024);
    const auto r = s->read(buf);
    REQUIRE(r.discontinuity);
    CHECK(r.startFrame == 200u * kBlock); // the true frame of the first block after the drop
    CHECK(r.count == kBlock);

    // A gap in the device's own sample time is the same kind of break.
    device->skip(1000);
    device->pump(1);
    CHECK(input.stats().gaps == 1u);
    const auto g = s->read(buf);
    CHECK(g.discontinuity);
}

TEST_CASE("multichannel: the channel analysis finds each channel's clicks on its own source only",
          "[multichannel][threading]") {
    audio::AudioInput input;
    input.setSources(monoSources(8));
    SyntheticCaptureConfig cfg; // clicks every 0.5 s, channel c offset by c * 31.25 ms
    SyntheticCapture* device = openSynthetic(input, cfg);
    std::vector<analysis::ChannelAnalysis::Source> sources;
    for (std::size_t i = 0; i < 8; ++i) {
        sources.push_back({.stream = input.sourceStream(i)});
    }
    analysis::ChannelAnalysis channels(sources, kRate);
    channels.start();
    std::vector<std::vector<double>> onsets(8);
    std::vector<analysis::LiveStemLatch> latches(8);
    const int hops = 3 * kRate / 512; // 3 s
    for (int h = 0; h < hops; ++h) {
        device->pump(4); // one 512-frame hop
        const auto want = static_cast<std::uint64_t>(h + 1) * 8;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (channels.hopsProduced() + 3 * 8 < want && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
        for (std::size_t i = 0; i < 8; ++i) {
            if (channels.acquire(i)) {
                analysis::LiveStemFrame f = channels.latest(i);
                latches[i].apply(f);
                if (f.frame.onset) {
                    onsets[i].push_back(f.frame.timeSeconds);
                }
            }
        }
    }
    channels.stop();
    for (std::uint32_t c = 0; c < 8; ++c) {
        INFO("source " << c + 1 << ": " << onsets[c].size() << " onsets");
        // Each click of this channel (after the analysis warms up) is an onset within 35 ms + a hop of its frame...
        for (std::uint64_t k = 1; device->impulseFrame(c, k) + 4096 < static_cast<std::uint64_t>(hops) * 512; ++k) {
            const double t = static_cast<double>(device->impulseFrame(c, k)) / kRate;
            CHECK(std::any_of(onsets[c].begin(), onsets[c].end(),
                              [&](double o) { return std::abs(o - t) <= 0.035 + 512.0 / kRate; }));
        }
        // ...and every onset is one of this channel's clicks, never a neighbour's (they are 31 ms apart).
        for (double o : onsets[c]) {
            const double period = static_cast<double>(cfg.impulsePeriodFrames) / kRate;
            const double offset = static_cast<double>(c * cfg.impulseStaggerFrames) / kRate;
            const double phase = std::fmod(o - offset + period, period);
            CHECK(std::min(phase, period - phase) <= 0.025);
        }
    }
    CHECK(channels.hopMicros() > 0.0);
}

TEST_CASE("multichannel: through the engine, live sources publish stem.<key>.*; a stall or a disconnect reads inactive",
          "[multichannel][threading][engine]") {
    app::Engine engine(app::EngineMode::Live);
    engine.setInputSources({{.key = "drums", .label = "Drums", .channel = 0},
                            {.key = "", .label = "Bass DI", .channel = 1},
                            {.key = "ghost", .label = "Input 12", .channel = 11}});
    REQUIRE(engine.inputSources().size() == 3);
    CHECK(engine.inputSources()[1].key == "bass_di");
    SyntheticCaptureConfig cfg;
    cfg.realtime = false;
    auto backend = std::make_unique<SyntheticCapture>(cfg);
    SyntheticCapture* device = backend.get();
    REQUIRE(engine.useAudioInputBackend(std::move(backend)).has_value());
    const auto active = *engine.signals().find("stem.drums.active");
    const auto level = *engine.signals().find("stem.drums.level");
    const auto ghost = *engine.signals().find("stem.ghost.active");
    double t = 0.0;
    const auto frame = [&] {
        engine.update(FrameTime{t, 1.0 / 60.0, static_cast<std::uint64_t>(t * 60.0)});
        t += 1.0 / 60.0;
    };
    bool sawActive = false;
    for (int i = 0; i < 120; ++i) {
        device->pump(6); // 768 frames: a little under one 60 Hz frame's worth
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        frame();
        sawActive |= engine.signals().value(active) == 1.0f;
        REQUIRE(engine.signals().value(ghost) == 0.0f); // channel 12 does not exist on an 8-channel device
    }
    CHECK(sawActive);
    CHECK(engine.signals().value(level) > 0.0f);
    CHECK(engine.inputSourceActive(0));
    CHECK_FALSE(engine.inputSourceActive(2));
    // The device stops delivering: within the stale window the source reads inactive, every value 0.
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    frame();
    CHECK(engine.signals().value(active) == 0.0f);
    CHECK(engine.signals().value(level) == 0.0f);
    // Delivering again brings it back; a disconnect takes it away at once.
    for (int i = 0; i < 10; ++i) {
        device->pump(6);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        frame();
    }
    CHECK(engine.signals().value(active) == 1.0f);
    device->disconnect();
    frame();
    CHECK(engine.signals().value(active) == 0.0f);
    engine.stopAudioInput();
    frame();
    CHECK(engine.signals().value(active) == 0.0f);
}
