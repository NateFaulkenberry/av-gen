// Fields on the GPU (ADR-025): the WGSL side of spatial/field.hpp. A packed FieldGpu record
// (320 bytes, spatial::packField) per slot in a uniform FieldBlock; fieldScalar / fieldVector /
// fieldColor sample slot i at a world position p with the same maths, in the same operation
// order, as spatial::sampleScalar / sampleVector / sampleColor (tests/rendering/test_fields_gpu.cpp
// compares them within 1e-4, noise kinds 1e-3).
//
// The including module declares the FieldBlock binding itself, e.g.
//   @group(1) @binding(3) var<uniform> fieldBlock: FieldBlock;
// (module-scope declarations may appear in any order). The simulated-grid table is different: it
// is declared HERE, at @group(0) @binding(15), so every module sees one binding; a module that
// includes this file must carry that entry in its group-0 layout (rendering::FieldUniforms owns
// the buffer and hands it over next to the field block). This file includes noise.wgsl; do not
// include it a second time.
//
// Conventions (p world space):
//   q = worldToLocal * p; d = falloff distance of the kind; w = strength * falloff(d)
//   scalar kinds : value = shape(q) [inverted: 1 - shape] * w
//   vector kinds : value = rotateToWorld(direction(q) [inverted: -direction] * w)
//   colour kinds : value = colour(q) (A/B swapped when inverted), alpha = w
//   cross-type   : scalar as vector = s * nWorld; vector as scalar = length(v); colour as scalar =
//                  luminance(rgb) * a; scalar as colour = (mix(A, B, saturate(s)), w); vector as
//                  colour = (v * 0.5 + 0.5, w)
//   compound     : combine(children) * own w. ONE level of nesting: a compound's children must be
//                  non-compound fields; a compound child evaluates as 0 (WGSL has no recursion).
//   time         : tau = strengthInnerOuterTau.w (speed * t + phase) animates noise kinds through
//                  nv = tau * (1, 0.7, 1.3); waves use t = wave1.w directly.
//   space        : the record's transform always applies; FieldSpace::Local is resolved by the
//                  caller (it decides what p is), not here.
#include "noise.wgsl"

struct FieldGpu {
    kind: u32,                       // FieldKind (FIELD_* below)
    fieldType: u32,                  // FieldType: 0 scalar, 1 vector, 2 colour
    falloffKind: u32,                // FalloffKind (FALLOFF_* below)
    seed: u32,
    worldToLocal: mat4x4<f32>,
    localToWorldRow0: vec4<f32>,     // rotation-only rows (scale sign kept): world = (dot(r0, v), dot(r1, v), dot(r2, v))
    localToWorldRow1: vec4<f32>,
    localToWorldRow2: vec4<f32>,
    strengthInnerOuterTau: vec4<f32>, // strength, falloff inner, falloff outer, tau
    axisRadius: vec4<f32>,           // axis.xyz (normalised on use), radius
    pointLength: vec4<f32>,          // point.xyz, length
    sizeSoftness: vec4<f32>,         // size.xyz, softness
    freqExpInvertBias: vec4<f32>,    // frequency, falloff exponent, invert (1/0), spiralBias
    wave0: vec4<f32>,                // amplitude, wavelength, waveSpeed, waveWidth
    wave1: vec4<f32>,                // waveOrigin, geometry, shape, t (seconds)
    colorA: vec4<f32>,
    colorB: vec4<f32>,
    curve: vec4<f32>,                // custom falloff curve control values
    noiseCombineMix: vec4<f32>,      // falloff noiseAmount, noiseScale, combine, mix
    children: vec4<i32>,             // compound child slots, -1 = none
    gridBounds0: vec4<f32>,          // Grid kind: boundsMin.xyz, offset into gridTable (floats)
    gridBounds1: vec4<f32>,          // boundsMax.xyz, components per cell (1 scalar, 2 Rd, 4 vector)
    gridRes: vec4<f32>,              // resolution.xyz, w = 0 unbound, 1 bound (clamp), 3 bound (wrap)
};

struct FieldBlock {
    count: u32,
    pad0: u32,
    pad1: u32,
    pad2: u32,
    fields: array<FieldGpu, 16>,
};

