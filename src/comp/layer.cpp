#include "comp/layer.hpp"

#include "comp/shape_layer.hpp"
#include "comp/text_layer.hpp"
#include "core/log.hpp"

#include <fmt/format.h>
#include <glm/gtc/constants.hpp>
#include <nlohmann/json.hpp>

#include <cmath>

namespace avgen::comp {

const char* layerKindName(LayerKind kind) {
    switch (kind) {
    case LayerKind::Text:
        return "text";
    case LayerKind::Shape:
        return "shape";
    }
    return "text";
}

Result<LayerKind> layerKindFromName(std::string_view name) {
    if (name == "text") {
        return LayerKind::Text;
    }
    if (name == "shape") {
        return LayerKind::Shape;
    }
    return fail("unknown layer kind '{}' (text|shape)", name);
}

const char* blendModeName(BlendMode mode) {
    switch (mode) {
    case BlendMode::Normal:
        return "normal";
    case BlendMode::Additive:
        return "add";
    case BlendMode::Screen:
        return "screen";
    case BlendMode::Multiply:
        return "multiply";
    }
    return "normal";
}

Result<BlendMode> blendModeFromName(std::string_view name) {
    if (name == "normal") {
        return BlendMode::Normal;
    }
    if (name == "add" || name == "additive") {
        return BlendMode::Additive;
    }
    if (name == "screen") {
        return BlendMode::Screen;
    }
    if (name == "multiply") {
        return BlendMode::Multiply;
    }
    return fail("unknown blend mode '{}' (normal|add|screen|multiply)", name);
}

// ---- parameters -------------------------------------------------------------------------------

std::string Layer::parameterPath(std::string_view property) const {
    return fmt::format("layers/{}/{}", id, property);
}

params::Parameter<float>& Layer::addFloat(params::ParameterSet& set, std::string_view property, float value, float lo,
                                          float hi) {
    params::ParamDesc<float> desc;
    desc.path = parameterPath(property);
    desc.defaultValue = value;
    desc.hardMin = lo;
    desc.hardMax = hi;
    desc.label = fmt::format("#{} {}", id, property);
    desc.group = "layers";
    auto& param = set.add(desc);
    param.setBase(value);
    return param;
}

params::Parameter<glm::vec2>& Layer::addVec2(params::ParameterSet& set, std::string_view property, glm::vec2 value,
                                             float lo, float hi) {
    params::ParamDesc<glm::vec2> desc;
    desc.path = parameterPath(property);
    desc.defaultValue = value;
    desc.hardMin = glm::vec2(lo);
    desc.hardMax = glm::vec2(hi);
    desc.label = fmt::format("#{} {}", id, property);
    desc.group = "layers";
    auto& param = set.add(desc);
    param.setBase(value);
    return param;
}

params::Parameter<glm::vec4>& Layer::addColor(params::ParameterSet& set, std::string_view property, glm::vec4 value) {
    params::ParamDesc<glm::vec4> desc;
    desc.path = parameterPath(property);
    desc.defaultValue = value;
    desc.hardMin = glm::vec4(0.0f);
    desc.hardMax = glm::vec4(1.0f);
    desc.label = fmt::format("#{} {}", id, property);
    desc.group = "layers";
    desc.isColor = true;
    auto& param = set.add(desc);
    param.setBase(value);
    return param;
}

void Layer::attach(params::ParameterSet& set) {
    // Position is unclamped beyond the frame on purpose: an entrance starts off screen, and a hard
    // range of 0..1 would quietly clamp every entrance keyframe to the edge of the picture.
    position_ = &addVec2(set, "position", position, -4.0f, 5.0f);
    scale_ = &addVec2(set, "scale", scale, 0.0f, 20.0f);
    rotation_ = &addFloat(set, "rotation", rotation, -3600.0f, 3600.0f);
    anchor_ = &addVec2(set, "anchor", anchor, -2.0f, 3.0f);
    opacity_ = &addFloat(set, "opacity", opacity, 0.0f, 1.0f);
    color_ = &addColor(set, "color", color);
    attachKind(set);
}

void Layer::detach() {
    position_ = nullptr;
    scale_ = nullptr;
    rotation_ = nullptr;
    anchor_ = nullptr;
    opacity_ = nullptr;
    color_ = nullptr;
    detachKind();
}

void Layer::pushAuthored() {
    if (position_ != nullptr) {
        position_->setBase(position);
        scale_->setBase(scale);
        rotation_->setBase(rotation);
        anchor_->setBase(anchor);
        opacity_->setBase(opacity);
        color_->setBase(color);
    }
    pushAuthoredKind();
}

void Layer::pullAuthored() {
    if (position_ != nullptr) {
        position = position_->base();
        scale = scale_->base();
        rotation = rotation_->base();
        anchor = anchor_->base();
        opacity = opacity_->base();
        color = color_->base();
    }
    pullAuthoredKind();
}

std::vector<std::string> Layer::parameterPaths() const {
    std::vector<std::string> out{parameterPath("position"), parameterPath("scale"),   parameterPath("rotation"),
                                parameterPath("anchor"),   parameterPath("opacity"), parameterPath("color")};
    collectPaths(out);
    return out;
}

glm::vec2 Layer::resolvedPosition() const { return position_ != nullptr ? position_->value() : position; }
glm::vec2 Layer::resolvedScale() const { return scale_ != nullptr ? scale_->value() : scale; }
float Layer::resolvedRotation() const { return rotation_ != nullptr ? rotation_->value() : rotation; }
glm::vec2 Layer::resolvedAnchor() const { return anchor_ != nullptr ? anchor_->value() : anchor; }
float Layer::resolvedOpacity() const { return opacity_ != nullptr ? opacity_->value() : opacity; }
glm::vec4 Layer::resolvedColor() const { return color_ != nullptr ? color_->value() : color; }

void Layer::fillTransform(const Frame& frame, LayerItem& item) const {
    const float unit = unitScale(frame);
    const glm::vec2 s = resolvedScale();
    const float a = unit * s.x;
    const float b = unit * s.y;
    const float theta = glm::radians(resolvedRotation());
    const float c = std::cos(theta);
    const float sn = std::sin(theta);
    // Composition space has y up; the render target has y down, so the vertical axis is negated
    // and the rotation runs the other way round in the matrix. Written out rather than assembled
    // from a flip times a rotation, because the flip-then-rotate order is exactly what gets got
    // wrong and a matrix product hides which one happened.
    item.xform0 = glm::vec4(a * c, -b * sn, -a * sn, -b * c);
    const glm::vec2 anchorLocal = resolvedAnchor() * boxLocal();
    const glm::vec2 origin = frame.toPixels(resolvedPosition());
    item.xform1.x = origin.x - (item.xform0.x * anchorLocal.x + item.xform0.y * anchorLocal.y);
    item.xform1.y = origin.y - (item.xform0.z * anchorLocal.x + item.xform0.w * anchorLocal.y);
    // The identity box: the vertex shader multiplies local coordinates by twice this, so a layer
    // whose vertices are already in final local units (text, in em) leaves it at a half.
    item.xform1.z = 0.5f;
    item.xform1.w = 0.5f;
}

void Layer::copyCommonTo(Layer& other) const {
    other.id = id;
    other.name = name;
    other.enabled = enabled;
    other.blend = blend;
    other.startTime = startTime;
    other.endTime = endTime;
    other.position = position;
    other.scale = scale;
    other.rotation = rotation;
    other.anchor = anchor;
    other.opacity = opacity;
    other.color = color;
}

// ---- JSON ---------------------------------------------------------------------------------------

namespace {

nlohmann::json vec2Json(glm::vec2 v) { return nlohmann::json::array({v.x, v.y}); }
nlohmann::json vec4Json(glm::vec4 v) { return nlohmann::json::array({v.x, v.y, v.z, v.w}); }

Result<glm::vec2> readVec2(const nlohmann::json& j, const char* what) {
    if (!j.is_array() || j.size() != 2 || !j[0].is_number() || !j[1].is_number()) {
        return fail("{} must be [x, y]", what);
    }
    return glm::vec2(j[0].get<float>(), j[1].get<float>());
}

Result<glm::vec4> readVec4(const nlohmann::json& j, const char* what) {
    if (!j.is_array() || (j.size() != 3 && j.size() != 4)) {
        return fail("{} must be [r, g, b] or [r, g, b, a]", what);
    }
    glm::vec4 v(0.0f, 0.0f, 0.0f, 1.0f);
    for (std::size_t i = 0; i < j.size(); ++i) {
        if (!j[i].is_number()) {
            return fail("{} must be numbers", what);
        }
        v[static_cast<glm::length_t>(i)] = j[i].get<float>();
    }
    return v;
}

} // namespace

Result<glm::vec2> layerReadVec2(const nlohmann::json& j, const char* what) { return readVec2(j, what); }
Result<glm::vec4> layerReadVec4(const nlohmann::json& j, const char* what) { return readVec4(j, what); }
nlohmann::json layerVec2Json(glm::vec2 v) { return vec2Json(v); }
nlohmann::json layerVec4Json(glm::vec4 v) { return vec4Json(v); }

nlohmann::json Layer::toJson() const {
    nlohmann::json j = nlohmann::json::object();
    j["id"] = id;
    j["kind"] = layerKindName(kind());
    j["name"] = name;
    j["enabled"] = enabled;
    j["blend"] = blendModeName(blend);
    j["start"] = startTime;
    j["end"] = endTime;
    j["position"] = vec2Json(position);
    j["scale"] = vec2Json(scale);
    j["rotation"] = rotation;
    j["anchor"] = vec2Json(anchor);
    j["opacity"] = opacity;
    j["color"] = vec4Json(color);
    writeJson(j);
    return j;
}

Result<std::unique_ptr<Layer>> Layer::fromJson(const nlohmann::json& j) {
    if (!j.is_object()) {
        return fail("a layer must be an object");
    }
    const auto kindName = j.value("kind", std::string("text"));
    auto kind = layerKindFromName(kindName);
    if (!kind) {
        return std::unexpected(kind.error());
    }
    std::unique_ptr<Layer> layer;
    switch (*kind) {
    case LayerKind::Text:
        layer = std::make_unique<TextLayer>();
        break;
    case LayerKind::Shape:
        layer = std::make_unique<ShapeLayer>();
        break;
    }
    layer->id = j.value("id", 0u);
    layer->name = j.value("name", std::string(layerKindName(*kind)));
    layer->enabled = j.value("enabled", true);
    if (j.contains("blend")) {
        auto blend = blendModeFromName(j["blend"].get<std::string>());
        if (!blend) {
            return std::unexpected(blend.error());
        }
        layer->blend = *blend;
    }
    layer->startTime = j.value("start", 0.0);
    layer->endTime = j.value("end", 0.0);
    if (j.contains("position")) {
        auto v = readVec2(j["position"], "layer position");
        if (!v) {
            return std::unexpected(v.error());
        }
        layer->position = *v;
    }
    if (j.contains("scale")) {
        auto v = readVec2(j["scale"], "layer scale");
        if (!v) {
            return std::unexpected(v.error());
        }
        layer->scale = *v;
    }
    layer->rotation = j.value("rotation", 0.0f);
    if (j.contains("anchor")) {
        auto v = readVec2(j["anchor"], "layer anchor");
        if (!v) {
            return std::unexpected(v.error());
        }
        layer->anchor = *v;
    }
    layer->opacity = j.value("opacity", 1.0f);
    if (j.contains("color")) {
        auto v = readVec4(j["color"], "layer color");
        if (!v) {
            return std::unexpected(v.error());
        }
        layer->color = *v;
    }
    if (auto r = layer->readJson(j); !r) {
        return std::unexpected(r.error());
    }
    layer->markGeometryDirty();
    return layer;
}

} // namespace avgen::comp
