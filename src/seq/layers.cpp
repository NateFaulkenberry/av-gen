#include "seq/layers.hpp"

#include "core/log.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace avgen::seq {
namespace {
using nlohmann::json;

constexpr std::array<std::pair<OverlayKind, const char*>, 3> kKinds{{
    {OverlayKind::Text, "text"},
    {OverlayKind::Shape, "shape"},
    {OverlayKind::Image, "image"},
}};

constexpr std::array<std::pair<OverlayPreset, const char*>, 7> kPresets{{
    {OverlayPreset::None, "none"},
    {OverlayPreset::FadeIn, "fadeIn"},
    {OverlayPreset::FadeOut, "fadeOut"},
    {OverlayPreset::FadeInOut, "fadeInOut"},
    {OverlayPreset::ScalePop, "scalePop"},
    {OverlayPreset::SlideUp, "slideUp"},
    {OverlayPreset::SlideDown, "slideDown"},
}};

template <typename E, std::size_t N>
const char* nameOf(const std::array<std::pair<E, const char*>, N>& table, E value) {
    for (const auto& [e, n] : table) {
        if (e == value) {
            return n;
        }
    }
    return table[0].second;
}

template <typename E, std::size_t N>
std::optional<E> valueOf(const std::array<std::pair<E, const char*>, N>& table, std::string_view name) {
    for (const auto& [e, n] : table) {
        if (name == n) {
            return e;
        }
    }
    return std::nullopt;
}

Result<float> readFloat(const json& j, const char* key, float fallback) {
    const auto it = j.find(key);
    if (it == j.end()) {
        return fallback;
    }
    if (!it->is_number()) {
        return fail("overlay '{}' must be a number", key);
    }
    return it->get<float>();
}

Result<glm::vec2> readVec2(const json& j, const char* key, glm::vec2 fallback) {
    const auto it = j.find(key);
    if (it == j.end()) {
        return fallback;
    }
    if (!it->is_array() || it->size() != 2) {
        return fail("overlay '{}' must be an array of 2 numbers", key);
    }
    for (const auto& v : *it) {
        if (!v.is_number()) {
            return fail("overlay '{}' must be an array of 2 numbers", key);
        }
    }
    return glm::vec2((*it)[0].get<float>(), (*it)[1].get<float>());
}

Result<glm::vec4> readVec4(const json& j, const char* key, glm::vec4 fallback) {
    const auto it = j.find(key);
    if (it == j.end()) {
        return fallback;
    }
    if (!it->is_array() || (it->size() != 3 && it->size() != 4)) {
        return fail("overlay '{}' must be an array of 3 or 4 numbers", key);
    }
    for (const auto& v : *it) {
        if (!v.is_number()) {
            return fail("overlay '{}' must be an array of numbers", key);
        }
    }
    glm::vec4 out = fallback;
    for (std::size_t i = 0; i < it->size(); ++i) {
        out[static_cast<glm::length_t>(i)] = (*it)[i].get<float>();
    }
    return out;
}

// One scalar track with explicit keys. Every overlay property the presets touch is a single
// component, so this is the only shape needed.
params::Track scalarTrack(const LayerTarget& target, std::vector<std::pair<double, float>> keys,
                          params::KeyInterp interp) {
    params::Track track;
    track.target = target.path;
    track.component = target.component;
    track.timeBase = params::TimeBase::Seconds;
    track.mode = params::TrackMode::Replace;
    for (const auto& [time, value] : keys) {
        params::Key k;
        k.time = time;
        k.value[0] = value;
        k.interp = interp;
        track.addKey(k);
    }
    return track;
}

// A hair before/after the cue, so "invisible outside the range" is a step rather than a ramp that
// began at the previous key, which may be many seconds earlier.
constexpr double kEdge = 1e-3;

} // namespace

const char* overlayKindName(OverlayKind kind) { return nameOf(kKinds, kind); }
std::optional<OverlayKind> overlayKindFromName(std::string_view name) { return valueOf(kKinds, name); }
const char* overlayPresetName(OverlayPreset preset) { return nameOf(kPresets, preset); }
std::optional<OverlayPreset> overlayPresetFromName(std::string_view name) { return valueOf(kPresets, name); }

Result<void> OverlayCue::validate() const {
    if (id.empty()) {
        return fail("overlay cue has no id");
    }
    if (!std::isfinite(startSeconds) || !std::isfinite(endSeconds)) {
        return fail("overlay '{}' has a non-finite time", id);
    }
    if (endSeconds <= startSeconds) {
        return fail("overlay '{}' ends at {:.3f}s, at or before its start {:.3f}s", id, endSeconds,
                    startSeconds);
    }
    if (presetSeconds < 0.0) {
        return fail("overlay '{}' has a negative preset duration", id);
    }
    return {};
}

