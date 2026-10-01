#include "scene/text_mesh.hpp"

#include "core/log.hpp"

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <mutex>
#include <string>

namespace avgen::scene {

namespace {

constexpr float kInsideLevel = 0.5f;

struct Field {
    const comp::SdfBitmap& bitmap;
    // Corner (i, j): column i, row j counted from the BOTTOM (the bitmap's row 0 is the top).
    [[nodiscard]] float at(int i, int j) const {
        const auto x = static_cast<std::uint32_t>(i);
        const auto y = bitmap.height - 1u - static_cast<std::uint32_t>(j);
        return bitmap.sample(x, y);
    }
    [[nodiscard]] bool inside(int i, int j) const { return at(i, j) > kInsideLevel; }
};

// Where the level crosses the edge from corner a to corner b, computed from the lower-index corner so
// the two cells that share an edge get bit-identical points.
glm::vec2 crossing(const Field& f, glm::ivec2 a, glm::ivec2 b) {
    if (b.x < a.x || b.y < a.y) {
        std::swap(a, b);
    }
    const float fa = f.at(a.x, a.y);
    const float fb = f.at(b.x, b.y);
    const float denom = fb - fa;
    const float t = std::abs(denom) > 1e-12f ? std::clamp((kInsideLevel - fa) / denom, 0.0f, 1.0f) : 0.5f;
    const glm::vec2 pa(static_cast<float>(a.x) + 0.5f, static_cast<float>(a.y) + 0.5f);
    const glm::vec2 pb(static_cast<float>(b.x) + 0.5f, static_cast<float>(b.y) + 0.5f);
    return pa + (pb - pa) * t;
}

void addFace(MeshData& mesh, const std::vector<glm::vec2>& poly, float z, bool front) {
    if (poly.size() < 3) {
        return;
    }
    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
    const glm::vec3 n(0.0f, 0.0f, front ? 1.0f : -1.0f);
    for (const glm::vec2& p : poly) {
        mesh.vertices.push_back(Vertex{{p.x, p.y, z}, n, {p.x, p.y}});
    }
    for (std::uint32_t k = 1; k + 1 < poly.size(); ++k) {
        if (front) {
            mesh.indices.insert(mesh.indices.end(), {base, base + k, base + k + 1});
        } else {
            mesh.indices.insert(mesh.indices.end(), {base, base + k + 1, base + k});
        }
    }
}

// A wall under the contour segment a -> b (the inside on its left, so the outside on its right).
void addWall(MeshData& mesh, glm::vec2 a, glm::vec2 b, float depth) {
    const glm::vec2 e = b - a;
    const float len = glm::length(e);
    if (!(len > 1e-9f) || !(depth > 0.0f)) {
        return;
    }
    const glm::vec3 n(e.y / len, -e.x / len, 0.0f);
    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back(Vertex{{a.x, a.y, 0.0f}, n, {0.0f, 0.0f}});
    mesh.vertices.push_back(Vertex{{b.x, b.y, 0.0f}, n, {1.0f, 0.0f}});
    mesh.vertices.push_back(Vertex{{b.x, b.y, depth}, n, {1.0f, 1.0f}});
    mesh.vertices.push_back(Vertex{{a.x, a.y, depth}, n, {0.0f, 1.0f}});
    mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
}

} // namespace

MeshData traceFieldMesh(const comp::SdfBitmap& bitmap, float depthTexels) {
    MeshData mesh;
    mesh.name = "text";
    if (bitmap.width < 2 || bitmap.height < 2) {
        return mesh;
    }
    const Field f{bitmap};
    const int cols = static_cast<int>(bitmap.width) - 1;
    const int rows = static_cast<int>(bitmap.height) - 1;
    std::vector<glm::vec2> poly;
    poly.reserve(8);
    for (int j = 0; j < rows; ++j) {
        int runStart = -1;
        const auto flushRun = [&](int endExclusive) {
            if (runStart < 0) {
                return;
            }
            const float x0 = static_cast<float>(runStart) + 0.5f;
            const float x1 = static_cast<float>(endExclusive) + 0.5f;
            const float y0 = static_cast<float>(j) + 0.5f;
            const float y1 = y0 + 1.0f;
            poly = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
            addFace(mesh, poly, depthTexels, true);
            addFace(mesh, poly, 0.0f, false);
            runStart = -1;
        };
        for (int i = 0; i < cols; ++i) {
            // Corners counter-clockwise: bottom-left, bottom-right, top-right, top-left.
            const std::array<glm::ivec2, 4> c{{{i, j}, {i + 1, j}, {i + 1, j + 1}, {i, j + 1}}};
            std::array<bool, 4> in{};
            int count = 0;
            for (int k = 0; k < 4; ++k) {
                in[static_cast<std::size_t>(k)] = f.inside(c[static_cast<std::size_t>(k)].x, c[static_cast<std::size_t>(k)].y);
                count += in[static_cast<std::size_t>(k)] ? 1 : 0;
            }
            if (count == 4) {
                if (runStart < 0) {
                    runStart = i;
                }
                continue;
            }
            flushRun(i);
            if (count == 0) {
                continue;
            }
            // Clip the cell to the inside (Sutherland-Hodgman against the field). A saddle comes out as
            // one hexagon (the two inside corners joined), and its walls follow the same choice.
            poly.clear();
            std::array<bool, 8> exitAt{}; // poly[k] is a crossing where the walk leaves the inside
            for (int k = 0; k < 4; ++k) {
                const auto ku = static_cast<std::size_t>(k);
                const auto nu = static_cast<std::size_t>((k + 1) % 4);
                if (in[ku]) {
                    exitAt[poly.size()] = false;
                    poly.push_back(glm::vec2(c[ku]) + 0.5f);
                }
                if (in[ku] != in[nu]) {
                    exitAt[poly.size()] = in[ku];
                    poly.push_back(crossing(f, c[ku], c[nu]));
                }
            }
            addFace(mesh, poly, depthTexels, true);
            addFace(mesh, poly, 0.0f, false);
            // A wall wherever the polygon runs from an exit crossing straight to the next crossing (an
            // entry): that edge is the contour, not a cell edge.
            for (std::size_t k = 0; k < poly.size(); ++k) {
                const std::size_t n = (k + 1) % poly.size();
                if (exitAt[k]) {
                    addWall(mesh, poly[k], poly[n], depthTexels);
                }
            }
        }
        flushRun(cols);
    }
    return mesh;
}

Result<MeshData> makeTextMesh(const TextMeshSpec& spec) {
    if (!(spec.size > 0.0f)) {
        return fail("text size must be positive (got {})", spec.size);
    }
    if (!(spec.depth >= 0.0f)) {
        return fail("text depth must be >= 0 (got {})", spec.depth);
    }
    MeshData mesh;
    mesh.name = "text";
    if (spec.text.empty()) {
        return mesh;
    }
    comp::FontBackend& fonts = comp::fontBackend();
    if (!fonts.available()) {
        return fail("text '{}': this build has no type engine", spec.text);
    }
    auto shaped = fonts.shape(spec.font, spec.text);
    if (!shaped) {
        return std::unexpected(shaped.error());
    }
    const float texel = 1.0f / static_cast<float>(comp::kSdfEmTexels); // em per field texel
    const float depthTexels = spec.depth / texel;
    // Line widths including tracking, for alignment.
    std::vector<float> widths = shaped->lineWidths;
    std::vector<int> glyphsPerLine(widths.size(), 0);
    for (const comp::ShapedGlyph& g : shaped->glyphs) {
        if (g.line < glyphsPerLine.size()) {
            ++glyphsPerLine[g.line];
        }
    }
    for (std::size_t l = 0; l < widths.size(); ++l) {
        widths[l] += spec.tracking * static_cast<float>(std::max(glyphsPerLine[l] - 1, 0));
    }
    const std::uint32_t lines = shaped->lineCount();
    float yShift = 0.0f;
    if (spec.valign == TextVAlign3d::Middle && lines > 0) {
        const float top = shaped->ascent;
        const float bottom = -static_cast<float>(lines - 1) * shaped->lineHeight - shaped->descent;
        yShift = -(top + bottom) * 0.5f;
    }
    std::vector<int> indexInLine(widths.size(), 0);
    for (const comp::ShapedGlyph& g : shaped->glyphs) {
        auto image = fonts.glyph(spec.font, g.glyphId);
        if (!image) {
            return std::unexpected(image.error());
        }
        const int k = g.line < indexInLine.size() ? indexInLine[g.line]++ : 0;
        if (image->sdf.empty()) {
            continue; // a space
        }
        const float lineWidth = g.line < widths.size() ? widths[g.line] : 0.0f;
        float lineX = 0.0f;
        switch (spec.align) {
        case TextAlign3d::Left: lineX = 0.0f; break;
        case TextAlign3d::Center: lineX = -lineWidth * 0.5f; break;
        case TextAlign3d::Right: lineX = -lineWidth; break;
        }
        // Texel (0.5, 0.5) of the trace is the centre of the field's bottom-left texel.
        const float ox = lineX + g.x + spec.tracking * static_cast<float>(k) + image->originX;
        const float oy = g.y + image->originY + yShift;
        const MeshData glyph = traceFieldMesh(image->sdf, depthTexels);
        const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
        for (const Vertex& v : glyph.vertices) {
            Vertex w = v;
            w.position = glm::vec3(ox + v.position.x * texel, oy + v.position.y * texel, v.position.z * texel) * spec.size;
            w.uv = glm::vec2(w.position.x, w.position.y);
            mesh.vertices.push_back(w);
        }
        for (const std::uint32_t i : glyph.indices) {
            mesh.indices.push_back(base + i);
        }
    }
    return mesh;
}

Result<std::shared_ptr<const MeshData>> cachedTextMesh(const TextMeshSpec& spec) {
    static std::mutex mutex;
    static std::map<std::string, std::shared_ptr<const MeshData>, std::less<>> cache;
    std::string key = spec.text;
    key += '\x1f';
    key += spec.font.key();
    for (const float v : {spec.size, spec.depth, spec.tracking}) {
        key += '\x1f';
        key += std::to_string(v);
    }
    key += '\x1f';
    key += std::to_string(static_cast<int>(spec.align)) + std::to_string(static_cast<int>(spec.valign));
    {
        const std::lock_guard lock(mutex);
        if (const auto it = cache.find(key); it != cache.end()) {
            return it->second;
        }
    }
    auto built = makeTextMesh(spec);
    if (!built) {
        return std::unexpected(built.error());
    }
    auto shared = std::make_shared<const MeshData>(std::move(*built));
    const std::lock_guard lock(mutex);
    cache.emplace(key, shared);
    return shared;
}

} // namespace avgen::scene
