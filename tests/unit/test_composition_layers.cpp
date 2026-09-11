// The 2D composition layer system (ADR-083). Behaviour, not setters: what the model *produces* --
// geometry, per-frame items, JSON, parameter paths -- rather than whether a field round-trips
// through its own accessor.

#include "comp/layer_stack.hpp"
#include "comp/sdf_build.hpp"
#include "params/parameter_set.hpp"
#include "params/timeline.hpp"

#include <nlohmann/json.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace avgen;
using Catch::Approx;

namespace {

// The pixel position of a layer's anchor point this frame: the transform applied to the anchor.
glm::vec2 anchorPixel(const comp::LayerItem& item, glm::vec2 anchorLocal) {
    return {item.xform0.x * anchorLocal.x + item.xform0.y * anchorLocal.y + item.xform1.x,
            item.xform0.z * anchorLocal.x + item.xform0.w * anchorLocal.y + item.xform1.y};
}

glm::vec2 layerAnchorPixel(comp::LayerStack& stack, comp::Layer& layer, const comp::Frame& frame, double t) {
    const comp::CompositionFrame& built = stack.build(frame, t);
    REQUIRE_FALSE(built.items.empty());
    // Text puts the shadow in item 0 and the glyphs in item 1; a shape has only item 0.
    const std::size_t index = layer.kind() == comp::LayerKind::Text ? 1 : 0;
    return anchorPixel(built.items[index], layer.resolvedAnchor() * layer.boxLocal());
}

} // namespace

TEST_CASE("a signed distance field is positive inside and negative outside", "[composition][sdf]") {
    // A 32x32 solid square inside a 64x64 field, rasterised at 4x.
    constexpr std::uint32_t kSize = 64;
    std::vector<std::uint8_t> coverage(kSize * kSize, 0);
    for (std::uint32_t y = 16; y < 48; ++y) {
        for (std::uint32_t x = 16; x < 48; ++x) {
            coverage[y * kSize + x] = 255;
        }
    }
    const comp::SdfBitmap sdf = comp::buildSdf(coverage.data(), kSize, kSize, 4, comp::kSdfSpreadTexels);
    REQUIRE(sdf.width == 16);
    REQUIRE(sdf.height == 16);
    // Centre of the square: well inside.
    CHECK(sdf.sample(8, 8) > 0.6f);
    // A corner of the field: well outside.
    CHECK(sdf.sample(0, 0) < 0.4f);
    // The edge runs between texels 3 and 4 (16/4), so the field crosses 0.5 there.
    CHECK(sdf.sample(3, 8) < sdf.sample(5, 8));
    CHECK(sdf.sample(4, 8) == Approx(0.5f).margin(0.12f));
}

TEST_CASE("an empty coverage bitmap yields a field that is everywhere outside", "[composition][sdf]") {
    std::vector<std::uint8_t> coverage(32 * 32, 0);
    const comp::SdfBitmap sdf = comp::buildSdf(coverage.data(), 32, 32, 4);
    REQUIRE_FALSE(sdf.empty());
    for (std::uint8_t texel : sdf.texels) {
        CHECK(texel == 0);
    }
}

TEST_CASE("layers keep a stable order and unique ids", "[composition][layers]") {
    comp::LayerStack stack;
    auto& a = stack.addText("one");
    auto& b = stack.addText("two");
    auto& c = stack.addShape(comp::ShapeKind::Rectangle);
    CHECK(stack.size() == 3);
    CHECK(a.id != b.id);
    CHECK(b.id != c.id);
    CHECK(stack.indexOf(a.id) == 0);
    CHECK(stack.indexOf(c.id) == 2);

    // Compositing order is list order, and moving one moves only one.
    const std::uint32_t cId = c.id;
    REQUIRE(stack.moveTo(cId, 0));
    CHECK(stack.indexOf(cId) == 0);
    CHECK(stack.at(1)->name == "Text 1");

    // A duplicate lands directly above its original, with its own id and its own name.
    comp::Layer* copy = stack.duplicate(cId);
    REQUIRE(copy != nullptr);
    CHECK(copy->id != cId);
    CHECK(stack.indexOf(copy->id) == 1);
    CHECK(copy->name != stack.find(cId)->name);

    REQUIRE(stack.remove(cId));
    CHECK(stack.find(cId) == nullptr);
    CHECK(stack.size() == 3);
}

