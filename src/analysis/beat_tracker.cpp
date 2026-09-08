// TEMPORARY STUB — replaced by the beat-tracking implementation (milestone 0.3).
#include "analysis/beat_tracker.hpp"

namespace avgen::analysis {

BeatTracker::BeatTracker(BeatTrackerConfig config, float hopSeconds) : config_(config), hopSeconds_(hopSeconds) {}
BeatState BeatTracker::push(float, bool) { return state_; }
void BeatTracker::reset() { state_ = BeatState{}; }
OfflineBeats trackBeatsOffline(std::span<const float>, float, const BeatTrackerConfig&) { return {}; }
std::pair<float, float> estimateTempo(std::span<const float>, float, const BeatTrackerConfig&) { return {0.0f, 0.0f}; }

} // namespace avgen::analysis
