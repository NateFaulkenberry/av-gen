#include "entity/field.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace avgen::entity {
namespace {

constexpr float kEps = 1e-6f;
// The steepness of the InverseSquare falloff, chosen so the curve has visibly more of its travel
// near the source than Smooth does without becoming a spike. Renormalised below so the curve still
// reaches exactly 1 at the inner edge and exactly 0 at the surface.
constexpr float kInverseSquareK = 8.0f;

glm::mat3 volumeBasis(const glm::vec3& rotationDegrees) {
    // The same Rz*Ry*Rx convention the composition uses for a node's Euler rotation, so a volume
    // and a node written with the same angles point the same way.
    return glm::mat3_cast(glm::quat(glm::radians(rotationDegrees)));
}

float distanceToSegment(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b) {
    const glm::vec3 ab = b - a;
    const float len2 = glm::dot(ab, ab);
    if (len2 < kEps) {
        return glm::length(p - a);
    }
    const float t = std::clamp(glm::dot(p - a, ab) / len2, 0.0f, 1.0f);
    return glm::length(p - (a + ab * t));
}

} // namespace

const char* volumeShapeName(VolumeShape shape) {
    switch (shape) {
    case VolumeShape::Sphere: return "sphere";
    case VolumeShape::Box: return "box";
    case VolumeShape::Capsule: return "capsule";
    }
    return "sphere";
}

bool volumeShapeFromName(std::string_view name, VolumeShape& out) {
    if (name == "sphere") { out = VolumeShape::Sphere; return true; }
    if (name == "box") { out = VolumeShape::Box; return true; }
    if (name == "capsule") { out = VolumeShape::Capsule; return true; }
    return false;
}

const char* falloffName(Falloff falloff) {
    switch (falloff) {
    case Falloff::Constant: return "constant";
    case Falloff::Linear: return "linear";
    case Falloff::Smooth: return "smooth";
    case Falloff::InverseSquare: return "inverseSquare";
    }
    return "smooth";
}

bool falloffFromName(std::string_view name, Falloff& out) {
    if (name == "constant") { out = Falloff::Constant; return true; }
    if (name == "linear") { out = Falloff::Linear; return true; }
    if (name == "smooth") { out = Falloff::Smooth; return true; }
    if (name == "inverseSquare" || name == "inversesquare") { out = Falloff::InverseSquare; return true; }
    return false;
}

const char* fieldAuthorityName(FieldAuthority authority) {
    switch (authority) {
    case FieldAuthority::Static: return "static";
    case FieldAuthority::Baked: return "baked";
    case FieldAuthority::Live: return "live";
    }
    return "static";
}

float applyFalloff(Falloff falloff, float x) {
    if (x <= 0.0f) {
        return 0.0f;
    }
    const float t = std::min(x, 1.0f);
    switch (falloff) {
    case Falloff::Constant:
        return 1.0f;
    case Falloff::Linear:
        return t;
    case Falloff::Smooth:
        return t * t * (3.0f - 2.0f * t);
    case Falloff::InverseSquare: {
        // 1/(1+k r^2) with r = 1 - t (0 at the inner edge, 1 at the surface), shifted and scaled
        // so the curve is exactly 1 at t = 1 and exactly 0 at t = 0.
        const float r = 1.0f - t;
        const float raw = 1.0f / (1.0f + kInverseSquareK * r * r);
        const float floorValue = 1.0f / (1.0f + kInverseSquareK);
        return (raw - floorValue) / (1.0f - floorValue);
    }
    }
    return t;
}

float TriggerVolume::reach() const {
    switch (shape) {
    case VolumeShape::Sphere:
        return std::max(radius, 0.0f);
    case VolumeShape::Box:
        return glm::length(glm::max(halfExtents, glm::vec3(0.0f)));
    case VolumeShape::Capsule:
        return std::max(radius, 0.0f) + std::max(height, 0.0f) * 0.5f;
    }
    return std::max(radius, 0.0f);
}

void TriggerVolume::bounds(glm::vec3& lo, glm::vec3& hi) const {
    if (shape == VolumeShape::Box) {
        // The box may be rotated, so the axis-aligned bound is the rotated half-extent projected
        // onto each world axis -- abs(R) * halfExtents, which is tighter than the corner radius.
        const glm::mat3 r = volumeBasis(rotation);
        glm::vec3 extent(0.0f);
        const glm::vec3 h = glm::max(halfExtents, glm::vec3(0.0f));
        for (int axis = 0; axis < 3; ++axis) {
            extent[axis] = std::abs(r[0][axis]) * h.x + std::abs(r[1][axis]) * h.y +
                           std::abs(r[2][axis]) * h.z;
        }
        lo = center - extent;
        hi = center + extent;
        return;
    }
    const float r = reach();
    lo = center - glm::vec3(r);
    hi = center + glm::vec3(r);
}

