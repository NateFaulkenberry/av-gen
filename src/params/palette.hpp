#pragma once

// ADR-1043: a project palette -- named palette states, interpolated perceptually, written into the
// parameters that make up the look (materials, lights, fog, emissive, grade).
//
// JSON (a project's top-level "palette"):
//   { "states": [ { "name": "dusk", "colors": { "wall": [r, g, b], "fog": [r, g, b], ... },
//                   "scalars": { "glow": 2.0, ... } }, ... ],
//     "bindings": [ { "role": "wall", "target": "sdf/world/material/baseColor",
//                     "gain": 1.0, "mode": "replace" | "multiply", "component": -1 } ],
//     "position": 0.0, "saturation": 1.0, "value": 1.0 }
//
// Colours are linear RGB, the space every colour parameter here is in. Three parameters drive it:
//   palette/position    a float through the ordered states (1.25 = a quarter of the way from state 1
//                       to state 2), clamped to [0, states - 1];
//   palette/saturation  a chroma multiplier in OKLCh (0 = grey, 1 = as authored, > 1 = richer);
//   palette/value       a lightness multiplier in OKLab.
// Colours blend along a straight line in OKLab (Ottosson 2020), so a teal-to-amber change passes
// through a quieter middle instead of sweeping round the hue wheel. Scalars blend linearly.
//
// Frame order: after the timeline and the routes (Engine::update), so the timeline keys the three
// parameters and a route may move them; the palette then REPLACES (or multiplies) its targets'
// finals. It is a pure function of this frame's finals, so it is seek-exact whenever they are.
//
// ADR-1420: an optional `selector` block replaces `palette/position` with a SELECTED state reached by
// a direct OKLab blend from wherever the palette stands (never through the states between):
//   "selector": { "select": 0, "transition": 4.0, "lock": false,
//                 "advance": { "signal": "beat.phrasePulse", "threshold": 0.5, "hold": 16.0,
//                              "order": [0, 3, 1, 2] } }
// Parameters (registered only with a selector): palette/select (the target state; a change retargets
// from the current blend, so nothing jumps), palette/transition (seconds), palette/lock (> 0.5: no
// automatic advance), palette/hold (the least time between two automatic changes). `advance` steps
// to the next state of `order` (default 0..n-1) when its bus signal reaches `threshold` (an event
// signal: fires with that strength; a continuous one: crosses it upward) and `hold` seconds have
// passed since the last change. The selector keeps state between frames: `step` advances it once a
// frame, before `apply`. A seek replays it with the routes (ADR-901) when the advance signal is one
// the replay rebuilds; otherwise a jump in time restarts it at the selected state (see ADR-1420).

#include "core/error.hpp"
#include "params/parameter_set.hpp"
#include "signals/signal_bus.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace avgen::params {

struct PaletteState {
    std::string name;
    std::map<std::string, glm::vec3> colors;
    std::map<std::string, float> scalars;
};

enum class PaletteMode : std::uint8_t { Replace, Multiply };

struct PaletteBinding {
    std::string role;
    std::string target;
    int component = -1; // -1: every component (a colour role into a vec3); >= 0: that component only
    float gain = 1.0f;
    PaletteMode mode = PaletteMode::Replace;
};

// ADR-1420: the selector's authored settings (the parameters' defaults and the advance rule).
struct PaletteSelector {
    bool enabled = false;      // a `selector` block was authored
    int select = 0;
    float transition = 4.0f;   // seconds
    bool lock = false;
    float hold = 16.0f;        // seconds between automatic changes
    std::string advanceSignal; // empty: no automatic advance
    float threshold = 0.5f;
    std::vector<int> order;    // empty: 0..n-1
};

// ADR-1420: what the selector carries between frames (captured and restored by a seek's replay).
struct PaletteSelectorState {
    int target = 0;
    std::map<std::string, glm::vec3> fromLab;   // the blend's start, per colour role (OKLab, before sat/value)
    std::map<std::string, float> fromScalar;
    double start = 0.0;     // when the current blend began
    float duration = 0.0f;  // its length (0: arrived)
    double lastChange = -1.0e9;
    double lastTime = -1.0; // the last stepped instant (< 0: never stepped)
    float lastSignal = 0.0f;
    int lastSelect = -1;    // the palette/select value last seen
};