const FIELD_CONSTANT: u32 = 0u;
const FIELD_LINEAR_GRADIENT: u32 = 1u;
const FIELD_RADIAL: u32 = 2u;
const FIELD_BOX: u32 = 3u;
const FIELD_SPHERE: u32 = 4u;
const FIELD_PLANE: u32 = 5u;
const FIELD_NOISE: u32 = 6u;
const FIELD_VORONOI: u32 = 7u;
const FIELD_DISTANCE: u32 = 8u;
const FIELD_WAVE: u32 = 9u;
const FIELD_DIRECTION: u32 = 10u;
const FIELD_RADIAL_VECTOR: u32 = 11u;
const FIELD_ATTRACTOR: u32 = 12u;
const FIELD_REPULSOR: u32 = 13u;
const FIELD_VORTEX: u32 = 14u;
const FIELD_CURL_NOISE: u32 = 15u;
const FIELD_SPIRAL: u32 = 16u;
const FIELD_WAVE_VECTOR: u32 = 17u;
const FIELD_CONSTANT_COLOR: u32 = 18u;
const FIELD_GRADIENT: u32 = 19u;
const FIELD_RADIAL_GRADIENT: u32 = 20u;
const FIELD_NOISE_COLOR: u32 = 21u;
const FIELD_POSITION_COLOR: u32 = 22u;
const FIELD_COMPOUND: u32 = 23u;
const FIELD_GRID: u32 = 24u;

const FIELD_TYPE_SCALAR: u32 = 0u;
const FIELD_TYPE_VECTOR: u32 = 1u;
const FIELD_TYPE_COLOR: u32 = 2u;

const FALLOFF_NONE: u32 = 0u;
const FALLOFF_LINEAR: u32 = 1u;
const FALLOFF_SMOOTHSTEP: u32 = 2u;
const FALLOFF_SMOOTH: u32 = 3u;
const FALLOFF_EASE_IN: u32 = 4u;
const FALLOFF_EASE_OUT: u32 = 5u;
const FALLOFF_EASE_IN_OUT: u32 = 6u;
const FALLOFF_EXPONENTIAL: u32 = 7u;
const FALLOFF_CUSTOM_CURVE: u32 = 8u;
const FALLOFF_NOISE_MODULATED: u32 = 9u;

const FIELD_COMBINE_ADD: u32 = 0u;
const FIELD_COMBINE_MULTIPLY: u32 = 1u;
const FIELD_COMBINE_MAX: u32 = 2u;
const FIELD_COMBINE_MIN: u32 = 3u;
const FIELD_COMBINE_MIX: u32 = 4u;
const FIELD_COMBINE_AVERAGE: u32 = 5u;

const FIELD_TWO_PI: f32 = 6.28318530717958647692;

// ---- simulated grids (ADR-032) ------------------------------------------------------------------
// Every simulated grid of the scene lives in one shared table, back to back in FieldSet::grids
// order (spatial::gridTableOffset, rendering::Simulation). The table is bound HERE, at group 0
// binding 15, so every module that includes this file sees the same binding; its bind group
// layout must carry that entry (a 16-byte placeholder buffer when the scene has no grids -
// gridRes.w is then 0 for every record and no sample reads the buffer).
@group(0) @binding(15) var<storage, read> gridTable: array<f32>;

// Continuous cell coordinate of a point in the grid's own space (cell i is centred at i).
fn gridCoord(fi: u32, q: vec3<f32>) -> vec3<f32> {
    let lo = fieldBlock.fields[fi].gridBounds0.xyz;
    let hi = fieldBlock.fields[fi].gridBounds1.xyz;
    let res = fieldBlock.fields[fi].gridRes.xyz;
    let extent = max(hi - lo, vec3<f32>(1e-6));
    return (q - lo) / extent * res - vec3<f32>(0.5);
}

fn gridAxisIndex(i: i32, n: i32, wraps: bool) -> i32 {
    if (wraps) {
        let m = i % n;
        return select(m, m + n, m < 0);
    }
    return clamp(i, 0, n - 1);
}

