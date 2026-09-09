#include "scene/composition.hpp"

#include "core/log.hpp"
#include "scene/mesh_generators.hpp"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <map>
#include <nlohmann/json.hpp>
#include <string_view>
#include <utility>

namespace avgen::scene {

namespace {

using nlohmann::json;

// ---- parameter descriptors --------------------------------------------------------------------

params::ParamDesc<float> floatDesc(std::string path, float def, float lo, float hi, float softLo,
                                   float softHi) {
    params::ParamDesc<float> d;
    d.path = std::move(path);
    d.defaultValue = def;
    d.hardMin = lo;
    d.hardMax = hi;
    d.softMin = softLo;
    d.softMax = softHi;
    return d;
}

params::ParamDesc<glm::vec3> vec3Desc(std::string path, glm::vec3 def, float lo, float hi, float softLo,
                                      float softHi) {
    params::ParamDesc<glm::vec3> d;
    d.path = std::move(path);
    d.defaultValue = def;
    d.hardMin = glm::vec3(lo);
    d.hardMax = glm::vec3(hi);
    d.softMin = glm::vec3(softLo);
    d.softMax = glm::vec3(softHi);
    return d;
}

params::ParamDesc<bool> boolDesc(std::string path, bool def) {
    params::ParamDesc<bool> d;
    d.path = std::move(path);
    d.defaultValue = def;
    d.hardMin = false;
    d.hardMax = true;
    return d;
}

// Node names and prefixes become parameter path segments; keep them free of separators.
std::string sanitise(std::string name) {
    for (auto& c : name) {
        if (c == '/' || c == ' ' || c == '.') {
            c = '_';
        }
    }
    return name;
}

bool hasRouteTo(const params::Modulator& modulator, std::string_view target) {
    const auto& routes = modulator.routes();
    return std::any_of(routes.begin(), routes.end(),
                       [&](const params::ModRoute& r) { return r.target == target; });
}

template <typename F>
void forEachParticleParam(ParticleParameters& p, F&& f) {
    f(p.spawnRate);
    f(p.burst);
    f(p.lifetime);
    f(p.speed);
    f(p.spread);
    f(p.position);
    f(p.extent);
    f(p.gravity);
    f(p.drag);
    f(p.turbulence);
    f(p.turbulenceScale);
    f(p.turbulenceSpeed);
    f(p.attractorPosition);
    f(p.attractorStrength);
    f(p.orbit);
    f(p.size);
    f(p.colorStart);
    f(p.colorEnd);
    f(p.emissive);
    f(p.enabled);
}

// ---- transforms --------------------------------------------------------------------------------

glm::quat quatFromEulerDegrees(const glm::vec3& degrees) {
    return glm::quat(glm::radians(degrees));
}

// Inverse of glm::quat(vec3): that constructor builds Rz * Ry * Rx. glm::eulerAngles recovers
// the middle angle with asin, which loses precision near +-90 degrees; atan2 does not.
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
        x = std::atan2(-m20 * m01, m11); // gimbal lock: fold roll into pitch
    }
    return glm::degrees(glm::vec3(x, y, z));
}

bool uniformScale(const glm::vec3& s) {
    return std::abs(s.x - s.y) < 1e-6f && std::abs(s.x - s.z) < 1e-6f;
}

// outer * inner: applies `inner` first. Exact TRS composition when the outer scale is uniform,
// matrix decomposition otherwise.
Transform compose(const Transform& outer, const Transform& inner) {
    if (uniformScale(outer.scale)) {
        Transform t;
        t.position = outer.position + outer.rotation * (inner.position * outer.scale.x);
        t.rotation = outer.rotation * inner.rotation;
        t.scale = inner.scale * outer.scale.x;
        return t;
    }
    return Transform::fromMatrix(outer.matrix() * inner.matrix());
}

glm::vec3 transformPoint(const Transform& t, const glm::vec3& p) {
    return t.position + t.rotation * (p * t.scale);
}

glm::vec3 transformDirection(const Transform& t, const glm::vec3& d) {
    const glm::vec3 r = t.rotation * d;
    const float len = glm::length(r);
    return len > 1e-12f ? r / len : d;
}

// The scale factor a nested particle system inherits (its extents and sizes are lengths).
float lengthScale(const Transform& t) {
    return std::cbrt(std::abs(t.scale.x * t.scale.y * t.scale.z));
}

// ---- particle JSON -----------------------------------------------------------------------------

const char* shapeName(EmitterShape shape) {
    switch (shape) {
    case EmitterShape::Point:
        return "point";
    case EmitterShape::Sphere:
        return "sphere";
    case EmitterShape::Disc:
        return "disc";
    case EmitterShape::Box:
        return "box";
    }
    return "sphere";
}

Result<EmitterShape> shapeFromName(const std::string& name) {
    if (name == "point") {
        return EmitterShape::Point;
    }
    if (name == "sphere") {
        return EmitterShape::Sphere;
    }
    if (name == "disc") {
        return EmitterShape::Disc;
    }
    if (name == "box") {
        return EmitterShape::Box;
    }
    return fail("unknown emitter shape '{}'", name);
}

const char* blendName(ParticleBlend blend) {
    return blend == ParticleBlend::Alpha ? "alpha" : "additive";
}

Result<ParticleBlend> blendFromName(const std::string& name) {
    if (name == "additive") {
        return ParticleBlend::Additive;
    }
    if (name == "alpha") {
        return ParticleBlend::Alpha;
    }
    return fail("unknown particle blend '{}'", name);
}

json vecToJson(const glm::vec3& v) {
    return json::array({v.x, v.y, v.z});
}

json vecToJson(const glm::vec4& v) {
    return json::array({v.x, v.y, v.z, v.w});
}

