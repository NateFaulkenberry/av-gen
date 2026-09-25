// The Director panel's decisions (spec §35, ADR-762), asked without a window, against the real
// benchmark compile so the rows are what the person would actually see.

#include "app/directing_context.hpp"
#include "app/engine.hpp"
#include "directing/compiler.hpp"
#include "directing/plan.hpp"
#include "support/project_round_trip.hpp"
#include "ui/director_panel_logic.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>

using namespace avgen;
using namespace avgen::ui;
namespace fs = std::filesystem;

namespace {

const PlanItemRow* row(const std::vector<PlanItemRow>& rows, std::string_view key) {
    const auto it = std::find_if(rows.begin(), rows.end(), [&](const PlanItemRow& r) { return r.key == key; });
    return it == rows.end() ? nullptr : &*it;
}

bool anyLine(const PlanItemRow& r, std::string_view text) {
    return std::any_of(r.lines.begin(), r.lines.end(), [&](const std::string& l) { return l.find(text) != std::string::npos; });
}

} // namespace

TEST_CASE("the benchmark proposal, as the Director panel shows it", "[directing][panel]") {
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(fs::path(AVGEN_SOURCE_DIR) / "examples/world/glowmere-valley-2-multicam.json"));
    const nlohmann::json golden =
        testsupport::readJson(fs::path(AVGEN_SOURCE_DIR) / "tests/data/directing/golden/rook_backflip.json");
    directing::PlanParse parsed = directing::parsePlan(golden.at("plan"));
    REQUIRE(parsed.plan);
    const directing::SceneFacts facts = app::sceneFactsFor(engine);
    const directing::Compilation c = directing::compilePlan(*parsed.plan, facts);

    const std::vector<PlanItemRow> rows = planItemRows(c.plan, c.validation);
    // "✗ Backflip unavailable", "! / ✗ Jump clearance insufficient" -- the spec's own examples.
    const PlanItemRow* run = row(rows, "rook-run");
    REQUIRE(run != nullptr);
    CHECK(run->kind == "performance");
    CHECK(run->mark == ItemMark::Blocked);
    CHECK(anyLine(*run, "does not have a backflip capability"));
    CHECK(anyLine(*run, "needs 5.75 m"));
    CHECK(run->lines.front().find("warning") == std::string::npos); // errors first
    // The shot compiles.
    const PlanItemRow* shot = row(rows, "rook-umbra");
    REQUIRE(shot != nullptr);
    CHECK(shot->mark == ItemMark::Ok);
    CHECK(shot->label == "rook-umbra");
    // What waits on the impossible is blocked, and says so.
    REQUIRE(row(rows, "umbra-pulse") != nullptr);
    CHECK(row(rows, "umbra-pulse")->mark == ItemMark::Blocked);

    // Changes: grouped by item, findings left to the rows.
    const std::vector<ChangeGroup> groups = changeGroups(c.diff);
    const auto shotGroup = std::find_if(groups.begin(), groups.end(), [](const ChangeGroup& g) { return g.item == "rook-umbra"; });
    REQUIRE(shotGroup != groups.end());
    CHECK(shotGroup->lines.size() >= 3); // the camera, the shot, the cut (and the rise/pass keys)
    for (const ChangeGroup& g : groups) {
        for (const directing::DiffLine& l : g.lines) {
            CHECK(l.sign != '!');
        }
    }
    CHECK(std::none_of(groups.begin(), groups.end(), [](const ChangeGroup& g) { return g.item == "rook-run"; }));

    // Context: the playhead, the section there, and every subject as resolved.
    const ContextView ctx = contextView(c.plan, facts.music, 118.7);
    CHECK(ctx.time == "01:58.700");
    CHECK(ctx.section == "chorus 2"); // "the second chorus" is 118.6 s on the benchmark
    REQUIRE(ctx.subjects.size() == 2);
    CHECK(ctx.subjects[0] == "rook -> rook (entity)");
    CHECK(ctx.subjects[1].find("umbra -> umbra-cap") == 0);
}

TEST_CASE("a plan-wide finding gets a row of its own, first", "[directing][panel]") {
    directing::Plan plan;
    plan.id = "p";
    plan.title = "Title";
    directing::PlanShot s;
    s.key = "s";
    s.name = "one";
    plan.shots.push_back(s);
    directing::Validation v;
    directing::Issue wide;
    wide.severity = directing::Severity::Warning;
    wide.message = "no song is loaded";
    v.issues.push_back(wide);
    directing::Issue shotWarn;
    shotWarn.severity = directing::Severity::Warning;
    shotWarn.item = "s";
    shotWarn.message = "not at a cut";
    v.issues.push_back(shotWarn);
    const auto rows = planItemRows(plan, v);
    REQUIRE(rows.size() == 2);
    CHECK(rows[0].key.empty());
    CHECK(rows[0].mark == ItemMark::Warning);
    CHECK(rows[0].label == "Title");
    CHECK(rows[1].mark == ItemMark::Warning);
}

