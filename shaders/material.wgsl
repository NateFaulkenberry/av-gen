// Procedural materials on the GPU (ADR-030, layered in ADR-036): the WGSL transliteration of
// scene::evaluateMaterialProgram (src/scene/material_program.cpp). A packed MaterialProgramGpu
// (5696 bytes: a 64-byte header, 4 layer records of 64 bytes and 48 ops of 112 bytes,
// scene::packMaterialProgram) per slot in a *storage* MaterialProgramBlock;
// evaluateMaterialProgram(slot, ctx, base) runs the base's ops over a register file of 8 vec4s
// (all zero at entry), reads the outputs, then runs and composites each layer, with a register
// index of -1 meaning "keep the material's own value" (the matching field of `base`).
// tests/rendering/test_material_gpu.cpp compares the two sides within 1e-4 (noise-based 1e-3).
// The exact per-op formulas are in docs/procedural-materials.md.
//
// The including module declares the binding itself, e.g.
//   @group(2) @binding(6) var<storage, read> materialPrograms: MaterialProgramBlock;
// and must also have included fields.wgsl (Field ops call fieldScalar / fieldVector / fieldColor /
// fieldTypeOf, and fbm3 / voronoiF1 come from the noise.wgsl it pulls in). This file includes
// color.wgsl; do not include that a second time.
#include "color.wgsl"

struct MaterialOpGpu {
    kind: u32,                    // MaterialOpKind, enum order (MAT_OP_* below)
    input: u32,                   // MaterialInput, enum order (MAT_IN_* below)
    seed: u32,                    // noise / voronoi lattice seed
    fieldSlot: i32,               // Field op: the FieldBlock slot, -1 when unresolved
    registers: vec4<i32>,         // dst, srcA, srcB, srcC
    valuePad: vec4<f32>,          // x = `value` (the scalar f), yzw = 0
    constant: vec4<f32>,
    constant2: vec4<f32>,
    constant3: vec4<f32>,
    constant4: vec4<f32>,
};

// One layer over the base (ADR-036); mirrors scene::MaterialLayerGpu.
struct MaterialLayerGpu {
    outputs: vec4<i32>,           // baseColor, metallic, roughness, emission registers (-1 = leave alone)
    aux: vec4<i32>,               // normal, occlusion, mask, height registers
    range: vec4<i32>,             // firstOp, opCount, 0, 0
    params: vec4<f32>,            // emissionIntensity, blendRange, 0, 0
};

struct MaterialProgramGpu {
    outputs: vec4<i32>,           // baseColor, metallic, roughness, emission registers (-1 = keep)
    opacityCountPad: vec4<i32>,   // opacity register, base op count, layer count, 0
    emissionIntensityPad: vec4<f32>,
    aux: vec4<i32>,               // normal, occlusion, height registers, total op count
    layers: array<MaterialLayerGpu, 4>,
    ops: array<MaterialOpGpu, 48>,
};

// Mirrors rendering::MaterialProgramBlock (45584 bytes; a storage buffer since ADR-036).
struct MaterialProgramBlock {
    count: u32,
    pad0: u32,
    pad1: u32,
    pad2: u32,
    programs: array<MaterialProgramGpu, 8>,
};

const MAT_MAX_OPS: u32 = 48u;
const MAT_MAX_LAYERS: u32 = 4u;
const MAT_MAX_PROGRAMS: i32 = 8;
const MAT_REGISTERS: i32 = 8;

