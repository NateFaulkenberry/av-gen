#include "ui/output_preview.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

namespace avgen::ui {

namespace {

// Even in both axes, for a reason that is not cosmetic. `SceneRenderer::resize` scales the caller's
// extent by `QualitySettings::renderScale` and *rounds each axis to even* before allocating the HDR
// target, and the projection's aspect ratio is taken from that target rather than from the extent
// the caller asked for (scene_renderer.cpp, `aspect = hdr_.width() / hdr_.height()`). An odd extent
// is therefore silently nudged by a pixel, and a pixel of nudge is a different aspect ratio and a
// different framing from the one the preview is claiming to show. Asking for an even extent in the
// first place is how the claim stays true.
//
// The video encoders want even dimensions too (h264 chroma subsampling), which is why
// `RenderSettings::validate` already refuses an odd size for a video render -- the same rule,
// arrived at from the other end.
[[nodiscard]] std::uint32_t evenClamp(double value, std::uint32_t lo, std::uint32_t hi) {
    const double rounded = std::floor(value * 0.5 + 0.5) * 2.0;
    const double bounded = std::clamp(rounded, static_cast<double>(lo), static_cast<double>(hi));
    return static_cast<std::uint32_t>(bounded);
}

constexpr std::array<OutputPreset, 5> kPresets{{
    {"Full HD", 1920, 1080},
    {"4K UHD", 3840, 2160},
    {"HD", 1280, 720},
    {"Vertical", 1080, 1920},
    {"Square", 1080, 1080},
}};

// Output pixels per device pixel. 1.0 is "100%" and means exactly what §8.2 says it means.
constexpr std::array<float, 4> kZoomStops{0.25f, 0.5f, 1.0f, 2.0f};

} // namespace

// ---- names --------------------------------------------------------------------------------------

const char* previewViewModeName(PreviewViewMode mode) {
    switch (mode) {
    case PreviewViewMode::Workspace: return "workspace";
    case PreviewViewMode::OutputFrame: return "outputFrame";
    case PreviewViewMode::OutputPreview: return "outputPreview";
    }
    return "workspace";
}

const char* previewViewModeLabel(PreviewViewMode mode) {
    switch (mode) {
    case PreviewViewMode::Workspace: return "Workspace";
    case PreviewViewMode::OutputFrame: return "Output Frame";
    case PreviewViewMode::OutputPreview: return "Preview";
    }
    return "Workspace";
}

bool previewViewModeFromName(std::string_view name, PreviewViewMode& out) {
    for (const auto mode : {PreviewViewMode::Workspace, PreviewViewMode::OutputFrame,
                            PreviewViewMode::OutputPreview}) {
        if (name == previewViewModeName(mode)) {
            out = mode;
            return true;
        }
    }
    return false;
}

const char* outsideFrameName(OutsideFrame outside) {
    switch (outside) {
    case OutsideFrame::Show: return "show";
    case OutsideFrame::Dim: return "dim";
    case OutsideFrame::Hide: return "hide";
    }
    return "dim";
}

bool outsideFrameFromName(std::string_view name, OutsideFrame& out) {
    for (const auto outside : {OutsideFrame::Show, OutsideFrame::Dim, OutsideFrame::Hide}) {
        if (name == outsideFrameName(outside)) {
            out = outside;
            return true;
        }
    }
    return false;
}

const char* previewQualityName(PreviewQuality quality) {
    switch (quality) {
    case PreviewQuality::Draft: return "draft";
    case PreviewQuality::Realtime: return "realtime";
    case PreviewQuality::Native: return "native";
    }
    return "realtime";
}

const char* previewQualityLabel(PreviewQuality quality) {
    switch (quality) {
    case PreviewQuality::Draft: return "Draft";
    case PreviewQuality::Realtime: return "Realtime";
    case PreviewQuality::Native: return "Output resolution";
    }
    return "Realtime";
}

bool previewQualityFromName(std::string_view name, PreviewQuality& out) {
    for (const auto quality : {PreviewQuality::Draft, PreviewQuality::Realtime, PreviewQuality::Native}) {
        if (name == previewQualityName(quality)) {
            out = quality;
            return true;
        }
    }
    return false;
}

std::span<const OutputPreset> outputPresets() { return {kPresets.data(), kPresets.size()}; }
std::span<const float> zoomStops() { return {kZoomStops.data(), kZoomStops.size()}; }

// ---- resolution ---------------------------------------------------------------------------------

Result<void> validateOutputResolution(std::uint64_t width, std::uint64_t height,
                                      std::uint32_t maxDimension) {
    const std::uint64_t cap =
        std::clamp<std::uint64_t>(maxDimension, kMinOutputDimension, kMaxOutputDimension);
    // Taken as 64-bit so that a value typed into the width box which has already overflowed 32 bits
    // arrives here as the number the person typed rather than as its wrapped remainder. §5.3 asks
    // for overflow to be *handled*, and a check written in the type that can overflow cannot do it.
    if (width == 0 || height == 0) {
        return fail("output resolution must be at least {}x{}: a zero dimension has no pixels",
                    kMinOutputDimension, kMinOutputDimension);
    }
    if (width < kMinOutputDimension || height < kMinOutputDimension) {
        return fail("output resolution {}x{} is below the {} px minimum in one axis; several render "
                    "passes halve their extent twice and would reach zero",
                    width, height, kMinOutputDimension);
    }
    if (width > cap || height > cap) {
        return fail("output resolution {}x{} exceeds this renderer's {} px per-axis maximum "
                    "(the adapter's WebGPU maxTextureDimension2D)",
                    width, height, cap);
    }
    // Both are <= `cap` by here, so the product cannot overflow 64 bits and this is a budget rather
    // than an overflow guard; it is stated anyway because the limit a person meets in practice is
    // memory, not the texture dimension.
    if (width * height > cap * cap) {
        return fail("output resolution {}x{} is more pixels than this build will allocate", width, height);
    }
    return {};
}

double outputAspect(std::uint32_t width, std::uint32_t height) {
    if (height == 0) {
        return 0.0;
    }
    return static_cast<double>(width) / static_cast<double>(height);
}

std::string aspectLabel(std::uint32_t width, std::uint32_t height) {
    if (width == 0 || height == 0) {
        return "-";
    }
    const std::uint32_t g = std::gcd(width, height);
    const std::uint32_t w = width / g;
    const std::uint32_t h = height / g;
    // A reduced ratio is only useful while it is one a person recognises. Cutting on the reduced
    // *height* is what separates them: 16:9, 9:16, 4:3, 1:1, 21:9 and 2:1 all land at 16 or below,
    // while 1920x816 reduces to 40:17 -- arithmetically correct and meaningless to read. The
    // decimal form is what a camera department would say about that frame anyway.
    if (h <= 16 && w <= 64) {
        return fmt::format("{}:{}", w, h);
    }
    return fmt::format("{:.2f}:1", outputAspect(width, height));
}

std::string zoomLabel(const PreviewZoom& zoom, float fittedScale) {
    if (zoom.fit) {
        return fmt::format("Fit ({:.0f}%)", static_cast<double>(fittedScale) * 100.0);
    }
    return fmt::format("{:.0f}%", static_cast<double>(zoom.scale) * 100.0);
}

// ---- frame geometry -----------------------------------------------------------------------------

PreviewFrame previewFrameFromCanvas(const CanvasRect& canvas) {
    PreviewFrame frame;
    frame.x = canvas.x;
    frame.y = canvas.y;
    frame.width = canvas.width;
    frame.height = canvas.height;
    frame.pointsPerOutputPixel = 1.0f;
    frame.fittedScale = 1.0f;
    return frame;
}

PreviewFrame fitOutputFrame(const CanvasRect& canvas, std::uint32_t outputWidth,
                            std::uint32_t outputHeight, const PreviewZoom& zoom, float pixelScale,
                            float panX, float panY) {
    PreviewFrame frame;
    if (!canvas.valid() || outputWidth == 0 || outputHeight == 0) {
        return frame;
    }
    const float scale = std::max(pixelScale, 1e-3f);
    const float ow = static_cast<float>(outputWidth);
    const float oh = static_cast<float>(outputHeight);

    // Fit, in *points*: the largest uniform scale at which the whole frame is inside the canvas.
    // Uniform is what preserves the aspect ratio exactly -- taking the two axes separately is
    // precisely the stretch §4.3 forbids.
    const float fitPoints = std::min(canvas.width / ow, canvas.height / oh);
    // Reported in the zoom's own units (output pixels per device pixel) so the toolbar can say
    // "Fit (42%)" in the same vocabulary as the 25/50/100/200 stops.
    frame.fittedScale = fitPoints * scale;

    const float points = zoom.fit ? fitPoints : std::max(zoom.scale, 1e-4f) / scale;
    frame.pointsPerOutputPixel = points;
    frame.width = ow * points;
    frame.height = oh * points;
    frame.overflows = frame.width > canvas.width + 0.5f || frame.height > canvas.height + 0.5f;

    // Centred, always, and panned only where there is something off screen to pan to. A frame that
    // fits therefore lands in exactly one place whatever the pan happens to be, which is what makes
    // Fit reproducible and what keeps a stale pan from nudging the frame after a window resize.
    const float centreX = canvas.x + (canvas.width - frame.width) * 0.5f;
    const float centreY = canvas.y + (canvas.height - frame.height) * 0.5f;
    frame.x = centreX + (frame.overflows ? panX : 0.0f);
    frame.y = centreY + (frame.overflows ? panY : 0.0f);
    return frame;
}

void clampPreviewPan(const CanvasRect& canvas, const PreviewFrame& unpanned, float& panX, float& panY,
                     float keepPoints) {
    if (!canvas.valid() || !unpanned.valid()) {
        panX = 0.0f;
        panY = 0.0f;
        return;
    }
    // The frame may be dragged until `keepPoints` of it is all that is left inside the canvas, and
    // no further: a frame dragged entirely off screen is a frame the user has to guess their way
    // back to, and there is no scrollbar here to tell them where it went.
    const auto limit = [&](float canvasExtent, float frameExtent) {
        const float half = std::abs(canvasExtent - frameExtent) * 0.5f;
        const float keep = std::max(0.0f, std::min(canvasExtent, frameExtent) - keepPoints);
        return half + keep;
    };
    const float limitX = limit(canvas.width, unpanned.width);
    const float limitY = limit(canvas.height, unpanned.height);
    panX = std::clamp(panX, -limitX, limitX);
    panY = std::clamp(panY, -limitY, limitY);
}

// ---- conversions --------------------------------------------------------------------------------

FrameUv frameUvFor(const PreviewFrame& frame, float windowX, float windowY) {
    FrameUv uv;
    if (!frame.valid()) {
        return uv;
    }
    uv.u = (windowX - frame.x) / frame.width;
    uv.v = (windowY - frame.y) / frame.height;
    return uv;
}

CanvasPixel outputPixelFor(const PreviewFrame& frame, std::uint32_t outputWidth,
                           std::uint32_t outputHeight, float windowX, float windowY) {
    CanvasPixel pixel;
    if (!frame.valid() || outputWidth == 0 || outputHeight == 0) {
        return pixel;
    }
    const FrameUv uv = frameUvFor(frame, windowX, windowY);
    const float px = std::clamp(uv.u, 0.0f, 1.0f) * static_cast<float>(outputWidth);
    const float py = std::clamp(uv.v, 0.0f, 1.0f) * static_cast<float>(outputHeight);
    pixel.x = std::min(outputWidth - 1u, static_cast<std::uint32_t>(px));
    pixel.y = std::min(outputHeight - 1u, static_cast<std::uint32_t>(py));
    return pixel;
}

// ---- render extent ------------------------------------------------------------------------------

std::string PreviewRender::describe(std::uint32_t outputWidth, std::uint32_t outputHeight) const {
    if (nativeOutput) {
        return fmt::format("{}x{}", width, height);
    }
    static_cast<void>(outputWidth);
    static_cast<void>(outputHeight);
    return fmt::format("{}x{} ({:.0f}% of output)", width, height,
                       static_cast<double>(scaleFromOutput) * 100.0);
}

PreviewRender previewRenderExtent(const PreviewFrame& frame, std::uint32_t outputWidth,
                                  std::uint32_t outputHeight, float pixelScale, PreviewQuality quality,
                                  std::uint32_t maxDimension) {
    PreviewRender out;
    if (outputWidth == 0 || outputHeight == 0) {
        return out;
    }
    const std::uint32_t cap = std::clamp(maxDimension, kMinOutputDimension, kMaxOutputDimension);

    // `k` is the one number: the extent is always `output * k`, in both axes, so the aspect ratio
    // of what is rendered is the aspect ratio of what will be delivered by construction. Nothing
    // here is allowed to size the two axes independently -- that is how a preview starts showing a
    // framing the render will not produce.
    double k = 1.0;
    if (quality == PreviewQuality::Native) {
        k = 1.0;
    } else {
        // The frame as it is actually displayed, in device pixels. A small preview costs a small
        // preview's pixels (spec §7.2, approach A); Draft halves each axis on top of that.
        const double displayedWidth = static_cast<double>(frame.valid() ? frame.width : 0.0f) *
                                      static_cast<double>(std::max(pixelScale, 1e-3f));
        if (displayedWidth < 1.0) {
            // No frame laid out yet (the first frame of a run). Half of the output is a defensible
            // starting point and is replaced the moment ImGui reports a canvas.
            k = 0.5;
        } else {
            k = displayedWidth / static_cast<double>(outputWidth);
        }
        if (quality == PreviewQuality::Draft) {
            k *= 0.5;
        }
        // Never more pixels than the deliverable: above 1.0 the preview would be supersampling,
        // which is ADR-212's business and an offline render's, not an interactive preview's.
        k = std::clamp(k, 0.0, 1.0);
    }

    out.width = evenClamp(static_cast<double>(outputWidth) * k, kMinOutputDimension, cap);
    out.height = evenClamp(static_cast<double>(outputHeight) * k, kMinOutputDimension, cap);
    // Both axes are clamped independently above, and a clamp that fires in one axis only would
    // change the aspect ratio. Re-derive the other axis from the one that was clamped so the ratio
    // survives -- and then say so if even that could not hold it.
    if (out.width == cap && outputWidth > outputHeight) {
        out.height = evenClamp(static_cast<double>(cap) * static_cast<double>(outputHeight) /
                                   static_cast<double>(outputWidth),
                               kMinOutputDimension, cap);
    } else if (out.height == cap && outputHeight > outputWidth) {
        out.width = evenClamp(static_cast<double>(cap) * static_cast<double>(outputWidth) /
                                  static_cast<double>(outputHeight),
                              kMinOutputDimension, cap);
    }

    out.nativeOutput = out.width == outputWidth && out.height == outputHeight;
    out.scaleFromOutput = static_cast<float>(out.height) / static_cast<float>(outputHeight);

    // The honesty check, computed rather than asserted. Rounding to an even extent moves the aspect
    // ratio by at most one pixel in the shorter axis, so the tolerance is one part in the height;
    // anything larger means the clamps above have distorted the frame and the toolbar must say so
    // instead of showing a picture that is framed differently from the render.
    const double wanted = outputAspect(outputWidth, outputHeight);
    const double got = outputAspect(out.width, out.height);
    const double tolerance = wanted * 2.0 / std::max(1.0, static_cast<double>(out.height));
    out.aspectMatches = std::abs(got - wanted) <= tolerance;
    return out;
}

// ---- guides -------------------------------------------------------------------------------------

Result<void> SafeAreaSettings::validate() const {
    if (!(actionFraction > 0.0f) || actionFraction > 1.0f) {
        return fail("the action-safe fraction must be in (0, 1]; {} is not", actionFraction);
    }
    if (!(titleFraction > 0.0f) || titleFraction > 1.0f) {
        return fail("the title-safe fraction must be in (0, 1]; {} is not", titleFraction);
    }
    if (titleFraction > actionFraction) {
        return fail("the title-safe area ({:.0f}%) must sit inside the action-safe area ({:.0f}%)",
                    static_cast<double>(titleFraction) * 100.0,
                    static_cast<double>(actionFraction) * 100.0);
    }
    return {};
}

GuideRect insetFrame(const PreviewFrame& frame, float fraction) {
    GuideRect rect;
    if (!frame.valid()) {
        return rect;
    }
    const float f = std::clamp(fraction, 0.0f, 1.0f);
    // A fraction of *each axis*, centred. The same rule in both axes is what makes the guide
    // correct for 9:16 and 1:1 without a second table: the inset is 3.5% of whichever axis it is
    // measured on, which is what ST 2046-1 specifies and is not the same as a fixed number of
    // pixels or a fraction of the long side.
    rect.width = frame.width * f;
    rect.height = frame.height * f;
    rect.x = frame.x + (frame.width - rect.width) * 0.5f;
    rect.y = frame.y + (frame.height - rect.height) * 0.5f;
    return rect;
}

ToolbarOrigin previewToolbarOrigin(const CanvasRect& canvas, float toolbarWidth, float toolbarHeight,
                                   float margin) {
    ToolbarOrigin origin;
    if (!canvas.valid()) {
        return origin;
    }
    const float m = std::max(margin, 0.0f);
    const float w = std::max(toolbarWidth, 0.0f);
    const float h = std::max(toolbarHeight, 0.0f);
    // Centred on the canvas, then pinned to the left margin if it does not fit. `std::max` rather
    // than a clamp of the centred value: the two differ only when the bar is wider than the canvas,
    // and that is exactly the case that has to resolve towards the left so the collapse button
    // stays on screen.
    origin.x = std::max(canvas.x + m, canvas.x + (canvas.width - w) * 0.5f);
    // The bottom edge, inset by the same margin. Pinned to the top if the canvas is shorter than
    // the bar, for the same reason: a bar whose controls are above the canvas cannot be reached.
    origin.y = std::max(canvas.y + m, canvas.y + canvas.height - h - m);
    return origin;
}

Result<void> validatePreviewViewState(const PreviewViewState& state) {
    if (auto r = state.guides.safe.validate(); !r) {
        return r;
    }
    if (!state.zoom.fit && !(state.zoom.scale > 0.0f)) {
        return fail("a zoom scale must be positive; {} is not", state.zoom.scale);
    }
    if (!state.zoom.fit && state.zoom.scale > 8.0f) {
        return fail("a zoom scale of {:.0f}% is beyond what this editor will display",
                    static_cast<double>(state.zoom.scale) * 100.0);
    }
    return {};
}

} // namespace avgen::ui
