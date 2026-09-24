// Shared PBR fragment shading (ADR-023): included by pbr.wgsl (entities), procedural.wgsl
// (instanced procedural geometry) and sdf_raymarch.wgsl (raymarched surfaces) so every path
// shades identically. Expects `frame` and `object` from common.wgsl; declares the material
// (group 2) and IBL (group 3) bindings.
//
// glTF metallic-roughness PBR: Cook-Torrance (GGX distribution, height-correlated Smith
// visibility, Schlick Fresnel) for punctual lights, split-sum image-based lighting, normal
// mapping via a derivative-based cotangent frame, emissive, occlusion, alpha mask/blend, then
// distance fog. Outputs scene-linear HDR radiance; tone mapping happens in tonemap.wgsl.
//
// Procedural materials (ADR-030, layered in ADR-036; material.wgsl): when the bound material names a program
// (materialSelect.program >= 0, a slot in materialPrograms) the program runs first, before any
// texture or lighting, and replaces the object's base colour, metallic, roughness, emission and
// opacity; the per-instance multipliers and the material textures then apply to its result. With
// no program the path is byte for byte the pre-ADR-030 shader. Modules that include this file
// must also include fields.wgsl (material.wgsl needs it for Field ops).
//
// Lighting (ADR-033/034) lives in lighting.wgsl: the packed light buffer, the froxel grid, area
// lights, shadow maps, contact shadows and ambient occlusion. Every path through this file uses
// it, so a mesh, an instance and an SDF surface receive light identically.
#include "material.wgsl"
#include "lighting.wgsl"
// ADR-207. Included here, so the one file every shading path goes through is also the one
// place surface waves are applied: entities, skinned characters, the procedural scatter and
// raymarched SDF surfaces all receive a wave with no per-asset code. It reads `kProceduralDraw`,
// which every includer of this file already defines.
#include "wave_effects.wgsl"
// ADR-230 §6. Only the ground half: the comet marching and the aurora shells belong to the sky
// draw, and a surface fragment has no use for them.
#include "atmosphere_ground.wgsl"

// ---- ADR-703: per-entity effect lanes (FXL) ------------------------------------------------------
//
// Glow, Pulse and Bloom Source reach a surface through these (world/effects/entity_fx.hpp). They are
// a module-scope PRIVATE variable rather than a read of the `entityFx` buffer because only pbr.wgsl
// and procedural.wgsl bind that buffer, each at its own group-1 binding (sdf_raymarch.wgsl binds
// none), and a function here that named a binding would put it in every includer's pipeline. Each
// binding includer fills this through `setEntityFxLanes` before calling `shadeSurface`; an includer
// that does not leaves it zero, which is the gate's "off".
struct EntityFxLanes {
    a: vec4<f32>,        // object.fxA: x = gain, y = bloom share, z = flags, w = record index
    b: vec4<f32>,        // object.fxB: rgb = tint on the material's own emission
    add: vec4<f32>,      // record lane 2: rgb = added radiance
    rim: vec4<f32>,      // record lane 3: rgb = rim radiance at grazing, w = rim power
    bandAxis: vec4<f32>, // record lane 4: u = dot(p, xyz) + w runs 0..1 across the owner
    band: vec4<f32>,     // record lane 5: x = centre, y = half width, z = waveform, w = depth
    // Wave 2 (world::EntityFxLaneIndex 6..14): the owner frame and the clip.
    frame0: vec4<f32>,   // lanes 6..8: world -> owner space q (rows)
    frame1: vec4<f32>,
    frame2: vec4<f32>,
    shape: vec4<f32>,    // lane 9: xyz = the node's origin in q, w = gy (h = q.y * gy + 0.5)
    clip: vec4<f32>,     // lane 10: x = mode, y = threshold, z = edge width, w = noise scale
    clipEdge: vec4<f32>, // lane 11: rgb = edge radiance, w = breakup / direction bias
    travel: vec4<f32>,   // lane 14 (only .w is read here: 1 / the largest radial distance)
};
var<private> entityFxLanes: EntityFxLanes;
// Set by `shadeSurface` when the clip removes this fragment. The DISCARD is the includer's, at the
// very end of its fragment entry, after every derivative and implicit-derivative sample: a discard
// in non-uniform control flow ahead of them left the surviving neighbours of a quad reading
// undefined derivatives on Metal, and one NaN from that blew the whole frame's exposure out (found
// by the dissolve shadow test, whose frame went black). Only the includers that fill the lanes
// (pbr.wgsl, procedural.wgsl) read it; for any other it stays false.
var<private> fxClipDiscard: bool;

// The extension record (world::EntityFxExtLane), read only when `kFxExt` is set.
struct EntityFxExtLanes {
    bio0: vec4<f32>,   // x = pattern, y = scale, z = coverage, w = colour variation
    bio1: vec4<f32>,   // rgb = radiance, w = breathe rate (Hz)
    bio2: vec4<f32>,   // x = breathe depth, y = wave speed, z = wave interval (s), w = wave gain
    veins0: vec4<f32>, // x = scale, y = width, z = noise, w = coordinate (0 height, 1 radial)
    veins1: vec4<f32>, // rgb = near radiance, w = pulse speed
    veins2: vec4<f32>, // rgb = far radiance, w = pulse width
    veins3: vec4<f32>, // x = pulse interval, y = glow between pulses
    hue0: vec4<f32>,   // x = speed, y = range, z = spatial frequency, w = channel
    hue1: vec4<f32>,   // xyz = axis (q), w = phase
    rim0: vec4<f32>,   // rgb = radiance, w = power
    rim1: vec4<f32>,   // xyz = direction (view space), w = threshold
    rim2: vec4<f32>,   // x = softness
};
var<private> entityFxExt: EntityFxExtLanes;

// The flag bits of `fxA.z` (world::EntityFxFlag).
const FX_RECORD: u32 = 2u;
const FX_BAND: u32 = 4u;
const FX_BLOOM_SHARE: u32 = 8u;
const FX_CLIP: u32 = 16u;
const FX_INFLATE: u32 = 32u;
const FX_TRAVEL: u32 = 64u;
const FX_SMEAR: u32 = 128u;
const FX_EXT: u32 = 256u;
const FX_BIO: u32 = 512u;
const FX_VEINS: u32 = 1024u;
const FX_HUE: u32 = 2048u;
const FX_RIM_LIGHT: u32 = 4096u;
const FX_DISPLACE: u32 = 224u; // FX_INFLATE | FX_TRAVEL | FX_SMEAR