const MAT_OP_INPUT: u32 = 0u;
const MAT_OP_CONSTANT: u32 = 1u;
const MAT_OP_GRADIENT: u32 = 2u;
const MAT_OP_NOISE: u32 = 3u;
const MAT_OP_VORONOI: u32 = 4u;
const MAT_OP_FRESNEL: u32 = 5u;
const MAT_OP_RAMP: u32 = 6u;
const MAT_OP_REMAP: u32 = 7u;
const MAT_OP_MULTIPLY: u32 = 8u;
const MAT_OP_ADD: u32 = 9u;
const MAT_OP_MIX: u32 = 10u;
const MAT_OP_MIXBY: u32 = 11u;
const MAT_OP_POWER: u32 = 12u;
const MAT_OP_SMOOTHSTEP: u32 = 13u;
const MAT_OP_THRESHOLD: u32 = 14u;
const MAT_OP_HUESHIFT: u32 = 15u;
const MAT_OP_SATURATE: u32 = 16u;
const MAT_OP_PALETTE: u32 = 17u;
const MAT_OP_FIELD: u32 = 18u;
const MAT_OP_TRIPLANAR: u32 = 19u;
const MAT_OP_WORLD_PROJECT: u32 = 20u;
const MAT_OP_OBJECT_PROJECT: u32 = 21u;
const MAT_OP_HEIGHT_BLEND: u32 = 22u;
const MAT_OP_DETAIL_NORMAL: u32 = 23u;
const MAT_OP_CURVATURE_MASK: u32 = 24u;
const MAT_OP_EDGE_WEAR: u32 = 25u;
const MAT_OP_DECAL_BOX: u32 = 26u;
const MAT_OP_ANISOTROPY: u32 = 27u;
const MAT_OP_ROUGHNESS_FILTER: u32 = 28u;
const MAT_OP_MICRO_DETAIL: u32 = 29u;

const MAT_IN_WORLD_POSITION: u32 = 0u;
const MAT_IN_LOCAL_POSITION: u32 = 1u;
const MAT_IN_NORMAL: u32 = 2u;
const MAT_IN_UV: u32 = 3u;
const MAT_IN_OBJECT_ID: u32 = 4u;
const MAT_IN_INSTANCE_INDEX: u32 = 5u;
const MAT_IN_INSTANCE_ID: u32 = 6u;
const MAT_IN_INSTANCE_RANDOM: u32 = 7u;
const MAT_IN_INSTANCE_COLOR: u32 = 8u;
const MAT_IN_INSTANCE_EMISSIVE: u32 = 9u;
const MAT_IN_TIME: u32 = 10u;
const MAT_IN_AUDIO: u32 = 11u;
const MAT_IN_AUDIO_BANDS: u32 = 12u;
const MAT_IN_BEAT_PHASE: u32 = 13u;
const MAT_IN_VIEW_DIRECTION: u32 = 14u;
const MAT_IN_DEPTH: u32 = 15u;
const MAT_IN_CURVATURE: u32 = 16u;
const MAT_IN_CONVEXITY: u32 = 17u;
const MAT_IN_CONCAVITY: u32 = 18u;
const MAT_IN_CAVITY: u32 = 19u;
const MAT_IN_OCCLUSION: u32 = 20u;
const MAT_IN_HEIGHT: u32 = 21u;
const MAT_IN_NORMAL_VARIANCE: u32 = 22u;
const MAT_IN_OBJECT_POSITION: u32 = 23u;
const MAT_IN_TRIPLANAR_WEIGHTS: u32 = 24u;
const MAT_IN_CAMERA_DISTANCE: u32 = 25u;
const MAT_IN_MATERIAL_ID: u32 = 26u;
const MAT_IN_FOOTPRINT: u32 = 27u;

// The per-fragment inputs, exactly scene::MaterialContext.
struct MaterialContext {
    worldPosition: vec3<f32>,
    localPosition: vec3<f32>,
    normal: vec3<f32>,
    uv: vec2<f32>,
    objectId: f32,
    instanceIndex: f32,
    instanceId: f32,
    instanceRandom: vec4<f32>,
    instanceColor: vec4<f32>,
    instanceEmissive: vec4<f32>,
    time: f32,
    audio: vec4<f32>,
    audioBands: vec4<f32>,
    beat: vec4<f32>,
    viewDirection: vec3<f32>,
    depth: f32,
    // ADR-036 geometric inputs; the caller derives these from screen-space derivatives and the
    // GTAO target (see materialGeometry() in pbr_shade.wgsl).
    curvature: f32,
    cavity: f32,
    occlusion: f32,
    height: f32,
    normalVariance: f32,
    footprint: f32,
    materialId: f32,
};

