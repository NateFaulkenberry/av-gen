// Procedural materials (ADR-030, layers and geometric inputs in ADR-036): the CPU reference of
// the material op interpreter, validation, structural hashing, JSON and GPU packing.
// `shaders/material.wgsl` is a transliteration of `evaluateMaterialProgram`; the exact per-op
// formulas are in docs/procedural-materials.md.
//
// Conventions chosen here (the header fixes the op semantics; these are the remaining choices):
//
// * Input layouts: positions (x, y, z, 1); normal (x, y, z, 0); uv (u, v, 0, 0); scalar inputs
//   (objectId, instanceIndex, instanceId, time, depth, and every ADR-036 geometric scalar)
//   broadcast to all four components; vec4 inputs (instanceRandom, instanceColor,
//   instanceEmissive, audio, audioBands, beatPhase) as-is; viewDirection (x, y, z, 0);
//   triplanarWeights (wx, wy, wz, 0).
// * Fresnel uses the context's normal and viewDirection as given (both are expected to be unit
//   vectors; the shader passes normalised N and V).
// * Ramp mixes its three stops linearly (all four components), t = clamp(a.x, 0, 1).
// * Remap: when k.y == k.x the normalised value is taken as 0 (out = k.z) instead of dividing by
//   zero; clamping (f > 0.5) clamps to [min(k.z, k.w), max(k.z, k.w)].
// * Smoothstep with k.x == k.y degenerates to step(k.x, a). Threshold is step(f, a): 1 when
//   a >= f, else 0, per component.
// * HueShift and Saturate keep a.w; Palette writes w = 1.
// * Field: without a sampler the op writes zeros; the sampler broadcasts scalar fields itself.
// * Ops with out-of-range register indices (a program that failed validation) are skipped.
// * Outputs: baseColor = max(reg.rgb, 0); metallic / roughness / opacity / occlusion =
//   clamp(reg.x, 0, 1); emission = reg.rgb * emissionIntensity (not clamped); normal =
//   normalize(reg.xyz) (a zero-length register keeps (0, 0, 1)); height = reg.x (unclamped).
// * Layers share the register file with the base and with each other, run in array order, and are
//   composited with heightBlendWeight(); a layer's mask is clamped to [0, 1] and the running
//   height becomes mix(runningHeight, layerHeight, t).

#include "scene/material_program.hpp"

#include "core/color.hpp"
#include "core/noise.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <fstream>
#include <utility>

