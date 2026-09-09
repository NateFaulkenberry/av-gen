// Effectors (ADR-025): the CPU reference of the per-frame field x operation pass.
//
// Exact semantics (shaders/points.wgsl transliterates this). For a point with object-space
// position p and an effector e over field F (strength k = e.strength):
//   sample position : F.space == World ? objectToWorld * p : p
//   samples         : s = sampleScalar(F), v = sampleVector(F), c = sampleColor(F) (at that position)
//   back to object  : for World fields v is multiplied by inverse(mat3(objectToWorld)); Local
//                     fields and the effector's own `axis` are already in object space.
// Each op has a raw value R and a natural result N from the existing value E:
//   PositionOffset : R = v k (scalar fields: s k axis)        N = E + R
//   Scale          : R = s k scaleAxis                          N = E * (1 + R)
//   Rotation       : axis a = normalize(v) for vector fields (skipped when |v| == 0), else
//                    e.axis; angle = s k (s = |v| for vector fields); N = angleAxis(angle, a) * E
//   Velocity       : R = v k                                    N = E + R
//   Color          : R = c.rgb                                  N.rgb = mix(E.rgb, c.rgb, c.a k)
//   Emission       : R = s k (broadcast)                        N = E * (1 + R)
//   Density        : R = s k                                    N = E * R
//   Attribute      : R = (s | v | c) k by the field's type      N = E + R
// Blend: Add -> N; Multiply -> E * R; Replace -> R; Min -> min(E, N); Max -> max(E, N);
// Mix -> mix(E, N, weight). Rotation: Add/Multiply/Min/Max -> N; Replace -> angleAxis(angle, a);
// Mix -> slerp(E, N, weight). Colour alpha is never written (records carry the id there).
// Disabled effectors, effectors whose field is missing or disabled, and (on records) Velocity and
// Attribute ops are skipped and not counted.

#include "spatial/effector.hpp"

#include "spatial/detail.hpp"

#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

