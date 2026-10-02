// ADR-1057: the spatial validator's film pass (`avgen --validate-space <project> --film`), on tiny projects written
// to disk and played through the offline engine: the camera inside a wall, through a wall between two frames,
// clipped by its near plane, too close to a wall; a structural node that wobbles after it is built versus one that
// is built and locks. And ADR-1058: the near plane as a parameter, and a journey's automatic one. No GPU, no assets.

#include "app/engine.hpp"
#include "app/film_validate.hpp"
#include "scene/space_validator.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>

using namespace avgen;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using nlohmann::json;

namespace {

void write(const std::filesystem::path& path, const json& j) {
    std::ofstream out(path);
    out << j.dump(2);
    REQUIRE(out.good());
}

json key(double t, json value, const char* interp = "linear") {
    return json{{"time", t}, {"value", std::move(value)}, {"interp", interp}};
}

json track(const std::string& target, json keys) {
    return json{{"target", target}, {"component", -1}, {"timeBase", "seconds"}, {"mode", "replace"},
                {"loopLength", 0.0}, {"enabled", true}, {"keys", std::move(keys)}};
}

// A 0.2 m wall across z = 0 (x, y within +-3), and two named nodes on a "house": `roofAt` (keyed by the caller)
// and `doorAt`.
json sceneWith(json camera, bool roofMoves = false) {
    json wall = {{"kind", "translate"}, {"translation", {0.0, 1.5, 0.0}},
                 {"children", json::array({json{{"kind", "box"}, {"size", {3.0, 1.5, 0.1}}}})}};
    wall["entity"] = {{"category", "wallDecoration"}, {"id", "Wall"}};
    json roof = {{"kind", "translate"}, {"name", "roofAt"}, {"translation", {0.0, 0.0, 0.0}},
                 {"children", json::array({json{{"kind", "translate"}, {"translation", {0.0, 4.0, -6.0}},
                                                {"children", json::array({json{{"kind", "box"}, {"size", {1.0, 0.1, 1.0}}}})}}})}};
    roof["entity"] = {{"category", "roof"}, {"id", "Roof"}};
    if (roofMoves) roof["entity"]["moves"] = true;
    json door = {{"kind", "translate"}, {"name", "doorAt"}, {"translation", {0.0, 0.0, 0.0}},
                 {"children", json::array({json{{"kind", "translate"}, {"translation", {2.0, 1.0, -6.0}},
                                                {"children", json::array({json{{"kind", "box"}, {"size", {0.4, 1.0, 0.05}}}})}}})}};
    door["entity"] = {{"category", "door"}, {"id", "Door"}};
    json root = {{"kind", "union"}, {"children", json::array({wall, roof, door})}};
    return json{{"format", "avgen-scene"},
                {"version", 1},
                {"name", "film"},
                {"camera", std::move(camera)},
                {"nodes", json::array({json{{"kind", "sdf"}, {"name", "world"}, {"visible", true},
                                            {"sdf", {{"tree", {{"root", root}}}, {"boundsMin", {-4, -1, -8}}, {"boundsMax", {4, 6, 4}}}}}})}};
}

std::filesystem::path writeProject(const std::string& name, const json& scene, json tracks) {
    const auto dir = testsupport::processTempDir() / "film_validate";
    std::filesystem::create_directories(dir);
    write(dir / (name + ".scene.json"), scene);
    const json project = {{"format", "avgen-project"},
                          {"version", 4},
                          {"assets", {{"scene", {{"kind", "composition"}, {"path", name + ".scene.json"}}}}},
                          {"render", {{"fps", 30.0}, {"width", 640}, {"height", 360}, {"startSeconds", 0.0}, {"endSeconds", 3.0}}},
                          {"timeline", {{"enabled", true}, {"cues", json::array()}, {"tracks", std::move(tracks)}}}};
    const auto path = dir / (name + ".json");
    write(path, project);
    return path;
}

json film(const std::filesystem::path& project, const json& scene, double to, double fps = 30.0, bool motion = false) {
    const json rules = scene::mergeSpaceRules(json::object());
    scene::SpaceValidateOptions so;
    so.cameraPath = false;
    auto report = scene::validateSpace(scene, rules, so);
    REQUIRE(report.has_value());
    app::FilmValidateOptions fo;
    fo.fps = fps;
    fo.to = to;
    fo.motion = motion;
    fo.camera = !motion;
    auto r = app::validateFilm(project.string(), scene, *report, rules, fo);
    REQUIRE(r.has_value());
    return *r;
}

json find(const json& report, const std::string& rule) {
    for (const auto& v : report["violations"]) {
        if (v["rule"] == rule) return v;
    }
    return nullptr;
}

json freeCamera(double nearPlane = 0.0) {
    json c = {{"mode", 1}, {"fov", 60.0}, {"position", {0.0, 1.6, 3.0}}, {"target", {0.0, 1.6, -10.0}}};
    if (nearPlane > 0.0) c["near"] = nearPlane;
    return c;
}

} // namespace

