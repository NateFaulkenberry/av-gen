// ADR-1042 (the journey) and ADR-1043 (the palette): the periodic path, the invisible wrap, the collision
// guard, nodes on the journey, the example world's clearance, and the palette's perceptual blend -- all
// on the CPU, and the engine-level properties the art direction depends on (seek equals play).

#include "app/engine.hpp"
#include "core/time.hpp"
#include "params/palette.hpp"
#include "scene/composition.hpp"
#include "scene/journey.hpp"
#include "spatial/sdf.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <glm/gtc/quaternion.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

using namespace avgen;
using Catch::Matchers::WithinAbs;

namespace {

scene::JourneySettings corridorSettings() {
    scene::JourneySettings s;
    s.path = {{0.0f, 0.0f, 0.0f}, {4.0f, 0.0f, 0.6f}, {8.0f, 1.0f, 0.0f}};
    s.screw.translation = {12.0f, 1.5f, 0.0f};
    return s;
}

float dist(const glm::vec3& a, const glm::vec3& b) {
    return glm::length(a - b);
}

nlohmann::json readJson(const std::filesystem::path& p) {
    std::ifstream in(p);
    REQUIRE(in.good());
    return nlohmann::json::parse(in);
}

} // namespace

TEST_CASE("Journey: the path continues through the screw with a continuous position and tangent",
          "[liminal][journey][adr1042]") {
    auto built = scene::JourneyPath::build(corridorSettings());
    REQUIRE(built.has_value());
    const scene::JourneyPath& path = *built;
    const double L = path.cellLength();
    REQUIRE(L > 12.0);
    // Periodic: one cell on is the screw applied.
    for (double s = 0.0; s < L; s += 0.37) {
        const auto a = path.sample(s);
        const auto b = path.sample(s + L);
        CHECK(dist(b.position, a.position + glm::vec3(12.0f, 1.5f, 0.0f)) < 1e-3f);
    }
    // Continuous through every seam: a small step moves a small distance, the tangent turns a little.
    for (int k = -2; k <= 3; ++k) {
        const double seam = k * L;
        const auto before = path.sample(seam - 0.01);
        const auto after = path.sample(seam + 0.01);
        CHECK(dist(before.position, after.position) < 0.021f);
        CHECK(glm::dot(before.tangent, after.tangent) > 0.999f);
    }
    // Arc length: sampling 1 m apart moves about a metre.
    for (double s = 0.3; s < 2.0 * L; s += 1.0) {
        CHECK_THAT(static_cast<double>(dist(path.sample(s).position, path.sample(s + 1.0).position)), WithinAbs(1.0, 0.02));
    }
}

TEST_CASE("Journey: the wrap is invisible -- a camera one wrap on sees the same frame", "[liminal][journey][adr1042]") {
    scene::JourneySettings helix;
    helix.path = {{6.0f, 0.0f, -2.0f}, {6.5f, 0.5f, 0.0f}, {6.0f, 1.0f, 2.0f}};
    helix.screw.count = 4;
    helix.screw.translation = {0.0f, 2.0f, 0.0f};
    auto built = scene::JourneyPath::build(helix);
    REQUIRE(built.has_value());
    CHECK(built->wrapCells() == 4);
    for (const double d : {0.0, 3.3, 11.7, 27.0}) {
        scene::JourneyView v;
        v.distance = d;
        v.yawDegrees = 20.0f; // (the bob follows the unwrapped distance so it never pops at the wrap)
        const auto a = scene::journeyPose(*built, v);
        v.distance = d + built->wrapLength();
        const auto b = scene::journeyPose(*built, v);
        CHECK(dist(a.eye, b.eye) < 2e-3f);
        CHECK(dist(a.target, b.target) < 2e-3f);
        // ... while the unwrapped path really did climb one full turn.
        CHECK_THAT(static_cast<double>(built->sample(d + built->wrapLength()).position.y - built->sample(d).position.y),
                   WithinAbs(8.0, 1e-3));
    }
    scene::JourneySettings bad = helix;
    bad.wrapCells = 3;
    CHECK_FALSE(scene::JourneyPath::build(bad).has_value());
    bad.wrapCells = 0;
    bad.path.resize(1);
    CHECK_FALSE(scene::JourneyPath::build(bad).has_value());
}

