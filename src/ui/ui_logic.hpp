#pragma once

// UI-facing pure helpers that can be unit-tested without Dear ImGui.

#include "params/modulation.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <iterator>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace avgen::ui {

// How much of the parameter tree an authoring layer reveals.
//
// Declared here rather than beside the World panel because it is pure logic over a path string and
// nothing else, while `world_panel.hpp` reaches the GPU through `rendering/debug_draw.hpp` -- so a
// unit test that wanted this had to pull WebGPU headers in with it, and therefore did not exist.
// The rule that decides whether the user can see a parameter is exactly the kind of thing that
// should be cheap to test.
enum class AuthoringLayer : int { Beginner = 0, Intermediate = 1, Advanced = 2 };

namespace detail {
inline bool pathStartsWith(const std::string& s, std::string_view prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

// The group prefixes each layer adds to the one below it.
//
// `shader/` sits in the first list, not with the advanced machinery it might look like it belongs
// to. A shader layer is not incidental complexity somebody stumbles into: it is there because they
// loaded a file on purpose, and its inputs are the entire reason they did. Before this it was in
// no list at all, so it showed only on Advanced, while the Shaders window told everyone
// unconditionally that "inputs appear in the Parameters window under 'shader'" -- a promise that
// was false on the layer the editor opens on.
inline constexpr std::string_view kBeginnerPrefixes[] = {"macros/", "scene/", "env/",   "post/",
                                                         "camera/", "root/",  "shader/"};
inline constexpr std::string_view kIntermediatePrefixes[] = {
    "procedural/", "field/", "spline/", "sdf/", "material/", "particles/"};
} // namespace detail

// True when `path` (a parameter path) belongs to the layer.
[[nodiscard]] inline bool layerShowsPath(AuthoringLayer layer, const std::string& path) {
    if (layer == AuthoringLayer::Advanced) {
        return true;
    }
    for (std::string_view prefix : detail::kBeginnerPrefixes) {
        if (detail::pathStartsWith(path, prefix)) {
            return true;
        }
    }
    if (layer == AuthoringLayer::Beginner) {
        return false;
    }
    for (std::string_view prefix : detail::kIntermediatePrefixes) {
        if (detail::pathStartsWith(path, prefix)) {
            return true;
        }
    }
    return detail::pathStartsWith(path, "nodes/");
}


// Slider bounds for a route amount. They must NOT depend on the current amount: a range derived
// from the value being dragged feeds back on itself (drag to the end -> range grows -> repeat)
// until the float range overflows and ImGui asserts (regression: milestone 0.2 crash).
constexpr float kRouteAmountLimit = 8.0f;
// ---- who the viewport's pointer belongs to -----------------------------------------------------
//
// Two things want the left mouse button over the world: the editor (select, box-select, drag a
// gizmo) and the camera (orbit, pan). They were deciding separately, on different events, from
// different sources -- the editor from Dear ImGui each frame, the camera from an SDL button-down --
// and the result was a race the camera always won. A press began a camera orbit, and the editor's
// box selection could not claim the pointer until the drag had *travelled*, by which time the
// gesture was already the camera's. Dragging a box round a group of objects was therefore
// impossible: measured on the scripted editor run, the camera moved 71.9 m during the box drag and
// the box caught nothing.
//
// So the decision is made once, here, from the button and the modifiers, and both sides ask.
//
// The rule: **the left button is the editor's, and the camera is on a modifier.** Selecting is what
// an artist does hundreds of times an hour and framing is what they do between; the frequent
// gesture gets the bare button. The other buttons are unchanged and always the camera's, so looking
// around never depends on which mode the editor is in.
enum class ViewportIntent : std::uint8_t {
    None,
    EditorPointer, // select, box-select, drag a gizmo, paint
    CameraOrbit,
    CameraPan,
    CameraLook,
};

// `alt` is the camera modifier (Option on a Mac). Chosen over Control because macOS turns
// Control+click into a secondary click before SDL ever sees it, which would make the binding work
// on one platform and silently do something else on this one; and over Shift because Shift already
// means "add to the selection", which a camera gesture must not disturb.
//
// Alt with a *click* that never travels still reaches the editor as "pick inside the group" -- a
// press is a click until it moves, so the two never collide.
[[nodiscard]] inline ViewportIntent viewportIntent(bool left, bool middle, bool right, bool alt,
                                                   bool shift) {
    if (middle) {
        return ViewportIntent::CameraPan;
    }
    if (right) {
        return ViewportIntent::CameraLook;
    }
    if (!left) {
        return ViewportIntent::None;
    }
    if (alt) {
        // Both camera drags live behind the one modifier, so "hold Option to move the world" is the
        // whole mental model. Pan stays reachable without a middle button, which a laptop trackpad
        // does not have.
        return shift ? ViewportIntent::CameraPan : ViewportIntent::CameraOrbit;
    }
    return ViewportIntent::EditorPointer;
}

// Whether a mouse event belongs to the world rather than to a panel.
//
// `hovered` is necessarily *last frame's* answer: SDL events are read before the frame is laid out,
// so the newest hover state there is comes from the previous layout. On a still window that is the
// current answer and the test is exact. On a moving pointer it is stale by one frame -- which is
// 16 ms at 60 Hz and nobody notices, and 80 ms or more in a heavy scene, which is long enough to
// move off the canvas onto the timeline and click. That is how a click on the sequencer ended up
// selecting something in the world.
//
// So the stale answer is confirmed against a fact that is *not* stale: where the pointer actually
// is, against the canvas rectangle. A click outside that rectangle is never the world's, whatever
// last frame believed. It does not help against a panel floating *over* the canvas -- there the
// rectangle test says yes and only the hover state knows better -- but that is the narrow case, and
// the stale hover is usually right about it.
//
// A drag that began on the canvas keeps the mouse until the button is released, wherever it travels:
// letting a panel steal a gesture halfway through is how an orbit jumps to a stop mid-swing.
[[nodiscard]] inline bool viewportOwnsPointer(bool hoveredLastFrame, bool pointerInsideCanvas,
                                              bool gestureInProgress) {
    if (gestureInProgress) {
        return true;
    }
    return hoveredLastFrame && pointerInsideCanvas;
}

// Labels for a list of file paths: the file name where that is enough to tell them apart, and as
// much of the trailing path as it takes where it is not.
//
// "Open Recent" showed `night-shift.json` twice. They were not duplicates -- one lived in the main
// checkout and one in an agent's worktree -- so the list was right and the label was the problem. It
// told the reader nothing, and it gave Dear ImGui the same id twice, which is a warning and a menu
// item that answers to the wrong click.
//
// Only the ambiguous entries grow. Lengthening every label to make two of them distinct would make
// a list of full paths, which is worse than the thing being fixed.
[[nodiscard]] inline std::vector<std::string> uniqueFileLabels(
    const std::vector<std::filesystem::path>& paths) {
    const auto tail = [](const std::filesystem::path& p, std::size_t parts) {
        std::filesystem::path out;
        std::vector<std::filesystem::path> segments;
        for (const auto& part : p) {
            segments.push_back(part);
        }
        const std::size_t take = std::min(parts, segments.size());
        for (std::size_t i = segments.size() - take; i < segments.size(); ++i) {
            out /= segments[i];
        }
        return out.generic_string();
    };

    std::vector<std::string> labels(paths.size());
    std::vector<bool> settled(paths.size(), false);
    // One segment, then two, and so on, settling whatever has become unique at each depth. Bounded
    // by the longest path, so a pair that is identical all the way up simply ends as equal strings
    // rather than looping -- the PushID at the call site is what keeps those clickable.
    std::size_t longest = 1;
    for (const auto& p : paths) {
        std::size_t n = 0;
        for ([[maybe_unused]] const auto& part : p) {
            ++n;
        }
        longest = std::max(longest, n);
    }
    for (std::size_t depth = 1; depth <= longest; ++depth) {
        std::vector<std::string> candidate(paths.size());
        for (std::size_t i = 0; i < paths.size(); ++i) {
            candidate[i] = tail(paths[i], depth);
        }
        for (std::size_t i = 0; i < paths.size(); ++i) {
            if (settled[i]) {
                continue;
            }
            std::size_t sharing = 0;
            for (std::size_t j = 0; j < paths.size(); ++j) {
                sharing += (candidate[j] == candidate[i]) ? 1 : 0;
            }
            if (sharing == 1) {
                labels[i] = candidate[i];
                settled[i] = true;
            }
        }
        // Anything still unsettled keeps the longest form tried so far, so a pair that never
        // separates still reads as much of itself as exists.
        for (std::size_t i = 0; i < paths.size(); ++i) {
            if (!settled[i]) {
                labels[i] = candidate[i];
            }
        }
    }
    return labels;
}

[[nodiscard]] inline bool intentIsCamera(ViewportIntent intent) {
    return intent == ViewportIntent::CameraOrbit || intent == ViewportIntent::CameraPan ||
           intent == ViewportIntent::CameraLook;
}

// ---- the sequencer strip's lanes -----------------------------------------------------------------
//
// Which lane a y-coordinate falls in, and how tall the strip is. Here rather than inside
// `SequencePanel::drawStrip` because the drawing and the hit testing have to agree about it, and
// when they stopped agreeing the symptom was a click meant to scrub the music moving the music
// instead (ADR-103). That is arithmetic, and it should be answerable without a window.
enum class StripLane : std::uint8_t {
    Ruler,    // the time axis and the marker row: always a scrub
    Sections, // the song's own shape: boundaries are draggable, section bodies are selectable
    Audio,    // the clips and their waveforms: deliberately holds nothing draggable
    Shots,
    Actors,
    Overlays,
    None,
};

// The strip's resting lane heights, before any zoom or fit-to-panel scaling. Here rather than in the
// panel because `stripLanesFor` below needs them and a test needs `stripLanesFor`.
inline constexpr float kStripRulerHeight = 22.0f;
inline constexpr float kStripMarkerHeight = 16.0f;
inline constexpr float kStripLaneHeight = 26.0f;
// The audio lane is the same height as the others by default, and grows with them.
inline constexpr float kStripAudioLaneHeight = kStripLaneHeight;
inline constexpr float kStripSectionLaneHeight = 22.0f;
inline constexpr float kStripLaneGap = 3.0f;

struct StripLanes {
    bool hasAudio = false;
    // The song-structure lane (ADR-216). Directly under the ruler and *above* the audio, because it
    // is the map of the piece everything below it is cut to -- and in a lane of its own rather than
    // drawn onto the waveform, which is the mistake ADR-103 recorded: blocks on the waveform stole
    // the click that scrubs.
    bool hasSections = false;
    std::size_t actorCount = 0;
    bool hasOverlays = false;
    float rulerHeight = 20.0f;
    float markerHeight = 16.0f;
    float laneHeight = 24.0f;
    // The audio lane is taller than the others: it is the only one whose content is a picture rather
    // than a label, and a waveform three pixels high says nothing about the music.
    float audioLaneHeight = 36.0f;
    float sectionLaneHeight = 22.0f;
    float gap = 3.0f;
    // The header column down the left of the strip, in points. Zero is a strip with no headers,
    // which is what it was before this pass and what the existing lane tests still describe.
    //
    // It lives here, with the lane heights, for exactly the reason the lane heights do: the
    // drawing and the hit testing both need to know where the time axis begins, and ADR-103 is the
    // record of what happens when two places calculate the strip's geometry separately. A click
    // left of `timeLeft()` is a click on a header and never a scrub, and that is one comparison
    // against one number that both sides read.
    float gutter = 0.0f;

