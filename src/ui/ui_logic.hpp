#pragma once

// UI-facing pure helpers that can be unit-tested without Dear ImGui.

#include "params/modulation.hpp"

#include <cmath>
#include <cstdint>
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

inline std::pair<float, float> routeAmountBounds(const params::ModRoute& /*route*/) {
    return {-kRouteAmountLimit, kRouteAmountLimit};
}

// Replaces NaN/inf (e.g. from a hand-edited project file) so widgets never see them.
inline float sanitiseFinite(float value, float fallback = 0.0f) { return std::isfinite(value) ? value : fallback; }

} // namespace avgen::ui
