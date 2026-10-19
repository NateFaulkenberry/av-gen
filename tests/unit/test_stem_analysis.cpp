// stems-multichannel M2: per-stem analysis on the generated fixture (tools/make_stem_fixture.py). Every assertion is
// a property with a stated tolerance, never a golden float.

#include "analysis/stem_analysis.hpp"
#include "audio/audio_file.hpp"
#include "support/stem_fixture.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <random>
#include <string>
#include <vector>

using namespace avgen;
using analysis::StemFrame;
using analysis::StemTrack;

namespace {

// An onset is reported on the hop whose window first shows the attack, so it can precede the event by up to half a
// window (21.3 ms) and follow it by a hop (10.7 ms). 35 ms covers both with a margin.
constexpr double kOnsetTolerance = 0.035;

StemTrack analyzeFile(const testsupport::StemFixture& fx, const std::string& name, bool hits = false) {
    auto file = audio::AudioFile::load(fx.file(name));
    REQUIRE(file.has_value());
    analysis::StemAnalysisConfig config;
    config.hits = hits;
    return StemTrack::analyze(*file, config);
}

std::vector<double> onsetTimes(const StemTrack& t, bool StemFrame::*flag = &StemFrame::onset) {
    std::vector<double> out;
    for (const StemFrame& f : t.frames()) {
        if (f.*flag) {
            out.push_back(f.timeSeconds);
        }
    }
    return out;
}

bool near(const std::vector<double>& times, double t, double tolerance) {
    return std::any_of(times.begin(), times.end(), [&](double x) { return std::abs(x - t) <= tolerance; });
}

const StemFrame& frameAt(const StemTrack& t, double seconds) {
    const auto& f = t.frames();
    const auto it = std::lower_bound(f.begin(), f.end(), seconds,
                                     [](const StemFrame& a, double s) { return a.timeSeconds < s; });
    return it == f.end() ? f.back() : *it;
}

} // namespace

TEST_CASE("stem analysis: every kick of the drum stem is an onset, and the drum gap has none", "[stems][analysis]") {
    const auto& fx = testsupport::requireStemFixture();
    const StemTrack drums = analyzeFile(fx, "drums.wav");
    const auto onsets = onsetTimes(drums);
    std::size_t found = 0;
    const auto kicks = fx.times("drums", "kicks");
    for (double k : kicks) {
        if (k < 0.1) {
            continue; // the very first hop has no previous spectrum to rise from
        }
        found += near(onsets, k, kOnsetTolerance) ? 1 : 0;
    }
    CHECK(found == kicks.size() - 1);
    // 8.0-10.0 s is silent on purpose; the last hat before it ends by 7.82 s and the kick at 7.5 s decays by 7.8 s.
    for (double t : onsets) {
        CHECK_FALSE((t > 7.9 && t < 9.97));
    }
    CHECK(drums.sampleRate() == 48000);
    CHECK(drums.durationSeconds() == Catch::Approx(20.0));
}

TEST_CASE("stem analysis: a stem's events are its own -- bass onsets are bass notes, synth onsets are synth notes",
          "[stems][analysis]") {
    const auto& fx = testsupport::requireStemFixture();
    const StemTrack bass = analyzeFile(fx, "bass.wav");
    const StemTrack synth = analyzeFile(fx, "synth.wav");
    const auto bassStarts = fx.times("bass", "notes");
    const auto bassOnsets = onsetTimes(bass);
    for (double s : bassStarts) {
        if (s >= 0.1) {
            CHECK(near(bassOnsets, s, kOnsetTolerance));
        }
    }
    // Every bass onset is a bass note start -- none of the drum hits or synth plucks leak in.
    for (double t : bassOnsets) {
        INFO("bass onset at " << t);
        CHECK(near(bassStarts, t, kOnsetTolerance));
    }
    std::vector<double> synthStarts = fx.times("synth", "plucks");
    for (double s : fx.times("synth", "sustains")) {
        synthStarts.push_back(s);
    }
    const auto synthOnsets = onsetTimes(synth);
    for (double p : fx.times("synth", "plucks")) {
        CHECK(near(synthOnsets, p, kOnsetTolerance));
    }
    for (double t : synthOnsets) {
        INFO("synth onset at " << t);
        // A sustained note's 300 ms attack may be seen anywhere along its rise.
        const bool pluck = near(fx.times("synth", "plucks"), t, kOnsetTolerance);
        const bool swell = std::any_of(synthStarts.begin(), synthStarts.end(),
                                       [&](double s) { return t >= s - kOnsetTolerance && t <= s + 0.35; });
        CHECK((pluck || swell));
    }
    // And the kick times that coincide with nothing in the synth are not synth onsets.
    for (double k : fx.times("drums", "kicks")) {
        if (!near(synthStarts, k, 0.4)) {
            CHECK_FALSE(near(synthOnsets, k, 0.010));
        }
    }
}

