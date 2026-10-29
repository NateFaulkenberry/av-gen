// ADR-1865: a mirror-still water under a cirrus sky shows the streaks, where the sky above it has them.
//
// The water evaluates the sky's own cirrus function (sky_cirrus.wgsl) along its reflected ray. The claim, measured
// against the CPU twin (scene::skyCirrus): for a flat, rippleless sheet, the pixels where the reflection is
// brightened by the layer are the pixels whose MIRRORED view ray the CPU twin says is under a streak.

#include "support/water_bench.hpp"

#include "scene/sky.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using namespace avgen;
using namespace avgen::testsupport;

namespace {

scene::MeshData flatSheet(float half) {
    scene::MeshData mesh;
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    mesh.vertices.push_back({{-half, 0.0f, -half}, up, {0.0f, 0.0f}});
    mesh.vertices.push_back({{half, 0.0f, -half}, up, {1.0f, 0.0f}});
    mesh.vertices.push_back({{half, 0.0f, half}, up, {1.0f, 1.0f}});
    mesh.vertices.push_back({{-half, 0.0f, half}, up, {0.0f, 1.0f}});
    mesh.indices = {0, 2, 1, 0, 3, 2};
    return mesh;
}

scene::Scene mirrorUnderCirrus(float amount) {
    scene::Scene s;
    s.environment.gridIntensity = 0.0f;
    s.environment.sky.enabled = true;
    s.environment.sky.showBackground = true;
    s.environment.sky.zenithColor = {0.02f, 0.03f, 0.12f};
    s.environment.sky.horizonColor = {0.03f, 0.04f, 0.14f};
    s.environment.sky.useKeyLight = false;
    s.environment.sky.sunDirection = glm::normalize(glm::vec3(-0.9f, 0.15f, 0.4f));
    s.environment.sky.sunIntensity = 0.0f;
    s.environment.sky.cirrus.amount = amount;
    s.environment.sky.cirrus.coverage = 0.5f;
    s.environment.sky.cirrus.softness = 0.15f;
    s.environment.sky.cirrus.litColor = {1.0f, 1.0f, 1.0f};
    s.environment.sky.cirrus.shadeColor = {1.0f, 1.0f, 1.0f};
    s.environment.sky.cirrus.sunScatter = 0.0f;
    s.environment.sky.cirrus.reflection = 0.0f; // the cube carries none: every streak in the water is analytic
    s.camera.position = {0.0f, 40.0f, 0.0f};
    s.camera.target = {0.0f, 0.0f, -60.0f}; // looking down at the water, ~34 degrees
    s.camera.fovYRadians = glm::radians(50.0f);
    s.camera.farPlane = 2000.0f;
    scene::Entity& water = s.addEntity("sea", s.addMesh(flatSheet(1500.0f)));
    water.style = scene::MeshStyle::Water;
    water.material.program = "sea";
    water.material.doubleSided = true;
    scene::WaterSettings w;
    w.shallowColor = {0.0f, 0.0f, 0.0f};
    w.deepColor = {0.0f, 0.0f, 0.0f};
    w.fresnel = 1.0f;          // the whole reflection, at every angle
    w.reflection = 1.0f;
    w.reflectionTint = {1.0f, 1.0f, 1.0f};
    w.roughness = 0.02f;
    w.ripple = 0.0f;           // glass
    w.specular = 0.0f;
    w.foam = 0.0f;
    w.glow = 0.0f;
    w.sparkle = 0.0f;
    w.refraction = 0.0f;
    w.maxOpacity = 1.0f;
    s.waters.push_back({"sea", w, 0.0f});
    return s;
}

} // namespace

