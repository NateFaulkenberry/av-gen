#pragma once

// Timeline (milestone 0.8, ADR-018): keyframed automation of parameters and preset cues.
//
// A Track keys one parameter (all components, or one) against time in seconds or beats.
// Evaluation is pure (a function of the clock only) so offline renders are deterministic and
// seeking is exact. The timeline writes the *final* values of its targets after
// ParameterSet::resetFinals() and before modulation routes run, so automation is the first
// modulation layer: the user's base values (sliders, projects, presets) are never overwritten,
// and audio-driven routes still add on top of the automated value.
//
// Cues are discrete events on the same clock: at a cue's time the named preset is recalled
// (base values), optionally morphed in over `morphSeconds` from the values current at that
// moment. The Timeline only *locates* cues (`cueAt`); the engine owns the recall state.
//
// JSON (inside a project document, "timeline"):
//   { "enabled": true,
//     "tracks": [ { "target": "orb/scale", "component": -1, "timeBase": "seconds"|"beats",
//                   "mode": "replace"|"add"|"multiply", "loopLength": 0.0, "enabled": true,
//                   "keys": [ { "time": 0.0, "value": [1.0], "interp": "linear",
//                               "tangentIn": [0.0], "tangentOut": [0.0] } ] } ],
//     "cues": [ { "time": 8.0, "name": "drop", "preset": "big", "morphSeconds": 0.5,
//                 "timeBase": "seconds" } ] }
// `value`/tangents hold one entry per keyed component (1 for component >= 0 or scalar
// targets, N for vector targets). Tangents are only read for "bezier" keys.

#include "core/error.hpp"
#include "params/parameter_set.hpp"

#include <nlohmann/json_fwd.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::params {

// Interpolation from a key to the next one. Step holds the key's value; Smooth is a clamped
// Catmull-Rom (Hermite with auto tangents) through the neighbours; the eases are cubic; Bezier
// is Hermite with the explicit tangents (value units per time unit).
enum class KeyInterp : std::uint8_t { Step, Linear, Smooth, EaseIn, EaseOut, EaseInOut, Bezier };
[[nodiscard]] const char* keyInterpName(KeyInterp interp);
[[nodiscard]] std::optional<KeyInterp> keyInterpFromName(std::string_view name);

enum class TimeBase : std::uint8_t { Seconds, Beats };
[[nodiscard]] const char* timeBaseName(TimeBase base);
[[nodiscard]] std::optional<TimeBase> timeBaseFromName(std::string_view name);

// How a track's value meets the target's final value (which equals the base at that point).
enum class TrackMode : std::uint8_t { Replace, Add, Multiply };
[[nodiscard]] const char* trackModeName(TrackMode mode);
[[nodiscard]] std::optional<TrackMode> trackModeFromName(std::string_view name);

using KeyValue = std::array<float, 4>;

struct Key {
    double time = 0.0;          // seconds or beats, per the track's time base
    KeyValue value{};           // components (entries past the keyed count are ignored)
    KeyInterp interp = KeyInterp::Linear;
    KeyValue tangentIn{};       // Bezier only: slope arriving at this key
    KeyValue tangentOut{};      // Bezier only: slope leaving this key
};

// The clock a timeline is evaluated against: audio time in seconds (render time without audio)
// and musical time in beats (beat count + phase from the beat clock; 0 when no tempo is known).
struct TimelineClock {
    double seconds = 0.0;
    double beats = 0.0;
    [[nodiscard]] double at(TimeBase base) const { return base == TimeBase::Beats ? beats : seconds; }
};

struct Track {
    std::string target;         // parameter path
    int component = -1;         // -1 = all components (value[0..N)), else that component from value[0]
    TimeBase timeBase = TimeBase::Seconds;
    TrackMode mode = TrackMode::Replace;
    double loopLength = 0.0;    // > 0: time wraps modulo this length (e.g. a 4-beat pattern)
    bool enabled = true;
    // Who wrote this track. Empty means a person did, which is the case for everything the editor
    // and the sequencer produce and is why it is the default.
    //
    // It exists because ownership could not otherwise survive a save. The camera director's tracks
    // were identified purely by *target path*, and the fact that the director owned them lived in
    // one runtime bool set when Direct to Music ran. Load a directed project and that bool is
    // false: reaching for the camera no longer hands it back, and the only way out is the menu
    // item -- which is exactly the reported bug. Worse, while the bool *was* true, handing back
    // erased every track on those targets, including camera automation somebody had authored by
    // hand. A marker that travels with the track answers both.
    std::string source;         // "" = authored by hand; "director" = the camera director's
    std::vector<Key> keys;      // sorted by time (addKey keeps it so; sortKeys after manual edits)

