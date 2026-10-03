#pragma once

// TRIGGER: deterministic event activation (Effect Library Wave 2, shared-infrastructure.md
// "TRIGGER").
//
// **The contract is one pure function.** `TriggerClock::lastTriggers(trigger, owner, t, out)` writes
// the most recent event times at or before `t`, newest first, and nothing else. It searches BACKWARD
// over data that is itself a function of the piece, never of how the transport got to `t`:
//
//   * Beat, Onset -- the offline analysis track (`analysis::AnalysisTrack`: the Ellis beat tracker's
//     beat times, and every hop's peak-picked onset with its strength). A Beat trigger counts
//     musical beats through the engine's `analysis::Meter` (ADR-896): beat 0 is beat 1 of bar 1,
//     the beat `beat.count` calls 0 and `music.downbeat` fires on, so "every 4th beat from 0" is
//     every downbeat;
//   * MusicEvent -- the musical-event classifier walked ONCE over the whole track from its first
//     frame (the same walk the Auto-director's structure fold does), so a drop is where the piece
//     puts it, not where a playback that started mid-song happened to recognise one;
//   * TimelineMarker -- the sequence's marker list;
//   * Repeat -- the schedule `phase + k period`;
//   * Proximity -- the owner's and the other entity's positions in the HistoryBank (HIST), which is
//     recorded on the simulation grid by a play AND by a seek's replay, and carried in the ADR-700
//     checkpoints -- so the crossing a scrub finds is the one a play found;
//   * Signal (ADR-1061) -- an event signal of the bus by name. The host DERIVES its event list from the
//     piece wherever the signal is a function of it (`notes.*` from the note track, `sonic.*`/`response.*`
//     from a walk of the timbre track, `audio.onset*`/`audio.beat` from the analysis frames), so a seek
//     finds the fronts a play reaches; otherwise -- live input, or any other signal -- it RECORDS the bus's
//     events as they fire, and a backward jump forgets the ones after it.
//
// Consumers compute `age = t - t0` and keep NO state. That is what makes a shockwave scrubbed to
// second N the same shockwave a play reaches at N (ADR-091): there is no "the wave started when I
// saw the beat" anywhere, only "the latest beat at or before N was at t0".
//
// **The one exception is an EDGE**, which is by definition about the frame rather than the second: a
// particle burst is "N more particles on the frame the event happens", and a frame is an interval.
// `edgeStart()` is the start of the current frame's interval (the previous evaluated second), held by
// `bind`; a jump longer than `kMaxEdgeSeconds` (a seek, a loop, the first frame) has no interval, so
// a scrub never fires a backlog of bursts. Particles are the engine's documented seek relaxation
// already (catalog-particles.md), and the burst is only ever added to one.
//
// **Where it reaches types.** `EffectContext::triggers` points at the engine's one clock. Every type
// resolves its window through `resolveActivationWindow(activation, timing, ctx, ...)`, which answers
// the `Trigger` activation from here; a type that wants several fronts (a Shockwave's overlapping
// rings) calls `effectEventTimes`.

#include "analysis/meter.hpp"
#include "signals/signal_bus.hpp"
#include "world/effects/effect_instance.hpp"
#include "world/effects/effect_timing.hpp"

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <limits>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::analysis {
class AnalysisTrack;
}
namespace avgen::seq {
struct Marker;
}

namespace avgen::world {

class HistoryBank;
struct HistorySubscription;

// One onset of the offline track: when, and how hard (flux over its adaptive threshold; 1 = just over).
struct TriggerOnset {
    double t = 0.0;
    float strength = 0.0f;
};
// One recognised musical moment. `event` is a `signals::MusicalEvent`.
struct TriggerMoment {
    double t = 0.0;
    std::uint8_t event = 0;
};
struct TriggerMarker {
    double t = 0.0;
    std::string name;
};

class TriggerClock {
public:
    // A frame interval longer than this is a jump, not a frame: no edge fires across it.
    static constexpr double kMaxEdgeSeconds = 0.25;

    // Points the clock at this frame's sources. Cheap when nothing changed: the analysis-derived
    // lists are rebuilt only when the track changes, the markers only when they differ. `seconds` is
    // the frame's transport second; a new value moves `edgeStart` to the previous one (see the
    // header). Called by the host once or more per frame -- repeated calls with the same second are
    // the same frame.
    void bind(const analysis::AnalysisTrack* track, std::span<const seq::Marker> markers,
              const HistoryBank* history, double seconds, const analysis::Meter& meter = {});

    // Direct setters, for a host that has no track (a test, a tool) -- and what `bind` itself uses.
    // `downbeat` is the index of the beat that is beat 1 of bar 1 (`analysis::Meter::downbeat`).
    void setBeats(std::span<const double> ascending, int downbeat = 0);
    void setOnsets(std::span<const TriggerOnset> ascending);
    void setMusicEvents(std::span<const TriggerMoment> ascending);
    void setMarkers(std::span<const TriggerMarker> markers); // any order; sorted here
    void setHistory(const HistoryBank* history) { history_ = history; }
    void setFrame(double seconds); // the edge bookkeeping `bind` does, alone

