#include "analysis/analysis_runner.hpp"
#include "audio/analysis_stream.hpp"
#include "support/synth.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <chrono>
#include <cmath>
#include <thread>

using namespace avgen;
using namespace avgen::analysis;
using Catch::Matchers::WithinAbs;

namespace {
constexpr std::uint32_t kRate = 48000;
constexpr std::size_t kChunk = 512;
using clock = std::chrono::steady_clock;

// Writes every sample, waiting for the consumer when the ring is full. Gives up after `budget`.
bool feed(audio::AnalysisStream& stream, std::span<const float> samples, std::chrono::milliseconds budget) {
    const auto deadline = clock::now() + budget;
    std::size_t offset = 0;
    while (offset < samples.size()) {
        const std::size_t take = std::min(kChunk, samples.size() - offset);
        const std::size_t written = stream.write(samples.subspan(offset, take));
        offset += written;
        if (written == 0) {
            if (clock::now() > deadline) {
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    return true;
}

bool waitForFrames(AnalysisRunner& runner, std::uint64_t count, std::chrono::milliseconds budget) {
    const auto deadline = clock::now() + budget;
    while (runner.framesProduced() < count) {
        if (clock::now() > deadline) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}
} // namespace

TEST_CASE("AnalysisRunner analyzes a streamed sine on its own thread", "[analysis][runner]") {
    audio::AnalysisStream stream(1u << 16);
    AnalyzerConfig config;
    config.sampleRate = kRate;
    AnalysisRunner runner(config, stream);
    CHECK_FALSE(runner.running());
    runner.start();
    CHECK(runner.running());

    const auto signal = testsupport::sine(440.0f, kRate, kRate * 2);
    stream.markDiscontinuity(0);
    REQUIRE(feed(stream, signal, std::chrono::seconds(2)));

    const auto expectedFrames = (signal.size() - config.windowSize) / config.hopSize + 1;
    REQUIRE(waitForFrames(runner, expectedFrames, std::chrono::seconds(2)));

    REQUIRE(runner.acquire());
    const auto& latest = runner.latest();
    CHECK_THAT(static_cast<double>(latest.rms), WithinAbs(std::sqrt(0.5), 0.01));
    CHECK(latest.bandCount == 5);
    CHECK(latest.frameIndex == config.windowSize / 2 + (expectedFrames - 1) * config.hopSize);
    CHECK_FALSE(runner.acquire()); // nothing newer

    const auto history = runner.history(16);
    REQUIRE(history.size() == 16);
    for (std::size_t i = 1; i < history.size(); ++i) {
        CHECK(history[i].frameIndex == history[i - 1].frameIndex + config.hopSize);
    }
    CHECK(history.back().frameIndex == latest.frameIndex);
    CHECK(runner.history(10000).size() == expectedFrames);
    CHECK(runner.framesProduced() == expectedFrames);
    CHECK(runner.averageHopMicros() > 0.0);
    WARN("AnalysisRunner average analysis time per hop: " << runner.averageHopMicros() << " us");

    runner.stop();
    CHECK_FALSE(runner.running());
}

TEST_CASE("AnalysisRunner restamps frames after a discontinuity", "[analysis][runner]") {
    audio::AnalysisStream stream(1u << 16);
    AnalyzerConfig config;
    config.sampleRate = kRate;
    AnalysisRunner runner(config, stream);
    runner.start();

    const auto signal = testsupport::sine(440.0f, kRate, config.windowSize);
    stream.markDiscontinuity(100000);
    REQUIRE(feed(stream, signal, std::chrono::seconds(2)));
    REQUIRE(waitForFrames(runner, 1, std::chrono::seconds(2)));
    REQUIRE(runner.acquire());
    CHECK(runner.latest().frameIndex == 100000 + config.windowSize / 2);

    // Seek: the next window starts fresh at the new position, and history is preserved.
    stream.markDiscontinuity(5000);
    REQUIRE(feed(stream, signal, std::chrono::seconds(2)));
    REQUIRE(waitForFrames(runner, 2, std::chrono::seconds(2)));
    REQUIRE(runner.acquire());
    CHECK(runner.latest().frameIndex == 5000 + config.windowSize / 2);
    CHECK(runner.latest().flux == 0.0f); // reset cleared the previous spectrum
    const auto history = runner.history(2);
    REQUIRE(history.size() == 2);
    CHECK(history[0].frameIndex == 100000 + config.windowSize / 2);
    CHECK(history[1].frameIndex == 5000 + config.windowSize / 2);
}

TEST_CASE("AnalysisRunner frames carry tempo and beats for a click track", "[analysis][runner]") {
    audio::AnalysisStream stream(1u << 16);
    AnalyzerConfig config;
    config.sampleRate = kRate;
    AnalysisRunner runner(config, stream);
    runner.start();

    const auto signal = testsupport::clickTrack(120.0f, kRate, kRate * 8);
    stream.markDiscontinuity(0);
    REQUIRE(feed(stream, signal, std::chrono::seconds(4)));
    const auto expectedFrames = (signal.size() - config.windowSize) / config.hopSize + 1;
    REQUIRE(waitForFrames(runner, expectedFrames, std::chrono::seconds(4)));
    REQUIRE(runner.acquire());
    const auto& latest = runner.latest();
    CHECK_THAT(static_cast<double>(latest.tempoBpm), WithinAbs(120.0, 2.0));
    CHECK(latest.tempoConfidence > 0.15f);
    CHECK(latest.beatCount >= 8); // beats emitted from the first estimate (~2 s) onwards
    CHECK(latest.beatPhase >= 0.0f);
    CHECK(latest.beatPhase <= 1.0f);
    const auto history = runner.history(200);
    std::size_t beats = 0;
    for (const auto& f : history) {
        beats += f.beat ? 1 : 0;
    }
    CHECK(beats >= 3); // 200 hops = 2.1 s at 2 beats/s

    // A seek resets the tracker: tempo is unknown until enough new audio has been analyzed.
    stream.markDiscontinuity(0);
    REQUIRE(feed(stream, std::span<const float>(signal.data(), config.windowSize), std::chrono::seconds(2)));
    REQUIRE(waitForFrames(runner, expectedFrames + 1, std::chrono::seconds(2)));
    REQUIRE(runner.acquire());
    CHECK(runner.latest().tempoBpm == 0.0f);
    CHECK(runner.latest().beatCount == 0);
}

TEST_CASE("AnalysisRunner stops cleanly from its destructor", "[analysis][runner]") {
    audio::AnalysisStream stream(1u << 12);
    {
        AnalysisRunner runner(AnalyzerConfig{}, stream);
        runner.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    SUCCEED("destructor joined the analysis thread");
}

TEST_CASE("AnalysisRunner history ring keeps the newest 512 frames after wrapping", "[analysis][runner]") {
    audio::AnalysisStream stream(1u << 16);
    AnalyzerConfig config;
    config.sampleRate = kRate;
    AnalysisRunner runner(config, stream);
    runner.start();

    const auto signal = testsupport::sine(440.0f, kRate, kRate * 6); // 560 frames > ring size
    stream.markDiscontinuity(0);
    REQUIRE(feed(stream, signal, std::chrono::seconds(4)));
    const auto expectedFrames = (signal.size() - config.windowSize) / config.hopSize + 1;
    REQUIRE(expectedFrames > 512);
    REQUIRE(waitForFrames(runner, expectedFrames, std::chrono::seconds(4)));

    const auto history = runner.history(10000);
    REQUIRE(history.size() == 512);
    const auto newest = config.windowSize / 2 + (expectedFrames - 1) * config.hopSize;
    CHECK(history.back().frameIndex == newest);
    CHECK(history.front().frameIndex == newest - 511 * config.hopSize);
    for (std::size_t i = 1; i < history.size(); ++i) {
        CHECK(history[i].frameIndex == history[i - 1].frameIndex + config.hopSize);
    }
    const auto tail = runner.history(3);
    REQUIRE(tail.size() == 3);
    CHECK(tail[2].frameIndex == newest);
    CHECK(tail[0].frameIndex == newest - 2 * config.hopSize);
}