float TriggerVolume::influenceAt(glm::vec3 point) const {
    // `d` is the normalised distance out: 0 at the centre, 1 at the surface, more outside. Every
    // shape reduces to it, and every shape then shares one falloff.
    float d = 2.0f;
    switch (shape) {
    case VolumeShape::Sphere: {
        if (radius <= kEps) {
            return 0.0f;
        }
        d = glm::length(point - center) / radius;
        break;
    }
    case VolumeShape::Box: {
        const glm::vec3 h = glm::max(halfExtents, glm::vec3(kEps));
        const glm::vec3 local = glm::transpose(volumeBasis(rotation)) * (point - center);
        const glm::vec3 q = glm::abs(local) / h;
        d = std::max(q.x, std::max(q.y, q.z)); // nested boxes, not nested spheres
        break;
    }
    case VolumeShape::Capsule: {
        if (radius <= kEps) {
            return 0.0f;
        }
        const glm::vec3 axis = volumeBasis(rotation) * glm::vec3(0.0f, 1.0f, 0.0f);
        const float half = std::max(height, 0.0f) * 0.5f;
        d = distanceToSegment(point, center - axis * half, center + axis * half) / radius;
        break;
    }
    }
    if (!(d < 1.0f)) {
        return 0.0f; // the surface belongs to the outside, and so does a NaN
    }
    const float in = std::clamp(inner, 0.0f, 0.999f);
    if (d <= in) {
        return 1.0f;
    }
    return applyFalloff(falloff, (1.0f - d) / (1.0f - in));
}

// ---- the grid --------------------------------------------------------------------------------

void EntityGrid::clear() {
    points_ = 0;
    starts_.clear();
    indices_.clear();
    counts_.clear();
    cellOfPoint_.clear();
    cursor_.clear();
    dims_ = glm::ivec3(1);
}

glm::ivec3 EntityGrid::cellOf(const glm::vec3& p) const {
    const glm::vec3 local = (p - origin_) * invCell_;
    glm::ivec3 c;
    for (int axis = 0; axis < 3; ++axis) {
        const float v = std::floor(local[axis]);
        // Clamp rather than reject: a query box that overhangs the point cloud still has to visit
        // the edge cells, and a point can only be outside the bounds by a rounding error.
        c[axis] = static_cast<int>(std::clamp(v, 0.0f, static_cast<float>(dims_[axis] - 1)));
    }
    return c;
}

void EntityGrid::build(std::span<const glm::vec3> points, float cellSize, std::size_t maxCells) {
    clear();
    if (points.empty()) {
        return;
    }
    points_ = points.size();
    glm::vec3 lo(std::numeric_limits<float>::max());
    glm::vec3 hi(std::numeric_limits<float>::lowest());
    for (const glm::vec3& p : points) {
        lo = glm::min(lo, p);
        hi = glm::max(hi, p);
    }
    cellSize_ = std::max(cellSize, 1e-3f);
    const glm::vec3 span = glm::max(hi - lo, glm::vec3(0.0f));
    // Inflate the cell until the dense grid fits the budget. One scene's crowd is 40 m across and
    // another's is 4 km; a fixed cell count would be wrong for one of them, and a fixed cell size
    // would allocate a hundred million cells for the other.
    for (int guard = 0; guard < 64; ++guard) {
        const glm::ivec3 d(std::max(1, static_cast<int>(std::floor(span.x / cellSize_)) + 1),
                           std::max(1, static_cast<int>(std::floor(span.y / cellSize_)) + 1),
                           std::max(1, static_cast<int>(std::floor(span.z / cellSize_)) + 1));
        const double total = static_cast<double>(d.x) * static_cast<double>(d.y) * static_cast<double>(d.z);
        if (total <= static_cast<double>(maxCells)) {
            dims_ = d;
            break;
        }
        cellSize_ *= 2.0f;
        dims_ = glm::ivec3(1);
    }
    invCell_ = 1.0f / cellSize_;
    origin_ = lo;

    const auto cells = static_cast<std::size_t>(dims_.x) * static_cast<std::size_t>(dims_.y) *
                       static_cast<std::size_t>(dims_.z);
    starts_.assign(cells + 1, 0);
    indices_.resize(points_);
    // Counting sort: one pass to count, a prefix sum, one pass to place. No per-cell container,
    // and no allocation at all once the two arrays have reached their size.
    counts_.assign(cells, 0);
    cellOfPoint_.resize(points_);
    for (std::size_t i = 0; i < points_; ++i) {
        const glm::ivec3 c = cellOf(points[i]);
        const auto index = static_cast<std::size_t>((c.z * dims_.y + c.y) * dims_.x + c.x);
        cellOfPoint_[i] = index;
        ++counts_[index];
    }
    std::size_t running = 0;
    for (std::size_t c = 0; c < cells; ++c) {
        starts_[c] = running;
        running += counts_[c];
    }
    starts_[cells] = running;
    cursor_.assign(starts_.begin(), starts_.end() - 1);
    for (std::size_t i = 0; i < points_; ++i) {
        indices_[cursor_[cellOfPoint_[i]]++] = static_cast<std::uint32_t>(i);
    }
}

