#pragma once

// Musical events on the signal bus (ADR-073).
//
// ADR-063 built the classifier and stopped there. `signals::MusicalEventDetector` recognises
// builds, breaks, drops and impacts, it is tested, and until this file nothing outside those tests
// ever called it -- structure was detected in principle and never in practice, so every scene in
// the project was still cut to a raw band level. That is the exact failure ADR-063 was written to
// end, and leaving the classifier unwired left it standing.
//
// This is the wiring, and nothing more: one signal per event kind, named `music.<name>` where
// `<name>` is whatever `signals::musicalEventName()` returns, declared momentary the way
// `beat.pulse` is so the existing attack/decay processors shape it unchanged.
//
// Three decisions carry the whole file.
//
// **The detector's clock is the analysis clock, never the render clock.** Analysis frames arrive
// at the hop rate and are a pure function of the audio; render frames are a function of how busy
// the GPU is. Feeding one MusicalFrame per *analysis* frame is what makes a 30 fps offline render
// and a 120 fps window agree about where a drop is. Feeding one per render frame would hand the
// detector a different dt and a different number of samples at every frame rate, which quietly
// undoes the seconds-based smoothing ADR-063 was careful to build.
//
// **The metre comes from the analysis frame's own beat fields, not from the engine's beat clock.**
// `Engine::updateTimeSignals` extrapolates beat phase forward by `deltaTime` between analysis
// frames, so its bar and phrase counters step at slightly different moments at different frame
// rates. `AnalysisFrame::beatCount` does not. Bars, phrases and sections are divided out of that
// count here rather than read off the bus for exactly that reason.
//
// **Events accumulate between publishes.** One render frame at 30 fps consumes about three
// analysis frames, and an impact in the first must not be overwritten by the quiet in the third --
// the same reason `Engine::update` already merges onsets across the frames it skips. `consume()`
// collects, `publish()` writes the strongest of each kind and clears.
//
// Nothing here goes near the audio thread: `consume()` runs on the render thread over a frame the
// analysis thread has already finished with, and allocates nothing -- the detector reserves its
// moment buffer once and every accumulator below is a fixed array.
//
// Implementation note: this is header-only because `src/app/*.cpp` is globbed into the application
// target only, while `avgen_tests` names the handful of app sources it compiles. Until this file
// is added to that list its definitions have to travel with its declarations or the test binary
// will not link.

#include "analysis/analyzer.hpp"
#include "signals/musical_events.hpp"
#include "signals/signal_bus.hpp"

#include <algorithm>
#include <array>
#include <span>
#include <vector>
#include <cstddef>
#include <cstdint>
#include <string>

namespace avgen::app {

class MusicRuntime {
public:
    // Room for the MusicalEvent enum to grow; the live count is discovered in declare().
    static constexpr std::size_t kMaxEvents = 16;
    // The bar the metre is divided into, matching the beat clock in Engine::updateTimeSignals.
    static constexpr std::uint32_t kBeatsPerBar = 4;
    // lastEventTime() for a kind that has not fired. Not 0.0: the detector primes on its first
    // frame and emits nothing, so no real moment can carry a time of zero, but a caller reading
    // "0.0" cannot tell "at the start" from "never" and one of them is a bug.
    static constexpr double kNever = -1.0;

    MusicRuntime() {
        ids_.fill(signals::kInvalidSignal);
        reset(); // lastTime_ has to start at kNever, and {} would start it at "fired at time zero"
    }

    // Declares music.beat, music.downbeat, ... on the bus. Idempotent, like SignalBus::declare.
    void declare(signals::SignalBus& bus) {
        count_ = 0;
        for (std::size_t i = 0; i < kMaxEvents; ++i) {
            const auto event = static_cast<signals::MusicalEvent>(i);
            const char* name = signals::musicalEventName(event);
            // musicalEventName() answers "beat" for anything it does not recognise, so the first
            // index that does not round-trip is one past the end of the enum. Discovering the
            // count this way means a twelfth event added to ADR-063's list reaches the bus without
            // anyone remembering to come back here -- and, more to the point, cannot silently
            // land on music.beat's id, which is what a hard-coded 11 would eventually have given
            // it, on a day nobody was looking.
            const auto back = signals::musicalEventFromName(name);
            if (!back || *back != event) {
                break;
            }
            ids_[i] = bus.declare(std::string("music.") + name, 0.0f, 1.0f, true);
            count_ = i + 1;
        }
    }

