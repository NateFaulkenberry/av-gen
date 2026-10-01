// ADR-1051: the room / spatial validator. Each case builds a tiny annotated scene with exactly one defect
// (or none) and checks the validator names it, measures it, and stays quiet about what is valid.

#include "scene/space_validator.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <nlohmann/json.hpp>

#include <array>
#include <cmath>
#include <string>

using nlohmann::json;
using Catch::Matchers::WithinAbs;

namespace {

json boxAt(double x, double y, double z, double hx, double hy, double hz) {
    return json{{"kind", "translate"}, {"translation", {x, y, z}},
                {"children", json::array({json{{"kind", "box"}, {"size", {hx, hy, hz}}}})}};
}

// A prop placed at `at`, turned `yaw` about +Y; `body` is built in the prop's frame (base y = 0, front +Z).
json place(json body, double x, double z, double yaw, json entity, double y = 0.0) {
    body["entity"] = std::move(entity);
    json inner = {{"kind", "rotate"}, {"rotation", {0.0, yaw, 0.0}}, {"children", json::array({body})}};
    return json{{"kind", "translate"}, {"translation", {x, y, z}}, {"children", json::array({inner})}};
}

json tableBody() {
    return json{{"kind", "union"}, {"children", json::array({boxAt(0, 0.375, 0, 0.6, 0.375, 0.4)})}};
}

json chairBody() {
    return json{{"kind", "union"},
                {"children", json::array({boxAt(0, 0.23, 0, 0.22, 0.23, 0.22), boxAt(0, 0.68, -0.2, 0.22, 0.22, 0.02)})}};
}

json room() {
    json shell = {{"kind", "translate"},
                  {"translation", {0.0, 1.35, 0.0}},
                  {"children", json::array({json{{"kind", "shell"}, {"offset", 0.15},
                                                 {"children", json::array({json{{"kind", "box"}, {"size", {3.075, 1.425, 2.575}}}})}}})},
                  {"entity", {{"category", "room"}, {"id", "room"}, {"interior", {{-3, 3}, {0, 2.7}, {-2.5, 2.5}}}}}};
    // a window on the -z wall (frame facing +Z into the room) and a door on the +x wall
    json window = boxAt(0, 1.5, 0.02, 0.6, 0.6, 0.03);
    window["entity"] = {{"category", "window"}, {"id", "Window_01"}};
    json windowAt = {{"kind", "translate"}, {"translation", {1.0, 0.0, -2.5}}, {"children", json::array({window})}};
    json door = boxAt(3.0, 1.0, 0.5, 0.05, 1.0, 0.45);
    door["entity"] = {{"category", "door"}, {"id", "Door_01"}, {"normal", {-1, 0, 0}}};
    return json{{"kind", "union"}, {"children", json::array({shell, windowAt, door})}};
}

json sdfObject(const std::string& name, json root, std::array<double, 3> lo = {-4, -1, -4},
               std::array<double, 3> hi = {4, 3.5, 4}) {
    return json{{"kind", "sdf"},
                {"name", name},
                {"sdf", {{"tree", {{"root", root}}}, {"boundsMin", lo}, {"boundsMax", hi}}}};
}

json scene(json furniture) {
    json s = {{"format", "avgen-scene"}, {"nodes", json::array()}};
    s["nodes"].push_back(sdfObject("shell", room()));
    s["nodes"].push_back(sdfObject("furniture", furniture));
    return s;
}

json run(const json& s, const json& rules = json::object()) {
    avgen::scene::SpaceValidateOptions o;
    o.cameraPath = s.contains("camera");
    auto r = avgen::scene::validateSpace(s, avgen::scene::mergeSpaceRules(rules), o);
    REQUIRE(r.has_value());
    return *r;
}

// The first violation of `rule` naming `id`, or null.
json find(const json& report, const std::string& rule, const std::string& id) {
    for (const auto& v : report["violations"]) {
        if (v["rule"] != rule) continue;
        for (const auto& e : v["entities"]) {
            if (e == id) return v;
        }
    }
    return nullptr;
}

json unionOf(std::initializer_list<json> items) {
    json kids = json::array();
    for (const json& i : items) kids.push_back(i);
    return json{{"kind", "union"}, {"children", kids}};
}

} // namespace