TEST_CASE("Journey: yaw turns left, pitch looks up, the bob follows distance not time", "[liminal][journey][adr1042]") {
    scene::JourneySettings s;
    s.path = {{0.0f, 0.0f, 0.0f}, {10.0f, 0.0f, 0.0f}};
    s.screw.translation = {20.0f, 0.0f, 0.0f};
    auto built = scene::JourneyPath::build(s);
    REQUIRE(built.has_value());
    scene::JourneyView v;
    v.distance = 5.0;
    auto pose = scene::journeyPose(*built, v);
    const glm::vec3 fwd = glm::normalize(pose.target - pose.eye);
    CHECK(fwd.x > 0.99f);
    CHECK_THAT(static_cast<double>(pose.eye.y), WithinAbs(1.6, 1e-4));
    v.yawDegrees = 90.0f;
    pose = scene::journeyPose(*built, v);
    CHECK(glm::normalize(pose.target - pose.eye).z < -0.99f); // +X turned left is -Z
    v.yawDegrees = 0.0f;
    v.pitchDegrees = 30.0f;
    pose = scene::journeyPose(*built, v);
    CHECK_THAT(static_cast<double>(glm::normalize(pose.target - pose.eye).y), WithinAbs(0.5, 1e-3));
    v.pitchDegrees = 0.0f;
    v.bob = 0.05f;
    v.stride = 2.0f;
    v.time = 0.0;
    const float y0 = scene::journeyPose(*built, v).eye.y;
    v.time = 7.3; // time alone does not move a walker who has stopped
    CHECK(scene::journeyPose(*built, v).eye.y == y0);
}

TEST_CASE("Journey: every chapter of the example is clear of its architecture at the deformations' maxima",
          "[liminal][journey][adr1042]") {
    const std::filesystem::path dir = std::filesystem::path(AVGEN_SOURCE_DIR) / "examples" / "liminal";
    const nlohmann::json doc = readJson(dir / "liminal.scene.json");
    auto journey = scene::Journey::fromJson(doc.at("camera").at("journey"));
    REQUIRE(journey.has_value());
    REQUIRE(journey->size() >= 2);
    const auto set = [](nlohmann::json& node, const std::string& name, const char* field, const nlohmann::json& value,
                        const auto& self) -> void {
        if (node.value("name", "") == name) {
            node[field] = value;
        }
        if (node.contains("children")) {
            for (auto& c : node["children"]) {
                self(c, name, field, value, self);
            }
        }
    };
    for (std::size_t c = 0; c < journey->size(); ++c) {
        const scene::JourneyChapter& chapter = journey->chapter(c);
        const scene::JourneyPath& path = journey->path(c);
        nlohmann::json treeJson;
        for (const auto& n : doc.at("nodes")) {
            if (n.value("name", "") == chapter.world.collide) {
                treeJson = n.at("sdf").at("tree");
            }
        }
        REQUIRE(!treeJson.is_null());
        // A near-field tremble may wrap the world (above the screw); its amplitude is centimetres.
        float trembleAllowance = 0.0f;
        if (treeJson["root"]["kind"] == "warp" && treeJson["root"].value("count", 0) == 1) {
            trembleAllowance = 0.06f;
            treeJson["root"] = treeJson["root"]["children"][0];
        }
        REQUIRE(treeJson["root"]["kind"] == "screw");
        float worst = 1e9f;
        double worstAt = 0.0;
        glm::vec3 worstEye(0.0f);
        // The deformations at the limits the example's routes and keys can reach: the breath at its base
        // plus the full bass route (0.08 + 0.22), at several phases; the room small and grown.
        for (const float amount : {0.08f, 0.30f}) {
            for (const float phase : {0.0f, 1.7f, 4.1f}) {
                for (const bool grown : {false, true}) {
                    nlohmann::json t = treeJson;
                    set(t["root"], "breath", "amount", amount, set);
                    set(t["root"], "breath", "translation", nlohmann::json::array({phase, 0.0f, phase * 0.6f}), set);
                    if (grown) {
                        set(t["root"], "room", "size", nlohmann::json::array({7.15f, 7.15f, 10.15f}), set);
                        set(t["root"], "roomAt", "translation", nlohmann::json::array({4.0f, 7.0f, 0.0f}), set);
                    }
                    // The true distance: the screw's cell content in the cells round the eye (the screw
                    // alone evaluates only the eye's own cell, and its seam guard caps at the boundary).
                    auto content = spatial::SdfTree::fromJson(nlohmann::json{{"root", t["root"]["children"][0]}});
                    REQUIRE(content.has_value());
                    for (double d = 0.0; d < 2.0 * path.cellLength(); d += 0.1) {
                        scene::JourneyView v;
                        v.distance = d; // chapter-local: the pose is in the chapter's own frame
                        const auto pose = scene::journeyPose(path, v);
                        float clearance = 1e9f;
                        for (int k = -3; k <= 3; ++k) {
                            clearance = std::min(clearance,
                                                 content->evaluate(chapter.world.screw.applyPoint(pose.eye, -k), 0.0));
                        }
                        if (clearance < worst) {
                            worst = clearance;
                            worstAt = d;
                            worstEye = pose.eye;
                        }
                    }
                }
            }
        }
        INFO("chapter '" << chapter.name << "': closest approach " << worst << " m at distance " << worstAt << " m, eye ("
                         << worstEye.x << ", " << worstEye.y << ", " << worstEye.z << ") (radius "
                         << chapter.world.radius << ")");
        CHECK(worst > chapter.world.radius + trembleAllowance);
    }
}

