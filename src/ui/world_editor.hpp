#pragma once

// The world editor: modes, the live ghost, selection, the gizmo, and every command an artist can
// issue (ADR-092, world-authoring-spec §17-§28 and §42-§44).
//
// This is the part that decides what a gesture *means*. It holds no ImGui: the viewport overlay
// hands it a cursor and some button states and then draws what it says, and the application asks it
// whether the mouse belongs to the editor before deciding whether to orbit. Keeping the decisions
// here and the pixels there is what makes "click-drag on the X arrow moves the selection four
// metres east" a thing a test can assert, in a repository where an ImGui screenshot is not
// available to anybody.
//
// **Two modes, not five.** §42 offers SELECT / PAINT / TERRAIN / WATER / CAMERA. Select and Place
// are here because they are this pass's; a Terrain and a Water mode belong to the passes that own
// terrain and water and would be empty rooms if built here. Camera is not a mode at all in this
// editor -- the viewport navigation gestures work in every mode, which is what an artist expects
// and one fewer state to be stuck in.

#include "app/edit_system.hpp"
#include "app/placement.hpp"
#include "assets/asset_library.hpp"
#include "scene/camera.hpp"
#include "ui/brush.hpp"
#include "ui/edit_history.hpp"
#include "ui/gizmo.hpp"
#include "ui/world_edit.hpp"
#include "ui/world_probe.hpp"

#include <string_view>

#include <glm/glm.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace avgen::app {
class Engine;
}

namespace avgen::ui {

// What the world editor puts on the application clipboard. Checked before Paste is offered, so a
// timeline never offers to paste a tree and this editor never offers to paste a marker.
inline constexpr std::string_view kWorldNodesClipboardType = "world/nodes";

enum class EditorMode : std::uint8_t { Select, Place };
[[nodiscard]] const char* editorModeName(EditorMode mode);

// One frame of pointer state over the canvas, in normalised device coordinates. Supplied by the
// overlay, which is the only thing that knows where the canvas is.
struct EditorInput {
    bool overCanvas = false;
    glm::vec2 ndc{0.0f};
    bool leftDown = false;
    bool leftPressed = false;
    bool leftReleased = false;
    bool shift = false;
    bool alt = false;     // bypass group selection: pick the object inside, not the group
    // The camera modifier is held, so this drag belongs to the camera and not to the editor
    // (`ui::viewportIntent`). Separate from `alt` even though the same key sets both, because they
    // are different questions: `alt` is about which object a *click* means, this is about who owns
    // a *drag*. Collapsing them would make Option-clicking inside a group start an orbit.
    bool cameraDrag = false;
    bool escape = false;  // cancel the drag in progress
};

// What the overlay draws this frame. Everything in world space; the overlay projects.
struct EditorVisuals {
    bool showGhost = false;
    bool showGizmo = false;
    GizmoFrame gizmo;
    GizmoHandle hovered = GizmoHandle::None;
    GizmoHandle dragging = GizmoHandle::None;
    std::string dragReadout;
    // What an Eraser or Replace stroke would take away, outlined so that nothing is ever removed
    // that the artist had not seen it about to remove.
    std::vector<scene::WorldBounds> erasingBoxes;
    // Box selection in progress, in NDC.
    bool boxing = false;
    glm::vec2 boxFrom{0.0f};
    glm::vec2 boxTo{0.0f};
    // What is selected, with its name: §23 asks for the object's identity on screen, and an
    // outline with no name is an outline you have to go and look up in a panel.
    struct SelectedBox {
        std::string name;
        scene::WorldBounds bounds;
        bool isGroup = false;
        std::size_t members = 0; // for a group
    };
    std::vector<SelectedBox> selectionBoxes;

