#pragma once

// Audio history for fields (ADR-1116): what a `spectrum` or `onset` field reads.
//
// Two things, both a function of the history's clock and nothing else:
//   * a log-frequency spectrogram, kAudioBins bins from kAudioMinHz to kAudioMaxHz, one row per
//     analysis hop (93.75 rows/s at the analyzer's defaults), each bin in [0, 1];
//   * per source (low / mid / high band onsets, beats), the onset times and strengths.
//
// Offline (a file with an analysed track) the history holds the WHOLE track and its clock is the
// transport second, so every query is a pure function of t and a seek is exact by construction.
// Live (an input device, or a file played live) rows are appended as the analysis delivers them and
// the clock is the newest analysed second; the history keeps 2 x kAudioRingRows rows.
//
// The GPU never sees this object. `rendering::FieldUniforms` copies the newest kAudioRingRows rows
// into a ring in the field table (group 0 binding 15) and the newest kOnsetHistory onsets of each
// source into the FieldBlock, once per frame. The CPU sampling below reproduces the GPU's maths --
// including the ring's limit: a row older than kAudioRingRows below the newest reads as 0 on both
// sides -- so the CPU reference and the GPU agree (tests/rendering/test_audio_fields_gpu.cpp).
//
// This header depends on nothing above `spatial`: who fills it (the engine, from an
// analysis::AnalysisTrack or the live analyzer) is analysis/audio_history_builder.hpp.

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace avgen::spatial {

inline constexpr int kAudioBins = 64;
inline constexpr float kAudioMinHz = 32.0f;
inline constexpr float kAudioMaxHz = 16000.0f;
// Rows the GPU ring holds: 16.4 s at 93.75 rows/s. A `spectrum` field's delay reaches at most this
// far back; anything older reads 0. 393,216 B in the field table.
inline constexpr int kAudioRingRows = 1536;
inline constexpr std::size_t kAudioRingFloats = static_cast<std::size_t>(kAudioRingRows) * kAudioBins;
// The furthest a spectrum field can hear into the past: 12.0 s at 93.75 rows/s. Shorter than the ring on
// purpose (ADR-1119): a simulation replaying a backlog steps up to 4 s behind the ring's newest row, and
// every step must still find all its rows, or a replayed step would hear less than the played one did.
inline constexpr int kAudioMaxDelayRows = 1125;
inline constexpr int kOnsetHistory = 8; // newest onsets per source handed to the GPU

// Appended only: the integer is packed into FieldGpu and mirrored in shaders/fields.wgsl.
enum class OnsetSource : std::uint8_t { Low, Mid, High, Beat };
inline constexpr int kOnsetSources = 4;
[[nodiscard]] const char* onsetSourceName(OnsetSource source);
[[nodiscard]] std::optional<OnsetSource> onsetSourceFromName(std::string_view name);

struct AudioOnset {
    double time = 0.0; // history-clock seconds
    float strength = 1.0f;
};

class AudioHistory {
public:
    // Offline: the whole track. `rows` is rowCount x kAudioBins, row r at firstRowTime + r / rowRate.
    // Onset lists need not be sorted; they are sorted here.
    static AudioHistory whole(double rowRate, double firstRowTime, std::vector<float> rows,
                              std::array<std::vector<AudioOnset>, kOnsetSources> onsets);
    // Live: empty, filled by appendLive / addLiveOnset.
    static AudioHistory livePlaceholder(double rowRate);

    // Live only. `time` is the analysed second of `row`; every row between the last one appended and
    // this one is filled with `row` (the render thread sees one analysis frame per render frame, so
    // hops in between are lost -- the spectrogram is held, not invented). A time that moves backwards
    // clears the history (the input restarted).
    void appendLive(double time, std::span<const float> row);
    void addLiveOnset(OnsetSource source, double time, float strength);

    [[nodiscard]] bool live() const { return live_; }
    [[nodiscard]] double rowRate() const { return rowRate_; }
    // The history's clock at transport second `renderTime`: the transport second itself offline, the
    // newest analysed second live (the live input's clock is not the transport's).
    [[nodiscard]] double now(double renderTime) const { return live_ ? liveNow_ : renderTime; }
    // The newest row at clock `clock`, or -1 when there is none yet.
    [[nodiscard]] std::int64_t newestRow(double clock) const;
    // Bin `bin` of row `row`, 0 when the row is not held.
    [[nodiscard]] float value(std::int64_t row, int bin) const;
    // The newest <= ages.size() onsets of `source` at or before `clock`, newest first: ages in seconds
    // (clock - time) and strengths. Returns how many were written.
    int lastOnsets(OnsetSource source, double clock, std::span<float> ages, std::span<float> strengths) const;
    [[nodiscard]] const std::vector<AudioOnset>& onsets(OnsetSource source) const {
        return onsets_[static_cast<std::size_t>(source)];
    }
    // Bumped whenever rows already handed out could have changed meaning (a new track, a live reset);
    // a GPU ring keyed to it is refilled.
    [[nodiscard]] std::uint64_t revision() const { return revision_; }
    [[nodiscard]] std::int64_t rowCount() const { return firstHeld_ + heldRows(); }
    [[nodiscard]] std::size_t bytes() const;

private:
    [[nodiscard]] std::int64_t heldRows() const {
        return static_cast<std::int64_t>(rows_.size() / static_cast<std::size_t>(kAudioBins));
    }
    bool live_ = false;
    double rowRate_ = 93.75;
    double firstRowTime_ = 0.0;
    std::vector<float> rows_;       // held rows, oldest first
    std::int64_t firstHeld_ = 0;    // absolute index of rows_[0]
    double liveNow_ = 0.0;
    std::array<std::vector<AudioOnset>, kOnsetSources> onsets_{};
    std::uint64_t revision_ = 1;
};

// ---- the sampling maths the GPU mirrors (shaders/fields.wgsl `audio*`) -----------------------------

// The spectrogram at `delaySeconds` behind the newest row at `clock`, at continuous bin position
// `binPos` in [0, 1] (0 = kAudioMinHz, 1 = kAudioMaxHz), bilinear in row and bin. 0 when the row is
// older than the ring or before the first row.
[[nodiscard]] float spectrumAt(const AudioHistory& audio, double clock, float delaySeconds, float binPos);
// The mean over the integer bins of [binLow, binHigh] (positions in [0, 1]) at the same delay.
[[nodiscard]] float spectrumRange(const AudioHistory& audio, double clock, float delaySeconds, float binLow,
                                  float binHigh);
// An onset response: sum over the newest kOnsetHistory onsets of strength * exp(-decay * age) *
// front, front = exp(-((distance - age * speed) / width)^2) (1 when width <= 0: a flash everywhere).
[[nodiscard]] float onsetResponse(std::span<const float> ages, std::span<const float> strengths, float distance,
                                  float speed, float width, float decay);

} // namespace avgen::spatial