TEST_CASE("Journey chapters: the camera changes world at a chapter's start, nodes and lights follow the chapter",
          "[liminal][journey][adr1042]") {
    auto journey = scene::Journey::fromJson(nlohmann::json::parse(R"({ "chapters": [
        { "name": "a", "start": 0, "path": [[0, 0, 0], [10, 0, 0]], "screw": { "translation": [20, 0, 0] },
          "nodes": ["worldA", "shared"], "lights": ["lampA"] },
        { "name": "b", "start": 30, "from": 5, "path": [[0, 0, 0], [0, 0, 10]], "screw": { "translation": [0, 0, 20] },
          "offset": [0, 0, 500], "yaw": 90, "nodes": ["worldB", "shared"] } ] })"));
    REQUIRE(journey.has_value());
    CHECK(journey->chapterAt(0.0) == 0);
    CHECK(journey->chapterAt(29.99) == 0);
    CHECK(journey->chapterAt(30.0) == 1);
    CHECK(journey->localDistance(1, 31.0) == 6.0);
    scene::JourneyView v;
    v.distance = 12.0;
    const auto a = journey->pose(v);
    CHECK(dist(a.eye, {12.0f, 1.6f, 0.0f}) < 1e-3f);
    v.distance = 31.0; // chapter b at local 6: (0, 0, 6) in its path frame, turned 90 degrees and moved 500 m
    const auto b = journey->pose(v);
    const glm::vec3 local(0.0f, 1.6f, 6.0f);
    const glm::vec3 turned = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f)) * local;
    CHECK(dist(b.eye, turned + glm::vec3(0.0f, 0.0f, 500.0f)) < 1e-3f);
    // Nodes: a chapter's nodes only while it is active; unlisted nodes always; a shared node always.
    CHECK(journey->nodeActive("worldA", 10.0));
    CHECK_FALSE(journey->nodeActive("worldB", 10.0));
    CHECK_FALSE(journey->nodeActive("worldA", 40.0));
    CHECK(journey->nodeActive("worldB", 40.0));
    CHECK(journey->nodeActive("shared", 10.0));
    CHECK(journey->nodeActive("shared", 40.0));
    CHECK(journey->nodeActive("figure", 40.0));
    CHECK(journey->lightActive("lampA", 10.0));
    CHECK_FALSE(journey->lightActive("lampA", 40.0));
    // A round trip through JSON keeps the chapters.
    auto again = scene::Journey::fromJson(journey->toJson());
    REQUIRE(again.has_value());
    CHECK(again->size() == 2);
    CHECK(dist(again->pose(v).eye, b.eye) < 1e-5f);
}