template <glm::length_t N>
Result<glm::vec<N, float>> readVec(const json& j, const char* key, const glm::vec<N, float>& def) {
    if (!j.contains(key)) {
        return def;
    }
    const json& a = j.at(key);
    if (!a.is_array() || a.size() != static_cast<std::size_t>(N)) {
        return fail("'{}' must be an array of {} numbers", key, static_cast<int>(N));
    }
    glm::vec<N, float> out{};
    for (glm::length_t i = 0; i < N; ++i) {
        const json& e = a.at(static_cast<std::size_t>(i));
        if (!e.is_number()) {
            return fail("'{}' must be an array of {} numbers", key, static_cast<int>(N));
        }
        out[i] = e.get<float>();
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

json particlesToJson(const ParticleSystem& s) {
    json j;
    j["enabled"] = s.enabled;
    j["capacity"] = s.capacity;
    j["seed"] = s.seed;
    j["shape"] = shapeName(s.shape);
    j["position"] = vecToJson(s.position);
    j["extent"] = vecToJson(s.extent);
    j["spawnRate"] = s.spawnRate;
    j["burst"] = s.burst;
    j["lifetimeMin"] = s.lifetimeMin;
    j["lifetimeMax"] = s.lifetimeMax;
    j["direction"] = vecToJson(s.direction);
    j["spread"] = s.spread;
    j["speedMin"] = s.speedMin;
    j["speedMax"] = s.speedMax;
    j["gravity"] = vecToJson(s.gravity);
    j["drag"] = s.drag;
    j["turbulence"] = s.turbulence;
    j["turbulenceScale"] = s.turbulenceScale;
    j["turbulenceSpeed"] = s.turbulenceSpeed;
    j["attractorPosition"] = vecToJson(s.attractorPosition);
    j["attractorStrength"] = s.attractorStrength;
    j["attractorRadius"] = s.attractorRadius;
    j["orbit"] = s.orbit;
    j["sizeStart"] = s.sizeStart;
    j["sizeEnd"] = s.sizeEnd;
    j["colorStart"] = vecToJson(s.colorStart);
    j["colorEnd"] = vecToJson(s.colorEnd);
    j["emissive"] = s.emissive;
    j["blend"] = blendName(s.blend);
    j["softness"] = s.softness;
    return j;
}

// Missing fields keep the ParticleSystem defaults so hand-written files can stay short.
Result<ParticleSystem> particlesFromJson(const json& j) {
    if (!j.is_object()) {
        return fail("'particles' must be an object");
    }
    ParticleSystem s;
#define AVGEN_READ(field, reader)                                                                            \
    do {                                                                                                     \
        auto r = reader(j, #field, s.field);                                                                 \
        if (!r) {                                                                                            \
            return std::unexpected(r.error());                                                               \
        }                                                                                                    \
        s.field = *r;                                                                                        \
    } while (false)
    AVGEN_READ(enabled, readBool);
    if (j.contains("capacity")) {
        if (!j.at("capacity").is_number_unsigned()) {
            return fail("'capacity' must be a positive integer");
        }
        s.capacity = j.at("capacity").get<std::uint32_t>();
    }
    if (j.contains("seed")) {
        if (!j.at("seed").is_number_unsigned()) {
            return fail("'seed' must be a positive integer");
        }
        s.seed = j.at("seed").get<std::uint32_t>();
    }
    if (j.contains("shape")) {
        auto name = readString(j, "shape", "");
        if (!name) {
            return std::unexpected(name.error());
        }
        auto shape = shapeFromName(*name);
        if (!shape) {
            return std::unexpected(shape.error());
        }
        s.shape = *shape;
    }
    AVGEN_READ(position, readVec<3>);
    AVGEN_READ(extent, readVec<3>);
    AVGEN_READ(spawnRate, readFloat);
    AVGEN_READ(burst, readFloat);
    AVGEN_READ(lifetimeMin, readFloat);
    AVGEN_READ(lifetimeMax, readFloat);
    AVGEN_READ(direction, readVec<3>);
    AVGEN_READ(spread, readFloat);
    AVGEN_READ(speedMin, readFloat);
    AVGEN_READ(speedMax, readFloat);
    AVGEN_READ(gravity, readVec<3>);
    AVGEN_READ(drag, readFloat);
    AVGEN_READ(turbulence, readFloat);
    AVGEN_READ(turbulenceScale, readFloat);
    AVGEN_READ(turbulenceSpeed, readFloat);
    AVGEN_READ(attractorPosition, readVec<3>);
    AVGEN_READ(attractorStrength, readFloat);
    AVGEN_READ(attractorRadius, readFloat);
    AVGEN_READ(orbit, readFloat);
    AVGEN_READ(sizeStart, readFloat);
    AVGEN_READ(sizeEnd, readFloat);
    AVGEN_READ(colorStart, readVec<4>);
    AVGEN_READ(colorEnd, readVec<4>);
    AVGEN_READ(emissive, readFloat);
    if (j.contains("blend")) {
        auto name = readString(j, "blend", "");
        if (!name) {
            return std::unexpected(name.error());
        }
        auto blend = blendFromName(*name);
        if (!blend) {
            return std::unexpected(blend.error());
        }
        s.blend = *blend;
    }
    AVGEN_READ(softness, readFloat);
#undef AVGEN_READ
    return s;
}

PunctualLight defaultKeyLight() {
    PunctualLight key;
    key.name = "key";
    key.direction = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.35f));
    key.color = glm::vec3(1.0f, 0.97f, 0.92f);
    key.intensity = 3.0f;
    return key;
}

void offsetTextureRef(TextureRef& ref, TextureId offset) {
    if (ref.valid()) {
        ref.texture += offset;
    }
}

void offsetEntityIds(Entity& e, MeshId meshOffset, TextureId textureOffset) {
    if (e.mesh != kInvalidMesh) {
        e.mesh += meshOffset;
    }
    offsetTextureRef(e.material.baseColorTexture, textureOffset);
    offsetTextureRef(e.material.metallicRoughnessTexture, textureOffset);
    offsetTextureRef(e.material.normalTexture, textureOffset);
    offsetTextureRef(e.material.emissiveTexture, textureOffset);
    offsetTextureRef(e.material.occlusionTexture, textureOffset);
}

constexpr float kFitFovRadians = 0.87f;

} // namespace

// ---- node kinds --------------------------------------------------------------------------------

const char* nodeKindName(NodeKind kind) {
    switch (kind) {
    case NodeKind::Gltf:
        return "gltf";
    case NodeKind::Orb:
        return "orb";
    case NodeKind::Grid:
        return "grid";
    case NodeKind::Particles:
        return "particles";
    case NodeKind::Scene:
        return "scene";
    }
    return "gltf";
}

Result<NodeKind> nodeKindFromName(const std::string& name) {
    for (const NodeKind kind :
         {NodeKind::Gltf, NodeKind::Orb, NodeKind::Grid, NodeKind::Particles, NodeKind::Scene}) {
        if (name == nodeKindName(kind)) {
            return kind;
        }
    }
    return fail("unknown node kind '{}'", name);
}

// ---- lifetime ----------------------------------------------------------------------------------

Composition::Composition(assets::AssetRegistry& registry, std::string name)
    : registry_(registry)
    , name_(std::move(name)) {
    scene_.environment.gridIntensity = 0.6f;
    scene_.environment.backgroundColor = glm::vec3(0.02f, 0.02f, 0.03f);
}

Composition::~Composition() = default;

// ---- nodes -------------------------------------------------------------------------------------

std::string Composition::uniqueName(const std::string& base) const {
    auto taken = [&](const std::string& candidate) {
        return std::any_of(nodes_.begin(), nodes_.end(),
                           [&](const std::unique_ptr<CompositionNode>& n) { return n->name == candidate; });
    };
    if (!taken(base)) {
        return base;
    }
    for (int i = 2;; ++i) {
        const std::string candidate = base + std::to_string(i);
        if (!taken(candidate)) {
            return candidate;
        }
    }
}