TEST_CASE("a layer outside its time range draws nothing", "[composition][layers]") {
    comp::LayerStack stack;
    auto& text = stack.addText("lyric");
    text.startTime = 2.0;
    text.endTime = 4.0;
    const comp::Frame frame{1920, 1080};

    CHECK(stack.build(frame, 0.0).drawnLayers == 0);
    CHECK(stack.build(frame, 1.999).drawnLayers == 0);
    CHECK(stack.build(frame, 2.0).drawnLayers == 1);
    CHECK(stack.build(frame, 3.9).drawnLayers == 1);
    CHECK(stack.build(frame, 4.0).drawnLayers == 0);

    // An end at or before the start means "until the end of time".
    text.endTime = 0.0;
    CHECK(stack.build(frame, 1000.0).drawnLayers == 1);

    // The eye toggle wins over everything.
    text.enabled = false;
    CHECK(stack.build(frame, 3.0).drawnLayers == 0);
}

TEST_CASE("a layer keyframed to zero opacity costs no draw", "[composition][layers]") {
    comp::LayerStack stack;
    auto& text = stack.addText("lyric");
    const comp::Frame frame{1920, 1080};
    REQUIRE(stack.build(frame, 0.0).drawnLayers == 1);
    text.opacity = 0.0f;
    const comp::CompositionFrame& built = stack.build(frame, 0.0);
    CHECK(built.drawnLayers == 0);
    CHECK(built.draws.empty());
}

TEST_CASE("layer position is resolution independent", "[composition][coordinates]") {
    comp::LayerStack stack;
    auto& text = stack.addText("centred");
    text.position = glm::vec2(0.5f, 0.5f);
    text.anchor = glm::vec2(0.5f, 0.5f);

    for (const comp::Frame frame : {comp::Frame{1280, 720}, comp::Frame{1920, 1080}, comp::Frame{2560, 1440},
                                    comp::Frame{3840, 2160}, comp::Frame{1000, 1000}}) {
        const glm::vec2 p = layerAnchorPixel(stack, text, frame, 0.0);
        CHECK(p.x == Approx(static_cast<float>(frame.width) * 0.5f).margin(0.01f));
        CHECK(p.y == Approx(static_cast<float>(frame.height) * 0.5f).margin(0.01f));
    }

    // "10% from the left, 90% from the bottom" stays exactly there, at any aspect ratio.
    text.position = glm::vec2(0.1f, 0.9f);
    for (const comp::Frame frame : {comp::Frame{1280, 720}, comp::Frame{3840, 2160}, comp::Frame{1080, 1920}}) {
        const glm::vec2 p = layerAnchorPixel(stack, text, frame, 0.0);
        CHECK(p.x == Approx(static_cast<float>(frame.width) * 0.1f).margin(0.01f));
        CHECK(p.y == Approx(static_cast<float>(frame.height) * 0.1f).margin(0.01f)); // 90% up = 10% down
    }
}

TEST_CASE("layer size scales with the frame height, so shapes keep their proportions",
          "[composition][coordinates]") {
    comp::LayerStack stack;
    auto& square = stack.addShape(comp::ShapeKind::Rectangle);
    square.size = glm::vec2(0.25f, 0.25f);

    // The same square drawn at two aspect ratios covers the same number of pixels each way.
    for (const comp::Frame frame : {comp::Frame{1920, 1080}, comp::Frame{1080, 1080}, comp::Frame{3840, 1080}}) {
        const comp::CompositionFrame& built = stack.build(frame, 0.0);
        const comp::LayerItem& item = built.items[0];
        // The 2x2 with no rotation is (a, 0, 0, -b): a and b are pixels per height unit.
        CHECK(std::abs(item.xform0.x) == Approx(1080.0f).margin(0.01f));
        CHECK(std::abs(item.xform0.w) == Approx(1080.0f).margin(0.01f));
        CHECK(item.xform1.z == Approx(0.125f));
        CHECK(item.xform1.w == Approx(0.125f));
    }
}