namespace {

// A plane wall the path runs straight into, to show the guard: the journey passes x = 5 where a wall
// stands at z = 0.2 with the path at z = 0.
constexpr const char* kGuardScene = R"({ "format": "avgen-scene", "version": 1, "name": "guard",
  "camera": { "mode": 3, "journey": { "path": [[0, 0, 0], [10, 0, 0]], "screw": { "translation": [20, 0, 0] },
                                      "collide": "wall", "radius": 0.5 } },
  "nodes": [
    { "name": "wall", "kind": "sdf", "sdf": { "tree": { "root": { "kind": "translate", "translation": [5, 1.6, 0.2],
        "children": [ { "kind": "box", "size": [1, 1, 0.05] } ] } }, "boundsMin": [-50, -50, -50], "boundsMax": [50, 50, 50] } },
    { "name": "guide", "kind": "group", "journey": { "distance": 7.0 }, "position": [1.0, 0.5, 0] }
  ] })";

} // namespace

TEST_CASE("Journey camera in a composition: parameters place it, the guard keeps it out of the SDF, nodes ride along",
          "[liminal][journey][adr1042]") {
    app::Engine engine{app::EngineMode::Offline};
    REQUIRE(engine.setCompositionJson(nlohmann::json::parse(kGuardScene)).has_value());
    auto* distance = engine.params().findAs<float>("camera/journey/distance");
    REQUIRE(distance != nullptr);
    REQUIRE(engine.params().find("nodes/guide/journey/distance") != nullptr);
    distance->setBase(2.0f);
    engine.update(FrameTime{0.0, 0.0, 0});
    CHECK(dist(engine.scene().camera.position, {2.0f, 1.6f, 0.0f}) < 1e-3f);
    // At x = 5 the eye would be 0.15 m from the wall's face (z = 0.15); the guard puts it 0.5 m away.
    distance->setBase(5.0f);
    engine.update(FrameTime{1.0 / 60.0, 1.0 / 60.0, 1});
    const glm::vec3 eye = engine.scene().camera.position;
    CHECK_THAT(static_cast<double>(eye.z), WithinAbs(0.15 - 0.5, 2e-3));
    CHECK_THAT(static_cast<double>(eye.x), WithinAbs(5.0, 1e-3));
    // The node on the journey: its position is an offset in the path frame (right = +Z along +X).
    const auto* comp = engine.composition();
    REQUIRE(comp != nullptr);
    const auto* guide = comp->findNode("guide");
    REQUIRE(guide != nullptr);
    const scene::Transform t = comp->nodeWorldTransform(*guide);
    CHECK(dist(t.position, {7.0f, 0.5f, 1.0f}) < 1e-3f);
    // Wrapped with the camera: one wrap on (20 m), both the camera and the node come back to the same place.
    distance->setBase(2.0f + 20.0f);
    engine.params().findAs<float>("nodes/guide/journey/distance")->setBase(27.0f);
    engine.update(FrameTime{2.0 / 60.0, 1.0 / 60.0, 2});
    CHECK(dist(engine.scene().camera.position, {2.0f, 1.6f, 0.0f}) < 1e-3f);
    CHECK(dist(comp->nodeWorldTransform(*guide).position, {7.0f, 0.5f, 1.0f}) < 1e-3f);
}