TEST_CASE("water reflects the cirrus where the mirrored sky has it", "[water][sky][gpu][cirrus][adr1865]") {
    auto ctx = makeWaterContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    renderer.setPassToggles(waterQuantitativeToggles());
    constexpr std::uint32_t W = 256, H = 144;
    const auto render = [&](const scene::Scene& s) {
        FrameTime t{};
        t.renderTime = 0.0;
        renderer.resetTemporalHistory();
        auto img = renderer.renderToImageFloat(s, t, W, H);
        REQUIRE(img.has_value());
        return luminance(*img);
    };
    const scene::Scene on = mirrorUnderCirrus(1.0f);
    const std::vector<float> clear = render(mirrorUnderCirrus(0.0f));
    const std::vector<float> clouded = render(on);

    const scene::SkyRuntime rt = scene::resolveSky(on.environment.sky, on.lights);
    const glm::vec3 eye = on.camera.position;
    const glm::vec3 fwd = glm::normalize(on.camera.target - eye);
    const glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0, 1, 0)));
    const glm::vec3 up = glm::cross(right, fwd);
    const float tv = std::tan(on.camera.fovYRadians * 0.5f);
    const float th = tv * static_cast<float>(W) / static_cast<float>(H);
    int agree = 0, disagree = 0, covered = 0, waterPixels = 0;
    for (std::uint32_t y = 0; y < H; ++y) {
        for (std::uint32_t x = 0; x < W; ++x) {
            const float sx = (2.0f * (static_cast<float>(x) + 0.5f) / static_cast<float>(W) - 1.0f) * th;
            const float sy = (1.0f - 2.0f * (static_cast<float>(y) + 0.5f) / static_cast<float>(H)) * tv;
            const glm::vec3 d = glm::normalize(fwd + right * sx + up * sy);
            if (d.y > -0.05f) {
                continue; // the sky itself, or water too close to the horizon to call
            }
            ++waterPixels;
            const glm::vec3 mirrored(d.x, -d.y, d.z); // a flat sheet's reflection
            const float cover = scene::skyCirrus(rt.cirrus, mirrored, rt.sunDirection).cover;
            if (cover > 0.05f && cover < 0.8f) {
                continue; // an edge
            }
            const std::size_t i = static_cast<std::size_t>(y) * W + x;
            const bool gpuCovered = clouded[i] - clear[i] > 0.15f;
            const bool cpuCovered = cover >= 0.8f;
            covered += cpuCovered ? 1 : 0;
            (gpuCovered == cpuCovered ? agree : disagree) += 1;
        }
    }
    INFO("water pixels " << waterPixels << ": agree " << agree << ", disagree " << disagree << ", covered " << covered);
    REQUIRE(waterPixels > 5000);
    CHECK(covered > 400);           // there are streaks in the water, or this proves nothing
    CHECK(disagree * 50 < agree);   // under 2% (the GPU fades where a pixel spans more than a streak)
}

#include "support/water_shader_variant.hpp"

TEST_CASE("a rippled water's cirrus does not flicker as the camera glides: the reflection is filtered to the pixel",
          "[water][sky][gpu][cirrus][adr1868]") {
    // ADR-1868. Fine ripples swing the reflected ray across a streak within a pixel; as the camera moves, the sample
    // lands on the other side of it frame to frame. The water flattens the normal the streaks are looked up with where
    // the ripples' spread dwarfs the view's own. Measured: luminance sign reversals over three frames of a sub-pixel
    // glide, against the same shader with that filter removed (the control, derived from the live file).
    const std::string live = testsupport::readWaterShaderSource();
    const std::string unfiltered = testsupport::replaceOnce(
        live, "mix(WATER_CIRRUS_FLATTEN, 1.0, rippleAlias)", "WATER_CIRRUS_FLATTEN");
    const auto controlDir = testsupport::writeWaterShaderVariant("cirrus-unfiltered", unfiltered);
    const auto reversals = [&](const std::vector<std::filesystem::path>& dirs) {
        auto ctx = makeWaterContext();
        std::vector<std::filesystem::path> d = dirs;
        d.emplace_back(AVGEN_SHADER_SOURCE_DIR);
        gpu::ShaderLibrary shaders(*ctx, d);
        rendering::SceneRenderer renderer(*ctx, shaders);
        REQUIRE(renderer.init().has_value());
        renderer.setPassToggles(waterQuantitativeToggles());
        constexpr std::uint32_t W = 256, H = 144;
        std::vector<std::vector<float>> frames;
        for (int f = 0; f < 4; ++f) {
            scene::Scene s = mirrorUnderCirrus(1.0f);
            scene::WaterSettings& w = s.waters.front().settings;
            w.ripple = 0.35f;
            w.rippleScale = 1.2f; // fine: resolvable on its own, but it swings the reflection across streaks
            w.rippleSpeed = 0.0f;
            const glm::vec3 glide(0.37f * static_cast<float>(f), 0.0f, -0.53f * static_cast<float>(f));
            s.camera.position += glide;
            s.camera.target += glide;
            FrameTime t{};
            t.renderTime = 0.0;
            renderer.resetTemporalHistory();
            auto img = renderer.renderToImageFloat(s, t, W, H);
            REQUIRE(img.has_value());
            frames.push_back(luminance(*img));
        }
        std::size_t n = 0;
        for (std::size_t i = 0; i < frames[0].size(); ++i) {
            for (int f = 1; f + 1 < 4; ++f) {
                const float a = frames[f][i] - frames[f - 1][i], b = frames[f + 1][i] - frames[f][i];
                if (a * b < 0.0f && std::abs(a) > 0.05f && std::abs(b) > 0.05f) ++n;
            }
        }
        return n;
    };
    const std::size_t control = reversals({controlDir});
    const std::size_t filtered = reversals({});
    INFO("sign reversals: unfiltered " << control << ", filtered " << filtered);
    REQUIRE(control > 200); // the control flickers, or this proves nothing
    CHECK(filtered * 2 < control);
}
