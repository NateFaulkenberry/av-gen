#pragma once

// Procedural materials (ADR-030, layered in ADR-036): an interpreted op program evaluated per
// fragment by `shaders/material.wgsl` (and on the CPU by evaluateMaterialProgram for tests).
//
// A program is a **base** plus up to kMaxMaterialLayers layers. The base's ops run first over a
// register file of kMaterialRegisters vec4 registers and produce the surface values; each layer
// then runs its own ops (over the *same* register file, so a layer can reuse what the base
// computed) and is composited over the running result with height-aware blending rather than a
// linear mix. base.ops.size() + sum(layer ops) may not exceed kMaxMaterialOps.
//
// Registers r0..r7 start as zero. Op semantics (a = reg[srcA], b = reg[srcB], c = reg[srcC],
// out = reg[dst], k = constant vec4, k2..k4 = constant2..4, f = float `value`):
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
//   Field     : out = fieldColor(fieldSlot, worldPos) (scalar fields -> vec4(s), vector -> v)
// ADR-036 ops (exact formulas in docs/procedural-materials.md; material_program.cpp is the
// reference implementation both sides follow):
//   Triplanar      : fbm3 on the three world planes of a.xyz * f + k.xyz, blended by
//                    triplanarWeights(normal, k2.x) -> vec4(s)
//   WorldProject   : out = vec4(worldPosition * f + k.xyz, 1)
//   ObjectProject  : out = vec4(localPosition * f + k.xyz, 1)
//   HeightBlend    : out = vec4(heightBlendWeight(a.x, b.x, c.x, f))
//   DetailNormal   : out = vec4(reorientNormal(a.xyz, b.xyz), 0)   (Barre-Brisebois & Hill RNM)
//   CurvatureMask  : out = vec4(smoothstep(k.x, k.y, curvature * f))
//   EdgeWear       : convex curvature broken up by noise
//   DecalBox       : out = vec4(u, v, mask, mask) for an axis-aligned world box (k.xyz = centre,
//                    k2.xyz = half extents, f = edge softness)
//   Anisotropy     : view-azimuth effective roughness of an anisotropic lobe (a.x = roughness,
//                    f = anisotropy, k.xyz = the brush direction)
//   RoughnessFilter: Kaplanyan specular anti-aliasing from normalVariance (a.x = roughness,
//                    f = variance scale)
//   MicroDetail    : noise whose amplitude fades with the screen-space footprint, so it never aliases
// Outputs: baseColor (rgb), metallic (x), roughness (x), emission (rgb x emissionIntensity),
// opacity (x), normal (xyz, tangent space), occlusion (x), height (x); -1 = keep the material's
// value.

#include "core/error.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace avgen::scene {

enum class MaterialOpKind : std::uint8_t {
    Input, Constant, Gradient, Noise, Voronoi, Fresnel, Ramp, Remap, Multiply, Add, Mix, MixBy, Power,
    Smoothstep, Threshold, HueShift, Saturate, Palette, Field,
    // ADR-036. Appended: the enum order is the wire format.
    Triplanar, WorldProject, ObjectProject, HeightBlend, DetailNormal, CurvatureMask, EdgeWear,
    DecalBox, Anisotropy, RoughnessFilter, MicroDetail,
    // Appended: reorders a register's components. Every op that takes a scalar takes it from a
    // register's x channel, and most of the values worth masking with arrive somewhere else -- the
    // y of a normal, the y of a world position, the second half of a uv. Without this, a program
    // can carry exactly one maskable scalar, which is one fewer than terrain needs.
    Swizzle,
};
[[nodiscard]] const char* materialOpKindName(MaterialOpKind kind);
[[nodiscard]] std::optional<MaterialOpKind> materialOpKindFromName(std::string_view name);

