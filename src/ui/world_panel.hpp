#pragma once

// World authoring UI (ADR-031): the layered parameter view, the world overview tree, the
// procedural inspector ("why is this moving?"), the states and world-macro panel, and the debug
// visualisation options. These are views over the one parameter set and the engine's existing
// data; nothing here owns state that the project does not.

#include "app/engine.hpp"
#include "rendering/debug_draw.hpp"
#include "ui/effects_panel.hpp"
#include "ui/ui_logic.hpp"

#include <string>
#include <vector>

namespace avgen::ui {

class WorldEditor;
class EditHistory;

// Which parameters a layer shows. Beginner: world macros, atmosphere, camera, post. Intermediate:
// + generators (procedural/*), fields, materials, deformers, particles, splines, sdf. Advanced:
// everything (graph, attributes, GPU, simulation).

// One reason a parameter is not simply its base value.
struct Influence {
    // `Entity` is the behaviour layer (ADR-088): a hover, a sway, a walk cycle, anything that
    // produces a `MotionOffset`. It was missing here until ADR-211, which meant the one panel whose
    // whole job is answering "why is this moving?" answered "nothing modulates this object; its
    // parameters are static" about a character that was visibly walking across the valley.
    enum class Kind { Route, Timeline, Cue, State, Macro, Entity, Staging } kind = Kind::Route;
    std::string source;      // signal / track / cue / state / macro / entity name
    std::string detail;      // amount, op, easing…
    float value = 0.0f;      // last contribution when known
    // Which route this is, for the ones that are routes: enough to find it again in the Modulation
    // panel. An index would go stale the moment a route above it is deleted, and this panel is
    // redrawn every frame against a list somebody may be editing in another window.
    std::string routeSource;
    std::string routeTarget;
    // Empty when this influence writes the parameter directly. Otherwise what it writes instead --
    // "the world root", "parent 'oak_3'" -- for an influence that moves this node by moving
    // something it hangs off. See the long note in `influencesOf`.
    std::string via;
};
// Everything that writes to `path`: routes (with their source and last output), timeline tracks,
// cues whose preset contains it, states whose preset contains it, world macros targeting it, the
// entity behaviours that fold a motion offset onto it, and the staging scenarios that write it
// directly (ADR-241).
//
// "Everything" is a claim this function has to keep, so the way to check it is to enumerate what
// writes a parameter's *final* rather than to reason about what feels like modulation:
// `params::modulation` (routes), `params::timeline` (tracks), `ui::edit_history` (an undo, which is
// a user action rather than an influence) and `entity::Entity` (behaviours and the action system).
// Presets reach a parameter through a cue or a state, both of which are listed. See ADR-211.
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

    // Set when somebody clicks a route in the Inspector's Influences list; the Modulation panel
    // consumes it, opens that route, scrolls to it and flashes it, then clears it (ADR-211).
    //
    // Identified by (source, target) rather than by index, because the two panels are redrawn from
    // the same live route list and an index is stale the moment a route above it is removed.
    // Cleared by the consumer rather than by a timer, so a click with the Modulation panel closed
    // is still waiting when it is opened.
    std::string focusRouteSource;
    std::string focusRouteTarget;

    // Set to scroll the Inspector so its Effects section is at the top on the next draw; cleared
    // when done. The headless capture hook uses it (`AVGEN_CAPTURE_WORLD_INSPECTOR`), because a
    // long Properties list otherwise keeps the section below the fold of a screenshot.
    bool scrollToEffects = false;

private:
    // ADR-421: why the last structural deformer edit was refused, if it was. Held rather than
    // logged, for the reason ADR-420's dead-subscription line is: a refusal an artist cannot see is
    // a refusal that looks like the button not working.
    std::string deformerStatus_;
    // ADR-702: the selected owner's effect stack. One section, whatever the owner is.
    EffectsSection effects_;

public:
    [[nodiscard]] bool wantsRouteFocus() const { return !focusRouteTarget.empty(); }

    // The tabs; each is drawn inside the caller's window/tab bar.
    void drawOverview(app::Engine& engine);
    // `history` is the editor's undo stack, for the Effects section's edits (ADR-702); null records
    // nothing. The selection decides whose effect stack the section shows: Environment -> the World,
    // a node -> that entity (a hero is a node, ADR-107), Camera -> the camera.
    void drawInspector(app::Engine& engine, EditHistory* history = nullptr);
    // ADR-421: the deformer stack of the selected procedural object, as a stack rather than as
    // eight anonymous groups of numbers. Drawn from `drawInspector`; separate so a test can ask
    // what leaves it names (`deformerRowLeaves`, in ui_logic.hpp) without an ImGui context.
    void drawDeformerStack(app::Engine& engine, const scene::ProceduralGeometry& object);
    void drawStates(app::Engine& engine);
    void drawMacros(app::Engine& engine);
    // The debug tab. `editor` is where the navigation overlay's toggles live (ADR-197) -- the
    // overlay is drawn by the viewport from `EditorVisuals`, not by the debug line layer, so its
    // switches belong to the editor rather than to `DebugViewOptions`. Passed rather than held,
    // because this panel owns no editor and a second one would be a second answer to "what is
    // selected". May be null in a session with no world editor; the tab then says so.
    void drawDebugOptions(app::Engine& engine, WorldEditor* editor = nullptr);
    // Art direction (ADR-041): the director's knobs and the shipped looks.
    void drawDirector(app::Engine& engine);
    // The staging director's live state (ADR-385), which is a different director entirely.
    void drawScenarios(app::Engine& engine);
    // The layer selector plus the filtered parameter list (used by the Parameters window).
    void drawLayerSelector();
    [[nodiscard]] bool shows(const std::string& path) const { return layerShowsPath(layer, path); }

private:
    void drawNavigationOptions(WorldEditor* editor);

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
