// Volumetric atmosphere (ADR-032). Two passes, both a fullscreen triangle, encoded by
// rendering::VolumeRenderer right after the lit pass and before the post chain:
//
//   fs_volume     raymarch into an RGBA16F target at `QualitySettings::volumeResolutionScale` of
//                 the scene's resolution (ADR-139; 0.5 is the shipped default, 1.0 is per pixel
//                 and is what Offline renders): rgb = in-scattered radiance
//                 that reached the eye, a = transmittance along the ray. Reads the scene depth
//                 so the march stops at the first surface (the fog is occluded correctly).
//   fs_composite  full resolution, into the HDR target with blending
//                 (src One, dst SrcAlpha) so the result is `scatter + hdr * transmittance`.
//                 The half-res texels are upsampled with a depth-aware bilinear filter: each of
//                 the four neighbours is weighted by how close the depth it marched (the
//                 full-res depth at 2*texel) is to this pixel's, so fog does not bleed across
//                 silhouettes.
//
// Density (the model in scene_types.hpp Environment):
//   density = volumeDensity * exp(-max(0, y - fogHeight) * fogHeightFalloff)
//             * (1 + volumeNoiseAmount * (fbm3(p * volumeNoiseScale + t * volumeNoiseSpeed) * 2 - 1))
//             * densityField(p)                                    (1 when no field is named)
// Extinction is density * volumeAbsorption, in-scatter density * volumeScattering * the key
// light through a Henyey-Greenstein phase (volumeAnisotropy), emission density * volumeEmission
// tinted by the colour field (or fogColor). Step starts are jittered by a hash of the pixel and
// the frame index - never the wall clock - so two renders of the same frame are identical.
//
// Bind groups: 0 = frame (common.wgsl) + the grid table (fields.wgsl); 1 = this pass's own
// {1 VolumeUniforms, 2 FieldBlock, 3 scene depth, 4 the half-res volume texture (composite only)}.
// Binding 0 of group 1 is deliberately empty: common.wgsl declares `object` there and no entry
// point below reads it. Mirrors rendering/volume_renderer.hpp.
#include "common.wgsl"
#include "fields.wgsl"
// ADR-388. After fields.wgsl, which is what brings in noise.wgsl's fbm3; the include directive
// does not de-duplicate, so this file must not include noise.wgsl itself.
#include "vortex.wgsl"
// ADR-563: the fog bank's own analytic field. Included after vortex.wgsl for the same reason
// vortex.wgsl is included after fields.wgsl -- the include directive does not de-duplicate, and
// this file must not pull noise.wgsl in twice.
#include "fog.wgsl"
// ADR-580, the Tornado: the second density function a medium slot can select. It needs
// `fbm3` and deliberately does not include `noise.wgsl` -- the include directive does not
// de-duplicate and `fields.wgsl` above has already brought it in.
#include "tornado.wgsl"

struct VolumeUniforms {
    params0: vec4<f32>,   // density, fogHeight, fogHeightFalloff, scattering
    params1: vec4<f32>,   // absorption, anisotropy, emission, maxDistance
    noiseParams: vec4<f32>, // noiseAmount, noiseScale, noiseSpeed, time (seconds)
    info: vec4<f32>,      // steps, density field slot (-1 none), colour field slot (-1 none), frame index
    sizes: vec4<f32>,     // march width, march height, full width, full height (ADR-139)
    depthParams: vec4<f32>, // camera near, camera far, march start jitter (ADR-461), 0
    fogColor: vec4<f32>,  // rgb = emission tint when no colour field is named
    glow: vec4<f32>,      // x = particle glow entries to read (ADR-040), y = local-light strength
    heightFog: vec4<f32>, // ADR-568: x = fogUpperDensity, y = fogHeightCurve; ADR-715: z = fogGroundFollow, w = 0
    selfShadow: vec4<f32>, // ADR-570: x = shadow march steps (0 = off), y = strength, zw = 0
    // ADR-562: the placed media, as lanes. `mediaInfo.x` is how many are live.
    //
    // Was twelve named `vortexN` members carrying exactly ONE medium, so the second placed medium
    // in a scene rendered as nothing and nothing said so (ADR-560). A slot is 16 `vec4`; slot `s`
    // occupies `media[s * 16 .. s * 16 + 15]`, and the lane map is declared beside each kind's
    // `packMedium` on the CPU. Lane 0's `.w` is the radius and is the per-slot gate, so an unused
    // slot costs one comparison.
    //
    // Flattened rather than `array<array<vec4<f32>, 16>, N>` because a uniform array of arrays has
    // a stride the WGSL/Metal layout rules make easy to get subtly wrong, and a flat array with an
    // index helper cannot be.
    mediaInfo: vec4<f32>,
    media: array<vec4<f32>, 64>,   // kMaxMedia (4) * kMediumLanes (16)
};

// Slot `s`'s lane `l`. The one place the flattening is expressed.
//
// **THE SAME LANE MEANS DIFFERENT THINGS TO DIFFERENT KINDS, AND THAT IS NOT A BUG.** A slot is
// one kind -- `mediumKind(s)` says which -- and never two at once, so each kind reads the sixteen
// lanes through its own map:
//
//   lanes 0..12   vortex/fog: the shared `VortexField`.     tornado: its own `TornadoField`.
//   lanes 13..14  fog: its shape controls (ADR-563).        tornado: colour + per-metre coefficient.
//   lane  15      the KIND TAG, for every kind (ADR-562). No packer may write it; it is set
//                 centrally in `buildAtmosphericFrame` after each kind's `pack` returns.
//
// So a reviewer reading `mediaLane(s, 13u)` in two places and seeing two different meanings is
// looking at the design rather than at a collision. `agent/tornado`'s packer DID collide with lane
// 15 once, writing a colour over the tag -- had the tag landed first that would have presented as
// a tornado intermittently rendering as a comet, which is a long thing to chase. The lane budget
// for a new kind is 0..14; fifteen is spoken for.
fn mediaLane(s: u32, l: u32) -> vec4<f32> {
    return vol.media[s * 16u + l];
}

// The packed vortex uniforms for slot `s`, in the order `shaders/vortex.wgsl` names them.
fn mediumVortexUniforms(s: u32) -> VortexUniformsWgsl {
    return VortexUniformsWgsl(mediaLane(s, 0u), mediaLane(s, 1u), mediaLane(s, 2u),
                              mediaLane(s, 3u), mediaLane(s, 4u), mediaLane(s, 6u),
                              mediaLane(s, 7u), mediaLane(s, 8u));
}

@group(1) @binding(1) var<uniform> vol: VolumeUniforms;
@group(1) @binding(2) var<uniform> fieldBlock: FieldBlock;
@group(1) @binding(3) var sceneDepth: texture_depth_2d;
@group(1) @binding(4) var volumeTex: texture_2d<f32>;
// ADR-040: two vec4 per particle system - (centre.xyz, spread radius) and (colour.rgb, power) -
// written by particles.wgsl's cs_glow_top. Slots past vol.glow.x are zero.
@group(1) @binding(5) var<storage, read> particleGlow: array<vec4<f32>>;

struct FsIn {
    @builtin(position) pos: vec4<f32>,
    @location(0) uv: vec2<f32>,
};

