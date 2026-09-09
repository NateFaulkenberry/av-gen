#pragma once

// Procedural materials (ADR-030): an interpreted op program evaluated per fragment by
// `shaders/material.wgsl` (and on the CPU by evaluateMaterialProgram for tests). A program has
// up to kMaxMaterialOps ops over a register file of kMaterialRegisters vec4 registers; inputs are
// loaded into registers by Input ops, outputs are read from registers named by the program.
//
// Registers r0..r7 start as zero. Op semantics (a = reg[srcA], b = reg[srcB], out = reg[dst],
// k = constant vec4, f = float `value`):
//   Input     : out = input(kind)   (kinds below; vec4 with the natural components)
//   Constant  : out = k
//   Gradient  : out = vec4(saturate(dot(a.xyz, k.xyz) * f + k.w))          (axis gradient of a position)
//   Noise     : out = vec4(fbm3(a.xyz * f + k.xyz, seed))                  (0..1)
//   Voronoi   : out = vec4(voronoiF1(a.xyz * f + k.xyz, seed))
//   Fresnel   : out = vec4(pow(1 - saturate(dot(normal, view)), f))         (uses the fragment's N/V)
//   Ramp      : out = ramp(a.x) with 3 stops k0 (t=0), k1 (t=0.5), k2 (t=1)  (k0 = constant, k1 = constant2, k2 = constant3)
//   Remap     : out = (a - k.x) / (k.y - k.x) * (k.w - k.z) + k.z, clamped when f > 0.5
//   Multiply  : out = a * b        Add: out = a + b        Mix: out = mix(a, b, f)  (MixBy: mix(a, b, reg[srcC].x))
//   Power     : out = pow(max(a, 0), f)
//   Smoothstep: out = smoothstep(k.x, k.y, a)
//   Threshold : out = step(f, a)
//   HueShift  : out.rgb = hueShift(a.rgb, f + b.x)  (OKLCH; b optional = 0)
//   Saturate  : out.rgb = saturate(a.rgb, f)
//   Palette   : out.rgb = cosinePalette(a.x + f) with k = a-term, constant2 = b, constant3 = c, constant4 = d
//   Field     : out = fieldColor(fieldSlot, worldPos) (scalar fields → vec4(s), vector → v)
// Outputs: baseColor (rgb), metallic (x), roughness (x), emission (rgb × emissionIntensity),
// opacity (x); -1 = keep the material's value.

#include "core/error.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::scene {

enum class MaterialOpKind : std::uint8_t {
    Input, Constant, Gradient, Noise, Voronoi, Fresnel, Ramp, Remap, Multiply, Add, Mix, MixBy, Power,
    Smoothstep, Threshold, HueShift, Saturate, Palette, Field,
};
[[nodiscard]] const char* materialOpKindName(MaterialOpKind kind);
[[nodiscard]] std::optional<MaterialOpKind> materialOpKindFromName(std::string_view name);

enum class MaterialInput : std::uint8_t {
    WorldPosition, LocalPosition, Normal, Uv, ObjectId, InstanceIndex, InstanceId, InstanceRandom, InstanceColor,
    InstanceEmissive, Time, Audio, AudioBands, BeatPhase, ViewDirection, Depth,
};
[[nodiscard]] const char* materialInputName(MaterialInput input);
[[nodiscard]] std::optional<MaterialInput> materialInputFromName(std::string_view name);
// Audio: (rms, bass, mid, treble); AudioBands: (lowMid, highMid, centroid, flux); BeatPhase: (phase, pulse, onset, bar)

struct MaterialOp {
    MaterialOpKind kind = MaterialOpKind::Constant;
    bool enabled = true;
    int dst = 0;                  // register 0..7
    int srcA = 0;
    int srcB = 0;
    int srcC = 0;
    float value = 1.0f;
    glm::vec4 constant{0.0f};
    glm::vec4 constant2{0.0f};
    glm::vec4 constant3{0.0f};
    glm::vec4 constant4{0.0f};
    std::uint32_t seed = 1;
    MaterialInput input = MaterialInput::WorldPosition;
    std::string field;            // Field op: field name
};
constexpr int kMaxMaterialOps = 16;
constexpr int kMaterialRegisters = 8;

