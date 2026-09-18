// Do editor changes to the scene reach a render? (ADR-207's family.)
#include "app/engine.hpp"
#include "scene/composition.hpp"
#include <nlohmann/json.hpp>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
using namespace avgen;
namespace {
std::filesystem::path proj() {
    return std::filesystem::path(AVGEN_SOURCE_DIR) / "examples" / "world" / "glowmere-valley-2-multicam.json";
}
}
TEST_CASE("what survives a save: heroes, node removal, node addition", "[.probe][editstate]") {
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(proj()).has_value());
    scene::Composition* comp = engine.composition();
    REQUIRE(comp != nullptr);

    const std::size_t nodesBefore = comp->nodes().size();
    const std::size_t heroesBefore = comp->heroes().size();

    // Pick a real, removable node.
    std::string victim;
    for (const auto& n : comp->nodes()) {
        if (n && n->kind == scene::NodeKind::Procedural && n->name != "visitor") { victim = n->name; break; }
    }
    REQUIRE_FALSE(victim.empty());

    // Star something that is not already a hero -- the owner's actual gesture.
    std::vector<world::HeroPoint> heroes = comp->heroes();
    world::HeroPoint star;
    star.name = "probe-star";
    star.assetId = "probe";
    star.position = glm::vec3(5.0f, 0.0f, 5.0f);
    star.height = 3.0f;
    star.radius = 1.0f;
    star.importance = 0.99f;
    heroes.push_back(star);
    REQUIRE(comp->setHeroes(heroes).has_value());

    engine.removeNode(victim);
    const std::size_t nodesAfterRemove = engine.composition()->nodes().size();

    const auto out = std::filesystem::temp_directory_path() / "avgen_editstate_probe.json";
    REQUIRE(engine.saveProject(out).has_value());

    app::Engine reloaded(app::EngineMode::Offline);
    REQUIRE(reloaded.loadProject(out).has_value());
    const std::size_t nodesAfterReload = reloaded.composition()->nodes().size();
    const std::size_t heroesAfterReload = reloaded.composition()->heroes().size();

    bool starBack = false;
    for (const world::HeroPoint& h : reloaded.composition()->heroes()) {
        if (h.name == "probe-star") { starBack = true; break; }
    }
    bool victimBack = false;
    for (const auto& n : reloaded.composition()->nodes()) {
        if (n && n->name == victim) { victimBack = true; break; }
    }

    WARN("removed '" << victim << "': nodes " << nodesBefore << " -> " << nodesAfterRemove
         << " -> reloaded " << nodesAfterReload << "  | victim back after reload: "
         << (victimBack ? "YES (removal lost)" : "no (removal survived)")
         << "  | heroes " << heroesBefore << " -> reloaded " << heroesAfterReload
         << "  | new star survived: " << (starBack ? "YES" : "NO (starring lost)"));
    std::filesystem::remove(out);
}