TEST_CASE("Palette: OKLab round trip, states at integer positions, a perceptual middle, saturation and value",
          "[liminal][palette][adr1043]") {
    for (const glm::vec3 c : {glm::vec3(0.2f, 0.5f, 0.9f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.02f, 0.03f, 0.01f)}) {
        const glm::vec3 back = params::Palette::fromOklab(params::Palette::toOklab(c));
        CHECK(dist(back, c) < 1e-4f);
    }
    auto p = params::Palette::fromJson(nlohmann::json::parse(R"({
        "states": [ { "name": "teal", "colors": { "wall": [0.05, 0.45, 0.45] }, "scalars": { "glow": 1.0 } },
                    { "name": "amber", "colors": { "wall": [0.9, 0.45, 0.05] }, "scalars": { "glow": 3.0 } },
                    { "name": "grey", "colors": {} } ],
        "bindings": [ { "role": "wall", "target": "x" } ] })"));
    REQUIRE(p.has_value());
    CHECK(dist(p->color("wall", 0.0f, 1.0f, 1.0f), {0.05f, 0.45f, 0.45f}) < 1e-4f);
    CHECK(dist(p->color("wall", 1.0f, 1.0f, 1.0f), {0.9f, 0.45f, 0.05f}) < 1e-4f);
    CHECK(dist(p->color("wall", 2.0f, 1.0f, 1.0f), {0.9f, 0.45f, 0.05f}) < 1e-4f); // grey inherits the nearest
    CHECK(dist(p->color("wall", 9.0f, 1.0f, 1.0f), p->color("wall", 2.0f, 1.0f, 1.0f)) < 1e-6f); // clamped
    // The middle is the OKLab midpoint, not the RGB one.
    const glm::vec3 mid = p->color("wall", 0.5f, 1.0f, 1.0f);
    const glm::vec3 labMid = 0.5f * (params::Palette::toOklab({0.05f, 0.45f, 0.45f}) + params::Palette::toOklab({0.9f, 0.45f, 0.05f}));
    CHECK(dist(params::Palette::toOklab(mid), labMid) < 1e-4f);
    CHECK(dist(mid, {0.475f, 0.45f, 0.25f}) > 0.02f);
    // Saturation 0 is grey; value scales lightness.
    const glm::vec3 grey = p->color("wall", 0.3f, 0.0f, 1.0f);
    CHECK(std::fabs(grey.r - grey.g) < 2e-3f);
    CHECK(std::fabs(grey.g - grey.b) < 2e-3f);
    CHECK_THAT(static_cast<double>(params::Palette::toOklab(p->color("wall", 0.3f, 1.0f, 0.5f)).x),
               WithinAbs(0.5 * static_cast<double>(params::Palette::toOklab(p->color("wall", 0.3f, 1.0f, 1.0f)).x), 1e-4));
    CHECK_THAT(static_cast<double>(p->scalar("glow", 0.25f)), WithinAbs(1.5, 1e-5));
    CHECK_FALSE(params::Palette::fromJson(nlohmann::json::parse(R"({"states": []})")).has_value());
}

namespace {

constexpr const char* kPaletteScene = R"({ "format": "avgen-scene", "version": 1, "name": "palette",
  "nodes": [ { "name": "room", "kind": "sdf", "sdf": { "tree": { "root": { "kind": "box", "size": [1, 1, 1] } } } } ] })";

