// The Director's capability registry is generated from engine data, never listed (ADR-754), and the
// Director module depends on no AI code (ADR-750).
//
// The benchmark is Rook in Glowmere Valley 2 multicam. His card has to be what his entity and his
// loaded rig say -- run, walk, jump, fall, land available; no backflip, because no activity maps one
// and no clip on the alien rig is one -- so that "Rook backflips over Umbra" is refused before it is
// compiled rather than played as whatever the fallback clip is.

#include "ai/capabilities.hpp"
#include "ai/engine_tools.hpp"
#include "ai/tool_api.hpp"
#include "app/engine.hpp"
#include "directing/capabilities.hpp"
#include "entity/entity.hpp"
#include "scene/composition.hpp"
#include "seq/events.hpp"
#include "seq/sequence.hpp"
#include "support/project_assets.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <string>

using namespace avgen;
namespace fs = std::filesystem;
using Catch::Matchers::WithinAbs;

namespace {

bool contains(const std::vector<std::string>& list, std::string_view item) {
    return std::find(list.begin(), list.end(), item) != list.end();
}

} // namespace

TEST_CASE("Rook's card is what his entity and his loaded rig say, and has no backflip",
          "[directing][capabilities][benchmark]") {
    testsupport::skipUnlessGlowmereBenchmarkAssetsPresent();
    app::Engine engine(app::EngineMode::Offline);
    const fs::path project = fs::path(AVGEN_SOURCE_DIR) / "examples/world/glowmere-valley-2-multicam.json";
    auto loaded = engine.loadProject(project);
    INFO((loaded ? std::string() : loaded.error().message));
    REQUIRE(loaded.has_value());
    REQUIRE(engine.composition() != nullptr);

    const directing::CapabilityRegistry registry =
        directing::CapabilityRegistry::fromComposition(*engine.composition());
    const directing::CharacterCard* rook = registry.character("rook");
    REQUIRE(rook != nullptr);
    INFO(rook->toJson().dump(2));
    REQUIRE(rook->rigLoaded);

    for (const char* activity : {"idle", "walk", "run", "jump", "fall", "land"}) {
        INFO(activity);
        CHECK(rook->can(activity));
    }
    CHECK_FALSE(rook->can("backflip"));
    CHECK(rook->activity("backflip") == nullptr);
    // As a set: the scene stores the activity map as a JSON object, so card order is key order.
    std::vector<std::string> airborne = rook->available(directing::ActivityKind::Airborne);
    std::sort(airborne.begin(), airborne.end());
    CHECK(airborne == std::vector<std::string>{"fall", "jump", "land"});

    // Semantic names first; the clip is information. `run` is the alien pack's `Running`: 0.70 s of
    // playable span. (The feasibility report's 0.73 s is the clip's last key time; the engine's
    // `AnimationClip::length()` subtracts the 1/30 s the exporter starts at, and length is what a
    // cue plays.)
    const directing::ActivityCapability* run = rook->activity("run");
    REQUIRE(run != nullptr);
    CHECK(run->clip == "Running");
    CHECK_THAT(run->seconds, WithinAbs(0.70, 0.005));
    CHECK(run->kind == directing::ActivityKind::Ground);
    // Measured (ADR-821, ADR-761): the scout's jump clip loops -- its end joins its start -- which is
    // why a compiled jump plays it `once` rather than trusting this flag.
    CHECK(rook->activity("jump")->loops);

    // `Jump_running` was the clip nothing mapped until ADR-822 mapped it as `jumpRunning` on all five
    // aliens. The card follows the scene file: mapped, available, and so not in `unmappedClips`.
    const directing::ActivityCapability* jumpRunning = rook->activity("jumpRunning");
    REQUIRE(jumpRunning != nullptr);
    CHECK(jumpRunning->clip == "Jump_running");
    CHECK(jumpRunning->available);
    // Custom, not Airborne: `jumpRunning` is a clip-map name, not an `entity::Activity`, and the kind
    // is the engine's classification, not a guess from the name. So the airborne set checked above
    // -- what the backflip refusal offers instead ("Available airborne actions: fall, jump, land")
    // -- is unchanged, which is the intent: a plan says "jump", and the compiler picks the clip.
    CHECK(jumpRunning->kind == directing::ActivityKind::Custom);
    CHECK_FALSE(contains(rook->unmappedClips, "Jump_running"));
    CHECK_FALSE(contains(rook->unmappedClips, "Running"));

    // His jump envelope is the engine's default, and says so: Rook has no `explore` behaviour.
    CHECK(rook->jump.source == "default");
    CHECK_THAT(rook->jump.apex, WithinAbs(1.1, 1e-6));
    CHECK_THAT(rook->runSpeed, WithinAbs(7.3892, 1e-3));

    // Umbra is a hero, not a character: nothing here claims otherwise.
    CHECK(registry.character("umbra-cap") == nullptr);
    CHECK(registry.characters().size() == engine.composition()->entities().size());
}

