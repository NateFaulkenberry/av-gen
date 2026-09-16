// The output preview's arithmetic (ADR-246, output-preview spec §4.3, §5.3, §8.2, §9, §10.2).
//
// What is checked here is what goes quietly wrong in a viewport feature and what no screenshot
// would catch: a frame whose aspect ratio drifts by a pixel, a click that lands somewhere other
// than where the gizmo was drawn, a safe-area box that is right in landscape and wrong in
// portrait, a render extent that is *nearly* the output's shape. None of these look broken. All of
// them make the preview a liar, which is the one thing this feature must not be.
//
// Every case here is ImGui-free and device-free on purpose -- same rule as test_editor_layout.cpp.

#include "ui/output_preview.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>

using namespace avgen::ui;
using Catch::Approx;

namespace {

CanvasRect canvasOf(float w, float h, float x = 0.0f, float y = 0.0f) {
    CanvasRect c;
    c.x = x;
    c.y = y;
    c.width = w;
    c.height = h;
    return c;
}

// The aspect ratio the frame is actually drawn at, which is the number the whole feature rests on.
double drawnAspect(const PreviewFrame& f) {
    return static_cast<double>(f.width) / static_cast<double>(f.height);
}

} // namespace

// ---- resolution validation (spec §5.3) ----------------------------------------------------------

TEST_CASE("an output resolution is refused with a reason a person can act on", "[ui][preview][output]") {
    REQUIRE(validateOutputResolution(1920, 1080).has_value());
    REQUIRE(validateOutputResolution(1080, 1920).has_value());
    REQUIRE(validateOutputResolution(1080, 1080).has_value());

    // Zero, and the message says what the minimum is rather than only that this is wrong.
    const auto zero = validateOutputResolution(1920, 0);
    REQUIRE_FALSE(zero.has_value());
    REQUIRE(zero.error().message.find("zero") != std::string::npos);

    REQUIRE_FALSE(validateOutputResolution(0, 0).has_value());
    REQUIRE_FALSE(validateOutputResolution(8, 1080).has_value());   // below the per-axis floor
    REQUIRE(validateOutputResolution(kMinOutputDimension, kMinOutputDimension).has_value());

    // §5.3: "Make any renderer-specific maximum explicit to the user." The number in the message is
    // the number that was applied, not a constant compiled in somewhere else.
    const auto tooBig = validateOutputResolution(20000, 1080);
    REQUIRE_FALSE(tooBig.has_value());
    REQUIRE(tooBig.error().message.find("16384") != std::string::npos);

    // A device that reports a smaller limit refuses a size the default would have allowed, and says
    // *that* number.
    const auto deviceLimited = validateOutputResolution(12000, 1080, 8192);
    REQUIRE_FALSE(deviceLimited.has_value());
    REQUIRE(deviceLimited.error().message.find("8192") != std::string::npos);
    REQUIRE(validateOutputResolution(12000, 1080).has_value()); // allowed under the default ceiling
}

TEST_CASE("a pixel count that has already overflowed 32 bits is still refused", "[ui][preview][output]") {
    // The argument type is the point of this case. 65536 * 65536 is 2^32, which is exactly 0 in a
    // 32-bit product -- a check written in `uint32_t` would compute "0 pixels" and wave it through.
    // Taking the dimensions as 64-bit means the per-axis ceiling catches it before any product is
    // formed at all.
    REQUIRE_FALSE(validateOutputResolution(65536, 65536).has_value());
    REQUIRE_FALSE(validateOutputResolution(4294967296ULL, 1080).has_value()); // 2^32, wraps to 0
    REQUIRE_FALSE(validateOutputResolution(4294967297ULL, 1080).has_value()); // 2^32+1, wraps to 1
}

TEST_CASE("an aspect ratio is named the way a person would name it", "[ui][preview][output]") {
    REQUIRE(aspectLabel(1920, 1080) == "16:9");
    REQUIRE(aspectLabel(3840, 2160) == "16:9");
    REQUIRE(aspectLabel(1280, 720) == "16:9");
    REQUIRE(aspectLabel(1080, 1920) == "9:16");
    REQUIRE(aspectLabel(1080, 1080) == "1:1");
    REQUIRE(aspectLabel(1440, 1080) == "4:3");
    // 1920x816 reduces to 40:17, which tells nobody anything. The decimal does.
    REQUIRE(aspectLabel(1920, 816) == "2.35:1");
    REQUIRE(aspectLabel(0, 1080) == "-");

    REQUIRE(outputAspect(1920, 1080) == Approx(16.0 / 9.0));
    REQUIRE(outputAspect(1920, 0) == 0.0); // no division by zero, and no NaN downstream
}

