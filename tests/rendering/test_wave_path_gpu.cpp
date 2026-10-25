// ADR-1662: a path wave on the GPU.
//   * off = byte-identical: the water with no path wave live -- and with one whose light and ripples are zero -- is
//     the frame a shader with every path-wave block removed draws, on the plain and the cinematic path; a lit
//     ground with an ordinary wave is unchanged by a dark or a distant path wave beside it;
//   * it follows the bend: round an L-shaped path, a point on the path past the corner, inside the wake, is lit,
//     while the point the same distance along the first leg's straight continuation is not;
//   * on water, its light and its ripples each reach the picture (the controls).

#include "support/water_bench.hpp"
#include "support/water_shader_variant.hpp"

#include "core/log.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/mesh_generators.hpp"
#include "scene/scene.hpp"
#include "world/effects/effect_instance.hpp"
#include "world/effects/effect_registry.hpp"
#include "world/wave_effect.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <memory>
#include <vector>

using namespace avgen;
using namespace avgen::testsupport;
namespace fs = std::filesystem;

namespace {

constexpr const char* kPathBegin = "---- path waves (ADR-1662) begin ----";
constexpr const char* kPathEnd = "---- path waves (ADR-1662) end ----";

constexpr std::uint32_t kW = 256;
constexpr std::uint32_t kH = 160;

// An L on the ground plane: east along z = 30 from x = -50 to 20, then north (-z) to z = -50.
const std::vector<glm::vec3> kL{{-50.0f, 0.0f, 30.0f}, {20.0f, 0.0f, 30.0f}, {20.0f, 0.0f, -50.0f}};

class LScene final : public world::EffectSceneQuery {
public:
    [[nodiscard]] bool nodePosition(std::string_view, glm::vec3&) const override { return false; }
    [[nodiscard]] bool featurePath(std::string_view node, std::string_view feature, std::span<const glm::vec3>& points,
                                   glm::mat4& transform) const override {
        if (node != "ground" || feature != "L") {
            return false;
        }
        points = kL;
        transform = glm::mat4(1.0f);
        return true;
    }
};

world::EffectInstance pathBeam(float intensity, float disturb = 0.0f) {
    world::EffectInstance e = world::makeEffect(world::EffectKind::GroundPulse, "beam");
    e.activation = world::Activation::Always;
    e.timing = world::Timing{};
    e.wave.source.kind = world::SourceKind::World;
    e.wave.source.position = {-50.0f, 0.0f, 30.0f};
    e.wave.propagation.kind = world::PropagationKind::PathWave;
    e.wave.propagation.path.node = "ground";
    e.wave.propagation.path.feature = "L";
    e.wave.propagation.path.ease = 0.0f;
    e.wave.propagation.path.wakeGrowth = 0.0f;
    e.wave.propagation.path.reach = 14.0f;
    e.wave.propagation.path.reachHeight = 20.0f;
    e.wave.propagation.path.land = 1.0f;
    e.wave.propagation.path.disturb = disturb;
    e.wave.propagation.speed = 100.0f;
    e.wave.propagation.range = 400.0f;
    e.wave.propagation.startOffset = 0.0f;
    e.wave.propagation.trailLength = 60.0f;
    e.wave.propagation.frontWidth = 3.0f;
    e.wave.propagation.verticalExtent = 10.0f;
    e.wave.appearance.color = {1.0f, 0.2f, 0.7f};
    e.wave.appearance.intensity = intensity;
    e.wave.appearance.edgeIntensity = intensity;
    e.wave.response = world::MaterialResponse{1.0f, 1.0f, 1.0f, 0.0f};
    return e;
}

// The beam's record at `seconds`, resolved exactly as the engine resolves it.
world::WaveGpu resolvedBeam(const world::EffectInstance& beam, double seconds, glm::vec3 camera, glm::vec3 target) {
    static const LScene scene;
    world::EffectContext ctx;
    ctx.seconds = seconds;
    ctx.cameraPosition = camera;
    ctx.cameraTarget = target;
    ctx.cameraForward = glm::normalize(target - camera);
    ctx.scene = &scene;
    const auto r = world::resolveWave(beam, ctx);
    REQUIRE(r.has_value());
    return world::packWave(*r);
}

world::WaveGpu radial() {
    world::EffectInstance instance = world::makeEffect(world::EffectKind::GroundPulse, "ring");
    instance.wave.sparkle.enabled = false;
    instance.wave.propagation.range = 120.0f;
    instance.wave.propagation.frontWidth = 4.0f;
    instance.wave.propagation.trailLength = 14.0f;
    world::ResolvedWave r;
    r.effect = &instance;
    r.frontDistance = 25.0f;
    r.envelope = 1.0f;
    r.color = instance.wave.appearance.color;
    return world::packWave(r);
}

std::unique_ptr<gpu::Context> makeContext() {
    log::init(log::Level::Warn);
    auto ctx = gpu::Context::create(gpu::ContextDesc{});
    if (!ctx) {
        SKIP("no GPU adapter available: " << ctx.error().message);
    }
    return std::move(*ctx);
}

scene::Scene groundScene() {
    scene::Scene scene;
    const scene::MeshId plane = scene.addMesh(scene::makePlane(120.0f, 1));
    scene::Entity& ground = scene.addEntity("ground", plane);
    ground.material.baseColor = glm::vec4(0.2f, 0.2f, 0.22f, 1.0f);
    ground.material.roughness = 0.9f;
    scene::PunctualLight key;
    key.type = scene::PunctualLight::Type::Directional;
    key.direction = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f));
    key.intensity = 1.0f;
    scene.addLight(key);
    scene.camera.position = glm::vec3(-10.0f, 70.0f, 95.0f);
    scene.camera.target = glm::vec3(10.0f, 0.0f, -5.0f);
    scene.camera.fovYRadians = glm::radians(50.0f);
    scene.environment.backgroundColor = glm::vec3(0.02f, 0.02f, 0.03f);
    return scene;
}

