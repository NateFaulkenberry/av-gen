// The world editor (ADR-092, world-authoring-spec §17-§28, §45): selection, groups, transforms,
// duplication, the brush's placement validity, the gizmo's drag arithmetic, undo/redo, and the
// round trip through a saved file.
//
// The point of this file is that none of it needs a window. Every decision the editor makes is in
// an ImGui-free translation unit for exactly that reason -- this repository cannot screenshot an
// ImGui frame, so an editor whose behaviour could only be checked by looking at it would be an
// editor nobody checked.

#include "app/engine.hpp"
#include "support/gltf_fixture.hpp"
#include "ui/brush.hpp"
#include "app/edit_system.hpp"
#include "ui/edit_history.hpp"
#include "ui/gizmo.hpp"
#include "ui/world_edit.hpp"
#include "ui/world_editor.hpp"
#include "ui/world_probe.hpp"

#include <glm/gtx/quaternion.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <filesystem>
#include <fstream>
#include <unistd.h>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

// A scene with three glTF nodes on flat ground, and nothing else. The glb is the shared triangle
// fixture, which has real bounds so the selection boxes and the collision tests are measuring
// something rather than agreeing about zero.
struct Fixture {
    fs::path dir;
    fs::path glb;
    app::Engine engine{app::EngineMode::Offline};

    Fixture() {
        dir = fs::temp_directory_path() /
              ("avgen_world_editor_" + std::to_string(static_cast<long long>(::getpid())));
        fs::remove_all(dir);
        fs::create_directories(dir);
        const auto tmp = testsupport::writeTriangleGlb("world_editor");
        glb = dir / "tri.glb";
        fs::copy_file(tmp, glb, fs::copy_options::overwrite_existing);
        fs::remove(tmp);
        engine.newComposition();
    }
    ~Fixture() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    std::string add(const std::string& name, glm::vec3 at) {
        scene::CompositionNode node;
        node.name = name;
        node.kind = scene::NodeKind::Gltf;
        node.asset = glb.generic_string();
        node.transform.position = at;
        auto added = engine.composition()->addNode(std::move(node));
        REQUIRE(added.has_value());
        engine.rebind();
        return (*added)->name;
    }

    [[nodiscard]] glm::vec3 positionOf(const std::string& name) const {
        const auto* p = const_cast<app::Engine&>(engine).params().find("nodes/" + name + "/position");
        REQUIRE(p != nullptr);
        return glm::vec3(p->baseComponent(0), p->baseComponent(1), p->baseComponent(2));
    }
};

scene::Camera lookingDown() {
    scene::Camera camera;
    camera.position = glm::vec3(0.0f, 40.0f, 40.0f);
    camera.target = glm::vec3(0.0f, 0.0f, 0.0f);
    camera.nearPlane = 0.1f;
    camera.farPlane = 1000.0f;
    camera.lens.useExplicitFov = true;
    camera.fovYRadians = 0.87f;
    return camera;
}

} // namespace

// ---- selection ------------------------------------------------------------------------------------

TEST_CASE("selection keeps its order and its active object") {
    ui::Selection selection;
    CHECK(selection.empty());
    selection.add("a");
    selection.add("b");
    selection.add("a"); // already there
    CHECK(selection.size() == 2);
    CHECK(selection.primary() == "b");
    selection.toggle("b");
    CHECK(selection.size() == 1);
    CHECK(selection.primary() == "a");
    selection.toggle("c");
    CHECK(selection.primary() == "c");
    selection.set("only");
    CHECK(selection.size() == 1);
}

TEST_CASE("a selection drops names the scene no longer has") {
    Fixture f;
    f.add("keep", glm::vec3(0.0f));
    ui::Selection selection;
    selection.add("keep");
    selection.add("gone");
    CHECK(selection.retainOnly(*f.engine.composition()));
    CHECK(selection.nodes() == std::vector<std::string>{"keep"});
}

// ---- groups ---------------------------------------------------------------------------------------

TEST_CASE("grouping moves nothing, and the group then moves everything") {
    Fixture f;
    const std::string a = f.add("a", glm::vec3(-4.0f, 0.0f, 0.0f));
    const std::string b = f.add("b", glm::vec3(4.0f, 0.0f, 0.0f));
    auto* composition = f.engine.composition();

    const glm::vec3 worldABefore = composition->nodeWorldTransform(*composition->findNode(a)).position;
    const glm::vec3 worldBBefore = composition->nodeWorldTransform(*composition->findNode(b)).position;

    std::string group;
    const std::vector<std::string> members{a, b};
    ui::EditCommand command = ui::groupNodes(f.engine, members, "rocks", &group);
    REQUIRE_FALSE(group.empty());
    REQUIRE(composition->findNode(group) != nullptr);
    CHECK(composition->findNode(group)->kind == scene::NodeKind::Group);

    // Nothing moved. This is the property that makes grouping usable at all.
    const glm::vec3 worldAAfter = composition->nodeWorldTransform(*composition->findNode(a)).position;
    const glm::vec3 worldBAfter = composition->nodeWorldTransform(*composition->findNode(b)).position;
    CHECK_THAT(worldAAfter.x, Catch::Matchers::WithinAbs(worldABefore.x, 1e-4));
    CHECK_THAT(worldAAfter.z, Catch::Matchers::WithinAbs(worldABefore.z, 1e-4));
    CHECK_THAT(worldBAfter.x, Catch::Matchers::WithinAbs(worldBBefore.x, 1e-4));

    // The group is the parent, and both are still their own editable nodes.
    CHECK(composition->findNode(a)->parent == group);
    CHECK(composition->findNode(b)->parent == group);

    // Moving the group moves both, as a unit.
    const std::vector<std::string> justTheGroup{group};
    ui::EditCommand moved = ui::moveNodes(f.engine, justTheGroup, glm::vec3(10.0f, 0.0f, 0.0f));
    CHECK_FALSE(moved.empty());
    CHECK_THAT(composition->nodeWorldTransform(*composition->findNode(a)).position.x,
               Catch::Matchers::WithinAbs(worldABefore.x + 10.0, 1e-3));
    CHECK_THAT(composition->nodeWorldTransform(*composition->findNode(b)).position.x,
               Catch::Matchers::WithinAbs(worldBBefore.x + 10.0, 1e-3));

    // Clicking a member selects the group, not the member.
    CHECK(ui::groupRootOf(*composition, a) == group);
    // ... and moving the pair does not move each of them twice.
    CHECK(ui::topmostOf(*composition, std::vector<std::string>{group, a, b}).size() == 1);
}

TEST_CASE("ungrouping leaves the members exactly where they were") {
    Fixture f;
    const std::string a = f.add("a", glm::vec3(-4.0f, 1.0f, 2.0f));
    const std::string b = f.add("b", glm::vec3(4.0f, 0.0f, -2.0f));
    auto* composition = f.engine.composition();
    std::string group;
    static_cast<void>(ui::groupNodes(f.engine, std::vector<std::string>{a, b}, "g", &group));
    static_cast<void>(ui::moveNodes(f.engine, std::vector<std::string>{group}, glm::vec3(7.0f, 3.0f, -1.0f)));

    const glm::vec3 worldA = composition->nodeWorldTransform(*composition->findNode(a)).position;
    const glm::vec3 worldB = composition->nodeWorldTransform(*composition->findNode(b)).position;

    std::vector<std::string> freed;
    ui::EditCommand command = ui::ungroupNode(f.engine, group, &freed);
    CHECK(composition->findNode(group) == nullptr);
    CHECK(freed.size() == 2);
    CHECK(composition->findNode(a)->parent.empty());
    const glm::vec3 afterA = composition->nodeWorldTransform(*composition->findNode(a)).position;
    const glm::vec3 afterB = composition->nodeWorldTransform(*composition->findNode(b)).position;
    CHECK_THAT(afterA.x, Catch::Matchers::WithinAbs(worldA.x, 1e-3));
    CHECK_THAT(afterA.y, Catch::Matchers::WithinAbs(worldA.y, 1e-3));
    CHECK_THAT(afterA.z, Catch::Matchers::WithinAbs(worldA.z, 1e-3));
    CHECK_THAT(afterB.x, Catch::Matchers::WithinAbs(worldB.x, 1e-3));
}

