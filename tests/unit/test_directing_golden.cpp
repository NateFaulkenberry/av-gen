// Golden Director Plans (spec §39): each file in tests/data/directing/golden is a request, its plan,
// and what the Director must make of it on the benchmark scene (Glowmere Valley 2 multicam).
//
// For every golden:  plan -> validation (the expected error codes, exactly; the expected blocked
// items, exactly) -> compiled content (the expected produced pieces, by domain) -> the diff (the
// expected lines) -> apply as one undo -> save after a frame -> reload -> the plan and every
// fingerprint intact -> recompile on the reloaded project: revision 2, no hand edits, and the same
// content again (deterministic equivalence) -> undo, and the scene is as it started.
//
// Adding a golden is adding a file; nothing here names one.

#include "app/directing_apply.hpp"
#include "app/directing_context.hpp"
#include "app/engine.hpp"
#include "directing/compiler.hpp"
#include "directing/plan.hpp"
#include "support/project_assets.hpp"
#include "support/project_round_trip.hpp"
#include "ui/edit_history.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <fmt/ranges.h>
#include <filesystem>
#include <map>
#include <set>
#include <string>

using namespace avgen;
using namespace avgen::directing;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {

json stagedJson(const Staging& s) {
    json effects = json::array();
    for (const world::EffectInstance& e : s.effects) {
        effects.push_back(e.toJson());
    }
    return {{"sequence", s.sequence.toJson()}, {"cameras", s.cameras.toJson()}, {"effects", std::move(effects)}};
}

} // namespace

TEST_CASE("golden Director plans compile, apply, round-trip and recompile exactly as recorded",
          "[directing][golden][benchmark]") {
    testsupport::skipUnlessGlowmereBenchmarkAssetsPresent();
    const fs::path dir = fs::path(AVGEN_SOURCE_DIR) / "tests/data/directing/golden";
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.path().extension() == ".json") {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    REQUIRE(files.size() >= 15);

    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(fs::path(AVGEN_SOURCE_DIR) / "examples/world/glowmere-valley-2-multicam.json"));
    const json startingPoint = stagedJson(app::sceneFactsFor(engine).staged);
    testsupport::ScratchDir scratch{"directing_golden"};

    for (const fs::path& file : files) {
        const json golden = testsupport::readJson(file);
        const std::string name = file.stem().string();
        INFO("golden: " << name << " -- " << golden.value("description", std::string{}));
        const json& expect = golden.at("expect");

        PlanParse parsed = parsePlan(golden.at("plan"));
        for (const Issue& i : parsed.issues) {
            INFO(i.toJson().dump());
        }
        REQUIRE(parsed.plan);
        CHECK(parsed.issues.empty());

        ui::EditHistory history;
        const Compilation c = compilePlan(*parsed.plan, app::sceneFactsFor(engine));
        INFO(c.diffText());

        // Diagnostics: exactly the expected error codes, and exactly the expected blocked items.
        std::set<std::string> errors;
        for (const Issue& i : c.validation.issues) {
            if (i.severity == Severity::Error) {
                errors.insert(issueCodeName(i.code));
            }
        }
        const auto expectedErrors = expect.at("errors").get<std::set<std::string>>();
        CHECK(errors == expectedErrors);
        CHECK(c.validation.blocked == expect.at("blocked").get<std::set<std::string>>());

        // Content: exactly the expected pieces, by domain.
        std::map<std::string, int> produced;
        for (const ContentRef& ref : c.plan.produced) {
            ++produced[contentDomainName(ref.domain)];
        }
        CHECK(produced == expect.at("produced").get<std::map<std::string, int>>());
        for (const std::string& line : expect.at("diff").get<std::vector<std::string>>()) {
            INFO("expected diff text: " << line);
            CHECK(c.diffText().find(line) != std::string::npos);
        }
        // Deterministic: the same plan against the same scene compiles to the same bytes.
        CHECK(stagedJson(compilePlan(*parsed.plan, app::sceneFactsFor(engine)).staged) == stagedJson(c.staged));

        if (!c.changesAnything()) {
            CHECK(produced.empty());
            continue;
        }

        // Apply, save after a frame, reload.
        REQUIRE(app::applyCompilation(engine, history, c));
        REQUIRE(history.undoSize() == 1);
        auto trip = testsupport::saveAndReload(engine, scratch / (name + ".json"));
        INFO((trip ? std::string() : trip.error().message));
        REQUIRE(trip);
        for (const std::string& w : trip->warnings) {
            INFO("reload warning: " << w);
        }
        INFO(fmt::format("warnings: {}", fmt::join(trip->warnings, " | ")));
        // One known false positive, and only it: the sequencer's bake warns that a first shot which
        // inherits its camera has nothing placing the MAIN camera, but a Director shot that cuts to
        // a rig on the camera track does not use the main camera (ADR-245) -- the bake cannot see
        // the camera track. Recorded in the progress record's known issues.
        // And one expected statement, only for a live plan (ADR-763/766): its goals and orders act on
        // the live system and are forwarded rather than baked -- which is what "live" means.
        const bool live = parsed.plan->tier != Tier::Baked;
        std::vector<std::string> unexpected;
        for (const std::string& w : trip->warnings) {
            const bool inherits = w.find("is the first shot and inherits its camera") != std::string::npos;
            const bool forwarded = live && w.find("act on a live system and cannot be baked") != std::string::npos;
            if (!inherits && !forwarded) {
                unexpected.push_back(w);
            }
        }
        CHECK(unexpected.empty());
        const auto& plans = trip->reloaded->directingPlans();
        REQUIRE(plans.size() == 1);
        CHECK(plans[0] == engine.directingPlans().at(0));
        const Staging reloaded = app::sceneFactsFor(*trip->reloaded).staged;
        for (const ContentRef& ref : plans[0].produced) {
            INFO(contentDomainName(ref.domain) << " " << ref.id);
            const auto content = contentOf(ref, reloaded);
            REQUIRE(content);
            CHECK(fingerprint(*content) == ref.fingerprint);
        }
        // Recompile on the reloaded project: a revision that finds its own content untouched and
        // rebuilds exactly what is there.
        const Compilation again = compilePlan(*parsed.plan, app::sceneFactsFor(*trip->reloaded));
        CHECK(again.plan.revision == 2);
        CHECK(std::none_of(again.validation.issues.begin(), again.validation.issues.end(),
                           [](const Issue& i) { return i.code == IssueCode::HandEdited; }));
        INFO(fmt::format("recompile differs at: {}", fmt::join(testsupport::differingPaths(stagedJson(reloaded), stagedJson(again.staged)), ", ")));
        CHECK(stagedJson(again.staged) == stagedJson(reloaded));

        // Undo, and the benchmark is as it started for the next golden.
        REQUIRE(history.undo(engine).ok());
        CHECK(stagedJson(app::sceneFactsFor(engine).staged) == startingPoint);
        CHECK(engine.directingPlans().empty());
    }
}