namespace avgen::spatial {

using nlohmann::json;

namespace {

glm::quat toQuat(const glm::vec4& v) {
    return glm::quat(v.w, v.x, v.y, v.z);
}

glm::vec4 fromQuat(const glm::quat& q) {
    return glm::vec4(q.x, q.y, q.z, q.w);
}

template <typename T>
T blendValue(EffectorBlend blend, const T& existing, const T& natural, const T& raw, float weight) {
    switch (blend) {
    case EffectorBlend::Add:
        return natural;
    case EffectorBlend::Multiply:
        return existing * raw;
    case EffectorBlend::Replace:
        return raw;
    case EffectorBlend::Min:
        return glm::min(existing, natural);
    case EffectorBlend::Max:
        return glm::max(existing, natural);
    case EffectorBlend::Mix:
        return glm::mix(existing, natural, weight);
    }
    return natural;
}

glm::vec4 blendRotation(EffectorBlend blend, const glm::vec4& existing, float angle, const glm::vec3& axis, float weight) {
    const glm::quat e = toQuat(existing);
    const glm::quat delta = glm::angleAxis(angle, axis);
    const glm::quat natural = glm::normalize(delta * e);
    switch (blend) {
    case EffectorBlend::Replace:
        return fromQuat(glm::normalize(delta));
    case EffectorBlend::Mix:
        return fromQuat(glm::normalize(glm::slerp(e, natural, weight)));
    default:
        return fromQuat(natural);
    }
}

// One sample of the field for the effector at object-space position p.
struct Sample {
    float s = 0.0f;
    glm::vec3 v{0.0f};
    glm::vec4 c{0.0f};
};

struct SampleContext {
    const FieldSet& fields;
    const FieldSpec& field;
    double time;
    glm::mat4 objectToWorld;
    glm::mat3 worldToObject; // inverse(mat3(objectToWorld))
    bool world;
};

Sample sampleAt(const SampleContext& ctx, const glm::vec3& p) {
    const glm::vec3 sp = ctx.world ? glm::vec3(ctx.objectToWorld * glm::vec4(p, 1.0f)) : p;
    Sample out;
    out.s = sampleScalar(ctx.field, sp, ctx.time, &ctx.fields);
    out.v = sampleVector(ctx.field, sp, ctx.time, &ctx.fields);
    if (ctx.world) {
        out.v = ctx.worldToObject * out.v;
    }
    out.c = sampleColor(ctx.field, sp, ctx.time, &ctx.fields);
    return out;
}

// Applies one effector through an accessor with position/rotation/scale/color/emissive/density
// getters and setters (the cloud and the record array share this).
template <typename Access>
void applyOne(const Effector& e, const SampleContext& ctx, std::size_t count, Access& access) {
    const float k = e.strength;
    const bool vectorField = ctx.field.type() == FieldType::Vector;
    for (std::size_t i = 0; i < count; ++i) {
        const glm::vec3 p = access.position(i);
        const Sample smp = sampleAt(ctx, p);
        switch (e.op) {
        case EffectorOp::PositionOffset: {
            const glm::vec3 raw = vectorField ? smp.v * k : e.axis * (smp.s * k);
            access.setPosition(i, blendValue(e.blend, p, p + raw, raw, e.weight));
            break;
        }
        case EffectorOp::Scale: {
            const glm::vec3 existing = access.scale(i);
            const glm::vec3 raw = e.scaleAxis * (smp.s * k);
            access.setScale(i, blendValue(e.blend, existing, existing * (glm::vec3(1.0f) + raw), raw, e.weight));
            break;
        }
        case EffectorOp::Rotation: {
            glm::vec3 axis = e.axis;
            if (vectorField) {
                const float len = glm::length(smp.v);
                if (len <= 1e-8f) {
                    break;
                }
                axis = smp.v / len;
            } else {
                const float len = glm::length(axis);
                if (len <= 1e-8f) {
                    break;
                }
                axis /= len;
            }
            access.setRotation(i, blendRotation(e.blend, access.rotation(i), smp.s * k, axis, e.weight));
            break;
        }
        case EffectorOp::Velocity: {
            const glm::vec3 existing = access.velocity(i);
            const glm::vec3 raw = vectorField ? smp.v * k : e.axis * (smp.s * k);
            access.setVelocity(i, blendValue(e.blend, existing, existing + raw, raw, e.weight));
            break;
        }
        case EffectorOp::Color: {
            const glm::vec3 existing = access.color(i);
            const glm::vec3 raw = glm::vec3(smp.c);
            access.setColor(i, blendValue(e.blend, existing, glm::mix(existing, raw, smp.c.a * k), raw, e.weight));
            break;
        }
        case EffectorOp::Emission: {
            const glm::vec3 existing = access.emissive(i);
            const glm::vec3 raw(smp.s * k);
            access.setEmissive(i, blendValue(e.blend, existing, existing * (glm::vec3(1.0f) + raw), raw, e.weight));
            break;
        }
        case EffectorOp::Density: {
            const float existing = access.density(i);
            const float raw = smp.s * k;
            access.setDensity(i, blendValue(e.blend, existing, existing * raw, raw, e.weight));
            break;
        }
        case EffectorOp::Attribute:
            access.attribute(i, e, ctx.field.type(), smp);
            break;
        }
    }
}

struct CloudAccess {
    PointCloud& cloud;
    std::span<glm::vec3> pos;
    std::span<glm::vec4> rot;
    std::span<glm::vec3> sc;
    std::span<glm::vec3> vel;
    std::span<glm::vec4> col;
    std::span<glm::vec3> emi;
    std::span<float> den;
    AttributeBuffer* target = nullptr;

    explicit CloudAccess(PointCloud& c)
        : cloud(c), pos(c.positions()), rot(c.rotations()), sc(c.scales()), vel(c.velocities()), col(c.colors()),
          emi(c.emissives()), den(c.densities()) {}

    glm::vec3 position(std::size_t i) const { return pos[i]; }
    void setPosition(std::size_t i, const glm::vec3& v) { pos[i] = v; }
    glm::vec4 rotation(std::size_t i) const { return rot[i]; }
    void setRotation(std::size_t i, const glm::vec4& v) { rot[i] = v; }
    glm::vec3 scale(std::size_t i) const { return sc[i]; }
    void setScale(std::size_t i, const glm::vec3& v) { sc[i] = v; }
    glm::vec3 velocity(std::size_t i) const { return vel[i]; }
    void setVelocity(std::size_t i, const glm::vec3& v) { vel[i] = v; }
    glm::vec3 color(std::size_t i) const { return glm::vec3(col[i]); }
    void setColor(std::size_t i, const glm::vec3& v) { col[i] = glm::vec4(v, col[i].a); }
    glm::vec3 emissive(std::size_t i) const { return emi[i]; }
    void setEmissive(std::size_t i, const glm::vec3& v) { emi[i] = v; }
    float density(std::size_t i) const { return den[i]; }
    void setDensity(std::size_t i, float v) { den[i] = v; }
    void attribute(std::size_t i, const Effector& e, FieldType type, const Sample& smp) {
        if (target == nullptr) {
            return;
        }
        const glm::vec4 existing = readAsVec4(*target, i);
        glm::vec4 raw(0.0f);
        switch (type) {
        case FieldType::Scalar:
            raw = glm::vec4(smp.s * e.strength);
            break;
        case FieldType::Vector:
            raw = glm::vec4(smp.v * e.strength, 0.0f);
            break;
        case FieldType::Color:
            raw = smp.c * e.strength;
            break;
        }
        writeFromVec4(*target, i, blendValue(e.blend, existing, existing + raw, raw, e.weight));
    }
};

struct RecordAccess {
    std::span<InstanceRecord> records;