TEST_CASE("deleting a group takes its contents with it, and undo brings them all back") {
    Fixture f;
    const std::string a = f.add("a", glm::vec3(-4.0f, 0.0f, 0.0f));
    const std::string b = f.add("b", glm::vec3(4.0f, 0.0f, 0.0f));
    auto* composition = f.engine.composition();
    std::string group;
    static_cast<void>(ui::groupNodes(f.engine, std::vector<std::string>{a, b}, "g", &group));

    ui::EditHistory history;
    history.push(ui::deleteNodes(f.engine, std::vector<std::string>{group}));
    CHECK(composition->findNode(group) == nullptr);
    CHECK(composition->findNode(a) == nullptr);
    CHECK(composition->findNode(b) == nullptr);

    const ui::EditApply undone = history.undo(f.engine);
    CHECK(undone.ok());
    REQUIRE(composition->findNode(group) != nullptr);
    REQUIRE(composition->findNode(a) != nullptr);
    CHECK(composition->findNode(a)->parent == group);
    CHECK(composition->findNode(b)->parent == group);
}

// ---- undo / redo ----------------------------------------------------------------------------------

TEST_CASE("undo and redo cover placement, movement and deletion") {
    Fixture f;
    ui::EditHistory history;
    auto* composition = f.engine.composition();

    // Place.
    std::vector<scene::CompositionNode> nodes;
    for (int i = 0; i < 3; ++i) {
        scene::CompositionNode node;
        node.name = "fern";
        node.kind = scene::NodeKind::Gltf;
        node.asset = f.glb.generic_string();
        node.transform.position = glm::vec3(static_cast<float>(i) * 3.0f, 0.0f, 0.0f);
        nodes.push_back(std::move(node));
    }
    std::vector<std::string> created;
    history.push(ui::placeNodes(f.engine, std::move(nodes), "Place 3 x fern", &created));
    REQUIRE(created.size() == 3);
    CHECK(composition->nodeCount() == 3);
    CHECK(history.undoLabel() == "Place 3 x fern");

    // Move.
    history.push(ui::moveNodes(f.engine, created, glm::vec3(0.0f, 5.0f, 0.0f)));
    CHECK_THAT(f.positionOf(created[0]).y, Catch::Matchers::WithinAbs(5.0, 1e-4));

    // Delete one.
    history.push(ui::deleteNodes(f.engine, std::vector<std::string>{created[1]}));
    CHECK(composition->nodeCount() == 2);

    // Back up the whole way.
    CHECK(history.undo(f.engine).ok());
    CHECK(composition->nodeCount() == 3);
    CHECK(history.undo(f.engine).ok());
    CHECK_THAT(f.positionOf(created[0]).y, Catch::Matchers::WithinAbs(0.0, 1e-4));
    CHECK(history.undo(f.engine).ok());
    CHECK(composition->nodeCount() == 0);
    CHECK_FALSE(history.canUndo());

    // And forward again.
    CHECK(history.redo(f.engine).ok());
    CHECK(composition->nodeCount() == 3);
    CHECK(history.redo(f.engine).ok());
    CHECK_THAT(f.positionOf(created[0]).y, Catch::Matchers::WithinAbs(5.0, 1e-4));
    CHECK(history.redo(f.engine).ok());
    CHECK(composition->nodeCount() == 2);
    CHECK_FALSE(history.canRedo());
}

TEST_CASE("a new edit after an undo discards the redo stack") {
    Fixture f;
    ui::EditHistory history;
    const std::string a = f.add("a", glm::vec3(0.0f));
    history.push(ui::moveNodes(f.engine, std::vector<std::string>{a}, glm::vec3(1.0f, 0.0f, 0.0f)));
    CHECK(history.undo(f.engine).ok());
    CHECK(history.canRedo());
    history.push(ui::moveNodes(f.engine, std::vector<std::string>{a}, glm::vec3(0.0f, 0.0f, 2.0f)));
    CHECK_FALSE(history.canRedo());
}

TEST_CASE("a drag is one undo step, and a drag that ends where it began is none") {
    Fixture f;
    ui::EditHistory history;
    const std::string a = f.add("a", glm::vec3(0.0f));
    const std::vector<std::string> paths = ui::transformParamPaths(std::vector<std::string>{a});

    history.beginDrag(f.engine, "Move a", paths);
    for (int frame = 1; frame <= 30; ++frame) {
        ui::setNodePosition(f.engine, a, glm::vec3(static_cast<float>(frame) * 0.2f, 0.0f, 0.0f));
    }
    history.commitDrag(f.engine);
    CHECK(history.undoSize() == 1); // thirty frames of writing, one thing to undo
    CHECK(history.undo(f.engine).ok());
    CHECK_THAT(f.positionOf(a).x, Catch::Matchers::WithinAbs(0.0, 1e-4));

    // The undo above moved that command to the redo stack, so the undo stack is empty. A drag that
    // ends where it began must leave it empty: an editor that records a no-op is one you have to
    // press undo twice to get past.
    CHECK(history.undoSize() == 0);
    history.beginDrag(f.engine, "Move a", paths);
    ui::setNodePosition(f.engine, a, glm::vec3(3.0f, 0.0f, 0.0f));
    ui::setNodePosition(f.engine, a, glm::vec3(0.0f, 0.0f, 0.0f)); // back where it started
    history.commitDrag(f.engine);
    CHECK(history.undoSize() == 0);
}

TEST_CASE("escape during a drag puts the transform back") {
    Fixture f;
    ui::EditHistory history;
    const std::string a = f.add("a", glm::vec3(2.0f, 0.0f, 0.0f));
    history.beginDrag(f.engine, "Move a", ui::transformParamPaths(std::vector<std::string>{a}));
    ui::setNodePosition(f.engine, a, glm::vec3(99.0f, 0.0f, 0.0f));
    history.cancelDrag(f.engine);
    CHECK_THAT(f.positionOf(a).x, Catch::Matchers::WithinAbs(2.0, 1e-4));
    CHECK(history.undoSize() == 0);
}

// ---- duplication ------------------------------------------------------------------------------------

TEST_CASE("duplicating a group copies the hierarchy rather than adding to the original") {
    Fixture f;
    const std::string a = f.add("a", glm::vec3(-1.0f, 0.0f, 0.0f));
    const std::string b = f.add("b", glm::vec3(1.0f, 0.0f, 0.0f));
    auto* composition = f.engine.composition();
    std::string group;
    static_cast<void>(ui::groupNodes(f.engine, std::vector<std::string>{a, b}, "g", &group));

    std::vector<std::string> created;
    ui::EditCommand command =
        ui::duplicateNodes(f.engine, std::vector<std::string>{group}, glm::vec3(20.0f, 0.0f, 0.0f), &created);
    REQUIRE(created.size() == 1);
    const std::string copy = created.front();
    CHECK(copy != group);
    // The original group still has exactly its two children, and the copy has two of its own.
    CHECK(ui::descendantsOf(*composition, group).size() == 2);
    CHECK(ui::descendantsOf(*composition, copy).size() == 2);
    // And the copy is where the offset put it.
    CHECK_THAT(composition->nodeWorldTransform(*composition->findNode(copy)).position.x -
                   composition->nodeWorldTransform(*composition->findNode(group)).position.x,
               Catch::Matchers::WithinAbs(20.0, 1e-3));
}

// ---- the ghost ---------------------------------------------------------------------------------------

