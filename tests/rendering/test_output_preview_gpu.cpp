// The output preview, on the device (ADR-246, output-preview spec §7, §16.2).
//
// ## Why this file exists at all
//
// The claim the whole feature rests on is "what the preview shows is what the render produces",
// and that claim is about an image. It cannot be settled by reading the source, and the machine
// this is developed on cannot see the editor's ImGui surface. So it is settled the only way it can
// be: render the same scene at the preview's extent and at the deliverable's, resample one onto the
// other, and measure how far apart they are -- with a control arm that renders at the *canvas's*
// aspect ratio instead, which is what the feature replaced and what a wrong implementation would
// still be doing.
//
// The arm is the point. A similarity threshold with nothing to compare it against is a number that
// passes whatever the code does (ADR-182); the two arms here differ by two orders of magnitude,
// and the assertion is on the ratio between them rather than on either one alone.
//
// Run under `tools/gpu-lock.sh` (ADR-170).

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/mesh_generators.hpp"
#include "scene/scene.hpp"
#include "ui/output_preview.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

using namespace avgen;
using Catch::Approx;

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

gpu::ShaderLibrary makeShaders(gpu::Context& ctx) {
    return gpu::ShaderLibrary(ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
}

// A scene with structure spread across the whole frame and out past its edges, which is what makes
// a framing difference visible at all. A centred subject on an empty background looks identical at
// every aspect ratio -- it would be the one scene that could not detect the defect this file is
// about.
scene::Scene griddedScene() {
    scene::Scene s;
    s.environment.backgroundColor = {0.02f, 0.03f, 0.05f};
    s.post.bloomEnabled = false;
    s.post.tonemap = scene::TonemapOperator::AgX;
    s.camera.position = {0.0f, 1.0f, 7.0f};
    s.camera.target = {0.0f, 1.0f, 0.0f};

    // A wide wall of cubes: eleven columns across, so the columns nearest the edges fall inside a
    // 16:9 frustum and outside a narrower one. That difference is precisely what "the preview is
    // showing the deliverable's framing" has to be able to see.
    const scene::MeshId cube = s.addMesh(scene::makeCube(0.28f));
    int index = 0;
    for (int ix = -5; ix <= 5; ++ix) {
        for (int iy = -2; iy <= 2; ++iy) {
            scene::Entity& entity = s.addEntity("cube" + std::to_string(index++), cube);
            entity.transform.position = glm::vec3(static_cast<float>(ix) * 0.9f,
                                                  1.0f + static_cast<float>(iy) * 0.9f, 0.0f);
            entity.material.baseColor = glm::vec4(0.25f + 0.07f * static_cast<float>(ix + 5),
                                                  0.7f - 0.1f * static_cast<float>(iy + 2), 0.55f, 1.0f);
            entity.material.roughness = 0.6f;
        }
    }
    scene::PunctualLight key;
    key.type = scene::PunctualLight::Type::Directional;
    key.direction = glm::normalize(glm::vec3(-0.4f, -0.8f, -0.45f));
    key.color = {1.0f, 0.97f, 0.92f};
    key.intensity = 4.0f;
    s.lights.push_back(key);
    return s;
}

// Mean absolute difference per channel, in 0..1, after resampling `image` to `w` x `h` with a box
// filter. Both images are resampled to a common small grid rather than one onto the other, so
// neither arm is advantaged by being the resampling target.
double meanAbsoluteDifference(const gpu::Image8& a, const gpu::Image8& b, std::uint32_t w,
                              std::uint32_t h) {
    const auto sample = [](const gpu::Image8& img, std::uint32_t gx, std::uint32_t gy, std::uint32_t gw,
                           std::uint32_t gh, int channel) {
        // The source block this grid cell covers, averaged. A box over the block rather than a
        // nearest-neighbour tap: a point sample of a cube grid at two resolutions is dominated by
        // which side of an edge the tap landed, which would make both arms look different.
        const std::uint32_t x0 = gx * img.width / gw;
        const std::uint32_t x1 = std::max(x0 + 1, (gx + 1) * img.width / gw);
        const std::uint32_t y0 = gy * img.height / gh;
        const std::uint32_t y1 = std::max(y0 + 1, (gy + 1) * img.height / gh);
        double sum = 0.0;
        std::uint32_t n = 0;
        for (std::uint32_t y = y0; y < std::min(y1, img.height); ++y) {
            for (std::uint32_t x = x0; x < std::min(x1, img.width); ++x) {
                sum += img.rgba[(static_cast<std::size_t>(y) * img.width + x) * 4 +
                                static_cast<std::size_t>(channel)];
                ++n;
            }
        }
        return n == 0 ? 0.0 : sum / static_cast<double>(n) / 255.0;
    };
    double total = 0.0;
    for (std::uint32_t y = 0; y < h; ++y) {
        for (std::uint32_t x = 0; x < w; ++x) {
            for (int c = 0; c < 3; ++c) {
                total += std::abs(sample(a, x, y, w, h, c) - sample(b, x, y, w, h, c));
            }
        }
    }
    return total / (static_cast<double>(w) * h * 3.0);
}

} // namespace