TEST_CASE("stem analysis: sustained energy follows the bass notes, and silence reads zero", "[stems][analysis]") {
    const auto& fx = testsupport::requireStemFixture();
    const StemTrack bass = analyzeFile(fx, "bass.wav");
    for (const auto& note : fx.meta["stems"]["bass"]["notes"]) {
        const double start = note["start"].get<double>();
        const double dur = note["duration"].get<double>();
        if (dur < 0.6) {
            continue; // the level's 0.3 s smoothing needs a note longer than its rise to read the note's body
        }
        const StemFrame& body = frameAt(bass, start + dur - 0.1);
        INFO("note at " << start);
        CHECK(body.rms > 0.2f);
        // Read against the stem's own reference (ADR-1510): a note at the stem's usual loudness sits mid-window.
        CHECK(body.level > 0.3f);
        CHECK(body.level < 0.9f);
    }
    // 11.75-14.0 s is silent on purpose: once the window has left the last note, rms and peak are exactly 0, no onset
    // fires, and the level falls on its 1 s release and reads 0 once it is under the -60 dBFS gate -- never held.
    double previous = 2.0;
    for (const StemFrame& f : bass.frames()) {
        if (f.timeSeconds < 11.80 || f.timeSeconds > 13.98) {
            continue;
        }
        INFO("t " << f.timeSeconds);
        REQUIRE(f.rms == 0.0f);
        REQUIRE(f.peak == 0.0f);
        REQUIRE_FALSE(f.onset);
        REQUIRE(static_cast<double>(f.level) <= previous);
        previous = static_cast<double>(f.level);
    }
    CHECK(previous == 0.0);

    const StemTrack silent = analyzeFile(fx, "silent.wav");
    for (const StemFrame& f : silent.frames()) {
        REQUIRE(f.rms == 0.0f);
        REQUIRE(f.level == 0.0f);
        REQUIRE_FALSE(f.onset);
    }
}

TEST_CASE("stem analysis: a late stem's onsets move by exactly its lateness; a 44.1 kHz stem lands on the same seconds",
          "[stems][analysis]") {
    const auto& fx = testsupport::requireStemFixture();
    const double late = fx.meta["variants"]["bass_late.wav"]["lateBySeconds"].get<double>();
    const auto aligned = onsetTimes(analyzeFile(fx, "bass.wav"));
    const auto lateOnsets = onsetTimes(analyzeFile(fx, "bass_late.wav"));
    REQUIRE_FALSE(lateOnsets.empty());
    const double hop = 512.0 / 48000.0;
    for (double t : lateOnsets) {
        INFO("late onset " << t);
        if (t - late < 0.1) {
            CHECK(t == Catch::Approx(late).margin(kOnsetTolerance)); // the note at 0 s: the aligned stem's first hop
            continue;                                                  // has no spectrum before it to rise from
        }
        CHECK(near(aligned, t - late, hop + 1e-9));
    }

    const StemTrack s48 = analyzeFile(fx, "synth.wav");
    const StemTrack s44 = analyzeFile(fx, "synth_44k1.wav");
    CHECK(s44.sampleRate() == 44100);
    CHECK(s44.hopSeconds() == Catch::Approx(512.0 / 44100.0));
    for (double p : fx.times("synth", "plucks")) {
        CHECK(near(onsetTimes(s44), p, kOnsetTolerance));
    }
    // The same note reads the same level at either rate (bands in Hz, levels in dBFS).
    CHECK(frameAt(s44, 5.5).level == Catch::Approx(frameAt(s48, 5.5).level).margin(0.02));
}