    // ---- authored lights, as objects in the world ------------------------------------------------
    //
    // A light has no geometry, so without this it is invisible in the Canvas -- you can select it in
    // the Lights panel and have nothing to look at, which is the half of the viewport brief that
    // makes the other half worth having. What is drawn is what the light *is*: where it stands, what
    // it is, which way it faces, and how far it reaches.
    //
    // Editor-only by construction. These are painted with `viewport_overlay.cpp`'s `Painter`, which
    // is ImGui draw-list geometry inside the canvas window -- an offline render reloads the project
    // and never runs a line of it. That is the viewport brief's §32 satisfied structurally rather
    // than by testing for the absence of something.
    struct LightMarker {
        std::string name;
        scene::PunctualLight::Type type = scene::PunctualLight::Type::Point;
        glm::vec3 position{0.0f};
        glm::vec3 direction{0.0f, -1.0f, 0.0f}; // the way the light travels
        glm::vec3 color{1.0f};
        float range = 0.0f;        // 0 = no cutoff; the ring is then drawn at a nominal radius
        float outerConeDegrees = 0.0f;
        float width = 0.0f;        // area kinds
        float height = 0.0f;
        bool enabled = true;
        bool selected = false;
    };
    std::vector<LightMarker> lightMarkers;

    // ---- authored cameras, as objects in the world ----------------------------------------------
    //
    // §28's three-way distinction, drawn rather than described. These are the **authored scene
    // cameras** -- `scene::CameraRig`s in the composition's `CameraDirection`. They are not the
    // editor navigation camera, which is the viewport's own `camera/*` parameters and is where you
    // are looking from rather than a thing to look at; and `active` marks the one the director has
    // put on screen right now, which is a property of the clock and not of the camera.
    //
    // `seq::Shot` is not `scene::CameraShot` and neither of them is here: a shot is a span of time
    // that names a camera, and what this draws is the camera.
    struct CameraMarker {
        std::string name;
        std::uint32_t id = 0;
        glm::vec3 position{0.0f};
        glm::vec3 target{0.0f, 0.0f, -1.0f};
        float fovDegrees = 50.0f;
        float aspect = 16.0f / 9.0f;
        bool active = false;   // the director has this one on screen now
        bool selected = false;
    };
    std::vector<CameraMarker> cameraMarkers;

    // The heroes this scene declares (ADR-104), drawn whether or not anything is selected.
    //
    // Designation has no other appearance -- a hero looks exactly like the object it was made from
    // -- and a toggle whose effect cannot be seen reads as a toggle that does nothing, which is
    // precisely how this arrived as a bug report. What is drawn is what the hero *means* to a
    // director: where it stands, how much room it claims, and how tall it is.
    struct HeroMarker {
        std::string name;
        glm::vec3 position{0.0f};
        float radius = 1.0f;
        float height = 1.0f;
        float importance = 0.5f;
        bool subject = false;   // the most important one: what a directed shot is about
    };
    std::vector<HeroMarker> heroMarkers;

    // A selected node's tie to its parent (ADR-188).
    //
    // Parenting is invisible. A spore emitter parented to the cap it falls from looks exactly like
    // one dropped at the same world position, and the difference only shows up when the cap moves
    // -- which is how "the snow is misaligned with the caps" arrived twice, and why the fix for it
    // could not be seen to have worked. So the tie gets an appearance: a line from the child to the
    // parent's origin, and a tick at each end.
    struct ParentLink {
        std::string child;
        std::string parent;
        glm::vec3 childPosition{0.0f};  // world
        glm::vec3 parentPosition{0.0f}; // world
    };
    std::vector<ParentLink> parentLinks;

    // ---- navigation (ADR-197) ------------------------------------------------------------------
    //
    // The navigation layer had no appearance at all. `explore` has published a route, a leg, a
    // destination and a path status since ADR-093 and nothing drew any of it, so the two questions
    // a character raises -- "why is it going that way" and "why is it not going anywhere" -- were
    // both unanswerable from the screen. The second is the worse one: a walker whose goal came back
    // `Unreachable` stands exactly as still as one that is idling.

    // One selected walker's plan, resolved onto the ground. `waypoints` is the route *after* the
    // walker, so `position` -> `waypoints[0]` is the leg it is on when `leg` is 0.
    struct NavRoute {
        std::string entity;
        std::string node;                  // the composition node it drives: what was selected
        glm::vec3 position{0.0f};          // where the walker is now
        std::vector<glm::vec3> waypoints;  // the plan ahead of it, on the ground
        std::size_t leg = 0;               // index into `waypoints` of the one being walked to
        bool hasDestination = false;
        glm::vec3 destination{0.0f};
        // What it is doing and how the last plan came back, as one line over its head. Always
        // present when the route is: a phase and a `PathStatus` are the whole of the answer when
        // there are no waypoints to draw, which is precisely the case worth seeing.
        std::string label;
        bool failed = false;  // the last path request did not produce a route; colour says so
    };
    std::vector<NavRoute> navRoutes;

