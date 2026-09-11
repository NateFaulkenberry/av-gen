#include "comp/text_layer.hpp"

#include "comp/glyph_atlas.hpp"
#include "core/log.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

namespace avgen::comp {

const char* textAlignName(TextAlign align) {
    switch (align) {
    case TextAlign::Left:
        return "left";
    case TextAlign::Center:
        return "center";
    case TextAlign::Right:
        return "right";
    }
    return "center";
}

Result<TextAlign> textAlignFromName(std::string_view name) {
    if (name == "left") {
        return TextAlign::Left;
    }
    if (name == "center" || name == "centre") {
        return TextAlign::Center;
    }
    if (name == "right") {
        return TextAlign::Right;
    }
    return fail("unknown text alignment '{}' (left|center|right)", name);
}

TextLayer::TextLayer() { name = "Text"; }

std::unique_ptr<Layer> TextLayer::clone() const {
    auto copy = std::make_unique<TextLayer>();
    copyCommonTo(*copy);
    copy->text_ = text_;
    copy->font_ = font_;
    copy->align_ = align_;
    copy->tracking_ = tracking_;
    copy->lineSpacing_ = lineSpacing_;
    copy->size = size;
    copy->outlineWidth = outlineWidth;
    copy->outlineColor = outlineColor;
    copy->glowRadius = glowRadius;
    copy->glowColor = glowColor;
    copy->shadowOffset = shadowOffset;
    copy->shadowOpacity = shadowOpacity;
    return copy;
}

void TextLayer::setText(std::string text) {
    if (text_ != text) {
        text_ = std::move(text);
        markGeometryDirty();
    }
}

void TextLayer::setFont(FontDesc font) {
    if (!(font_ == font)) {
        font_ = std::move(font);
        reportedSubstitution_ = false;
        markGeometryDirty();
    }
}

void TextLayer::setAlign(TextAlign align) {
    if (align_ != align) {
        align_ = align;
        markGeometryDirty();
    }
}

void TextLayer::setTracking(float em) {
    if (tracking_ != em) {
        tracking_ = em;
        markGeometryDirty();
    }
}

void TextLayer::setLineSpacing(float multiple) {
    if (lineSpacing_ != multiple) {
        lineSpacing_ = multiple;
        markGeometryDirty();
    }
}

float TextLayer::resolvedSize() const { return sizeParam_ != nullptr ? sizeParam_->value() : size; }

float TextLayer::unitScale(const Frame& frame) const { return resolvedSize() * frame.unit(); }

void TextLayer::attachKind(params::ParameterSet& set) {
    sizeParam_ = &addFloat(set, "size", size, 0.0f, 4.0f);
    outlineParam_ = &addFloat(set, "outlineWidth", outlineWidth, 0.0f, GlyphAtlas::kSpreadEm);
    outlineColorParam_ = &addColor(set, "outlineColor", outlineColor);
    glowParam_ = &addFloat(set, "glow", glowRadius, 0.0f, GlyphAtlas::kSpreadEm);
    glowColorParam_ = &addColor(set, "glowColor", glowColor);
    shadowOffsetParam_ = &addVec2(set, "shadowOffset", shadowOffset, -GlyphAtlas::kSpreadEm, GlyphAtlas::kSpreadEm);
    shadowOpacityParam_ = &addFloat(set, "shadowOpacity", shadowOpacity, 0.0f, 1.0f);
}

void TextLayer::detachKind() {
    sizeParam_ = nullptr;
    outlineParam_ = nullptr;
    outlineColorParam_ = nullptr;
    glowParam_ = nullptr;
    glowColorParam_ = nullptr;
    shadowOffsetParam_ = nullptr;
    shadowOpacityParam_ = nullptr;
}

void TextLayer::pushAuthoredKind() {
    if (sizeParam_ == nullptr) {
        return;
    }
    sizeParam_->setBase(size);
    outlineParam_->setBase(outlineWidth);
    outlineColorParam_->setBase(outlineColor);
    glowParam_->setBase(glowRadius);
    glowColorParam_->setBase(glowColor);
    shadowOffsetParam_->setBase(shadowOffset);
    shadowOpacityParam_->setBase(shadowOpacity);
}

void TextLayer::pullAuthoredKind() {
    if (sizeParam_ == nullptr) {
        return;
    }
    size = sizeParam_->base();
    outlineWidth = outlineParam_->base();
    outlineColor = outlineColorParam_->base();
    glowRadius = glowParam_->base();
    glowColor = glowColorParam_->base();
    shadowOffset = shadowOffsetParam_->base();
    shadowOpacity = shadowOpacityParam_->base();
}

void TextLayer::collectPaths(std::vector<std::string>& out) const {
    out.push_back(parameterPath("size"));
    out.push_back(parameterPath("outlineWidth"));
    out.push_back(parameterPath("outlineColor"));
    out.push_back(parameterPath("glow"));
    out.push_back(parameterPath("glowColor"));
    out.push_back(parameterPath("shadowOffset"));
    out.push_back(parameterPath("shadowOpacity"));
}

std::uint32_t TextLayer::buildGeometry(std::vector<LayerVertex>& vertices, std::vector<LayerRun>& runs,
                                       std::uint32_t firstItem, GlyphAtlas& atlas) {
    box_ = glm::vec2(0.0f, 1.0f);
    resolved_ = fontBackend().resolve(font_);
    if (!resolved_.available) {
        if (!reportedSubstitution_) {
            reportedSubstitution_ = true;
            log::error("composition: layer '{}' asked for font '{}' and this machine has no usable face; "
                       "the layer will not draw",
                       name, font_.family);
        }
        return 0;
    }
    if (resolved_.substituted && !reportedSubstitution_) {
        reportedSubstitution_ = true;
        // Loud, and once. A render whose type quietly changed is a render whose determinism claim
        // is false, so this is an error-level line and not a debug note.
        log::error("composition: layer '{}' asked for font '{}'{} and got '{}' ({}); this machine does not have "
                   "the requested face, so this render will not match one made where it is installed",
                   name, font_.family, font_.postScriptName.empty() ? "" : (" / " + font_.postScriptName),
                   resolved_.postScriptName, resolved_.family);
    }
    auto shaped = fontBackend().shape(font_, text_);
    if (!shaped) {
        log::warn("composition: layer '{}': {}", name, shaped.error().message);
        return 0;
    }
    const std::uint32_t lineCount = std::max(1u, shaped->lineCount());
    const float step = shaped->lineHeight * lineSpacing_;

    // Line widths including tracking, and the box they all sit in.
    std::vector<float> widths(lineCount, 0.0f);
    std::vector<std::uint32_t> glyphsInLine(lineCount, 0);
    for (const ShapedGlyph& g : shaped->glyphs) {
        if (g.line < lineCount) {
            ++glyphsInLine[g.line];
        }
    }
    for (std::uint32_t i = 0; i < lineCount; ++i) {
        const float base = i < shaped->lineWidths.size() ? shaped->lineWidths[i] : 0.0f;
        widths[i] = base + (glyphsInLine[i] > 1 ? tracking_ * static_cast<float>(glyphsInLine[i] - 1) : 0.0f);
        box_.x = std::max(box_.x, widths[i]);
    }
    box_.y = static_cast<float>(lineCount - 1) * step + shaped->ascent + shaped->descent;
    if (!(box_.y > 0.0f)) {
        box_.y = 1.0f;
    }

    // Local space: origin at the bottom left of the box, y up. Baselines counted down from the top.
    std::vector<LayerVertex> quads;
    quads.reserve(shaped->glyphs.size() * 6);
    std::vector<std::uint32_t> indexInLine(lineCount, 0);
    for (const ShapedGlyph& g : shaped->glyphs) {
        const std::uint32_t line = std::min(g.line, lineCount - 1);
        const std::uint32_t index = indexInLine[line]++;
        const GlyphAtlas::Entry* entry = atlas.acquire(font_, g.glyphId);
        if (entry == nullptr || !entry->drawable()) {
            continue;
        }
        float lineX = 0.0f;
        switch (align_) {
        case TextAlign::Left:
            lineX = 0.0f;
            break;
        case TextAlign::Center:
            lineX = (box_.x - widths[line]) * 0.5f;
            break;
        case TextAlign::Right:
            lineX = box_.x - widths[line];
            break;
        }
        const float baseline = box_.y - shaped->ascent - static_cast<float>(line) * step;
        const float x0 = lineX + g.x + tracking_ * static_cast<float>(index) + entry->originX;
        const float y0 = baseline + entry->originY;
        const float x1 = x0 + entry->sizeX;
        const float y1 = y0 + entry->sizeY;
        // Atlas row 0 is the top of the glyph (CoreGraphics bitmaps are stored top row first), so
        // the quad's bottom edge takes v1 and its top edge takes v0.
        const LayerVertex bl{{x0, y0}, {entry->u0, entry->v1}, 0};
        const LayerVertex br{{x1, y0}, {entry->u1, entry->v1}, 0};
        const LayerVertex tr{{x1, y1}, {entry->u1, entry->v0}, 0};
        const LayerVertex tl{{x0, y1}, {entry->u0, entry->v0}, 0};
        quads.push_back(bl);
        quads.push_back(br);
        quads.push_back(tr);
        quads.push_back(bl);
        quads.push_back(tr);
        quads.push_back(tl);
    }
    if (quads.empty()) {
        return 0;
    }
    // Two runs over the same glyphs: the drop shadow underneath, then the text. The shadow run is
    // always built, never conditionally, so that shadow opacity can be keyframed from nothing to
    // something without rebuilding a vertex buffer mid-animation. A run whose items are fully
    // transparent is dropped from the draw list each frame, so an unused shadow costs no fill.
    for (std::uint32_t pass = 0; pass < 2; ++pass) {
        LayerRun run;
        run.firstVertex = static_cast<std::uint32_t>(vertices.size());
        run.vertexCount = static_cast<std::uint32_t>(quads.size());
        run.item = firstItem + pass;
        run.secondary = pass == 0; // the shadow
        for (LayerVertex v : quads) {
            v.item = run.item;
            vertices.push_back(v);
        }
        runs.push_back(run);
    }
    return 2;
}

void TextLayer::buildItems(const Frame& frame, double, LayerItem* items) const {
    const float alpha = std::clamp(resolvedOpacity(), 0.0f, 1.0f);
    const float outline = outlineParam_ != nullptr ? outlineParam_->value() : outlineWidth;
    const float glow = glowParam_ != nullptr ? glowParam_->value() : glowRadius;
    const glm::vec4 outlineCol = outlineColorParam_ != nullptr ? outlineColorParam_->value() : outlineColor;
    const glm::vec4 glowCol = glowColorParam_ != nullptr ? glowColorParam_->value() : glowColor;
    const glm::vec2 shadowOff = shadowOffsetParam_ != nullptr ? shadowOffsetParam_->value() : shadowOffset;
    const float shadowA = shadowOpacityParam_ != nullptr ? shadowOpacityParam_->value() : shadowOpacity;
    // em to signed-distance units: the field encodes kSpreadEm either side of the edge over the
    // 0..1 range, so half of that range is one spread.
    const float toSdf = 1.0f / (2.0f * GlyphAtlas::kSpreadEm);
    const float outlineSdf = std::clamp(outline * toSdf, 0.0f, 0.5f);
    const float glowSdf = std::clamp(glow * toSdf, 0.0f, 0.5f);

    LayerItem& shadow = items[0];
    LayerItem& main = items[1];
    fillTransform(frame, main);
    shadow.xform0 = main.xform0;
    shadow.xform1 = main.xform1;
    // The shadow is offset in the layer's own space, so it leans with the layer when it rotates.
    shadow.xform1.x += main.xform0.x * shadowOff.x + main.xform0.y * shadowOff.y;
    shadow.xform1.y += main.xform0.z * shadowOff.x + main.xform0.w * shadowOff.y;

    const auto shader = static_cast<float>(ItemShader::Glyph);
    glm::vec4 fill = resolvedColor();
    fill.a *= alpha;
    main.color = fill;
    main.accent = glm::vec4(glm::vec3(outlineCol), outlineCol.a * alpha);
    main.glow = glm::vec4(glm::vec3(glowCol), glowCol.a * alpha);
    main.params = glm::vec4(shader, outlineSdf, 0.0f, glowSdf);

    const float shadowAlpha = std::clamp(shadowA, 0.0f, 1.0f) * alpha;
    shadow.color = glm::vec4(0.0f, 0.0f, 0.0f, shadowAlpha);
    shadow.accent = glm::vec4(0.0f, 0.0f, 0.0f, shadowAlpha);
    shadow.glow = glm::vec4(0.0f);
    shadow.params = glm::vec4(shader, outlineSdf, 0.0f, 0.0f);
}

void TextLayer::writeJson(nlohmann::json& j) const {
    j["text"] = text_;
    j["font"] = font_.toJson();
    j["size"] = size;
    j["align"] = textAlignName(align_);
    j["tracking"] = tracking_;
    j["lineSpacing"] = lineSpacing_;
    if (outlineWidth > 0.0f) {
        j["outlineWidth"] = outlineWidth;
        j["outlineColor"] = layerVec4Json(outlineColor);
    }
    if (glowRadius > 0.0f) {
        j["glow"] = glowRadius;
        j["glowColor"] = layerVec4Json(glowColor);
    }
    if (shadowOpacity > 0.0f) {
        j["shadowOpacity"] = shadowOpacity;
        j["shadowOffset"] = layerVec2Json(shadowOffset);
    }
}

Result<void> TextLayer::readJson(const nlohmann::json& j) {
    text_ = j.value("text", std::string("Lyric"));
    if (j.contains("font")) {
        auto font = FontDesc::fromJson(j["font"]);
        if (!font) {
            return std::unexpected(font.error());
        }
        font_ = std::move(*font);
    }
    size = j.value("size", 0.09f);
    if (j.contains("align")) {
        auto align = textAlignFromName(j["align"].get<std::string>());
        if (!align) {
            return std::unexpected(align.error());
        }
        align_ = *align;
    }
    tracking_ = j.value("tracking", 0.0f);
    lineSpacing_ = j.value("lineSpacing", 1.0f);
    outlineWidth = j.value("outlineWidth", 0.0f);
    if (j.contains("outlineColor")) {
        auto v = layerReadVec4(j["outlineColor"], "outlineColor");
        if (!v) {
            return std::unexpected(v.error());
        }
        outlineColor = *v;
    }
    glowRadius = j.value("glow", 0.0f);
    if (j.contains("glowColor")) {
        auto v = layerReadVec4(j["glowColor"], "glowColor");
        if (!v) {
            return std::unexpected(v.error());
        }
        glowColor = *v;
    }
    shadowOpacity = j.value("shadowOpacity", 0.0f);
    if (j.contains("shadowOffset")) {
        auto v = layerReadVec2(j["shadowOffset"], "shadowOffset");
        if (!v) {
            return std::unexpected(v.error());
        }
        shadowOffset = *v;
    }
    return {};
}

} // namespace avgen::comp
