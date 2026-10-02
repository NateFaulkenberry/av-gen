// ADR-1056: the spatial validator's structural, containment and text checks (art pass 4). Each case builds a tiny
// annotated scene with one defect (or none), as test_space_validator.cpp does for ADR-1051, and checks the
// validator names it, measures it in metres, and stays quiet about what is valid. No assets.

#include "scene/space_validator.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <nlohmann/json.hpp>

#include <array>
#include <string>

using nlohmann::json;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;

namespace {

json box(double hx, double hy, double hz) {
    return json{{"kind", "box"}, {"size", {hx, hy, hz}}};
}

json at(double x, double y, double z, json child) {
    return json{{"kind", "translate"}, {"translation", {x, y, z}}, {"children", json::array({std::move(child)})}};
}

json boxAt(double x, double y, double z, double hx, double hy, double hz) {
    return at(x, y, z, box(hx, hy, hz));
}

json unionOf(std::initializer_list<json> items) {
    json kids = json::array();
    for (const json& i : items) kids.push_back(i);
    return json{{"kind", "union"}, {"children", kids}};
}

json differenceOf(json solid, std::initializer_list<json> cuts) {
    json kids = json::array({std::move(solid)});
    for (const json& c : cuts) kids.push_back(c);
    return json{{"kind", "difference"}, {"children", kids}};
}

json tagged(json n, json entity) {
    n["entity"] = std::move(entity);
    return n;
}

// The room of test_space_validator.cpp: interior x [-3, 3], y [0, 2.7], z [-2.5, 2.5], 0.15 m walls.
json shell(const std::string& id = "room") {
    return tagged(at(0.0, 1.35, 0.0, json{{"kind", "shell"}, {"offset", 0.15}, {"children", json::array({box(3.075, 1.425, 2.575)})}}),
                  {{"category", "room"}, {"id", id}, {"interior", {{-3, 3}, {0, 2.7}, {-2.5, 2.5}}}, {"wall", 0.15}});
}

// A window frame (a hollow box, 5 cm deep, in the -z wall's interior plane) centred at x, with a ledge (the sill)
// whose top is at `sillTop`, wider than the frame and standing 0.1 m proud into the room.
json windowAt(double x, double frameBottom, double sillTop, const std::string& id = "Window_01") {
    const double h = 0.6;
    json frame = differenceOf(boxAt(x, frameBottom + h, -2.5, 0.6, h, 0.05), {boxAt(x, frameBottom + h, -2.5, 0.55, h - 0.05, 0.2)});
    json ledge = boxAt(x, sillTop - 0.02, -2.46, 0.68, 0.02, 0.06);
    return tagged(unionOf({frame, ledge}), {{"category", "window"}, {"id", id}, {"room", "room"}});
}

// The -z wall's cut for a window opening x [x-0.6, x+0.6], y [y0, y0+1.2].
json windowCut(double x, double y0) {
    return boxAt(x, y0 + 0.6, -2.575, 0.6, 0.6, 0.3);
}

// A door on the +x wall at z = 0.5, 0.9 m wide, 2.05 m high: its cut and its architrave (7 cm round the opening).
json doorCut() {
    return boxAt(3.0, 1.015, 0.5, 0.6, 1.025, 0.45);
}
json doorFrame() {
    json outer = boxAt(2.99, 1.06, 0.5, 0.03, 1.06, 0.52);
    json hole = boxAt(2.99, 1.0, 0.5, 0.2, 1.05, 0.45);
    return tagged(differenceOf(outer, {hole}), {{"category", "door"}, {"id", "Door_01"}, {"room", "room"}, {"normal", {-1, 0, 0}}});
}

json sdfObject(const std::string& name, json root, std::array<double, 3> lo = {-4, -1, -4}, std::array<double, 3> hi = {4, 3.5, 4}) {
    return json{{"kind", "sdf"}, {"name", name}, {"sdf", {{"tree", {{"root", root}}}, {"boundsMin", lo}, {"boundsMax", hi}}}};
}

json sceneOf(json architecture, json furniture = json()) {
    json s = {{"format", "avgen-scene"}, {"nodes", json::array({sdfObject("shell", std::move(architecture))})}};
    if (!furniture.is_null()) s["nodes"].push_back(sdfObject("furniture", std::move(furniture)));
    return s;
}

json run(const json& s, const json& rules = json::object()) {
    avgen::scene::SpaceValidateOptions o;
    o.cameraPath = false;
    auto r = avgen::scene::validateSpace(s, avgen::scene::mergeSpaceRules(rules), o);
    REQUIRE(r.has_value());
    return *r;
}

json find(const json& report, const std::string& rule, const std::string& id = "") {
    for (const auto& v : report["violations"]) {
        if (v["rule"] != rule) continue;
        if (id.empty()) return v;
        for (const auto& e : v["entities"]) {
            if (e == id) return v;
        }
    }
    return nullptr;
}

// A room with a window opening on -z (at x = 1, y 0.9-2.1) and a door on +x, plus `extra` pieces in its object.
json roomWith(json window, double cutX = 1.0, std::initializer_list<json> extra = {}) {
    json walls = differenceOf(shell(), {windowCut(cutX, 0.9), doorCut()});
    json u = unionOf({walls, window, doorFrame()});
    for (const json& e : extra) u["children"].push_back(e);
    return u;
}

} // namespace

