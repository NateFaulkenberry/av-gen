// stems-multichannel M6 (ADR-1522): the Core Audio capture backend on real Core Audio devices of this machine -- a
// private aggregate of the built-in microphone and BlackHole 2ch (two input streams, the miniaudio #312 shape) and
// BlackHole 2ch itself. SKIPs without BlackHole 2ch (every hosted CI runner): these are local tests, by design.

#include "app/engine.hpp"
#include "audio/audio_input.hpp"
#include "audio/capture_coreaudio.hpp"
#include "core/time.hpp"

#include <catch2/catch_test_macros.hpp>

#if defined(__APPLE__)

#include "support/coreaudio_rig.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>
#include <thread>
#include <vector>

using namespace avgen;

namespace {

const char* const kBlackHole = "BlackHole2ch_UID";
const char* const kMic = "BuiltInMicrophoneDevice";

void requireBlackHole() {
    if (!audio::coreAudioDevicePresent(kBlackHole)) {
        SKIP("BlackHole 2ch is not installed: the Core Audio capture tests need its loopback");
    }
}

std::vector<float> collect(audio::AnalysisStream& s, std::size_t frames, std::size_t perFrame = 1) {
    std::vector<float> out;
    std::vector<float> buf(4096);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (out.size() < frames * perFrame && std::chrono::steady_clock::now() < deadline) {
        const auto r = s.read(buf);
        out.insert(out.end(), buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(r.count));
        if (r.count == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    return out;
}

double powerAt(const std::vector<float>& x, double hz, double rate) {
    double re = 0.0, im = 0.0;
    const double w = 2.0 * std::numbers::pi * hz / rate;
    for (std::size_t n = 0; n < x.size(); ++n) {
        re += x[n] * std::cos(w * static_cast<double>(n));
        im += x[n] * std::sin(w * static_cast<double>(n));
    }
    return re * re + im * im;
}

} // namespace

TEST_CASE("Core Audio capture: every channel of a two-stream device, each where it belongs", "[coreaudio][audio]") {
    requireBlackHole();
    if (!audio::coreAudioDevicePresent(kMic)) {
        SKIP("no built-in microphone to make the aggregate's first stream");
    }
    // The microphone's single channel first, BlackHole's two after it: the shape miniaudio 0.11.25 reads as one channel.
    testsupport::PrivateAggregate aggregate("com.avgen.test.stems.aggregate", {kMic, kBlackHole});
    REQUIRE(aggregate.ok());
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    const auto devices = audio::coreAudioInputDevices();
    const auto it = std::find_if(devices.begin(), devices.end(),
                                 [&](const audio::CoreAudioInputDevice& d) { return d.uid == aggregate.uid(); });
    REQUIRE(it != devices.end());
    CHECK(it->streamChannels == std::vector<std::uint32_t>{1, 2});

    testsupport::TonePlayer tone(kBlackHole, 440.0, 1250.0);
    REQUIRE(tone.playing());
    audio::AudioInput input;
    input.setSources({{.key = "left", .label = "BH L", .channel = 1}, {.key = "right", .label = "BH R", .channel = 2}});
    REQUIRE(input.openBackend(std::make_unique<audio::CoreAudioCapture>(aggregate.uid())).has_value());
    CHECK(input.channels() == 3);
    const double rate = input.sampleRate();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    const auto left = collect(*input.sourceStream(0), 24000);
    const auto right = collect(*input.sourceStream(1), 24000);
    REQUIRE(left.size() >= 24000);
    REQUIRE(right.size() >= 24000);
    CHECK(powerAt(left, 440.0, rate) > 100.0 * powerAt(left, 1250.0, rate));
    CHECK(powerAt(right, 1250.0, rate) > 100.0 * powerAt(right, 440.0, rate));
    CHECK(input.connected());
    CHECK(input.stats().gaps == 0u);
}

TEST_CASE("Core Audio capture: a device that disappears mid-capture reads disconnected at once", "[coreaudio][audio]") {
    requireBlackHole();
    testsupport::PrivateAggregate aggregate("com.avgen.test.stems.unplug", {kBlackHole});
    REQUIRE(aggregate.ok());
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    audio::AudioInput input;
    input.setSources({{.key = "a", .label = "A", .channel = 0}});
    REQUIRE(input.openBackend(std::make_unique<audio::CoreAudioCapture>(aggregate.uid())).has_value());
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    CHECK(input.connected());
    CHECK(input.stats().blocks > 0u);
    const auto destroying = std::chrono::steady_clock::now();
    aggregate.destroy(); // synchronous: the HAL tears the device down (and notifies) before this returns
    const auto destroyed = std::chrono::steady_clock::now();
    const bool seenDuringDestroy = !input.connected();
    const double destroyMs = std::chrono::duration<double, std::milli>(destroyed - destroying).count();
    UNSCOPED_INFO("destroy took " << destroyMs << " ms");
    while (input.connected() && std::chrono::steady_clock::now() - destroyed < std::chrono::seconds(2)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - destroyed).count();
    INFO("disconnect seen " << ms << " ms after the destroy returned (" << (seenDuringDestroy ? "already during it" : "after it") << ")");
    CHECK_FALSE(input.connected());
    // Measured: the destroy call returns at once and the HAL's IsAlive notification follows ~360 ms later (in a
    // bare probe with no capture running, 5 ms). The engine reads the flag every frame, so that is the latency.
    CHECK(ms < 1000.0);
    static_cast<void>(seenDuringDestroy);
    CHECK_FALSE(audio::coreAudioDevicePresent("com.avgen.test.stems.unplug"));
    input.close(); // and closing after the device is gone is safe
}

TEST_CASE("Core Audio capture: on BlackHole 2ch the main stream carries the same samples as the plain miniaudio path",
          "[coreaudio][audio]") {
    requireBlackHole();
    testsupport::TonePlayer tone(kBlackHole, 440.0, 1250.0);
    REQUIRE(tone.playing());
    audio::AudioInput plain;
    REQUIRE(plain.open("BlackHole 2ch").has_value());
    audio::AudioInput hal;
    REQUIRE(hal.openBackend(std::make_unique<audio::CoreAudioCapture>(kBlackHole)).has_value());
    REQUIRE(plain.channels() == 2);
    REQUIRE(hal.channels() == 2);
    const auto a = collect(plain.analysisStream(), 24000);
    const auto b = collect(hal.analysisStream(), 24000);
    REQUIRE(a.size() >= 24000);
    REQUIRE(b.size() >= 24000);
    // The two clients started at different instants: find b's lag against a (the tone repeats, so search one period
    // of the slower beat), then require the overlap to be identical, sample for sample.
    const std::size_t window = 4096;
    std::size_t bestLag = 0;
    double best = 1e30;
    for (std::size_t lag = 0; lag + window < b.size() && lag < 12000; ++lag) {
        double d = 0.0;
        for (std::size_t i = 0; i < 256; ++i) {
            const double e = static_cast<double>(a[8000 + i]) - b[lag + i];
            d += e * e;
        }
        if (d < best) {
            best = d;
            bestLag = lag;
        }
    }
    // Identical, or within float rounding: measured, one sample in 4096 differed, by 1.2e-15, at a point where the two
    // channels cancel -- the two clients' samples are not bit-identical there, which no analysis can see.
    std::size_t equal = 0;
    double worst = 0.0;
    for (std::size_t i = 0; i < window; ++i) {
        equal += a[8000 + i] == b[bestLag + i] ? 1 : 0;
        worst = std::max(worst, std::abs(static_cast<double>(a[8000 + i]) - static_cast<double>(b[bestLag + i])));
    }
    INFO("lag " << bestLag << ", " << equal << " of " << window << " samples identical, worst difference " << worst);
    CHECK(worst < 1e-6);
    CHECK(equal >= window - 8);
}

TEST_CASE("Core Audio capture: the engine takes the Core Audio path for sources and the plain path without; a device that "
          "comes back is reopened",
          "[coreaudio][audio][engine]") {
    requireBlackHole();
    app::Engine engine(app::EngineMode::Live);
    double t = 0.0;
    const auto frame = [&] {
        engine.update(FrameTime{t, 1.0 / 60.0, static_cast<std::uint64_t>(t * 60.0)});
        t += 1.0 / 60.0;
    };
    REQUIRE(engine.useAudioInput("BlackHole 2ch").has_value());
    CHECK(engine.audioInput()->deviceId().empty()); // no sources: miniaudio, exactly as before
    engine.setInputSources({{.key = "l", .label = "L", .channel = 0}, {.key = "r", .label = "R", .channel = 1}});
    CHECK(engine.audioInput()->deviceId() == kBlackHole); // reopened through Core Audio for its sources
    CHECK(engine.audioInput()->sources().size() == 2);
    engine.setInputSources({});
    CHECK(engine.audioInput()->deviceId().empty()); // and back
    engine.stopAudioInput();

    // A device that goes and comes back with the same UID is reopened, and nothing is substituted meanwhile.
    auto aggregate = std::make_unique<testsupport::PrivateAggregate>("com.avgen.test.stems.replug",
                                                                     std::vector<std::string>{kBlackHole});
    REQUIRE(aggregate->ok());
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    engine.setInputSources({{.key = "a", .label = "A", .channel = 0}});
    REQUIRE(engine.useAudioInput("com.avgen.test.stems.replug").has_value());
    const auto active = *engine.signals().find("stem.a.active");
    for (int i = 0; i < 30; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(17));
        frame();
    }
    CHECK(engine.signals().value(active) == 1.0f);
    aggregate.reset(); // unplugged
    for (int i = 0; i < 60; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(17));
        frame();
    }
    CHECK_FALSE(engine.audioInput()->connected());
    CHECK(engine.signals().value(active) == 0.0f);
    CHECK(engine.audioInput()->deviceId() == "com.avgen.test.stems.replug"); // still that device, not another
    aggregate = std::make_unique<testsupport::PrivateAggregate>("com.avgen.test.stems.replug",
                                                                std::vector<std::string>{kBlackHole});
    REQUIRE(aggregate->ok());
    for (int i = 0; i < 120 && !(engine.audioInput()->connected() && engine.signals().value(active) == 1.0f); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(17));
        frame();
    }
    CHECK(engine.audioInput()->connected());
    CHECK(engine.signals().value(active) == 1.0f);
    engine.stopAudioInput();
}

#else

TEST_CASE("Core Audio capture is macOS only", "[coreaudio][audio]") {
    SKIP("no Core Audio on this platform");
}

#endif