TEST_CASE("the panel's buttons: what is live, and why not", "[directing][panel]") {
    SECTION("nothing proposed") {
        const PanelActions a = panelActions(PanelState{});
        CHECK_FALSE(a.accept.enabled);
        CHECK_FALSE(a.reject.enabled);
        CHECK_FALSE(a.preview.enabled);
        CHECK_FALSE(a.endPreview.enabled);
        CHECK(a.accept.why == "no proposal is waiting");
    }
    SECTION("a proposal that changes something") {
        const PanelActions a = panelActions(PanelState{true, true, true, false, false});
        CHECK(a.accept.enabled);
        CHECK(a.reject.enabled);
        CHECK(a.preview.enabled);
        CHECK_FALSE(a.endPreview.enabled);
        CHECK_FALSE(a.revertPreviewFirst);
    }
    SECTION("everything blocked: nothing to accept or preview, but it can be rejected") {
        const PanelActions a = panelActions(PanelState{true, true, false, false, false});
        CHECK_FALSE(a.accept.enabled);
        CHECK(a.accept.why == "nothing to apply: every item is blocked");
        CHECK_FALSE(a.preview.enabled);
        CHECK(a.reject.enabled);
    }
    SECTION("previewing, nothing done since: Accept and Reject take the preview out first") {
        const PanelActions a = panelActions(PanelState{true, true, true, true, true});
        CHECK_FALSE(a.preview.enabled);
        CHECK(a.endPreview.enabled);
        CHECK(a.revertPreviewFirst);
        CHECK(a.accept.enabled);
    }
    SECTION("previewing, with later edits: never undo somebody's later work") {
        const PanelActions a = panelActions(PanelState{true, true, true, true, false});
        CHECK_FALSE(a.endPreview.enabled);
        CHECK(a.endPreview.why == "later edits were made: undo the preview from the history");
        CHECK_FALSE(a.revertPreviewFirst);
        CHECK(a.accept.enabled);
    }
}

TEST_CASE("the Record button: only for a live proposal, and it holds Accept while it runs", "[directing][panel]") {
    PanelState live{true, true, true, false, false, true, false};
    PanelActions a = panelActions(live);
    CHECK(a.record.enabled);
    CHECK(a.accept.enabled);

    PanelState baked = live;
    baked.liveToRecord = false;
    a = panelActions(baked);
    CHECK_FALSE(a.record.enabled);
    CHECK(a.record.why == "nothing live to record: every performance is already baked");

    PanelState running = live;
    running.recording = true;
    a = panelActions(running);
    CHECK_FALSE(a.record.enabled);
    CHECK_FALSE(a.accept.enabled); // what would be accepted is about to change
    CHECK_FALSE(a.preview.enabled);
    CHECK(a.reject.enabled);       // declining is always possible
    CHECK(a.cancelRecording.enabled);
    CHECK_FALSE(panelActions(live).cancelRecording.enabled);

    PanelState stalePreview = live;
    stalePreview.previewing = true;
    stalePreview.previewIsNewest = false;
    CHECK_FALSE(panelActions(stalePreview).record.enabled);
}

TEST_CASE("an Info finding is said on its row but marks nothing", "[directing][panel]") {
    directing::Plan plan;
    plan.id = "p";
    directing::PlanShot s;
    s.key = "s";
    s.name = "one";
    plan.shots.push_back(s);
    directing::Validation v;
    directing::Issue kept;
    kept.severity = directing::Severity::Info;
    kept.item = "s";
    kept.message = "locked: this shot keeps the frame from 'UFO Watch'";
    v.issues.push_back(kept);
    const auto rows = planItemRows(plan, v);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].mark == ItemMark::Ok);
    REQUIRE(rows[0].lines.size() == 1);
    CHECK(rows[0].lines[0] == kept.message);
}

TEST_CASE("Modify needs a follow-up; Regenerate needs only a waiting proposal; neither while recording",
          "[directing][panel]") {
    PanelState waiting{true, true, true, false, false, false, false, false};
    PanelActions a = panelActions(waiting);
    CHECK_FALSE(a.modify.enabled);
    CHECK(a.modify.why == "say what to change in the box first");
    CHECK(a.regenerate.enabled);
    waiting.followUp = true;
    a = panelActions(waiting);
    CHECK(a.modify.enabled);
    PanelState recording = waiting;
    recording.recording = true;
    a = panelActions(recording);
    CHECK_FALSE(a.modify.enabled);
    CHECK_FALSE(a.regenerate.enabled);
    const PanelActions none = panelActions(PanelState{});
    CHECK_FALSE(none.modify.enabled);
    CHECK_FALSE(none.regenerate.enabled);
    // Even a proposal that builds nothing can be modified or regenerated: that is how it is fixed.
    PanelState empty{true, true, false, false, false, false, false, true};
    CHECK(panelActions(empty).modify.enabled);
    CHECK(panelActions(empty).regenerate.enabled);
}
