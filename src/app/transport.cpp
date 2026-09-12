#include "app/transport.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace avgen::app {
namespace {

// A position is "the same" when it differs by less than this. A microsecond is far below one sample
// at any rate this engine will see and far above the rounding of a double over a long piece, so it
// separates "the user moved the playhead" from "the arithmetic wobbled".
constexpr double kEpsilon = 1e-6;

} // namespace

const char* transportStateName(TransportState state) {
    switch (state) {
    case TransportState::Stopped: return "stopped";
    case TransportState::Playing: return "playing";
    case TransportState::Paused: return "paused";
    }
    return "stopped";
}

const char* transportModeName(TransportMode mode) {
    return mode == TransportMode::Offline ? "offline" : "realtime";
}

// ---- FrameRate ---------------------------------------------------------------------------------

double FrameRate::fps() const {
    return denominator > 0 ? static_cast<double>(numerator) / static_cast<double>(denominator) : 0.0;
}

FrameRate FrameRate::fromFps(double fps) {
    static constexpr std::array<FrameRate, 11> kStandard{{{24000, 1001},
                                                          {24, 1},
                                                          {25, 1},
                                                          {30000, 1001},
                                                          {30, 1},
                                                          {48000, 1001},
                                                          {48, 1},
                                                          {50, 1},
                                                          {60000, 1001},
                                                          {60, 1},
                                                          {120, 1}}};
    for (const FrameRate candidate : kStandard) {
        // Within half a thousandth: 29.97 typed by a person, 29.97002997 read from a file and
        // 30000/1001 all have to arrive at the same rational, or the engine has two frame rates that
        // look identical in the UI and disagree one frame in a thousand.
        if (std::abs(candidate.fps() - fps) < 5e-4) {
            return candidate;
        }
    }
    if (!(fps > 0.0) || fps > 10000.0) {
        return FrameRate{60, 1};
    }
    // Anything else kept exactly as typed, to a thousandth.
    const auto scaled = static_cast<int>(std::llround(fps * 1000.0));
    return FrameRate{std::max(1, scaled), 1000};
}

// ---- what the host tells it ---------------------------------------------------------------------

void Transport::setDuration(double seconds) {
    const double clean = (seconds > 0.0 && std::isfinite(seconds)) ? seconds : 0.0;
    if (std::abs(clean - duration_) < kEpsilon) {
        return;
    }
    duration_ = clean;
    ++revision_;
    // A shorter piece must not leave the playhead or the loop hanging past the end of it. This is
    // the "timeline duration changed during playback" case: re-baking a sequence while it plays is
    // the normal way to work in this application, not an edge case.
    if (mode_ == TransportMode::Realtime && position_ > duration_) {
        position_ = duration_;
    }
    if (loop_.endSeconds > duration_) {
        loop_.endSeconds = duration_;
    }
    if (loop_.startSeconds > duration_) {
        loop_.startSeconds = std::max(0.0, duration_ - kMinLoopSeconds);
    }
}

void Transport::setFrameRate(FrameRate rate) {
    if (!rate.valid() || rate == frameRate_) {
        return;
    }
    frameRate_ = rate;
    ++revision_;
}

void Transport::setTempo(double bpm, int beatsPerBar) {
    const double clean = (bpm > 0.0 && std::isfinite(bpm)) ? bpm : 0.0;
    const int bars = std::clamp(beatsPerBar, 1, 32);
    if (std::abs(clean - tempoBpm_) < 1e-4 && bars == beatsPerBar_) {
        return;
    }
    tempoBpm_ = clean;
    beatsPerBar_ = bars;
    ++revision_;
}

void Transport::setMode(TransportMode mode) {
    if (mode == mode_) {
        return;
    }
    mode_ = mode;
    ++revision_;
}

// ---- the range ----------------------------------------------------------------------------------

double Transport::playStartSeconds() const {
    return loop_.usable() ? loop_.startSeconds : 0.0;
}