    // Where the time axis begins, measured from the left of the strip.
    [[nodiscard]] float timeLeft() const { return gutter; }
    // True when `x`, relative to the left of the strip, is on a lane's header rather than on the
    // time axis.
    [[nodiscard]] bool inGutter(float x) const { return gutter > 0.0f && x < gutter; }

    // Where the lanes start, measured from the top of the strip.
    [[nodiscard]] float lanesTop() const { return rulerHeight + markerHeight + gap; }
    [[nodiscard]] float sectionsTop() const { return lanesTop(); }
    [[nodiscard]] float audioTop() const {
        return hasSections ? sectionsTop() + sectionLaneHeight + gap : lanesTop();
    }
    [[nodiscard]] float shotsTop() const {
        return hasAudio ? audioTop() + audioLaneHeight + gap : audioTop();
    }
    [[nodiscard]] float actorsTop() const { return shotsTop() + laneHeight + gap; }
    [[nodiscard]] float overlaysTop() const {
        return actorsTop() + static_cast<float>(actorCount) * (laneHeight + gap);
    }
    [[nodiscard]] float height() const {
        const int rows = 1 + static_cast<int>(actorCount) + (hasOverlays ? 1 : 0);
        return rulerHeight + markerHeight + static_cast<float>(rows) * (laneHeight + gap) + gap +
               (hasAudio ? audioLaneHeight + gap : 0.0f) +
               (hasSections ? sectionLaneHeight + gap : 0.0f);
    }

