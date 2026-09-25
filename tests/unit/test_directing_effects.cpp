// The Director and ADR-702's effects: the catalogue, effect references, effect windows and field
// cues, and effect undo and persistence (director-system-progress.md, the Slice 0 effect items
// deferred until the refactor merged, and Slice 1's effect compilation).

#include "app/directing_apply.hpp"
#include "app/directing_context.hpp"
#include "app/edit_capture.hpp"
#include "app/engine.hpp"
#include "directing/compiler.hpp"
#include "directing/plan.hpp"
#include "directing/validator.hpp"
#include "params/serialization.hpp"
#include "support/project_round_trip.hpp"
#include "ui/edit_history.hpp"
#include "world/effects/effect_kind.hpp"
#include "world/effects/effect_registry.hpp"
#include "world/effects/effect_stack.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <fmt/ranges.h>
#include <filesystem>
#include <string>

using namespace avgen;
using namespace avgen::directing;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {

std::unique_ptr<app::Engine> benchmark() {
    auto engine = std::make_unique<app::Engine>(app::EngineMode::Offline);
    auto loaded = engine->loadProject(fs::path(AVGEN_SOURCE_DIR) / "examples/world/glowmere-valley-2-multicam.json");
    INFO((loaded ? std::string() : loaded.error().message));
    REQUIRE(loaded.has_value());
    return engine;
}

json routesJson(params::Modulator& modulator) {
    json out = json::array();
    for (const params::ModRoute& r : modulator.routes()) {
        out.push_back(params::routeToJson(r));
    }
    return out;
}

json effectsJson(const std::vector<world::EffectInstance>& list) {
    json out = json::array();
    for (const world::EffectInstance& e : list) {
        out.push_back(e.toJson());
    }
    return out;
}

Plan planFrom(const json& doc) {
    PlanParse parsed = parsePlan(doc);
    for (const Issue& i : parsed.issues) {
        INFO(i.toJson().dump());
    }
    REQUIRE(parsed.plan);
    return *parsed.plan;
}

// "At the second chorus, pulse the Umbra hero effect for two seconds."
json umbraPulsePlan() {
    return json::parse(R"({
      "schemaVersion": 1, "id": "umbra-pulse", "title": "Umbra pulses on the second chorus", "tier": "baked",
      "subjects": [{"alias": "umbra", "text": "the Umbra hero", "hint": "hero"}],
      "cues": [{"key": "pulse", "effect": {"owner": "umbra", "type": "groundPulse"}, "at": "the second chorus",
                "holdSeconds": 2.0}]
    })");
}

} // namespace

TEST_CASE("the effect catalogue is the registry's types and the scene's instances", "[directing][effects][benchmark]") {
    auto engine = benchmark();
    const CapabilityRegistry registry = CapabilityRegistry::fromComposition(*engine->composition());
    const EffectCatalog& effects = registry.effects();
    CHECK(effects.types.size() == world::declaredEffectKinds().size());
    const EffectTypeCapability* pulse = effects.type("groundPulse");
    REQUIRE(pulse != nullptr);
    CHECK(std::find(pulse->owners.begin(), pulse->owners.end(), "entity") != pulse->owners.end());
    CHECK_FALSE(pulse->fields.empty());
    const EffectTypeCapability* aurora = effects.type("aurora");
    REQUIRE(aurora != nullptr);
    CHECK(aurora->owners == std::vector<std::string>{"world"});

    CHECK(effects.instances.size() == engine->effects().size());
    const auto umbra = std::find_if(effects.instances.begin(), effects.instances.end(),
                                    [](const EffectInstanceCapability& i) { return i.id == "umbra-cap-hero-pulse"; });
    REQUIRE(umbra != effects.instances.end());
    CHECK(umbra->type == "groundPulse");
    CHECK(umbra->ownerKind == "entity");
    CHECK(umbra->owner == "umbra-cap");
    CHECK(umbra->activation == "heroFocus");
    CHECK(registry.toJson().contains("effects"));
}