Result<std::unique_ptr<Composition>> Composition::loadChild(const std::filesystem::path& asset) const {
    if (asset.empty()) {
        return fail("scene node needs an asset path");
    }
    const std::filesystem::path resolved = registry_.resolve(asset);
    if (resolved == sourcePath_ ||
        std::find(ancestors_.begin(), ancestors_.end(), resolved) != ancestors_.end()) {
        return fail("scene file includes itself: '{}'", resolved.string());
    }
    if (depth_ + 1 > kMaxNestingDepth) {
        return fail("scene '{}' nests deeper than {} levels", resolved.string(), kMaxNestingDepth);
    }
    std::vector<std::filesystem::path> chain = ancestors_;
    if (!sourcePath_.empty()) {
        chain.push_back(sourcePath_);
    }
    return loadNested(resolved, registry_, depth_ + 1, std::move(chain));
}

bool Composition::wouldCycle(const std::string& node, const std::string& parent) const {
    // Walk up from `parent`; reaching `node` (or looping longer than the node count) is a cycle.
    std::string current = parent;
    for (std::size_t guard = 0; !current.empty() && guard <= nodes_.size(); ++guard) {
        if (current == node) {
            return true;
        }
        const CompositionNode* p = findNode(current);
        if (p == nullptr) {
            return false; // unresolved parent: a root
        }
        current = p->parent;
    }
    return !current.empty();
}

Result<void> Composition::setParent(const std::string& name, const std::string& parent) {
    CompositionNode* node = findNode(name);
    if (node == nullptr) {
        return fail("node '{}' not found", name);
    }
    if (!parent.empty()) {
        if (parent == name || wouldCycle(name, parent)) {
            return fail("node '{}': parent '{}' would form a cycle", name, parent);
        }
        if (findNode(parent) == nullptr) {
            log::warn("composition '{}': node '{}' parent '{}' not found (treated as root)", name_, name, parent);
        }
    }
    node->parent = parent;
    dirty_ = true;
    return {};
}

Transform Composition::nodeWorldTransform(const CompositionNode& node) const {
    Transform world = nodeTransform(node);
    const CompositionNode* current = &node;
    for (std::size_t guard = 0; !current->parent.empty() && guard < nodes_.size(); ++guard) {
        const CompositionNode* parent = findNode(current->parent);
        if (parent == nullptr || parent == &node) {
            break; // unresolved parent (root) or a cycle (edited in place): stop here
        }
        world = compose(nodeTransform(*parent), world);
        current = parent;
    }
    return world;
}

Result<CompositionNode*> Composition::addNode(CompositionNode node) {
    const std::string base = node.name.empty() ? std::string(nodeKindName(node.kind)) : sanitise(node.name);
    node.name = uniqueName(base);
    if (!node.parent.empty()) {
        if (node.parent == node.name || wouldCycle(node.name, node.parent)) {
            return fail("node '{}': parent '{}' would form a cycle", node.name, node.parent);
        }
        if (findNode(node.parent) == nullptr) {
            log::warn("composition '{}': node '{}' parent '{}' not found (treated as root)", name_, node.name,
                      node.parent);
        }
    }
    node.sceneAsset.reset();
    node.child.reset();
    node.positionParam = nullptr;
    node.rotationParam = nullptr;
    node.scaleParam = nullptr;
    node.visibleParam = nullptr;
    node.emissiveParam = nullptr;
    node.roughnessParam = nullptr;
    node.particleParams = {};

    switch (node.kind) {
    case NodeKind::Gltf: {
        if (node.asset.empty()) {
            return fail("node '{}': gltf node needs an asset path", node.name);
        }
        auto asset = registry_.loadScene(node.asset);
        if (!asset) {
            return std::unexpected(asset.error());
        }
        node.sceneAsset = std::move(*asset);
        break;
    }
    case NodeKind::Scene: {
        auto child = loadChild(node.asset);
        if (!child) {
            return std::unexpected(child.error());
        }
        node.child = std::move(*child);
        break;
    }
    case NodeKind::Particles:
        node.particles.name = node.name;
        node.particleRest = node.particles;
        break;
    case NodeKind::Orb:
    case NodeKind::Grid:
        break;
    }

    nodes_.push_back(std::make_unique<CompositionNode>(std::move(node)));
    CompositionNode* added = nodes_.back().get();
    dirty_ = true;
    if (attached()) {
        registerNodeParameters(*added);
    }
    return added;
}

bool Composition::removeNode(const std::string& name) {
    const auto it = std::find_if(nodes_.begin(), nodes_.end(),
                                 [&](const std::unique_ptr<CompositionNode>& n) { return n->name == name; });
    if (it == nodes_.end()) {
        return false;
    }
    unregisterNodeParameters(**it);
    const std::string grandParent = (*it)->parent;
    nodes_.erase(it);
    for (auto& other : nodes_) {
        if (other->parent == name) {
            other->parent = grandParent; // children keep their local transforms under the grandparent
        }
    }
    dirty_ = true;
    return true;
}

CompositionNode* Composition::findNode(const std::string& name) {
    const auto it = std::find_if(nodes_.begin(), nodes_.end(),
                                 [&](const std::unique_ptr<CompositionNode>& n) { return n->name == name; });
    return it == nodes_.end() ? nullptr : it->get();
}

const CompositionNode* Composition::findNode(const std::string& name) const {
    const auto it = std::find_if(nodes_.begin(), nodes_.end(),
                                 [&](const std::unique_ptr<CompositionNode>& n) { return n->name == name; });
    return it == nodes_.end() ? nullptr : it->get();
}

// ---- parameters --------------------------------------------------------------------------------

float Composition::fitDistance() const {
    return radius_ / std::tan(kFitFovRadians * 0.5f) * 1.15f;
}