TEST_CASE("rotation turns counter-clockwise about the anchor", "[composition][coordinates]") {
    comp::LayerStack stack;
    auto& shape = stack.addShape(comp::ShapeKind::Rectangle);
    shape.size = glm::vec2(0.2f, 0.2f);
    shape.anchor = glm::vec2(0.5f, 0.5f);
    shape.position = glm::vec2(0.5f, 0.5f);
    shape.rotation = 90.0f;
    const comp::Frame frame{1000, 1000};
    const comp::CompositionFrame& built = stack.build(frame, 0.0);
    const comp::LayerItem& item = built.items[0];
    // A point one local unit to the right of the anchor should end up one unit *above* it, and
    // "above" in target pixels is a smaller y.
    const glm::vec2 centre = anchorPixel(item, glm::vec2(0.1f, 0.1f));
    const glm::vec2 right = anchorPixel(item, glm::vec2(0.2f, 0.1f));
    CHECK(right.x == Approx(centre.x).margin(0.01f));
    CHECK(right.y < centre.y - 10.0f);
}

TEST_CASE("every animatable layer property is registered before the timeline binds",
          "[composition][params]") {
    comp::LayerStack stack;
    auto& text = stack.addText("lyric");
    auto& shape = stack.addShape(comp::ShapeKind::Ellipse);
    params::ParameterSet set;
    stack.attach(set);

    for (const std::string& path : stack.parameterPaths()) {
        INFO(path);
        CHECK(set.find(path) != nullptr);
    }
    CHECK(set.find(text.parameterPath("opacity")) != nullptr);
    CHECK(set.find(text.parameterPath("size")) != nullptr);
    CHECK(set.find(shape.parameterPath("strokeWidth")) != nullptr);

    // The whole point: a track naming any of them binds, rather than silently doing nothing.
    params::Timeline timeline;
    for (const std::string& path : stack.parameterPaths()) {
        params::Track track;
        track.target = path;
        track.keys.push_back(params::Key{0.0, {0.5f, 0.5f, 0.5f, 0.5f}, params::KeyInterp::Linear, {}, {}});
        timeline.addTrack(track);
    }
    const auto bound = timeline.bind(set);
    CHECK(bound.has_value());
}

TEST_CASE("a keyframed opacity drives what the layer draws", "[composition][params][timeline]") {
    comp::LayerStack stack;
    auto& text = stack.addText("fade");
    params::ParameterSet set;
    stack.attach(set);

    params::Timeline timeline;
    params::Track track;
    track.target = text.parameterPath("opacity");
    track.addKey(params::Key{10.0, {0.0f, 0.0f, 0.0f, 0.0f}, params::KeyInterp::Linear, {}, {}});
    track.addKey(params::Key{11.0, {1.0f, 0.0f, 0.0f, 0.0f}, params::KeyInterp::Linear, {}, {}});
    timeline.addTrack(track);
    REQUIRE(timeline.bind(set).has_value());

    const comp::Frame frame{1920, 1080};
    const auto opacityAt = [&](double seconds) {
        set.resetFinals();
        timeline.apply(params::TimelineClock{seconds, 0.0});
        const comp::CompositionFrame& built = stack.build(frame, seconds);
        return built.items[1].color.a;
    };
    CHECK(opacityAt(9.0) == Approx(0.0f).margin(1e-4f));
    CHECK(opacityAt(10.5) == Approx(0.5f).margin(1e-3f));
    CHECK(opacityAt(12.0) == Approx(1.0f).margin(1e-4f));
    // The authored value is untouched: the timeline writes finals, never bases (ADR-018).
    CHECK(text.opacity == Approx(1.0f));
}

TEST_CASE("interpolation modes reach the same endpoints by different routes",
          "[composition][params][timeline]") {
    params::Track track;
    const auto sample = [&](params::KeyInterp interp, double t) {
        track.keys.clear();
        track.addKey(params::Key{0.0, {0.0f, 0, 0, 0}, interp, {}, {}});
        track.addKey(params::Key{1.0, {1.0f, 0, 0, 0}, interp, {}, {}});
        return track.evaluate(t)[0];
    };
    for (auto interp : {params::KeyInterp::Linear, params::KeyInterp::EaseIn, params::KeyInterp::EaseOut,
                        params::KeyInterp::EaseInOut}) {
        CHECK(sample(interp, 0.0) == Approx(0.0f).margin(1e-5f));
        CHECK(sample(interp, 1.0) == Approx(1.0f).margin(1e-5f));
    }
    // Hold holds.
    CHECK(sample(params::KeyInterp::Step, 0.99) == Approx(0.0f));
    CHECK(sample(params::KeyInterp::Step, 1.0) == Approx(1.0f));
    // Ease in starts slower than linear, ease out starts faster.
    CHECK(sample(params::KeyInterp::EaseIn, 0.25) < sample(params::KeyInterp::Linear, 0.25));
    CHECK(sample(params::KeyInterp::EaseOut, 0.25) > sample(params::KeyInterp::Linear, 0.25));
    // Before the first key and after the last one, the value holds.
    CHECK(sample(params::KeyInterp::Linear, -5.0) == Approx(0.0f));
    CHECK(sample(params::KeyInterp::Linear, 5.0) == Approx(1.0f));
}

