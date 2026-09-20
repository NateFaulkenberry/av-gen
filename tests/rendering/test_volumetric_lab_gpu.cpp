// The Volumetric / Atmosphere Lab (lab #9, docs/volumetric-lab/README.md).
//
// The question is what the air is doing between the camera and the subject, so the fixture is a
// subject at five known distances inside a volume with known parameters and nothing else:
// `examples/labs/volumetric-atmosphere-lab.scene.json`. Five identical unlit slabs at 4, 12, 24, 40
// and 60 m are five samples of one exponential, and the NEAREST slab is the control -- at 4 m the
// volume should barely touch it, so an arm in which the near slab moves as much as the far one is
// measuring something other than distance.
//
// What this lab owns is the ORDERING and the gating. The absolute level belongs to the post chain
// and the HDR Lab, and nothing here asserts one.

#include "app/engine.hpp"
#include "core/log.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "rendering/volume_renderer.hpp"
#include "scene/composition.hpp"
#include "scene/scene.hpp"
#include "support/image_diff.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace fs = std::filesystem;
using namespace avgen;

namespace {

constexpr std::uint32_t kW = 640;
constexpr std::uint32_t kH = 360;

// The slabs, in the order the fixture declares them, with the distance each one is at. All five are
// centred on the view axis, so each is read at the centre of the frame with only the NEAREST one
// visible there -- which is why the readings are taken by stepping the camera rather than by
// windowing. See `radianceOf`.
struct Slab {
    const char* name;
    float distance;
};
constexpr std::array<Slab, 5> kSlabs{{{"slab-004", 4.0f},
                                      {"slab-012", 12.0f},
                                      {"slab-024", 24.0f},
                                      {"slab-040", 40.0f},
                                      {"slab-060", 60.0f}}};

struct VolumeBench {
    std::unique_ptr<gpu::Context> ctx;
    std::unique_ptr<gpu::ShaderLibrary> shaders;
    std::unique_ptr<rendering::SceneRenderer> renderer;
    std::unique_ptr<app::Engine> engine;
    FixedStepClock clock{60.0};
    FrameTime time{};

    static fs::path scenePath() {
        return fs::path(AVGEN_SOURCE_DIR) / "examples" / "labs" / "volumetric-atmosphere-lab.scene.json";
    }

    static VolumeBench make() {
        static bool logInit = false;
        if (!logInit) {
            log::init(log::Level::Warn);
            logInit = true;
        }
        VolumeBench b;
        auto ctx = gpu::Context::create(gpu::ContextDesc{});
        if (!ctx) {
            SKIP("no GPU adapter available: " << ctx.error().message);
        }
        b.ctx = std::move(*ctx);
        b.shaders = std::make_unique<gpu::ShaderLibrary>(*b.ctx,
                                                         std::vector{fs::path(AVGEN_SHADER_SOURCE_DIR)});
        b.renderer = std::make_unique<rendering::SceneRenderer>(*b.ctx, *b.shaders);
        REQUIRE(b.renderer->init().has_value());
        b.engine = std::make_unique<app::Engine>(app::EngineMode::Offline);
        REQUIRE(b.engine->loadComposition(scenePath()).has_value());
        b.engine->setViewport(kW, kH);
        b.time = b.engine->tick(b.clock);
        b.engine->update(b.time);
        return b;
    }

    scene::Scene& scene() { return engine->composition()->scene(); }

    void setFloat(const char* path, float value) {
        auto* p = engine->params().findAs<float>(path);
        INFO("parameter " << path);
        REQUIRE(p != nullptr);
        p->setBase(value);
        engine->params().resetFinals();
        engine->update(time);
    }

