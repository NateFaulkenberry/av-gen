#pragma once

// The transport: one authoritative playback state and one authoritative timeline position for the
// whole application (ADR-102).
//
// ## Why this exists
//
// ADR-012 specified this class in 2026 and it was never written:
//
//     "Timeline transport. `Transport` owns play/pause/seek and the mapping between `renderTime`
//      and audio position."
//
// `Engine` absorbed the responsibility instead and delegated the state to `AudioPlayer`, so for
// three milestones "am I playing" has meant "is the audio device running". Everything followed from
// that: the project's duration was the wav's length, a sequence longer than its audio stopped at the
// end of the audio, and a project with **no** audio could not be played, paused, seeked or stopped
// at all -- it ran from process start, forever, because `updateTimelineClock` fell back to the
// free-running render clock and overwrote any seek on the next frame.
//
// So this owns the position, and audio follows it. That is the whole inversion.
//
// ## What it is not
//
// It is not a clock. `FrameClock` (core/time.hpp) still produces the frame's time, and ADR-012's
// rule that no engine code outside `RealtimeClock` reads a system clock is unchanged -- this class
// reads no clock at all. It is told how much time passed and decides what that means for the
// position, which is what makes it testable without a device, a window or a thread, and what keeps
// offline rendering deterministic.
//
// It is not an audio player, and it does not own one. Audio is a *follower*: the transport says
// where the position is and the engine tells the player to be there. The single-file player is
// today's only follower; an audio clip model (which does not exist yet -- see
// docs/architecture/transport-audit.md §3) would be a second one and would not re-open this class.
//
// ## The two times, named at last
//
// The engine has always had two, and nothing said so:
//
//   * **`FrameTime::renderTime`** -- the render clock. Drives `deltaTime` integration, ambient
//     motion, wind, water, the shader layers. Free-runs while the transport is parked, which is why
//     a world keeps breathing in the editor with nothing playing.
//   * **the transport position** -- the timeline. Drives `Timeline::apply`, cues, baked sequence
//     animation, overlays and sequence events. Moves only when the transport moves it.
//
// They are equal while the transport governs time (playing, or offline). A new subsystem that is
// *timed by the piece* reads the transport; one that is *animated continuously* reads renderTime.
//
// ImGui-free, engine-free, thread-free; see tests/unit/test_transport.cpp.

#include <cstdint>
#include <string>

namespace avgen::app {

// Playing, or not, or not and parked at the start.
//
// Deliberately three. The brief sketched `Seeking` and `Rendering` as states too: a seek is
// instantaneous here (there is no asynchronous reconstruction to wait on -- evaluation is pure in
// the position, ADR-089), so a `Seeking` state would be a value nothing could ever observe; and
// "rendering" is `TransportMode::Offline` below, because it changes how time *advances* rather than
// whether the user has asked for playback. A state nobody can see is a state that rots.
enum class TransportState : std::uint8_t { Stopped, Playing, Paused };
[[nodiscard]] const char* transportStateName(TransportState state);

// How the position advances. Realtime integrates elapsed wall time (or follows audio); Offline is
// told the position outright by a `FixedStepClock` and applies no clamping, no loop and no
// end-of-piece rule -- a render of 0..120 s against 30 s of audio renders 120 seconds, and a loop
// set for previewing must not silently become part of an export.
enum class TransportMode : std::uint8_t { Realtime, Offline };
[[nodiscard]] const char* transportModeName(TransportMode mode);

// A frame rate as the rational it actually is. 29.97 is 30000/1001 and not 29.97, and the difference
// is one frame every thousand: a double-precision 29.97 drifts a frame out over a six-minute piece,
// which is exactly the length of the pieces this engine renders.
struct FrameRate {
    int numerator = 60;
    int denominator = 1;

    [[nodiscard]] double fps() const;
    [[nodiscard]] bool valid() const { return numerator > 0 && denominator > 0; }
    // The nearest standard rate to `fps`, exactly: 23.976, 24, 25, 29.97, 30, 47.952, 48, 50, 59.94,
    // 60, 120. Anything else becomes numerator/1000 so a hand-typed 37.5 is still exact.
    [[nodiscard]] static FrameRate fromFps(double fps);
    [[nodiscard]] bool ntsc() const { return denominator == 1001; }