TEST_CASE("Film pass: a camera that walks into the wall is inside it, at the frames it is", "[space][liminal][adr1057]") {
    // 3 m/s from z = 3 towards the wall at z = 0 (+-0.1): inside from about 0.97 s.
    const json s = sceneWith(freeCamera());
    const auto p = writeProject("inside", s,
                                json::array({track("camera/position", json::array({key(0.0, {0.0, 1.6, 3.0}), key(2.0, {0.0, 1.6, -3.0})})),
                                             track("camera/target", json::array({key(0.0, {0.0, 1.6, -10.0}), key(2.0, {0.0, 1.6, -16.0})}))}));
    const json r = film(p, s, 1.4);
    const json v = find(r, "cameraInside");
    REQUIRE(v != nullptr);
    CHECK(v["severity"] == "ERROR");
    CHECK_THAT(v["time"].get<double>(), WithinAbs(0.97, 0.05));
    CHECK(v["frame"].get<int>() >= 28);
    CHECK_THAT(v["message"].get<std::string>(), ContainsSubstring("Wall"));
    // before the wall, close to it, it warns about the close-up first
    CHECK(find(r, "closeGeometry") != nullptr);
}

TEST_CASE("Film pass: a camera that jumps through a wall between two samples crosses it", "[space][liminal][adr1057]") {
    // At 5 samples a second the camera moves 0.6 m per sample, so no sample lands in the 0.2 m wall.
    const json s = sceneWith(freeCamera());
    const auto p = writeProject("cross", s,
                                json::array({track("camera/position", json::array({key(0.0, {0.0, 1.6, 0.9}), key(1.0, {0.0, 1.6, -2.1})})),
                                             track("camera/target", json::array({key(0.0, {0.0, 1.6, -10.0}), key(1.0, {0.0, 1.6, -13.0})}))}));
    const json r = film(p, s, 1.0, 5.0);
    CHECK(find(r, "cameraInside") == nullptr);
    const json v = find(r, "cameraCrossing");
    REQUIRE(v != nullptr);
    CHECK(v["severity"] == "ERROR");
}

TEST_CASE("Film pass: a near plane deeper than the camera's clearance clips the wall (ADR-1058)", "[space][liminal][adr1057][adr1058]") {
    // Standing 0.25 m from the wall, looking at it. A 0.4 m near plane (the old automatic value in a large world)
    // cuts the wall open; the automatic one here does not.
    const json still = json::array({track("camera/position", json::array({key(0.0, {0.0, 1.6, 0.35})})),
                                    track("camera/target", json::array({key(0.0, {0.0, 1.6, -10.0})}))});
    const json clipped = sceneWith(freeCamera(0.4));
    const json r = film(writeProject("near", clipped, still), clipped, 0.3);
    const json v = find(r, "nearClip");
    REQUIRE(v != nullptr);
    CHECK(v["severity"] == "ERROR");
    const json open = sceneWith(freeCamera());
    const json r2 = film(writeProject("near2", open, still), open, 0.3);
    CHECK(find(r2, "nearClip") == nullptr);
    CHECK(find(r2, "closeGeometry") != nullptr); // still a close-up: 0.25 m from a wall fills the lens
}

