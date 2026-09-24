// ADR-702's one effect list, through both of the engine's serialisers.
//
// A scene holds its effects in `Composition::toJson` (the scene file); a session holds them in
// `Engine::projectDocument` (the project, which carries the routes and the parameter values too).
// Several effects on several owners must survive either with their ids, owners, stack order,
// enable state, values and modulation -- and a save of a load of a save must be the save.
//
// The last case is the one the week's worst defect lived in: it only appeared after a FRAME had
// run, because a frame is what writes the live state a save photographs (ADR-264, and the
// "saving a project does not move its hero anchors" case in test_glowmere_multicam_defects.cpp,
// whose pattern this copies). A round trip straight after a load passes and proves nothing.

#include "app/engine.hpp"
#include "assets/asset_registry.hpp"
#include "core/time.hpp"
#include "params/modulation.hpp"
#include "scene/composition.hpp"
#include "world/atmospherics.hpp"
#include "world/effects/effect_instance.hpp"
#include "world/effects/effect_params.hpp"
#include "world/effects/effect_stack.hpp"
#include "world/effects/field_bus.hpp"
#include "world/hero.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <unistd.h>
#include <vector>

using namespace avgen;
using nlohmann::json;
namespace fs = std::filesystem;
using world::EffectKind;
using world::EffectOwner;

namespace {

fs::path scratch(const char* name) {
    const fs::path dir =
        fs::temp_directory_path() / ("avgen_effects_project_" + std::to_string(getpid()) + "_" + name);
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

json readJson(const fs::path& path) {
    std::ifstream in(path);
    REQUIRE(in.good());
    json doc = json::parse(in, nullptr, false);
    REQUIRE_FALSE(doc.is_discarded());
    return doc;
}

FrameTime frameAt(double seconds) {
    FrameTime t;
    t.renderTime = seconds;
    t.deltaTime = 0.0;
    t.frameIndex = 0;
    return t;
}

// A scene's worth of effects on four owners: two on the World, one each on two entities, one on a
// camera -- one of them disabled and one with a value that is not its type's default.
std::vector<world::EffectInstance> severalEffects() {
    std::vector<world::EffectInstance> effects;
    const auto add = [&](const EffectOwner& owner, EffectKind kind) {
        auto id = world::addEffect(effects, owner, kind);
        REQUIRE(id.has_value());
        return *id;
    };
    add(EffectOwner::world(), EffectKind::Aurora);
    add(EffectOwner::world(), EffectKind::Tornado);
    add(EffectOwner::entity("rook"), EffectKind::GroundPulse);
    add(EffectOwner::entity("sage"), EffectKind::GroundPulse);
    add(EffectOwner::camera("crane"), EffectKind::TravelBeam);
    // Moved, so stack order is not just insertion order.
    REQUIRE(world::moveEffect(effects, "tornado", -1));
    effects[world::findEffect(effects, "sage-ground-pulse")].enabled = false;
    effects[world::findEffect(effects, "rook-ground-pulse")].wave.appearance.intensity = 6.5f;
    REQUIRE(world::validateEffects(effects).has_value());
    return effects;
}

std::vector<world::HeroPoint> twoHeroes() {
    world::HeroPoint rook;
    rook.name = "rook";
    rook.position = glm::vec3(10.0f, 0.0f, 0.0f);
    world::HeroPoint sage;
    sage.name = "sage";
    sage.position = glm::vec3(-10.0f, 0.0f, 5.0f);
    return {rook, sage};
}

json effectsJson(const std::vector<world::EffectInstance>& effects) {
    json out = json::array();
    for (const auto& e : effects) {
        out.push_back(e.toJson());
    }
    return out;
}

// Everything in a project document that names an effect parameter, keyed so two documents compare.
json fxRoutes(const json& doc) {
    json out = json::array();
    for (const json& r : doc.value("routes", json::array())) {
        if (r.value("target", std::string()).starts_with("fx/")) {
            out.push_back(r);
        }
    }
    return out;
}
json fxParameters(const json& doc) {
    json out = json::object();
    for (const auto& [path, value] : doc.value("parameters", json::object()).items()) {
        if (path.starts_with("fx/")) {
            out[path] = value;
        }
    }
    return out;
}

void checkSameEffects(const std::vector<world::EffectInstance>& a, const std::vector<world::EffectInstance>& b) {
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        INFO("effect " << i << ": " << a[i].id);
        CHECK(b[i].id == a[i].id);
        CHECK(b[i].kind == a[i].kind);
        CHECK(b[i].owner == a[i].owner);
        CHECK(b[i].order == a[i].order);
        CHECK(b[i].enabled == a[i].enabled);
        CHECK(b[i].toJson() == a[i].toJson());
    }
}

} // namespace