// ---- frame geometry (spec §4.3) ------------------------------------------------------------------

TEST_CASE("the output frame keeps its aspect ratio in every canvas shape", "[ui][preview][geometry]") {
    struct Case {
        const char* what;
        float canvasW;
        float canvasH;
        std::uint32_t outW;
        std::uint32_t outH;
    };
    const Case cases[] = {
        {"16:9 in a wide canvas", 1600.0f, 600.0f, 1920, 1080},
        {"16:9 in a tall canvas", 600.0f, 1600.0f, 1920, 1080},
        {"16:9 in the editor's own centre dock", 864.0f, 612.0f, 1920, 1080},
        {"9:16 in a wide canvas", 1600.0f, 600.0f, 1080, 1920},
        {"9:16 in a tall canvas", 600.0f, 1600.0f, 1080, 1920},
        {"1:1 in a wide canvas", 1600.0f, 600.0f, 1080, 1080},
        {"1:1 in a tall canvas", 600.0f, 1600.0f, 1080, 1080},
        {"4K in a small canvas", 300.0f, 200.0f, 3840, 2160},
        {"a very short canvas", 1200.0f, 40.0f, 1920, 1080},
        {"a very narrow canvas", 40.0f, 1200.0f, 1920, 1080},
    };
    for (const Case& c : cases) {
        INFO(c.what);
        const CanvasRect canvas = canvasOf(c.canvasW, c.canvasH);
        const PreviewFrame frame = fitOutputFrame(canvas, c.outW, c.outH, PreviewZoom{}, 1.0f);
        REQUIRE(frame.valid());
        // Exactly, not approximately: the frame is `output * scale` in both axes by construction,
        // so any drift here means the two axes were sized independently -- which is the stretch
        // §4.3 forbids.
        REQUIRE(drawnAspect(frame) == Approx(outputAspect(c.outW, c.outH)).epsilon(1e-6));
        // Inside the canvas, and touching it in at least one axis: that is what "fit" means.
        REQUIRE(frame.width <= canvas.width + 1e-3f);
        REQUIRE(frame.height <= canvas.height + 1e-3f);
        const bool touchesX = std::abs(frame.width - canvas.width) < 1e-3f;
        const bool touchesY = std::abs(frame.height - canvas.height) < 1e-3f;
        REQUIRE((touchesX || touchesY));
        REQUIRE_FALSE(frame.overflows);
        // Centred.
        REQUIRE(frame.x + frame.width * 0.5f == Approx(canvas.x + canvas.width * 0.5f));
        REQUIRE(frame.y + frame.height * 0.5f == Approx(canvas.y + canvas.height * 0.5f));
    }
}

TEST_CASE("the frame is centred in the canvas wherever the canvas is", "[ui][preview][geometry]") {
    // The canvas is a docked ImGui window, so its origin is nowhere near (0,0) in a real session.
    // A frame computed from the canvas's *size* and placed at the canvas's *origin* would be a
    // whole panel's width out, and would look plausible in a screenshot.
    const CanvasRect canvas = canvasOf(800.0f, 600.0f, 273.0f, 48.0f);
    const PreviewFrame frame = fitOutputFrame(canvas, 1920, 1080, PreviewZoom{}, 1.0f);
    REQUIRE(frame.x == Approx(273.0f));                       // 800 / (16/9) = 450 high, so x touches
    REQUIRE(frame.width == Approx(800.0f));
    REQUIRE(frame.height == Approx(450.0f));
    REQUIRE(frame.y == Approx(48.0f + (600.0f - 450.0f) * 0.5f));
}