    friend bool operator==(const FrameRate&, const FrameRate&) = default;
};

// A play range. Disabled by default; a range shorter than this is treated as no range at all,
// because a zero-length loop is an infinite number of wraps per frame.
inline constexpr double kMinLoopSeconds = 0.01;

struct TransportLoop {
    bool enabled = false;
    double startSeconds = 0.0;
    double endSeconds = 0.0;

    [[nodiscard]] bool usable() const {
        return enabled && endSeconds - startSeconds >= kMinLoopSeconds && startSeconds >= 0.0;
    }
    friend bool operator==(const TransportLoop&, const TransportLoop&) = default;
};

// What the UI reads. One struct, taken once a frame, so a panel cannot ask three questions and get
// answers from two different moments.
struct TransportSnapshot {
    TransportState state = TransportState::Stopped;
    TransportMode mode = TransportMode::Realtime;
    double positionSeconds = 0.0;
    double durationSeconds = 0.0;
    double rate = 1.0;
    FrameRate frameRate{};
    std::int64_t frame = 0;
    double tempoBpm = 0.0;
    int beatsPerBar = 4;
    TransportLoop loop{};
    // Bumped by every change. A view that caches derived text refreshes when this moves and not
    // otherwise; a test asserts that a command that changed nothing did not bump it.
    std::uint64_t revision = 0;

    [[nodiscard]] bool playing() const { return state == TransportState::Playing; }
};

// What one advance did, so the caller can do the work only it can do -- resynchronising audio,
// entities, events and modulation at a discontinuity.
struct TransportTick {
    double positionSeconds = 0.0;
    bool looped = false;     // wrapped at the loop end this tick
    bool reachedEnd = false; // ran into the end of the piece and stopped
};

class Transport {
public:
    // ---- what the host tells it ------------------------------------------------------------
    // The project's length: the longest of the audio, the sequence and the timeline. Not the audio
    // file's length, which is what it used to be and is why a sequence longer than its wav stopped
    // early. Clamps the position and the loop into the new range.
    void setDuration(double seconds);
    [[nodiscard]] double durationSeconds() const { return duration_; }
    void setFrameRate(FrameRate rate);
    [[nodiscard]] FrameRate frameRate() const { return frameRate_; }
    // For the bars/beats display and for beat stepping when there is no analysed beat grid.
    void setTempo(double bpm, int beatsPerBar = 4);
    [[nodiscard]] double tempoBpm() const { return tempoBpm_; }
    [[nodiscard]] int beatsPerBar() const { return beatsPerBar_; }
    void setMode(TransportMode mode);
    [[nodiscard]] TransportMode mode() const { return mode_; }

    // ---- commands ---------------------------------------------------------------------------
    // Each returns whether it changed anything, so a caller can avoid doing expensive resynchronising
    // work for a command that was a no-op, and a test can assert that a second Play did nothing.
    //
    // Play from the end of the piece returns to the start first. The alternative -- refusing, or
    // playing zero seconds and stopping -- makes the most common gesture in the application ("watch
    // it again") into two gestures.
    bool play();
    bool pause();
    bool togglePlay();
    // Stop and park at the start of the play range: the loop start when a usable loop is on, else 0.
    // This is the conventional stop, and it is what `Engine::stop` already did by way of
    // `AudioPlayer::stop` (pause + seek 0).
    bool stop();
    // Stop where it stands. Pause and this differ in what the next Play does after the piece ends,
    // and in what the UI shows; they are separate commands because a person means different things.
    bool stopInPlace();
    bool returnToStart();

    // Seeks are clamped into [0, duration] in Realtime and taken literally in Offline. Returns the
    // position actually taken, which is not always the one asked for.
    double seek(double seconds);
    double seekFrame(std::int64_t frame);
    double seekRelative(double deltaSeconds);
    double stepFrames(std::int64_t frames);