void Composition::attach(params::ParameterSet& params, params::Modulator& modulator,
                         const std::string& prefix) {
    params_ = &params;
    modulator_ = &modulator;
    prefix_ = prefix;
    if (dirty_) {
        rebuild(); // camera defaults are fitted to the flattened bounds
    }
    const bool root = prefix_.empty();
    const float fit = fitDistance();
    cameraDistance_ = &params.add(floatDesc(prefix_ + "camera/distance", cameraDistanceSetting_.value_or(fit),
                                            0.01f, 1000.0f, fit * 0.3f, fit * 3.0f));
    cameraHeight_ = &params.add(
        floatDesc(prefix_ + "camera/height", cameraHeightSetting_.value_or(center_.y + radius_ * 0.35f),
                  -1000.0f, 1000.0f, center_.y - radius_, center_.y + radius_ * 2.0f));
    cameraOrbitSpeed_ = &params.add(
        floatDesc(prefix_ + "camera/orbitSpeed", cameraOrbitSpeedSetting_, -3.0f, 3.0f, -1.0f, 1.0f));
    cameraFov_ =
        &params.add(floatDesc(prefix_ + "camera/fov", cameraFovSetting_, 5.0f, 120.0f, 20.0f, 90.0f));
    envIntensity_ =
        &params.add(floatDesc(prefix_ + "env/intensity", envIntensitySetting_, 0.0f, 20.0f, 0.0f, 4.0f));
    envRotation_ =
        &params.add(floatDesc(prefix_ + "env/rotation", 0.0f, -6.2832f, 6.2832f, -3.1416f, 3.1416f));
    brightness_ = &params.add(floatDesc(prefix_ + "scene/brightness", 1.0f, 0.0f, 8.0f, 0.0f, 3.0f));
    gridIntensity_ = &params.add(floatDesc(prefix_ + "scene/gridIntensity", 0.6f, 0.0f, 4.0f, 0.0f, 2.0f));
    rootScale_ = &params.add(floatDesc(prefix_ + "root/scale", 1.0f, 0.05f, 8.0f, 0.2f, 3.0f));
    // Nested compositions do not spin on their own by default; the enclosing root does.
    rootRotationSpeed_ = &params.add(
        floatDesc(prefix_ + "root/rotationSpeed", root ? 0.15f : 0.0f, -20.0f, 20.0f, -3.0f, 3.0f));
    rootImpulse_ = &params.add(floatDesc(prefix_ + "root/impulse", 0.0f, 0.0f, 4.0f, 0.0f, 1.0f));
    for (auto& node : nodes_) {
        registerNodeParameters(*node);
    }
    if (root) {
        addDefaultRoutes(modulator);
    } else {
        dirty_ = true; // nested particle systems were renamed for their parameter paths
    }
}

void Composition::registerNodeParameters(CompositionNode& node) {
    if (params_ == nullptr) {
        return;
    }
    const std::string base = prefix_ + "nodes/" + node.name + "/";
    const float reach = 10.0f * std::max(radius_, 0.1f);
    node.positionParam =
        &params_->add(vec3Desc(base + "position", node.transform.position, -1e4f, 1e4f, -reach, reach));
    node.rotationParam = &params_->add(
        vec3Desc(base + "rotation", eulerDegrees(node.transform.rotation), -360.0f, 360.0f, -360.0f, 360.0f));
    node.scaleParam =
        &params_->add(vec3Desc(base + "scale", node.transform.scale, 0.001f, 100.0f, 0.01f, 5.0f));
    node.visibleParam = &params_->add(boolDesc(base + "visible", node.visible));
    node.emissiveParam =
        &params_->add(floatDesc(base + "emissiveBoost", node.emissiveBoost, 0.0f, 50.0f, 0.0f, 8.0f));
    node.roughnessParam =
        &params_->add(floatDesc(base + "roughnessScale", node.roughnessScale, 0.0f, 2.0f, 0.0f, 2.0f));
    if (node.kind == NodeKind::Particles) {
        // registerParticleParameters has no prefix: fold it into the system name instead
        // ("particles/nodes_a_sparks/..." for node "sparks" inside node "a").
        node.particleRest.name = sanitise(prefix_) + node.name;
        node.particleParams = registerParticleParameters(*params_, node.particleRest);
    }
    if (node.kind == NodeKind::Scene && node.child) {
        node.child->attach(*params_, *modulator_, base);
    }
}

void Composition::unregisterNodeParameters(CompositionNode& node) {
    if (params_ != nullptr) {
        const std::string base = prefix_ + "nodes/" + node.name + "/";
        for (const char* suffix :
             {"position", "rotation", "scale", "visible", "emissiveBoost", "roughnessScale"}) {
            params_->remove(base + suffix);
        }
        forEachParticleParam(node.particleParams, [&](auto* p) {
            if (p != nullptr) {
                params_->remove(p->path());
            }
        });
        if (node.child) {
            node.child->unregisterParameters();
        }
    }
    node.positionParam = nullptr;
    node.rotationParam = nullptr;
    node.scaleParam = nullptr;
    node.visibleParam = nullptr;
    node.emissiveParam = nullptr;
    node.roughnessParam = nullptr;
    node.particleParams = {};
}

void Composition::unregisterParameters() {
    if (params_ != nullptr) {
        for (const char* path : {"camera/distance", "camera/height", "camera/orbitSpeed", "camera/fov",
                                 "env/intensity", "env/rotation", "scene/brightness", "scene/gridIntensity",
                                 "root/scale", "root/rotationSpeed", "root/impulse"}) {
            params_->remove(prefix_ + path);
        }
        for (auto& node : nodes_) {
            unregisterNodeParameters(*node);
        }
    }
    detach();
}

void Composition::detach() {
    for (auto& node : nodes_) {
        node->positionParam = nullptr;
        node->rotationParam = nullptr;
        node->scaleParam = nullptr;
        node->visibleParam = nullptr;
        node->emissiveParam = nullptr;
        node->roughnessParam = nullptr;
        node->particleParams = {};
        if (node->child) {
            node->child->detach();
        }
    }
    cameraDistance_ = nullptr;
    cameraHeight_ = nullptr;
    cameraOrbitSpeed_ = nullptr;
    cameraFov_ = nullptr;
    envIntensity_ = nullptr;
    envRotation_ = nullptr;
    brightness_ = nullptr;
    gridIntensity_ = nullptr;
    rootScale_ = nullptr;
    rootRotationSpeed_ = nullptr;
    rootImpulse_ = nullptr;
    params_ = nullptr;
    modulator_ = nullptr;
}

void Composition::addDefaultRoutes(params::Modulator& modulator) {
    using namespace params;
    if (!hasRouteTo(modulator, "root/scale")) {
        ModRoute r{.source = "audio.bass", .target = "root/scale", .amount = 0.35f};
        r.chain.curve = CurveType::Power;
        r.chain.curveAmount = 0.8f;
        r.chain.attackMs = 15.0f;
        r.chain.decayMs = 180.0f;
        modulator.addRoute(r);
    }
    if (!hasRouteTo(modulator, "root/rotationSpeed")) {
        ModRoute r{.source = "audio.mid", .target = "root/rotationSpeed", .amount = 1.5f};
        r.chain.attackMs = 50.0f;
        r.chain.decayMs = 400.0f;
        modulator.addRoute(r);
    }
    if (!hasRouteTo(modulator, "scene/brightness")) {
        ModRoute r{.source = "audio.rms", .target = "scene/brightness", .amount = 0.5f};
        r.chain.attackMs = 30.0f;
        r.chain.decayMs = 500.0f;
        modulator.addRoute(r);
    }
    if (!hasRouteTo(modulator, "root/impulse")) {
        ModRoute r{.source = "audio.onset", .target = "root/impulse", .amount = 0.25f};
        r.chain.envelope = EnvelopeMode::PeakHold;
        r.chain.envelopeHoldMs = 30.0f;
        r.chain.envelopeFallPerSecond = 4.0f;
        modulator.addRoute(r);
    }
}

