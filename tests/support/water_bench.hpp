#pragma once

// The QA water scene on the GPU, for the water surface tests (ADR-914, ADR-915, ADR-916).
//
// The scene is `examples/qa/renderer-qa-water.scene.json`: the shipped world's river, generated for
// real, driven through the Engine. Any number of renderers can draw it -- one per shader under
// comparison (see water_shader_variant.hpp), all on one device -- and every measurement reads the
// scene-linear HDR target with post and ambient occlusion off: exposure re-meters when content
// changes, and the AO buffer is temporally jittered.

#include "app/engine.hpp"
#include "assets/image.hpp"
#include "core/log.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/composition.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace avgen::testsupport {

inline std::unique_ptr<gpu::Context> makeWaterContext() {
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

inline rendering::SceneRenderer::PassToggles waterQuantitativeToggles() {
    rendering::SceneRenderer::PassToggles toggles;
    toggles.post = false;
    toggles.ao = false;
    return toggles;
}

inline std::filesystem::path qaWaterScene() {
    return std::filesystem::path(AVGEN_SOURCE_DIR) / "examples" / "qa" / "renderer-qa-water.scene.json";
}

struct WaterBench {
    std::unique_ptr<gpu::Context> ctx;
    std::unique_ptr<app::Engine> engine;
    FixedStepClock clock{60.0};
    FrameTime time{};
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    struct Arm {
        std::unique_ptr<gpu::ShaderLibrary> shaders;
        std::unique_ptr<rendering::SceneRenderer> renderer;
    };

    static WaterBench make(std::uint32_t w, std::uint32_t h) {
        WaterBench b;
        b.ctx = makeWaterContext();
        b.width = w;
        b.height = h;
        b.engine = std::make_unique<app::Engine>(app::EngineMode::Offline);
        REQUIRE(b.engine->loadComposition(qaWaterScene()).has_value());
        b.time = b.engine->tick(b.clock);
        b.engine->setViewport(w, h);
        b.engine->update(b.time);
        return b;
    }

    // A renderer over `dirs`, searched in order; the live source tree is always last.
    Arm arm(std::vector<std::filesystem::path> dirs = {}) {
        dirs.emplace_back(AVGEN_SHADER_SOURCE_DIR);
        Arm a;
        a.shaders = std::make_unique<gpu::ShaderLibrary>(*ctx, dirs);
        a.renderer = std::make_unique<rendering::SceneRenderer>(*ctx, *a.shaders);
        REQUIRE(a.renderer->init().has_value());
        a.renderer->setPassToggles(waterQuantitativeToggles());
        return a;
    }

    void resize(std::uint32_t w, std::uint32_t h) {
        width = w;
        height = h;
        engine->setViewport(w, h);
        engine->update(time);
    }

    // The camera is re-derived from these parameters every update, so writing `scene().camera`
    // directly would do nothing.
    void aim(glm::vec3 position, glm::vec3 target) {
        params::ParameterSet& parameters = engine->params();
        auto* mode = parameters.findAs<int>("camera/mode");
        auto* pos = parameters.findAs<glm::vec3>("camera/position");
        auto* at = parameters.findAs<glm::vec3>("camera/target");
        REQUIRE(mode != nullptr);
        REQUIRE(pos != nullptr);
        REQUIRE(at != nullptr);
        mode->setBase(1);
        pos->setBase(position);
        at->setBase(target);
        parameters.resetFinals();
        time = engine->tick(clock);
        engine->setViewport(width, height);
        engine->update(time);
        REQUIRE(scene().camera.position == position);
    }

    // Pins the timeline second the water is drawn at: its flow clock is `time.renderTime`.
    void hold(double seconds) {
        time.renderTime = seconds;
        time.deltaTime = 1.0 / 60.0;
        time.frameIndex = 0;
        engine->update(time);
    }

    scene::Scene& scene() { return engine->composition()->scene(); }

    gpu::ImageF render(Arm& a) {
        a.renderer->resetTemporalHistory();
        auto image = a.renderer->renderToImageFloat(scene(), time, width, height);
        REQUIRE(image.has_value());
        return std::move(*image);
    }

    // The same frame with the water pass off: the only reference that isolates the water.
    gpu::ImageF renderDry(Arm& a) {
        auto toggles = waterQuantitativeToggles();
        toggles.water = false;
        a.renderer->setPassToggles(toggles);
        gpu::ImageF image = render(a);
        a.renderer->setPassToggles(waterQuantitativeToggles());
        return image;
    }
};

// With AVGEN_WATER_DUMP set to a directory, writes an HDR frame there as a PNG (exposure `gain`, a
// Reinhard curve and sRGB), so a failing measurement can be looked at. Does nothing otherwise.
inline void dumpWater(const gpu::ImageF& img, const std::string& name, float gain = 1.0f) {
    const char* dir = std::getenv("AVGEN_WATER_DUMP");
    if (dir == nullptr || dir[0] == '\0') {
        return;
    }
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(img.width) * img.height * 4);
    for (std::size_t i = 0; i < static_cast<std::size_t>(img.width) * img.height; ++i) {
        for (int c = 0; c < 3; ++c) {
            const float v = std::max(img.rgba[i * 4 + c] * gain, 0.0f);
            const float mapped = v / (1.0f + v);
            const float srgb = mapped <= 0.0031308f ? mapped * 12.92f : 1.055f * std::pow(mapped, 1.0f / 2.4f) - 0.055f;
            rgba[i * 4 + c] = static_cast<std::uint8_t>(std::clamp(srgb, 0.0f, 1.0f) * 255.0f + 0.5f);
        }
        rgba[i * 4 + 3] = 255;
    }
    static_cast<void>(assets::writePng(std::filesystem::path(dir) / ("water-" + name + ".png"), img.width,
                                       img.height, rgba));
}

