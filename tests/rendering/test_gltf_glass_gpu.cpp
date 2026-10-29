// ADR-1819: an imported glass refracts what is behind it; with refraction off it is exactly the opaque material it was
// before ADR-1819. ADR-1818: multiple-scattering compensation brightens rough metal and leaves smooth dielectric alone.
//
// `AVGEN_EFFECT_DUMP=<dir>` writes the frames as PNGs.

#include "assets/gltf_loader.hpp"
#include "assets/image.hpp"
#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/mesh_generators.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

constexpr std::uint32_t kWidth = 480;
constexpr std::uint32_t kHeight = 270;

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

void dump(const gpu::Image8& img, const std::string& stem) {
    const char* dir = std::getenv("AVGEN_EFFECT_DUMP");
    if (dir != nullptr && dir[0] != '\0') {
        static_cast<void>(assets::writePng(fs::path(dir) / (stem + ".png"), img.width, img.height, img.rgba));
    }
}

void lightIt(scene::Scene& s, glm::vec3 eye, glm::vec3 target) {
    s.environment.backgroundColor = {0.02f, 0.02f, 0.03f};
    s.environment.showSkybox = false;
    s.camera.position = eye;
    s.camera.target = target;
    scene::PunctualLight sun;
    sun.direction = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.6f));
    sun.intensity = 3.0f;
    s.addLight(sun);
}

gpu::Image8 render(gpu::Context& ctx, gpu::ShaderLibrary& shaders, const scene::Scene& s, bool refraction = true,
                   bool multiScatter = true) {
    rendering::SceneRenderer r(ctx, shaders);
    REQUIRE(r.init().has_value());
    rendering::QualitySettings q = r.qualitySettings();
    q.refraction = refraction;
    q.multiScatter = multiScatter;
    r.setQualitySettings(q);
    FrameTime t{};
    t.renderTime = 1.0;
    auto img = r.renderToImage(s, t, kWidth, kHeight);
    REQUIRE(img.has_value());
    return std::move(*img);
}

// Mean luma and the spread of the red-minus-green signal (the stripes) over a box.
struct Box {
    double luma = 0.0;
    double stripeSpread = 0.0;
};
Box measure(const gpu::Image8& img, std::uint32_t x0, std::uint32_t y0, std::uint32_t x1, std::uint32_t y1) {
    double sum = 0.0, sum2 = 0.0, luma = 0.0;
    int n = 0;
    for (std::uint32_t y = y0; y < y1; ++y) {
        for (std::uint32_t x = x0; x < x1; ++x) {
            const auto* p = img.pixel(x, y);
            const double rg = static_cast<double>(p[0]) - static_cast<double>(p[1]);
            sum += rg;
            sum2 += rg * rg;
            luma += 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2];
            ++n;
        }
    }
    const double mean = sum / n;
    return {luma / n, std::sqrt(std::max(sum2 / n - mean * mean, 0.0))};
}

} // namespace

TEST_CASE("An imported glass refracts, and with refraction off is the opaque material it was", "[gpu][gltf][adr1819]") {
    const fs::path dir(AVGEN_GLASS_FIXTURE_DIR);
    if (!fs::exists(dir / "glass.glb")) {
        SKIP("the glass fixture was not generated");
    }
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    const glm::vec3 eye(0.8f, 1.5f, 4.5f);
    const glm::vec3 target(0.0f, 1.0f, 0.0f);

    scene::Scene glass;
    REQUIRE(assets::loadGltf(dir / "glass.glb", glass).has_value());
    lightIt(glass, eye, target);
    scene::Scene opaque;
    assets::GltfLoadOptions noTransmission;
    noTransmission.importTransmission = false;
    REQUIRE(assets::loadGltf(dir / "glass.glb", opaque, noTransmission).has_value());
    lightIt(opaque, eye, target);

    const gpu::Image8 refracted = render(*ctx, shaders, glass);
    const gpu::Image8 fallback = render(*ctx, shaders, glass, /*refraction=*/false);
    const gpu::Image8 before = render(*ctx, shaders, opaque);
    dump(refracted, "glass-refracted");
    dump(fallback, "glass-fallback");
    dump(before, "glass-opaque-import");

    // With refraction off, the glass is exactly the opaque material it imports as without the extensions.
    CHECK(gpu::hashImage(fallback) == gpu::hashImage(before));
    // With it on, the sphere shows the stripes behind it: a strong red-green signal where the opaque sphere is white.
    const Box through = measure(refracted, 190, 100, 270, 170);
    const Box solid = measure(before, 190, 100, 270, 170);
    INFO("stripe spread inside the sphere: glass " << through.stripeSpread << ", opaque " << solid.stripeSpread);
    CHECK(through.stripeSpread > 4.0 * std::max(solid.stripeSpread, 1.0));
    CHECK(ctx->errorCount() == 0);
}

// Mean absolute difference per channel between two frames over a box.
static double meanDiff(const gpu::Image8& a, const gpu::Image8& b, std::uint32_t x0, std::uint32_t y0, std::uint32_t x1,
                std::uint32_t y1) {
    double sum = 0.0;
    int n = 0;
    for (std::uint32_t y = y0; y < y1; ++y) {
        for (std::uint32_t x = x0; x < x1; ++x) {
            for (int c = 0; c < 3; ++c) {
                sum += std::abs(static_cast<double>(a.pixel(x, y)[c]) - static_cast<double>(b.pixel(x, y)[c]));
                ++n;
            }
        }
    }
    return sum / n;
}