    // Runtime (not serialised)
    IParameter* param = nullptr;

    // Inserts sorted; a key within 1e-6 of an existing time replaces it. Returns the index.
    std::size_t addKey(Key key);
    void sortKeys();
    // Number of components this track writes (1 for a single component or an unbound track).
    [[nodiscard]] std::size_t keyedComponents() const;
    // Pure evaluation. No keys: zeros. Before the first key: its value. After the last key: its
    // value, unless loopLength > 0 in which case the time wraps first.
    [[nodiscard]] KeyValue evaluate(double time) const;
    // Local (looped) time and the key span it falls in; exposed for the UI curve preview.
    [[nodiscard]] double localTime(double time) const;
    [[nodiscard]] double firstKeyTime() const;
    [[nodiscard]] double lastKeyTime() const;
};

struct Cue {
    double time = 0.0;
    std::string name;
    std::string preset;         // preset name in the project's bank (may be empty = marker only)
    double morphSeconds = 0.0;  // 0 = instant
    TimeBase timeBase = TimeBase::Seconds;
};

class Timeline {
public:
    bool enabled = true;

    // ---- tracks ----
    Track& addTrack(Track track);                 // appends (a duplicate target/component is allowed but warned)
    bool removeTrack(std::size_t index);
    [[nodiscard]] Track* findTrack(const std::string& target, int component = -1);
    [[nodiscard]] const Track* findTrack(const std::string& target, int component = -1) const;
    [[nodiscard]] std::vector<Track>& tracks() { return tracks_; }
    [[nodiscard]] const std::vector<Track>& tracks() const { return tracks_; }
    // Adds (or replaces) a key on the track for `target`/`component`, creating the track when
    // needed, with the parameter's current *base* value. Returns null when the path is unknown.
    Track* recordKey(ParameterSet& params, const std::string& target, int component, double time,
                     KeyInterp interp = KeyInterp::Linear, TimeBase base = TimeBase::Seconds);
    // True when an enabled, bound track drives this path (any component when component < 0).
    [[nodiscard]] bool isAutomated(const std::string& path, int component = -1) const;

    // ---- cues ----
    Cue& addCue(Cue cue);                         // inserted sorted by time
    bool removeCue(std::size_t index);
    [[nodiscard]] std::vector<Cue>& cues() { return cues_; }
    [[nodiscard]] const std::vector<Cue>& cues() const { return cues_; }
    void sortCues();
    struct CueState {
        int index = -1;         // latest cue at or before the clock, -1 = none yet
        float progress = 1.0f;  // morph progress 0..1 (1 when the cue has no morph)
    };
    // Pure lookup on the clock (each cue is compared in its own time base).
    [[nodiscard]] CueState cueAt(const TimelineClock& clock) const;

    // ---- evaluation ----
    // Resolves every track's target in `params`; unknown targets leave `param` null (the track
    // is kept so a later scene can bind it). Returns an error naming the unresolved targets.
    [[nodiscard]] Result<void> bind(ParameterSet& params);
    // The targets the last bind() could not resolve, deduplicated and in track order. A track that
    // names a parameter this build does not have is a feature that does nothing and says nothing,
    // and that has cost this project two features already (ADR-075, ADR-080). bind() logs it; this
    // is so the editor can *show* it, next to the tracks, for as long as it is still true.
    [[nodiscard]] const std::vector<std::string>& unboundTargets() const { return unbound_; }
    void unbind();
    // Writes the finals of every enabled, bound track. Call after ParameterSet::resetFinals()
    // and before the modulator's routes. Does nothing when `enabled` is false.
    void apply(const TimelineClock& clock) const;

    // Latest key or cue time in seconds-based tracks/cues (beat-based ones are excluded).
    [[nodiscard]] double durationSeconds() const;
    [[nodiscard]] bool empty() const { return tracks_.empty() && cues_.empty(); }
    void clear();

    // ---- JSON ----
    [[nodiscard]] nlohmann::json toJson() const;
    // Replaces the contents; nothing is mutated on failure. Call bind() afterwards.
    [[nodiscard]] Result<void> fromJson(const nlohmann::json& j);

private:
    std::vector<Track> tracks_;
    std::vector<Cue> cues_;
    std::vector<std::string> unbound_;
};

} // namespace avgen::params
