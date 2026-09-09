// SDF scene objects (ADR-027): validation, structural hashing, rebuild (surface-nets meshing in
// Mesh mode, cached by the structural hash), JSON, and the rest/live parameter pattern.
//
// Conventions chosen here (the header fixes the rest):
//
// * structuralHash covers the tree (every node member, spatial::SdfTree::structuralHash), the
//   bounds, the resolution and the render mode. The transform, the material and the march
//   settings (maxSteps, epsilon, stepScale, normalEpsilon) are per-frame uniforms and not part of
//   it. rebuild() keys on it: a changed hash bumps structureVersion and, in Mesh mode, re-meshes
//   (the mesh is evaluated at the `time` given, so an animated displacement is frozen at the
//   rebuild time; Raymarch mode evaluates the tree every frame on the GPU).
// * Parameters register only the members a node kind uses (a sphere has radius, a rotate has
//   rotation, ...) plus `enabled` on every node; the handle vectors (nodeAmount, nodeRadius,
//   nodeSmooth) have one entry per node in pre-order and are null where the kind has no such
//   member. Node indices are 1-based over enabled AND disabled nodes, in pre-order, so the path
//   of a node does not move when a sibling is disabled.
// * applySdfParameters copies finals into `live` (live = rest, then overwrite) and returns whether
//   live's structural hash differs from what it was before the call. Every tree parameter is
//   part of the structural hash, so the result is "true" whenever a node parameter moved. The
//   caller passes that to rebuild(): in Raymarch mode rebuild only bumps structureVersion (the
//   renderer repacks the node buffer every frame anyway), in Mesh mode it re-meshes, which is
//   the intended cost of modulating a meshed object.

#include "scene/sdf_object.hpp"

#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace avgen::scene {

using nlohmann::json;
using spatial::SdfNode;
using spatial::SdfNodeKind;