TEST_CASE("the frame geometry recomputes when a panel opens and the canvas shrinks",
          "[ui][preview][geometry]") {
    const PreviewFrame wide = fitOutputFrame(canvasOf(1200.0f, 700.0f), 1920, 1080, PreviewZoom{}, 1.0f);
    // A panel opens on the right: the canvas loses 300 points of width and moves nothing else.
    const PreviewFrame narrow = fitOutputFrame(canvasOf(900.0f, 700.0f), 1920, 1080, PreviewZoom{}, 1.0f);
    REQUIRE(narrow.width < wide.width);
    REQUIRE(drawnAspect(narrow) == Approx(drawnAspect(wide)).epsilon(1e-6));
    REQUIRE(narrow.width == Approx(900.0f)); // width-bound in both, so it tracks the canvas exactly
}

TEST_CASE("an unlaid-out canvas produces no frame rather than a degenerate one",
          "[ui][preview][geometry]") {
    // The first frame of a run, before ImGui has placed the canvas window. A zero-height frame
    // would divide by zero in every conversion below it.
    REQUIRE_FALSE(fitOutputFrame(canvasOf(0.0f, 0.0f), 1920, 1080, PreviewZoom{}, 1.0f).valid());
    REQUIRE_FALSE(fitOutputFrame(canvasOf(800.0f, 600.0f), 0, 1080, PreviewZoom{}, 1.0f).valid());
    REQUIRE_FALSE(fitOutputFrame(canvasOf(800.0f, 600.0f), 1920, 0, PreviewZoom{}, 1.0f).valid());
}

// ---- zoom (spec §8.2) ----------------------------------------------------------------------------

TEST_CASE("100% means one output pixel per device pixel", "[ui][preview][zoom]") {
    const CanvasRect canvas = canvasOf(1600.0f, 900.0f);

    // A 1x display: one output pixel is one point.
    const PreviewFrame one = fitOutputFrame(canvas, 1920, 1080, PreviewZoom{false, 1.0f}, 1.0f);
    REQUIRE(one.width == Approx(1920.0f));
    REQUIRE(one.height == Approx(1080.0f));

    // A 2x display: the same 1920 output pixels occupy 960 *points*, because a point is two device
    // pixels. Getting this wrong is how "100%" comes to mean "twice actual size" on a Retina
    // screen, which is the platform this project runs on.
    const PreviewFrame retina = fitOutputFrame(canvas, 1920, 1080, PreviewZoom{false, 1.0f}, 2.0f);
    REQUIRE(retina.width == Approx(960.0f));
    REQUIRE(retina.height == Approx(540.0f));

    const PreviewFrame half = fitOutputFrame(canvas, 1920, 1080, PreviewZoom{false, 0.5f}, 2.0f);
    REQUIRE(half.width == Approx(480.0f));
    const PreviewFrame twice = fitOutputFrame(canvas, 1920, 1080, PreviewZoom{false, 2.0f}, 2.0f);
    REQUIRE(twice.width == Approx(1920.0f));
    REQUIRE(twice.overflows); // 1920 points into a 1600-point canvas
}

TEST_CASE("zoom changes only the display, never the output or the aspect ratio", "[ui][preview][zoom]") {
    const CanvasRect canvas = canvasOf(1600.0f, 900.0f);
    const double wanted = outputAspect(1080, 1920);
    for (const float stop : zoomStops()) {
        INFO("zoom " << stop);
        const PreviewFrame frame = fitOutputFrame(canvas, 1080, 1920, PreviewZoom{false, stop}, 2.0f);
        REQUIRE(frame.valid());
        REQUIRE(drawnAspect(frame) == Approx(wanted).epsilon(1e-6));
        // Output pixels per point scales exactly with the zoom.
        REQUIRE(frame.pointsPerOutputPixel == Approx(stop / 2.0f));
    }
}

TEST_CASE("fit reports the scale it chose so the toolbar can say so", "[ui][preview][zoom]") {
    const CanvasRect canvas = canvasOf(960.0f, 540.0f);
    const PreviewFrame frame = fitOutputFrame(canvas, 1920, 1080, PreviewZoom{}, 2.0f);
    // 960 points at 2x is 1920 device pixels for 1920 output pixels: Fit here is exactly 100%.
    REQUIRE(frame.fittedScale == Approx(1.0f));
    REQUIRE(zoomLabel(PreviewZoom{}, frame.fittedScale) == "Fit (100%)");
    REQUIRE(zoomLabel(PreviewZoom{false, 0.5f}, frame.fittedScale) == "50%");

    // Half the canvas, half the fitted scale.
    const PreviewFrame smaller = fitOutputFrame(canvasOf(480.0f, 270.0f), 1920, 1080, PreviewZoom{}, 2.0f);
    REQUIRE(smaller.fittedScale == Approx(0.5f));
}