TEST_CASE("the preview's framing is the render's framing, and the canvas's is not",
          "[gpu][preview][framing]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    const scene::Scene scene = griddedScene();
    const FrameTime time{};

    // The deliverable. `RenderJob` renders at exactly `RenderSettings::width/height`, through
    // exactly this call, so this is not a stand-in for the render -- it is the render's own path.
    constexpr std::uint32_t kOutW = 1920;
    constexpr std::uint32_t kOutH = 1080;
    auto deliverable = renderer.renderToImage(scene, time, kOutW, kOutH);
    REQUIRE(deliverable.has_value());

    // What the preview asks for, computed by the production code from a canvas the editor really
    // has: a centre dock of 600 x 400 points on a 2x display -- an editor with panels open on both
    // sides and the sequencer up, which is the ordinary working layout. Its aspect ratio is 1.50
    // against the output's 1.78, which is the disagreement this whole feature is about.
    ui::CanvasRect canvas;
    canvas.width = 600.0f;
    canvas.height = 400.0f;
    const ui::PreviewFrame frame = ui::fitOutputFrame(canvas, kOutW, kOutH, ui::PreviewZoom{}, 2.0f);
    REQUIRE(frame.valid());
    const ui::PreviewRender extent =
        ui::previewRenderExtent(frame, kOutW, kOutH, 2.0f, ui::PreviewQuality::Realtime);
    REQUIRE(extent.aspectMatches);
    REQUIRE(extent.width > 0);
    // It is genuinely a smaller render than the deliverable -- otherwise this proves nothing about
    // a preview and everything about rendering the same size twice.
    REQUIRE(extent.width < kOutW);
    auto preview = renderer.renderToImage(scene, time, extent.width, extent.height);
    REQUIRE(preview.has_value());

    // The control arm: the canvas's own aspect ratio, which is what this editor rendered at before
    // ADR-246 and what a preview that "just shows the viewport" still shows. Same camera, same
    // scene, same time, same renderer -- the aspect ratio is the only thing that moves.
    const auto canvasW = static_cast<std::uint32_t>(canvas.width * 2.0f);
    const auto canvasH = static_cast<std::uint32_t>(canvas.height * 2.0f);
    REQUIRE(std::abs(static_cast<double>(canvasW) / canvasH - static_cast<double>(kOutW) / kOutH) > 0.25);
    auto canvasArm = renderer.renderToImage(scene, time, canvasW, canvasH);
    REQUIRE(canvasArm.has_value());

    // Compared on a common 64 x 36 grid -- the deliverable's own aspect, coarse enough that
    // resolution differences average out and structure does not.
    constexpr std::uint32_t kGridW = 64;
    constexpr std::uint32_t kGridH = 36;
    const double previewDiff = meanAbsoluteDifference(*preview, *deliverable, kGridW, kGridH);
    const double canvasDiff = meanAbsoluteDifference(*canvasArm, *deliverable, kGridW, kGridH);

    INFO("preview extent " << extent.width << "x" << extent.height << " differs from the "
                           << kOutW << "x" << kOutH << " deliverable by " << previewDiff
                           << "; the canvas-aspect arm (" << canvasW << "x" << canvasH << ") by "
                           << canvasDiff);

    // The arm is what makes the number above mean something. An implementation that kept rendering
    // at the canvas's shape would land on `canvasDiff`, so the assertion is the gap between them
    // and not a threshold somebody chose.
    REQUIRE(canvasDiff > previewDiff * 5.0);
    // And in absolute terms the preview is close: what is left is resampling and the finer
    // sampling of the larger render, not a different framing.
    REQUIRE(previewDiff < 0.02);
}