TEST_CASE("a composition survives a save and load unchanged", "[composition][serialization]") {
    comp::LayerStack stack;
    stack.reference = comp::Frame{2560, 1440};
    auto& text = stack.addText("Hold your breath", 4.0, 9.5);
    text.name = "line one";
    text.position = glm::vec2(0.5f, 0.18f);
    text.scale = glm::vec2(1.2f, 1.2f);
    text.rotation = -3.0f;
    text.anchor = glm::vec2(0.5f, 0.0f);
    text.opacity = 0.85f;
    text.color = glm::vec4(0.9f, 0.95f, 1.0f, 1.0f);
    text.size = 0.11f;
    text.setAlign(comp::TextAlign::Left);
    text.setTracking(0.03f);
    text.setLineSpacing(1.3f);
    text.outlineWidth = 0.02f;
    text.glowRadius = 0.05f;
    text.shadowOpacity = 0.4f;
    text.blend = comp::BlendMode::Screen;
    comp::FontDesc font;
    font.family = "Futura";
    font.postScriptName = "Futura-Medium";
    font.weight = 0.3f;
    font.italic = true;
    text.setFont(font);

    auto& border = stack.addShape(comp::ShapeKind::Rectangle);
    border.size = glm::vec2(1.6f, 0.9f);
    border.strokeWidth = 0.004f;
    border.color = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
    border.cornerRadius = 0.01f;
    border.blend = comp::BlendMode::Additive;

    const nlohmann::json doc = stack.toJson();
    comp::LayerStack loaded;
    REQUIRE(loaded.fromJson(doc).has_value());
    CHECK(loaded.toJson() == doc);

    REQUIRE(loaded.size() == 2);
    const auto* copy = dynamic_cast<const comp::TextLayer*>(loaded.layers()[0].get());
    REQUIRE(copy != nullptr);
    CHECK(copy->name == "line one");
    CHECK(copy->text() == "Hold your breath");
    CHECK(copy->startTime == Approx(4.0));
    CHECK(copy->endTime == Approx(9.5));
    CHECK(copy->font().family == "Futura");
    CHECK(copy->font().postScriptName == "Futura-Medium");
    CHECK(copy->font().italic);
    CHECK(copy->align() == comp::TextAlign::Left);
    CHECK(copy->tracking() == Approx(0.03f));
    CHECK(copy->blend == comp::BlendMode::Screen);
    CHECK(loaded.reference.width == 2560);

    const auto* shape = dynamic_cast<const comp::ShapeLayer*>(loaded.layers()[1].get());
    REQUIRE(shape != nullptr);
    CHECK(shape->strokeWidth == Approx(0.004f));
    CHECK(shape->cornerRadius == Approx(0.01f));
    CHECK(shape->blend == comp::BlendMode::Additive);
}

TEST_CASE("loading rejects a newer composition and keeps what was there", "[composition][serialization]") {
    comp::LayerStack stack;
    stack.addText("kept");
    nlohmann::json doc = stack.toJson();
    doc["version"] = comp::LayerStack::kFormatVersion + 1;
    CHECK_FALSE(stack.fromJson(doc).has_value());
    CHECK(stack.size() == 1);

    nlohmann::json broken = stack.toJson();
    broken["layers"][0]["kind"] = "hologram";
    CHECK_FALSE(stack.fromJson(broken).has_value());
    CHECK(stack.size() == 1);
}

TEST_CASE("an empty composition object loads as an empty composition", "[composition][serialization]") {
    comp::LayerStack stack;
    stack.addText("gone");
    REQUIRE(stack.fromJson(nlohmann::json::object()).has_value());
    CHECK(stack.empty());
    CHECK(stack.build(comp::Frame{1920, 1080}, 0.0).draws.empty());
}

