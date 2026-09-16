#pragma once

// The output preview: where the deliverable's frame goes inside the flexible canvas, and what the
// editor draws over it (output-preview spec §4, §5, §8, §9, §10.2).
//
// ## What this file is, and what it deliberately is not
//
// It is arithmetic. No ImGui, no GPU, no scene. That is the same rule `editor_layout.hpp` states
// and for the same reason: the part of a viewport feature that goes quietly wrong is the geometry
// -- a frame off by the width of a border, a click that lands two pixels from where the gizmo was
// drawn, a safe-area box that is right in landscape and wrong in portrait -- and a rule that needs
// a window and a device to check is a rule nobody checks. Every number the preview draws with and
// every conversion a click goes through is computed here, where a test can reach it.
//
// It is **not** a second output configuration. The deliverable's resolution already lives in
// `app::RenderSettings` (project JSON, key "render"), and that is the only place it lives; this
// file is handed a width and a height and never stores one. It is not a second camera either: it
// never mentions a camera at all. The preview's camera is whatever `engine.scene().camera` holds
// this frame, read at the single seam `Application::previewCamera()`; see ADR-246.
//
// ## The one idea
//
// The renderer takes its aspect ratio from the extent it is told to render at -- `Camera::
// projection(aspect)` and `Composition::setViewport` are the only two places it enters, and both
// are fed from `Application`'s `renderWidth_`/`renderHeight_`. So a preview that renders at an
// extent with the *output's* aspect ratio is not an approximation of the render's framing: it is
// the render's framing, computed by the same code from the same camera. Nothing is stretched and
// nothing is cropped. What changes between the preview and the deliverable is how many pixels the
// picture is sampled at, which this file reports rather than hides (`PreviewRender::scaleFromOutput`
// and `PreviewRender::aspectMatches`).
//
// The alternative -- render at the canvas's aspect and draw a rectangle over it -- is exact only
// when the canvas is *wider* than the output, because the vertical field of view does not move
// with aspect. The editor canvas is a centre dock roughly 1.4:1, narrower than 16:9, so that
// approach would have been wrong in the ordinary case. See ADR-246.

#include "core/error.hpp"
#include "ui/editor_layout.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace avgen::ui {

// ---- view mode (spec §4.2) ---------------------------------------------------------------------
//
// One canvas, three presentations. Workspace is exactly what the editor did before this existed:
// the world is rendered at the canvas's own size and fills it. The other two render at the
// output's aspect ratio and place that frame inside the canvas.
enum class PreviewViewMode : std::uint8_t {
    Workspace,    // the flexible canvas, unchanged
    OutputFrame,  // the output frame inside the canvas, editing and overlays intact
    OutputPreview // the output frame as the subject, editor overlays suppressed
};

[[nodiscard]] const char* previewViewModeName(PreviewViewMode mode);
[[nodiscard]] const char* previewViewModeLabel(PreviewViewMode mode); // for the toolbar
[[nodiscard]] bool previewViewModeFromName(std::string_view name, PreviewViewMode& out);
// True when this mode places a frame with the output's aspect ratio inside the canvas -- i.e.
// everything except Workspace. The one predicate the host branches on.
[[nodiscard]] constexpr bool modeShowsOutputFrame(PreviewViewMode mode) {
    return mode != PreviewViewMode::Workspace;
}

// What the canvas outside the output frame looks like (spec §4.4). Always an editor presentation:
// none of it reaches the render target, so none of it can reach a deliverable.
enum class OutsideFrame : std::uint8_t {
    Show, // the plain canvas ground, as any letterbox
    Dim,  // darkened, so the eye goes to the frame
    Hide  // filled flat; the preview is the only thing on screen
};
[[nodiscard]] const char* outsideFrameName(OutsideFrame outside);
[[nodiscard]] bool outsideFrameFromName(std::string_view name, OutsideFrame& out);

// How many pixels the preview is rendered at, as against how big it is displayed (spec §7.3).
// Every rung is a real render-target extent -- there is deliberately no rung that changes only a
// label.
enum class PreviewQuality : std::uint8_t {
    Draft,    // half the displayed frame's device pixels in each axis
    Realtime, // one render pixel per displayed device pixel
    Native    // exactly the configured output resolution, displayed scaled
};
[[nodiscard]] const char* previewQualityName(PreviewQuality quality);
[[nodiscard]] const char* previewQualityLabel(PreviewQuality quality);
[[nodiscard]] bool previewQualityFromName(std::string_view name, PreviewQuality& out);