    // Feeds one analysis frame to the classifier. Safe to call twice with the same frame: the
    // engine hands frames over from two places (the offline catch-up loop walks every frame, then
    // publishFrame() publishes the last of them again) and duplicate delivery would otherwise
    // advance the detector's clock by zero seconds and double-count the frame's onset.
    void consume(const analysis::AnalysisFrame& frame, int phraseBars, int sectionPhrases) {
        if (haveFrame_ && frame.frameIndex == lastFrameIndex_) {
            return;
        }
        haveFrame_ = true;
        lastFrameIndex_ = frame.frameIndex;
        ++consumedFrames_;

        signals::MusicalFrame mf;
        mf.timeSeconds = frame.timeSeconds;
        mf.rms = frame.rms;
        mf.bass = band(frame, 0);
        mf.treble = band(frame, 4);
        mf.spectralCentroid = frame.centroidNorm;
        // Gated on the peak-picked onset rather than passed raw: onsetStrength is published on
        // every hop, so an untouched sustained transient sits above the impact threshold for its
        // whole decay and fires an Impact on each hop the cooldown permits.
        mf.onsetStrength = frame.onset ? frame.onsetStrength : 0.0f;
        mf.beat = frame.beat;
        mf.beatInBar = static_cast<int>(frame.beatCount % kBeatsPerBar);
        mf.barCount = frame.beatCount / kBeatsPerBar;
        mf.phraseCount = mf.barCount / static_cast<std::uint32_t>(std::max(1, phraseBars));
        mf.sectionCount = mf.phraseCount / static_cast<std::uint32_t>(std::max(1, sectionPhrases));

        // Kept, not only folded into the per-kind accumulators. Publishing needs the strongest of
        // each kind; folding a whole track into a structure needs every moment in order, and the
        // detector returns them once. Recovering them afterwards from `lastTime_` is impossible --
        // that keeps one time per kind, and a structure is made of the sequence.
        lastMoments_.clear();
        for (const signals::MusicalMoment& moment : detector_.update(mf)) {
            lastMoments_.push_back(moment);
            const auto i = static_cast<std::size_t>(moment.event);
            if (i >= kMaxEvents) {
                continue;
            }
            // Strength and firing are tracked apart. A beat during near-silence is a real beat
            // with a strength of zero, and folding the two together would drop it -- which is the
            // difference between a route that triggers on the event flag working and not.
            fired_[i] = true;
            pending_[i] = std::max(pending_[i], moment.strength);
            lastTime_[i] = moment.timeSeconds;
        }
    }

    // Writes this render frame's events and clears the accumulators. Call once per frame,
    // unconditionally: with no audio nothing was consumed, every signal goes to false, and that is
    // the answer -- silence is not an absence of an answer.
    void publish(signals::SignalBus& bus) {
        for (std::size_t i = 0; i < count_; ++i) {
            bus.setEvent(ids_[i], fired_[i], pending_[i]);
            fired_[i] = false;
            pending_[i] = 0.0f;
        }
    }

    // Forgets the piece. Called on a seek, a stop and a fresh load, where the analysis clock jumps
    // and a detector carrying the old energy history would read the discontinuity as a drop.
    void reset() {
        lastMoments_.clear();
        detector_.reset();
        fired_.fill(false);
        pending_.fill(0.0f);
        lastTime_.fill(kNever);
        haveFrame_ = false;
        lastFrameIndex_ = 0;
        consumedFrames_ = 0;
    }

    [[nodiscard]] signals::SignalId signal(signals::MusicalEvent event) const {
        const auto i = static_cast<std::size_t>(event);
        return i < count_ ? ids_[i] : signals::kInvalidSignal;
    }
    // When this kind last fired, on the analysis clock (kNever if it has not). The detector
    // exposes shortEnergy() for the same reason: somebody is going to ask why the drop landed
    // where it did, and the answer is a time, not a frame number.
    [[nodiscard]] double lastEventTime(signals::MusicalEvent event) const {
        const auto i = static_cast<std::size_t>(event);
        return i < kMaxEvents ? lastTime_[i] : kNever;
    }
    [[nodiscard]] std::size_t eventCount() const { return count_; }
    [[nodiscard]] std::uint64_t consumedFrames() const { return consumedFrames_; }
    // What the most recent `consume` classified, in the order the detector emitted it. Empty on a
    // frame that produced nothing, and on a repeated frame index.
    [[nodiscard]] std::span<const signals::MusicalMoment> lastMoments() const { return lastMoments_; }
    [[nodiscard]] const signals::MusicalEventDetector& detector() const { return detector_; }
    [[nodiscard]] signals::MusicalEventDetector& detector() { return detector_; }

private:
    static float band(const analysis::AnalysisFrame& frame, std::size_t index) {
        return index < frame.bandCount && index < analysis::kMaxBands ? frame.bands[index] : 0.0f;
    }

    // Reserved once: the detector emits at most a handful per frame and this must not allocate
    // on a frame that happens to be busy.
    std::vector<signals::MusicalMoment> lastMoments_ = [] { std::vector<signals::MusicalMoment> v; v.reserve(8); return v; }();
    signals::MusicalEventDetector detector_;
    std::array<signals::SignalId, kMaxEvents> ids_{};
    std::array<bool, kMaxEvents> fired_{};
    std::array<float, kMaxEvents> pending_{};
    std::array<double, kMaxEvents> lastTime_{};
    std::size_t count_ = 0;
    std::uint64_t lastFrameIndex_ = 0;
    std::uint64_t consumedFrames_ = 0;
    bool haveFrame_ = false;
};

} // namespace avgen::app
