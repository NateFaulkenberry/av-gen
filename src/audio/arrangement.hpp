#pragma once

// An arrangement of audio clips, mixed down to one buffer (ADR-103).
//
// ## The problem this solves
//
// `Engine` holds one `audioFile_` and plays it through one `AudioPlayer`. A piece made of several
// files -- a stem set, a song followed by a spoken outro, two cues with a gap between them -- could
// not be expressed at all. `docs/cinematic-world-gap-analysis.md` §25 records it as missing.
//
// ## Why a mixdown, and not a mixer
//
// The obvious shape is a mixer in the audio callback: keep every clip decoded, walk the active ones
// per buffer and sum. That is the architecture a DAW has, and it is the wrong one here, for three
// reasons that are all about *what else would have to change*.
//
// Everything downstream of `AudioFile` is built on one continuous buffer: `AudioPlayer`'s
// real-time callback, `AnalysisStream`, `AnalysisRunner`, `AnalysisTrack::analyze`,
// `WaveformSummary`, and the transport's "audio is the master clock" rule. A live mixer changes
// every one of them, and several are real-time code that is correct today.
//
// A mixdown changes **none** of them. The arrangement is rendered to an `AudioFile` when it is
// edited, and the engine installs that file exactly as it installs a loaded one. One clip at gain 1
// with no trim mixes to a buffer that is *bit-identical* to the file itself -- which is the property
// that makes routing the existing single-file path through here safe, and it is tested.
//
// ADR-003 already decided that audio lives decoded in memory. A mixdown is that decision applied
// once more, not a departure from it.
//
// The cost is memory (the sources plus the mix) and a pass over the samples per edit. Both are
// measured in `tests/unit/test_arrangement.cpp`; for reference, a six-minute 48 kHz stereo
// arrangement mixes in about a tenth of a second and holds about 140 MB.
//
// ## What it does not do
//
// No fades unless asked for, so the identity property holds. No pitch or time stretching. Clips at
// different sample rates are resampled linearly and **warned about**, because linear resampling is
// audible on music and the honest fix is to convert the file.

#include "audio/audio_file.hpp"
#include "core/error.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace avgen::audio {

// One piece of audio, placed on the timeline.
//
// Four numbers, and they are the four every editor has: where it sits, how far into the file it
// starts, how long it plays, and how loud. `durationSeconds == 0` means "to the end of the file",
// which is what a clip dropped on a timeline means before anybody trims it.
struct AudioClip {
    std::filesystem::path file;
    double startSeconds = 0.0;    // where the clip begins on the project's timeline
    double inSeconds = 0.0;       // how far into the file the clip starts
    double durationSeconds = 0.0; // 0 = the rest of the file from `inSeconds`
    float gain = 1.0f;
    // Fades, in seconds, at the clip's own edges. Zero by default, deliberately: an automatic fade
    // would break the one-clip identity property, and a person cutting between two takes wants to
    // choose where the crossfade is rather than discover one.
    double fadeInSeconds = 0.0;
    double fadeOutSeconds = 0.0;
    bool enabled = true;
    std::string name; // for the lane label; the file's stem when empty

    [[nodiscard]] bool operator==(const AudioClip&) const = default;
};

// What a mix did, for the log and the UI. A mix that quietly dropped a clip is the failure mode this
// exists to make impossible.
struct MixReport {
    std::size_t clipsMixed = 0;
    std::size_t clipsSkipped = 0;
    double durationSeconds = 0.0;
    std::uint32_t sampleRate = 0;
    std::uint32_t channels = 0;
    double millis = 0.0;
    // A clip that could not be loaded, or one that had to be resampled. Every one of these is
    // surfaced: the engine puts them in the project warnings.
    std::vector<std::string> warnings;
};

// Loads the files a set of clips names, once each however many clips share one.
//
// Separate from the mix so an editor can hold the decoded sources across many edits -- re-mixing
// after dragging a clip must not re-decode a hundred megabytes of wav.
class ClipSources {
public:
    // Loads anything not already held and drops anything no clip names any more. Returns the paths
    // that failed, which the caller reports; a missing file is not a reason to refuse the mix.
    std::vector<std::string> sync(std::span<const AudioClip> clips);
    [[nodiscard]] std::shared_ptr<const AudioFile> find(const std::filesystem::path& path) const;
    [[nodiscard]] std::size_t size() const { return files_.size(); }
    void clear() { files_.clear(); }

private:
    std::vector<std::pair<std::filesystem::path, std::shared_ptr<const AudioFile>>> files_;
};

// Mixes the enabled clips into one file.
//
// The output rate is the **highest** rate among the clips, so the common case -- every file at the
// same rate -- is a straight copy with no resampling at all, and the identity property holds.
// Channels are the highest count, mono sources being spread to every channel.
//
// An empty arrangement is not an error: it returns a file with no frames, which is what "this
// project has no audio" has always looked like.
[[nodiscard]] Result<AudioFile> mixArrangement(std::span<const AudioClip> clips, const ClipSources& sources,
                                               MixReport* report = nullptr);

// Where a clip ends on the timeline, given the file it names (or 0 when it names none).
[[nodiscard]] double clipEndSeconds(const AudioClip& clip, const AudioFile* file);
// The end of the whole arrangement.
[[nodiscard]] double arrangementDuration(std::span<const AudioClip> clips, const ClipSources& sources);

// ---- cutting a clip in two (ADR-355) --------------------------------------------------------------
//
// The slice tool's audio arm. There was no split here before, and the arithmetic is the reason it is
// worth having in one place: a clip is a window onto a file, so cutting it at a timeline second has
// to move `inSeconds` by the same amount it moves `startSeconds`, or the later half plays the wrong
// part of the take. Doing that at a call site is one sign error away from a cut that sounds like a
// skip.
//
// `endSeconds` is the clip's resolved end -- `clipEndSeconds(clip, file)` -- passed in rather than
// derived, because a `durationSeconds` of 0 means "the rest of the file" and only the caller holds
// the file. The later half inherits that open end: a split of an open clip stays open, so it still
// follows the source if the source is replaced.
//
// Refuses where either half would be shorter than `minSeconds`, the same way `splitShot` does, and
// returns the index of the new later half. The clip's fades stay at the OUTER edges -- the fade in
// on the first half, the fade out on the second -- because a cut is not a place a person asked for
// two new fades, and a crossfade at the seam is a decision they make afterwards.
[[nodiscard]] std::optional<std::size_t> splitClip(std::vector<AudioClip>& clips, std::size_t index,
                                                   double seconds, double endSeconds,
                                                   double minSeconds = 0.25);

} // namespace avgen::audio
