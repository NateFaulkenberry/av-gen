// Procedural materials on the GPU (ADR-030, layered in ADR-036): the WGSL transliteration of
// scene::evaluateMaterialProgram (src/scene/material_program.cpp). A packed MaterialProgramGpu
// (5712 bytes: an 80-byte header, 4 layer records of 64 bytes and 48 ops of 112 bytes,
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
    value: f32,                   // the scalar `f`
    fieldOrdinal: i32,            // Field op: which of MaterialProgramGpu.fieldSlots, -1 = none
    pad0: f32,
    pad1: f32,
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
    fieldSlots: vec4<i32>,        // FieldBlock slot of each distinct field named, -1 = unused
    layers: array<MaterialLayerGpu, 4>,
    ops: array<MaterialOpGpu, 48>,
};

// Mirrors rendering::MaterialProgramBlock (45712 bytes; a storage buffer since ADR-036).
struct MaterialProgramBlock {
    count: u32,
    pad0: u32,
    pad1: u32,
    pad2: u32,
    programs: array<MaterialProgramGpu, 8>,
};

const MAT_MAX_OPS: u32 = 48u;
const MAT_MAX_LAYERS: u32 = 4u;
const MAT_MAX_FIELDS: u32 = 4u;   // scene::kMaxMaterialFields
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
const MAT_OP_SWIZZLE: u32 = 30u;

fn matPick(v: vec4<f32>, index: f32) -> f32 {
    return v[clamp(i32(index), 0, 3)];
}

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

// The register file. Eight vec4s, and deliberately *not* an array: a dynamically indexed local
// array cannot live in registers, so on Metal it becomes thread-private indexable memory, and
// `regs[dst] = f(regs[srcA], ...)` then becomes a loop-carried dependency through memory whose very
// addresses arrive from another load. That chain -- not the arithmetic, and not the op fetch --
// was a third of the interpreter's cost: see ADR-050 and the probes in
// tests/rendering/test_material_perf.cpp. Named fields plus a select tree keep the file in
// registers and turn the chain into ALU.
struct MatRegs {
    r0: vec4<f32>,
    r1: vec4<f32>,
    r2: vec4<f32>,
    r3: vec4<f32>,
    r4: vec4<f32>,
    r5: vec4<f32>,
    r6: vec4<f32>,
    r7: vec4<f32>,
};

// The program's distinct field samples, one per fieldSlots entry. Four named fields, again not an
// array: the point of the pre-pass is to keep the field evaluator out of the interpreter's loop,
// and putting its results in indexable memory would give back what that buys.
struct MatFields {
    f0: vec4<f32>,
    f1: vec4<f32>,
    f2: vec4<f32>,
    f3: vec4<f32>,
};

fn matFieldGet(f: MatFields, index: i32) -> vec4<f32> {
    if (index < 0) {
        return vec4<f32>(0.0);   // more distinct fields than kMaxMaterialFields, or none at all
    }
    let i = index & 3;
    let h0 = select(f.f0, f.f1, (i & 1) != 0);
    let h1 = select(f.f2, f.f3, (i & 1) != 0);
    return select(h0, h1, (i & 2) != 0);
}

fn matFieldSet(f: ptr<function, MatFields>, index: i32, v: vec4<f32>) {
    let i = index & 3;
    (*f).f0 = select((*f).f0, v, i == 0);
    (*f).f1 = select((*f).f1, v, i == 1);
    (*f).f2 = select((*f).f2, v, i == 2);
    (*f).f3 = select((*f).f3, v, i == 3);
}

fn matRegsZero() -> MatRegs {
    return MatRegs(vec4<f32>(0.0), vec4<f32>(0.0), vec4<f32>(0.0), vec4<f32>(0.0), vec4<f32>(0.0),
                   vec4<f32>(0.0), vec4<f32>(0.0), vec4<f32>(0.0));
}

// A balanced select tree: seven selects, branch-free, no memory. Callers have already checked the
// index is 0..7 (materialRegisterInRange); the mask makes an unchecked one wrap rather than fault.
fn matGet(regs: MatRegs, index: i32) -> vec4<f32> {
    let i = index & 7;
    let h0 = select(regs.r0, regs.r1, (i & 1) != 0);
    let h1 = select(regs.r2, regs.r3, (i & 1) != 0);
    let h2 = select(regs.r4, regs.r5, (i & 1) != 0);
    let h3 = select(regs.r6, regs.r7, (i & 1) != 0);
    let q0 = select(h0, h1, (i & 2) != 0);
    let q1 = select(h2, h3, (i & 2) != 0);
    return select(q0, q1, (i & 4) != 0);
}