TEST_CASE("the ghost says where a placement lands and why it may not") {
    Fixture f;
    auto* composition = f.engine.composition();

    // A world with a hill and a lake, so slope and water are real rather than asserted.
    scene::CompositionNode terrain;
    terrain.name = "ground";
    terrain.kind = scene::NodeKind::Terrain;
    terrain.worldMap = world::defaultWorld();
    terrain.worldMap.size = glm::vec2(200.0f, 200.0f);
    terrain.worldMap.prepare();
    REQUIRE(f.engine.composition()->addNode(std::move(terrain)).has_value());

    assets::AssetDescriptor descriptor;
    descriptor.id = "fern";
    descriptor.name = "Fern";
    descriptor.naturalSize = glm::vec3(1.0f, 2.0f, 1.0f);
    descriptor.preferredScale = 2.0f;
    ui::BrushAsset asset{&descriptor, f.glb.generic_string()};

    app::PlacementSettings settings;
    settings.mode = app::PlacementMode::Single;
    settings.seed = 7u;
    settings.yawJitter = 0.0f;
    settings.scaleJitter = 0.0f;

    // In the middle of the map, on whatever the ground is there.
    const ui::GroundSample middle = ui::sampleGroundAt(*composition, glm::vec2(0.0f, 0.0f));
    REQUIRE(middle.valid);
    CHECK(middle.hasTerrain);
    CHECK(middle.insideWorld);

    ui::BrushPreview ok = ui::planBrush(*composition, settings, asset, middle, 1u);
    REQUIRE(ok.instances.size() == 1);
    // The ghost's instance is on the ground, not on the plane the cursor happened to be over.
    CHECK_THAT(ok.instances[0].position.y, Catch::Matchers::WithinAbs(middle.position.y, 1e-3));
    CHECK(ok.instances[0].footprintRadius > 0.0f);
    CHECK(ok.instances[0].height > 0.0f);

    // Outside the world's extent: refused, and it says so.
    const ui::GroundSample outside = ui::sampleGroundAt(*composition, glm::vec2(5000.0f, 5000.0f));
    ui::BrushPreview far = ui::planBrush(*composition, settings, asset, outside, 1u);
    REQUIRE(far.instances.size() == 1);
    CHECK(far.instances[0].issue == ui::PlacementIssue::OutsideWorld);
    CHECK(far.reason == "outside the world");
    CHECK_FALSE(far.placeable());

    // A slope limit of zero refuses anything that is not perfectly flat, and the reason carries the
    // numbers -- a warning without them is one nobody can act on.
    app::PlacementSettings steep = settings;
    steep.maxSlopeDegrees = 0.0f;
    ui::BrushPreview blocked = ui::planBrush(*composition, steep, asset, middle, 1u);
    if (middle.slopeDegrees > 0.0f) {
        CHECK(blocked.instances[0].issue == ui::PlacementIssue::TooSteep);
        CHECK(blocked.reason.find("limit") != std::string::npos);
    }

    // Nothing armed is its own honest answer rather than an empty ghost.
    ui::BrushPreview none = ui::planBrush(*composition, settings, ui::BrushAsset{}, middle, 1u);
    CHECK(none.issue == ui::PlacementIssue::NoAsset);
    CHECK_FALSE(none.armed);
}

TEST_CASE("the ghost refuses to plant inside something that is already there") {
    Fixture f;
    auto* composition = f.engine.composition();
    const std::string rock = f.add("rock", glm::vec3(0.0f, 0.0f, 0.0f));
    const scene::WorldBounds bounds = composition->nodeBounds(rock);
    REQUIRE(bounds.valid);

    assets::AssetDescriptor descriptor;
    descriptor.id = "fern";
    descriptor.naturalSize = glm::vec3(1.0f, 1.0f, 1.0f);
    descriptor.preferredScale = 1.0f;
    ui::BrushAsset asset{&descriptor, f.glb.generic_string()};

    app::PlacementSettings settings;
    settings.mode = app::PlacementMode::Single;
    settings.avoidCollisions = true;
    settings.seed = 3u;

    ui::GroundSample on;
    on.valid = true;
    on.position = glm::vec3(bounds.centre().x, bounds.min.y, bounds.centre().z);
    ui::BrushPreview blocked = ui::planBrush(*composition, settings, asset, on, 1u);
    REQUIRE(blocked.instances.size() == 1);
    CHECK(blocked.instances[0].issue == ui::PlacementIssue::Collides);
    CHECK(blocked.instances[0].blockedBy == rock);
    CHECK(blocked.reason == "blocked by " + rock);

    // Well clear of it: allowed.
    ui::GroundSample away = on;
    away.position.x += 50.0f;
    ui::BrushPreview allowed = ui::planBrush(*composition, settings, asset, away, 1u);
    CHECK(allowed.instances[0].valid());
    CHECK(allowed.placeable());

    // And with collision avoidance off, the artist's judgement wins.
    app::PlacementSettings anyway = settings;
    anyway.avoidCollisions = false;
    CHECK(ui::planBrush(*composition, anyway, asset, on, 1u).instances[0].valid());
}

TEST_CASE("a brush plan respects its spacing and its density") {
    app::PlacementSettings settings;
    settings.mode = app::PlacementMode::Brush;
    settings.brushRadius = 8.0f;
    settings.spacing = 2.0f;
    settings.density = 1.0f;
    const auto full = app::planPlacements(settings, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f), 11u);
    REQUIRE(full.size() > 4);
    for (std::size_t i = 0; i < full.size(); ++i) {
        for (std::size_t j = i + 1; j < full.size(); ++j) {
            CHECK(glm::length(full[i].position - full[j].position) >= settings.spacing - 1e-3f);
        }
    }
    settings.density = 0.25f;
    const auto thin = app::planPlacements(settings, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f), 11u);
    CHECK(thin.size() < full.size());
    // Thinner, not closer together: density must not become a second spacing control.
    for (std::size_t i = 0; i < thin.size(); ++i) {
        for (std::size_t j = i + 1; j < thin.size(); ++j) {
            CHECK(glm::length(thin[i].position - thin[j].position) >= settings.spacing - 1e-3f);
        }
    }
}

// ---- the probe --------------------------------------------------------------------------------------

TEST_CASE("a ray finds the ground it points at, and reports nothing when it points at the sky") {
    Fixture f;
    scene::CompositionNode terrain;
    terrain.name = "ground";
    terrain.kind = scene::NodeKind::Terrain;
    terrain.worldMap = world::defaultWorld();
    terrain.worldMap.size = glm::vec2(400.0f, 400.0f);
    terrain.worldMap.prepare();
    REQUIRE(f.engine.composition()->addNode(std::move(terrain)).has_value());
    auto* composition = f.engine.composition();
    // The map from the composition, not the local that was just moved out of.
    const world::WorldMap& map = composition->findNode("ground")->worldMap;

    const scene::Camera camera = lookingDown();
    const ui::ViewRay down = ui::rayThroughNdc(camera, 16.0f / 9.0f, glm::vec2(0.0f, 0.0f));
    const ui::GroundSample hit = ui::sampleGroundAlong(*composition, down);
    REQUIRE(hit.valid);
    // The point the march found is on the surface: the height function agrees with it.
    CHECK_THAT(hit.position.y,
               Catch::Matchers::WithinAbs(map.height(glm::vec2(hit.position.x, hit.position.z)), 0.05));
    // And it is on the ray.
    const glm::vec3 along = down.origin + down.direction * hit.distance;
    CHECK_THAT(glm::length(along - hit.position), Catch::Matchers::WithinAbs(0.0, 0.05));

    ui::ViewRay up;
    up.origin = glm::vec3(0.0f, 10.0f, 0.0f);
    up.direction = glm::vec3(0.0f, 1.0f, 0.0f);
    CHECK_FALSE(ui::sampleGroundAlong(*composition, up).valid);
}