// The material values a program reads from (`base`) and writes (scene::MaterialResult without
// the register file, which the shader keeps in a local).
struct MaterialResult {
    baseColor: vec3<f32>,
    metallic: f32,
    roughness: f32,
    emission: vec3<f32>,
    opacity: f32,
    normal: vec3<f32>,   // tangent space; (0, 0, 1) = unperturbed
    occlusion: f32,
    height: f32,
};

alias MatRegs = array<vec4<f32>, 8>;

fn materialContextZero() -> MaterialContext {
    var ctx: MaterialContext;
    ctx.worldPosition = vec3<f32>(0.0);
    ctx.localPosition = vec3<f32>(0.0);
    ctx.normal = vec3<f32>(0.0, 1.0, 0.0);
    ctx.uv = vec2<f32>(0.0);
    ctx.objectId = 0.0;
    ctx.instanceIndex = 0.0;
    ctx.instanceId = 0.0;
    ctx.instanceRandom = vec4<f32>(0.0);
    ctx.instanceColor = vec4<f32>(1.0);
    ctx.instanceEmissive = vec4<f32>(0.0);
    ctx.time = 0.0;
    ctx.audio = vec4<f32>(0.0);
    ctx.audioBands = vec4<f32>(0.0);
    ctx.beat = vec4<f32>(0.0);
    ctx.viewDirection = vec3<f32>(0.0, 0.0, 1.0);
    ctx.depth = 0.0;
    ctx.curvature = 0.0;
    ctx.cavity = 0.0;
    ctx.occlusion = 1.0;
    ctx.height = 0.0;
    ctx.normalVariance = 0.0;
    ctx.footprint = 0.0;
    ctx.materialId = 0.0;
    return ctx;
}

fn materialResultZero() -> MaterialResult {
    var r: MaterialResult;
    r.baseColor = vec3<f32>(1.0);
    r.metallic = 0.0;
    r.roughness = 0.5;
    r.emission = vec3<f32>(0.0);
    r.opacity = 1.0;
    r.normal = vec3<f32>(0.0, 0.0, 1.0);
    r.occlusion = 1.0;
    r.height = 0.0;
    return r;
}

// ---- helpers (the CPU's saturate1 / mix4 / smoothstep1 / std::pow) --------------------------------

fn matMix4(a: vec4<f32>, b: vec4<f32>, t: f32) -> vec4<f32> {
    return a * (1.0 - t) + b * t;
}

// std::pow with pow(0, 0) == 1 (WGSL leaves that case to the implementation).
fn matPow1(b: f32, e: f32) -> f32 {
    if (b == 0.0 && e == 0.0) {
        return 1.0;
    }
    return pow(b, e);
}

fn matPow4(b: vec4<f32>, e: f32) -> vec4<f32> {
    return vec4<f32>(matPow1(b.x, e), matPow1(b.y, e), matPow1(b.z, e), matPow1(b.w, e));
}

// smoothstep(e0, e1, x) with the CPU's degenerate case: e0 == e1 is step(e0, x).
fn matSmoothstep1(e0: f32, e1: f32, x: f32) -> f32 {
    if (e0 == e1) {
        return select(1.0, 0.0, x < e0);
    }
    let t = saturate((x - e0) / (e1 - e0));
    return t * t * (3.0 - 2.0 * t);
}

fn matSmoothstep4(e0: f32, e1: f32, a: vec4<f32>) -> vec4<f32> {
    return vec4<f32>(matSmoothstep1(e0, e1, a.x), matSmoothstep1(e0, e1, a.y), matSmoothstep1(e0, e1, a.z),
                     matSmoothstep1(e0, e1, a.w));
}

