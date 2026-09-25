// ADR-834 (Phase D §36): optional cinematic signals -- isHero, inShot, distanceToCamera,
// visibility, screenImportance -- published per character on the bus, and the invariant §36 puts
// first: a character's world state does not depend on where the camera is.

#include "app/engine.hpp"
#include "entity/entity.hpp"
#include "scene/camera_rig.hpp"
#include "scene/composition.hpp"
#include "scene/detail_limits.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <map>
#include <string>

using namespace avgen;

namespace {
std::filesystem::path multicam() {
    return std::filesystem::path(AVGEN_SOURCE_DIR) / "examples" / "world" / "glowmere-valley-2-multicam.json";
}
bool present() {
    return std::filesystem::exists(std::filesystem::path(AVGEN_SOURCE_DIR) / "assets" / "aliens" / "alien-scout.glb");
}
void frame(app::Engine& engine, int i) {
    engine.update(FrameTime{i / 60.0, i == 0 ? 0.0 : 1.0 / 60.0, static_cast<std::uint64_t>(i)});
}
// The film with one camera, welded to Rook, holding the frame for the whole piece.
void followRook(app::Engine& engine) {
    scene::CameraDirection direction = engine.composition()->cameraDirection();
    scene::CameraRig rig;
    rig.name = "On Rook";
    rig.followNode = "rook";
    rig.followOffset = glm::vec3(5.0f, 2.2f, 5.0f);
    rig.aimNode = "rook";
    rig.aimOffset = glm::vec3(0.0f, 1.6f, 0.0f);
    rig.fovDegrees = 45.0f;
    const scene::CameraId id = direction.addCamera(rig);
    direction.shots.clear();
    direction.defaultCamera = id;
    REQUIRE(engine.setCameraDirection(direction).has_value());
}
} // namespace

TEST_CASE("the camera's subject is the hero, in the shot, and important", "[entity][phaseD][cinematic][adr834]") {
    if (!present()) {
        SKIP("Glowmere assets are not present");
    }
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(multicam()).has_value());
    engine.setDetailLimits(scene::DetailLimits::unlimited());
    followRook(engine);
    for (int i = 0; i <= 120; ++i) {
        frame(engine, i);
    }
    const scene::Composition& comp = *engine.composition();
    const auto rook = comp.cinematicSignals("rook");
    INFO("rook: hero " << rook.isHero << " inShot " << rook.inShot << " distance " << rook.distanceToCamera
                       << " visibility " << rook.visibility << " importance " << rook.screenImportance);
    CHECK(rook.isHero);
    CHECK(rook.inShot);
    CHECK(rook.distanceToCamera > 5.0f); // the 5 x 2.2 x 5 offset: ~7.4 m to his feet
    CHECK(rook.distanceToCamera < 10.0f);
    CHECK(rook.screenImportance > 0.2f); // a 3.2 m body at ~7 m through 45 degrees fills most of the frame
    CHECK(rook.visibility > 0.5f);
    for (const char* other : {"tide", "sage", "ember", "vane", "visitor"}) {
        INFO(other);
        CHECK_FALSE(comp.cinematicSignals(other).isHero);
    }
    // And on the bus, where a route can read them.
    const auto& bus = engine.signals();
    const auto hero = bus.find("character.rook.isHero");
    const auto importance = bus.find("character.rook.screenImportance");
    REQUIRE(hero.has_value());
    REQUIRE(importance.has_value());
    CHECK(bus.value(*hero) == 1.0f);
    CHECK(bus.value(*importance) == rook.screenImportance);
    // Something far behind the camera is not in the shot and has no importance.
    int outOfShot = 0;
    for (const auto& e : comp.entityWorld().entities()) {
        const auto s = comp.cinematicSignals(e->name());
        if (!s.inShot) {
            ++outOfShot;
            CHECK(s.screenImportance == 0.0f);
        }
    }
    CHECK(outOfShot > 0);
}

TEST_CASE("where the camera is does not change what the characters do", "[entity][phaseD][cinematic][adr834][benchmark]") {
    if (!present()) {
        SKIP("Glowmere assets are not present");
    }
    const auto run = [](bool follow) {
        app::Engine engine(app::EngineMode::Offline);
        REQUIRE(engine.loadProject(multicam()).has_value());
        engine.setDetailLimits(scene::DetailLimits::unlimited()); // no camera-distance LOD
        if (follow) {
            followRook(engine);
        }
        std::map<std::string, glm::vec3> at;
        for (int i = 0; i <= 60 * 20; ++i) {
            frame(engine, i);
        }
        for (const auto& e : engine.composition()->entityWorld().entities()) {
            at[e->name()] = e->state().position();
        }
        return at;
    };
    const auto film = run(false);
    const auto welded = run(true);
    REQUIRE(film.size() == welded.size());
    for (const auto& [name, p] : film) {
        INFO(name);
        CHECK(welded.at(name) == p); // exactly: the signals are published, never read by the step
    }
}
