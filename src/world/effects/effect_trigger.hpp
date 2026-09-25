#pragma once

// TRIGGER: deterministic event activation (Effect Library Wave 2, shared-infrastructure.md
// "TRIGGER").
//
// **The contract is one pure function.** `TriggerClock::lastTriggers(trigger, owner, t, out)` writes
// the most recent event times at or before `t`, newest first, and nothing else. It searches BACKWARD
// over data that is itself a function of the piece, never of how the transport got to `t`:
//
//   * Beat, Onset -- the offline analysis track (`analysis::AnalysisTrack`: the Ellis beat tracker's
//     beat times, and every hop's peak-picked onset with its strength);
//   * MusicEvent -- the musical-event classifier walked ONCE over the whole track from its first
//     frame (the same walk the Auto-director's structure fold does), so a drop is where the piece
//     puts it, not where a playback that started mid-song happened to recognise one;
//   * TimelineMarker -- the sequence's marker list;
//   * Repeat -- the schedule `phase + k period`;
//   * Proximity -- the owner's and the other entity's positions in the HistoryBank (HIST), which is
//     recorded on the simulation grid by a play AND by a seek's replay, and carried in the ADR-700
//     checkpoints -- so the crossing a scrub finds is the one a play found.
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

#include "world/effects/effect_instance.hpp"
#include "world/effects/effect_timing.hpp"

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
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
              const HistoryBank* history, double seconds, int phraseBars = 4, int sectionPhrases = 4);

    // Direct setters, for a host that has no track (a test, a tool) -- and what `bind` itself uses.
    void setBeats(std::span<const double> ascending);
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

private:
    std::size_t proximity(const Trigger& trigger, std::string_view owner, double t, std::span<double> out) const;

    std::vector<double> beats_;
    std::vector<TriggerOnset> onsets_;
    std::vector<TriggerMoment> moments_;
    std::vector<TriggerMarker> markers_;
    const HistoryBank* history_ = nullptr;

    // What `bind` last derived the analysis lists from, so it re-derives only on a change.
    const analysis::AnalysisTrack* boundTrack_ = nullptr;
    std::size_t boundFrames_ = 0;
    std::size_t boundBeats_ = 0;
    int boundPhraseBars_ = 0;
    int boundSectionPhrases_ = 0;

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