#include <chrono>

TEST_CASE("the Director's own costs on the benchmark, measured", "[directing][performance][benchmark]") {
    // Spec §37: measure, do not assume. Facts are a copy of the scene's plans, sequence, cameras,
    // effects and every parameter base (5,500+ on this film); compilation is arithmetic over them;
    // applying reinstalls the sequence and the cameras. Reported, and bounded loosely in an optimised
    // build only (docs/testing.md: a wall-clock ceiling means nothing in debug, and machine load moves
    // it -- re-run alone before believing a failure).
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(fs::path(AVGEN_SOURCE_DIR) / "examples/world/glowmere-valley-2-multicam.json"));
    const json doc = testsupport::readJson(fs::path(AVGEN_SOURCE_DIR) / "tests/data/directing/golden/rook_backflip.json");
    PlanParse parsed = parsePlan(doc.at("plan"));
    REQUIRE(parsed.plan);
    using clock = std::chrono::steady_clock;
    const auto ms = [](clock::duration d) { return std::chrono::duration<double, std::milli>(d).count(); };

    constexpr int kRuns = 10;
    double factsMs = 0.0;
    double compileMs = 0.0;
    SceneFacts facts;
    for (int i = 0; i < kRuns; ++i) {
        const auto a = clock::now();
        facts = app::sceneFactsFor(engine);
        const auto b = clock::now();
        const Compilation c = compilePlan(*parsed.plan, facts);
        const auto e = clock::now();
        factsMs += ms(b - a);
        compileMs += ms(e - b);
    }
    ui::EditHistory history;
    const Compilation c = compilePlan(*parsed.plan, facts);
    const auto a = clock::now();
    REQUIRE(app::applyCompilation(engine, history, c));
    const double applyMs = ms(clock::now() - a);
    WARN(fmt::format("director costs on glowmere-valley-2-multicam: facts {:.2f} ms, validate+compile {:.2f} ms "
                     "(mean of {}), apply {:.1f} ms",
                     factsMs / kRuns, compileMs / kRuns, kRuns, applyMs));
#ifdef NDEBUG
    CHECK(factsMs / kRuns < 250.0);
    CHECK(compileMs / kRuns < 250.0);
#endif
}