    // The navigation grid near the camera, as cell centres. Cells rather than a mesh because the
    // grid *is* cells -- what a walker is refused by is one cell being unwalkable, and a smoothed
    // surface would hide exactly the resolution that decides the route.
    struct NavCellMark {
        glm::vec3 centre{0.0f};
        std::uint8_t flags = 0;    // NavWalkable / NavWater / NavSteep / NavBlocked / NavEdge
        std::uint16_t region = 0;  // the connected component; 0 is the unwalkable set
    };
    std::vector<NavCellMark> navCells;
    float navCellSize = 0.0f;
    bool navRegionColours = false;   // colour walkable cells by region rather than by flag
    // What the grid found for free while it was being built and nothing has ever looked at.
    std::vector<glm::vec3> navShore;
    std::vector<glm::vec3> navVistas;
};

class WorldEditor : public app::EditContext {
public:
    // ---- what the artist has set -------------------------------------------------------------
    EditorMode mode = EditorMode::Select;
    GizmoMode gizmoMode = GizmoMode::Move;
    bool localSpace = false;      // gizmo basis: the active object's rotation rather than the world's
    GizmoSnap snap;
    app::PlacementSettings brush;
    std::string brushAssetId;
    // A brush stroke becomes one group, so a thicket painted in one gesture is one thing to move.
    bool groupStrokes = false;

    // ---- navigation overlay (ADR-197) ------------------------------------------------------------
    //
    // Two toggles, deliberately different in kind.
    //
    // The route is **per selection** and therefore on by default: it costs nothing until something
    // that walks is selected, and the reason the hero markers and the parent link are tied to the
    // selection applies here with more force -- a world of always-drawn routes would be a cat's
    // cradle over the scenery that moved every frame.
    //
    // The grid is **the world**, so it is off by default and bounded by a radius around the camera.
    // Drawing a 640 m world at four-metre cells is 25,600 quads, every one of them projected on the
    // CPU; the radius is what makes it a thing you can leave on. `navStatus()` says what it is
    // actually drawing and why it is drawing nothing, because a toggle that silently does nothing
    // in some state is this project's recurring failure.
    bool showNavRoute = true;
    bool showNavGrid = false;
    bool navGridRegions = false;   // colour walkable cells by connected region rather than by flag
    bool showNavPoints = false;    // the shore and vista points the grid extracts while it builds
    float navGridRadius = 80.0f;   // metres around the camera; the whole cost knob

    Selection selection;

    // ---- the application's edit system (ADR-101) ------------------------------------------------
    //
    // The editor does not own a history. It used to, and that was right while it was the only thing
    // that edited anything: the moment a second editor exists, a stack per editor cannot answer
    // "take back the last thing I did", because neither editor knows which of them acted last.
    //
    // Attached once by the application. Required: an editor with nowhere to record an edit would
    // perform edits nobody could undo, which is worse than refusing, so the edit methods check.
    // Attaching *is* registering. They were two calls, and a caller that made only the first got an
    // editor that recorded edits but was never asked to do any, and whose selection stopped coming
    // back after an undo -- silently, because nothing is wrong with half of it. One call cannot be
    // half done.
    void attachEdits(app::EditSystem& edits) {
        edits_ = &edits;
        edits.addContext(*this);
    }
    [[nodiscard]] bool hasEdits() const { return edits_ != nullptr; }
    [[nodiscard]] app::EditSystem* edits() const { return edits_; }
    [[nodiscard]] EditHistory& history() { return edits_->history(); }
    [[nodiscard]] const EditHistory& history() const { return edits_->history(); }

    // ---- EditContext: what this editor offers the application -----------------------------------
    [[nodiscard]] std::string_view editContextName() const override { return "World"; }
    [[nodiscard]] bool canEdit(app::EditAction action) const override;
    bool doEdit(app::EditAction action, app::Engine& engine, app::EditSystem& edits) override;
    void editSelectionRestored(const std::vector<std::string>& names) override;