double Transport::playEndSeconds() const {
    if (loop_.usable()) {
        return loop_.endSeconds;
    }
    return duration_;
}

double Transport::clampToRange(double seconds) const {
    if (mode_ == TransportMode::Offline) {
        return seconds; // the fixed-step clock is the authority; a render range is not a play range
    }
    if (!std::isfinite(seconds)) {
        return position_;
    }
    // A duration of zero means "nothing has said how long this is" -- a world with no sequence and
    // no audio -- not "this piece is zero seconds long". Such a transport is unbounded: it plays
    // forward and never reaches an end, which is what the engine did before it had a transport at
    // all. Clamping to [0,0] here would pin the playhead at zero and make Play do nothing.
    if (!(duration_ > 0.0)) {
        return std::max(0.0, seconds);
    }
    return std::clamp(seconds, 0.0, duration_);
}

void Transport::moveTo(double seconds) {
    if (std::abs(seconds - position_) < kEpsilon) {
        return;
    }
    position_ = seconds;
    ++revision_;
}

// ---- commands ------------------------------------------------------------------------------------

bool Transport::play() {
    if (state_ == TransportState::Playing) {
        return false;
    }
    // At (or past) the end: start again rather than play nothing. The piece has one obvious next
    // thing to do and this is it.
    const double end = playEndSeconds();
    if (end > 0.0 && position_ >= end - kEpsilon) {
        position_ = playStartSeconds();
    } else if (position_ < playStartSeconds() - kEpsilon && loop_.usable()) {
        // Playing from before a loop is allowed -- the loop catches it at the end. Nothing to do.
    }
    state_ = TransportState::Playing;
    ++revision_;
    return true;
}

bool Transport::pause() {
    if (state_ != TransportState::Playing) {
        return false;
    }
    state_ = TransportState::Paused;
    ++revision_;
    return true;
}

bool Transport::togglePlay() {
    return state_ == TransportState::Playing ? pause() : play();
}

bool Transport::stop() {
    const double home = playStartSeconds();
    const bool moved = std::abs(home - position_) >= kEpsilon;
    const bool wasRunning = state_ != TransportState::Stopped;
    if (!moved && !wasRunning) {
        return false;
    }
    state_ = TransportState::Stopped;
    position_ = home;
    ++revision_;
    return true;
}

bool Transport::stopInPlace() {
    if (state_ == TransportState::Stopped) {
        return false;
    }
    state_ = TransportState::Stopped;
    ++revision_;
    return true;
}

bool Transport::returnToStart() {
    const double home = playStartSeconds();
    if (std::abs(home - position_) < kEpsilon) {
        return false;
    }
    position_ = home;
    ++revision_;
    return true;
}

double Transport::seek(double seconds) {
    const double before = position_;
    moveTo(clampToRange(seconds));
    if (std::abs(position_ - before) >= kEpsilon) {
        ++discontinuityRevision_;
    }
    return position_;
}

double Transport::seekFrame(std::int64_t frame) {
    return seek(secondsOfFrame(frame));
}

double Transport::seekRelative(double deltaSeconds) {
    return seek(position_ + deltaSeconds);
}

double Transport::stepFrames(std::int64_t frames) {
    // From the frame the playhead is *in*, so stepping forward from halfway through frame 7 lands on
    // frame 8 rather than on 7 again. Stepping back from exactly frame 7 lands on 6 for the same
    // reason, and that symmetry is what makes holding an arrow key walk the piece frame by frame.
    return seekFrame(frameOf(position_) + frames);
}

bool Transport::setRate(double rate) {
    const double clean = std::clamp(std::isfinite(rate) ? rate : 1.0, 0.0625, 16.0);
    if (std::abs(clean - rate_) < 1e-6) {
        return false;
    }
    rate_ = clean;
    ++revision_;
    return true;
}

