#include "analysis/analyzer.hpp"
#include "support/synth.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <numeric>
#include <string>
#include <vector>

using namespace avgen;
using namespace avgen::analysis;
using Catch::Matchers::WithinAbs;

namespace {

constexpr std::uint32_t kRate = 48000;

enum Band : std::size_t { Bass = 0, LowMid = 1, Mid = 2, HighMid = 3, Treble = 4 };

AnalyzerConfig defaultConfig() {
    AnalyzerConfig config;
    config.sampleRate = kRate;
    return config;
}

// Pushes `samples` in chunks of `chunk` samples and returns every frame produced.
std::vector<AnalysisFrame> analyze(const std::vector<float>& samples, std::size_t chunk,
                                   const AnalyzerConfig& config = defaultConfig()) {
    Analyzer analyzer(config);
    std::vector<AnalysisFrame> frames;
    AnalysisFrame frame;
    for (std::size_t offset = 0; offset < samples.size(); offset += chunk) {
        const std::size_t take = std::min(chunk, samples.size() - offset);
        analyzer.push(std::span<const float>(samples.data() + offset, take));
        while (analyzer.pop(frame)) {
            frames.push_back(frame);
        }
    }
    return frames;
}

bool bitsEqual(float a, float b) {
    return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
}
bool bitsEqual(double a, double b) {
    return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
}

bool bitsEqual(const std::vector<float>& a, const std::vector<float>& b) {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [](float x, float y) { return bitsEqual(x, y); });
}

bool framesIdentical(const AnalysisFrame& a, const AnalysisFrame& b) {
    bool same = a.frameIndex == b.frameIndex && bitsEqual(a.timeSeconds, b.timeSeconds) &&
                bitsEqual(a.rms, b.rms) && bitsEqual(a.peak, b.peak) && a.bandCount == b.bandCount &&
                bitsEqual(a.centroidHz, b.centroidHz) && bitsEqual(a.centroidNorm, b.centroidNorm) &&
                bitsEqual(a.flux, b.flux) && bitsEqual(a.onsetStrength, b.onsetStrength) &&
                a.onset == b.onset;
    for (std::size_t i = 0; i < kMaxBands; ++i) {
        same = same && bitsEqual(a.bandsRaw[i], b.bandsRaw[i]) && bitsEqual(a.bands[i], b.bands[i]);
    }
    return same && bitsEqual(a.magnitude, b.magnitude) && bitsEqual(a.spectrum, b.spectrum);
}

bool allFinite(const AnalysisFrame& f) {
    const bool scalars = std::isfinite(f.rms) && std::isfinite(f.peak) && std::isfinite(f.centroidHz) &&
                         std::isfinite(f.centroidNorm) && std::isfinite(f.flux) &&
                         std::isfinite(f.onsetStrength);
    const auto finite = [](float v) { return std::isfinite(v); };
    return scalars && std::all_of(f.bands.begin(), f.bands.end(), finite) &&
           std::all_of(f.bandsRaw.begin(), f.bandsRaw.end(), finite) &&
           std::all_of(f.magnitude.begin(), f.magnitude.end(), finite) &&
           std::all_of(f.spectrum.begin(), f.spectrum.end(), finite);
}

std::size_t dominantBand(const AnalysisFrame& f) {
    return static_cast<std::size_t>(std::distance(
        f.bandsRaw.begin(), std::max_element(f.bandsRaw.begin(), f.bandsRaw.begin() + f.bandCount)));
}

std::vector<std::uint64_t> onsetFrames(const std::vector<AnalysisFrame>& frames) {
    std::vector<std::uint64_t> out;
    for (const auto& f : frames) {
        if (f.onset) {
            out.push_back(f.frameIndex);
        }
    }
    return out;
}

} // namespace

