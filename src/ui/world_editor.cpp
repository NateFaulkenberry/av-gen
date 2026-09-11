#include "ui/world_editor.hpp"

#include "app/engine.hpp"
#include "core/log.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <functional>

namespace avgen::ui {
namespace {

// How close to a handle the pointer has to be, in NDC. A sixtieth of the frame is about ten pixels
// on a 1200-pixel-high canvas, which is the tolerance every tool uses and the one people's hands
// are calibrated to.
constexpr float kHandlePickRadius = 0.033f;
// A drag has to travel this far in NDC before it counts as a drag rather than a click. Below it a
// box selection would open on every click and a paint stroke would place twice.
constexpr float kDragThreshold = 0.012f;

glm::vec3 eulerOf(const scene::Composition& composition, const std::string& name) {
    const scene::CompositionNode* node = composition.findNode(name);
    if (node == nullptr) {
        return glm::vec3(0.0f);
    }
    return node->rotationParam != nullptr ? node->rotationParam->base()
                                          : scene::eulerDegrees(node->transform.rotation);
}

} // namespace

const char* editorModeName(EditorMode mode) {
    return mode == EditorMode::Place ? "place" : "select";
}

// ---- per frame ---------------------------------------------------------------------------------

void WorldEditor::update(app::Engine& engine, const assets::AssetLibrary* library,
                         const scene::Camera& camera, float aspect, const EditorInput& input) {
    visuals_ = EditorVisuals{};
    wantsMouse_ = false;
    scene::Composition* composition = engine.composition();
    if (composition == nullptr) {
        status_ = "no scene";
        preview_ = BrushPreview{};
        return;
    }
    selection.retainOnly(*composition);

    // The camera modifier is down, so this drag is the camera's and the editor takes no part in it
    // (`ui::viewportIntent`). Without this the one gesture would do both: Option-dragging would spin
    // the world *and* drag a selection box across it.
    //
    // Anything already in progress is abandoned rather than left half-open -- a gizmo drag that was
    // interrupted by a camera move must not resume against a different view when the modifier is
    // released, because the handle it was following is no longer under the pointer.
    if (input.cameraDrag) {
        if (drag_.active) {
            history.cancelDrag(engine);
            drag_ = GizmoDrag{};
            startTransforms_.clear();
        }
        boxing_ = false;
        status_ = "moving the view";
        preview_ = BrushPreview{};
        return;
    }

    if (input.escape) {
        if (drag_.active) {
            history.cancelDrag(engine);
            drag_ = GizmoDrag{};
            startTransforms_.clear();
        }
        if (stroking_) {
            commitStroke(engine);
        }
        boxing_ = false;
    }

    if (mode == EditorMode::Place) {
        updateGhost(engine, library, camera, aspect, input);
    } else {
        preview_ = BrushPreview{};
        updateGizmo(engine, camera, aspect, input);
        updateBox(engine, camera, aspect, input);
    }

    // The selection's outlines, whichever mode we are in: an artist painting still wants to see
    // what is chosen, and losing the outline on a mode change reads as losing the selection.
    for (const std::string& name : selection.nodes()) {
        const scene::WorldBounds bounds = composition->nodeBounds(name);
        if (!bounds.valid) {
            continue;
        }
        EditorVisuals::SelectedBox box;
        box.name = name;
        box.bounds = bounds;
        if (const scene::CompositionNode* node = composition->findNode(name)) {
            box.isGroup = node->kind == scene::NodeKind::Group;
            if (box.isGroup) {
                box.members = descendantsOf(*composition, name).size();
            }
        }
        visuals_.selectionBoxes.push_back(std::move(box));
    }

    // The status line. Whatever else is true, it says what the next click does.
    if (mode == EditorMode::Place) {
        status_ = previewSummary(preview_);
    } else if (drag_.active) {
        status_ = visuals_.dragReadout;
    } else if (selection.empty()) {
        status_ = "click to select  |  drag a box for several  |  shift-click to add";
    } else if (selection.size() == 1) {
        status_ = fmt::format("{}  |  {} in {} space", selection.primary(), gizmoModeName(gizmoMode),
                              localSpace ? "local" : "world");
    } else {
        status_ = fmt::format("{} objects  |  {} in {} space", selection.size(), gizmoModeName(gizmoMode),
                              localSpace ? "local" : "world");
    }
}

void WorldEditor::updateGhost(app::Engine& engine, const assets::AssetLibrary* library,
                              const scene::Camera& camera, float aspect, const EditorInput& input) {
    scene::Composition* composition = engine.composition();
    BrushAsset asset;
    if (library != nullptr && !brushAssetId.empty()) {
        asset.descriptor = library->find(brushAssetId);
        if (asset.descriptor != nullptr) {
            asset.file = library->resolve(*asset.descriptor).generic_string();
        }
    }
    if (!input.overCanvas && !stroking_) {
        preview_ = BrushPreview{};
        status_ = "move the pointer over the world to place";
        return;
    }
    // In Place mode the pointer is always the editor's: a click paints. That is the whole reason
    // arming is an explicit mode rather than a checkbox somewhere.
    wantsMouse_ = true;

    const ViewRay ray = rayThroughNdc(camera, aspect, input.ndc);
    const GroundSample ground = sampleGroundAlong(*composition, ray);
    if (strokeSeed_ == 0) {
        strokeSeed_ = 1u;
    }
    preview_ = planBrush(*composition, brush, asset, ground, strokeSeed_);
    visuals_.showGhost = preview_.armed && ground.valid;
    for (const std::string& name : preview_.erasing) {
        const scene::WorldBounds bounds = composition->nodeBounds(name);
        if (bounds.valid) {
            visuals_.erasingBoxes.push_back(bounds);
        }
    }

    const bool eraser = brush.mode == app::PlacementMode::Eraser;
    const bool replacing = brush.mode == app::PlacementMode::Replace;
    const bool painting = brush.mode == app::PlacementMode::Brush || eraser || replacing;

    if (input.leftPressed) {
        // A press while a stroke is already open would overwrite `stroke_`, and `stroke_` owns the
        // nodes an Eraser has taken out of the scene -- they would be destroyed with it and could
        // not be undone. Closing the open one first is what `EditHistory::beginDrag` does with a
        // drag that was never released, and for the same reason.
        if (stroking_) {
            commitStroke(engine);
        }
        stroking_ = true;
        stroke_ = EditCommand("Paint");
        stroke_.selectionBefore = selection.nodes();
        strokeGroup_.clear();
        hasStrokePoint_ = false;
        // The name is taken now, not at commit: by the time the button comes up the pointer may
        // have left the canvas and the preview been cleared, and "Place 36 x asset" is a history
        // entry that tells the artist nothing about which 36 they are taking back.
        strokeAsset_ = preview_.assetName;
    }
    if (stroking_ && input.leftDown && ground.valid) {
        // A paint drag lays down more as the cursor travels. The threshold is the brush's own
        // spacing, so "how fast do I have to move" is answered by a control that is already on
        // screen rather than by a constant in here.
        const float travelled =
            hasStrokePoint_ ? glm::length(ground.position - lastStrokePoint_) : 1e9f;
        const float step = painting ? std::max(brush.brushRadius * 0.5f, brush.spacing) : 1e9f;
        const bool first = !hasStrokePoint_;
        if (first || travelled >= step) {
            if (eraser || replacing) {
                if (!preview_.erasing.empty()) {
                    EditCommand removal = deleteNodes(engine, preview_.erasing);
                    for (NodeRecord& record : removal.removed) {
                        stroke_.removed.push_back(std::move(record));
                    }
                    for (ParamChange& change : removal.params) {
                        stroke_.params.push_back(std::move(change));
                    }
                }
            }
            if (!eraser && preview_.placeable()) {
                if (groupStrokes && strokeGroup_.empty()) {
                    scene::CompositionNode group;
                    group.name = preview_.assetId.empty() ? "stroke" : preview_.assetId + "_group";
                    group.kind = scene::NodeKind::Group;
                    group.transform.position = ground.position;
                    if (auto added = composition->addNode(std::move(group))) {
                        strokeGroup_ = (*added)->name;
                        stroke_.added.emplace_back(strokeGroup_);
                    }
                }
                std::vector<scene::CompositionNode> nodes =
                    makeNodes(*composition, preview_, asset, strokeGroup_);
                if (!nodes.empty()) {
                    // A node parented to a group needs its transform expressed in the group's
                    // frame. The group is a pure translation, so this is a subtraction.
                    if (!strokeGroup_.empty()) {
                        if (const scene::CompositionNode* group = composition->findNode(strokeGroup_)) {
                            const glm::vec3 pivot = composition->nodeWorldTransform(*group).position;
                            for (scene::CompositionNode& node : nodes) {
                                node.transform.position -= pivot;
                            }
                        }
                    }
                    EditCommand placed = placeNodes(engine, std::move(nodes), "Paint");
                    for (NodeRecord& record : placed.added) {
                        stroke_.added.push_back(std::move(record));
                    }
                }
            }
            lastStrokePoint_ = ground.position;
            hasStrokePoint_ = true;
            ++strokeSeed_; // the next dab is a different arrangement, not the same one again
            if (!painting) {
                // Single, Cluster and Landmark place once per click, not once per pixel of travel.
                hasStrokePoint_ = true;
                lastStrokePoint_ = ground.position + glm::vec3(1e9f);
            }
        }
    }
    if (input.leftReleased || (!input.leftDown && stroking_)) {
        commitStroke(engine);
    }
}

void WorldEditor::commitStroke(app::Engine& engine) {
    if (!stroking_) {
        return;
    }
    stroking_ = false;
    strokeGroup_.clear();
    hasStrokePoint_ = false;
    if (stroke_.empty()) {
        stroke_ = EditCommand{};
        return;
    }
    const std::size_t placed = stroke_.added.size();
    const std::size_t erased = stroke_.removed.size();
    if (placed > 0 && erased > 0) {
        stroke_.label = fmt::format("Replace {} with {}", erased, placed);
    } else if (erased > 0) {
        stroke_.label = fmt::format("Erase {} object{}", erased, erased == 1 ? "" : "s");
    } else {
        stroke_.label =
            fmt::format("Place {} x {}", placed, strokeAsset_.empty() ? "asset" : strokeAsset_);
    }
    stroke_.selectionAfter = selection.nodes();
    history.push(std::move(stroke_));
    stroke_ = EditCommand{};
    static_cast<void>(engine);
}

bool WorldEditor::buildGizmoFrame(app::Engine& engine, const scene::Camera& camera) {
    scene::Composition* composition = engine.composition();
    if (composition == nullptr || selection.empty()) {
        return false;
    }
    const std::vector<std::string> moving = topmostOf(*composition, selection.nodes());
    const scene::WorldBounds bounds = selectionBounds(*composition, moving);
    if (!bounds.valid) {
        return false;
    }
    visuals_.gizmo.origin = bounds.centre();
    visuals_.gizmo.basis = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    if (localSpace) {
        // The active object's rotation, not the whole selection's: a set of differently oriented
        // objects has no shared local frame, and picking the one the artist chose last is the
        // convention that makes "local" mean something for a multi-selection at all.
        if (const scene::CompositionNode* node = composition->findNode(selection.primary())) {
            visuals_.gizmo.basis = composition->nodeWorldTransform(*node).rotation;
        }
    }
    visuals_.gizmo.scale = gizmoWorldScale(camera, visuals_.gizmo.origin);
    return true;
}

void WorldEditor::updateGizmo(app::Engine& engine, const scene::Camera& camera, float aspect,
                              const EditorInput& input) {
    scene::Composition* composition = engine.composition();
    if (!buildGizmoFrame(engine, camera)) {
        if (drag_.active) {
            history.commitDrag(engine);
            drag_ = GizmoDrag{};
            startTransforms_.clear();
        }
        return;
    }
    visuals_.showGizmo = true;

    if (drag_.active) {
        wantsMouse_ = true;
        visuals_.dragging = drag_.handle;
        const GizmoDelta delta = updateGizmoDrag(drag_, camera, aspect, input.ndc, snap);
        if (delta.valid) {
            visuals_.dragReadout = delta.readout;
            const glm::vec3 pivot = drag_.frame.origin;
            for (const StartTransform& start : startTransforms_) {
                const scene::CompositionNode* node = composition->findNode(start.node);
                if (node == nullptr) {
                    continue;
                }
                // Everything is re-derived from the transforms at the press. Applying a delta on
                // top of last frame's result would accumulate rounding, and rounding in a transform
                // tool is an object that does not return to where it started.
                switch (drag_.mode) {
                case GizmoMode::Move: {
                    const glm::vec3 world = start.worldPosition + delta.translation;
                    // Back into the node's own frame, so a grouped object moves with its parent's
                    // rotation and scale accounted for.
                    glm::mat4 frame(1.0f);
                    if (!node->parent.empty()) {
                        if (const scene::CompositionNode* parent = composition->findNode(node->parent)) {
                            frame = composition->nodeWorldTransform(*parent).matrix();
                        }
                    }
                    const glm::vec4 local = glm::inverse(frame) * glm::vec4(world, 1.0f);
                    setNodePosition(engine, start.node, glm::vec3(local));
                    break;
                }
                case GizmoMode::Rotate: {
                    // The delta is world space; the parameter is the node's *local* rotation. For a
                    // node under a rotated parent those differ, and multiplying them directly turns
                    // the object about the wrong axis -- group some rocks, turn the group, then turn
                    // one rock inside it. The position half of this case already goes back through
                    // the parent frame; so does this now.
                    glm::quat parentRotation(1.0f, 0.0f, 0.0f, 0.0f);
                    if (!node->parent.empty()) {
                        if (const scene::CompositionNode* parent = composition->findNode(node->parent)) {
                            parentRotation = composition->nodeWorldTransform(*parent).rotation;
                        }
                    }
                    const glm::quat localDelta =
                        glm::conjugate(parentRotation) * delta.rotation * parentRotation;
                    const glm::quat turned = localDelta * scene::quatFromEulerDegrees(start.rotation);
                    setNodeRotation(engine, start.node, scene::eulerDegrees(turned));
                    // Several objects turn about the shared pivot, not each about its own: that is
                    // what makes rotating a group of rocks look like turning an arrangement.
                    const glm::vec3 offset = start.worldPosition - pivot;
                    const glm::vec3 world = pivot + delta.rotation * offset;
                    glm::mat4 frame(1.0f);
                    if (!node->parent.empty()) {
                        if (const scene::CompositionNode* parent = composition->findNode(node->parent)) {
                            frame = composition->nodeWorldTransform(*parent).matrix();
                        }
                    }
                    const glm::vec4 local = glm::inverse(frame) * glm::vec4(world, 1.0f);
                    setNodePosition(engine, start.node, glm::vec3(local));
                    break;
                }
                case GizmoMode::Scale: {
                    setNodeScale(engine, start.node, start.scale * delta.scale);
                    const glm::vec3 offset = start.worldPosition - pivot;
                    const glm::vec3 world = pivot + drag_.frame.basis *
                                                        (glm::conjugate(drag_.frame.basis) * offset * delta.scale);
                    glm::mat4 frame(1.0f);
                    if (!node->parent.empty()) {
                        if (const scene::CompositionNode* parent = composition->findNode(node->parent)) {
                            frame = composition->nodeWorldTransform(*parent).matrix();
                        }
                    }
                    const glm::vec4 local = glm::inverse(frame) * glm::vec4(world, 1.0f);
                    setNodePosition(engine, start.node, glm::vec3(local));
                    break;
                }
                }
            }
        }
        if (input.leftReleased || !input.leftDown) {
            history.commitDrag(engine);
            drag_ = GizmoDrag{};
            startTransforms_.clear();
        }
        return;
    }

    if (!input.overCanvas) {
        return;
    }
    visuals_.hovered = pickHandle(camera, aspect, visuals_.gizmo, gizmoMode, input.ndc, kHandlePickRadius);
    if (visuals_.hovered != GizmoHandle::None) {
        // The pointer is on the gizmo. It must not orbit -- a drag that started on a handle and
        // spun the camera instead is the single most infuriating thing a viewport can do.
        wantsMouse_ = true;
    }
    if (input.leftPressed && visuals_.hovered != GizmoHandle::None) {
        if (beginGizmoDrag(drag_, camera, aspect, visuals_.gizmo, gizmoMode, visuals_.hovered, input.ndc)) {
            startTransforms_.clear();
            const std::vector<std::string> moving = topmostOf(*composition, selection.nodes());
            for (const std::string& name : moving) {
                const scene::CompositionNode* node = composition->findNode(name);
                if (node == nullptr) {
                    continue;
                }
                StartTransform start;
                start.node = name;
                start.position = node->positionParam != nullptr ? node->positionParam->base()
                                                                : node->transform.position;
                start.rotation = eulerOf(*composition, name);
                start.scale = node->scaleParam != nullptr ? node->scaleParam->base() : node->transform.scale;
                start.worldPosition = composition->nodeWorldTransform(*node).position;
                startTransforms_.push_back(std::move(start));
            }
            const std::string label = fmt::format(
                "{} {}", gizmoMode == GizmoMode::Move ? "Move" : gizmoMode == GizmoMode::Rotate ? "Rotate" : "Scale",
                selection.size() == 1 ? selection.primary() : std::to_string(selection.size()) + " objects");
            history.beginDrag(engine, label, transformParamPaths(topmostOf(*composition, selection.nodes())),
                              selection.nodes());
        }
    }
}

void WorldEditor::updateBox(app::Engine& engine, const scene::Camera& camera, float aspect,
                            const EditorInput& input) {
    scene::Composition* composition = engine.composition();
    if (composition == nullptr) {
        return;
    }
    if (input.leftPressed && input.overCanvas && visuals_.hovered == GizmoHandle::None && !drag_.active) {
        boxFrom_ = input.ndc;
        boxAdditive_ = input.shift;
        boxing_ = false; // not yet: a press is a click until it travels
        return;
    }
    if (input.leftDown && !drag_.active) {
        if (!boxing_ && glm::length(input.ndc - boxFrom_) > kDragThreshold) {
            boxing_ = true;
        }
        if (boxing_) {
            wantsMouse_ = true;
            visuals_.boxing = true;
            visuals_.boxFrom = boxFrom_;
            visuals_.boxTo = input.ndc;
        }
        return;
    }
    if (boxing_ && !input.leftDown) {
        boxing_ = false;
        std::vector<std::string> caught = nodesInScreenRect(*composition, camera, aspect, boxFrom_, input.ndc);
        // A box catches the groups things are in, not their members: dragging over a thicket and
        // getting two hundred individual ferns is not what the artist meant by drawing a box round
        // the thicket.
        std::vector<std::string> roots;
        for (const std::string& name : caught) {
            const std::string root = groupRootOf(*composition, name);
            if (std::find(roots.begin(), roots.end(), root) == roots.end()) {
                roots.push_back(root);
            }
        }
        if (!boxAdditive_) {
            selection.clear();
        }
        for (const std::string& name : roots) {
            selection.add(name);
        }
    }
    static_cast<void>(engine);
}

// ---- selection ----------------------------------------------------------------------------------

void WorldEditor::applyPick(app::Engine& engine, const std::string& node, bool additive, bool bypassGroups) {
    scene::Composition* composition = engine.composition();
    if (composition == nullptr) {
        return;
    }
    if (node.empty()) {
        if (!additive) {
            selection.clear();
        }
        return;
    }
    const std::string target = bypassGroups ? node : groupRootOf(*composition, node);
    if (additive) {
        selection.toggle(target);
    } else {
        selection.set(target);
    }
}

// ---- commands -------------------------------------------------------------------------------------

void WorldEditor::undo(app::Engine& engine) {
    std::vector<std::string> restored;
    const EditApply applied = history.undo(engine, &restored);
    for (const std::string& problem : applied.problems) {
        log::warn("undo: {}", problem);
    }
    selection.set(std::move(restored));
    reconcile(engine);
}

void WorldEditor::redo(app::Engine& engine) {
    std::vector<std::string> restored;
    const EditApply applied = history.redo(engine, &restored);
    for (const std::string& problem : applied.problems) {
        log::warn("redo: {}", problem);
    }
    selection.set(std::move(restored));
    reconcile(engine);
}

void WorldEditor::deleteSelection(app::Engine& engine) {
    if (selection.empty()) {
        return;
    }
    EditCommand command = deleteNodes(engine, selection.nodes());
    command.selectionBefore = selection.nodes();
    selection.clear();
    command.selectionAfter.clear();
    history.push(std::move(command));
}

void WorldEditor::duplicateSelection(app::Engine& engine) {
    if (selection.empty()) {
        return;
    }
    scene::Composition* composition = engine.composition();
    if (composition == nullptr) {
        return;
    }
    // Offset by a fraction of what is being copied, so the duplicate is visibly beside the original
    // rather than exactly inside it -- a copy you cannot see is a copy you press the button for
    // four more times.
    const scene::WorldBounds bounds = selectionBounds(*composition, selection.nodes());
    const glm::vec3 offset = bounds.valid
                                 ? glm::vec3(std::max(bounds.size().x, 0.5f) * 1.1f, 0.0f, 0.0f)
                                 : glm::vec3(1.0f, 0.0f, 0.0f);
    std::vector<std::string> created;
    EditCommand command = duplicateNodes(engine, selection.nodes(), offset, &created);
    if (command.empty()) {
        return;
    }
    command.selectionBefore = selection.nodes();
    selection.set(created);
    command.selectionAfter = selection.nodes();
    history.push(std::move(command));
}

void WorldEditor::groupSelection(app::Engine& engine) {
    if (selection.size() < 2) {
        return;
    }
    std::string made;
    EditCommand command = groupNodes(engine, selection.nodes(), "group", &made);
    if (command.empty()) {
        return;
    }
    command.selectionBefore = selection.nodes();
    selection.set(made);
    command.selectionAfter = selection.nodes();
    history.push(std::move(command));
}

void WorldEditor::ungroupSelection(app::Engine& engine) {
    scene::Composition* composition = engine.composition();
    if (composition == nullptr || selection.empty()) {
        return;
    }
    EditCommand combined("Ungroup");
    std::vector<std::string> freed;
    const std::vector<std::string> chosen = selection.nodes();
    for (const std::string& name : chosen) {
        const scene::CompositionNode* node = composition->findNode(name);
        if (node == nullptr || node->kind != scene::NodeKind::Group) {
            continue;
        }
        EditCommand one = ungroupNode(engine, name, &freed);
        for (ParentChange& change : one.parents) {
            combined.parents.push_back(std::move(change));
        }
        for (ParamChange& change : one.params) {
            combined.params.push_back(std::move(change));
        }
        for (NodeRecord& record : one.removed) {
            combined.removed.push_back(std::move(record));
        }
    }
    if (combined.empty()) {
        return;
    }
    combined.selectionBefore = chosen;
    selection.set(freed);
    combined.selectionAfter = selection.nodes();
    history.push(std::move(combined));
}

void WorldEditor::selectAll(app::Engine& engine) {
    scene::Composition* composition = engine.composition();
    if (composition == nullptr) {
        return;
    }
    selection.clear();
    for (const auto& node : composition->nodes()) {
        if (!node || node->kind == scene::NodeKind::Terrain) {
            continue;
        }
        if (node->parent.empty()) {
            selection.add(node->name);
        }
    }
}

void WorldEditor::nudgeSelection(app::Engine& engine, glm::vec3 delta) {
    if (selection.empty()) {
        return;
    }
    EditCommand command = moveNodes(engine, selection.nodes(), delta);
    if (command.empty()) {
        return;
    }
    command.selectionBefore = selection.nodes();
    command.selectionAfter = selection.nodes();
    history.push(std::move(command));
}

namespace {

// The shared shape of the three numeric setters: capture, write, record.
EditCommand oneShot(app::Engine& engine, const Selection& selection, const char* what,
                    const std::function<void(const std::string&)>& write) {
    EditCommand command(fmt::format("{} {}", what, selection.size() == 1
                                                       ? selection.primary()
                                                       : std::to_string(selection.size()) + " objects"));
    for (const std::string& name : selection.nodes()) {
        for (const char* field : {"position", "rotation", "scale"}) {
            ParamChange change;
            change.path = "nodes/" + name + "/" + field;
            change.before = baseComponents(engine, change.path);
            if (!change.before.empty()) {
                command.params.push_back(std::move(change));
            }
        }
    }
    for (const std::string& name : selection.nodes()) {
        write(name);
    }
    std::vector<ParamChange> moved;
    for (ParamChange& change : command.params) {
        change.after = baseComponents(engine, change.path);
        if (change.after.size() == change.before.size() && change.after != change.before) {
            moved.push_back(std::move(change));
        }
    }
    command.params = std::move(moved);
    command.selectionBefore = selection.nodes();
    command.selectionAfter = selection.nodes();
    return command;
}

} // namespace

void WorldEditor::setSelectionPosition(app::Engine& engine, glm::vec3 position) {
    history.push(oneShot(engine, selection, "Position",
                         [&](const std::string& name) { setNodePosition(engine, name, position); }));
}

void WorldEditor::setSelectionRotation(app::Engine& engine, glm::vec3 degrees) {
    history.push(oneShot(engine, selection, "Rotate",
                         [&](const std::string& name) { setNodeRotation(engine, name, degrees); }));
}

void WorldEditor::setSelectionScale(app::Engine& engine, glm::vec3 scale) {
    history.push(oneShot(engine, selection, "Scale",
                         [&](const std::string& name) { setNodeScale(engine, name, scale); }));
}

void WorldEditor::copySelection(app::Engine& engine) {
    static_cast<void>(engine);
    clipboard_ = selection.nodes();
}

void WorldEditor::paste(app::Engine& engine) {
    if (clipboard_.empty()) {
        return;
    }
    scene::Composition* composition = engine.composition();
    if (composition == nullptr) {
        return;
    }
    // Names that have since been deleted are dropped rather than failing the whole paste.
    std::vector<std::string> live;
    for (const std::string& name : clipboard_) {
        if (composition->findNode(name) != nullptr) {
            live.push_back(name);
        }
    }
    if (live.empty()) {
        log::warn("paste: nothing on the clipboard is still in the scene");
        return;
    }
    const scene::WorldBounds bounds = selectionBounds(*composition, live);
    const glm::vec3 offset = bounds.valid
                                 ? glm::vec3(std::max(bounds.size().x, 0.5f) * 1.1f, 0.0f, 0.0f)
                                 : glm::vec3(1.0f, 0.0f, 0.0f);
    std::vector<std::string> created;
    EditCommand command = duplicateNodes(engine, live, offset, &created);
    if (command.empty()) {
        return;
    }
    command.label = "Paste " + std::to_string(created.size()) + " object(s)";
    command.selectionBefore = selection.nodes();
    selection.set(created);
    command.selectionAfter = selection.nodes();
    history.push(std::move(command));
}

void WorldEditor::reconcile(app::Engine& engine) {
    if (scene::Composition* composition = engine.composition()) {
        selection.retainOnly(*composition);
    }
}

void WorldEditor::reset() {
    history.clear();
    selection.clear();
    clipboard_.clear();
    drag_ = GizmoDrag{};
    startTransforms_.clear();
    stroking_ = false;
    stroke_ = EditCommand{};
    preview_ = BrushPreview{};
    visuals_ = EditorVisuals{};
}

} // namespace avgen::ui