TEST_CASE("several effects round-trip through the scene file", "[integration][effects][composition]") {
    const fs::path dir = scratch("scene");
    const std::vector<world::EffectInstance> authored = severalEffects();

    app::Engine session(app::EngineMode::Offline);
    session.newComposition();
    REQUIRE(session.composition()->setHeroes(twoHeroes()).has_value());
    REQUIRE(session.setEffects(authored).has_value());
    REQUIRE(session.saveComposition(dir / "scene.json").has_value());

    // Straight through the Composition's own reader and writer...
    const json doc = readJson(dir / "scene.json");
    REQUIRE(doc.contains("effects"));
    CHECK(doc["effects"] == effectsJson(authored));
    assets::AssetRegistry registry;
    registry.setBaseDirectory(dir);
    auto comp = scene::Composition::fromJson(doc, registry);
    INFO((comp ? std::string() : comp.error().message));
    REQUIRE(comp.has_value());
    checkSameEffects(authored, (*comp)->effects());
    CHECK((*comp)->toJson()["effects"] == doc["effects"]);

    // ...and through an engine that loads the file, which is the path a person takes.
    app::Engine reopened(app::EngineMode::Offline);
    REQUIRE(reopened.loadComposition(dir / "scene.json").has_value());
    checkSameEffects(authored, reopened.effects());
    CHECK(reopened.params().find("fx/tornado/density") != nullptr);
    CHECK_FALSE(reopened.effects()[world::findEffect(reopened.effects(), "sage-ground-pulse")].enabled);
    fs::remove_all(dir);
}

TEST_CASE("several effects and their routes round-trip through the project document",
          "[integration][effects][project]") {
    const fs::path dir = scratch("project");
    const std::vector<world::EffectInstance> authored = severalEffects();

    app::Engine session(app::EngineMode::Offline);
    session.newComposition();
    REQUIRE(session.composition()->setHeroes(twoHeroes()).has_value());
    // The scene file first, EMPTY of effects, so the project has to carry every one of them itself
    // rather than by reference -- the case ADR-264 is about.
    REQUIRE(session.saveComposition(dir / "scene.json").has_value());
    REQUIRE(session.setEffects(authored).has_value());
    // A route per entity pulse, and a value moved on a slider, all addressed by id.
    REQUIRE(session.addDefaultEffectRoutes("rook-ground-pulse") > 0);
    params::ModRoute route;
    route.source = "audio.rms";
    route.target = "fx/sage-ground-pulse/speed";
    route.amount = 4.0f;
    session.modulator().addRoute(route);
    session.rebind();
    params::IParameter* auroraIntensity = session.params().find("fx/aurora/intensity");
    REQUIRE(auroraIntensity != nullptr);
    auroraIntensity->setBaseComponent(0, 3.25f);

    REQUIRE(session.saveProject(dir / "once.json").has_value());
    const json once = readJson(dir / "once.json");
    REQUIRE(once.contains("effects"));
    CHECK(once["effects"].size() == authored.size());

    app::Engine render(app::EngineMode::Offline);
    REQUIRE(render.loadProject(dir / "once.json").has_value());
    checkSameEffects(authored, render.effects());
    // The routes reached the loaded engine, aimed at the same ids, and bound to real parameters.
    std::set<std::string> targets;
    for (const params::ModRoute& r : render.modulator().routes()) {
        if (r.target.starts_with("fx/")) {
            targets.insert(r.target);
            CHECK(render.params().find(r.target) != nullptr);
        }
    }
    CHECK(targets.count("fx/sage-ground-pulse/speed") == 1);
    CHECK(targets.count("fx/rook-ground-pulse/intensity") == 1);
    // The slider value is the project's, not the effect's authored default.
    REQUIRE(render.params().find("fx/aurora/intensity") != nullptr);
    CHECK(render.params().find("fx/aurora/intensity")->baseComponent(0) == 3.25f);

    REQUIRE(render.saveProject(dir / "twice.json").has_value());
    const json twice = readJson(dir / "twice.json");
    CHECK(twice["effects"] == once["effects"]);
    CHECK(fxRoutes(twice) == fxRoutes(once));
    CHECK(fxParameters(twice) == fxParameters(once));
    // The control: the comparison had something in it.
    CHECK(fxRoutes(once).size() >= 2);
    CHECK(fxParameters(once).size() > 50);
    fs::remove_all(dir);
}

