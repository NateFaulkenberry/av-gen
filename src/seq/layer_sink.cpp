#include "seq/layer_sink.hpp"

#include "comp/shape_layer.hpp"
#include "comp/text_layer.hpp"
#include "core/log.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <utility>

namespace avgen::seq {
namespace {
using nlohmann::json;

constexpr std::array<std::pair<std::string_view, OverlayStyle>, 4> kStyles{{
    // A lyric: the default, because it is what the system is for. Small enough to sit under the
    // picture rather than over it, and heavy enough to hold against a bright frame.
    {"lyric", OverlayStyle{.family = "Helvetica Neue", .weight = 0.23f, .italic = false,
                           .sizeEm = 0.052f, .tracking = 0.045f, .lineSpacing = 1.15f,
                           .outlineWidth = 0.0f, .shadowOpacity = 0.45f,
                           .align = comp::TextAlign::Center}},
    // A title: the one piece of type allowed to be the subject of its frame.
    {"title", OverlayStyle{.family = "Helvetica Neue", .weight = 0.62f, .italic = false,
                           .sizeEm = 0.105f, .tracking = 0.10f, .lineSpacing = 1.05f,
                           .outlineWidth = 0.0f, .shadowOpacity = 0.35f,
                           .align = comp::TextAlign::Center}},
    // A caption: information, not voice. Small, wide-tracked, never centred over the action.
    {"caption", OverlayStyle{.family = "Helvetica Neue", .weight = 0.0f, .italic = false,
                             .sizeEm = 0.028f, .tracking = 0.14f, .lineSpacing = 1.2f,
                             .outlineWidth = 0.0f, .shadowOpacity = 0.3f,
                             .align = comp::TextAlign::Left}},
    // A credit: a caption that has stopped apologising.
    {"credit", OverlayStyle{.family = "Helvetica Neue", .weight = -0.2f, .italic = false,
                            .sizeEm = 0.034f, .tracking = 0.22f, .lineSpacing = 1.3f,
                            .outlineWidth = 0.0f, .shadowOpacity = 0.25f,
                            .align = comp::TextAlign::Center}},
}};

[[nodiscard]] float readNumber(const json& j, const char* key, float fallback) {
    const auto it = j.find(key);
    return it != j.end() && it->is_number() ? it->get<float>() : fallback;
}

[[nodiscard]] bool readBool(const json& j, const char* key, bool fallback) {
    const auto it = j.find(key);
    return it != j.end() && it->is_boolean() ? it->get<bool>() : fallback;
}

[[nodiscard]] std::string readString(const json& j, const char* key) {
    const auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string{};
}

[[nodiscard]] glm::vec2 readVec2(const json& j, const char* key, glm::vec2 fallback) {
    const auto it = j.find(key);
    if (it == j.end() || !it->is_array() || it->size() != 2) {
        return fallback;
    }
    if (!(*it)[0].is_number() || !(*it)[1].is_number()) {
        return fallback;
    }
    return glm::vec2((*it)[0].get<float>(), (*it)[1].get<float>());
}

[[nodiscard]] glm::vec4 readVec4(const json& j, const char* key, glm::vec4 fallback) {
    const auto it = j.find(key);
    if (it == j.end() || !it->is_array() || (it->size() != 3 && it->size() != 4)) {
        return fallback;
    }
    glm::vec4 out = fallback;
    for (std::size_t i = 0; i < it->size(); ++i) {
        if ((*it)[i].is_number()) {
            out[static_cast<glm::length_t>(i)] = (*it)[i].get<float>();
        }
    }
    return out;
}

} // namespace

OverlayStyle overlayStyle(std::string_view name) {
    for (const auto& [key, style] : kStyles) {
        if (key == name) {
            return style;
        }
    }
    return kStyles[0].second; // "lyric"
}

std::vector<std::string_view> overlayStyleNames() {
    std::vector<std::string_view> out;
    out.reserve(kStyles.size());
    for (const auto& [key, style] : kStyles) {
        out.push_back(key);
    }
    return out;
}

CompositionLayerSink::CompositionLayerSink(comp::LayerStack& stack, params::ParameterSet* params)
    : stack_(stack), params_(params) {}

std::string CompositionLayerSink::layerName(std::string_view cueId) {
    return std::string(kPrefix) + std::string(cueId);
}

bool CompositionLayerSink::ownedName(std::string_view name) { return name.starts_with(kPrefix); }

void CompositionLayerSink::clear() {
    retired_.clear();
    warnings_.clear();
    created_.clear();
    std::vector<std::uint32_t> doomed;
    for (const auto& layer : stack_.layers()) {
        if (ownedName(layer->name)) {
            doomed.push_back(layer->id);
            for (auto& path : layer->parameterPaths()) {
                retired_.push_back(std::move(path));
            }
        }
    }
    for (const std::uint32_t id : doomed) {
        if (const comp::Layer* layer = stack_.find(id); layer != nullptr && params_ != nullptr) {
            stack_.removeParameters(*params_, *layer);
        }
        (void)stack_.remove(id);
    }
}

Result<std::string> CompositionLayerSink::realise(const OverlayCue& cue) {
    const json& extra = cue.extra.is_object() ? cue.extra : json::object();

    comp::Layer* made = nullptr;
    switch (cue.kind) {
    case OverlayKind::Text: {
        const OverlayStyle style = overlayStyle(cue.style);
        comp::TextLayer& text = stack_.addText(cue.content, cue.startSeconds, cue.endSeconds);
        comp::FontDesc font;
        font.family = readString(extra, "font").empty() ? style.family : readString(extra, "font");
        font.weight = readNumber(extra, "weight", style.weight);
        font.italic = readBool(extra, "italic", style.italic);
        text.setFont(font);
        text.setTracking(readNumber(extra, "tracking", style.tracking));
        text.setLineSpacing(readNumber(extra, "lineSpacing", style.lineSpacing));
        if (const std::string align = readString(extra, "align"); !align.empty()) {
            if (auto parsed = comp::textAlignFromName(align)) {
                text.setAlign(*parsed);
            } else {
                warnings_.push_back(fmt::format("overlay '{}': unknown align '{}'", cue.id, align));
                text.setAlign(style.align);
            }
        } else {
            text.setAlign(style.align);
        }
        // `cue.size` is a multiplier on the style, not an absolute: a cue can say "a bit bigger
        // than a lyric" without knowing what a lyric is.
        text.size = readNumber(extra, "size", style.sizeEm) * cue.size;
        text.outlineWidth = readNumber(extra, "outlineWidth", style.outlineWidth);
        text.outlineColor = readVec4(extra, "outlineColor", text.outlineColor);
        text.glowRadius = readNumber(extra, "glow", 0.0f);
        text.glowColor = readVec4(extra, "glowColor", text.glowColor);
        text.shadowOpacity = readNumber(extra, "shadowOpacity", style.shadowOpacity);
        text.shadowOffset = readVec2(extra, "shadowOffset", text.shadowOffset);
        made = &text;
        break;
    }
    case OverlayKind::Shape: {
        comp::ShapeKind kind = comp::ShapeKind::Rectangle;
        if (!cue.content.empty()) {
            auto parsed = comp::shapeKindFromName(cue.content);
            if (!parsed) {
                return fail("overlay '{}': unknown shape '{}' (rectangle, ellipse, line)", cue.id,
                            cue.content);
            }
            kind = *parsed;
        }
        comp::ShapeLayer& shape = stack_.addShape(kind);
        shape.startTime = cue.startSeconds;
        shape.endTime = cue.endSeconds;
        shape.size = readVec2(extra, "size", glm::vec2(1.6f, 0.9f)) * cue.size;
        shape.cornerRadius = readNumber(extra, "cornerRadius", 0.0f);
        shape.strokeWidth = readNumber(extra, "strokeWidth", 0.0f);
        shape.strokeColor = readVec4(extra, "strokeColor", cue.color);
        shape.glowRadius = readNumber(extra, "glow", 0.0f);
        shape.glowColor = readVec4(extra, "glowColor", shape.glowColor);
        made = &shape;
        break;
    }
    case OverlayKind::Image:
        // Honest rather than silent: there is no image layer kind (ADR-083 has Text and Shape), so
        // the cue keeps its timing, contributes no tracks, and says why.
        warnings_.push_back(fmt::format(
            "overlay '{}' is an image cue; the composition system has no image layer, so it draws "
            "nothing",
            cue.id));
        return std::string{};
    }

    made->name = layerName(cue.id);
    made->position = cue.anchor;
    made->anchor = cue.pivot;
    made->rotation = cue.rotationDegrees;
    made->color = cue.color;
    made->opacity = 1.0f; // the preset tracks own opacity; a base of 1 is what they multiply into
    if (const std::string blend = readString(extra, "blend"); !blend.empty()) {
        if (auto parsed = comp::blendModeFromName(blend)) {
            made->blend = *parsed;
        } else {
            warnings_.push_back(fmt::format("overlay '{}': unknown blend '{}'", cue.id, blend));
        }
    }
    if (params_ != nullptr) {
        made->attach(*params_);
    }
    created_.push_back(made->id);
    return std::to_string(made->id);
}

LayerTarget CompositionLayerSink::target(std::string_view layerId, std::string_view property) const {
    std::uint32_t id = 0;
    const auto [ptr, ec] = std::from_chars(layerId.data(), layerId.data() + layerId.size(), id);
    if (ec != std::errc{} || ptr != layerId.data() + layerId.size()) {
        return {};
    }
    const comp::Layer* layer = stack_.find(id);
    if (layer == nullptr) {
        return {};
    }
    // "scale.x" -> the vec2 parameter "scale", component 0.
    const std::size_t dot = property.find('.');
    const std::string_view leaf = dot == std::string_view::npos ? property : property.substr(0, dot);
    int component = -1;
    if (dot != std::string_view::npos && dot + 1 < property.size()) {
        component = property[dot + 1] == 'y' ? 1 : 0;
    }
    std::string path = layer->parameterPath(leaf);
    // The path must actually be registered, or the track binds to nothing and says nothing.
    if (params_ != nullptr && params_->find(path) == nullptr) {
        return {};
    }
    return LayerTarget{std::move(path), component};
}

} // namespace avgen::seq
