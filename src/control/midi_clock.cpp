#include "control/midi_clock.hpp"

#include <algorithm>
#include <numeric>

namespace avgen::control {

void MidiClockTracker::reset() {
    running_ = false;
    started_ = false;
    pendingBeat_ = false;
    beatEvent_ = false;
    ticks_ = 0;
    haveTick_ = false;
    lastStampNs_ = 0;
    lastTickFrameNs_ = 0;
    haveLastTick_ = false;
    intervals_.clear();
    intervalCursor_ = 0;
    rejectedRun_ = 0;
    medianIntervalNs_ = 0.0;
    meanIntervalNs_ = 0.0;
    bpm_ = 0.0;
    phase_ = 0.0;
}

bool MidiClockTracker::feed(const MidiMessage& message, std::uint64_t frameNs) {
    const std::uint64_t stamp = message.timestampNs != 0 ? message.timestampNs : frameNs;
    switch (message.kind) {
    case MidiKind::Start:
        // Song position 0: the next Clock is the first tick of beat 0.
        running_ = true;
        started_ = true;
        ticks_ = 0;
        haveTick_ = false;
        phase_ = 0.0;
        pendingBeat_ = false;
        haveLastTick_ = false;
        return true;
    case MidiKind::Continue:
        running_ = true;
        started_ = true;
        haveLastTick_ = false; // the pause left a hole: do not measure across it
        return true;
    case MidiKind::Stop:
        running_ = false;
        haveLastTick_ = false;
        return true;
    case MidiKind::Clock:
        if (!started_) {
            running_ = true; // a device that only sends clocks: free-running clock
        }
        onTick(stamp, frameNs);
        return true;
    default:
        return false;
    }
}

void MidiClockTracker::onTick(std::uint64_t stampNs, std::uint64_t frameNs) {
    if (haveLastTick_ && stampNs > lastStampNs_) {
        const std::uint64_t interval = stampNs - lastStampNs_;
        // Guard against a gap (device paused without a Stop): anything longer than the timeout
        // restarts the measurement instead of poisoning the ring.
        if (interval < kTimeoutNs) {
            acceptInterval(interval);
        }
    }
    lastStampNs_ = stampNs;
    lastTickFrameNs_ = frameNs;
    haveLastTick_ = true;
    if (!running_) {
        return; // a stopped device still refines the tempo; the beat clock waits for Start
    }
    if (haveTick_) {
        ++ticks_;
    } else {
        haveTick_ = true; // tick 0: the downbeat
    }
    if (ticks_ % static_cast<std::uint64_t>(kTicksPerBeat) == 0) {
        pendingBeat_ = true;
    }
    phase_ = static_cast<double>(ticks_ % static_cast<std::uint64_t>(kTicksPerBeat)) / kTicksPerBeat;
}

void MidiClockTracker::acceptInterval(std::uint64_t intervalNs) {
    if (intervals_.size() >= kGateMinIntervals && medianIntervalNs_ > 0.0) {
        const double ratio = static_cast<double>(intervalNs) / medianIntervalNs_;
        if (ratio < 0.5 || ratio > 1.5) {
            // A dropped tick (2x) or a doubled one (0.5x): discard. Several in a row mean the
            // tempo really changed: start over from the new intervals.
            if (++rejectedRun_ < kTempoJumpRun) {
                return;
            }
            intervals_.clear();
            intervalCursor_ = 0;
        }
    }
    rejectedRun_ = 0;
    if (intervals_.size() < kMaxIntervals) {
        intervals_.push_back(intervalNs);
    } else {
        intervals_[intervalCursor_] = intervalNs;
        intervalCursor_ = (intervalCursor_ + 1) % kMaxIntervals;
    }
    recomputeTempo();
}

void MidiClockTracker::recomputeTempo() {
    if (intervals_.empty()) {
        return;
    }
    std::vector<std::uint64_t> sorted(intervals_);
    std::sort(sorted.begin(), sorted.end());
    const std::size_t n = sorted.size();
    medianIntervalNs_ = n % 2 == 1 ? static_cast<double>(sorted[n / 2])
                                   : 0.5 * (static_cast<double>(sorted[n / 2 - 1]) + static_cast<double>(sorted[n / 2]));
    // Mean of consecutive intervals = (last stamp - first stamp) / n: interior jitter cancels.
    const double sum = std::accumulate(intervals_.begin(), intervals_.end(), 0.0);
    meanIntervalNs_ = sum / static_cast<double>(n);
    if (meanIntervalNs_ > 0.0) {
        bpm_ = 60.0e9 / (meanIntervalNs_ * kTicksPerBeat);
    }
}

void MidiClockTracker::advance(std::uint64_t frameNs) {
    beatEvent_ = pendingBeat_;
    pendingBeat_ = false;
    if (!running_) {
        return;
    }
    if (haveLastTick_ && frameNs > lastTickFrameNs_) {
        const std::uint64_t since = frameNs - lastTickFrameNs_;
        if (since > kTimeoutNs) {
            running_ = false; // clock went away
            haveLastTick_ = false;
            return;
        }
        if (meanIntervalNs_ > 0.0 && haveTick_) {
            // Extrapolate inside the current tick, never past the next one.
            const double fraction = std::min(0.999, static_cast<double>(since) / meanIntervalNs_);
            const double tickInBeat = static_cast<double>(ticks_ % static_cast<std::uint64_t>(kTicksPerBeat));
            phase_ = (tickInBeat + fraction) / kTicksPerBeat;
        }
    }
}

} // namespace avgen::control