// ---- flattening --------------------------------------------------------------------------------

Transform Composition::nodeTransform(const CompositionNode& node) const {
    Transform t = node.transform;
    if (node.positionParam != nullptr) {
        t.position = node.positionParam->value();
    }
    if (node.rotationParam != nullptr) {
        t.rotation = quatFromEulerDegrees(node.rotationParam->value());
    }
    if (node.scaleParam != nullptr) {
        t.scale = node.scaleParam->value();
    }
    return t;
}

bool Composition::nodeVisible(const CompositionNode& node) {
    return node.visibleParam != nullptr ? node.visibleParam->value() : node.visible;
}

void Composition::ensureBuilt() {
    if (dirty_) {
        rebuild();
    }
}

void Composition::rebuild() {
    scene_.meshes.clear();
    scene_.textures.clear();
    scene_.entities.clear();
    scene_.particles.clear();
    scene_.lights.clear();
    scene_.cameras.clear();
    ranges_.clear();
    ranges_.reserve(nodes_.size());

    // Each unique asset's meshes and textures are stored once; instances share them by offset.
    std::map<std::string, std::pair<MeshId, TextureId>> assetOffsets;

    for (const auto& nodePtr : nodes_) {
        const CompositionNode& node = *nodePtr;
        NodeRange range;
        range.firstEntity = scene_.entities.size();
        range.firstParticle = scene_.particles.size();
        const Transform nodeT = nodeWorldTransform(node);
        const bool visible = nodeVisible(node);

        switch (node.kind) {
        case NodeKind::Gltf: {
            if (!node.sceneAsset) {
                break;
            }
            const assets::SceneAsset& asset = *node.sceneAsset;
            const std::string key = asset.path.string();
            auto it = assetOffsets.find(key);
            if (it == assetOffsets.end()) {
                const auto offsets = std::make_pair(static_cast<MeshId>(scene_.meshes.size()),
                                                    static_cast<TextureId>(scene_.textures.size()));
                for (const auto& mesh : asset.scene.meshes) {
                    scene_.meshes.push_back(mesh);
                }
                for (const auto& texture : asset.scene.textures) {
                    scene_.textures.push_back(texture);
                }
                it = assetOffsets.emplace(key, offsets).first;
            }
            const auto [meshOffset, textureOffset] = it->second;
            for (const Entity& src : asset.scene.entities) {
                Entity e = src;
                offsetEntityIds(e, meshOffset, textureOffset);
                e.name = node.name + "/" + src.name;
                e.transform = compose(nodeT, src.transform);
                e.visible = visible && src.visible;
                scene_.entities.push_back(std::move(e));
                range.restTransforms.push_back(src.transform);
                range.restEmissive.push_back(src.material.emissiveIntensity);
                range.restRoughness.push_back(src.material.roughness);
            }
            for (const PunctualLight& src : asset.scene.lights) {
                PunctualLight light = src;
                light.name = node.name + "/" + src.name;
                light.position = transformPoint(nodeT, src.position);
                light.direction = transformDirection(nodeT, src.direction);
                scene_.addLight(std::move(light));
            }
            break;
        }
        case NodeKind::Orb: {
            const MeshId mesh = scene_.addMesh(makeIcosphere(1.0f, 3));
            Entity& orb = scene_.addEntity(node.name, mesh);
            orb.style = MeshStyle::Lit;
            orb.material.baseColor = glm::vec3(0.75f, 0.2f, 0.9f);
            orb.material.emissiveColor = glm::vec3(0.9f, 0.45f, 1.0f);
            orb.material.emissiveIntensity = 0.15f;
            orb.material.roughness = 0.35f;
            orb.transform = nodeT;
            orb.visible = visible;
            range.restTransforms.emplace_back();
            range.restEmissive.push_back(0.15f);
            range.restRoughness.push_back(0.35f);
            break;
        }
        case NodeKind::Grid: {
            const MeshId mesh = scene_.addMesh(makePlane(12.0f, 48));
            Entity& grid = scene_.addEntity(node.name, mesh);
            grid.style = MeshStyle::Grid;
            grid.transform = nodeT;
            grid.visible = visible;
            range.restTransforms.emplace_back();
            range.restEmissive.push_back(0.0f);
            range.restRoughness.push_back(0.5f);
            break;
        }
        case NodeKind::Particles: {
            ParticleSystem ps = node.particleRest;
            ps.position = transformPoint(nodeT, ps.position);
            ps.attractorPosition = transformPoint(nodeT, ps.attractorPosition);
            ps.enabled = ps.enabled && visible;
            range.particleIndex = static_cast<int>(scene_.particles.size());
            scene_.particles.push_back(std::move(ps));
            break;
        }
        case NodeKind::Scene: {
            if (!node.child) {
                break;
            }
            node.child->ensureBuilt();
            const Scene& cs = node.child->scene();
            const auto meshOffset = static_cast<MeshId>(scene_.meshes.size());
            const auto textureOffset = static_cast<TextureId>(scene_.textures.size());
            for (const auto& mesh : cs.meshes) {
                scene_.meshes.push_back(mesh);
            }
            for (const auto& texture : cs.textures) {
                scene_.textures.push_back(texture);
            }
            for (const Entity& src : cs.entities) {
                Entity e = src;
                offsetEntityIds(e, meshOffset, textureOffset);
                e.name = node.name + "/" + src.name;
                e.transform = compose(nodeT, src.transform);
                e.visible = visible && src.visible;
                scene_.entities.push_back(std::move(e));
                range.restTransforms.push_back(src.transform);
                range.restEmissive.push_back(src.material.emissiveIntensity);
                range.restRoughness.push_back(src.material.roughness);
            }
            for (const PunctualLight& src : cs.lights) {
                PunctualLight light = src;
                light.name = node.name + "/" + src.name;
                light.position = transformPoint(nodeT, src.position);
                light.direction = transformDirection(nodeT, src.direction);
                scene_.addLight(std::move(light));
            }
            const float scale = lengthScale(nodeT);
            for (const ParticleSystem& src : cs.particles) {
                ParticleSystem ps = src;
                ps.position = transformPoint(nodeT, src.position);
                ps.attractorPosition = transformPoint(nodeT, src.attractorPosition);
                ps.extent = src.extent * scale;
                ps.sizeStart = src.sizeStart * scale;
                ps.sizeEnd = src.sizeEnd * scale;
                ps.enabled = src.enabled && visible;
                scene_.particles.push_back(std::move(ps));
            }
            range.childMeshVersion = cs.meshVersion;
            range.childEntityCount = cs.entities.size();
            range.childParticleCount = cs.particles.size();
            break;
        }
        }
        range.entityCount = scene_.entities.size() - range.firstEntity;
        range.particleCount = scene_.particles.size() - range.firstParticle;
        ranges_.push_back(std::move(range));
    }

    if (scene_.lights.empty()) {
        scene_.addLight(defaultKeyLight());
    }

    scene_.environment.environmentMap = kInvalidTexture;
    if (!environmentPath_.empty()) {
        auto image = registry_.loadImage(environmentPath_, false);
        if (!image) {
            log::warn("composition '{}': environment map: {}", name_, image.error().message);
        } else if (!(*image)->image.isHdr()) {
            log::warn("composition '{}': environment map '{}' is not an HDR image", name_,
                      environmentPath_.string());
        } else {
            scene_.environment.environmentMap = scene_.addTexture((*image)->image);
        }
    }

    // Framing: lit geometry when there is any, otherwise the particle emitters.
    const auto [lo, hi] = scene_.bounds();
    const bool hasLit = std::any_of(scene_.entities.begin(), scene_.entities.end(), [&](const Entity& e) {
        return e.visible && e.style == MeshStyle::Lit && e.mesh < scene_.meshes.size();
    });
    if (hasLit) {
        center_ = (lo + hi) * 0.5f;
        radius_ = std::max(glm::length(hi - lo) * 0.5f, 0.05f);
    } else if (!scene_.particles.empty()) {
        glm::vec3 plo(std::numeric_limits<float>::max());
        glm::vec3 phi(std::numeric_limits<float>::lowest());
        for (const auto& ps : scene_.particles) {
            const float reach = std::max({ps.extent.x, ps.extent.y, ps.extent.z, 0.5f});
            plo = glm::min(plo, ps.position - glm::vec3(reach));
            phi = glm::max(phi, ps.position + glm::vec3(reach));
        }
        center_ = (plo + phi) * 0.5f;
        radius_ = std::max(glm::length(phi - plo) * 0.5f, 0.05f);
    } else {
        center_ = glm::vec3(0.0f);
        radius_ = 1.0f;
    }

    ++scene_.meshVersion;
    ++scene_.textureVersion;
    dirty_ = false;
}

