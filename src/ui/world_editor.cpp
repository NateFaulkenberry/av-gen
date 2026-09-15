#include "ui/world_editor.hpp"

#include "app/engine.hpp"
#include "core/log.hpp"
#include "entity/entity.hpp"
#include "entity/nav_grid.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <functional>

namespace avgen::ui {
namespace {

// How close to a handle the pointer has to be, in NDC. A sixtieth of the frame is about ten pixels
// on a 1200-pixel-high canvas, which is the tolerance every tool uses and the one people's hands
// are calibrated to.
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
    // Recorded for `canEdit`, which is const and sees no engine: the menu must not offer Select All
    // in a session that has no scene.
    sceneAvailable_ = composition != nullptr;
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
            history().cancelDrag(engine);
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
            history().cancelDrag(engine);
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

    // The heroes that are selected, and only those.
    //
    // A hero is the one piece of authored state with no appearance of its own, so the mark has to
    // exist -- but a world with five of them was five rings and five stalks standing over the
    // scenery at all times, in the viewport and in a take alike. Tying them to the selection makes
    // them what they actually are: the read-out for the object you are working on, which is also why
    // playing does not hide them. If you have something selected while the piece plays, you are
    // still working on it.
    //
    // `heroes()` is kept ranked, so index 0 is the subject a directed shot would be about.
    {
        const std::vector<world::HeroPoint>& heroes = composition->heroes();
        for (std::size_t i = 0; i < heroes.size(); ++i) {
            if (!selection.contains(heroes[i].name)) {
                continue;
            }
            EditorVisuals::HeroMarker marker;
            marker.name = heroes[i].name;
            marker.position = heroes[i].position;
            marker.radius = heroes[i].radius;
            marker.height = heroes[i].height;
            marker.importance = heroes[i].importance;
            marker.subject = i == 0;
            visuals_.heroMarkers.push_back(std::move(marker));
        }
    }

    // The tie to a parent, for every selected node that has one (ADR-188). Drawn from the selection
    // rather than always, for the same reason the hero markers are: a world of parented emitters
    // would be a cat's cradle over the scenery at all times.
    for (const std::string& name : selection.nodes()) {
        const scene::CompositionNode* node = composition->findNode(name);
        if (node == nullptr || node->parent.empty()) {
            continue;
        }
        const scene::CompositionNode* parent = composition->findNode(node->parent);
        if (parent == nullptr) {
            continue; // a dangling parent is the composition's problem to report, not a line to draw
        }
        EditorVisuals::ParentLink link;
        link.child = name;
        link.parent = node->parent;
        link.childPosition = composition->nodeWorldTransform(*node).position;
        link.parentPosition = composition->nodeWorldTransform(*parent).position;
        visuals_.parentLinks.push_back(std::move(link));
    }

