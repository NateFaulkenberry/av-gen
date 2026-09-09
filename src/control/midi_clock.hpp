#pragma once

// MIDI clock as a tempo source (ADR-021 follow-up): consumes system real-time messages (Clock =
// 24 pulses per quarter note, Start, Continue, Stop) and produces a tempo, a beat phase 0..1, a
// beat count and a running flag. The tempo is a median-filtered estimate over the last 24..48
// tick intervals: the median of the window gates each new interval (a dropped or doubled tick
// is far from it and is discarded; a run of gated intervals is a real tempo change and flushes
// the window), and the tempo is the mean of the accepted intervals, which telescopes to the span
// of the window so per-tick jitter cancels instead of accumulating. Two time bases are kept
// apart: message timestamps measure tick intervals (a message without one uses the frame time
// passed to feed()), and the frame time extrapolates the phase between ticks.

#include "control/midi.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace avgen::control {

class MidiClockTracker {
public:
    static constexpr int kTicksPerBeat = 24;
    static constexpr std::size_t kMaxIntervals = 48;
    static constexpr std::size_t kMinIntervals = 24; // until then the tempo is a plain mean
    // Without a tick for this long the clock is considered stopped (device gone).
    static constexpr std::uint64_t kTimeoutNs = 2'000'000'000ull;
    static constexpr std::size_t kGateMinIntervals = 8; // the median gate needs some history
    static constexpr int kTempoJumpRun = 4;             // gated intervals in a row = tempo change

    // Feeds one message; anything but Clock/Start/Continue/Stop is ignored. `frameNs` is the
    // engine's frame time (used as the timestamp when the message carries none, and as the base
    // for phase extrapolation). Returns true when the message was a clock message.
    bool feed(const MidiMessage& message, std::uint64_t frameNs);
    // Per frame: extrapolates the phase to `frameNs`, applies the timeout, latches the beat event.
    void advance(std::uint64_t frameNs);
    void reset();

    [[nodiscard]] bool running() const { return running_; }
    [[nodiscard]] double bpm() const { return bpm_; }          // 0 until two ticks were seen
    [[nodiscard]] double beatPhase() const { return phase_; }  // 0..1, extrapolated per frame
    [[nodiscard]] std::uint32_t beatCount() const { return static_cast<std::uint32_t>(ticks_ / kTicksPerBeat); }
    [[nodiscard]] bool beatEvent() const { return beatEvent_; } // a beat boundary passed since the last advance()
    // Clocks received since Start (the first one is tick 0, the downbeat).
    [[nodiscard]] std::uint64_t tickCount() const { return ticks_ + (haveTick_ ? 1 : 0); }
    [[nodiscard]] bool hasTempo() const { return bpm_ > 0.0; }

private:
    void onTick(std::uint64_t stampNs, std::uint64_t frameNs);
    void acceptInterval(std::uint64_t intervalNs);
    void recomputeTempo();

    bool running_ = false;
    bool started_ = false;      // a Start/Continue was seen (clocks before it free-run the clock)
    bool pendingBeat_ = false;
    bool beatEvent_ = false;
    std::uint64_t ticks_ = 0;       // index of the last received clock since Start (0 = downbeat)
    bool haveTick_ = false;         // a clock was received since Start
    std::uint64_t lastStampNs_ = 0;   // message time base
    std::uint64_t lastTickFrameNs_ = 0; // frame time base
    bool haveLastTick_ = false;
    std::vector<std::uint64_t> intervals_; // ring of recent tick intervals (ns)
    std::size_t intervalCursor_ = 0;
    int rejectedRun_ = 0;
    double medianIntervalNs_ = 0.0;
    double meanIntervalNs_ = 0.0;
    double bpm_ = 0.0;
    double phase_ = 0.0;
};

} // namespace avgen::control