// One cell component, with the wrap rule. Matches spatial::GridField::at.
fn gridFetch(fi: u32, i: i32, j: i32, k: i32, c: i32) -> f32 {
    let nx = i32(fieldBlock.fields[fi].gridRes.x);
    let ny = i32(fieldBlock.fields[fi].gridRes.y);
    let nz = i32(fieldBlock.fields[fi].gridRes.z);
    if (nx <= 0 || ny <= 0 || nz <= 0) {
        return 0.0;
    }
    let comps = i32(fieldBlock.fields[fi].gridBounds1.w + 0.5);
    if (c < 0 || c >= comps) {
        return 0.0;
    }
    let wraps = fieldBlock.fields[fi].gridRes.w > 2.0;
    let ii = gridAxisIndex(i, nx, wraps);
    let jj = gridAxisIndex(j, ny, wraps);
    let kk = gridAxisIndex(k, nz, wraps);
    let base = i32(fieldBlock.fields[fi].gridBounds0.w + 0.5);
    let idx = base + ((kk * ny + jj) * nx + ii) * comps + c;
    if (idx < 0 || u32(idx) >= arrayLength(&gridTable)) {
        return 0.0;
    }
    return gridTable[u32(idx)];
}

// Trilinear gather of one component. Matches spatial's trilinear() operation for operation.
fn gridTrilinear(fi: u32, coord: vec3<f32>, c: i32) -> f32 {
    let base = floor(coord);
    let f = coord - base;
    let i = i32(base.x);
    let j = i32(base.y);
    let k = i32(base.z);
    let c000 = gridFetch(fi, i, j, k, c);
    let c100 = gridFetch(fi, i + 1, j, k, c);
    let c010 = gridFetch(fi, i, j + 1, k, c);
    let c110 = gridFetch(fi, i + 1, j + 1, k, c);
    let c001 = gridFetch(fi, i, j, k + 1, c);
    let c101 = gridFetch(fi, i + 1, j, k + 1, c);
    let c011 = gridFetch(fi, i, j + 1, k + 1, c);
    let c111 = gridFetch(fi, i + 1, j + 1, k + 1, c);
    let x00 = c000 + (c100 - c000) * f.x;
    let x10 = c010 + (c110 - c010) * f.x;
    let x01 = c001 + (c101 - c001) * f.x;
    let x11 = c011 + (c111 - c011) * f.x;
    let y0 = x00 + (x10 - x00) * f.y;
    let y1 = x01 + (x11 - x01) * f.y;
    return y0 + (y1 - y0) * f.z;
}

// The grid's scalar reading: the single channel, or B for reaction-diffusion (2 components).
fn gridScalarAt(fi: u32, q: vec3<f32>) -> f32 {
    if (fieldBlock.fields[fi].gridRes.w < 0.5) {
        return 0.0;
    }
    let comps = i32(fieldBlock.fields[fi].gridBounds1.w + 0.5);
    let channel = select(0, 1, comps == 2);
    return gridTrilinear(fi, gridCoord(fi, q), channel);
}

fn gridVectorAt(fi: u32, q: vec3<f32>) -> vec3<f32> {
    if (fieldBlock.fields[fi].gridRes.w < 0.5) {
        return vec3<f32>(0.0);
    }
    let coord = gridCoord(fi, q);
    return vec3<f32>(gridTrilinear(fi, coord, 0), gridTrilinear(fi, coord, 1), gridTrilinear(fi, coord, 2));
}

// normalize() that returns 0 for a zero vector (the CPU does the same).
fn fieldNormalize(v: vec3<f32>) -> vec3<f32> {
    let l2 = dot(v, v);
    if (l2 > 1e-20) {
        return v / sqrt(l2);
    }
    return vec3<f32>(0.0);
}

fn fieldAxis(fi: u32) -> vec3<f32> {
    return fieldNormalize(fieldBlock.fields[fi].axisRadius.xyz);
}

fn fieldLocal(fi: u32, p: vec3<f32>) -> vec3<f32> {
    return (fieldBlock.fields[fi].worldToLocal * vec4<f32>(p, 1.0)).xyz;
}

fn fieldToWorld(fi: u32, v: vec3<f32>) -> vec3<f32> {
    return vec3<f32>(dot(fieldBlock.fields[fi].localToWorldRow0.xyz, v), dot(fieldBlock.fields[fi].localToWorldRow1.xyz, v), dot(fieldBlock.fields[fi].localToWorldRow2.xyz, v));
}

// ---- falloff ---------------------------------------------------------------------------------

fn smoothstepFalloff(t: f32) -> f32 {
    return 1.0 - t * t * (3.0 - 2.0 * t);
}