TEST_CASE("a frame that fits is centred whatever the pan says", "[ui][preview][zoom]") {
    // A stale pan left over from a 200% inspection must not nudge the frame once the user returns
    // to Fit -- which is what makes Fit reproducible rather than "wherever you left it".
    const CanvasRect canvas = canvasOf(1600.0f, 900.0f);
    const PreviewFrame panned = fitOutputFrame(canvas, 1920, 1080, PreviewZoom{}, 1.0f, 300.0f, -200.0f);
    const PreviewFrame unpanned = fitOutputFrame(canvas, 1920, 1080, PreviewZoom{}, 1.0f);
    REQUIRE(panned.x == Approx(unpanned.x));
    REQUIRE(panned.y == Approx(unpanned.y));
}

TEST_CASE("an overflowing frame pans, and cannot be thrown off screen", "[ui][preview][zoom]") {
    const CanvasRect canvas = canvasOf(800.0f, 400.0f);
    const PreviewZoom zoom{false, 1.0f};
    const PreviewFrame unpanned = fitOutputFrame(canvas, 1920, 1080, zoom, 1.0f);
    REQUIRE(unpanned.overflows);

    float panX = 100000.0f;
    float panY = -100000.0f;
    clampPreviewPan(canvas, unpanned, panX, panY);
    const PreviewFrame frame = fitOutputFrame(canvas, 1920, 1080, zoom, 1.0f, panX, panY);
    // Some of the frame is still on the canvas in both axes, so there is always something to grab.
    REQUIRE(frame.x < canvas.x + canvas.width);
    REQUIRE(frame.x + frame.width > canvas.x);
    REQUIRE(frame.y < canvas.y + canvas.height);
    REQUIRE(frame.y + frame.height > canvas.y);
    // And the pan did move it, so the clamp is a bound rather than a veto.
    REQUIRE(panX > 0.0f);
}

// ---- coordinate conversions (spec §10.2) --------------------------------------------------------

TEST_CASE("a click lands where the frame was drawn, in every letterbox", "[ui][preview][coords]") {
    // The hazard this exists for: the frame is letterboxed inside the canvas, so a conversion that
    // still divides by the *canvas* size puts every selection and every gizmo handle out by the
    // width of the letterbox. It looks almost right, which is why it survives a screenshot.
    struct Case {
        const char* what;
        float canvasW;
        float canvasH;
        std::uint32_t outW;
        std::uint32_t outH;
    };
    const Case cases[] = {
        {"landscape in a tall canvas (pillarbox above and below)", 800.0f, 800.0f, 1920, 1080},
        {"landscape in a wide canvas (bars left and right)", 1600.0f, 400.0f, 1920, 1080},
        {"portrait", 800.0f, 800.0f, 1080, 1920},
        {"square", 1600.0f, 400.0f, 1080, 1080},
    };
    for (const Case& c : cases) {
        INFO(c.what);
        const CanvasRect canvas = canvasOf(c.canvasW, c.canvasH, 120.0f, 64.0f);
        const PreviewFrame frame = fitOutputFrame(canvas, c.outW, c.outH, PreviewZoom{}, 1.0f);
        REQUIRE(frame.valid());

        // The four corners and the centre of the frame map to the four corners and the centre of
        // the output image.
        REQUIRE(frameUvFor(frame, frame.x, frame.y).u == Approx(0.0f));
        REQUIRE(frameUvFor(frame, frame.x, frame.y).v == Approx(0.0f));
        const FrameUv centre = frameUvFor(frame, frame.x + frame.width * 0.5f,
                                          frame.y + frame.height * 0.5f);
        REQUIRE(centre.u == Approx(0.5f));
        REQUIRE(centre.v == Approx(0.5f));
        REQUIRE(centre.inside());

        const CanvasPixel mid = outputPixelFor(frame, c.outW, c.outH, frame.x + frame.width * 0.5f,
                                               frame.y + frame.height * 0.5f);
        REQUIRE(mid.x == c.outW / 2);
        REQUIRE(mid.y == c.outH / 2);

        // A point in the letterbox is outside the frame and says so, rather than silently
        // clamping into a pixel that was never under the pointer.
        const FrameUv outside = frameUvFor(frame, canvas.x + 0.5f, canvas.y + 0.5f);
        if (frame.x > canvas.x + 1.0f || frame.y > canvas.y + 1.0f) {
            REQUIRE_FALSE(outside.inside());
        }
        // But asking for a pixel anyway clamps into the image rather than naming one outside it.
        const CanvasPixel clamped = outputPixelFor(frame, c.outW, c.outH, canvas.x - 500.0f,
                                                   canvas.y - 500.0f);
        REQUIRE(clamped.x == 0u);
        REQUIRE(clamped.y == 0u);
        const CanvasPixel far = outputPixelFor(frame, c.outW, c.outH, canvas.x + 99999.0f,
                                               canvas.y + 99999.0f);
        REQUIRE(far.x == c.outW - 1u);
        REQUIRE(far.y == c.outH - 1u);
    }
}