TEST_CASE("a box over the screen catches what is inside it and nothing else") {
    Fixture f;
    const std::string near = f.add("near", glm::vec3(0.0f, 0.0f, 0.0f));
    const std::string far = f.add("far", glm::vec3(300.0f, 0.0f, 0.0f));
    auto* composition = f.engine.composition();
    const scene::Camera camera = lookingDown();
    const std::vector<std::string> caught =
        ui::nodesInScreenRect(*composition, camera, 16.0f / 9.0f, glm::vec2(-0.5f, -0.5f), glm::vec2(0.5f, 0.5f));
    CHECK(std::find(caught.begin(), caught.end(), near) != caught.end());
    CHECK(std::find(caught.begin(), caught.end(), far) == caught.end());
}

TEST_CASE("a point behind the camera does not project to a plausible screen position") {
    const scene::Camera camera = lookingDown();
    const ui::Projected behind = ui::projectPoint(camera, 16.0f / 9.0f, glm::vec3(0.0f, 40.0f, 400.0f));
    CHECK_FALSE(behind.inFront);
    const ui::Projected ahead = ui::projectPoint(camera, 16.0f / 9.0f, glm::vec3(0.0f, 0.0f, 0.0f));
    CHECK(ahead.inFront);
    CHECK(std::abs(ahead.ndc.x) < 0.1f);
}

// ---- the gizmo ---------------------------------------------------------------------------------------

TEST_CASE("an axis drag moves by what the ray says, not by the mouse delta") {
    const scene::Camera camera = lookingDown();
    const float aspect = 16.0f / 9.0f;
    ui::GizmoFrame frame;
    frame.origin = glm::vec3(0.0f);
    frame.scale = ui::gizmoWorldScale(camera, frame.origin);
    CHECK(frame.scale > 0.0f);

    // Press on the X arm. Its projected position is where the handle picker must find it.
    const auto [a, b] = ui::axisSegment(frame, ui::GizmoHandle::AxisX);
    const ui::Projected mid = ui::projectPoint(camera, aspect, (a + b) * 0.5f);
    REQUIRE(mid.inFront);
    CHECK(ui::pickHandle(camera, aspect, frame, ui::GizmoMode::Move, mid.ndc, 0.033f) ==
          ui::GizmoHandle::AxisX);

    ui::GizmoDrag drag;
    REQUIRE(ui::beginGizmoDrag(drag, camera, aspect, frame, ui::GizmoMode::Move, ui::GizmoHandle::AxisX,
                               mid.ndc));
    // Drag to where a point four metres along +X projects: the delta must come out as four metres.
    const ui::Projected target = ui::projectPoint(camera, aspect, (a + b) * 0.5f + glm::vec3(4.0f, 0.0f, 0.0f));
    const ui::GizmoDelta delta = ui::updateGizmoDrag(drag, camera, aspect, target.ndc, ui::GizmoSnap{});
    REQUIRE(delta.valid);
    CHECK_THAT(delta.translation.x, Catch::Matchers::WithinAbs(4.0, 0.05));
    CHECK_THAT(delta.translation.y, Catch::Matchers::WithinAbs(0.0, 1e-4));
    CHECK_THAT(delta.translation.z, Catch::Matchers::WithinAbs(0.0, 1e-4));
    CHECK(delta.readout.find("along X") != std::string::npos);

    // Asking twice gives the same answer: the update is pure, so the caller can re-apply from the
    // original transforms every frame instead of accumulating.
    const ui::GizmoDelta again = ui::updateGizmoDrag(drag, camera, aspect, target.ndc, ui::GizmoSnap{});
    CHECK_THAT(again.translation.x, Catch::Matchers::WithinAbs(delta.translation.x, 1e-6));
}

TEST_CASE("move snapping lands on the grid") {
    const scene::Camera camera = lookingDown();
    const float aspect = 16.0f / 9.0f;
    ui::GizmoFrame frame;
    frame.scale = ui::gizmoWorldScale(camera, frame.origin);
    const auto [a, b] = ui::axisSegment(frame, ui::GizmoHandle::AxisX);
    const glm::vec3 press = (a + b) * 0.5f;
    ui::GizmoDrag drag;
    REQUIRE(ui::beginGizmoDrag(drag, camera, aspect, frame, ui::GizmoMode::Move, ui::GizmoHandle::AxisX,
                               ui::projectPoint(camera, aspect, press).ndc));
    ui::GizmoSnap snap;
    snap.move = 1.0f;
    const ui::Projected target = ui::projectPoint(camera, aspect, press + glm::vec3(4.4f, 0.0f, 0.0f));
    const ui::GizmoDelta delta = ui::updateGizmoDrag(drag, camera, aspect, target.ndc, snap);
    REQUIRE(delta.valid);
    CHECK_THAT(delta.translation.x, Catch::Matchers::WithinAbs(4.0, 1e-3));
}

TEST_CASE("rotate and scale drags report their own units") {
    const scene::Camera camera = lookingDown();
    const float aspect = 16.0f / 9.0f;
    ui::GizmoFrame frame;
    frame.scale = ui::gizmoWorldScale(camera, frame.origin);

    ui::GizmoDrag rotate;
    const std::vector<glm::vec3> ring = ui::rotationRing(frame, ui::GizmoHandle::AxisY, 48);
    REQUIRE(ring.size() == 48);
    REQUIRE(ui::beginGizmoDrag(rotate, camera, aspect, frame, ui::GizmoMode::Rotate, ui::GizmoHandle::AxisY,
                               ui::projectPoint(camera, aspect, ring[0]).ndc));
    const ui::GizmoDelta turned = ui::updateGizmoDrag(
        rotate, camera, aspect, ui::projectPoint(camera, aspect, ring[12]).ndc, ui::GizmoSnap{});
    REQUIRE(turned.valid);
    CHECK(turned.readout.find("deg") != std::string::npos);
    // A quarter of the way round the ring is ninety degrees, whichever way the sign comes out.
    const float degrees = glm::degrees(2.0f * std::acos(std::clamp(turned.rotation.w, -1.0f, 1.0f)));
    CHECK_THAT(degrees, Catch::Matchers::WithinAbs(90.0, 2.0));

    ui::GizmoDrag scale;
    const auto [a, b] = ui::axisSegment(frame, ui::GizmoHandle::AxisX);
    REQUIRE(ui::beginGizmoDrag(scale, camera, aspect, frame, ui::GizmoMode::Scale, ui::GizmoHandle::AxisX,
                               ui::projectPoint(camera, aspect, b).ndc));
    const glm::vec3 twiceAsFar = frame.origin + (b - frame.origin) * 2.0f;
    const ui::GizmoDelta grown = ui::updateGizmoDrag(
        scale, camera, aspect, ui::projectPoint(camera, aspect, twiceAsFar).ndc, ui::GizmoSnap{});
    REQUIRE(grown.valid);
    CHECK_THAT(grown.scale.x, Catch::Matchers::WithinAbs(2.0, 0.05));
    CHECK_THAT(grown.scale.y, Catch::Matchers::WithinAbs(1.0, 1e-4)); // an axis handle scales one axis
}

// ---- serialization (§45) --------------------------------------------------------------------------------