// ---- output resolution (spec §5.2, §5.3) --------------------------------------------------------

struct OutputPreset {
    std::string_view label; // "Full HD"
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};
// The presets the toolbar offers, in menu order. Editor/project data, never renderer data: the
// renderer has never heard of "1080p" and must not start to.
[[nodiscard]] std::span<const OutputPreset> outputPresets();

// The per-axis ceiling this editor applies when nothing better is known. 16384 rather than a
// number of its own, because `app::RenderSettings::validate` has always refused above 16384 and a
// preview that accepts a resolution the render will then refuse is worse than no check at all.
//
// It is a fallback, not the answer: the real ceiling is the adapter's reported
// `maxTextureDimension2D`, which the host passes in from `gpu::Capabilities::limits`. §5.3 asks for
// the renderer-specific maximum to be explicit to the user, and a constant compiled in last month
// is not that.
inline constexpr std::uint32_t kMaxOutputDimension = 16384;
// Refused below this in either axis: a 0- or 1-pixel target is not a picture, and several passes
// halve their extent twice.
inline constexpr std::uint32_t kMinOutputDimension = 16;

// Why a resolution was refused, in words a person can act on (spec §5.3: "Display actionable
// validation errors", "Make any renderer-specific maximum explicit to the user"). Success carries
// no message. `maxDimension` is the device's reported per-axis texture limit where the caller has
// one.
[[nodiscard]] Result<void> validateOutputResolution(std::uint64_t width, std::uint64_t height,
                                                    std::uint32_t maxDimension = kMaxOutputDimension);

// The output's aspect ratio. Zero height answers 0 rather than dividing by it.
[[nodiscard]] double outputAspect(std::uint32_t width, std::uint32_t height);
// "16:9", "9:16", "1:1", or "1.85:1" when the reduced integer ratio is uglier than a decimal.
[[nodiscard]] std::string aspectLabel(std::uint32_t width, std::uint32_t height);

// ---- zoom (spec §8.2) ---------------------------------------------------------------------------
//
// 100% means one output pixel per *device* pixel, which on a 2x display is half a point. Fit means
// the largest scale at which the whole frame is inside the canvas. Zoom never touches the output
// resolution, the camera or the scene; it is the only thing in this file that can make the frame
// larger than the canvas, and `PreviewFrame::overflows` says when it has.
struct PreviewZoom {
    bool fit = true;
    float scale = 1.0f; // output pixels per device pixel; ignored while `fit`
    [[nodiscard]] bool operator==(const PreviewZoom&) const = default;
};
// The fixed rungs the toolbar offers beside Fit.
[[nodiscard]] std::span<const float> zoomStops();
[[nodiscard]] std::string zoomLabel(const PreviewZoom& zoom, float fittedScale);

// ---- the frame's geometry (spec §4.3) -----------------------------------------------------------

// Where the output frame sits inside the canvas, in the same ImGui points `CanvasRect` uses, so
// the two are directly comparable and a caller never has to remember which space it is in.
struct PreviewFrame {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    // Output pixels per point. `width == outputWidth * pointsPerOutputPixel`, exactly.
    float pointsPerOutputPixel = 0.0f;
    // The scale a Fit would have chosen, in output pixels per device pixel. Reported even when the
    // zoom is explicit, because the toolbar shows "Fit (42%)".
    float fittedScale = 1.0f;
    // The frame is larger than the canvas in at least one axis, so part of it is off-screen and
    // panning is what reaches the rest.
    bool overflows = false;

    [[nodiscard]] bool valid() const { return width >= 1.0f && height >= 1.0f; }
    [[nodiscard]] bool contains(float px, float py) const {
        return px >= x && py >= y && px < x + width && py < y + height;
    }
    // The frame expressed as a canvas rectangle, for the two conversions that are already written
    // against one (`editorInputFromImGui` and the overlay painter's `toScreen`). This is how the
    // letterbox is kept out of selection and gizmo coordinates: they are handed the frame instead
    // of the canvas and nothing else about them changes.
    [[nodiscard]] CanvasRect asCanvasRect(bool hovered) const {
        CanvasRect r;
        r.x = x;
        r.y = y;
        r.width = width;
        r.height = height;
        r.hovered = hovered;
        return r;
    }
};

