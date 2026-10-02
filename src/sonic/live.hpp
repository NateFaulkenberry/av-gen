#pragma once

// Live Sonic input (ADR-1025): a MIDI keyboard and an audio input driving the same Sonic Character and Musical
// Context the file path computes (ADR-1020).
//
//   audio callback -> AnalysisStream (existing SPSC ring; unchanged, allocation-free)
//        analysis thread: Analyzer -> AnalysisRunner -> LiveTimbre::onFrame (a FrameTap)
//                         -> TimbreAnalyzer::analyze, the function the file path's load-time pass runs
//                         -> TimbreSnapshot into a fixed SPSC queue (no lock, no allocation, drop-newest when full)
//        render thread:   LiveSonic::frame drains the queue and steps the SAME SonicRuntime::step once per analysis
//                         frame (the hop clock, dt = hop), then publishes sonic.* / timbre.*.
//   CoreMIDI thread -> MidiInbox (existing) -> ControlHub::update on the engine thread
//                         -> LiveSonic::midi -> LiveNotes: the note on/off into a NoteTrack on the live clock
//                         -> the SAME contextAt / eventsBetween the file path reads -> notes.*.
//
// So live and file differ only in where frames and notes come from; everything that interprets them is shared, and
// a live run over the same samples reaches the same character (tested).
//
// The live clock is host time (mach_absolute_time on macOS, the clock CoreMIDI stamps messages with), counted from
// when live input was enabled. It never jumps back, so a transport loop or seek cannot disturb it.
//
// Nothing here runs unless live input is enabled: the tap checks one atomic flag and returns.

#include "analysis/analysis_runner.hpp"
#include "control/midi.hpp"
#include "signals/signal_bus.hpp"
#include "sonic/sonic_runtime.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace avgen::sonic {

// Nanoseconds on the host clock MIDI timestamps use (mach_absolute_time on macOS; steady_clock elsewhere).
[[nodiscard]] std::uint64_t hostNowNs();

// A fixed-capacity single-producer single-consumer queue. No allocation after construction, no locks.
template <class T, std::size_t Capacity>
class SpscQueue {
    static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0, "capacity must be a power of two");

public:
    // Producer. False (and the item is not queued) when full.
    bool push(const T& item) {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        if (head - tail_.load(std::memory_order_acquire) >= Capacity) {
            return false;
        }
        items_[head & (Capacity - 1)] = item;
        head_.store(head + 1, std::memory_order_release);
        return true;
    }
    // Consumer.
    bool pop(T& out) {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) {
            return false;
        }
        out = items_[tail & (Capacity - 1)];
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }
    [[nodiscard]] std::size_t size() const {
        return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_acquire);
    }

private:
    std::array<T, Capacity> items_{};
    alignas(64) std::atomic<std::size_t> head_{0};
    alignas(64) std::atomic<std::size_t> tail_{0};
};

// One analysis frame's timbre: the compact thing the analysis thread hands the render thread (about 100 bytes).
struct TimbreSnapshot {
    TimbreFeatures features;
    double hopSeconds = 0.0;         // the analysis hop: the step's dt
    double audioSeconds = 0.0;       // the frame's position in the input stream (its window centre)
    std::uint64_t analysedNs = 0;    // host time the timbre was finished (latency accounting)
};

// The timbre stage on the analysis thread. Construct with the runner's analyzer config and install with
// `AnalysisRunner::setTap` before the runner starts; it must outlive the runner.
class LiveTimbre final : public analysis::FrameTap {
public:
    static constexpr std::size_t kQueueCapacity = 512; // about 5 s of frames at 48 kHz / 512

    explicit LiveTimbre(const analysis::AnalyzerConfig& config, TimbreConfig timbre = {});

    // Analysis thread.
    void onFrame(const analysis::AnalysisFrame& frame) override;

    // Any thread. Off (the default): onFrame returns at once and nothing is queued.
    void setEnabled(bool enabled) { enabled_.store(enabled, std::memory_order_release); }
    [[nodiscard]] bool enabled() const { return enabled_.load(std::memory_order_acquire); }

    // Render thread: the oldest queued snapshot.
    bool pop(TimbreSnapshot& out) { return queue_.pop(out); }
    [[nodiscard]] std::size_t queued() const { return queue_.size(); }

    [[nodiscard]] std::uint64_t produced() const { return produced_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t dropped() const { return dropped_.load(std::memory_order_relaxed); }
    // Wall microseconds the timbre pass takes per frame, exponentially averaged (on the analysis thread).
    [[nodiscard]] double averageMicros() const { return micros_.load(std::memory_order_relaxed); }
    [[nodiscard]] double hopSeconds() const { return hopSeconds_; }
    [[nodiscard]] std::uint32_t sampleRate() const { return sampleRate_; }

private:
    TimbreAnalyzer analyzer_;
    double hopSeconds_;
    std::uint32_t sampleRate_;
    SpscQueue<TimbreSnapshot, kQueueCapacity> queue_;
    std::atomic<bool> enabled_{false};
    std::atomic<std::uint64_t> produced_{0};
    std::atomic<std::uint64_t> dropped_{0};
    std::atomic<double> micros_{0.0};
};

// Live notes into a NoteTrack on the live clock (seconds). A held note's duration is kept at "held so far plus a
// margin" so `contextAt` counts it sounding and `eventsBetween` never sees it end early; its note-off fixes the real
// duration, and the next publish fires notes.noteOff. Notes that can no longer affect the context are dropped, so
// the track stays small however long the session runs.
//
// The MIDI 1.0 subset used: note on/off with velocity, channel and key; the sustain pedal (CC 64) defers note-offs;
// all-notes-off (CC 123) and all-sound-off (CC 120) end what is held. The note keeps channel, key, a float velocity
// and a float pitch, so MIDI 2.0 velocity and per-note pitch (MPE) fit later without a change of shape.
class LiveNotes {
public:
    // How long a finished note is kept: long enough for every window in `ContextSettings`.
    explicit LiveNotes(const ContextSettings& settings = {});