TEST_CASE("painted assets, groups and transforms survive a save and a load") {
    Fixture f;
    auto* composition = f.engine.composition();
    const std::string a = f.add("a", glm::vec3(-3.0f, 0.0f, 1.0f));
    const std::string b = f.add("b", glm::vec3(3.0f, 0.0f, -1.0f));
    std::string group;
    static_cast<void>(ui::groupNodes(f.engine, std::vector<std::string>{a, b}, "arrangement", &group));
    // Move and turn the group, so what is written is the edit and not the authored transform.
    static_cast<void>(ui::moveNodes(f.engine, std::vector<std::string>{group}, glm::vec3(12.0f, 2.0f, -4.0f)));
    ui::setNodeRotation(f.engine, group, glm::vec3(0.0f, 35.0f, 0.0f));
    ui::setNodeScale(f.engine, a, glm::vec3(1.7f, 1.7f, 1.7f));

    const glm::vec3 worldA = composition->nodeWorldTransform(*composition->findNode(a)).position;
    const glm::vec3 worldB = composition->nodeWorldTransform(*composition->findNode(b)).position;
    const std::size_t nodes = composition->nodeCount();

    // Both halves of the round trip, because the project's `parameters` block overrides the scene
    // file and this project has twice lost time to an edit that was written to only one of them.
    const fs::path sceneFile = f.dir / "edited.json";
    const fs::path projectFile = f.dir / "edited.avgen.json";
    REQUIRE(f.engine.saveComposition(sceneFile).has_value());
    REQUIRE(f.engine.saveProject(projectFile).has_value());

    SECTION("the scene file alone") {
        app::Engine fresh(app::EngineMode::Offline);
        REQUIRE(fresh.loadComposition(sceneFile).has_value());
        auto* loaded = fresh.composition();
        REQUIRE(loaded != nullptr);
        CHECK(loaded->nodeCount() == nodes);
        REQUIRE(loaded->findNode(group) != nullptr);
        CHECK(loaded->findNode(group)->kind == scene::NodeKind::Group);
        CHECK(loaded->findNode(a)->parent == group);
        const glm::vec3 backA = loaded->nodeWorldTransform(*loaded->findNode(a)).position;
        const glm::vec3 backB = loaded->nodeWorldTransform(*loaded->findNode(b)).position;
        CHECK_THAT(backA.x, Catch::Matchers::WithinAbs(worldA.x, 1e-2));
        CHECK_THAT(backA.y, Catch::Matchers::WithinAbs(worldA.y, 1e-2));
        CHECK_THAT(backA.z, Catch::Matchers::WithinAbs(worldA.z, 1e-2));
        CHECK_THAT(backB.x, Catch::Matchers::WithinAbs(worldB.x, 1e-2));
        CHECK_THAT(backB.z, Catch::Matchers::WithinAbs(worldB.z, 1e-2));
        // The scale edit came back too.
        const auto* scale = fresh.params().find("nodes/" + a + "/scale");
        REQUIRE(scale != nullptr);
        CHECK_THAT(scale->baseComponent(0), Catch::Matchers::WithinAbs(1.7, 1e-3));
    }

    SECTION("the project, which is what an offline render reloads") {
        app::Engine fresh(app::EngineMode::Offline);
        REQUIRE(fresh.loadProject(projectFile).has_value());
        auto* loaded = fresh.composition();
        REQUIRE(loaded != nullptr);
        CHECK(loaded->nodeCount() == nodes);
        const glm::vec3 backA = loaded->nodeWorldTransform(*loaded->findNode(a)).position;
        CHECK_THAT(backA.x, Catch::Matchers::WithinAbs(worldA.x, 1e-2));
        CHECK_THAT(backA.y, Catch::Matchers::WithinAbs(worldA.y, 1e-2));
        CHECK_THAT(backA.z, Catch::Matchers::WithinAbs(worldA.z, 1e-2));
    }
}

TEST_CASE("a node that is deleted and undone saves the transform it had, not the one it was born with") {
    // The trap this guards: a CompositionNode registers its parameters from its own `transform`, so
    // an editor that moved things by writing only the parameter would have the move silently
    // reverted the moment the node left the scene and came back.
    Fixture f;
    ui::EditHistory history;
    const std::string a = f.add("a", glm::vec3(0.0f));
    history.push(ui::moveNodes(f.engine, std::vector<std::string>{a}, glm::vec3(0.0f, 0.0f, 9.0f)));
    history.push(ui::deleteNodes(f.engine, std::vector<std::string>{a}));
    CHECK(history.undo(f.engine).ok());
    CHECK_THAT(f.positionOf(a).z, Catch::Matchers::WithinAbs(9.0, 1e-4));

    const fs::path file = f.dir / "restored.json";
    REQUIRE(f.engine.saveComposition(file).has_value());
    app::Engine fresh(app::EngineMode::Offline);
    REQUIRE(fresh.loadComposition(file).has_value());
    const auto* position = fresh.params().find("nodes/" + a + "/position");
    REQUIRE(position != nullptr);
    CHECK_THAT(position->baseComponent(2), Catch::Matchers::WithinAbs(9.0, 1e-3));
}

// ---- the editor as a whole -------------------------------------------------------------------------

TEST_CASE("the editor's own commands go through the history") {
    Fixture f;
    app::EditSystem edits;
    ui::WorldEditor editor;
    editor.attachEdits(edits);
    const std::string a = f.add("a", glm::vec3(-2.0f, 0.0f, 0.0f));
    const std::string b = f.add("b", glm::vec3(2.0f, 0.0f, 0.0f));
    auto* composition = f.engine.composition();

    editor.applyPick(f.engine, a, false, false);
    CHECK(editor.selection.nodes() == std::vector<std::string>{a});
    editor.applyPick(f.engine, b, true, false); // shift-click
    CHECK(editor.selection.size() == 2);

    editor.groupSelection(f.engine);
    REQUIRE(editor.selection.size() == 1);
    const std::string group = editor.selection.primary();
    CHECK(composition->findNode(group)->kind == scene::NodeKind::Group);

    // Clicking a member now selects the group; alt-clicking reaches the member itself.
    editor.applyPick(f.engine, a, false, false);
    CHECK(editor.selection.primary() == group);
    editor.applyPick(f.engine, a, false, true);
    CHECK(editor.selection.primary() == a);

    // Duplicate, then take all of it back.
    editor.selection.set(group);
    editor.duplicateSelection(f.engine);
    CHECK(composition->nodeCount() == 6);
    editor.undo(f.engine);
    CHECK(composition->nodeCount() == 3);
    editor.undo(f.engine); // the grouping
    CHECK(composition->nodeCount() == 2);
    CHECK(composition->findNode(a)->parent.empty());

    // Delete and undo.
    editor.selection.set(std::vector<std::string>{a, b});
    editor.deleteSelection(f.engine);
    CHECK(composition->nodeCount() == 0);
    editor.undo(f.engine);
    CHECK(composition->nodeCount() == 2);
    // Undo restored what was selected when the delete happened, so the artist can see what came
    // back rather than staring at a scene with nothing chosen.
    CHECK(editor.selection.size() == 2);
}

TEST_CASE("copy and paste leave the original alone") {
    Fixture f;
    app::EditSystem edits;
    ui::WorldEditor editor;
    editor.attachEdits(edits);
    const std::string a = f.add("a", glm::vec3(0.0f));
    editor.selection.set(a);
    CHECK(editor.clipboardEmpty());
    editor.copySelection(f.engine);
    CHECK_FALSE(editor.clipboardEmpty());
    editor.paste(f.engine);
    CHECK(f.engine.composition()->nodeCount() == 2);
    CHECK(editor.selection.primary() != a);
    editor.paste(f.engine); // again, from the same clipboard
    CHECK(f.engine.composition()->nodeCount() == 3);
    editor.undo(f.engine);
    editor.undo(f.engine);
    CHECK(f.engine.composition()->nodeCount() == 1);
}

// ---- §44: placing one thing must not rebuild the world ------------------------------------------------

