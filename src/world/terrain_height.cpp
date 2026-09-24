#include "world/terrain_height.hpp"

#include "world/world_map.hpp"

#include <algorithm>
#include <cmath>
#include <thread>

namespace avgen::world {

float TerrainHeightField::bilinear(glm::vec2 local) const {
    if (empty()) {
        return 0.0f;
    }
    // The same steps, in the same order, as `terrainGroundAt` in shaders/height_fog.wgsl.
    const glm::vec2 last(static_cast<float>(width - 1), static_cast<float>(depth - 1));
    const glm::vec2 g = (local - origin) * (1.0f / spacing);
    const glm::vec2 c = glm::clamp(g, glm::vec2(0.0f), last);
    const glm::vec2 i0 = glm::min(glm::floor(c), last - 1.0f);
    const glm::vec2 f = c - i0;
    const auto i = static_cast<std::uint32_t>(i0.x);
    const auto j = static_cast<std::uint32_t>(i0.y);
    const float h00 = at(i, j);
    const float h10 = at(i + 1, j);
    const float h01 = at(i, j + 1);
    const float h11 = at(i + 1, j + 1);
    return glm::mix(glm::mix(h00, h10, f.x), glm::mix(h01, h11, f.x), f.y);
}

float terrainHeightSpacing(glm::vec2 size) {
    const float extent = std::max(size.x, size.y);
    const float coarsest = extent / static_cast<float>(kTerrainHeightMaxSamples - 1);
    return std::max(kTerrainHeightSpacing, coarsest);
}

TerrainHeightField bakeTerrainHeight(const WorldMap& map, std::uint64_t hash) {
    TerrainHeightField field;
    field.hash = hash;
    const glm::vec2 lo = map.min();
    const glm::vec2 hi = map.max();
    const glm::vec2 size = hi - lo;
    if (size.x <= 0.0f || size.y <= 0.0f) {
        return field;
    }
    field.spacing = terrainHeightSpacing(size);
    field.origin = lo;
    // Enough samples that the last one reaches the far edge: ceil, +1 for the fencepost. The grid
    // can overhang the far edge by less than one spacing when the map is not a whole number of
    // spacings across; `WorldMap::height` is defined there, so the overhang is simply more ground.
    field.width = static_cast<std::uint32_t>(std::ceil(size.x / field.spacing)) + 1;
    field.depth = static_cast<std::uint32_t>(std::ceil(size.y / field.spacing)) + 1;
    field.width = std::min(field.width, kTerrainHeightMaxSamples);
    field.depth = std::min(field.depth, kTerrainHeightMaxSamples);
    field.heights.resize(static_cast<std::size_t>(field.width) * field.depth);

    const auto bakeRows = [&field, &map](std::uint32_t first, std::uint32_t last) {
        for (std::uint32_t j = first; j < last; ++j) {
            for (std::uint32_t i = 0; i < field.width; ++i) {
                const glm::vec2 p = field.origin +
                                    glm::vec2(static_cast<float>(i), static_cast<float>(j)) * field.spacing;
                field.heights[static_cast<std::size_t>(j) * field.width + i] = map.height(p);
            }
        }
    };
    const unsigned hardware = std::max(1u, std::thread::hardware_concurrency());
    const std::uint32_t workers = std::min<std::uint32_t>(hardware, std::max<std::uint32_t>(field.depth / 16, 1));
    if (workers <= 1) {
        bakeRows(0, field.depth);
    } else {
        std::vector<std::thread> pool;
        pool.reserve(workers - 1);
        const std::uint32_t span = (field.depth + workers - 1) / workers;
        for (std::uint32_t w = 1; w < workers; ++w) {
            const std::uint32_t first = std::min(w * span, field.depth);
            const std::uint32_t last = std::min(first + span, field.depth);
            if (first < last) {
                pool.emplace_back(bakeRows, first, last);
            }
        }
        bakeRows(0, std::min(span, field.depth));
        for (std::thread& t : pool) {
            t.join();
        }
    }
    return field;
}

glm::vec2 TerrainGround::worldMin() const {
    if (!valid()) {
        return glm::vec2(0.0f);
    }
    const glm::vec2 s(scale.x, scale.z);
    const glm::vec2 a = glm::vec2(translation.x, translation.z) + field->origin * s;
    const glm::vec2 b = glm::vec2(translation.x, translation.z) + field->extentMax() * s;
    return glm::min(a, b);
}

glm::vec2 TerrainGround::worldMax() const {
    if (!valid()) {
        return glm::vec2(0.0f);
    }
    const glm::vec2 s(scale.x, scale.z);
    const glm::vec2 a = glm::vec2(translation.x, translation.z) + field->origin * s;
    const glm::vec2 b = glm::vec2(translation.x, translation.z) + field->extentMax() * s;
    return glm::max(a, b);
}

glm::vec4 TerrainGround::map0() const {
    if (!valid()) {
        return glm::vec4(0.0f);
    }
    const glm::vec2 o = glm::vec2(translation.x, translation.z) + field->origin * glm::vec2(scale.x, scale.z);
    return glm::vec4(o, 1.0f / (field->spacing * scale.x), 1.0f / (field->spacing * scale.z));
}

glm::vec4 TerrainGround::map1() const {
    if (!valid()) {
        return glm::vec4(0.0f);
    }
    return glm::vec4(scale.y, translation.y, std::max(fade, 1e-3f), 1.0f);
}

float TerrainGround::groundAt(glm::vec2 xz) const {
    if (!valid()) {
        return 0.0f;
    }
    // The steps of `terrainGroundAt` (shaders/height_fog.wgsl), in its order.
    const glm::vec4 m0 = map0();
    const glm::vec4 m1 = map1();
    const TerrainHeightField& f = *field;
    const glm::vec2 last(static_cast<float>(f.width - 1), static_cast<float>(f.depth - 1));
    const glm::vec2 g = (xz - glm::vec2(m0.x, m0.y)) * glm::vec2(m0.z, m0.w);
    const glm::vec2 c = glm::clamp(g, glm::vec2(0.0f), last);
    const glm::vec2 i0 = glm::min(glm::floor(c), last - 1.0f);
    const glm::vec2 fr = c - i0;
    const auto i = static_cast<std::uint32_t>(i0.x);
    const auto j = static_cast<std::uint32_t>(i0.y);
    const float h = glm::mix(glm::mix(f.at(i, j), f.at(i + 1, j), fr.x),
                             glm::mix(f.at(i, j + 1), f.at(i + 1, j + 1), fr.x), fr.y);
    const float outside = glm::length((g - c) / glm::vec2(m0.z, m0.w));
    const float keep = 1.0f - glm::clamp(outside / m1.z, 0.0f, 1.0f);
    return (h * m1.x + m1.y) * keep;
}

TerrainGround placeTerrainGround(std::shared_ptr<const TerrainHeightField> field, const glm::vec3& translation,
                                 const glm::vec3& scale, bool rotated) {
    TerrainGround ground;
    if (rotated || field == nullptr || field->empty() || scale.x <= 0.0f || scale.z <= 0.0f) {
        return ground;
    }
    ground.field = std::move(field);
    ground.translation = translation;
    ground.scale = scale;
    // 5% of the footprint, and never less than four samples: wide enough that the layer eases back
    // to the plane rather than stepping, narrow enough to be off-screen in any shot framed on the
    // terrain. 32 m on Glowmere's 640 m map.
    const glm::vec2 span = (ground.field->extentMax() - ground.field->origin) * glm::vec2(scale.x, scale.z);
    ground.fade = std::max(0.05f * std::max(span.x, span.y), 4.0f * ground.field->spacing * std::max(scale.x, scale.z));
    return ground;
}

} // namespace avgen::world