// Mean luma of a 3x3 block round the pixel a ground point projects to.
double lumaAt(const gpu::Image8& img, const scene::Scene& s, glm::vec3 p) {
    const glm::mat4 view = glm::lookAt(s.camera.position, s.camera.target, glm::vec3(0, 1, 0));
    const glm::mat4 proj = glm::perspective(s.camera.effectiveFovY(), static_cast<float>(kW) / static_cast<float>(kH),
                                            0.1f, 500.0f);
    const glm::vec4 clip = proj * view * glm::vec4(p, 1.0f);
    REQUIRE(clip.w > 0.0f);
    const int x = static_cast<int>((clip.x / clip.w * 0.5f + 0.5f) * static_cast<float>(kW));
    const int y = static_cast<int>((0.5f - clip.y / clip.w * 0.5f) * static_cast<float>(kH));
    REQUIRE(x >= 1);
    REQUIRE(y >= 1);
    REQUIRE(x + 1 < static_cast<int>(kW));
    REQUIRE(y + 1 < static_cast<int>(kH));
    double total = 0.0;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            const std::size_t at = (static_cast<std::size_t>(y + dy) * kW + static_cast<std::size_t>(x + dx)) * 4;
            total += 0.2126 * img.rgba[at] + 0.7152 * img.rgba[at + 1] + 0.0722 * img.rgba[at + 2];
        }
    }
    return total / 9.0;
}

} // namespace

