#pragma once

// UI-facing pure helpers that can be unit-tested without Dear ImGui.

#include "params/modulation.hpp"

#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

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