TEST_CASE("Space validator: a chair through a table is an error with its penetration; an adjacent one passes",
          "[space][liminal][adr1051]") {
    const json bad = scene(unionOf({place(tableBody(), 0, 0, 0, {{"category", "table"}, {"id", "Table_01"}}),
                                    place(chairBody(), 0, 0.3, 180, {{"category", "chair"}, {"id", "Chair_03"}})}));
    const json r = run(bad);
    const json v = find(r, "intersection", "Chair_03");
    REQUIRE(v != nullptr);
    CHECK(v["severity"] == "ERROR");
    CHECK(v["measured"]["penetration"].get<double>() > 0.05);
    CHECK(v.contains("fix"));
    CHECK(v["message"].get<std::string>().find("Chair_03 intersects Table_01") != std::string::npos);

    // At the table's edge, facing it: no intersection, and the facing relationship passes.
    const json good = scene(unionOf({place(tableBody(), 0, 0, 0, {{"category", "table"}, {"id", "Table_01"}}),
                                     place(chairBody(), 0, 0.68, 180, {{"category", "chair"}, {"id", "Chair_03"}})}));
    const json g = run(good);
    CHECK(find(g, "intersection", "Chair_03") == nullptr);
    CHECK(find(g, "orientation", "Chair_03") == nullptr);
    CHECK(find(g, "floor", "Chair_03") == nullptr);
    CHECK(g["summary"]["pass"]["relationship"].get<int>() >= 1);
}

TEST_CASE("Space validator: floating and buried furniture, measured against the room's floor", "[space][liminal][adr1051]") {
    const json s = scene(unionOf({place(chairBody(), -2.0, 1.5, 0, {{"category", "chair"}, {"id", "Chair_04"}}, 0.37),
                                  place(tableBody(), 1.5, 1.0, 0, {{"category", "table"}, {"id", "Table_01"}}, -0.14)}));
    const json r = run(s);
    const json f = find(r, "floor", "Chair_04");
    REQUIRE(f != nullptr);
    CHECK(f["severity"] == "WARNING");
    CHECK_THAT(f["measured"]["clearance"].get<double>(), WithinAbs(0.37, 0.01));
    CHECK_THAT(f["fix"]["translate"][1].get<double>(), WithinAbs(-0.37, 0.01));
    const json t = find(r, "floor", "Table_01");
    REQUIRE(t != nullptr);
    CHECK(t["severity"] == "ERROR");
    CHECK_THAT(t["measured"]["penetration"].get<double>(), WithinAbs(0.14, 0.01));
}

TEST_CASE("Space validator: a desk chair turned away from its desk is reported with the angle", "[space][liminal][adr1051]") {
    const json s = scene(unionOf({place(tableBody(), 0, 0, 0, {{"category", "desk"}, {"id", "Desk_01"}}),
                                  place(chairBody(), 0, 0.75, -117, {{"category", "chair"}, {"id", "DeskChair_01"}})}));
    const json v = find(run(s), "orientation", "DeskChair_01");
    REQUIRE(v != nullptr);
    CHECK_THAT(v["measured"]["angle"].get<double>(), WithinAbs(63.0, 3.0));
    CHECK(v["message"].get<std::string>().find("away from Desk_01") != std::string::npos);
}

