// Pass 1's engine gap 2 ("the node tint doesn't reach a figure whose position is keyed or journey-anchored"),
// rechecked for art pass 2. At this head the tint reaches a still, a walking and a moved figure (this test),
// and a walking journey-anchored figure with its tint bound to a palette role (checked by render, see
// PROGRESS-eng.md). The asset is local-only (CC0, not committed), so the case skips without it.

#include "app/engine.hpp"
#include "core/time.hpp"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>

using namespace avgen;

TEST_CASE("A figure's tint reaches it standing, walking and moved", "[liminal][tint][adr1044]") {
    const std::filesystem::path asset =
        std::filesystem::path(AVGEN_SOURCE_DIR) / "assets/quaternius/animations/UAL1_Standard.glb";
    if (!std::filesystem::exists(asset)) {
        SKIP("the figure asset is local-only: " << asset.string());
    }
    for (int variant = 0; variant < 3; ++variant) {
        INFO("variant " << variant << " (0 still, 1 walking, 2 walking and moved)");
        app::Engine engine(app::EngineMode::Offline);
        nlohmann::json node = {{"name", "figure"}, {"kind", "gltf"}, {"asset", asset.string()},
                               {"tint", {0.1, 0.9, 0.1}}};
        if (variant >= 1) {
            node["animation"] = {{"state", "Walk_Loop"}};
        }
        const nlohmann::json scene = {{"format", "avgen-scene"}, {"version", 1}, {"name", "t"}, {"nodes", {node}}};
        REQUIRE(engine.setCompositionJson(scene).has_value());
        REQUIRE(engine.params().find("nodes/figure/tint") != nullptr);
        engine.params().findAs<glm::vec3>("nodes/figure/tint")->setBase({0.9f, 0.1f, 0.1f});
        for (int k = 0; k < 5; ++k) {
            if (variant == 2) {
                engine.params().findAs<glm::vec3>("nodes/figure/position")->setBase({0.2f * static_cast<float>(k), 0, 0});
            }
            engine.update(FrameTime{k / 60.0, k != 0 ? 1.0 / 60.0 : 0.0, static_cast<std::uint64_t>(k)});
        }
        REQUIRE_FALSE(engine.scene().entities.empty());
        for (const auto& e : engine.scene().entities) {
            INFO(e.name);
            CHECK(e.material.baseColor.r > 4.0f * e.material.baseColor.g); // the red tint, not the asset's own
        }
    }
}