// The curve value for t in [0, 1] (1 -> 0). `p` (world) and the seed feed NoiseModulated.
fn falloffCurve(kind: u32, t: f32, exponent: f32, curve: vec4<f32>, noiseAmount: f32, noiseScale: f32,
                p: vec3<f32>, seed: u32) -> f32 {
    if (kind == FALLOFF_LINEAR) {
        return 1.0 - t;
    }
    if (kind == FALLOFF_SMOOTHSTEP) {
        return smoothstepFalloff(t);
    }
    if (kind == FALLOFF_SMOOTH) {
        return 1.0 - t * t * t * (t * (6.0 * t - 15.0) + 10.0);
    }
    if (kind == FALLOFF_EASE_IN) {
        return 1.0 - t * t * t;
    }
    if (kind == FALLOFF_EASE_OUT) {
        let u = 1.0 - t;
        return u * u * u;
    }
    if (kind == FALLOFF_EASE_IN_OUT) {
        let a = 4.0 * t * t * t;
        let b = -2.0 * t + 2.0;
        let c = 1.0 - b * b * b / 2.0;
        return 1.0 - select(c, a, t < 0.5);
    }
    if (kind == FALLOFF_EXPONENTIAL) {
        return pow(1.0 - t, exponent);
    }
    if (kind == FALLOFF_CUSTOM_CURVE) {
        // cubic Bezier on y with control values (1, curve.x, curve.y, curve.z), De Casteljau
        let b01 = mix(1.0, curve.x, t);
        let b12 = mix(curve.x, curve.y, t);
        let b23 = mix(curve.y, curve.z, t);
        let b012 = mix(b01, b12, t);
        let b123 = mix(b12, b23, t);
        return mix(b012, b123, t);
    }
    if (kind == FALLOFF_NOISE_MODULATED) {
        let n = fbm3(p * noiseScale, seed) * 2.0 - 1.0;
        return smoothstepFalloff(t) * saturate(1.0 + noiseAmount * n);
    }
    return 1.0;
}

// Weight of the falloff at distance d: 1 inside `inner`, 0 beyond `outer`, the curve between.
fn falloffWeight(fi: u32, d: f32, p: vec3<f32>) -> f32 {
    if (fieldBlock.fields[fi].falloffKind == FALLOFF_NONE) {
        return 1.0;
    }
    let inner = fieldBlock.fields[fi].strengthInnerOuterTau.y;
    let outer = fieldBlock.fields[fi].strengthInnerOuterTau.z;
    if (d <= inner) {
        return 1.0;
    }
    if (d >= outer) {
        return 0.0;
    }
    let t = (d - inner) / (outer - inner);
    return falloffCurve(fieldBlock.fields[fi].falloffKind, t, fieldBlock.fields[fi].freqExpInvertBias.y, fieldBlock.fields[fi].curve, fieldBlock.fields[fi].noiseCombineMix.x, fieldBlock.fields[fi].noiseCombineMix.y, p,
                        fieldBlock.fields[fi].seed);
}

// The distance the falloff is measured over for each kind.
fn fieldDistance(fi: u32, q: vec3<f32>) -> f32 {
    let kind = fieldBlock.fields[fi].kind;
    if (kind == FIELD_LINEAR_GRADIENT || kind == FIELD_PLANE || kind == FIELD_DIRECTION || kind == FIELD_GRADIENT) {
        return abs(dot(q, fieldAxis(fi)));
    }
    if (kind == FIELD_BOX) {
        let e = abs(q) - fieldBlock.fields[fi].sizeSoftness.xyz;
        return max(max(e.x, max(e.y, e.z)), 0.0);
    }
    return length(q - fieldBlock.fields[fi].pointLength.xyz);
}

// The NoiseModulated falloff samples its noise at the LOCAL point q so the pattern moves with
// the field's frame (matches spatial::Falloff::weight on the CPU).
fn fieldWeightOf(fi: u32, q: vec3<f32>, p: vec3<f32>) -> f32 {
    return fieldBlock.fields[fi].strengthInnerOuterTau.x * falloffWeight(fi, fieldDistance(fi, q), q);
}

// ---- kinds -------------------------------------------------------------------------------------

fn fieldNoiseOffset(fi: u32) -> vec3<f32> {
    return fieldBlock.fields[fi].strengthInnerOuterTau.w * vec3<f32>(1.0, 0.7, 1.3);
}

fn waveDistance(fi: u32, q: vec3<f32>) -> f32 {
    let geometry = u32(fieldBlock.fields[fi].wave1.y + 0.5);
    let n = fieldAxis(fi);
    if (geometry == 0u) { // planar
        return dot(q, n);
    }
    let r = q - fieldBlock.fields[fi].pointLength.xyz;
    if (geometry == 2u) { // spherical
        return length(r);
    }
    return length(r - n * dot(r, n)); // radial / cylindrical
}

