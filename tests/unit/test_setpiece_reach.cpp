// Can a person find and change a UFO set piece they can see? (The owner's rule: anything visible in
// the picture must be findable and adjustable in the UI, under a name that describes what the viewer
// sees; ADR-387's lesson: asserting the registration proves nothing about reach.)
//
// A set piece is visible three ways, and each has a home this file checks with the panels' own
// arithmetic rather than by opening a window (the windowed app rewrites the owner's preferences):
//
//   * its knobs -- hover height, beam brightness, the moment the beam lights -- are parameters under
//     `staging/setpiece/<key>/`: the Parameters panel groups them under "staging", sub-group
//     "setpiece/<key>", each labelled in viewer words, on the layer the editor opens on;
//   * the set piece itself -- which event, where, when, its variation -- is a plan item: the Director
//     panel's "UFO set pieces" section lists it in words and edits it, each edit one undo;
//   * its moments are bus events a route can key on: the Modulation panel's source picker names them.
//
// Each case carries a control that fails if the check is vacuous (ADR-182).

#include "ai/director_tools.hpp"
#include "ai/tool_api.hpp"
#include "ai/tool_context.hpp"
#include "app/directing_apply.hpp"
#include "app/directing_context.hpp"
#include "app/directing_plan_file.hpp"
#include "app/edit_system.hpp"
#include "app/engine.hpp"
#include "directing/compiler.hpp"
#include "params/parameter_set.hpp"
#include "stage/setpiece.hpp"
#include "ui/director_panel_logic.hpp"
#include "ui/ui_logic.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

using namespace avgen;
using Catch::Matchers::ContainsSubstring;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {

fs::path labProject() { return fs::path(AVGEN_SOURCE_DIR) / "tests/data/setpieces/setpiece-lab.json"; }

json plan() {
    return json::parse(R"({
      "schemaVersion": 1, "id": "ufo", "title": "UFO activity", "tier": "baked",
      "setPieces": [
        {"key": "west", "template": "abduction", "craft": "saucer", "at": {"seconds": 12},
         "place": {"point": [-120, -60]}, "set": {"animals": 1, "approachSeconds": 6}, "framingMetres": 30},
        {"key": "field", "template": "abduction", "craft": "saucer", "at": {"seconds": 60},
         "place": {"point": [62, 22]}, "beamColor": [1.0, 0.25, 0.15],
         "set": {"animals": 2, "approachSeconds": 6, "hoverHeight": 30, "approachBearing": 90}},
        {"key": "far", "template": "survey", "craft": "saucer", "at": {"seconds": 100},
         "place": {"point": [-200, 200]}, "set": {"approachSeconds": 6}}
      ]})");
}

std::unique_ptr<app::Engine> lab() {
    auto engine = std::make_unique<app::Engine>(app::EngineMode::Offline);
    REQUIRE(engine->loadProject(labProject()).has_value());
    return engine;
}

} // namespace

TEST_CASE("a set piece's knobs are in the Parameters panel, labelled for the picture, on the opening layer",
          "[setpiece][reach][ui][adr928]") {
    auto engine = lab();
    auto applied = app::applyPlanDocument(*engine, plan());
    REQUIRE(applied.has_value());
    // The Parameters panel's own grouping, over the registry: group "staging", sub-group
    // "setpiece/<key>", row label `label()`.
    std::map<std::string, std::map<std::string, std::string>> rows; // sub-group -> path leaf -> label
    for (const params::IParameter* p : engine->params().ordered()) {
        if (!ui::detail::pathStartsWith(p->path(), "staging/setpiece/")) {
            continue;
        }
        INFO(p->path());
        CHECK(p->flags().exposed);
        CHECK(p->group() == "staging");
        CHECK(ui::layerShowsPath(ui::AuthoringLayer::Beginner, p->path()));
        CHECK(ui::layerShowsPath(ui::AuthoringLayer::Intermediate, p->path()));
        rows[ui::parameterSubGroup(p->path(), "staging")][std::string(ui::parameterLeaf(p->path()))] = p->label();
    }
    REQUIRE(rows.size() == 3);
    REQUIRE(rows.contains("setpiece/west"));
    REQUIRE(rows.contains("setpiece/field"));
    REQUIRE(rows.contains("setpiece/far"));
    // What a viewer would call each knob, with its unit -- not the scenario author's camelCase.
    CHECK(rows["setpiece/west"]["hoverHeight"] == "hover height above the ground (m)");
    CHECK(rows["setpiece/west"]["beamEmissive"] == "beam brightness (x)");
    CHECK(rows["setpiece/west"]["beamAt"] == "beam at (s)");
    CHECK(rows["setpiece/field"]["beamRed"] == "beam colour: red");
    CHECK(rows["setpiece/far"]["sweepSeconds"] == "sweep (s)");
    for (const auto& [sub, leaves] : rows) {
        for (const auto& [leaf, label] : leaves) {
            INFO(sub << "/" << leaf);
            CHECK(label != leaf); // every one of them has a label of its own
        }
    }
    // Controls: a group on no layer list shows only on Advanced -- which is where `staging/` was.
    CHECK_FALSE(ui::layerShowsPath(ui::AuthoringLayer::Intermediate, "nosuchgroup/setpiece/west/hoverHeight"));
    CHECK(std::find(std::begin(ui::detail::kBeginnerPrefixes), std::end(ui::detail::kBeginnerPrefixes), "staging/") !=
          std::end(ui::detail::kBeginnerPrefixes));
}