    bool setRate(double rate);
    [[nodiscard]] double rate() const { return rate_; }

    bool setLoop(TransportLoop loop);
    bool setLoopEnabled(bool enabled);
    bool clearLoop();
    [[nodiscard]] const TransportLoop& loop() const { return loop_; }

    // ---- advancing ---------------------------------------------------------------------------
    // Realtime, with nothing to follow: integrates `wallDeltaSeconds * rate`.
    [[nodiscard]] TransportTick advance(double wallDeltaSeconds);
    // Realtime, following a master that knows better -- the audio play-head (ADR-012: audio is the
    // master clock while it is running). Loop and end rules still apply, and the answer says when
    // they fired so the caller can put the master back where the transport went.
    [[nodiscard]] TransportTick follow(double masterSeconds);
    // Offline: the fixed-step clock is the authority and this records what it said. No clamp, no
    // loop, no end rule -- see `TransportMode`.
    void setOfflinePosition(double seconds);

    // ---- reading ------------------------------------------------------------------------------
    [[nodiscard]] double positionSeconds() const { return position_; }
    [[nodiscard]] TransportState state() const { return state_; }
    [[nodiscard]] bool isPlaying() const { return state_ == TransportState::Playing; }
    [[nodiscard]] std::int64_t frame() const { return frameOf(position_); }
    [[nodiscard]] std::uint64_t revision() const { return revision_; }
    [[nodiscard]] TransportSnapshot snapshot() const;

    // ---- frames and time ----------------------------------------------------------------------
    // The conversion, in one place, for the display, the stepper and the exporter.
    //
    //   frame 0 is time 0; frame N starts at N * denominator / numerator
    //
    // `frameOf` floors: the frame a time is *in*, so a position one microsecond before a boundary is
    // still the earlier frame. A time exactly on a boundary is the later frame, which is what makes
    // stepping forward from frame N land on N+1 rather than oscillating.
    [[nodiscard]] std::int64_t frameOf(double seconds) const;
    [[nodiscard]] double secondsOfFrame(std::int64_t frame) const;

    // Where the piece ends for playback: the loop end when a usable loop is on, else the duration.
    [[nodiscard]] double playEndSeconds() const;
    // Where Play starts from when stopped: the loop start when a usable loop is on, else 0.
    [[nodiscard]] double playStartSeconds() const;

private:
    void moveTo(double seconds); // sets the position and bumps the revision when it actually moved
    [[nodiscard]] double clampToRange(double seconds) const;
    [[nodiscard]] TransportTick wrapOrStop(double previous, double candidate);

    TransportState state_ = TransportState::Stopped;
    TransportMode mode_ = TransportMode::Realtime;
    double position_ = 0.0;
    double duration_ = 0.0;
    double rate_ = 1.0;
    FrameRate frameRate_{};
    double tempoBpm_ = 0.0;
    int beatsPerBar_ = 4;
    TransportLoop loop_{};
    std::uint64_t revision_ = 1;
};

// ---- display ---------------------------------------------------------------------------------
//
// Every one of these takes the position and renders it; none of them keeps state. The transport bar
// switches between them and the status bar uses the same functions, so two places cannot format the
// same second differently.

// "1:23.456" -- hours only once there are hours.
[[nodiscard]] std::string formatClockTime(double seconds);
// "01:23:45:12", the frame count after the last colon. Non-drop: NTSC rates label the *frame*
// correctly and let the label drift from wall time, because this engine renders picture and has no
// broadcast timecode to match. Drop-frame is a thing to add the day something needs it, and doing it
// wrong is worse than not doing it.
[[nodiscard]] std::string formatTimecode(double seconds, FrameRate rate);
// "17.3" -- bar.beat, one-based, from a tempo. Empty when there is no tempo, because "1.1" printed
// against no known tempo is a claim rather than a reading.
[[nodiscard]] std::string formatBarsBeats(double seconds, double tempoBpm, int beatsPerBar);

} // namespace avgen::app