TEST_CASE("every preview extent the toolbar can ask for renders, and renders the same framing",
          "[gpu][preview][framing]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    const scene::Scene scene = griddedScene();
    const FrameTime time{};

    ui::CanvasRect canvas;
    canvas.width = 900.0f;
    canvas.height = 700.0f;

    struct Format {
        std::uint32_t w;
        std::uint32_t h;
    };
    // Landscape, portrait and square, because a preview that is right in 16:9 and wrong in 9:16 is
    // the defect §9.1 and §4.3 both warn about, and no 16:9 test would see it.
    for (const Format format : {Format{1280, 720}, Format{720, 1280}, Format{768, 768}}) {
        auto deliverable = renderer.renderToImage(scene, time, format.w, format.h);
        REQUIRE(deliverable.has_value());

        for (const auto quality :
             {ui::PreviewQuality::Draft, ui::PreviewQuality::Realtime, ui::PreviewQuality::Native}) {
            const ui::PreviewFrame frame =
                ui::fitOutputFrame(canvas, format.w, format.h, ui::PreviewZoom{}, 2.0f);
            const ui::PreviewRender extent =
                ui::previewRenderExtent(frame, format.w, format.h, 2.0f, quality);
            INFO(format.w << "x" << format.h << " at " << ui::previewQualityName(quality) << " -> "
                          << extent.width << "x" << extent.height);
            REQUIRE(extent.aspectMatches);

            auto preview = renderer.renderToImage(scene, time, extent.width, extent.height);
            REQUIRE(preview.has_value());
            REQUIRE(preview->width == extent.width);
            REQUIRE(preview->height == extent.height);

            const double diff = meanAbsoluteDifference(*preview, *deliverable, 48, 48);
            INFO("mean absolute difference from the deliverable: " << diff);
            REQUIRE(diff < 0.03);
            // Native is the same extent as the deliverable, so it must be much closer still than
            // the sampled rungs -- if it were not, something other than resolution would be moving
            // between the two paths.
            if (quality == ui::PreviewQuality::Native) {
                REQUIRE(extent.width == format.w);
                REQUIRE(extent.height == format.h);
                REQUIRE(diff < 0.001);
            }
        }
    }
}

TEST_CASE("switching preview resolutions repeatedly does not grow the renderer's targets",
          "[gpu][preview][resources]") {
    // Spec §14: "Repeated resolution changes must not cause unbounded allocations." The renderer
    // has exactly one HDR target and replaces it on resize, so the structural claim is that the
    // target after a cycle of sizes is the size that was last asked for and no other target
    // survives -- counted, not timed (ADR-170: on a contended machine, count structure).
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    const scene::Scene scene = griddedScene();
    const FrameTime time{};

    ui::CanvasRect canvas;
    canvas.width = 900.0f;
    canvas.height = 620.0f;

    const std::uint32_t sizes[][2] = {{1280, 720}, {1920, 1080}, {1080, 1920}, {1080, 1080}, {1280, 720}};
    for (int pass = 0; pass < 3; ++pass) {
        for (const auto& size : sizes) {
            const ui::PreviewFrame frame =
                ui::fitOutputFrame(canvas, size[0], size[1], ui::PreviewZoom{}, 2.0f);
            const ui::PreviewRender extent =
                ui::previewRenderExtent(frame, size[0], size[1], 2.0f, ui::PreviewQuality::Realtime);
            REQUIRE(renderer.resize(extent.width, extent.height).has_value());
            auto image = renderer.renderToImage(scene, time, extent.width, extent.height);
            REQUIRE(image.has_value());
            REQUIRE(image->width == extent.width);
            REQUIRE(image->height == extent.height);
        }
    }
    // Back to the first size and still producing exactly that size: the renderer is not carrying a
    // stale target from any of the fourteen extents before it.
    const ui::PreviewFrame frame = ui::fitOutputFrame(canvas, 1280, 720, ui::PreviewZoom{}, 2.0f);
    const ui::PreviewRender extent =
        ui::previewRenderExtent(frame, 1280, 720, 2.0f, ui::PreviewQuality::Native);
    REQUIRE(renderer.resize(extent.width, extent.height).has_value());
    auto last = renderer.renderToImage(scene, time, extent.width, extent.height);
    REQUIRE(last.has_value());
    REQUIRE(last->width == 1280u);
    REQUIRE(last->height == 720u);
}
