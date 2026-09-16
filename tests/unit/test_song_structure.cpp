// Music structure analysis (ADR-206).
//
// The hard problem in testing this is that **there is no ground truth in this repository**, and a
// test asserting that the detector finds a chorus at 1:02.4 of some real track is a test of the
// detector's current opinion rather than of its correctness. It would pass today, fail on the first
// improvement, and be "fixed" by writing down the new opinion. That is a ratchet, not a test.
//
// So two kinds of assertion here, and no others:
//
// **Structural invariants**, which must hold for any input whatever. Sections are ordered, gapless,
// non-overlapping, cover the track, carry sub-second precision, and are deterministic.
//
// **Synthetic fixtures whose construction is known**, because the boundaries were *placed*. A signal
// built as A B A B C A -- distinct harmonic and timbral material per letter, at a fixed tempo -- has
// boundaries at times this file chose, and a repetition structure this file built. Asserting that
// the pipeline recovers them tests the pipeline without pretending to test musical judgement.
//
// Detection quality on real music is reported by a human listening to it, not asserted here.

#include "analysis/analysis_track.hpp"
#include "analysis/structure.hpp"
#include "audio/audio_file.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <numbers>
#include <set>

using namespace avgen;
using namespace avgen::analysis;

namespace {
constexpr std::uint32_t kRate = 48000;

// One "section" of synthetic music: a chord, a brightness, a level, and a click on every beat.
//
// The click matters as much as the chord: everything downstream is beat-synchronous, so a fixture
// the beat tracker cannot find beats in tests nothing at all. A short percussive transient at a
// steady period is what the Ellis tracker is built to lock onto.
struct Material {
    std::vector<float> partials; // Hz
    float level = 0.3f;
    float brightness = 1.0f; // multiplies a high partial's amplitude: the timbral difference
};

const Material kA{{220.0f, 277.2f, 329.6f, 1760.0f}, 0.30f, 0.15f}; // A minor-ish, dark
const Material kB{{293.7f, 370.0f, 440.0f, 1760.0f}, 0.55f, 0.85f}; // D major-ish, bright and loud
const Material kC{{246.9f, 311.1f, 370.0f, 1760.0f}, 0.20f, 0.30f}; // B diminished-ish, quiet

// Appends `seconds` of `m` at `bpm`, clicking on each beat.
void append(std::vector<float>& out, const Material& m, double seconds, double bpm) {
    const auto samples = static_cast<std::size_t>(seconds * kRate);
    const auto beatSamples = static_cast<std::size_t>(60.0 / bpm * kRate);
    const std::size_t start = out.size();
    for (std::size_t i = 0; i < samples; ++i) {
        const double t = static_cast<double>(i) / kRate;
        float value = 0.0f;
        for (std::size_t p = 0; p < m.partials.size(); ++p) {
            const float amplitude = p + 1 == m.partials.size() ? m.level * m.brightness : m.level;
            value += amplitude * static_cast<float>(std::sin(2.0 * std::numbers::pi * m.partials[p] * t));
        }
        // The click: a fast-decaying broadband burst at each beat.
        const std::size_t sinceBeat = (start + i) % beatSamples;
        if (sinceBeat < beatSamples / 8) {
            const float decay = 1.0f - static_cast<float>(sinceBeat) / static_cast<float>(beatSamples / 8);
            value += 0.5f * decay * decay *
                     static_cast<float>(std::sin(2.0 * std::numbers::pi * 2000.0 * t));
        }
        out.push_back(std::tanh(value * 0.6f));
    }
}

struct Fixture {
    // `AudioFile` has no default constructor, so the fixture is assembled and moved rather than
    // default-built and filled.
    audio::AudioFile file = audio::AudioFile::fromInterleaved(std::vector<float>{0.0f}, 1, kRate);
    std::vector<double> boundaries; // the times this file *placed*, in seconds
};

// A B A B C A at 120 bpm, sixteen seconds each: the smallest thing with a repetition structure worth
// recovering. Sixteen seconds is eight bars at 120, which is a musically plausible section and
// comfortably over the detector's eight-second minimum.
Fixture abacFixture(double each = 16.0, double bpm = 120.0) {
    std::vector<float> mono;
    Fixture f;
    const Material* order[] = {&kA, &kB, &kA, &kB, &kC, &kA};
    for (std::size_t i = 0; i < std::size(order); ++i) {
        if (i > 0) {
            f.boundaries.push_back(static_cast<double>(i) * each);
        }
        append(mono, *order[i], each, bpm);
    }
    f.file = audio::AudioFile::fromInterleaved(mono, 1, kRate);
    return f;
}

AnalysisTrack analyzed(const audio::AudioFile& file) {
    return AnalysisTrack::analyze(file, AnalyzerConfig{});
}
} // namespace