    glm::vec3 position(std::size_t i) const { return glm::vec3(records[i].position); }
    void setPosition(std::size_t i, const glm::vec3& v) { records[i].position = glm::vec4(v, records[i].position.w); }
    glm::vec4 rotation(std::size_t i) const { return records[i].rotation; }
    void setRotation(std::size_t i, const glm::vec4& v) { records[i].rotation = v; }
    glm::vec3 scale(std::size_t i) const { return glm::vec3(records[i].scale); }
    void setScale(std::size_t i, const glm::vec3& v) { records[i].scale = glm::vec4(v, records[i].scale.w); }
    glm::vec3 velocity(std::size_t) const { return glm::vec3(0.0f); }
    void setVelocity(std::size_t, const glm::vec3&) {}
    glm::vec3 color(std::size_t i) const { return glm::vec3(records[i].color); }
    void setColor(std::size_t i, const glm::vec3& v) { records[i].color = glm::vec4(v, records[i].color.a); }
    glm::vec3 emissive(std::size_t i) const { return glm::vec3(records[i].emissive); }
    void setEmissive(std::size_t i, const glm::vec3& v) { records[i].emissive = glm::vec4(v, records[i].emissive.a); }
    float density(std::size_t i) const { return records[i].position.w; }
    void setDensity(std::size_t i, float v) { records[i].position.w = v; }
    void attribute(std::size_t, const Effector&, FieldType, const Sample&) {}
};

SampleContext makeContext(const FieldSet& fields, const FieldSpec& field, double time, const glm::mat4& objectToWorld) {
    return SampleContext{fields, field, time, objectToWorld, glm::inverse(glm::mat3(objectToWorld)),
                         field.space == FieldSpace::World};
}

AttributeType attributeTypeFor(FieldType type) {
    switch (type) {
    case FieldType::Vector:
        return AttributeType::Vec3;
    case FieldType::Color:
        return AttributeType::Color;
    case FieldType::Scalar:
        break;
    }
    return AttributeType::Float;
}

} // namespace

// ================================================================================================
// Names
// ================================================================================================

const char* effectorOpName(EffectorOp op) {
    switch (op) {
    case EffectorOp::PositionOffset:
        return "positionOffset";
    case EffectorOp::Scale:
        return "scale";
    case EffectorOp::Rotation:
        return "rotation";
    case EffectorOp::Velocity:
        return "velocity";
    case EffectorOp::Color:
        return "color";
    case EffectorOp::Emission:
        return "emission";
    case EffectorOp::Density:
        return "density";
    case EffectorOp::Attribute:
        return "attribute";
    }
    return "positionOffset";
}

std::optional<EffectorOp> effectorOpFromName(std::string_view name) {
    for (const auto op : {EffectorOp::PositionOffset, EffectorOp::Scale, EffectorOp::Rotation, EffectorOp::Velocity,
                          EffectorOp::Color, EffectorOp::Emission, EffectorOp::Density, EffectorOp::Attribute}) {
        if (name == effectorOpName(op)) {
            return op;
        }
    }
    return std::nullopt;
}

const char* effectorBlendName(EffectorBlend blend) {
    switch (blend) {
    case EffectorBlend::Add:
        return "add";
    case EffectorBlend::Multiply:
        return "multiply";
    case EffectorBlend::Replace:
        return "replace";
    case EffectorBlend::Min:
        return "min";
    case EffectorBlend::Max:
        return "max";
    case EffectorBlend::Mix:
        return "mix";
    }
    return "add";
}

std::optional<EffectorBlend> effectorBlendFromName(std::string_view name) {
    for (const auto b : {EffectorBlend::Add, EffectorBlend::Multiply, EffectorBlend::Replace, EffectorBlend::Min,
                         EffectorBlend::Max, EffectorBlend::Mix}) {
        if (name == effectorBlendName(b)) {
            return b;
        }
    }
    return std::nullopt;
}

// ================================================================================================
// Effector data
// ================================================================================================

json Effector::toJson() const {
    json j = json::object();
    j["field"] = field;
    j["op"] = effectorOpName(op);
    j["blend"] = effectorBlendName(blend);
    j["enabled"] = enabled;
    j["strength"] = strength;
    j["weight"] = weight;
    j["axis"] = detail::vecToJson(axis);
    j["scaleAxis"] = detail::vecToJson(scaleAxis);
    j["target"] = target;
    return j;
}

Result<Effector> Effector::fromJson(const json& j) {
    if (!j.is_object()) {
        return fail("effector must be a JSON object");
    }
    Effector e;
    AVGEN_SPATIAL_READ(e.field, "field", detail::readString);
    AVGEN_SPATIAL_READ_ENUM(e.op, "op", &effectorOpFromName, "effector op");
    AVGEN_SPATIAL_READ_ENUM(e.blend, "blend", &effectorBlendFromName, "effector blend");
    AVGEN_SPATIAL_READ(e.enabled, "enabled", detail::readBool);
    AVGEN_SPATIAL_READ(e.strength, "strength", detail::readFloat);
    AVGEN_SPATIAL_READ(e.weight, "weight", detail::readFloat);
    AVGEN_SPATIAL_READ(e.axis, "axis", detail::readVec3);
    AVGEN_SPATIAL_READ(e.scaleAxis, "scaleAxis", detail::readVec3);
    AVGEN_SPATIAL_READ(e.target, "target", detail::readString);
    if (e.field.empty()) {
        return fail("effector needs a 'field'");
    }
    if (e.op == EffectorOp::Attribute && e.target.empty()) {
        return fail("attribute effector needs a 'target'");
    }
    return e;
}

std::uint64_t Effector::structuralHash() const {
    detail::Fnv h;
    h.str(field);
    h.u8(static_cast<std::uint8_t>(op));
    h.u8(static_cast<std::uint8_t>(blend));
    h.boolean(enabled);
    h.f32(strength);
    h.f32(weight);
    h.v3(axis);
    h.v3(scaleAxis);
    h.str(target);
    return h.value();
}

// ================================================================================================
// Application
// ================================================================================================

int applyEffectors(PointCloud& cloud, std::span<const Effector> effectors, const FieldSet& fields, double time,
                   const glm::mat4& objectToWorld) {
    int applied = 0;
    cloud.ensureCore();
    for (const Effector& e : effectors) {
        if (!e.enabled) {
            continue;
        }
        const FieldSpec* field = fields.find(e.field);
        if (field == nullptr || !field->enabled) {
            continue;
        }
        if (e.op == EffectorOp::Attribute) {
            if (e.target.empty()) {
                continue;
            }
            auto added = cloud.attributes.add(e.target, attributeTypeFor(field->type()));
            if (!added) {
                continue; // type clash: leave the column alone
            }
        }
        CloudAccess access(cloud);
        if (e.op == EffectorOp::Attribute) {
            access.target = cloud.attributes.find(e.target);
        }
        const SampleContext ctx = makeContext(fields, *field, time, objectToWorld);
        applyOne(e, ctx, cloud.count(), access);
        ++applied;
    }
    return applied;
}

int applyEffectorsToRecords(std::span<InstanceRecord> records, std::span<const Effector> effectors, const FieldSet& fields,
                            double time, const glm::mat4& objectToWorld) {
    int applied = 0;
    for (const Effector& e : effectors) {
        if (!e.enabled || e.op == EffectorOp::Velocity || e.op == EffectorOp::Attribute) {
            continue;
        }
        const FieldSpec* field = fields.find(e.field);
        if (field == nullptr || !field->enabled) {
            continue;
        }
        RecordAccess access{records};
        const SampleContext ctx = makeContext(fields, *field, time, objectToWorld);
        applyOne(e, ctx, records.size(), access);
        ++applied;
    }
    return applied;
}

EffectorGpu packEffector(const Effector& effector, const FieldSet& fields) {
    EffectorGpu g{};
    g.op = static_cast<std::uint32_t>(effector.op);
    g.blend = static_cast<std::uint32_t>(effector.blend);
    const FieldSpec* field = fields.find(effector.field);
    g.fieldSlot = (effector.enabled && field != nullptr && field->enabled) ? fields.indexOf(effector.field) : -1;
    g.strength = effector.strength;
    g.axisWeight = glm::vec4(effector.axis, effector.weight);
    g.scaleAxisPad = glm::vec4(effector.scaleAxis, 0.0f);
    return g;
}

} // namespace avgen::spatial