struct MaterialProgram {
    std::string name = "material";
    std::vector<MaterialOp> ops;
    int baseColorRegister = -1;
    int metallicRegister = -1;
    int roughnessRegister = -1;
    int emissionRegister = -1;
    float emissionIntensity = 1.0f;
    int opacityRegister = -1;
    [[nodiscard]] Result<void> validate() const;
    [[nodiscard]] std::uint64_t structuralHash() const;
    [[nodiscard]] nlohmann::json toJson() const;
    static Result<MaterialProgram> fromJson(const nlohmann::json& j);
};

// CPU evaluation context/results for tests and tools.
struct MaterialContext {
    glm::vec3 worldPosition{0.0f};
    glm::vec3 localPosition{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec2 uv{0.0f};
    float objectId = 0.0f;
    float instanceIndex = 0.0f;
    float instanceId = 0.0f;
    glm::vec4 instanceRandom{0.0f};
    glm::vec4 instanceColor{1.0f};
    glm::vec4 instanceEmissive{0.0f};
    float time = 0.0f;
    glm::vec4 audio{0.0f};
    glm::vec4 audioBands{0.0f};
    glm::vec4 beat{0.0f};
    glm::vec3 viewDirection{0.0f, 0.0f, 1.0f};
    float depth = 0.0f;
    // Field samples by name are resolved through this callback (null = zeros).
    const struct MaterialFieldSampler* fields = nullptr;
};
struct MaterialFieldSampler {
    virtual ~MaterialFieldSampler() = default;
    [[nodiscard]] virtual glm::vec4 sample(std::string_view field, const glm::vec3& worldPosition) const = 0;
};
struct MaterialResult {
    glm::vec3 baseColor{1.0f};
    float metallic = 0.0f;
    float roughness = 0.5f;
    glm::vec3 emission{0.0f};
    float opacity = 1.0f;
    std::array<glm::vec4, kMaterialRegisters> registers{};
};
// Evaluates the program starting from `base` values (the material's own).
[[nodiscard]] MaterialResult evaluateMaterialProgram(const MaterialProgram& program, const MaterialContext& ctx,
                                                     const MaterialResult& base);

// GPU packing (see shaders/material.wgsl): 112 bytes per op, kMaxMaterialOps ops + a header.
struct alignas(16) MaterialOpGpu {
    std::uint32_t kind;
    std::uint32_t input;
    std::uint32_t seed;
    std::int32_t fieldSlot;
    glm::ivec4 registers;        // dst, srcA, srcB, srcC
    glm::vec4 valuePad;          // value, 0, 0, 0
    glm::vec4 constant;
    glm::vec4 constant2;
    glm::vec4 constant3;
    glm::vec4 constant4;
};
static_assert(sizeof(MaterialOpGpu) == 112);
struct alignas(16) MaterialProgramGpu {
    glm::ivec4 outputs;          // baseColor, metallic, roughness, emission registers
    glm::ivec4 opacityCountPad;  // opacity register, op count, 0, 0
    glm::vec4 emissionIntensityPad;
    std::array<MaterialOpGpu, kMaxMaterialOps> ops;
};
static_assert(sizeof(MaterialProgramGpu) == 48 + 112 * kMaxMaterialOps);
// `fieldSlotOf` maps a field name to a GPU slot (-1 when unknown).
template <typename SlotFn>
MaterialProgramGpu packMaterialProgram(const MaterialProgram& program, SlotFn&& fieldSlotOf);
MaterialProgramGpu packMaterialProgramWithSlots(const MaterialProgram& program, const std::vector<std::pair<std::string, int>>& slots);

template <typename SlotFn>
MaterialProgramGpu packMaterialProgram(const MaterialProgram& program, SlotFn&& fieldSlotOf) {
    std::vector<std::pair<std::string, int>> slots;
    for (const MaterialOp& op : program.ops) {
        if (op.kind == MaterialOpKind::Field) {
            slots.emplace_back(op.field, fieldSlotOf(op.field));
        }
    }
    return packMaterialProgramWithSlots(program, slots);
}

} // namespace avgen::scene