TEST_CASE("an effect reference resolves by owner and type, or says exactly why not", "[directing][effects][benchmark]") {
    auto engine = benchmark();
    const SceneFacts facts = app::sceneFactsFor(*engine);
    Plan plan = planFrom(umbraPulsePlan());
    REQUIRE(resolvePlanSubjects(plan, facts.subjects).empty());

    const ResolvedEffect umbra = resolveEffect(EffectRef{"", "umbra", "groundPulse"}, plan, facts);
    REQUIRE_FALSE(umbra.issue);
    CHECK(umbra.id == "umbra-cap-hero-pulse"); // the owner's ruling: "the Umbra hero effect"

    const ResolvedEffect sky = resolveEffect(EffectRef{"", "world", "aurora"}, plan, facts);
    REQUIRE_FALSE(sky.issue);
    CHECK(sky.id == "aurora");

    const ResolvedEffect wrongOwner = resolveEffect(EffectRef{"", "umbra", "aurora"}, plan, facts);
    REQUIRE(wrongOwner.issue);
    CHECK(wrongOwner.issue->code == IssueCode::CapabilityUnavailable);
    CHECK(wrongOwner.issue->details["owners"] == json::array({"world"}));

    const ResolvedEffect typo = resolveEffect(EffectRef{"", "umbra", "groundPuls"}, plan, facts);
    REQUIRE(typo.issue);
    CHECK(typo.issue->code == IssueCode::UnknownSubject);
    CHECK(typo.issue->suggestions == std::vector<std::string>{"groundPulse"});

    const ResolvedEffect byId = resolveEffect(EffectRef{"camera-travel-beam", "world", "travelBeam"}, plan, facts);
    CHECK_FALSE(byId.issue);
    const ResolvedEffect mismatched = resolveEffect(EffectRef{"aurora", "world", "travelBeam"}, plan, facts);
    CHECK(mismatched.issue);
}

TEST_CASE("activating the Umbra hero effect compiles to its own window, applies as one undo, and survives a save",
          "[directing][effects][benchmark][persistence]") {
    auto engine = benchmark();
    ui::EditHistory history;
    // Captured lists (slider bases included): what an undo restores, and what a save writes.
    const json before = effectsJson(engine->capturedEffects());
    const json sequenceBefore = engine->sequence().toJson();

    const Compilation c = compilePlan(planFrom(umbraPulsePlan()), app::sceneFactsFor(*engine));
    INFO(c.diffText());
    CHECK_FALSE(c.validation.hasErrors());
    // The owner's pulse is untouched (it still fires on hero focus); a copy fires on the chorus.
    const auto original = std::find_if(c.staged.effects.begin(), c.staged.effects.end(),
                                       [](const world::EffectInstance& e) { return e.id == "umbra-cap-hero-pulse"; });
    REQUIRE(original != c.staged.effects.end());
    CHECK(original->activation == world::Activation::HeroFocus);
    const auto window = std::find_if(c.staged.effects.begin(), c.staged.effects.end(),
                                     [](const world::EffectInstance& e) { return e.id == "umbra-pulse-pulse"; });
    REQUIRE(window != c.staged.effects.end());
    CHECK(window->kind == original->kind);
    CHECK(window->owner == original->owner);
    CHECK(window->activation == world::Activation::Window);
    CHECK(std::abs(window->timing.windowStart - 118.6) < 0.1); // the chorus that comes back
    CHECK(window->timing.windowSeconds == 2.0);
    CHECK(c.plan.produced.size() == 1);
    CHECK(c.plan.produced[0].domain == ContentDomain::EffectInstance);
    CHECK(c.diffText().find("Effect \"Ground Pulse\" on umbra-cap (a copy of 'umbra-cap-hero-pulse')") != std::string::npos);
    CHECK(effectsJson(engine->capturedEffects()) == before); // dry run

    REQUIRE(app::applyCompilation(*engine, history, c));
    REQUIRE(history.undoSize() == 1);
    CHECK(engine->params().find("fx/umbra-pulse-pulse/enabled") != nullptr); // an ordinary instance now
    CHECK(engine->sequence().toJson() == sequenceBefore); // nothing else was touched
    const json after = effectsJson(engine->capturedEffects());

    REQUIRE(history.undo(*engine).ok());
    CHECK(effectsJson(engine->capturedEffects()) == before);
    CHECK(engine->params().find("fx/umbra-pulse-pulse/enabled") == nullptr);
    CHECK(engine->directingPlans().empty());
    REQUIRE(history.redo(*engine).ok());
    CHECK(effectsJson(engine->capturedEffects()) == after);

    testsupport::ScratchDir dir{"directing_effect_window"};
    auto trip = testsupport::saveAndReload(*engine, dir / "film.json");
    INFO((trip ? std::string() : trip.error().message));
    REQUIRE(trip);
    CHECK(trip->warnings.empty());
    CHECK(effectsJson(trip->reloaded->capturedEffects()) == effectsJson(engine->capturedEffects()));
    REQUIRE(trip->reloaded->directingPlans().size() == 1);
    const ContentRef& ref = trip->reloaded->directingPlans()[0].produced.at(0);
    const auto content = contentOf(ref, app::sceneFactsFor(*trip->reloaded).staged);
    REQUIRE(content);
    CHECK(fingerprint(*content) == ref.fingerprint); // as installed, so a revision will not call it a hand edit
    const Compilation revised = compilePlan(planFrom(umbraPulsePlan()), app::sceneFactsFor(*trip->reloaded));
    CHECK(revised.plan.revision == 2);
    CHECK(std::none_of(revised.validation.issues.begin(), revised.validation.issues.end(),
                       [](const Issue& i) { return i.code == IssueCode::HandEdited; }));
}

