#pragma once

// A drawable summary of an audio file: the minimum and maximum sample in each short slice of time.
//
// This exists because the obvious thing is unaffordable. A 105-second stereo file is five million
// samples, and a waveform lane 1,400 pixels wide asks "what are the extremes in this column" 1,400
// times per frame. Answering that from the samples is the whole file per frame, at 60 Hz, on the
// thread that also runs the editor. Answering it from a summary built once is two lookups per
// column.
//
// The summary is in *time*, not in pixels, so it survives zooming: at 200 buckets per second a
// column covering a tenth of a second reads twenty buckets, and a column covering ten seconds reads
// two thousand. That costs 105 s x 200 x 2 floats = 168 KB for Night Shift, which is nothing beside
// the 20 MB of audio it describes.
//
// Deliberately GPU-free and ImGui-free: what a waveform *is* can be checked in a unit test, and
// only the drawing needs a device.

#include "audio/audio_file.hpp"

#include <cstdint>
#include <utility>
#include <vector>

namespace avgen::audio {

struct WaveformSummary {
    std::uint32_t bucketsPerSecond = 200;
    std::uint32_t sampleRate = 0;
    double durationSeconds = 0.0;
    // One entry per bucket. Separate arrays rather than pairs because the draw walks each in a run.
    std::vector<float> minValues;
    std::vector<float> maxValues;

    [[nodiscard]] bool empty() const { return minValues.empty(); }
    [[nodiscard]] std::size_t bucketCount() const { return minValues.size(); }

    // The extremes over [fromSeconds, toSeconds). Clamped to the file; an empty or reversed range,
    // or one entirely outside the file, gives {0, 0} -- a flat line, which is what silence looks
    // like and is the right thing to draw past the end of a clip.
    //
    // A range narrower than one bucket still reads the bucket it falls in rather than returning
    // nothing, so zooming in past the summary's resolution flattens gradually instead of vanishing.
    [[nodiscard]] std::pair<float, float> peak(double fromSeconds, double toSeconds) const;
};

// Builds the summary from the file's mono mixdown. `bucketsPerSecond` is clamped to [1, 2000]: at
// the top of that a bucket is half a millisecond, finer than any screen can show and already far
// past where the eye stops reading a waveform as a shape.
[[nodiscard]] WaveformSummary summarise(const AudioFile& file, std::uint32_t bucketsPerSecond = 200);

} // namespace avgen::audio
