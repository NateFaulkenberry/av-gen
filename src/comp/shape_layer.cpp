#include "comp/shape_layer.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace avgen::comp {

const char* shapeKindName(ShapeKind kind) {
    switch (kind) {
    case ShapeKind::Rectangle:
        return "rectangle";
    case ShapeKind::Ellipse:
        return "ellipse";
    case ShapeKind::Line:
        return "line";
    }
    return "rectangle";
}

Result<ShapeKind> shapeKindFromName(std::string_view name) {
    if (name == "rectangle" || name == "rect") {
        return ShapeKind::Rectangle;
    }
    if (name == "ellipse" || name == "circle") {
        return ShapeKind::Ellipse;
    }
    if (name == "line") {
        return ShapeKind::Line;
    }
    return fail("unknown shape '{}' (rectangle|ellipse|line)", name);
}

ShapeLayer::ShapeLayer() { name = "Shape"; }

std::unique_ptr<Layer> ShapeLayer::clone() const {
    auto copy = std::make_unique<ShapeLayer>();
    copyCommonTo(*copy);
    copy->shape = shape;
    copy->size = size;
    copy->cornerRadius = cornerRadius;
    copy->strokeWidth = strokeWidth;
    copy->strokeColor = strokeColor;
    copy->glowRadius = glowRadius;
    copy->glowColor = glowColor;
    return copy;
}

glm::vec2 ShapeLayer::resolvedSize() const { return sizeParam_ != nullptr ? sizeParam_->value() : size; }

void ShapeLayer::attachKind(params::ParameterSet& set) {
    sizeParam_ = &addVec2(set, "size", size, 0.0f, 8.0f);
    radiusParam_ = &addFloat(set, "cornerRadius", cornerRadius, 0.0f, 4.0f);
    strokeParam_ = &addFloat(set, "strokeWidth", strokeWidth, 0.0f, 1.0f);
    strokeColorParam_ = &addColor(set, "strokeColor", strokeColor);
    glowParam_ = &addFloat(set, "glow", glowRadius, 0.0f, 1.0f);
    glowColorParam_ = &addColor(set, "glowColor", glowColor);
}

void ShapeLayer::detachKind() {
    sizeParam_ = nullptr;
    radiusParam_ = nullptr;
    strokeParam_ = nullptr;
    strokeColorParam_ = nullptr;
    glowParam_ = nullptr;
    glowColorParam_ = nullptr;
}

void ShapeLayer::pushAuthoredKind() {
    if (sizeParam_ == nullptr) {
        return;
    }
    sizeParam_->setBase(size);
    radiusParam_->setBase(cornerRadius);
    strokeParam_->setBase(strokeWidth);
    strokeColorParam_->setBase(strokeColor);
    glowParam_->setBase(glowRadius);
    glowColorParam_->setBase(glowColor);
}

void ShapeLayer::pullAuthoredKind() {
    if (sizeParam_ == nullptr) {
        return;
    }
    size = sizeParam_->base();
    cornerRadius = radiusParam_->base();
    strokeWidth = strokeParam_->base();
    strokeColor = strokeColorParam_->base();
    glowRadius = glowParam_->base();
    glowColor = glowColorParam_->base();
}

void ShapeLayer::collectPaths(std::vector<std::string>& out) const {
    out.push_back(parameterPath("size"));
    out.push_back(parameterPath("cornerRadius"));
    out.push_back(parameterPath("strokeWidth"));
    out.push_back(parameterPath("strokeColor"));
    out.push_back(parameterPath("glow"));
    out.push_back(parameterPath("glowColor"));
}

std::uint32_t ShapeLayer::buildGeometry(std::vector<LayerVertex>& vertices, std::vector<LayerRun>& runs,
                                        std::uint32_t firstItem, GlyphAtlas&) {
    // Box-normalised: the shape occupies 0..1 and the quad reaches kMargin past it on each side.
    // The vertex shader multiplies by the frame's box size, so nothing here depends on the size.
    const float lo = -kMargin;
    const float hi = 1.0f + kMargin;
    const auto vertex = [](float x, float y) {
        return LayerVertex{{x, y}, {x - 0.5f, y - 0.5f}, 0};
    };
    LayerRun run;
    run.firstVertex = static_cast<std::uint32_t>(vertices.size());
    run.item = firstItem;
    const LayerVertex bl = vertex(lo, lo);
    const LayerVertex br = vertex(hi, lo);
    const LayerVertex tr = vertex(hi, hi);
    const LayerVertex tl = vertex(lo, hi);
    for (const LayerVertex& v : {bl, br, tr, bl, tr, tl}) {
        LayerVertex copy = v;
        copy.item = firstItem;
        vertices.push_back(copy);
    }
    run.vertexCount = 6;
    runs.push_back(run);
    return 1;
}