// Fills the lanes from a draw's inline pair and its record. Called by each includer that binds the
// record buffer (pbr.wgsl for entities and skinned characters, procedural.wgsl for procedural
// objects) behind its own `fxA.z != 0` gate, so there is one reading of the record, not one per path.
fn setEntityFxLanes(a: vec4<f32>, b: vec4<f32>, record: EntityFx) {
    entityFxLanes.a = a;
    entityFxLanes.b = b;
    entityFxLanes.add = record.lanes[2];
    entityFxLanes.rim = record.lanes[3];
    entityFxLanes.bandAxis = record.lanes[4];
    entityFxLanes.band = record.lanes[5];
    entityFxLanes.frame0 = record.lanes[6];
    entityFxLanes.frame1 = record.lanes[7];
    entityFxLanes.frame2 = record.lanes[8];
    entityFxLanes.shape = record.lanes[9];
    entityFxLanes.clip = record.lanes[10];
    entityFxLanes.clipEdge = record.lanes[11];
    entityFxLanes.travel = record.lanes[14];
}

// The extension record (index + 1), by the same includers, only when the flags carry FX_EXT.
fn setEntityFxExt(record: EntityFx) {
    entityFxExt.bio0 = record.lanes[0];
    entityFxExt.bio1 = record.lanes[1];
    entityFxExt.bio2 = record.lanes[2];
    entityFxExt.veins0 = record.lanes[3];
    entityFxExt.veins1 = record.lanes[4];
    entityFxExt.veins2 = record.lanes[5];
    entityFxExt.veins3 = record.lanes[6];
    entityFxExt.hue0 = record.lanes[7];
    entityFxExt.hue1 = record.lanes[8];
    entityFxExt.rim0 = record.lanes[9];
    entityFxExt.rim1 = record.lanes[10];
    entityFxExt.rim2 = record.lanes[11];
}

// ---- Wave 2: the owner frame, the vertex sub-blocks and the clip ---------------------------------
//
// Everything below is reached only through a flag bit, so a draw whose flags do not carry it -- and
// every draw with `fxA.z == 0` -- runs none of it. The vertex half is called from each includer's
// vertex stage (pbr.wgsl `vs_entity`, procedural.wgsl `vs_proc`) for the current AND the previous
// frame's time, so the velocity target sees the motion; the clip half from the lit fragment AND the
// depth-only fragment, so the depth prepass and every shadow map lose exactly what the colour does.

// A world point in its owner's space q: node-local, centred on the drawn bounds, divided by their
// half diagonal (about [-1, 1]), so a pattern or a front rides the owner however it moves.
fn fxOwnerSpace(p: vec3<f32>, f0: vec4<f32>, f1: vec4<f32>, f2: vec4<f32>) -> vec3<f32> {
    let h = vec4<f32>(p, 1.0);
    return vec3<f32>(dot(f0, h), dot(f1, h), dot(f2, h));
}

// The record lanes the vertex stage reads (world::EntityFxLaneIndex 6..9 and 12..15).
struct FxVertexLanes {
    frame0: vec4<f32>,
    frame1: vec4<f32>,
    frame2: vec4<f32>,
    shape: vec4<f32>,
    inflate: vec4<f32>, // x = amplitude (m), y = rate (Hz), z = asymmetry, w = phase
    region: vec4<f32>,  // x = region centre (h), y = region width, z = bulge amplitude, w = bulge width
    travel: vec4<f32>,  // x = speed (h/s), y = interval (h), z = direction, w = radial
    smear: vec4<f32>,   // xyz = smear vector (m), w = sharpness
};

fn fxVertexLanesOf(record: EntityFx) -> FxVertexLanes {
    var l: FxVertexLanes;
    l.frame0 = record.lanes[6];
    l.frame1 = record.lanes[7];
    l.frame2 = record.lanes[8];
    l.shape = record.lanes[9];
    l.inflate = record.lanes[12];
    l.region = record.lanes[13];
    l.travel = record.lanes[14];
    l.smear = record.lanes[15];
    return l;
}

// One breath, x in cycles: 0 -> 1 -> 0, the swell taking `a` of the cycle and the fall the rest.
// Asymmetry > 0 is a quick inhale and a long exhale.
fn fxBreath(x: f32, asymmetry: f32) -> f32 {
    let a = clamp(0.5 - 0.4 * asymmetry, 0.1, 0.9);
    let u = fract(x);
    if (u < a) {
        return smoothstep(0.0, 1.0, u / a);
    }
    return 1.0 - smoothstep(0.0, 1.0, (u - a) / (1.0 - a));
}

// Where the displacement sub-blocks move a vertex at world position `p` with world normal `n`, at
// transport second `t`. The modes SUM (rendering-architecture §7): a Breathing, an Organic Pulsation
// and a Motion Smear on one owner all move it. Only the vertex's position is displaced; the normal is
// the undisplaced one (a swell of a few centimetres does not need a new normal).
fn fxVertexOffset(p: vec3<f32>, n: vec3<f32>, t: f32, flags: u32, l: FxVertexLanes) -> vec3<f32> {
    var off = vec3<f32>(0.0);
    if ((flags & (FX_INFLATE | FX_TRAVEL)) != 0u) {
        let q = fxOwnerSpace(p, l.frame0, l.frame1, l.frame2);
        let h = q.y * l.shape.w + 0.5;
        if ((flags & FX_INFLATE) != 0u) {
            var region = 1.0;
            if (l.region.y > 0.0) {
                let d = (h - l.region.x) / l.region.y;
                region = exp(-d * d);
            }
            off = off + n * (l.inflate.x * fxBreath(t * l.inflate.y + l.inflate.w, l.inflate.z) * region);
        }
        if ((flags & FX_TRAVEL) != 0u) {
            let s = select(1.0 - h, h, l.travel.z > 0.0);
            let interval = max(l.travel.y, 1e-3);
            let u = s - l.travel.x * t;
            let d = (fract(u / interval + 0.5) - 0.5) * interval;
            let w = max(l.region.w, 1e-3);
            off = off + n * (l.region.z * exp(-(d * d) / (w * w)));
        }
    }
    if ((flags & FX_SMEAR) != 0u) {
        let len = length(l.smear.xyz);
        let dir = l.smear.xyz / max(len, 1e-6);
        off = off + l.smear.xyz * pow(clamp(dot(n, dir), 0.0, 1.0), l.smear.w);
    }
    return off;
}

// The clip's keep-value at a world point: kept where k >= the threshold (lane 10.y), which the CPU
// chose so that "nothing hidden" keeps every k and draws no edge, and "all hidden" keeps none
// (world::entityFxClipThreshold). Noise: a dissolve, optionally swept by height (bias > 0 from the
// bottom up, < 0 from the top down). Height and Radial: a growth front, tip or rim first to go and
// last to come, with noise breaking the front up.
fn fxClipKeep(worldPos: vec3<f32>) -> f32 {
    let l = entityFxLanes;
    let q = fxOwnerSpace(worldPos, l.frame0, l.frame1, l.frame2);
    let mode = u32(l.clip.x + 0.5);
    let h = q.y * l.shape.w + 0.5;
    let breakup = l.clipEdge.w;
    let noise = fbm3(q * l.clip.w + vec3<f32>(3.7, 1.3, 5.1), 7u);
    if (mode == 1u) {
        let k = clamp((noise - 0.5) * 2.0 + 0.5, 0.0, 1.0);
        return select(mix(k, 1.0 - h, -breakup), mix(k, h, breakup), breakup >= 0.0);
    }
    var k = 1.0 - h;
    if (mode == 3u) {
        k = 1.0 - length(q - l.shape.xyz) * l.travel.w;
    }
    return k + breakup * 0.5 * (noise - 0.5);
}

