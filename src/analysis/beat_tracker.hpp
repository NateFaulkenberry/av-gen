#pragma once

// Beat and tempo tracking (ADR-004 §revisit; research audio-analysis.md §2.6; the beat clock pass, ADR-1780..1783).
// Two paths share the same onset-strength input at hop rate and one tempo scorer:
//
//   scoreTempo         the metrical tempo score (ADR-1780): the autocorrelation of the compressed onset envelope,
//                      summed over each candidate period's first four multiples (a comb, Scheirer / Davies-Plumbley),
//                      times a log-Gaussian prior around 120 BPM. A syncopated sub-period (three sixteenths) or a
//                      half-time period has to win on the whole metrical evidence, not on one autocorrelation peak.
//   BeatTracker        causal, for live use (ADR-1781): a fast first estimate from 2 s of history, refined as the
//                      history grows to 8 s; a lock that holds unless another tempo wins twice running; a phase-locked
//                      predictor that snaps to nearby onsets; and a silence gate that stops the beat and drops the
//                      confidence within half a second, holding the phase so a short break resumes the lock.
//   trackBeatsOffline  the file grid (ADR-1782): a tempo path through the file (the scorer over 8 s windows, a
//                      Viterbi path with a cost on tempo change), then Ellis 2007 dynamic programming with that local
//                      period, so a tempo change is followed and a section is not forced onto the whole file's tempo.

#include <cstdint>
#include <span>
#include <vector>

namespace avgen::analysis {

struct BeatTrackerConfig {
    float minBpm = 60.0f;
    float maxBpm = 200.0f;
    float windowSeconds = 8.0f;          // live: the longest history the tempo reads; offline: the local tempo window
    float firstEstimateSeconds = 2.0f;   // live: history needed before the first estimate
    float lockSeconds = 4.0f;            // live: history needed before an estimate can confirm a lock
    float tempoUpdateSeconds = 0.5f;     // live: re-estimation interval while locked
    float acquireUpdateSeconds = 0.25f;  // live: re-estimation interval while acquiring
    float phaseLockStrength = 0.5f;      // 0 = free-running, 1 = snap fully to onsets
    float confidenceFloor = 0.15f;       // below this, tempo is reported as unknown (0)
    float preferredBpm = 120.0f;         // log-Gaussian prior centre (Ellis)
    float priorWidthOctaves = 1.0f;      // prior sigma in octaves
    // Live silence gate (ADR-1781): input RMS below `silenceFloorDb` (dBFS, a full-scale sine is -3). Once it has
    // lasted `silenceHoldSeconds`, the next beat that falls due in it is not emitted and closes the gate (a rest
    // between two hits ends on a hit, so it is not silence); `silenceHoldMaxSeconds` of it (at least a beat and a bit)
    // closes the gate whatever the beat. The confidence then falls with time constant `silenceFallSeconds`, and the phase is held for
    // `resumeSeconds` so the lock can resume when the music returns.
    float silenceFloorDb = -60.0f;
    float silenceHoldSeconds = 0.15f;
    float silenceHoldMaxSeconds = 0.4f;
    float silenceFallSeconds = 0.04f;
    float resumeSeconds = 8.0f;
    // Offline tempo path (ADR-1782).
    float localStepSeconds = 1.0f;       // the tempo path's step
    float tempoChangeCost = 1.5f;        // cost of a tempo change per octave, in windows of full evidence
};

struct BeatState {
    float tempoBpm = 0.0f;       // 0 while unknown or gated by silence
    float confidence = 0.0f;     // 0..1
    bool beat = false;           // a beat is predicted in this hop
    float phase = 0.0f;          // 0..1 since the last beat (hop-rate)
    std::uint32_t beatCount = 0; // beats since reset
    bool silent = false;         // the silence gate is closed (ADR-1781)
};

class BeatTracker {
public:
    BeatTracker(BeatTrackerConfig config, float hopSeconds);
    // One call per analysis hop with that hop's beat strength and low beat strength (`AnalysisFrame::beatStrength`,
    // `beatLowStrength`, >= 0), picked-onset flag and window RMS (0..1).
    BeatState push(float strength, float lowStrength, bool onset, float rms);
    void reset();
    [[nodiscard]] const BeatState& state() const { return state_; }
    [[nodiscard]] const BeatTrackerConfig& config() const { return config_; }
    [[nodiscard]] float hopSeconds() const { return hopSeconds_; }

private:
    void updateTempo();
    void initialisePhase(); // comb over the history at period_ to place the next beat
    void clearHistory();

    BeatTrackerConfig config_;
    float hopSeconds_;
    BeatState state_{};
    std::vector<float> history_;             // ring of beat strengths, windowSeconds long
    std::vector<float> lowHistory_;          // ring of low beat strengths, parallel to history_
    std::vector<std::uint8_t> onsetHistory_; // ring of picked-onset flags, parallel to history_
    std::vector<float> scratch_;             // chronological copy of the ring for the scorer
    std::size_t historyHead_ = 0;            // next write position
    std::size_t historyCount_ = 0;
    std::size_t minHistoryHops_ = 1;  // history required before the first estimate
    std::size_t hopsSinceTempo_ = 0;
    std::size_t hopsUnconfident_ = 0; // consecutive non-silent hops with confidence below the floor
    double period_ = 0.0;             // seconds per beat (0 = unknown)
    double candidatePeriod_ = 0.0;    // a different tempo that won the last estimate (hysteresis)
    double nextBeatTime_ = 0.0;       // seconds (hop index * hopSeconds)
    double time_ = 0.0;               // time of the hop being processed
    double lastBeatTime_ = -1.0;      // -1 until the first beat is placed
    bool locked_ = false;             // the period has been confirmed by a second estimate
    // The silence gate.
    std::size_t silentHops_ = 0;      // consecutive hops below the floor
    bool gated_ = false;
    double gatedSince_ = 0.0;
    float heldConfidence_ = 0.0f;     // the confidence when the gate closed, restored on a resume
    int resumeChecks_ = 0;            // onsets after the gate opened still to be checked against the held phase
    bool resuming_ = false;
    double pendingSince_ = -1.0;      // a beat fell due in silence at this time (-1: none)
    std::uint64_t hopIndex_ = 0;
};

struct OfflineBeats {
    float tempoBpm = 0.0f;   // the file's typical tempo: the median of the beat intervals
    float confidence = 0.0f;
    std::vector<double> beatTimes; // seconds, ascending
};

// The file grid: a tempo path through the file, then Ellis (2007) "Beat Tracking by Dynamic Programming" with that
// local period. `strength` has one entry per hop (`AnalysisFrame::beatStrength`); `lowStrength`, the same length or
// empty, is the low register's (`beatLowStrength`): where it is rhythmic at the path's tempo the DP places beats on it.
OfflineBeats trackBeatsOffline(std::span<const float> strength, std::span<const float> lowStrength, float hopSeconds,
                               const BeatTrackerConfig& config = {});

// The metrical tempo score over one window of onset strength; returns (bpm, confidence 0..1). Exposed for tests.
std::pair<float, float> estimateTempo(std::span<const float> onsetStrength, float hopSeconds,
                                      const BeatTrackerConfig& config);

// The local tempo of a beat grid at `seconds`: 60 over the median of the (up to) eight beat intervals around it. 0
// with fewer than two beats.
[[nodiscard]] double localBpmAt(std::span<const double> beatTimes, double seconds);

} // namespace avgen::analysis