TEST_CASE("Structure: a window in its opening passes; shifted, it is misaligned per edge in metres", "[space][liminal][adr1056]") {
    const json good = run(sceneOf(roomWith(windowAt(1.0, 0.9, 0.9))));
    CHECK(find(good, "openingAlignment", "Window_01") == nullptr);
    CHECK(find(good, "sill", "Window_01") == nullptr);
    CHECK(find(good, "opening", "Window_01") == nullptr);
    CHECK(find(good, "openingAlignment", "Door_01") == nullptr);

    // The frame is 0.12 m to the +x side of its cut: seen from inside facing -z, +x is the right.
    const json bad = run(sceneOf(roomWith(windowAt(1.12, 0.9, 0.9))));
    const json v = find(bad, "openingAlignment", "Window_01");
    REQUIRE(v != nullptr);
    CHECK(v["severity"] == "ERROR");
    CHECK(v["tier"] == "critical");
    CHECK_THAT(v["measured"]["right"].get<double>(), WithinAbs(0.12, 0.02));
    CHECK_THAT(v["measured"]["left"].get<double>(), WithinAbs(-0.12, 0.02));
    CHECK_THAT(v["message"].get<std::string>(), ContainsSubstring("right edge"));
    CHECK_THAT(v["fix"]["translate"][0].get<double>(), WithinAbs(-0.12, 0.02));
}

TEST_CASE("Structure: a sill above the opening's lower edge is offset by its distance", "[space][liminal][adr1056]") {
    // The frame fits its opening (0.9-2.1); the ledge's top is at 1.08, 0.18 m above it.
    const json r = run(sceneOf(roomWith(windowAt(1.0, 0.9, 1.08))));
    const json v = find(r, "sill", "Window_01");
    REQUIRE(v != nullptr);
    CHECK(v["severity"] == "ERROR");
    CHECK_THAT(v["measured"]["sillToOpening"].get<double>(), WithinAbs(0.18, 0.02));
    CHECK_THAT(v["message"].get<std::string>(), ContainsSubstring("above the opening's lower edge"));
}

TEST_CASE("Structure: a window on a solid wall has no opening", "[space][liminal][adr1056]") {
    // the cut is 2 m away from the frame
    const json r = run(sceneOf(roomWith(windowAt(-1.5, 0.9, 0.9), 1.0)));
    const json v = find(r, "opening", "Window_01");
    REQUIRE(v != nullptr);
    CHECK(v["severity"] == "ERROR");
    // ...and a deliberate one is marked blind
    json blind = windowAt(-1.5, 0.9, 0.9);
    blind["entity"]["blind"] = true;
    CHECK(find(run(sceneOf(roomWith(blind, 1.0))), "opening", "Window_01") == nullptr);
}