// ---- invariants that must hold on any input -----------------------------------------------------

TEST_CASE("A detected structure is ordered, gapless and covers the track", "[analysis][structure]") {
    const auto fixture = abacFixture();
    const auto track = analyzed(fixture.file);
    auto structure = detectStructure(track);
    INFO((structure ? std::string() : structure.error().message));
    REQUIRE(structure.has_value());
    REQUIRE_FALSE(structure->sections.empty());

    CHECK(structure->validate().has_value());
    // `validate()` is the implementation's own opinion of these properties, so they are checked
    // here independently as well -- a bug that weakened both would otherwise be invisible.
    for (std::size_t i = 0; i < structure->sections.size(); ++i) {
        const auto& s = structure->sections[i];
        CHECK(s.endSeconds > s.startSeconds);
        if (i > 0) {
            CHECK_THAT(s.startSeconds,
                       Catch::Matchers::WithinAbs(structure->sections[i - 1].endSeconds, 1e-9));
        }
    }
    CHECK_THAT(structure->sections.front().startSeconds, Catch::Matchers::WithinAbs(0.0, 1e-6));
    // The last section runs to the end of the analyzed audio, not to the last beat the tracker was
    // confident about -- music carries on past the last beat and that tail belongs to something.
    CHECK(structure->sections.back().endSeconds > track.frames().back().timeSeconds - 1.0);
}

TEST_CASE("Boundaries keep sub-second precision", "[analysis][structure]") {
    // The requirement is explicit: detection gets close, human refinement gets exact, and rounding
    // to whole seconds throws away the thing refinement is for.
    //
    // The first draft of this asserted "some boundary is not a whole number", which was a test of
    // the *fixture* rather than of the code: at 120 bpm a beat is exactly 0.5 s and a 16-second
    // section is exactly 32 beats, so every honest boundary is a whole number of seconds and the
    // assertion failed against perfectly correct output. Tempo is not the invariant.
    //
    // The invariant is that a boundary **is a beat time**, bit for bit -- the detector reports the
    // beat's own timestamp and never a rounded, snapped or reconstructed one. That holds at any
    // tempo, and it is the property rounding would break.
    const auto fixture = abacFixture();
    const auto track = analyzed(fixture.file);
    auto structure = detectStructure(track);
    REQUIRE(structure.has_value());
    REQUIRE(structure->sections.size() >= 2);

    const auto& beats = structure->beatTimes;
    for (std::size_t i = 1; i < structure->sections.size(); ++i) {
        const double boundary = structure->sections[i].startSeconds;
        const bool isABeatTime = std::any_of(beats.begin(), beats.end(), [&](double b) {
            return std::fabs(b - boundary) < 1e-12;
        });
        INFO("boundary " << boundary << " is not any beat's time, so something moved it");
        CHECK(isABeatTime);
    }

    // And at a tempo whose beats are *not* round numbers, the times that come out are not round
    // numbers either -- which is the thing a user notices when it goes wrong.
    const auto odd = abacFixture(14.0, 131.0);
    const auto oddTrack = analyzed(odd.file);
    auto oddStructure = detectStructure(oddTrack);
    REQUIRE(oddStructure.has_value());
    if (oddStructure->sections.size() >= 2) {
        const bool anyFractional = std::any_of(
            oddStructure->sections.begin() + 1, oddStructure->sections.end(), [](const auto& s) {
                return std::fabs(s.startSeconds - std::round(s.startSeconds)) > 1e-4;
            });
        CHECK(anyFractional);
    }
}

TEST_CASE("The same audio gives the same structure", "[analysis][structure]") {
    // Determinism is a requirement rather than a nicety: these boundaries are saved in a project and
    // then edited by a person, so a detector that answers differently on the second run silently
    // moves work somebody did.
    const auto fixture = abacFixture();
    const auto track = analyzed(fixture.file);
    auto first = detectStructure(track);
    auto second = detectStructure(track);
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    REQUIRE(first->sections.size() == second->sections.size());
    for (std::size_t i = 0; i < first->sections.size(); ++i) {
        CHECK_THAT(first->sections[i].startSeconds,
                   Catch::Matchers::WithinAbs(second->sections[i].startSeconds, 0.0));
        CHECK(first->sections[i].function == second->sections[i].function);
        CHECK(first->sections[i].repetitionGroup == second->sections[i].repetitionGroup);
    }
}