void ShapeLayer::buildItems(const Frame& frame, double, LayerItem* items) const {
    LayerItem& item = items[0];
    const glm::vec2 box = resolvedSize();
    fillTransform(frame, item);
    // zw carries the box half extent in height units: the vertex shader scales the normalised
    // quad by it, and the fragment shader evaluates the distance field in the same units.
    item.xform1.z = box.x * 0.5f;
    item.xform1.w = box.y * 0.5f;

    const float alpha = std::clamp(resolvedOpacity(), 0.0f, 1.0f);
    const float minHalf = std::max(0.0f, std::min(box.x, box.y) * 0.5f);
    const float limit = std::max(box.x, box.y) * kMargin;
    const float radius = std::clamp(radiusParam_ != nullptr ? radiusParam_->value() : cornerRadius, 0.0f, minHalf);
    const float stroke = std::max(0.0f, strokeParam_ != nullptr ? strokeParam_->value() : strokeWidth);
    const float glow = std::clamp(glowParam_ != nullptr ? glowParam_->value() : glowRadius, 0.0f, limit);
    const glm::vec4 strokeCol = strokeColorParam_ != nullptr ? strokeColorParam_->value() : strokeColor;
    const glm::vec4 glowCol = glowColorParam_ != nullptr ? glowColorParam_->value() : glowColor;

    glm::vec4 fill = resolvedColor();
    fill.a *= alpha;
    item.color = fill;
    item.accent = glm::vec4(glm::vec3(strokeCol), strokeCol.a * alpha * (stroke > 0.0f ? 1.0f : 0.0f));
    item.glow = glm::vec4(glm::vec3(glowCol), glowCol.a * alpha * (glow > 0.0f ? 1.0f : 0.0f));
    const auto shader = shape == ShapeKind::Ellipse ? ItemShader::Ellipse : ItemShader::Box;
    item.params = glm::vec4(static_cast<float>(shader), stroke * 0.5f, radius, glow);
}

void ShapeLayer::writeJson(nlohmann::json& j) const {
    j["shape"] = shapeKindName(shape);
    j["size"] = layerVec2Json(size);
    if (cornerRadius > 0.0f) {
        j["cornerRadius"] = cornerRadius;
    }
    if (strokeWidth > 0.0f) {
        j["strokeWidth"] = strokeWidth;
        j["strokeColor"] = layerVec4Json(strokeColor);
    }
    if (glowRadius > 0.0f) {
        j["glow"] = glowRadius;
        j["glowColor"] = layerVec4Json(glowColor);
    }
}

Result<void> ShapeLayer::readJson(const nlohmann::json& j) {
    if (j.contains("shape")) {
        auto s = shapeKindFromName(j["shape"].get<std::string>());
        if (!s) {
            return std::unexpected(s.error());
        }
        shape = *s;
    }
    if (j.contains("size")) {
        auto v = layerReadVec2(j["size"], "shape size");
        if (!v) {
            return std::unexpected(v.error());
        }
        size = *v;
    }
    cornerRadius = j.value("cornerRadius", 0.0f);
    strokeWidth = j.value("strokeWidth", 0.0f);
    if (j.contains("strokeColor")) {
        auto v = layerReadVec4(j["strokeColor"], "strokeColor");
        if (!v) {
            return std::unexpected(v.error());
        }
        strokeColor = *v;
    }
    glowRadius = j.value("glow", 0.0f);
    if (j.contains("glowColor")) {
        auto v = layerReadVec4(j["glowColor"], "glowColor");
        if (!v) {
            return std::unexpected(v.error());
        }
        glowColor = *v;
    }
    return {};
}

} // namespace avgen::comp
