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
#include <span>
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


// The section a parameter belongs to inside its group: everything between the group and the leaf.
//
//   ("post/bloom/intensity",       "post")  -> "bloom"
//   ("post/enabled",               "post")  -> ""        (a direct member of the group)
//   ("nodes/tree-of-life/wind/lag", "nodes") -> "tree-of-life/wind"
//
// A parameter's `group()` is the FIRST path segment and its `label()` is the LAST, so without this
// the middle is simply discarded -- which is how the Parameters panel came to show three checkboxes
// all reading "enabled" under one "post" heading, with nothing to say which was bloom and which was
// halation. The owner reported it as "I have no idea what I'm enabling when I click a checkbox."
//
// Pure and here rather than inline in the panel because panel string arithmetic is a repeat offender
// in this repository: the Tree panel's sections drew empty boxes for months over five characters of
// `substr`, and the tests passed because they asserted the REGISTRATION paths and never the ones the
// panel computed. A wrong path neither fails to compile nor throws -- the section just renders
// blank, which is indistinguishable from "this scene has no such effect".
[[nodiscard]] inline std::string parameterSubGroup(std::string_view path, std::string_view group) {
    std::string_view rest = path;
    if (!group.empty() && path.size() > group.size() + 1 && path.starts_with(group) &&
        path[group.size()] == '/') {
        rest = path.substr(group.size() + 1);
    }
    const std::size_t slash = rest.rfind('/');
    return slash == std::string_view::npos ? std::string{} : std::string(rest.substr(0, slash));
}

