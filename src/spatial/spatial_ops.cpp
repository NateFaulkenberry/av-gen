// Spatial operators over point clouds (ADR-024 §4).
//
// Conventions chosen here:
// * Random ops draw hashIndex(uint(seed[i]) + op.seed * 0x9E3779B9, uint(id[i]), channel): with
//   op.seed == 0 the point's own seed column is used unchanged, so a Randomize op with seed 0 on
//   a cloud whose seed column holds the generator seed and whose ids are the row indices
//   reproduces scene::variationTransform bit for bit. Channels: Randomize 16..25 (position
//   16-18, rotation 19-21, scale 22-24, uniform 25, exactly Variation's), Scatter 40..42,
//   FilterProbability 43, FilterDensity (probabilistic) 44.
// * `amount` scales Translate, Rotate (angle), Noise, Scatter and Randomize (all ranges);
//   amount 0 makes those ops a no-op. Other kinds ignore it.
// * Filters (FilterX, Delete, Sample) keep ids and the `index` column; Sort, Duplicate and
//   Merge renumber indices (index = row / (n - 1)).
// * Transform: p' = pivot + R (S (p - pivot)) + position; rotation' = R rotation; scale' *= S.
// * Duplicate copy k (1..copies): p' = pivot + R^k ((p - pivot) factor^k) + offset k;
//   rotation' = R^k rotation; scale' *= factor^k; id' = maxId + 1 + (k - 1) n + row.
// * Sample by count keeps rows floor(k (n - 1) / (count - 1)) for k in 0..count-1 (all rows when
//   count >= n); by stride keeps rows i >= start with (i - start) % stride == 0.
// * mergeClouds renumbers b's ids to maxId(a) + 1 + row.

#include "spatial/spatial_ops.hpp"

#include "core/noise.hpp"
#include "spatial/detail.hpp"

#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace avgen::spatial {

using nlohmann::json;