bool Transport::setLoop(TransportLoop loop) {
    if (!std::isfinite(loop.startSeconds) || !std::isfinite(loop.endSeconds)) {
        return false;
    }
    if (loop.endSeconds < loop.startSeconds) {
        std::swap(loop.startSeconds, loop.endSeconds); // a range dragged right to left is a range
    }
    loop.startSeconds = std::max(0.0, loop.startSeconds);
    if (duration_ > 0.0) {
        loop.endSeconds = std::min(loop.endSeconds, duration_);
        loop.startSeconds = std::min(loop.startSeconds, duration_);
    }
    if (loop == loop_) {
        return false;
    }
    loop_ = loop;
    ++revision_;
    return true;
}

bool Transport::setLoopEnabled(bool enabled) {
    if (loop_.enabled == enabled) {
        return false;
    }
    loop_.enabled = enabled;
    ++revision_;
    return true;
}

bool Transport::clearLoop() {
    if (loop_ == TransportLoop{}) {
        return false;
    }
    loop_ = TransportLoop{};
    ++revision_;
    return true;
}

// ---- advancing --------------------------------------------------------------------------------

TransportTick Transport::wrapOrStop(double previous, double candidate) {
    TransportTick out;
    // The wrap is a *crossing*, not a comparison: it fires when the step took the playhead over the
    // loop end from at or before it. A playhead parked past the loop -- dropped there by a click on
    // the ruler, or left there when the loop was moved -- plays on to the end of the piece the way
    // it would with no loop at all, rather than being yanked backwards into a range it was never in.
    if (loop_.usable() && previous < loop_.endSeconds && candidate >= loop_.endSeconds) {
        const double start = loop_.startSeconds;
        const double span = loop_.endSeconds - start;
        // Modulo rather than "set to start": at a low frame rate, or a high rate, one step can
        // overshoot a short loop by more than its length, and a wrap that discards the overshoot
        // loses time on every lap. Ten seconds of a 0.5 s loop at 30 fps is twenty laps, and
        // twenty discarded remainders is a loop that runs measurably slow.
        const double over = std::fmod(candidate - start, span);
        candidate = start + (over < 0.0 ? over + span : over);
        out.looped = true;
        moveTo(candidate);
        out.positionSeconds = position_;
        return out;
    }
    if (duration_ > 0.0 && candidate >= duration_) {
        candidate = duration_;
        out.reachedEnd = true;
        state_ = TransportState::Paused; // parked at the end, where Play starts it again
        ++revision_;
    }
    moveTo(clampToRange(candidate));
    out.positionSeconds = position_;
    return out;
}

TransportTick Transport::advance(double wallDeltaSeconds) {
    if (state_ != TransportState::Playing || mode_ == TransportMode::Offline) {
        return TransportTick{position_, false, false};
    }
    if (!std::isfinite(wallDeltaSeconds) || wallDeltaSeconds <= 0.0) {
        return TransportTick{position_, false, false};
    }
    return wrapOrStop(position_, position_ + wallDeltaSeconds * rate_);
}

TransportTick Transport::follow(double masterSeconds) {
    if (state_ != TransportState::Playing || mode_ == TransportMode::Offline) {
        return TransportTick{position_, false, false};
    }
    if (!std::isfinite(masterSeconds)) {
        return TransportTick{position_, false, false};
    }
    return wrapOrStop(position_, masterSeconds);
}

void Transport::setOfflinePosition(double seconds) {
    if (!std::isfinite(seconds)) {
        return;
    }
    moveTo(seconds);
}

// ---- reading ------------------------------------------------------------------------------------

TransportSnapshot Transport::snapshot() const {
    TransportSnapshot out;
    out.state = state_;
    out.mode = mode_;
    out.positionSeconds = position_;
    out.durationSeconds = duration_;
    out.rate = rate_;
    out.frameRate = frameRate_;
    out.frame = frameOf(position_);
    out.tempoBpm = tempoBpm_;
    out.beatsPerBar = beatsPerBar_;
    out.loop = loop_;
    out.revision = revision_;
    return out;
}