TEST_CASE("renaming an effect in the engine orphans no parameter, route or field",
          "[integration][effects][identity]") {
    app::Engine engine(app::EngineMode::Offline);
    engine.newComposition();
    std::vector<world::EffectInstance> effects;
    REQUIRE(world::addEffect(effects, EffectOwner::world(), EffectKind::Vortex).has_value());
    REQUIRE(world::addEffect(effects, EffectOwner::world(), EffectKind::Aurora).has_value());
    const std::string vortex = effects[0].id;
    REQUIRE(engine.setEffects(effects).has_value());
    REQUIRE(engine.addDefaultEffectRoutes("aurora") > 0);
    engine.update(frameAt(0.5));

    const auto fxPaths = [&] {
        std::vector<std::string> out = engine.effectParameters().registered;
        std::sort(out.begin(), out.end());
        return out;
    };
    const auto fieldNames = [&] {
        std::vector<std::string> out;
        for (const std::string_view n : engine.fieldBus().names()) {
            out.emplace_back(n);
        }
        std::sort(out.begin(), out.end());
        return out;
    };
    const std::vector<std::string> pathsBefore = fxPaths();
    const std::vector<std::string> fieldsBefore = fieldNames();
    // The control: the vortex publishes a field of its own, so "unchanged" below is about a name
    // that exists. It is named for the instance's id, the way its parameters are.
    REQUIRE(std::find(fieldsBefore.begin(), fieldsBefore.end(), world::fields::vortexFieldName(vortex)) !=
            fieldsBefore.end());

    REQUIRE(engine
                .editEffects([](std::vector<world::EffectInstance>& list) -> Result<void> {
                    for (world::EffectInstance& e : list) {
                        e.name = "Renamed " + e.name;
                    }
                    return {};
                })
                .has_value());
    engine.update(frameAt(0.5));

    CHECK(engine.effects()[0].name.starts_with("Renamed "));
    CHECK(engine.effects()[0].id == vortex);
    CHECK(fxPaths() == pathsBefore);
    CHECK(fieldNames() == fieldsBefore);
    for (const params::ModRoute& r : engine.modulator().routes()) {
        if (r.target.starts_with("fx/")) {
            INFO(r.target);
            CHECK(engine.params().find(r.target) != nullptr);
        }
    }
}

// ---- the regression ------------------------------------------------------------------------------

namespace {

fs::path filmProject() {
#ifdef AVGEN_SOURCE_DIR
    return fs::path(AVGEN_SOURCE_DIR) / "examples" / "world" / "glowmere-valley-2-multicam.json";
#else
    return {};
#endif
}

} // namespace

TEST_CASE("Glowmere multicam's effects survive save, load and save after a frame has run",
          "[integration][effects][project][glowmere][regression]") {
    if (filmProject().empty() || !fs::exists(filmProject())) {
        SKIP("the Glowmere Valley 2 multicam project is not present");
    }
    const fs::path dir = scratch("glowmere");
    const json onDisk = readJson(filmProject());
    REQUIRE(onDisk.contains("effects"));

    app::Engine engine(app::EngineMode::Offline);
    {
        auto loaded = engine.loadProject(filmProject());
        INFO((loaded ? std::string() : loaded.error().message));
        REQUIRE(loaded.has_value());
    }

    // What the migration made of it (tools/migrate_effects.py): the World carries the aurora and
    // the travel beam, and each of the sixteen heroes carries exactly one Ground Pulse of its own.
    const std::vector<world::EffectInstance>& effects = engine.effects();
    REQUIRE(effects.size() == 18);
    std::vector<EffectKind> worldKinds;
    for (const std::size_t i : world::effectsOf(effects, EffectOwner::world())) {
        worldKinds.push_back(effects[i].kind);
    }
    CHECK(worldKinds == std::vector<EffectKind>{EffectKind::Aurora, EffectKind::TravelBeam});
    const auto& heroes = engine.composition()->heroes();
    REQUIRE(heroes.size() == 16);
    for (const world::HeroPoint& hero : heroes) {
        INFO("hero: " << hero.name);
        const auto mine = world::effectsOf(effects, EffectOwner::entity(hero.name));
        REQUIRE(mine.size() == 1);
        CHECK(effects[mine.front()].kind == EffectKind::GroundPulse);
        CHECK(effects[mine.front()].wave.source.kind == world::SourceKind::Owner);
    }

    // **One frame.** Everything a save reads that a frame writes -- the live effect list with its
    // modulation applied, the status table, the captured parameters -- is now in play.
    engine.update(frameAt(0.0));
    engine.update(frameAt(12.5));

    REQUIRE(engine.saveProject(dir / "first.json").has_value());
    const json first = readJson(dir / "first.json");
    REQUIRE(first.contains("effects"));
    CHECK(first["effects"].size() == 18);

    app::Engine reloaded(app::EngineMode::Offline);
    {
        auto loaded = reloaded.loadProject(dir / "first.json");
        INFO((loaded ? std::string() : loaded.error().message));
        REQUIRE(loaded.has_value());
    }
    reloaded.update(frameAt(0.0));
    reloaded.update(frameAt(12.5));
    REQUIRE(reloaded.saveProject(dir / "second.json").has_value());
    const json second = readJson(dir / "second.json");

    // The two saves agree on every effect, and on everything that names one.
    REQUIRE(second.contains("effects"));
    CHECK(second["effects"] == first["effects"]);
    CHECK(fxRoutes(second) == fxRoutes(first));
    CHECK(fxParameters(second) == fxParameters(first));
    // The control: the migration duplicated routes per hero, so there ARE routes to compare.
    CHECK(fxRoutes(first).size() >= 16);
    CHECK(fxParameters(first).size() > 100);

    // And the file on disk is what a save writes: loading and saving the shipped project changes
    // nothing about its effects.
    CHECK(onDisk["effects"] == first["effects"]);
    fs::remove_all(dir);
}