namespace {

constexpr std::uint32_t kRandomizeChannel = 16;
constexpr std::uint32_t kScatterChannel = 40;
constexpr std::uint32_t kProbabilityChannel = 43;
constexpr std::uint32_t kDensityChannel = 44;

glm::vec3 unitOr(const glm::vec3& v, const glm::vec3& fallback) {
    const float len = glm::length(v);
    return len > 1e-8f ? v / len : fallback;
}

glm::quat toQuat(const glm::vec4& v) {
    return glm::quat(v.w, v.x, v.y, v.z);
}

glm::vec4 fromQuat(const glm::quat& q) {
    return glm::vec4(q.x, q.y, q.z, q.w);
}

float opRandom(const PointCloud& cloud, const PointOp& op, std::size_t i, std::uint32_t channel) {
    const auto seed = static_cast<std::uint32_t>(cloud.seeds()[i]) + op.seed * 0x9E3779B9u;
    return noise::hashIndex(seed, static_cast<std::uint32_t>(cloud.ids()[i]), channel);
}

float compareValues(float a, float b, int op) {
    switch (op) {
    case 0:
        return a < b ? 1.0f : 0.0f;
    case 1:
        return a <= b ? 1.0f : 0.0f;
    case 2:
        return a == b ? 1.0f : 0.0f;
    case 4:
        return a > b ? 1.0f : 0.0f;
    case 5:
        return a != b ? 1.0f : 0.0f;
    case 3:
    default:
        return a >= b ? 1.0f : 0.0f;
    }
}

std::int32_t maxId(const PointCloud& cloud) {
    std::int32_t m = -1;
    for (const std::int32_t id : cloud.ids()) {
        m = std::max(m, id);
    }
    return m;
}

// Applies a filter predicate (keep row i?) honouring `invert`.
template <typename Pred>
void filterRows(PointCloud& cloud, const PointOp& op, Pred pred) {
    const std::size_t n = cloud.count();
    std::vector<std::uint8_t> keep(n);
    for (std::size_t i = 0; i < n; ++i) {
        const bool k = pred(i);
        keep[i] = (k != op.invert) ? 1u : 0u;
    }
    cloud.attributes.keepRows(keep);
}

// ---- per-kind implementations -----------------------------------------------------------------------

void applyTransform(PointCloud& cloud, const PointOp& op) {
    const glm::quat r = glm::quat(glm::radians(op.rotationDegrees));
    auto pos = cloud.positions();
    auto rot = cloud.rotations();
    auto sc = cloud.scales();
    for (std::size_t i = 0; i < cloud.count(); ++i) {
        pos[i] = op.pivot + r * ((pos[i] - op.pivot) * op.scale) + op.position;
        rot[i] = fromQuat(glm::normalize(r * toQuat(rot[i])));
        sc[i] *= op.scale;
    }
}

void applyTranslate(PointCloud& cloud, const PointOp& op) {
    const glm::vec3 delta = op.offset * op.amount;
    for (glm::vec3& p : cloud.positions()) {
        p += delta;
    }
}

void applyRotate(PointCloud& cloud, const PointOp& op) {
    const glm::quat r = glm::angleAxis(op.angle * op.amount, unitOr(op.axis, glm::vec3(0.0f, 1.0f, 0.0f)));
    auto pos = cloud.positions();
    auto rot = cloud.rotations();
    for (std::size_t i = 0; i < cloud.count(); ++i) {
        pos[i] = op.pivot + r * (pos[i] - op.pivot);
        rot[i] = fromQuat(glm::normalize(r * toQuat(rot[i])));
    }
}

void applyScale(PointCloud& cloud, const PointOp& op) {
    auto pos = cloud.positions();
    auto sc = cloud.scales();
    for (std::size_t i = 0; i < cloud.count(); ++i) {
        pos[i] = op.pivot + (pos[i] - op.pivot) * op.factor;
        if (op.scaleInstances) {
            sc[i] *= op.factor;
        }
    }
}

void applyNoise(PointCloud& cloud, const PointOp& op) {
    for (glm::vec3& p : cloud.positions()) {
        p += noise::fbm3Vec(p * op.frequency + op.offset, op.seed) * op.axisMask * op.amount;
    }
}

void applyRandomize(PointCloud& cloud, const PointOp& op) {
    auto pos = cloud.positions();
    auto rot = cloud.rotations();
    auto sc = cloud.scales();
    const glm::vec3 randomPosition = op.randomPosition * op.amount;
    const glm::vec3 randomRotation = op.randomRotation * op.amount;
    const glm::vec3 randomScale = op.randomScale * op.amount;
    const float randomUniformScale = op.randomUniformScale * op.amount;
    for (std::size_t i = 0; i < cloud.count(); ++i) {
        // Identical to scene::variationTransform with (seed, index) = (point seed, point id).
        const auto r = [&](std::uint32_t channel) { return opRandom(cloud, op, i, kRandomizeChannel + channel) * 2.0f - 1.0f; };
        const glm::vec3 offset = randomPosition * glm::vec3(r(0), r(1), r(2));
        const glm::vec3 euler = randomRotation * glm::vec3(r(3), r(4), r(5));
        const glm::quat q = glm::length(euler) > 0.0f ? glm::normalize(glm::quat(euler)) : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        const float uniform = 1.0f + randomUniformScale * r(9);
        const glm::vec3 perAxis = glm::vec3(1.0f) + randomScale * glm::vec3(r(6), r(7), r(8));
        const glm::vec3 s = glm::max(perAxis * uniform, glm::vec3(1e-3f));
        pos[i] += offset;
        rot[i] = fromQuat(q * toQuat(rot[i])); // q is unit; no re-normalisation so identity rotations stay exact
        sc[i] *= s;
    }
}

void applyScatter(PointCloud& cloud, const PointOp& op) {
    auto pos = cloud.positions();
    for (std::size_t i = 0; i < cloud.count(); ++i) {
        const glm::vec3 r(opRandom(cloud, op, i, kScatterChannel) * 2.0f - 1.0f,
                          opRandom(cloud, op, i, kScatterChannel + 1) * 2.0f - 1.0f,
                          opRandom(cloud, op, i, kScatterChannel + 2) * 2.0f - 1.0f);
        pos[i] += r * op.range * op.amount;
    }
}

Result<void> applySort(PointCloud& cloud, const PointOp& op) {
    const AttributeBuffer* b = cloud.attributes.find(op.attribute);
    if (b == nullptr) {
        return fail("sort: attribute '{}' not found", op.attribute);
    }
    const auto comp = static_cast<glm::length_t>(std::clamp(op.component, 0, 3));
    const std::size_t n = cloud.count();
    std::vector<float> keys(n);
    for (std::size_t i = 0; i < n; ++i) {
        keys[i] = readAsVec4(*b, i)[comp];
    }
    std::vector<std::uint32_t> order(n);
    std::iota(order.begin(), order.end(), 0u);
    if (op.descending) {
        std::stable_sort(order.begin(), order.end(), [&](std::uint32_t a, std::uint32_t c) { return keys[a] > keys[c]; });
    } else {
        std::stable_sort(order.begin(), order.end(), [&](std::uint32_t a, std::uint32_t c) { return keys[a] < keys[c]; });
    }
    cloud.attributes.keepIndices(order);
    cloud.renumberIndices();
    return {};
}

void applyDuplicate(PointCloud& cloud, const PointOp& op) {
    const int copies = std::max(op.copies, 0);
    const std::size_t n = cloud.count();
    if (copies == 0 || n == 0) {
        return;
    }
    const AttributeSet original = cloud.attributes;
    std::vector<std::uint32_t> all(n);
    std::iota(all.begin(), all.end(), 0u);
    const glm::quat step = glm::angleAxis(op.angle, unitOr(op.axis, glm::vec3(0.0f, 1.0f, 0.0f)));
    const std::int32_t firstId = maxId(cloud) + 1;
    glm::quat rk(1.0f, 0.0f, 0.0f, 0.0f);
    glm::vec3 fk(1.0f);
    glm::vec3 offset(0.0f);
    for (int k = 1; k <= copies; ++k) {
        rk = glm::normalize(step * rk);
        fk *= op.factor;
        offset += op.offset;
        const std::size_t base = cloud.count();
        cloud.attributes.appendRows(original, all);
        auto pos = cloud.positions();
        auto rot = cloud.rotations();
        auto sc = cloud.scales();
        auto idSpan = cloud.ids();
        for (std::size_t j = 0; j < n; ++j) {
            const std::size_t i = base + j;
            pos[i] = op.pivot + rk * ((pos[i] - op.pivot) * fk) + offset;
            rot[i] = fromQuat(glm::normalize(rk * toQuat(rot[i])));
            sc[i] *= fk;
            idSpan[i] = firstId + static_cast<std::int32_t>(static_cast<std::size_t>(k - 1) * n + j);
        }
    }
    cloud.renumberIndices();
}

void applySample(PointCloud& cloud, const PointOp& op) {
    if (op.count > 0) {
        sampleCloud(cloud, op.count);
        return;
    }
    const auto stride = static_cast<std::size_t>(std::max(op.stride, 1));
    const auto start = static_cast<std::size_t>(std::max(op.start, 0));
    filterRows(cloud, PointOp{}, [&](std::size_t i) { return i >= start && (i - start) % stride == 0; });
}

} // namespace