// True where the clip removes this fragment; the depth-only entries call this and discard.
fn fxClipped(worldPos: vec3<f32>) -> bool {
    return fxClipKeep(worldPos) < entityFxLanes.clip.y;
}

// The world size of one pixel at a view depth, stretched by grazing: how fine a pattern may get before
// it has to fade to its mean rather than alias. Derivative-free (the pattern branches are uniform per
// draw, but the analysis cannot see that through a private variable), from the projection's focal
// scale -- row 1 of viewProj is P11 times the camera's unit up axis.
fn fxFootprint(viewDepth: f32, nDotV: f32) -> f32 {
    let focal = length(vec3<f32>(frame.viewProj[0].y, frame.viewProj[1].y, frame.viewProj[2].y));
    return viewDepth * 2.0 / max(focal * frame.targetSize.y, 1e-4) / max(nDotV, 0.25);
}

// Hue rotation about the grey axis (Rodrigues), `turns` of the wheel; luminance is nearly kept.
fn fxHueRotate(c: vec3<f32>, turns: f32) -> vec3<f32> {
    let a = turns * 6.28318530718;
    let k = vec3<f32>(0.57735027);
    let cs = cos(a);
    return max(c * cs + cross(k, c) * sin(a) + k * dot(k, c) * (1.0 - cs), vec3<f32>(0.0));
}

// Color Cycling: how far the hue is turned here and now. A range of a whole turn or more runs round
// the wheel; less swings back and forth within it.
fn fxHueTurns(worldPos: vec3<f32>) -> f32 {
    let e = entityFxExt;
    let l = entityFxLanes;
    let q = fxOwnerSpace(worldPos, l.frame0, l.frame1, l.frame2);
    let phase = frame.params.x * e.hue0.x + dot(q, e.hue1.xyz) * e.hue0.z + e.hue1.w;
    if (e.hue0.y >= 1.0) {
        return phase;
    }
    return e.hue0.y * 0.5 * sin(6.28318530718 * phase);
}

// Bioluminescence: photophores, stripes or cell walls in owner space, each cell breathing on its own
// phase, with a slow wave rolling up the body now and then. Fades to its mean coverage where a cell
// is smaller than about a pixel, so a distant creature glows evenly instead of sparkling.
fn fxBioAt(worldPos: vec3<f32>, viewDepth: f32, nDotV: f32) -> vec3<f32> {
    let e = entityFxExt;
    let l = entityFxLanes;
    let q = fxOwnerSpace(worldPos, l.frame0, l.frame1, l.frame2);
    let scale = e.bio0.y;
    let p = q * scale;
    let cov = clamp(e.bio0.z, 0.0, 1.0);
    let foot = fxFootprint(viewDepth, nDotV) * length(l.frame0.xyz) * scale; // pixels, in cells
    let soft = max(0.04, foot);
    let pattern = u32(e.bio0.x + 0.5);
    var mask = 0.0;
    var cell = 0.0;
    var mean = 0.0;
    if (pattern == 2u) { // stripes, wavering round the body
        let s = q.y * scale + 0.6 * (fbm3(p * 0.5, 13u) - 0.5);
        let f = fract(s) - 0.5;
        let halfWidth = mix(0.06, 0.4, cov);
        mask = 1.0 - smoothstep(halfWidth - soft, halfWidth + soft, abs(f));
        cell = hash01(vec3<i32>(i32(floor(s)), 0, 0), 17u);
        mean = 2.0 * halfWidth;
    } else {
        let w = worleyF1F2(p, 11u);
        cell = w.z;
        if (pattern == 3u) { // cell walls
            let width = mix(0.03, 0.22, cov);
            mask = 1.0 - smoothstep(width - soft, width + soft, w.y - w.x);
            mean = 2.5 * width;
        } else { // spots
            let r0 = mix(0.12, 0.45, cov);
            mask = 1.0 - smoothstep(r0 - soft, r0 + soft, w.x);
            mean = 2.0 * r0 * r0;
        }
    }
    mask = mix(mask, clamp(mean, 0.0, 1.0), smoothstep(0.35, 1.2, foot));
    let t = frame.params.x;
    // Asynchronous breathing: every cell on its own phase.
    let breathe = 1.0 - e.bio2.x * (0.5 + 0.5 * sin(6.28318530718 * (t * e.bio1.w + cell)));
    // The wave: once every `interval` seconds a band rolls from the bottom of the body to the top.
    let h = q.y * l.shape.w + 0.5;
    let front = (t - floor(t / e.bio2.z) * e.bio2.z) * e.bio2.y - 0.2;
    let dw = (h - front) / 0.12;
    let wave = e.bio2.w * exp(-dw * dw);
    let color = fxHueRotate(e.bio1.rgb, (cell - 0.5) * e.bio0.w);
    return color * mask * (breathe + wave);
}

// Pulsing Veins: the edges of a domain-warped Worley field (F2 - F1 small) as a vein network,
// thinning away from the source, with pulses of light travelling outward along it.
fn fxVeinsAt(worldPos: vec3<f32>, viewDepth: f32, nDotV: f32) -> vec3<f32> {
    let e = entityFxExt;
    let l = entityFxLanes;
    let q = fxOwnerSpace(worldPos, l.frame0, l.frame1, l.frame2);
    let scale = e.veins0.x;
    var p = q * scale;
    p = p + e.veins0.z * fbm3Vec(p * 0.45, 19u);
    let w = worleyF1F2(p, 23u);
    let c = select(q.y * l.shape.w + 0.5, length(q - l.shape.xyz) * l.travel.w, e.veins0.w > 0.5);
    let width = e.veins0.y * mix(1.0, 0.45, clamp(c, 0.0, 1.0));
    let foot = fxFootprint(viewDepth, nDotV) * length(l.frame0.xyz) * scale;
    let soft = max(width * 0.35, foot * 0.5);
    var mask = 1.0 - smoothstep(width - soft, width + soft, w.y - w.x);
    mask = mix(mask, clamp(width * 2.5, 0.0, 1.0), smoothstep(width * 2.0, width * 8.0, foot));
    let interval = max(e.veins3.x, 1e-3);
    let u = c - e.veins1.w * frame.params.x;
    let d = (fract(u / interval + 0.5) - 0.5) * interval;
    let pw = max(e.veins2.w, 1e-3);
    let pulse = exp(-(d * d) / (pw * pw));
    let intensity = e.veins3.y + (1.0 - e.veins3.y) * pulse;
    return mix(e.veins1.rgb, e.veins2.rgb, clamp(c, 0.0, 1.0)) * mask * intensity;
}