enum class MaterialInput : std::uint8_t {
    WorldPosition, LocalPosition, Normal, Uv, ObjectId, InstanceIndex, InstanceId, InstanceRandom, InstanceColor,
    InstanceEmissive, Time, Audio, AudioBands, BeatPhase, ViewDirection, Depth,
    // ADR-036 geometric inputs. ObjectPosition is LocalPosition and CameraDistance is Depth under
    // the names ADR-036 uses; both spellings exist so a program reads the way the ADR is written.
    Curvature, Convexity, Concavity, Cavity, Occlusion, Height, NormalVariance, ObjectPosition,
    TriplanarWeights, CameraDistance, MaterialId, Footprint,
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
constexpr int kMaxMaterialOps = 48;   // ADR-036 (was 16); shared by the base and every layer
constexpr int kMaterialRegisters = 8;
// Distinct fields one program may name (ADR-050). Field ops are evaluated once each, before the
// interpreter's loop, because the field evaluator inlined into that loop costs every material
// program 2.2x whether or not it uses one -- see docs/performance.md.
constexpr int kMaxMaterialFields = 4;
constexpr int kMaxMaterialLayers = 4;

// One layer over the base (ADR-036). Its ops run after the base's, over the same registers, and
// the values it names are composited with `heightBlendWeight(runningHeight, height, mask, range)`.
// An output register of -1 means the layer leaves that channel alone.
struct MaterialLayer {
    std::string name;
    bool enabled = true;
    std::vector<MaterialOp> ops;
    int maskRegister = -1;      // -1 = mask 1 (the layer applies wherever its height allows)
    int heightRegister = -1;    // -1 = height 0
    float blendRange = 0.1f;    // width of the height transition; 0 = a hard height threshold
    int baseColorRegister = -1;
    int metallicRegister = -1;
    int roughnessRegister = -1;
    int emissionRegister = -1;
    float emissionIntensity = 1.0f;
    int normalRegister = -1;    // tangent-space normal
    int occlusionRegister = -1;
};

struct MaterialProgram {
    std::string name = "material";
    std::vector<MaterialOp> ops;
    std::vector<MaterialLayer> layers;
    int baseColorRegister = -1;
    int metallicRegister = -1;
    int roughnessRegister = -1;
    int emissionRegister = -1;
    float emissionIntensity = 1.0f;
    int opacityRegister = -1;
    int normalRegister = -1;    // tangent-space normal perturbation (ADR-036)
    int occlusionRegister = -1; // multiplies the material's occlusion
    int heightRegister = -1;    // the surface height the layers blend against
    [[nodiscard]] int totalOpCount() const; // base + every enabled layer
    [[nodiscard]] Result<void> validate() const;
    [[nodiscard]] std::uint64_t structuralHash() const;
    [[nodiscard]] nlohmann::json toJson() const;
    static Result<MaterialProgram> fromJson(const nlohmann::json& j);
    // Loads a program from a standalone `.material.json` file (the same object `fromJson` takes),
    // so a library material can be shared between scenes instead of pasted into each of them.
    static Result<MaterialProgram> loadFile(const std::filesystem::path& path);
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
    // ADR-036 geometric inputs. The shader derives curvature, cavity, normal variance and the
    // footprint from screen-space derivatives and reads `occlusion` from the GTAO target; the CPU
    // reference takes them as given, so both sides evaluate the same numbers.
    float curvature = 0.0f;       // signed, 1/metre; positive convex, negative concave
    float cavity = 0.0f;          // 0..1 concavity at the current screen footprint
    float occlusion = 1.0f;       // ambient visibility, 1 = unoccluded
    float height = 0.0f;          // running surface height (the base's height output, for layers)
    float normalVariance = 0.0f;  // 0.5 (|dN/dx|^2 + |dN/dy|^2)
    float footprint = 0.0f;       // world units covered by one pixel
    float materialId = 0.0f;
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
    glm::vec3 normal{0.0f, 0.0f, 1.0f}; // tangent space; (0, 0, 1) = unperturbed
    float occlusion = 1.0f;
    float height = 0.0f;
    std::array<glm::vec4, kMaterialRegisters> registers{};
};

// Height-aware layer blending (ADR-036): the weight of a layer of height `layerHeight` and mask
// `mask` over a surface of height `baseHeight`, with a transition of width `range`.
//   a1 = baseHeight + (1 - mask), a2 = layerHeight + mask, m = max(a1, a2) - max(range, 1e-4)
//   t  = max(a2 - m, 0) / (max(a1 - m, 0) + max(a2 - m, 0))
// mask 0 gives 0 and mask 1 gives 1; between them the layer sits wherever the base is low.
[[nodiscard]] float heightBlendWeight(float baseHeight, float layerHeight, float mask, float range);

// Triplanar blend weights from a normal: |n|^sharpness, normalised to sum 1. Even in n, so a
// normal flip leaves them unchanged.
[[nodiscard]] glm::vec3 triplanarWeights(const glm::vec3& normal, float sharpness);

// Reoriented normal mapping (Barre-Brisebois & Hill 2012): detail `d` applied over base `b`.
[[nodiscard]] glm::vec3 reorientNormal(const glm::vec3& base, const glm::vec3& detail);

// Evaluates the program starting from `base` values (the material's own).
[[nodiscard]] MaterialResult evaluateMaterialProgram(const MaterialProgram& program, const MaterialContext& ctx,
                                                     const MaterialResult& base);

// GPU packing (see shaders/material.wgsl): an 80-byte header, kMaxMaterialLayers 64-byte layer
// records and kMaxMaterialOps 112-byte ops.
struct alignas(16) MaterialOpGpu {
    std::uint32_t kind;
    std::uint32_t input;
    std::uint32_t seed;
    std::int32_t fieldSlot;
    glm::ivec4 registers;        // dst, srcA, srcB, srcC
    float value;                 // the scalar `f`
    std::int32_t fieldOrdinal;   // Field op: which of MaterialProgramGpu::fieldSlots, -1 = none
    float pad0;
    float pad1;
    glm::vec4 constant;
    glm::vec4 constant2;
    glm::vec4 constant3;
    glm::vec4 constant4;
};
static_assert(sizeof(MaterialOpGpu) == 112);
struct alignas(16) MaterialLayerGpu {
    glm::ivec4 outputs;          // baseColor, metallic, roughness, emission registers
    glm::ivec4 aux;              // normal, occlusion, mask, height registers
    glm::ivec4 range;            // firstOp, opCount, 0, 0
    glm::vec4 params;            // emissionIntensity, blendRange, 0, 0
};
static_assert(sizeof(MaterialLayerGpu) == 64);
struct alignas(16) MaterialProgramGpu {
    glm::ivec4 outputs;          // baseColor, metallic, roughness, emission registers
    glm::ivec4 opacityCountPad;  // opacity register, base op count, layer count, 0
    glm::vec4 emissionIntensityPad;
    glm::ivec4 aux;              // normal, occlusion, height registers, total op count
    glm::ivec4 fieldSlots;       // FieldBlock slot of each distinct field the program names, -1 = unused
    std::array<MaterialLayerGpu, kMaxMaterialLayers> layers;
    std::array<MaterialOpGpu, kMaxMaterialOps> ops;
};
static_assert(sizeof(MaterialProgramGpu) == 80 + 64 * kMaxMaterialLayers + 112 * kMaxMaterialOps);
static_assert(kMaxMaterialFields == 4); // fieldSlots is one ivec4
// `fieldSlotOf` maps a field name to a GPU slot (-1 when unknown).
template <typename SlotFn>
MaterialProgramGpu packMaterialProgram(const MaterialProgram& program, SlotFn&& fieldSlotOf);
MaterialProgramGpu packMaterialProgramWithSlots(const MaterialProgram& program, const std::vector<std::pair<std::string, int>>& slots);

template <typename SlotFn>
MaterialProgramGpu packMaterialProgram(const MaterialProgram& program, SlotFn&& fieldSlotOf) {
    std::vector<std::pair<std::string, int>> slots;
    const auto collect = [&slots, &fieldSlotOf](const std::vector<MaterialOp>& ops) {
        for (const MaterialOp& op : ops) {
            if (op.kind == MaterialOpKind::Field) {
                slots.emplace_back(op.field, fieldSlotOf(op.field));
            }
        }
    };
    collect(program.ops);
    for (const MaterialLayer& layer : program.layers) {
        collect(layer.ops);
    }
    return packMaterialProgramWithSlots(program, slots);
}

} // namespace avgen::scene