// ================================================================================================
// Names
// ================================================================================================

const char* pointOpKindName(PointOpKind kind) {
    switch (kind) {
    case PointOpKind::Transform:
        return "transform";
    case PointOpKind::Translate:
        return "translate";
    case PointOpKind::Rotate:
        return "rotate";
    case PointOpKind::Scale:
        return "scale";
    case PointOpKind::Noise:
        return "noise";
    case PointOpKind::Randomize:
        return "randomize";
    case PointOpKind::Scatter:
        return "scatter";
    case PointOpKind::FilterDensity:
        return "filterDensity";
    case PointOpKind::FilterAttribute:
        return "filterAttribute";
    case PointOpKind::FilterDistance:
        return "filterDistance";
    case PointOpKind::FilterProbability:
        return "filterProbability";
    case PointOpKind::FilterBounds:
        return "filterBounds";
    case PointOpKind::Delete:
        return "delete";
    case PointOpKind::Sort:
        return "sort";
    case PointOpKind::Merge:
        return "merge";
    case PointOpKind::Duplicate:
        return "duplicate";
    case PointOpKind::Sample:
        return "sample";
    case PointOpKind::Attribute:
        return "attribute";
    }
    return "translate";
}

std::optional<PointOpKind> pointOpKindFromName(std::string_view name) {
    for (const auto kind :
         {PointOpKind::Transform, PointOpKind::Translate, PointOpKind::Rotate, PointOpKind::Scale, PointOpKind::Noise,
          PointOpKind::Randomize, PointOpKind::Scatter, PointOpKind::FilterDensity, PointOpKind::FilterAttribute,
          PointOpKind::FilterDistance, PointOpKind::FilterProbability, PointOpKind::FilterBounds, PointOpKind::Delete,
          PointOpKind::Sort, PointOpKind::Merge, PointOpKind::Duplicate, PointOpKind::Sample, PointOpKind::Attribute}) {
        if (name == pointOpKindName(kind)) {
            return kind;
        }
    }
    return std::nullopt;
}