// ---- per-frame ---------------------------------------------------------------------------------

void Composition::update(const FrameTime& time) {
    if (dirty_) {
        rebuild();
    }
    // Children first so their parameters apply before this level reads their scenes; a child
    // whose structure changed (meshes added or removed) forces a re-flatten.
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        CompositionNode& node = *nodes_[i];
        if (node.kind != NodeKind::Scene || !node.child) {
            continue;
        }
        node.child->update(time);
        const Scene& cs = node.child->scene();
        if (i >= ranges_.size() || cs.meshVersion != ranges_[i].childMeshVersion ||
            cs.entities.size() != ranges_[i].childEntityCount ||
            cs.particles.size() != ranges_[i].childParticleCount) {
            dirty_ = true;
        }
    }
    if (dirty_) {
        rebuild();
    }

    const auto dt = static_cast<float>(time.deltaTime);
    if (rootRotationSpeed_ != nullptr) {
        rootAngle_ += rootRotationSpeed_->value() * dt;
    }
    if (cameraOrbitSpeed_ != nullptr) {
        cameraAngle_ += cameraOrbitSpeed_->value() * dt;
    }
    applyParameters();
}

void Composition::applyParameters() {
    // Root: uniform scale about the bounds centre and rotation about +Y, as a Transform so it
    // composes with the node and rest transforms.
    const float rootScale = (rootScale_ != nullptr ? rootScale_->value() : 1.0f) +
                            (rootImpulse_ != nullptr ? rootImpulse_->value() : 0.0f);
    Transform root;
    root.rotation = glm::angleAxis(rootAngle_, glm::vec3(0.0f, 1.0f, 0.0f));
    root.scale = glm::vec3(rootScale);
    root.position = center_ - root.rotation * (center_ * rootScale);

    for (std::size_t i = 0; i < nodes_.size() && i < ranges_.size(); ++i) {
        CompositionNode& node = *nodes_[i];
        NodeRange& range = ranges_[i];

        // Keep the authored values in step with the parameter bases so saving and re-attaching
        // see what the user set.
        if (node.positionParam != nullptr) {
            node.transform.position = node.positionParam->base();
        }
        if (node.rotationParam != nullptr) {
            node.transform.rotation = quatFromEulerDegrees(node.rotationParam->base());
        }
        if (node.scaleParam != nullptr) {
            node.transform.scale = node.scaleParam->base();
        }
        if (node.visibleParam != nullptr) {
            node.visible = node.visibleParam->base();
        }
        if (node.emissiveParam != nullptr) {
            node.emissiveBoost = node.emissiveParam->base();
        }
        if (node.roughnessParam != nullptr) {
            node.roughnessScale = node.roughnessParam->base();
        }

        const Transform nodeT = nodeWorldTransform(node);
        const bool visible = nodeVisible(node);
        const float emissiveBoost =
            node.emissiveParam != nullptr ? node.emissiveParam->value() : node.emissiveBoost;
        const float roughnessScale =
            node.roughnessParam != nullptr ? node.roughnessParam->value() : node.roughnessScale;
        const Transform full = compose(root, nodeT);

        const Scene* child = (node.kind == NodeKind::Scene && node.child) ? &node.child->scene() : nullptr;
        if (child != nullptr && child->entities.size() == range.entityCount) {
            // Nested parameters moved the child's entities: take its current state as the rest.
            for (std::size_t k = 0; k < range.entityCount; ++k) {
                range.restTransforms[k] = child->entities[k].transform;
                range.restEmissive[k] = child->entities[k].material.emissiveIntensity;
                range.restRoughness[k] = child->entities[k].material.roughness;
            }
        }
        for (std::size_t k = 0; k < range.entityCount && range.firstEntity + k < scene_.entities.size();
             ++k) {
            Entity& e = scene_.entities[range.firstEntity + k];
            e.transform = compose(full, range.restTransforms[k]);
            e.visible = visible && (child == nullptr || child->entities[k].visible);
            if (e.style == MeshStyle::Lit) {
                e.material.emissiveIntensity = range.restEmissive[k] * emissiveBoost;
                e.material.roughness = std::clamp(range.restRoughness[k] * roughnessScale, 0.0f, 1.0f);
            }
        }

        if (node.kind == NodeKind::Particles && range.particleIndex >= 0 &&
            static_cast<std::size_t>(range.particleIndex) < scene_.particles.size()) {
            ParticleSystem& ps = scene_.particles[static_cast<std::size_t>(range.particleIndex)];
            ps = node.particleRest;
            applyParticleParameters(node.particleParams, node.particleRest, ps);
            const float scale = lengthScale(full);
            ps.position = transformPoint(full, ps.position);
            ps.attractorPosition = transformPoint(full, ps.attractorPosition);
            ps.extent *= scale;
            ps.sizeStart *= scale;
            ps.sizeEnd *= scale;
            ps.enabled = ps.enabled && visible;
        } else if (child != nullptr && child->particles.size() == range.particleCount) {
            const float scale = lengthScale(full);
            for (std::size_t k = 0; k < range.particleCount; ++k) {
                const ParticleSystem& src = child->particles[k];
                ParticleSystem& ps = scene_.particles[range.firstParticle + k];
                ps = src;
                ps.position = transformPoint(full, src.position);
                ps.attractorPosition = transformPoint(full, src.attractorPosition);
                ps.extent = src.extent * scale;
                ps.sizeStart = src.sizeStart * scale;
                ps.sizeEnd = src.sizeEnd * scale;
                ps.enabled = src.enabled && visible;
            }
        }
    }

    // Orbit camera around the bounds centre (distance fitted to the radius by default).
    const float fit = fitDistance();
    const float distance =
        cameraDistance_ != nullptr ? cameraDistance_->value() : cameraDistanceSetting_.value_or(fit);
    const float height = cameraHeight_ != nullptr
                             ? cameraHeight_->value()
                             : cameraHeightSetting_.value_or(center_.y + radius_ * 0.35f);
    const float fov = cameraFov_ != nullptr ? cameraFov_->value() : cameraFovSetting_;
    scene_.camera.position =
        center_ + glm::vec3(std::sin(cameraAngle_) * distance, 0.0f, std::cos(cameraAngle_) * distance);
    scene_.camera.position.y = height;
    scene_.camera.target = center_;
    scene_.camera.fovYRadians = glm::radians(fov);
    scene_.camera.nearPlane = std::max(radius_ * 0.01f, 0.01f);
    scene_.camera.farPlane = std::max(radius_ * 50.0f, 100.0f);

    if (brightness_ != nullptr) {
        scene_.environment.brightness = brightness_->value();
    }
    if (gridIntensity_ != nullptr) {
        scene_.environment.gridIntensity = gridIntensity_->value();
    }
    scene_.environment.environmentIntensity =
        envIntensity_ != nullptr ? envIntensity_->value() : envIntensitySetting_;
    if (envRotation_ != nullptr) {
        scene_.environment.environmentRotation = envRotation_->value();
    }
}

