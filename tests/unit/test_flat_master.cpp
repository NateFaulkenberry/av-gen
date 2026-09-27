// Musical features that survive a flat master (ADR-897): long-term band levels that are not
// auto-gained, a stereo width, and an energy composite that follows the arrangement rather than the
// level -- each with the control that shows what the old signal did instead.

#include "analysis/analysis_track.hpp"
#include "analysis/analyzer.hpp"
#include "analysis/span_profile.hpp"
#include "audio/audio_file.hpp"
#include "core/rng.hpp"
#include "support/groove.hpp"
#include "support/synth.hpp"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <vector>

using namespace avgen;

namespace {

constexpr std::uint32_t kRate = 48000;

std::vector<analysis::AnalysisFrame> analyse(const std::vector<float>& mono, const std::vector<float>& side = {}) {
    analysis::AnalyzerConfig config;
    config.sampleRate = kRate;
    analysis::Analyzer a(config);
    std::vector<analysis::AnalysisFrame> out;
    const std::size_t hop = config.hopSize;
    analysis::AnalysisFrame f;
    for (std::size_t i = 0; i < mono.size(); i += hop) {
        const std::size_t n = std::min(hop, mono.size() - i);
        a.push(std::span<const float>(mono).subspan(i, n),
               side.empty() ? std::span<const float>{} : std::span<const float>(side).subspan(i, n));
        while (a.pop(f)) {
            out.push_back(std::move(f));
        }
    }
    return out;
}

const analysis::AnalysisFrame& at(const std::vector<analysis::AnalysisFrame>& frames, double seconds) {
    std::size_t best = 0;
    for (std::size_t i = 0; i < frames.size(); ++i) {
        if (std::fabs(frames[i].timeSeconds - seconds) < std::fabs(frames[best].timeSeconds - seconds)) {
            best = i;
        }
    }
    return frames[best];
}

} // namespace

TEST_CASE("A long-term band level stays down when the band does; the auto-gained one does not",
          "[analysis][flatmaster]") {
    // 8 s of a 100 Hz tone, the last 4 s 6 dB quieter.
    auto tone = testsupport::sine(100.0f, kRate, 8 * kRate, 0.5f);
    for (std::size_t i = 4 * kRate; i < tone.size(); ++i) {
        tone[i] *= 0.5f;
    }
    const auto frames = analyse(tone);
    const analysis::AnalysisFrame& loud = at(frames, 3.5);
    const analysis::AnalysisFrame& quiet = at(frames, 7.5);
    // The fixed scale: 60 dB wide, so 6 dB is 0.1 -- and it holds 3.5 s after the change.
    INFO("level " << loud.bandLevels[0] << " -> " << quiet.bandLevels[0] << "; auto-gained " << loud.bands[0]
                  << " -> " << quiet.bands[0]);
    CHECK(std::fabs((loud.bandLevels[0] - quiet.bandLevels[0]) - 0.1f) < 0.01f);
    // A full-scale-ish sine sits near the top of the scale, not at an arbitrary normalisation.
    CHECK(std::fabs(loud.bandLevels[0] - (1.0f + static_cast<float>(20.0 * std::log10(0.5)) / 60.0f)) < 0.02f);
    // The control: the auto-gained band has renormalised to full scale again.
    CHECK(quiet.bands[0] > 0.9f);
    CHECK(loud.bands[0] > 0.9f);
}

