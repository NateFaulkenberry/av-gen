#include "analysis/analysis_track.hpp"
#include "audio/audio_file.hpp"
#include "support/synth.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>

using namespace avgen;
using namespace avgen::analysis;
using Catch::Matchers::WithinAbs;

namespace {
constexpr std::uint32_t kRate = 48000;

audio::AudioFile sineFile(double seconds, std::uint32_t rate = kRate) {
    const auto mono = testsupport::sine(440.0f, rate, static_cast<std::size_t>(seconds * rate));
    return audio::AudioFile::fromInterleaved(mono, 1, rate);
}
} // namespace

TEST_CASE("AnalysisTrack analyses a whole file with monotonic timestamps", "[analysis][track]") {
    const auto file = sineFile(2.0);
    const auto track = AnalysisTrack::analyze(file, AnalyzerConfig{});
    REQUIRE_FALSE(track.empty());
    const auto& frames = track.frames();
    const auto& cfg = track.config();
    CHECK(cfg.sampleRate == kRate);
    CHECK(frames.size() == (file.frameCount() - cfg.windowSize) / cfg.hopSize + 1);
    CHECK(frames.front().frameIndex == cfg.windowSize / 2);
    for (std::size_t i = 1; i < frames.size(); ++i) {
        CHECK(frames[i].timeSeconds > frames[i - 1].timeSeconds);
        CHECK(frames[i].frameIndex == frames[i - 1].frameIndex + cfg.hopSize);
    }
    for (const auto& f : frames) {
        CHECK_THAT(static_cast<double>(f.rms), WithinAbs(std::sqrt(0.5), 0.01));
    }
}

TEST_CASE("AnalysisTrack overrides the configured sample rate with the file's", "[analysis][track]") {
    AnalyzerConfig config;
    config.sampleRate = 8000;
    const auto file = sineFile(1.0, 44100);
    const auto track = AnalysisTrack::analyze(file, config);
    REQUIRE_FALSE(track.empty());
    CHECK(track.config().sampleRate == 44100);
    CHECK_THAT(track.frames().front().timeSeconds,
               WithinAbs(static_cast<double>(config.windowSize / 2) / 44100.0, 1e-12));
}

TEST_CASE("AnalysisTrack::at returns the frame nearest a time and clamps", "[analysis][track]") {
    const auto file = sineFile(2.0);
    const auto track = AnalysisTrack::analyze(file, AnalyzerConfig{});
    REQUIRE_FALSE(track.empty());
    const auto& frames = track.frames();

    const auto& mid = track.at(0.5);
    double best = 1e9;
    for (const auto& f : frames) {
        best = std::min(best, std::fabs(f.timeSeconds - 0.5));
    }
    CHECK_THAT(std::fabs(mid.timeSeconds - 0.5), WithinAbs(best, 1e-12));
    CHECK(std::fabs(mid.timeSeconds - 0.5) <=
          static_cast<double>(track.config().hopSize) / (2.0 * kRate) + 1e-9);

    CHECK(&track.at(-1.0) == &frames.front());
    CHECK(&track.at(100.0) == &frames.back());
    CHECK(&track.at(frames[10].timeSeconds) == &frames[10]);
    // Just past the midpoint between two frames rounds to the later one.
    const double midpoint = 0.5 * (frames[10].timeSeconds + frames[11].timeSeconds);
    CHECK(&track.at(midpoint + 1e-6) == &frames[11]);
    CHECK(&track.at(midpoint - 1e-6) == &frames[10]);
}

TEST_CASE("AnalysisTrack of a file shorter than one window is empty", "[analysis][track]") {
    const auto file = audio::AudioFile::fromInterleaved(testsupport::sine(440.0f, kRate, 100), 1, kRate);
    const auto track = AnalysisTrack::analyze(file, AnalyzerConfig{});
    CHECK(track.empty());
}