TEST_CASE("AnalyzerConfig default bands are the five documented ranges", "[analysis][analyzer]") {
    const auto bands = AnalyzerConfig::defaultBands();
    REQUIRE(bands.size() == 5);
    CHECK(bands[Bass].name == "bass");
    CHECK(bands[Bass].lowHz == 20.0f);
    CHECK(bands[Bass].highHz == 150.0f);
    CHECK(bands[LowMid].name == "lowMid");
    CHECK(bands[LowMid].highHz == 400.0f);
    CHECK(bands[Mid].name == "mid");
    CHECK(bands[Mid].highHz == 2000.0f);
    CHECK(bands[HighMid].name == "highMid");
    CHECK(bands[HighMid].highHz == 6000.0f);
    CHECK(bands[Treble].name == "treble");
    CHECK(bands[Treble].lowHz == 6000.0f);
    CHECK(bands[Treble].highHz == 16000.0f);
    for (std::size_t i = 1; i < bands.size(); ++i) {
        CHECK(bands[i].lowHz == bands[i - 1].highHz);
    }
}

TEST_CASE("Analyzer binHz and binCount follow the window size", "[analysis][analyzer]") {
    Analyzer analyzer(defaultConfig());
    CHECK(analyzer.binCount() == 1025);
    CHECK(analyzer.binHz(0) == 0.0f);
    CHECK_THAT(static_cast<double>(analyzer.binHz(1)), WithinAbs(48000.0 / 2048.0, 1e-6));
    CHECK_THAT(static_cast<double>(analyzer.binHz(1024)), WithinAbs(24000.0, 1e-3));
}

TEST_CASE("Analyzer produces no frame until a full window is available", "[analysis][analyzer]") {
    Analyzer analyzer(defaultConfig());
    const auto signal = testsupport::sine(440.0f, kRate, 2048 + 512 * 3);
    AnalysisFrame frame;
    analyzer.push(std::span<const float>(signal.data(), 2047));
    CHECK(analyzer.pendingFrames() == 0);
    CHECK_FALSE(analyzer.pop(frame));
    analyzer.push(std::span<const float>(signal.data() + 2047, 1));
    CHECK(analyzer.pendingFrames() == 1);
    analyzer.push(std::span<const float>(signal.data() + 2048, 511));
    CHECK(analyzer.pendingFrames() == 1);
    analyzer.push(std::span<const float>(signal.data() + 2559, 1));
    CHECK(analyzer.pendingFrames() == 2);
    REQUIRE(analyzer.pop(frame));
    CHECK(frame.frameIndex == 1024);
    REQUIRE(analyzer.pop(frame));
    CHECK(frame.frameIndex == 1024 + 512);
    CHECK_FALSE(analyzer.pop(frame));
}

TEST_CASE("Analyzer silence yields exact zeros and never an onset", "[analysis][analyzer]") {
    const auto frames = analyze(testsupport::silence(kRate * 2), 4096);
    REQUIRE(frames.size() > 100);
    for (const auto& f : frames) {
        CHECK(allFinite(f));
        CHECK(f.rms == 0.0f);
        CHECK(f.peak == 0.0f);
        CHECK(f.bandCount == 5);
        for (std::size_t b = 0; b < f.bandCount; ++b) {
            CHECK(f.bandsRaw[b] == 0.0f);
            CHECK(f.bands[b] == 0.0f);
        }
        CHECK(f.centroidHz == 0.0f);
        CHECK(f.centroidNorm == 0.0f);
        CHECK(f.flux == 0.0f);
        CHECK(f.onsetStrength == 0.0f);
        CHECK_FALSE(f.onset);
        CHECK(std::all_of(f.magnitude.begin(), f.magnitude.end(), [](float m) { return m == 0.0f; }));
        CHECK(std::all_of(f.spectrum.begin(), f.spectrum.end(), [](float s) { return s == 0.0f; }));
    }
}