// The last path segment: the switch a section is gated on is found by its leaf being "enabled",
// rather than by a list of subsystem names that goes stale the moment somebody adds an effect.
[[nodiscard]] inline std::string_view parameterLeaf(std::string_view path) {
    const std::size_t slash = path.rfind('/');
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
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

// Whether an arrow key belongs to the transport, and therefore must NOT also reach Dear ImGui.
//
// ADR-357 gave the four arrows to the transport: they step the playhead by the sequencer's active
// snap unit. But `ImGuiConfigFlags_NavEnableKeyboard` makes the same four keys drive keyboard
// navigation, and the application hands every event to ImGui *before* it decides whether a shortcut
// claims it. So the playhead moved and the focus ring walked the transport's own buttons at the same
// time, which is what the owner reported: "it also selects buttons from the sequence transport menu".
// Consuming the key afterwards cannot undo that -- by then ImGui has already seen it.
//
// The three exceptions are the cases where the arrows genuinely belong to the interface:
//
//   * `typing` -- a text field has the keyboard, and Left/Right move the caret.
//   * `widgetActive` -- a slider is mid-drag or a combo is open, and the arrows adjust it.
//   * `popupOpen` -- a menu is pulled down. This one is easy to miss, because an open menu is a
//     popup *window* and not an active *item*: `IsAnyItemActive()` is false while a menu the user
//     just opened plainly owns the arrows. Without this the fix would make menus unnavigable, which
//     trades one regression for another.
//
// The cost, stated rather than discovered later: while none of the three holds, the arrows cannot
// move ImGui's focus ring anywhere in the application. That is what giving the arrows to the
// transport means, and Tab still moves focus.
[[nodiscard]] inline bool transportOwnsArrowKey(bool isArrowKey, bool withCommand, bool typing,
                                                bool widgetActive, bool popupOpen) {
    if (!isArrowKey || withCommand) {
        return false; // Cmd+arrow belongs to whoever wants it; the transport takes neither.
    }
    return !typing && !widgetActive && !popupOpen;
}

// Whether an arrow key belongs to the *editor* -- nudging the selection -- rather than the
// transport.
//
// A selection is not enough, and that is the whole point of this function. `nudgeSelection` used to
// claim the arrows whenever anything at all was selected, so the transport only ever saw them with
// an empty selection. That was tolerable while only composition nodes could be selected, because a
// user scrubbing a timeline usually had nothing selected. Making **lights and cameras selectable
// widens that window enormously**: selecting a light in the Lights panel to check its intensity
// would silently stop the arrows scrubbing, anywhere in the application, until it was deselected.
// The owner has already reported this symptom once from the other direction (ADR-357, the focus
// ring and the playhead moving on one press), and it must not come back wearing a different hat.
//
// So the editor has to be the thing the user is actually pointing at. `viewportHasPointer` is the
// canvas's own hover state -- the same signal `viewportOwnsPointer` uses for the mouse -- which is
// what every DCC does: the arrows go to the editor under the cursor. Working in the sequencer with
// a light selected therefore scrubs, and hovering the viewport nudges.
[[nodiscard]] inline bool editorOwnsArrowKey(bool hasSelection, bool viewportHasPointer) {
    return hasSelection && viewportHasPointer;
}

// May this gesture stand the director down?
//
// **This is a guard on live data loss, not a preference.** `releaseDirectedCamera` destroys six
// `directedCameraTargets()` worth of timeline tracks, the whole `cameraAimFollow` table and the
// whole `cameraShotSpans` table -- 37 and 42 entries on the multicam film -- and the next Save
// writes the loss into the project. It was observed happening for real: the app was open, it saved,
// and the bake was gone. Re-baking is not a recovery either, because it re-photographs the hero
// anchors (ADR-344), so the cut comes back different.
//
// While that could only be reached by dragging the viewport under a directed camera it was a defect
// you had to know the sequence for. The moment a camera is a draggable object in the Canvas it is
// one click away from anybody composing a shot, which is why the lock lands with the dragging and
// not after it.
//
// The distinction is the one every DCC makes and the viewport brief's §7 asks for by name:
// **navigating the view is not modifying the camera.** An incidental gesture -- a drag, a framing,
// a dolly -- may not discard a cut. A deliberate one -- the menu's "take the camera back", the
// Camera panel's unlock -- may, because the user said so in words rather than by moving a mouse.
// `hasBake` is whether standing the director down would actually destroy anything --
// `app::directedCameraBakeSize`, which counts the same four things `releaseDirectedCamera` removes.
// A guard on data loss with no data to lose is only a cost, and shipped as one: locked
// unconditionally, this refused the viewport on EVERY directed project. On the Tree of Life -- 0
// camera tracks, 0 aim-follow entries, 0 shot spans -- Option-drag became a no-op and a status line
// in order to protect nothing, which is how the owner found it. The multicam film carries 42 spans
// and 37 follow entries, was the case the lock was written for, and still locks.
[[nodiscard]] inline bool viewportMayReleaseDirector(bool directed, bool locked, bool deliberate,
                                                     bool hasBake) {
    if (!directed) {
        return false; // nothing to stand down
    }
    if (!hasBake) {
        return true; // nothing to lose, so nothing to defend
    }
    return deliberate || !locked;
}

// ---- what the canvas is for, this frame (ADR-391) ----------------------------------------------
//
// The editor viewpoint is the user's choice, and two things overrule it. Both are cases where
// somebody other than the person driving is looking at this frame, and in both of them the canvas
// has to be the film:
//
//   * `previewShowsOutputFrame` -- Output Frame and Preview render at the output's aspect ratio and
//     exist to show what the deliverable contains. An editor viewpoint inside them would be a lie
//     about the one thing those modes are for. Workspace navigates; Output Frame composes, and a
//     drag there moves the film's camera exactly as it always has.
//   * `outputWatching` -- an open output window or a Syphon/NDI share. Both are handed the live
//     viewport's own render target (`presentAll`, `TextureShare::publish`), so whatever the canvas
//     shows is on the projector. Flying without disturbing a live output would take a second render
//     of the film every frame; what must not happen is that it silently re-frames somebody's show.
//
// `hasComposition` is false for the scenes that have one fixed camera and no composition to hold an
// editor pose (the orb, a bare glTF). Those have always been the film and still are.
//
// A predicate rather than a branch inside `Application`, because it is the rule that decides
// whether a mouse drag edits the deliverable, and a rule like that should be readable and reachable
// by a test without a window, a device or a project.
[[nodiscard]] inline bool viewportShowsFilm(bool userWantsFilm, bool previewShowsOutputFrame,
                                            bool outputWatching, bool hasComposition) {
    return userWantsFilm || previewShowsOutputFrame || outputWatching || !hasComposition;
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

// ---- the slice tool (ADR-356) --------------------------------------------------------------------
//
// Cmd+click cuts whatever is under the pointer. The *operations* it performs already existed, one
// per lane, and the context menu has been offering two of them for some time; what did not exist was
// the one decision that stands in front of all of them -- which lane, which block, where exactly the
// cut lands once the grid has had its say, and whether that cut is legal.
//
// It is here, and it is pure, for the reason the lane geometry above is: the panel's copy of this
// question cannot be tested, and a slice that quietly cuts the wrong block or refuses for no visible
// reason is precisely the kind of defect that only a test written against arithmetic will catch. The
// panel supplies the times and performs the mutation; nothing below reads a mouse.

// What a slice would cut. One per lane that has something sliceable in it.
enum class SliceKind : std::uint8_t {
    None,
    Shot,
    Section,
    AudioClip,
    Overlay,
    ActorClip, // a span between two clip cues on one actor's lane
};

// Why a slice was refused, so the panel can say so rather than doing nothing.
//
// A reason rather than a bool, because "there is nothing here" and "there is something here but the
// cut would shave a twentieth of a second off it" are different mistakes and the second one is the
// one a person will repeat until told.
enum class SliceRefusal : std::uint8_t {
    None,               // legal: go ahead
    LaneNotSliceable,   // the ruler, a lane header, a lane with no blocks in it at all
    NothingUnderPointer,
    WouldBeTooShort,    // a block is there, but this cut would leave a half under the minimum
};

// A block on a lane, as the slice tool needs to see it: two times and nothing else.
//
// Deliberately not a `seq::Shot`, an `audio::AudioClip` or a `song::Section`. Five lanes hold five
// unrelated types whose only shared property is that they occupy a span, and the alternative to
// this two-field struct is five copies of the same decision -- which is the drift the panel's
// "one function per operation" rule exists to prevent, one level up.
struct SliceBlock {
    double start = 0.0;
    double end = 0.0;
};

// Everything the panel needs to carry out a slice, or to grey out the menu item that would.
struct SlicePlan {
    SliceKind kind = SliceKind::None;
    int index = -1;    // which block within the lane
    int actorRow = -1; // which actor's lane, for SliceKind::ActorClip
    // Where the cut lands: the snapped time, not the pointer's. Meaningful only when `legal()`.
    double atSeconds = 0.0;
    SliceRefusal refusal = SliceRefusal::LaneNotSliceable;

    [[nodiscard]] bool legal() const { return refusal == SliceRefusal::None; }
    // **Whether the edit re-mixes the audio.** Only the audio lane does, and getting this wrong in
    // the other direction is not a cosmetic error: `TimelineChange::clipsTouched` makes an undo
    // record re-open every source and re-mix the whole piece, so a shot slice that claimed to touch
    // the audio would spend a pass over five million samples saying nothing had changed.
    [[nodiscard]] bool touchesAudio() const { return kind == SliceKind::AudioClip; }
};

// Which lane cuts what. `None` for the lanes that hold no spans.
[[nodiscard]] inline SliceKind sliceKindFor(StripLane lane) {
    switch (lane) {
    case StripLane::Shots: return SliceKind::Shot;
    case StripLane::Sections: return SliceKind::Section;
    case StripLane::Audio: return SliceKind::AudioClip;
    case StripLane::Overlays: return SliceKind::Overlay;
    case StripLane::Actors: return SliceKind::ActorClip;
    case StripLane::Ruler:
    case StripLane::None:
    default: return SliceKind::None;
    }
}

// Which of the strip's two grids a slice obeys.
//
// **The strip has two snaps on purpose** and this is the decision about which one a cut takes. A
// section boundary is dragged on `sectionSnap_` and everything else on `snapMode_`, because -- as
// the declaration of `snapSection` puts it -- a person dragging a section boundary and a person
// dragging a shot are not necessarily asking for the same grid.
//
// A slice on the section lane *creates a section boundary*: the identical object, in the identical
// list, that the very next gesture will drag. If the cut landed on the strip's grid and the drag
// that followed it moved on the section grid, then nudging a boundary you had just placed would
// move it somewhere you did not put it -- and the two grids only differ at all when somebody has
// deliberately set them apart, which is exactly when they would notice. So the rule is: **a cut
// lands on the grid the thing it creates is dragged on.** Sections take the section grid; shots,
// clips, lyrics and cues take the strip's.
enum class SliceGrid : std::uint8_t { Strip, Section };

[[nodiscard]] inline SliceGrid sliceGridFor(StripLane lane) {
    return lane == StripLane::Sections ? SliceGrid::Section : SliceGrid::Strip;
}

// The block containing `seconds`, or -1. Half-open at the end, so two blocks butted edge to edge
// answer once between them rather than twice at the seam.
[[nodiscard]] inline int sliceBlockAt(std::span<const SliceBlock> blocks, double seconds) {
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        if (seconds >= blocks[i].start && seconds < blocks[i].end) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// The whole decision.
//
// `rawSeconds` is where the pointer is; `snappedSeconds` is where the grid would put the cut. Both,
// because they answer different halves of the question and conflating them cuts the wrong block:
// **the block is found at the pointer and the cut is tested against that block.** With beats on and
// the pointer near a shot's end, the snapped time can land past that end and inside the next shot --
// and slicing a block the person did not point at is a worse outcome than refusing. So a snap that
// leaves the pointed-at block is `WouldBeTooShort`, which is what it is: there is no legal cut here.
[[nodiscard]] inline SlicePlan planSlice(StripLane lane, std::span<const SliceBlock> blocks,
                                         double rawSeconds, double snappedSeconds, double minSeconds,
                                         int actorRow = -1) {
    SlicePlan plan;
    plan.kind = sliceKindFor(lane);
    plan.actorRow = actorRow;
    if (plan.kind == SliceKind::None) {
        plan.refusal = SliceRefusal::LaneNotSliceable;
        return plan;
    }
    // An actor lane needs to know *which* actor, and the strip's hit test answers -1 in the gaps
    // between rows. A slice with no row is a slice with no list of blocks to cut.
    if (plan.kind == SliceKind::ActorClip && actorRow < 0) {
        plan.refusal = SliceRefusal::NothingUnderPointer;
        return plan;
    }
    const int index = sliceBlockAt(blocks, rawSeconds);
    if (index < 0) {
        plan.refusal = SliceRefusal::NothingUnderPointer;
        return plan;
    }
    plan.index = index;
    const SliceBlock& block = blocks[static_cast<std::size_t>(index)];
    plan.atSeconds = snappedSeconds;
    // The same comparison the operations themselves make, so that a menu item enabled by this and
    // an operation refusing it cannot disagree. Strictly greater / strictly less: a cut exactly
    // `minSeconds` from an edge leaves a half of exactly the minimum, which `splitShot` accepts and
    // `song::splitSection` accepts, and rounding the two apart is how the menu and the operation
    // drifted the first time.
    if (!(snappedSeconds > block.start + minSeconds) || !(snappedSeconds < block.end - minSeconds)) {
        plan.refusal = SliceRefusal::WouldBeTooShort;
        return plan;
    }
    plan.refusal = SliceRefusal::None;
    return plan;
}

// What to tell somebody whose slice was refused. One line, in the status strip under the lanes.
//
// **There is no silent arm.** A cut that cannot happen has to say why, or the tool reads as broken
// on the one gesture -- clicking a fraction of a second from an edge -- that people will try first.
[[nodiscard]] inline const char* sliceRefusalMessage(SliceRefusal refusal) {
    switch (refusal) {
    case SliceRefusal::None: return "";
    case SliceRefusal::LaneNotSliceable: return "Nothing to slice in this lane";
    case SliceRefusal::NothingUnderPointer: return "Nothing under the pointer to slice";
    case SliceRefusal::WouldBeTooShort: return "Too close to an edge: a slice there would leave nothing";
    default: return "";
    }
}

// The noun for the status line a slice writes when it succeeds.
[[nodiscard]] inline const char* sliceKindName(SliceKind kind) {
    switch (kind) {
    case SliceKind::Shot: return "shot";
    case SliceKind::Section: return "section";
    case SliceKind::AudioClip: return "audio clip";
    case SliceKind::Overlay: return "lyric";
    case SliceKind::ActorClip: return "clip cue";
    case SliceKind::None:
    default: return "";
    }
}

// ---- what an arrow key moves the playhead by (ADR-357) --------------------------------------------
//
// **The step is the active snap mode's own unit, and Shift is the next unit up.** That is the whole
// idea, and it is the reason this is worth a function rather than four lines at a key handler: the
// arrows were bound to a *fixed* pair -- a frame plain, a beat with Shift -- which ignored the grid
// the strip was actually snapped to, so somebody working in beats got frames from the plain key and
// somebody working in frames got beats from the shifted one. Exactly backwards in one of the two.
//
//   | snap mode | arrow           | Shift + arrow                  |
//   |-----------|-----------------|--------------------------------|
//   | Beats     | one beat        | one bar (`beatsPerBar`)        |
//   | Frames    | one frame       | one second                     |
//   | Markers   | one marker      | one *section* marker           |
//   | Off       | 1% of the view  | 10% of the view                |
//
// It returns a *unit and a count* rather than a new time, and that is deliberate. The transport
// already knows how to walk a beat grid -- `Engine::beatBoundary` uses the analysed beats where
// there are any and the tempo where there are not -- and a pure function handed a vector of beats
// could not reproduce that fallback. So this decides the thing that is a decision, and the existing
// stepping does the thing it already does correctly.

enum class NudgeUnit : std::uint8_t {
    Frames,
    Beats,
    Markers,
    Seconds, // the Off arm: no grid, so the step is a distance
};

struct Nudge {
    NudgeUnit unit = NudgeUnit::Frames;
    int count = 0;         // signed, for Frames / Beats / Markers
    double seconds = 0.0;  // signed, for NudgeUnit::Seconds
    // Markers with Shift. The marker list is mostly cues; the section boundaries are the structural
    // landmarks within it, and they are what "the next unit up" means when the unit is a place.
    bool sectionsOnly = false;
};

// 1% of the visible span, and 10% for the coarse step.
//
// **View-relative, and only for Off.** With no grid there is no unit, so the only thing a step can
// be proportional to is what is on screen -- and a step that is a fixed number of seconds is either
// invisible at a four-minute view or enormous at a ten-second one. At a 10 s view these are 0.1 s
// and 1 s; at a 240 s view they are 2.4 s and 24 s, which is the same *gesture* at both.
inline constexpr double kNudgeViewFraction = 0.01;
inline constexpr double kNudgeCoarseViewFraction = 0.10;
// What Off falls back to before the strip has ever been drawn and there is no visible span to be a
// fraction of.
inline constexpr double kNudgeFallbackSeconds = 0.1;

// `snapMode` is the strip's `snapMode_`, which is `seq::SnapMode`'s ordering as an int: 0 Off,
// 1 Frames, 2 Beats, 3 Markers. The caller static_asserts that against the enum; this header
// deliberately does not include the sequencer to find out.
//
// `direction` is -1 or +1. `coarse` is Shift. `beatsPerBar` comes from `seq::BakeOptions` rather
// than being spelled 4 here, so that the day time-signature detection lands, one default changes and
// this follows it.
[[nodiscard]] inline Nudge arrowNudge(int snapMode, int direction, bool coarse, double fps,
                                      double viewSpanSeconds, int beatsPerBar = 4) {
    Nudge nudge;
    const int sign = direction < 0 ? -1 : 1;
    const int bar = beatsPerBar > 0 ? beatsPerBar : 1;
    switch (std::clamp(snapMode, 0, 3)) {
    case 1: // Frames
        nudge.unit = NudgeUnit::Frames;
        // A second, in frames. Not four frames: the unit above a frame is a second in every editor
        // that has a frame counter, and four frames is not a duration anybody thinks in.
        nudge.count = sign * (coarse ? std::max(1, static_cast<int>(std::lround(fps > 0.0 ? fps : 60.0)))
                                     : 1);
        break;
    case 2: // Beats
        nudge.unit = NudgeUnit::Beats;
        nudge.count = sign * (coarse ? bar : 1);
        break;
    case 3: // Markers
        nudge.unit = NudgeUnit::Markers;
        nudge.count = sign;
        // **Shift jumps to the next section rather than four markers along.** Marker spacing is
        // irregular, so "four markers" is a distance nobody can predict -- it could be four bars or
        // four minutes. A section boundary is the coarser *landmark*, which is the same relationship
        // a bar has to a beat, so somebody arriving from Beats finds Shift meaning what it meant
        // there: the bigger structural step. The caller falls back to plain marker stepping where a
        // piece has no sections, so Shift is never a dead key.
        nudge.sectionsOnly = coarse;
        break;
    case 0: // Off
    default: {
        nudge.unit = NudgeUnit::Seconds;
        const double fraction = coarse ? kNudgeCoarseViewFraction : kNudgeViewFraction;
        double step = viewSpanSeconds > 0.0 ? viewSpanSeconds * fraction
                                            : kNudgeFallbackSeconds * (coarse ? 10.0 : 1.0);
        // **Off must still move.** A fraction of a very short view can come out below a frame, and
        // an arrow key that rounds to nothing is a feature that vanished with the grid -- which is
        // the one thing turning snapping off must not do.
        const double oneFrame = 1.0 / (fps > 0.0 ? fps : 60.0);
        step = std::max(step, oneFrame);
        nudge.seconds = static_cast<double>(sign) * step;
        break;
    }
    }
    return nudge;
}

// The Off arm's landing place, clamped to the piece.
//
// Left at zero stops at zero and right past the end stops at the end: a playhead that ran negative
// would be a position no clock in the application can represent, and one that ran past the end would
// leave the transport and the strip disagreeing about where the piece was. The grid arms clamp in
// `Engine::seekSeconds`, which goes through `Transport::seek`; this one is here so that the arm with
// no grid is clamped by something a test can reach.
[[nodiscard]] inline double nudgedTime(double seconds, double delta, double durationSeconds) {
    const double end = durationSeconds > 0.0 ? durationSeconds : 0.0;
    return std::clamp(seconds + delta, 0.0, end);
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

// ---- the vortex's rows in the World Effects panel (ADR-387) -------------------------------------
//
// Declared here, as data, for the reason ADR-382's Tree-panel defect gives: that panel computed a
// parameter path by string arithmetic, got it five characters wrong, drew nothing, and said
// nothing about it -- and no test could catch it because the arithmetic lived inside an ImGui
// function nothing could call. A row list that is plain data can be walked by the panel and by a
// test, and the test's question is the panel's question: does every leaf this asks for exist on a
// vortex?
//
// ADR-392: the comet's and the aurora's rows are here too now. They were the mechanical change
// ADR-387 deferred, and deferring it had a cost -- for as long as those two kinds' rows were
// string literals inside an ImGui call, `conformance::checkLeavesExist` could be pointed at one of
// the family's three kinds and not at the other two. A leaf five characters wrong in either of
// them still drew an empty box and said nothing.
//
// The anchor combo in each advanced section stays inline: it writes a `SkyAnchor` enum on the
// effect, not a parameter, so it is not a row and pretending it were would put a leaf in the table
// that registration does not produce.
struct EffectRow {
    std::string_view section; // non-empty starts a new SeparatorText before this row
    std::string_view leaf;    // appended to `atmos/<name>/`
    std::string_view label;
    std::string_view format;    // empty = the panel's default
    bool logarithmic = false;
    bool color = false;
    // ADR-388: the row's own tooltip, empty for none. Per row rather than "whatever the panel last
    // drew", which is how it worked for one release and which meant adding a row at the end of a
    // list silently stole the tooltip off the row above it.
    std::string_view tip;
};

// What somebody reaches for first: what colour, how bright, how big, how fast.
[[nodiscard]] inline std::span<const EffectRow> vortexRows() {
    static constexpr EffectRow kRows[] = {
        {"", "colorDeep", "Deep colour", "", false, true},
        {"", "colorMid", "Mid colour", "", false, true},
        {"", "colorAccent", "Accent colour", "", false, true},
        {"", "emission", "Brightness", "%.3f /m"},
        {"", "density", "Thickness", "%.4f /m"},
        {"", "radius", "Mouth radius", "%.0f m", true},
        {"", "funnelDepth", "Funnel depth", "%.0f m", true},
        {"", "rotationSpeed", "Rotation", "%.3f rad/s"},
        {"", "swirl", "Swirl", ""},
        {"", "filaments", "Filaments", ""},
        {"", "smokeWarp", "Smoke", "", false, false,
         "Drags the fine detail into the big swirl instead of letting it sit on top as speckle.\n"
         "The single control that decides whether this reads as smoke or as noise -- raise it\n"
         "first, before reaching for anything else here."},
        {"", "smokeBillow", "Billow", "", false, false,
         "0 is wispy and filamentary; 1 is rounded, puffy masses with creases between them.\n"
         "The difference between a nebula and a smoke column."},
        {"", "detail", "Fine detail", "", false, false,
         "Weight of the finest noise octave. Detail below what the volume march can sample is\n"
         "faded out automatically, so raising this past the point where it stops changing the\n"
         "picture means the march is the limit, not this."},
        {"", "spill", "Light spill", "", false, false,
         "How much of the funnel's own light lands on the surfaces above it. Separate from\n"
         "Brightness so it can be tuned against the island without changing the funnel."},
        {"", "scattering", "Scene light inside", "", false, false,
         "At 0 the funnel makes its own light and the scene's lights do not appear inside it --\n"
         "a spotlight aimed up through it stops at its edge. Above 0 they do; watch the tree's\n"
         "key light, which is bright enough to flatten the whole funnel if this goes far."},
    };
    return kRows;
}

[[nodiscard]] inline std::span<const EffectRow> vortexAdvancedRows() {
    static constexpr EffectRow kRows[] = {
        {"Placement", "centerX", "Centre X", "%.1f m"},
        {"", "centerY", "Centre Y", "%.1f m"},
        {"", "centerZ", "Centre Z", "%.1f m", false, false,
         "Where the mouth of the funnel sits in the world. A particle system that names this\n"
         "vortex as its attractor follows it here, so the island and the funnel stay related\n"
         "when either of them moves."},
        {"Shape", "thickness", "Wall thickness", "%.0f m", true},
        {"", "throat", "Throat", "%.2f of mouth"},
        {"", "throatDensity", "Throat thickness", ""},
        {"", "innerVoid", "Inner void", ""},
        {"", "contrast", "Contrast", ""},
        {"Motion", "turbulence", "Turbulence", ""},
        {"", "turbulenceScale", "Turbulence scale", ""},
        {"", "breathAmount", "Breath amount", ""},
        {"", "breathSpeed", "Breath speed", ""},
        {"Comet response", "cometResponse", "Comet light", ""},
        {"", "cometReach", "Comet reach", "%.1f x", false, false,
         "How much of a comet's light this medium takes, and how far past the comet's ground\n"
         "pool it reaches. Off by default: the funnel must not scatter the scene's ordinary\n"
         "lights, which is what the control above is for."},
    };
    return kRows;
}

// ---- which codecs this machine can actually produce (ADR-383, audit G4) -------------------------
//
// The Render panel's codec list was a hardcoded array of eight, four of them ffmpeg-only, offered
// whether or not this build has a native encoder and whether or not an ffmpeg exists anywhere on
// the machine. The brief's own rule is "do not expose unavailable backends", and the shipping UI
// broke it one layer below where the brief was looking: choosing `libx265` with no ffmpeg installed
// is a render that fails at the moment the output is opened, after the project has been saved.
//
// A pure function of three facts so a test can ask it every combination without an encoder.
// `backend` is the panel's own setting -- "auto", "native" or "ffmpeg" -- because asking for
// `native` narrows the list further than `auto` does.
[[nodiscard]] inline std::vector<std::string> availableCodecs(std::span<const std::string> native,
                                                              bool haveFfmpeg,
                                                              std::string_view backend) {
    // Everything the ffmpeg path understands beyond the four the native backend shares with it.
    static const char* kFfmpegOnly[] = {"libx264", "libx265", "prores_ks", "libvpx-vp9"};
    std::vector<std::string> out;
    const bool wantNative = backend != "ffmpeg";
    const bool wantFfmpeg = backend != "native";
    if (wantNative) {
        out.assign(native.begin(), native.end());
    }
    if (wantFfmpeg && haveFfmpeg) {
        // The native four are already spelled the same way for ffmpeg (`planFfmpegCodec` maps
        // them), so under "ffmpeg" they stay offered rather than disappearing.
        if (!wantNative) {
            for (const char* n : {"prores4444", "prores422", "h264", "hevc"}) {
                out.emplace_back(n);
            }
        }
        for (const char* n : kFfmpegOnly) {
            out.emplace_back(n);
        }
    }
    return out;
}


// ---- comet ---------------------------------------------------------------------------------------
//
// Above the fold: what somebody reaches for first. Every leaf here is checked against what a comet
// actually registers by `tests/unit/test_effect_conformance.cpp`.
[[nodiscard]] inline std::span<const EffectRow> cometRows() {
    static constexpr EffectRow kRows[] = {
        {"", "coreColor", "Core colour", "", false, true},
        {"", "tailColor", "Tail colour", "", false, true},
        {"", "coreIntensity", "Core brightness"},
        {"", "headSize", "Head size", "%.0f m"},
        {"", "tailLength", "Tail length", "%.0f m"},
        {"", "tailWidth", "Tail width", "%.0f m"},
        {"", "travelSeconds", "Crossing", "%.1f s"},
    };
    return kRows;
}

// The advanced rows, minus the two checkboxes and the anchor combo, which are not parameters of
// this shape. The first row carries no section because the panel has already drawn "Trajectory"
// above the anchor combo.
[[nodiscard]] inline std::span<const EffectRow> cometAdvancedRows() {
    static constexpr EffectRow kRows[] = {
        {"", "startAzimuth", "Start bearing", "%.0f deg"},
        {"", "startElevation", "Start height", "%.0f deg"},
        {"", "endAzimuth", "End bearing", "%.0f deg"},
        {"", "endElevation", "End height", "%.0f deg"},
        {"", "distance", "Distance", "%.0f m", true},
        {"", "speed", "Speed"},
        {"", "acceleration", "Acceleration"},
        {"", "arcLift", "Arc lift", "%.0f m"},
        {"", "curvature", "Curvature", "%.0f m"},
        {"Appearance", "haloColor", "Halo colour", "", false, true},
        {"", "haloIntensity", "Halo brightness"},
        {"", "haloSize", "Halo size", "%.0f m"},
        {"", "tailIntensity", "Tail brightness"},
        {"", "tailFalloff", "Tail falloff"},
        {"", "wispAmount", "Wisp amount", "%.0f m"},
        {"", "wispScale", "Wisp scale", "%.4f"},
        {"", "flowSpeed", "Wisp flow"},
        {"Fragments", "sparkleDensity", "Density", "%.3f /m"},
        {"", "sparkleSize", "Size"},
        {"", "sparkleIntensity", "Brightness"},
        {"", "sparkleSpeed", "Twinkle"},
    };
    return kRows;
}

// ---- aurora --------------------------------------------------------------------------------------

[[nodiscard]] inline std::span<const EffectRow> auroraRows() {
    static constexpr EffectRow kRows[] = {
        {"", "lowColor", "Base colour", "", false, true},
        {"", "midColor", "Middle colour", "", false, true},
        {"", "topColor", "Top colour", "", false, true},
        {"", "intensity", "Brightness"},
        {"", "curtainHeight", "Height", "%.0f m"},
        {"", "curtains", "Curtains", "%.0f"},
        {"", "flowSpeed", "Flow"},
        {"", "audioSensitivity", "Audio response"},
        {"", "spectrumShape", "Spectrum shape"},
    };
    return kRows;
}

[[nodiscard]] inline std::span<const EffectRow> auroraAdvancedRows() {
    static constexpr EffectRow kRows[] = {
        {"", "radius", "Distance", "%.0f m", true},
        {"", "layerSpacing", "Layer spacing"},
        {"", "baseHeight", "Base height", "%.0f m"},
        {"", "waveAmplitude", "Wave amount"},
        {"", "waveScale", "Wave scale"},
        {"", "turbulence", "Turbulence"},
        {"", "complexity", "Ray structure", "%.0f"},
        {"", "driftSpeed", "Fold drift"},
        {"", "verticalSpeed", "Vertical drift"},
        {"Appearance", "emission", "Bloom weight"},
        {"", "opacity", "Curtain opacity"},
        {"", "edgeBrightness", "Edge brightness"},
        {"", "filaments", "Filaments"},
        {"", "sparkle", "Sparkle"},
        {"", "horizonGlow", "Horizon glow"},
        // Â§4.2's per-band depths. These scale the bands already in the frame block; they are not a
        // second analyzer, and every one of them is itself an ordinary parameter a route can drive.
        {"Audio response", "audioBass", "Bass -> height"},
        {"", "audioLowMid", "Low-mid -> waves"},
        {"", "audioMid", "Mid -> folds"},
        {"", "audioHigh", "High -> filaments"},
        {"", "audioBeat", "Beat -> pulse"},
    };
    return kRows;
}

// The hue-cycle rows, shared by the comet and the aurora. A vortex registers none of them, which is
// why the panel returns before this rather than drawing five rows that would find nothing.
[[nodiscard]] inline std::span<const EffectRow> skyRainbowRows() {
    static constexpr EffectRow kRows[] = {
        {"Rainbow", "rainbowSpeed", "Speed"},
        {"", "rainbowScale", "Scale"},
        {"", "rainbowHue", "Hue offset"},
        {"", "rainbowSaturation", "Saturation"},
        {"", "rainbowBrightness", "Brightness"},
    };
    return kRows;
}

// Offered only when the ground glow is not Off, so it is a separate table rather than a tail of the
// rainbow's.
[[nodiscard]] inline std::span<const EffectRow> skyGroundRows() {
    static constexpr EffectRow kRows[] = {
        {"Ground illumination", "groundRadius", "Radius", "%.0f m", true},
        {"", "groundFalloff", "Falloff"},
    };
    return kRows;
}

} // namespace avgen::ui