TEST_CASE("Space validator: a lyric over a window is an error with a clear spot suggested; a clear one passes",
          "[space][liminal][adr1051]") {
    json s = scene(unionOf({}));
    s["nodes"][1] = sdfObject("furniture", unionOf({place(tableBody(), -2.0, 1.5, 0, {{"category", "table"}, {"id", "T"}})}));
    auto word = [](const std::string& name, double x, double y) {
        return json{{"kind", "procedural"},
                    {"name", name},
                    {"position", {x, y, -2.49}},
                    {"rotation", {0.0, 0.0, 0.0}},
                    {"entity", {{"category", "wallText"}, {"room", "room"}}},
                    {"procedural", {{"source", {{"kind", "text"}, {"text", "HOW LITTLE"}, {"textSize", 0.3}}}}}};
    };
    s["nodes"].push_back(word("Lyric_12", 1.0, 1.5));
    s["nodes"].push_back(word("Lyric_13", -1.6, 1.6));
    const json r = run(s);
    const json bad = find(r, "lyricPlacement", "Lyric_12");
    REQUIRE(bad != nullptr);
    CHECK(bad["severity"] == "ERROR");
    CHECK(bad["message"].get<std::string>().find("Window_01") != std::string::npos);
    CHECK(bad["measured"]["overlap"].get<double>() > 0.2);
    REQUIRE(bad.contains("fix"));
    // the suggested spot is clear of the window (with the margin)
    const double fx = bad["fix"]["position"][0].get<double>();
    const double fy = bad["fix"]["position"][1].get<double>();
    CHECK((std::abs(fx - 1.0) > 0.6 + 0.15 || fy - 0.15 >= 2.1 - 1e-3 || fy + 0.15 <= 0.9 + 1e-3));
    CHECK(find(r, "lyricPlacement", "Lyric_13") == nullptr);
    CHECK(find(r, "lyricClearance", "Lyric_13") == nullptr);
    CHECK(r["summary"]["pass"]["lyric"].get<int>() == 1);

    // A larger margin makes the clear one jammed against the window? No: against the wall's edge it is not; the
    // margin is configurable and reported.
    const json r2 = run(s, json{{"lyric", {{"margin", 1.2}}}});
    const json jam = find(r2, "lyricClearance", "Lyric_13");
    REQUIRE(jam != nullptr);
    CHECK_THAT(jam["measured"]["margin"].get<double>(), WithinAbs(1.2, 1e-6));
}

TEST_CASE("Space validator: a lyric facing into its wall, or upside down, is an orientation error", "[space][liminal][adr1051]") {
    json s = scene(unionOf({}));
    s["nodes"].push_back(json{{"kind", "procedural"}, {"name", "Back"}, {"position", {-1.6, 1.6, -2.49}},
                              {"rotation", {0.0, 180.0, 0.0}}, {"entity", {{"category", "wallText"}, {"room", "room"}}},
                              {"procedural", {{"source", {{"kind", "text"}, {"text", "GO"}, {"textSize", 0.3}}}}}});
    s["nodes"].push_back(json{{"kind", "procedural"}, {"name", "Flip"}, {"position", {-1.6, 1.0, -2.49}},
                              {"rotation", {0.0, 0.0, 180.0}}, {"entity", {{"category", "wallText"}, {"room", "room"}}},
                              {"procedural", {{"source", {{"kind", "text"}, {"text", "GO"}, {"textSize", 0.3}}}}}});
    const json r = run(s);
    CHECK(find(r, "lyricOrientation", "Back") != nullptr);
    const json flip = find(r, "lyricOrientation", "Flip");
    REQUIRE(flip != nullptr);
    CHECK(flip["message"].get<std::string>().find("upside down") != std::string::npos);
}

TEST_CASE("Space validator: a mannequin's head outside its object's march bounds is a missing component",
          "[space][liminal][adr1051]") {
    json head = boxAt(0, 1.7, 0, 0.08, 0.11, 0.08);
    head["part"] = "head";
    json man = unionOf({boxAt(0, 0.8, 0, 0.2, 0.8, 0.12), head});
    man["entity"] = {{"category", "mannequin"}, {"id", "Mannequin_01"}, {"pose", "stand"}};
    json s = scene(unionOf({}));
    s["nodes"].push_back(sdfObject("man", man, {-1, -0.05, -1}, {1, 1.5, 1}));
    const json r = run(s);
    const json v = find(r, "integrity", "Mannequin_01");
    REQUIRE(v != nullptr);
    CHECK(v["severity"] == "ERROR");
    CHECK(v["measured"]["part"] == "head");
    CHECK(v["fix"]["boundsMax"][1].get<double>() > 1.81);

    // No head part at all: the integrity check names what is missing.
    json headless = unionOf({boxAt(0, 0.8, 0, 0.2, 0.8, 0.12)});
    headless["entity"] = {{"category", "mannequin"}, {"id", "Mannequin_02"}};
    json s2 = scene(unionOf({}));
    s2["nodes"].push_back(sdfObject("man", headless));
    const json m = find(run(s2), "integrity", "Mannequin_02");
    REQUIRE(m != nullptr);
    CHECK(m["message"].get<std::string>().find("missing head geometry") != std::string::npos);

    // Without any annotation, a piece of an object outside its bounds is still caught.
    json s3 = scene(unionOf({}));
    s3["nodes"].push_back(sdfObject("bare", unionOf({boxAt(0, 0.8, 0, 0.2, 0.8, 0.12), boxAt(0, 1.7, 0, 0.08, 0.11, 0.08)}),
                                    {-1, -0.05, -1}, {1, 1.5, 1}));
    bool clipped = false;
    for (const auto& v3 : run(s3)["violations"]) {
        clipped = clipped || (v3["rule"] == "clipped" && v3["severity"] == "ERROR");
    }
    CHECK(clipped);
}

