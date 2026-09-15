#pragma once

// Music structure analysis (ADR-206): where the sections of a piece are, and what they probably are.
//
// The project already folded musical *moments* into sections -- `signals::MusicalStructure`, ADR-063
// -- from builds, drops and energy trends. That fold is good at the thing it was built for, which is
// EDM-shaped dynamics, and it has no way to notice that bar 33 and bar 97 are the same music. This
// is the other half: structure from *repetition and change* rather than from energy alone.
//
// ## Two problems, deliberately not conflated
//
// **Where** a section boundary is, and **what** the section is, are separate questions with separate
// confidences. A boundary can be certain while its label is a guess -- that is the normal case, not
// a degenerate one -- so nothing here returns a label without saying how much to trust it, and a
// track whose labels are all uncertain still returns boundaries worth having.
//
// This matters most for genre robustness. On an ambient piece the honest answer is "here are five
// sections, A B A C A, and I do not know what to call them"; a detector that reports Verse and
// Chorus there is not more useful, it is wrong with confidence.
//
// ## The pipeline
//
//     per-frame features (existing Analyzer)
//       -> chroma + timbre, folded from the magnitude spectrum
//       -> aggregated onto the beat grid (existing Ellis beat tracker)
//       -> self-similarity matrices, one per feature family
//       -> Foote novelty, several kernel widths
//       -> peak-picked candidate boundaries, snapped to beats
//       -> recurrence grouping: which segments are the same music
//       -> functional labels from structural evidence
//
// Every stage is a pure function of the one before it. There is no wall clock, no rng and no thread
// scheduling anywhere in it, so the same audio produces the same sections on every machine and in
// every run -- which is a requirement rather than a nicety, because these boundaries get saved in a
// project and a user edits them.
//
// ## Why it is beat-synchronous
//
// Not for musicality -- for tractability. A four-minute track is ~20,000 analysis hops, and a dense
// self-similarity matrix over hops is 400 million cells. Over beats it is ~500 x 500. The beat grid
// is already computed (`OfflineBeats`, Ellis 2007) and aggregating onto it makes the whole approach
// affordable. The precise timestamps are kept: a boundary is reported at the beat's own time in
// seconds, never rounded.
//
// References, used to inform rather than to reproduce: Foote (2000), *Automatic Audio Segmentation
// Using a Measure of Audio Novelty*; Mueller, *FMP* C4S4 on novelty-based segmentation; librosa's
// `segment.recurrence_matrix`; MIREX Structural Segmentation.

#include "analysis/analysis_track.hpp"
#include "core/error.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::analysis {

// What a section is for, musically. Wider than `signals::MusicalSection` on purpose: that enum is
// the *director's* vocabulary and every one of its values has to mean something to a shot, whereas
// this one is a description of the music and is allowed to say "I do not know".
enum class SectionFunction : std::uint8_t {
    Intro,
    Verse,
    PreChorus,
    Build,
    Chorus,
    Drop,
    Break,
    Bridge,
    Instrumental,
    Breakdown,
    FinalChorus,
    Outro,
    // Not a failure and not a fallback for bugs: the correct answer for music that does not have
    // verses and choruses. A section labelled `Other` with a good boundary and a repetition group is
    // more useful than a confident wrong label, and this is what keeps the detector honest on
    // ambient, orchestral and experimental material.
    Other,
};
[[nodiscard]] const char* sectionFunctionName(SectionFunction f);
[[nodiscard]] std::optional<SectionFunction> sectionFunctionFromName(std::string_view name);
// Every function, in declaration order. One list, so a UI offering the choices and a test checking
// them all cannot come to disagree about how many there are.
[[nodiscard]] std::span<const SectionFunction> allSectionFunctions();

// Where a section's boundaries and label came from. The reason this exists is data loss: re-running
// analysis must replace what the analyser guessed and leave alone what a person decided, and that is
// not expressible unless each section remembers which it is.
enum class SectionOrigin : std::uint8_t {
    Detected, // the analyser's own interpretation; a re-analysis may replace it
    Refined,  // detected, then moved or relabelled by a person; a re-analysis must not touch it
    Authored, // created by a person from nothing
};
[[nodiscard]] const char* sectionOriginName(SectionOrigin o);
[[nodiscard]] std::optional<SectionOrigin> sectionOriginFromName(std::string_view name);

// One section of a piece.
//
// Times are seconds, `double`, and are never rounded or quantised -- a boundary snapped to a beat is
// snapped to *that beat's own time*, which is not a whole number of anything.
struct SongSection {
    SectionFunction function = SectionFunction::Other;
    std::string label; // a person's name for it; empty means "use the function's name"
    double startSeconds = 0.0;
    double endSeconds = 0.0;

    // How much to trust the *label*. Separate from the boundary confidences below, because they are
    // separate claims and the label is very often the weaker one.
    float labelConfidence = 0.0f;
    // How much to trust each boundary's position. The start of section n and the end of section n-1
    // are the same boundary and carry the same number.
    float startConfidence = 0.0f;
    float endConfidence = 0.0f;

    // Which segments are the same music. Sections sharing a `repetitionGroup` were found to repeat;
    // `occurrence` is 0 for the first time that material appears, 1 for the second, and so on.
    // -1 means the grouping found nothing to group it with, which is a real answer.
    int repetitionGroup = -1;
    int occurrence = 0;

