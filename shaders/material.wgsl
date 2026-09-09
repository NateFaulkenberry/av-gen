// Procedural materials on the GPU (ADR-030): the WGSL transliteration of
// scene::evaluateMaterialProgram (src/scene/material_program.cpp). A packed MaterialProgramGpu
// (1840 bytes: a 48-byte header + 16 ops of 112 bytes, scene::packMaterialProgram) per slot in a
// uniform MaterialProgramBlock; evaluateMaterialProgram(slot, ctx, base) runs the ops over a
// register file of 8 vec4s (all zero at entry) and returns the material values, with a register
// index of -1 meaning "keep the material's own value" (the matching field of `base`).
// tests/rendering/test_material_gpu.cpp compares the two sides within 1e-4 (noise/voronoi 1e-3).
// The exact per-op formulas are in docs/procedural-materials.md.
//
// The including module declares the binding itself, e.g.
//   @group(2) @binding(6) var<uniform> materialPrograms: MaterialProgramBlock;
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

struct MaterialProgramGpu {
    outputs: vec4<i32>,           // baseColor, metallic, roughness, emission registers (-1 = keep)
    opacityCountPad: vec4<i32>,   // opacity register, op count, 0, 0
    emissionIntensityPad: vec4<f32>,
    ops: array<MaterialOpGpu, 16>,
};

// Mirrors rendering::MaterialProgramBlock (14736 bytes).
struct MaterialProgramBlock {
    count: u32,
    pad0: u32,
    pad1: u32,
    pad2: u32,
    programs: array<MaterialProgramGpu, 8>,
};

const MAT_MAX_OPS: u32 = 16u;
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
};

// The material values a program reads from (`base`) and writes (scene::MaterialResult without
// the register file, which the shader keeps in a local).
struct MaterialResult {
    baseColor: vec3<f32>,
    metallic: f32,
    roughness: f32,
    emission: vec3<f32>,
    opacity: f32,
};

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
    return ctx;
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

fn materialInputValue(input: u32, ctx: MaterialContext) -> vec4<f32> {
    if (input == MAT_IN_WORLD_POSITION) {
        return vec4<f32>(ctx.worldPosition, 1.0);
    }
    if (input == MAT_IN_LOCAL_POSITION) {
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
    if (input == MAT_IN_DEPTH) {
        return vec4<f32>(ctx.depth);
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

// Evaluates the program in slot `programIndex` for `ctx`, starting from the material's own values
// in `base`. A programIndex outside 0..count-1 returns `base` unchanged.
fn evaluateMaterialProgram(programIndex: i32, ctx: MaterialContext, base: MaterialResult) -> MaterialResult {
    if (programIndex < 0 || programIndex >= MAT_MAX_PROGRAMS || u32(programIndex) >= materialPrograms.count) {
        return base;
    }
    let pi = u32(programIndex);
    var regs = array<vec4<f32>, 8>(vec4<f32>(0.0), vec4<f32>(0.0), vec4<f32>(0.0), vec4<f32>(0.0), vec4<f32>(0.0),
                                   vec4<f32>(0.0), vec4<f32>(0.0), vec4<f32>(0.0));
    let count = u32(max(materialPrograms.programs[pi].opacityCountPad.y, 0));
    for (var i = 0u; i < MAT_MAX_OPS; i = i + 1u) {
        if (i >= count) {
            break;
        }
        let op = materialPrograms.programs[pi].ops[i];
        let dst = op.registers.x;
        if (!materialRegisterInRange(dst) || !materialRegisterInRange(op.registers.y) ||
            !materialRegisterInRange(op.registers.z) || !materialRegisterInRange(op.registers.w)) {
            continue;
        }
        let a = regs[op.registers.y];
        let b = regs[op.registers.z];
        let c = regs[op.registers.w];
        let k = op.constant;
        let f = op.valuePad.x;
        var value = vec4<f32>(0.0);
        if (op.kind == MAT_OP_INPUT) {
            value = materialInputValue(op.input, ctx);
        } else if (op.kind == MAT_OP_CONSTANT) {
            value = k;
        } else if (op.kind == MAT_OP_GRADIENT) {
            value = vec4<f32>(saturate(dot(a.xyz, k.xyz) * f + k.w));
        } else if (op.kind == MAT_OP_NOISE) {
            value = vec4<f32>(fbm3(a.xyz * f + k.xyz, op.seed));
        } else if (op.kind == MAT_OP_VORONOI) {
            value = vec4<f32>(voronoiF1(a.xyz * f + k.xyz, op.seed));
        } else if (op.kind == MAT_OP_FRESNEL) {
            value = vec4<f32>(matPow1(1.0 - saturate(dot(ctx.normal, ctx.viewDirection)), f));
        } else if (op.kind == MAT_OP_RAMP) {
            value = ramp3(op.constant, op.constant2, op.constant3, a.x);
        } else if (op.kind == MAT_OP_REMAP) {
            let inSpan = k.y - k.x;
            var u = vec4<f32>(0.0);
            if (inSpan != 0.0) {
                u = (a - vec4<f32>(k.x)) / inSpan;
            }
            var remapped = u * (k.w - k.z) + vec4<f32>(k.z);
            if (f > 0.5) {
                remapped = clamp(remapped, vec4<f32>(min(k.z, k.w)), vec4<f32>(max(k.z, k.w)));
            }
            value = remapped;
        } else if (op.kind == MAT_OP_MULTIPLY) {
            value = a * b;
        } else if (op.kind == MAT_OP_ADD) {
            value = a + b;
        } else if (op.kind == MAT_OP_MIX) {
            value = matMix4(a, b, f);
        } else if (op.kind == MAT_OP_MIXBY) {
            value = matMix4(a, b, c.x);
        } else if (op.kind == MAT_OP_POWER) {
            value = matPow4(max(a, vec4<f32>(0.0)), f);
        } else if (op.kind == MAT_OP_SMOOTHSTEP) {
            value = matSmoothstep4(k.x, k.y, a);
        } else if (op.kind == MAT_OP_THRESHOLD) {
            value = select(vec4<f32>(0.0), vec4<f32>(1.0), a >= vec4<f32>(f));
        } else if (op.kind == MAT_OP_HUESHIFT) {
            value = vec4<f32>(hueShift(a.xyz, f + b.x), a.w);
        } else if (op.kind == MAT_OP_SATURATE) {
            value = vec4<f32>(colorSaturate(a.xyz, f), a.w);
        } else if (op.kind == MAT_OP_PALETTE) {
            value = vec4<f32>(cosinePalette(op.constant.xyz, op.constant2.xyz, op.constant3.xyz, op.constant4.xyz,
                                            a.x + f),
                              1.0);
        } else if (op.kind == MAT_OP_FIELD) {
            value = materialFieldValue(op.fieldSlot, ctx.worldPosition);
        }
        regs[dst] = value;
    }

    var result = base;
    let outputs = materialPrograms.programs[pi].outputs;
    let opacityRegister = materialPrograms.programs[pi].opacityCountPad.x;
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
    return result;
}