json OverlayCue::toJson() const {
    json j{
        {"id", id},
        {"kind", overlayKindName(kind)},
        {"start", startSeconds},
        {"end", endSeconds},
        {"order", order},
        {"anchor", json::array({anchor.x, anchor.y})},
        {"pivot", json::array({pivot.x, pivot.y})},
        {"size", size},
        {"rotation", rotationDegrees},
        {"color", json::array({color.r, color.g, color.b, color.a})},
        {"preset", overlayPresetName(preset)},
        {"presetSeconds", presetSeconds},
    };
    if (!content.empty()) {
        j["content"] = content;
    }
    if (!style.empty()) {
        j["style"] = style;
    }
    if (!extra.is_null()) {
        j["extra"] = extra;
    }
    return j;
}

Result<OverlayCue> OverlayCue::fromJson(const json& j) {
    if (!j.is_object()) {
        return fail("overlay cue must be an object");
    }
    OverlayCue cue;
    const auto id = j.find("id");
    if (id == j.end() || !id->is_string()) {
        return fail("overlay cue is missing a string 'id'");
    }
    cue.id = id->get<std::string>();
    if (const auto k = j.find("kind"); k != j.end()) {
        if (!k->is_string()) {
            return fail("overlay '{}' kind must be a string", cue.id);
        }
        const auto parsed = overlayKindFromName(k->get<std::string>());
        if (!parsed) {
            return fail("overlay '{}': unknown kind '{}'", cue.id, k->get<std::string>());
        }
        cue.kind = *parsed;
    }
    if (const auto c = j.find("content"); c != j.end()) {
        if (!c->is_string()) {
            return fail("overlay '{}' content must be a string", cue.id);
        }
        cue.content = c->get<std::string>();
    }
    if (const auto s = j.find("style"); s != j.end()) {
        if (!s->is_string()) {
            return fail("overlay '{}' style must be a string", cue.id);
        }
        cue.style = s->get<std::string>();
    }
    const auto start = j.find("start");
    const auto end = j.find("end");
    if (start == j.end() || !start->is_number() || end == j.end() || !end->is_number()) {
        return fail("overlay '{}' needs numeric 'start' and 'end'", cue.id);
    }
    cue.startSeconds = start->get<double>();
    cue.endSeconds = end->get<double>();
    if (const auto o = j.find("order"); o != j.end()) {
        if (!o->is_number_integer()) {
            return fail("overlay '{}' order must be an integer", cue.id);
        }
        cue.order = o->get<int>();
    }
    auto anchor = readVec2(j, "anchor", cue.anchor);
    if (!anchor) {
        return std::unexpected(anchor.error());
    }
    cue.anchor = *anchor;
    auto pivot = readVec2(j, "pivot", cue.pivot);
    if (!pivot) {
        return std::unexpected(pivot.error());
    }
    cue.pivot = *pivot;
    auto size = readFloat(j, "size", cue.size);
    if (!size) {
        return std::unexpected(size.error());
    }
    cue.size = *size;
    auto rotation = readFloat(j, "rotation", cue.rotationDegrees);
    if (!rotation) {
        return std::unexpected(rotation.error());
    }
    cue.rotationDegrees = *rotation;
    auto color = readVec4(j, "color", cue.color);
    if (!color) {
        return std::unexpected(color.error());
    }
    cue.color = *color;
    if (const auto p = j.find("preset"); p != j.end()) {
        if (!p->is_string()) {
            return fail("overlay '{}' preset must be a string", cue.id);
        }
        const auto parsed = overlayPresetFromName(p->get<std::string>());
        if (!parsed) {
            return fail("overlay '{}': unknown preset '{}'", cue.id, p->get<std::string>());
        }
        cue.preset = *parsed;
    }
    // Read as a double, not a float: a round trip through a float turns 0.45 into
    // 0.44999998807907104, and a re-serialised project that differs from the one just loaded makes
    // every "did this change?" check in the editor and in a test useless.
    if (const auto ps = j.find("presetSeconds"); ps != j.end()) {
        if (!ps->is_number()) {
            return fail("overlay '{}' presetSeconds must be a number", cue.id);
        }
        cue.presetSeconds = ps->get<double>();
    }
    if (const auto e = j.find("extra"); e != j.end()) {
        cue.extra = *e;
    }
    if (auto ok = cue.validate(); !ok) {
        return std::unexpected(ok.error());
    }
    return cue;
}

Result<std::string> NullLayerSink::realise(const OverlayCue& cue) {
    // Declining is not an error. The sequence is allowed to carry lyrics before anything can draw
    // them; what would be wrong is pretending otherwise.
    declined_.push_back(cue.id);
    return std::string{};
}