std::filesystem::path paletteProject() {
    const auto dir = testsupport::processTempDir() / "liminal_palette";
    std::filesystem::create_directories(dir);
    {
        std::ofstream(dir / "palette.scene.json") << kPaletteScene;
    }
    const nlohmann::json project = nlohmann::json::parse(R"({
      "format": "avgen-project", "version": 4, "app": { "name": "avgen", "version": "0.1.0" },
      "assets": { "scene": { "kind": "composition", "path": "palette.scene.json" } },
      "timeline": { "enabled": true, "cues": [], "tracks": [
        { "target": "palette/position", "component": -1, "timeBase": "seconds", "mode": "replace",
          "keys": [ { "time": 0.0, "value": [0.0], "interp": "smooth" }, { "time": 2.0, "value": [1.0], "interp": "smooth" } ] } ] },
      "palette": {
        "states": [ { "name": "cool", "colors": { "wall": [0.1, 0.2, 0.5], "fog": [0.01, 0.02, 0.04] } },
                    { "name": "warm", "colors": { "wall": [0.7, 0.35, 0.1], "fog": [0.05, 0.02, 0.01] } } ],
        "bindings": [ { "role": "wall", "target": "sdf/room/material/baseColor" },
                      { "role": "fog", "target": "scene/fogColor" } ],
        "saturation": 0.8 } })");
    std::ofstream(dir / "palette.json") << project.dump(1);
    return dir / "palette.json";
}

glm::vec3 finalColor(app::Engine& e, const char* path) {
    const auto* p = e.params().find(path);
    REQUIRE(p != nullptr);
    return {p->finalComponent(0), p->finalComponent(1), p->finalComponent(2)};
}

} // namespace

TEST_CASE("Palette in the engine: the timeline moves it, it writes its targets, a seek lands where a play does, "
          "and it survives a save", "[liminal][palette][adr1043]") {
    const auto project = paletteProject();
    app::Engine played{app::EngineMode::Offline};
    REQUIRE(played.loadProject(project).has_value());
    REQUIRE(played.params().find("palette/position") != nullptr);
    glm::vec3 at1s{};
    for (int k = 0; k <= 60; ++k) {
        played.update(FrameTime{k / 60.0, k == 0 ? 0.0 : 1.0 / 60.0, static_cast<std::uint64_t>(k)});
    }
    at1s = finalColor(played, "sdf/room/material/baseColor");
    // Half-way through the key (smooth: 0.5 at 1 s), at saturation 0.8.
    params::Palette ref;
    ref.states = {{"cool", {{"wall", {0.1f, 0.2f, 0.5f}}}, {}}, {"warm", {{"wall", {0.7f, 0.35f, 0.1f}}}, {}}};
    CHECK(dist(at1s, ref.color("wall", 0.5f, 0.8f, 1.0f)) < 1e-4f);
    CHECK(dist(finalColor(played, "scene/fogColor"), {0.01f, 0.02f, 0.04f}) > 1e-3f);

    app::Engine seeked{app::EngineMode::Offline};
    REQUIRE(seeked.loadProject(project).has_value());
    seeked.seekSeconds(1.0);
    seeked.update(FrameTime{1.0, 0.0, 0});
    CHECK(finalColor(seeked, "sdf/room/material/baseColor") == at1s);

    const auto copy = project.parent_path() / "saved.json";
    REQUIRE(played.saveProject(copy).has_value());
    const nlohmann::json saved = readJson(copy);
    REQUIRE(saved.contains("palette"));
    CHECK(saved["palette"]["states"].size() == 2);
    CHECK(saved["palette"]["saturation"].get<float>() == 0.8f);
}

TEST_CASE("Journey look-at: the gaze eases to a world point and holds still across a wrap", "[liminal][journey][adr1042]") {
    scene::JourneySettings s;
    s.path = {{0.0f, 0.0f, 0.0f}, {10.0f, 0.0f, 0.0f}};
    s.screw.translation = {20.0f, 0.0f, 0.0f};
    auto built = scene::JourneyPath::build(s);
    REQUIRE(built.has_value());
    scene::JourneyView v;
    v.distance = 5.0;
    v.lookAt = {5.0f, 1.6f, -10.0f}; // straight to the left
    v.lookAtWeight = 1.0f;
    auto pose = scene::journeyPose(*built, v);
    CHECK(glm::normalize(pose.target - pose.eye).z < -0.999f);
    v.lookAtWeight = 0.5f;
    pose = scene::journeyPose(*built, v);
    const glm::vec3 half = glm::normalize(pose.target - pose.eye);
    CHECK_THAT(static_cast<double>(half.x), WithinAbs(static_cast<double>(-half.z), 1e-3)); // 45 degrees
    // One wrap on, the same point one wrap on (the unwrapped frame) gives the same aim.
    v.distance = 25.0;
    v.lookAt = {25.0f, 1.6f, -10.0f};
    const glm::vec3 again = glm::normalize(scene::journeyPose(*built, v).target - scene::journeyPose(*built, v).eye);
    CHECK(dist(again, half) < 1e-4f);
}

