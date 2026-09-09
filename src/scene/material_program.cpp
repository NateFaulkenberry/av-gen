// Procedural materials (ADR-030): the CPU reference of the material op interpreter, validation,
// structural hashing, JSON and GPU packing. `shaders/material.wgsl` is a transliteration of
// `evaluateMaterialProgram`; the exact per-op formulas are in docs/procedural-materials.md.
//
// Conventions chosen here (the header fixes the op semantics; these are the remaining choices):
//
// * Input layouts: positions (x, y, z, 1); normal (x, y, z, 0); uv (u, v, 0, 0); scalar inputs
//   (objectId, instanceIndex, instanceId, time, depth) broadcast to all four components; vec4
//   inputs (instanceRandom, instanceColor, instanceEmissive, audio, audioBands, beatPhase) as-is;
//   viewDirection (x, y, z, 0).
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
// * Outputs: baseColor = max(reg.rgb, 0); metallic / roughness / opacity = clamp(reg.x, 0, 1);
//   emission = reg.rgb * emissionIntensity (not clamped).

#include "scene/material_program.hpp"

#include "core/color.hpp"
#include "core/noise.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
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
    {MaterialOpKind::Input, "input"},         {MaterialOpKind::Constant, "constant"},
    {MaterialOpKind::Gradient, "gradient"},   {MaterialOpKind::Noise, "noise"},
    {MaterialOpKind::Voronoi, "voronoi"},     {MaterialOpKind::Fresnel, "fresnel"},
    {MaterialOpKind::Ramp, "ramp"},           {MaterialOpKind::Remap, "remap"},
    {MaterialOpKind::Multiply, "multiply"},   {MaterialOpKind::Add, "add"},
    {MaterialOpKind::Mix, "mix"},             {MaterialOpKind::MixBy, "mixBy"},
    {MaterialOpKind::Power, "power"},         {MaterialOpKind::Smoothstep, "smoothstep"},
    {MaterialOpKind::Threshold, "threshold"}, {MaterialOpKind::HueShift, "hueShift"},
    {MaterialOpKind::Saturate, "saturate"},   {MaterialOpKind::Palette, "palette"},
    {MaterialOpKind::Field, "field"},
};

struct InputName {
    MaterialInput input;
    const char* name;
};
constexpr InputName kInputNames[] = {
    {MaterialInput::WorldPosition, "worldPosition"},     {MaterialInput::LocalPosition, "localPosition"},
    {MaterialInput::Normal, "normal"},                   {MaterialInput::Uv, "uv"},
    {MaterialInput::ObjectId, "objectId"},               {MaterialInput::InstanceIndex, "instanceIndex"},
    {MaterialInput::InstanceId, "instanceId"},           {MaterialInput::InstanceRandom, "instanceRandom"},
    {MaterialInput::InstanceColor, "instanceColor"},     {MaterialInput::InstanceEmissive, "instanceEmissive"},
    {MaterialInput::Time, "time"},                       {MaterialInput::Audio, "audio"},
    {MaterialInput::AudioBands, "audioBands"},           {MaterialInput::BeatPhase, "beatPhase"},
    {MaterialInput::ViewDirection, "viewDirection"},     {MaterialInput::Depth, "depth"},
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

glm::vec4 inputValue(MaterialInput input, const MaterialContext& ctx) {
    switch (input) {
    case MaterialInput::WorldPosition:
        return glm::vec4(ctx.worldPosition, 1.0f);
    case MaterialInput::LocalPosition:
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
        return glm::vec4(ctx.depth);
    }
    return glm::vec4(0.0f);
}

bool registerInRange(int r) {
    return r >= 0 && r < kMaterialRegisters;
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
    }
    return glm::vec4(0.0f);
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

// ---- MaterialProgram -----------------------------------------------------------------------------

Result<void> MaterialProgram::validate() const {
    if (ops.size() > static_cast<std::size_t>(kMaxMaterialOps)) {
        return fail("material '{}': at most {} ops (got {})", name, kMaxMaterialOps, ops.size());
    }
    for (std::size_t i = 0; i < ops.size(); ++i) {
        const MaterialOp& op = ops[i];
        if (!registerInRange(op.dst) || !registerInRange(op.srcA) || !registerInRange(op.srcB) ||
            !registerInRange(op.srcC)) {
            return fail("material '{}': op {} ({}) uses a register outside 0..{}", name, i,
                        materialOpKindName(op.kind), kMaterialRegisters - 1);
        }
        if (op.kind == MaterialOpKind::Field && op.field.empty()) {
            return fail("material '{}': op {} (field) needs a field name", name, i);
        }
    }
    const auto outputOk = [](int r) { return r >= -1 && r < kMaterialRegisters; };
    if (!outputOk(baseColorRegister) || !outputOk(metallicRegister) || !outputOk(roughnessRegister) ||
        !outputOk(emissionRegister) || !outputOk(opacityRegister)) {
        return fail("material '{}': output registers must be in -1..{}", name, kMaterialRegisters - 1);
    }
    return {};
}

std::uint64_t MaterialProgram::structuralHash() const {
    StructHash h;
    h.str(name);
    h.u32(static_cast<std::uint32_t>(ops.size()));
    for (const MaterialOp& op : ops) {
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
    h.i32(baseColorRegister);
    h.i32(metallicRegister);
    h.i32(roughnessRegister);
    h.i32(emissionRegister);
    h.f32(emissionIntensity);
    h.i32(opacityRegister);
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
    AVGEN_MAT_READ(p.baseColorRegister, "baseColor", readInt);
    AVGEN_MAT_READ(p.metallicRegister, "metallic", readInt);
    AVGEN_MAT_READ(p.roughnessRegister, "roughness", readInt);
    AVGEN_MAT_READ(p.emissionRegister, "emission", readInt);
    AVGEN_MAT_READ(p.emissionIntensity, "emissionIntensity", readFloat);
    AVGEN_MAT_READ(p.opacityRegister, "opacity", readInt);
    if (auto ok = p.validate(); !ok) {
        return std::unexpected(ok.error());
    }
    return p;
}

#undef AVGEN_MAT_READ

// ---- evaluation ----------------------------------------------------------------------------------

MaterialResult evaluateMaterialProgram(const MaterialProgram& program, const MaterialContext& ctx,
                                       const MaterialResult& base) {
    MaterialResult result = base;
    result.registers = {};
    for (const MaterialOp& op : program.ops) {
        if (!op.enabled) {
            continue;
        }
        if (!registerInRange(op.dst) || !registerInRange(op.srcA) || !registerInRange(op.srcB) ||
            !registerInRange(op.srcC)) {
            continue;
        }
        result.registers[static_cast<std::size_t>(op.dst)] = evaluateOp(op, ctx, result.registers);
    }
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
    for (const MaterialOp& op : program.ops) {
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
    gpu.opacityCountPad = glm::ivec4(program.opacityRegister, count, 0, 0);
    return gpu;
}

} // namespace avgen::scene