    gpu::ImageF renderFloat() {
        auto image = renderer->renderToImageFloat(scene(), time, kW, kH);
        REQUIRE(image.has_value());
        return std::move(*image);
    }
    gpu::Image8 render8() {
        auto image = renderer->renderToImage(scene(), time, kW, kH);
        REQUIRE(image.has_value());
        return std::move(*image);
    }
};

// Mean scene-linear luminance of the centre block of the frame, which is the slab the camera is
// nearest to. The block is small (an eighth of the frame each way) so it lies wholly inside the
// 2.4 x 6 m slab at every distance the fixture uses.
double centreLuminance(const gpu::ImageF& img) {
    const std::uint32_t x0 = img.width * 7 / 16;
    const std::uint32_t x1 = img.width * 9 / 16;
    const std::uint32_t y0 = img.height * 7 / 16;
    const std::uint32_t y1 = img.height * 9 / 16;
    double sum = 0.0;
    std::size_t n = 0;
    for (std::uint32_t y = y0; y < y1; ++y) {
        for (std::uint32_t x = x0; x < x1; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * img.width + x) * 4;
            sum += 0.2126 * img.rgba[i] + 0.7152 * img.rgba[i + 1] + 0.0722 * img.rgba[i + 2];
            ++n;
        }
    }
    REQUIRE(n > 0);
    return sum / static_cast<double>(n);
}

// The radiance the camera sees from the slab `index` metres away, with every nearer slab moved out
// of the way. Moving the obstruction is deliberate: windowing a region of one frame would read five
// different parts of the image, and a difference between two parts of an image is not a difference
// between two distances.
double radianceAt(VolumeBench& bench, std::size_t index) {
    scene::Scene& s = bench.scene();
    // Every procedural in this fixture is one slab, in declaration order.
    REQUIRE(s.procedurals.size() == kSlabs.size());
    for (std::size_t i = 0; i < s.procedurals.size(); ++i) {
        REQUIRE(s.procedurals[i].instances.size() == 1);
        // Nearer slabs are pushed far off axis rather than deleted, so the draw count, the depth
        // prepass and the number of things the march is occluded by all stay the same between arms.
        const float offset = i < index ? 500.0f : 0.0f;
        s.procedurals[i].instances[0].position.x = offset;
        s.procedurals[i].structureVersion += 1;
    }
    const gpu::ImageF img = bench.renderFloat();
    return centreLuminance(img);
}

} // namespace

TEST_CASE("the volumetric fixture is the ladder it claims to be", "[volumetric][lab][gpu]") {
    // ADR-182 on the fixture, before anything is measured through it. Five slabs at five distances
    // with ONE material between them: if the slabs differ in anything but position, the ladder
    // below is a measurement of the difference and not of the air.
    VolumeBench bench = VolumeBench::make();
    const scene::Scene& s = bench.scene();
    REQUIRE(s.procedurals.size() == kSlabs.size());
    for (std::size_t i = 0; i < kSlabs.size(); ++i) {
        INFO("slab " << kSlabs[i].name);
        CHECK(s.procedurals[i].name == kSlabs[i].name);
        CHECK(s.procedurals[i].material.unlit); // or a reading is also a measurement of the BRDF
        CHECK(s.procedurals[i].material.baseColor == s.procedurals[0].material.baseColor);
        CHECK(s.procedurals[i].material.emissiveIntensity == 0.0f);
    }
    // The three multiplicative terms the fixture switches off, and the density it leaves on. A
    // fixture that cannot state the density at a point measures nothing.
    CHECK(s.environment.volumeDensity > 0.0f);
    CHECK(s.environment.volumeNoiseAmount == 0.0f);
    CHECK(s.environment.fogHeightAmount == 0.0f);
    CHECK(s.environment.volumeAnisotropy == 0.0f);
    CHECK(s.environment.volumeDensityField.empty());
    CHECK(rendering::VolumeRenderer::enabled(s));
    CHECK(bench.ctx->errorCount() == 0);
}

TEST_CASE("what survives to a slab falls with the distance to it", "[volumetric][lab][gpu]") {
    // Lab case 2. Five samples of one exponential, read one at a time.
    VolumeBench bench = VolumeBench::make();
    std::array<double, kSlabs.size()> lit{};
    for (std::size_t i = 0; i < kSlabs.size(); ++i) {
        lit[i] = radianceAt(bench, i);
    }
    INFO("radiance by distance: 4 m " << lit[0] << ", 12 m " << lit[1] << ", 24 m " << lit[2] << ", 40 m "
                                      << lit[3] << ", 60 m " << lit[4]);

    // Neither-ran guard. A ladder of five zeros is monotone in both directions.
    REQUIRE(lit[0] > 1e-4);

    // The ordering, which is what this lab owns. Absolute levels belong to the post chain.
    for (std::size_t i = 1; i < lit.size(); ++i) {
        INFO("slab " << kSlabs[i].name << " against " << kSlabs[i - 1].name);
        CHECK(lit[i] < lit[i - 1]);
    }

    // The control: at 4 m there is almost no air in the way, so the near slab must sit close to the
    // unattenuated value while the far one is visibly darker. An arm in which the near slab has
    // fallen as far as the 60 m one is measuring an ambient term, an exposure change or a composite
    // that is not depth aware -- not a transmittance.
    CHECK(lit[4] < lit[0] * 0.8);
    CHECK(bench.ctx->errorCount() == 0);
}

