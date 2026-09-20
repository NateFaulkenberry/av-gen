// ADR-380: a particle system whose attractor IS the vortex.
//
// §9 asks for the vortex's relationship to the island to survive the island moving, and §11 makes
// particles entrained by the vortex the scene's storytelling mechanism. Neither survives a
// hand-typed attractor position, which stops describing the thing it was copied from the moment
// anything moves. This asserts the attractor is taken from the environment and not from the file --
// with a control that a system which has not asked keeps whatever it authored.

#include "assets/asset_registry.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

using Catch::Approx;
using json = nlohmann::json;
using namespace avgen;

namespace {

std::filesystem::path scratch() {
    auto dir = std::filesystem::temp_directory_path() / "avgen_vortex_attractor";
    std::filesystem::create_directories(dir);
    return dir;
}

json motes(const char* name, bool fromVortex) {
    json p;
    p["enabled"] = true;
    p["capacity"] = 256;
    // Authored, and deliberately nowhere near the vortex, so a binding is unmistakable.
    p["attractorPosition"] = json::array({7.0, 7.0, 7.0});
    p["attractorRadius"] = 3.0;
    json n;
    n["name"] = name;
    n["kind"] = "particles";
    n["particles"] = p;
    if (fromVortex) {
        n["vortexAttractor"] = true;
        n["vortexReach"] = 4.0;
    }
    return n;
}

std::filesystem::path writeScene(const char* file, bool withVortex) {
    json doc;
    doc["format"] = "avgen-scene";
    doc["version"] = 1;
    doc["name"] = "attractor";
    if (withVortex) {
        doc["environment"] = {{"vortex", {{"center", json::array({12.0, -400.0, -8.0})}, {"radius", 250.0}}}};
    }
    doc["nodes"] = json::array({motes("bound", true), motes("free", false)});
    const auto path = scratch() / file;
    std::ofstream(path) << doc.dump(1);
    return path;
}

const scene::ParticleSystem* find(const scene::Scene& s, const char* name) {
    for (const auto& p : s.particles) {
        if (p.name == name) {
            return &p;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("A particle system can take its attractor from the vortex", "[vortex][particles][composition]") {
    assets::AssetRegistry registry;
    registry.setBaseDirectory(scratch());
    params::ParameterSet params;
    params::Modulator modulator;

    auto loaded = scene::Composition::loadFile(writeScene("bound.scene.json", true), registry);
    REQUIRE(loaded.has_value());
    (*loaded)->attach(params, modulator);
    const scene::Scene& s = (*loaded)->scene();

    const scene::ParticleSystem* bound = find(s, "bound");
    const scene::ParticleSystem* free = find(s, "free");
    REQUIRE(bound != nullptr);
    REQUIRE(free != nullptr);

    CHECK(bound->attractorPosition.x == Approx(12.0f));
    CHECK(bound->attractorPosition.y == Approx(-400.0f));
    CHECK(bound->attractorPosition.z == Approx(-8.0f));
    CHECK(bound->attractorRadius == Approx(250.0f * 4.0f));

    // THE CONTROL: a system that did not ask keeps what it authored. Without it this would pass
    // just as well against a pass that rewrote every attractor in the scene.
    CHECK(free->attractorPosition.x == Approx(7.0f));
    CHECK(free->attractorRadius == Approx(3.0f));
}

TEST_CASE("Without a vortex the binding leaves the authored attractor alone",
          "[vortex][particles][composition]") {
    // The second control, and the one that matters for every scene in the repository that has no
    // vortex: asking to be bound to something that does not exist must not move anything, and must
    // not clamp an attractor to the origin either.
    assets::AssetRegistry registry;
    registry.setBaseDirectory(scratch());
    params::ParameterSet params;
    params::Modulator modulator;

    auto loaded = scene::Composition::loadFile(writeScene("free.scene.json", false), registry);
    REQUIRE(loaded.has_value());
    (*loaded)->attach(params, modulator);

    const scene::ParticleSystem* bound = find((*loaded)->scene(), "bound");
    REQUIRE(bound != nullptr);
    CHECK(bound->attractorPosition.x == Approx(7.0f));
    CHECK(bound->attractorRadius == Approx(3.0f));
}