    // `y` relative to the top of the strip. The gaps between lanes belong to no lane, and that is
    // deliberate: a click that lands in one is a scrub, which is the safe answer.
    [[nodiscard]] StripLane at(float y) const {
        const auto within = [y](float top, float h) { return y >= top && y < top + h; };
        if (y < lanesTop()) {
            return StripLane::Ruler;
        }
        if (hasSections && within(sectionsTop(), sectionLaneHeight)) {
            return StripLane::Sections;
        }
        if (hasAudio && within(audioTop(), audioLaneHeight)) {
            return StripLane::Audio;
        }
        if (within(shotsTop(), laneHeight)) {
            return StripLane::Shots;
        }
        if (actorCount > 0 && y >= actorsTop() && y < overlaysTop()) {
            return StripLane::Actors;
        }
        if (hasOverlays && within(overlaysTop(), laneHeight)) {
            return StripLane::Overlays;
        }
        return StripLane::None;
    }
};

// The strip's lanes for a piece, at a given vertical zoom.
//
// A free function rather than an aggregate initialiser in the panel, and the reason is a bug rather
// than a preference. Written inline it was
//
//     StripLanes lanes{ ..., .laneHeight = kLaneHeight * laneZoom_, ... };
//
// and a bulk rename turned the right-hand `kLaneHeight` into `lanes.laneHeight` -- the member of the
// object being initialised, which is zero. Every lane below the waveform then drew at zero height:
// the shots lane, every actor lane and the overlay lane vanished from a piece that had five shots,
// and the whole unit suite passed, because nothing outside a window could see the construction.
//
// Here it can. `sectionLaneHeight` does not take the zoom, deliberately: vertical zoom exists to read
// a waveform or fit a long cast, and a section block is a label on a span that says nothing more for
// being taller -- it also stays put while the lanes under it grow, which is what makes it usable as
// the ruler it is.
[[nodiscard]] inline StripLanes stripLanesFor(bool hasAudio, bool hasSections, std::size_t actorCount,
                                              bool hasOverlays, float laneZoom) {
    const float zoom = laneZoom > 0.0f ? laneZoom : 1.0f;
    StripLanes lanes;
    lanes.hasAudio = hasAudio;
    lanes.hasSections = hasSections;
    lanes.actorCount = actorCount;
    lanes.hasOverlays = hasOverlays;
    lanes.rulerHeight = kStripRulerHeight;
    lanes.markerHeight = kStripMarkerHeight;
    lanes.laneHeight = kStripLaneHeight * zoom;
    lanes.audioLaneHeight = kStripAudioLaneHeight * zoom;
    lanes.sectionLaneHeight = kStripSectionLaneHeight;
    lanes.gap = kStripLaneGap;
    return lanes;
}

// ---- blocks in a lane: where the body ends and the grip begins -------------------------------
//
// A clip, a shot and an overlay are all the same shape -- a box on a time axis with a draggable
// body and two edges that resize it -- so which part of one the pointer is over is answered once,
// here, without a window. The cursor, the hover highlight and the drag all read the same answer,
// which is the property that stopped the lanes and the hit test drifting apart in ADR-103.
enum class BlockZone : std::uint8_t {
    None,      // not over the block at all
    Body,      // moves it
    LeftEdge,  // trims the start
    RightEdge, // trims the end
};

// A block narrower than this offers no edge grips at all. Two of them would leave a body of a few
// points, and a move gesture that can only be started by hitting a three-pixel target is not a
// gesture, it is a lottery -- zooming in is the honest answer, and the ruler makes that easy.
inline constexpr float kMinGrippableBlockWidth = 18.0f;

// `x` is the pointer, `a` and `b` the block's left and right edges, all in one space (screen
// points). `grab` is how far inside an edge still counts as that edge.
//
// The grips are strictly *inside* the block. A grip that reached past the edge would swallow the
// first few points of the neighbouring block, and in a lane packed edge to edge that is every
// click. It also shrinks on a narrow block -- a third of the width at most -- so that the body
// never disappears behind its own handles.
[[nodiscard]] inline BlockZone blockZoneAt(float x, float a, float b, float grab) {
    if (b < a) {
        std::swap(a, b);
    }
    if (x < a || x > b) {
        return BlockZone::None;
    }
    const float width = b - a;
    if (width < kMinGrippableBlockWidth) {
        return BlockZone::Body;
    }
    const float grip = std::min(std::max(grab, 0.0f), width / 3.0f);
    if (grip <= 0.0f) {
        return BlockZone::Body;
    }
    // The right edge wins a tie on a block so narrow that the two grips meet, because extending the
    // end is the commoner gesture and because `a + grip <= b - grip` is guaranteed by the third
    // above -- the tie is only reachable at exactly a third.
    if (x >= b - grip) {
        return BlockZone::RightEdge;
    }
    if (x <= a + grip) {
        return BlockZone::LeftEdge;
    }
    return BlockZone::Body;
}

// ---- a right-click that is not the end of a right-drag ----------------------------------------
//
// Dear ImGui opens a context menu on the *release* of the right button and makes no test of how far
// it travelled (`IsPopupOpenRequestForItem` in imgui.cpp: `IsMouseReleased(button) &&
// IsItemHovered(...)`, and nothing else). On a list row that is exactly right. On the sequencer
// strip it is wrong, because a right-drag is how the view pans -- so every pan would finish by
// opening a menu over wherever it happened to stop.
//
// The strip therefore decides for itself: a right press that stays put is a menu, a right press
// that moves is a pan, and once it has moved it cannot become a menu again however far it comes
// back. Latching `travelled` is what makes that last part true; testing the distance only at the
// release would call a there-and-back drag a click.
struct ContextClickTracker {
    bool down = false;
    bool travelled = false;
    float pressX = 0.0f;
    float pressY = 0.0f;
};

// Four points of slop: enough that a hand resting on a trackpad does not lose its menu, small
// enough that a deliberate pan never opens one.
inline constexpr float kContextClickSlop = 4.0f;

// Feed the button's state every frame. Returns true on the single frame a context menu should open.
[[nodiscard]] inline bool updateContextClick(ContextClickTracker& t, bool pressed, bool released,
                                             float x, float y, float slop = kContextClickSlop) {
    if (pressed) {
        t.down = true;
        t.travelled = false;
        t.pressX = x;
        t.pressY = y;
    }
    if (t.down && !t.travelled) {
        const float dx = x - t.pressX;
        const float dy = y - t.pressY;
        if (dx * dx + dy * dy > slop * slop) {
            t.travelled = true;
        }
    }
    if (released) {
        const bool menu = t.down && !t.travelled;
        t.down = false;
        t.travelled = false;
        return menu;
    }
    return false;
}

// ---- when the canvas admits it is working ------------------------------------------------------
//
// The rule the brief asks for, as arithmetic: nothing at all under the threshold, then a fade in.
//
// A threshold is the whole point. Procedural regeneration defers for 90 ms by design (ADR-084), and
// an indicator that appeared for every one of those would blink on every frame of every slider
// drag -- which does not communicate "working", it teaches the eye to ignore the one place the
// application has to say so. And it must fade rather than appear: something that blinks on at full
// strength reads as an error, and this is not one.
//
// This reads a wall clock, and a wall clock may not decide what a render contains. It does not: the
// hint is drawn by Dear ImGui into the window's swapchain image, after the world has been rendered
// into its own target and never into it. `--headless` builds no window and draws no ImGui at all.
struct ProcessingHint {
    bool visible = false;
    float opacity = 0.0f; // 0..1
};

// How long the work must have been going before the canvas says anything.
inline constexpr double kProcessingAppearMs = 180.0;
// And how long it then takes to reach full strength.
inline constexpr double kProcessingFadeMs = 140.0;

[[nodiscard]] inline ProcessingHint processingHint(bool busy, double busyForMs) {
    ProcessingHint out;
    if (!busy || busyForMs < kProcessingAppearMs) {
        return out;
    }
    const double into = busyForMs - kProcessingAppearMs;
    out.visible = true;
    out.opacity = static_cast<float>(std::clamp(into / kProcessingFadeMs, 0.0, 1.0));
    return out;
}

// Accumulates how long the world has been busy, so `processingHint` has something to ask about.
// Fed the frame's own delta rather than reading a clock, which is what makes it testable and what
// keeps the one wall clock involved at the call site where it can be seen.
struct ProcessingTracker {
    double busyForMs = 0.0;

