// ADR-1046: text as extruded glyph geometry (spatial lyric typography).

#include "app/engine.hpp"
#include "comp/font.hpp"
#include "comp/sdf_build.hpp"
#include "core/time.hpp"
#include "scene/procedural.hpp"
#include "scene/text_mesh.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <cmath>
#include <map>
#include <tuple>

using namespace avgen;
using Catch::Matchers::WithinAbs;

namespace {

// A disc of radius r texels as a distance field: 0.5 on the edge, positive inside.
comp::SdfBitmap disc(std::uint32_t n, float r) {
    comp::SdfBitmap b;
    b.width = n;
    b.height = n;
    b.texels.resize(static_cast<std::size_t>(n) * n);
    const float c = static_cast<float>(n) * 0.5f;
    for (std::uint32_t y = 0; y < n; ++y) {
        for (std::uint32_t x = 0; x < n; ++x) {
            const float d = r - std::hypot(static_cast<float>(x) + 0.5f - c, static_cast<float>(y) + 0.5f - c);
            const float v = std::clamp(0.5f + d / (2.0f * comp::kSdfSpreadTexels), 0.0f, 1.0f);
            b.texels[y * n + x] = static_cast<std::uint8_t>(std::lround(v * 255.0f));
        }
    }
    return b;
}

// Signed volume by the divergence theorem: only a closed, consistently wound mesh gives the solid's.
double signedVolume(const scene::MeshData& m) {
    double v = 0.0;
    for (std::size_t i = 0; i + 2 < m.indices.size(); i += 3) {
        const glm::dvec3 a(m.vertices[m.indices[i]].position);
        const glm::dvec3 b(m.vertices[m.indices[i + 1]].position);
        const glm::dvec3 c(m.vertices[m.indices[i + 2]].position);
        v += glm::dot(a, glm::cross(b, c)) / 6.0;
    }
    return v;
}

} // namespace

TEST_CASE("A traced field is a closed, outward-wound solid of the field's area times its depth",
          "[text][adr1046]") {
    const float r = 20.0f;
    const auto field = disc(64, r);
    const scene::MeshData m = scene::traceFieldMesh(field, 8.0f);
    REQUIRE_FALSE(m.indices.empty());
    REQUIRE(m.indices.size() % 3 == 0);
    // The volume is the disc's area times the depth, to within the trace's resolution.
    const double expected = 3.14159265358979 * r * r * 8.0;
    CHECK_THAT(signedVolume(m), WithinAbs(expected, expected * 0.02));
    // Every triangle's winding agrees with its stored normal (outward).
    int disagree = 0;
    for (std::size_t i = 0; i + 2 < m.indices.size(); i += 3) {
        const auto& a = m.vertices[m.indices[i]];
        const auto& b = m.vertices[m.indices[i + 1]];
        const auto& c = m.vertices[m.indices[i + 2]];
        const glm::vec3 n = glm::cross(b.position - a.position, c.position - a.position);
        if (glm::length(n) > 1e-9f && glm::dot(n, a.normal) <= 0.0f) {
            ++disagree;
        }
    }
    CHECK(disagree == 0);
    // Side walls reach exactly the contour: every wall vertex lies on the disc's rim.
    double worst = 0.0;
    const double c = 32.0;
    for (const auto& v : m.vertices) {
        if (std::abs(v.normal.z) < 0.5f) {
            const double rr = std::hypot(static_cast<double>(v.position.x) - c, static_cast<double>(v.position.y) - c);
            worst = std::max(worst, std::abs(rr - r));
        }
    }
    CHECK(worst < 0.15);
    // The trace is deterministic.
    const scene::MeshData again = scene::traceFieldMesh(field, 8.0f);
    CHECK(again.vertices.size() == m.vertices.size());
    CHECK(signedVolume(again) == signedVolume(m));
}

TEST_CASE("Text becomes a centred, extruded mesh one em per textSize metres", "[text][adr1046]") {
    if (!comp::fontBackend().available()) {
        SKIP("no type engine in this build");
    }
    scene::TextMeshSpec spec;
    spec.text = "LET";
    spec.size = 0.5f;
    spec.depth = 0.1f;
    auto mesh = scene::makeTextMesh(spec);
    REQUIRE(mesh.has_value());
    REQUIRE(mesh->valid());
    REQUIRE(mesh->indices.size() > 300);
    const auto [lo, hi] = mesh->bounds();
    // Centred horizontally and vertically, the back at z = 0 and the front one depth out.
    CHECK_THAT(static_cast<double>(lo.x + hi.x), WithinAbs(0.0, 0.05));
    CHECK(std::abs(lo.y + hi.y) < 0.15f);
    CHECK_THAT(static_cast<double>(lo.z), WithinAbs(0.0, 1e-5));
    CHECK_THAT(static_cast<double>(hi.z), WithinAbs(0.05, 1e-4));
    // Capital letters are about 0.7 em tall: 0.35 m at 0.5 m per em.
    CHECK(hi.y - lo.y > 0.25f);
    CHECK(hi.y - lo.y < 0.45f);
    CHECK(signedVolume(*mesh) > 0.0);
    // Left alignment puts the text to the right of x = 0; the cache returns the same geometry.
    spec.align = scene::TextAlign3d::Left;
    auto left = scene::cachedTextMesh(spec);
    REQUIRE(left.has_value());
    CHECK((*left)->bounds().first.x > -0.02f);
    auto again = scene::cachedTextMesh(spec);
    REQUIRE(again.has_value());
    CHECK(again->get() == left->get());
}

TEST_CASE("A procedural node with a text source loads, round-trips and is addressable", "[text][adr1046]") {
    if (!comp::fontBackend().available()) {
        SKIP("no type engine in this build");
    }
    app::Engine engine(app::EngineMode::Offline);
    const auto scene = nlohmann::json::parse(R"({"format": "avgen-scene", "version": 1, "name": "words",
      "nodes": [{"kind": "procedural", "name": "w_let", "position": [0, 1.5, -3], "rotation": [0, 20, 0],
                 "procedural": {"source": {"kind": "text", "text": "LET", "textSize": 0.6, "textDepth": 0.05,
                                           "font": {"family": "Helvetica Neue", "weight": 0.6}},
                                "material": {"baseColor": [0.02, 0.02, 0.02], "emissiveColor": [1, 0.3, 0.8],
                                             "emissiveIntensity": 4}}}]})");
    REQUIRE(engine.setCompositionJson(scene).has_value());
    engine.update(FrameTime{0.0, 0.0, 0});
    for (const char* p : {"nodes/w_let/scale", "nodes/w_let/position", "nodes/w_let/visible"}) {
        INFO(p);
        CHECK(engine.params().find(p) != nullptr);
    }
    REQUIRE(engine.composition() != nullptr);
    const nlohmann::json out = engine.composition()->toJson();
    const auto& node = out["nodes"][0];
    CHECK(node["procedural"]["source"]["kind"] == "text");
    CHECK(node["procedural"]["source"]["text"] == "LET");
    CHECK(node["procedural"]["source"]["font"]["family"] == "Helvetica Neue");
    const auto& procedurals = engine.scene().procedurals;
    REQUIRE_FALSE(procedurals.empty());
    auto built = scene::makeSourceMesh(procedurals.front().source);
    INFO((built ? std::string("ok") : built.error().message) << " kind " << static_cast<int>(procedurals.front().source.kind)
         << " text '" << procedurals.front().source.text << "'");
    REQUIRE(built.has_value());
    CHECK(built->indices.size() > 300);
}