// ================================================================================================
// Application
// ================================================================================================

Result<void> applyPointOp(PointCloud& cloud, const PointOp& op) {
    if (!op.enabled) {
        return {};
    }
    cloud.ensureCore();
    switch (op.kind) {
    case PointOpKind::Transform:
        applyTransform(cloud, op);
        return {};
    case PointOpKind::Translate:
        applyTranslate(cloud, op);
        return {};
    case PointOpKind::Rotate:
        applyRotate(cloud, op);
        return {};
    case PointOpKind::Scale:
        applyScale(cloud, op);
        return {};
    case PointOpKind::Noise:
        if (op.amount != 0.0f) {
            applyNoise(cloud, op);
        }
        return {};
    case PointOpKind::Randomize:
        if (op.amount != 0.0f) {
            applyRandomize(cloud, op);
        }
        return {};
    case PointOpKind::Scatter:
        if (op.amount != 0.0f) {
            applyScatter(cloud, op);
        }
        return {};
    case PointOpKind::FilterDensity: {
        const auto den = cloud.densities();
        filterRows(cloud, op, [&](std::size_t i) {
            return op.probabilistic ? den[i] >= opRandom(cloud, op, i, kDensityChannel) : den[i] >= op.threshold;
        });
        return {};
    }
    case PointOpKind::FilterAttribute: {
        const AttributeBuffer* b = cloud.attributes.find(op.attribute);
        if (b == nullptr) {
            return fail("filterAttribute: attribute '{}' not found", op.attribute);
        }
        filterRows(cloud, op, [&](std::size_t i) { return compareValues(readAsVec4(*b, i).x, op.value, op.compare) != 0.0f; });
        return {};
    }
    case PointOpKind::FilterDistance: {
        const auto pos = cloud.positions();
        filterRows(cloud, op, [&](std::size_t i) {
            const float dist = glm::length(pos[i] - op.pivot);
            return dist >= op.minDistance && dist <= op.maxDistance;
        });
        return {};
    }
    case PointOpKind::FilterProbability:
        filterRows(cloud, op, [&](std::size_t i) { return opRandom(cloud, op, i, kProbabilityChannel) < op.probability; });
        return {};
    case PointOpKind::FilterBounds: {
        const auto pos = cloud.positions();
        filterRows(cloud, op, [&](std::size_t i) {
            return glm::all(glm::greaterThanEqual(pos[i], op.boundsMin)) && glm::all(glm::lessThanEqual(pos[i], op.boundsMax));
        });
        return {};
    }
    case PointOpKind::Delete: {
        const std::string name = op.attribute.empty() ? "delete" : op.attribute;
        const AttributeBuffer* b = cloud.attributes.find(name);
        if (b == nullptr) {
            return {};
        }
        filterRows(cloud, PointOp{}, [&](std::size_t i) { return readAsVec4(*b, i).x == 0.0f; });
        return {};
    }
    case PointOpKind::Sort:
        return applySort(cloud, op);
    case PointOpKind::Merge:
        return {};
    case PointOpKind::Duplicate:
        applyDuplicate(cloud, op);
        return {};
    case PointOpKind::Sample:
        applySample(cloud, op);
        return {};
    case PointOpKind::Attribute:
        return applyAttributeOp(cloud.attributes, op.attributeOp);
    }
    return fail("unknown point op kind");
}

Result<void> applyPointOps(PointCloud& cloud, std::span<const PointOp> ops) {
    for (std::size_t i = 0; i < ops.size(); ++i) {
        if (auto ok = applyPointOp(cloud, ops[i]); !ok) {
            return fail("op {} ({}): {}", i + 1, pointOpKindName(ops[i].kind), ok.error().message);
        }
    }
    return {};
}

void mergeClouds(PointCloud& a, const PointCloud& b) {
    const std::size_t base = a.count();
    const std::int32_t firstId = maxId(a) + 1;
    a.attributes.append(b.attributes);
    a.ensureCore();
    auto idSpan = a.ids();
    for (std::size_t j = 0; base + j < idSpan.size(); ++j) {
        idSpan[base + j] = firstId + static_cast<std::int32_t>(j);
    }
    a.renumberIndices();
}