    // ---- per frame ---------------------------------------------------------------------------
    // Recomputes the ghost, services the gizmo and the box selection, and performs placements.
    // Everything that needs the pointer happens here, once, in the UI pass.
    void update(app::Engine& engine, const assets::AssetLibrary* library, const scene::Camera& camera,
                float aspect, const EditorInput& input);

    [[nodiscard]] const BrushPreview& preview() const { return preview_; }
    [[nodiscard]] const EditorVisuals& visuals() const { return visuals_; }
    // True when the pointer belongs to the editor and must not start a camera gesture: over a gizmo
    // handle, mid-drag, mid-box, or in Place mode where a click paints.
    [[nodiscard]] bool wantsMouse() const { return wantsMouse_; }
    // One line for the status bar. Always says what the next click will do.
    [[nodiscard]] const std::string& status() const { return status_; }
    // One line about the navigation layer, for the panel that owns its toggles: what the grid is,
    // how much of it is being drawn, or which of the several reasons there is nothing to draw.
    [[nodiscard]] const std::string& navStatus() const { return navStatus_; }

    // ---- what a click on the scene resolved to ------------------------------------------------
    // Called by the application after the GPU picker answers, which remains the authority for a
    // click: it is exact, and it can see procedural instances and terrain chunks that no node owns.
    // `node` is empty when the click landed on something no node owns, or on the sky.
    void applyPick(app::Engine& engine, const std::string& node, bool additive, bool bypassGroups);

    // ---- commands (§24, §25, §27, §28) --------------------------------------------------------
    void undo(app::Engine& engine);
    void redo(app::Engine& engine);
    void deleteSelection(app::Engine& engine);
    void duplicateSelection(app::Engine& engine);
    void groupSelection(app::Engine& engine);
    void ungroupSelection(app::Engine& engine);
    void selectAll(app::Engine& engine);
    void nudgeSelection(app::Engine& engine, glm::vec3 delta);
    // Numeric entry (§24): set the active object's transform outright, as one undoable command.
    void setSelectionPosition(app::Engine& engine, glm::vec3 position);
    void setSelectionRotation(app::Engine& engine, glm::vec3 eulerDegrees);
    void setSelectionScale(app::Engine& engine, glm::vec3 scale);
    // Copy / paste (§27). The clipboard holds names; paste duplicates them where they are plus an
    // offset, which is what "paste" means in a 3D scene with no cursor position of its own.
    void copySelection(app::Engine& engine);
    // Cut: copy, then remove, as *one* history entry (ADR-101). Copy changes nothing and so is not
    // an edit; the removal is, and undoing a cut must bring the objects back in one press rather
    // than leaving the user to discover that the first Cmd+Z only took back a copy.
    bool cutSelection(app::Engine& engine);
    void paste(app::Engine& engine);
    // The application's clipboard, not one of the editor's own. Asks whether it holds *world nodes*:
    // something being on the clipboard is not the same as something this editor can paste.
    [[nodiscard]] bool clipboardEmpty() const {
        return !hasEdits() || !edits_->clipboard().holds(kWorldNodesClipboardType);
    }

    // ---- lock and hide, the way an image editor's layer list does it ---------------------------
    //
    // Two different kinds of thing, deliberately kept different.
    //
    // Hiding is an **edit**: it changes what the scene looks like, it is saved with the scene, it
    // goes through the `visible` parameter, and so it is undoable and animatable like anything else.
    //
    // Locking is **not an edit**. It says which things should stop answering the pointer while you
    // work -- a statement about you, not about the scene -- so it does not enter the history, where
    // it would sit between two real edits and make Cmd+Z take back a click on a padlock. It is still
    // saved with the scene, because which things you had put out of the way is worth keeping.
    void setNodesVisible(app::Engine& engine, std::span<const std::string> names, bool visible);
    void setNodesLocked(app::Engine& engine, std::span<const std::string> names, bool locked);
    // Declares objects heroes, or takes the declaration back (ADR-072/074). Undoable, and saved
    // with the scene: a hero is authored state, not a view setting.
    void setNodesHero(app::Engine& engine, std::span<const std::string> names, bool hero);
    // Puts an edit to one hero -- its importance, its aim, its stand-off -- on the history. The edit
    // itself has already happened (`Composition::editHero`, live under the mouse); this is the
    // record of it, taken once when the drag ends.
    void recordHeroEdit(app::Engine& engine, const world::HeroPoint& before,
                        const world::HeroPoint& after);