class Palette {
public:
    std::vector<PaletteState> states;
    std::vector<PaletteBinding> bindings;
    float position = 0.0f;   // the parameters' defaults
    float saturation = 1.0f;
    float value = 1.0f;
    PaletteSelector selector; // ADR-1420

    [[nodiscard]] bool empty() const { return states.empty(); }
    [[nodiscard]] nlohmann::json toJson() const;
    static Result<Palette> fromJson(const nlohmann::json& j);

    // ADR-1420. Once a frame, before `apply`: reads palette/select, /transition, /lock, /hold (their finals, or their
    // bases when `bases` -- a seek's replay, where no frame has rebuilt the finals) and the advance signal on `bus`
    // at transport second `time`. A step backwards in time, or forwards by more than a second, restarts at the
    // selected state. Does nothing without a selector.
    void step(double time, const signals::SignalBus& bus, bool bases = false);
    // ADR-1420: restart at the selected state (a seek's replay begins here; tests).
    void resetSelector();
    [[nodiscard]] const PaletteSelectorState& selectorState() const { return selectorState_; }
    void restoreSelectorState(const PaletteSelectorState& s) { selectorState_ = s; }
    // ADR-1420: the state the selector is heading to and how far it has come (0..1, eased), at `time`.
    [[nodiscard]] int selectedState() const { return selectorState_.target; }
    [[nodiscard]] float selectorProgress(double time) const;
    // ADR-1420: the selector's colour of a role / scalar at `time` (with saturation and value applied).
    [[nodiscard]] glm::vec3 selectorColor(const std::string& role, double time, float saturation, float value) const;
    [[nodiscard]] float selectorScalar(const std::string& role, double time) const;
    // ADR-1420: whether the selector's advance signal is `name`'s (a replay asks whether it can rebuild it).
    [[nodiscard]] const std::string& advanceSignal() const { return selector.advanceSignal; }

    // Registers palette/position, palette/saturation, palette/value (idempotent); removes them.
    void attach(ParameterSet& params);
    void detach(ParameterSet& params);
    // Writes every binding's target final from this frame's palette parameters. Unknown targets are
    // skipped (reported once by `unresolved`).
    void apply(ParameterSet& params);
    [[nodiscard]] const std::vector<std::string>& unresolved() const { return unresolved_; }

    // The blended colour of a role / the blended scalar (pure). A role a state does not define takes
    // the nearest state's that does; a role no state defines is black / 0.
    [[nodiscard]] glm::vec3 color(const std::string& role, float position, float saturation, float value) const;
    [[nodiscard]] float scalar(const std::string& role, float position) const;

    // Linear sRGB <-> OKLab (exposed for tests).
    static glm::vec3 toOklab(const glm::vec3& linear);
    static glm::vec3 fromOklab(const glm::vec3& lab);

    // The parameters (nullptr before attach).
    Parameter<float>* positionParam = nullptr;
    Parameter<float>* saturationParam = nullptr;
    Parameter<float>* valueParam = nullptr;
    Parameter<float>* selectParam = nullptr;     // ADR-1420 (null without a selector)
    Parameter<float>* transitionParam = nullptr;
    Parameter<float>* lockParam = nullptr;
    Parameter<float>* holdParam = nullptr;

private:
    void retarget(int target, double time, float duration);
    [[nodiscard]] glm::vec3 stateLab(int state, const std::string& role) const;
    [[nodiscard]] float stateScalar(int state, const std::string& role) const;
    [[nodiscard]] int selectValue(bool bases) const;
    std::vector<std::string> unresolved_;
    PaletteSelectorState selectorState_;
    double applyTime_ = 0.0; // the instant `step` last saw (what `apply` blends at)
    std::optional<signals::SignalId> advanceId_;
    std::size_t advanceBusSize_ = 0;
};

} // namespace avgen::params
