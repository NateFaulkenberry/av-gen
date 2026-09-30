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

#include "core/error.hpp"
#include "params/parameter_set.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>

#include <map>
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

class Palette {
public:
    std::vector<PaletteState> states;
    std::vector<PaletteBinding> bindings;
    float position = 0.0f;   // the parameters' defaults
    float saturation = 1.0f;
    float value = 1.0f;

    [[nodiscard]] bool empty() const { return states.empty(); }
    [[nodiscard]] nlohmann::json toJson() const;
    static Result<Palette> fromJson(const nlohmann::json& j);

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

private:
    std::vector<std::string> unresolved_;
};

} // namespace avgen::params