fn matSafeNormalize(v: vec3<f32>, fallback: vec3<f32>) -> vec3<f32> {
    let len2 = dot(v, v);
    if (len2 > 1e-12) {
        return v * inverseSqrt(len2);
    }
    return fallback;
}

// scene::heightBlendWeight.
fn matHeightBlend(baseHeight: f32, layerHeight: f32, mask: f32, range: f32) -> f32 {
    let m = saturate(mask);
    let a1 = baseHeight + (1.0 - m);
    let a2 = layerHeight + m;
    let top = max(a1, a2) - max(range, 1e-4);
    let b1 = max(a1 - top, 0.0);
    let b2 = max(a2 - top, 0.0);
    return b2 / max(b1 + b2, 1e-6);
}

// scene::triplanarWeights.
fn matTriplanarWeights(n: vec3<f32>, sharpness: f32) -> vec3<f32> {
    var p = sharpness;
    if (p <= 0.0) {
        p = 4.0;
    }
    let w = vec3<f32>(matPow1(abs(n.x), p), matPow1(abs(n.y), p), matPow1(abs(n.z), p));
    let sum = w.x + w.y + w.z;
    if (sum <= 1e-8) {
        return vec3<f32>(1.0 / 3.0);
    }
    return w / sum;
}

// scene::reorientNormal (Barre-Brisebois & Hill 2012).
fn matReorientNormal(base: vec3<f32>, detail: vec3<f32>) -> vec3<f32> {
    let t = base + vec3<f32>(0.0, 0.0, 1.0);
    let u = detail * vec3<f32>(-1.0, -1.0, 1.0);
    let r = t * (dot(t, u) / max(t.z, 1e-5)) - u;
    return matSafeNormalize(r, vec3<f32>(0.0, 0.0, 1.0));
}

// A decal box's per-axis falloff: 1 inside, 0 outside, `soft` wide at the boundary.
fn matDecalFalloff(q: f32, soft: f32) -> f32 {
    if (soft <= 1e-6) {
        return select(0.0, 1.0, q <= 1.0);
    }
    return matSmoothstep1(1.0, 1.0 - soft, q);
}