    // Drops selected names that no longer exist (after a scene swap or a Generate).
    void reconcile(app::Engine& engine);
    // A fresh scene means a history that describes a world that is gone.
    void reset();

private:
    app::EditSystem* edits_ = nullptr;
    // Whether the last frame had a composition to edit. `canEdit` is const and has no engine, and
    // the menu must not offer Select All in a session with no scene -- so the answer is recorded
    // when `update` runs, which is the only place the editor sees the engine.
    bool sceneAvailable_ = false;

    void updateGhost(app::Engine& engine, const assets::AssetLibrary* library, const scene::Camera& camera,
                     float aspect, const EditorInput& input);
    void updateGizmo(app::Engine& engine, const scene::Camera& camera, float aspect,
                     const EditorInput& input);
    void updateBox(app::Engine& engine, const scene::Camera& camera, float aspect,
                   const EditorInput& input);
    // Collects the navigation visuals from the selection and the camera (ADR-197).
    void updateNavigation(app::Engine& engine, const scene::Camera& camera);
    void commitStroke(app::Engine& engine);
    [[nodiscard]] bool buildGizmoFrame(app::Engine& engine, const scene::Camera& camera);

    BrushPreview preview_;
    EditorVisuals visuals_;
    bool wantsMouse_ = false;
    std::string status_;
    std::string navStatus_;

    // The gizmo drag. `startTransforms_` are the transforms the selection had at the press, so each
    // frame re-applies the whole delta from them rather than accumulating frame by frame.
    GizmoDrag drag_;
    struct StartTransform {
        std::string node;
        glm::vec3 position{0.0f};
        glm::vec3 rotation{0.0f}; // Euler degrees, the parameter's own units
        glm::vec3 scale{1.0f};
        glm::vec3 worldPosition{0.0f};
    };
    std::vector<StartTransform> startTransforms_;

    // A selected light's state at the press. Separate from `StartTransform` because a light is not
    // a node: it has no parent frame to go back through, no scale, and its rotation is a pair of
    // world angles rather than a local Euler triple.
    struct StartLight {
        std::string id;                   // the parameter path's stem, not the display name
        glm::vec3 position{0.0f};
        glm::vec3 direction{0.0f, -1.0f, 0.0f};
        bool aimed = false;               // directional or spot: has azimuth/elevation parameters
    };
    std::vector<StartLight> startLights_;

    // A selected camera's state at the press. A camera is neither a node nor a light: it has no
    // parent frame and no scale, and its orientation is a *target point* rather than angles -- so
    // rotating one moves where it looks, not a quaternion it stores.
    struct StartCamera {
        std::string prefix;   // `cameras/<slug>/`, frozen at creation so a rename cannot orphan it
        glm::vec3 position{0.0f};
        glm::vec3 target{0.0f};
    };
    std::vector<StartCamera> startCameras_;
    void collectSelectedCameras(app::Engine& engine, std::vector<StartCamera>& out) const;
    // The selected lights' positions and ids, rebuilt each frame for the gizmo and the drag.
    void collectSelectedLights(app::Engine& engine, std::vector<StartLight>& out) const;

    // A paint stroke: one command however many clicks-worth of plants it laid down.
    bool stroking_ = false;
    EditCommand stroke_;
    std::string strokeGroup_;
    std::string strokeAsset_;
    glm::vec3 lastStrokePoint_{0.0f};
    bool hasStrokePoint_ = false;
    std::uint32_t strokeSeed_ = 1u;

    bool boxing_ = false;
    // A press that actually landed on the world armed this box. Without it, holding the button down
    // anywhere -- dragging a slider in a panel, scrubbing the timeline -- satisfied "the left button
    // is down" and opened a selection box on the canvas from wherever the *last* canvas press had
    // been. `leftDown` is a global fact about the mouse; only a press knows where it began.
    bool boxArmed_ = false;
    glm::vec2 boxFrom_{0.0f};
    bool boxAdditive_ = false;

};

} // namespace avgen::ui