    // The navigation layer (ADR-197): the selected walker's route, and the grid it was planned on.
    updateNavigation(engine, camera);

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

// ---- navigation (ADR-197) -----------------------------------------------------------------------

namespace {

// How many grid cells the overlay will draw before it stops and says so. Each one is four world
// points projected on the CPU and one filled quad in the ImGui draw list, and the radius knob is
// the thing an author turns -- this is the backstop that keeps a 0.5 m grid over a kilometre of
// world from turning a viewport into a slideshow without warning. Measured in the report: 4,700
// cells is about 0.9 ms of the UI pass on this machine.
constexpr std::size_t kNavCellCap = 12000;

// A metre and a half of clearance, so a route over sloping ground reads as lying on it rather than
// disappearing into it. The overlay is drawn after the frame with no depth test at all, so this is
// legibility rather than z-fighting: without it a polyline on a hillside reads as flat.
constexpr float kNavLift = 0.15f;

} // namespace

void WorldEditor::updateNavigation(app::Engine& engine, const scene::Camera& camera) {
    navStatus_.clear();
    scene::Composition* composition = engine.composition();
    if (composition == nullptr) {
        return;
    }
    const entity::EntityWorld& world = composition->entityWorld();
    const entity::Navigator& navigator = world.navigator();
    const entity::NavGrid* grid = navigator.grid();
    const auto onGround = [&](glm::vec2 p) {
        return glm::vec3(p.x, navigator.groundHeight(p) + kNavLift, p.y);
    };

    // ---- the selected walkers' routes ----
    //
    // From the selection, never always-on. The comment on the hero markers above is the reason and
    // it applies harder here: a route is a polyline that changes every few seconds, and a world of
    // them would be a cat's cradle that also moved.
    std::size_t walkers = 0;
    if (showNavRoute) {
        for (const std::unique_ptr<entity::Entity>& entity : world.entities()) {
            const std::string& node = entity->desc().driven();
            if (!selection.contains(node) && !selection.contains(entity->name())) {
                continue;
            }
            ++walkers;
            // Which of two authorities is moving this body decides whose route is the real one.
            // An action or a director override preempts the behaviour (ADR-091/096) and the
            // behaviour keeps its plan while it yields -- so drawing the behaviour's route while an
            // action is walking the body somewhere else would draw a line nothing is following.
            std::span<const glm::vec2> route = entity->actions().route();
            std::size_t leg = entity->actions().routeLeg();
            std::string phase;
            std::string status;
            bool failed = false;
            bool hasDestination = false;
            glm::vec3 destination{0.0f};
            if (!route.empty()) {
                phase = "action";
                const entity::ActionDesc* action = entity->actions().current();
                if (action != nullptr && !action->target.empty()) {
                    // The target's *name* when it has one -- an entity or a node -- and its kind
                    // otherwise, because a `move` to a bare world point has nothing else to say.
                    phase = fmt::format("action -> {}", action->target.name.empty()
                                                            ? entity::targetKindName(action->target.kind)
                                                            : action->target.name);
                }
                hasDestination = true;
                destination = onGround(route.back());
            } else {
                for (const std::unique_ptr<entity::IBehavior>& behavior : entity->behaviors()) {
                    entity::NavDebug nav;
                    if (!behavior->navDebug(nav)) {
                        continue;
                    }
                    route = nav.route;
                    leg = nav.leg;
                    phase = std::string(nav.phase);
                    if (!nav.goalName.empty()) {
                        phase += fmt::format(" -> {} {}", nav.goalKind, nav.goalName);
                    } else if (nav.hasDestination) {
                        phase += fmt::format(" -> {}", nav.goalKind);
                    }
                    status = entity::pathStatusName(nav.status);
                    // Every status but these two means the last request produced no route, which is
                    // the case this overlay exists for: a character standing still because its goal
                    // is on the other side of a lake looks exactly like one that is idling.
                    failed = nav.status != entity::PathStatus::Ok &&
                             nav.status != entity::PathStatus::AlreadyThere;
                    hasDestination = nav.hasDestination;
                    destination = nav.destination;
                    break;
                }
            }
            if (phase.empty()) {
                // Selected, an entity, and navigating nothing. Said out loud rather than skipped:
                // "this thing has no route" and "the overlay is not working" are the same picture
                // otherwise, and telling them apart is most of what a diagnostic is for.
                phase = "no navigation";
            }

            EditorVisuals::NavRoute out;
            out.entity = entity->name();
            out.node = node;
            out.position = entity->state().position();
            out.leg = leg;
            out.waypoints.reserve(route.size());
            for (const glm::vec2& waypoint : route) {
                out.waypoints.push_back(onGround(waypoint));
            }
            out.hasDestination = hasDestination;
            out.destination = hasDestination
                                  ? glm::vec3(destination.x, destination.y + kNavLift, destination.z)
                                  : glm::vec3(0.0f);
            out.failed = failed;
            out.label = out.entity + "  " + phase;
            if (!status.empty()) {
                out.label += fmt::format("  [{}]", status);
            }
            if (!out.waypoints.empty()) {
                out.label += fmt::format("  leg {}/{}", std::min(leg + 1, out.waypoints.size()),
                                         out.waypoints.size());
            }
            visuals_.navRoutes.push_back(std::move(out));
        }
    }

    // ---- the grid, and the points it found while it was being built ----
    visuals_.navRegionColours = navGridRegions;
    std::size_t drawn = 0;
    std::size_t inRange = 0;
    if ((showNavGrid || showNavPoints) && grid != nullptr && grid->valid()) {
        const entity::NavGridStats& stats = grid->stats();
        visuals_.navCellSize = stats.cellSize;
        if (showNavGrid && stats.cellSize > 0.0f) {
            // Centred on what the view is *about*, not on where the eye is: an editor camera
            // two hundred metres up looking at a valley would otherwise paint the grid under
            // itself and leave the valley bare.
            const glm::vec2 centre(camera.target.x, camera.target.z);
            const float radius = std::max(navGridRadius, stats.cellSize);
            const int reach = static_cast<int>(std::ceil(radius / stats.cellSize));
            const glm::ivec2 middle = grid->cellOf(centre);
            const float radiusSquared = radius * radius;
            for (int z = middle.y - reach; z <= middle.y + reach; ++z) {
                for (int x = middle.x - reach; x <= middle.x + reach; ++x) {
                    const glm::ivec2 cell(x, z);
                    if (!grid->inside(cell)) {
                        continue;
                    }
                    const glm::vec2 at = grid->centerOf(cell);
                    const glm::vec2 delta = at - centre;
                    if (glm::dot(delta, delta) > radiusSquared) {
                        continue;
                    }
                    ++inRange;
                    if (drawn >= kNavCellCap) {
                        continue; // counted, not drawn: the status line reports both
                    }
                    const entity::NavCell& contents = grid->at(cell);
                    EditorVisuals::NavCellMark mark;
                    mark.centre = glm::vec3(at.x, contents.ground + kNavLift, at.y);
                    mark.flags = contents.flags;
                    mark.region = grid->regionAt(at);
                    visuals_.navCells.push_back(mark);
                    ++drawn;
                }
            }
        }
        if (showNavPoints) {
            for (const glm::vec3& point : grid->shorePoints()) {
                visuals_.navShore.push_back(point);
            }
            for (const glm::vec3& point : grid->vistaPoints()) {
                visuals_.navVistas.push_back(point);
            }
        }
    }

    // ---- what the panel says about all of that ----
    //
    // Every state that draws nothing has to say why it draws nothing. A checkbox wired to a
    // condition that is false is indistinguishable from a checkbox wired to nothing, and this
    // project has shipped the second one more than once.
    if (grid == nullptr || !grid->valid()) {
        navStatus_ = world.empty()
                         ? "no entities in this scene, so no navigation graph was built"
                         : "this scene has no navigation graph (navCellSize 0 disables it)";
    } else {
        const entity::NavGridStats& stats = grid->stats();
        navStatus_ = fmt::format("grid {}x{} at {:.1f} m  {} walkable / {} water / {} blocked  "
                                 "{} region(s), largest {}  built in {:.0f} ms",
                                 stats.width, stats.height, stats.cellSize, stats.walkable,
                                 stats.water, stats.blocked, stats.regions, stats.largestRegion,
                                 stats.buildMs);
        if (showNavGrid) {
            navStatus_ += fmt::format("\ndrawing {} cell(s) within {:.0f} m of the view", drawn,
                                      navGridRadius);
            if (inRange > drawn) {
                navStatus_ += fmt::format("; {} more are in range and capped off at {}",
                                          inRange - drawn, kNavCellCap);
            }
        }
        if (showNavPoints) {
            navStatus_ += fmt::format("\n{} shore point(s), {} vista point(s)",
                                      visuals_.navShore.size(), visuals_.navVistas.size());
        }
    }
    if (showNavRoute) {
        navStatus_ += walkers == 0
                          ? "\nno entity selected, so no route is drawn"
                          : fmt::format("\n{} selected entity/entities", walkers);
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
    history().push(std::move(stroke_));
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
            history().commitDrag(engine);
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
            history().commitDrag(engine);
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
            history().beginDrag(engine, label, transformParamPaths(topmostOf(*composition, selection.nodes())),
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
        boxing_ = false;   // not yet: a press is a click until it travels
        boxArmed_ = true;  // but it did land on the world, which is what makes the travel ours
        return;
    }
    if (input.leftDown && boxArmed_ && !drag_.active) {
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
    if (!input.leftDown) {
        boxArmed_ = false; // the button is up; the next box needs its own press on the world
    }
    if (boxing_ && !input.leftDown) {
        boxing_ = false;
        std::vector<std::string> caught = nodesInScreenRect(*composition, camera, aspect, boxFrom_, input.ndc);
        // A box catches the groups things are in, not their members: dragging over a thicket and
        // getting two hundred individual ferns is not what the artist meant by drawing a box round
        // the thicket.
        std::vector<std::string> roots;
        for (const std::string& name : caught) {
            if (nodeLocked(*composition, name)) {
                continue; // locked out of the pointer, and a box is the pointer
            }
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
    if (nodeLocked(*composition, node)) {
        // A locked object answers a click the way empty space does: the click still lands, it simply
        // does not land on *this*. Anything else makes a lock a thing you have to aim around rather
        // than a thing that gets out of the way -- and the ground a world is built on is under the
        // pointer everywhere.
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
    const EditApply applied = history().undo(engine, &restored);
    for (const std::string& problem : applied.problems) {
        log::warn("undo: {}", problem);
    }
    selection.set(std::move(restored));
    reconcile(engine);
}

void WorldEditor::redo(app::Engine& engine) {
    std::vector<std::string> restored;
    const EditApply applied = history().redo(engine, &restored);
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
    history().push(std::move(command));
}

bool WorldEditor::cutSelection(app::Engine& engine) {
    if (selection.empty() || !hasEdits()) {
        return false;
    }
    copySelection(engine);
    // One command, labelled for what the user did. Built here rather than calling deleteSelection so
    // the history says "Cut 3 objects" -- a person looking for what to undo is looking for the verb
    // they used, not for the one the implementation happened to reuse.
    EditCommand command = deleteNodes(engine, selection.nodes());
    if (command.empty()) {
        return false;
    }
    command.label = "Cut " + std::to_string(command.touched()) +
                    (command.touched() == 1 ? " object" : " objects");
    command.selectionBefore = selection.nodes();
    selection.clear();
    command.selectionAfter.clear();
    history().push(std::move(command));
    return true;
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
    history().push(std::move(command));
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
    history().push(std::move(command));
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
    history().push(std::move(combined));
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
        if (node->parent.empty() && !node->locked) {
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
    history().push(std::move(command));
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
    history().push(oneShot(engine, selection, "Position",
                         [&](const std::string& name) { setNodePosition(engine, name, position); }));
}

void WorldEditor::setSelectionRotation(app::Engine& engine, glm::vec3 degrees) {
    history().push(oneShot(engine, selection, "Rotate",
                         [&](const std::string& name) { setNodeRotation(engine, name, degrees); }));
}

void WorldEditor::setSelectionScale(app::Engine& engine, glm::vec3 scale) {
    history().push(oneShot(engine, selection, "Scale",
                         [&](const std::string& name) { setNodeScale(engine, name, scale); }));
}

void WorldEditor::copySelection(app::Engine& engine) {
    if (!hasEdits() || selection.empty()) {
        return;
    }
    scene::Composition* composition = engine.composition();
    if (composition == nullptr) {
        return;
    }
    // Clones, not names. The clipboard used to hold the names of the selected nodes, which meant
    // copy-then-delete-then-paste pasted nothing -- the editor said so in a warning, which is a
    // clear symptom of a clipboard that never held anything. What a person copies, they expect to
    // still have after deleting the original.
    //
    // Descendants come too, so copying a group copies what is in it.
    auto nodes = std::make_shared<std::vector<scene::CompositionNode>>();
    for (const std::string& name : withDescendants(*composition, topmostOf(*composition, selection.nodes()))) {
        if (const scene::CompositionNode* source = composition->findNode(name); source != nullptr) {
            nodes->push_back(scene::cloneNodeSpec(*source));
        }
    }
    if (nodes->empty()) {
        return;
    }
    app::ClipboardPayload payload;
    payload.type = std::string(kWorldNodesClipboardType);
    payload.source = "World";
    payload.count = nodes->size();
    payload.data = std::move(nodes);
    edits_->clipboard().set(std::move(payload));
}

void WorldEditor::paste(app::Engine& engine) {
    if (!hasEdits()) {
        return;
    }
    const auto nodes = edits_->clipboard().payload().as<std::vector<scene::CompositionNode>>(
        kWorldNodesClipboardType);
    if (nodes == nullptr || nodes->empty()) {
        return;
    }
    // Offset, so a paste on top of the original is visibly a second object rather than looking like
    // nothing happened. Sized from what is being pasted, the way duplicate does it: a fixed metre
    // is invisible beside a hillside and enormous beside a mushroom.
    glm::vec3 extent(1.0f, 0.0f, 0.0f);
    if (!nodes->empty()) {
        const glm::vec3 size = nodes->front().transform.scale;
        extent = glm::vec3(std::max(std::abs(size.x), 0.5f) * 1.1f, 0.0f, 0.0f);
    }
    std::vector<std::string> created;
    EditCommand command = pasteNodes(engine, *nodes, extent, &created);
    if (command.empty()) {
        return;
    }
    command.selectionBefore = selection.nodes();
    selection.set(created);
    command.selectionAfter = selection.nodes();
    history().push(std::move(command));
}

void WorldEditor::setNodesVisible(app::Engine& engine, std::span<const std::string> names, bool visible) {
    scene::Composition* composition = engine.composition();
    if (composition == nullptr || names.empty() || !hasEdits()) {
        return;
    }
    EditCommand command(fmt::format("{} {}", visible ? "Show" : "Hide",
                                    names.size() == 1 ? names[0]
                                                      : std::to_string(names.size()) + " objects"));
    for (const std::string& name : names) {
        ParamChange change;
        change.path = "nodes/" + name + "/visible";
        change.before = baseComponents(engine, change.path);
        if (change.before.empty()) {
            continue; // no such node, or a kind that registers no visibility
        }
        setNodeVisible(engine, name, visible);
        change.after = baseComponents(engine, change.path);
        if (change.after != change.before) {
            command.params.push_back(std::move(change));
        }
    }
    if (command.empty()) {
        return; // everything asked for was already in the state asked for
    }
    // The selection is not part of this edit either side: hiding something does not deselect it,
    // any more than hiding a layer deselects it, so undoing a hide must not move the selection.
    command.selectionBefore = selection.nodes();
    command.selectionAfter = selection.nodes();
    history().push(std::move(command));
}

void WorldEditor::setNodesLocked(app::Engine& engine, std::span<const std::string> names, bool locked) {
    scene::Composition* composition = engine.composition();
    if (composition == nullptr) {
        return;
    }
    for (const std::string& name : names) {
        if (scene::CompositionNode* node = composition->findNode(name)) {
            node->locked = locked;
        }
    }
    if (!locked) {
        return;
    }
    // Something that has stopped answering the pointer should not still be held by it. Leaving a
    // locked object selected leaves its gizmo up and the arrow keys still moving it, which is the
    // one thing a lock is for.
    std::vector<std::string> keep;
    for (const std::string& held : selection.nodes()) {
        if (!nodeLocked(*composition, held)) {
            keep.push_back(held);
        }
    }
    if (keep.size() != selection.size()) {
        selection.set(std::move(keep));
    }
}

void WorldEditor::setNodesHero(app::Engine& engine, std::span<const std::string> names, bool hero) {
    if (names.empty() || !hasEdits()) {
        return;
    }
    EditCommand command = ui::setNodesHero(engine, names, hero);
    if (command.empty()) {
        return;   // already in the state asked for, or the set the scene would end up with is invalid
    }
    // Designating something does not select or deselect it, so an undo must not move the selection.
    command.selectionBefore = selection.nodes();
    command.selectionAfter = selection.nodes();
    history().push(std::move(command));
}

void WorldEditor::recordHeroEdit(app::Engine& engine, const world::HeroPoint& before,
                                 const world::HeroPoint& after) {
    if (!hasEdits() || before.name != after.name) {
        return;
    }
    if (engine.composition() == nullptr) {
        return;
    }
    HeroChange change;
    change.node = before.name;
    change.before.push_back(before);
    change.after.push_back(after);
    EditCommand command(fmt::format("Adjust hero {}", before.name));
    command.heroes.push_back(std::move(change));
    // Not applied here: the panel wrote the value live so the world moved under the mouse. Pushing
    // it applies the same command forward again, which is a no-op against the state it produced and
    // is what makes the undo exact.
    command.selectionBefore = selection.nodes();
    command.selectionAfter = selection.nodes();
    history().push(std::move(command));
}

void WorldEditor::reconcile(app::Engine& engine) {
    if (scene::Composition* composition = engine.composition()) {
        selection.retainOnly(*composition);
    }
}

void WorldEditor::reset() {
    // Through the system, so the save marker moves with the history: a freshly loaded scene is not
    // "modified", and a marker taken against the old document must not match the new one.
    if (hasEdits()) {
        edits_->clearHistory();
    }
    selection.clear();
    // The clipboard is *not* cleared. It holds clones rather than references, so what was copied
    // survives the scene it came from -- and copying out of one scene into another is a thing people
    // do on purpose. It is the application's clipboard, not the scene's.
    drag_ = GizmoDrag{};
    startTransforms_.clear();
    stroking_ = false;
    stroke_ = EditCommand{};
    preview_ = BrushPreview{};
    visuals_ = EditorVisuals{};
}

// ---- EditContext (ADR-101) ----------------------------------------------------------------------

bool WorldEditor::canEdit(app::EditAction action) const {
    // An editor with nowhere to record an edit must not claim it can make one: the action would
    // happen and nothing could take it back, which is worse than the menu greying the item.
    if (!hasEdits()) {
        return false;
    }
    const bool chosen = !selection.empty();
    switch (action) {
    case app::EditAction::Copy:
    case app::EditAction::Cut:
    case app::EditAction::Duplicate:
    case app::EditAction::Delete:
    case app::EditAction::SelectNone:
        return chosen;
    case app::EditAction::Paste:
        // What is on the clipboard, not merely that something is: a world editor offering to paste
        // a timeline marker is a menu item that cannot do what it says.
        return edits_->clipboard().holds(kWorldNodesClipboardType);
    case app::EditAction::SelectAll:
        return sceneAvailable_;
    case app::EditAction::Undo:
    case app::EditAction::Redo:
        return false; // the system's, never an editor's
    }
    return false;
}

bool WorldEditor::doEdit(app::EditAction action, app::Engine& engine, app::EditSystem& edits) {
    if (!canEdit(action)) {
        return false;
    }
    switch (action) {
    case app::EditAction::Copy:
        copySelection(engine);
        return true;
    case app::EditAction::Cut:
        return cutSelection(engine);
    case app::EditAction::Paste:
        paste(engine);
        return true;
    case app::EditAction::Duplicate:
        duplicateSelection(engine);
        return true;
    case app::EditAction::Delete:
        deleteSelection(engine);
        return true;
    case app::EditAction::SelectAll:
        selectAll(engine);
        return true;
    case app::EditAction::SelectNone:
        selection.clear();
        return true;
    case app::EditAction::Undo:
    case app::EditAction::Redo:
        return false;
    }
    static_cast<void>(edits);
    return false;
}

void WorldEditor::editSelectionRestored(const std::vector<std::string>& names) {
    // Undoing a delete gives the objects back; without this it gives them back with nothing
    // selected, and the user cannot tell which ones returned.
    selection.set(names);
}

} // namespace avgen::ui