fn materialInputValue(input: u32, ctx: MaterialContext) -> vec4<f32> {
    if (input == MAT_IN_WORLD_POSITION) {
        return vec4<f32>(ctx.worldPosition, 1.0);
    }
    if (input == MAT_IN_LOCAL_POSITION || input == MAT_IN_OBJECT_POSITION) {
        return vec4<f32>(ctx.localPosition, 1.0);
    }
    if (input == MAT_IN_NORMAL) {
        return vec4<f32>(ctx.normal, 0.0);
    }
    if (input == MAT_IN_UV) {
        return vec4<f32>(ctx.uv, 0.0, 0.0);
    }
    if (input == MAT_IN_OBJECT_ID) {
        return vec4<f32>(ctx.objectId);
    }
    if (input == MAT_IN_INSTANCE_INDEX) {
        return vec4<f32>(ctx.instanceIndex);
    }
    if (input == MAT_IN_INSTANCE_ID) {
        return vec4<f32>(ctx.instanceId);
    }
    if (input == MAT_IN_INSTANCE_RANDOM) {
        return ctx.instanceRandom;
    }
    if (input == MAT_IN_INSTANCE_COLOR) {
        return ctx.instanceColor;
    }
    if (input == MAT_IN_INSTANCE_EMISSIVE) {
        return ctx.instanceEmissive;
    }
    if (input == MAT_IN_TIME) {
        return vec4<f32>(ctx.time);
    }
    if (input == MAT_IN_AUDIO) {
        return ctx.audio;
    }
    if (input == MAT_IN_AUDIO_BANDS) {
        return ctx.audioBands;
    }
    if (input == MAT_IN_BEAT_PHASE) {
        return ctx.beat;
    }
    if (input == MAT_IN_VIEW_DIRECTION) {
        return vec4<f32>(ctx.viewDirection, 0.0);
    }
    if (input == MAT_IN_DEPTH || input == MAT_IN_CAMERA_DISTANCE) {
        return vec4<f32>(ctx.depth);
    }
    if (input == MAT_IN_CURVATURE) {
        return vec4<f32>(ctx.curvature);
    }
    if (input == MAT_IN_CONVEXITY) {
        return vec4<f32>(max(ctx.curvature, 0.0));
    }
    if (input == MAT_IN_CONCAVITY) {
        return vec4<f32>(max(-ctx.curvature, 0.0));
    }
    if (input == MAT_IN_CAVITY) {
        return vec4<f32>(ctx.cavity);
    }
    if (input == MAT_IN_OCCLUSION) {
        return vec4<f32>(ctx.occlusion);
    }
    if (input == MAT_IN_HEIGHT) {
        return vec4<f32>(ctx.height);
    }
    if (input == MAT_IN_NORMAL_VARIANCE) {
        return vec4<f32>(ctx.normalVariance);
    }
    if (input == MAT_IN_TRIPLANAR_WEIGHTS) {
        return vec4<f32>(matTriplanarWeights(ctx.normal, 4.0), 0.0);
    }
    if (input == MAT_IN_MATERIAL_ID) {
        return vec4<f32>(ctx.materialId);
    }
    if (input == MAT_IN_FOOTPRINT) {
        return vec4<f32>(ctx.footprint);
    }
    return vec4<f32>(0.0);
}

// A Field op samples the slot at the world position: scalar fields broadcast to all four
// components, vector fields are (x, y, z, 0), colour fields are (rgb, weight). An invalid slot
// reads as zero (fieldTypeOf reports Scalar and fieldScalar returns 0).
fn materialFieldValue(slot: i32, p: vec3<f32>) -> vec4<f32> {
    let kind = fieldTypeOf(slot);
    if (kind == FIELD_TYPE_VECTOR) {
        return vec4<f32>(fieldVector(slot, p), 0.0);
    }
    if (kind == FIELD_TYPE_COLOR) {
        return fieldColor(slot, p);
    }
    return vec4<f32>(fieldScalar(slot, p));
}

// ---- the interpreter ------------------------------------------------------------------------------

fn materialRegisterInRange(r: i32) -> bool {
    return r >= 0 && r < MAT_REGISTERS;
}