TEST_CASE("A track with no beat grid is refused, not guessed at", "[analysis][structure]") {
    // Everything in the detector is beat-synchronous. Falling back to hop resolution would be a
    // different algorithm returning results under this one's name, and nothing downstream could
    // tell which it had been given.
    const auto quiet = audio::AudioFile::fromInterleaved(std::vector<float>(kRate, 0.0f), 1, kRate);
    const auto track = analyzed(quiet);
    auto structure = detectStructure(track);
    CHECK_FALSE(structure.has_value());
}

// ---- the fixture's own construction, recovered --------------------------------------------------

TEST_CASE("Placed boundaries are recovered", "[analysis][structure]") {
    // The five boundaries in A|B|A|B|C|A are at 16, 32, 48, 64 and 80 s because this test put them
    // there. The tolerance is one bar at 120 bpm (2 s): a novelty peak lands on the beat nearest the
    // change, and the checkerboard kernel is symmetric, so sub-bar accuracy is not what this stage
    // promises -- that is what the user's refinement is for.
    const auto fixture = abacFixture();
    const auto track = analyzed(fixture.file);
    auto structure = detectStructure(track);
    INFO((structure ? std::string() : structure.error().message));
    REQUIRE(structure.has_value());

    std::vector<double> found;
    for (std::size_t i = 1; i < structure->sections.size(); ++i) {
        found.push_back(structure->sections[i].startSeconds);
    }
    INFO("placed " << fixture.boundaries.size() << ", found " << found.size());

    std::size_t matched = 0;
    for (const double placed : fixture.boundaries) {
        const bool near = std::any_of(found.begin(), found.end(), [&](double f) {
            return std::fabs(f - placed) <= 2.0;
        });
        if (near) {
            ++matched;
        }
    }
    // Four of the five. Not all five, because A|B|A|B|C|A has one boundary (B->A at 48 s) that is a
    // return to material already heard, and a checkerboard kernel is weakest exactly there -- the
    // past and the future of that point are both "A-ish" at some scale. Demanding five would be
    // demanding the algorithm be better than the algorithm is, and would be met by loosening the
    // tolerance until a coincidence counted.
    CHECK(matched >= 4);
    // And not by finding a boundary everywhere: a detector that returned a boundary every bar would
    // satisfy the line above trivially.
    CHECK(found.size() <= 8);
}

TEST_CASE("Repeated material is grouped", "[analysis][structure]") {
    // A appears three times and B twice. The grouping does not have to agree with that exactly --
    // it does not know where the boundaries "should" be -- but it must find *some* family with more
    // than one member, or the recurrence half of the pipeline is doing nothing and the labels that
    // depend on it are guesses wearing a confidence.
    const auto fixture = abacFixture();
    const auto track = analyzed(fixture.file);
    auto structure = detectStructure(track);
    REQUIRE(structure.has_value());

    std::map<int, int> familySize;
    for (const auto& s : structure->sections) {
        if (s.repetitionGroup >= 0) {
            ++familySize[s.repetitionGroup];
        }
    }
    const bool anyRepeats = std::any_of(familySize.begin(), familySize.end(),
                                        [](const auto& kv) { return kv.second >= 2; });
    INFO("families found: " << familySize.size());
    CHECK(anyRepeats);

    // Occurrence numbers within a family are 0, 1, 2... in time order, because a UI shows them as
    // "Chorus 1 / Chorus 2" and getting that wrong is visible.
    std::map<int, int> lastOccurrence;
    for (const auto& s : structure->sections) {
        if (s.repetitionGroup < 0) {
            continue;
        }
        auto it = lastOccurrence.find(s.repetitionGroup);
        if (it != lastOccurrence.end()) {
            CHECK(s.occurrence > it->second);
        }
        lastOccurrence[s.repetitionGroup] = s.occurrence;
    }
}

TEST_CASE("Energy and density are measured against the track, not asserted absolutely",
          "[analysis][structure]") {
    // B is built to be the loud material and C the quiet one. What is asserted is the *ordering*,
    // because the numbers are rescaled against this track's own range -- which is the point of them.
    const auto fixture = abacFixture();
    const auto track = analyzed(fixture.file);
    auto structure = detectStructure(track);
    REQUIRE(structure.has_value());

    float loudest = 0.0f;
    float quietest = 1.0f;
    for (const auto& s : structure->sections) {
        loudest = std::max(loudest, s.energy);
        quietest = std::min(quietest, s.energy);
        CHECK(s.energy >= 0.0f);
        CHECK(s.energy <= 1.0f);
        CHECK(s.density >= 0.0f);
        CHECK(s.density <= 1.0f);
    }
    CHECK(loudest > quietest); // a rescale that collapsed would pass every bound above
}