TEST_CASE("Analyzer 440 Hz full-scale sine: level, mid band, centroid", "[analysis][analyzer]") {
    const auto frames = analyze(testsupport::sine(440.0f, kRate, kRate * 2), 4096);
    REQUIRE(frames.size() > 100);
    const auto expectedBin = static_cast<std::size_t>(std::lround(440.0 * 2048.0 / kRate));
    for (const auto& f : frames) {
        CHECK(allFinite(f));
        CHECK_THAT(static_cast<double>(f.rms), WithinAbs(std::sqrt(0.5), 0.005));
        CHECK_THAT(static_cast<double>(f.peak), WithinAbs(1.0, 0.001));
        CHECK(f.frameIndex % 512 == 0);

        const auto peakBin = static_cast<std::size_t>(
            std::distance(f.magnitude.begin(), std::max_element(f.magnitude.begin(), f.magnitude.end())));
        CHECK(peakBin == expectedBin);
        CHECK_THAT(static_cast<double>(f.magnitude[peakBin]), WithinAbs(1.0, 0.15));
        CHECK(f.spectrum[peakBin] > 0.95f);

        CHECK(f.bandsRaw[Mid] > 0.8f);
        CHECK(f.bandsRaw[Mid] <= 1.0f);
        CHECK(dominantBand(f) == Mid);
        CHECK(f.bandsRaw[Bass] < 0.05f);
        CHECK(f.bandsRaw[LowMid] < 0.2f);
        CHECK(f.bandsRaw[HighMid] < 0.05f);
        CHECK(f.bandsRaw[Treble] < 0.05f);
        CHECK_THAT(static_cast<double>(f.bands[Mid]), WithinAbs(1.0, 1e-4));

        CHECK_THAT(static_cast<double>(f.centroidHz), WithinAbs(440.0, 60.0));
        CHECK_THAT(static_cast<double>(f.centroidNorm),
                   WithinAbs(std::log(440.0 / 20.0) / std::log(24000.0 / 20.0), 0.03));
    }
    // Stationary tone: the only onset (if any) is at the very beginning.
    for (std::size_t i = 2; i < frames.size(); ++i) {
        CHECK_FALSE(frames[i].onset);
        CHECK(frames[i].flux < 0.05f);
    }
}

TEST_CASE("Analyzer band normalisation tracks the running maximum", "[analysis][analyzer]") {
    // Loud then quiet: the quiet section reads below 1 until the running peak has decayed.
    AnalyzerConfig config = defaultConfig();
    config.normalizationDecaySeconds = 0.5f;
    auto loud = testsupport::sine(1000.0f, kRate, kRate);
    const auto quiet = testsupport::sine(1000.0f, kRate, kRate * 3, 0.1f);
    loud.insert(loud.end(), quiet.begin(), quiet.end());
    const auto frames = analyze(loud, 4096, config);
    REQUIRE(frames.size() > 300);
    const auto at = [&](double seconds) {
        return *std::min_element(frames.begin(), frames.end(), [seconds](const auto& a, const auto& b) {
            return std::fabs(a.timeSeconds - seconds) < std::fabs(b.timeSeconds - seconds);
        });
    };
    CHECK_THAT(static_cast<double>(at(0.5).bands[Mid]), WithinAbs(1.0, 1e-4));
    const auto justAfter = at(1.1);
    CHECK(justAfter.bands[Mid] < 0.2f);
    CHECK(justAfter.bands[Mid] > 0.09f);
    const auto later = at(3.9);
    CHECK(later.bands[Mid] > justAfter.bands[Mid]);
    CHECK_THAT(static_cast<double>(later.bands[Mid]), WithinAbs(1.0, 0.05));
}

TEST_CASE("Analyzer 60 Hz sine is bass dominant", "[analysis][analyzer]") {
    const auto frames = analyze(testsupport::sine(60.0f, kRate, kRate), 1000);
    REQUIRE_FALSE(frames.empty());
    for (const auto& f : frames) {
        CHECK(dominantBand(f) == Bass);
        CHECK(f.bandsRaw[Bass] > 0.8f);
        CHECK(f.bandsRaw[Mid] < 0.05f);
        CHECK(f.centroidHz < 150.0f);
        CHECK(f.centroidNorm < 0.3f);
    }
}