TEST_CASE("stem analysis: width is the stereo stem's, and a mono stem says it has none", "[stems][analysis]") {
    const auto& fx = testsupport::requireStemFixture();
    const StemTrack synth = analyzeFile(fx, "synth.wav");
    const StemTrack bass = analyzeFile(fx, "bass.wav");
    const StemFrame& sustain = frameAt(synth, 5.5); // L 1.0, R 0.7: side/mid = 0.3 / 1.7
    CHECK(sustain.stereo);
    CHECK(sustain.width == Catch::Approx(0.3 / 1.7).margin(0.02));
    CHECK_FALSE(frameAt(bass, 5.0).stereo);
    CHECK(frameAt(bass, 5.0).width == 0.0f);
}

TEST_CASE("stem analysis: deterministic, and independent of how the samples were chunked", "[stems][analysis]") {
    const auto& fx = testsupport::requireStemFixture();
    auto file = audio::AudioFile::load(fx.file("drums.wav"));
    REQUIRE(file.has_value());
    const StemTrack a = StemTrack::analyze(*file, {});
    const StemTrack b = StemTrack::analyze(*file, {});
    REQUIRE(a.frames().size() == b.frames().size());
    REQUIRE(a.frames() == b.frames());
    // Analysed while decoding (the engine's path: no PCM held) is the same analysis as analysed from memory.
    audio::StemFileFacts facts;
    const auto streamed = StemTrack::analyzeFile(fx.file("drums.wav"), {}, facts);
    REQUIRE(streamed.has_value());
    CHECK(streamed->frames() == a.frames());
    CHECK(streamed->durationSeconds() == a.durationSeconds());
    CHECK(facts.frames == file->frameCount());
    CHECK(facts.channels == 1);
    CHECK(facts.format == "f32");
    CHECK_FALSE(facts.silent);
    audio::StemFileFacts synthFacts;
    auto synthFile = audio::AudioFile::load(fx.file("synth.wav"));
    REQUIRE(synthFile.has_value());
    const auto synthStreamed = StemTrack::analyzeFile(fx.file("synth.wav"), {}, synthFacts);
    REQUIRE(synthStreamed.has_value());
    CHECK(synthStreamed->frames() == StemTrack::analyze(*synthFile, {}).frames()); // stereo: mono and side alike

    analysis::StemAnalysisConfig config;
    config.analyzer.sampleRate = file->sampleRate();
    analysis::StemAnalyzer streaming(config);
    std::mt19937 rng(7);
    std::uniform_int_distribution<std::size_t> chunk(1, 3000);
    const auto mono = file->mono();
    std::vector<StemFrame> out;
    StemFrame f;
    for (std::size_t i = 0; i < mono.size();) {
        const std::size_t n = std::min(chunk(rng), mono.size() - i);
        streaming.push(mono.subspan(i, n));
        i += n;
        while (streaming.pop(f)) {
            out.push_back(f);
        }
    }
    REQUIRE(out.size() == a.frames().size());
    CHECK(out == a.frames());
}

TEST_CASE("stem analysis: the hits tier finds the drum stem's kicks and leaves the levels tier untouched",
          "[stems][analysis]") {
    const auto& fx = testsupport::requireStemFixture();
    const StemTrack levels = analyzeFile(fx, "drums.wav", false);
    const StemTrack hits = analyzeFile(fx, "drums.wav", true);
    CHECK(onsetTimes(levels, &StemFrame::kick).empty());
    const auto kickEvents = onsetTimes(hits, &StemFrame::kick);
    const auto kicks = fx.times("drums", "kicks");
    std::size_t found = 0;
    for (double k : kicks) {
        // The causal classifier decides a kick up to one hop after its attack (ADR-1060's deferral).
        found += near(kickEvents, k, kOnsetTolerance + 512.0 / 48000.0) ? 1 : 0;
    }
    INFO("kicks found " << found << " of " << kicks.size() << "; kick events " << kickEvents.size());
    CHECK(found >= kicks.size() * 8 / 10);
    for (double t : kickEvents) {
        CHECK(near(kicks, t, kOnsetTolerance + 512.0 / 48000.0));
    }
    // The levels are the same with or without the classifier.
    REQUIRE(levels.frames().size() == hits.frames().size());
    for (std::size_t i = 0; i < levels.frames().size(); ++i) {
        REQUIRE(levels.frames()[i].level == hits.frames()[i].level);
        REQUIRE(levels.frames()[i].onset == hits.frames()[i].onset);
    }
}