// ---- seeded variation ------------------------------------------------------------------------

Rng arcStream(std::uint32_t entitySeed, std::string_view fieldName, std::uint32_t count) {
    std::uint64_t h = 1469598103934665603ull ^ entitySeed;
    for (const char c : fieldName) {
        h ^= static_cast<std::uint8_t>(c);
        h *= 1099511628211ull;
    }
    h ^= static_cast<std::uint64_t>(count) * 0x9E3779B97F4A7C15ull;
    h *= 1099511628211ull;
    return Rng(h, (static_cast<std::uint64_t>(entitySeed) << 1u) | 1u);
}

// ---- serialisation ---------------------------------------------------------------------------

namespace {

glm::vec3 readVec3(const nlohmann::json& j, const char* key, glm::vec3 fallback) {
    if (!j.contains(key) || !j[key].is_array() || j[key].size() < 3) {
        return fallback;
    }
    glm::vec3 out = fallback;
    for (int i = 0; i < 3; ++i) {
        if (j[key][static_cast<std::size_t>(i)].is_number()) {
            out[i] = j[key][static_cast<std::size_t>(i)].get<float>();
        }
    }
    return out;
}

float readFloat(const nlohmann::json& j, const char* key, float fallback) {
    return j.contains(key) && j[key].is_number() ? j[key].get<float>() : fallback;
}

bool readBool(const nlohmann::json& j, const char* key, bool fallback) {
    return j.contains(key) && j[key].is_boolean() ? j[key].get<bool>() : fallback;
}

nlohmann::json vec3Json(glm::vec3 v) { return nlohmann::json{v.x, v.y, v.z}; }

} // namespace

Result<FieldDesc> fieldFromJson(const nlohmann::json& j) {
    if (!j.is_object()) {
        return fail("a field must be an object");
    }
    if (!j.contains("name") || !j["name"].is_string() || j["name"].get<std::string>().empty()) {
        return fail("field: 'name' is required and must be a non-empty string");
    }
    FieldDesc f;
    f.name = j["name"].get<std::string>();
    if (j.contains("shape") && j["shape"].is_string()) {
        if (!volumeShapeFromName(j["shape"].get<std::string>(), f.volume.shape)) {
            return fail("field '{}': unknown shape '{}' (sphere, box, capsule)", f.name,
                        j["shape"].get<std::string>());
        }
    }
    if (j.contains("falloff") && j["falloff"].is_string()) {
        if (!falloffFromName(j["falloff"].get<std::string>(), f.volume.falloff)) {
            return fail("field '{}': unknown falloff '{}' (constant, linear, smooth, inverseSquare)",
                        f.name, j["falloff"].get<std::string>());
        }
    }
    f.volume.center = readVec3(j, "center", f.volume.center);
    f.volume.radius = readFloat(j, "radius", f.volume.radius);
    f.volume.height = readFloat(j, "height", f.volume.height);
    f.volume.halfExtents = readVec3(j, "halfExtents", f.volume.halfExtents);
    f.volume.rotation = readVec3(j, "rotation", f.volume.rotation);
    f.volume.inner = std::clamp(readFloat(j, "inner", f.volume.inner), 0.0f, 0.999f);
    if (j.contains("source") && j["source"].is_string()) {
        f.source = j["source"].get<std::string>();
    }
    f.strength = readFloat(j, "strength", f.strength);
    f.floorGain = readFloat(j, "floorGain", f.floorGain);
    f.scaleReactions = readBool(j, "scaleReactions", f.scaleReactions);
    f.requireScrubExact = readBool(j, "requireScrubExact", f.requireScrubExact);
    f.enabled = readBool(j, "enabled", f.enabled);
    if (j.contains("tags")) {
        if (!j["tags"].is_array()) {
            return fail("field '{}': 'tags' must be an array of strings", f.name);
        }
        for (const auto& tag : j["tags"]) {
            if (!tag.is_string()) {
                return fail("field '{}': 'tags' must be an array of strings", f.name);
            }
            f.tags.push_back(tag.get<std::string>());
        }
    }
    if (j.contains("arc")) {
        const nlohmann::json& a = j["arc"];
        if (!a.is_object()) {
            return fail("field '{}': 'arc' must be an object", f.name);
        }
        f.arc.enabled = readBool(a, "enabled", true); // an arc block that exists is an arc that runs
        f.arc.delaySeconds = std::max(0.0f, readFloat(a, "delaySeconds", f.arc.delaySeconds));
        f.arc.delayJitter = std::max(0.0f, readFloat(a, "delayJitter", f.arc.delayJitter));
        f.arc.holdSeconds = std::max(0.0f, readFloat(a, "holdSeconds", f.arc.holdSeconds));
        f.arc.holdJitter = std::max(0.0f, readFloat(a, "holdJitter", f.arc.holdJitter));
        f.arc.releaseSeconds = std::max(0.0f, readFloat(a, "releaseSeconds", f.arc.releaseSeconds));
        f.arc.intensity = readFloat(a, "intensity", f.arc.intensity);
        f.arc.intensityJitter = std::max(0.0f, readFloat(a, "intensityJitter", f.arc.intensityJitter));
        f.arc.refractorySeconds = std::max(0.0f, readFloat(a, "refractorySeconds", f.arc.refractorySeconds));
        f.arc.holdStill = readBool(a, "holdStill", f.arc.holdStill);
        if (a.contains("activity") && a["activity"].is_string()) {
            f.arc.activity = a["activity"].get<std::string>();
        }
    }
    return f;
}