inline std::vector<float> luminance(const gpu::ImageF& img) {
    std::vector<float> out(static_cast<std::size_t>(img.width) * img.height);
    for (std::uint32_t y = 0; y < img.height; ++y) {
        for (std::uint32_t x = 0; x < img.width; ++x) {
            const float* p = img.pixel(x, y);
            out[static_cast<std::size_t>(y) * img.width + x] = 0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2];
        }
    }
    return out;
}

// Where the water pass changed the frame at all. Water writes no identifier (its pipeline masks the
// auxiliary targets), so an A/B against the same frame with the water off is the instrument.
inline std::vector<char> waterMask(const gpu::ImageF& wet, const gpu::ImageF& dry) {
    std::vector<char> mask(static_cast<std::size_t>(wet.width) * wet.height, 0);
    for (std::size_t i = 0; i < mask.size(); ++i) {
        const float* a = wet.rgba.data() + i * 4;
        const float* b = dry.rgba.data() + i * 4;
        const float d = std::fabs(a[0] - b[0]) + std::fabs(a[1] - b[1]) + std::fabs(a[2] - b[2]);
        mask[i] = d > 1e-5f ? 1 : 0;
    }
    return mask;
}

inline std::size_t countMask(const std::vector<char>& mask) {
    return static_cast<std::size_t>(std::count(mask.begin(), mask.end(), 1));
}

// The fine structure of a luminance field: each pixel against the mean of its 3x3 neighbourhood,
// averaged over the pixels whose whole neighbourhood is water (so the shoreline's own edge is not
// counted as ripple). What streaking and aliasing raise, and what a fade lowers.
struct Fine {
    double energy = 0.0;   // mean |pixel - its 3x3 mean|
    double spread = 0.0;   // standard deviation of the same pixels
    std::size_t pixels = 0;
    // The fine structure relative to the contrast it rides on: how much of the water's variation is at
    // the pixel scale. Robust to a frame that is simply brighter or more contrasty than another.
    [[nodiscard]] double relative() const { return spread > 0.0 ? energy / spread : 0.0; }
};

inline Fine fineStructure(const std::vector<float>& lum, const std::vector<char>& mask, std::uint32_t w,
                          std::uint32_t h, std::uint32_t rowFrom = 0, std::uint32_t rowTo = 0xffffffffu) {
    Fine out;
    double sum = 0.0;
    double lumSum = 0.0;
    double lumSq = 0.0;
    for (std::uint32_t y = std::max(rowFrom, 1u); y + 1 < std::min(rowTo, h); ++y) {
        for (std::uint32_t x = 1; x + 1 < w; ++x) {
            bool inside = true;
            double mean = 0.0;
            for (int dy = -1; dy <= 1 && inside; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const std::size_t q = static_cast<std::size_t>(y + dy) * w + (x + dx);
                    if (mask[q] == 0) {
                        inside = false;
                        break;
                    }
                    mean += lum[q];
                }
            }
            if (!inside) {
                continue;
            }
            mean /= 9.0;
            const double v = lum[static_cast<std::size_t>(y) * w + x];
            sum += std::fabs(v - mean);
            lumSum += v;
            lumSq += v * v;
            ++out.pixels;
        }
    }
    if (out.pixels > 0) {
        const double n = static_cast<double>(out.pixels);
        out.energy = sum / n;
        out.spread = std::sqrt(std::max(lumSq / n - (lumSum / n) * (lumSum / n), 0.0));
    }
    return out;
}

// A 2x box-average of a luminance field, onto the grid of a frame half the size.
inline std::vector<float> boxDown2(const std::vector<float>& src, std::uint32_t w, std::uint32_t h) {
    std::vector<float> out(static_cast<std::size_t>(w / 2) * (h / 2));
    for (std::uint32_t y = 0; y < h / 2; ++y) {
        for (std::uint32_t x = 0; x < w / 2; ++x) {
            const std::size_t a = static_cast<std::size_t>(2 * y) * w + 2 * x;
            out[static_cast<std::size_t>(y) * (w / 2) + x] =
                0.25f * (src[a] + src[a + 1] + src[a + w] + src[a + w + 1]);
        }
    }
    return out;
}

// A mask on the grid of a frame half the size: a coarse pixel is set when all four fine ones are.
inline std::vector<char> maskDown2(const std::vector<char>& src, std::uint32_t w, std::uint32_t h) {
    std::vector<char> out(static_cast<std::size_t>(w / 2) * (h / 2));
    for (std::uint32_t y = 0; y < h / 2; ++y) {
        for (std::uint32_t x = 0; x < w / 2; ++x) {
            const std::size_t a = static_cast<std::size_t>(2 * y) * w + 2 * x;
            out[static_cast<std::size_t>(y) * (w / 2) + x] =
                (src[a] != 0 && src[a + 1] != 0 && src[a + w] != 0 && src[a + w + 1] != 0) ? 1 : 0;
        }
    }
    return out;
}

} // namespace avgen::testsupport