// Outward direction of the wave geometry at q (for WaveVector).
fn waveDirection(fi: u32, q: vec3<f32>) -> vec3<f32> {
    let geometry = u32(fieldBlock.fields[fi].wave1.y + 0.5);
    let n = fieldAxis(fi);
    if (geometry == 0u) {
        return n;
    }
    let r = q - fieldBlock.fields[fi].pointLength.xyz;
    if (geometry == 2u) {
        return fieldNormalize(r);
    }
    return fieldNormalize(r - n * dot(r, n));
}

fn waveValue(fi: u32, q: vec3<f32>) -> f32 {
    let t = fieldBlock.fields[fi].wave1.w;
    let s = waveDistance(fi, q) - fieldBlock.fields[fi].wave1.x - fieldBlock.fields[fi].wave0.z * t;
    let width = fieldBlock.fields[fi].wave0.w;
    var envelope = 1.0;
    if (width > 0.0) {
        envelope = smoothstepFalloff(saturate(abs(s) / width));
    }
    let k = FIELD_TWO_PI / max(fieldBlock.fields[fi].wave0.y, 1e-6);
    let shape = u32(fieldBlock.fields[fi].wave1.z + 0.5);
    var v = sin(k * s);
    if (shape == 1u) { // pulse
        let ks = k * s;
        v = exp(-(ks * ks));
    } else if (shape == 2u) { // triangle
        v = 4.0 * abs(fract(k * s / FIELD_TWO_PI + 0.75) - 0.5) - 1.0;
    }
    return fieldBlock.fields[fi].wave0.x * envelope * v;
}

// Scalar shape of a scalar kind (before invert and weight).
fn scalarShape(fi: u32, q: vec3<f32>) -> f32 {
    let kind = fieldBlock.fields[fi].kind;
    if (kind == FIELD_CONSTANT) {
        return 1.0;
    }
    if (kind == FIELD_LINEAR_GRADIENT) {
        return saturate(dot(q, fieldAxis(fi)) / max(fieldBlock.fields[fi].pointLength.w, 1e-6) + 0.5);
    }
    if (kind == FIELD_RADIAL) {
        return 1.0 - saturate(length(q - fieldBlock.fields[fi].pointLength.xyz) / max(fieldBlock.fields[fi].axisRadius.w, 1e-6));
    }
    if (kind == FIELD_BOX) {
        let e = abs(q) - fieldBlock.fields[fi].sizeSoftness.xyz;
        return 1.0 - saturate(max(max(e.x, max(e.y, e.z)), 0.0) / max(fieldBlock.fields[fi].sizeSoftness.w, 1e-6));
    }
    if (kind == FIELD_SPHERE) {
        return 1.0 - saturate((length(q - fieldBlock.fields[fi].pointLength.xyz) - fieldBlock.fields[fi].axisRadius.w) / max(fieldBlock.fields[fi].sizeSoftness.w, 1e-6));
    }
    if (kind == FIELD_PLANE) {
        return saturate(dot(q, fieldAxis(fi)) / max(fieldBlock.fields[fi].sizeSoftness.w, 1e-6));
    }
    if (kind == FIELD_NOISE) {
        return fbm3(q * fieldBlock.fields[fi].freqExpInvertBias.x + fieldNoiseOffset(fi), fieldBlock.fields[fi].seed);
    }
    if (kind == FIELD_VORONOI) {
        return saturate(voronoiF1(q * fieldBlock.fields[fi].freqExpInvertBias.x + fieldNoiseOffset(fi), fieldBlock.fields[fi].seed));
    }
    if (kind == FIELD_DISTANCE) {
        return saturate(length(q - fieldBlock.fields[fi].pointLength.xyz) / max(fieldBlock.fields[fi].axisRadius.w, 1e-6));
    }
    if (kind == FIELD_WAVE) {
        return waveValue(fi, q);
    }
    if (kind == FIELD_GRID) {
        return gridScalarAt(fi, q);
    }
    return 0.0; // anything unknown
}