namespace {

constexpr std::array<const char*, 2> kRenderModeNames = {"raymarch", "mesh"};

// ---- hashing (FNV-1a over bit patterns, like the tree hash) ------------------------------------

class StructHash {
public:
    void u32(std::uint32_t v) {
        for (int i = 0; i < 4; ++i) {
            h_ = (h_ ^ ((v >> (8 * i)) & 0xFFu)) * 0x100000001b3ULL;
        }
    }
    void u64(std::uint64_t v) {
        u32(static_cast<std::uint32_t>(v));
        u32(static_cast<std::uint32_t>(v >> 32));
    }
    void i32(int v) { u32(std::bit_cast<std::uint32_t>(v)); }
    void f32(float v) { u32(std::bit_cast<std::uint32_t>(v == 0.0f ? 0.0f : v)); } // -0 == +0
    void v3(const glm::vec3& v) {
        f32(v.x);
        f32(v.y);
        f32(v.z);
    }
    [[nodiscard]] std::uint64_t value() const { return h_; }

private:
    std::uint64_t h_ = 0xcbf29ce484222325ULL;
};

// ---- Euler (degrees) <-> quaternion, the composition node convention --------------------------

glm::quat quatFromEulerDegrees(const glm::vec3& degrees) {
    return glm::quat(glm::radians(degrees));
}

glm::vec3 eulerDegrees(const glm::quat& q) {
    const glm::mat3 m = glm::mat3_cast(q); // m[column][row]
    const float m00 = m[0][0];
    const float m10 = m[0][1];
    const float m20 = m[0][2];
    const float m01 = m[1][0];
    const float m11 = m[1][1];
    const float m21 = m[1][2];
    const float m22 = m[2][2];
    const float cy = std::sqrt(m00 * m00 + m10 * m10);
    const float y = std::atan2(-m20, cy);
    float x = 0.0f;
    float z = 0.0f;
    if (cy > 1e-6f) {
        x = std::atan2(m21, m22);
        z = std::atan2(m10, m00);
    } else {
        x = std::atan2(-m20 * m01, m11);
    }
    return glm::degrees(glm::vec3(x, y, z));
}

// ---- JSON helpers (the procedural.cpp style) ---------------------------------------------------

json vecToJson(const glm::vec3& v) {
    return json::array({v.x, v.y, v.z});
}

Result<glm::vec3> readVec3(const json& j, const char* key, const glm::vec3& def) {
    if (!j.contains(key)) {
        return def;
    }
    const json& a = j.at(key);
    if (!a.is_array() || a.size() != 3) {
        return fail("'{}' must be an array of 3 numbers", key);
    }
    glm::vec3 out{};
    for (std::size_t i = 0; i < 3; ++i) {
        const json& e = a.at(i);
        if (!e.is_number()) {
            return fail("'{}' must be an array of 3 numbers", key);
        }
        out[static_cast<glm::length_t>(i)] = e.get<float>();
    }
    return out;
}

Result<float> readFloat(const json& j, const char* key, float def) {
    if (!j.contains(key)) {
        return def;
    }
    const json& v = j.at(key);
    if (!v.is_number()) {
        return fail("'{}' must be a number", key);
    }
    return v.get<float>();
}

Result<int> readInt(const json& j, const char* key, int def) {
    if (!j.contains(key)) {
        return def;
    }
    const json& v = j.at(key);
    if (!v.is_number_integer()) {
        return fail("'{}' must be an integer", key);
    }
    return v.get<int>();
}

Result<bool> readBool(const json& j, const char* key, bool def) {
    if (!j.contains(key)) {
        return def;
    }
    const json& v = j.at(key);
    if (!v.is_boolean()) {
        return fail("'{}' must be a boolean", key);
    }
    return v.get<bool>();
}

Result<std::string> readString(const json& j, const char* key, const std::string& def) {
    if (!j.contains(key)) {
        return def;
    }
    const json& v = j.at(key);
    if (!v.is_string()) {
        return fail("'{}' must be a string", key);
    }
    return v.get<std::string>();
}

#define AVGEN_SDF_READ(target, key, reader)                                                             \
    do {                                                                                                \
        auto value_ = reader(j, key, target);                                                           \
        if (!value_) {                                                                                  \
            return std::unexpected(value_.error());                                                     \
        }                                                                                               \
        target = *value_;                                                                               \
    } while (false)

// ---- per-kind node members exposed as parameters ------------------------------------------------

enum class NodeField : std::uint8_t {
    Radius, Height, Size, Rounding, Offset, Translation, Rotation, Scale, Amount, Smooth, Frequency, Speed, Enabled
};

constexpr std::array<const char*, 13> kNodeFieldNames = {
    "radius", "height", "size", "rounding", "offset", "translation", "rotation",
    "scale",  "amount", "smooth", "frequency", "speed", "enabled",
};

// The members a node kind actually uses (plus Enabled on every node).
std::vector<NodeField> nodeFields(SdfNodeKind kind) {
    using F = NodeField;
    std::vector<NodeField> out;
    switch (kind) {
    case SdfNodeKind::Sphere:
        out = {F::Radius};
        break;
    case SdfNodeKind::Box:
        out = {F::Size};
        break;
    case SdfNodeKind::RoundedBox:
        out = {F::Size, F::Rounding};
        break;
    case SdfNodeKind::Cylinder:
    case SdfNodeKind::Capsule:
    case SdfNodeKind::Cone:
        out = {F::Radius, F::Height};
        break;
    case SdfNodeKind::Torus:
        out = {F::Radius, F::Rounding};
        break;
    case SdfNodeKind::Plane:
        out = {F::Offset};
        break;
    case SdfNodeKind::Union:
    case SdfNodeKind::Intersection:
    case SdfNodeKind::Difference:
        break;
    case SdfNodeKind::SmoothUnion:
    case SdfNodeKind::SmoothIntersection:
    case SdfNodeKind::SmoothDifference:
        out = {F::Smooth};
        break;
    case SdfNodeKind::Translate:
        out = {F::Translation};
        break;
    case SdfNodeKind::Rotate:
        out = {F::Rotation};
        break;
    case SdfNodeKind::Scale:
        out = {F::Scale};
        break;
    case SdfNodeKind::Twist:
    case SdfNodeKind::Bend:
        out = {F::Amount};
        break;
    case SdfNodeKind::Repeat:
    case SdfNodeKind::Mirror:
        out = {F::Size};
        break;
    case SdfNodeKind::PolarRepeat:
        break;
    case SdfNodeKind::DisplaceNoise:
    case SdfNodeKind::DisplaceWave:
        out = {F::Amount, F::Frequency, F::Speed};
        break;
    case SdfNodeKind::DisplaceVoronoi:
        out = {F::Amount, F::Frequency};
        break;
    case SdfNodeKind::DisplaceField:
        out = {F::Amount};
        break;
    }
    out.push_back(F::Enabled);
    return out;
}

// Pre-order visit of a tree with 1-based indices.
template <typename Fn>
void visitPreOrder(SdfNode& node, int& index, Fn&& fn) {
    fn(node, ++index);
    for (SdfNode& child : node.children) {
        visitPreOrder(child, index, fn);
    }
}
template <typename Fn>
void visitPreOrder(const SdfNode& node, int& index, Fn&& fn) {
    fn(node, ++index);
    for (const SdfNode& child : node.children) {
        visitPreOrder(child, index, fn);
    }
}

std::string nodePath(int index, NodeField field) {
    return "node/" + std::to_string(index) + "/" + kNodeFieldNames[static_cast<std::size_t>(field)];
}

struct Registrar {
    params::ParameterSet& params;
    SdfParameters& out;
    std::string group;