TEST_CASE("Node tint: an opt-in colour multiplier a palette can bind", "[liminal][palette][adr1044]") {
    app::Engine engine{app::EngineMode::Offline};
    REQUIRE(engine.setCompositionJson(nlohmann::json::parse(R"({ "format": "avgen-scene", "version": 1, "name": "tint",
        "nodes": [ { "kind": "orb", "name": "plain", "position": [0, 0, 0] },
                   { "kind": "orb", "name": "figure", "position": [3, 0, 0], "tint": [0.5, 0.25, 1.0] } ] })"))
                .has_value());
    CHECK(engine.params().find("nodes/plain/tint") == nullptr);
    auto* tint = engine.params().findAs<glm::vec3>("nodes/figure/tint");
    REQUIRE(tint != nullptr);
    engine.update(FrameTime{0.0, 0.0, 0});
    const auto* comp = engine.composition();
    REQUIRE(comp != nullptr);
    glm::vec3 plain(0.0f);
    glm::vec3 tinted(0.0f);
    for (const auto& e : engine.scene().entities) {
        if (e.transform.position.x < 1.5f) {
            plain = e.material.baseColor;
        } else {
            tinted = e.material.baseColor;
        }
    }
    CHECK(dist(tinted, plain * glm::vec3(0.5f, 0.25f, 1.0f)) < 1e-4f);
    tint->setBase({0.1f, 0.1f, 0.1f});
    engine.update(FrameTime{1.0 / 60.0, 1.0 / 60.0, 1});
    for (const auto& e : engine.scene().entities) {
        if (e.transform.position.x > 1.5f) {
            CHECK(dist(e.material.baseColor, plain * 0.1f) < 1e-4f);
        }
    }
}

