#include "rendering/output_mapping.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

namespace avgen::rendering {

namespace {
constexpr float kEps = 1e-6f;

bool near(float a, float b) { return std::abs(a - b) <= kEps; }
} // namespace

bool operator==(const OutputCrop& a, const OutputCrop& b) {
    return near(a.x, b.x) && near(a.y, b.y) && near(a.w, b.w) && near(a.h, b.h);
}

bool operator==(const OutputBlend& a, const OutputBlend& b) {
    return near(a.left, b.left) && near(a.right, b.right) && near(a.top, b.top) && near(a.bottom, b.bottom);
}

bool operator==(const OutputMapping& a, const OutputMapping& b) {
    for (std::size_t i = 0; i < 4; ++i) {
        if (!near(a.corners[i].x, b.corners[i].x) || !near(a.corners[i].y, b.corners[i].y)) {
            return false;
        }
    }
    return a.crop == b.crop && a.blend == b.blend && near(a.blendGamma, b.blendGamma) &&
           near(a.brightness, b.brightness) && near(a.gamma, b.gamma) && a.flipX == b.flipX && a.flipY == b.flipY;
}

bool OutputMapping::isIdentity() const {
    const OutputMapping id;
    return crop == id.crop && blend == id.blend && near(brightness, 1.0f) && near(gamma, 1.0f) && !flipX &&
           !flipY && [&] {
               for (std::size_t i = 0; i < 4; ++i) {
                   if (!near(corners[i].x, id.corners[i].x) || !near(corners[i].y, id.corners[i].y)) {
                       return false;
                   }
               }
               return true;
           }();
}

Result<void> OutputMapping::validate() const {
    if (!(crop.w > 0.0f) || !(crop.h > 0.0f) || crop.x < 0.0f || crop.y < 0.0f || crop.x + crop.w > 1.0f + kEps ||
        crop.y + crop.h > 1.0f + kEps) {
        return fail("output mapping: crop must lie inside 0..1 with a positive size");
    }
    // Convexity with consistent winding: every consecutive edge pair must turn the same way.
    float sign = 0.0f;
    for (std::size_t i = 0; i < 4; ++i) {
        const glm::vec2 a = corners[i];
        const glm::vec2 b = corners[(i + 1) % 4];
        const glm::vec2 c = corners[(i + 2) % 4];
        const glm::vec2 e1 = b - a;
        const glm::vec2 e2 = c - b;
        const float cross = e1.x * e2.y - e1.y * e2.x;
        if (std::abs(cross) < kEps) {
            return fail("output mapping: corners are degenerate (collinear)");
        }
        if (sign == 0.0f) {
            sign = cross > 0.0f ? 1.0f : -1.0f;
        } else if ((cross > 0.0f ? 1.0f : -1.0f) != sign) {
            return fail("output mapping: corners must form a convex quad");
        }
    }
    for (float w : {blend.left, blend.right, blend.top, blend.bottom}) {
        if (w < 0.0f || w > 1.0f) {
            return fail("output mapping: blend widths must be in 0..1");
        }
    }
    if (!(blendGamma > 0.0f) || !(gamma > 0.0f) || brightness < 0.0f) {
        return fail("output mapping: blendGamma and gamma must be > 0 and brightness >= 0");
    }
    return {};
}

glm::mat3 homographyFromCorners(const std::array<glm::vec2, 4>& corners) {
    // Heckbert, "Fundamentals of Texture Mapping and Image Warping": unit square -> quad.
    const glm::vec2 p0 = corners[0]; // (0,0)
    const glm::vec2 p1 = corners[1]; // (1,0)
    const glm::vec2 p2 = corners[2]; // (1,1)
    const glm::vec2 p3 = corners[3]; // (0,1)
    const float sx = p0.x - p1.x + p2.x - p3.x;
    const float sy = p0.y - p1.y + p2.y - p3.y;
    float g = 0.0f;
    float h = 0.0f;
    if (std::abs(sx) > kEps || std::abs(sy) > kEps) {
        const float dx1 = p1.x - p2.x;
        const float dx2 = p3.x - p2.x;
        const float dy1 = p1.y - p2.y;
        const float dy2 = p3.y - p2.y;
        const float det = dx1 * dy2 - dx2 * dy1;
        if (std::abs(det) > kEps * kEps) {
            g = (sx * dy2 - dx2 * sy) / det;
            h = (dx1 * sy - sx * dy1) / det;
        }
    }
    const float a = p1.x - p0.x + g * p1.x;
    const float b = p3.x - p0.x + h * p3.x;
    const float c = p0.x;
    const float d = p1.y - p0.y + g * p1.y;
    const float e = p3.y - p0.y + h * p3.y;
    const float f = p0.y;
    // glm::mat3 is column-major: (col0, col1, col2).
    return glm::mat3(a, d, g, b, e, h, c, f, 1.0f);
}

glm::mat3 inverseHomography(const std::array<glm::vec2, 4>& corners) {
    glm::mat3 inv = glm::inverse(homographyFromCorners(corners));
    const glm::vec2 centre = 0.25f * (corners[0] + corners[1] + corners[2] + corners[3]);
    const glm::vec3 q = inv * glm::vec3(centre, 1.0f);
    if (q.z < 0.0f) {
        inv = -inv;
    }
    return inv;
}

glm::vec2 projectPoint(const glm::mat3& h, glm::vec2 p) {
    const glm::vec3 q = h * glm::vec3(p, 1.0f);
    const float w = std::abs(q.z) > kEps ? q.z : kEps;
    return glm::vec2(q.x / w, q.y / w);
}

float blendWeight(float distance, float width, float gamma) {
    if (width <= 0.0f) {
        return 1.0f;
    }
    const float t = std::clamp(distance / width, 0.0f, 1.0f);
    return std::pow(t, gamma);
}

float blendWeightAt(glm::vec2 uv, const OutputBlend& blend, float gamma) {
    return blendWeight(uv.x, blend.left, gamma) * blendWeight(1.0f - uv.x, blend.right, gamma) *
           blendWeight(uv.y, blend.top, gamma) * blendWeight(1.0f - uv.y, blend.bottom, gamma);
}

nlohmann::json OutputMapping::toJson() const {
    nlohmann::json j;
    j["crop"] = {crop.x, crop.y, crop.w, crop.h};
    j["corners"] = nlohmann::json::array();
    for (const auto& c : corners) {
        j["corners"].push_back({c.x, c.y});
    }
    j["blend"] = {blend.left, blend.right, blend.top, blend.bottom};
    j["blendGamma"] = blendGamma;
    j["brightness"] = brightness;
    j["gamma"] = gamma;
    j["flipX"] = flipX;
    j["flipY"] = flipY;
    return j;
}

namespace {
Result<void> readFloats(const nlohmann::json& j, const char* key, float* out, std::size_t count) {
    if (!j.contains(key)) {
        return {};
    }
    const auto& v = j.at(key);
    if (!v.is_array() || v.size() != count) {
        return fail("output mapping: '{}' must be an array of {} numbers", key, count);
    }
    for (std::size_t i = 0; i < count; ++i) {
        if (!v[i].is_number()) {
            return fail("output mapping: '{}' must be an array of {} numbers", key, count);
        }
        out[i] = v[i].get<float>();
    }
    return {};
}

template <typename T>
Result<void> readScalar(const nlohmann::json& j, const char* key, T& out) {
    if (!j.contains(key)) {
        return {};
    }
    const auto& v = j.at(key);
    if constexpr (std::is_same_v<T, bool>) {
        if (!v.is_boolean()) {
            return fail("output mapping: '{}' must be a boolean", key);
        }
    } else {
        if (!v.is_number()) {
            return fail("output mapping: '{}' must be a number", key);
        }
    }
    out = v.get<T>();
    return {};
}
} // namespace

Result<OutputMapping> OutputMapping::fromJson(const nlohmann::json& j) {
    if (!j.is_object()) {
        return fail("output mapping: expected an object");
    }
    OutputMapping m;
    float crop[4] = {m.crop.x, m.crop.y, m.crop.w, m.crop.h};
    if (auto r = readFloats(j, "crop", crop, 4); !r) {
        return std::unexpected(r.error());
    }
    m.crop = {crop[0], crop[1], crop[2], crop[3]};
    if (j.contains("corners")) {
        const auto& c = j.at("corners");
        if (!c.is_array() || c.size() != 4) {
            return fail("output mapping: 'corners' must be four [x, y] pairs");
        }
        for (std::size_t i = 0; i < 4; ++i) {
            if (!c[i].is_array() || c[i].size() != 2 || !c[i][0].is_number() || !c[i][1].is_number()) {
                return fail("output mapping: 'corners' must be four [x, y] pairs");
            }
            m.corners[i] = {c[i][0].get<float>(), c[i][1].get<float>()};
        }
    }
    float blend[4] = {m.blend.left, m.blend.right, m.blend.top, m.blend.bottom};
    if (auto r = readFloats(j, "blend", blend, 4); !r) {
        return std::unexpected(r.error());
    }
    m.blend = {blend[0], blend[1], blend[2], blend[3]};
    if (auto r = readScalar(j, "blendGamma", m.blendGamma); !r) {
        return std::unexpected(r.error());
    }
    if (auto r = readScalar(j, "brightness", m.brightness); !r) {
        return std::unexpected(r.error());
    }
    if (auto r = readScalar(j, "gamma", m.gamma); !r) {
        return std::unexpected(r.error());
    }
    if (auto r = readScalar(j, "flipX", m.flipX); !r) {
        return std::unexpected(r.error());
    }
    if (auto r = readScalar(j, "flipY", m.flipY); !r) {
        return std::unexpected(r.error());
    }
    if (auto r = m.validate(); !r) {
        return std::unexpected(r.error());
    }
    return m;
}

} // namespace avgen::rendering