TEST_CASE("the two ways of not running the volume agree at the byte", "[volumetric][lab][gpu]") {
    // Lab cases 3 and 4. `--disable volume` removes the pass that exists; `volumeDensity = 0` means
    // `VolumeRenderer::enabled` is false and no pass is encoded at all. Two different routes to
    // "the volume did not run", and if they disagree then one of them is not what it says.
    //
    // ADR-387 as a frame: the only proof a gate is a gate is that the gated frame is the ungated
    // one.
    VolumeBench bench = VolumeBench::make();

    const gpu::Image8 withFog = bench.render8();

    rendering::SceneRenderer::PassToggles toggles;
    toggles.volume = false;
    bench.renderer->setPassToggles(toggles);
    const gpu::Image8 passDisabled = bench.render8();
    bench.renderer->setPassToggles(rendering::SceneRenderer::PassToggles{});

    bench.setFloat("scene/volumeDensity", 0.0f);
    REQUIRE_FALSE(rendering::VolumeRenderer::enabled(bench.scene()));
    const gpu::Image8 densityZero = bench.render8();

    // ADR-362: a count and an offset, never `a.rgba == b.rgba` over a multi-megabyte buffer.
    {
        const auto d = testing::byteDiff(passDisabled.rgba, densityZero.rgba);
        INFO("pass disabled vs density zero: " << d.describe());
        CHECK(d.identical());
    }
    // ...and the control that says both of them are a change at all. Without this, a renderer that
    // had quietly stopped encoding the volume pass under every condition would pass the assertion
    // above and this whole test would be agreeing about nothing.
    {
        const auto d = testing::byteDiff(withFog.rgba, passDisabled.rgba);
        INFO("fog on vs pass disabled: " << d.describe());
        CHECK_FALSE(d.identical());
    }
    CHECK(bench.ctx->errorCount() == 0);
}

TEST_CASE("the march's resolution changes its cost and not the ladder", "[volumetric][lab][gpu]") {
    // Lab case 5. The slabs are large, flat and square to the view, so there is no spatial frequency
    // for a resolution change to reach: the readings must survive it. This is the control for any
    // future arm about the depth-aware upsample, whose whole subject is the edges.
    VolumeBench bench = VolumeBench::make();

    rendering::QualitySettings half = bench.renderer->qualitySettings();
    half.volumeResolutionScale = 0.5f;
    bench.renderer->setQualitySettings(half);
    const double halfRes = radianceAt(bench, kSlabs.size() - 1);
    const rendering::VolumeStats halfStats = bench.renderer->volumes().stats();

    rendering::QualitySettings full = half;
    full.volumeResolutionScale = 1.0f;
    bench.renderer->setQualitySettings(full);
    const double fullRes = radianceAt(bench, kSlabs.size() - 1);
    const rendering::VolumeStats fullStats = bench.renderer->volumes().stats();

    INFO("march " << halfStats.marchWidth << "x" << halfStats.marchHeight << " -> " << fullStats.marchWidth
                  << "x" << fullStats.marchHeight << "; 60 m radiance " << halfRes << " -> " << fullRes);

    // The scale was APPLIED and not clamped away by the viewport. These fields exist so that a
    // reading taken at a scale nobody got can be told apart from one taken at the scale asked for,
    // and without this check the comparison below would pass trivially when both arms marched at
    // the same size.
    REQUIRE(fullStats.marchWidth > halfStats.marchWidth);
    REQUIRE(halfStats.marchWidth > 0);

    REQUIRE(halfRes > 1e-4);
    CHECK(std::abs(fullRes - halfRes) < halfRes * 0.05);
    CHECK(bench.ctx->errorCount() == 0);
}