TEST_CASE("Structure: trim running across a doorway is named, with its height; trim cut at the door passes",
          "[space][liminal][adr1056]") {
    // A skirting band round the room (the kit's wall_band: a shell round the interior, clipped to 0-0.1 m).
    const json prism = at(0.0, 0.0, 0.0, json{{"kind", "shell"}, {"offset", 0.03}, {"children", json::array({box(3.0, 50.0, 2.5)})}});
    const json band = json{{"kind", "intersection"}, {"children", json::array({prism, boxAt(0.0, 0.05, 0.0, 4.0, 0.05, 4.0)})}};
    const json r = run(sceneOf(roomWith(windowAt(1.0, 0.9, 0.9), 1.0, {band})));
    const json v = find(r, "openingCrossed", "Door_01");
    REQUIRE(v != nullptr);
    CHECK_THAT(v["message"].get<std::string>(), ContainsSubstring("trim"));
    CHECK_THAT(v["measured"]["yTo"].get<double>(), WithinAbs(0.1, 0.05));
    CHECK(v["measured"]["share"].get<double>() > 0.9);

    const json cutBand = differenceOf(band, {doorCut()});
    const json ok = run(sceneOf(roomWith(windowAt(1.0, 0.9, 0.9), 1.0, {cutBand})));
    CHECK(find(ok, "openingCrossed", "Door_01") == nullptr);
}

TEST_CASE("Connections: stairs that end in space, a floating roof, a railing that does not follow its stairs",
          "[space][liminal][adr1056]") {
    // Five steps up to 1.5 m in the room, with no floor at the top.
    json steps = json{{"kind", "union"}, {"children", json::array()}};
    for (int i = 0; i < 5; ++i) steps["children"].push_back(boxAt(-2.0 + 0.3 * i, 0.15 * (i + 1), 0.0, 0.15, 0.15 * (i + 1), 0.45));
    json stairs = tagged(steps, {{"category", "stairs"}, {"id", "Stair_01"}, {"room", "room"}, {"rise", 0.3}});
    const json r = run(sceneOf(unionOf({shell(), stairs})));
    const json s = find(r, "stairs", "Stair_01");
    REQUIRE(s != nullptr);
    CHECK_THAT(s["message"].get<std::string>(), ContainsSubstring("terminates in empty space"));
    // marked as the image it is
    stairs["entity"]["terminates"] = true;
    CHECK(find(run(sceneOf(unionOf({shell(), stairs}))), "stairs", "Stair_01") == nullptr);

    // A roof 0.3 m above a 2 m building box; then resting on it.
    const json building = tagged(boxAt(0.0, 1.0, 0.0, 1.0, 1.0, 1.0), {{"category", "building"}, {"id", "House_01"}});
    auto roofAt = [](double y) {
        return tagged(boxAt(0.0, y + 0.1, 0.0, 1.1, 0.1, 1.1), {{"category", "roof"}, {"id", "Roof_01"}, {"anchor", "House_01"}});
    };
    const json floating = run(json{{"format", "avgen-scene"}, {"nodes", json::array({sdfObject("house", unionOf({building, roofAt(2.3)}))})}});
    const json fv = find(floating, "roof", "Roof_01");
    REQUIRE(fv != nullptr);
    CHECK(fv["severity"] == "ERROR");
    CHECK_THAT(fv["measured"]["gap"].get<double>(), WithinAbs(0.3, 0.03));
    const json resting = run(json{{"format", "avgen-scene"}, {"nodes", json::array({sdfObject("house", unionOf({building, roofAt(2.0)}))})}});
    CHECK(find(resting, "roof", "Roof_01") == nullptr);

    // A level rail over the rising steps: its height above the treads varies by more than a step.
    json stairs2 = tagged(steps, {{"category", "stairs"}, {"id", "Stair_02"}, {"terminates", true}});
    const json level = tagged(boxAt(-1.4, 2.4, 0.4, 0.75, 0.02, 0.02), {{"category", "railing"}, {"id", "Rail_01"}, {"anchor", "Stair_02"}});
    const json rr = run(sceneOf(unionOf({shell(), stairs2, level})));
    const json rv = find(rr, "railing", "Rail_01");
    REQUIRE(rv != nullptr);
    CHECK_THAT(rv["message"].get<std::string>(), ContainsSubstring("does not follow"));
}