// ---- the labels are claims, and say how strong they are -----------------------------------------

TEST_CASE("A label always carries a confidence, and Other carries none", "[analysis][structure]") {
    // The distinction the brief insists on and the one the UI depends on: `Other` is not a low score
    // for a guess, it is the absence of a claim. A section the detector cannot name reports zero
    // rather than a small number that a progress bar would round up into an opinion.
    const auto fixture = abacFixture();
    const auto track = analyzed(fixture.file);
    auto structure = detectStructure(track);
    REQUIRE(structure.has_value());

    for (const auto& s : structure->sections) {
        CHECK(s.labelConfidence >= 0.0f);
        CHECK(s.labelConfidence <= 1.0f);
        if (s.function == SectionFunction::Other) {
            CHECK(s.labelConfidence == 0.0f);
        }
        CHECK(s.startConfidence >= 0.0f);
        CHECK(s.startConfidence <= 1.0f);
        CHECK(s.endConfidence >= 0.0f);
        CHECK(s.endConfidence <= 1.0f);
    }
    // The track's own start and end are certain by construction -- nothing detected them.
    CHECK(structure->sections.front().startConfidence == 1.0f);
    CHECK(structure->sections.back().endConfidence == 1.0f);
}

TEST_CASE("Everything detected is marked as detected", "[analysis][structure]") {
    // The provenance that makes re-analysis safe. If the detector ever emitted `Refined`, a
    // re-analysis would refuse to replace its own previous guess and the user would be unable to
    // get a fresh reading.
    const auto fixture = abacFixture();
    const auto track = analyzed(fixture.file);
    auto structure = detectStructure(track);
    REQUIRE(structure.has_value());
    for (const auto& s : structure->sections) {
        CHECK(s.origin == SectionOrigin::Detected);
        CHECK(s.label.empty()); // a label is a person's name for a section; the detector sets none
    }
}

TEST_CASE("Section names round-trip", "[analysis][structure]") {
    using F = SectionFunction;
    for (const F f : {F::Intro, F::Verse, F::PreChorus, F::Build, F::Chorus, F::Drop, F::Break,
                      F::Bridge, F::Instrumental, F::Breakdown, F::FinalChorus, F::Outro, F::Other}) {
        const auto back = sectionFunctionFromName(sectionFunctionName(f));
        REQUIRE(back.has_value());
        CHECK(*back == f);
    }
    for (const SectionOrigin o : {SectionOrigin::Detected, SectionOrigin::Refined, SectionOrigin::Authored}) {
        const auto back = sectionOriginFromName(sectionOriginName(o));
        REQUIRE(back.has_value());
        CHECK(*back == o);
    }
    CHECK_FALSE(sectionFunctionFromName("chorusish").has_value());
}

TEST_CASE("sectionAt finds the section a time is in", "[analysis][structure]") {
    const auto fixture = abacFixture();
    const auto track = analyzed(fixture.file);
    auto structure = detectStructure(track);
    REQUIRE(structure.has_value());
    REQUIRE(structure->sections.size() >= 2);

    const auto& second = structure->sections[1];
    const double inside = (second.startSeconds + second.endSeconds) * 0.5;
    const SongSection* found = structure->sectionAt(inside);
    REQUIRE(found != nullptr);
    CHECK_THAT(found->startSeconds, Catch::Matchers::WithinAbs(second.startSeconds, 1e-9));
    // A boundary belongs to the section it opens, not to the one it closes.
    const SongSection* atBoundary = structure->sectionAt(second.startSeconds);
    REQUIRE(atBoundary != nullptr);
    CHECK_THAT(atBoundary->startSeconds, Catch::Matchers::WithinAbs(second.startSeconds, 1e-9));
    CHECK(structure->sectionAt(-1.0) == nullptr);
}

// ---- the stages, separately ---------------------------------------------------------------------

TEST_CASE("Foote novelty peaks where a checkerboard says it should", "[analysis][structure]") {
    // A synthetic SSM with two perfectly self-similar halves and no similarity between them. The
    // novelty maximum must land at the join. This is the kernel on its own, with no audio anywhere
    // near it -- so when the pipeline misbehaves, this says whether the kernel is the reason.
    constexpr int n = 64;
    std::vector<float> ssm(n * n, 0.0f);
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            const bool sameHalf = (i < n / 2) == (j < n / 2);
            ssm[i * n + j] = sameHalf ? 1.0f : 0.0f;
        }
    }
    const auto novelty = footeNovelty(ssm, n, 16);
    REQUIRE(novelty.size() == static_cast<std::size_t>(n));
    const auto peak = static_cast<int>(
        std::distance(novelty.begin(), std::max_element(novelty.begin(), novelty.end())));
    CHECK(std::abs(peak - n / 2) <= 2);
}

