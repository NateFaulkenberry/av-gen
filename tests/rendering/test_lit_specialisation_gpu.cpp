// ADR-1815: lit-shader specialisation. A specialised entity variant (features its frame does not use compiled out)
// draws what the general shader draws, and the frame's feature mask opens exactly the features its scene uses.
//
// The scene: lit spheres under a shadowed sun and a point light. Each section adds one feature -- a cel material, an
// anisotropic one, a rect area light -- and checks that the mask opened for it and that the frame still matches the
// general shader's (the renderer with compiled variants off).

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/lit_features.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/mesh_generators.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>

using namespace avgen;

namespace {

std::unique_ptr<gpu::Context> makeContext() {
    static bool logInit = false;
    if (!logInit) {
        log::init(log::Level::Warn);
        logInit = true;
    }
    auto ctx = gpu::Context::create(gpu::ContextDesc{});
    if (!ctx) {
        SKIP("no GPU adapter available: " << ctx.error().message);
    }
    return std::move(*ctx);
}

scene::Scene litScene() {
    scene::Scene s;
    s.environment.backgroundColor = {0.02f, 0.02f, 0.03f};
    s.environment.showSkybox = false;
    s.camera.position = {0.0f, 2.5f, 8.0f};
    s.camera.target = {0.0f, 0.5f, 0.0f};
    scene::PunctualLight sun;
    sun.direction = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.5f));
    sun.intensity = 3.0f;
    sun.castsShadow = true;
    s.addLight(sun);
    scene::PunctualLight point;
    point.type = scene::PunctualLight::Type::Point;
    point.position = {2.0f, 2.0f, 2.0f};
    point.color = {1.0f, 0.3f, 0.2f};
    point.intensity = 20.0f;
    point.range = 10.0f;
    s.addLight(point);
    const scene::MeshId ball = s.addMesh(scene::makeIcosphere(0.9f, 4));
    const scene::MeshId floor = s.addMesh(scene::makePlane(6.0f, 4));
    scene::Entity& ground = s.addEntity("floor", floor);
    ground.material.baseColor = {0.6f, 0.6f, 0.6f};
    for (int i = 0; i < 3; ++i) {
        scene::Entity& e = s.addEntity("ball" + std::to_string(i), ball);
        e.transform.position = glm::vec3(static_cast<float>(i) * 2.2f - 2.2f, 0.9f, 0.0f);
        e.material.baseColor = {0.8f, 0.5f + 0.2f * static_cast<float>(i), 0.3f};
        e.material.roughness = 0.25f + 0.25f * static_cast<float>(i);
        e.material.metallic = i == 2 ? 1.0f : 0.0f;
    }
    return s;
}

int worstDifference(const gpu::Image8& a, const gpu::Image8& b, int& differing) {
    int worst = 0;
    differing = 0;
    for (std::uint32_t y = 0; y < a.height; ++y) {
        for (std::uint32_t x = 0; x < a.width; ++x) {
            int d = 0;
            for (int c = 0; c < 3; ++c) {
                d = std::max(d, std::abs(static_cast<int>(a.pixel(x, y)[c]) - static_cast<int>(b.pixel(x, y)[c])));
            }
            worst = std::max(worst, d);
            differing += d > 0 ? 1 : 0;
        }
    }
    return worst;
}

// The general frame (variants off), the specialised frame (Blocking: the first frame waits for its variant) and the
// mask the specialised renderer derived.
struct Pair {
    gpu::Image8 general;
    gpu::Image8 specialised;
    std::uint32_t mask = 0;
};

Pair render(gpu::Context& ctx, gpu::ShaderLibrary& shaders, const scene::Scene& s) {
    FrameTime t{};
    t.renderTime = 1.0;
    rendering::SceneRenderer general(ctx, shaders);
    REQUIRE(general.init().has_value());
    auto a = general.renderToImage(s, t, 256, 256);
    REQUIRE(a.has_value());
    rendering::SceneRenderer specialised(ctx, shaders);
    REQUIRE(specialised.init().has_value());
    specialised.setCompiledMaterials(rendering::CompiledMaterialsMode::Blocking);
    auto b = specialised.renderToImage(s, t, 256, 256);
    REQUIRE(b.has_value());
    CHECK(specialised.compiledMaterialsPending() == 0);
    return {std::move(*a), std::move(*b), specialised.stats().litFeatures};
}

} // namespace

TEST_CASE("A specialised lit variant draws what the general shader draws, and opens what the scene uses",
          "[gpu][material][adr1815]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    scene::Scene s = litScene();
    std::uint32_t expected = 0;

    SECTION("plain PBR: every gate closed") {
    }
    SECTION("a cel material opens the toon gate") {
        s.entities[1].material.toon.bands = 3.0f;
        expected = rendering::kLitToon;
    }
    SECTION("an anisotropic material opens the optics gate") {
        s.entities[2].material.anisotropy.strength = 0.6f;
        expected = rendering::kLitOptics;
    }
    SECTION("a rect light opens the area-light gate") {
        scene::PunctualLight rect;
        rect.type = scene::PunctualLight::Type::Rect;
        rect.position = {-2.0f, 2.5f, 1.5f};
        rect.direction = glm::normalize(glm::vec3(0.5f, -1.0f, -0.3f));
        rect.width = 1.5f;
        rect.height = 0.5f;
        rect.intensity = 8.0f;
        s.addLight(rect);
        expected = rendering::kLitAreaLights;
    }

    const Pair p = render(*ctx, shaders, s);
    INFO("mask " << rendering::litFeatureNames(p.mask) << ", expected " << rendering::litFeatureNames(expected));
    CHECK(p.mask == expected);
    int differing = 0;
    const int worst = worstDifference(p.specialised, p.general, differing);
    INFO("worst " << worst << ", differing " << differing);
    CHECK(worst <= 1);
    CHECK(differing < 64);
    CHECK(ctx->errorCount() == 0);
}