    template <typename T>
    params::Parameter<T>* add(params::ParamDesc<T> d, const std::string& rel, const std::string& label) {
        d.path = out.prefix + rel;
        d.label = label;
        d.group = group;
        auto& p = params.add(std::move(d));
        out.all.push_back(&p);
        return &p;
    }
    params::Parameter<float>* f(const std::string& rel, const std::string& label, float def, float lo, float hi, float slo,
                                float shi) {
        params::ParamDesc<float> d;
        d.defaultValue = def;
        d.hardMin = lo;
        d.hardMax = hi;
        d.softMin = slo;
        d.softMax = shi;
        return add(std::move(d), rel, label);
    }
    params::Parameter<int>* i(const std::string& rel, int def, int lo, int hi, int slo, int shi) {
        params::ParamDesc<int> d;
        d.defaultValue = def;
        d.hardMin = lo;
        d.hardMax = hi;
        d.softMin = slo;
        d.softMax = shi;
        return add(std::move(d), rel, rel);
    }
    params::Parameter<bool>* b(const std::string& rel, const std::string& label, bool def) {
        params::ParamDesc<bool> d;
        d.defaultValue = def;
        d.hardMin = false;
        d.hardMax = true;
        return add(std::move(d), rel, label);
    }
    params::Parameter<glm::vec3>* v3(const std::string& rel, const std::string& label, glm::vec3 def, float lo, float hi,
                                     float slo, float shi, bool isColor = false) {
        params::ParamDesc<glm::vec3> d;
        d.defaultValue = def;
        d.hardMin = glm::vec3(lo);
        d.hardMax = glm::vec3(hi);
        d.softMin = glm::vec3(slo);
        d.softMax = glm::vec3(shi);
        d.isColor = isColor;
        return add(std::move(d), rel, label);
    }
};

// Path -> parameter over the relative part (no allocation per lookup: keys view the parameter
// paths, which outlive the map).
class ParamIndex {
public:
    explicit ParamIndex(const SdfParameters& p) : prefixSize_(p.prefix.size()) {
        map_.reserve(p.all.size());
        for (params::IParameter* ip : p.all) {
            if (ip != nullptr && ip->path().size() >= prefixSize_) {
                map_.emplace(std::string_view(ip->path()).substr(prefixSize_), ip);
            }
        }
    }
    template <typename T>
    [[nodiscard]] params::Parameter<T>* find(std::string_view rel) const {
        const auto it = map_.find(rel);
        return it == map_.end() ? nullptr : dynamic_cast<params::Parameter<T>*>(it->second);
    }
    template <typename T>
    void copy(std::string_view rel, T& target) const {
        if (auto* param = find<T>(rel)) {
            target = param->value();
        }
    }

private:
    std::size_t prefixSize_;
    std::unordered_map<std::string_view, params::IParameter*> map_;
};

// "node/<i>/<field>" into a stack buffer.
std::string_view nodeKey(char* buf, std::size_t size, int index, NodeField field) {
    const int n = std::snprintf(buf, size, "node/%d/%s", index, kNodeFieldNames[static_cast<std::size_t>(field)]);
    return std::string_view(buf, n > 0 ? static_cast<std::size_t>(n) : 0);
}

} // namespace