// ---- files -------------------------------------------------------------------------------------

void Composition::setEnvironmentMap(const std::filesystem::path& path) {
    environmentPath_ = path;
    dirty_ = true;
}

nlohmann::json Composition::toJson() const {
    json j;
    j["format"] = kFormatName;
    j["version"] = kFormatVersion;
    j["name"] = name_;

    json camera = json::object();
    if (cameraDistance_ != nullptr) {
        camera["distance"] = cameraDistance_->base();
    } else if (cameraDistanceSetting_) {
        camera["distance"] = *cameraDistanceSetting_;
    }
    if (cameraHeight_ != nullptr) {
        camera["height"] = cameraHeight_->base();
    } else if (cameraHeightSetting_) {
        camera["height"] = *cameraHeightSetting_;
    }
    camera["orbitSpeed"] =
        cameraOrbitSpeed_ != nullptr ? cameraOrbitSpeed_->base() : cameraOrbitSpeedSetting_;
    camera["fov"] = cameraFov_ != nullptr ? cameraFov_->base() : cameraFovSetting_;
    j["camera"] = std::move(camera);

    json environment = json::object();
    if (!environmentPath_.empty()) {
        environment["map"] = environmentPath_.generic_string();
    }
    environment["intensity"] = envIntensity_ != nullptr ? envIntensity_->base() : envIntensitySetting_;
    j["environment"] = std::move(environment);

    json nodes = json::array();
    for (const auto& nodePtr : nodes_) {
        const CompositionNode& node = *nodePtr;
        json n;
        n["name"] = node.name;
        n["kind"] = nodeKindName(node.kind);
        if (!node.asset.empty()) {
            n["asset"] = node.asset.generic_string();
        }
        if (!node.parent.empty()) {
            n["parent"] = node.parent;
        }
        n["position"] = vecToJson(node.transform.position);
        n["rotation"] = vecToJson(node.rotationParam != nullptr ? node.rotationParam->base()
                                                                : eulerDegrees(node.transform.rotation));
        n["scale"] = vecToJson(node.transform.scale);
        n["visible"] = node.visible;
        n["emissiveBoost"] = node.emissiveBoost;
        n["roughnessScale"] = node.roughnessScale;
        if (node.kind == NodeKind::Particles) {
            n["particles"] = particlesToJson(node.particles);
        }
        nodes.push_back(std::move(n));
    }
    j["nodes"] = std::move(nodes);
    return j;
}

Result<std::unique_ptr<Composition>> Composition::fromJson(const nlohmann::json& j,
                                                           assets::AssetRegistry& registry, int depth) {
    return fromJsonImpl(j, registry, depth, {}, {});
}

