#pragma once

// UI-facing pure helpers that can be unit-tested without Dear ImGui.

#include "params/modulation.hpp"

#include <cmath>
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
inline std::pair<float, float> routeAmountBounds(const params::ModRoute& /*route*/) {
    return {-kRouteAmountLimit, kRouteAmountLimit};
}

// Replaces NaN/inf (e.g. from a hand-edited project file) so widgets never see them.
inline float sanitiseFinite(float value, float fallback = 0.0f) { return std::isfinite(value) ? value : fallback; }

} // namespace avgen::ui