TEST_CASE("A thin-walled glass sees straight through, unbent", "[gpu][gltf][adr1819]") {
    // glTF: a transmissive material without KHR_materials_volume is thin-walled -- the view is not bent. The S3 lab's
    // glass is that kind, and bending it anyway (ADR-1819's first cut) pushed the emissive arch out of the sphere.
    const fs::path dir(AVGEN_GLASS_FIXTURE_DIR);
    if (!fs::exists(dir / "glass-thin.glb")) {
        SKIP("the glass fixture was not generated");
    }
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    const glm::vec3 eye(0.8f, 1.5f, 4.5f);
    const glm::vec3 target(0.0f, 1.0f, 0.0f);
    scene::Scene thin;
    REQUIRE(assets::loadGltf(dir / "glass-thin.glb", thin).has_value());
    lightIt(thin, eye, target);
    scene::Scene backdrop = thin;
    backdrop.entities.erase(backdrop.entities.begin()); // the stripes alone (entity 0 is the sphere)
    scene::Scene solid;
    REQUIRE(assets::loadGltf(dir / "glass.glb", solid).has_value());
    lightIt(solid, eye, target);

    const gpu::Image8 seen = render(*ctx, shaders, thin);
    const gpu::Image8 bare = render(*ctx, shaders, backdrop);
    const gpu::Image8 bent = render(*ctx, shaders, solid);
    dump(seen, "glass-thin");
    dump(bare, "glass-thin-backdrop");
    // The middle of the sphere, where the Fresnel reflects 4%: the thin glass shows the stripes where they are.
    const double straight = meanDiff(seen, bare, 190, 100, 270, 170);
    const double volume = meanDiff(bent, bare, 190, 100, 270, 170);
    INFO("mean difference from the bare backdrop: thin " << straight << ", with a volume " << volume);
    CHECK(straight < 12.0);
    CHECK(volume > 3.0 * straight); // the solid sphere does bend (and absorb)
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("A BLEND-exported glass refracts where refraction is on and keeps its blend where it is off",
          "[gpu][gltf][adr1819]") {
    const fs::path dir(AVGEN_GLASS_FIXTURE_DIR);
    if (!fs::exists(dir / "glass-blend.glb")) {
        SKIP("the glass fixture was not generated");
    }
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    const glm::vec3 eye(0.8f, 1.5f, 4.5f);
    const glm::vec3 target(0.0f, 1.0f, 0.0f);
    scene::Scene glass;
    REQUIRE(assets::loadGltf(dir / "glass-blend.glb", glass).has_value());
    lightIt(glass, eye, target);
    scene::Scene fade; // the blended fallback, as imported before ADR-1819
    assets::GltfLoadOptions noTransmission;
    noTransmission.importTransmission = false;
    REQUIRE(assets::loadGltf(dir / "glass-blend.glb", fade, noTransmission).has_value());
    lightIt(fade, eye, target);

    const gpu::Image8 refracted = render(*ctx, shaders, glass);
    const gpu::Image8 low = render(*ctx, shaders, glass, /*refraction=*/false);
    const gpu::Image8 before = render(*ctx, shaders, fade);
    dump(refracted, "glass-blend-refracted");
    dump(low, "glass-blend-low");
    // Refraction off (Low, Emergency): exactly the blend it was exported as.
    CHECK(gpu::hashImage(low) == gpu::hashImage(before));
    // On: the refracting glass, not the 25% fade.
    CHECK(gpu::hashImage(refracted) != gpu::hashImage(before));
    CHECK(meanDiff(refracted, before, 190, 100, 270, 170) > 8.0);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("Multiple-scattering compensation brightens rough metal, not smooth dielectric", "[gpu][material][adr1818]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    scene::Scene s;
    lightIt(s, glm::vec3(0.0f, 1.0f, 6.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    const scene::MeshId ball = s.addMesh(scene::makeIcosphere(1.0f, 5));
    scene::Entity& metal = s.addEntity("rough-gold", ball);
    metal.transform.position = {-1.3f, 1.0f, 0.0f};
    metal.material.baseColor = {1.0f, 0.78f, 0.34f};
    metal.material.metallic = 1.0f;
    metal.material.roughness = 0.8f;
    scene::Entity& plastic = s.addEntity("smooth-plastic", ball);
    plastic.transform.position = {1.3f, 1.0f, 0.0f};
    plastic.material.baseColor = {0.2f, 0.4f, 0.8f};
    plastic.material.metallic = 0.0f;
    plastic.material.roughness = 0.1f;

    const gpu::Image8 on = render(*ctx, shaders, s);
    const gpu::Image8 off = render(*ctx, shaders, s, true, /*multiScatter=*/false);
    dump(on, "multiscatter-on");
    dump(off, "multiscatter-off");
    // The left sphere (rough gold) and the right one (smooth plastic), boxes inside each.
    const double goldOn = measure(on, 110, 105, 170, 165).luma;
    const double goldOff = measure(off, 110, 105, 170, 165).luma;
    const double plasticOn = measure(on, 310, 105, 370, 165).luma;
    const double plasticOff = measure(off, 310, 105, 370, 165).luma;
    INFO("rough gold " << goldOff << " -> " << goldOn << "; smooth plastic " << plasticOff << " -> " << plasticOn);
    CHECK(goldOn > goldOff * 1.03);
    CHECK(std::abs(plasticOn - plasticOff) < 0.02 * std::max(plasticOff, 1.0));
    CHECK(ctx->errorCount() == 0);
}