TEST_CASE("ADR-1662: a path wave follows the bend, and a dark or distant one leaves the ground as it was",
          "[gpu][waves][adr1662]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    scene::Scene scene = groundScene();
    const FrameTime time{.renderTime = 1.0, .deltaTime = 1.0 / 60.0, .frameIndex = 1};
    const auto render = [&](const scene::Scene& s) {
        auto image = renderer.renderToImage(s, time, kW, kH);
        REQUIRE(image.has_value());
        return std::move(*image);
    };

    // off: an ordinary ring alone, then beside a path wave that is dark, then beside one whose front is far away
    scene.waves.count = 1;
    scene.waves.effects[0] = radial();
    const gpu::Image8 ringOnly = render(scene);
    scene.waves.count = 2;
    scene.waves.effects[1] = resolvedBeam(pathBeam(0.0f), 1.0, scene.camera.position, scene.camera.target);
    CHECK(gpu::hashImage(render(scene)) == gpu::hashImage(ringOnly));
    world::WaveGpu far = resolvedBeam(pathBeam(8.0f), 1.0, scene.camera.position, scene.camera.target);
    for (glm::vec4& p : far.path) {
        p += glm::vec4(5000.0f, 0.0f, 5000.0f, 0.0f);
    }
    far.originKind += glm::vec4(5000.0f, 0.0f, 0.0f, 0.0f);
    scene.waves.effects[1] = far;
    CHECK(gpu::hashImage(render(scene)) == gpu::hashImage(ringOnly));

    // the bend: 1.0 s in, the front is 100 m along -- 30 m up the second leg -- with a 60 m wake behind it
    scene.waves = world::WaveFrame{};
    const gpu::Image8 dark = render(scene);
    scene.waves.count = 1;
    scene.waves.effects[0] = resolvedBeam(pathBeam(8.0f), 1.0, scene.camera.position, scene.camera.target);
    const gpu::Image8 lit = render(scene);
    const glm::vec3 roundTheBend{20.0f, 0.0f, 15.0f};   // on the path, 15 m past the corner, 15 m behind the front
    const glm::vec3 straightOn{55.0f, 0.0f, 30.0f};     // the first leg carried straight on by the same 85 m
    const glm::vec3 behind{-30.0f, 0.0f, 30.0f};        // on the path but 80 m behind the front: past the wake
    const double gainBend = lumaAt(lit, scene, roundTheBend) - lumaAt(dark, scene, roundTheBend);
    const double gainStraight = lumaAt(lit, scene, straightOn) - lumaAt(dark, scene, straightOn);
    const double gainBehind = lumaAt(lit, scene, behind) - lumaAt(dark, scene, behind);
    INFO("luma gained: round the bend " << gainBend << ", straight on " << gainStraight << ", behind the wake "
                                        << gainBehind);
    CHECK(gainBend > 20.0);
    CHECK(gainStraight < 1.0);
    CHECK(gainBehind < 1.0);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("ADR-1662: with no path wave lit the water is byte-identical to the shader with no path-wave code",
          "[gpu][renderer][water][adr1662][identity]") {
    if (!fs::is_regular_file(qaWaterScene())) {
        SKIP("the water QA scene is not present");
    }
    int blocks = 0;
    const std::string stripped = stripMarkedBlocks(readWaterShaderSource(), kPathBegin, kPathEnd, &blocks);
    INFO(blocks << " marked path-wave blocks removed");
    REQUIRE(blocks >= 3);
    for (const char* name : {"wavePathWater", "wavePathDisturb", "pathLight"}) {
        INFO(name);
        REQUIRE(stripped.find(name) == std::string::npos);
    }
    const fs::path strippedDir = writeWaterShaderVariant("no-path-waves", stripped);

    WaterBench bench = WaterBench::make(256, 176);
    WaterBench::Arm live = bench.arm();
    WaterBench::Arm none = bench.arm({strippedDir});
    bench.aim({40.0f, 6.0f, -86.0f}, {12.0f, -1.0f, -108.0f});
    bench.hold(12.0);
    scene::WaterSettings& w = bench.scene().waters.at(0).settings;

    // a straight chain across the visible river, its front mid-way
    world::WaveGpu beam{};
    beam.originKind = {12.0f, -1.0f, -108.0f, 2.0f};
    beam.axisFront = {-1.0f, 0.0f, 0.0f, 60.0f};
    beam.shape = {3.0f, 80.0f, 0.0f, 1.4f};
    beam.vertical = {10.0f, 20.0f, 0.0f, 12.0f};
    beam.color = {0.0f, 0.0f, 0.0f, 3.0f};
    beam.edge = {0.0f, 0.0f, 0.0f, 0.0f};
    beam.rainbow = {0.625f, 0.0f, 0.0f, 12.0f};
    beam.sparkle = {0.0f, 80.0f, 80.0f, 0.0f};
    beam.response = {1.0f, 1.0f, 1.0f, 0.0f};
    const float xs[8] = {16.0f, 12.0f, 8.0f, 2.0f, -8.0f, -24.0f, -44.0f, -68.0f};
    for (int i = 0; i < 4; ++i) {
        beam.path[i] = {xs[2 * i], -108.0f, xs[2 * i + 1], -108.0f};
    }

    for (const bool cinematic : {false, true}) {
        INFO((cinematic ? "the cinematic path" : "the plain path"));
        if (cinematic) {
            w.ssr = 0.5f;
            w.tintPatches = 0.4f;
        }
        REQUIRE(w.cinematic() == cinematic);
        bench.scene().waves = world::WaveFrame{};
        CHECK(gpu::hashImage(bench.render(live)) == gpu::hashImage(bench.render(none)));
        bench.scene().waves.count = 2;
        bench.scene().waves.effects[0] = radial();
        bench.scene().waves.effects[1] = beam; // dark, still
        CHECK(gpu::hashImage(bench.render(live)) == gpu::hashImage(bench.render(none)));

        // THE CONTROLS: its ripples, and its light, each reach the picture
        bench.scene().waves.effects[1].rainbow.y = 0.08f;
        CHECK(gpu::hashImage(bench.render(live)) != gpu::hashImage(bench.render(none)));
        bench.scene().waves.effects[1].rainbow.y = 0.0f;
        bench.scene().waves.effects[1].color = {4.0f, 0.8f, 2.8f, 3.0f};
        CHECK(gpu::hashImage(bench.render(live)) != gpu::hashImage(bench.render(none)));
        bench.scene().waves.effects[1].color = beam.color;
    }
    CHECK(bench.ctx->errorCount() == 0);
}