TEST_CASE("Space validator: a cabinet in the doorway blocks it; a room with no door has no entrance", "[space][liminal][adr1051]") {
    const json s = scene(unionOf({place(boxAt(0, 0.5, 0, 0.6, 0.5, 0.3), 2.6, 0.5, -90,
                                        {{"category", "cabinet"}, {"id", "Cabinet_01"}})}));
    const json r = run(s);
    const json v = find(r, "doorway", "Door_01");
    REQUIRE(v != nullptr);
    CHECK(v["severity"] == "ERROR");
    CHECK(v["entities"].dump().find("Cabinet_01") != std::string::npos);

    json open = scene(unionOf({}));
    const json r2 = run(open);
    CHECK(find(r2, "doorway", "Door_01") == nullptr);
    CHECK(find(r2, "access", "room") == nullptr);
    // remove the door: no entrance
    open["nodes"][0]["sdf"]["tree"]["root"]["children"].erase(2);
    CHECK(find(run(open), "access", "room") != nullptr);
}

TEST_CASE("Space validator: a seated mannequin sits on its chair's seat; too high is reported", "[space][liminal][adr1051]") {
    auto withMan = [](double hipY) {
        json head = boxAt(0, hipY + 0.76, 0, 0.08, 0.11, 0.08);
        head["part"] = "head";
        json man = unionOf({boxAt(0, hipY + 0.3, 0, 0.18, 0.3, 0.1), boxAt(0, hipY - 0.2, 0.25, 0.1, 0.05, 0.25), head});
        return scene(unionOf({place(chairBody(), -1.0, 0.0, 0, {{"category", "chair"}, {"id", "Chair_01"}}),
                              place(man, -1.0, 0.0, 0,
                                    {{"category", "mannequin"}, {"id", "Man"}, {"pose", "thinker"}, {"anchor", "Chair_01"},
                                     {"hip", {0.0, hipY, 0.0}}})}));
    };
    const json ok = run(withMan(0.47));
    CHECK(find(ok, "pose", "Man") == nullptr);
    CHECK(find(ok, "intersection", "Man") == nullptr); // the anchored seat contact is expected
    CHECK(ok["summary"]["pass"]["character"].get<int>() == 1);
    const json high = find(run(withMan(0.8)), "pose", "Man");
    REQUIRE(high != nullptr);
    CHECK(high["message"].get<std::string>().find("above the seat of Chair_01") != std::string::npos);
}