// Where the frame goes, given the canvas, the output size, the zoom, the display's backing scale
// and the pan. `pan` is in points and is applied only when the frame overflows; a frame that fits
// is always centred, which is what makes "Fit" reproducible.
//
// Handles portrait, square and landscape without a special case, and handles a canvas that is very
// narrow or very short by letting the frame overflow rather than by collapsing it: a frame of zero
// height would divide by zero in every conversion below.
// The whole canvas as a frame: what Workspace mode uses, and the fallback whenever a frame could
// not be computed. Its existence is the reason the rest of the editor needs only one code path --
// every conversion divides by a `PreviewFrame`, and in Workspace mode that frame is the canvas.
[[nodiscard]] PreviewFrame previewFrameFromCanvas(const CanvasRect& canvas);

[[nodiscard]] PreviewFrame fitOutputFrame(const CanvasRect& canvas, std::uint32_t outputWidth,
                                          std::uint32_t outputHeight, const PreviewZoom& zoom,
                                          float pixelScale, float panX = 0.0f, float panY = 0.0f);

// The pan that keeps at least `keepPoints` of the frame inside the canvas in each axis. Applied by
// the host after every gesture, so a frame cannot be thrown off screen and lost (spec §8.2: "Do
// not introduce uncontrolled scrolling into the editor").
void clampPreviewPan(const CanvasRect& canvas, const PreviewFrame& unpanned, float& panX, float& panY,
                     float keepPoints = 48.0f);

// ---- coordinate conversions (spec §10.2) --------------------------------------------------------

// Where a point in ImGui window points falls inside the frame, normalised to [0,1] with the origin
// at the top left. Outside the frame the values simply leave the range, which is what lets a drag
// that began inside continue outside.
struct FrameUv {
    float u = 0.0f;
    float v = 0.0f;
    [[nodiscard]] bool inside() const { return u >= 0.0f && u <= 1.0f && v >= 0.0f && v <= 1.0f; }
};
[[nodiscard]] FrameUv frameUvFor(const PreviewFrame& frame, float windowX, float windowY);
// The output pixel a point lands on, clamped into the frame so a drag released outside still names
// a pixel. Only meaningful for a valid frame.
[[nodiscard]] CanvasPixel outputPixelFor(const PreviewFrame& frame, std::uint32_t outputWidth,
                                         std::uint32_t outputHeight, float windowX, float windowY);

// ---- what to render at (spec §7.2, §7.3) --------------------------------------------------------

// The extent the host resizes the renderer and the engine's viewport to, in device pixels.
struct PreviewRender {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    // The extent is exactly the configured output resolution.
    bool nativeOutput = false;
    // The extent's aspect ratio equals the output's to within one pixel of rounding. False would
    // mean the preview is quietly showing a different framing from the deliverable, which is the
    // one thing this feature must never do; the host asserts on it in debug and the toolbar says so.
    bool aspectMatches = true;
    // Height / outputHeight. 1.0 is native; below 1 the preview is sampling fewer pixels than the
    // deliverable will and the toolbar has to say so (spec §8.1).
    float scaleFromOutput = 1.0f;
    // A resolution-independent summary for the toolbar: "1920x1080" or "960x540 (50% of output)".
    [[nodiscard]] std::string describe(std::uint32_t outputWidth, std::uint32_t outputHeight) const;
};

// Chooses the render extent for a frame, a quality rung and a display scale. Always the output's
// aspect ratio to within rounding, always at least `kMinOutputDimension`, never above
// `kMaxOutputDimension` in either axis; the caller's own budget can lower it further by passing a
// smaller `maxDimension`.
//
// Hybrid, in the spec §7.2 vocabulary: Draft and Realtime size from the frame as displayed
// (approach A, so a small preview costs a small preview's pixels), Native sizes from the
// configuration (approach B, so an output-dependent effect can be inspected at the size it will
// ship at). Both are reachable from the toolbar and both say which they are.
[[nodiscard]] PreviewRender previewRenderExtent(const PreviewFrame& frame, std::uint32_t outputWidth,
                                                std::uint32_t outputHeight, float pixelScale,
                                                PreviewQuality quality,
                                                std::uint32_t maxDimension = kMaxOutputDimension);