TEST_CASE("a character whose rig did not load can do nothing, rather than anything",
          "[directing][capabilities]") {
    app::Engine engine(app::EngineMode::Offline);
    engine.newComposition();
    entity::EntityDesc ghost;
    ghost.name = "ghost";
    ghost.clips = {{"run", "Running"}, {"jump", "Jumping"}};
    REQUIRE(engine.composition()->setEntities({ghost}).has_value());

    const auto registry = directing::CapabilityRegistry::fromComposition(*engine.composition());
    const directing::CharacterCard* card = registry.character("ghost");
    REQUIRE(card != nullptr);
    CHECK_FALSE(card->rigLoaded);
    CHECK_FALSE(card->can("run"));
    CHECK(card->activity("run") != nullptr); // mapped, so the validator can say "mapped but missing"
    CHECK(card->available(directing::ActivityKind::Airborne).empty());
}

TEST_CASE("camera and event catalogues are the engine's own name tables and tiers",
          "[directing][capabilities]") {
    app::Engine engine(app::EngineMode::Offline);
    engine.newComposition();
    const auto registry = directing::CapabilityRegistry::fromComposition(*engine.composition());

    const directing::CameraCatalog& cameras = registry.cameras();
    CHECK(cameras.presets.size() == seq::allCameraPresets().size());
    CHECK(cameras.behaviors == std::vector<std::string>{"chase", "orbit", "pov"});
    REQUIRE_FALSE(cameras.rigs.empty()); // the main camera, always
    CHECK_FALSE(cameras.cutTransitions.empty());
    CHECK_FALSE(cameras.sequenceTransitions.empty());

    // Every trigger and action the dispatcher knows, with the tier the engine gives it. Checked
    // against the predicates directly, so a tier change in seq/events.cpp moves this too.
    const directing::EventCatalog& events = registry.events();
    CHECK(events.triggers.size() == 12);
    CHECK(events.actions.size() == 8); // ADR-828 added `characterGoal`, the Director's Slice 4 request
    CHECK(std::any_of(events.actions.begin(), events.actions.end(),
                      [](const directing::EventKindCapability& a) { return a.name == "characterGoal"; }));
    for (const directing::EventKindCapability& t : events.triggers) {
        INFO(t.name);
        CHECK(t.deterministic == seq::triggerIsScheduled(*seq::triggerKindFromName(t.name)));
    }
    for (const directing::EventKindCapability& a : events.actions) {
        INFO(a.name);
        CHECK(a.deterministic == seq::actionIsBaked(*seq::eventActionKindFromName(a.name)));
    }
    const auto cue = std::find_if(events.triggers.begin(), events.triggers.end(),
                                  [](const auto& t) { return t.name == seq::triggerKindName(seq::TriggerKind::Cue); });
    REQUIRE(cue != events.triggers.end());
    CHECK(cue->deterministic);
}

TEST_CASE("the directing module includes no AI code and names no AI vendor",
          "[directing][boundary]") {
    // ADR-750: `src/ai/` may depend on `src/directing/`, never the reverse. A plan, a validator and
    // a compiler that reached into a provider could not be used without one.
    const fs::path root = fs::path(AVGEN_SOURCE_DIR) / "src" / "directing";
    REQUIRE(fs::is_directory(root));
    std::size_t files = 0;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        ++files;
        std::ifstream in(entry.path());
        std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
        INFO(entry.path().string());
        CHECK(text.find("#include \"ai/") == std::string::npos);
        std::string lower = text;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        for (const char* vendor : {"anthropic", "openai", "gemini", "claude", "llama", "laya"}) {
            INFO(vendor);
            // Whole words: "playable" contains "laya", and that is not a dependency.
            CHECK_FALSE(std::regex_search(lower, std::regex(std::string("\\b") + vendor + "\\b")));
        }
    }
    CHECK(files >= 2);
}


TEST_CASE("capability.list derives availability from the tools, so a domain with tools is never 'unavailable'",
          "[directing][capabilities][ai]") {
    // `entity` and `render` were declared unavailable by hand while `entity.list` and `render.probe`
    // sat in them. Generated now: the registry decides.
    app::Engine engine(app::EngineMode::Offline);
    ai::ToolRegistry registry;
    ai::registerEngineTools(registry);
    const nlohmann::json doc = ai::capabilityDocument(registry, engine);
    std::size_t domainsWithTools = 0;
    for (const nlohmann::json& domain : doc.at("domains")) {
        INFO(domain.at("id").get<std::string>());
        if (domain.contains("operations") && !domain.at("operations").empty()) {
            ++domainsWithTools;
            CHECK(domain.at("available").get<bool>());
            CHECK_FALSE(domain.contains("unavailableReason"));
        }
    }
    CHECK(domainsWithTools > 0);
    // Every tool's domain is listed.
    for (const ai::Tool* tool : registry.all()) {
        const std::string id(tool->definition.domain());
        INFO(id);
        CHECK(std::any_of(doc.at("domains").begin(), doc.at("domains").end(),
                          [&](const nlohmann::json& d) { return d.at("id") == id; }));
    }
}