TEST_CASE("Space validator: a camera path through a wall is an error, one inside the room is clear", "[space][liminal][adr1051]") {
    json s = scene(unionOf({}));
    auto journey = [](double endX) {
        return json{{"mode", 3},
                    {"journey", {{"chapters", json::array({json{{"name", "walk"}, {"start", 0.0},
                                                                {"path", {{-2.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {endX, 0.0, 0.0}}},
                                                                {"screw", {{"translation", {100.0, 0.0, 0.0}}, {"count", 0}}},
                                                                {"nodes", {"shell", "furniture"}}}})}}}};
    };
    s["camera"] = journey(5.0);
    const json bad = find(run(s), "cameraPath", "walk");
    REQUIRE(bad != nullptr);
    CHECK(bad["severity"] == "ERROR");
    s["camera"] = journey(2.0);
    CHECK(find(run(s), "cameraPath", "walk") == nullptr);
}

TEST_CASE("Space validator: wall pieces need a wall; rules are data and merge over the defaults", "[space][liminal][adr1051]") {
    json painting = boxAt(0, 0, 0.02, 0.4, 0.3, 0.02);
    painting["entity"] = {{"category", "painting"}, {"id", "Painting_01"}};
    json onWall = {{"kind", "translate"}, {"translation", {-1.0, 1.6, -2.5}}, {"children", json::array({painting})}};
    CHECK(find(run(scene(unionOf({onWall}))), "support", "Painting_01") == nullptr);
    json off = onWall;
    off["translation"] = {-1.0, 1.6, -1.9};
    const json v = find(run(scene(unionOf({off}))), "support", "Painting_01");
    REQUIRE(v != nullptr);
    CHECK_THAT(v["fix"]["translate"][2].get<double>(), WithinAbs(-0.6, 0.02));

    const json rules = avgen::scene::mergeSpaceRules(json{{"categories", {{"painting", {{"mounts", "ceiling"}}}}}, {"lyric", {{"margin", 0.3}}}});
    CHECK(rules["categories"]["painting"]["mounts"] == "ceiling");
    CHECK(rules["categories"]["painting"]["group"] == "decor"); // merged, not replaced
    CHECK(rules["lyric"]["margin"] == 0.3);
    CHECK(rules["categories"].contains("chair"));
    CHECK_FALSE(avgen::scene::validateSpace(json::object(), rules).has_value());
    const std::string text = avgen::scene::formatSpaceReport(run(scene(unionOf({off}))));
    CHECK(text.find("Warnings: ") != std::string::npos);
    CHECK(text.find("PASS") != std::string::npos);
}

TEST_CASE("Space validator: entities shown at different times are not checked against each other", "[space][liminal][adr1051]") {
    auto figure = [](const std::string& id, double t0, double t1) {
        json body = unionOf({boxAt(0, 0.8, 0, 0.2, 0.8, 0.15)});
        return place(body, 0.0, 0.0, 0, {{"category", "mannequin"}, {"id", id}, {"t0", t0}, {"t1", t1}});
    };
    const json apart = scene(unionOf({figure("PoseA", 10, 20), figure("PoseB", 30, 40)}));
    CHECK(find(run(apart), "intersection", "PoseA") == nullptr);
    const json together = scene(unionOf({figure("PoseA", 10, 35), figure("PoseB", 30, 40)}));
    CHECK(find(run(together), "intersection", "PoseA") != nullptr);
}

TEST_CASE("Space validator: a camera path is checked only against what is present when the camera passes",
          "[space][liminal][adr1051]") {
    json s = scene(unionOf({}));
    json man = unionOf({boxAt(0, 1.6, 0, 0.3, 0.3, 0.3)});
    man["entity"] = {{"category", "mannequin"}, {"id", "Late"}, {"t0", 60.0}, {"t1", 70.0}};
    s["nodes"].push_back(sdfObject("man", man));
    s["camera"] = json{{"mode", 3},
                       {"journey", {{"chapters", json::array({json{{"name", "walk"}, {"start", 0.0},
                                                                   {"path", {{-2.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {2.0, 0.0, 0.0}}},
                                                                   {"screw", {{"translation", {100.0, 0.0, 0.0}}, {"count", 0}}},
                                                                   {"nodes", {"shell", "furniture", "man"}}}})}}}};
    auto runAt = [&](double t0, double t1) {
        avgen::scene::SpaceValidateOptions o;
        o.journeyDistances = {0.0, 4.0};
        o.journeyKeys = {{t0, 0.0}, {t1, 4.0}};
        auto r = avgen::scene::validateSpace(s, avgen::scene::defaultSpaceRules(), o);
        REQUIRE(r.has_value());
        return *r;
    };
    CHECK(find(runAt(35.0, 40.0), "cameraPath", "walk") == nullptr); // he is not there yet
    CHECK(find(runAt(62.0, 66.0), "cameraPath", "walk") != nullptr); // the camera walks through him
}