void sampleCloud(PointCloud& cloud, int count) {
    const std::size_t n = cloud.count();
    if (count <= 0) {
        cloud.attributes.keepIndices({});
        return;
    }
    const auto want = static_cast<std::size_t>(count);
    if (want >= n) {
        return;
    }
    std::vector<std::uint32_t> keep(want);
    for (std::size_t k = 0; k < want; ++k) {
        keep[k] = want > 1 ? static_cast<std::uint32_t>(k * (n - 1) / (want - 1)) : 0u;
    }
    cloud.attributes.keepIndices(keep);
}

// ================================================================================================
// JSON and hashing
// ================================================================================================

json pointOpToJson(const PointOp& op) {
    const PointOp def;
    json j = json::object();
    j["kind"] = pointOpKindName(op.kind);
    const auto put = [&](const char* key, const auto& value, const auto& defValue) {
        if (value != defValue) {
            j[key] = value;
        }
    };
    const auto putVec = [&](const char* key, const auto& value, const auto& defValue) {
        if (value != defValue) {
            j[key] = detail::vecToJson(value);
        }
    };
    put("enabled", op.enabled, def.enabled);
    put("amount", op.amount, def.amount);
    putVec("offset", op.offset, def.offset);
    putVec("axis", op.axis, def.axis);
    put("angle", op.angle, def.angle);
    putVec("pivot", op.pivot, def.pivot);
    putVec("factor", op.factor, def.factor);
    put("scaleInstances", op.scaleInstances, def.scaleInstances);
    putVec("position", op.position, def.position);
    putVec("rotation", op.rotationDegrees, def.rotationDegrees);
    putVec("scale", op.scale, def.scale);
    put("frequency", op.frequency, def.frequency);
    putVec("axisMask", op.axisMask, def.axisMask);
    put("seed", op.seed, def.seed);
    putVec("randomPosition", op.randomPosition, def.randomPosition);
    putVec("randomRotation", op.randomRotation, def.randomRotation);
    putVec("randomScale", op.randomScale, def.randomScale);
    put("randomUniformScale", op.randomUniformScale, def.randomUniformScale);
    putVec("range", op.range, def.range);
    put("threshold", op.threshold, def.threshold);
    put("probabilistic", op.probabilistic, def.probabilistic);
    put("attribute", op.attribute, def.attribute);
    put("value", op.value, def.value);
    put("compare", op.compare, def.compare);
    put("minDistance", op.minDistance, def.minDistance);
    put("maxDistance", op.maxDistance, def.maxDistance);
    put("probability", op.probability, def.probability);
    putVec("boundsMin", op.boundsMin, def.boundsMin);
    putVec("boundsMax", op.boundsMax, def.boundsMax);
    put("invert", op.invert, def.invert);
    put("component", op.component, def.component);
    put("descending", op.descending, def.descending);
    put("copies", op.copies, def.copies);
    put("stride", op.stride, def.stride);
    put("count", op.count, def.count);
    put("start", op.start, def.start);
    if (op.kind == PointOpKind::Attribute) {
        j["attributeOp"] = attributeOpToJson(op.attributeOp);
    }
    return j;
}

