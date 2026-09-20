#include "world/world_map.hpp"

#include "core/noise.hpp"
#include "scene/struct_hash.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <limits>

namespace avgen::world {
namespace {

using json = nlohmann::json;
using scene::detail::StructHash;

constexpr float kNegInf = -std::numeric_limits<float>::infinity();

// One octave. `valueNoise` returns [0, 1); the signed form is what both the fBm and the ridged
// shapes are built from, and mixing between them per octave is what keeps a range from reading as
// either all dunes or all teeth.
float octave(glm::vec2 p, const NoiseLayer& layer, std::uint32_t seed) {
    glm::vec2 q = p * layer.frequency;
    if (layer.warp > 0.0f) {
        const glm::vec3 w = noise::fbm3Vec(glm::vec3(q * 0.5f, 0.0f), seed ^ 0x9e3779b9u);
        q += glm::vec2(w.x, w.y) * layer.warp * layer.frequency;
    }
    const float s = noise::valueNoise(glm::vec3(q, 0.0f), seed) * 2.0f - 1.0f;
    const float ridged = 1.0f - 2.0f * std::fabs(s);
    return glm::mix(s, ridged, glm::clamp(layer.ridged, 0.0f, 1.0f));
}

// The octave sum. `erosion` turns it from a plain fBm into a multifractal: each octave is scaled by
// how high the coarser octaves already put this point, so a crest keeps its fine detail and a
// hollow loses it. The weight is clamped to 1 so the sum cannot run away.
float octaveSum(glm::vec2 p, const std::vector<NoiseLayer>& layers, std::uint32_t seed, float erosion) {
    const float e = glm::clamp(erosion, 0.0f, 1.0f);
    float sum = 0.0f;
    float weight = 1.0f;
    for (std::size_t i = 0; i < layers.size(); ++i) {
        const float s = octave(p, layers[i], seed + static_cast<std::uint32_t>(i) * 7919u);
        sum += s * layers[i].amplitude * 0.5f * glm::mix(1.0f, weight, e);
        // The next octave's allowance comes from this one's own value, not from a running product:
        // a product decays whatever the terrain does and just blurs everything, where this keeps
        // full detail wherever the coarser octave is already high and takes it away in the hollows.
        weight = glm::clamp((s * 0.5f + 0.5f) * 1.9f, 0.0f, 1.0f);
    }
    return sum;
}

// Distance from p to the polyline, and the level (path y) interpolated at the closest point. One
// point is a radial feature; the loop below degenerates to a point distance without a special case.
struct PathHit {
    float distance = 0.0f;
    float level = 0.0f;
};
// Segments per bounding block. Eight is enough to pay for the box test out of the segments it skips
// and small enough that a block of a meandering curve is still a local piece of it.
constexpr std::size_t kPathBlock = 8;

// Distance from p to an axis-aligned box, or 0 inside it. Conservative by construction: every point
// of the block's polyline is inside the box, so no segment in it can be nearer than this.
// How far past `cutoff` a block's box has to be before skipping it is safe. Relative, because the
// two expressions being reconciled are both a `sqrt` of a sum of squares over world coordinates
// that reach a thousand metres, and absolute as well so that a cutoff of zero still has a margin.
float kCutoffSlack(float cutoff) { return std::abs(cutoff) * 1.0e-5f + 1.0e-3f; }

float boxDistance(const glm::vec4& box, glm::vec2 p) {
    const float dx = std::max({box.x - p.x, 0.0f, p.x - box.z});
    const float dz = std::max({box.y - p.y, 0.0f, p.y - box.w});
    return std::sqrt(dx * dx + dz * dz);
}

// `blocks` is optional and is purely an accelerator: skipping a block whose box is further away than
// the best distance so far cannot change the answer, because the update below is a strict `<` and
// any segment inside the box is at least the box's distance away. Passing an empty span walks every
// segment and gives the identical result -- which is what the determinism captures require and what
// makes this safe to add to a format that is already serialised in scenes.
PathHit closestOnPath(const std::vector<glm::vec3>& path, glm::vec2 p,
                      const std::vector<glm::vec4>& blocks = {}) {
    PathHit best{std::numeric_limits<float>::max(), 0.0f};
    if (path.empty()) {
        return {0.0f, 0.0f};
    }
    if (path.size() == 1) {
        return {glm::distance(p, glm::vec2(path[0].x, path[0].z)), path[0].y};
    }
    const bool blocked = blocks.size() * kPathBlock >= path.size() - 1;
    // ---- a cutoff, so the skip bites from the first block (interactive-performance pass) --------
    //
    // The skip below is a comparison against `best.distance`, and `best.distance` starts at
    // infinity -- so the first block is always walked in full, and for a query point far from the
    // start of the path many more are walked before `best` shrinks enough to reject anything. On
    // Glowmere this function is **62% of an entire timeline scrub**: it is reached from
    // `WorldMap::height` for every terrain feature, `height` is reached from `WorldMap::sample`,
    // and that is what `Navigator::pathClear` asks per walker per step of a 5,400-step replay.
    //
    // So: find the block whose box is nearest `p` -- boxes only, no segment arithmetic -- and walk
    // just that one to get `cutoff`, a distance some segment actually achieves. Every block whose
    // box is **strictly** further than that cannot contain the answer.
    //
    // **Why this returns the identical PathHit, not merely an equally good one.** `cutoff` is a
    // distance achieved by a real segment, so `cutoff >= trueMin`. The block holding the true
    // minimum therefore has `boxDistance <= trueMin <= cutoff` and is never skipped. Every segment
    // this skips has `d >= boxDistance > cutoff >= trueMin`, so `d > trueMin`: it could only ever
    // have been an *intermediate* update of `best`, never the final one, because the update is a
    // strict `<` and the result is decided by the first segment reaching the global minimum.
    // Intermediate updates are invisible in the return value.
    //
    // **And why the comparison carries slack, which that argument does not predict.** `boxDistance`
    // and the segment distance are different expressions, so when the nearest point on a segment
    // *is* the corner of its own block's box the two are mathematically equal and numerically need
    // not be: the box came out a few ULPs larger, `box > cutoff` fired on the block holding the
    // answer, every other block was already further, and the function returned FLT_MAX. Measured,
    // with the slack removed: heights differing from the unaccelerated walk by up to **8.0 m** on
    // four of the terrain styles, which `the path block accelerator returns the identical height`
    // in tests/unit/test_terrain_gen.cpp is what caught. The slack is one-sided on purpose -- it
    // can only ever skip *fewer* blocks, and the worst case it degrades to is the behaviour before
    // this paragraph existed.
    //
    // The distances are computed once and kept, because the loop below needs the same numbers: a
    // feature is a river of about fifteen blocks and a world has a dozen features, so recomputing
    // them was the larger half of what this pass cost. 64 blocks is 512 segments; past that the
    // buffer is declined and the function behaves exactly as it did before this paragraph existed,
    // which is also the fallback that keeps this correct rather than merely bounded.
    constexpr std::size_t kMaxCachedBlocks = 64;
    std::array<float, kMaxCachedBlocks> boxDist{};
    float cutoff = std::numeric_limits<float>::max();
    const bool cached = blocked && blocks.size() > 1 && blocks.size() <= kMaxCachedBlocks;
    if (cached) {
        std::size_t nearest = 0;
        float nearestBox = std::numeric_limits<float>::max();
        for (std::size_t b = 0; b < blocks.size(); ++b) {
            boxDist[b] = boxDistance(blocks[b], p);
            if (boxDist[b] < nearestBox) {
                nearestBox = boxDist[b];
                nearest = b;
            }
        }
        const std::size_t from = nearest * kPathBlock;
        const std::size_t to = std::min(from + kPathBlock, path.size() - 1);
        for (std::size_t i = from; i < to; ++i) {
            const glm::vec2 a(path[i].x, path[i].z);
            const glm::vec2 b(path[i + 1].x, path[i + 1].z);
            const glm::vec2 ab = b - a;
            const float len2 = glm::dot(ab, ab);
            const float t = len2 > 1e-12f ? glm::clamp(glm::dot(p - a, ab) / len2, 0.0f, 1.0f) : 0.0f;
            cutoff = std::min(cutoff, glm::distance(p, a + ab * t));
        }
    }
    for (std::size_t i = 0; i + 1 < path.size(); ++i) {
        if (blocked && i % kPathBlock == 0) {
            const std::size_t b = i / kPathBlock;
            const float box = cached ? boxDist[b] : boxDistance(blocks[b], p);
            if (box >= best.distance || box > cutoff + kCutoffSlack(cutoff)) {
                i += kPathBlock - 1;
                continue;
            }
        }
        const glm::vec2 a(path[i].x, path[i].z);
        const glm::vec2 b(path[i + 1].x, path[i + 1].z);
        const glm::vec2 ab = b - a;
        const float len2 = glm::dot(ab, ab);
        const float t = len2 > 1e-12f ? glm::clamp(glm::dot(p - a, ab) / len2, 0.0f, 1.0f) : 0.0f;
        const float d = glm::distance(p, a + ab * t);
        if (d < best.distance) {
            best.distance = d;
            best.level = glm::mix(path[i].y, path[i + 1].y, t);
        }
    }
    return best;
}

// Weight of a feature at distance d. smoothstep first so the shoulder meets the untouched terrain
// with a zero derivative (a linear falloff leaves a visible crease ring); `falloff` then shapes it.
float featureWeight(float d, float width, float falloff) {
    if (width <= 0.0f) {
        return 0.0f;
    }
    const float u = glm::clamp(1.0f - d / width, 0.0f, 1.0f);
    const float s = u * u * (3.0f - 2.0f * u);
    return falloff == 1.0f ? s : std::pow(s, std::max(falloff, 0.01f));
}

// Chaikin corner cutting: each segment contributes two points a quarter and three quarters along,
// endpoints preserved. The curve stays inside the convex hull of the control polygon, so a smoothed
// river never rises above the levels it was authored with.
std::vector<glm::vec3> chaikin(const std::vector<glm::vec3>& in, int iterations) {
    std::vector<glm::vec3> out = in;
    for (int it = 0; it < iterations && out.size() >= 3; ++it) {
        std::vector<glm::vec3> next;
        next.reserve(out.size() * 2);
        next.push_back(out.front());
        for (std::size_t i = 0; i + 1 < out.size(); ++i) {
            next.push_back(glm::mix(out[i], out[i + 1], 0.25f));
            next.push_back(glm::mix(out[i], out[i + 1], 0.75f));
        }
        next.push_back(out.back());
        out = std::move(next);
    }
    return out;
}

} // namespace

const char* featureKindName(FeatureKind kind) {
    switch (kind) {
    case FeatureKind::Ridge: return "ridge";
    case FeatureKind::Valley: return "valley";
    case FeatureKind::River: return "river";
    case FeatureKind::Flat: return "flat";
    }
    return "ridge";
}

std::optional<FeatureKind> featureKindFromName(std::string_view name) {
    if (name == "ridge") return FeatureKind::Ridge;
    if (name == "valley") return FeatureKind::Valley;
    if (name == "river") return FeatureKind::River;
    if (name == "flat") return FeatureKind::Flat;
    return std::nullopt;
}

Result<void> Feature::validate() const {
    if (path.empty()) {
        return fail("feature '{}': needs at least one path point", name);
    }
    if (path.size() > 256) {
        return fail("feature '{}': path has {} points (max 256)", name, path.size());
    }
    if (!(width > 0.0f) || width > 100000.0f) {
        return fail("feature '{}': width must be in (0, 100000]", name);
    }
    if (!std::isfinite(amplitude) || std::fabs(amplitude) > 100000.0f) {
        return fail("feature '{}': amplitude must be finite and <= 100000", name);
    }
    if (falloff < 0.01f || falloff > 16.0f) {
        return fail("feature '{}': falloff must be in [0.01, 16]", name);
    }
    if (flatten < 0.0f || flatten > 1.0f) {
        return fail("feature '{}': flatten must be in [0, 1]", name);
    }
    if (roughness < 0.0f || roughness > 8.0f) {
        return fail("feature '{}': roughness must be in [0, 8]", name);
    }
    if (waterDepth < 0.0f || waterDepth > 10000.0f) {
        return fail("feature '{}': waterDepth must be in [0, 10000]", name);
    }
    if (smoothing < 0 || smoothing > 5) {
        return fail("feature '{}': smoothing must be in [0, 5]", name);
    }
    return {};
}

float HeightImage::bilinear(glm::vec2 uv) const {
    if (width == 0 || height == 0 || samples.size() < static_cast<std::size_t>(width) * height) {
        return 0.0f;
    }
    const float fx = glm::clamp(uv.x, 0.0f, 1.0f) * static_cast<float>(width - 1);
    const float fy = glm::clamp(uv.y, 0.0f, 1.0f) * static_cast<float>(height - 1);
    const auto x0 = static_cast<std::uint32_t>(fx);
    const auto y0 = static_cast<std::uint32_t>(fy);
    const std::uint32_t x1 = std::min(x0 + 1u, width - 1u);
    const std::uint32_t y1 = std::min(y0 + 1u, height - 1u);
    const float tx = fx - static_cast<float>(x0);
    const float ty = fy - static_cast<float>(y0);
    const float a = samples[static_cast<std::size_t>(y0) * width + x0];
    const float b = samples[static_cast<std::size_t>(y0) * width + x1];
    const float c = samples[static_cast<std::size_t>(y1) * width + x0];
    const float d = samples[static_cast<std::size_t>(y1) * width + x1];
    return glm::mix(glm::mix(a, b, tx), glm::mix(c, d, tx), ty);
}

float WorldMap::height(glm::vec2 p) const {
    // Pass one: what the features want here. Collected before the noise is evaluated because the
    // smallest `roughness` any of them asks for damps the noise itself -- that is what makes a
    // river bed smooth without carving a smooth shape out of a rough one and leaving a rim.
    float raise = 0.0f;
    float roughness = 1.0f;
    float flattenWeight = 0.0f;
    float flattenTarget = 0.0f;
    // Water cuts down to a level; it does not subtract a constant. An additive carve cannot promise
    // a continuous channel -- take 5 m out of a 30 m hillside and the river bed is still 25 m in the
    // air, which is exactly how a river ends up rendering as disconnected puddles. So water features
    // record the bed they want and pull the terrain down to it, weighted by their falloff so the
    // banks still blend out.
    float cutTarget = std::numeric_limits<float>::max();
    float cutWeight = 0.0f;
    for (const Feature& f : features) {
        if (!f.reaches(p)) {
            continue; // exactly equivalent to a zero weight, and it costs one compare
        }
        const PathHit hit = closestOnPath(f.samplePath(), p, f.blocks);
        const float w = featureWeight(hit.distance, f.width, f.falloff);
        if (w <= 0.0f) {
            continue;
        }
        switch (f.kind) {
        case FeatureKind::Ridge: raise += f.amplitude * w; break;
        case FeatureKind::Valley: raise -= f.amplitude * w; break;
        case FeatureKind::River:
            // The path level is the water surface; the bed is `amplitude` below it. The cut is
            // applied with weight w at the end, so the channel centre is guaranteed under the water
            // line along the whole course while the banks blend out continuously.
            cutTarget = std::min(cutTarget, hit.level - f.amplitude);
            cutWeight = std::max(cutWeight, w);
            break;
        case FeatureKind::Flat:
            if (f.water) {
                cutTarget = std::min(cutTarget, hit.level);
                cutWeight = std::max(cutWeight, w);
            }
            break;
        }
        roughness = std::min(roughness, glm::mix(1.0f, f.roughness, w));
        const float fw = w * f.flatten;
        if (fw > 0.0f) {
            // Overlapping flatteners average by weight rather than fighting: two clearings that
            // touch produce one terrace, not a step.
            flattenTarget = (flattenTarget * flattenWeight + hit.level * fw) / (flattenWeight + fw);
            flattenWeight = std::min(1.0f, flattenWeight + fw);
        }
    }

    float base = octaveSum(p, layers, seed, erosion);
    if (image && imageBlend > 0.0f) {
        const glm::vec2 uv = (p - min()) / glm::max(size, glm::vec2(1e-6f));
        // v is flipped: image row 0 is the top of the map, which is -Z.
        const float painted = image->bilinear(glm::vec2(uv.x, 1.0f - uv.y)) * imageHeight;
        base = glm::mix(base, painted, glm::clamp(imageBlend, 0.0f, 1.0f));
    }

    float h = baseHeight + base * roughness + raise;
    h = glm::mix(h, flattenTarget, glm::clamp(flattenWeight, 0.0f, 1.0f));
    if (cutWeight > 0.0f) {
        h = glm::mix(h, std::min(h, cutTarget), cutWeight);
    }
    return h;
}

glm::vec3 WorldMap::normal(glm::vec2 p, float epsilon) const {
    const float e = std::max(epsilon, 1e-3f);
    const float hx = height(p + glm::vec2(e, 0.0f)) - height(p - glm::vec2(e, 0.0f));
    const float hz = height(p + glm::vec2(0.0f, e)) - height(p - glm::vec2(0.0f, e));
    return glm::normalize(glm::vec3(-hx, 2.0f * e, -hz));
}

float WorldMap::waterSurface(glm::vec2 p) const {
    float surface = seaLevel;
    for (const Feature& f : features) {
        if (!f.water || !f.reaches(p)) {
            continue;
        }
        const PathHit hit = closestOnPath(f.samplePath(), p, f.blocks);
        // Only inside the bank, and only where the feature actually reaches: a wide, soft-shouldered
        // river should not flood the shoulder just because the shoulder is within `width`.
        if (featureWeight(hit.distance, f.width, f.falloff) > 0.0f) {
            surface = std::max(surface, hit.level + f.waterDepth);
        }
    }
    return surface > kNegInf ? surface : kNegInf;
}

float WorldMap::waterTable(glm::vec2 p) const {
    // The nearest water feature wins, rather than the highest or the sum. A point between two
    // courses belongs to the one it is closer to; taking the max would make a high tributary flood
    // the ecology of a low valley across the ridge between them.
    float best = std::numeric_limits<float>::max();
    float surface = seaLevel;
    for (const Feature& f : features) {
        if (!f.water) {
            continue;
        }
        // No `reaches` gate, unlike `waterSurface`: the whole purpose is to answer outside the bank.
        const PathHit hit = closestOnPath(f.samplePath(), p, f.blocks);
        if (hit.distance < best) {
            best = hit.distance;
            surface = hit.level + f.waterDepth;
        }
    }
    return surface;
}

float WorldMap::moisture(glm::vec2 p, float altitude01) const {
    // Distance to the nearest water feature's bank, faded over `moistureReach`. The bounding boxes
    // features carry are grown by their width, not by the reach, so the search is over every water
    // feature rather than the ones whose weight is non-zero -- there are few of them and the fade
    // extends well past the bank.
    float wet = 0.0f;
    for (const Feature& f : features) {
        if (!f.water) {
            continue;
        }
        const PathHit hit = closestOnPath(f.samplePath(), p, f.blocks);
        const float beyondBank = std::max(hit.distance - f.width, 0.0f);
        wet = std::max(wet, std::exp(-beyondBank / std::max(moistureReach, 1e-3f)));
    }
    // Low ground is damp even away from a river: that is what makes a basin floor read as a basin
    // floor rather than as a plateau that happens to be low.
    const float lowland = lowlandMoisture * (1.0f - glm::clamp(altitude01, 0.0f, 1.0f));
    return glm::clamp(std::max(wet, lowland), 0.0f, 1.0f);
}

Sample WorldMap::sample(glm::vec2 p, float epsilon) const {
    Sample s;
    s.height = height(p);
    s.normal = normal(p, epsilon);
    s.slope = glm::clamp(1.0f - s.normal.y, 0.0f, 1.0f);
    s.waterSurface = waterSurface(p);
    s.submerged = s.height < s.waterSurface;
    s.altitude = altitude01(s.height);
    s.moisture = moisture(p, s.altitude);
    return s;
}

void WorldMap::prepare() {
    for (Feature& f : features) {
        f.curve = f.smoothing > 0 && f.path.size() >= 3 ? chaikin(f.path, f.smoothing) : std::vector<glm::vec3>{};
        glm::vec2 lo(std::numeric_limits<float>::max());
        glm::vec2 hi(std::numeric_limits<float>::lowest());
        for (const glm::vec3& q : f.samplePath()) {
            lo = glm::min(lo, glm::vec2(q.x, q.z));
            hi = glm::max(hi, glm::vec2(q.x, q.z));
        }
        f.boundsMin = lo - glm::vec2(f.width);
        f.boundsMax = hi + glm::vec2(f.width);
        f.blocks.clear();
        const std::vector<glm::vec3>& pts = f.samplePath();
        for (std::size_t i = 0; i + 1 < pts.size(); i += kPathBlock) {
            glm::vec2 blo(std::numeric_limits<float>::max());
            glm::vec2 bhi(std::numeric_limits<float>::lowest());
            for (std::size_t k = i; k < std::min(i + kPathBlock + 1, pts.size()); ++k) {
                blo = glm::min(blo, glm::vec2(pts[k].x, pts[k].z));
                bhi = glm::max(bhi, glm::vec2(pts[k].x, pts[k].z));
            }
            f.blocks.emplace_back(blo.x, blo.y, bhi.x, bhi.y);
        }
    }
    // A coarse survey rather than the exact extremes: 97 samples a side is enough to place the
    // range within a metre or two, and what altitude blending needs is a stable reference every
    // chunk agrees on, not the true maximum.
    constexpr int kSurvey = 97;
    sampledMinHeight = std::numeric_limits<float>::max();
    sampledMaxHeight = std::numeric_limits<float>::lowest();
    for (int j = 0; j < kSurvey; ++j) {
        for (int i = 0; i < kSurvey; ++i) {
            const glm::vec2 uv((static_cast<float>(i) + 0.5f) / kSurvey, (static_cast<float>(j) + 0.5f) / kSurvey);
            const float h = height(min() + size * uv);
            sampledMinHeight = std::min(sampledMinHeight, h);
            sampledMaxHeight = std::max(sampledMaxHeight, h);
        }
    }
    if (!(sampledMaxHeight > sampledMinHeight)) {
        sampledMaxHeight = sampledMinHeight + 1.0f;
    }
}

Result<void> WorldMap::validate() const {
    if (!(size.x > 0.0f) || !(size.y > 0.0f) || size.x > 1e6f || size.y > 1e6f) {
        return fail("world '{}': size must be positive and <= 1e6 metres", name);
    }
    if (layers.size() > 12) {
        return fail("world '{}': {} noise layers (max 12)", name, layers.size());
    }
    for (const NoiseLayer& l : layers) {
        if (!(l.frequency > 0.0f) || l.frequency > 10.0f) {
            return fail("world '{}': layer frequency must be in (0, 10]", name);
        }
        if (!std::isfinite(l.amplitude) || std::fabs(l.amplitude) > 100000.0f) {
            return fail("world '{}': layer amplitude must be finite and <= 100000", name);
        }
        if (l.ridged < 0.0f || l.ridged > 1.0f) {
            return fail("world '{}': layer ridged must be in [0, 1]", name);
        }
        if (l.warp < 0.0f || l.warp > 10000.0f) {
            return fail("world '{}': layer warp must be in [0, 10000]", name);
        }
    }
    if (features.size() > 512) {
        return fail("world '{}': {} features (max 512)", name, features.size());
    }
    for (const Feature& f : features) {
        if (auto r = f.validate(); !r) {
            return r;
        }
    }
    if (auto r = biomes.validate(); !r) {
        return fail("world '{}': {}", name, r.error().message);
    }
    if (moistureReach <= 0.0f || moistureReach > 100000.0f) {
        return fail("world '{}': moistureReach must be in (0, 100000]", name);
    }
    if (lowlandMoisture < 0.0f || lowlandMoisture > 1.0f) {
        return fail("world '{}': lowlandMoisture must be in [0, 1]", name);
    }
    if (erosion < 0.0f || erosion > 1.0f) {
        return fail("world '{}': erosion must be in [0, 1]", name);
    }
    if (imageBlend < 0.0f || imageBlend > 1.0f) {
        return fail("world '{}': imageBlend must be in [0, 1]", name);
    }
    return {};
}

std::uint64_t WorldMap::structuralHash() const {
    StructHash h;
    h.str(name);
    h.u32(seed);
    h.f32(size.x);
    h.f32(size.y);
    h.f32(baseHeight);
    h.f32(erosion);
    h.f32(seaLevel);
    h.u64(layers.size());
    for (const NoiseLayer& l : layers) {
        h.f32(l.frequency);
        h.f32(l.amplitude);
        h.f32(l.ridged);
        h.f32(l.warp);
    }
    h.u64(features.size());
    for (const Feature& f : features) {
        h.str(f.name);
        h.u32(static_cast<std::uint32_t>(f.kind));
        h.u64(f.path.size());
        for (const glm::vec3& q : f.path) {
            h.v3(q);
        }
        h.f32(f.width);
        h.f32(f.amplitude);
        h.f32(f.falloff);
        h.f32(f.flatten);
        h.f32(f.roughness);
        h.boolean(f.water);
        h.f32(f.waterDepth);
        h.i32(f.smoothing);
    }
    h.f32(moistureReach);
    h.f32(lowlandMoisture);
    h.u64(biomes.structuralHash());
    h.str(heightImage);
    h.f32(imageHeight);
    h.f32(imageBlend);
    // The loaded image is part of what a sample is, and two different files can share a path
    // across a reload, so its size and a cheap checksum go in too.
    if (image) {
        h.u32(image->width);
        h.u32(image->height);
        float sum = 0.0f;
        for (std::size_t i = 0; i < image->samples.size(); i += 97) {
            sum += image->samples[i];
        }
        h.f32(sum);
    }
    return h.value();
}

// ---- the shipped world -------------------------------------------------------------------------

// "Glowmere Basin". A bowl 640 m across, walled to the north by a high rim and to the east and
// west by lower arms, with a valley running north to south down the middle and a river in it. The
// camera starts in the south of the basin looking north, so the composition it sees is: foreground
// hollow, midground river, background rim -- three depth planes that exist in the geometry rather
// than being faked by fog.
//
// Every number here is a design decision, not a default. The rim is 62 m because a 45 mm lens at
// 200 m puts a 62 m wall across the top third of frame; the valley flattens only 55% so it reads as
// a valley floor rather than a road; the river's roughness of 0.12 is what stops its bed from
// inheriting the ridged octave and looking like rapids everywhere.
WorldMap defaultWorld() {
    WorldMap w;
    w.name = "glowmere";
    w.seed = 20260909u;
    w.size = {640.0f, 640.0f};
    w.baseHeight = 0.0f;
    w.erosion = 0.35f;
    w.biomes = defaultBiomes();
    w.seaLevel = -1000.0f;
    // Five octaves, weighted toward the middle rather than the bottom. A spectrum dominated by its
    // lowest octave gives two smooth hills and nothing to read at any other distance; the 0.0075 and
    // 0.018 layers are the ones that put spurs on a hillside and drainage lines between them, which
    // is what tells the eye how big the hill is.
    w.layers = {
        {0.0030f, 32.0f, 0.10f, 60.0f},  // continental shape; the warp keeps it off a grid
        {0.0075f, 21.0f, 0.45f, 26.0f},  // massifs and the spurs that come off them
        {0.0180f, 13.0f, 0.72f, 9.0f},   // ridged drainage texture: gullies between the spurs
        {0.0460f, 5.0f, 0.35f, 0.0f},    // hillside relief at walking scale
        {0.1300f, 1.6f, 0.15f, 0.0f},    // what a hillside looks like from ten metres away
        {0.3400f, 0.45f, 0.0f, 0.0f},    // ground texture underfoot; invisible past thirty metres
    };

    auto feature = [](std::string name, FeatureKind kind, std::vector<glm::vec3> path, float width,
                      float amplitude, float falloff, float flatten, float roughness) {
        Feature f;
        f.name = std::move(name);
        f.kind = kind;
        f.path = std::move(path);
        f.width = width;
        f.amplitude = amplitude;
        f.falloff = falloff;
        f.flatten = flatten;
        f.roughness = roughness;
        return f;
    };

    // The rim: the thing on the horizon. A single line across the north with broad shoulders.
    w.features.push_back(feature("northern-rim", FeatureKind::Ridge,
                                 {{-330.0f, 0.0f, -300.0f}, {-90.0f, 0.0f, -258.0f},
                                  {80.0f, 0.0f, -276.0f}, {320.0f, 0.0f, -238.0f}},
                                 165.0f, 62.0f, 0.75f, 0.0f, 1.0f));
    // The arms. Lower, asymmetric on purpose: a symmetrical bowl reads as a crater.
    w.features.push_back(feature("west-arm", FeatureKind::Ridge,
                                 {{-268.0f, 0.0f, -230.0f}, {-236.0f, 0.0f, -60.0f}, {-282.0f, 0.0f, 150.0f}},
                                 130.0f, 40.0f, 0.85f, 0.0f, 1.0f));
    w.features.push_back(feature("east-arm", FeatureKind::Ridge,
                                 {{252.0f, 0.0f, -216.0f}, {214.0f, 0.0f, -20.0f}, {268.0f, 0.0f, 180.0f}},
                                 112.0f, 31.0f, 0.9f, 0.0f, 1.0f));
    // A spur reaching into the basin from the west: it crosses the eye-line at mid depth and gives
    // the wide shot something to occlude with, which is what separates a valley from a bowl.
    w.features.push_back(feature("west-spur", FeatureKind::Ridge,
                                 {{-232.0f, 0.0f, -104.0f}, {-120.0f, 0.0f, -122.0f}, {-52.0f, 0.0f, -150.0f}},
                                 54.0f, 26.0f, 1.2f, 0.0f, 1.0f));

    // The valley floor, authored as a descending centre line from the rim down to the south edge.
    // `flatten` at 0.55 leaves the noise half visible, so the floor undulates the way a real one does.
    w.features.push_back(feature("basin-floor", FeatureKind::Valley,
                                 {{-24.0f, 25.0f, -252.0f}, {-6.0f, 13.0f, -180.0f}, {16.0f, 5.0f, -104.0f},
                                  {20.0f, 1.0f, -62.0f}, {6.0f, -3.4f, -8.0f}, {-6.0f, -8.0f, 58.0f},
                                  {-20.0f, -11.0f, 108.0f}, {-40.0f, -15.0f, 250.0f}},
                                 132.0f, 22.0f, 0.7f, 0.5f, 0.85f));

    // The river. Narrow, shallow and meandering: 7 m of half-width, 2.4 m of bed under the water
    // line, and fourteen path points that wander either side of the valley centre.
    //
    // Its levels sit about five metres under the valley floor's, and that margin is load bearing.
    // A water surface is flat across its width and stops where the ground rises through it; author
    // it above the ground beside it and it stops instead at the edge of its own channel, as a wall
    // of water standing over the floodplain. The floor here wanders several metres either side of
    // the line `basin-floor` flattens toward, so the margin has to cover that wander, not just the
    // nominal difference between the two paths. Straightness is
    // the single loudest tell that a river was generated, and it costs nothing to author away --
    // each point is a bend, and the levels descend monotonically so the water always runs downhill.
    w.features.push_back(feature("glowmere-run", FeatureKind::River,
                                 {{-24.0f, 17.0f, -246.0f},  {-10.0f, 13.4f, -222.0f}, {-16.0f, 10.2f, -196.0f},
                                  {2.0f, 7.0f, -172.0f},    {12.0f, 3.6f, -146.0f},   {2.0f, 0.4f, -118.0f},
                                  {14.0f, -2.8f, -92.0f},     {26.0f, -5.8f, -64.0f},   {14.0f, -8.4f, -38.0f},
                                  {6.0f, -10.6f, -12.0f},     {12.0f, -12.8f, 18.0f},    {-4.0f, -15.2f, 56.0f},
                                  {-20.0f, -17.6f, 104.0f},  {-14.0f, -19.4f, 152.0f}, {-38.0f, -22.0f, 248.0f}},
                                 7.0f, 2.4f, 1.4f, 0.0f, 0.12f));
    w.features.back().water = true;
    w.features.back().waterDepth = 0.0f;

    // The tarn: a still pool in a side hollow off the west bank, at a level of its own. A second
    // water body at a different height is what tells the eye the ground is not one plane.
    w.features.push_back(feature("west-tarn", FeatureKind::Valley, {{-104.0f, 0.0f, -34.0f}}, 44.0f, 11.0f,
                                 1.0f, 0.0f, 0.6f));
    // The tarn's bed sits well under the ground around its bowl, for the same reason the river's
    // water line does: a still body is flat, and the ground at the edge of its basin has to be
    // above it or the surface ends in a wall. The first version was authored at -6.5 in ground
    // that runs about -7, and flooded outward until its own width cut it off.
    w.features.push_back(feature("west-tarn-bed", FeatureKind::Flat, {{-104.0f, -13.5f, -34.0f}}, 30.0f, 0.0f,
                                 1.4f, 0.9f, 0.2f));
    w.features.back().water = true;
    w.features.back().waterDepth = 1.4f;

    // The hollow: the clearing the opening shot is composed in. Flattened but not levelled, and
    // quiet -- roughness 0.3 -- because a hero foreground has to hold a plant without it tilting.
    w.features.push_back(feature("the-hollow", FeatureKind::Flat, {{4.0f, -4.2f, -14.0f}}, 30.0f, 0.0f, 1.1f,
                                 0.88f, 0.3f));
    // Two shelves on the west slope: places a camera can stand, and steps that break the slope into
    // readable bands instead of one long ramp.
    w.features.push_back(feature("upper-shelf", FeatureKind::Flat, {{-138.0f, 17.0f, -128.0f}, {-96.0f, 15.0f, -92.0f}},
                                 28.0f, 0.0f, 1.2f, 0.8f, 0.45f));
    w.features.push_back(feature("lower-shelf", FeatureKind::Flat, {{-74.0f, 2.5f, -46.0f}, {-58.0f, 1.5f, -12.0f}},
                                 22.0f, 0.0f, 1.2f, 0.75f, 0.5f));

    // The south half is where the camera starts, so it cannot be the part that was left as a slope.
    // A pair of knolls to occlude with, a side valley entering from the east to break the symmetry,
    // and a terrace at the mouth so the ground the camera stands on is level enough to compose on.
    w.features.push_back(feature("south-knoll", FeatureKind::Ridge, {{86.0f, 0.0f, 74.0f}}, 62.0f, 21.0f, 1.1f,
                                 0.0f, 1.0f));
    w.features.push_back(feature("hollow-knoll", FeatureKind::Ridge,
                                 {{-64.0f, 0.0f, 34.0f}, {-30.0f, 0.0f, 62.0f}}, 40.0f, 13.0f, 1.3f, 0.0f, 1.0f));
    w.features.push_back(feature("east-gully", FeatureKind::Valley,
                                 {{206.0f, 0.0f, -34.0f}, {124.0f, 0.0f, -18.0f}, {46.0f, 0.0f, -14.0f}},
                                 46.0f, 16.0f, 1.0f, 0.25f, 0.8f));
    w.features.push_back(feature("south-terrace", FeatureKind::Flat,
                                 {{-6.0f, -7.5f, 34.0f}, {36.0f, -7.0f, 20.0f}}, 44.0f, 0.0f, 1.0f, 0.6f, 0.6f));

    w.prepare();
    return w;
}

// ---- json --------------------------------------------------------------------------------------

namespace {

Result<float> readFloat(const json& j, const char* key, float def) {
    if (!j.contains(key)) {
        return def;
    }
    if (!j.at(key).is_number()) {
        return fail("'{}' must be a number", key);
    }
    return j.at(key).get<float>();
}

Result<bool> readBool(const json& j, const char* key, bool def) {
    if (!j.contains(key)) {
        return def;
    }
    if (!j.at(key).is_boolean()) {
        return fail("'{}' must be a boolean", key);
    }
    return j.at(key).get<bool>();
}

Result<glm::vec3> readVec3(const json& j) {
    if (!j.is_array() || j.size() != 3) {
        return fail("path points must be arrays of 3 numbers [x, level, z]");
    }
    glm::vec3 v{};
    for (std::size_t i = 0; i < 3; ++i) {
        if (!j.at(i).is_number()) {
            return fail("path points must be arrays of 3 numbers [x, level, z]");
        }
        v[static_cast<glm::length_t>(i)] = j.at(i).get<float>();
    }
    return v;
}

} // namespace

Result<WorldMap> worldMapFromJson(const json& j) {
    if (!j.is_object()) {
        return fail("world map must be a JSON object");
    }
    // Anything omitted keeps the shipped world's value, so a scene can say `{"seed": 12}` and get a
    // designed landscape rather than an empty one. A world file that wants to start from nothing
    // sets "layers": [] and "features": [] explicitly.
    WorldMap w = defaultWorld();
    if (j.contains("name")) {
        if (!j.at("name").is_string()) {
            return fail("world 'name' must be a string");
        }
        w.name = j.at("name").get<std::string>();
    }
    if (j.contains("seed")) {
        if (!j.at("seed").is_number_unsigned()) {
            return fail("world 'seed' must be an unsigned integer");
        }
        w.seed = j.at("seed").get<std::uint32_t>();
    }
    if (j.contains("size")) {
        const json& s = j.at("size");
        if (s.is_number()) {
            w.size = glm::vec2(s.get<float>());
        } else if (s.is_array() && s.size() == 2 && s.at(0).is_number() && s.at(1).is_number()) {
            w.size = {s.at(0).get<float>(), s.at(1).get<float>()};
        } else {
            return fail("world 'size' must be a number or [x, z]");
        }
    }
    auto baseHeight = readFloat(j, "baseHeight", w.baseHeight);
    if (!baseHeight) return fail("world '{}': {}", w.name, baseHeight.error().message);
    w.baseHeight = *baseHeight;
    auto moistureReach = readFloat(j, "moistureReach", w.moistureReach);
    if (!moistureReach) return fail("world '{}': {}", w.name, moistureReach.error().message);
    w.moistureReach = *moistureReach;
    auto lowland = readFloat(j, "lowlandMoisture", w.lowlandMoisture);
    if (!lowland) return fail("world '{}': {}", w.name, lowland.error().message);
    w.lowlandMoisture = *lowland;
    if (j.contains("biomes")) {
        auto set = biomeSetFromJson(j.at("biomes"));
        if (!set) {
            return fail("world '{}': {}", w.name, set.error().message);
        }
        w.biomes = std::move(*set);
    }
    auto erosion = readFloat(j, "erosion", w.erosion);
    if (!erosion) return fail("world '{}': {}", w.name, erosion.error().message);
    w.erosion = *erosion;
    auto seaLevel = readFloat(j, "seaLevel", w.seaLevel);
    if (!seaLevel) return fail("world '{}': {}", w.name, seaLevel.error().message);
    w.seaLevel = *seaLevel;
    auto imageHeight = readFloat(j, "imageHeight", w.imageHeight);
    if (!imageHeight) return fail("world '{}': {}", w.name, imageHeight.error().message);
    w.imageHeight = *imageHeight;
    auto imageBlend = readFloat(j, "imageBlend", w.imageBlend);
    if (!imageBlend) return fail("world '{}': {}", w.name, imageBlend.error().message);
    w.imageBlend = *imageBlend;
    if (j.contains("heightImage")) {
        if (!j.at("heightImage").is_string()) {
            return fail("world 'heightImage' must be a string path");
        }
        w.heightImage = j.at("heightImage").get<std::string>();
    }

    if (j.contains("layers")) {
        if (!j.at("layers").is_array()) {
            return fail("world 'layers' must be an array");
        }
        w.layers.clear();
        for (const json& e : j.at("layers")) {
            if (!e.is_object()) {
                return fail("world 'layers' entries must be objects");
            }
            NoiseLayer l;
            auto f = readFloat(e, "frequency", l.frequency);
            if (!f) return fail("world layer: {}", f.error().message);
            l.frequency = *f;
            auto a = readFloat(e, "amplitude", l.amplitude);
            if (!a) return fail("world layer: {}", a.error().message);
            l.amplitude = *a;
            auto r = readFloat(e, "ridged", l.ridged);
            if (!r) return fail("world layer: {}", r.error().message);
            l.ridged = *r;
            auto wp = readFloat(e, "warp", l.warp);
            if (!wp) return fail("world layer: {}", wp.error().message);
            l.warp = *wp;
            w.layers.push_back(l);
        }
    }

    if (j.contains("features")) {
        if (!j.at("features").is_array()) {
            return fail("world 'features' must be an array");
        }
        w.features.clear();
        for (const json& e : j.at("features")) {
            if (!e.is_object()) {
                return fail("world 'features' entries must be objects");
            }
            Feature f;
            if (e.contains("name")) {
                if (!e.at("name").is_string()) {
                    return fail("feature 'name' must be a string");
                }
                f.name = e.at("name").get<std::string>();
            }
            if (e.contains("kind")) {
                if (!e.at("kind").is_string()) {
                    return fail("feature '{}': 'kind' must be a string", f.name);
                }
                const auto kind = featureKindFromName(e.at("kind").get<std::string>());
                if (!kind) {
                    return fail("feature '{}': unknown kind '{}' (ridge, valley, river, flat)", f.name,
                                e.at("kind").get<std::string>());
                }
                f.kind = *kind;
            }
            if (!e.contains("path") || !e.at("path").is_array()) {
                return fail("feature '{}': 'path' must be an array of [x, level, z] points", f.name);
            }
            for (const json& pt : e.at("path")) {
                auto v = readVec3(pt);
                if (!v) return fail("feature '{}': {}", f.name, v.error().message);
                f.path.push_back(*v);
            }
            struct FloatField { const char* key; float* target; };
            for (const FloatField& field : {FloatField{"width", &f.width}, FloatField{"amplitude", &f.amplitude},
                                            FloatField{"falloff", &f.falloff}, FloatField{"flatten", &f.flatten},
                                            FloatField{"roughness", &f.roughness},
                                            FloatField{"waterDepth", &f.waterDepth}}) {
                auto v = readFloat(e, field.key, *field.target);
                if (!v) return fail("feature '{}': {}", f.name, v.error().message);
                *field.target = *v;
            }
            if (e.contains("smoothing")) {
                if (!e.at("smoothing").is_number_integer()) {
                    return fail("feature '{}': 'smoothing' must be an integer", f.name);
                }
                f.smoothing = e.at("smoothing").get<int>();
            }
            auto water = readBool(e, "water", f.water);
            if (!water) return fail("feature '{}': {}", f.name, water.error().message);
            f.water = *water;
            w.features.push_back(std::move(f));
        }
    }

    if (auto r = w.validate(); !r) {
        return fail("{}", r.error().message);
    }
    w.prepare();
    return w;
}

json worldMapToJson(const WorldMap& map) {
    json j = json::object();
    j["name"] = map.name;
    j["seed"] = map.seed;
    j["size"] = json::array({map.size.x, map.size.y});
    j["baseHeight"] = map.baseHeight;
    j["erosion"] = map.erosion;
    j["moistureReach"] = map.moistureReach;
    j["lowlandMoisture"] = map.lowlandMoisture;
    if (!map.biomes.empty()) {
        j["biomes"] = biomeSetToJson(map.biomes);
    }
    j["seaLevel"] = map.seaLevel;
    if (!map.heightImage.empty()) {
        j["heightImage"] = map.heightImage;
        j["imageHeight"] = map.imageHeight;
        j["imageBlend"] = map.imageBlend;
    }
    json layers = json::array();
    for (const NoiseLayer& l : map.layers) {
        layers.push_back(json{{"frequency", l.frequency}, {"amplitude", l.amplitude},
                              {"ridged", l.ridged}, {"warp", l.warp}});
    }
    j["layers"] = std::move(layers);
    json features = json::array();
    for (const Feature& f : map.features) {
        json path = json::array();
        for (const glm::vec3& p : f.path) {
            path.push_back(json::array({p.x, p.y, p.z}));
        }
        json e{{"name", f.name},         {"kind", featureKindName(f.kind)}, {"path", std::move(path)},
               {"width", f.width},       {"amplitude", f.amplitude},        {"falloff", f.falloff},
               {"flatten", f.flatten},   {"roughness", f.roughness},        {"smoothing", f.smoothing}};
        if (f.water) {
            e["water"] = true;
            e["waterDepth"] = f.waterDepth;
        }
        features.push_back(std::move(e));
    }
    j["features"] = std::move(features);
    return j;
}

} // namespace avgen::world