Result<std::unique_ptr<Composition>> Composition::fromJsonImpl(const nlohmann::json& j,
                                                               assets::AssetRegistry& registry, int depth,
                                                               std::vector<std::filesystem::path> ancestors,
                                                               std::filesystem::path sourcePath) {
    if (!j.is_object()) {
        return fail("scene file: root is not an object");
    }
    auto format = readString(j, "format", "");
    if (!format) {
        return std::unexpected(format.error());
    }
    if (*format != kFormatName) {
        return fail("not an avgen scene file (format '{}')", *format);
    }
    if (!j.contains("version") || !j.at("version").is_number_integer()) {
        return fail("scene file: missing 'version'");
    }
    if (const int version = j.at("version").get<int>(); version != kFormatVersion) {
        return fail("unsupported scene file version {} (expected {})", version, kFormatVersion);
    }
    if (depth > kMaxNestingDepth) {
        return fail("scene nests deeper than {} levels", kMaxNestingDepth);
    }
    auto name =
        readString(j, "name", sourcePath.empty() ? std::string("composition") : sourcePath.stem().string());
    if (!name) {
        return std::unexpected(name.error());
    }

    auto comp = std::make_unique<Composition>(registry, *name);
    comp->depth_ = depth;
    comp->ancestors_ = std::move(ancestors);
    comp->sourcePath_ = std::move(sourcePath);

    if (j.contains("camera")) {
        const json& c = j.at("camera");
        if (!c.is_object()) {
            return fail("'camera' must be an object");
        }
        if (c.contains("distance")) {
            auto v = readFloat(c, "distance", 0.0f);
            if (!v) {
                return std::unexpected(v.error());
            }
            if (*v > 0.0f) { // 0 (or negative) means "fit to the bounds"
                comp->cameraDistanceSetting_ = *v;
            }
        }
        if (c.contains("height")) {
            auto v = readFloat(c, "height", 0.0f);
            if (!v) {
                return std::unexpected(v.error());
            }
            comp->cameraHeightSetting_ = *v;
        }
        auto orbit = readFloat(c, "orbitSpeed", comp->cameraOrbitSpeedSetting_);
        if (!orbit) {
            return std::unexpected(orbit.error());
        }
        comp->cameraOrbitSpeedSetting_ = *orbit;
        auto fov = readFloat(c, "fov", comp->cameraFovSetting_);
        if (!fov) {
            return std::unexpected(fov.error());
        }
        comp->cameraFovSetting_ = *fov;
    }
    if (j.contains("environment")) {
        const json& e = j.at("environment");
        if (!e.is_object()) {
            return fail("'environment' must be an object");
        }
        auto map = readString(e, "map", "");
        if (!map) {
            return std::unexpected(map.error());
        }
        comp->environmentPath_ = *map;
        auto intensity = readFloat(e, "intensity", comp->envIntensitySetting_);
        if (!intensity) {
            return std::unexpected(intensity.error());
        }
        comp->envIntensitySetting_ = *intensity;
    }

    if (j.contains("nodes")) {
        const json& nodes = j.at("nodes");
        if (!nodes.is_array()) {
            return fail("'nodes' must be an array");
        }
        std::vector<std::pair<std::string, std::string>> pendingParents; // (node, parent)
        for (const json& item : nodes) {
            if (!item.is_object()) {
                return fail("scene node must be an object");
            }
            CompositionNode node;
            auto nodeName = readString(item, "name", "");
            if (!nodeName) {
                return std::unexpected(nodeName.error());
            }
            node.name = *nodeName;
            auto kindName = readString(item, "kind", "");
            if (!kindName) {
                return std::unexpected(kindName.error());
            }
            auto kind = nodeKindFromName(*kindName);
            if (!kind) {
                return fail("node '{}': {}", node.name, kind.error().message);
            }
            node.kind = *kind;
            auto asset = readString(item, "asset", "");
            if (!asset) {
                return std::unexpected(asset.error());
            }
            node.asset = *asset;
            auto parent = readString(item, "parent", "");
            if (!parent) {
                return std::unexpected(parent.error());
            }
            const std::string parentName = *parent; // applied after every node exists (forward references)
            auto position = readVec<3>(item, "position", node.transform.position);
            auto rotation = readVec<3>(item, "rotation", glm::vec3(0.0f));
            auto scale = readVec<3>(item, "scale", node.transform.scale);
            auto visible = readBool(item, "visible", true);
            auto emissive = readFloat(item, "emissiveBoost", 1.0f);
            auto roughness = readFloat(item, "roughnessScale", 1.0f);
            const Error* fieldError = nullptr;
            auto check = [&](const auto& r) {
                if (!r && fieldError == nullptr) {
                    fieldError = &r.error();
                }
            };
            check(position);
            check(rotation);
            check(scale);
            check(visible);
            check(emissive);
            check(roughness);
            if (fieldError != nullptr) {
                return fail("node '{}': {}", node.name, fieldError->message);
            }
            node.transform.position = *position;
            node.transform.rotation = quatFromEulerDegrees(*rotation);
            node.transform.scale = *scale;
            node.visible = *visible;
            node.emissiveBoost = *emissive;
            node.roughnessScale = *roughness;
            if (item.contains("particles")) {
                auto particles = particlesFromJson(item.at("particles"));
                if (!particles) {
                    return fail("node '{}': {}", node.name, particles.error().message);
                }
                node.particles = std::move(*particles);
            }

            const std::string label = node.name.empty() ? std::string(nodeKindName(node.kind)) : node.name;
            const NodeKind nodeKind = node.kind;
            const std::filesystem::path assetPath = node.asset;
            auto added = comp->addNode(std::move(node));
            if (added) {
                if (!parentName.empty()) {
                    pendingParents.emplace_back((*added)->name, parentName);
                }
            } else {
                // A nested scene file that exists but fails is a structural error (cycle, depth,
                // malformed); a missing asset only costs its node.
                std::error_code ec;
                if (nodeKind == NodeKind::Scene && !assetPath.empty() &&
                    std::filesystem::exists(registry.resolve(assetPath), ec)) {
                    return fail("node '{}': {}", label, added.error().message);
                }
                log::warn("scene '{}': node '{}' skipped: {}", comp->name_, label, added.error().message);
            }
        }
        for (const auto& [child, parentName] : pendingParents) {
            if (parentName == child || comp->wouldCycle(child, parentName)) {
                return fail("node '{}': parent '{}' forms a cycle", child, parentName);
            }
            if (comp->findNode(parentName) == nullptr) {
                log::warn("scene '{}': node '{}' parent '{}' not found (treated as root)", comp->name_, child,
                          parentName);
            }
            comp->findNode(child)->parent = parentName;
        }
    }
    return comp;
}

Result<std::unique_ptr<Composition>> Composition::loadNested(const std::filesystem::path& path,
                                                             assets::AssetRegistry& registry, int depth,
                                                             std::vector<std::filesystem::path> ancestors) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) {
        return fail("scene file not found: '{}'", path.string());
    }
    std::ifstream in(path);
    if (!in) {
        return fail("cannot open scene file '{}'", path.string());
    }
    const json j = json::parse(in, nullptr, /*allow_exceptions*/ false);
    if (j.is_discarded()) {
        return fail("scene file '{}': invalid JSON", path.string());
    }
    auto comp = fromJsonImpl(j, registry, depth, std::move(ancestors), path);
    if (!comp) {
        return fail("scene file '{}': {}", path.filename().string(), comp.error().message);
    }
    log::info("loaded scene '{}': {} node(s)", path.filename().string(), (*comp)->nodeCount());
    return comp;
}

Result<std::unique_ptr<Composition>> Composition::loadFile(const std::filesystem::path& path,
                                                           assets::AssetRegistry& registry, int depth) {
    return loadNested(registry.resolve(path), registry, depth, {});
}

Result<void> Composition::saveFile(const std::filesystem::path& path) const {
    std::ofstream out(path);
    if (!out) {
        return fail("cannot write scene file '{}'", path.string());
    }
    out << toJson().dump(2) << '\n';
    if (!out) {
        return fail("failed writing scene file '{}'", path.string());
    }
    return {};
}

} // namespace avgen::scene
