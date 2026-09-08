#pragma once

// Beat and tempo tracking (ADR-004 §revisit; research audio-analysis.md §2.6). Two paths share
// the same onset-strength input at hop rate:
//   BeatTracker      causal, for live use: tempo from an autocorrelation tempogram over a sliding
//                    window, beat phase from a comb-filter / phase-locked predictor that snaps to
//                    nearby onsets. Converges within a few seconds and follows tempo changes.
//   trackBeatsOffline  Ellis 2007 dynamic-programming beat tracker over a whole track: a global
//                    tempo estimate then beats maximising onset strength + tempo consistency.

#include <cstdint>
#include <span>
#include <vector>

namespace avgen::analysis {

struct BeatTrackerConfig {
    float minBpm = 60.0f;
    float maxBpm = 200.0f;
    float windowSeconds = 6.0f;      // autocorrelation window for tempo estimation
    float tempoUpdateSeconds = 0.5f; // how often the tempogram is re-evaluated
    float phaseLockStrength = 0.5f;  // 0 = free-running, 1 = snap fully to onsets
    float confidenceFloor = 0.15f;   // below this, tempo is reported as unknown (0)
    float preferredBpm = 120.0f;     // log-Gaussian prior centre (Ellis)
    float priorWidthOctaves = 1.0f;  // prior sigma in octaves
};

struct BeatState {
    float tempoBpm = 0.0f;       // 0 while unknown
    float confidence = 0.0f;     // 0..1
    bool beat = false;           // a beat is predicted in this hop
    float phase = 0.0f;          // 0..1 since the last beat (hop-rate)
    std::uint32_t beatCount = 0; // beats since reset
};

class BeatTracker {
public:
    BeatTracker(BeatTrackerConfig config, float hopSeconds);
    // One call per analysis hop with that hop's onset strength (>= 0) and picked-onset flag.
    BeatState push(float onsetStrength, bool onset);
    void reset();
    [[nodiscard]] const BeatState& state() const { return state_; }
    [[nodiscard]] const BeatTrackerConfig& config() const { return config_; }
    [[nodiscard]] float hopSeconds() const { return hopSeconds_; }

private:
    [[nodiscard]] float historyAt(std::size_t logicalIndex) const; // 0 = oldest kept sample
    void updateTempo();
    void initialisePhase(); // comb over the history at period_ to place the next beat

    BeatTrackerConfig config_;
    float hopSeconds_;
    BeatState state_{};
    std::vector<float> history_;             // ring of onset strengths, windowSeconds long
    std::vector<std::uint8_t> onsetHistory_; // ring of picked-onset flags, parallel to history_
    std::vector<float> scratch_;             // chronological copy of the ring for estimateTempo
    std::size_t historyHead_ = 0;            // next write position
    std::size_t historyCount_ = 0;
    std::size_t tempoUpdateHops_ = 1; // tempoUpdateSeconds in hops
    std::size_t minHistoryHops_ = 1;  // history required before the first estimate
    std::size_t hopsSinceTempo_ = 0;
    std::size_t hopsUnconfident_ = 0; // consecutive hops with confidence below the floor
    double period_ = 0.0;             // seconds per beat (0 = unknown)
    double nextBeatTime_ = 0.0;       // seconds (hop index * hopSeconds)
    double time_ = 0.0;               // time of the hop being processed
    double lastBeatTime_ = -1.0;      // -1 until the first beat is placed
    std::uint64_t hopIndex_ = 0;
};

struct OfflineBeats {
    float tempoBpm = 0.0f;
    float confidence = 0.0f;
    std::vector<double> beatTimes; // seconds, ascending
};

// Ellis (2007) "Beat Tracking by Dynamic Programming". onsetStrength has one entry per hop.
OfflineBeats trackBeatsOffline(std::span<const float> onsetStrength, float hopSeconds,
                               const BeatTrackerConfig& config = {});

// Tempogram helper (autocorrelation of an onset-strength window with the Ellis log-Gaussian
// prior); returns (bpm, confidence 0..1). Exposed for tests.
std::pair<float, float> estimateTempo(std::span<const float> onsetStrength, float hopSeconds,
                                      const BeatTrackerConfig& config);

} // namespace avgen::analysis
