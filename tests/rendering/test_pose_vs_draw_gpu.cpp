// CP-D r2 feet (diagnostic turned regression): the mesh the GPU draws for a posed character is where the CPU's skin of
// the same palette says it is -- the lowest drawn pixel row of the character against the CPU-skinned lowest vertex
// projected, through a Landing (the clip whose root drops 0.567 m). ADR-1991 ruled this out as the sinking's cause.

#include "assets/asset_registry.hpp"
#include "core/log.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/composition.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <cmath>
#include <filesystem>

using namespace avgen;

TEST_CASE("A posed character is drawn where the CPU skin of its palette puts it", "[gpu][feet][pose]") {
    const std::filesystem::path aliens = std::filesystem::path(AVGEN_SOURCE_DIR) / "assets" / "aliens";
    if (!std::filesystem::exists(aliens / "alien-scout.glb")) {
        SKIP("assets/aliens is not present");
    }
    log::init(log::Level::Warn);
    auto ctx = gpu::Context::create(gpu::ContextDesc{});
    if (!ctx) SKIP("no GPU");
    gpu::ShaderLibrary shaders(**ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(**ctx, shaders);
    REQUIRE(renderer.init().has_value());
    nlohmann::json json = nlohmann::json::parse(R"({
        "format": "avgen-scene", "version": 1, "name": "pose",
        "camera": {"position": [9, 1.2, 0], "target": [0, 0.8, 0], "fov": 45},
        "environment": {"background": [0, 0, 0], "skybox": false, "intensity": 1.0},
        "nodes": [ {"name": "alien", "kind": "gltf", "position": [0, 0, 0], "scale": [1.94, 1.94, 1.94],
                    "animation": {"state": "Idle", "blend": 0.0, "speed": 1.0}} ]})");
    json["nodes"][0]["asset"] = (aliens / "alien-scout.glb").string();
    assets::AssetRegistry registry{testsupport::processTempDir()};
    auto c = scene::Composition::fromJson(json, registry);
    REQUIRE(c.has_value());
    scene::Composition& comp = **c;
    params::ParameterSet params;
    params::Modulator modulator;
    comp.attach(params, modulator);
    constexpr std::uint32_t W = 320, H = 320;
    comp.setViewport(W, H);
    float worstRows = 0.0f;
    for (int f = 0; f <= 30; ++f) {
        FrameTime t{};
        t.renderTime = 10.0 + f / 30.0;
        t.deltaTime = f == 0 ? 0.0 : 1.0 / 30.0;
        if (f == 0) REQUIRE(comp.setNodeAnimation("alien", "Landing", t.renderTime, 0.0f, 1.0f));
        params.resetFinals();
        comp.update(t);
        const scene::Scene& s = comp.scene();
        auto img = renderer.renderToImage(s, t, W, H);
        REQUIRE(img.has_value());
        // the GPU: the lowest row with a non-background pixel
        int gpuRow = -1;
        for (int y = static_cast<int>(H) - 1; y >= 0 && gpuRow < 0; --y) {
            for (std::uint32_t x = 0; x < W; ++x) {
                const std::uint8_t* p = img->pixel(x, static_cast<std::uint32_t>(y));
                if (p[0] + p[1] + p[2] > 30) { gpuRow = y; break; }
            }
        }
        // the CPU: every vertex skinned with the same palette, projected
        const glm::mat4 vp = s.camera.projection(static_cast<float>(W) / static_cast<float>(H)) * s.camera.view();
        float cpuRow = -1.0f;
        float lowY = 1e9f;
        for (const scene::Entity& e : s.entities) {
            if (e.rig == scene::kInvalidRig || e.mesh >= s.meshes.size() || !s.meshes[e.mesh].skinned()) continue;
            const scene::MeshData& mesh = s.meshes[e.mesh];
            const scene::SkinnedRig& rig = s.rigs[e.rig];
            const glm::mat4 m = e.transform.matrix();
            for (std::size_t v = 0; v < mesh.vertices.size(); ++v) {
                glm::vec3 p(0.0f);
                for (int k = 0; k < 4; ++k) {
                    const float w = mesh.skin[v].weights[k];
                    if (w > 0.0f && mesh.skin[v].joints[k] < rig.palette.size())
                        p += glm::vec3(rig.palette[mesh.skin[v].joints[k]] * glm::vec4(mesh.vertices[v].position, 1.0f)) * w;
                }
                const glm::vec4 world = m * glm::vec4(p, 1.0f);
                lowY = std::min(lowY, world.y);
                const glm::vec4 clip = vp * world;
                const float row = (0.5f - 0.5f * clip.y / clip.w) * static_cast<float>(H);
                cpuRow = std::max(cpuRow, row);
            }
        }
        CAPTURE(t.renderTime, lowY, cpuRow, gpuRow);
        REQUIRE(cpuRow < static_cast<float>(H) - 2.0f); // the feet are in the frame
        REQUIRE(gpuRow >= 0);
        worstRows = std::max(worstRows, std::abs(static_cast<float>(gpuRow) - cpuRow));
    }
    CHECK(worstRows <= 3.0f);
}