TEST_CASE("reusing a terrain's products builds the same scene as re-meshing it") {
    // The whole risk of the cache (ADR-092) is that a reused terrain is not the terrain that would
    // have been built. So: flatten a world, add a node, flatten again, and compare the two against
    // a run with the reuse switched off. If the scene differs by a single mesh or a single chunk
    // bound, the optimisation is a bug with a benchmark attached.
    const auto build = [](bool cache) {
        app::Engine engine(app::EngineMode::Offline);
        engine.newComposition();
        scene::CompositionNode terrain;
        terrain.name = "ground";
        terrain.kind = scene::NodeKind::Terrain;
        terrain.worldMap = world::defaultWorld();
        terrain.worldMap.size = glm::vec2(160.0f, 160.0f);
        terrain.worldMap.prepare();
        terrain.terrain.chunkSize = 40.0f;
        terrain.terrain.resolution = 16;
        REQUIRE(engine.composition()->addNode(std::move(terrain)).has_value());

        if (!cache) {
            ::setenv("AVGEN_NO_TERRAIN_CACHE", "1", 1);
        } else {
            ::unsetenv("AVGEN_NO_TERRAIN_CACHE");
        }
        // First flatten.
        static_cast<void>(engine.composition()->nodeBounds("ground"));
        // A structural edit that has nothing to do with the terrain, then a second flatten.
        scene::CompositionNode orb;
        orb.name = "orb";
        orb.kind = scene::NodeKind::Orb;
        orb.transform.position = glm::vec3(3.0f, 1.0f, 0.0f);
        REQUIRE(engine.composition()->addNode(std::move(orb)).has_value());
        static_cast<void>(engine.composition()->nodeBounds("orb"));

        struct Shape {
            std::size_t meshes = 0;
            std::size_t entities = 0;
            std::size_t chunks = 0;
            std::size_t triangles = 0;
            glm::vec3 lo{0.0f};
            glm::vec3 hi{0.0f};
        } shape;
        const scene::Scene& s = engine.composition()->scene();
        shape.meshes = s.meshes.size();
        shape.entities = s.entities.size();
        const scene::CompositionNode* built = engine.composition()->findNode("ground");
        shape.chunks = built->chunks.size();
        for (const world::TerrainChunk& chunk : built->chunks) {
            shape.triangles += s.meshes[chunk.meshes[0]].indices.size() / 3;
            shape.lo = glm::min(shape.lo, chunk.boundsMin);
            shape.hi = glm::max(shape.hi, chunk.boundsMax);
        }
        return shape;
    };

    const auto reused = build(true);
    const auto rebuilt = build(false);
    ::unsetenv("AVGEN_NO_TERRAIN_CACHE");

    CHECK(reused.chunks > 0);
    CHECK(reused.triangles > 0);
    CHECK(reused.meshes == rebuilt.meshes);
    CHECK(reused.entities == rebuilt.entities);
    CHECK(reused.chunks == rebuilt.chunks);
    // The triangle count is read through the chunks' *mesh ids*, so this also proves the rebasing:
    // a cached chunk whose ids were not moved to where its meshes actually landed would index some
    // other node's geometry and the count would not match.
    CHECK(reused.triangles == rebuilt.triangles);
    CHECK_THAT(reused.lo.y, Catch::Matchers::WithinAbs(rebuilt.lo.y, 1e-4));
    CHECK_THAT(reused.hi.y, Catch::Matchers::WithinAbs(rebuilt.hi.y, 1e-4));
}

TEST_CASE("duplicating a group and one of its members does not move the member twice") {
    // `topmostOf` drops the member from the set that gets the offset, but the member is still in the
    // batch as a descendant, and an earlier version of `duplicateNodes` treated anything the caller
    // named as a root. The child's local transform is relative to its parent, which has already
    // moved, so it came out twice as far away as the group it belongs to.
    Fixture f;
    const std::string a = f.add("a", glm::vec3(-1.0f, 0.0f, 0.0f));
    const std::string b = f.add("b", glm::vec3(1.0f, 0.0f, 0.0f));
    auto* composition = f.engine.composition();
    std::string group;
    static_cast<void>(ui::groupNodes(f.engine, std::vector<std::string>{a, b}, "g", &group));

    const glm::vec3 worldA = composition->nodeWorldTransform(*composition->findNode(a)).position;
    const glm::vec3 offset(25.0f, 0.0f, 0.0f);
    std::vector<std::string> created;
    // The group *and* one of its members, which is what a shift-click and a Cmd+D produce.
    static_cast<void>(
        ui::duplicateNodes(f.engine, std::vector<std::string>{group, a}, offset, &created));

    // Find the copy of `a`: the child of the new group whose name is not `b`'s copy.
    std::string copiedGroup;
    for (const auto& node : composition->nodes()) {
        if (node && node->kind == scene::NodeKind::Group && node->name != group) {
            copiedGroup = node->name;
        }
    }
    REQUIRE_FALSE(copiedGroup.empty());
    const std::vector<std::string> children = ui::descendantsOf(*composition, copiedGroup);
    REQUIRE(children.size() == 2);
    for (const std::string& child : children) {
        const glm::vec3 world = composition->nodeWorldTransform(*composition->findNode(child)).position;
        // Every member of the copy is exactly one offset from its original, never two.
        const float dx = world.x - (child.find(a) == 0 ? worldA.x
                                                       : composition->nodeWorldTransform(
                                                             *composition->findNode(b)).position.x);
        CHECK_THAT(dx, Catch::Matchers::WithinAbs(offset.x, 1e-3));
    }
}

// ---- §3: one spatial-query surface, not one per consumer ---------------------------------------------

TEST_CASE("the ghost's terrain facts are the shared query's, to the last decimal") {
    // §3 forbids separate terrain logic per consumer, and the way a project acquires it is not by
    // deciding to -- it is by one consumer wanting a slightly different answer and writing three
    // lines instead of asking. So this asserts the brush's ground sample *is* TerrainQuery's answer,
    // which is the property that stops them drifting.
    Fixture f;
    scene::CompositionNode terrain;
    terrain.name = "ground";
    terrain.kind = scene::NodeKind::Terrain;
    terrain.worldMap = world::defaultWorld();
    terrain.worldMap.size = glm::vec2(300.0f, 300.0f);
    terrain.worldMap.prepare();
    REQUIRE(f.engine.composition()->addNode(std::move(terrain)).has_value());
    auto* composition = f.engine.composition();
    const world::TerrainQuery query = composition->terrainQuery();
    REQUIRE(query.valid());

    for (const glm::vec2 p : {glm::vec2(0.0f, 0.0f), glm::vec2(40.0f, -70.0f), glm::vec2(-120.0f, 95.0f)}) {
        const ui::GroundSample sample = ui::sampleGroundAt(*composition, p);
        const world::TerrainPoint point = query.at(p);
        CHECK_THAT(sample.position.y, Catch::Matchers::WithinAbs(point.height, 1e-5));
        CHECK_THAT(sample.waterDepth, Catch::Matchers::WithinAbs(point.waterDepth, 1e-5));
        CHECK(sample.submerged == point.water);
        CHECK_THAT(sample.canopyHeight, Catch::Matchers::WithinAbs(point.canopy, 1e-5));
        CHECK(sample.reject == point.reject);
        CHECK(sample.walkable == point.walkable);
        // And the degrees the artist sets a limit in are the degrees of the query's own normal.
        CHECK_THAT(sample.slopeDegrees,
                   Catch::Matchers::WithinAbs(glm::degrees(std::acos(std::clamp(point.normal.y, -1.0f, 1.0f))),
                                              1e-4));
    }
}