TEST_CASE("the conversion survives a zoom other than fit and a resize under it", "[ui][preview][coords]") {
    const CanvasRect canvas = canvasOf(700.0f, 500.0f, 40.0f, 40.0f);
    for (const float stop : zoomStops()) {
        INFO("zoom " << stop);
        const PreviewFrame frame = fitOutputFrame(canvas, 1920, 1080, PreviewZoom{false, stop}, 2.0f);
        const CanvasPixel mid = outputPixelFor(frame, 1920, 1080, frame.x + frame.width * 0.5f,
                                               frame.y + frame.height * 0.5f);
        REQUIRE(mid.x == 960u);
        REQUIRE(mid.y == 540u);
    }
    // And a canvas resize under a held zoom moves the frame but not the conversion's meaning.
    const PreviewFrame before = fitOutputFrame(canvas, 1920, 1080, PreviewZoom{false, 0.5f}, 2.0f);
    const PreviewFrame after =
        fitOutputFrame(canvasOf(400.0f, 500.0f, 40.0f, 40.0f), 1920, 1080, PreviewZoom{false, 0.5f}, 2.0f);
    REQUIRE(before.width == Approx(after.width)); // an explicit zoom does not track the canvas
    const CanvasPixel mid = outputPixelFor(after, 1920, 1080, after.x + after.width * 0.5f,
                                           after.y + after.height * 0.5f);
    REQUIRE(mid.x == 960u);
}

// ---- the render extent (spec §7.2, §7.3) ---------------------------------------------------------

TEST_CASE("the preview renders at the output's aspect ratio, never the canvas's",
          "[ui][preview][render]") {
    // This is the case the whole feature exists for. The editor's centre dock is about 1.41:1 and
    // the output is 1.78:1; a preview rendered at the canvas's extent has a different horizontal
    // field of view from the deliverable, because `Camera::projection(aspect)` holds the *vertical*
    // FOV fixed and widens horizontally with aspect. Same camera, different picture.
    const CanvasRect canvas = canvasOf(864.0f, 612.0f);
    const PreviewFrame frame = fitOutputFrame(canvas, 1920, 1080, PreviewZoom{}, 2.0f);
    for (const auto quality : {PreviewQuality::Draft, PreviewQuality::Realtime, PreviewQuality::Native}) {
        INFO(previewQualityName(quality));
        const PreviewRender render = previewRenderExtent(frame, 1920, 1080, 2.0f, quality);
        REQUIRE(render.width >= kMinOutputDimension);
        REQUIRE(render.height >= kMinOutputDimension);
        REQUIRE(render.aspectMatches);
        // Within a pixel of the deliverable's ratio, in the deliverable's own terms.
        REQUIRE(outputAspect(render.width, render.height) ==
                Approx(outputAspect(1920, 1080)).epsilon(1.0 / render.height));
        // And nothing here ever resembles the canvas's shape, which is the wrong answer.
        REQUIRE(outputAspect(render.width, render.height) !=
                Approx(static_cast<double>(canvas.width) / static_cast<double>(canvas.height)).epsilon(0.01));
    }
}