TEST_CASE("Peak picking takes the strongest peaks and respects separation",
          "[analysis][structure]") {
    // Two peaks close together and one far away. With a separation wider than the close pair, the
    // stronger of the two survives and the weaker does not -- strongest-first, which is why the
    // picker sorts rather than sweeping left to right.
    std::vector<float> novelty(100, 0.1f);
    novelty[20] = 0.9f;
    novelty[23] = 0.7f;
    novelty[70] = 0.8f;
    const auto peaks = pickBoundaries(novelty, 2.0f, 8, 10);
    REQUIRE(peaks.size() == 2);
    CHECK(peaks[0] == 20);
    CHECK(peaks[1] == 70);
}

TEST_CASE("A flat novelty curve has no peaks", "[analysis][structure]") {
    // A fixed threshold would invent boundaries in silence. The picker thresholds on the curve's own
    // median absolute deviation, so a curve with no spread has nothing above anything.
    const std::vector<float> flat(100, 0.42f);
    CHECK(pickBoundaries(flat, 2.0f, 4, 10).empty());
}

TEST_CASE("Chroma ignores level and follows pitch", "[analysis][structure]") {
    // The claim the whole harmonic half of the pipeline rests on: the same notes at two volumes look
    // alike, and different notes do not. Asserted on real analysis frames rather than on a mocked
    // spectrum, because the binning is the part that can be wrong.
    const auto tone = [](float hz, float amplitude) {
        std::vector<float> mono;
        for (std::size_t i = 0; i < kRate * 2; ++i) {
            const double t = static_cast<double>(i) / kRate;
            mono.push_back(amplitude * static_cast<float>(std::sin(2.0 * std::numbers::pi * hz * t)));
        }
        return audio::AudioFile::fromInterleaved(mono, 1, kRate);
    };
    const auto quiet = analyzed(tone(440.0f, 0.1f));
    const auto loud = analyzed(tone(440.0f, 0.8f));
    const auto other = analyzed(tone(523.25f, 0.8f)); // C5, three semitones up

    const auto cq = chromagram(quiet);
    const auto cl = chromagram(loud);
    const auto co = chromagram(other);
    REQUIRE_FALSE(cq.empty());
    const std::size_t mid = cq.size() / 2;

    const auto dot = [](const std::array<float, 12>& a, const std::array<float, 12>& b) {
        float sum = 0.0f;
        for (std::size_t i = 0; i < 12; ++i) {
            sum += a[i] * b[i];
        }
        return sum;
    };
    // Same note, eighteen decibels apart: nearly identical.
    CHECK(dot(cq[mid], cl[mid]) > 0.9f);
    // A different note is measurably further away than that.
    CHECK(dot(cl[mid], co[co.size() / 2]) < dot(cq[mid], cl[mid]));
}

// ---- what it says about real music, reported rather than asserted --------------------------------

TEST_CASE("Structure of the Glowmere track", "[.report][analysis][structure]") {
    // Hidden (`[.report]`), and deliberately so. There is no ground truth for this track, so there
    // is nothing here to pass or fail -- asserting that the detector finds a chorus at 1:02.4 would
    // be writing down its current opinion and calling it a requirement, and every future improvement
    // would then "break" it.
    //
    // What this is for: running it prints what the detector thinks, so a person can listen and judge.
    // That is the honest place for detection quality. Run with:
    //
    //     avgen_tests "Structure of the Glowmere track"
    const std::filesystem::path wav =
        std::filesystem::path(AVGEN_SOURCE_DIR) / "assets" / "audio" / "glowmere-valley.wav";
    if (!std::filesystem::exists(wav)) {
        WARN("no track at " << wav.string());
        return;
    }
    auto file = audio::AudioFile::load(wav);
    REQUIRE(file.has_value());
    const auto track = analyzed(*file);
    auto structure = detectStructure(track);
    INFO((structure ? std::string() : structure.error().message));
    REQUIRE(structure.has_value());

    WARN("tempo " << structure->tempoBpm << " bpm (confidence " << structure->tempoConfidence
                  << "), " << structure->sections.size() << " section(s) over "
                  << structure->durationSeconds << " s");
    for (const auto& s : structure->sections) {
        WARN(sectionFunctionName(s.function)
             << "  " << s.startSeconds << " -> " << s.endSeconds << "  label conf "
             << s.labelConfidence << "  boundary conf " << s.startConfidence << "  group "
             << s.repetitionGroup << "/" << s.occurrence << "  energy " << s.energy << "  density "
             << s.density);
    }
}