Result<PointOp> pointOpFromJson(const json& j) {
    if (!j.is_object()) {
        return fail("point op must be a JSON object");
    }
    if (!j.contains("kind")) {
        return fail("point op needs a 'kind'");
    }
    PointOp op;
    AVGEN_SPATIAL_READ_ENUM(op.kind, "kind", &pointOpKindFromName, "point op kind");
    AVGEN_SPATIAL_READ(op.enabled, "enabled", detail::readBool);
    AVGEN_SPATIAL_READ(op.amount, "amount", detail::readFloat);
    AVGEN_SPATIAL_READ(op.offset, "offset", detail::readVec3);
    AVGEN_SPATIAL_READ(op.axis, "axis", detail::readVec3);
    AVGEN_SPATIAL_READ(op.angle, "angle", detail::readFloat);
    AVGEN_SPATIAL_READ(op.pivot, "pivot", detail::readVec3);
    AVGEN_SPATIAL_READ(op.factor, "factor", detail::readVec3);
    AVGEN_SPATIAL_READ(op.scaleInstances, "scaleInstances", detail::readBool);
    AVGEN_SPATIAL_READ(op.position, "position", detail::readVec3);
    AVGEN_SPATIAL_READ(op.rotationDegrees, "rotation", detail::readVec3);
    AVGEN_SPATIAL_READ(op.scale, "scale", detail::readVec3);
    AVGEN_SPATIAL_READ(op.frequency, "frequency", detail::readFloat);
    AVGEN_SPATIAL_READ(op.axisMask, "axisMask", detail::readVec3);
    AVGEN_SPATIAL_READ(op.seed, "seed", detail::readU32);
    AVGEN_SPATIAL_READ(op.randomPosition, "randomPosition", detail::readVec3);
    AVGEN_SPATIAL_READ(op.randomRotation, "randomRotation", detail::readVec3);
    AVGEN_SPATIAL_READ(op.randomScale, "randomScale", detail::readVec3);
    AVGEN_SPATIAL_READ(op.randomUniformScale, "randomUniformScale", detail::readFloat);
    AVGEN_SPATIAL_READ(op.range, "range", detail::readVec3);
    AVGEN_SPATIAL_READ(op.threshold, "threshold", detail::readFloat);
    AVGEN_SPATIAL_READ(op.probabilistic, "probabilistic", detail::readBool);
    AVGEN_SPATIAL_READ(op.attribute, "attribute", detail::readString);
    AVGEN_SPATIAL_READ(op.value, "value", detail::readFloat);
    AVGEN_SPATIAL_READ(op.compare, "compare", detail::readInt);
    AVGEN_SPATIAL_READ(op.minDistance, "minDistance", detail::readFloat);
    AVGEN_SPATIAL_READ(op.maxDistance, "maxDistance", detail::readFloat);
    AVGEN_SPATIAL_READ(op.probability, "probability", detail::readFloat);
    AVGEN_SPATIAL_READ(op.boundsMin, "boundsMin", detail::readVec3);
    AVGEN_SPATIAL_READ(op.boundsMax, "boundsMax", detail::readVec3);
    AVGEN_SPATIAL_READ(op.invert, "invert", detail::readBool);
    AVGEN_SPATIAL_READ(op.component, "component", detail::readInt);
    AVGEN_SPATIAL_READ(op.descending, "descending", detail::readBool);
    AVGEN_SPATIAL_READ(op.copies, "copies", detail::readInt);
    AVGEN_SPATIAL_READ(op.stride, "stride", detail::readInt);
    AVGEN_SPATIAL_READ(op.count, "count", detail::readInt);
    AVGEN_SPATIAL_READ(op.start, "start", detail::readInt);
    if (j.contains("attributeOp")) {
        auto inner = attributeOpFromJson(j.at("attributeOp"));
        if (!inner) {
            return std::unexpected(inner.error());
        }
        op.attributeOp = *inner;
    }
    return op;
}

std::uint64_t pointOpHash(const PointOp& op) {
    detail::Fnv h;
    h.u8(static_cast<std::uint8_t>(op.kind));
    h.boolean(op.enabled);
    h.f32(op.amount);
    h.v3(op.offset);
    h.v3(op.axis);
    h.f32(op.angle);
    h.v3(op.pivot);
    h.v3(op.factor);
    h.boolean(op.scaleInstances);
    h.v3(op.position);
    h.v3(op.rotationDegrees);
    h.v3(op.scale);
    h.f32(op.frequency);
    h.v3(op.axisMask);
    h.u32(op.seed);
    h.v3(op.randomPosition);
    h.v3(op.randomRotation);
    h.v3(op.randomScale);
    h.f32(op.randomUniformScale);
    h.v3(op.range);
    h.f32(op.threshold);
    h.boolean(op.probabilistic);
    h.str(op.attribute);
    h.f32(op.value);
    h.i32(op.compare);
    h.f32(op.minDistance);
    h.f32(op.maxDistance);
    h.f32(op.probability);
    h.v3(op.boundsMin);
    h.v3(op.boundsMax);
    h.boolean(op.invert);
    h.i32(op.component);
    h.boolean(op.descending);
    h.i32(op.copies);
    h.i32(op.stride);
    h.i32(op.count);
    h.i32(op.start);
    const AttributeOp& a = op.attributeOp;
    h.u8(static_cast<std::uint8_t>(a.kind));
    h.boolean(a.enabled);
    h.str(a.target);
    h.str(a.source);
    h.str(a.secondSource);
    h.str(a.positionAttribute);
    h.v4(a.value);
    h.f32(a.amount);
    h.f32(a.inMin);
    h.f32(a.inMax);
    h.f32(a.outMin);
    h.f32(a.outMax);
    h.boolean(a.clamp);
    h.f32(a.scale);
    h.v3(a.offset);
    h.v4(a.range);
    h.u32(a.seed);
    h.i32(a.radius);
    h.i32(a.compare);
    h.str(a.idAttribute);
    return h.value();
}

} // namespace avgen::spatial