// The link anchor declared in the header: only this translation unit defines it, so every
// including TU drags sdf_object.cpp.o into the link ahead of pending_wave2.cpp's weak stubs.
namespace detail {
} // namespace detail

// ---- render mode names ---------------------------------------------------------------------------

const char* sdfRenderModeName(SdfRenderMode mode) {
    const auto i = static_cast<std::size_t>(mode);
    return i < kRenderModeNames.size() ? kRenderModeNames[i] : "raymarch";
}

std::optional<SdfRenderMode> sdfRenderModeFromName(std::string_view name) {
    for (std::size_t i = 0; i < kRenderModeNames.size(); ++i) {
        if (name == kRenderModeNames[i]) {
            return static_cast<SdfRenderMode>(i);
        }
    }
    return std::nullopt;
}

// ---- SdfObject -----------------------------------------------------------------------------------

Result<void> SdfObject::validate() const {
    if (auto ok = tree.validate(); !ok) {
        return fail("sdf '{}': {}", name, ok.error().message);
    }
    if (!(boundsMax.x > boundsMin.x && boundsMax.y > boundsMin.y && boundsMax.z > boundsMin.z)) {
        return fail("sdf '{}': bounds must have a positive extent on every axis", name);
    }
    if (!std::isfinite(boundsMin.x) || !std::isfinite(boundsMin.y) || !std::isfinite(boundsMin.z) ||
        !std::isfinite(boundsMax.x) || !std::isfinite(boundsMax.y) || !std::isfinite(boundsMax.z)) {
        return fail("sdf '{}': bounds must be finite", name);
    }
    if (resolution < 2 || resolution > 256) {
        return fail("sdf '{}': resolution must be in 2..256 (got {})", name, resolution);
    }
    if (maxSteps < 1 || maxSteps > 1024) {
        return fail("sdf '{}': maxSteps must be in 1..1024 (got {})", name, maxSteps);
    }
    if (!(epsilon > 0.0f) || !std::isfinite(epsilon)) {
        return fail("sdf '{}': epsilon must be > 0", name);
    }
    if (!(stepScale >= 0.1f && stepScale <= 1.0f)) {
        return fail("sdf '{}': stepScale must be in 0.1..1 (got {})", name, stepScale);
    }
    if (!(normalEpsilon > 0.0f) || !std::isfinite(normalEpsilon)) {
        return fail("sdf '{}': normalEpsilon must be > 0", name);
    }
    return {};
}

std::uint64_t SdfObject::structuralHash() const {
    StructHash h;
    h.u64(tree.structuralHash());
    h.v3(boundsMin);
    h.v3(boundsMax);
    h.i32(resolution);
    h.u32(static_cast<std::uint32_t>(renderMode));
    return h.value();
}

bool SdfObject::rebuild(double time, const spatial::FieldSet* fields) {
    const std::uint64_t hash = structuralHash();
    const bool changed = hash != builtHash || (renderMode == SdfRenderMode::Mesh && meshHash != hash);
    if (!changed) {
        return false;
    }
    builtHash = hash;
    ++structureVersion;
    if (renderMode == SdfRenderMode::Mesh) {
        auto meshed = spatial::meshSdf(tree, boundsMin, boundsMax, resolution, time, fields);
        if (meshed) {
            mesh = std::move(*meshed);
            mesh.name = name.empty() ? "sdf" : name;
        } else {
            mesh = MeshData{};
        }
        meshHash = hash;
    } else {
        mesh = MeshData{};
        meshHash = 0;
    }
    return true;
}