TEST_CASE("Containment: a washer in the kitchen, crowded chairs, a bed off its wall; expected overlaps are info",
          "[space][liminal][adr1056]") {
    json room = shell("kitchen");
    const json washer = tagged(boxAt(-2.5, 0.42, -2.1, 0.3, 0.42, 0.3), {{"category", "washer"}, {"id", "Washer_01"}, {"room", "kitchen"}});
    const json r = run(sceneOf(unionOf({room}), unionOf({washer})));
    const json v = find(r, "roomKind", "Washer_01");
    REQUIRE(v != nullptr);
    CHECK_THAT(v["message"].get<std::string>(), ContainsSubstring("laundry"));
    json laundry = shell("laundry");
    json washer2 = washer;
    washer2["entity"]["room"] = "laundry";
    CHECK(find(run(sceneOf(unionOf({laundry}), unionOf({washer2}))), "roomKind", "Washer_01") == nullptr);

    // two chairs at one table, 0.3 m apart
    const json table = tagged(boxAt(0.0, 0.375, 0.0, 0.6, 0.375, 0.4), {{"category", "table"}, {"id", "Table_01"}});
    auto chair = [](const std::string& id, double x) {
        return at(x, 0.0, 0.7, json{{"kind", "rotate"}, {"rotation", {0.0, 180.0, 0.0}},
                                    {"children", json::array({tagged(boxAt(0.0, 0.23, 0.0, 0.12, 0.23, 0.15),
                                                                     {{"category", "chair"}, {"id", id}, {"anchor", "Table_01"}})})}});
    };
    const json crowded = run(sceneOf(unionOf({shell()}), unionOf({table, chair("Chair_A", -0.15), chair("Chair_B", 0.15)})));
    CHECK(find(crowded, "chairSpacing", "Chair_A") != nullptr);

    // a bed whose back is 0.6 m off the -z wall (its front faces +z, into the room)
    const json bed = tagged(boxAt(0.0, 0.3, -0.85, 0.75, 0.3, 1.0), {{"category", "bed"}, {"id", "Bed_01"}});
    const json b = find(run(sceneOf(unionOf({shell()}), unionOf({bed}))), "againstWall", "Bed_01");
    REQUIRE(b != nullptr);
    CHECK_THAT(b["message"].get<std::string>(), ContainsSubstring("0.65m off wall -z"));

    // a rug under the table: expected, so informational, never an error
    const json rug = tagged(boxAt(0.0, 0.005, 0.0, 1.0, 0.03, 0.8), {{"category", "rug"}, {"id", "Rug_01"}});
    const json withRug = run(sceneOf(unionOf({shell()}), unionOf({table, rug})));
    const json info = find(withRug, "expectedIntersection", "Rug_01");
    REQUIRE(info != nullptr);
    CHECK(info["severity"] == "INFO");
    CHECK(info["tier"] == "info");
    CHECK(find(withRug, "intersection", "Rug_01") == nullptr);
}