TEST_CASE("an effect field cue is a baked Add on the instance's own parameter", "[directing][effects][benchmark]") {
    auto engine = benchmark();
    const SceneFacts facts = app::sceneFactsFor(*engine);
    const EffectTypeCapability* aurora = facts.capabilities.effects().type("aurora");
    REQUIRE(aurora != nullptr);
    // Any single-number field of the aurora -- read from the catalogue, not named here.
    std::string field;
    for (const std::string& leaf : aurora->fields) {
        const std::vector<float>* base = facts.base("fx/aurora/" + leaf);
        if (base != nullptr && base->size() == 1) {
            field = leaf;
            break;
        }
    }
    REQUIRE_FALSE(field.empty());
    json doc = json::parse(R"({"schemaVersion": 1, "id": "sky", "subjects": [],
      "cues": [{"key": "swell", "effect": {"owner": "world", "type": "aurora"}, "at": "1:00", "value": 0.5,
                "rampSeconds": 0.5, "holdSeconds": 1.0}]})");
    doc["cues"][0]["field"] = field;
    const Compilation c = compilePlan(planFrom(doc), facts);
    INFO(c.diffText());
    CHECK_FALSE(c.validation.hasErrors());
    const auto event = std::find_if(c.staged.sequence.events.begin(), c.staged.sequence.events.end(),
                                    [](const seq::SequenceEvent& e) { return e.id == "sky.swell"; });
    REQUIRE(event != c.staged.sequence.events.end());
    CHECK(event->what.target == "fx/aurora/" + field);
    CHECK(event->what.mode == params::TrackMode::Add);

    doc["cues"][0]["field"] = "notAField";
    const Compilation bad = compilePlan(planFrom(doc), facts);
    CHECK(bad.validation.isBlocked("swell"));
}

TEST_CASE("an operation that adds an effect and a route is one undo, with routes in exactly one record",
          "[directing][effects][undo]") {
    app::Engine engine(app::EngineMode::Offline);
    engine.newComposition();
    ui::EditHistory history;
    const json effectsBefore = effectsJson(engine.capturedEffects());
    const json routesBefore = routesJson(engine.modulator()); // newComposition's own defaults

    app::EditCapture capture;
    capture.begin(engine);
    REQUIRE(engine.editEffects([](std::vector<world::EffectInstance>& list) -> Result<void> {
        auto added = world::addEffect(list, world::EffectOwner::world(), world::EffectKind::Aurora);
        if (!added) {
            return std::unexpected(added.error());
        }
        return {};
    }));
    REQUIRE_FALSE(engine.effects().empty());
    const std::string id = engine.effects().back().id;
    params::ModRoute route;
    route.source = "audio.bass";
    route.target = "fx/" + id + "/enabled";
    engine.modulator().addRoute(route);
    engine.rebind();
    ui::EditCommand command = capture.finish(engine, "Add aurora");
    REQUIRE(command.effects != nullptr);
    CHECK_FALSE(command.effects->routesTouched); // routes live in the automation record only
    REQUIRE(command.automation != nullptr);
    CHECK(command.automation->routesTouched);
    history.push(std::move(command));
    const json effectsAfter = effectsJson(engine.capturedEffects());

    REQUIRE(history.undo(engine).ok());
    CHECK(effectsJson(engine.capturedEffects()) == effectsBefore);
    CHECK(routesJson(engine.modulator()) == routesBefore);
    CHECK(engine.params().find("fx/" + id + "/enabled") == nullptr);
    REQUIRE(history.redo(engine).ok());
    CHECK(effectsJson(engine.capturedEffects()) == effectsAfter);
    REQUIRE(engine.modulator().routes().size() == routesBefore.size() + 1);
    CHECK(engine.modulator().routes().back().target == "fx/" + id + "/enabled");
}