// Evaluates one op against the register file; the caller writes the result to reg[dst].
fn materialEvalOp(op: MaterialOpGpu, ctx: MaterialContext, regs: MatRegs) -> vec4<f32> {
    let a = regs[op.registers.y];
    let b = regs[op.registers.z];
    let c = regs[op.registers.w];
    let k = op.constant;
    let f = op.valuePad.x;
    if (op.kind == MAT_OP_INPUT) {
        return materialInputValue(op.input, ctx);
    }
    if (op.kind == MAT_OP_CONSTANT) {
        return k;
    }
    if (op.kind == MAT_OP_GRADIENT) {
        return vec4<f32>(saturate(dot(a.xyz, k.xyz) * f + k.w));
    }
    if (op.kind == MAT_OP_NOISE) {
        return vec4<f32>(fbm3(a.xyz * f + k.xyz, op.seed));
    }
    if (op.kind == MAT_OP_VORONOI) {
        return vec4<f32>(voronoiF1(a.xyz * f + k.xyz, op.seed));
    }
    if (op.kind == MAT_OP_FRESNEL) {
        return vec4<f32>(matPow1(1.0 - saturate(dot(ctx.normal, ctx.viewDirection)), f));
    }
    if (op.kind == MAT_OP_RAMP) {
        return ramp3(op.constant, op.constant2, op.constant3, a.x);
    }
    if (op.kind == MAT_OP_REMAP) {
        let inSpan = k.y - k.x;
        var u = vec4<f32>(0.0);
        if (inSpan != 0.0) {
            u = (a - vec4<f32>(k.x)) / inSpan;
        }
        var remapped = u * (k.w - k.z) + vec4<f32>(k.z);
        if (f > 0.5) {
            remapped = clamp(remapped, vec4<f32>(min(k.z, k.w)), vec4<f32>(max(k.z, k.w)));
        }
        return remapped;
    }
    if (op.kind == MAT_OP_MULTIPLY) {
        return a * b;
    }
    if (op.kind == MAT_OP_ADD) {
        return a + b;
    }
    if (op.kind == MAT_OP_MIX) {
        return matMix4(a, b, f);
    }
    if (op.kind == MAT_OP_MIXBY) {
        return matMix4(a, b, c.x);
    }
    if (op.kind == MAT_OP_POWER) {
        return matPow4(max(a, vec4<f32>(0.0)), f);
    }
    if (op.kind == MAT_OP_SMOOTHSTEP) {
        return matSmoothstep4(k.x, k.y, a);
    }
    if (op.kind == MAT_OP_THRESHOLD) {
        return select(vec4<f32>(0.0), vec4<f32>(1.0), a >= vec4<f32>(f));
    }
    if (op.kind == MAT_OP_HUESHIFT) {
        return vec4<f32>(hueShift(a.xyz, f + b.x), a.w);
    }
    if (op.kind == MAT_OP_SATURATE) {
        return vec4<f32>(colorSaturate(a.xyz, f), a.w);
    }
    if (op.kind == MAT_OP_PALETTE) {
        return vec4<f32>(cosinePalette(op.constant.xyz, op.constant2.xyz, op.constant3.xyz, op.constant4.xyz,
                                       a.x + f),
                         1.0);
    }
    if (op.kind == MAT_OP_FIELD) {
        return materialFieldValue(op.fieldSlot, ctx.worldPosition);
    }
    if (op.kind == MAT_OP_TRIPLANAR) {
        let p = a.xyz * f + k.xyz;
        let w = matTriplanarWeights(ctx.normal, op.constant2.x);
        let sx = fbm3(vec3<f32>(p.y, p.z, 0.0), op.seed);
        let sy = fbm3(vec3<f32>(p.z, p.x, 0.0), op.seed);
        let sz = fbm3(vec3<f32>(p.x, p.y, 0.0), op.seed);
        return vec4<f32>(w.x * sx + w.y * sy + w.z * sz);
    }
    if (op.kind == MAT_OP_WORLD_PROJECT) {
        return vec4<f32>(ctx.worldPosition * f + k.xyz, 1.0);
    }
    if (op.kind == MAT_OP_OBJECT_PROJECT) {
        return vec4<f32>(ctx.localPosition * f + k.xyz, 1.0);
    }
    if (op.kind == MAT_OP_HEIGHT_BLEND) {
        return vec4<f32>(matHeightBlend(a.x, b.x, c.x, f));
    }
    if (op.kind == MAT_OP_DETAIL_NORMAL) {
        return vec4<f32>(matReorientNormal(a.xyz, b.xyz), 0.0);
    }
    if (op.kind == MAT_OP_CURVATURE_MASK) {
        return vec4<f32>(matSmoothstep1(k.x, k.y, ctx.curvature * f));
    }
    if (op.kind == MAT_OP_EDGE_WEAR) {
        let edge = saturate(max(ctx.curvature, 0.0) * f);
        let nz = fbm3(ctx.worldPosition * k.x + vec3<f32>(k.y, k.z, k.w), op.seed);
        let influence = saturate(op.constant2.x);
        let v = edge * (1.0 - influence + influence * nz);
        return vec4<f32>(matSmoothstep1(op.constant2.y, op.constant2.z, v));
    }
    if (op.kind == MAT_OP_DECAL_BOX) {
        let halfExtent = max(abs(op.constant2.xyz), vec3<f32>(1e-4));
        let q = (ctx.worldPosition - k.xyz) / halfExtent;
        let soft = saturate(f);
        let mask =
            matDecalFalloff(abs(q.x), soft) * matDecalFalloff(abs(q.y), soft) * matDecalFalloff(abs(q.z), soft);
        return vec4<f32>(q.x * 0.5 + 0.5, q.y * 0.5 + 0.5, mask, mask);
    }
    if (op.kind == MAT_OP_ANISOTROPY) {
        let roughness = max(a.x, 0.0);
        let alpha = roughness * roughness;
        let aniso = clamp(f, -0.95, 0.95);
        let alphaT = alpha * (1.0 + aniso);
        let alphaB = alpha * (1.0 - aniso);
        let n = ctx.normal;
        let tangent = k.xyz - n * dot(n, k.xyz);
        if (dot(tangent, tangent) <= 1e-10) {
            return vec4<f32>(roughness);
        }
        let t = tangent * inverseSqrt(dot(tangent, tangent));
        let viewTangent = ctx.viewDirection - n * dot(n, ctx.viewDirection);
        var cos2 = 0.5;
        if (dot(viewTangent, viewTangent) > 1e-10) {
            let cosine = dot(viewTangent * inverseSqrt(dot(viewTangent, viewTangent)), t);
            cos2 = saturate(cosine * cosine);
        }
        let alphaEff = sqrt(alphaT * alphaT * cos2 + alphaB * alphaB * (1.0 - cos2));
        return vec4<f32>(sqrt(alphaEff));
    }
    if (op.kind == MAT_OP_ROUGHNESS_FILTER) {
        let roughness = max(a.x, 0.0);
        let alpha = roughness * roughness;
        let kernel = min(2.0 * max(f, 0.0) * max(ctx.normalVariance, 0.0), 0.18);
        return vec4<f32>(sqrt(min(alpha + kernel, 1.0)));
    }
    if (op.kind == MAT_OP_MICRO_DETAIL) {
        let p = a.xyz * f + k.xyz;
        let fade = saturate(1.0 - ctx.footprint * abs(f) * 2.0);
        return vec4<f32>(0.5 + (fbm3(p, op.seed) - 0.5) * fade);
    }
    return vec4<f32>(0.0);
}