json SdfObject::toJson() const {
    json j = json::object();
    j["name"] = name;
    j["visible"] = visible;
    j["tree"] = tree.toJson();
    j["position"] = vecToJson(transform.position);
    j["rotation"] = vecToJson(eulerDegrees(transform.rotation));
    j["scale"] = vecToJson(transform.scale);
    {
        json m = json::object();
        m["baseColor"] = vecToJson(material.baseColor);
        m["emissiveColor"] = vecToJson(material.emissiveColor);
        m["emissiveIntensity"] = material.emissiveIntensity;
        m["roughness"] = material.roughness;
        m["metallic"] = material.metallic;
        j["material"] = std::move(m);
    }
    j["renderMode"] = sdfRenderModeName(renderMode);
    j["boundsMin"] = vecToJson(boundsMin);
    j["boundsMax"] = vecToJson(boundsMax);
    j["resolution"] = resolution;
    j["maxSteps"] = maxSteps;
    j["epsilon"] = epsilon;
    j["stepScale"] = stepScale;
    j["normalEpsilon"] = normalEpsilon;
    return j;
}

Result<SdfObject> SdfObject::fromJson(const json& j) {
    if (!j.is_object()) {
        return fail("sdf object must be a JSON object");
    }
    SdfObject o;
    AVGEN_SDF_READ(o.name, "name", readString);
    AVGEN_SDF_READ(o.visible, "visible", readBool);
    if (j.contains("tree")) {
        auto tree = spatial::SdfTree::fromJson(j.at("tree"));
        if (!tree) {
            return std::unexpected(tree.error());
        }
        o.tree = std::move(*tree);
    }
    AVGEN_SDF_READ(o.transform.position, "position", readVec3);
    {
        auto rotation = readVec3(j, "rotation", eulerDegrees(o.transform.rotation));
        if (!rotation) {
            return std::unexpected(rotation.error());
        }
        o.transform.rotation = quatFromEulerDegrees(*rotation);
    }
    AVGEN_SDF_READ(o.transform.scale, "scale", readVec3);
    if (j.contains("material")) {
        const json& m = j.at("material");
        if (!m.is_object()) {
            return fail("'material' must be an object");
        }
        auto read = [&](auto& target, const char* key, auto reader) -> Result<void> {
            auto value = reader(m, key, target);
            if (!value) {
                return std::unexpected(value.error());
            }
            target = *value;
            return {};
        };
        if (auto r = read(o.material.baseColor, "baseColor", readVec3); !r) return std::unexpected(r.error());
        if (auto r = read(o.material.emissiveColor, "emissiveColor", readVec3); !r) return std::unexpected(r.error());
        if (auto r = read(o.material.emissiveIntensity, "emissiveIntensity", readFloat); !r) return std::unexpected(r.error());
        if (auto r = read(o.material.roughness, "roughness", readFloat); !r) return std::unexpected(r.error());
        if (auto r = read(o.material.metallic, "metallic", readFloat); !r) return std::unexpected(r.error());
    }
    if (j.contains("renderMode")) {
        const json& v = j.at("renderMode");
        if (!v.is_string()) {
            return fail("'renderMode' must be a string");
        }
        const auto mode = sdfRenderModeFromName(v.get<std::string>());
        if (!mode) {
            return fail("unknown sdf render mode '{}'", v.get<std::string>());
        }
        o.renderMode = *mode;
    }
    AVGEN_SDF_READ(o.boundsMin, "boundsMin", readVec3);
    AVGEN_SDF_READ(o.boundsMax, "boundsMax", readVec3);
    AVGEN_SDF_READ(o.resolution, "resolution", readInt);
    AVGEN_SDF_READ(o.maxSteps, "maxSteps", readInt);
    AVGEN_SDF_READ(o.epsilon, "epsilon", readFloat);
    AVGEN_SDF_READ(o.stepScale, "stepScale", readFloat);
    AVGEN_SDF_READ(o.normalEpsilon, "normalEpsilon", readFloat);
    if (auto ok = o.validate(); !ok) {
        return std::unexpected(ok.error());
    }
    return o;
}