TEST_CASE("The energy composite follows the arrangement, not the master's level", "[analysis][flatmaster]") {
    // A dark passage (a low tone) then a bright busy one (the tone plus hats every 125 ms), both
    // normalised to the same RMS -- a limited master. Then the whole thing 12 dB down.
    const std::size_t n = 12 * kRate;
    auto dark = testsupport::sine(110.0f, kRate, n, 0.5f);
    std::vector<float> mono(n, 0.0f);
    Rng rng(4);
    for (std::size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / kRate;
        float v = dark[i];
        if (t >= 6.0) {
            const double sinceHat = std::fmod(t, 0.125);
            if (sinceHat < 0.03) {
                v = 0.6f * dark[i] + 0.5f * static_cast<float>(std::exp(-sinceHat * 80.0)) * (rng.nextFloat() * 2.0f - 1.0f);
            } else {
                v = 0.6f * dark[i];
            }
        }
        mono[i] = v;
    }
    const auto frames = analyse(mono);
    const analysis::AnalysisFrame& a = at(frames, 5.5);
    const analysis::AnalysisFrame& b = at(frames, 11.5);
    INFO("energy " << a.energy << " -> " << b.energy << ", rms " << a.rms << " -> " << b.rms);
    CHECK(b.energy > a.energy + 0.15f);

    // 12 dB down: the composite moves by almost nothing; the RMS -- the old "energy" -- by 4x.
    std::vector<float> quiet(mono);
    for (float& v : quiet) {
        v *= 0.25f;
    }
    const auto low = analyse(quiet);
    CHECK(std::fabs(at(low, 11.5).energy - b.energy) < 0.03f);
    CHECK(std::fabs(at(low, 5.5).energy - a.energy) < 0.03f);
    CHECK(at(low, 11.5).rms < 0.3f * b.rms);
    // Silence is zero, not a normalised noise floor.
    const auto silent = analyse(testsupport::silence(3 * kRate));
    CHECK(silent.back().energy == 0.0f);
}

TEST_CASE("Stereo width is measured from the side channel, and unknown without one", "[analysis][flatmaster]") {
    const std::size_t n = 2 * kRate;
    const auto left = testsupport::whiteNoise(n, 0.3f, 1);
    const auto right = testsupport::whiteNoise(n, 0.3f, 2);
    std::vector<float> mono(n), side(n), zero(n, 0.0f);
    for (std::size_t i = 0; i < n; ++i) {
        mono[i] = 0.5f * (left[i] + right[i]);
        side[i] = 0.5f * (left[i] - right[i]);
    }
    const auto wide = analyse(mono, side);
    CHECK(wide.back().stereo);
    CHECK(std::fabs(wide.back().width - 1.0f) < 0.1f); // uncorrelated channels at equal level
    const auto centred = analyse(mono, zero);
    CHECK(centred.back().stereo);
    CHECK(centred.back().width < 0.01f);
    const auto mono_only = analyse(mono);
    CHECK_FALSE(mono_only.back().stereo);
}

TEST_CASE("A span profile measures the level-free features over a stretch of the track",
          "[analysis][flatmaster]") {
    // Two bars with no kick in the middle of sixteen -- a pull-back. (Eight kickless bars would also
    // throw the beat tracker, which is not what this measures.)
    testsupport::GrooveSpec spec;
    spec.bars = 16;
    spec.kicklessBars = {8, 9};
    const testsupport::Groove g = testsupport::makeGroove(spec);
    const analysis::AnalysisTrack track = analysis::AnalysisTrack::analyze(g.file, analysis::AnalyzerConfig{});
    const analysis::SpanProfile first = analysis::profileSpan(track, g.truth.downbeats[0], g.truth.downbeats[8]);
    // The pull-back measured inside its bar lines: a kick's onset is stamped on its attack frame, which
    // can sit a few milliseconds before the bar line the kick is on.
    const analysis::SpanProfile second =
        analysis::profileSpan(track, g.truth.downbeats[8] + 0.05, g.truth.downbeats[10] - 0.05);
    INFO("kicks/s " << first.kickRate << " -> " << second.kickRate << ", onsets/s " << first.onsetRate << " -> "
                    << second.onsetRate);
    CHECK(first.measured());
    CHECK(std::fabs(first.kickRate - 2.0f) < 0.2f); // 120 BPM, a kick a beat
    CHECK(second.kickRate == 0.0f);
    CHECK(first.onsetRate > second.onsetRate);
    CHECK(first.brightnessHz > 0.0f);
    CHECK(first.bandCount == 5);
    CHECK(first.stereo);
    // A span outside the track measures nothing rather than borrowing a neighbour's numbers.
    CHECK_FALSE(analysis::profileSpan(track, 1000.0, 1010.0).measured());
    CHECK_FALSE(analysis::profileSpan(track, 5.0, 5.0).measured());
    // And it survives JSON exactly as a plan writes it.
    const analysis::SpanProfile back = analysis::spanProfileFromJson(analysis::spanProfileToJson(first));
    CHECK(back.energy == first.energy);
    CHECK(back.kickRate == first.kickRate);
    CHECK(back.bandDb == first.bandDb);
    CHECK(back.width == first.width);
}