TEST_CASE("a refused placement can be nudged to somewhere the world accepts") {
    Fixture f;
    scene::CompositionNode terrain;
    terrain.name = "ground";
    terrain.kind = scene::NodeKind::Terrain;
    terrain.worldMap = world::defaultWorld();
    terrain.worldMap.size = glm::vec2(300.0f, 300.0f);
    terrain.worldMap.prepare();
    REQUIRE(f.engine.composition()->addNode(std::move(terrain)).has_value());
    auto* composition = f.engine.composition();

    // Well outside the map: refused for being out of bounds, whatever else is true of it.
    const glm::vec2 far(900.0f, 900.0f);
    CHECK_FALSE(ui::sampleGroundAt(*composition, far).insideWorld);

    assets::AssetDescriptor descriptor;
    descriptor.id = "fern";
    descriptor.naturalSize = glm::vec3(1.0f, 2.0f, 1.0f);
    descriptor.preferredScale = 2.0f;
    ui::BrushAsset asset{&descriptor, f.glb.generic_string()};

    app::PlacementSettings settings;
    settings.mode = app::PlacementMode::Single;
    settings.seed = 5u;
    settings.avoidCollisions = false;
    const ui::GroundSample outside = ui::sampleGroundAt(*composition, far);

    // Without the nudge it is refused, and the ghost says which constraint did it.
    const ui::BrushPreview refused = ui::planBrush(*composition, settings, asset, outside, 1u);
    REQUIRE(refused.instances.size() == 1);
    CHECK(refused.instances[0].issue == ui::PlacementIssue::OutsideWorld);

    // The nudge searches the world's own `nearestValidPoint`. From that far out there is nothing
    // within the search radius, so it stays refused rather than being teleported across the map --
    // which is the behaviour that matters: a snap that always succeeds is a snap that lies.
    app::PlacementSettings snapping = settings;
    snapping.snapToValid = true;
    snapping.snapSearchRadius = 10.0f;
    CHECK(ui::planBrush(*composition, snapping, asset, outside, 1u).instances[0].issue ==
          ui::PlacementIssue::OutsideWorld);

    // Just over the edge, though, it finds its way back in.
    const glm::vec2 edge(composition->findNode("ground")->worldMap.max().x + 4.0f, 0.0f);
    const ui::GroundSample justOut = ui::sampleGroundAt(*composition, edge);
    CHECK_FALSE(ui::planBrush(*composition, settings, asset, justOut, 1u).instances[0].valid());
    app::PlacementSettings reach = snapping;
    reach.snapSearchRadius = 60.0f;
    const ui::BrushPreview nudged = ui::planBrush(*composition, reach, asset, justOut, 1u);
    REQUIRE(nudged.instances.size() == 1);
    CHECK(nudged.instances[0].valid());
    // And it moved: the ghost is where the thing will actually be, not where the cursor was.
    CHECK(nudged.instances[0].position.x < edge.x);
}

// ---- regressions ---------------------------------------------------------------------------------

TEST_CASE("undoing a command that both removed and added does not collide over a recycled name") {
    // A Replace stroke erases `a` and paints something that is then given the name `a`, because
    // `uniqueName` hands out the first free one. If undo restores the old `a` while the new one is
    // still in the scene, the old one is renamed on the way in -- and every parameter path and
    // selection record in the command then names a node that is not there. The fix is to free the
    // names before handing them back: removals are taken first in *both* directions.
    Fixture f;
    ui::EditHistory history;
    const std::string original = f.add("a", glm::vec3(0.0f, 0.0f, 7.0f));
    REQUIRE(original == "a");

    ui::EditCommand replace("Replace");
    ui::EditCommand removal = ui::deleteNodes(f.engine, std::vector<std::string>{original});
    REQUIRE(removal.removed.size() == 1);
    for (ui::NodeRecord& record : removal.removed) {
        replace.removed.push_back(std::move(record));
    }
    // The name is free now, so the replacement takes it.
    scene::CompositionNode fresh;
    fresh.name = "a";
    fresh.kind = scene::NodeKind::Gltf;
    fresh.asset = f.glb.generic_string();
    fresh.transform.position = glm::vec3(0.0f, 0.0f, -7.0f);
    std::vector<scene::CompositionNode> batch;
    batch.push_back(std::move(fresh));
    std::vector<std::string> created;
    ui::EditCommand placed = ui::placeNodes(f.engine, std::move(batch), "Replace", &created);
    REQUIRE(created.size() == 1);
    CHECK(created.front() == "a"); // the collision this test is about
    for (ui::NodeRecord& record : placed.added) {
        replace.added.push_back(std::move(record));
    }
    history.push(std::move(replace));

    auto* composition = f.engine.composition();
    CHECK(composition->nodeCount() == 1);
    CHECK_THAT(f.positionOf("a").z, Catch::Matchers::WithinAbs(-7.0, 1e-4));

    const ui::EditApply undone = history.undo(f.engine);
    CHECK(undone.ok()); // no "came back as" problem
    CHECK(composition->nodeCount() == 1);
    REQUIRE(composition->findNode("a") != nullptr);
    // And it is the *original* that came back, at the position the original had.
    CHECK_THAT(f.positionOf("a").z, Catch::Matchers::WithinAbs(7.0, 1e-4));

    const ui::EditApply redone = history.redo(f.engine);
    CHECK(redone.ok());
    CHECK_THAT(f.positionOf("a").z, Catch::Matchers::WithinAbs(-7.0, 1e-4));
}

TEST_CASE("rotating an object inside a turned group turns it about the world axis, not its parent's") {
    // The gizmo's delta is world space; the parameter it writes is the node's *local* rotation.
    // Multiplying them directly is right only when the parent chain is unrotated, and wrong by
    // exactly the parent's rotation when it is not.
    Fixture f;
    const std::string a = f.add("a", glm::vec3(-2.0f, 0.0f, 0.0f));
    const std::string b = f.add("b", glm::vec3(2.0f, 0.0f, 0.0f));
    auto* composition = f.engine.composition();
    std::string group;
    static_cast<void>(ui::groupNodes(f.engine, std::vector<std::string>{a, b}, "g", &group));
    // Turn the group a quarter turn about X -- deliberately a different axis from the one the drag
    // will use. A parent turned about the *same* axis commutes with the delta and the wrong formula
    // gives the right answer, which is how this bug survives a test written without thinking about
    // it: the first version of this one rotated the group about Y and passed either way.
    ui::setNodeRotation(f.engine, group, glm::vec3(90.0f, 0.0f, 0.0f));

    app::EditSystem edits;
    ui::WorldEditor editor;
    editor.attachEdits(edits);
    editor.gizmoMode = ui::GizmoMode::Rotate;
    editor.localSpace = false; // the world's Y, which is the axis the handle stands for
    editor.selection.set(a);

    const scene::Camera camera = lookingDown();
    const float aspect = 16.0f / 9.0f;
    ui::EditorInput input;
    input.overCanvas = true;

    // One frame to settle the gizmo frame, then read it back and aim at its Y ring.
    editor.update(f.engine, nullptr, camera, aspect, input);
    REQUIRE(editor.visuals().showGizmo);
    const ui::GizmoFrame frame = editor.visuals().gizmo;
    const std::vector<glm::vec3> ring = ui::rotationRing(frame, ui::GizmoHandle::AxisY, 48);
    REQUIRE(ring.size() == 48);

    const glm::quat worldBefore = composition->nodeWorldTransform(*composition->findNode(a)).rotation;

    // Pressed at 45 degrees round the ring rather than at 0. The three rotation rings intersect
    // wherever one of them crosses another's plane -- the Y ring's zero point lies on the X ring
    // too -- and at a crossing the picker takes the first axis it tested. Anywhere between the
    // crossings names exactly one ring.
    REQUIRE(ui::pickHandle(camera, aspect, frame, ui::GizmoMode::Rotate,
                           ui::projectPoint(camera, aspect, ring[6]).ndc,
                           0.033f) == ui::GizmoHandle::AxisY);
    input.ndc = ui::projectPoint(camera, aspect, ring[6]).ndc;
    input.leftPressed = true;
    input.leftDown = true;
    editor.update(f.engine, nullptr, camera, aspect, input);

    input.leftPressed = false;
    input.ndc = ui::projectPoint(camera, aspect, ring[12]).ndc; // a further eighth: 45 degrees on
    editor.update(f.engine, nullptr, camera, aspect, input);

    const glm::quat worldAfter = composition->nodeWorldTransform(*composition->findNode(a)).rotation;
    // The world-space change is a rotation about the world's Y and nothing else.
    const glm::quat delta = worldAfter * glm::inverse(worldBefore);
    const glm::vec3 axis = glm::axis(glm::normalize(delta));
    const float degrees = glm::degrees(glm::angle(glm::normalize(delta)));
    CHECK_THAT(degrees, Catch::Matchers::WithinAbs(45.0, 3.0));
    CHECK_THAT(std::abs(axis.y), Catch::Matchers::WithinAbs(1.0, 0.02));
    CHECK_THAT(axis.x, Catch::Matchers::WithinAbs(0.0, 0.02));
    CHECK_THAT(axis.z, Catch::Matchers::WithinAbs(0.0, 0.02));

    input.leftDown = false;
    input.leftReleased = true;
    editor.update(f.engine, nullptr, camera, aspect, input);
    CHECK(editor.history().undoSize() == 1); // and the whole drag is one thing to undo
}