std::int64_t Transport::frameOf(double seconds) const {
    if (!frameRate_.valid() || !std::isfinite(seconds)) {
        return 0;
    }
    // In the rational, not through the double fps: `seconds * (num/den)` at 30000/1001 rounds
    // differently from `seconds * num / den`, and the difference lands exactly on frame boundaries,
    // which is the only place it matters.
    const double frames = seconds * static_cast<double>(frameRate_.numerator) /
                          static_cast<double>(frameRate_.denominator);
    // A hair of tolerance so a position produced by `secondsOfFrame(n)` reads back as n rather than
    // n-1 when the division lost a bit. Without it, stepping forward then back does not return.
    return static_cast<std::int64_t>(std::floor(frames + 1e-9));
}

double Transport::secondsOfFrame(std::int64_t frame) const {
    if (!frameRate_.valid()) {
        return 0.0;
    }
    return static_cast<double>(frame) * static_cast<double>(frameRate_.denominator) /
           static_cast<double>(frameRate_.numerator);
}

// ---- display -------------------------------------------------------------------------------------

std::string formatClockTime(double seconds) {
    const bool negative = seconds < 0.0;
    double t = std::abs(seconds);
    const auto hours = static_cast<int>(t / 3600.0);
    t -= hours * 3600.0;
    const auto minutes = static_cast<int>(t / 60.0);
    t -= minutes * 60.0;
    // Truncated, not rounded: a display that rounds shows the next second while the playhead is
    // still in this one, and a person stepping frames watches the seconds field jump early.
    const auto whole = static_cast<int>(t);
    const auto millis = static_cast<int>((t - whole) * 1000.0);
    char buffer[32];
    if (hours > 0) {
        std::snprintf(buffer, sizeof(buffer), "%s%d:%02d:%02d.%03d", negative ? "-" : "", hours, minutes,
                      whole, millis);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%s%d:%02d.%03d", negative ? "-" : "", minutes, whole, millis);
    }
    return buffer;
}

std::string formatTimecode(double seconds, FrameRate rate) {
    if (!rate.valid()) {
        return formatClockTime(seconds);
    }
    const bool negative = seconds < 0.0;
    const double t = std::abs(seconds);
    // Counted in frames throughout rather than in seconds and then frames: at 29.97 the second
    // boundary and the frame boundary are different places, and splitting on seconds first puts the
    // last frame of each second in the wrong second.
    const auto totalFrames = static_cast<std::int64_t>(
        std::floor(t * static_cast<double>(rate.numerator) / static_cast<double>(rate.denominator) + 1e-9));
    // The label's frames per second: 30 for 29.97, which is what non-drop timecode counts in.
    const auto perSecond = static_cast<std::int64_t>(
        std::llround(static_cast<double>(rate.numerator) / static_cast<double>(rate.denominator)));
    const std::int64_t safe = std::max<std::int64_t>(1, perSecond);
    const std::int64_t frames = totalFrames % safe;
    const std::int64_t totalSeconds = totalFrames / safe;
    char buffer[40];
    std::snprintf(buffer, sizeof(buffer), "%s%02lld:%02lld:%02lld:%02lld", negative ? "-" : "",
                  static_cast<long long>(totalSeconds / 3600), static_cast<long long>((totalSeconds / 60) % 60),
                  static_cast<long long>(totalSeconds % 60), static_cast<long long>(frames));
    return buffer;
}

std::string formatBarsBeats(double seconds, double tempoBpm, int beatsPerBar) {
    if (!(tempoBpm > 0.0) || beatsPerBar < 1) {
        return {};
    }
    const double beats = seconds * tempoBpm / 60.0;
    const auto whole = static_cast<std::int64_t>(std::floor(beats));
    const std::int64_t bar = whole / beatsPerBar;
    const std::int64_t beat = whole % beatsPerBar;
    char buffer[32];
    // One-based, the way every sequencer numbers bars: nobody counts from bar zero out loud.
    std::snprintf(buffer, sizeof(buffer), "%lld.%lld", static_cast<long long>(bar + 1),
                  static_cast<long long>(beat + 1));
    return buffer;
}

} // namespace avgen::app