// The usual oversized triangle: uv (0,0) top-left to (1,1) bottom-right.
@vertex
fn vs_volume(@builtin(vertex_index) index: u32) -> FsIn {
    var out: FsIn;
    let uv = vec2<f32>(f32((index << 1u) & 2u), f32(index & 2u));
    out.uv = uv;
    out.pos = vec4<f32>(uv * vec2<f32>(2.0, -2.0) + vec2<f32>(-1.0, 1.0), 0.0, 1.0);
    return out;
}

fn ndcOf(uv: vec2<f32>) -> vec2<f32> {
    return vec2<f32>(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
}

// World position of an NDC point at clip depth z (0 = near plane, 1 = far plane).
fn worldAt(ndc: vec2<f32>, z: f32) -> vec3<f32> {
    let p = frame.invViewProj * vec4<f32>(ndc, z, 1.0);
    return p.xyz / p.w;
}

// ADR-707: the direction of the camera ray through `ndc`, built from the camera's own basis and
// NOT from `worldAt(ndc, 1.0) - worldAt(ndc, 0.0)`.
//
// That subtraction is what this used to be, and it is wrong in a way that depends on WHERE the
// camera stands. The composition derives the far plane as fifty times the orbit radius, so a
// camera 4 km from its target has a 212 km far plane and a near/far ratio near 1:400,000. At
// `z = 1` the inverse view-projection's `w` is about 5e-6 -- the difference of two numbers near 1
// in f32 -- and its rounding error multiplies the camera's WORLD position into the far point. So
// the error grows with the distance from the world origin: measured on the CPU in f32 against a
// double reference, the worst direction error over the `_tc-4` frame is 1.3e-3 rad with the storm
// at x = 7857 m and 3.0e-5 rad with the same storm at x = 0, which at 4.2 km is 5.6 m against
// 0.13 m. On the GPU the error came back piecewise-constant across the screen, so whole 60-pixel
// columns of pixels marched one ray, and a 110 m column 4 km away was hit in some and missed in
// the rest: the "Tall Column is missing its middle" defect, which is not in the field at all.
//
// The basis is exact to f32: `cameraRight/Up/Forward` are orthonormal, and the projection's two
// scale factors are the lengths of the view-projection's first two rows (the view's rows are
// unit vectors). This ignores an off-centre projection term, which `Camera::projection` never
// produces; if one is ever added, add it here.
fn viewRayDirection(ndc: vec2<f32>) -> vec3<f32> {
    let vp = frame.viewProj;
    let sx = length(vec3<f32>(vp[0].x, vp[1].x, vp[2].x));
    let sy = length(vec3<f32>(vp[0].y, vp[1].y, vp[2].y));
    return normalize(frame.cameraForward.xyz + frame.cameraRight.xyz * (ndc.x / max(sx, 1e-6)) +
                     frame.cameraUp.xyz * (ndc.y / max(sy, 1e-6)));
}

// View-space distance a clip depth stands for (used only to weight the upsample).
fn linearDepth(z: f32) -> f32 {
    let near = vol.depthParams.x;
    let far = vol.depthParams.y;
    return near * far / max(far - z * (far - near), 1e-6);
}

// Deterministic jitter in [0, 1) from the pixel and the frame index (no wall clock).
//
// ADR-461: scaled by `Environment::volumeJitter` (depthParams.z), and centred on the middle of the
// step so that lowering it converges on the step's midpoint rather than on its start -- the mean
// sample position must not move, or the medium's integrated density moves with it and every
// per-metre coefficient calibrated against it is wrong (ADR-374/379/381/389's family).
//
// Why it is a control at all: jitter turns banding into noise, which is a good trade when the
// medium varies LITTLE across one step. The cosmic vortex is the opposite -- at 4000 m over 32
// steps a step is 125 metres and the funnel changes completely across one, so a full-step offset
// between neighbouring pixels is 125 metres of uncorrelated displacement and it reads as
// salt-and-pepper. ADR-460 measured that on a field with the noise switched off entirely and the
// grain still fell 61% when the steps went up eight times; this is the same artifact from the
// other side, for nothing.
//
// An ORDERED offset was tried first and is not the answer: interleaved gradient noise at the same
// amplitude moved the grain by -11% where the field is smooth and **+8% on the shipped frame**,
// where the field is aliased noise and a structured sample pattern exposes error that an
// independent one averages away. That is ADR-389's weight-based band-limit finding again, in a
// different mechanism. The amount is the lever; the arrangement is not.
fn stepJitter(px: vec2<i32>, frameIndex: u32) -> f32 {
    let h = pcg3d(vec3<u32>(bitcast<u32>(px.x), bitcast<u32>(px.y), frameIndex));
    let u = f32(h.x) * (1.0 / 4294967296.0);
    return 0.5 + clamp(vol.depthParams.z, 0.0, 1.0) * (u - 0.5);
}

// ADR-371: the cosmic vortex, as a world-space density field inside the volumetric march.
//
// WHY HERE and not a new pass or a skybox. The brief is emphatic that this is not a backdrop: it
// must sit physically below the island, be seen in perspective, be occluded by the rock, gain depth
// as the camera drops and flatten as it rises. The volumetric pass already gives every one of those
// for free -- it marches world space, it stops at the depth buffer, and ADR-139's half-resolution
// march with a depth-aware upsample is the scalable quality knob the brief asks for. A second
// raymarcher would be a second set of all of that, drifting.
//
// The model is the polar construction the brief describes: radius and angle about the centre, the
// angle sheared by radius so the structure winds, then domain-warped noise at three spatial and
// three TEMPORAL rates. The last part is what stops it reading as a screensaver -- a single rate
// makes everything move together, which the eye reads instantly as procedural.
// ADR-388: the funnel's shape comes from `shaders/vortex.wgsl` now, which is the transliteration
// of `core/vortex.cpp`. It used to live here, in this file, which meant the volumetric march was
// the only thing in the engine that could ask where the vortex was -- particles approximated it
// with an attractor and an orbit force (ADR-380), and anything else had to guess again.
//
// This wrapper exists so the march reads the five uniform slots it already uploads. The numbers
// and their order are unchanged, which is what makes the Tree of Life byte-identical across this
// move rather than something to re-tune.
// ADR-389: the march passes its own step length so the field can drop the octaves this many
// samples cannot resolve. `info.x` is the step count and `params1.w` the far distance, which is the
// same pair `fs_march` divides to get `stepLength` -- kept in one expression here so the two cannot
// disagree about how finely this frame is being sampled.
fn vortexFilterWidth() -> f32 {
    return vol.params1.w / max(vol.info.x, 1.0);
}

// ADR-562's kind tag, in the slot's last lane. ADR-563 is the first reader: until a second kind
// needed a different density function the tag was packed, compared and never uploaded, which is
// this branch's own defect family inside the foundation written to fix it.
const kMediumKindFog: u32 = 4u; // AtmosphereKind::VolumetricFog

fn mediumKind(s: u32) -> u32 {
    return u32(mediaLane(s, 15u).x + 0.5);
}

// The fog bank reads the SAME lanes the vortex does for the parameters they share -- centre,
// radius, thickness -- and its own shape controls from the lanes the vortex leaves empty. One lane
// map, two readings of it, which is what keeps a fog bank a different authoring surface onto one
// primitive rather than a second primitive (ADR-500's argument, still standing).
fn mediumFogUniforms(s: u32) -> FogUniformsWgsl {
    return FogUniformsWgsl(mediaLane(s, 0u), mediaLane(s, 13u), mediaLane(s, 14u),
                           mediaLane(s, 7u), mediaLane(s, 12u), mediaLane(s, 2u),
                           mediaLane(s, 6u));
}

// ADR-580's tornado: the first kind whose field is a DIFFERENT function rather than a different
// reading of the same lanes. A fog bank and a cosmic vortex are one primitive with two authoring
// surfaces and share `vortexShapeAt`; a tornado is a different phenomenon.
const kMediumKindTornado: u32 = 5u; // AtmosphereKind::Tornado

// The packed tornado uniforms for slot `s`, in the order `shaders/tornado.wgsl` names them. Its
// thirteen field lanes are 0..12 and its appearance is 13..14 -- the same two lanes a fog bank
// uses for its shape, read differently, because a slot is one kind and never both.
fn mediumTornadoUniforms(s: u32) -> TornadoUniformsWgsl {
    return TornadoUniformsWgsl(mediaLane(s, 0u), mediaLane(s, 1u), mediaLane(s, 2u),
                               mediaLane(s, 3u), mediaLane(s, 4u), mediaLane(s, 5u),
                               mediaLane(s, 6u), mediaLane(s, 7u), mediaLane(s, 8u),
                               mediaLane(s, 9u), mediaLane(s, 10u), mediaLane(s, 11u),
                               mediaLane(s, 12u));
}

// The per-metre EXTINCTION for slot `s`. Kind-aware for the same reason `mediumShape` is: a
// vortex and a fog bank share a lane layout because they share a field, and a tornado does not.
// Reading `lane(1).w` unconditionally -- which both call sites used to do -- gives a tornado its
// own `radiusMidControl` as a density, which is a number in the tens rather than the hundredths.
fn mediumDensityCoeff(s: u32) -> f32 {
    if (mediumKind(s) == kMediumKindTornado) {
        return mediaLane(s, 13u).w;
    }
    return mediaLane(s, 1u).w;
}

// How much of the SCENE's light this medium scatters (ADR-388). A tornado defaults to LIT and a
// cosmic vortex to unlit, which is the reversal that makes one smoke and the other a nebula.
fn mediumScatterWeight(s: u32) -> f32 {
    if (mediumKind(s) == kMediumKindTornado) {
        return mediaLane(s, 12u).w;
    }
    return mediaLane(s, 5u).z;
}

// How much of a COMET's light this medium takes. A tornado has no such control -- it is a cosmic
// vortex's, for the one shot that needed it -- so it answers zero rather than reading a lane that
// means something else entirely.
fn mediumCometResponse(s: u32) -> f32 {
    if (mediumKind(s) == kMediumKindTornado) {
        return 0.0;
    }
    return mediaLane(s, 5u).x;
}

// The dispatch the kind tag exists for.
fn mediumShape(s: u32, p: vec3<f32>, t: f32) -> f32 {
    if (mediumKind(s) == kMediumKindFog) {
        return fogShapeAt(mediumFogUniforms(s), p, t);
    }
    if (mediumKind(s) == kMediumKindTornado) {
        return tornadoDensityAt(mediumTornadoUniforms(s), p, t, vortexFilterWidth());
    }
    return vortexShapeAt(mediumVortexUniforms(s), p, t, vortexFilterWidth());
}

// The medium's own light. Emissive rather than lit: nothing in this scene could illuminate
// something that size, and the brief's reference is a nebula, which glows.
fn mediumEmissionAt(s: u32, p: vec3<f32>, shape: f32, t: f32) -> vec3<f32> {
    if (mediumKind(s) == kMediumKindTornado) {
        // A tornado is SMOKE and is mostly LIT rather than glowing, so its emission may be zero and
        // it still reads -- the opposite default from a nebula. Two colours rather than three: the
        // vortex's luminous accent is a filament highlight, and a tornado's equivalent is the
        // helical striations, which are geometry here rather than colour.
        // Lanes 13 and 14 carry a colour and its own per-metre coefficient each; lane 15 is the
        // kind tag and is not appearance.
        let thin = mediaLane(s, 13u);
        let thick = mediaLane(s, 14u);
        if (shape <= 0.0 || thick.w <= 0.0) {
            return vec3<f32>(0.0);
        }
        let c = mix(thin.rgb, thick.rgb, smoothstep(0.0, 0.85, shape));
        return c * (shape * thick.w);
    }
    let l3 = mediaLane(s, 3u);
    if (shape <= 0.0 || l3.z <= 0.0) {
        return vec3<f32>(0.0);
    }
    // Colour hierarchy: DARK -> MID -> LUMINOUS ACCENT, keyed on density, so the bright colour
    // appears only in the dense filaments and the bulk of the cloud stays deep. Saturating
    // everything is the failure mode the brief names.
    var c = mix(mediaLane(s, 9u).rgb, mediaLane(s, 10u).rgb, smoothstep(0.0, 0.45, shape));
    let filament = smoothstep(0.62, 0.95, shape) * clamp(l3.w, 0.0, 4.0);
    c = c + mediaLane(s, 11u).rgb * filament;
    // ADR-575 (§26): the emission's own height influence, as a per-kind ARM rather than a lane
    // read. `mediumEmissionAt` is a shared accessor and lane 12's meaning is per-kind, which is
    // precisely the shape ADR-562 §9 recorded three defects of -- so the fog's number is reached
    // through the same dispatch `mediumShape` uses and a kind that has no such control is
    // untouched, by construction rather than by a zero.
    var height = 1.0;
    if (mediumKind(s) == kMediumKindFog) {
        let f = mediumFogUniforms(s);
        height = fogEmissionHeight(f, p.y - f.f0.y);
    }
    return c * (shape * l3.z * height);
}

// ADR-566: the bound the interval below is built from -- (radiusXZ, yBot, yTop).
//
// `world::mediumBound` in `src/world/medium_bound.cpp` is the transliteration of this function,
// and that file carries the full argument. The short version: a bound is a CLAIM that every
// non-zero sample of this slot's field lies inside it, a generous bound costs only field
// evaluations (the march's step positions do not depend on it), and a tight one deletes part of
// the medium in the way that is hardest to see. So when it is uncertain, be generous.
//
// It was tight in two places, both invisible at the defaults and both severe at the ends of the
// controls that caused them:
//   - horizontally, `fogEllipticalRadius` normalises the long axis by `radius * bankLength`, so a
//     bank reaches `bankLength` times as far along it. At `bankLength` 6 the old bound cut five
//     sixths of the length off;
//   - vertically, three thicknesses is where a GAUSSIAN ends. A fog bank's upper profile is an
//     EXPONENTIAL whose rate is `heightFalloff`, and at the control's low end 82% of the column's
//     optical depth lay above the old ceiling.
fn mediumBoundOf(s: u32) -> vec3<f32> {
    let l0 = mediaLane(s, 0u);
    let radius = l0.w;
    let breath = 1.0 + max(mediaLane(s, 3u).x, 0.0);
    let thickness = max(mediaLane(s, 1u).x, 1e-3);
    let depth = max(mediaLane(s, 4u).x, 0.0);
    let centre = l0.xyz;

    // ADR-580. A tornado's bound is the same cylinder with different numbers, and it is the case
    // the cylinder was chosen for: a column 80 m across and 800 m tall is 1% of its own bounding
    // sphere. `l0.w` is the HEIGHT here rather than a radius, and `l0.xyz` is the GROUND CONTACT
    // rather than a centre -- the two conventions differ on purpose, and this is one of the two
    // places both are read, so it is where they could be confused.
    //
    // Moved here from `mediumInterval` when `agent/fog` factored the bound out (ADR-566): the
    // bound now has a CPU twin, `world::mediumBound`, and a per-kind arm that lived in the
    // interval would have been invisible to it.
    if (mediumKind(s) == kMediumKindTornado) {
        let l1 = mediaLane(s, 1u);
        let l2 = mediaLane(s, 2u);
        let l4 = mediaLane(s, 4u);
        let l7 = mediaLane(s, 7u);
        let l8 = mediaLane(s, 8u);
        // The radius curve is a quadratic Bezier, so it never leaves the convex hull of its three
        // control values -- taking the largest is a provable bound and not an estimate.
        let widest = max(l1.x, max(l1.y, l1.z));
        let funnel = widest * (1.0 + max(l2.x, 0.0) + max(mediaLane(s, 3u).x, 0.0));
        let skirt = l1.x * max(l4.x, 1.0) * (1.0 + max(l4.w, 0.0));
        let cloud = l1.z * max(l8.w, 1.0);
        // The axis is a curve: the lean displaces the top and the wobble swings it, and both move
        // the whole column sideways within the bound rather than deforming it.
        let lateral = length(vec2<f32>(l7.x, l7.y)) + max(l7.z, 0.0);
        // Vertically the field is compactly supported: nothing above `h = 1.08`, and nothing below
        // the funnel's tip or the debris cloud's rounded underside (ADR-706), which is the same
        // `tornadoSupportBelow` the field itself early-outs on.
        return vec3<f32>(max(funnel, max(skirt, cloud)) + lateral,
                         centre.y - radius * tornadoSupportBelow(mediumTornadoUniforms(s)),
                         centre.y + radius * 1.08);
    }

    if (mediumKind(s) != kMediumKindFog) {
        return vec3<f32>(radius * breath * 1.35,
                         centre.y - depth - thickness * 3.0,
                         centre.y + thickness * 3.0);
    }

    let f = mediumFogUniforms(s);
    let shape = fogShapeKind(f);
    let along = max(mediaLane(s, 13u).y, 0.05);
    // The sphere is the one primitive that ignores `bankLength`, so it is the one whose bound
    // must not carry it.
    var reach = max(along, 1.0);
    if (shape == kFogShapeSphere) {
        reach = 1.0;
    }
    let rr = radius * breath * reach * 1.35;

    if (shape == kFogShapeBank) {
        // Solve `exp(-h * falloff) = 0.01`: 1% of the column left outside, which is below what a
        // frame can show. The floor keeps a steep bank at the old three thicknesses; the ceiling
        // is where a bound this generous stops being worth the samples.
        let falloff = max(mediaLane(s, 14u).z, 0.01);
        let bias = clamp(mediaLane(s, 14u).y, 0.0, 1.0);
        let base = -thickness + 2.0 * thickness * bias;
        let hTop = clamp(4.6 / falloff, 3.0, 40.0);
        return vec3<f32>(rr,
                         centre.y + base - depth - thickness * 1.5,
                         centre.y + base + thickness * hTop);
    }
    // A closed primitive ends where its own surface ends, and the height influence can only make
    // it thinner.
    var half = thickness;
    if (shape == kFogShapeSphere) {
        half = radius;
    }
    return vec3<f32>(rr, centre.y - depth - half * 1.35, centre.y + half * 1.35);
}

// ADR-562 §4: the per-slot ray interval, as a VERTICAL CYLINDER.
//
// This is the highest-value affordance in the foundation and it is why the slot array had to come
// before the fog authoring work. ADR-560 measured that shrinking a medium's screen footprint moved
// `volume.march` only 7.14 -> 6.62 ms, because every pixel still marched all 32 steps across the
// full 4 km and the only saving was the per-sample early-out inside the field. Smaller on screen
// did not mean fewer steps. `agent/tornado` measured the same thing from the other side: a fixed
// 128-step march put 31 m between samples and broke a 24 m column into disconnected pieces.
//
// A CYLINDER rather than a sphere because every term in this family's field is a function of
// `length(rel.xz)` and `rel.y` alone -- so a cylinder is the field's own shape, and for the two
// cases that matter it is dramatically tighter: a wide flat fog bank and a tall thin tornado are
// both mostly empty inside their bounding spheres.
//
// Returns (tEnter, tExit); tExit <= tEnter means the ray misses this medium entirely.
fn mediumInterval(s: u32, origin: vec3<f32>, dir: vec3<f32>, maxDistance: f32) -> vec2<f32> {
    let l0 = mediaLane(s, 0u);
    let radius = l0.w;
    if (radius <= 0.0) {
        return vec2<f32>(1.0, -1.0);
    }
    let bound = mediumBoundOf(s);
    let rr = bound.x;
    let yBot = bound.y;
    let yTop = bound.z;
    let centre = l0.xyz;

    // Infinite cylinder about +Y, then clipped by the two caps.
    let d = vec2<f32>(dir.x, dir.z);
    let o = vec2<f32>(origin.x - centre.x, origin.z - centre.z);
    let a = dot(d, d);
    var t0 = 0.0;
    var t1 = maxDistance;
    if (a < 1e-12) {
        // Ray is vertical: inside the cylinder for its whole length, or never.
        if (dot(o, o) > rr * rr) {
            return vec2<f32>(1.0, -1.0);
        }
    } else {
        let b = dot(o, d);
        let c = dot(o, o) - rr * rr;
        let disc = b * b - a * c;
        if (disc < 0.0) {
            return vec2<f32>(1.0, -1.0);
        }
        let sq = sqrt(disc);
        t0 = max(t0, (-b - sq) / a);
        t1 = min(t1, (-b + sq) / a);
    }
    // The caps.
    if (abs(dir.y) < 1e-9) {
        if (origin.y < yBot || origin.y > yTop) {
            return vec2<f32>(1.0, -1.0);
        }
    } else {
        let ta = (yBot - origin.y) / dir.y;
        let tb = (yTop - origin.y) / dir.y;
        t0 = max(t0, min(ta, tb));
        t1 = min(t1, max(ta, tb));
    }
    return vec2<f32>(max(t0, 0.0), min(t1, maxDistance));
}

fn volumeDensityAt(p: vec3<f32>) -> f32 {
    // ADR-567: the SAME function the surface fog integrates (`height_fog.wgsl`, reached through
    // this file's include of common.wgsl). It used to be this expression written out here and the
    // antiderivative written out in common.wgsl -- two statements of one model, in two files, with
    // nothing asserting they were a function and its integral.
    var heightTerm = fogHeightProfile(p.y - vol.params0.y, vol.params0.z,
                                      vol.heightFog.x, vol.heightFog.y);
    // ADR-715: the layer's top follows the ground. The texture and its placement are the frame's
    // (group 0, common.wgsl) -- the same two the surface fog reads, so the two readers cannot be
    // handed different terrains. At follow 0 the line above is the whole of it, bit for bit.
    if (vol.heightFog.z > 0.0 && frame.terrainMap1.w > 0.5) {
        heightTerm = fogGroundProfileAt(terrainHeightTex, frame.terrainMap0, frame.terrainMap1, p, vol.params0.y,
                                        vol.heightFog.z, vol.params0.z, vol.heightFog.x, vol.heightFog.y);
    }
    var base = vol.params0.x * heightTerm;
    let densitySlot = i32(vol.info.y);
    if (densitySlot >= 0) {
        base = base * max(0.0, fieldScalar(densitySlot, p));
    }
    // The noise is the expensive term (a 3-octave fBM): only evaluate it where there is fog.
    if (base <= 1e-6 || vol.noiseParams.x == 0.0) {
        return max(0.0, base);
    }
    let offset = vec3<f32>(vol.noiseParams.z * vol.noiseParams.w);
    let noiseTerm = 1.0 + vol.noiseParams.x * (fbm3(p * vol.noiseParams.y + offset, 17u) * 2.0 - 1.0);
    return max(0.0, base * noiseTerm);
}

// The fog and the vortex are one medium as far as the march is concerned: one density, one
// transmittance. Keeping them separate would mean two marches or a composite, and a composite of
// two participating media is wrong wherever they overlap.
fn volumeTotalDensityAt(p: vec3<f32>, t: f32) -> f32 {
    var total = volumeDensityAt(p);
    let count = u32(vol.mediaInfo.x);
    for (var s = 0u; s < count; s = s + 1u) {
        total = total + mediumShape(s, p, t) * mediumDensityCoeff(s);
    }
    return total;
}

// Henyey-Greenstein phase function; g = 0 is isotropic.
fn henyeyGreenstein(cosTheta: f32, g: f32) -> f32 {
    let g2 = g * g;
    let d = max(1.0 + g2 - 2.0 * g * cosTheta, 1e-4);
    return (1.0 - g2) / (4.0 * PI * d * sqrt(d));
}

// In-scattered radiance at `p` for a ray travelling along `direction`.
//
// Every enabled light contributes in proportion to its `volumetricStrength` (packed into
// `cone.z` by rendering::SceneRenderer; 0 means "this light does not light the air"), so a rig
// can put a warm practical in the haze without the key washing the whole volume out. Lights with
// zero strength cost one comparison. There is no shadowing in the fog: a beam is the falloff of a
// local emitter, not an occluded shaft.
fn lightRadiance(index: i32, p: vec3<f32>) -> vec4<f32> {
    let light = frame.lights[index];
    let kind = light.positionType.w;
    if (kind < 0.5) { // directional
        return vec4<f32>(light.colorIntensity.rgb, 0.0);
    }
    let toLight = light.positionType.xyz - p;
    let dist2 = max(dot(toLight, toLight), 1e-4);
    var attenuation = 1.0 / dist2;
    let range = light.directionRange.w;
    if (range > 0.0) {
        let ratio = clamp(1.0 - pow(sqrt(dist2) / range, 4.0), 0.0, 1.0);
        attenuation = attenuation * ratio * ratio;
    }
    if (kind > 1.5) { // spot
        let towards = toLight * inverseSqrt(dist2);
        let cosAngle = dot(light.directionRange.xyz, -towards);
        attenuation = attenuation * clamp((cosAngle - light.cone.x) * light.cone.y, 0.0, 1.0);
    }
    return vec4<f32>(light.colorIntensity.rgb * attenuation, 1.0);
}

fn lightTowards(index: i32, p: vec3<f32>) -> vec3<f32> {
    let light = frame.lights[index];
    if (light.positionType.w < 0.5) {
        return -light.directionRange.xyz;
    }
    let toLight = light.positionType.xyz - p;
    return toLight * inverseSqrt(max(dot(toLight, toLight), 1e-4));
}

// ADR-570 (the brief's §20 and §22): the SHARED self-shadow march.
//
// **What it is.** At a sample `p`, march a short secondary ray toward a light through the placed
// media's own density and attenuate that light's in-scatter by the transmittance. That is the one
// missing term between "fog that is lit" and "fog that is lit FROM A DIRECTION": without it a bank
// is equally bright on the side facing the sun and the side away from it, which is why ADR-358
// refused to build a volumetric beam without shadow sampling and why §20's "backlit fog" and
// "dark moody fog" were not reachable by any setting of the existing controls.
//
// **And it is §22's answer without a second system.** A shaft is what you see when the air behind
// an occluder is dark and the air beside it is not. The occluder here is the medium itself, so a
// spotlight aimed through a fog bank produces a beam out of the same density the march already
// integrates -- "light -> fog -> scattering -> visible beam" from one field, which is what §22 asks
// for and what it warns against faking with a 2D radial blur.
//
// **Why it lives HERE and dispatches through `mediumShape`.** `agent/tornado` needs the same term
// and must not write a second one. This marches whatever each slot's kind says its density is, so
// a kind added tomorrow self-shadows correctly without touching this function -- the same property
// ADR-562 gave the primary march, and ADR-562 §9 is the record of what happens when a shared
// reader is left assuming one kind's layout.
//
// **The cost, stated because it is the reason this is off by default.** ADR-562 §8 measured that
// the field evaluation IS the cost of this pass. This multiplies the field evaluations per march
// step by (lights that light the air) x (slots the shadow ray crosses) x `steps`. What keeps it
// from being catastrophic is that the per-slot interval (ADR-562 §4) culls the shadow ray the same
// way it culls the primary one: a ray toward the sun from a point nowhere near a medium costs four
// analytic interval tests and evaluates no field at all. So the cost concentrates where the media
// actually are, which is where it should be. It is still a multiple, and `volumeShadowSteps`
// defaults to 0, which returns 1.0 from the first branch and leaves every existing frame
// bit-identical.
fn mediumSelfShadow(p: vec3<f32>, towards: vec3<f32>, t: f32) -> f32 {
    let steps = i32(vol.selfShadow.x);
    let strength = vol.selfShadow.y;
    if (steps <= 0 || strength <= 0.0) {
        return 1.0;
    }
    let mediumCount = u32(vol.mediaInfo.x);
    var tau = 0.0;
    for (var s = 0u; s < mediumCount; s = s + 1u) {
        // The SAME bound the primary march clips to (ADR-566), so the shadow ray cannot miss part
        // of a medium the camera ray can see -- and so an ADR-566-style defect could not be
        // introduced here separately: there is one bound and this is a second reader of it.
        let iv = mediumInterval(s, p, towards, 1.0e7);
        if (iv.y <= iv.x) {
            continue;
        }
        let dt = (iv.y - iv.x) / f32(steps);
        // Through the KIND-AWARE accessor, not `mediaLane(s, 1u).w`. That direct read is what this
        // line said when ADR-570 wrote it on `agent/fog`, where the only kinds were the vortex and
        // the fog bank and they share a lane layout. `agent/tornado` brought a kind that does not:
        // lane 1 is its radius curve, so an unconditional read hands the shadow march
        // `radiusMidControl` -- a number in the tens -- as an extinction in the hundredths, and a
        // tornado would swallow every light that crossed it. The merge created that defect by
        // putting a new shared reader and a new kind in the same tree; `mediumDensityCoeff` is the
        // accessor that already existed for exactly this and this is now its third call site.
        let extinctionPerShape = mediumDensityCoeff(s);
        for (var k = 0; k < steps; k = k + 1) {
            let x = p + towards * (iv.x + (f32(k) + 0.5) * dt);
            let shape = mediumShape(s, x, t);
            if (shape > 0.0) {
                tau = tau + shape * extinctionPerShape * dt;
            }
            // NO EARLY-OUT HERE, and that is a measurement rather than an oversight. The primary
            // march breaks at `transmittance < 0.002` and the same test was written into this
            // loop, expecting the same win. Measured interleaved at 4 steps with one volumetric
            // light, three repeats: +1.57, +0.85, -0.78 ms. **Mixed sign, so no effect is
            // established** (docs/testing.md 19's discard rule: discard on sign disagreement,
            // never on spread), and it was removed rather than kept on the argument that it must
            // help. At four steps the loop can save at most three evaluations and only deep
            // inside a thick medium, which is probably why. If `volumeShadowSteps`' cap of 16 is
            // ever raised, measure it again before assuming the answer is the same.
        }
    }
    // `params1.x` is the absorption the primary march turns density into extinction with, so the
    // shadow ray and the camera ray agree about how opaque the medium is. `strength` is the
    // artist's dial on top: 1 is physical, less is a softer bank that light reaches further into,
    // and more is a bank that swallows light -- which is a look, not an error.
    return exp(-tau * vol.params1.x * strength);
}

fn inScatterAt(p: vec3<f32>, direction: vec3<f32>, anisotropy: f32) -> vec3<f32> {
    var total = vec3<f32>(0.0);
    let count = min(i32(frame.envParams.z), 8);
    for (var i = 0; i < count; i = i + 1) {
        let strength = frame.lights[i].cone.z;
        if (strength <= 0.0) {
            continue;
        }
        let towards = lightTowards(i, p);
        let phase = henyeyGreenstein(dot(direction, towards), anisotropy);
        // ADR-570: and how much of this light actually reaches `p` through the media. One shadow
        // march per light that lights the air; a light with `volumetricStrength` 0 skipped above
        // costs nothing here either, which is what makes a rig with one volumetric key affordable.
        let shadow = mediumSelfShadow(p, towards, vol.noiseParams.w);
        total = total + strength * phase * shadow * lightRadiance(i, p).rgb;
    }
    return total;
}

// ADR-053: the clustered local lights, so the air near a glowing thing takes its colour. The
// march reads the same froxel list the surface shading reads (group 0 bindings 1 and 2 of the
// shared frame layout); the structs are declared here rather than by including lighting.wgsl,
// which would drag the shadow atlas, the LTC tables and the whole BRDF into a pass that needs
// none of them.
struct VolumeGpuLight {
    positionType: vec4<f32>,
    directionRange: vec4<f32>,
    colorIntensity: vec4<f32>,
    cone: vec4<f32>,
    sizeSoft: vec4<f32>,
    up: vec4<f32>,
    tangent: vec4<f32>,
    extra: vec4<f32>,
};
@group(0) @binding(1) var<storage, read> volumeLights: array<VolumeGpuLight>;
@group(0) @binding(2) var<storage, read> volumeClusters: array<u32>;
const VOLUME_MAX_PER_CLUSTER: u32 = 32u;
// The march samples at most this many of a froxel's lights. Fog has no detail to resolve: what it
// needs is the colour and rough strength of the light in the air, and taking every one of 32
// candidates at every step of every half-res pixel cost more than the whole ecology light field
// did on the surfaces it actually lit.
const VOLUME_LIGHT_SAMPLES: u32 = 6u;

fn volumeClusterIndex(screenUv: vec2<f32>, viewDepth: f32) -> u32 {
    let dims = vec3<u32>(u32(frame.clusterParams.x), u32(frame.clusterParams.y), u32(frame.clusterParams.z));
    let ix = min(u32(clamp(screenUv.x, 0.0, 0.9999) * f32(dims.x)), dims.x - 1u);
    let iy = min(u32(clamp(1.0 - screenUv.y, 0.0, 0.9999) * f32(dims.y)), dims.y - 1u);
    let depth = max(viewDepth, frame.clusterDepth.z);
    let slice = i32(floor(log2(depth) * frame.clusterDepth.x + frame.clusterDepth.y));
    let iz = u32(clamp(slice, 0, i32(dims.z) - 1));
    return (iz * dims.y + iy) * dims.x + ix;
}

// The local lights of one froxel, in-scattered. Point-like only: an area emitter contributes
// through its centre here, because the difference between a disk and a point is not visible in
// fog and the LTC integration is not affordable per march step.
fn localInScatterAt(p: vec3<f32>, screenUv: vec2<f32>, viewDepth: f32, direction: vec3<f32>,
                    anisotropy: f32) -> vec3<f32> {
    let gain = vol.glow.y;
    if (frame.clusterParams.w <= 0.5 || gain <= 0.0) {
        return vec3<f32>(0.0);
    }
    let directional = u32(frame.lightCounts.x + 0.5);
    let totalLights = u32(frame.lightCounts.y + 0.5);
    let cluster = volumeClusterIndex(screenUv, viewDepth);
    let clusterCount = u32(frame.clusterParams.x * frame.clusterParams.y * frame.clusterParams.z);
    let count = min(min(volumeClusters[cluster], VOLUME_MAX_PER_CLUSTER), VOLUME_LIGHT_SAMPLES);
    let base = clusterCount + cluster * VOLUME_MAX_PER_CLUSTER;
    var sum = vec3<f32>(0.0);
    for (var i = 0u; i < count; i = i + 1u) {
        let index = directional + volumeClusters[base + i];
        if (index >= totalLights) { continue; }
        let light = volumeLights[index];
        let toLight = light.positionType.xyz - p;
        let d2 = max(dot(toLight, toLight), 1e-4);
        let range = light.directionRange.w;
        if (range > 0.0 && d2 > range * range) { continue; }
        let dist = sqrt(d2);
        let towards = toLight / dist;
        // Inverse square with the same smooth window the surface shading uses, so a light does
        // not end at a hard edge in the fog where it faded out on the ground.
        var attenuation = 1.0 / d2;
        if (range > 0.0) {
            let t = clamp(1.0 - (dist / range) * (dist / range) * (dist / range) * (dist / range), 0.0, 1.0);
            attenuation = attenuation * t * t;
        }
        let phase = henyeyGreenstein(dot(direction, towards), anisotropy);
        // ADR-570 deliberately does NOT self-shadow the clustered local lights, and the reason is
        // arithmetic rather than principle: this loop runs over a froxel's whole light list, so a
        // shadow march here is one per light per sample with no `volumetricStrength` gate to thin
        // it. The eight frame lights are gated and few; a froxel's list is neither. If a scene
        // wants a shadowed practical it should be one of the eight. Revisit with a measurement.
        sum = sum + light.colorIntensity.rgb * (attenuation * phase);
    }
    return sum * gain;
}

// ADR-040: emissive particles light the dust around them. Each system is reduced on the GPU to
// one sphere - the emission-weighted centroid of its alive particles, the standard deviation of
// their positions, the mean colour and the total power - and the march treats it as a soft
// luminous ball whose radiance falls off as a Gaussian at the cloud's own spread. The coupling is
// one-directional and costs one Gaussian per step per system: the volume never touches the
// simulation, so determinism is untouched.
// One particle at full opacity and unit emissive intensity is treated as a point emitter of this
// radiant intensity. Small on purpose: a spark is a millimetre of hot metal, and a burst of ten
// thousand of them should read as a glow in the dust, not as a second sun.
const kParticleGlowIntensity: f32 = 0.0015;

fn particleGlowAt(p: vec3<f32>) -> vec3<f32> {
    let count = u32(vol.glow.x);
    var sum = vec3<f32>(0.0);
    for (var i = 0u; i < count; i = i + 1u) {
        let centre = particleGlow[i * 2u];
        let color = particleGlow[i * 2u + 1u];
        if (color.w <= 0.0) { continue; }
        let radius = max(centre.w, 0.25);
        let d = p - centre.xyz;
        let falloff = exp(-dot(d, d) / (2.0 * radius * radius));
        // Power is the summed emissive weight of the alive particles; spreading it over the
        // cloud's surface keeps a wide cloud from being as bright as a tight one.
        sum += color.rgb * (color.w * kParticleGlowIntensity * falloff / (4.0 * PI * radius * radius));
    }
    return sum;
}

@fragment
fn fs_volume(in: FsIn) -> @location(0) vec4<f32> {
    let marchPx = vec2<i32>(floor(in.pos.xy));
    // The full-resolution texel this march pixel marches (the composite pass uses the same rule
    // to decide which neighbour saw which surface). `ratio` is 2 at the default half-resolution
    // scale, where this is exactly the `marchPx * 2` it has always been, and 1 at scale 1.0,
    // where every march pixel is its own full-res pixel.
    let ratio = vol.sizes.zw / vol.sizes.xy;
    let mapped = vec2<i32>(floor(vec2<f32>(marchPx) * ratio));
    let fullPx = vec2<i32>(min(mapped.x, i32(vol.sizes.z) - 1), min(mapped.y, i32(vol.sizes.w) - 1));
    let ndc = ndcOf((vec2<f32>(fullPx) + vec2<f32>(0.5)) / vol.sizes.zw);
    let origin = worldAt(ndc, 0.0);
    let direction = viewRayDirection(ndc);

    let sceneZ = textureLoad(sceneDepth, fullPx, 0);
    var maxDistance = vol.params1.w;
    if (sceneZ < 1.0) {
        maxDistance = min(maxDistance, distance(worldAt(ndc, sceneZ), origin));
    }
    let steps = max(i32(vol.info.x), 1);
    let stepLength = maxDistance / f32(steps);
    if (stepLength <= 0.0) {
        return vec4<f32>(0.0, 0.0, 0.0, 1.0);
    }
    let jitter = stepJitter(marchPx, u32(vol.info.w));
    let screenUv = (vec2<f32>(fullPx) + vec2<f32>(0.5)) / vol.sizes.zw;
    // Froxel depth is measured along the camera axis, not along this pixel's ray, or a sample at
    // the edge of a wide frame lands a slice or two deep and reads the wrong light list.
    let depthAlongRay = dot(direction, frame.cameraForward.xyz);
    let anisotropy = clamp(vol.params1.y, -0.95, 0.95);
    let colorSlot = i32(vol.info.z);

    // ADR-562 §4: each slot's ray interval, computed ONCE per pixel rather than per step.
    //
    // The march's step positions are deliberately unchanged -- the same `steps` over the same
    // `maxDistance`, so the Environment fog integrates exactly as it did and no existing frame
    // moves. What the intervals buy is that a step outside a medium's cylinder costs a comparison
    // instead of a field evaluation, and ADR-374 measured that the cost IS how many samples have
    // non-zero density.
    //
    // Redistributing the steps into the union of the intervals is the bigger win and is NOT done
    // here: it would change the fog's own integration and every existing frame with it, so it is a
    // separate measurement with its own arm. `agent/tornado`'s per-medium step derivation (rope 462
    // steps, wedge 96, against a fixed 128 that broke a 24 m column into pieces) is the evidence
    // that it is worth doing next.
    let mediumCount = u32(vol.mediaInfo.x);
    var slotMin: array<f32, 4>;
    var slotMax: array<f32, 4>;
    for (var s = 0u; s < 4u; s = s + 1u) {
        if (s < mediumCount) {
            let iv = mediumInterval(s, origin, direction, maxDistance);
            slotMin[s] = iv.x;
            slotMax[s] = iv.y;
        } else {
            slotMin[s] = 1.0;
            slotMax[s] = -1.0;
        }
    }

    var transmittance = 1.0;
    var scattered = vec3<f32>(0.0);
    for (var i = 0; i < steps; i = i + 1) {
        let t = (f32(i) + jitter) * stepLength;
        let p = origin + direction * t;
        // ADR-371/562: every placed medium is part of ONE medium as far as the march is
        // concerned -- one density, one transmittance, one early-out. Keeping them separate would
        // mean N marches or a composite, and a composite of two participating media is wrong
        // wherever they overlap.
        //
        // ADR-562 §4: a slot whose interval this step is outside costs one comparison instead of a
        // field evaluation. That is the difference between ADR-560's measured 7% saving from a
        // smaller medium and a proportional one.
        let fogDensity = volumeDensityAt(p);
        var mediumDensity = 0.0;
        var mediumScatter = 0.0;
        var mediumEmission = vec3<f32>(0.0);
        var cometLit = 0.0;
        for (var s = 0u; s < mediumCount; s = s + 1u) {
            if (t < slotMin[s] || t > slotMax[s]) {
                continue;
            }
            let shape = mediumShape(s, p, vol.noiseParams.w);
            if (shape <= 0.0) {
                continue;
            }
            let l5 = mediaLane(s, 5u);
            let d = shape * mediumDensityCoeff(s);
            mediumDensity = mediumDensity + d;
            // Per-slot scene-light scattering weight (ADR-388), so a dark forward-scattering
            // tornado and a self-luminous nebula can stand in one frame without sharing a knob.
            mediumScatter = mediumScatter + d * mediumScatterWeight(s);
            mediumEmission = mediumEmission + mediumEmissionAt(s, p, shape, vol.noiseParams.w);
            cometLit = cometLit + shape * mediumCometResponse(s);
        }
        let density = fogDensity + mediumDensity;
        if (density <= 0.0) {
            continue;
        }
        let extinction = density * vol.params1.x;
        // ADR-371: the FOG scatters the scene's lights; the VORTEX does not. This is the single
        // most important line in the effect, and the first version did not have it. A nebula four
        // hundred metres below an island is not lit by that island's key light, and letting it be
        // turned the frame into an even wash: at density 0.0015 with the key at intensity 22, a
        // 2.6 km march accumulated so much in-scattered key light that the picture came back mean
        // luminance 131 of 255 with the vortex's own emission set to ZERO. That is precisely the
        // flat haze ADR-358 predicted when it refused to build a volumetric beam without shadow
        // sampling -- the same defect, arrived at from the other direction.
        //
        // So the vortex contributes extinction and emission and nothing else. It is self-luminous,
        // which is both what a nebula is and what keeps it out of the scene's lighting entirely.
        //
        // ADR-388 adds the controlled way back in, and leaves the refusal above standing because it
        // is still the right default. `vortex5.z` is 0 unless a scene asks otherwise, so every
        // frame rendered before this line changed renders identically after it. What it is FOR is
        // one thing: an upward spotlight aimed through the funnel lights the surfaces it reaches
        // and the ordinary fog, and its beam stops dead at the funnel's edge, because the medium
        // the beam is supposed to be visible in does not scatter. Above 0 it does.
        //
        // The 131-of-255 above was measured on the PRE-FUNNEL slab; against the shape ADR-374 left
        // -- a throat, a void and a rim -- full scattering is worth +15 luminance levels on the
        // shipped frame, not a wash. The refusal is still the right default. The number is no
        // longer what this knob does, and it is left above because it is why the knob starts at 0.
        let scattering = (fogDensity + mediumScatter) * vol.params0.w;
        var emission = vec3<f32>(0.0);
        if (vol.params1.z > 0.0) {
            var emissionColor = vol.fogColor.rgb;
            if (colorSlot >= 0) {
                let c = fieldColor(colorSlot, p);
                emissionColor = c.rgb * c.a;
            }
            emission = density * vol.params1.z * emissionColor;
        }
        // ...and the vortex brings its own light. Emissive rather than lit, because nothing in this
        // scene could illuminate something that size and because the reference is a nebula.
        emission = emission + mediumEmission;
        // ADR-381, phase 13: the COMET, and only the comet, is allowed to light the vortex.
        //
        // ADR-374 established that letting the scene's lights scatter in this medium turns the
        // frame into flat haze -- the key at intensity 22 accumulated over a 2.6 km march returned
        // mean luminance 131 of 255 with the vortex's own emission at zero. So this is not a
        // relaxation of that: it is one small, moving, distance-limited source, taken from the same
        // `skyGroundPoint` description the SURFACES are lit from, so the rock and the fog brighten
        // from one account of where the comet is rather than two that can disagree.
        //
        // Weighted by the vortex's own density, so it reads as the comet finding the cloud rather
        // than as a light in empty space, and gated by a parameter that is zero by default.
        if (cometLit > 0.0) {
            let lit = frame.skyGroundPointColor;
            if (lit.r + lit.g + lit.b > 0.0) {
                // XZ ONLY, and a wider reach than the surfaces get. `atmosphere_ground.wgsl` says
                // why for the first: the comet is kilometres up, so the distance that matters is
                // how far a point is from the spot *under* it, not from the spot itself. My first
                // version used the 3D distance and measured nothing at all -- the ground pool's
                // 220 m radius is the right size for the island's rock and the vortex is hundreds
                // of metres below the ground plane, so every sample fell outside a sphere of it.
                //
                // And the second: the fog is kilometres across where the pool is metres, so the
                // radius is scaled by `vortex5.y`. A light that reaches the rock and stops dead at
                // the cloud beside it is the discontinuity ADR-369 was about, one object along.
                let d = length(p.xz - frame.skyGroundPoint.xz) /
                        max(frame.skyGroundPoint.w * max(mediaLane(0u, 5u).y, 1.0), 1.0);
                let fall = pow(clamp(1.0 - d, 0.0, 1.0), max(lit.w, 0.5));
                // ADR-389 is the fourth member of this family and the first that is not about
                // metres: replacing the vortex's contrast curve changed the MEAN of its shape by
                // six times, and `density` and `emission` were calibrated against the old one. The
                // general form, for whoever meets the fifth: a coefficient tuned against a quantity
                // is invalidated by any change to that quantity's DISTRIBUTION, not only by a
                // change to its units.
                //
                // The 0.005 is the per-metre conversion, and it is the THIRD time in this branch
                // that adding a radiance to a march without it has produced a number two orders of
                // magnitude wrong (ADR-374's density, ADR-379's spill). `lit.rgb` is a surface
                // radiance; this loop integrates over metres. Without it, cometResponse 0.6 lifted
                // the whole frame by 125 luminance levels.
                emission = emission + lit.rgb * (fall * cometLit * 0.005);
            }
        }
        // The particle glow arrives as light to scatter, not as fog emission, so denser dust
        // catches more of it - which is what reads as "the sparks are lighting the dust".
        let local = localInScatterAt(p, screenUv, max(t * depthAlongRay, 1e-3), direction, anisotropy);
        let source = scattering * (inScatterAt(p, direction, anisotropy) + particleGlowAt(p) + local) + emission;
        scattered = scattered + transmittance * source * stepLength;
        transmittance = transmittance * exp(-extinction * stepLength);
        if (transmittance < 0.002) {
            break;
        }
    }
    return vec4<f32>(scattered, transmittance);
}

@fragment
fn fs_composite(in: FsIn) -> @location(0) vec4<f32> {
    let fullPx = vec2<i32>(floor(in.pos.xy));
    let myDepth = linearDepth(textureLoad(sceneDepth, fullPx, 0));
    // Bilinear footprint in march-texel space. `invRatio` is 0.5 at the default half-resolution
    // scale and 1.0 when the march ran per pixel -- and at 1.0 `coord` lands exactly on an
    // integer, so `f` is zero, the (0,0) neighbour takes the whole weight and the filter is a
    // copy of the pixel's own march rather than a blur of four.
    let ratio = vol.sizes.zw / vol.sizes.xy;
    let invRatio = vol.sizes.xy / vol.sizes.zw;
    let coord = (vec2<f32>(fullPx) + vec2<f32>(0.5)) * invRatio - vec2<f32>(0.5);
    let base = floor(coord);
    let f = coord - base;
    let bx = i32(base.x);
    let by = i32(base.y);
    let maxX = i32(vol.sizes.x) - 1;
    let maxY = i32(vol.sizes.y) - 1;
    let fullMaxX = i32(vol.sizes.z) - 1;
    let fullMaxY = i32(vol.sizes.w) - 1;

    var accum = vec4<f32>(0.0);
    var weightSum = 0.0;
    for (var j = 0; j < 2; j = j + 1) {
        for (var i = 0; i < 2; i = i + 1) {
            let hx = clamp(bx + i, 0, maxX);
            let hy = clamp(by + j, 0, maxY);
            let bilinear = (f32(1 - i) + f32(2 * i - 1) * f.x) * (f32(1 - j) + f32(2 * j - 1) * f.y);
            let mapped = vec2<i32>(floor(vec2<f32>(f32(hx), f32(hy)) * ratio));
            let sourcePx = vec2<i32>(min(mapped.x, fullMaxX), min(mapped.y, fullMaxY));
            let theirDepth = linearDepth(textureLoad(sceneDepth, sourcePx, 0));
            let w = bilinear / (1.0 + 8.0 * abs(theirDepth - myDepth) / max(myDepth, 1e-3));
            accum = accum + textureLoad(volumeTex, vec2<i32>(hx, hy), 0) * w;
            weightSum = weightSum + w;
        }
    }
    if (weightSum <= 1e-6) {
        return vec4<f32>(0.0, 0.0, 0.0, 1.0);
    }
    let result = accum / weightSum;
    return vec4<f32>(result.rgb, clamp(result.a, 0.0, 1.0));
}