TEST_CASE("compositing order follows list order and merges into one draw",
          "[composition][layers]") {
    comp::LayerStack stack;
    auto& back = stack.addShape(comp::ShapeKind::Rectangle);
    auto& mid = stack.addText("over");
    auto& front = stack.addShape(comp::ShapeKind::Ellipse);
    back.name = "back";
    mid.shadowOpacity = 0.0f;
    front.name = "front";

    const comp::Frame frame{1920, 1080};
    const comp::CompositionFrame& built = stack.build(frame, 0.0);
    CHECK(built.drawnLayers == 3);
    // Every layer is Normal and their runs are contiguous, so the whole stack is one draw call.
    REQUIRE(built.draws.size() == 1);
    CHECK(built.draws[0].firstVertex == 0);

    // A different blend mode in the middle splits the batch, and only there.
    mid.blend = comp::BlendMode::Additive;
    const comp::CompositionFrame& split = stack.build(frame, 0.0);
    CHECK(split.draws.size() == 3);
    CHECK(split.draws[1].blend == comp::BlendMode::Additive);
}

TEST_CASE("geometry is rebuilt for content changes and not for animation", "[composition][layers]") {
    comp::LayerStack stack;
    auto& text = stack.addText("before");
    const comp::Frame frame{1920, 1080};
    stack.build(frame, 0.0);
    const std::uint64_t version = stack.vertexVersion();

    // Moving, fading, scaling and recolouring the layer touch no vertex.
    text.position = glm::vec2(0.2f, 0.3f);
    text.opacity = 0.4f;
    text.scale = glm::vec2(3.0f, 3.0f);
    text.size = 0.4f;
    text.color = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
    stack.build(frame, 1.0);
    CHECK(stack.vertexVersion() == version);

    // Changing the words does.
    text.setText("after");
    stack.build(frame, 1.0);
    CHECK(stack.vertexVersion() > version);
}

TEST_CASE("the platform type engine shapes and rasterises", "[composition][font]") {
    comp::FontBackend& backend = comp::fontBackend();
    if (!backend.available()) {
        SUCCEED("this build has no font backend");
        return;
    }
    CHECK_FALSE(backend.families().empty());

    comp::FontDesc desc;
    desc.family = "Helvetica";
    const comp::FontResolution resolution = backend.resolve(desc);
    CHECK(resolution.available);
    CHECK_FALSE(resolution.postScriptName.empty());

    auto shaped = backend.shape(desc, "AV");
    REQUIRE(shaped.has_value());
    CHECK(shaped->glyphs.size() == 2);
    CHECK(shaped->lineWidths.size() == 1);
    CHECK(shaped->lineWidths[0] > 0.5f); // two capitals are wider than half an em
    CHECK(shaped->ascent > 0.0f);
    CHECK(shaped->lineHeight > shaped->ascent);
    // Glyphs advance left to right.
    CHECK(shaped->glyphs[1].x > shaped->glyphs[0].x);

    auto multiline = backend.shape(desc, "one\ntwo\nthree");
    REQUIRE(multiline.has_value());
    CHECK(multiline->lineCount() == 3);
    CHECK(multiline->glyphs.back().line == 2);
    CHECK(multiline->glyphs.back().y < 0.0f); // later lines sit below the first

    auto glyph = backend.glyph(desc, shaped->glyphs[0].glyphId);
    REQUIRE(glyph.has_value());
    CHECK_FALSE(glyph->sdf.empty());
    CHECK(glyph->sizeX > 0.0f);
    CHECK(glyph->sizeY > 0.0f);
    // The field has texels on both sides of the edge, which is what makes it a field and not a mask.
    bool inside = false;
    bool outside = false;
    for (std::uint8_t t : glyph->sdf.texels) {
        inside = inside || t > 160;
        outside = outside || t < 96;
    }
    CHECK(inside);
    CHECK(outside);
}

TEST_CASE("a font that is not installed is reported, not silently swapped", "[composition][font]") {
    comp::FontBackend& backend = comp::fontBackend();
    if (!backend.available()) {
        SUCCEED("this build has no font backend");
        return;
    }
    comp::FontDesc desc;
    desc.family = "A Font Nobody Has Installed 9f3c";
    desc.fallback = "Helvetica";
    const comp::FontResolution resolution = backend.resolve(desc);
    CHECK(resolution.available);   // something usable was found
    CHECK(resolution.substituted); // and the caller is told it is not what was asked for
}
