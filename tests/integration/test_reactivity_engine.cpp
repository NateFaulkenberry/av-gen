// ADR-924/927 through the engine: the default proposal on the glade, compiled and installed the way the
// Director panel and the assistant install a plan.
//
//   * it is ONE undo: the routes and the sources they read go in together, undo takes both back to
//     the byte, redo restores them, and every `produced` fingerprint matches what is installed;
//   * a revision replaces its routes in place and never overwrites one a person has edited since;
//   * the installed routes are seek-exact: a seek lands every target where a play from zero does
//     (ADR-901 replays their sources -- the analysed audio, the section signals, the plan's LFO and
//     timeline);
//   * a save and a reload keep the plan, its routes (each still saying which item made it) and its
//     sources;
//   * the panels' own logic reads it: the Modulation panel's plan note, the Director panel's rows.

#include "app/directing_apply.hpp"
#include "app/directing_context.hpp"
#include "app/engine.hpp"
#include "app/source_document.hpp"
#include "core/time.hpp"
#include "directing/compiler.hpp"
#include "directing/reactivity_proposer.hpp"
#include "params/serialization.hpp"
#include "support/gltf_fixture.hpp"
#include "support/project_round_trip.hpp"
#include "support/reactivity_fixture.hpp"
#include "ui/director_panel_logic.hpp"
#include "ui/edit_history.hpp"
#include "ui/route_row_logic.hpp"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <map>
#include <string>

using namespace avgen;
using namespace avgen::directing;
using nlohmann::json;

namespace {

std::filesystem::path glb() {
    static const auto path = testsupport::writeTriangleGlb("reactivity_engine");
    return path;
}

struct Glade {
    app::Engine engine{app::EngineMode::Offline};
    Glade() { REQUIRE(testsupport::loadGlade(engine, glb(), glb())); }

    [[nodiscard]] Compilation proposal() {
        const SceneFacts facts = app::sceneFactsFor(engine);
        return compilePlan(proposeReactivity(facts.capabilities.reactive(), facts.music).plan, facts);
    }
    void frame(long long k) {
        engine.update(FrameTime{static_cast<double>(k) / 60.0, k == 0 ? 0.0 : 1.0 / 60.0, static_cast<std::uint64_t>(k)});
    }
    void landing(long long k) { engine.update(FrameTime{static_cast<double>(k) / 60.0, 0.0, 0}); }
};

json routesDoc(app::Engine& engine) {
    json out = json::array();
    for (const params::ModRoute& r : engine.modulator().routes()) {
        out.push_back(params::routeToJson(r));
    }
    return out;
}

std::size_t planRoutes(app::Engine& engine, std::string_view planId) {
    return static_cast<std::size_t>(std::count_if(engine.modulator().routes().begin(), engine.modulator().routes().end(),
                                                  [&](const params::ModRoute& r) {
                                                      return r.planItem.starts_with(std::string(planId) + "/");
                                                  }));
}

} // namespace

TEST_CASE("The default proposal installs as one undo: routes and sources in, undo takes both back, redo restores",
          "[directing][reactivity][undo][adr924]") {
    Glade glade;
    ui::EditHistory history;
    const json routesBefore = routesDoc(glade.engine);
    const json sourcesBefore = app::sourcesDocument(glade.engine);

    const Compilation c = glade.proposal();
    REQUIRE_FALSE(c.validation.hasErrors());
    REQUIRE(c.plan.routes.size() >= 15);
    REQUIRE(app::applyCompilation(glade.engine, history, c));
    REQUIRE(history.undoSize() == 1);
    CHECK(planRoutes(glade.engine, c.plan.id) == c.plan.routes.size());
    for (const PlanSource& s : c.plan.sources) {
        INFO(s.signal());
        CHECK(glade.engine.sources().find(s.kind, s.name) != nullptr);
        CHECK(glade.engine.signals().find(s.signal()).has_value());
    }
    // The LFO's own parameters came with it.
    const params::IParameter* beats = glade.engine.params().find("sources/two-bar-breath/beatsPerCycle");
    REQUIRE(beats != nullptr);
    CHECK(beats->baseComponent(0) == 8.0f);
    // Every produced piece of content is there exactly as recorded.
    REQUIRE(glade.engine.directingPlans().size() == 1);
    const Plan& stored = glade.engine.directingPlans()[0];
    CHECK(stored.produced.size() == c.plan.routes.size() + c.plan.sources.size());
    CHECK(app::verifyInstalled(glade.engine, stored.id).empty());
    const json routesAfter = routesDoc(glade.engine);
    const json sourcesAfter = app::sourcesDocument(glade.engine);

    REQUIRE(history.undo(glade.engine).ok());
    CHECK(routesDoc(glade.engine) == routesBefore);
    CHECK(app::sourcesDocument(glade.engine) == sourcesBefore);
    CHECK(glade.engine.directingPlans().empty());
    CHECK(glade.engine.sources().find("lfo", "two-bar-breath") == nullptr);

    REQUIRE(history.redo(glade.engine).ok());
    CHECK(routesDoc(glade.engine) == routesAfter);
    CHECK(app::sourcesDocument(glade.engine) == sourcesAfter);
    CHECK(app::verifyInstalled(glade.engine, "reactivity").empty());
}