// Direction of a vector kind (before invert and weight), in the field's local frame.
fn vectorDirection(fi: u32, q: vec3<f32>) -> vec3<f32> {
    let kind = fieldBlock.fields[fi].kind;
    let n = fieldAxis(fi);
    let r = q - fieldBlock.fields[fi].pointLength.xyz;
    if (kind == FIELD_DIRECTION) {
        return n;
    }
    if (kind == FIELD_RADIAL_VECTOR || kind == FIELD_REPULSOR) {
        return fieldNormalize(r);
    }
    if (kind == FIELD_ATTRACTOR) {
        return fieldNormalize(-r);
    }
    if (kind == FIELD_VORTEX) {
        return fieldNormalize(cross(n, r));
    }
    if (kind == FIELD_CURL_NOISE) {
        return curlNoise(q * fieldBlock.fields[fi].freqExpInvertBias.x + fieldNoiseOffset(fi), fieldBlock.fields[fi].seed);
    }
    if (kind == FIELD_SPIRAL) {
        return fieldNormalize(fieldNormalize(cross(n, r)) + fieldBlock.fields[fi].freqExpInvertBias.w * fieldNormalize(r));
    }
    if (kind == FIELD_WAVE_VECTOR) {
        return waveValue(fi, q) * waveDirection(fi, q);
    }
    if (kind == FIELD_GRID) {
        return gridVectorAt(fi, q);
    }
    return vec3<f32>(0.0);
}

// Colour of a colour kind (rgb; alpha is the weight, applied by the caller).
fn colorShape(fi: u32, q: vec3<f32>, a: vec4<f32>, b: vec4<f32>) -> vec3<f32> {
    let kind = fieldBlock.fields[fi].kind;
    if (kind == FIELD_CONSTANT_COLOR) {
        return a.rgb;
    }
    if (kind == FIELD_GRADIENT) {
        return mix(a.rgb, b.rgb, saturate(dot(q, fieldAxis(fi)) / max(fieldBlock.fields[fi].pointLength.w, 1e-6) + 0.5));
    }
    if (kind == FIELD_RADIAL_GRADIENT) {
        return mix(a.rgb, b.rgb, saturate(length(q - fieldBlock.fields[fi].pointLength.xyz) / max(fieldBlock.fields[fi].axisRadius.w, 1e-6)));
    }
    if (kind == FIELD_NOISE_COLOR) {
        return mix(a.rgb, b.rgb, fbm3(q * fieldBlock.fields[fi].freqExpInvertBias.x + fieldNoiseOffset(fi), fieldBlock.fields[fi].seed));
    }
    if (kind == FIELD_POSITION_COLOR) {
        return fract(q * fieldBlock.fields[fi].freqExpInvertBias.x);
    }
    return vec3<f32>(0.0);
}

fn fieldLuminance(c: vec3<f32>) -> f32 {
    return dot(c, vec3<f32>(0.2126, 0.7152, 0.0722));
}

// ---- one non-compound field, with the cross-type rules -----------------------------------------

// Scalar value of a scalar kind (invert and weight applied).
fn ownScalar(fi: u32, q: vec3<f32>, w: f32) -> f32 {
    var v = scalarShape(fi, q);
    if (fieldBlock.fields[fi].freqExpInvertBias.z > 0.5) {
        v = 1.0 - v;
    }
    return v * w;
}

// World-space vector of a vector kind (invert and weight applied).
fn ownVector(fi: u32, q: vec3<f32>, w: f32) -> vec3<f32> {
    var v = vectorDirection(fi, q);
    if (fieldBlock.fields[fi].freqExpInvertBias.z > 0.5) {
        v = -v;
    }
    return fieldToWorld(fi, v * w);
}

// Colour of a colour kind (A/B swapped when inverted), alpha = weight.
fn ownColor(fi: u32, q: vec3<f32>, w: f32) -> vec4<f32> {
    var a = fieldBlock.fields[fi].colorA;
    var b = fieldBlock.fields[fi].colorB;
    if (fieldBlock.fields[fi].freqExpInvertBias.z > 0.5) {
        a = fieldBlock.fields[fi].colorB;
        b = fieldBlock.fields[fi].colorA;
    }
    return vec4<f32>(colorShape(fi, q, a, b), w);
}

fn basicScalar(fi: u32, p: vec3<f32>) -> f32 {
    let q = fieldLocal(fi, p);
    let w = fieldWeightOf(fi, q, p);
    if (fieldBlock.fields[fi].fieldType == FIELD_TYPE_VECTOR) {
        return length(ownVector(fi, q, w));
    }
    if (fieldBlock.fields[fi].fieldType == FIELD_TYPE_COLOR) {
        let c = ownColor(fi, q, w);
        return fieldLuminance(c.rgb) * c.a;
    }
    return ownScalar(fi, q, w);
}