TEST_CASE("Text: off the wall's right edge in metres; inside geometry; two words in one place", "[space][liminal][adr1056]") {
    // "GO" at 0.3 m per em centred 2.95 m along the -z wall (whose right edge, facing it, is x = 3).
    json s = sceneOf(unionOf({shell()}));
    s["nodes"].push_back(json{{"kind", "procedural"}, {"name", "Edge"}, {"position", {2.95, 1.6, -2.495}}, {"rotation", {0.0, 0.0, 0.0}},
                              {"entity", {{"category", "wallText"}, {"room", "room"}}},
                              {"procedural", {{"source", {{"kind", "text"}, {"text", "GO"}, {"textSize", 0.3}}}}}});
    const json r = run(s);
    const json v = find(r, "lyricPlacement", "Edge");
    REQUIRE(v != nullptr);
    CHECK_THAT(v["message"].get<std::string>(), ContainsSubstring("on the right edge"));
    CHECK(v["measured"]["right"].get<double>() > 0.05);
    CHECK(v["measured"]["left"].get<double>() < 0.0);

    // floating text through a free-standing column, and two words in one place at one time
    json f = sceneOf(unionOf({shell()}), unionOf({tagged(boxAt(0.0, 1.0, 0.0, 0.1, 1.0, 0.1), {{"category", "prop"}, {"id", "Column"}})}));
    auto word = [](const std::string& name, double x) {
        return json{{"kind", "procedural"}, {"name", name}, {"position", {x, 1.5, -0.04}}, {"rotation", {0.0, 0.0, 0.0}},
                    {"entity", {{"category", "floatingText"}, {"t0", 1.0}, {"t1", 2.0}}},
                    {"procedural", {{"source", {{"kind", "text"}, {"text", "WAIT"}, {"textSize", 0.3}}}}}};
    };
    f["nodes"].push_back(word("Through", 0.0));
    f["nodes"].push_back(word("Twin", 0.1));
    const json fr = run(f);
    const json ti = find(fr, "textIntersection", "Through");
    REQUIRE(ti != nullptr);
    CHECK_THAT(ti["message"].get<std::string>(), ContainsSubstring("Column"));
    CHECK(find(fr, "textOverlap", "Through") != nullptr);
    // the same words at different times do not overlap
    f["nodes"][3]["entity"]["t0"] = 3.0;
    f["nodes"][3]["entity"]["t1"] = 4.0;
    CHECK(find(run(f), "textOverlap", "Through") == nullptr);
}

TEST_CASE("Report: the Scene Validation Report has the three tiers and the counts by rule", "[space][liminal][adr1056]") {
    const json r = run(sceneOf(roomWith(windowAt(1.12, 0.9, 0.9))));
    const std::string md = avgen::scene::formatSceneValidationMarkdown(r);
    CHECK_THAT(md, ContainsSubstring("# Scene Validation Report"));
    CHECK_THAT(md, ContainsSubstring("## Critical"));
    CHECK_THAT(md, ContainsSubstring("## Warnings"));
    CHECK_THAT(md, ContainsSubstring("## Informational"));
    CHECK_THAT(md, ContainsSubstring("| openingAlignment | 1 |"));

    // a film pass merges into it: re-sorted, recounted, its statistics kept
    json merged = r;
    const int before = merged["summary"]["errors"].get<int>();
    avgen::scene::mergeFilmReport(merged, json{{"violations", json::array({json{{"severity", "ERROR"}, {"rule", "cameraInside"},
                                                                                {"entities", {"room"}}, {"message", "inside"}}})},
                                               {"camera", {{"samples", 10}}}});
    CHECK(merged["summary"]["errors"].get<int>() == before + 1);
    CHECK(merged["violations"][0]["severity"] == "ERROR");
    CHECK(merged["film"]["camera"]["samples"] == 10);
    bool tiered = true;
    for (const auto& v : merged["violations"]) tiered = tiered && v.contains("tier");
    CHECK(tiered);
}

TEST_CASE("Containment: a mannequin brushing a wall is reported with its gap; one standing clear is not", "[space][liminal][adr1056]") {
    auto man = [](double x) {
        json head = boxAt(0.0, 1.7, 0.0, 0.1, 0.12, 0.1);
        head["part"] = "head";
        return at(x, 0.0, 0.0, tagged(unionOf({boxAt(0.0, 0.8, 0.0, 0.2, 0.8, 0.12), head}),
                                      {{"category", "mannequin"}, {"id", "Man"}, {"pose", "stand"}}));
    };
    // its side 1 cm from the -x wall (x = -3)
    const json v = find(run(sceneOf(unionOf({shell()}), unionOf({man(-2.79)}))), "characterClearance", "Man");
    REQUIRE(v != nullptr);
    CHECK_THAT(v["measured"]["gap"].get<double>(), WithinAbs(0.01, 0.006));
    CHECK(find(run(sceneOf(unionOf({shell()}), unionOf({man(-2.0)}))), "characterClearance", "Man") == nullptr);
}