// Rim Light: a kicker. Grazing (1 - N.V)^power, but only on the side facing the rim direction, which
// is fixed in VIEW space so it frames the same way wherever the camera goes.
fn fxRimLightAt(n: vec3<f32>, v: vec3<f32>) -> vec3<f32> {
    let e = entityFxExt;
    let d = e.rim1.xyz;
    let dir = normalize(frame.cameraRight.xyz * d.x + frame.cameraUp.xyz * d.y + frame.cameraForward.xyz * d.z);
    let facing = pow(1.0 - clamp(dot(n, v), 0.0, 1.0), max(e.rim0.w, 0.05));
    let soft = max(e.rim2.x, 1e-3);
    let side = smoothstep(e.rim1.w - soft, e.rim1.w + soft, dot(n, dir));
    return e.rim0.rgb * facing * side;
}

// One cycle of a pulse waveform, x in [0, 1): 0 at both ends, 1 at the crest. The GPU twin of
// `world::pulseWave` (entity_fx.cpp); the numbering is `world::FxWaveform`'s and is append-only.
fn fxWave(kind: u32, xIn: f32) -> f32 {
    let x = fract(xIn);
    if (kind == 1u) { // triangle
        return 1.0 - abs(2.0 * x - 1.0);
    }
    if (kind == 2u) { // square, soft-edged, on for the middle half
        return smoothstep(0.22, 0.28, x) * (1.0 - smoothstep(0.72, 0.78, x));
    }
    if (kind == 3u) { // saw, falling away softly at the end
        return x * (1.0 - smoothstep(0.94, 1.0, x));
    }
    if (kind == 4u) { // heartbeat: lub-dub
        let a = (x - 0.15) / 0.045;
        let b = (x - 0.38) / 0.06;
        return min(1.0, exp(-a * a) + 0.6 * exp(-b * b));
    }
    return 0.5 - 0.5 * cos(6.28318530718 * x); // sine
}

// The travelling band's multiplier at a world position: 1 - depth outside the band, rising to 1 at
// its crest. The cross-section is one cycle of the pulse's waveform spread over the band's width.
fn fxBandAt(worldPos: vec3<f32>) -> f32 {
    let lanes = entityFxLanes;
    let u = dot(worldPos, lanes.bandAxis.xyz) + lanes.bandAxis.w;
    let x = (u - lanes.band.x) / max(2.0 * lanes.band.y, 1e-4) + 0.5;
    var w = 0.0;
    if (x > 0.0 && x < 1.0) {
        w = fxWave(u32(lanes.band.z + 0.5), x);
    }
    return 1.0 - lanes.band.w + lanes.band.w * w;
}

// Bloom Source: the emission target is raised `share` of the way towards the surface's whole
// (pre-fog) radiance, so selective bloom treats that much of it as light; the colour is untouched.
fn fxBloomShare(result: ShadeResult, radiance: vec3<f32>) -> ShadeResult {
    var out = result;
    let share = clamp(entityFxLanes.a.y, 0.0, 1.0);
    out.emission = out.emission + share * max(radiance - out.emission, vec3<f32>(0.0));
    out.bloomWeight = max(out.bloomWeight, share);
    return out;
}

// applyFog / fogHeightIntegral moved to common.wgsl in ADR-099: water.wgsl needs the same fog and
// does not include this file, and two copies of a fog curve is how two surfaces end up in
// different weather.

// Which program the bound material runs; a 16-byte slice of the shared select buffer.
struct MaterialSelect {
    program: i32,   // material program slot, -1 = none
    pad0: i32,
    pad1: i32,
    pad2: i32,
};

@group(2) @binding(0) var materialSampler: sampler;
@group(2) @binding(1) var baseColorTex: texture_2d<f32>;
@group(2) @binding(2) var metallicRoughnessTex: texture_2d<f32>;
@group(2) @binding(3) var normalTex: texture_2d<f32>;
@group(2) @binding(4) var emissiveTex: texture_2d<f32>;
@group(2) @binding(5) var occlusionTex: texture_2d<f32>;
@group(2) @binding(6) var<storage, read> materialPrograms: MaterialProgramBlock;
@group(2) @binding(7) var<uniform> materialSelect: MaterialSelect;

@group(3) @binding(0) var iblSampler: sampler;
@group(3) @binding(1) var irradianceMap: texture_cube<f32>;
@group(3) @binding(2) var prefilteredMap: texture_cube<f32>;
@group(3) @binding(3) var brdfLut: texture_2d<f32>;

fn distributionGGX(nDotH: f32, alpha: f32) -> f32 {
    let a2 = alpha * alpha;
    let d = nDotH * nDotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d + 1e-7);
}

// Height-correlated Smith visibility term (Heitz 2014), already divided by 4 n.l n.v.
fn visibilitySmithGGX(nDotV: f32, nDotL: f32, alpha: f32) -> f32 {
    let a2 = alpha * alpha;
    let ggxV = nDotL * sqrt(nDotV * nDotV * (1.0 - a2) + a2);
    let ggxL = nDotV * sqrt(nDotL * nDotL * (1.0 - a2) + a2);
    return 0.5 / max(ggxV + ggxL, 1e-5);
}