fn basicVector(fi: u32, p: vec3<f32>) -> vec3<f32> {
    let q = fieldLocal(fi, p);
    let w = fieldWeightOf(fi, q, p);
    if (fieldBlock.fields[fi].fieldType == FIELD_TYPE_VECTOR) {
        return ownVector(fi, q, w);
    }
    var s = 0.0;
    if (fieldBlock.fields[fi].fieldType == FIELD_TYPE_COLOR) {
        let c = ownColor(fi, q, w);
        s = fieldLuminance(c.rgb) * c.a;
    } else {
        s = ownScalar(fi, q, w);
    }
    return s * fieldToWorld(fi, fieldAxis(fi));
}

fn basicColor(fi: u32, p: vec3<f32>) -> vec4<f32> {
    let q = fieldLocal(fi, p);
    let w = fieldWeightOf(fi, q, p);
    if (fieldBlock.fields[fi].fieldType == FIELD_TYPE_COLOR) {
        return ownColor(fi, q, w);
    }
    if (fieldBlock.fields[fi].fieldType == FIELD_TYPE_VECTOR) {
        return vec4<f32>(ownVector(fi, q, w) * 0.5 + 0.5, w);
    }
    let s = ownScalar(fi, q, w);
    return vec4<f32>(mix(fieldBlock.fields[fi].colorA.rgb, fieldBlock.fields[fi].colorB.rgb, saturate(s)), w);
}

// ---- compound combination (one level) -----------------------------------------------------------

fn childSlot(fi: u32, c: u32) -> i32 {
    let slot = fieldBlock.fields[fi].children[c];
    if (slot < 0 || u32(slot) >= fieldBlock.count) {
        return -1;
    }
    if (fieldBlock.fields[u32(slot)].kind == FIELD_COMPOUND) {
        return -1; // nested compounds are not supported on the GPU (evaluate as 0)
    }
    return slot;
}

fn combineScalar(fi: u32, p: vec3<f32>) -> f32 {
    let combine = u32(fieldBlock.fields[fi].noiseCombineMix.z + 0.5);
    var acc = 0.0;
    var n = 0u;
    var first = 0.0;
    var second = 0.0;
    for (var c = 0u; c < 4u; c = c + 1u) {
        let slot = childSlot(fi, c);
        if (slot < 0) { continue; }
        let v = basicScalar(u32(slot), p);
        if (n == 0u) { first = v; }
        if (n == 1u) { second = v; }
        if (combine == FIELD_COMBINE_MULTIPLY) {
            acc = select(acc * v, v, n == 0u);
        } else if (combine == FIELD_COMBINE_MAX) {
            acc = select(max(acc, v), v, n == 0u);
        } else if (combine == FIELD_COMBINE_MIN) {
            acc = select(min(acc, v), v, n == 0u);
        } else {
            acc = acc + v;
        }
        n = n + 1u;
    }
    if (n == 0u) {
        return 0.0;
    }
    if (combine == FIELD_COMBINE_MIX) {
        return mix(first, second, fieldBlock.fields[fi].noiseCombineMix.w);
    }
    if (combine == FIELD_COMBINE_AVERAGE) {
        return acc / f32(n);
    }
    return acc;
}

fn combineVector(fi: u32, p: vec3<f32>) -> vec3<f32> {
    let combine = u32(fieldBlock.fields[fi].noiseCombineMix.z + 0.5);
    var acc = vec3<f32>(0.0);
    var n = 0u;
    var first = vec3<f32>(0.0);
    var second = vec3<f32>(0.0);
    for (var c = 0u; c < 4u; c = c + 1u) {
        let slot = childSlot(fi, c);
        if (slot < 0) { continue; }
        let v = basicVector(u32(slot), p);
        if (n == 0u) { first = v; }
        if (n == 1u) { second = v; }
        if (combine == FIELD_COMBINE_MULTIPLY) {
            acc = select(acc * v, v, n == 0u);
        } else if (combine == FIELD_COMBINE_MAX) {
            acc = select(max(acc, v), v, n == 0u);
        } else if (combine == FIELD_COMBINE_MIN) {
            acc = select(min(acc, v), v, n == 0u);
        } else {
            acc = acc + v;
        }
        n = n + 1u;
    }
    if (n == 0u) {
        return vec3<f32>(0.0);
    }
    if (combine == FIELD_COMBINE_MIX) {
        return mix(first, second, fieldBlock.fields[fi].noiseCombineMix.w);
    }
    if (combine == FIELD_COMBINE_AVERAGE) {
        return acc / f32(n);
    }
    return acc;
}

