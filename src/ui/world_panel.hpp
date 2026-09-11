#pragma once

// World authoring UI (ADR-031): the layered parameter view, the world overview tree, the
// procedural inspector ("why is this moving?"), the states and world-macro panel, and the debug
// visualisation options. These are views over the one parameter set and the engine's existing
// data; nothing here owns state that the project does not.

#include "app/engine.hpp"
#include "rendering/debug_draw.hpp"
#include "ui/ui_logic.hpp"

#include <string>
#include <vector>

namespace avgen::ui {

// Which parameters a layer shows. Beginner: world macros, atmosphere, camera, post. Intermediate:
// + generators (procedural/*), fields, materials, deformers, particles, splines, sdf. Advanced:
// everything (graph, attributes, GPU, simulation).

// One reason a parameter is not simply its base value.
struct Influence {
    enum class Kind { Route, Timeline, Cue, State, Macro } kind = Kind::Route;
    std::string source;      // signal / track / cue / state / macro name
    std::string detail;      // amount, op, easing…
    float value = 0.0f;      // last contribution when known
};
// Everything that writes to `path`: routes (with their source and last output), timeline tracks,
// cues whose preset contains it, states whose preset contains it, and world macros targeting it.
[[nodiscard]] std::vector<Influence> influencesOf(app::Engine& engine, const std::string& path);

// Selection shared by the overview and the inspector.
struct WorldSelection {
    // `Node` is a composition node selected in the viewport, whose parameters live under
    // "nodes/<name>/". It is separate from the kinds below because those name things *inside* a
    // flattened scene; a click in the viewport selects the node a person placed, which is the thing
    // they can move, rename and delete.
    enum class Kind { None, Node, Procedural, Field, Spline, Sdf, Particles, Material, Environment, Camera } kind = Kind::None;
    std::string name;        // object name in the scene
    [[nodiscard]] std::string parameterPrefix() const; // "procedural/<name>/", "field/<name>/", …
};

class WorldPanel {
public:
    AuthoringLayer layer = AuthoringLayer::Intermediate;
    WorldSelection selection;
    rendering::DebugViewOptions debug;
    bool showDebugOptions = false;

    // The tabs; each is drawn inside the caller's window/tab bar.
    void drawOverview(app::Engine& engine);
    void drawInspector(app::Engine& engine);
    void drawStates(app::Engine& engine);
    void drawMacros(app::Engine& engine);
    void drawDebugOptions(app::Engine& engine);
    // Art direction (ADR-041): the director's knobs and the shipped looks.
    void drawDirector(app::Engine& engine);
    // The layer selector plus the filtered parameter list (used by the Parameters window).
    void drawLayerSelector();
    [[nodiscard]] bool shows(const std::string& path) const { return layerShowsPath(layer, path); }

private:
    char macroName_[64] = "energy";
    char lookName_[64] = "My Look";
    int selectedLook_ = 0;
    std::string lastLookResult_;
    char macroTargetPath_[192] = "";
    float macroTargetMin_ = 0.0f;
    float macroTargetMax_ = 1.0f;
    char stateName_[64] = "state";
    int statePreset_ = 0;
    float stateSeconds_ = 2.0f;
};

} // namespace avgen::ui