TEST_CASE("A revision replaces its routes in place, and never overwrites one a person has edited since",
          "[directing][reactivity][revision][adr924]") {
    Glade glade;
    ui::EditHistory history;
    const Compilation first = glade.proposal();
    REQUIRE(app::applyCompilation(glade.engine, history, first));
    const std::size_t count = glade.engine.modulator().routes().size();

    // Proposed again unchanged: revision 2, every item a replacement, nothing added beside it.
    const Compilation again = glade.proposal();
    CHECK(again.plan.revision == 2);
    CHECK(std::none_of(again.diff.begin(), again.diff.end(), [](const DiffLine& d) { return d.sign == '+'; }));
    CHECK(std::any_of(again.diff.begin(), again.diff.end(), [](const DiffLine& d) { return d.sign == '~'; }));
    CHECK(again.plan.routes.size() == first.plan.routes.size()); // its own routes are not "the author's"
    REQUIRE(app::applyCompilation(glade.engine, history, again));
    CHECK(glade.engine.modulator().routes().size() == count);

    // A person turns the fungi's kick down in the Modulation panel...
    const std::string fungi = planItemId("reactivity", "kick.fungi");
    auto& routes = glade.engine.modulator().routes();
    const auto edited = std::find_if(routes.begin(), routes.end(), [&](const params::ModRoute& r) { return r.planItem == fungi; });
    REQUIRE(edited != routes.end());
    edited->amount = 0.12f;
    // ...and the next revision leaves that route as they left it, saying so, and rebuilds the rest.
    const Compilation third = glade.proposal();
    const auto handEdited = std::find_if(third.validation.issues.begin(), third.validation.issues.end(),
                                         [](const Issue& i) { return i.code == IssueCode::HandEdited; });
    REQUIRE(handEdited != third.validation.issues.end());
    CHECK(handEdited->item == "kick.fungi");
    REQUIRE(app::applyCompilation(glade.engine, history, third));
    const auto kept = std::find_if(glade.engine.modulator().routes().begin(), glade.engine.modulator().routes().end(),
                                   [&](const params::ModRoute& r) { return r.planItem == fungi; });
    REQUIRE(kept != glade.engine.modulator().routes().end());
    CHECK(kept->amount == 0.12f);
    CHECK(glade.engine.modulator().routes().size() == count);

    // And the revision after that still leaves it: keeping a person's edit once is not adopting it.
    const Compilation fourth = glade.proposal();
    CHECK(std::any_of(fourth.validation.issues.begin(), fourth.validation.issues.end(),
                      [](const Issue& i) { return i.code == IssueCode::HandEdited && i.item == "kick.fungi"; }));
    REQUIRE(app::applyCompilation(glade.engine, history, fourth));
    const auto still = std::find_if(glade.engine.modulator().routes().begin(), glade.engine.modulator().routes().end(),
                                    [&](const params::ModRoute& r) { return r.planItem == fungi; });
    REQUIRE(still != glade.engine.modulator().routes().end());
    CHECK(still->amount == 0.12f);

    // The Director panel's rows say which route is no longer the plan's as made.
    const Plan& stored = glade.engine.directingPlans().front();
    const auto rows = ui::reactivityRows(stored, glade.engine.modulator().routes());
    const auto row = std::find_if(rows.begin(), rows.end(), [](const ui::ReactivityRow& r) { return r.key == "kick.fungi"; });
    REQUIRE(row != rows.end());
    CHECK(row->state == "edited by hand");
    CHECK(std::count_if(rows.begin(), rows.end(), [](const ui::ReactivityRow& r) { return r.state == "as made"; }) ==
          static_cast<long>(rows.size()) - 1);
}

