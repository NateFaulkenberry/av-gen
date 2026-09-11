// GPU tests: require a WebGPU adapter. Skipped (not failed) when none is available.
#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/render_target.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <memory>

using namespace avgen;

namespace {

std::unique_ptr<gpu::Context> makeContext() {
    static bool logInit = false;
    if (!logInit) {
        log::init(log::Level::Warn);
        logInit = true;
    }
    gpu::ContextDesc desc{};
    desc.metalLayer = nullptr;
    auto ctx = gpu::Context::create(desc);
    if (!ctx) {
        SKIP("no GPU adapter available: " << ctx.error().message);
    }
    return std::move(*ctx);
}

gpu::ShaderLibrary makeShaders(gpu::Context& ctx) {
    return gpu::ShaderLibrary(ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
}

// A unit cube built inline so this test does not depend on the scene generators.
scene::MeshData cubeMesh(float h) {
    scene::MeshData m;
    const glm::vec3 n[6] = {{0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
    for (int f = 0; f < 6; ++f) {
        const glm::vec3 normal = n[f];
        const glm::vec3 u = std::abs(normal.y) > 0.5f ? glm::vec3(1, 0, 0) : glm::cross(glm::vec3(0, 1, 0), normal);
        const glm::vec3 v = glm::cross(normal, u);
        const auto base = static_cast<std::uint32_t>(m.vertices.size());
        m.vertices.push_back({normal * h - u * h - v * h, normal, {0, 0}});
        m.vertices.push_back({normal * h + u * h - v * h, normal, {1, 0}});
        m.vertices.push_back({normal * h + u * h + v * h, normal, {1, 1}});
        m.vertices.push_back({normal * h - u * h + v * h, normal, {0, 1}});
        m.indices.insert(m.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    return m;
}

scene::Scene cubeScene() {
    scene::Scene s;
    const auto mesh = s.addMesh(cubeMesh(1.0f));
    auto& e = s.addEntity("cube", mesh);
    e.transform.position = {0.0f, 1.0f, 0.0f};
    e.material.baseColor = {0.9f, 0.3f, 0.2f};
    e.material.emissiveIntensity = 0.5f;
    s.camera.position = {0.0f, 1.5f, 5.0f};
    s.camera.target = {0.0f, 1.0f, 0.0f};
    scene::PunctualLight key;
    key.direction = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.6f));
    key.intensity = 3.0f;
    s.addLight(key);
    return s;
}

} // namespace

TEST_CASE("Headless context reports capabilities", "[gpu]") {
    auto ctx = makeContext();
    CHECK_FALSE(ctx->capabilities().adapterName.empty());
    CHECK_FALSE(ctx->capabilities().backendName.empty());
    CHECK(ctx->capabilities().limits.maxColorAttachments >= 4);
    CHECK_FALSE(ctx->hasSurface());
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("Shader compilation reports errors with diagnostics and succeeds on valid WGSL", "[gpu][shader]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    auto bad = shaders.compile("@vertex fn vs() -> @builtin(position) vec4<f32> { return oops; }", "bad.wgsl");
    REQUIRE_FALSE(bad.has_value());
    CHECK(bad.error().message.find("bad.wgsl") != std::string::npos);
    CHECK(bad.error().message.find("oops") != std::string::npos);
    ctx->clearErrors();

    auto good = shaders.compile("@vertex fn vs() -> @builtin(position) vec4<f32> { return vec4<f32>(0.0); }",
                                "good.wgsl");
    REQUIRE(good.has_value());
    CHECK(ctx->errorCount() == 0);

    auto missing = shaders.load("does_not_exist.wgsl");
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().message.find("not found") != std::string::npos);

    for (const char* name : {"common.wgsl", "pbr.wgsl", "grid.wgsl", "skybox.wgsl", "tonemap.wgsl"}) {
        auto src = shaders.loadSource(name);
        REQUIRE(src.has_value());
    }
    auto pbr = shaders.load("pbr.wgsl");
    REQUIRE(pbr.has_value());
}

TEST_CASE("Clearing a texture and reading it back yields the clear colour", "[gpu][readback]") {
    auto ctx = makeContext();
    gpu::RenderTargetDesc desc{};
    desc.width = 8;
    desc.height = 8;
    desc.colorFormat = wgpu::TextureFormat::RGBA8Unorm;
    desc.depthFormat = wgpu::TextureFormat::Undefined;
    desc.extraColorUsage = wgpu::TextureUsage::CopySrc;
    auto target = gpu::RenderTarget::create(*ctx, desc);
    REQUIRE(target.has_value());

    wgpu::RenderPassColorAttachment color{};
    color.view = target->colorView();
    color.loadOp = wgpu::LoadOp::Clear;
    color.storeOp = wgpu::StoreOp::Store;
    color.clearValue = {0.25, 0.5, 0.75, 1.0};
    wgpu::RenderPassDescriptor pass{};
    pass.colorAttachmentCount = 1;
    pass.colorAttachments = &color;
    wgpu::CommandEncoder encoder = ctx->device().CreateCommandEncoder();
    encoder.BeginRenderPass(&pass).End();
    wgpu::CommandBuffer commands = encoder.Finish();
    ctx->queue().Submit(1, &commands);

    auto image = gpu::readTexture8(*ctx, target->colorTexture(), 8, 8, false);
    REQUIRE(image.has_value());
    const auto* px = image->pixel(3, 5);
    CHECK(std::abs(int(px[0]) - 64) <= 1);
    CHECK(std::abs(int(px[1]) - 128) <= 1);
    CHECK(std::abs(int(px[2]) - 191) <= 1);
    CHECK(px[3] == 255);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("SceneRenderer renders a lit cube deterministically", "[gpu][renderer]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    auto scene = cubeScene();
    FrameTime time{};
    auto image = renderer.renderToImage(scene, time, 96, 64);
    REQUIRE(image.has_value());
    CHECK(ctx->errorCount() == 0);
    CHECK(image->width == 96);
    CHECK(image->height == 64);

    // The cube covers the centre; the corner shows the (very dark) background.
    const auto* centre = image->pixel(48, 32);
    const auto* corner = image->pixel(1, 1);
    const int centreSum = centre[0] + centre[1] + centre[2];
    const int cornerSum = corner[0] + corner[1] + corner[2];
    // Background 0.012 linear encodes to ~19/255 in sRGB, so the corner is dark but not black.
    CHECK(centreSum > cornerSum + 60);
    CHECK(cornerSum < 90);
    CHECK(centre[0] > centre[2]); // reddish material
    if (const char* dumpDir = std::getenv("AVGEN_DUMP_DIR")) {
        REQUIRE(gpu::writePpm(*image, std::filesystem::path(dumpDir) / "cube.ppm").has_value());
    }

    auto again = renderer.renderToImage(scene, time, 96, 64);
    REQUIRE(again.has_value());
    CHECK(gpu::hashImage(*image) == gpu::hashImage(*again));

    // Changing the scene changes the image.
    scene.entities[0].material.baseColor = {0.1f, 0.2f, 0.9f};
    auto changed = renderer.renderToImage(scene, time, 96, 64);
    REQUIRE(changed.has_value());
    CHECK(gpu::hashImage(*image) != gpu::hashImage(*changed));

    CHECK(renderer.stats().drawCalls == 2);
    CHECK(renderer.stats().triangles == 13);
    CHECK(renderer.stats().entities == 1);
    if (ctx->capabilities().timestampQuery) {
        CHECK(renderer.stats().gpuFrameMs >= 0.0);
    }
}

    TEST_CASE("camera motion never mutates a static entity transform", "[gpu][renderer][transform]") {
        auto ctx = makeContext();
        auto shaders = makeShaders(*ctx);
        rendering::SceneRenderer renderer(*ctx, shaders);
        REQUIRE(renderer.init().has_value());
        auto scene = cubeScene();
        const scene::Transform authored = scene.entities[0].transform;
        FrameTime time{};
        auto first = renderer.renderToImage(scene, time, 96, 64);
        REQUIRE(first.has_value());

        scene.camera.position = {4.0f, 2.0f, 3.0f};
        scene.camera.target = {0.0f, 1.0f, 0.0f};
        time.frameIndex = 1;
        time.renderTime = 1.0 / 30.0;
        auto movedCamera = renderer.renderToImage(scene, time, 96, 64);
        REQUIRE(movedCamera.has_value());
        CHECK(scene.entities[0].transform.position == authored.position);
        CHECK(scene.entities[0].transform.rotation == authored.rotation);
        CHECK(scene.entities[0].transform.scale == authored.scale);

        scene.camera.position = {0.0f, 1.5f, 5.0f};
        scene.camera.target = {0.0f, 1.0f, 0.0f};
        time.frameIndex = 2;
        time.renderTime = 2.0 / 30.0;
        auto returned = renderer.renderToImage(scene, time, 96, 64);
        REQUIRE(returned.has_value());
        CHECK(gpu::hashImage(*returned) == gpu::hashImage(*first));
        CHECK(ctx->errorCount() == 0);
    }

TEST_CASE("Stylized shading is distinct, deterministic and reversible", "[gpu][renderer][stylized]") {
    auto context = makeContext();
    auto shaders = makeShaders(*context);
    rendering::SceneRenderer renderer(*context, shaders);
    REQUIRE(renderer.init());
    auto scene = cubeScene();
    const auto original = renderer.renderToImage(scene, {}, 96, 64);
    REQUIRE(original);
    scene.environment.stylized = true;
    const auto styled = renderer.renderToImage(scene, {}, 96, 64);
    REQUIRE(styled);
    CHECK(gpu::hashImage(*original) != gpu::hashImage(*styled));
    const auto repeated = renderer.renderToImage(scene, {}, 96, 64);
    REQUIRE(repeated);
    CHECK(gpu::hashImage(*styled) == gpu::hashImage(*repeated));
    const auto* center = styled->pixel(48, 32);
    CHECK(center[0] > center[2]);
    CHECK(center[0] > 40);
    scene.environment.stylized = false;
    const auto restored = renderer.renderToImage(scene, {}, 96, 64);
    REQUIRE(restored);
    CHECK(gpu::hashImage(*original) == gpu::hashImage(*restored));
    CHECK(context->errorCount() == 0);
}

TEST_CASE("Stylized lit surfaces use factors while unlit images retain texture color",
          "[gpu][renderer][stylized]") {
    auto context = makeContext();
    auto shaders = makeShaders(*context);
    rendering::SceneRenderer renderer(*context, shaders);
    REQUIRE(renderer.init());
    auto scene = cubeScene();
    scene.environment.stylized = true;
    const auto plain = renderer.renderToImage(scene, {}, 96, 64);
    REQUIRE(plain);
    scene::TextureData green;
    green.name = "green";
    green.width = green.height = 1;
    green.data = {0, 255, 0, 255};
    scene.entities[0].material.baseColorTexture.texture = scene.addTexture(std::move(green));
    const auto textured = renderer.renderToImage(scene, {}, 96, 64);
    REQUIRE(textured);
    CHECK(gpu::hashImage(*plain) == gpu::hashImage(*textured));
    scene.entities[0].material.unlit = true;
    scene.entities[0].material.emissiveIntensity = 0.0f;
    const auto unlit = renderer.renderToImage(scene, {}, 96, 64);
    REQUIRE(unlit);
    CHECK(unlit->pixel(48, 32)[1] > unlit->pixel(48, 32)[0] + 30);
    CHECK(context->errorCount() == 0);
}

TEST_CASE("SceneRenderer survives resizes and invalid meshes", "[gpu][renderer]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    auto scene = cubeScene();
    scene.entities.push_back(scene::Entity{.name = "broken", .mesh = 42});
    scene::MeshData bad;
    bad.vertices.push_back({{0, 0, 0}, {0, 1, 0}, {0, 0}});
    bad.indices = {0, 1, 2}; // out-of-range indices
    const auto badMesh = scene.addMesh(std::move(bad));
    scene.addEntity("invalid-mesh", badMesh);

    FrameTime time{};
    for (auto [w, h] : {std::pair{32u, 32u}, std::pair{128u, 16u}, std::pair{7u, 9u}}) {
        auto image = renderer.renderToImage(scene, time, w, h);
        REQUIRE(image.has_value());
        CHECK(image->width == w);
        CHECK(image->height == h);
        CHECK(renderer.stats().entities == 1);
    }
    CHECK(ctx->errorCount() == 0);
    CHECK_FALSE(renderer.resize(0, 10).has_value());
}

namespace {
// Synthetic equirect: bright warm sky above the horizon, dark cool ground below.
scene::TextureData syntheticSky(std::uint32_t w, std::uint32_t h) {
    scene::TextureData tex;
    tex.name = "synthetic-sky";
    tex.width = w;
    tex.height = h;
    tex.format = scene::TextureFormat::Rgba32Float;
    tex.data.resize(static_cast<std::size_t>(w) * h * 16);
    auto* px = reinterpret_cast<float*>(tex.data.data());
    for (std::uint32_t y = 0; y < h; ++y) {
        const float t = static_cast<float>(y) / static_cast<float>(h - 1); // 0 top .. 1 bottom
        const bool sky = t < 0.5f;
        for (std::uint32_t x = 0; x < w; ++x) {
            float* p = px + (static_cast<std::size_t>(y) * w + x) * 4;
            p[0] = sky ? 4.0f : 0.05f;
            p[1] = sky ? 3.5f : 0.05f;
            p[2] = sky ? 2.5f : 0.08f;
            p[3] = 1.0f;
        }
    }
    return tex;
}
} // namespace

TEST_CASE("Image-based lighting lights a rough white sphere from above", "[gpu][ibl]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    scene::Scene s;
    const auto mesh = s.addMesh(cubeMesh(1.0f));
    auto& e = s.addEntity("cube", mesh);
    e.material.baseColor = {1.0f, 1.0f, 1.0f};
    e.material.emissiveIntensity = 0.0f;
    e.material.roughness = 0.9f;
    e.material.metallic = 0.0f;
    s.camera.position = {0.0f, 0.0f, 5.0f};
    s.camera.target = {0.0f, 0.0f, 0.0f};
    s.environment.backgroundColor = {0.0f, 0.0f, 0.0f};
    s.environment.showSkybox = false;
    // No punctual lights: everything comes from the environment. The procedural sky (ADR-036)
    // would otherwise stand in for the missing map, which is not what this test is measuring.
    s.environment.sky.enabled = false;
    FrameTime time{};

    auto noEnv = renderer.renderToImage(s, time, 64, 64);
    REQUIRE(noEnv.has_value());
    CHECK_FALSE(renderer.stats().ibl);

    s.environment.environmentMap = s.addTexture(syntheticSky(64, 32));
    auto withEnv = renderer.renderToImage(s, time, 64, 64);
    REQUIRE(withEnv.has_value());
    CHECK(ctx->errorCount() == 0);
    CHECK(renderer.stats().ibl);

    // The front face (+Z) sees half sky, half ground: brighter than the hemispheric fallback.
    const auto* frontEnv = withEnv->pixel(32, 32);
    const auto* frontNo = noEnv->pixel(32, 32);
    CHECK(int(frontEnv[0]) > int(frontNo[0]) + 40);
    // Warm sky tint: red channel above blue.
    CHECK(frontEnv[0] > frontEnv[2]);

    // Skybox on: a background pixel now shows the sky colour instead of black.
    s.environment.showSkybox = true;
    auto sky = renderer.renderToImage(s, time, 64, 64);
    REQUIRE(sky.has_value());
    const auto* corner = sky->pixel(2, 2);
    CHECK(int(corner[0]) + int(corner[1]) + int(corner[2]) > 300);
    CHECK(gpu::hashImage(*sky) == gpu::hashImage(*renderer.renderToImage(s, time, 64, 64)));
    if (const char* dumpDir = std::getenv("AVGEN_DUMP_DIR")) {
        REQUIRE(gpu::writePpm(*sky, std::filesystem::path(dumpDir) / "ibl.ppm").has_value());
    }
}