// The music video (tools/liminal/make_all_you_got.py): every chapter's walk clears its architecture, with the
// world at the state the camera meets it in (the grown room, the narrowed loop corridor, the open's walls gone)
// and the breathing at its largest. The collision guard is a safety net; this is the guarantee by construction.
TEST_CASE("All You Got: every chapter's walk is clear of its world", "[liminal][journey][allyougot]") {
    const std::filesystem::path dir = std::filesystem::path(AVGEN_SOURCE_DIR) / "examples" / "liminal";
    const std::filesystem::path file = dir / "all-you-got.scene.json";
    if (!std::filesystem::exists(file)) {
        SKIP("examples/liminal/all-you-got.scene.json not generated");
    }
    const nlohmann::json doc = readJson(file);
    auto journey = scene::Journey::fromJson(doc.at("camera").at("journey"));
    REQUIRE(journey.has_value());
    REQUIRE(journey->size() >= 9);
    const auto set = [](nlohmann::json& node, const std::string& name, const char* field, const nlohmann::json& value,
                        const auto& self) -> void {
        if (node.value("name", "") == name) {
            node[field] = value;
        }
        if (node.contains("children")) {
            for (auto& c : node["children"]) {
                self(c, name, field, value, self);
            }
        }
    };
    const auto vec = [](float x, float y, float z) { return nlohmann::json::array({x, y, z}); };
    for (std::size_t c = 0; c < journey->size(); ++c) {
        const scene::JourneyChapter& chapter = journey->chapter(c);
        const scene::JourneyPath& path = journey->path(c);
        nlohmann::json root;
        for (const auto& n : doc.at("nodes")) {
            if (n.value("name", "") == chapter.world.collide) {
                root = n.at("sdf").at("tree").at("root");
            }
        }
        REQUIRE(!root.is_null());
        // The near-field tremble wraps the world; its amplitude is centimetres.
        if (root["kind"] == "warp" && root.value("count", 0) == 1) {
            root = root["children"][0];
        }
        // The world as the camera meets it: the breathing at its largest (the bass route's 0.22 m at full depth,
        // 0.4 of that in the intro's corridor, 1.6 times it in the release).
        const bool corridor = chapter.name == "corridor" || chapter.name == "loop";
        set(root, "breath", "amount", chapter.name == "open" ? 0.36f : (corridor ? 0.09f : 0.22f), set);
        if (chapter.name == "loop") {
            set(root, "wallL", "translation", vec(0.0f, 0.0f, 0.32f), set);
            set(root, "wallR", "translation", vec(0.0f, 0.0f, -0.32f), set);
            set(root, "ceiling", "translation", vec(0.0f, -0.75f, 0.0f), set);
            set(root, "lampsAt", "translation", vec(0.0f, -0.75f, 0.0f), set);
        }
        if (chapter.name == "grow") { // grown (the camera walks in as it grows; grown is the tightest for the far door)
            set(root, "grow", "size", vec(9.15f, 5.15f, 6.65f), set);
            set(root, "growAt", "translation", vec(9.0f, 5.0f, 0.0f), set);
            set(root, "skyDoor", "size", vec(0.7f, 1.7f, 1.0f), set);
            set(root, "skyDoorAt", "translation", vec(18.0f, 1.7f, 0.0f), set);
        }
        if (chapter.name == "open") {
            set(root, "wallW", "translation", vec(-14.0f, 0.0f, 0.0f), set);
            set(root, "wallN", "translation", vec(0.0f, 0.0f, 14.0f), set);
            set(root, "wallS", "translation", vec(0.0f, 0.0f, -14.0f), set);
            set(root, "wallE", "translation", vec(0.0f, -40.0f, 0.0f), set);
            set(root, "lid", "translation", vec(0.0f, 24.0f, 0.0f), set);
        }
        auto tree = spatial::SdfTree::fromJson(nlohmann::json{{"root", root}});
        REQUIRE(tree.has_value());
        // Where the walk leaves this chapter: at the next chapter's start (its own distance there), or, for the
        // last chapter, at its last authored point (the generator carries every path two points straight on
        // past its end, so the gaze never runs onto the closing segment; those points are never walked).
        const auto& pts = chapter.world.path;
        const glm::vec3 last = pts.size() > 3 ? pts[pts.size() - 3] : pts.back();
        const double walkedEnd = c + 1 < journey->size()
                                     ? chapter.from + (journey->chapter(c + 1).start - chapter.start)
                                     : path.cellLength();
        float worst = 1e9f;
        double worstAt = 0.0;
        glm::vec3 worstEye(0.0f);
        for (double d = chapter.from; d < std::min(walkedEnd, static_cast<double>(path.cellLength())); d += 0.05) {
            scene::JourneyView v;
            v.distance = d;
            const auto pose = scene::journeyPose(path, v);
            if (c + 1 == journey->size() && glm::length(path.sample(d).position - last) < 0.4f) {
                break;
            }
            const float clearance = tree->evaluate(pose.eye, 0.0);
            if (clearance < worst) {
                worst = clearance;
                worstAt = d;
                worstEye = pose.eye;
            }
        }
        INFO("chapter '" << chapter.name << "': closest approach " << worst << " m at local distance " << worstAt
                         << " m, eye (" << worstEye.x << ", " << worstEye.y << ", " << worstEye.z << ") (guard radius "
                         << chapter.world.radius << ")");
        // Clear of the guard's radius everywhere: the walk never needs the guard (whose push is a visible jolt).
        // Near a screw seam the field is a bound (min(d, boundary + seam)), so this is conservative there.
        CHECK(worst > chapter.world.radius);
    }
}