fn matSet(regs: ptr<function, MatRegs>, index: i32, v: vec4<f32>) {
    let i = index & 7;
    (*regs).r0 = select((*regs).r0, v, i == 0);
    (*regs).r1 = select((*regs).r1, v, i == 1);
    (*regs).r2 = select((*regs).r2, v, i == 2);
    (*regs).r3 = select((*regs).r3, v, i == 3);
    (*regs).r4 = select((*regs).r4, v, i == 4);
    (*regs).r5 = select((*regs).r5, v, i == 5);
    (*regs).r6 = select((*regs).r6, v, i == 6);
    (*regs).r7 = select((*regs).r7, v, i == 7);
}

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

// The op record read one field at a time. `let op = ...ops[oi]` instead copies all 112 bytes into
// the live set of every op, and on this backend the live set is what costs: the interpreter's
// dependency chain can only be hidden by occupancy, and occupancy is what a large live set spends.
// Reading through these lets each constant sink into the one branch that wants it, which is worth
// 16% of the interpreter (ADR-050).
fn matOpInput(pi: u32, oi: u32) -> u32 { return materialPrograms.programs[pi].ops[oi].input; }
fn matOpSeed(pi: u32, oi: u32) -> u32 { return materialPrograms.programs[pi].ops[oi].seed; }
fn matOpFieldOrdinal(pi: u32, oi: u32) -> i32 { return materialPrograms.programs[pi].ops[oi].fieldOrdinal; }
fn matOpConstant2(pi: u32, oi: u32) -> vec4<f32> { return materialPrograms.programs[pi].ops[oi].constant2; }
fn matOpConstant3(pi: u32, oi: u32) -> vec4<f32> { return materialPrograms.programs[pi].ops[oi].constant3; }
fn matOpConstant4(pi: u32, oi: u32) -> vec4<f32> { return materialPrograms.programs[pi].ops[oi].constant4; }