Result<std::vector<FieldDesc>> fieldsFromJson(const nlohmann::json& j) {
    if (!j.is_array()) {
        return fail("'fields' must be an array");
    }
    std::vector<FieldDesc> out;
    out.reserve(j.size());
    for (const auto& item : j) {
        auto field = fieldFromJson(item);
        if (!field) {
            return fail("{}", field.error().message);
        }
        const std::string& name = field->name;
        if (std::any_of(out.begin(), out.end(), [&](const FieldDesc& d) { return d.name == name; })) {
            return fail("field '{}' is declared twice", name);
        }
        out.push_back(std::move(*field));
    }
    return out;
}

nlohmann::json fieldToJson(const FieldDesc& field) {
    nlohmann::json j = nlohmann::json::object();
    j["name"] = field.name;
    j["shape"] = volumeShapeName(field.volume.shape);
    j["center"] = vec3Json(field.volume.center);
    switch (field.volume.shape) {
    case VolumeShape::Sphere:
        j["radius"] = field.volume.radius;
        break;
    case VolumeShape::Box:
        j["halfExtents"] = vec3Json(field.volume.halfExtents);
        j["rotation"] = vec3Json(field.volume.rotation);
        break;
    case VolumeShape::Capsule:
        j["radius"] = field.volume.radius;
        j["height"] = field.volume.height;
        j["rotation"] = vec3Json(field.volume.rotation);
        break;
    }
    j["falloff"] = falloffName(field.volume.falloff);
    if (field.volume.inner > 0.0f) {
        j["inner"] = field.volume.inner;
    }
    if (!field.source.empty()) {
        j["source"] = field.source;
    }
    j["strength"] = field.strength;
    if (field.floorGain != 0.0f) {
        j["floorGain"] = field.floorGain;
    }
    if (!field.tags.empty()) {
        j["tags"] = field.tags;
    }
    if (!field.scaleReactions) {
        j["scaleReactions"] = false;
    }
    if (field.requireScrubExact) {
        j["requireScrubExact"] = true;
    }
    if (!field.enabled) {
        j["enabled"] = false;
    }
    if (field.arc.enabled) {
        nlohmann::json a = nlohmann::json::object();
        a["enabled"] = true;
        a["delaySeconds"] = field.arc.delaySeconds;
        a["delayJitter"] = field.arc.delayJitter;
        a["holdSeconds"] = field.arc.holdSeconds;
        a["holdJitter"] = field.arc.holdJitter;
        a["releaseSeconds"] = field.arc.releaseSeconds;
        a["intensity"] = field.arc.intensity;
        a["intensityJitter"] = field.arc.intensityJitter;
        if (field.arc.refractorySeconds > 0.0f) {
            a["refractorySeconds"] = field.arc.refractorySeconds;
        }
        if (field.arc.holdStill) {
            a["holdStill"] = true;
        }
        a["activity"] = field.arc.activity;
        j["arc"] = std::move(a);
    }
    return j;
}

nlohmann::json fieldsToJson(const std::vector<FieldDesc>& fields) {
    nlohmann::json j = nlohmann::json::array();
    for (const FieldDesc& field : fields) {
        j.push_back(fieldToJson(field));
    }
    return j;
}

} // namespace avgen::entity