    // THE contract. Up to `out.size()` event times <= `t`, newest first; returns how many. Pure: a
    // function of (trigger, owner, t) and the bound sources only -- the same arguments give the same
    // answer whatever was asked before. `owner` is the owner's node name (Proximity measures from it).
    std::size_t lastTriggers(const Trigger& trigger, std::string_view owner, double t, std::span<double> out) const;

    // Why a trigger has fired NOTHING yet, as a sentence for the panel (the reason under a Dormant
    // badge), or null when its source has events and simply none has come yet.
    [[nodiscard]] const char* silence(const Trigger& trigger, std::string_view owner) const;

    // The start of the current frame's interval: an edge at t0 belongs to this frame when
    // edgeStart() < t0 <= seconds. Equal to the frame's second after a jump (no interval).
    [[nodiscard]] double edgeStart() const { return edgeStart_; }
    [[nodiscard]] double seconds() const { return seconds_; }

    [[nodiscard]] std::span<const double> beats() const { return beats_; }
    [[nodiscard]] std::span<const TriggerOnset> onsets() const { return onsets_; }
    [[nodiscard]] std::span<const TriggerMoment> musicEvents() const { return moments_; }
    [[nodiscard]] std::span<const TriggerMarker> markers() const { return markers_; }

    // ---- ADR-1061: Signal triggers ----
    // The names asked for that the host has not yet derived or chosen to record. Asking `lastTriggers` or
    // `silence` about a Signal trigger is what puts its name here.
    [[nodiscard]] std::vector<std::string> pendingSignals() const;
    // The host's answer for one name: its whole event list as a function of the piece (ascending), or "record it".
    void setDerivedSignal(const std::string& name, std::vector<TriggerOnset> ascending);
    void setRecordedSignal(const std::string& name);
    // Forgets every derivation and recording (a new piece, live input turned on or off); names stay wanted.
    void resetSignals();
    // Appends this frame's events of every recorded name (call once per frame, after the bus is published).
    void recordSignals(const signals::SignalBus& bus, double t);
    struct SignalEvents {
        std::vector<TriggerOnset> events;
        bool derived = false;
        bool resolved = false; // the host has derived it or chosen to record it
        signals::SignalId id = signals::kInvalidSignal;
    };
    static constexpr std::size_t kMaxRecordedEvents = 4096;

private:
    std::size_t proximity(const Trigger& trigger, std::string_view owner, double t, std::span<double> out) const;

    std::vector<double> beats_;
    int downbeat_ = 0; // ADR-896: beats_[downbeat_] is musical beat 0
    std::vector<TriggerOnset> onsets_;
    std::vector<TriggerMoment> moments_;
    std::vector<TriggerMarker> markers_;
    const HistoryBank* history_ = nullptr;

    // What `bind` last derived the analysis lists from, so it re-derives only on a change.
    const analysis::AnalysisTrack* boundTrack_ = nullptr;
    std::size_t boundFrames_ = 0;
    std::size_t boundBeats_ = 0;
    analysis::Meter boundMeter_{0, 0, 0, 0}; // never a real meter, so the first bind derives

    [[nodiscard]] const SignalEvents& signalEvents(const std::string& name) const;
    mutable std::map<std::string, SignalEvents, std::less<>> signals_; // mutable: asking registers the name
    double lastRecorded_ = std::numeric_limits<double>::quiet_NaN();

    double seconds_ = 0.0;
    double edgeStart_ = 0.0;
    bool haveFrame_ = false;
};

// ---- helpers every consumer shares -------------------------------------------------------------

// The event times an instance's FRONTS start at, newest first, all <= ctx.seconds: for a Trigger
// activation its last triggers (none past the lifetime, when it has one); for any other activation
// the start of its current window (after the delay), and every `repeatSeconds` after that. What a
// type with several overlapping fronts (Shockwave, Ripple) draws one front per.
std::size_t effectEventTimes(const EffectInstance& effect, const EffectContext& ctx, std::span<double> out);

// True when a Trigger-activated instance's source fires inside the current frame's interval
// (`edgeStart`, `seconds`]. Always false for other activations and without a clock.
[[nodiscard]] bool effectTriggerEdge(const EffectInstance& effect, const EffectContext& ctx);

// The reason a Trigger-activated instance is Dormant, for the panel: "no audio is analysed, so ..."
// or "waiting for the first ...". Null for an instance that is not Trigger-activated.
[[nodiscard]] const char* effectTriggerDormancy(const EffectInstance& effect, const EffectContext& ctx);

// What an owner's history must hold for these instances: the owner AND the other entity of every
// Proximity trigger (as deep as the instance looks back), and the owner of every distortion type
// that reads its past path (a Shockwave released where the owner WAS, a Velocity Distortion's wake).
// Appended to `wanted`; the HistoryBank merges duplicates, the deeper winning.
void appendEffectHistoryNeeds(std::span<const EffectInstance> effects, std::vector<HistorySubscription>& wanted);

// The `trigger` block of an instance's JSON. Every field is written, so a round trip is lossless;
// an unknown source or a malformed field is refused by name (ADR-441: no aliases).
[[nodiscard]] nlohmann::json triggerToJson(const Trigger& trigger);
[[nodiscard]] Result<Trigger> triggerFromJson(const nlohmann::json& j);

} // namespace avgen::world
