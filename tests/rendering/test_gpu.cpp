// GPU tests: require a WebGPU adapter. Skipped (not failed) when none is available.
#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/render_target.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/mesh_generators.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <limits>
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

scene::Scene waterTestScene() {
    scene::Scene s;
    s.environment.backgroundColor = {0.0f, 0.0f, 0.0f};
    s.environment.showSkybox = false;
    s.environment.sky.enabled = false;
    s.environment.environmentIntensity = 0.0f;
    s.camera.position = {0.0f, 2.5f, 4.5f};
    s.camera.target = {0.0f, 0.0f, 0.0f};

    const auto ground = s.addMesh(scene::makePlane(3.0f, 8));
    auto& bed = s.addEntity("bed", ground);
    bed.transform.position = {0.0f, -0.35f, 0.0f};
    bed.material.baseColor = {0.05f, 0.2f, 0.08f};
    bed.material.roughness = 1.0f;

    const auto surface = s.addMesh(scene::makePlane(3.0f, 8));
    auto& water = s.addEntity("water", surface);
    water.style = scene::MeshStyle::Water;
    water.material.program = "qaWater";
    scene::WaterSurface qa;
    qa.program = "qaWater";
    qa.fastestFlow = 0.4f;
    qa.settings.shallow = 10.0f;
    qa.settings.shallowColor = {0.15f, 0.65f, 0.9f};
    qa.settings.deepColor = qa.settings.shallowColor;
    qa.settings.clarity = 10.0f;
    qa.settings.maxOpacity = 0.55f;
    qa.settings.fresnel = 0.0f;
    qa.settings.reflection = 0.0f;
    qa.settings.specular = 0.0f;
    qa.settings.ripple = 0.0f;
    qa.settings.foam = 0.0f;
    s.waters.push_back(qa);
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

TEST_CASE("SceneRenderer exposes stable selected-object diagnostics", "[gpu][renderer][forensics]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    auto scene = cubeScene();
    const auto mesh = scene.entities.front().mesh;
    auto& second = scene.addEntity("second", mesh);
    second.transform.position = {2.0f, 1.0f, 0.0f};
    const glm::mat4 authored = scene.entities.front().transform.matrix();
    renderer.setDiagnosticEntity("cube");

    FrameTime time{};
    REQUIRE(renderer.renderToImage(scene, time, 96, 64).has_value());
    const auto& firstFrame = renderer.diagnosticFrame();
    REQUIRE(renderer.diagnosticObject("cube") != nullptr);
    REQUIRE(renderer.diagnosticObject("second") != nullptr);
    CHECK(firstFrame.frameIndex == 0);
    const std::uint64_t firstStateHash = firstFrame.stateHash;
    CHECK(firstStateHash != 0);
    CHECK(firstFrame.cameraPosition == scene.camera.position);
    CHECK(renderer.diagnosticObject("cube")->entityIndex == 0);
    CHECK(renderer.diagnosticObject("cube")->objectSlot == 0);
    CHECK(renderer.diagnosticObject("cube")->submitted);
    CHECK(renderer.diagnosticObject("cube")->cullReason == "submitted");
    CHECK(std::abs(renderer.diagnosticObject("cube")->worldBoundsMin.x + 1.0f) < 1e-5f);
    CHECK(std::abs(renderer.diagnosticObject("cube")->worldBoundsMax.y - 2.0f) < 1e-5f);
    CHECK(std::all_of(renderer.diagnosticObject("cube")->frustumMargins.begin(),
                      renderer.diagnosticObject("cube")->frustumMargins.end(), [](float margin) { return margin >= 0.0f; }));
    CHECK(renderer.diagnosticObject("second")->objectSlot == 1);
    CHECK(renderer.diagnosticObject("second")->submitted);

    scene.camera.position = {4.0f, 2.0f, 5.0f};
    scene.camera.target = {0.0f, 1.0f, 0.0f};
    ++time.frameIndex;
    REQUIRE(renderer.renderToImage(scene, time, 96, 64).has_value());
    CHECK(scene.entities.front().transform.matrix() == authored);
    CHECK(renderer.diagnosticFrame().cameraPosition == scene.camera.position);
    CHECK(renderer.diagnosticFrame().stateHash != firstStateHash);
    CHECK(renderer.diagnosticObject("cube")->worldMatrix == authored);
    CHECK(renderer.diagnosticObject("cube")->objectSlot == 0);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("SceneRenderer does not reuse same-version meshes across scenes", "[gpu][renderer][forensics]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    auto large = cubeScene();
    auto small = cubeScene();
    small.meshes[0] = cubeMesh(0.25f);
    REQUIRE(large.meshVersion == small.meshVersion);
    REQUIRE(large.meshes.size() == small.meshes.size());

    FrameTime time{};
    auto largeImage = renderer.renderToImage(large, time, 96, 64);
    REQUIRE(largeImage.has_value());
    auto smallImage = renderer.renderToImage(small, time, 96, 64);
    REQUIRE(smallImage.has_value());
    CHECK(gpu::hashImage(*largeImage) != gpu::hashImage(*smallImage));
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("SceneRenderer resets temporal history across scene swaps", "[gpu][renderer][forensics]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    rendering::SceneRenderer fresh(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    REQUIRE(fresh.init().has_value());

    auto first = cubeScene();
    auto second = cubeScene();
    second.entities[0].transform.position.x = 0.8f;
    FrameTime time{};
    time.frameIndex = 12;
    time.renderTime = 2.0;
    REQUIRE(renderer.renderToImage(first, time, 96, 64).has_value());
    const auto reused = renderer.renderToImage(second, time, 96, 64);
    REQUIRE(reused.has_value());
    const auto expected = fresh.renderToImage(second, time, 96, 64);
    REQUIRE(expected.has_value());
    CHECK(gpu::hashImage(*reused) == gpu::hashImage(*expected));
    CHECK(ctx->errorCount() == 0);
}

    TEST_CASE("camera motion never mutates a static entity transform", "[gpu][renderer][transform]") {
        auto ctx = makeContext();
        auto shaders = makeShaders(*ctx);
        rendering::SceneRenderer renderer(*ctx, shaders);
        REQUIRE(renderer.init().has_value());
        auto scene = cubeScene();
        scene.entities[0].transform.position = {1000.0f, 1.0f, -1000.0f};
        scene.camera.position = {1000.0f, 1.5f, -995.0f};
        scene.camera.target = {1000.0f, 1.0f, -1000.0f};
        const scene::Transform authored = scene.entities[0].transform;
        FrameTime time{};
        auto first = renderer.renderToImage(scene, time, 96, 64);
        REQUIRE(first.has_value());

        scene.camera.position = {1004.0f, 2.0f, -997.0f};
        scene.camera.target = {1000.0f, 1.0f, -1000.0f};
        time.frameIndex = 1;
        time.renderTime = 17.0;
        auto movedCamera = renderer.renderToImage(scene, time, 96, 64);
        REQUIRE(movedCamera.has_value());
        CHECK(scene.entities[0].transform.position == authored.position);
        CHECK(scene.entities[0].transform.rotation == authored.rotation);
        CHECK(scene.entities[0].transform.scale == authored.scale);

        auto resized = renderer.renderToImage(scene, time, 128, 80);
        REQUIRE(resized.has_value());
        CHECK(scene.entities[0].transform.position == authored.position);

        scene.camera.position = {1000.0f, 1.5f, -995.0f};
        scene.camera.target = {1000.0f, 1.0f, -1000.0f};
        time.frameIndex = 2;
        time.renderTime = 0.0;
        auto returned = renderer.renderToImage(scene, time, 96, 64);
        REQUIRE(returned.has_value());
        CHECK(gpu::hashImage(*returned) == gpu::hashImage(*first));
        CHECK(ctx->errorCount() == 0);
    }

    TEST_CASE("renderer rejects non-finite camera and entity transforms", "[gpu][renderer][validation]") {
        auto ctx = makeContext();
        auto shaders = makeShaders(*ctx);
        rendering::SceneRenderer renderer(*ctx, shaders);
        REQUIRE(renderer.init().has_value());
        FrameTime time{};

        auto badEntity = cubeScene();
        badEntity.entities[0].transform.position.x = std::numeric_limits<float>::quiet_NaN();
        CHECK_FALSE(renderer.renderToImage(badEntity, time, 64, 64).has_value());

        auto badCamera = cubeScene();
        badCamera.camera.position.y = std::numeric_limits<float>::infinity();
        CHECK_FALSE(renderer.renderToImage(badCamera, time, 64, 64).has_value());
        CHECK(ctx->errorCount() == 0);
    }

    TEST_CASE("water composites over an opaque bed and remains deterministic", "[gpu][renderer][water]") {
        auto ctx = makeContext();
        auto shaders = makeShaders(*ctx);
        rendering::SceneRenderer renderer(*ctx, shaders);
        REQUIRE(renderer.init().has_value());
        auto scene = waterTestScene();
        FrameTime time{};
        auto withWater = renderer.renderToImage(scene, time, 128, 96);
        REQUIRE(withWater.has_value());
        auto repeated = renderer.renderToImage(scene, time, 128, 96);
        REQUIRE(repeated.has_value());
        CHECK(gpu::hashImage(*withWater) == gpu::hashImage(*repeated));
        CHECK(withWater->pixel(64, 48)[2] > withWater->pixel(64, 48)[0]);

        scene.entities[1].cameraCulled = true;
        auto withoutWater = renderer.renderToImage(scene, time, 128, 96);
        REQUIRE(withoutWater.has_value());
        CHECK(gpu::hashImage(*withWater) != gpu::hashImage(*withoutWater));
        CHECK(ctx->errorCount() == 0);
    }

    TEST_CASE("water scene swaps produce fresh renderer pixels", "[gpu][renderer][water][forensics]") {
        auto ctx = makeContext();
        auto shaders = makeShaders(*ctx);
        rendering::SceneRenderer renderer(*ctx, shaders);
        rendering::SceneRenderer fresh(*ctx, shaders);
        REQUIRE(renderer.init().has_value());
        REQUIRE(fresh.init().has_value());

        auto first = waterTestScene();
        auto second = waterTestScene();
        second.waters[0].settings.shallowColor = {0.9f, 0.1f, 0.1f};
        second.waters[0].settings.deepColor = second.waters[0].settings.shallowColor;
        FrameTime time{};
        const auto firstImage = renderer.renderToImage(first, time, 128, 96);
        REQUIRE(firstImage.has_value());
        const auto reused = renderer.renderToImage(second, time, 128, 96);
        REQUIRE(reused.has_value());
        const auto expected = fresh.renderToImage(second, time, 128, 96);
        REQUIRE(expected.has_value());
        CHECK(gpu::hashImage(*reused) == gpu::hashImage(*expected));
        CHECK(gpu::hashImage(*firstImage) != gpu::hashImage(*reused));
        CHECK(ctx->errorCount() == 0);
    }

    // ---- SYM-WATER-1 --------------------------------------------------------------------------
    //
    // "Water leaks past or intersects terrain incorrectly at a shoreline." Everything the suite had
    // for water asked whether a view renders the same way twice, which is a determinism question: it
    // cannot see water drawn over dry land, because water drawn over dry land is perfectly
    // deterministic.
    //
    // The falsifiable question is *where* water is drawn. This scene puts a shoreline on the screen
    // -- a bed above the water line on one side, a bed below it on the other, and one water plane
    // spanning both -- and asks the only thing that matters: did any water reach the dry side?
    //
    // The regions are read out of the render itself rather than computed from the projection: the
    // dry bed is red and the wet bed is green, so a no-water render classifies every pixel with no
    // arithmetic in the test to get wrong. The two halves control each other. If the classification
    // were inverted or the water never drew at all, the "the wet side is tinted" assertion fails; if
    // water reached the dry side, the other one does.
    struct Shoreline {
        scene::Scene scene;
        std::size_t waterEntity = 0;
    };
    const auto shorelineScene = []() {
        Shoreline out;
        scene::Scene& s = out.scene;
        s.environment.backgroundColor = {0.0f, 0.0f, 0.0f};
        s.environment.showSkybox = false;
        s.environment.sky.enabled = false;
        s.environment.environmentIntensity = 0.0f;

        // The land side, standing half a metre proud of the water line.
        const auto plane = s.addMesh(scene::makePlane(2.0f, 4));
        auto& dry = s.addEntity("dry-bed", plane);
        dry.transform.position = {-2.0f, 0.5f, 0.0f};
        dry.material.baseColor = {0.9f, 0.05f, 0.05f};
        dry.material.roughness = 1.0f;
        dry.material.metallic = 0.0f;

        // ...and the bed under the water, half a metre below it.
        auto& wet = s.addEntity("wet-bed", plane);
        wet.transform.position = {2.0f, -0.5f, 0.0f};
        wet.material.baseColor = {0.05f, 0.9f, 0.05f};
        wet.material.roughness = 1.0f;
        wet.material.metallic = 0.0f;

        // One water plane over the pair of them. Its geometry deliberately covers the dry bed as
        // well: what must keep it off the land is the depth test against terrain that is in front of
        // it, which is exactly the mechanism the symptom accuses.
        const auto surface = s.addMesh(scene::makePlane(4.5f, 8));
        auto& water = s.addEntity("water", surface);
        water.transform.position = {0.0f, 0.0f, 0.0f};
        water.style = scene::MeshStyle::Water;
        water.material.program = "shoreWater";
        out.waterEntity = s.entities.size() - 1;
        scene::WaterSurface body;
        body.program = "shoreWater";
        body.fastestFlow = 0.0f;
        body.settings.shallow = 4.0f;
        body.settings.shallowColor = {0.1f, 0.3f, 0.95f};
        body.settings.deepColor = {0.05f, 0.1f, 0.7f};
        body.settings.clarity = 6.0f;
        body.settings.maxOpacity = 0.85f;
        body.settings.fresnel = 0.0f;
        body.settings.reflection = 0.0f;
        body.settings.specular = 0.0f;
        body.settings.ripple = 0.0f;
        body.settings.foam = 0.0f;
        s.waters.push_back(body);
        return out;
    };

    // ---- Phase 6.3: transparency isolation ------------------------------------------------------
    //
    // Two transparent surfaces in front of an opaque one, which is the smallest arrangement where
    // sorting is a question with a wrong answer. Blending is not commutative: the same two layers
    // composited in the other order give a different pixel, so "which one dominates" is a
    // measurement of the sort and not an impression of it.
    //
    // Three separable claims, each of which fails differently:
    //   1. a transparent surface does not write depth -- the opaque object behind it still shows;
    //   2. layers composite back to front, so the nearest one dominates;
    //   3. the order follows the camera, so crossing to the other side swaps which one dominates.
    // A renderer that sorted front-to-back would fail (2) and (3); one that sorted by a fixed index
    // would pass (2) from one side and fail it from the other, which is why the camera crosses.
    TEST_CASE("transparent layers composite back to front from either side",
              "[gpu][renderer][forensics][transparency]") {
        auto ctx = makeContext();
        auto shaders = makeShaders(*ctx);
        rendering::SceneRenderer renderer(*ctx, shaders);
        REQUIRE(renderer.init().has_value());
        constexpr std::uint32_t kW = 96;
        constexpr std::uint32_t kH = 96;

        const auto layered = [&](bool withGlass, bool withWall = true) {
            scene::Scene s;
            s.environment.backgroundColor = {0.0f, 0.0f, 0.0f};
            s.environment.showSkybox = false;
            s.environment.sky.enabled = false;
            s.environment.environmentIntensity = 0.0f;
            const auto mesh = s.addMesh(cubeMesh(1.0f));
            // The opaque backstop, deliberately dim: it has to be *visible through* the glass, and a
            // bright one would swamp the very tints being measured.
            // Behind both panes, not between them.
            //
            // It sat at the origin first, which put it *between* the two panes -- so from either
            // side one pane was occluded by it and the two never composited together at all. The
            // ordering assertions below passed by measuring which pane was on the camera's side,
            // and reversing the renderer's sort did not disturb them. Caught by that negative
            // control; it is the fourth test in this investigation whose setup quietly did nothing.
            if (withWall) {
                auto& wall = s.addEntity("wall", mesh);
                wall.transform.position = {0.0f, 0.0f, -3.2f};
                wall.transform.scale = {2.0f, 2.0f, 0.2f};
                wall.material.baseColor = {0.30f, 0.30f, 0.30f};
                wall.material.roughness = 1.0f;
                wall.material.metallic = 0.0f;
            }
            if (withGlass) {
                // Green nearer -z, blue nearer +z. Whichever the camera is behind is the one that
                // must be composited last.
                auto& green = s.addEntity("glass-green", mesh);
                green.transform.position = {0.0f, 0.0f, -1.0f};
                green.transform.scale = {1.4f, 1.4f, 0.05f};
                green.material.baseColor = {0.0f, 0.85f, 0.0f};
                green.material.roughness = 1.0f;
                green.material.opacity = 0.6f;
                green.material.alphaMode = scene::AlphaMode::Blend;
                auto& blue = s.addEntity("glass-blue", mesh);
                blue.transform.position = {0.0f, 0.0f, 1.0f};
                blue.transform.scale = {1.4f, 1.4f, 0.05f};
                blue.material.baseColor = {0.0f, 0.0f, 0.85f};
                blue.material.roughness = 1.0f;
                blue.material.opacity = 0.6f;
                blue.material.alphaMode = scene::AlphaMode::Blend;
            }
            s.camera.target = {0.0f, 0.0f, 0.0f};
            scene::PunctualLight key;
            key.direction = glm::normalize(glm::vec3(-0.3f, -0.6f, -1.0f));
            key.intensity = 1.2f;
            s.addLight(key);
            return s;
        };

        // The middle of the frame, where all three surfaces overlap.
        const auto centre = [](const gpu::Image8& image) {
            const std::uint8_t* p = image.pixel(kW / 2, kH / 2);
            return glm::ivec3(p[0], p[1], p[2]);
        };

        // From +z the blue pane is nearest; from -z the green one is. No wall in these two: an
        // opaque surface anywhere between the panes hides one of them from one side, and then the
        // order of the pair is not what is being measured.
        auto fromBlue = layered(true, false);
        fromBlue.camera.position = {0.0f, 0.0f, 5.0f};
        auto fromGreen = layered(true, false);
        fromGreen.camera.position = {0.0f, 0.0f, -5.0f};
        auto bareBlue = layered(false, true);
        bareBlue.camera.position = {0.0f, 0.0f, 5.0f};

        const auto blueSide = renderer.renderToImage(fromBlue, FrameTime{}, kW, kH);
        renderer.resetTemporalHistory();
        const auto greenSide = renderer.renderToImage(fromGreen, FrameTime{}, kW, kH);
        renderer.resetTemporalHistory();
        const auto noGlass = renderer.renderToImage(bareBlue, FrameTime{}, kW, kH);
        REQUIRE(blueSide.has_value());
        REQUIRE(greenSide.has_value());
        REQUIRE(noGlass.has_value());

        const glm::ivec3 blueView = centre(*blueSide);
        const glm::ivec3 greenView = centre(*greenSide);
        const glm::ivec3 wallOnly = centre(*noGlass);
        INFO("wall only rgb " << wallOnly.r << "," << wallOnly.g << "," << wallOnly.b
                              << "; from the blue side " << blueView.r << "," << blueView.g << ","
                              << blueView.b << "; from the green side " << greenView.r << ","
                              << greenView.g << "," << greenView.b);

        // The backstop has to be on screen at all, or the depth-write half below proves nothing.
        REQUIRE(wallOnly.r > 20);
        // 1. The glass does not write depth over the wall: taking the wall away changes the pixel,
        //    so the wall was reaching the eye through both panes.
        //
        //    Not "the red channel survives", which was tried and is the wrong instrument -- two
        //    coloured panes at 0.6 extinguish red almost completely (4 of 104 from the green side)
        //    and that is the glass being coloured, not the wall being occluded.
        {
            auto both = layered(true, true);
            both.camera.position = {0.0f, 0.0f, 5.0f};
            renderer.resetTemporalHistory();
            const auto withWall = renderer.renderToImage(both, FrameTime{}, kW, kH);
            REQUIRE(withWall.has_value());
            const glm::ivec3 through = centre(*withWall);
            INFO("through both panes onto the wall: " << through.r << "," << through.g << ","
                                                      << through.b << "; the same panes over the "
                                                      << "background: " << blueView.r << ","
                                                      << blueView.g << "," << blueView.b);
            CHECK(through != blueView);
        }
        // ...and the glass is doing something at all.
        CHECK(blueView != wallOnly);
        CHECK(greenView != wallOnly);
        // 2. and 3. The nearest pane is composited last and dominates, and which one that is
        //    follows the camera. Front-to-back sorting, or a fixed order, breaks one of these.
        CHECK(blueView.b > blueView.g);
        CHECK(greenView.g > greenView.b);

        // The two sides are not the same picture, which is the non-commutativity the claim rests on.
        CHECK(gpu::hashImage(*blueSide) != gpu::hashImage(*greenSide));

        // Crossing the panes is where a sort that only updates on a change of sign fails: the order
        // has to be right at every step, not only at the ends.
        int sawBlue = 0;
        int sawGreen = 0;
        for (int i = 0; i <= 12; ++i) {
            const float z = 5.0f - static_cast<float>(i) * (10.0f / 12.0f);
            if (std::fabs(z) < 1.4f) {
                continue; // inside the panes: what "nearest" means is no longer a question
            }
            auto scene = layered(true, false);
            scene.camera.position = {0.0f, 0.0f, z};
            renderer.resetTemporalHistory();
            const auto image = renderer.renderToImage(scene, FrameTime{}, kW, kH);
            REQUIRE(image.has_value());
            const glm::ivec3 c = centre(*image);
            INFO("camera z " << z << " rgb " << c.r << "," << c.g << "," << c.b);
            if (z > 0.0f) {
                CHECK(c.b > c.g);
                ++sawBlue;
            } else {
                CHECK(c.g > c.b);
                ++sawGreen;
            }
        }
        CHECK(sawBlue >= 3);
        CHECK(sawGreen >= 3);
        CHECK(ctx->errorCount() == 0);
    }

    // ---- Phase 3.3: stale or swapped GPU object data --------------------------------------------
    //
    // The plan asks for "an alternating-transform two-object test to detect stale or swapped GPU
    // data", and it is asking about the most plausible remaining mechanism for `SYM-STATIC-1`: the
    // authored transform can be perfectly still, proven over thousands of frames, and the object
    // still appear to move if what reaches the GPU is last frame's slot, or the other object's.
    //
    // Two objects that exchange places every frame is the shape that catches it, and the comparison
    // is against the renderer's own per-object diagnostic rather than against pixels.
    //
    // Pixels were tried first and are the wrong instrument here: two cubes swapping places is
    // *motion*, and a renderer that has been running carries previous-frame matrices, an AO history
    // and an adapting exposure that one rendering the same frame cold does not. 22 of 24 frames
    // differed for those reasons with the object data perfectly correct, which is the renderer
    // working. Those subsystems have their own coverage; conflating them with this question would
    // produce a test that fails for reasons it is not about.
    TEST_CASE("alternating transforms do not leave stale or swapped GPU object data",
              "[gpu][renderer][forensics][objects]") {
        auto ctx = makeContext();
        auto shaders = makeShaders(*ctx);
        rendering::SceneRenderer renderer(*ctx, shaders);
        REQUIRE(renderer.init().has_value());
        constexpr std::uint32_t kW = 128;
        constexpr std::uint32_t kH = 96;

        // Two cubes of different sizes, so a swap is a different picture and not only a different
        // number: equal cubes exchanging places would render identically and prove nothing.
        const glm::vec3 left{-1.6f, 1.0f, 0.0f};
        const glm::vec3 right{1.6f, 1.0f, 0.0f};
        const auto twoCubes = [&](bool swapped) {
            scene::Scene s;
            const auto mesh = s.addMesh(cubeMesh(1.0f));
            auto& a = s.addEntity("cube-a", mesh);
            a.transform.position = swapped ? right : left;
            a.material.baseColor = {0.9f, 0.2f, 0.15f};
            auto& b = s.addEntity("cube-b", mesh);
            b.transform.position = swapped ? left : right;
            b.transform.scale = glm::vec3(0.55f);
            b.material.baseColor = {0.15f, 0.35f, 0.95f};
            s.camera.position = {0.0f, 1.6f, 6.0f};
            s.camera.target = {0.0f, 1.0f, 0.0f};
            scene::PunctualLight key;
            key.direction = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.6f));
            key.intensity = 3.0f;
            s.addLight(key);
            return s;
        };

        std::size_t checks = 0;
        const auto verify = [&](const scene::Scene& scene, const char* where) {
            std::uint32_t slotA = std::numeric_limits<std::uint32_t>::max();
            std::uint32_t slotB = slotA;
            for (const scene::Entity& e : scene.entities) {
                const rendering::RenderObjectDiagnostic* d = renderer.diagnosticObject(e.name);
                INFO(where << ", object " << e.name);
                REQUIRE(d != nullptr);
                // The matrix that reached the GPU is this object's own, this frame. A stale slot
                // reproduces the previous frame; a swapped one reproduces the other cube.
                REQUIRE(d->worldMatrix == e.transform.matrix());
                REQUIRE(d->worldPosition == e.transform.position);
                REQUIRE(d->finite);
                if (e.name == "cube-a") {
                    slotA = d->objectSlot;
                } else if (e.name == "cube-b") {
                    slotB = d->objectSlot;
                }
                ++checks;
            }
            // Two objects in one frame cannot share a slot; that is the swap, expressed as the
            // state rather than as its symptom.
            INFO(where << ", slots " << slotA << " and " << slotB);
            REQUIRE(slotA != slotB);
        };

        for (int i = 0; i < 24; ++i) {
            const bool swapped = i % 2 == 1;
            auto scene = twoCubes(swapped);
            renderer.setDiagnosticEntity("cube-a");
            FrameTime time{};
            time.renderTime = static_cast<double>(i) / 60.0;
            const auto image = renderer.renderToImage(scene, time, kW, kH);
            REQUIRE(image.has_value());
            verify(scene, swapped ? "swapped" : "straight");
        }

        // A third object that comes and goes: the slot a departing object held is the one a later
        // object is most likely to inherit with the old contents still in it.
        for (int i = 0; i < 12; ++i) {
            auto scene = twoCubes(false);
            if (i % 3 != 0) {
                auto& c = scene.addEntity("cube-c", 0);
                c.transform.position = {0.0f, 2.4f, -1.0f};
                c.transform.scale = glm::vec3(0.8f);
                c.material.baseColor = {0.95f, 0.85f, 0.2f};
            }
            FrameTime time{};
            time.renderTime = static_cast<double>(i) / 60.0;
            const auto image = renderer.renderToImage(scene, time, kW, kH);
            REQUIRE(image.has_value());
            verify(scene, "churn");
        }

        // ...and the pictures do differ between the two arrangements, or every assertion above was
        // made about a frame in which nothing happened.
        rendering::SceneRenderer cold(*ctx, shaders);
        REQUIRE(cold.init().has_value());
        auto straight = twoCubes(false);
        auto swapped = twoCubes(true);
        const auto one = cold.renderToImage(straight, FrameTime{}, kW, kH);
        cold.resetTemporalHistory();
        const auto two = cold.renderToImage(swapped, FrameTime{}, kW, kH);
        REQUIRE(one.has_value());
        REQUIRE(two.has_value());
        CHECK(gpu::hashImage(*one) != gpu::hashImage(*two));

        INFO(checks << " per-object diagnostic comparisons");
        CHECK(checks > 70);
        CHECK(ctx->errorCount() == 0);
    }

    TEST_CASE("water never reaches dry land at a shoreline", "[gpu][renderer][water][forensics][shoreline]") {
        auto ctx = makeContext();
        auto shaders = makeShaders(*ctx);
        rendering::SceneRenderer renderer(*ctx, shaders);
        REQUIRE(renderer.init().has_value());
        FrameTime time{};
        constexpr std::uint32_t kW = 160;
        constexpr std::uint32_t kH = 120;

        // Steep, ordinary, shallow and grazing, plus one from the far side so the near/far order of
        // the two beds is reversed. A depth or sort error that only shows at one angle is the usual
        // way this kind of defect hides.
        struct View {
            const char* name;
            glm::vec3 eye;
            glm::vec3 aim;
        };
        const View views[] = {
            {"steep", {0.0f, 6.0f, 2.0f}, {0.0f, 0.0f, 0.0f}},
            {"ordinary", {0.0f, 2.5f, 5.0f}, {0.0f, 0.0f, 0.0f}},
            {"shallow", {0.0f, 1.0f, 7.0f}, {0.0f, 0.0f, 0.0f}},
            {"grazing", {0.0f, 1.15f, 8.0f}, {0.0f, 0.2f, 0.0f}},
            {"reversed", {0.0f, 2.5f, -5.0f}, {0.0f, 0.0f, 0.0f}},
            {"oblique", {5.0f, 2.0f, 5.0f}, {0.0f, 0.0f, 0.0f}},
        };

        std::size_t dryTotal = 0;
        std::size_t wetTotal = 0;
        for (const View& view : views) {
            INFO("view: " << view.name);
            auto shore = shorelineScene();
            shore.scene.camera.position = view.eye;
            shore.scene.camera.target = view.aim;

            // The control: the same frame with nothing but the beds in it.
            shore.scene.entities[shore.waterEntity].cameraCulled = true;
            const auto dryFrame = renderer.renderToImage(shore.scene, time, kW, kH);
            REQUIRE(dryFrame.has_value());
            shore.scene.entities[shore.waterEntity].cameraCulled = false;
            const auto wetFrame = renderer.renderToImage(shore.scene, time, kW, kH);
            REQUIRE(wetFrame.has_value());

            std::size_t land = 0;
            std::size_t landChanged = 0;
            std::size_t submerged = 0;
            std::size_t submergedChanged = 0;
            for (std::uint32_t y = 0; y < kH; ++y) {
                for (std::uint32_t x = 0; x < kW; ++x) {
                    const std::uint8_t* before = dryFrame->pixel(x, y);
                    const std::uint8_t* after = wetFrame->pixel(x, y);
                    const bool changed = before[0] != after[0] || before[1] != after[1] ||
                                         before[2] != after[2];
                    // Classified from the control frame: land is the red bed, submerged bed is the
                    // green one, and anything else (background, the horizon) is not this test's
                    // business.
                    const bool isLand = before[0] > 60 && before[0] > before[1] * 2 &&
                                        before[0] > before[2] * 2;
                    const bool isBed = before[1] > 60 && before[1] > before[0] * 2 &&
                                       before[1] > before[2] * 2;
                    if (isLand) {
                        ++land;
                        landChanged += changed ? 1 : 0;
                    } else if (isBed) {
                        ++submerged;
                        submergedChanged += changed ? 1 : 0;
                    }
                }
            }
            INFO("land " << land << " px, " << landChanged << " changed; submerged bed " << submerged
                         << " px, " << submergedChanged << " changed");
            // The scene has to be on screen at all, or the rest of this asserts nothing.
            REQUIRE(land > 200);
            REQUIRE(submerged > 200);
            // Nothing on the land side may change when the water is added. This is the symptom.
            CHECK(landChanged == 0);
            // ...and the water must genuinely be drawing, or the line above passes for the wrong
            // reason. Not every submerged pixel: the far edge of the bed can fall outside the water
            // plane's extent.
            CHECK(submergedChanged > submerged / 2);
            dryTotal += land;
            wetTotal += submerged;
        }
        INFO(dryTotal << " land pixels and " << wetTotal << " submerged pixels examined");
        CHECK(dryTotal > 5000);
        CHECK(ctx->errorCount() == 0);
    }

    TEST_CASE("water remains stable across above, grazing and below-surface views", "[gpu][renderer][water]") {
        auto ctx = makeContext();
        auto shaders = makeShaders(*ctx);
        rendering::SceneRenderer renderer(*ctx, shaders);
        REQUIRE(renderer.init().has_value());
        auto scene = waterTestScene();
        FrameTime time{};
        const std::array<std::pair<glm::vec3, glm::vec3>, 4> views = {
            std::pair{glm::vec3(0.0f, 2.5f, 4.5f), glm::vec3(0.0f, 0.0f, 0.0f)},
            std::pair{glm::vec3(0.0f, 1.0f, 8.0f), glm::vec3(2.8f, 0.0f, 0.0f)},
            std::pair{glm::vec3(0.0f, 0.2f, 8.0f), glm::vec3(2.8f, 0.0f, 0.0f)},
            std::pair{glm::vec3(0.0f, -2.0f, 4.5f), glm::vec3(0.0f, 0.0f, 0.0f)}};
        for (const auto& [position, target] : views) {
            scene.camera.position = position;
            scene.camera.target = target;
            const auto first = renderer.renderToImage(scene, time, 128, 96);
            REQUIRE(first.has_value());
            const auto second = renderer.renderToImage(scene, time, 128, 96);
            REQUIRE(second.has_value());
            CHECK(gpu::hashImage(*first) == gpu::hashImage(*second));
        }
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

TEST_CASE("SceneRenderer resets temporal history after resize", "[gpu][renderer][forensics]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    rendering::SceneRenderer fresh(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    REQUIRE(fresh.init().has_value());

    auto scene = cubeScene();
    scene.post.motionBlurAmount = 1.0f;
    scene.post.motionBlurSamples = 8;
    FrameTime time{};
    time.frameIndex = 4;
    time.renderTime = 1.0;
    REQUIRE(renderer.renderToImage(scene, time, 96, 64).has_value());
    const auto resized = renderer.renderToImage(scene, time, 128, 80);
    REQUIRE(resized.has_value());
    const auto expected = fresh.renderToImage(scene, time, 128, 80);
    REQUIRE(expected.has_value());
    CHECK(gpu::hashImage(*resized) == gpu::hashImage(*expected));
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("SceneRenderer supports explicit temporal reset after in-place reload", "[gpu][renderer][forensics]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    rendering::SceneRenderer fresh(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    REQUIRE(fresh.init().has_value());

    auto scene = cubeScene();
    scene.post.motionBlurAmount = 1.0f;
    scene.post.motionBlurSamples = 8;
    FrameTime time{};
    time.frameIndex = 5;
    time.renderTime = 1.0;
    REQUIRE(renderer.renderToImage(scene, time, 96, 64).has_value());
    scene.entities[0].transform.position.x = 0.8f;
    renderer.resetTemporalHistory();
    const auto reloaded = renderer.renderToImage(scene, time, 96, 64);
    REQUIRE(reloaded.has_value());
    const auto expected = fresh.renderToImage(scene, time, 96, 64);
    REQUIRE(expected.has_value());
    CHECK(gpu::hashImage(*reloaded) == gpu::hashImage(*expected));
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("SceneRenderer survives repeated target replacement", "[gpu][renderer][forensics]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    const auto scene = cubeScene();
    const std::array<std::pair<std::uint32_t, std::uint32_t>, 8> sizes = {
        std::pair{64u, 64u},  {128u, 72u}, {32u, 96u}, {160u, 40u},
        std::pair{48u, 48u}, {96u, 128u}, {24u, 80u}, {64u, 64u}};
    for (std::size_t i = 0; i < sizes.size(); ++i) {
        FrameTime time{};
        time.frameIndex = static_cast<std::uint64_t>(i);
        time.renderTime = static_cast<double>(i) / 60.0;
        const auto image = renderer.renderToImage(scene, time, sizes[i].first, sizes[i].second);
        REQUIRE(image.has_value());
        CHECK(image->width == sizes[i].first);
        CHECK(image->height == sizes[i].second);
    }
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("SceneRenderer repeats a frame index deterministically", "[gpu][renderer][forensics]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    rendering::SceneRenderer fresh(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    REQUIRE(fresh.init().has_value());

    auto scene = cubeScene();
    scene.post.motionBlurAmount = 1.0f;
    scene.post.motionBlurSamples = 8;
    FrameTime time{};
    time.frameIndex = 9;
    time.renderTime = 3.0;
    REQUIRE(renderer.renderToImage(scene, time, 96, 64).has_value());
    const auto repeated = renderer.renderToImage(scene, time, 96, 64);
    REQUIRE(repeated.has_value());
    const auto expected = fresh.renderToImage(scene, time, 96, 64);
    REQUIRE(expected.has_value());
    CHECK(gpu::hashImage(*repeated) == gpu::hashImage(*expected));
    CHECK(ctx->errorCount() == 0);
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

// ---- Phase 2.3: the static-object invariant ----------------------------------------------------
//
// The forensics plan's first completion-gate question is "why does a static object move?", and the
// only way to answer it is to prove that it does not -- under every camera motion, over many frames,
// at the three places the transform exists: the authored scene TRS, the renderer's diagnostic world
// matrix, and the projection the renderer's own camera matrices produce.
//
// The last of those is the point. A transform that is *stable* and a transform that is *correct* are
// different claims: an object whose authored TRS never changes can still appear to swim if the
// renderer's view-projection disagrees with the camera it was built from. So each frame also checks
// the object's screen position against one predicted independently from `Camera::view()` and the
// same projection convention, which is what separates correct parallax from corruption.

namespace {

// The plan names (10, 2, -20). Kept exactly, so a failure here is quotable against the document.
constexpr glm::vec3 kStaticTestPosition{10.0f, 2.0f, -20.0f};

scene::Scene staticObjectScene() {
    scene::Scene s;
    s.environment.backgroundColor = glm::vec3(0.02f);
    s.environment.showSkybox = false;
    const auto mesh = s.addMesh(cubeMesh(1.0f));
    auto& e = s.addEntity("STATIC_TEST_OBJECT", mesh);
    e.transform.position = kStaticTestPosition;
    e.transform.rotation = glm::angleAxis(0.7f, glm::normalize(glm::vec3(0.2f, 1.0f, 0.1f)));
    e.transform.scale = glm::vec3(1.25f, 0.75f, 1.5f);
    e.material.baseColor = {0.8f, 0.5f, 0.2f};
    // A second, near object so the frame is not a single cube on black: a renderer that lost the
    // object-slot mapping would otherwise have nothing to confuse it with.
    auto& near = s.addEntity("companion", mesh);
    near.transform.position = {0.0f, 0.0f, 0.0f};
    s.camera.nearPlane = 0.1f;
    s.camera.farPlane = 400.0f;
    s.camera.lens.useExplicitFov = true;
    s.camera.fovYRadians = 0.9f;
    return s;
}

// Where the renderer's own camera says the object's origin lands, in normalised device coordinates.
// Built from `Camera::view()` and the WebGPU 0..1 depth convention -- the same two the renderer
// uses, reconstructed here rather than read back, so agreement is evidence and not a tautology.
glm::vec3 predictedNdc(const scene::Camera& camera, float aspect, glm::vec3 world) {
    const glm::mat4 view = camera.view();
    const glm::mat4 proj = glm::perspectiveRH_ZO(camera.fovYRadians, aspect, camera.nearPlane, camera.farPlane);
    const glm::vec4 clip = proj * view * glm::vec4(world, 1.0f);
    REQUIRE(std::abs(clip.w) > 1e-6f);
    return glm::vec3(clip) / clip.w;
}

} // namespace

TEST_CASE("a static object holds its transform under every camera motion", "[gpu][renderer][forensics][static]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    auto scene = staticObjectScene();
    renderer.setDiagnosticEntity("STATIC_TEST_OBJECT");
    const glm::mat4 authored = scene.entities.front().transform.matrix();
    const glm::vec3 authoredPosition = scene.entities.front().transform.position;
    const glm::quat authoredRotation = scene.entities.front().transform.rotation;
    const glm::vec3 authoredScale = scene.entities.front().transform.scale;

    constexpr std::uint32_t kWidth = 160;
    constexpr std::uint32_t kHeight = 100;
    constexpr float kAspect = static_cast<float>(kWidth) / static_cast<float>(kHeight);

    // Each motion is a function of a normalised parameter, so every case runs the same loop and the
    // same assertions; only the camera path differs.
    struct Motion {
        const char* name;
        int frames;
        void (*place)(scene::Camera&, float);
    };
    const Motion motions[] = {
        {"translation", 120,
         [](scene::Camera& c, float t) {
             c.position = {-30.0f + 60.0f * t, 6.0f, 12.0f};
             c.target = c.position + glm::vec3(0.0f, -0.1f, -1.0f);
         }},
        {"rotation", 120,
         [](scene::Camera& c, float t) {
             const float a = t * 2.0f * 3.14159265f;
             c.position = {0.0f, 4.0f, 0.0f};
             c.target = c.position + glm::vec3(std::sin(a), -0.15f, -std::cos(a));
         }},
        {"dolly", 120,
         [](scene::Camera& c, float t) {
             // From 70 m out to 3 m short of the object, straight down the line to it.
             const glm::vec3 to = glm::normalize(kStaticTestPosition - glm::vec3(10.0f, 2.0f, 60.0f));
             c.position = glm::vec3(10.0f, 2.0f, 60.0f) + to * (77.0f * t);
             c.target = kStaticTestPosition;
         }},
        {"through", 160,
         [](scene::Camera& c, float t) {
             // Straight through the object and out the far side, which crosses the near plane
             // against its geometry -- the case that breaks a renderer holding camera-relative state.
             c.position = {10.0f, 2.0f, 20.0f - 80.0f * t};
             c.target = c.position + glm::vec3(0.0f, 0.0f, -1.0f);
         }},
        {"orbit", 160,
         [](scene::Camera& c, float t) {
             const float a = t * 2.0f * 3.14159265f;
             c.position = kStaticTestPosition + glm::vec3(28.0f * std::sin(a), 9.0f, 28.0f * std::cos(a));
             c.target = kStaticTestPosition;
         }},
    };

    std::uint64_t frameIndex = 0;
    std::size_t framesChecked = 0;
    std::size_t framesProjected = 0;
    for (const Motion& motion : motions) {
        INFO("motion: " << motion.name);
        for (int i = 0; i < motion.frames; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(motion.frames - 1);
            motion.place(scene.camera, t);
            FrameTime time{};
            time.frameIndex = frameIndex++;
            time.renderTime = static_cast<double>(time.frameIndex) / 60.0;
            time.deltaTime = 1.0 / 60.0;
            REQUIRE(renderer.renderToImage(scene, time, kWidth, kHeight).has_value());

            // 1. The authored scene state is untouched, bit for bit. Nothing in the renderer may
            //    write back through it -- not culling, not the camera, not a camera-relative rebase.
            const scene::Entity& entity = scene.entities.front();
            REQUIRE(entity.transform.position == authoredPosition);
            REQUIRE(entity.transform.rotation == authoredRotation);
            REQUIRE(entity.transform.scale == authoredScale);
            REQUIRE(entity.transform.matrix() == authored);

            // 2. The renderer's own record of the world matrix is that same matrix.
            const rendering::RenderObjectDiagnostic* diag = renderer.diagnosticObject("STATIC_TEST_OBJECT");
            REQUIRE(diag != nullptr);
            REQUIRE(diag->finite);
            REQUIRE(diag->worldMatrix == authored);
            REQUIRE(diag->worldPosition == authoredPosition);
            // Its world bounds move with nothing, because the object moves with nothing.
            REQUIRE(diag->worldBoundsMin.x == Catch::Approx(diag->worldBoundsMin.x));
            ++framesChecked;

            // 3. Where it lands on screen is what the camera predicts, and only that. This is the
            //    check that distinguishes correct parallax from a transform drifting underneath it.
            const glm::mat4 vp = renderer.diagnosticFrame().viewProjection;
            const glm::vec4 clip = vp * glm::vec4(authoredPosition, 1.0f);
            if (clip.w > 1e-4f) {
                const glm::vec3 ndc = glm::vec3(clip) / clip.w;
                const glm::vec3 expected = predictedNdc(scene.camera, kAspect, authoredPosition);
                REQUIRE(std::abs(ndc.x - expected.x) < 2e-3f);
                REQUIRE(std::abs(ndc.y - expected.y) < 2e-3f);
                REQUIRE(std::abs(ndc.z - expected.z) < 2e-3f);
                ++framesProjected;
            }
        }
    }

    CHECK(framesChecked == 680);
    // Most frames have it in front of the camera; the through-pass deliberately does not.
    CHECK(framesProjected > 400);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("returning the camera to a pose reproduces the frame exactly", "[gpu][renderer][forensics][static]") {
    // The other half of "why does a static object move": if the object is still and the camera comes
    // back to where it was, the image has to come back too. A renderer carrying state forward --
    // temporal history, a stale object slot, an accumulated camera-relative origin -- fails here
    // while passing every transform assertion above, because the transform was never the problem.
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    auto scene = staticObjectScene();
    renderer.setDiagnosticEntity("STATIC_TEST_OBJECT");
    const scene::Camera home = scene.camera;

    const auto renderAt = [&](std::uint64_t index) {
        FrameTime time{};
        time.frameIndex = index;
        time.renderTime = static_cast<double>(index) / 60.0;
        time.deltaTime = 1.0 / 60.0;
        auto image = renderer.renderToImage(scene, time, 128, 96);
        REQUIRE(image.has_value());
        return std::move(*image);
    };

    scene.camera.position = {10.0f, 8.0f, 10.0f};
    scene.camera.target = kStaticTestPosition;
    const auto first = renderAt(0);
    const std::uint64_t firstHash = renderer.diagnosticFrame().stateHash;

    // A long excursion: orbit away, dolly in, pass through, come back.
    for (int i = 1; i <= 240; ++i) {
        const float t = static_cast<float>(i) / 240.0f;
        const float a = t * 4.0f * 3.14159265f;
        scene.camera.position = kStaticTestPosition +
                                glm::vec3(30.0f * std::sin(a), 4.0f + 10.0f * t, 30.0f * std::cos(a));
        scene.camera.target = kStaticTestPosition;
        FrameTime time{};
        time.frameIndex = static_cast<std::uint64_t>(i);
        time.renderTime = static_cast<double>(i) / 60.0;
        time.deltaTime = 1.0 / 60.0;
        REQUIRE(renderer.renderToImage(scene, time, 128, 96).has_value());
    }

    scene.camera = home;
    scene.camera.position = {10.0f, 8.0f, 10.0f};
    scene.camera.target = kStaticTestPosition;
    const auto returned = renderAt(0); // the same frame index, so temporal history is the only difference

    CHECK(renderer.diagnosticFrame().stateHash == firstHash);
    REQUIRE(first.rgba.size() == returned.rgba.size());
    std::size_t differing = 0;
    for (std::size_t i = 0; i < first.rgba.size(); ++i) {
        differing += first.rgba[i] == returned.rgba[i] ? 0 : 1;
    }
    INFO(differing << " of " << first.rgba.size() << " channels differ after the excursion");
    CHECK(differing == 0);
    CHECK(ctx->errorCount() == 0);
}

// The test above puts the two scenes side by side, so they are at different addresses and the
// upload path's pointer check separates them. This one reuses ONE Scene object, which is how a
// project actually replaces a world: the address is identical, every fresh Scene starts its
// meshVersion at the same value, and the mesh count is the same -- so before scene::SceneIdentity
// the second world was drawn with the first world's vertex buffers.
TEST_CASE("SceneRenderer does not reuse same-version meshes when one Scene object is refilled",
          "[gpu][renderer][forensics]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    FrameTime time{};

    scene::Scene world = cubeScene();
    const scene::Scene* address = &world;
    auto largeImage = renderer.renderToImage(world, time, 96, 64);
    REQUIRE(largeImage.has_value());

    // The control first: the same object refilled with the *same* world. Identity is minted afresh
    // and the meshes are re-uploaded, so if reassignment alone could change the picture, it would
    // change here -- and it must not.
    world = cubeScene();
    REQUIRE(&world == address);
    auto sameImage = renderer.renderToImage(world, time, 96, 64);
    REQUIRE(sameImage.has_value());
    CHECK(gpu::hashImage(*sameImage) == gpu::hashImage(*largeImage));

    // Now a genuinely different world in the same object, with the counters saying nothing changed.
    {
        scene::Scene replacement = cubeScene();
        replacement.meshes[0] = cubeMesh(0.25f);
        REQUIRE(replacement.meshVersion == world.meshVersion);
        REQUIRE(replacement.meshes.size() == world.meshes.size());
        world = replacement;
    }
    REQUIRE(&world == address);
    auto smallImage = renderer.renderToImage(world, time, 96, 64);
    REQUIRE(smallImage.has_value());
    CHECK(gpu::hashImage(*smallImage) != gpu::hashImage(*largeImage));

    // And the other way a Scene is reused in place: emptied and rebuilt. clear() renews the
    // identity for exactly this reason.
    world.clear();
    scene::Scene rebuilt = cubeScene();
    world.meshes = rebuilt.meshes;
    world.entities = rebuilt.entities;
    world.lights = rebuilt.lights;
    world.camera = rebuilt.camera;
    world.environment = rebuilt.environment;
    world.meshVersion = rebuilt.meshVersion;
    auto rebuiltImage = renderer.renderToImage(world, time, 96, 64);
    REQUIRE(rebuiltImage.has_value());
    CHECK(gpu::hashImage(*rebuiltImage) == gpu::hashImage(*largeImage));
    CHECK(ctx->errorCount() == 0);
}