    void noteOn(double seconds, std::uint8_t channel, std::uint8_t key, float velocity);
    void noteOff(double seconds, std::uint8_t channel, std::uint8_t key);
    void sustain(double seconds, std::uint8_t channel, bool down);
    void allOff(double seconds, int channel = -1); // -1: every channel
    void clear();

    // The track as of `seconds` (at or after every event given so far): held notes extended, old notes dropped.
    const NoteTrack& at(double seconds);
    [[nodiscard]] const NoteTrack& track() const { return track_; }
    [[nodiscard]] int held() const { return static_cast<int>(open_.size()); }
    [[nodiscard]] std::uint64_t received() const { return received_; }
    [[nodiscard]] double horizon() const { return horizon_; }

    static constexpr double kOpenMargin = 1.0; // a held note ends this far past "now" until it is released
    static constexpr std::size_t kMaxNotes = 4096;

private:
    struct Open {
        std::uint32_t id = 0;
        std::uint8_t channel = 0;
        std::uint8_t key = 0;
        bool released = false; // the key is up but the pedal holds it
    };
    NoteEvent* find(std::uint32_t id);
    void end(std::uint32_t id, double seconds);

    NoteTrack track_;
    std::vector<Open> open_;
    std::array<bool, 16> pedal_{};
    std::uint32_t nextId_ = 0;
    std::uint64_t received_ = 0;
    double horizon_;
};

// The live session: owns the runtime, the notes and the status the panel shows. Engine thread only (the timbre
// queue is the one thing it shares, through LiveTimbre).
class LiveSonic {
public:
    struct Status {
        bool midiReceiving = false;   // a MIDI message in the last `kReceivingSeconds`
        bool audioReceiving = false;  // an analysed frame above -60 dBFS in the last `kReceivingSeconds`
        std::uint64_t midiMessages = 0;
        std::uint64_t notes = 0;      // note-ons received
        int held = 0;
        float lastVelocity = 0.0f;
        int lastKey = -1;
        int lastChannel = -1;
        std::uint64_t audioFrames = 0; // snapshots stepped
        std::uint64_t audioDropped = 0;
        float audioLevelDb = -120.0f;
        double timbreMicros = 0.0;
        // Latency, milliseconds, exponentially averaged: a MIDI message's host timestamp to the frame that
        // published it, and a snapshot's analysis time to the frame that stepped it.
        double midiToFrameMs = 0.0;
        double snapshotToFrameMs = 0.0;
    };
    static constexpr double kReceivingSeconds = 0.5;

    LiveSonic();

    // (Re)starts the session: the live clock's origin is now; notes, character and status are cleared.
    void begin();
    [[nodiscard]] bool running() const { return running_; }
    void end();

    // Seconds on the live clock for a host timestamp (0 = now).
    [[nodiscard]] double seconds(std::uint64_t hostNs) const;
    [[nodiscard]] double now() const { return seconds(0); }

    // A MIDI message, drained on the engine thread during the frame that will publish at `frameSeconds`. Returns
    // whether it was a message the note model uses. Its time is its own timestamp, kept inside the interval this
    // frame publishes, so every event reaches the bus exactly once.
    bool midi(const control::MidiMessage& message, double frameSeconds);

    // Once per frame, on the engine thread: steps every queued snapshot through the character, then publishes
    // sonic.*, timbre.* and notes.* at `frameSeconds`.
    void frame(const SonicSetup& setup, LiveTimbre* timbre, signals::SignalBus& bus, double frameSeconds,
               std::uint64_t frameNs);

    void declare(signals::SignalBus& bus) { runtime_.declare(bus); }
    void setSmoothing(float scale) { runtime_.setTimeScale(scale); }
    void setControls(const ResponseControls& c) { runtime_.setControls(c); } // ADR-1062
    [[nodiscard]] const SonicRuntime& runtime() const { return runtime_; }
    [[nodiscard]] LiveNotes& notes() { return notes_; }
    [[nodiscard]] Status status(double frameSeconds) const;
    // For the test: what the last `frame` stepped.
    [[nodiscard]] std::size_t lastStepped() const { return lastStepped_; }

private:
    SonicRuntime runtime_;
    LiveNotes notes_;
    bool running_ = false;
    std::uint64_t originNs_ = 0;
    double lastPublish_ = 0.0;
    double lastMidiSeconds_ = -1e9;
    double lastAudioSeconds_ = -1e9;
    std::size_t lastStepped_ = 0;
    Status status_;
};

} // namespace avgen::sonic