    ProcessingHint advance(bool busy, double deltaMs) {
        // The timer resets the moment the work finishes, so a second burst starts from zero and
        // has to earn the indicator again. Carrying it over would make the indicator appear
        // instantly on every subsequent slider nudge, which is the flashing the threshold exists
        // to prevent.
        busyForMs = busy ? busyForMs + std::max(deltaMs, 0.0) : 0.0;
        return processingHint(busy, busyForMs);
    }
};

// ---- the timeline ruler's divisions ------------------------------------------------------------
//
// A ruler with one tick size is a row of scratches; a ruler with two is readable at a glance,
// because the minor ticks give the eye something to count between the labels. Which two depends on
// the zoom, and picking them is arithmetic over a 1-2-5 ladder rather than a guess.
struct RulerTicks {
    double major = 1.0; // labelled
    double minor = 0.0; // unlabelled; zero means there is no room for them
};

// `span` is how many seconds are on screen, `width` how many points wide that is.
// `minLabelSpacing` keeps the labels from colliding; `minMinorSpacing` stops the minor ticks
// closing up into a grey band, which is worse than no minor ticks at all.
[[nodiscard]] inline RulerTicks rulerTicks(double span, float width, float minLabelSpacing = 68.0f,
                                           float minMinorSpacing = 7.0f) {
    RulerTicks out;
    if (!(span > 0.0) || !(width > 0.0f)) {
        return out;
    }
    // Seconds, on the ladder a person reads time in. 1-2-5 up to a minute and then the minute
    // multiples, because 100 seconds is not a unit anybody thinks in.
    static constexpr double kLadder[] = {0.01, 0.025, 0.05, 0.1, 0.25, 0.5,  1.0,   2.0,
                                         5.0,  10.0,  15.0, 30.0, 60.0, 120.0, 300.0, 600.0};
    const double perPoint = span / static_cast<double>(width);
    out.major = kLadder[std::size(kLadder) - 1];
    for (const double step : kLadder) {
        if (step / perPoint >= static_cast<double>(minLabelSpacing)) {
            out.major = step;
            break;
        }
    }
    // The finest subdivision of the major that still reads as separate ticks. Halves, quarters and
    // fifths only: a major divided by three is not something the eye counts.
    for (const int divisor : {5, 4, 2}) {
        const double candidate = out.major / static_cast<double>(divisor);
        if (candidate / perPoint >= static_cast<double>(minMinorSpacing)) {
            out.minor = candidate;
            break;
        }
    }
    return out;
}

// ---- parameters the current state ignores ------------------------------------------------------
//
// The camera has three mutually exclusive ways of being placed (`camera/mode`: 0 orbit, 1 free,
// 2 spline) and each has its own parameters. The other two families are still registered, still
// exposed, and still move under the mouse -- they simply do not reach the picture. Glowmere is a
// free camera, so its `camera/distance`, `camera/height` and `camera/orbitSpeed` sliders drag and
// do nothing, which is indistinguishable from a broken engine and was reported as one.
//
// The same is true of `camera/fov` when the lens is driving the field of view.
//
// Marking rather than hiding, and marking rather than disabling: the value is real, it is saved,
// and it is what the camera will use the moment the mode changes -- so it must stay visible and
// stay editable. What it must not do is look like it is working.
struct ParameterInertness {
    bool inert = false;
    std::string_view because;    // "the camera is in free mode"
    std::string_view belongsTo;  // "the orbit camera"
    std::string_view fixPath;    // the parameter that would make it live again
    float fixValue = 0.0f;
    std::string_view fixLabel;   // what to call that in a menu
};

// `mode` is `camera/mode`; `explicitFov` is `camera/lens/useExplicitFov`.
[[nodiscard]] inline ParameterInertness parameterInertness(std::string_view path, int mode,
                                                           bool explicitFov) {
    const auto cameraModeName = [](int m) -> std::string_view {
        return m == 0 ? "orbit" : (m == 1 ? "free" : "spline");
    };
    ParameterInertness out;
    const auto ignoredUnless = [&](bool live, int wantedMode, std::string_view family) {
        if (live) {
            return;
        }
        out.inert = true;
        out.because = cameraModeName(mode) == std::string_view("orbit")   ? "the camera is in orbit mode"
                      : cameraModeName(mode) == std::string_view("free")  ? "the camera is in free mode"
                                                                          : "the camera is in spline mode";
        out.belongsTo = family;
        out.fixPath = "camera/mode";
        out.fixValue = static_cast<float>(wantedMode);
        out.fixLabel = wantedMode == 0   ? "Switch the camera to orbit mode"
                       : wantedMode == 1 ? "Switch the camera to free mode"
                                         : "Switch the camera to spline mode";
    };
    if (path == "camera/distance" || path == "camera/height" || path == "camera/orbitSpeed") {
        // Mode 2 falls back to the orbit placement when the scene names no camera spline, so only
        // free mode is certain to ignore these. Claiming more than is true is how a marker like
        // this loses its meaning.
        ignoredUnless(mode != 1, 0, "the orbit camera");
    } else if (path == "camera/position" || path == "camera/target") {
        ignoredUnless(mode == 1, 1, "the free camera");
    } else if (path == "camera/splineT" || path == "camera/lookAhead" || path == "camera/splineOffset") {
        ignoredUnless(mode == 2, 2, "the spline camera");
    } else if (path == "camera/fov" && !explicitFov) {
        out.inert = true;
        out.because = "the lens is driving the field of view";
        out.belongsTo = "an explicit field of view";
        out.fixPath = "camera/lens/useExplicitFov";
        out.fixValue = 1.0f;
        out.fixLabel = "Use this field of view instead of the lens";
    }
    return out;
}

inline std::pair<float, float> routeAmountBounds(const params::ModRoute& /*route*/) {
    return {-kRouteAmountLimit, kRouteAmountLimit};
}

// Replaces NaN/inf (e.g. from a hand-edited project file) so widgets never see them.
inline float sanitiseFinite(float value, float fallback = 0.0f) { return std::isfinite(value) ? value : fallback; }

// ---- how wide text is allowed to be ------------------------------------------------------------
//
// Dear ImGui reads a *negative* wrap position as "do not wrap at all": `CalcWrapWidthForPos`
// returns 0 below zero and `TextEx` only wraps when `wrap_pos_x >= 0`. Every wrap width in this
// application is a subtraction -- the space available, less an inset, less an indent -- and a
// subtraction goes negative as soon as somebody drags the panel narrow enough. At that point
// wrapping switches *off*, silently, on the panel that needed it most, and the sentence walks out
// of the window with nothing on screen to say so.
//
// So the arithmetic lives here with a test, where a wrong answer is a number rather than an
// appearance. `minimum` is a floor and not a clamp to zero because zero is its own unreadable
// answer: ImGui treats 0.0f as "wrap at the window's right edge" when it is a *wrap position*, but
// as a *width* it would break after every character.
inline constexpr float kMinWrapWidth = 96.0f;

[[nodiscard]] inline float wrapWidthFor(float available, float inset = 0.0f,
                                        float minimum = kMinWrapWidth) {
    if (!std::isfinite(available) || !std::isfinite(inset) || !std::isfinite(minimum)) {
        return kMinWrapWidth;
    }
    return std::max(available - inset, std::max(minimum, 1.0f));
}

// How wide a tooltip may get before it wraps.
//
// A tooltip is an auto-resizing window, so the trick every panel uses -- `PushTextWrapPos(0.0f)`,
// "wrap at the window's right edge" -- is circular inside one: the right edge is wherever the text
// has already put it, so nothing wraps and the window simply grows. A tooltip needs a number.
//
// This repository writes three- and four-sentence tooltips. Laid out on one line at the editor's
// 15-point font they are over a thousand points wide, which is wider than the window the editor
// opens at (1440x900), and ImGui then pushes the tooltip back against the viewport edge and clips
// the far end off. The sentence is drawn and cannot be read, which is the whole defect.
//
// Two bounds, and the smaller wins. `kTooltipWrapEms` is the comfortable measure -- roughly the
// forty characters a line of prose wants -- and the viewport fraction is what keeps a tooltip from
// being wider than the screen it has to fit on when somebody runs the editor in a small window.
inline constexpr float kTooltipWrapEms = 40.0f;
inline constexpr float kTooltipViewportFraction = 0.45f;

[[nodiscard]] inline float tooltipWrapWidth(float fontSize, float viewportWidth) {
    if (!std::isfinite(fontSize) || fontSize <= 0.0f) {
        fontSize = 15.0f; // the editor's font; a tooltip still has to be drawn if this is nonsense
    }
    const float comfortable = fontSize * kTooltipWrapEms;
    if (!std::isfinite(viewportWidth) || viewportWidth <= 0.0f) {
        return comfortable;
    }
    return std::max(std::min(comfortable, viewportWidth * kTooltipViewportFraction), kMinWrapWidth);
}

// The width to give a widget so that its own label still fits beside it.
//
// Dear ImGui draws a widget's label *after* the widget, on the same line, and
// `SetNextItemWidth(-1)` means "take everything to the right edge". The two together put the label
// past the right edge of the window, where it is clipped away entirely -- so a slider labelled
// "Volume" is drawn as an unlabelled bar with a stray "V" at the border, and a modulation route's
// slider says nothing at all about which route it is. `SetNextItemWidth(-90)` is the same mistake
// with a guess in place of the measurement: ninety points holds "gain" and loses
// "audio.bass -> particles/emission".
//
// The measurement belongs at the call site (only ImGui can measure a string in the current font);
// the arithmetic belongs here. `minItem` is what stops the widget itself collapsing to nothing on a
// panel too narrow to hold both -- past that point something has to give, and a draggable bar with
// no width is less useful than a label that overflows.
inline constexpr float kMinItemWidth = 60.0f;

[[nodiscard]] inline float itemWidthBesideLabel(float available, float labelWidth, float spacing,
                                                float trailing = 0.0f,
                                                float minItem = kMinItemWidth) {
    if (!std::isfinite(available) || !std::isfinite(labelWidth) || !std::isfinite(spacing) ||
        !std::isfinite(trailing)) {
        return minItem;
    }
    const float room = available - labelWidth - spacing - trailing;
    return std::max(room, std::max(minItem, 1.0f));
}

// Whether the next item on a toolbar row still fits before the window's right edge.
//
// A toolbar built from `SameLine()` is a row until it is wider than the panel, and then the tail of
// it is drawn outside the window -- where ImGui clips it and, because a panel has no horizontal
// scrollbar, there is no way to reach it at all. The Sequence panel's row ended in "Catch playhead"
// and at 1000 points wide the button read "Catcl" and could not be pressed.
//
// All four values are in the same space (screen x, or window-local x -- the caller picks, and the
// comparison does not care which as long as they agree).
[[nodiscard]] inline bool toolbarItemFits(float lastItemRight, float nextItemWidth, float spacing,
                                          float rightEdge) {
    if (!std::isfinite(lastItemRight) || !std::isfinite(nextItemWidth) || !std::isfinite(spacing) ||
        !std::isfinite(rightEdge)) {
        return true; // nonsense in: keep the row rather than scatter it down the panel
    }
    return lastItemRight + spacing + nextItemWidth <= rightEdge;
}

// Where a two-column row's second column starts, given a fixed column the label may be too long for.
//
// A hard-coded `SameLine(190.0f)` is a column until somebody writes a label wider than 190 points,
// and then the widget is drawn *on top of* the text. Both are then unreadable, which is worse than
// either alone. This keeps the column where it is whenever the label fits and pushes it out only
// when it does not.
[[nodiscard]] inline float labelColumnX(float column, float labelEndX, float spacing) {
    if (!std::isfinite(column) || !std::isfinite(labelEndX) || !std::isfinite(spacing)) {
        return std::isfinite(column) ? column : 0.0f;
    }
    return std::max(column, labelEndX + spacing);
}

// ---- catching the playhead (Logic's "catch") -------------------------------------------------
//
// Two small pieces of arithmetic, here rather than inline in the panel because the panel cannot be
// tested and these are the whole of the behaviour. `stripLanesFor` is next door for the same reason,
// and for the reason it was extracted: a bulk rename once turned lane heights into zero and the
// entire suite stayed green, because nothing could see the number.

// Where the strip should be scrolled to so the playhead stays on screen while time advances.
//
// Deliberately NOT "keep the playhead centred". Centring scrolls the strip every single frame,
// which makes the waveform crawl continuously under a still pointer and is exhausting to watch;
// Logic pages instead, and so does this. The view moves only when the playhead reaches `edge` of
// the way across, and then it jumps so the playhead lands `lead` of the way in -- giving most of a
// screen of what is coming next, which is what the view is for.
//
// Returns `view` unchanged when the playhead is comfortably inside, which is most frames.
[[nodiscard]] inline double caughtView(double view, double span, double now, double duration,
                                       double edge = 0.9, double lead = 0.1) {
    if (!(span > 0.0)) {
        return view;
    }
    const double maxView = std::max(0.0, duration - span);
    // Behind the left edge is a backwards seek, and paging backwards by a screen is the same idea.
    if (now < view || now > view + span * edge) {
        return std::clamp(now - span * lead, 0.0, maxView);
    }
    return std::clamp(view, 0.0, maxView);
}

// Where the strip should be scrolled to after a zoom, so `anchor` stays at the same place on screen.
//
// Without this a zoom grows or shrinks the window around its *left edge*, so the thing being looked
// at slides away exactly when somebody zooms in to look at it more closely. With it, the anchor --
// the playhead, when catch is on -- is the one point that does not move.
[[nodiscard]] inline double viewAfterZoom(double view, double oldSpan, double newSpan, double anchor,
                                          double duration) {
    if (!(oldSpan > 0.0) || !(newSpan > 0.0)) {
        return view;
    }
    // The anchor's position across the window, kept: view' = anchor - fraction * newSpan.
    const double fraction = (anchor - view) / oldSpan;
    return std::clamp(anchor - fraction * newSpan, 0.0, std::max(0.0, duration - newSpan));
}

// A duration a person reads off a progress line, as h:mm:ss or m:ss.
//
// Seconds alone stop being a duration somewhere around a minute: "4231 s elapsed" is a number you
// have to do arithmetic on before it means anything, and a render that long is exactly when you
// want to know at a glance. Hours appear only when there are any, so a short render is not padded
// with a leading zero that never changes.
//
// Here rather than beside a panel because two panels already format a duration and had drifted to
// different answers -- `world_builder_panel`'s own helper is m:ss with no hours, so a ninety-minute
// build reads "125:30" rather than "2:05:30".
[[nodiscard]] inline std::string elapsedClock(double seconds) {
    if (!(seconds >= 0.0) || !std::isfinite(seconds)) {
        return "--:--";
    }
    const long long total = static_cast<long long>(seconds);
    const long long h = total / 3600;
    const long long m = (total % 3600) / 60;
    const long long sec = total % 60;
    char buffer[32];
    if (h > 0) {
        std::snprintf(buffer, sizeof(buffer), "%lld:%02lld:%02lld", h, m, sec);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%lld:%02lld", m, sec);
    }
    return buffer;
}

} // namespace avgen::ui