// Runs ops [first, first + count) of program `pi` over `regs`.
fn materialRunOps(pi: u32, first: u32, count: u32, ctx: MaterialContext, regsIn: MatRegs) -> MatRegs {
    var regs = regsIn;
    for (var i = 0u; i < MAT_MAX_OPS; i = i + 1u) {
        if (i >= count) {
            break;
        }
        let index = first + i;
        if (index >= MAT_MAX_OPS) {
            break;
        }
        let op = materialPrograms.programs[pi].ops[index];
        let dst = op.registers.x;
        if (!materialRegisterInRange(dst) || !materialRegisterInRange(op.registers.y) ||
            !materialRegisterInRange(op.registers.z) || !materialRegisterInRange(op.registers.w)) {
            continue;
        }
        regs[dst] = materialEvalOp(op, ctx, regs);
    }
    return regs;
}

// Evaluates the program in slot `programIndex` for `ctx`, starting from the material's own values
// in `base`. A programIndex outside 0..count-1 returns `base` unchanged.
fn evaluateMaterialProgram(programIndex: i32, ctx: MaterialContext, base: MaterialResult) -> MaterialResult {
    if (programIndex < 0 || programIndex >= MAT_MAX_PROGRAMS || u32(programIndex) >= materialPrograms.count) {
        return base;
    }
    let pi = u32(programIndex);
    var regs = MatRegs(vec4<f32>(0.0), vec4<f32>(0.0), vec4<f32>(0.0), vec4<f32>(0.0), vec4<f32>(0.0),
                       vec4<f32>(0.0), vec4<f32>(0.0), vec4<f32>(0.0));
    var local = ctx;
    let baseCount = u32(max(materialPrograms.programs[pi].opacityCountPad.y, 0));
    regs = materialRunOps(pi, 0u, baseCount, local, regs);

    var result = base;
    let outputs = materialPrograms.programs[pi].outputs;
    let opacityRegister = materialPrograms.programs[pi].opacityCountPad.x;
    let aux = materialPrograms.programs[pi].aux;
    if (materialRegisterInRange(outputs.x)) {
        result.baseColor = max(regs[outputs.x].xyz, vec3<f32>(0.0));
    }
    if (materialRegisterInRange(outputs.y)) {
        result.metallic = saturate(regs[outputs.y].x);
    }
    if (materialRegisterInRange(outputs.z)) {
        result.roughness = saturate(regs[outputs.z].x);
    }
    if (materialRegisterInRange(outputs.w)) {
        result.emission = regs[outputs.w].xyz * materialPrograms.programs[pi].emissionIntensityPad.x;
    }
    if (materialRegisterInRange(opacityRegister)) {
        result.opacity = saturate(regs[opacityRegister].x);
    }
    if (materialRegisterInRange(aux.x)) {
        result.normal = matSafeNormalize(regs[aux.x].xyz, vec3<f32>(0.0, 0.0, 1.0));
    }
    if (materialRegisterInRange(aux.y)) {
        result.occlusion = saturate(regs[aux.y].x);
    }
    if (materialRegisterInRange(aux.z)) {
        result.height = regs[aux.z].x;
    }

    // ---- layers (ADR-036) ----
    let layerCount = u32(max(materialPrograms.programs[pi].opacityCountPad.z, 0));
    for (var li = 0u; li < MAT_MAX_LAYERS; li = li + 1u) {
        if (li >= layerCount) {
            break;
        }
        let layer = materialPrograms.programs[pi].layers[li];
        local.height = result.height;
        regs = materialRunOps(pi, u32(max(layer.range.x, 0)), u32(max(layer.range.y, 0)), local, regs);
        var mask = 1.0;
        if (materialRegisterInRange(layer.aux.z)) {
            mask = saturate(regs[layer.aux.z].x);
        }
        var layerHeight = 0.0;
        if (materialRegisterInRange(layer.aux.w)) {
            layerHeight = regs[layer.aux.w].x;
        }
        let t = matHeightBlend(result.height, layerHeight, mask, layer.params.y);
        if (materialRegisterInRange(layer.outputs.x)) {
            result.baseColor = mix(result.baseColor, max(regs[layer.outputs.x].xyz, vec3<f32>(0.0)), t);
        }
        if (materialRegisterInRange(layer.outputs.y)) {
            result.metallic = mix(result.metallic, saturate(regs[layer.outputs.y].x), t);
        }
        if (materialRegisterInRange(layer.outputs.z)) {
            result.roughness = mix(result.roughness, saturate(regs[layer.outputs.z].x), t);
        }
        if (materialRegisterInRange(layer.outputs.w)) {
            result.emission = mix(result.emission, regs[layer.outputs.w].xyz * layer.params.x, t);
        }
        if (materialRegisterInRange(layer.aux.x)) {
            let value = matSafeNormalize(regs[layer.aux.x].xyz, vec3<f32>(0.0, 0.0, 1.0));
            result.normal = matSafeNormalize(mix(result.normal, value, t), vec3<f32>(0.0, 0.0, 1.0));
        }
        if (materialRegisterInRange(layer.aux.y)) {
            result.occlusion = mix(result.occlusion, saturate(regs[layer.aux.y].x), t);
        }
        result.height = mix(result.height, layerHeight, t);
    }
    return result;
}