TEST_CASE("A drag that began on a panel does not open a selection box", "[ui][editor][box]") {
    // The bug: dragging a slider in the Parameters panel, or scrubbing the sequencer's timeline,
    // opened a selection box across the world.
    //
    // `EditorInput::leftDown` is a global fact about the mouse -- the button is down, somewhere --
    // and the box only ever asked that. Its *start* correctly required a press that landed on the
    // canvas, so a press on a panel left `boxFrom_` holding the last canvas press's position; then
    // the next frame saw "button down, and the pointer is a long way from boxFrom_" and called that
    // a drag. The further the panel from wherever you last clicked on the world, the more certain.
    Fixture f;
    f.add("a", glm::vec3(-2.0f, 0.0f, 0.0f));
    f.add("b", glm::vec3(2.0f, 0.0f, 0.0f));

    app::EditSystem edits;
    ui::WorldEditor editor;
    editor.attachEdits(edits);
    const scene::Camera camera = lookingDown();
    const float aspect = 16.0f / 9.0f;

    // A real box first, so `boxFrom_` holds a live position -- which is what made the bug reachable.
    ui::EditorInput input;
    input.overCanvas = true;
    input.ndc = glm::vec2(-0.8f, -0.8f);
    input.leftPressed = true;
    input.leftDown = true;
    editor.update(f.engine, nullptr, camera, aspect, input);
    input.leftPressed = false;
    input.ndc = glm::vec2(0.8f, 0.8f);
    editor.update(f.engine, nullptr, camera, aspect, input);
    CHECK(editor.visuals().boxing); // the ordinary case still works
    input.leftDown = false;
    input.leftReleased = true;
    editor.update(f.engine, nullptr, camera, aspect, input);
    CHECK_FALSE(editor.visuals().boxing);

    // The legitimate box above selected what it swept, which is the point of it. Cleared here so
    // the checks below are about the phantom box and nothing else.
    editor.selection.clear();

    // Now a press that never touches the world: the button goes down over a panel, so `leftPressed`
    // is true but `overCanvas` is false -- exactly what ImGui reports when a slider takes the press,
    // or when a panel is being dragged by its tab.
    input.leftReleased = false;
    input.overCanvas = false;
    input.ndc = glm::vec2(-0.9f, 0.2f);
    input.leftPressed = true;
    input.leftDown = true;
    editor.update(f.engine, nullptr, camera, aspect, input);
    CHECK_FALSE(editor.visuals().boxing);

    // Held, and travelling far -- a slider drag. Nothing on the world may open.
    input.leftPressed = false;
    for (const float x : {-0.5f, 0.0f, 0.5f, 0.9f}) {
        input.ndc = glm::vec2(x, 0.2f);
        editor.update(f.engine, nullptr, camera, aspect, input);
        INFO("panel drag reached ndc x " << x);
        CHECK_FALSE(editor.visuals().boxing);
        CHECK_FALSE(editor.wantsMouse());
    }

    // And releasing it selects nothing, rather than catching whatever the phantom box swept.
    input.leftDown = false;
    input.leftReleased = true;
    editor.update(f.engine, nullptr, camera, aspect, input);
    CHECK(editor.selection.empty());
}

TEST_CASE("A history state has an identity, not just a depth", "[ui][editor][history]") {
    // What "is this what was saved?" needs. Depth cannot answer it: undo twice, make a different
    // edit, and the stack stands at the same height holding an entirely different document. A save
    // marker compared against a count would call that saved. So each command names the state it
    // produced, and the state is named by the newest one.
    Fixture f;
    const std::string a = f.add("a", glm::vec3(0.0f));
    const std::vector<std::string> one{a};
    ui::EditHistory history;

    const std::uint64_t empty = history.stateId();
    history.push(ui::moveNodes(f.engine, one, glm::vec3(1.0f, 0.0f, 0.0f)));
    const std::uint64_t afterFirst = history.stateId();
    CHECK(afterFirst != empty);

    history.push(ui::moveNodes(f.engine, one, glm::vec3(0.0f, 1.0f, 0.0f)));
    const std::uint64_t afterSecond = history.stateId();
    CHECK(afterSecond != afterFirst);

    SECTION("undo returns to the state that was there before, not to a new one") {
        history.undo(f.engine);
        CHECK(history.stateId() == afterFirst);
        history.undo(f.engine);
        CHECK(history.stateId() == empty);
        history.redo(f.engine);
        CHECK(history.stateId() == afterFirst);
        history.redo(f.engine);
        CHECK(history.stateId() == afterSecond);
    }

    SECTION("a different edit at the same depth is a different state") {
        // The case the save marker exists for. Both stacks are one deep; the documents are not the
        // same document, and nothing may call the second one saved because the first one was.
        history.undo(f.engine);
        REQUIRE(history.undoSize() == 1);
        history.push(ui::moveNodes(f.engine, one, glm::vec3(0.0f, 0.0f, 9.0f)));
        // Two deep again -- the same depth the second edit reached -- holding a different document.
        // A save marker that compared depths would call this saved.
        CHECK(history.undoSize() == 2);
        CHECK(history.stateId() != afterSecond);
        CHECK(history.stateId() != afterFirst);
        CHECK_FALSE(history.canRedo()); // and the old branch is gone
    }

    SECTION("clearing is a new identity, not a return to the beginning") {
        // A project was loaded or a scene swapped: the empty history now describes a different
        // document, and a marker taken before the clear must not match after it.
        history.clear();
        CHECK(history.stateId() != empty);
        CHECK(history.stateId() != afterFirst);
        CHECK(history.stateId() != afterSecond);
    }

    SECTION("the revision moves on every change, so a panel can tell without re-reading") {
        const std::uint64_t r0 = history.revision();
        history.undo(f.engine);
        const std::uint64_t r1 = history.revision();
        CHECK(r1 != r0);
        history.redo(f.engine);
        CHECK(history.revision() != r1);
        history.clear();
        CHECK(history.revision() != r0);
    }
}

TEST_CASE("Trimming moves what the empty stack means", "[ui][editor][history]") {
    // With a command dropped off the bottom, "undo everything" no longer lands on the document the
    // session opened with -- it lands on the document as it stood after the dropped command. If the
    // empty-stack identity did not move with it, a save marker could match a state the history can
    // no longer reach, and the project would claim to be saved when it is not.
    Fixture f;
    const std::string a = f.add("a", glm::vec3(0.0f));
    const std::vector<std::string> one{a};
    ui::EditHistory history(2); // capacity two, so the third push drops the first

    const std::uint64_t openedWith = history.stateId();
    history.push(ui::moveNodes(f.engine, one, glm::vec3(1.0f, 0.0f, 0.0f)));
    history.push(ui::moveNodes(f.engine, one, glm::vec3(1.0f, 0.0f, 0.0f)));
    CHECK(history.undoSize() == 2);
    CHECK(history.stateId() != openedWith);

    history.push(ui::moveNodes(f.engine, one, glm::vec3(1.0f, 0.0f, 0.0f)));
    CHECK(history.undoSize() == 2); // capacity held

    history.undo(f.engine);
    history.undo(f.engine);
    CHECK_FALSE(history.canUndo());
    // Back as far as the history goes -- and that is not where the session started.
    CHECK(history.stateId() != openedWith);
}