fn fresnelSchlick(cosTheta: f32, f0: vec3<f32>) -> vec3<f32> {
    return f0 + (vec3<f32>(1.0) - f0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

fn fresnelSchlickRoughness(cosTheta: f32, f0: vec3<f32>, roughness: f32) -> vec3<f32> {
    let fr = max(vec3<f32>(1.0 - roughness), f0) - f0;
    return f0 + fr * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// The cotangent frame a tangent-space normal is expressed in (Schüler), from screen-space
// derivatives, so meshes need no tangent attribute. It takes derivatives, so it must be called in
// uniform control flow; `perturbNormal` below is the whole operation for callers that can.
fn cotangentFrame(n: vec3<f32>, worldPos: vec3<f32>, uv: vec2<f32>) -> mat3x3<f32> {
    let dp1 = dpdx(worldPos);
    let dp2 = dpdy(worldPos);
    let duv1 = dpdx(uv);
    let duv2 = dpdy(uv);
    let dp2perp = cross(dp2, n);
    let dp1perp = cross(n, dp1);
    let t = dp2perp * duv1.x + dp1perp * duv2.x;
    let b = dp2perp * duv1.y + dp1perp * duv2.y;
    let scale = max(dot(t, t), dot(b, b));
    if (scale < 1e-12) {
        // No usable UV gradient (procedural geometry often carries a constant UV): fall back to a
        // frame built from the position gradient, so a program's normal channel still perturbs.
        let tf = matSafeNormalize(dp1 - n * dot(n, dp1), vec3<f32>(1.0, 0.0, 0.0));
        return mat3x3<f32>(tf, cross(n, tf), n);
    }
    let invMax = inverseSqrt(scale + 1e-12);
    return mat3x3<f32>(t * invMax, b * invMax, n);
}

fn perturbNormal(n: vec3<f32>, worldPos: vec3<f32>, uv: vec2<f32>, mapNormal: vec3<f32>) -> vec3<f32> {
    return normalize(cotangentFrame(n, worldPos, uv) * mapNormal);
}

// The ADR-036 geometric inputs a fragment can derive from screen-space derivatives:
//   x = signed curvature (1/metre; positive convex, negative concave), from how fast the normal
//       turns per unit of surface travelled: (dN/dx . dP/dx + dN/dy . dP/dy) / (|dP/dx|^2 + |dP/dy|^2)
//   y = cavity: the concave part scaled by the footprint, so it only picks up crevices small
//       enough to matter at this screen size
//   z = normal variance, 0.5 (|dN/dx|^2 + |dN/dy|^2) (Kaplanyan et al. 2016), which drives the
//       roughnessFilter op's specular anti-aliasing
//   w = footprint: world units covered by one pixel, which fades micro detail before it aliases
// Call it in uniform control flow: it takes derivatives.
fn materialGeometry(worldPos: vec3<f32>, n: vec3<f32>) -> vec4<f32> {
    let dpx = dpdx(worldPos);
    let dpy = dpdy(worldPos);
    let dnx = dpdx(n);
    let dny = dpdy(n);
    let denom = max(dot(dpx, dpx) + dot(dpy, dpy), 1e-12);
    let curvature = (dot(dnx, dpx) + dot(dny, dpy)) / denom;
    let footprint = sqrt(denom);
    let variance = 0.5 * (dot(dnx, dnx) + dot(dny, dny));
    let cavity = saturate(max(-curvature, 0.0) * footprint * 8.0);
    return vec4<f32>(curvature, cavity, variance, footprint);
}

// How fast an alpha-masked material's cutoff falls as its texture is minified, per mip level.
// 0.22 halves the cutoff over about three levels, which is roughly where a Quaternius leaf card
// starts losing coverage; it is a constant rather than a knob because preserving what the artist
// authored is not a matter of taste, and a scene that wanted the erosion could only want it by
// accident.
const kAlphaCoverageFade: f32 = 0.22;

// How far down its mip chain a 2D texture is being read, from the UV derivatives -- the quantity
// the sampler computes for itself and WGSL offers no way to ask it for. Uniform control flow only.
fn textureLodFor(uv: vec2<f32>, size: vec2<f32>) -> f32 {
    let dx = dpdx(uv) * size;
    let dy = dpdy(uv) * size;
    return max(0.5 * log2(max(dot(dx, dx), dot(dy, dy))), 0.0);
}

// The material-program inputs only the caller knows: the object-space position (before the
// deformer stack), the object id and the instance record's lanes. Entities and SDF surfaces have
// no instance, so materialInstanceZero() stands in for them.
struct MaterialInstanceInfo {
    localPosition: vec3<f32>,
    objectId: f32,
    instanceIndex: f32,   // normalised index in [0, 1]
    instanceId: f32,
    instanceRandom: vec4<f32>,
    instanceColor: vec4<f32>,
    instanceEmissive: vec4<f32>,
};

fn materialInstanceZero(localPosition: vec3<f32>) -> MaterialInstanceInfo {
    var info: MaterialInstanceInfo;
    info.localPosition = localPosition;
    info.objectId = pickIndex(object.ids.x);
    info.instanceIndex = 0.0;
    info.instanceId = 0.0;
    info.instanceRandom = vec4<f32>(0.0);
    info.instanceColor = vec4<f32>(1.0);
    info.instanceEmissive = vec4<f32>(1.0);
    return info;
}

// Everything one fragment needs to fill the auxiliary targets as well as the colour (ADR-035).
struct ShadeResult {
    color: vec4<f32>,
    normal: vec3<f32>,     // the shading normal, after normal mapping
    roughness: f32,
    emission: vec3<f32>,
    bloomWeight: f32,
    flags: f32,            // 1 = lit, 2 = emissive, 4 = transparent
};

// The whole material evaluation for one fragment. `colorMul` / `emissiveMul` are per-instance
// multipliers on the base colour and emissive (vec3(1.0) for entities). May discard (alpha mask).
// Without instance information (entities before ADR-030 wiring): the object's own local position.
fn shadePbr(worldPos: vec3<f32>, normalIn: vec3<f32>, uv: vec2<f32>, frontFacing: bool,
            colorMul: vec3<f32>, emissiveMul: vec3<f32>, screenUv: vec2<f32>) -> vec4<f32> {
    return shadeSurface(worldPos, normalIn, uv, frontFacing, colorMul, emissiveMul,
                        materialInstanceZero(vec3<f32>(0.0)), screenUv).color;
}

fn shadePbrInstanced(worldPos: vec3<f32>, normalIn: vec3<f32>, uv: vec2<f32>, frontFacing: bool,
                     colorMul: vec3<f32>, emissiveMul: vec3<f32>, info: MaterialInstanceInfo,
                     screenUv: vec2<f32>) -> vec4<f32> {
    return shadeSurface(worldPos, normalIn, uv, frontFacing, colorMul, emissiveMul, info, screenUv).color;
}

fn shadeSurface(worldPos: vec3<f32>, normalIn: vec3<f32>, uv: vec2<f32>, frontFacing: bool,
                colorMul: vec3<f32>, emissiveMul: vec3<f32>, info: MaterialInstanceInfo,
                screenUv: vec2<f32>) -> ShadeResult {
    var result: ShadeResult;
    result.normal = normalize(normalIn);
    result.roughness = 1.0;
    result.emission = vec3<f32>(0.0);
    result.bloomWeight = max(object.ids.z, 0.0);
    result.flags = 1.0;
    // ADR-133: this draw's material tier and its local-light budget. Uniform across the draw, so
    // every branch below that reads `tier` is wave-uniform -- the condition ADR-118 measured a
    // saving to need. `tier == 0` is byte for byte the pre-ADR-133 shader.
    let tier = select(materialTierOf(), proceduralRungTierOf(proceduralRungTier()), kProceduralDraw);
    let tierLocalLights = materialTierLocalLights(tier);
    let texMask = u32(object.flags.w + 0.5);
    let hasBaseColor = (texMask & 1u) != 0u;
    let hasMetalRough = (texMask & 2u) != 0u;
    let hasNormal = (texMask & 4u) != 0u;
    let hasEmissive = (texMask & 8u) != 0u;
    let hasOcclusion = (texMask & 16u) != 0u;

    // The geometric normal (front-facing corrected) and the view vector: the material program's
    // `normal` / `viewDirection` / Fresnel inputs, and the frame the normal map perturbs below.
    var n = normalize(normalIn);
    if (!frontFacing) {
        n = -n;
    }
    // ADR-111: kept before anything perturbs `n`. This is the normal of the surface the depth
    // prepass and the shadow maps actually rasterised, and it is what every shadow term is biased
    // along; `n` below becomes the shading normal and is what the BRDF uses. See ShadeContext in
    // lighting.wgsl for why they have to be two different vectors.
    let geoNormal = n;
    let v = normalize(frame.cameraPos.xyz - worldPos);

    // ---- procedural material program (ADR-030, ADR-036) ----
    var matColor = object.baseColor;        // rgb = base colour, a = opacity
    var matEmissive = object.emissive;      // rgb = emissive colour, w = intensity
    var matRoughMetal = object.material.xy; // x = roughness, y = metallic
    var programNormal = vec3<f32>(0.0, 0.0, 1.0); // tangent-space perturbation the program asked for
    var programOcclusion = 1.0;
    let programIndex = materialSelect.program;
    let viewDepth = max(dot(worldPos - frame.cameraPos.xyz, frame.cameraForward.xyz), 1e-4);
    // The screen-space geometry, the tangent frame and the ambient occlusion the program reads
    // (ADR-036). All three are hoisted out of the branches that use them because they take
    // derivatives, which WGSL only allows in uniform control flow; the guards below are uniform
    // (they read the material's select and the object's flags), so a fragment with no program and
    // no normal map pays for none of it.
    var geometry = vec4<f32>(0.0);
    var tangentFrame = mat3x3<f32>(vec3<f32>(1.0, 0.0, 0.0), vec3<f32>(0.0, 1.0, 0.0), n);
    var occlusion: AoSample;
    occlusion.visibility = 1.0;
    occlusion.bentNormal = n;
    var sampledOcclusion = false;
    if ((programIndex >= 0 || hasNormal) && frame.lightCounts.z < 0.5) {
        tangentFrame = cotangentFrame(n, worldPos, uv);
    }
    // A leaf card's alpha is a coverage mask, and every mip level averages it towards its own mean,
    // so a fixed cutoff eats a little more of the leaf at each level: a crown that is solid up close
    // erodes with distance into a handful of specks that crawl as the camera moves. Scaling the
    // cutoff down as the texture is minified holds roughly the coverage that was authored. Castano
    // (2010) computes the exact per-level scale offline from each mip's alpha histogram; this is the
    // one-line approximation of it, and the derivatives are the same two the block below takes.
    // Hoisted here because it takes them, and the guard is uniform: both terms are per-draw.
    var alphaCutoff = object.flags.y;
    if (object.flags.x > 0.5 && object.flags.x < 1.5 && hasBaseColor) {
        let lod = textureLodFor(uv, vec2<f32>(textureDimensions(baseColorTex, 0)));
        alphaCutoff = alphaCutoff * exp2(-lod * kAlphaCoverageFade);
    }
    // scene::MaterialGate: a gated program runs only on the instances and at the camera distances it
    // admits, and a fragment it refuses takes the program-less path below exactly -- no interpreter,
    // no early ambient-occlusion read, the material's own emission lane. That varies per fragment,
    // so it is not uniform control flow; the geometry's derivatives stay behind the per-draw guard,
    // where they were, and are simply unused on a refused fragment. For a program with no gate
    // `programRuns` is `programIndex >= 0`, and nothing about it changes. The distance is the one
    // the program's own `cameraDistance` input reads.
    let programRuns = programIndex >= 0 &&
                      materialProgramAdmits(programIndex, info.instanceRandom, distance(frame.cameraPos.xyz, worldPos));
    if (programIndex >= 0) {
        geometry = materialGeometry(worldPos, n);
    }
    if (programRuns) {
        occlusion = sampleAmbientOcclusion(screenUv, viewDepth, n);
        sampledOcclusion = true;
    }
    if (programRuns) {
        var ctx = materialContextZero();
        ctx.worldPosition = worldPos;
        ctx.localPosition = info.localPosition;
        ctx.normal = n;
        ctx.uv = uv;
        ctx.objectId = info.objectId;
        ctx.instanceIndex = info.instanceIndex;
        ctx.instanceId = info.instanceId;
        ctx.instanceRandom = info.instanceRandom;
        ctx.instanceColor = info.instanceColor;
        ctx.instanceEmissive = info.instanceEmissive;
        ctx.time = frame.params.x;
        ctx.audio = frame.audio;
        ctx.audioBands = frame.audioBands;
        ctx.beat = frame.beat;
        ctx.viewDirection = v;
        ctx.depth = distance(frame.cameraPos.xyz, worldPos);
        ctx.curvature = geometry.x;
        ctx.cavity = geometry.y;
        ctx.normalVariance = geometry.z;
        ctx.footprint = geometry.w;
        ctx.occlusion = occlusion.visibility;
        ctx.materialId = object.ids.y;
        var base = materialResultZero();
        base.baseColor = object.baseColor.rgb;
        base.metallic = object.material.y;
        base.roughness = object.material.x;
        base.emission = object.emissive.rgb * object.emissive.w;
        base.opacity = object.baseColor.a;
        let program = evaluateMaterialProgram(programIndex, ctx, base);
        // The program's emission is a finished radiance, so the intensity lane becomes 1.
        matColor = vec4<f32>(program.baseColor, program.opacity);
        matEmissive = vec4<f32>(program.emission, 1.0);
        matRoughMetal = vec2<f32>(program.roughness, program.metallic);
        programNormal = program.normal;
        programOcclusion = program.occlusion;
    }

    var baseColor = matColor * vec4<f32>(colorMul, 1.0);
    if (hasBaseColor) {
        if (frame.lightCounts.z < 0.5 || object.flags.z > 0.5) {
            baseColor = baseColor * textureSample(baseColorTex, materialSampler, uv);
        } else if (object.flags.x > 0.5) {
            baseColor.a *= textureSample(baseColorTex, materialSampler, uv).a;
        }
    }
    let alphaMode = object.flags.x;
    if (alphaMode > 0.5 && alphaMode < 1.5 && baseColor.a < alphaCutoff) {
        discard;
    }
    let alpha = select(1.0, baseColor.a, alphaMode > 1.5);

    // ---- Wave 2 (FXL): the clip and the hue cycle on the base colour ----
    // Both behind their own flag bit, read from the lanes the includer filled behind `fxA.z != 0`.
    let fxSurface = u32(entityFxLanes.a.z + 0.5);
    var fxEdge = vec3<f32>(0.0);
    if ((fxSurface & FX_CLIP) != 0u) {
        // The same test `fs_depth` makes (`fxClipped`), so the prepass and the shadows lose exactly
        // these fragments. Discarded by the includer at the end of its entry point; see above.
        let keep = fxClipKeep(worldPos) - entityFxLanes.clip.y;
        fxClipDiscard = keep < 0.0;
        let w = max(entityFxLanes.clip.z, 1e-4);
        fxEdge = entityFxLanes.clipEdge.rgb * (1.0 - smoothstep(0.0, w, keep));
        if (u32(entityFxLanes.clip.x + 0.5) == 1u) {
            // A dissolve chars just behind its burning edge.
            let charred = 1.0 - smoothstep(w, 3.0 * w, keep);
            baseColor = vec4<f32>(baseColor.rgb * (1.0 - 0.85 * charred), baseColor.a);
        }
    }
    var fxHue = 0.0;
    if ((fxSurface & FX_HUE) != 0u) {
        fxHue = fxHueTurns(worldPos);
        if (entityFxExt.hue0.w < 0.5 || entityFxExt.hue0.w > 1.5) {
            baseColor = vec4<f32>(fxHueRotate(baseColor.rgb, fxHue), baseColor.a);
        }
    }

    // ---- ADR-703: the entity's effect lanes (FXL) ----
    //
    // `fxFlags == 0` is every draw no lane effect touches, and the branch below is skipped whole --
    // uniform per draw (ADR-118), since the lanes come from the object uniform. Inside it, the gain
    // (times the travelling band, per fragment) scales the material's OWN emission through its
    // intensity lane -- so its emissive texture and the built-in Fresnel rim below scale with it --
    // and the added glow and rim are computed here and added after the texture, scaled by the same
    // gain: emission = (own * tint + added + rim) * gain * band, all before fog.
    let fxFlags = u32(entityFxLanes.a.z + 0.5);
    var fxAdded = vec3<f32>(0.0);
    if (fxFlags != 0u) {
        var gain = max(entityFxLanes.a.x, 0.0);
        if ((fxFlags & 4u) != 0u) {
            gain = gain * fxBandAt(worldPos);
        }
        matEmissive = vec4<f32>(matEmissive.rgb * entityFxLanes.b.rgb, matEmissive.w * gain);
        if ((fxFlags & 2u) != 0u) {
            let facing = clamp(dot(n, v), 0.0, 1.0);
            let rim = entityFxLanes.rim.rgb * pow(1.0 - facing, max(entityFxLanes.rim.w, 0.05));
            fxAdded = (entityFxLanes.add.rgb + rim) * gain;
        }
        // Wave 2: the patterns and the kicker are added light like the glow, under the same gain (a
        // Pulse on the owner pulses them); the clip's edge is its own light and is not.
        if ((fxFlags & (FX_BIO | FX_VEINS | FX_RIM_LIGHT)) != 0u) {
            let nDotV = clamp(dot(n, v), 0.0, 1.0);
            var pattern = vec3<f32>(0.0);
            if ((fxFlags & FX_BIO) != 0u) {
                pattern = pattern + fxBioAt(worldPos, viewDepth, nDotV);
            }
            if ((fxFlags & FX_VEINS) != 0u) {
                pattern = pattern + fxVeinsAt(worldPos, viewDepth, nDotV);
            }
            if ((fxFlags & FX_RIM_LIGHT) != 0u) {
                pattern = pattern + fxRimLightAt(n, v);
            }
            fxAdded = fxAdded + pattern * gain;
        }
        if ((fxFlags & FX_HUE) != 0u && entityFxExt.hue0.w > 0.5) {
            matEmissive = vec4<f32>(fxHueRotate(matEmissive.rgb, fxHue), matEmissive.w);
            fxAdded = fxHueRotate(fxAdded, fxHue);
        }
        fxAdded = fxAdded + fxEdge;
    }
    let emissiveBase = matEmissive.rgb * emissiveMul;
    var emissive = emissiveBase * matEmissive.w;
    if (hasEmissive) {
        emissive = emissive * textureSample(emissiveTex, materialSampler, uv).rgb;
    }
    if (fxFlags != 0u) {
        emissive = emissive + fxAdded;
    }

    if (object.flags.z > 0.5) { // unlit
        // ADR-207: additive, even here. An unlit surface is one the lighting does not reach, not one
        // the world cannot touch.
        let fx = wavesAt(worldPos, n, emissive);
        result.color = vec4<f32>(applyFog(baseColor.rgb + emissive + fx.radiance, worldPos), alpha);
        result.normal = n;
        result.emission = baseColor.rgb + emissive + fx.radiance;
        result.bloomWeight = max(result.bloomWeight, fx.bloom);
        if ((fxFlags & 8u) != 0u) { // ADR-703: Bloom Source (an unlit surface's emission is already all of it)
            result = fxBloomShare(result, baseColor.rgb + emissive + fx.radiance);
        }
        result.flags = 2.0;
        return result;
    }

    if (frame.lightCounts.z > 0.5) {
        let roughness = clamp(matRoughMetal.x, 0.15, 1.0);
        var context: ShadeContext;
        context.worldPos = worldPos;
        context.normal = n;
        context.geoNormal = geoNormal;
        context.view = v;
        context.diffuseColor = baseColor.rgb;
        context.f0 = vec3<f32>(0.04);
        context.roughness = roughness;
        context.alpha = roughness * roughness;
        context.nDotV = max(dot(n, v), 0.0);
        context.screenUv = screenUv;
        context.viewDepth = viewDepth;
        context.rotation = gradientNoise(screenUv * frame.targetSize.xy) * 6.28318531;
        context.jitter = 0.5;
        context.maskable = alphaMode < 1.5; // ADR-087: blended surfaces are not in the depth prepass
        context.tier = tier;
        context.localLightBudget = tierLocalLights;
        let lighting = directLighting(context);
        // The flat tier's AO budget is zero: it takes the ambient unoccluded. A material program
        // may already have sampled it, in which case the read is spent whatever this says.
        if (!sampledOcclusion && tier < 2u) {
            occlusion = sampleAmbientOcclusion(screenUv, viewDepth, n);
        }
        // The styled path's ambient carries most of a night landscape, so screen-space AO applied
        // to it at full depth writes its own sampling noise straight into the largest term in the
        // image -- visible as a faint lattice on open ground, and absent from the PBR control where
        // ambient is one contributor among several. frame.styledSky.w is the floor that keeps the
        // contact darkening without printing the noise; a scene that wants deeper contact should
        // reach for the ground ambient below, which is smooth, rather than for this.
        let visibility = mix(frame.styledSky.w, 1.0,
                             clamp(occlusion.visibility * programOcclusion, 0.0, 1.0));
        let hemisphere = mix(frame.styledGround.rgb, frame.styledSky.rgb, n.y * 0.5 + 0.5);
        let ambient = baseColor.rgb * hemisphere * visibility;
        let edge = pow(1.0 - context.nDotV, 4.0) * smoothstep(-0.2, 0.7, n.y);
        let rim = baseColor.rgb * vec3<f32>(0.25, 0.45, 0.5) * edge * 0.16;
        // ADR-207: the world-effect contribution, added to the radiance *before* fog so a wave far
        // down a valley is seen through the air that is between it and the eye, and added to the
        // emission target so the bloom chain sees it too.
        let fx = wavesAt(worldPos, n, emissive);
        // ADR-230 §6: the sky's light on the ground. Multiplied by the albedo because it is
        // *incoming light* and not paint -- adding it flat would lift every black surface in the
        // valley to the aurora's colour, which is the "flattens the scene" failure §6 warns about.
        // It is deliberately absent from `result.emission`: illumination is not emission, and a
        // ground wash that blooms is a ground wash nobody can control.
        let skyLit = atmosphereGroundAt(worldPos, n) * baseColor.rgb;
        result.color = vec4<f32>(applyFog(lighting.diffuse + lighting.specular + ambient + emissive + rim
                                          + fx.radiance + skyLit,
                                         worldPos), alpha);
        result.normal = n;
        result.roughness = roughness;
        result.emission = emissive + fx.radiance;
        result.bloomWeight = max(result.bloomWeight, fx.bloom);
        if ((fxFlags & 8u) != 0u) { // ADR-703: Bloom Source -- the same sum the colour fogs, pre-fog
            result = fxBloomShare(result, lighting.diffuse + lighting.specular + ambient + emissive + rim
                                          + fx.radiance + skyLit);
        }
        result.flags = select(1.0, 3.0, dot(result.emission, result.emission) > 1e-6);
        if (alphaMode > 1.5) {
            result.flags = result.flags + 4.0;
        }
        return result;
    }

    var roughness = matRoughMetal.x;
    var metallic = matRoughMetal.y;
    if (hasMetalRough) {
        let mr = textureSample(metallicRoughnessTex, materialSampler, uv);
        roughness = roughness * mr.g;
        metallic = metallic * mr.b;
    }
    roughness = clamp(roughness, 0.045, 1.0);
    metallic = clamp(metallic, 0.0, 1.0);
    var ao = programOcclusion; // ADR-036: the program's own occlusion channel
    if (hasOcclusion) {
        let occ = textureSample(occlusionTex, materialSampler, uv).r;
        ao = ao * (1.0 + object.material.w * (occ - 1.0));
    }

    // ADR-036: the program's tangent-space normal first, then the material's normal map over it
    // (reoriented, so the two compose rather than one replacing the other).
    var mapNormal = programNormal;
    var perturb = programNormal.z < 0.99999;
    if (hasNormal && tier < 2u) {
        var mapN = textureSample(normalTex, materialSampler, uv).xyz * 2.0 - 1.0;
        mapN = vec3<f32>(mapN.xy * object.material.z, mapN.z);
        mapNormal = matReorientNormal(mapNormal, normalize(mapN));
        perturb = true;
    }
    if (perturb) {
        n = normalize(tangentFrame * mapNormal);
    }
    let nDotV = max(dot(n, v), 1e-4);

    let albedo = baseColor.rgb;
    let f0 = mix(vec3<f32>(0.04), albedo, metallic);
    let diffuseColor = albedo * (1.0 - metallic);
    let alphaR = roughness * roughness;

    // ---- direct lighting (ADR-033): clustered, area-aware, shadowed ----
    var ctx: ShadeContext;
    ctx.worldPos = worldPos;
    ctx.normal = n;
    ctx.geoNormal = geoNormal;
    ctx.view = v;
    ctx.diffuseColor = diffuseColor;
    ctx.f0 = f0;
    ctx.roughness = roughness;
    ctx.alpha = alphaR;
    ctx.nDotV = nDotV;
    ctx.screenUv = screenUv;
    ctx.viewDepth = viewDepth;
    // Deterministic per-pixel rotation and ray offset: the frame index enters through the sample
    // position, never a wall clock, so the same frame renders identically twice.
    let noise = gradientNoise(screenUv * frame.targetSize.xy);
    ctx.rotation = noise * 6.28318531;
    ctx.jitter = noise;
    ctx.maskable = alphaMode < 1.5; // ADR-087: blended surfaces are not in the depth prepass
    ctx.tier = tier;
    ctx.localLightBudget = tierLocalLights;
    let lit = directLighting(ctx);
    let direct = lit.diffuse + lit.specular;

    // ---- ambient occlusion (ADR-034): applied to ambient diffuse, and to specular via the bent normal ----
    // A material program already sampled it above (it can read the visibility); otherwise sample
    // it now, on the lit path only.
    if (!sampledOcclusion && tier < 2u) {
        occlusion = sampleAmbientOcclusion(screenUv, viewDepth, n);
    }
    let aoStrength = clamp(frame.aoParams.x, 0.0, 4.0);
    let visibility = clamp(mix(1.0, occlusion.visibility, aoStrength), 0.0, 1.0);
    let bentNormal = normalize(mix(n, occlusion.bentNormal, aoStrength * 0.9));
    // Frostbite's specular occlusion from the visibility cone (Lagarde & de Rousiers 2014).
    let specularOcclusion =
        clamp(pow(nDotV + visibility, exp2(-16.0 * roughness - 1.0)) - 1.0 + visibility, 0.0, 1.0);

    // ---- image-based lighting (split sum) or hemispheric fallback ----
    var ambient: vec3<f32>;
    let kS = fresnelSchlickRoughness(nDotV, f0, roughness);
    let kD = (vec3<f32>(1.0) - kS) * (1.0 - metallic);
    if (frame.envParams.w > 0.5 && tier < 2u) {
        let r = reflect(-v, n);
        let irradiance = textureSample(irradianceMap, iblSampler, envRotate(bentNormal)).rgb;
        let maxMip = frame.envParams.y;
        let prefiltered = textureSampleLevel(prefilteredMap, iblSampler, envRotate(r), roughness * maxMip).rgb;
        let brdf = textureSample(brdfLut, iblSampler, vec2<f32>(nDotV, roughness)).rg;
        let specular = prefiltered * (kS * brdf.x + brdf.y) * specularOcclusion;
        ambient = (kD * irradiance * albedo * visibility + specular) * frame.params.w;
    } else {
        let sky = vec3<f32>(0.10, 0.12, 0.20);
        let ground = vec3<f32>(0.02, 0.015, 0.03);
        let hemi = mix(ground, sky, bentNormal.y * 0.5 + 0.5);
        ambient = (kD * albedo * hemi * visibility + kS * hemi * 0.5 * specularOcclusion) * 0.8;
    }
    ambient = ambient * ao;

    // Fresnel rim tinted with the emissive colour so glowing objects read as luminous at grazing angles.
    let rim = pow(1.0 - nDotV, 3.0) * emissiveBase * (0.3 * matEmissive.w);

    // ADR-207, as on the styled path above: additive, pre-fog, and into the emission target.
    let fx = wavesAt(worldPos, n, emissive);
    // ADR-230 §6, as on the styled path above: albedo-multiplied, and not into the emission target.
    let skyLit = atmosphereGroundAt(worldPos, n) * baseColor.rgb;
    result.color = vec4<f32>(applyFog(direct + ambient + emissive + rim + fx.radiance + skyLit, worldPos), alpha);
    result.normal = n;
    result.roughness = roughness;
    result.emission = emissive + rim + fx.radiance;
    result.bloomWeight = max(result.bloomWeight, fx.bloom);
    if ((fxFlags & 8u) != 0u) { // ADR-703: Bloom Source -- the same sum the colour fogs, pre-fog
        result = fxBloomShare(result, direct + ambient + emissive + rim + fx.radiance + skyLit);
    }
    result.flags = select(1.0, 3.0, dot(result.emission, result.emission) > 1e-6);
    if (alphaMode > 1.5) {
        result.flags = result.flags + 4.0;
    }
    return result;
}