// ---- safe areas and composition guides (spec §9) ------------------------------------------------
//
// The percentages are SMPTE ST 2046-1 / RP 218 as adopted by EBU R 95: the *action*-safe area is
// the centred 93% of width and height and the *title*-safe area the centred 90%. Both are
// fractions of each axis rather than a fixed inset, which is what makes them correct for portrait
// and square output without a second rule -- a 9:16 frame gets the same 3.5% and 5% insets, they
// are simply smaller in absolute terms on the short axis.
//
// Configurable rather than constant because a delivery target can specify its own (broadcast
// 4:3-protect, a platform's caption band), and because pretending one number is universal is what
// §9.1 explicitly warns against.
struct SafeAreaSettings {
    float actionFraction = 0.93f;
    float titleFraction = 0.90f;
    [[nodiscard]] Result<void> validate() const;
    [[nodiscard]] bool operator==(const SafeAreaSettings&) const = default;
};

// A guide rectangle in window points, inset from `frame` by a fraction of each axis.
struct GuideRect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};
[[nodiscard]] GuideRect insetFrame(const PreviewFrame& frame, float fraction);

// Where the floating preview toolbar sits inside the canvas, in window points.
//
// Bottom-centred rather than top-left. The bar is chrome laid over the picture, and the top-left is
// the one corner it shares with every other overlay the editor draws -- the frame border, the safe
// areas and the thirds all start there, so a bar in that corner sits on top of the guides it exists
// to toggle. Along the bottom edge it overlaps the least-used band of a 16:9 frame and reads as a
// transport rather than as part of the image.
//
// `toolbarWidth` is measured, not predicted: the bar auto-sizes to its contents, so the caller
// passes what the previous frame came out at. That is a frame of lag on a width that only changes
// when a control appears or disappears, and none at all on the position itself.
//
// Clamped, and that is the case worth having a function for: a bar wider than the canvas would
// centre to a negative x and be clipped at the left, hiding the collapse button -- the one control
// that would get the user out of it. It pins to the left margin instead, so the bar runs off the
// right where the controls are expendable and the button stays reachable.
struct ToolbarOrigin {
    float x = 0.0f;
    float y = 0.0f;
};
[[nodiscard]] ToolbarOrigin previewToolbarOrigin(const CanvasRect& canvas, float toolbarWidth,
                                                 float toolbarHeight, float margin = 8.0f);

// Which overlays the editor is drawing over the frame. Pure view state; none of it is a scene edit
// and none of it goes on the undo stack (spec §13.2).
struct GuideSettings {
    bool safeAreas = false;
    bool thirds = false;
    bool centreCross = false;
    bool frameBorder = true;
    SafeAreaSettings safe;
    [[nodiscard]] bool operator==(const GuideSettings&) const = default;
};

// ---- the editor-local view state (spec §13.1) ---------------------------------------------------
//
// Everything here is the editor's, not the project's: it describes how this person is looking at
// the piece on this machine, and opening someone else's project must not change it. The
// project-owned half -- width, height, frame rate -- is `app::RenderSettings` and is not restated
// here, which is the whole of §5.1's "do not introduce a second incompatible configuration".
//
// Persisted in `app::AppSettings` under "outputPreview" (ADR-225: a setting the application does
// not keep is not a setting).
struct PreviewViewState {
    PreviewViewMode mode = PreviewViewMode::Workspace;
    OutsideFrame outside = OutsideFrame::Dim;
    PreviewQuality quality = PreviewQuality::Realtime;
    PreviewZoom zoom;
    GuideSettings guides;
    // Points, applied only while the frame overflows the canvas.
    float panX = 0.0f;
    float panY = 0.0f;
    // The toolbar across the top of the canvas. Collapsible because the canvas is the point.
    bool toolbar = true;
    // Fullscreen preview (spec §11): a view-state change inside the existing window -- the panels
    // are hidden and restored, the dock tree is untouched, and nothing about the project or the
    // session is reset. `EditorLayout` holds the panel set that was open when it was entered.
    bool fullscreen = false;

    [[nodiscard]] bool showsOutputFrame() const { return modeShowsOutputFrame(mode); }
    // Member-wise, so "has anything moved?" is a comparison of the fields and not of the padding
    // between them. The host asks it every frame to decide whether the settings file is stale.
    [[nodiscard]] bool operator==(const PreviewViewState&) const = default;
};

[[nodiscard]] Result<void> validatePreviewViewState(const PreviewViewState& state);

} // namespace avgen::ui