TEST_CASE("the Director panel lists each UFO set piece in the words of the picture", "[setpiece][reach][ui][adr929]") {
    auto engine = lab();
    REQUIRE(app::applyPlanDocument(*engine, plan()).has_value());
    const directing::SceneFacts facts = app::sceneFactsFor(*engine);
    REQUIRE(engine->directingPlans().size() == 1);
    const std::vector<ui::SetPieceRow> rows = ui::setPieceRows(engine->directingPlans()[0], facts);
    REQUIRE(rows.size() == 3);
    const ui::SetPieceRow& west = rows[0];
    CHECK(west.key == "west");
    CHECK(west.what == "abduction: lifts 1 animal, flown by saucer");
    CHECK(west.when == "beam at 00:12.000");
    CHECK(west.where == "over (-120, -60)");
    CHECK_THAT(west.how, ContainsSubstring("comes in from the south (180 deg)"));
    CHECK_THAT(west.how, ContainsSubstring("hovers 23 m up"));
    CHECK_THAT(west.how, ContainsSubstring("meant to be seen from about 30 m"));
    CHECK(west.state == "as made");
    CHECK(west.editable);
    CHECK(west.moments == std::vector<std::string>{"approach", "beam", "lift", "depart"});
    const ui::SetPieceRow& field = rows[1];
    CHECK(field.what == "abduction: lifts 2 animals, flown by saucer");
    CHECK_THAT(field.how, ContainsSubstring("comes in from the east (90 deg)"));
    CHECK_THAT(field.how, ContainsSubstring("red beam"));
    CHECK(field.height == 30.0f);
    CHECK(rows[2].what == "survey: the beam sweeps a field and lifts nothing, flown by saucer");
    // The same plan's items in a proposal's list: a set piece is a row, not a plan-wide line.
    directing::PlanParse parsed = directing::parsePlan(plan());
    REQUIRE(parsed.plan);
    const directing::Compilation c = directing::compilePlan(*parsed.plan, facts);
    const std::vector<ui::PlanItemRow> items = ui::planItemRows(c.plan, c.validation);
    const auto setPieceItems = std::count_if(items.begin(), items.end(), [](const ui::PlanItemRow& r) { return r.kind == "set piece"; });
    CHECK(setPieceItems == 3);
}

TEST_CASE("an edit in the UFO set pieces section is one revision and one undo", "[setpiece][reach][ui][undo][adr929]") {
    auto engine = lab();
    app::EditSystem edits;
    {
        directing::PlanParse parsed = directing::parsePlan(plan());
        REQUIRE(parsed.plan);
        const directing::Compilation c = directing::compilePlan(*parsed.plan, app::sceneFactsFor(*engine));
        REQUIRE(app::applyCompilation(*engine, edits.history(), c).has_value());
    }
    const auto hover = [&] { return engine->params().find("staging/setpiece/west/hoverHeight")->baseComponent(0); };
    const auto beamAt = [&] { return engine->params().find("staging/setpiece/west/beamAt")->baseComponent(0); };
    REQUIRE(hover() == 23.0f);
    REQUIRE(beamAt() == 12.0f);
    // The hover height, as the slider's release makes it.
    ui::SetPieceEdit higher;
    higher.slots = {{"hoverHeight", 40.0f}};
    auto compiled = ui::compileSetPieceEdit(engine->directingPlans()[0], "west", higher, app::sceneFactsFor(*engine));
    INFO((compiled ? std::string() : compiled.error().message));
    REQUIRE(compiled.has_value());
    CHECK(compiled->plan.revision == 2);
    REQUIRE(app::applyCompilation(*engine, edits.history(), *compiled, ui::setPieceEditLabel("west", higher)).has_value());
    CHECK(edits.history().undoSize() == 2);
    CHECK(edits.history().undoLabel() == "UFO set piece 'west': hoverHeight");
    CHECK(hover() == 40.0f);
    CHECK(engine->directingPlans()[0].revision == 2);
    // The time, as the "at (s)" field writes it.
    ui::SetPieceEdit later;
    later.seconds = 20.0;
    compiled = ui::compileSetPieceEdit(engine->directingPlans()[0], "west", later, app::sceneFactsFor(*engine));
    REQUIRE(compiled.has_value());
    REQUIRE(app::applyCompilation(*engine, edits.history(), *compiled, ui::setPieceEditLabel("west", later)).has_value());
    CHECK(beamAt() == 20.0f);
    // One undo each, back through both.
    REQUIRE(edits.execute(app::EditAction::Undo, *engine));
    CHECK(beamAt() == 12.0f);
    CHECK(hover() == 40.0f);
    REQUIRE(edits.execute(app::EditAction::Undo, *engine));
    CHECK(hover() == 23.0f);
    CHECK(engine->directingPlans()[0].revision == 1);
    // The template: an abduction becomes a flyby -- what a flyby cannot take goes with it.
    ui::SetPieceEdit flyby;
    flyby.templateName = "flyby";
    auto revised = ui::editSetPiece(engine->directingPlans()[0], "west", flyby);
    REQUIRE(revised.has_value());
    const directing::PlanSetPiece& west = revised->setPieces[0];
    CHECK(west.templateName == "flyby");
    CHECK(std::none_of(west.set.begin(), west.set.end(), [](const auto& o) { return o.first == "animals"; }));
    // A refusal changes nothing: a beam at 1 s would need the craft before the film starts, and the
    // revision that tried would have taken the set piece out and built nothing in its place.
    ui::SetPieceEdit tooEarly;
    tooEarly.seconds = 1.0;
    const std::size_t undoBefore = edits.history().undoSize();
    auto refused = ui::compileSetPieceEdit(engine->directingPlans()[0], "west", tooEarly, app::sceneFactsFor(*engine));
    REQUIRE_FALSE(refused.has_value());
    CHECK_THAT(refused.error().message, ContainsSubstring("not applied"));
    CHECK_THAT(refused.error().message, ContainsSubstring("before the film starts"));
    CHECK(edits.history().undoSize() == undoBefore);
    // And an edit that changes nothing is said, not pushed.
    ui::SetPieceEdit same;
    same.slots = {{"hoverHeight", 23.0f}};
    auto nothing = ui::compileSetPieceEdit(engine->directingPlans()[0], "west", same, app::sceneFactsFor(*engine));
    CHECK_FALSE(nothing.has_value());
}