TEST_CASE("Analyzer white noise has a high centroid and a flat spectrum", "[analysis][analyzer]") {
    const auto frames = analyze(testsupport::whiteNoise(kRate * 2, 1.0f, 3), 4096);
    REQUIRE(frames.size() > 100);
    const std::size_t bins = frames.front().spectrum.size();
    std::vector<double> mean(bins, 0.0);
    for (const auto& f : frames) {
        CHECK(allFinite(f));
        CHECK(f.centroidNorm > 0.5f);
        CHECK(f.centroidNorm < 0.95f);
        CHECK_THAT(static_cast<double>(f.centroidHz), WithinAbs(12000.0, 1500.0));
        for (std::size_t k = 0; k < bins; ++k) {
            mean[k] += static_cast<double>(f.spectrum[k]) / static_cast<double>(frames.size());
        }
    }
    // Averaged over frames, every quarter of the spectrum sits at the same level.
    const auto quarter = [&](std::size_t q) {
        const std::size_t lo = 1 + q * (bins - 2) / 4;
        const std::size_t hi = 1 + (q + 1) * (bins - 2) / 4;
        return std::accumulate(mean.begin() + static_cast<std::ptrdiff_t>(lo),
                               mean.begin() + static_cast<std::ptrdiff_t>(hi), 0.0) /
               static_cast<double>(hi - lo);
    };
    const double reference = quarter(0);
    CHECK(reference > 0.2);
    CHECK(reference < 0.8);
    for (std::size_t q = 1; q < 4; ++q) {
        CHECK_THAT(quarter(q), WithinAbs(reference, 0.03));
    }
}

TEST_CASE("Analyzer impulse train produces one onset per impulse", "[analysis][analyzer]") {
    constexpr std::size_t period = 12000;
    constexpr std::size_t length = kRate * 4;
    const auto config = defaultConfig();
    const auto frames = analyze(testsupport::impulseTrain(length, period), 4096, config);
    REQUIRE_FALSE(frames.empty());
    const auto onsets = onsetFrames(frames);

    // Impulse 0 sits at sample 0 where the Hann window is zero, so it is invisible; every later
    // impulse must be detected exactly once, in the first window that contains it: the onset's
    // window centre lies at most windowSize/2 before the impulse and never after it.
    std::vector<std::size_t> impulses;
    for (std::size_t s = period; s + config.windowSize / 2 <= length; s += period) {
        impulses.push_back(s);
    }
    CHECK(onsets.size() == impulses.size());
    for (const std::size_t impulse : impulses) {
        std::size_t hits = 0;
        for (const auto onset : onsets) {
            const auto centre = static_cast<std::int64_t>(onset);
            const auto lead = static_cast<std::int64_t>(impulse) - centre;
            if (lead >= 0 && lead < static_cast<std::int64_t>(config.windowSize / 2)) {
                ++hits;
            }
        }
        INFO("impulse at " << impulse);
        CHECK(hits == 1);
    }
    // Every onset is attributable to some impulse (no spurious detections).
    for (const auto onset : onsets) {
        const bool attributable = std::any_of(impulses.begin(), impulses.end(), [&](std::size_t impulse) {
            const auto lead = static_cast<std::int64_t>(impulse) - static_cast<std::int64_t>(onset);
            return lead >= 0 && lead < static_cast<std::int64_t>(config.windowSize / 2);
        });
        CHECK(attributable);
    }
    // Flux is quiet between impulses.
    for (const auto& f : frames) {
        const bool nearImpulse = std::any_of(impulses.begin(), impulses.end(), [&](std::size_t impulse) {
            return impulse + config.windowSize / 2 > f.frameIndex &&
                   impulse < f.frameIndex + config.windowSize / 2;
        });
        if (!nearImpulse) {
            CHECK(f.flux == 0.0f);
        }
    }
}

TEST_CASE("Analyzer 120 BPM click track onset count", "[analysis][analyzer]") {
    const auto frames = analyze(testsupport::clickTrack(120.0f, kRate, kRate * 4), 4096);
    const auto onsets = onsetFrames(frames);
    // 4 s at 120 BPM: clicks at 0, 0.5, ..., 3.5 s = 8.
    CHECK(onsets.size() >= 7);
    CHECK(onsets.size() <= 9);
    for (std::size_t i = 1; i < onsets.size(); ++i) {
        // Each onset pair is one beat (0.5 s = 24000 frames) apart, within a window.
        const auto gap = onsets[i] - onsets[i - 1];
        CHECK(gap > 24000 - 2048);
        CHECK(gap < 24000 + 2048);
    }
}