TEST_CASE("every preview quality rung is a different real extent", "[ui][preview][render]") {
    // §7.3: "Do not present a quality option that merely changes a label or performs a no-op."
    const CanvasRect canvas = canvasOf(960.0f, 540.0f);
    const PreviewFrame frame = fitOutputFrame(canvas, 1920, 1080, PreviewZoom{}, 2.0f);

    const PreviewRender draft = previewRenderExtent(frame, 1920, 1080, 2.0f, PreviewQuality::Draft);
    const PreviewRender realtime = previewRenderExtent(frame, 1920, 1080, 2.0f, PreviewQuality::Realtime);
    const PreviewRender native = previewRenderExtent(frame, 1920, 1080, 2.0f, PreviewQuality::Native);

    REQUIRE(draft.width < realtime.width);
    REQUIRE(realtime.width <= native.width);
    REQUIRE(draft.width == Approx(static_cast<float>(realtime.width) * 0.5f).margin(2.0));

    REQUIRE(native.nativeOutput);
    REQUIRE(native.width == 1920u);
    REQUIRE(native.height == 1080u);
    REQUIRE(native.scaleFromOutput == Approx(1.0f));
    REQUIRE(native.describe(1920, 1080) == "1920x1080");

    REQUIRE_FALSE(draft.nativeOutput);
    REQUIRE(draft.scaleFromOutput < 1.0f);
    // §8.1: "Clear indication when preview is rendering at reduced resolution or quality."
    REQUIRE(draft.describe(1920, 1080).find("% of output") != std::string::npos);
}

TEST_CASE("the preview never asks for more pixels than the deliverable", "[ui][preview][render]") {
    // A frame zoomed to 200% on a 2x display is 4x the output's pixels on screen. Rendering that
    // many would be supersampling, which is ADR-212's business and an offline render's -- not an
    // interactive preview's, and not something to start doing because a zoom control was dragged.
    const CanvasRect canvas = canvasOf(4000.0f, 3000.0f);
    const PreviewFrame frame = fitOutputFrame(canvas, 1920, 1080, PreviewZoom{false, 2.0f}, 2.0f);
    const PreviewRender render = previewRenderExtent(frame, 1920, 1080, 2.0f, PreviewQuality::Realtime);
    REQUIRE(render.width <= 1920u);
    REQUIRE(render.height <= 1080u);
    REQUIRE(render.scaleFromOutput <= 1.0f);
}

TEST_CASE("the render extent is even in both axes", "[ui][preview][render]") {
    // Not cosmetic. `SceneRenderer::resize` rounds each axis to even before allocating the HDR
    // target and takes the projection's aspect ratio from *that* target, so an odd extent is
    // silently nudged by a pixel -- and a pixel of nudge is a different framing from the one the
    // preview claims to be showing.
    const CanvasRect canvas = canvasOf(733.0f, 411.0f);
    for (const auto quality : {PreviewQuality::Draft, PreviewQuality::Realtime, PreviewQuality::Native}) {
        for (const auto out : {std::pair{1920u, 1080u}, std::pair{1080u, 1920u}, std::pair{1080u, 1080u},
                               std::pair{1280u, 720u}, std::pair{3840u, 2160u}}) {
            INFO(previewQualityName(quality) << " at " << out.first << "x" << out.second);
            const PreviewFrame frame = fitOutputFrame(canvas, out.first, out.second, PreviewZoom{}, 2.0f);
            const PreviewRender render = previewRenderExtent(frame, out.first, out.second, 2.0f, quality);
            REQUIRE(render.width % 2u == 0u);
            REQUIRE(render.height % 2u == 0u);
            REQUIRE(render.aspectMatches);
        }
    }
}