TEST_CASE("The installed proposal is seek-exact: a seek lands every planned route's target where a play does",
          "[directing][reactivity][seek][adr924][adr901]") {
    // Mid-groove, inside the drop and in the kickless break: delays, attacks, decays, the section
    // depth, the two-bar breath and the section-keyed hue all carry history or follow the clock.
    const std::vector<long long> instants{1219, 1622, 2711};
    std::vector<std::string> targets;
    std::map<long long, std::map<std::string, float>> played;
    {
        Glade glade;
        ui::EditHistory history;
        const Compilation c = glade.proposal();
        REQUIRE(app::applyCompilation(glade.engine, history, c));
        for (const PlanRoute& r : c.plan.routes) {
            if (std::find(targets.begin(), targets.end(), r.route.target) == targets.end()) {
                targets.push_back(r.route.target);
            }
        }
        long long k = 0;
        for (const long long t : instants) {
            for (; k <= t; ++k) {
                glade.frame(k);
            }
            for (const std::string& path : targets) {
                played[t][path] = glade.engine.params().find(path)->finalComponent(0);
            }
        }
    }
    REQUIRE(targets.size() >= 15);
    Glade glade;
    ui::EditHistory history;
    REQUIRE(app::applyCompilation(glade.engine, history, glade.proposal()));
    std::size_t moved = 0;
    for (const long long t : instants) {
        glade.engine.seekSeconds(static_cast<double>(t) / 60.0);
        glade.landing(t);
        for (const std::string& path : targets) {
            const float seeked = glade.engine.params().find(path)->finalComponent(0);
            INFO(path << " at frame " << t << " (" << static_cast<double>(t) / 60.0 << " s)");
            CHECK(seeked == played[t][path]);
            moved += seeked != glade.engine.params().find(path)->baseComponent(0) ? 1 : 0;
        }
    }
    // Not a comparison of rest values: most targets are away from their bases at these instants.
    CHECK(moved > targets.size());
}

TEST_CASE("A save and a reload keep the plan, its routes and the sources they read",
          "[directing][reactivity][persistence][adr924]") {
    Glade glade;
    ui::EditHistory history;
    const Compilation c = glade.proposal();
    REQUIRE(app::applyCompilation(glade.engine, history, c));
    testsupport::ScratchDir dir{"reactivity_persistence"};
    auto trip = testsupport::saveAndReload(glade.engine, dir / "glade.json");
    INFO((trip ? std::string() : trip.error().message));
    REQUIRE(trip);
    app::Engine& back = *trip->reloaded;
    REQUIRE(back.directingPlans().size() == 1);
    CHECK(back.directingPlans()[0] == glade.engine.directingPlans()[0]);
    CHECK(planRoutes(back, "reactivity") == c.plan.routes.size());
    CHECK(back.sources().find("lfo", "two-bar-breath") != nullptr);
    CHECK(back.sources().find("timeline", "glowing-plants-hue-by-section") != nullptr);
    CHECK(back.params().find("sources/two-bar-breath/beatsPerCycle")->baseComponent(0) == 8.0f);
    CHECK(app::verifyInstalled(back, "reactivity").empty());
    // The Modulation panel's note on a reloaded route still finds its plan and its reason.
    const auto& routes = back.modulator().routes();
    const auto fungi = std::find_if(routes.begin(), routes.end(), [](const params::ModRoute& r) {
        return r.planItem == "reactivity/kick.fungi";
    });
    REQUIRE(fungi != routes.end());
    const ui::RoutePlanNote note = ui::routePlanNote(*fungi, back.directingPlans());
    CHECK(note.show);
    CHECK(note.text == "[plan: kick.fungi]");
    CHECK(note.tooltip.find("fungi") != std::string::npos);
    CHECK(note.tooltip.find("kick") != std::string::npos);
    // A route a person made carries no note; a route whose plan is gone says so.
    params::ModRoute plain = *fungi;
    plain.planItem.clear();
    CHECK_FALSE(ui::routePlanNote(plain, back.directingPlans()).show);
    const ui::RoutePlanNote orphan = ui::routePlanNote(*fungi, {});
    CHECK(orphan.show);
    CHECK(orphan.tooltip.find("no longer in this project") != std::string::npos);
}

TEST_CASE("The Director panel lists a proposal's route and source items, and their findings, in its plan view",
          "[directing][reactivity][ui][adr924]") {
    Glade glade;
    Compilation c = glade.proposal();
    // Add a route the validator refuses, so a row is marked blocked with the reason.
    PlanRoute dark;
    dark.key = "dark";
    dark.route.source = "audio.onsetLow";
    dark.route.target = "nodes/meadow/scatter/stones/emissionGain";
    Plan plan = c.plan;
    plan.routes.push_back(dark);
    c = compilePlan(plan, app::sceneFactsFor(glade.engine));
    const auto rows = ui::planItemRows(c.plan, c.validation);
    const auto kind = [&](std::string_view key) {
        const auto it = std::find_if(rows.begin(), rows.end(), [&](const ui::PlanItemRow& r) { return r.key == key; });
        REQUIRE(it != rows.end());
        return *it;
    };
    CHECK(kind("kick.fungi").kind == "route");
    CHECK(kind("kick.fungi").mark == ui::ItemMark::Ok);
    CHECK(kind("source.two-bar-breath").kind == "source");
    const ui::PlanItemRow blocked = kind("dark");
    CHECK(blocked.mark == ui::ItemMark::Blocked);
    REQUIRE_FALSE(blocked.lines.empty());
    CHECK(blocked.lines.front().find("cannot reach the picture") != std::string::npos);
    // The heading the "Plans in this project" section gives a plan's reactivity.
    const std::string heading = ui::reactivityHeading(c.plan);
    CHECK(heading.find("micro") != std::string::npos);
    CHECK(heading.find("macro") != std::string::npos);
}
