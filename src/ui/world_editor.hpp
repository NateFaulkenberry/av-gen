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

#include "app/placement.hpp"
#include "assets/asset_library.hpp"
#include "scene/camera.hpp"
#include "ui/brush.hpp"
#include "ui/edit_history.hpp"
#include "ui/gizmo.hpp"
#include "ui/world_edit.hpp"
#include "ui/world_probe.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace avgen::app {
class Engine;
}

namespace avgen::ui {

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
};

class WorldEditor {
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

    Selection selection;
    EditHistory history;

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
    void paste(app::Engine& engine);
    [[nodiscard]] bool clipboardEmpty() const { return clipboard_.empty(); }

    // Drops selected names that no longer exist (after a scene swap or a Generate).
    void reconcile(app::Engine& engine);
    // A fresh scene means a history that describes a world that is gone.
    void reset();

private:
    void updateGhost(app::Engine& engine, const assets::AssetLibrary* library, const scene::Camera& camera,
                     float aspect, const EditorInput& input);
    void updateGizmo(app::Engine& engine, const scene::Camera& camera, float aspect,
                     const EditorInput& input);
    void updateBox(app::Engine& engine, const scene::Camera& camera, float aspect,
                   const EditorInput& input);
    void commitStroke(app::Engine& engine);
    [[nodiscard]] bool buildGizmoFrame(app::Engine& engine, const scene::Camera& camera);

    BrushPreview preview_;
    EditorVisuals visuals_;
    bool wantsMouse_ = false;
    std::string status_;

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

    std::vector<std::string> clipboard_;
};

} // namespace avgen::ui