TEST_CASE("a budget that cannot hold the output still holds its shape", "[ui][preview][render]") {
    // A 4K output previewed on a device whose ceiling is 2048: both axes cannot simply be clamped,
    // because clamping one of them is a different aspect ratio and therefore a different framing.
    const CanvasRect canvas = canvasOf(3000.0f, 2000.0f);
    const PreviewFrame frame = fitOutputFrame(canvas, 3840, 2160, PreviewZoom{}, 2.0f);
    const PreviewRender render =
        previewRenderExtent(frame, 3840, 2160, 2.0f, PreviewQuality::Native, 2048);
    REQUIRE(render.width <= 2048u);
    REQUIRE(render.height <= 2048u);
    REQUIRE(render.aspectMatches);
    REQUIRE(outputAspect(render.width, render.height) == Approx(16.0 / 9.0).epsilon(0.002));
    REQUIRE_FALSE(render.nativeOutput); // and it says it is no longer showing the output's size
}

TEST_CASE("an unlaid-out canvas still produces a usable preview extent", "[ui][preview][render]") {
    const PreviewRender render = previewRenderExtent(PreviewFrame{}, 1920, 1080, 2.0f,
                                                     PreviewQuality::Realtime);
    REQUIRE(render.width >= kMinOutputDimension);
    REQUIRE(render.aspectMatches);
    REQUIRE(previewRenderExtent(PreviewFrame{}, 0, 0, 2.0f, PreviewQuality::Realtime).width == 0u);
}

// ---- safe areas and guides (spec §9) -------------------------------------------------------------

TEST_CASE("safe areas are the same fraction of each axis in every format", "[ui][preview][guides]") {
    // SMPTE ST 2046-1 / EBU R 95: action-safe is the centred 93% and title-safe the centred 90% of
    // *each axis*. A fixed inset in points, or a fraction of the long side, would be right in
    // landscape and wrong in portrait -- which is the mistake §9.1 warns about by name.
    const SafeAreaSettings safe;
    REQUIRE(safe.validate().has_value());

    for (const auto out : {std::pair{1920u, 1080u}, std::pair{1080u, 1920u}, std::pair{1080u, 1080u}}) {
        INFO(out.first << "x" << out.second);
        const PreviewFrame frame =
            fitOutputFrame(canvasOf(900.0f, 700.0f, 30.0f, 70.0f), out.first, out.second, PreviewZoom{}, 1.0f);
        const GuideRect action = insetFrame(frame, safe.actionFraction);
        const GuideRect title = insetFrame(frame, safe.titleFraction);

        REQUIRE(action.width == Approx(frame.width * 0.93f));
        REQUIRE(action.height == Approx(frame.height * 0.93f));
        REQUIRE(title.width == Approx(frame.width * 0.90f));
        // Concentric with the frame, all three.
        REQUIRE(action.x + action.width * 0.5f == Approx(frame.x + frame.width * 0.5f));
        REQUIRE(action.y + action.height * 0.5f == Approx(frame.y + frame.height * 0.5f));
        REQUIRE(title.x + title.width * 0.5f == Approx(frame.x + frame.width * 0.5f));
        // Title inside action inside frame, always.
        REQUIRE(title.x > action.x);
        REQUIRE(action.x > frame.x);
        REQUIRE(title.x + title.width < action.x + action.width);
    }
}

TEST_CASE("a safe-area setting that is not a safe area is refused", "[ui][preview][guides]") {
    SafeAreaSettings safe;
    safe.actionFraction = 0.0f;
    REQUIRE_FALSE(safe.validate().has_value());
    safe.actionFraction = 1.5f;
    REQUIRE_FALSE(safe.validate().has_value());
    // Title outside action is the one that would draw two boxes in the wrong order and look fine.
    safe = SafeAreaSettings{};
    safe.titleFraction = 0.97f;
    const auto inverted = safe.validate();
    REQUIRE_FALSE(inverted.has_value());
    REQUIRE(inverted.error().message.find("inside") != std::string::npos);

    // And a whole view state is validated through it.
    PreviewViewState state;
    REQUIRE(validatePreviewViewState(state).has_value());
    state.guides.safe.titleFraction = 0.99f;
    REQUIRE_FALSE(validatePreviewViewState(state).has_value());
    state = PreviewViewState{};
    state.zoom = PreviewZoom{false, 0.0f};
    REQUIRE_FALSE(validatePreviewViewState(state).has_value());
    state.zoom = PreviewZoom{false, 64.0f};
    REQUIRE_FALSE(validatePreviewViewState(state).has_value());
}