#undef AVGEN_SDF_READ

// ---- parameters ----------------------------------------------------------------------------------

SdfParameters registerSdfParameters(params::ParameterSet& params, const SdfObject& rest, const std::string& prefix) {
    SdfParameters p;
    p.prefix = prefix;
    std::string group = prefix;
    while (!group.empty() && group.back() == '/') {
        group.pop_back();
    }
    Registrar r{params, p, group};

    p.visible = r.b("visible", "visible", rest.visible);
    p.position = r.v3("transform/position", "transform/position", rest.transform.position, -1e4f, 1e4f, -20.0f, 20.0f);
    p.rotation = r.v3("transform/rotation", "transform/rotation", eulerDegrees(rest.transform.rotation), -360.0f, 360.0f,
                      -360.0f, 360.0f);
    p.scale = r.v3("transform/scale", "transform/scale", rest.transform.scale, 0.001f, 100.0f, 0.01f, 5.0f);
    p.baseColor = r.v3("material/baseColor", "material/baseColor", rest.material.baseColor, 0.0f, 100.0f, 0.0f, 1.0f, true);
    r.v3("material/emissiveColor", "material/emissiveColor", rest.material.emissiveColor, 0.0f, 100.0f, 0.0f, 1.0f, true);
    p.emissive = r.f("material/emissive", "material/emissive", rest.material.emissiveIntensity, 0.0f, 1000.0f, 0.0f, 20.0f);
    r.f("material/roughness", "material/roughness", rest.material.roughness, 0.0f, 1.0f, 0.0f, 1.0f);
    r.f("material/metallic", "material/metallic", rest.material.metallic, 0.0f, 1.0f, 0.0f, 1.0f);
    r.v3("bounds/min", "bounds/min", rest.boundsMin, -1e4f, 1e4f, -20.0f, 20.0f);
    r.v3("bounds/max", "bounds/max", rest.boundsMax, -1e4f, 1e4f, -20.0f, 20.0f);
    r.i("resolution", rest.resolution, 2, 256, 8, 128);

    const int nodeCount = rest.tree.nodeCount();
    p.nodeAmount.assign(static_cast<std::size_t>(nodeCount), nullptr);
    p.nodeRadius.assign(static_cast<std::size_t>(nodeCount), nullptr);
    p.nodeSmooth.assign(static_cast<std::size_t>(nodeCount), nullptr);
    int index = 0;
    visitPreOrder(rest.tree.root, index, [&](const SdfNode& n, int i) {
        const std::string kindName = spatial::sdfNodeKindName(n.kind);
        const auto slot = static_cast<std::size_t>(i - 1);
        for (const NodeField field : nodeFields(n.kind)) {
            const std::string rel = nodePath(i, field);
            const std::string label = kindName + "/" + kNodeFieldNames[static_cast<std::size_t>(field)];
            switch (field) {
            case NodeField::Radius:
                p.nodeRadius[slot] = r.f(rel, label, n.radius, 0.0f, 1000.0f, 0.0f, 10.0f);
                break;
            case NodeField::Height:
                r.f(rel, label, n.height, 0.0f, 1000.0f, 0.0f, 20.0f);
                break;
            case NodeField::Size:
                r.v3(rel, label, n.size, 0.0f, 1000.0f, 0.0f, 10.0f);
                break;
            case NodeField::Rounding:
                r.f(rel, label, n.rounding, 0.0f, 1000.0f, 0.0f, 2.0f);
                break;
            case NodeField::Offset:
                r.f(rel, label, n.offset, -1e4f, 1e4f, -10.0f, 10.0f);
                break;
            case NodeField::Translation:
                r.v3(rel, label, n.translation, -1e4f, 1e4f, -10.0f, 10.0f);
                break;
            case NodeField::Rotation:
                r.v3(rel, label, n.rotationDegrees, -360.0f, 360.0f, -360.0f, 360.0f);
                break;
            case NodeField::Scale:
                r.f(rel, label, n.scale, 0.001f, 1000.0f, 0.01f, 10.0f);
                break;
            case NodeField::Amount:
                p.nodeAmount[slot] = r.f(rel, label, n.amount, -100.0f, 100.0f, -3.0f, 3.0f);
                break;
            case NodeField::Smooth:
                p.nodeSmooth[slot] = r.f(rel, label, n.smooth, 0.0f, 100.0f, 0.0f, 2.0f);
                break;
            case NodeField::Frequency:
                r.f(rel, label, n.frequency, 0.0f, 100.0f, 0.0f, 10.0f);
                break;
            case NodeField::Speed:
                r.f(rel, label, n.speed, -100.0f, 100.0f, -5.0f, 5.0f);
                break;
            case NodeField::Enabled:
                r.b(rel, label, n.enabled);
                break;
            }
        }
    });
    return p;
}

