#pragma once

// Scene states and world macros (ADR-031). Both are thin layers over existing mechanisms:
//   * a State = a preset name + transition settings + triggers; a transition is a preset morph
//     (params::applyPresetBlend, the same operation timeline cues use) driven by the engine
//     clock with an easing curve and optional beat/bar quantisation;
//   * a WorldMacro = a macro knob ("macros/<knob>", signal "macro.<knob>") plus a target map;
//     applying it writes ordinary ModRoutes (source "macro.<knob>", remap min..max, curve) into
//     the modulator, so every other layer (timeline, OSC, presets, offline) sees plain routes.
// Nothing here evaluates parameters itself.

#include "core/error.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "params/preset.hpp"
#include "params/processor.hpp"
#include "signals/signal_bus.hpp"

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::app {

enum class TransitionEasing : std::uint8_t { Linear, Smooth, EaseIn, EaseOut, EaseInOut, Bezier };
[[nodiscard]] const char* transitionEasingName(TransitionEasing easing);
[[nodiscard]] std::optional<TransitionEasing> transitionEasingFromName(std::string_view name);
enum class TransitionQuantize : std::uint8_t { None, Beat, Bar };
[[nodiscard]] const char* transitionQuantizeName(TransitionQuantize q);
[[nodiscard]] std::optional<TransitionQuantize> transitionQuantizeFromName(std::string_view name);
// Eased progress in [0, 1]; Bezier uses (c0, c1) as the y of the inner control points.
[[nodiscard]] float easeTransition(TransitionEasing easing, float t, float c0 = 0.42f, float c1 = 0.58f);

struct StateTransition {
    double seconds = 2.0;
    TransitionEasing easing = TransitionEasing::Smooth;
    float bezierC0 = 0.42f;
    float bezierC1 = 0.58f;
    TransitionQuantize quantize = TransitionQuantize::None; // start on the next beat/bar
};

// When a trigger fires the machine goes to the state that owns it (`target` overrides).
enum class TriggerKind : std::uint8_t {
    Manual,   // only go()/OSC
    Beat,     // every `every`-th beat.pulse event
    Bar,      // every `every`-th bar (beat.bar phase wrap)
    Onset,    // audio.onset events with audio.onsetStrength >= threshold
    Signal,   // `signal` rises through `threshold`
    Macro,    // "macro.<signal>" rises through `threshold` (falls when `falling`)
    Cue,      // reserved: cues address states through their preset
};
[[nodiscard]] const char* triggerKindName(TriggerKind kind);
[[nodiscard]] std::optional<TriggerKind> triggerKindFromName(std::string_view name);

struct StateTrigger {
    TriggerKind kind = TriggerKind::Manual;
    std::string signal;          // Signal / Macro (knob name)
    float threshold = 0.5f;
    bool falling = false;        // Signal/Macro: fire on the falling crossing instead
    int every = 1;               // Beat / Bar
    std::string fromState;       // only when the current state has this name ("" = any)
    std::string target;          // state to go to ("" = the owning state)
    bool enabled = true;
};

struct SceneState {
    std::string name;
    std::string preset;          // preset in the project's bank
    StateTransition transition;
    std::vector<StateTrigger> triggers;
};

struct BeatInfo {
    bool beatPulse = false;
    float barPhase = 0.0f;
    bool onset = false;
    float onsetStrength = 0.0f;
    double beatSeconds = 0.5;    // seconds per beat (for quantised starts)
    double barSeconds = 2.0;
};

class StateMachine {
public:
    std::vector<SceneState> states;
    std::string initial;         // applied instantly on reset() when set

    [[nodiscard]] const SceneState* find(std::string_view name) const;
    [[nodiscard]] SceneState* find(std::string_view name);
    // Starts a transition to `name` (from the current parameter bases). Returns false when the
    // state or its preset is unknown. `instant` skips the transition.
    bool go(std::string_view name, params::ParameterSet& params, const params::PresetBank& presets, bool instant = false);
    // Evaluates triggers against the bus, then advances the running transition (writes bases).
    void update(double seconds, double dt, const signals::SignalBus& bus, const BeatInfo& beat,
                params::ParameterSet& params, const params::PresetBank& presets);
    void reset(params::ParameterSet& params, const params::PresetBank& presets);

    [[nodiscard]] const std::string& current() const { return current_; }
    [[nodiscard]] const std::string& pending() const { return pending_; }
    [[nodiscard]] float progress() const { return progress_; }
    [[nodiscard]] bool transitioning() const { return !pending_.empty(); }
    [[nodiscard]] int currentIndex() const;
    [[nodiscard]] bool empty() const { return states.empty(); }

    [[nodiscard]] nlohmann::json toJson() const;
    Result<void> fromJson(const nlohmann::json& j);
    [[nodiscard]] Result<void> validate(const params::PresetBank& presets) const;

private:
    std::string current_;
    std::string pending_;
    params::Preset from_;
    double startSeconds_ = 0.0;
    double waitUntil_ = -1.0;    // quantised start (< 0 = none)
    float progress_ = 1.0f;
    // trigger edge state
    int beatCounter_ = 0;
    int barCounter_ = 0;
    float lastBarPhase_ = 0.0f;
    std::vector<float> lastSignal_; // per (state, trigger) flattened
    std::string pendingQuantized_;  // go() waiting for the next beat/bar
    double lastSeconds_ = 0.0;
    void beginTransition(const SceneState& state, params::ParameterSet& params, const params::PresetBank& presets,
                         double seconds, bool instant);
};

// ---- world macros ----------------------------------------------------------------------------

struct WorldMacroTarget {
    std::string path;            // parameter path
    int component = -1;
    float min = 0.0f;            // knob 0 -> min, knob 1 -> max (after the curve)
    float max = 1.0f;
    params::CurveType curve = params::CurveType::Linear;
    float curveAmount = 1.0f;
    params::ModOp op = params::ModOp::Add; // Add offsets the base; Replace sets it
};

struct WorldMacro {
    std::string name;            // knob name ("macros/<name>", signal "macro.<name>")
    std::string label;           // UI label ("WORLD ENERGY")
    float defaultValue = 0.5f;
    std::vector<WorldMacroTarget> targets;
    [[nodiscard]] nlohmann::json toJson() const;
    static Result<WorldMacro> fromJson(const nlohmann::json& j);
    // The routes this macro expands to (source "macro.<name>", remap min..max, curve, op).
    [[nodiscard]] std::vector<params::ModRoute> routes() const;
    [[nodiscard]] static bool isGenerated(const params::ModRoute& route, std::string_view macroName);
};
// Removes the routes previously generated for `macro` from `modulator` and adds the current ones.
void applyWorldMacro(const WorldMacro& macro, params::Modulator& modulator);
void removeWorldMacroRoutes(std::string_view macroName, params::Modulator& modulator);

} // namespace avgen::app