TEST_CASE("Analyzer output is invariant to input chunking", "[analysis][analyzer]") {
    auto signal = testsupport::mix(testsupport::clickTrack(120.0f, kRate, kRate * 2, 256, 0.5f),
                                   testsupport::sine(220.0f, kRate, kRate * 2, 0.4f));
    const auto reference = analyze(signal, 4096);
    REQUIRE(reference.size() > 100);
    for (const std::size_t chunk :
         {std::size_t{1}, std::size_t{100}, std::size_t{4096}, std::size_t{96000}}) {
        const auto frames = analyze(signal, chunk);
        INFO("chunk size " << chunk);
        REQUIRE(frames.size() == reference.size());
        for (std::size_t i = 0; i < frames.size(); ++i) {
            INFO("frame " << i);
            CHECK(framesIdentical(frames[i], reference[i]));
        }
    }
    CHECK(std::any_of(reference.begin(), reference.end(), [](const auto& f) { return f.onset; }));
}

TEST_CASE("Analyzer is deterministic across instances", "[analysis][analyzer]") {
    const auto signal = testsupport::whiteNoise(kRate, 0.7f, 11);
    Analyzer a(defaultConfig());
    Analyzer b(defaultConfig());
    a.push(signal);
    b.push(signal);
    REQUIRE(a.pendingFrames() == b.pendingFrames());
    AnalysisFrame fa;
    AnalysisFrame fb;
    std::size_t count = 0;
    while (a.pop(fa)) {
        REQUIRE(b.pop(fb));
        CHECK(framesIdentical(fa, fb));
        ++count;
    }
    CHECK_FALSE(b.pop(fb));
    CHECK(count > 0);
}

TEST_CASE("Analyzer reset restamps frames and clears history", "[analysis][analyzer]") {
    const auto config = defaultConfig();
    Analyzer analyzer(config);
    const auto signal = testsupport::sine(440.0f, kRate, config.windowSize + config.hopSize);
    analyzer.push(signal);
    CHECK(analyzer.pendingFrames() == 2);

    analyzer.reset(123456);
    CHECK(analyzer.pendingFrames() == 0);
    analyzer.push(signal);
    AnalysisFrame frame;
    REQUIRE(analyzer.pop(frame));
    CHECK(frame.frameIndex == 123456 + config.windowSize / 2);
    CHECK_THAT(frame.timeSeconds,
               WithinAbs(static_cast<double>(123456 + config.windowSize / 2) / kRate, 1e-12));
    CHECK(frame.flux == 0.0f); // previous spectrum cleared: first frame after reset has no flux
    CHECK_THAT(static_cast<double>(frame.bands[Mid]), WithinAbs(1.0, 1e-4));
    REQUIRE(analyzer.pop(frame));
    CHECK(frame.frameIndex == 123456 + config.windowSize / 2 + config.hopSize);

    // A reset mid-window discards the partial window.
    analyzer.push(std::span<const float>(signal.data(), 1000));
    analyzer.reset(7);
    analyzer.push(std::span<const float>(signal.data(), config.windowSize - 1));
    CHECK(analyzer.pendingFrames() == 0);
    analyzer.push(std::span<const float>(signal.data(), 1));
    REQUIRE(analyzer.pop(frame));
    CHECK(frame.frameIndex == 7 + config.windowSize / 2);

    // Frames after a reset equal frames from a fresh analyzer over the same samples.
    const auto fresh = analyze(signal, 4096);
    analyzer.reset(0);
    analyzer.push(signal);
    for (const auto& expected : fresh) {
        REQUIRE(analyzer.pop(frame));
        CHECK(framesIdentical(frame, expected));
    }
}

TEST_CASE("Analyzer sanitises degenerate configuration", "[analysis][analyzer]") {
    AnalyzerConfig config = defaultConfig();
    config.hopSize = 4096; // larger than the window
    Analyzer analyzer(config);
    CHECK(analyzer.config().hopSize == config.windowSize);
    config.hopSize = 0;
    CHECK(Analyzer(config).config().hopSize == 1);
}