TEST_CASE("camera/near sets the near plane; a journey's automatic near plane is at most 5 cm (ADR-1058)", "[liminal][adr1058]") {
    {
        const json s = sceneWith(freeCamera(0.3));
        const auto p = writeProject("nearParam", s, json::array());
        app::Engine engine(app::EngineMode::Offline);
        REQUIRE(engine.loadProject(p).has_value());
        FrameTime t;
        engine.update(t);
        CHECK_THAT(engine.scene().camera.nearPlane, WithinAbs(0.3, 1e-6));
        REQUIRE(engine.params().find("camera/near") != nullptr);
    }
    {
        // A journey through a 160 m wide world: 0.5% of its radius would be 0.4 m.
        json s = sceneWith(json{{"mode", 3},
                                {"fov", 60.0},
                                {"journey", {{"chapters", json::array({json{{"name", "a"}, {"start", 0.0}, {"from", 0.0},
                                                                            {"screw", {{"translation", {1000.0, 0.0, 0.0}}, {"count", 0}}},
                                                                            {"path", json::array({json::array({0.0, 0.0, 3.0}),
                                                                                                  json::array({0.0, 0.0, -3.0})})}}})}}}});
        s["nodes"].push_back(json{{"kind", "sdf"}, {"name", "far"}, {"visible", true},
                                  {"sdf", {{"tree", {{"root", {{"kind", "translate"}, {"translation", {80.0, 0.0, 0.0}},
                                                               {"children", json::array({json{{"kind", "box"}, {"size", {1.0, 1.0, 1.0}}}})}}}}},
                                           {"boundsMin", {70, -5, -10}}, {"boundsMax", {90, 5, 10}}}}});
        for (double x : {-80.0, 80.0}) {
            s["nodes"].push_back(json{{"kind", "procedural"}, {"name", x < 0 ? "markerW" : "markerE"}, {"position", {x, 0.0, 0.0}},
                                      {"procedural", {{"source", {{"kind", "sphere"}, {"radius", 1.0}, {"segments", 8}, {"rings", 4}}},
                                                      {"distribution", {{"kind", "single"}}}}}});
        }
        const auto p = writeProject("journeyNear", s, json::array());
        app::Engine engine(app::EngineMode::Offline);
        REQUIRE(engine.loadProject(p).has_value());
        FrameTime t;
        engine.update(t);
        CHECK(engine.scene().camera.nearPlane <= 0.05f + 1e-6f);
    }
}

TEST_CASE("Film pass: a structural node that wobbles after it is built is a build-lock warning; built-and-locked passes",
          "[space][liminal][adr1057]") {
    // The roof drops in at 0.5 s and holds (a build event); from 1.5 s it steps +-3 cm every 0.1 s (a wobble).
    json roofKeys = json::array({key(0.0, {0.0, 3.0, 0.0}, "step"), key(0.5, {0.0, 3.0, 0.0}), key(0.7, {0.0, 0.0, 0.0}, "step")});
    for (int i = 0; i < 12; ++i) roofKeys.push_back(key(1.5 + 0.1 * i, {0.0, i % 2 == 0 ? 0.03 : -0.03, 0.0}, "step"));
    roofKeys.push_back(key(2.7, {0.0, 0.0, 0.0}, "step"));
    // The door drops in once and holds.
    const json doorKeys = json::array({key(0.0, {0.0, 2.0, 0.0}, "step"), key(0.8, {0.0, 2.0, 0.0}), key(1.0, {0.0, 0.0, 0.0}, "step")});
    const json tracks = json::array({track("sdf/world/node/roofAt/translation", roofKeys), track("sdf/world/node/doorAt/translation", doorKeys)});
    const json s = sceneWith(freeCamera());
    const json r = film(writeProject("wobble", s, tracks), s, 3.0, 30.0, true);
    const json v = find(r, "buildLock");
    REQUIRE(v != nullptr);
    CHECK(v["severity"] == "WARNING");
    CHECK(v["entities"][0] == "sdf/world/node/roofAt/translation");
    CHECK_THAT(v["message"].get<std::string>(), ContainsSubstring("roof"));
    CHECK(v["measured"]["reversals"].get<int>() >= 5);
    CHECK_THAT(v["time"].get<double>(), WithinAbs(1.5, 0.1));
    for (const auto& x : r["violations"]) CHECK(x["entities"][0] != "sdf/world/node/doorAt/translation");
    CHECK(r["motion"]["rebuildEvents"].get<int>() + r["motion"]["locked"].get<int>() >= 1);

    // The same wobble on an entity that says it moves is not reported.
    const json moving = sceneWith(freeCamera(), true);
    const json r2 = film(writeProject("wobble2", moving, tracks), moving, 3.0, 30.0, true);
    CHECK(find(r2, "buildLock") == nullptr);
}