bool applySdfParameters(const SdfParameters& p, const SdfObject& rest, SdfObject& live) {
    const std::uint64_t previous = live.structuralHash();
    // Keep the structural outputs of `live` (version, mesh cache); everything else restarts from rest.
    const std::uint64_t structureVersion = live.structureVersion;
    const std::uint64_t builtHash = live.builtHash;
    MeshData mesh = std::move(live.mesh);
    const std::uint64_t meshHash = live.meshHash;
    live = rest;
    live.structureVersion = structureVersion;
    live.builtHash = builtHash;
    live.mesh = std::move(mesh);
    live.meshHash = meshHash;

    const ParamIndex index(p);
    index.copy("visible", live.visible);
    index.copy("transform/position", live.transform.position);
    if (auto* rot = index.find<glm::vec3>("transform/rotation")) {
        live.transform.rotation = quatFromEulerDegrees(rot->value());
    }
    index.copy("transform/scale", live.transform.scale);
    index.copy("material/baseColor", live.material.baseColor);
    index.copy("material/emissiveColor", live.material.emissiveColor);
    index.copy("material/emissive", live.material.emissiveIntensity);
    index.copy("material/roughness", live.material.roughness);
    index.copy("material/metallic", live.material.metallic);
    index.copy("bounds/min", live.boundsMin);
    index.copy("bounds/max", live.boundsMax);
    index.copy("resolution", live.resolution);

    char buf[64];
    int i = 0;
    visitPreOrder(live.tree.root, i, [&](SdfNode& n, int nodeIndex) {
        for (const NodeField field : nodeFields(n.kind)) {
            const std::string_view key = nodeKey(buf, sizeof(buf), nodeIndex, field);
            switch (field) {
            case NodeField::Radius:
                index.copy(key, n.radius);
                break;
            case NodeField::Height:
                index.copy(key, n.height);
                break;
            case NodeField::Size:
                index.copy(key, n.size);
                break;
            case NodeField::Rounding:
                index.copy(key, n.rounding);
                break;
            case NodeField::Offset:
                index.copy(key, n.offset);
                break;
            case NodeField::Translation:
                index.copy(key, n.translation);
                break;
            case NodeField::Rotation:
                index.copy(key, n.rotationDegrees);
                break;
            case NodeField::Scale:
                index.copy(key, n.scale);
                break;
            case NodeField::Amount:
                index.copy(key, n.amount);
                break;
            case NodeField::Smooth:
                index.copy(key, n.smooth);
                break;
            case NodeField::Frequency:
                index.copy(key, n.frequency);
                break;
            case NodeField::Speed:
                index.copy(key, n.speed);
                break;
            case NodeField::Enabled:
                index.copy(key, n.enabled);
                break;
            }
        }
    });
    return live.structuralHash() != previous;
}

void unregisterSdfParameters(params::ParameterSet& params, const SdfParameters& p) {
    for (params::IParameter* ip : p.all) {
        if (ip != nullptr) {
            const std::string path = ip->path(); // remove() destroys the parameter that owns it
            params.remove(path);
        }
    }
}

} // namespace avgen::scene