// Compound colours: rgb combined like the other types, alpha = max child alpha (the owner's
// strength * falloff is applied to the alpha only, by fieldColor). Matches spatial::colorAt.
fn combineColor(fi: u32, p: vec3<f32>) -> vec4<f32> {
    let combine = u32(fieldBlock.fields[fi].noiseCombineMix.z + 0.5);
    var acc = vec3<f32>(0.0);
    var n = 0u;
    var first = vec3<f32>(0.0);
    var second = vec3<f32>(0.0);
    var alpha = 0.0;
    for (var c = 0u; c < 4u; c = c + 1u) {
        let slot = childSlot(fi, c);
        if (slot < 0) { continue; }
        let cv = basicColor(u32(slot), p);
        let v = cv.rgb;
        alpha = max(alpha, cv.a);
        if (n == 0u) { first = v; }
        if (n == 1u) { second = v; }
        if (combine == FIELD_COMBINE_MULTIPLY) {
            acc = select(acc * v, v, n == 0u);
        } else if (combine == FIELD_COMBINE_MAX) {
            acc = select(max(acc, v), v, n == 0u);
        } else if (combine == FIELD_COMBINE_MIN) {
            acc = select(min(acc, v), v, n == 0u);
        } else {
            acc = acc + v;
        }
        n = n + 1u;
    }
    if (n == 0u) {
        return vec4<f32>(0.0);
    }
    var rgb = acc;
    if (combine == FIELD_COMBINE_MIX) {
        rgb = mix(first, second, fieldBlock.fields[fi].noiseCombineMix.w);
    } else if (combine == FIELD_COMBINE_AVERAGE) {
        rgb = acc / f32(n);
    }
    if (fieldBlock.fields[fi].freqExpInvertBias.z > 0.5) {
        rgb = vec3<f32>(1.0) - rgb;
    }
    return vec4<f32>(rgb, alpha);
}

// ---- public API ---------------------------------------------------------------------------------

fn fieldValid(i: i32) -> bool {
    return i >= 0 && u32(i) < fieldBlock.count;
}

// strength * falloff at p (0 for an invalid slot).
fn fieldWeight(i: i32, p: vec3<f32>) -> f32 {
    if (!fieldValid(i)) {
        return 0.0;
    }
    let fi = u32(i);
    return fieldWeightOf(fi, fieldLocal(fi, p), p);
}

fn fieldScalar(i: i32, p: vec3<f32>) -> f32 {
    if (!fieldValid(i)) {
        return 0.0;
    }
    let fi = u32(i);
    if (fieldBlock.fields[fi].kind == FIELD_COMPOUND) {
        return combineScalar(fi, p) * fieldWeightOf(fi, fieldLocal(fi, p), p);
    }
    return basicScalar(fi, p);
}

fn fieldVector(i: i32, p: vec3<f32>) -> vec3<f32> {
    if (!fieldValid(i)) {
        return vec3<f32>(0.0);
    }
    let fi = u32(i);
    if (fieldBlock.fields[fi].kind == FIELD_COMPOUND) {
        return combineVector(fi, p) * fieldWeightOf(fi, fieldLocal(fi, p), p);
    }
    return basicVector(fi, p);
}

fn fieldColor(i: i32, p: vec3<f32>) -> vec4<f32> {
    if (!fieldValid(i)) {
        return vec4<f32>(0.0);
    }
    let fi = u32(i);
    if (fieldBlock.fields[fi].kind == FIELD_COMPOUND) {
        let c = combineColor(fi, p);
        return vec4<f32>(c.rgb, c.a * fieldWeightOf(fi, fieldLocal(fi, p), p));
    }
    return basicColor(fi, p);
}

// The FieldType of a slot (scalar for invalid slots).
fn fieldTypeOf(i: i32) -> u32 {
    if (!fieldValid(i)) {
        return FIELD_TYPE_SCALAR;
    }
    return fieldBlock.fields[u32(i)].fieldType;
}