    // Measured, not invented. Both are relative to the track's own distribution rather than absolute,
    // because "loud" only means anything next to the rest of the piece.
    float energy = 0.0f;  // 0..1, from RMS over the section against the track's range
    float density = 0.0f; // 0..1, from onset rate over the section against the track's range
    //
    // There is deliberately no `tension` field. It was asked for, and there is no derivation for it
    // here that is not a formula dressed up as a measurement -- so it is absent rather than
    // fabricated. If one arrives that is defensible (rising energy *and* rising density *and*
    // harmonic instability across a window, validated against something), it belongs here then.

    SectionOrigin origin = SectionOrigin::Detected;

    [[nodiscard]] double durationSeconds() const { return endSeconds - startSeconds; }
};

// Everything the detector found, plus enough of its working to explain itself.
struct SongStructure {
    std::vector<SongSection> sections; // ordered, gapless, non-overlapping, covering the track
    double durationSeconds = 0.0;
    float tempoBpm = 0.0f;
    float tempoConfidence = 0.0f;
    // The beat grid the boundaries were snapped to, so a UI can offer beat-aware dragging without
    // re-running the beat tracker.
    std::vector<double> beatTimes;
    // The novelty curve, one value per beat, for a UI that wants to show *why* a boundary is where
    // it is. Not required by anything; cheap to carry and the first thing anybody asks for when a
    // boundary looks wrong.
    std::vector<float> novelty;

    [[nodiscard]] Result<void> validate() const;
    [[nodiscard]] const SongSection* sectionAt(double seconds) const;
};

struct StructureConfig {
    // Kernel widths for Foote novelty, in beats. Several, because a section change is visible at the
    // scale of the thing that changed: 8 beats catches a two-bar turnaround and misses a key change,
    // 64 beats does the reverse. The curves are combined, not chosen between.
    std::vector<int> kernelBeats{16, 32, 64};
    // How much each feature family contributes to the combined similarity. Harmonic material is the
    // strongest evidence that two passages are "the same music"; timbre catches instrumentation
    // changes that leave the harmony alone; energy is the weakest because it is the one a section
    // change is least obliged to move.
    float chromaWeight = 0.5f;
    float timbreWeight = 0.35f;
    float energyWeight = 0.15f;

    // A section shorter than this is absorbed into its neighbour. Guards the failure mode that
    // matters: not missing a section, but finding forty of them.
    double minSectionSeconds = 8.0;
    // A novelty peak must stand this far above the local median, in units of the curve's own median
    // absolute deviation, to be a boundary. Robust statistics rather than a fixed threshold, because
    // novelty scale varies wildly between a dense mix and a sparse one.
    float peakThresholdMad = 2.2f;
    // The most sections to return. A cap rather than a target: a piece with six sections returns six.
    int maxSections = 32;
};

// The whole pipeline, offline, on a fully analysed track.
//
// Needs `track.beats().beatTimes` to be populated -- everything here is beat-synchronous, and a
// track with no beat grid is refused rather than silently analysed at hop resolution, which would be
// a different algorithm wearing this one's name.
[[nodiscard]] Result<SongStructure> detectStructure(const AnalysisTrack& track,
                                                    const StructureConfig& config = {});

// ---- the stages, exposed because they are separately testable ------------------------------------
//
// Not exposed for reuse: exposed because a pipeline whose stages can only be tested through its
// final output is a pipeline whose failures can only be diagnosed by staring at the final output.

// Twelve pitch classes per frame, folded from the magnitude spectrum and normalised so that a frame
// is a direction rather than a level. Silence yields all zeroes rather than a normalised noise floor.
[[nodiscard]] std::vector<std::array<float, 12>> chromagram(const AnalysisTrack& track);

// A compact timbre vector per frame: log-compressed band energies, mean-removed, so it describes the
// *shape* of the spectrum rather than its loudness. Roughly what MFCCs are for, without a DCT this
// file would have to justify keeping.
[[nodiscard]] std::vector<std::vector<float>> timbregram(const AnalysisTrack& track);

// Aggregates per-hop features onto beat intervals by median. Median rather than mean because one
// transient inside a beat should not decide what that beat looked like.
[[nodiscard]] std::vector<std::vector<float>> beatSynchronous(
    const std::vector<std::vector<float>>& perFrame, std::span<const double> frameTimes,
    std::span<const double> beatTimes);

// Cosine self-similarity of beat-synchronous features, row-major, `n * n`.
[[nodiscard]] std::vector<float> selfSimilarity(const std::vector<std::vector<float>>& beatFeatures);

// Foote's checkerboard kernel convolved down the diagonal of `ssm` (which is `n * n`). Returns one
// novelty value per beat, normalised to 0..1.
[[nodiscard]] std::vector<float> footeNovelty(std::span<const float> ssm, int n, int kernelBeats);

// Peak-picks a novelty curve into beat indices. `thresholdMad` is in median-absolute-deviations
// above the running median; `minSeparation` is in beats.
[[nodiscard]] std::vector<int> pickBoundaries(std::span<const float> novelty, float thresholdMad,
                                              int minSeparation, int maxCount);

} // namespace avgen::analysis