namespace avgen::scene {

using nlohmann::json;

namespace {

// ---- names ---------------------------------------------------------------------------------------

struct OpKindName {
    MaterialOpKind kind;
    const char* name;
};
constexpr OpKindName kOpKindNames[] = {
    {MaterialOpKind::Input, "input"},
    {MaterialOpKind::Constant, "constant"},
    {MaterialOpKind::Gradient, "gradient"},
    {MaterialOpKind::Noise, "noise"},
    {MaterialOpKind::Voronoi, "voronoi"},
    {MaterialOpKind::Fresnel, "fresnel"},
    {MaterialOpKind::Ramp, "ramp"},
    {MaterialOpKind::Remap, "remap"},
    {MaterialOpKind::Multiply, "multiply"},
    {MaterialOpKind::Add, "add"},
    {MaterialOpKind::Mix, "mix"},
    {MaterialOpKind::MixBy, "mixBy"},
    {MaterialOpKind::Power, "power"},
    {MaterialOpKind::Smoothstep, "smoothstep"},
    {MaterialOpKind::Threshold, "threshold"},
    {MaterialOpKind::HueShift, "hueShift"},
    {MaterialOpKind::Saturate, "saturate"},
    {MaterialOpKind::Palette, "palette"},
    {MaterialOpKind::Field, "field"},
    {MaterialOpKind::Triplanar, "triplanar"},
    {MaterialOpKind::WorldProject, "worldProject"},
    {MaterialOpKind::ObjectProject, "objectProject"},
    {MaterialOpKind::HeightBlend, "heightBlend"},
    {MaterialOpKind::DetailNormal, "detailNormal"},
    {MaterialOpKind::CurvatureMask, "curvatureMask"},
    {MaterialOpKind::EdgeWear, "edgeWear"},
    {MaterialOpKind::DecalBox, "decalBox"},
    {MaterialOpKind::Anisotropy, "anisotropy"},
    {MaterialOpKind::RoughnessFilter, "roughnessFilter"},
    {MaterialOpKind::MicroDetail, "microDetail"},
};

struct InputName {
    MaterialInput input;
    const char* name;
};
constexpr InputName kInputNames[] = {
    {MaterialInput::WorldPosition, "worldPosition"},
    {MaterialInput::LocalPosition, "localPosition"},
    {MaterialInput::Normal, "normal"},
    {MaterialInput::Uv, "uv"},
    {MaterialInput::ObjectId, "objectId"},
    {MaterialInput::InstanceIndex, "instanceIndex"},
    {MaterialInput::InstanceId, "instanceId"},
    {MaterialInput::InstanceRandom, "instanceRandom"},
    {MaterialInput::InstanceColor, "instanceColor"},
    {MaterialInput::InstanceEmissive, "instanceEmissive"},
    {MaterialInput::Time, "time"},
    {MaterialInput::Audio, "audio"},
    {MaterialInput::AudioBands, "audioBands"},
    {MaterialInput::BeatPhase, "beatPhase"},
    {MaterialInput::ViewDirection, "viewDirection"},
    {MaterialInput::Depth, "depth"},
    {MaterialInput::Curvature, "curvature"},
    {MaterialInput::Convexity, "convexity"},
    {MaterialInput::Concavity, "concavity"},
    {MaterialInput::Cavity, "cavity"},
    {MaterialInput::Occlusion, "occlusion"},
    {MaterialInput::Height, "height"},
    {MaterialInput::NormalVariance, "normalVariance"},
    {MaterialInput::ObjectPosition, "objectPosition"},
    {MaterialInput::TriplanarWeights, "triplanarWeights"},
    {MaterialInput::CameraDistance, "cameraDistance"},
    {MaterialInput::MaterialId, "materialId"},
    {MaterialInput::Footprint, "footprint"},
};

// ---- structural hashing (FNV-1a over the bit patterns, as in procedural.cpp) ----------------------

class StructHash {
public:
    void u32(std::uint32_t v) {
        for (int i = 0; i < 4; ++i) {
            h_ = (h_ ^ ((v >> (8 * i)) & 0xFFu)) * 0x100000001b3ULL;
        }
    }
    void i32(int v) { u32(std::bit_cast<std::uint32_t>(v)); }
    void f32(float v) { u32(std::bit_cast<std::uint32_t>(v == 0.0f ? 0.0f : v)); } // -0 == +0
    void boolean(bool v) { u32(v ? 1u : 0u); }
    void v4(const glm::vec4& v) {
        f32(v.x);
        f32(v.y);
        f32(v.z);
        f32(v.w);
    }
    void str(std::string_view s) {
        u32(static_cast<std::uint32_t>(s.size()));
        for (const char c : s) {
            u32(static_cast<std::uint32_t>(static_cast<unsigned char>(c)));
        }
    }
    [[nodiscard]] std::uint64_t value() const { return h_; }

private:
    std::uint64_t h_ = 0xcbf29ce484222325ULL;
};

// ---- JSON helpers --------------------------------------------------------------------------------

json vec4ToJson(const glm::vec4& v) {
    return json::array({v.x, v.y, v.z, v.w});
}

Result<glm::vec4> readVec4(const json& j, const char* key, const glm::vec4& def) {
    if (!j.contains(key)) {
        return def;
    }
    const json& a = j.at(key);
    if (!a.is_array() || a.size() != 4) {
        return fail("'{}' must be an array of 4 numbers", key);
    }
    glm::vec4 out{};
    for (std::size_t i = 0; i < 4; ++i) {
        const json& e = a.at(i);
        if (!e.is_number()) {
            return fail("'{}' must be an array of 4 numbers", key);
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

Result<std::uint32_t> readU32(const json& j, const char* key, std::uint32_t def) {
    if (!j.contains(key)) {
        return def;
    }
    const json& v = j.at(key);
    if (!v.is_number_unsigned() && !(v.is_number_integer() && v.get<long long>() >= 0)) {
        return fail("'{}' must be a non-negative integer", key);
    }
    return v.get<std::uint32_t>();
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

// Assigns `target` from `reader(j, key, target)` or returns the error.
#define AVGEN_MAT_READ(target, key, reader)                                                             \
    do {                                                                                                \
        auto value_ = reader(j, key, target);                                                            \
        if (!value_) {                                                                                  \
            return std::unexpected(value_.error());                                                     \
        }                                                                                               \
        target = *value_;                                                                               \
    } while (false)

Result<MaterialOp> opFromJson(const json& j, std::size_t index) {
    if (!j.is_object()) {
        return fail("op {} must be an object", index);
    }
    auto kindName = readString(j, "kind", "");
    if (!kindName) {
        return std::unexpected(kindName.error());
    }
    const auto kind = materialOpKindFromName(*kindName);
    if (!kind) {
        return fail("op {}: unknown op kind '{}'", index, *kindName);
    }
    MaterialOp op;
    op.kind = *kind;
    AVGEN_MAT_READ(op.enabled, "enabled", readBool);
    AVGEN_MAT_READ(op.dst, "dst", readInt);
    AVGEN_MAT_READ(op.srcA, "srcA", readInt);
    AVGEN_MAT_READ(op.srcB, "srcB", readInt);
    AVGEN_MAT_READ(op.srcC, "srcC", readInt);
    AVGEN_MAT_READ(op.value, "value", readFloat);
    AVGEN_MAT_READ(op.constant, "constant", readVec4);
    AVGEN_MAT_READ(op.constant2, "constant2", readVec4);
    AVGEN_MAT_READ(op.constant3, "constant3", readVec4);
    AVGEN_MAT_READ(op.constant4, "constant4", readVec4);
    AVGEN_MAT_READ(op.seed, "seed", readU32);
    AVGEN_MAT_READ(op.field, "field", readString);
    if (j.contains("input")) {
        auto inputName = readString(j, "input", "");
        if (!inputName) {
            return std::unexpected(inputName.error());
        }
        const auto input = materialInputFromName(*inputName);
        if (!input) {
            return fail("op {}: unknown input '{}'", index, *inputName);
        }
        op.input = *input;
    }
    return op;
}

json opToJson(const MaterialOp& op) {
    const MaterialOp def;
    json j = json::object();
    j["kind"] = materialOpKindName(op.kind);
    if (op.enabled != def.enabled) {
        j["enabled"] = op.enabled;
    }
    if (op.dst != def.dst) {
        j["dst"] = op.dst;
    }
    if (op.srcA != def.srcA) {
        j["srcA"] = op.srcA;
    }
    if (op.srcB != def.srcB) {
        j["srcB"] = op.srcB;
    }
    if (op.srcC != def.srcC) {
        j["srcC"] = op.srcC;
    }
    if (op.value != def.value) {
        j["value"] = op.value;
    }
    if (op.constant != def.constant) {
        j["constant"] = vec4ToJson(op.constant);
    }
    if (op.constant2 != def.constant2) {
        j["constant2"] = vec4ToJson(op.constant2);
    }
    if (op.constant3 != def.constant3) {
        j["constant3"] = vec4ToJson(op.constant3);
    }
    if (op.constant4 != def.constant4) {
        j["constant4"] = vec4ToJson(op.constant4);
    }
    if (op.seed != def.seed) {
        j["seed"] = op.seed;
    }
    if (op.input != def.input) {
        j["input"] = materialInputName(op.input);
    }
    if (!op.field.empty()) {
        j["field"] = op.field;
    }
    return j;
}

Result<MaterialLayer> layerFromJson(const json& j, std::size_t index) {
    if (!j.is_object()) {
        return fail("layer {} must be an object", index);
    }
    MaterialLayer layer;
    AVGEN_MAT_READ(layer.name, "name", readString);
    AVGEN_MAT_READ(layer.enabled, "enabled", readBool);
    if (j.contains("ops")) {
        const json& arr = j.at("ops");
        if (!arr.is_array()) {
            return fail("layer {}: 'ops' must be an array", index);
        }
        for (std::size_t i = 0; i < arr.size(); ++i) {
            auto op = opFromJson(arr.at(i), i);
            if (!op) {
                return std::unexpected(op.error());
            }
            layer.ops.push_back(std::move(*op));
        }
    }
    AVGEN_MAT_READ(layer.maskRegister, "mask", readInt);
    AVGEN_MAT_READ(layer.heightRegister, "height", readInt);
    AVGEN_MAT_READ(layer.blendRange, "blendRange", readFloat);
    AVGEN_MAT_READ(layer.baseColorRegister, "baseColor", readInt);
    AVGEN_MAT_READ(layer.metallicRegister, "metallic", readInt);
    AVGEN_MAT_READ(layer.roughnessRegister, "roughness", readInt);
    AVGEN_MAT_READ(layer.emissionRegister, "emission", readInt);
    AVGEN_MAT_READ(layer.emissionIntensity, "emissionIntensity", readFloat);
    AVGEN_MAT_READ(layer.normalRegister, "normal", readInt);
    AVGEN_MAT_READ(layer.occlusionRegister, "occlusion", readInt);
    return layer;
}

json layerToJson(const MaterialLayer& layer) {
    const MaterialLayer def;
    json j = json::object();
    if (!layer.name.empty()) {
        j["name"] = layer.name;
    }
    if (layer.enabled != def.enabled) {
        j["enabled"] = layer.enabled;
    }
    json arr = json::array();
    for (const MaterialOp& op : layer.ops) {
        arr.push_back(opToJson(op));
    }
    j["ops"] = std::move(arr);
    j["mask"] = layer.maskRegister;
    j["height"] = layer.heightRegister;
    j["blendRange"] = layer.blendRange;
    j["baseColor"] = layer.baseColorRegister;
    j["metallic"] = layer.metallicRegister;
    j["roughness"] = layer.roughnessRegister;
    j["emission"] = layer.emissionRegister;
    j["emissionIntensity"] = layer.emissionIntensity;
    j["normal"] = layer.normalRegister;
    j["occlusion"] = layer.occlusionRegister;
    return j;
}

// ---- evaluation helpers --------------------------------------------------------------------------

float saturate1(float x) {
    return std::clamp(x, 0.0f, 1.0f);
}

glm::vec4 mix4(const glm::vec4& a, const glm::vec4& b, float t) {
    return a * (1.0f - t) + b * t;
}

float smoothstep1(float e0, float e1, float x) {
    if (e0 == e1) {
        return x < e0 ? 0.0f : 1.0f;
    }
    const float t = saturate1((x - e0) / (e1 - e0));
    return t * t * (3.0f - 2.0f * t);
}

// A decal box's per-axis falloff: 1 inside, 0 outside, `soft` wide at the boundary.
float decalFalloff(float q, float soft) {
    if (soft <= 1e-6f) {
        return q <= 1.0f ? 1.0f : 0.0f;
    }
    return smoothstep1(1.0f, 1.0f - soft, q);
}

glm::vec3 safeNormalize(const glm::vec3& v, const glm::vec3& fallback) {
    const float len2 = glm::dot(v, v);
    return len2 > 1e-12f ? v / std::sqrt(len2) : fallback;
}

glm::vec4 inputValue(MaterialInput input, const MaterialContext& ctx) {
    switch (input) {
    case MaterialInput::WorldPosition:
        return glm::vec4(ctx.worldPosition, 1.0f);
    case MaterialInput::LocalPosition:
    case MaterialInput::ObjectPosition:
        return glm::vec4(ctx.localPosition, 1.0f);
    case MaterialInput::Normal:
        return glm::vec4(ctx.normal, 0.0f);
    case MaterialInput::Uv:
        return glm::vec4(ctx.uv, 0.0f, 0.0f);
    case MaterialInput::ObjectId:
        return glm::vec4(ctx.objectId);
    case MaterialInput::InstanceIndex:
        return glm::vec4(ctx.instanceIndex);
    case MaterialInput::InstanceId:
        return glm::vec4(ctx.instanceId);
    case MaterialInput::InstanceRandom:
        return ctx.instanceRandom;
    case MaterialInput::InstanceColor:
        return ctx.instanceColor;
    case MaterialInput::InstanceEmissive:
        return ctx.instanceEmissive;
    case MaterialInput::Time:
        return glm::vec4(ctx.time);
    case MaterialInput::Audio:
        return ctx.audio;
    case MaterialInput::AudioBands:
        return ctx.audioBands;
    case MaterialInput::BeatPhase:
        return ctx.beat;
    case MaterialInput::ViewDirection:
        return glm::vec4(ctx.viewDirection, 0.0f);
    case MaterialInput::Depth:
    case MaterialInput::CameraDistance:
        return glm::vec4(ctx.depth);
    case MaterialInput::Curvature:
        return glm::vec4(ctx.curvature);
    case MaterialInput::Convexity:
        return glm::vec4(std::max(ctx.curvature, 0.0f));
    case MaterialInput::Concavity:
        return glm::vec4(std::max(-ctx.curvature, 0.0f));
    case MaterialInput::Cavity:
        return glm::vec4(ctx.cavity);
    case MaterialInput::Occlusion:
        return glm::vec4(ctx.occlusion);
    case MaterialInput::Height:
        return glm::vec4(ctx.height);
    case MaterialInput::NormalVariance:
        return glm::vec4(ctx.normalVariance);
    case MaterialInput::TriplanarWeights:
        return glm::vec4(triplanarWeights(ctx.normal, 4.0f), 0.0f);
    case MaterialInput::MaterialId:
        return glm::vec4(ctx.materialId);
    case MaterialInput::Footprint:
        return glm::vec4(ctx.footprint);
    }
    return glm::vec4(0.0f);
}

bool registerInRange(int r) {
    return r >= 0 && r < kMaterialRegisters;
}

bool outputRegisterOk(int r) {
    return r >= -1 && r < kMaterialRegisters;
}

glm::vec4 evaluateOp(const MaterialOp& op, const MaterialContext& ctx,
                     const std::array<glm::vec4, kMaterialRegisters>& regs) {
    const glm::vec4& a = regs[static_cast<std::size_t>(op.srcA)];
    const glm::vec4& b = regs[static_cast<std::size_t>(op.srcB)];
    const glm::vec4& c = regs[static_cast<std::size_t>(op.srcC)];
    const glm::vec4& k = op.constant;
    const float f = op.value;
    switch (op.kind) {
    case MaterialOpKind::Input:
        return inputValue(op.input, ctx);
    case MaterialOpKind::Constant:
        return k;
    case MaterialOpKind::Gradient:
        return glm::vec4(saturate1(glm::dot(glm::vec3(a), glm::vec3(k)) * f + k.w));
    case MaterialOpKind::Noise:
        return glm::vec4(noise::fbm3(glm::vec3(a) * f + glm::vec3(k), op.seed));
    case MaterialOpKind::Voronoi:
        return glm::vec4(noise::voronoiF1(glm::vec3(a) * f + glm::vec3(k), op.seed));
    case MaterialOpKind::Fresnel:
        return glm::vec4(std::pow(1.0f - saturate1(glm::dot(ctx.normal, ctx.viewDirection)), f));
    case MaterialOpKind::Ramp: {
        const float t = saturate1(a.x);
        return t < 0.5f ? mix4(op.constant, op.constant2, t * 2.0f)
                        : mix4(op.constant2, op.constant3, (t - 0.5f) * 2.0f);
    }
    case MaterialOpKind::Remap: {
        const float inSpan = k.y - k.x;
        const glm::vec4 u = inSpan != 0.0f ? (a - glm::vec4(k.x)) / inSpan : glm::vec4(0.0f);
        glm::vec4 out = u * (k.w - k.z) + glm::vec4(k.z);
        if (f > 0.5f) {
            out = glm::clamp(out, glm::vec4(std::min(k.z, k.w)), glm::vec4(std::max(k.z, k.w)));
        }
        return out;
    }
    case MaterialOpKind::Multiply:
        return a * b;
    case MaterialOpKind::Add:
        return a + b;
    case MaterialOpKind::Mix:
        return mix4(a, b, f);
    case MaterialOpKind::MixBy:
        return mix4(a, b, c.x);
    case MaterialOpKind::Power: {
        const glm::vec4 base = glm::max(a, glm::vec4(0.0f));
        return {std::pow(base.x, f), std::pow(base.y, f), std::pow(base.z, f), std::pow(base.w, f)};
    }
    case MaterialOpKind::Smoothstep:
        return {smoothstep1(k.x, k.y, a.x), smoothstep1(k.x, k.y, a.y), smoothstep1(k.x, k.y, a.z),
                smoothstep1(k.x, k.y, a.w)};
    case MaterialOpKind::Threshold:
        return {a.x >= f ? 1.0f : 0.0f, a.y >= f ? 1.0f : 0.0f, a.z >= f ? 1.0f : 0.0f, a.w >= f ? 1.0f : 0.0f};
    case MaterialOpKind::HueShift:
        return glm::vec4(color::hueShift(glm::vec3(a), f + b.x), a.w);
    case MaterialOpKind::Saturate:
        return glm::vec4(color::saturate(glm::vec3(a), f), a.w);
    case MaterialOpKind::Palette: {
        const color::CosinePalette palette{glm::vec3(op.constant), glm::vec3(op.constant2), glm::vec3(op.constant3),
                                           glm::vec3(op.constant4)};
        return glm::vec4(palette.sample(a.x + f), 1.0f);
    }
    case MaterialOpKind::Field:
        return ctx.fields != nullptr ? ctx.fields->sample(op.field, ctx.worldPosition) : glm::vec4(0.0f);

    // ---- ADR-036 ----
    case MaterialOpKind::Triplanar: {
        const glm::vec3 p = glm::vec3(a) * f + glm::vec3(k);
        const glm::vec3 w = triplanarWeights(ctx.normal, op.constant2.x);
        const float sx = noise::fbm3(glm::vec3(p.y, p.z, 0.0f), op.seed);
        const float sy = noise::fbm3(glm::vec3(p.z, p.x, 0.0f), op.seed);
        const float sz = noise::fbm3(glm::vec3(p.x, p.y, 0.0f), op.seed);
        return glm::vec4(w.x * sx + w.y * sy + w.z * sz);
    }
    case MaterialOpKind::WorldProject:
        return glm::vec4(ctx.worldPosition * f + glm::vec3(k), 1.0f);
    case MaterialOpKind::ObjectProject:
        return glm::vec4(ctx.localPosition * f + glm::vec3(k), 1.0f);
    case MaterialOpKind::HeightBlend:
        return glm::vec4(heightBlendWeight(a.x, b.x, c.x, f));
    case MaterialOpKind::DetailNormal:
        return glm::vec4(reorientNormal(glm::vec3(a), glm::vec3(b)), 0.0f);
    case MaterialOpKind::CurvatureMask:
        return glm::vec4(smoothstep1(k.x, k.y, ctx.curvature * f));
    case MaterialOpKind::EdgeWear: {
        const float edge = saturate1(std::max(ctx.curvature, 0.0f) * f);
        const float nz = noise::fbm3(ctx.worldPosition * k.x + glm::vec3(k.y, k.z, k.w), op.seed);
        const float influence = saturate1(op.constant2.x);
        const float v = edge * (1.0f - influence + influence * nz);
        return glm::vec4(smoothstep1(op.constant2.y, op.constant2.z, v));
    }
    case MaterialOpKind::DecalBox: {
        const glm::vec3 half = glm::max(glm::abs(glm::vec3(op.constant2)), glm::vec3(1e-4f));
        const glm::vec3 q = (ctx.worldPosition - glm::vec3(k)) / half;
        const float soft = saturate1(f);
        const float mask =
            decalFalloff(std::abs(q.x), soft) * decalFalloff(std::abs(q.y), soft) * decalFalloff(std::abs(q.z), soft);
        return {q.x * 0.5f + 0.5f, q.y * 0.5f + 0.5f, mask, mask};
    }
    case MaterialOpKind::Anisotropy: {
        const float roughness = std::max(a.x, 0.0f);
        const float alpha = roughness * roughness;
        const float aniso = std::clamp(f, -0.95f, 0.95f);
        const float alphaT = alpha * (1.0f + aniso);
        const float alphaB = alpha * (1.0f - aniso);
        const glm::vec3 n = ctx.normal;
        const glm::vec3 dir = glm::vec3(k);
        const glm::vec3 tangent = dir - n * glm::dot(n, dir);
        if (glm::dot(tangent, tangent) <= 1e-10f) {
            return glm::vec4(roughness);
        }
        const glm::vec3 t = tangent / std::sqrt(glm::dot(tangent, tangent));
        const glm::vec3 viewTangent = ctx.viewDirection - n * glm::dot(n, ctx.viewDirection);
        float cos2 = 0.5f;
        if (glm::dot(viewTangent, viewTangent) > 1e-10f) {
            const float cosine = glm::dot(viewTangent / std::sqrt(glm::dot(viewTangent, viewTangent)), t);
            cos2 = saturate1(cosine * cosine);
        }
        const float alphaEff = std::sqrt(alphaT * alphaT * cos2 + alphaB * alphaB * (1.0f - cos2));
        return glm::vec4(std::sqrt(alphaEff));
    }
    case MaterialOpKind::RoughnessFilter: {
        const float roughness = std::max(a.x, 0.0f);
        const float alpha = roughness * roughness;
        const float kernel = std::min(2.0f * std::max(f, 0.0f) * std::max(ctx.normalVariance, 0.0f), 0.18f);
        return glm::vec4(std::sqrt(std::min(alpha + kernel, 1.0f)));
    }
    case MaterialOpKind::MicroDetail: {
        const glm::vec3 p = glm::vec3(a) * f + glm::vec3(k);
        const float fade = saturate1(1.0f - ctx.footprint * std::abs(f) * 2.0f);
        return glm::vec4(0.5f + (noise::fbm3(p, op.seed) - 0.5f) * fade);
    }
    }
    return glm::vec4(0.0f);
}

// Runs `ops` over `regs`, skipping disabled ops and (as a program that failed validation would)
// ops with out-of-range registers. `budget` is the shared kMaxMaterialOps allowance.
void runOps(const std::vector<MaterialOp>& ops, const MaterialContext& ctx,
            std::array<glm::vec4, kMaterialRegisters>& regs, int& budget) {
    for (const MaterialOp& op : ops) {
        if (!op.enabled) {
            continue;
        }
        if (budget <= 0) {
            return;
        }
        --budget;
        if (!registerInRange(op.dst) || !registerInRange(op.srcA) || !registerInRange(op.srcB) ||
            !registerInRange(op.srcC)) {
            continue;
        }
        regs[static_cast<std::size_t>(op.dst)] = evaluateOp(op, ctx, regs);
    }
}

} // namespace

// ---- names ---------------------------------------------------------------------------------------

const char* materialOpKindName(MaterialOpKind kind) {
    for (const auto& entry : kOpKindNames) {
        if (entry.kind == kind) {
            return entry.name;
        }
    }
    return "constant";
}

std::optional<MaterialOpKind> materialOpKindFromName(std::string_view name) {
    for (const auto& entry : kOpKindNames) {
        if (name == entry.name) {
            return entry.kind;
        }
    }
    return std::nullopt;
}

const char* materialInputName(MaterialInput input) {
    for (const auto& entry : kInputNames) {
        if (entry.input == input) {
            return entry.name;
        }
    }
    return "worldPosition";
}

std::optional<MaterialInput> materialInputFromName(std::string_view name) {
    for (const auto& entry : kInputNames) {
        if (name == entry.name) {
            return entry.input;
        }
    }
    return std::nullopt;
}

// ---- shared maths (ADR-036) ----------------------------------------------------------------------

float heightBlendWeight(float baseHeight, float layerHeight, float mask, float range) {
    const float m = saturate1(mask);
    const float a1 = baseHeight + (1.0f - m);
    const float a2 = layerHeight + m;
    const float top = std::max(a1, a2) - std::max(range, 1e-4f);
    const float b1 = std::max(a1 - top, 0.0f);
    const float b2 = std::max(a2 - top, 0.0f);
    return b2 / std::max(b1 + b2, 1e-6f);
}

glm::vec3 triplanarWeights(const glm::vec3& normal, float sharpness) {
    const float p = sharpness > 0.0f ? sharpness : 4.0f;
    glm::vec3 w{std::pow(std::abs(normal.x), p), std::pow(std::abs(normal.y), p), std::pow(std::abs(normal.z), p)};
    const float sum = w.x + w.y + w.z;
    if (sum <= 1e-8f) {
        return glm::vec3(1.0f / 3.0f);
    }
    return w / sum;
}

glm::vec3 reorientNormal(const glm::vec3& base, const glm::vec3& detail) {
    const glm::vec3 t = base + glm::vec3(0.0f, 0.0f, 1.0f);
    const glm::vec3 u = detail * glm::vec3(-1.0f, -1.0f, 1.0f);
    const glm::vec3 r = t * (glm::dot(t, u) / std::max(t.z, 1e-5f)) - u;
    return safeNormalize(r, glm::vec3(0.0f, 0.0f, 1.0f));
}

// ---- MaterialProgram -----------------------------------------------------------------------------

int MaterialProgram::totalOpCount() const {
    std::size_t count = ops.size();
    for (const MaterialLayer& layer : layers) {
        count += layer.ops.size();
    }
    return static_cast<int>(count);
}

Result<void> MaterialProgram::validate() const {
    if (layers.size() > static_cast<std::size_t>(kMaxMaterialLayers)) {
        return fail("material '{}': at most {} layers (got {})", name, kMaxMaterialLayers, layers.size());
    }
    if (totalOpCount() > kMaxMaterialOps) {
        return fail("material '{}': at most {} ops across the base and its layers (got {})", name, kMaxMaterialOps,
                    totalOpCount());
    }
    const auto checkOps = [this](const std::vector<MaterialOp>& list, const char* where) -> Result<void> {
        for (std::size_t i = 0; i < list.size(); ++i) {
            const MaterialOp& op = list[i];
            if (!registerInRange(op.dst) || !registerInRange(op.srcA) || !registerInRange(op.srcB) ||
                !registerInRange(op.srcC)) {
                return fail("material '{}': {}op {} ({}) uses a register outside 0..{}", name, where, i,
                            materialOpKindName(op.kind), kMaterialRegisters - 1);
            }
            if (op.kind == MaterialOpKind::Field && op.field.empty()) {
                return fail("material '{}': {}op {} (field) needs a field name", name, where, i);
            }
        }
        return Result<void>{};
    };
    if (auto ok = checkOps(ops, ""); !ok) {
        return ok;
    }
    if (!outputRegisterOk(baseColorRegister) || !outputRegisterOk(metallicRegister) ||
        !outputRegisterOk(roughnessRegister) || !outputRegisterOk(emissionRegister) ||
        !outputRegisterOk(opacityRegister) || !outputRegisterOk(normalRegister) ||
        !outputRegisterOk(occlusionRegister) || !outputRegisterOk(heightRegister)) {
        return fail("material '{}': output registers must be in -1..{}", name, kMaterialRegisters - 1);
    }
    for (std::size_t li = 0; li < layers.size(); ++li) {
        const MaterialLayer& layer = layers[li];
        if (auto ok = checkOps(layer.ops, "layer "); !ok) {
            return ok;
        }
        if (!outputRegisterOk(layer.maskRegister) || !outputRegisterOk(layer.heightRegister) ||
            !outputRegisterOk(layer.baseColorRegister) || !outputRegisterOk(layer.metallicRegister) ||
            !outputRegisterOk(layer.roughnessRegister) || !outputRegisterOk(layer.emissionRegister) ||
            !outputRegisterOk(layer.normalRegister) || !outputRegisterOk(layer.occlusionRegister)) {
            return fail("material '{}': layer {} registers must be in -1..{}", name, li, kMaterialRegisters - 1);
        }
    }
    return {};
}

std::uint64_t MaterialProgram::structuralHash() const {
    StructHash h;
    const auto hashOps = [&h](const std::vector<MaterialOp>& list) {
        h.u32(static_cast<std::uint32_t>(list.size()));
        for (const MaterialOp& op : list) {
            h.u32(static_cast<std::uint32_t>(op.kind));
            h.boolean(op.enabled);
            h.i32(op.dst);
            h.i32(op.srcA);
            h.i32(op.srcB);
            h.i32(op.srcC);
            h.f32(op.value);
            h.v4(op.constant);
            h.v4(op.constant2);
            h.v4(op.constant3);
            h.v4(op.constant4);
            h.u32(op.seed);
            h.u32(static_cast<std::uint32_t>(op.input));
            h.str(op.field);
        }
    };
    h.str(name);
    hashOps(ops);
    h.i32(baseColorRegister);
    h.i32(metallicRegister);
    h.i32(roughnessRegister);
    h.i32(emissionRegister);
    h.f32(emissionIntensity);
    h.i32(opacityRegister);
    // ADR-036 members hash after the ADR-030 ones, so the hash of a program that uses none of them
    // still changes only when the program does.
    h.i32(normalRegister);
    h.i32(occlusionRegister);
    h.i32(heightRegister);
    h.u32(static_cast<std::uint32_t>(layers.size()));
    for (const MaterialLayer& layer : layers) {
        h.str(layer.name);
        h.boolean(layer.enabled);
        hashOps(layer.ops);
        h.i32(layer.maskRegister);
        h.i32(layer.heightRegister);
        h.f32(layer.blendRange);
        h.i32(layer.baseColorRegister);
        h.i32(layer.metallicRegister);
        h.i32(layer.roughnessRegister);
        h.i32(layer.emissionRegister);
        h.f32(layer.emissionIntensity);
        h.i32(layer.normalRegister);
        h.i32(layer.occlusionRegister);
    }
    return h.value();
}

json MaterialProgram::toJson() const {
    json j = json::object();
    j["name"] = name;
    json arr = json::array();
    for (const MaterialOp& op : ops) {
        arr.push_back(opToJson(op));
    }
    j["ops"] = std::move(arr);
    j["baseColor"] = baseColorRegister;
    j["metallic"] = metallicRegister;
    j["roughness"] = roughnessRegister;
    j["emission"] = emissionRegister;
    j["emissionIntensity"] = emissionIntensity;
    j["opacity"] = opacityRegister;
    // ADR-036 members are written only when they are used, so a pre-layer program round-trips to
    // exactly the JSON it had before.
    if (normalRegister != -1) {
        j["normal"] = normalRegister;
    }
    if (occlusionRegister != -1) {
        j["occlusion"] = occlusionRegister;
    }
    if (heightRegister != -1) {
        j["height"] = heightRegister;
    }
    if (!layers.empty()) {
        json layerArray = json::array();
        for (const MaterialLayer& layer : layers) {
            layerArray.push_back(layerToJson(layer));
        }
        j["layers"] = std::move(layerArray);
    }
    return j;
}

Result<MaterialProgram> MaterialProgram::fromJson(const json& j) {
    if (!j.is_object()) {
        return fail("material program must be an object");
    }
    MaterialProgram p;
    AVGEN_MAT_READ(p.name, "name", readString);
    if (j.contains("ops")) {
        const json& arr = j.at("ops");
        if (!arr.is_array()) {
            return fail("'ops' must be an array");
        }
        for (std::size_t i = 0; i < arr.size(); ++i) {
            auto op = opFromJson(arr.at(i), i);
            if (!op) {
                return std::unexpected(op.error());
            }
            p.ops.push_back(std::move(*op));
        }
    }
    if (j.contains("layers")) {
        const json& arr = j.at("layers");
        if (!arr.is_array()) {
            return fail("'layers' must be an array");
        }
        for (std::size_t i = 0; i < arr.size(); ++i) {
            auto layer = layerFromJson(arr.at(i), i);
            if (!layer) {
                return std::unexpected(layer.error());
            }
            p.layers.push_back(std::move(*layer));
        }
    }
    AVGEN_MAT_READ(p.baseColorRegister, "baseColor", readInt);
    AVGEN_MAT_READ(p.metallicRegister, "metallic", readInt);
    AVGEN_MAT_READ(p.roughnessRegister, "roughness", readInt);
    AVGEN_MAT_READ(p.emissionRegister, "emission", readInt);
    AVGEN_MAT_READ(p.emissionIntensity, "emissionIntensity", readFloat);
    AVGEN_MAT_READ(p.opacityRegister, "opacity", readInt);
    AVGEN_MAT_READ(p.normalRegister, "normal", readInt);
    AVGEN_MAT_READ(p.occlusionRegister, "occlusion", readInt);
    AVGEN_MAT_READ(p.heightRegister, "height", readInt);
    if (auto ok = p.validate(); !ok) {
        return std::unexpected(ok.error());
    }
    return p;
}

#undef AVGEN_MAT_READ

Result<MaterialProgram> MaterialProgram::loadFile(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) {
        return fail("cannot read material file '{}'", path.string());
    }
    json parsed;
    try {
        in >> parsed;
    } catch (const json::exception& e) {
        return fail("material file '{}': {}", path.string(), e.what());
    }
    auto program = fromJson(parsed);
    if (!program) {
        return fail("material file '{}': {}", path.string(), program.error().message);
    }
    if (!parsed.is_object() || !parsed.contains("name")) {
        // A file that does not name itself is named by its path, minus the ".material" the library
        // files carry, rather than taking fromJson's generic default and colliding with the next one.
        std::string stem = path.stem().string();
        if (const auto dot = stem.rfind(".material"); dot != std::string::npos && dot + 9 == stem.size()) {
            stem.erase(dot);
        }
        if (!stem.empty()) {
            program->name = std::move(stem);
        }
    }
    return program;
}

// ---- evaluation ----------------------------------------------------------------------------------

MaterialResult evaluateMaterialProgram(const MaterialProgram& program, const MaterialContext& ctx,
                                       const MaterialResult& base) {
    MaterialResult result = base;
    result.registers = {};
    MaterialContext local = ctx;
    int budget = kMaxMaterialOps;
    runOps(program.ops, local, result.registers, budget);

    const auto reg = [&result](int r) { return result.registers[static_cast<std::size_t>(r)]; };
    if (registerInRange(program.baseColorRegister)) {
        result.baseColor = glm::max(glm::vec3(reg(program.baseColorRegister)), glm::vec3(0.0f));
    }
    if (registerInRange(program.metallicRegister)) {
        result.metallic = saturate1(reg(program.metallicRegister).x);
    }
    if (registerInRange(program.roughnessRegister)) {
        result.roughness = saturate1(reg(program.roughnessRegister).x);
    }
    if (registerInRange(program.emissionRegister)) {
        result.emission = glm::vec3(reg(program.emissionRegister)) * program.emissionIntensity;
    }
    if (registerInRange(program.opacityRegister)) {
        result.opacity = saturate1(reg(program.opacityRegister).x);
    }
    if (registerInRange(program.normalRegister)) {
        result.normal = safeNormalize(glm::vec3(reg(program.normalRegister)), glm::vec3(0.0f, 0.0f, 1.0f));
    }
    if (registerInRange(program.occlusionRegister)) {
        result.occlusion = saturate1(reg(program.occlusionRegister).x);
    }
    if (registerInRange(program.heightRegister)) {
        result.height = reg(program.heightRegister).x;
    }

    // ---- layers (ADR-036): height-aware compositing over the running result ----
    int layerCount = 0;
    for (const MaterialLayer& layer : program.layers) {
        if (!layer.enabled || layerCount >= kMaxMaterialLayers) {
            continue;
        }
        ++layerCount;
        local.height = result.height;
        runOps(layer.ops, local, result.registers, budget);
        const float mask = registerInRange(layer.maskRegister) ? saturate1(reg(layer.maskRegister).x) : 1.0f;
        const float layerHeight = registerInRange(layer.heightRegister) ? reg(layer.heightRegister).x : 0.0f;
        const float t = heightBlendWeight(result.height, layerHeight, mask, layer.blendRange);
        if (registerInRange(layer.baseColorRegister)) {
            const glm::vec3 value = glm::max(glm::vec3(reg(layer.baseColorRegister)), glm::vec3(0.0f));
            result.baseColor = result.baseColor * (1.0f - t) + value * t;
        }
        if (registerInRange(layer.metallicRegister)) {
            result.metallic = result.metallic * (1.0f - t) + saturate1(reg(layer.metallicRegister).x) * t;
        }
        if (registerInRange(layer.roughnessRegister)) {
            result.roughness = result.roughness * (1.0f - t) + saturate1(reg(layer.roughnessRegister).x) * t;
        }
        if (registerInRange(layer.emissionRegister)) {
            const glm::vec3 value = glm::vec3(reg(layer.emissionRegister)) * layer.emissionIntensity;
            result.emission = result.emission * (1.0f - t) + value * t;
        }
        if (registerInRange(layer.normalRegister)) {
            const glm::vec3 value = safeNormalize(glm::vec3(reg(layer.normalRegister)), glm::vec3(0.0f, 0.0f, 1.0f));
            result.normal = safeNormalize(result.normal * (1.0f - t) + value * t, glm::vec3(0.0f, 0.0f, 1.0f));
        }
        if (registerInRange(layer.occlusionRegister)) {
            result.occlusion = result.occlusion * (1.0f - t) + saturate1(reg(layer.occlusionRegister).x) * t;
        }
        result.height = result.height * (1.0f - t) + layerHeight * t;
    }
    return result;
}

// ---- GPU packing ---------------------------------------------------------------------------------

MaterialProgramGpu packMaterialProgramWithSlots(const MaterialProgram& program,
                                                const std::vector<std::pair<std::string, int>>& slots) {
    MaterialProgramGpu gpu{};
    gpu.outputs = glm::ivec4(program.baseColorRegister, program.metallicRegister, program.roughnessRegister,
                             program.emissionRegister);
    gpu.emissionIntensityPad = glm::vec4(program.emissionIntensity, 0.0f, 0.0f, 0.0f);
    int count = 0;
    const auto packOps = [&](const std::vector<MaterialOp>& list) {
        for (const MaterialOp& op : list) {
            if (!op.enabled || count >= kMaxMaterialOps) {
                continue;
            }
            MaterialOpGpu& g = gpu.ops[static_cast<std::size_t>(count)];
            g.kind = static_cast<std::uint32_t>(op.kind);
            g.input = static_cast<std::uint32_t>(op.input);
            g.seed = op.seed;
            g.fieldSlot = -1;
            if (op.kind == MaterialOpKind::Field) {
                const auto it = std::find_if(slots.begin(), slots.end(),
                                             [&op](const auto& slot) { return slot.first == op.field; });
                if (it != slots.end()) {
                    g.fieldSlot = it->second;
                }
            }
            g.registers = glm::ivec4(op.dst, op.srcA, op.srcB, op.srcC);
            g.valuePad = glm::vec4(op.value, 0.0f, 0.0f, 0.0f);
            g.constant = op.constant;
            g.constant2 = op.constant2;
            g.constant3 = op.constant3;
            g.constant4 = op.constant4;
            ++count;
        }
    };
    packOps(program.ops);
    const int baseCount = count;
    int layerCount = 0;
    for (const MaterialLayer& layer : program.layers) {
        if (!layer.enabled || layerCount >= kMaxMaterialLayers) {
            continue;
        }
        const int first = count;
        packOps(layer.ops);
        MaterialLayerGpu& g = gpu.layers[static_cast<std::size_t>(layerCount)];
        g.outputs = glm::ivec4(layer.baseColorRegister, layer.metallicRegister, layer.roughnessRegister,
                               layer.emissionRegister);
        g.aux = glm::ivec4(layer.normalRegister, layer.occlusionRegister, layer.maskRegister, layer.heightRegister);
        g.range = glm::ivec4(first, count - first, 0, 0);
        g.params = glm::vec4(layer.emissionIntensity, layer.blendRange, 0.0f, 0.0f);
        ++layerCount;
    }
    gpu.opacityCountPad = glm::ivec4(program.opacityRegister, baseCount, layerCount, 0);
    gpu.aux = glm::ivec4(program.normalRegister, program.occlusionRegister, program.heightRegister, count);
    return gpu;
}

} // namespace avgen::scene