TEST_CASE("a set piece tuned by hand in the Parameters panel says so in the Director panel", "[setpiece][reach][ui][adr929]") {
    auto engine = lab();
    REQUIRE(app::applyPlanDocument(*engine, plan()).has_value());
    engine->params().find("staging/setpiece/west/hoverHeight")->setBaseComponent(0, 31.0f);
    const std::vector<ui::SetPieceRow> rows = ui::setPieceRows(engine->directingPlans()[0], app::sceneFactsFor(*engine));
    REQUIRE(rows.size() == 3);
    CHECK(rows[0].state == "tuned by hand");
    CHECK_FALSE(rows[0].editable);
    CHECK_THAT(rows[0].whyNot, ContainsSubstring("staging/setpiece/west/"));
    CHECK_THAT(rows[0].whyNot, ContainsSubstring("Reset to default"));
    // Control: the others were not touched and stay editable.
    CHECK(rows[1].state == "as made");
    CHECK(rows[1].editable);
    // Reset to default -- the value the plan made -- and it is the plan's again.
    engine->params().find("staging/setpiece/west/hoverHeight")->resetToDefault();
    CHECK(ui::setPieceRows(engine->directingPlans()[0], app::sceneFactsFor(*engine))[0].state == "as made");
}

TEST_CASE("a set piece's moments are route sources the Modulation panel names", "[setpiece][reach][routes][adr930]") {
    auto engine = lab();
    REQUIRE(app::applyPlanDocument(*engine, plan()).has_value());
    const std::vector<std::string> items = ui::routeSourceItems(engine->signals());
    const auto has = [&](const std::string& item) { return std::find(items.begin(), items.end(), item) != items.end(); };
    CHECK(has("setpiece/field/beam  -  UFO set piece 'field': beam"));
    CHECK(has("setpiece/west/lift  -  UFO set piece 'west': lift"));
    CHECK(has("setpiece/far/sweep  -  UFO set piece 'far': sweep"));
    // Control: a moment a template does not have is not a source.
    CHECK_FALSE(has("setpiece/far/lift  -  UFO set piece 'far': lift"));
}

TEST_CASE("director.inspect_capabilities lists the set piece templates, their slots and the crafts",
          "[setpiece][reach][directing][adr929]") {
    auto engine = lab();
    ai::ToolRegistry registry;
    ai::registerDirectorTools(registry);
    ai::ToolContext ctx(*engine);
    const ai::ToolResult r = registry.invoke("director.inspect_capabilities", json::object(), ctx);
    REQUIRE(r.success);
    REQUIRE(r.value.contains("setPieces"));
    const json& sp = r.value["setPieces"];
    REQUIRE(sp["templates"].size() == 3);
    CHECK(sp["templates"][0]["name"] == "abduction");
    CHECK(sp["templates"][0]["moments"] == json::array({"approach", "beam", "lift", "depart"}));
    bool hover = false;
    for (const json& slot : sp["templates"][0]["slots"]) {
        if (slot["name"] == "hoverHeight") {
            hover = slot["default"] == 23.0 && slot["unit"] == "m" && slot["kind"] == "knob";
        }
    }
    CHECK(hover);
    REQUIRE(sp["crafts"].size() == 1);
    CHECK(sp["crafts"][0]["name"] == "saucer");
    CHECK(sp["crafts"][0]["entity"] == "visitor");
    CHECK(sp["crafts"][0]["beam"] == true);
}