TEST_CASE("guides scale with the frame rather than with the canvas", "[ui][preview][guides]") {
    const PreviewFrame big = fitOutputFrame(canvasOf(1600.0f, 900.0f), 1920, 1080, PreviewZoom{}, 1.0f);
    const PreviewFrame small = fitOutputFrame(canvasOf(800.0f, 450.0f), 1920, 1080, PreviewZoom{}, 1.0f);
    const GuideRect bigSafe = insetFrame(big, 0.9f);
    const GuideRect smallSafe = insetFrame(small, 0.9f);
    REQUIRE(bigSafe.width == Approx(smallSafe.width * 2.0f));
    // The *proportion* is what is invariant, which is what makes the guide mean the same thing at
    // any zoom.
    REQUIRE(bigSafe.width / big.width == Approx(smallSafe.width / small.width));
}

// ---- names and view state ------------------------------------------------------------------------

TEST_CASE("every preview enumerator has a name that round-trips", "[ui][preview][state]") {
    for (const auto mode : {PreviewViewMode::Workspace, PreviewViewMode::OutputFrame,
                            PreviewViewMode::OutputPreview}) {
        PreviewViewMode back{};
        REQUIRE(previewViewModeFromName(previewViewModeName(mode), back));
        REQUIRE(back == mode);
        REQUIRE(std::string_view(previewViewModeLabel(mode)).size() > 0);
    }
    for (const auto outside : {OutsideFrame::Show, OutsideFrame::Dim, OutsideFrame::Hide}) {
        OutsideFrame back{};
        REQUIRE(outsideFrameFromName(outsideFrameName(outside), back));
        REQUIRE(back == outside);
    }
    for (const auto quality : {PreviewQuality::Draft, PreviewQuality::Realtime, PreviewQuality::Native}) {
        PreviewQuality back{};
        REQUIRE(previewQualityFromName(previewQualityName(quality), back));
        REQUIRE(back == quality);
    }
    PreviewViewMode mode{};
    REQUIRE_FALSE(previewViewModeFromName("a mode nobody wrote", mode));

    // Workspace is the only mode that leaves the canvas alone. That predicate is what the host
    // branches on, so it is worth pinning rather than re-deriving at each call site.
    REQUIRE_FALSE(modeShowsOutputFrame(PreviewViewMode::Workspace));
    REQUIRE(modeShowsOutputFrame(PreviewViewMode::OutputFrame));
    REQUIRE(modeShowsOutputFrame(PreviewViewMode::OutputPreview));
}

TEST_CASE("the editor opens in the workspace it has always opened in", "[ui][preview][state]") {
    // The flexible canvas is the default and stays the default (spec §4.1, §1.2). A build that
    // shipped with the output frame on by default would have changed the editor for everyone who
    // never asked for this feature.
    const PreviewViewState fresh;
    REQUIRE(fresh.mode == PreviewViewMode::Workspace);
    REQUIRE_FALSE(fresh.showsOutputFrame());
    REQUIRE(fresh.zoom.fit);
    REQUIRE_FALSE(fresh.fullscreen);
    REQUIRE_FALSE(fresh.guides.safeAreas);
    REQUIRE_FALSE(fresh.guides.thirds);
}

TEST_CASE("the presets cover the formats the spec names, and all of them validate",
          "[ui][preview][output]") {
    bool fullHd = false;
    bool uhd = false;
    bool hd = false;
    bool vertical = false;
    bool square = false;
    for (const OutputPreset& preset : outputPresets()) {
        INFO(preset.label);
        REQUIRE_FALSE(preset.label.empty());
        REQUIRE(validateOutputResolution(preset.width, preset.height).has_value());
        // Even in both axes, so a preset can be rendered to video without being refused.
        REQUIRE(preset.width % 2u == 0u);
        REQUIRE(preset.height % 2u == 0u);
        fullHd = fullHd || (preset.width == 1920 && preset.height == 1080);
        uhd = uhd || (preset.width == 3840 && preset.height == 2160);
        hd = hd || (preset.width == 1280 && preset.height == 720);
        vertical = vertical || (preset.width == 1080 && preset.height == 1920);
        square = square || (preset.width == 1080 && preset.height == 1080);
    }
    REQUIRE(fullHd);
    REQUIRE(uhd);
    REQUIRE(hd);
    REQUIRE(vertical);
    REQUIRE(square);
}