// Evaluates one op against the register file; the caller writes the result to register `dst`.
// Takes the op by (program, index) rather than by value: a `let op = ...ops[oi]` copies all 112
// bytes into the live set of every op, and it is that live set, not the loads, that costs --
// the interpreter's register-to-register dependency chain is only hidable by occupancy.
// Reading each field where it is used lets the four constants sink into the five branches
// that want them.
fn materialEvalOp(pi: u32, oi: u32, ctx: MaterialContext, regs: MatRegs, fields: MatFields) -> vec4<f32> {
    let kind = materialPrograms.programs[pi].ops[oi].kind;
    let reg = materialPrograms.programs[pi].ops[oi].registers;
    let a = matGet(regs, reg.y);
    let b = matGet(regs, reg.z);
    let c = matGet(regs, reg.w);
    let k = materialPrograms.programs[pi].ops[oi].constant;
    let f = materialPrograms.programs[pi].ops[oi].value;
    if (kind == MAT_OP_INPUT) {
        return materialInputValue(matOpInput(pi, oi), ctx);
    }
    if (kind == MAT_OP_CONSTANT) {
        return k;
    }
    if (kind == MAT_OP_GRADIENT) {
        return vec4<f32>(saturate(dot(a.xyz, k.xyz) * f + k.w));
    }
    if (kind == MAT_OP_NOISE) {
        return vec4<f32>(fbm3(a.xyz * f + k.xyz, matOpSeed(pi, oi)));
    }
    if (kind == MAT_OP_VORONOI) {
        return vec4<f32>(voronoiF1(a.xyz * f + k.xyz, matOpSeed(pi, oi)));
    }
    if (kind == MAT_OP_FRESNEL) {
        return vec4<f32>(matPow1(1.0 - saturate(dot(ctx.normal, ctx.viewDirection)), f));
    }
    if (kind == MAT_OP_RAMP) {
        return ramp3(k, matOpConstant2(pi, oi), matOpConstant3(pi, oi), a.x);
    }
    if (kind == MAT_OP_REMAP) {
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
    if (kind == MAT_OP_MULTIPLY) {
        return a * b;
    }
    if (kind == MAT_OP_ADD) {
        return a + b;
    }
    if (kind == MAT_OP_MIX) {
        return matMix4(a, b, f);
    }
    if (kind == MAT_OP_MIXBY) {
        return matMix4(a, b, c.x);
    }
    if (kind == MAT_OP_POWER) {
        return matPow4(max(a, vec4<f32>(0.0)), f);
    }
    if (kind == MAT_OP_SMOOTHSTEP) {
        return matSmoothstep4(k.x, k.y, a);
    }
    if (kind == MAT_OP_THRESHOLD) {
        return select(vec4<f32>(0.0), vec4<f32>(1.0), a >= vec4<f32>(f));
    }
    if (kind == MAT_OP_HUESHIFT) {
        return vec4<f32>(hueShift(a.xyz, f + b.x), a.w);
    }
    if (kind == MAT_OP_SATURATE) {
        return vec4<f32>(colorSaturate(a.xyz, f), a.w);
    }
    if (kind == MAT_OP_PALETTE) {
        return vec4<f32>(cosinePalette(k.xyz, matOpConstant2(pi, oi).xyz, matOpConstant3(pi, oi).xyz,
                                       matOpConstant4(pi, oi).xyz, a.x + f),
                         1.0);
    }
    if (kind == MAT_OP_FIELD) {
        return matFieldGet(fields, matOpFieldOrdinal(pi, oi));
    }
    if (kind == MAT_OP_TRIPLANAR) {
        let p = a.xyz * f + k.xyz;
        let w = matTriplanarWeights(ctx.normal, matOpConstant2(pi, oi).x);
        let sx = fbm3(vec3<f32>(p.y, p.z, 0.0), matOpSeed(pi, oi));
        let sy = fbm3(vec3<f32>(p.z, p.x, 0.0), matOpSeed(pi, oi));
        let sz = fbm3(vec3<f32>(p.x, p.y, 0.0), matOpSeed(pi, oi));
        return vec4<f32>(w.x * sx + w.y * sy + w.z * sz);
    }
    if (kind == MAT_OP_WORLD_PROJECT) {
        return vec4<f32>(ctx.worldPosition * f + k.xyz, 1.0);
    }
    if (kind == MAT_OP_OBJECT_PROJECT) {
        return vec4<f32>(ctx.localPosition * f + k.xyz, 1.0);
    }
    if (kind == MAT_OP_HEIGHT_BLEND) {
        return vec4<f32>(matHeightBlend(a.x, b.x, c.x, f));
    }
    if (kind == MAT_OP_DETAIL_NORMAL) {
        return vec4<f32>(matReorientNormal(a.xyz, b.xyz), 0.0);
    }
    if (kind == MAT_OP_CURVATURE_MASK) {
        return vec4<f32>(matSmoothstep1(k.x, k.y, ctx.curvature * f));
    }
    if (kind == MAT_OP_EDGE_WEAR) {
        let edge = saturate(max(ctx.curvature, 0.0) * f);
        let nz = fbm3(ctx.worldPosition * k.x + vec3<f32>(k.y, k.z, k.w), matOpSeed(pi, oi));
        let influence = saturate(matOpConstant2(pi, oi).x);
        let v = edge * (1.0 - influence + influence * nz);
        return vec4<f32>(matSmoothstep1(matOpConstant2(pi, oi).y, matOpConstant2(pi, oi).z, v));
    }
    if (kind == MAT_OP_DECAL_BOX) {
        let halfExtent = max(abs(matOpConstant2(pi, oi).xyz), vec3<f32>(1e-4));
        let q = (ctx.worldPosition - k.xyz) / halfExtent;
        let soft = saturate(f);
        let mask =
            matDecalFalloff(abs(q.x), soft) * matDecalFalloff(abs(q.y), soft) * matDecalFalloff(abs(q.z), soft);
        return vec4<f32>(q.x * 0.5 + 0.5, q.y * 0.5 + 0.5, mask, mask);
    }
    if (kind == MAT_OP_ANISOTROPY) {
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
    if (kind == MAT_OP_ROUGHNESS_FILTER) {
        let roughness = max(a.x, 0.0);
        let alpha = roughness * roughness;
        let kernel = min(2.0 * max(f, 0.0) * max(ctx.normalVariance, 0.0), 0.18);
        return vec4<f32>(sqrt(min(alpha + kernel, 1.0)));
    }
    if (kind == MAT_OP_MICRO_DETAIL) {
        let p = a.xyz * f + k.xyz;
        let fade = saturate(1.0 - ctx.footprint * abs(f) * 2.0);
        return vec4<f32>(0.5 + (fbm3(p, matOpSeed(pi, oi)) - 0.5) * fade);
    }
    if (kind == MAT_OP_SWIZZLE) {
        // `constant` names the source component of each output channel. The default all-zero
        // constant broadcasts x, which is the case that matters: it takes a value that arrived in
        // some other channel and puts it where every mask op looks for it.
        return vec4<f32>(matPick(a, k.x), matPick(a, k.y), matPick(a, k.z), matPick(a, k.w));
    }
    return vec4<f32>(0.0);
}

// Samples the program's distinct fields once, before any op runs.
//
// This is the single largest thing in the interpreter's cost, and not for the reason it looks
// like. A Field op inlines fields.wgsl's whole evaluator -- combineScalar's four-child loop over
// basicScalar, each of those a grid fetch, a falloff and a noise -- into the *body of the op
// loop*, and the loop body's size is what sets the shader's register allocation and so its
// occupancy. Every material program paid for that, including ones with no Field op and including
// programs of zero ops: measured at 2.2x on an M2 Max. The same evaluator called from outside the
// loop costs nothing measurable.
//
// Hoisting is exact rather than an approximation: a Field op's value is materialFieldValue(slot,
// ctx.worldPosition), and neither argument changes while the program runs. Ops naming the same
// field share an ordinal, so each field is sampled once however often it is read.
fn materialFieldPrepass(pi: u32, p: vec3<f32>) -> MatFields {
    let slots = materialPrograms.programs[pi].fieldSlots;
    var f = MatFields(vec4<f32>(0.0), vec4<f32>(0.0), vec4<f32>(0.0), vec4<f32>(0.0));
    // A loop rather than four calls: one inlined copy of the evaluator is the whole point.
    for (var k = 0; k < i32(MAT_MAX_FIELDS); k = k + 1) {
        let slot = slots[k];
        if (slot >= 0) {
            matFieldSet(&f, k, materialFieldValue(slot, p));
        }
    }
    return f;
}

// Runs ops [first, first + count) of program `pi` over `regs`.
fn materialRunOps(pi: u32, first: u32, count: u32, ctx: MaterialContext, regsIn: MatRegs,
                  fields: MatFields) -> MatRegs {
    var regs = regsIn;
    for (var i = 0u; i < MAT_MAX_OPS; i = i + 1u) {
        if (i >= count) {
            break;
        }
        let index = first + i;
        if (index >= MAT_MAX_OPS) {
            break;
        }
        let reg = materialPrograms.programs[pi].ops[index].registers;
        let dst = reg.x;
        if (!materialRegisterInRange(dst) || !materialRegisterInRange(reg.y) ||
            !materialRegisterInRange(reg.z) || !materialRegisterInRange(reg.w)) {
            continue;
        }
        matSet(&regs, dst, materialEvalOp(pi, index, ctx, regs, fields));
    }
    return regs;
}

// Evaluates the program in slot `programIndex` for `ctx`, starting from the material's own values
// in `base`. A programIndex outside 0..count-1 returns `base` unchanged.
// Whether program `programIndex` runs on this fragment at all (scene::MaterialGate). Header lanes:
// opacityCountPad.w = the tested instanceRandom component + 1 (0 = no instance test);
// emissionIntensityPad = (intensity, below, near, far), near/far 0 = no edge. A fragment the gate
// refuses takes the program-less path, so the caller asks this BEFORE deciding which path it is
// on -- see pbr_shade.wgsl.
fn materialProgramAdmits(programIndex: i32, instanceRandom: vec4<f32>, cameraDistance: f32) -> bool {
    if (programIndex < 0 || programIndex >= MAT_MAX_PROGRAMS || u32(programIndex) >= materialPrograms.count) {
        return true; // not a gate's business: evaluateMaterialProgram answers an unknown index itself
    }
    let pi = u32(programIndex);
    let lane = materialPrograms.programs[pi].opacityCountPad.w;
    let g = materialPrograms.programs[pi].emissionIntensityPad;
    if (lane > 0 && !(instanceRandom[u32(min(lane, 4)) - 1u] < g.y)) {
        return false;
    }
    return cameraDistance >= g.z && (g.w <= 0.0 || cameraDistance < g.w);
}

fn evaluateMaterialProgram(programIndex: i32, ctx: MaterialContext, base: MaterialResult) -> MaterialResult {
    if (programIndex < 0 || programIndex >= MAT_MAX_PROGRAMS || u32(programIndex) >= materialPrograms.count) {
        return base;
    }
    let pi = u32(programIndex);
    var regs = matRegsZero();
    var local = ctx;
    let fields = materialFieldPrepass(pi, ctx.worldPosition);
    let baseCount = u32(max(materialPrograms.programs[pi].opacityCountPad.y, 0));
    regs = materialRunOps(pi, 0u, baseCount, local, regs, fields);

    var result = base;
    let outputs = materialPrograms.programs[pi].outputs;
    let opacityRegister = materialPrograms.programs[pi].opacityCountPad.x;
    let aux = materialPrograms.programs[pi].aux;
    if (materialRegisterInRange(outputs.x)) {
        result.baseColor = max(matGet(regs, outputs.x).xyz, vec3<f32>(0.0));
    }
    if (materialRegisterInRange(outputs.y)) {
        result.metallic = saturate(matGet(regs, outputs.y).x);
    }
    if (materialRegisterInRange(outputs.z)) {
        result.roughness = saturate(matGet(regs, outputs.z).x);
    }
    if (materialRegisterInRange(outputs.w)) {
        result.emission = matGet(regs, outputs.w).xyz * materialPrograms.programs[pi].emissionIntensityPad.x;
    }
    if (materialRegisterInRange(opacityRegister)) {
        result.opacity = saturate(matGet(regs, opacityRegister).x);
    }
    if (materialRegisterInRange(aux.x)) {
        result.normal = matSafeNormalize(matGet(regs, aux.x).xyz, vec3<f32>(0.0, 0.0, 1.0));
    }
    if (materialRegisterInRange(aux.y)) {
        result.occlusion = saturate(matGet(regs, aux.y).x);
    }
    if (materialRegisterInRange(aux.z)) {
        result.height = matGet(regs, aux.z).x;
    }

    // ---- layers (ADR-036) ----
    let layerCount = u32(max(materialPrograms.programs[pi].opacityCountPad.z, 0));
    for (var li = 0u; li < MAT_MAX_LAYERS; li = li + 1u) {
        if (li >= layerCount) {
            break;
        }
        let layer = materialPrograms.programs[pi].layers[li];
        local.height = result.height;
        regs = materialRunOps(pi, u32(max(layer.range.x, 0)), u32(max(layer.range.y, 0)), local, regs, fields);
        var mask = 1.0;
        if (materialRegisterInRange(layer.aux.z)) {
            mask = saturate(matGet(regs, layer.aux.z).x);
        }
        var layerHeight = 0.0;
        if (materialRegisterInRange(layer.aux.w)) {
            layerHeight = matGet(regs, layer.aux.w).x;
        }
        let t = matHeightBlend(result.height, layerHeight, mask, layer.params.y);
        if (materialRegisterInRange(layer.outputs.x)) {
            result.baseColor = mix(result.baseColor, max(matGet(regs, layer.outputs.x).xyz, vec3<f32>(0.0)), t);
        }
        if (materialRegisterInRange(layer.outputs.y)) {
            result.metallic = mix(result.metallic, saturate(matGet(regs, layer.outputs.y).x), t);
        }
        if (materialRegisterInRange(layer.outputs.z)) {
            result.roughness = mix(result.roughness, saturate(matGet(regs, layer.outputs.z).x), t);
        }
        if (materialRegisterInRange(layer.outputs.w)) {
            result.emission = mix(result.emission, matGet(regs, layer.outputs.w).xyz * layer.params.x, t);
        }
        if (materialRegisterInRange(layer.aux.x)) {
            let value = matSafeNormalize(matGet(regs, layer.aux.x).xyz, vec3<f32>(0.0, 0.0, 1.0));
            result.normal = matSafeNormalize(mix(result.normal, value, t), vec3<f32>(0.0, 0.0, 1.0));
        }
        if (materialRegisterInRange(layer.aux.y)) {
            result.occlusion = mix(result.occlusion, saturate(matGet(regs, layer.aux.y).x), t);
        }
        result.height = mix(result.height, layerHeight, t);
    }
    return result;
}
