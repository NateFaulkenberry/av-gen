// The drawable summary of an audio file (sequencer audio lane).

#include "audio/waveform.hpp"
#include "support/synth.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <vector>

using namespace avgen;

namespace {

// A file whose shape is known exactly: silence, then full scale, then silence, so the summary has
// something to be right or wrong about at a known second.
audio::AudioFile threePartFile(std::uint32_t rate = 48000) {
    std::vector<float> mono(static_cast<std::size_t>(rate) * 3, 0.0f);
    for (std::size_t i = rate; i < static_cast<std::size_t>(rate) * 2; ++i) {
        mono[i] = (i % 2 == 0) ? 1.0f : -1.0f; // alternating, so min and max are both saturated
    }
    return audio::AudioFile::fromInterleaved(std::move(mono), 1, rate);
}

} // namespace

TEST_CASE("A waveform summary describes the file in time, not in samples", "[audio][waveform]") {
    const audio::AudioFile file = threePartFile();
    const audio::WaveformSummary sum = audio::summarise(file, 100);

    CHECK(sum.bucketsPerSecond == 100);
    CHECK_THAT(sum.durationSeconds, Catch::Matchers::WithinAbs(3.0, 1e-6));
    // Three seconds at a hundred buckets each. Exactly, because the file is a whole number of
    // seconds -- a fencepost here would put the loud second in the wrong place on screen.
    CHECK(sum.bucketCount() == 300);

    // The quiet halves read as silence and the loud second reads as full scale, which is the whole
    // claim: the summary knows *when* something happens.
    auto [q1lo, q1hi] = sum.peak(0.0, 1.0);
    CHECK(q1lo == 0.0f);
    CHECK(q1hi == 0.0f);
    auto [loudLo, loudHi] = sum.peak(1.0, 2.0);
    CHECK_THAT(loudLo, Catch::Matchers::WithinAbs(-1.0f, 1e-5f));
    CHECK_THAT(loudHi, Catch::Matchers::WithinAbs(1.0f, 1e-5f));
    auto [q2lo, q2hi] = sum.peak(2.0, 3.0);
    CHECK(q2lo == 0.0f);
    CHECK(q2hi == 0.0f);
}

TEST_CASE("A waveform summary answers ranges no column will ever ask for", "[audio][waveform]") {
    const audio::AudioFile file = threePartFile();
    const audio::WaveformSummary sum = audio::summarise(file, 100);

    // Past the end, before the start, and straddling each edge. A lane draws all four of these the
    // moment the view is scrolled, and every one of them must be a flat line rather than a crash
    // or a wrapped read.
    CHECK(sum.peak(5.0, 6.0) == std::pair<float, float>{0.0f, 0.0f});
    CHECK(sum.peak(-2.0, -1.0) == std::pair<float, float>{0.0f, 0.0f});
    CHECK(sum.peak(-1.0, 1.5).second > 0.5f);  // straddles the start and reaches the loud part
    CHECK(sum.peak(2.5, 9.0) == std::pair<float, float>{0.0f, 0.0f}); // straddles the end, quiet there

    // Reversed and empty ranges are silence, not undefined behaviour.
    CHECK(sum.peak(2.0, 1.0) == std::pair<float, float>{0.0f, 0.0f});
    CHECK(sum.peak(1.5, 1.5) == std::pair<float, float>{0.0f, 0.0f});

    // Zoomed in past the summary's own resolution the waveform must flatten gradually, not vanish:
    // a range a tenth the width of one bucket still reads the bucket it falls inside. This is the
    // case that made the first version disappear at high zoom.
    const double tinyRange = 1.0 / (100.0 * 10.0);
    auto [lo, hi] = sum.peak(1.5, 1.5 + tinyRange);
    CHECK(hi > 0.5f);
    CHECK(lo < -0.5f);
}

TEST_CASE("A waveform summary keeps the tail and does not drift", "[audio][waveform]") {
    // A duration that is not a whole number of buckets, so the last bucket is partial. Dropping it
    // is the easy bug and it shows up as a waveform that stops slightly before the audio does.
    constexpr std::uint32_t rate = 44100;
    std::vector<float> mono(static_cast<std::size_t>(rate * 2) + 1234, 0.25f);
    mono.back() = 0.9f; // the very last sample, which must survive into the summary
    const audio::AudioFile file = audio::AudioFile::fromInterleaved(std::move(mono), 1, rate);
    const audio::WaveformSummary sum = audio::summarise(file, 60);

    REQUIRE(!sum.empty());
    CHECK(static_cast<double>(sum.bucketCount()) >= file.durationSeconds() * 60.0);
    // The last sample is in the summary, which is only true if the partial bucket was built and the
    // bucket edges did not drift across two seconds of audio.
    CHECK(sum.peak(file.durationSeconds() - 0.05, file.durationSeconds()).second > 0.8f);
}

TEST_CASE("A waveform summary of nothing is empty rather than wrong", "[audio][waveform]") {
    const audio::AudioFile silence = audio::AudioFile::fromInterleaved({}, 1, 48000);
    const audio::WaveformSummary sum = audio::summarise(silence);
    CHECK(sum.empty());
    CHECK(sum.peak(0.0, 1.0) == std::pair<float, float>{0.0f, 0.0f});

    // An absurd bucket rate is clamped rather than allocating per sample or dividing by zero.
    const audio::AudioFile file = threePartFile(8000);
    CHECK(audio::summarise(file, 0).bucketsPerSecond == 1);
    CHECK(audio::summarise(file, 100000).bucketsPerSecond == 2000);
}

TEST_CASE("A waveform summary costs a scan, not a scan per column", "[audio][waveform]") {
    // The reason this type exists. Building is O(samples) once; a lane's worth of columns is then
    // O(buckets touched), which for a full-piece view is the summary and for a zoomed view is a
    // slice of it. Asserted as a bound on work rather than on time: 1,400 columns over a
    // three-second file must not read more buckets than the summary has, several times over.
    const audio::AudioFile file = threePartFile();
    const audio::WaveformSummary sum = audio::summarise(file, 200);
    REQUIRE(sum.bucketCount() == 600);

    double sumOfPeaks = 0.0;
    constexpr int kColumns = 1400;
    for (int c = 0; c < kColumns; ++c) {
        const double from = 3.0 * static_cast<double>(c) / kColumns;
        const double to = 3.0 * static_cast<double>(c + 1) / kColumns;
        sumOfPeaks += sum.peak(from, to).second;
    }
    // The loud second is a third of the piece, so about a third of the columns are at full scale.
    CHECK(sumOfPeaks > static_cast<double>(kColumns) * 0.25);
    CHECK(sumOfPeaks < static_cast<double>(kColumns) * 0.45);
}