LayerTarget NullLayerSink::target(std::string_view, std::string_view) const { return {}; }

Result<std::string> PrefixLayerSink::realise(const OverlayCue& cue) {
    realised_.push_back(cue.id);
    return cue.id;
}

LayerTarget PrefixLayerSink::target(std::string_view layerId, std::string_view property) const {
    // Split "scale.x" into a path and a component, the same way a real sink has to.
    const std::size_t dot = property.find('.');
    const std::string_view leaf = dot == std::string_view::npos ? property : property.substr(0, dot);
    int component = -1;
    if (dot != std::string_view::npos && dot + 1 < property.size()) {
        component = property[dot + 1] == 'y' ? 1 : 0;
    }
    return LayerTarget{prefix_ + std::string(layerId) + "/" + std::string(leaf), component};
}

std::vector<params::Track> overlayPresetTracks(const OverlayCue& cue, const LayerSink& sink,
                                               std::string_view layerId) {
    std::vector<params::Track> tracks;
    if (layerId.empty()) {
        return tracks;
    }
    const double in = std::min(cue.presetSeconds, cue.durationSeconds() * 0.5);
    const double start = cue.startSeconds;
    const double end = cue.endSeconds;
    const LayerTarget opacity = sink.target(layerId, overlay_property::kOpacity);
    if (!opacity.valid()) {
        return tracks;
    }

    // Opacity. Every preset writes it, because spec 23's "outside that range it is invisible" is
    // only true if something keys it to zero at both ends.
    std::vector<std::pair<double, float>> alpha;
    switch (cue.preset) {
    case OverlayPreset::None:
        alpha = {{start - kEdge, 0.0f}, {start, 1.0f}, {end, 1.0f}, {end + kEdge, 0.0f}};
        break;
    case OverlayPreset::FadeIn:
        alpha = {{start - kEdge, 0.0f}, {start, 0.0f}, {start + in, 1.0f}, {end, 1.0f}, {end + kEdge, 0.0f}};
        break;
    case OverlayPreset::FadeOut:
        alpha = {{start - kEdge, 0.0f}, {start, 1.0f}, {end - in, 1.0f}, {end, 0.0f}};
        break;
    case OverlayPreset::FadeInOut:
    case OverlayPreset::ScalePop:
    case OverlayPreset::SlideUp:
    case OverlayPreset::SlideDown:
        alpha = {{start - kEdge, 0.0f}, {start, 0.0f}, {start + in, 1.0f}, {end - in, 1.0f}, {end, 0.0f}};
        break;
    }
    // Step at the outer edges so a layer does not bleed backwards from the previous cue; the
    // interior is eased so a fade reads as a fade.
    params::Track opacityTrack = scalarTrack(opacity, alpha, params::KeyInterp::EaseInOut);
    if (!opacityTrack.keys.empty()) {
        opacityTrack.keys.front().interp = params::KeyInterp::Step;
    }
    tracks.push_back(std::move(opacityTrack));

    switch (cue.preset) {
    case OverlayPreset::ScalePop: {
        // The layer's own scale, not its type size: scaling the layer leaves the shaped glyphs and
        // the vertex buffer alone, so a pop costs 96 bytes of upload. Both axes, so the pop is
        // uniform and a wide frame does not stretch it.
        const std::vector<std::pair<double, float>> pop{
            {start, 0.72f}, {start + in, 1.06f}, {start + in * 1.8, 1.0f}, {end, 1.0f}};
        for (const std::string_view axis : {overlay_property::kScaleX, overlay_property::kScaleY}) {
            const LayerTarget t = sink.target(layerId, axis);
            if (t.valid()) {
                tracks.push_back(scalarTrack(t, pop, params::KeyInterp::EaseOut));
            }
        }
        break;
    }
    case OverlayPreset::SlideUp:
    case OverlayPreset::SlideDown: {
        const LayerTarget t = sink.target(layerId, overlay_property::kPositionY);
        if (t.valid()) {
            // Normalised frame units, absolute rather than relative: the track replaces the
            // position, so the keys are the anchor plus the travel rather than the travel alone.
            // A twentieth of the frame is a move you notice without it becoming the subject.
            const float travel = cue.preset == OverlayPreset::SlideUp ? -0.05f : 0.05f;
            const float home = cue.anchor.y;
            tracks.push_back(scalarTrack(t,
                                         {{start, home + travel},
                                          {start + in, home},
                                          {end - in, home},
                                          {end, home - travel * 0.4f}},
                                         params::KeyInterp::EaseOut));
        }
        break;
    }
    case OverlayPreset::None:
    case OverlayPreset::FadeIn:
    case OverlayPreset::FadeOut:
    case OverlayPreset::FadeInOut:
        break;
    }
    return tracks;
}

} // namespace avgen::seq
