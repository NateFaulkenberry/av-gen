// SDF raymarch pass (ADR-027): one draw per Raymarch-mode SDF object. The vertex stage emits a
// screen-space quad over the object's projected bounds (sdf.rect, NDC; the full screen when the
// camera is inside the bounds); the fragment stage casts the camera ray through the pixel,
// clips it to the object's local AABB (slab test), sphere-traces the packed node program from
// sdf.wgsl with relaxation `stepScale` and the perspective hit threshold d < epsilon * t, then
// shades the hit with the shared PBR fragment (pbr_shade.wgsl: lights, IBL, emissive, fog) and
// writes the hit's clip depth to frag_depth so meshes, procedural instances and particles compose
// with it in both directions. A miss discards.
//
// Marching happens in the object's local space (ray origin/direction through worldToLocal, the
// direction re-normalised; t is in local units, so the threshold and the step scale are
// invariant to the object's uniform scale). The march starts at the later of the AABB entry and
// the camera's near plane, so hits between the eye and the near plane are not produced.
//
// Bind groups: 0 frame (common.wgsl); 1 = {0 ObjectUniforms (dynamic offset: model = local ->
// world, normalMatrix, the material lanes shadePbr reads), 1 SdfObjectUniforms (dynamic offset),
// 2 array<SdfNodeGpu> (read-only storage, every object's records concatenated), 3 FieldBlock,
// 4 step statistics, 5 + 6 the ADR-1142 density volume and its linear sampler};
// 2 material, 3 IBL (declared by pbr_shade.wgsl). Mirrors rendering/sdf_renderer.hpp.
#include "common.wgsl"
// ADR-138: whether this module's draws are the procedural scatter, which is the share tier
// assignment could actually reach on Glowmere (authored entities are 77% of coverage and the
// terrain can never be demoted). `pbr_shade.wgsl` reads it to pick which tier applies. Every
// includer must define it -- a missing one is a compile error rather than a silent wrong tier.
const kProceduralDraw: bool = false;
// ADR-155: this draw's rung tier, or 0 (Full) for a path that has no rungs. Defined by
// every includer of pbr_shade.wgsl, so a missing one is a compile error.
fn proceduralRungTier() -> f32 { return 0.0; }
// ADR-1152: the engraving's record for pbr_shade.wgsl. Only the engraved variant (kSdfEngraved, set between
// the @@SDF_SURFACE@@ markers) fills it; in every other module the branch is removed at compile time.
var<private> sdfDetail: SurfaceDetail;
fn surfaceDetail() -> SurfaceDetail {
    if (kSdfEngraved) {
        return sdfDetail;
    }
    return SurfaceDetail(vec3<f32>(0.0), 0.0, vec3<f32>(0.0), 0.0, 0.0);
}
#include "pbr_shade.wgsl"
#include "sdf.wgsl"

struct SdfObjectUniforms {
    worldToLocal: mat4x4<f32>,
    boundsMin: vec4<f32>,   // xyz = local AABB min
    boundsMax: vec4<f32>,   // xyz = local AABB max
    info: vec4<u32>,        // x = node offset, y = node count, z = max steps, w = shadow steps
    march: vec4<f32>,       // x = epsilon, y = step scale, z = normal epsilon, w = time
    rect: vec4<f32>,        // NDC rect of the projected bounds: xmin, ymin, xmax, ymax
    // ADR-1002 (scene::SdfLook, the march cap)
    look0: vec4<f32>,       // ao strength, ao distance, edge intensity, edge width
    look1: vec4<f32>,       // edge colour rgb, max distance (0 = the bounds only)
    look2: vec4<f32>,       // shadow strength, shadow softness k, shadow steps, 1 = collect step statistics
    look3: vec4<f32>,       // shadow direction (world, towards the light)
    surfaces: vec4<u32>,    // ADR-1044: x = surface records after the node offset, y = their count
    look4: vec4<f32>,       // ADR-1047: edge width in pixels (0 = look0.w, local units), threshold, softness;
                            // ADR-1052: w = rim power
    look5: vec4<f32>,       // ADR-1052: rim colour rgb, rim intensity (0 = off)
    look6: vec4<f32>,       // ADR-1054: static cell (local units), rate (Hz), roll strength, 0
    // ADR-1055: the world wave: origin + progress, direction + width, colour + intensity, trail colour + trail,
    // (hue, hue span, edge tint, 1 = on).
    wave0: vec4<f32>,
    wave1: vec4<f32>,
    wave2: vec4<f32>,
    wave3: vec4<f32>,
    wave4: vec4<f32>,
    // ADR-1142: "density iso (+) SDF". density0.w = 1 draws the iso-surface of a particle system's
    // density volume (ADR-1141) sharpened toward this object's tree; 0 is the object as it always was.
    density0: vec4<f32>,    // iso, sharpness, one density cell in local units, 1 = density mode
    density1: vec4<f32>,    // the volume's world-space min corner, 0
    density2: vec4<f32>,    // 1 / the volume's world-space extent, 0
};

// ADR-1055: the world wave at a world point -- the band's colour, how much of the band is here, and how
// far behind the front the point is (0 ahead .. 1 passed).
struct SdfWaveSample {
    color: vec3<f32>,
    band: f32,
    behind: f32,
};

fn sdfWaveAt(worldPos: vec3<f32>) -> SdfWaveSample {
    var w: SdfWaveSample;
    let x = dot(worldPos - sdf.wave0.xyz, sdf.wave1.xyz);
    let f = sdf.wave0.w - x;            // metres behind the front (> 0 = already passed)
    let width = sdf.wave1.w;
    w.band = exp(-(f / width) * (f / width));
    w.behind = smoothstep(-width, width, f);
    if (sdf.wave4.y > 0.0) {
        let t = sdf.wave4.x + sdf.wave4.y * clamp(f / (2.0 * width), -1.0, 1.0);
        w.color = 0.5 + 0.5 * cos(6.28318531 * (vec3<f32>(t) + vec3<f32>(0.0, 0.33333, 0.66667)));
    } else {
        w.color = sdf.wave2.xyz;
    }
    return w;
}

// Recolour `base` toward the wave: inside the band to its colour (edge tint), behind it to the trail colour,
// keeping the base's luminance so lines keep their brightness and only change hue.
fn sdfWaveRecolor(base: vec3<f32>, w: SdfWaveSample) -> vec3<f32> {
    let lum = dot(base, vec3<f32>(0.2126, 0.7152, 0.0722));
    let bandC = w.color / max(dot(w.color, vec3<f32>(0.2126, 0.7152, 0.0722)), 1e-3) * lum;
    let trailC = sdf.wave3.xyz / max(dot(sdf.wave3.xyz, vec3<f32>(0.2126, 0.7152, 0.0722)), 1e-3) * lum;
    let tBand = clamp(w.band * sdf.wave4.z, 0.0, 1.0);
    let tTrail = clamp(w.behind * sdf.wave3.w, 0.0, 1.0) * (1.0 - tBand);
    return base * (1.0 - tBand - tTrail) + bandC * tBand + trailC * tTrail;
}

// ADR-1002: step statistics, accumulated by the lit pass on every 4th pixel in x and y.
// [0] sum of steps, [1] max steps, [2] sampled rays, [3] hits, [4] rays that ran out of steps.
struct SdfStepStats {
    counters: array<atomic<u32>, 8>,
};

@group(1) @binding(1) var<uniform> sdf: SdfObjectUniforms;
@group(1) @binding(2) var<storage, read> sdfNodes: array<SdfNodeGpu>;
@group(1) @binding(3) var<uniform> fieldBlock: FieldBlock;
@group(1) @binding(4) var<storage, read_write> sdfStepStats: SdfStepStats;
// ADR-1142: the density volume (a 1x1x1 zero placeholder for every object not in density mode).
@group(1) @binding(5) var sdfDensityTex: texture_3d<f32>;
@group(1) @binding(6) var sdfDensitySampler: sampler;
// ADR-1150: the volume's coarse max-occupancy grid (one texel per 8^3 block; the placeholder otherwise).
@group(1) @binding(7) var sdfDensityCoarse: texture_3d<f32>;

// ADR-1003: every field evaluation of this pass goes through sdfField. By default it is the packed
// interpreter; an object with `compile` on gets a pipeline whose module replaces the block between
// the two markers with the tree compiled to WGSL (spatial::sdfCompileWgsl), where `offset` addresses
// that object's per-node parameter table instead of its packed program.
// @@SDF_FIELD_BEGIN@@
fn sdfField(offset: u32, count: u32, p: vec3<f32>, t: f32, world: mat4x4<f32>) -> f32 {
    return sdfEvaluate(offset, count, p, t, world);
}
// ADR-1044: the surface id at p (a compiled tree replaces this; the interpreter shades surface 0).
fn sdfSurface(offset: u32, count: u32, p: vec3<f32>, t: f32, world: mat4x4<f32>) -> u32 {
    return 0u;
}
// @@SDF_FIELD_END@@

// ADR-1142: the density at a tree-local point (0 outside the volume, so the clamped edge texels do
// not smear into streaks out to the march's bounds).
fn sdfDensityAt(pLocal: vec3<f32>) -> f32 {
    let w = (object.model * vec4<f32>(pLocal, 1.0)).xyz;
    let uvw = (w - sdf.density1.xyz) * sdf.density2.xyz;
    if (any(uvw < vec3<f32>(0.0)) || any(uvw > vec3<f32>(1.0))) {
        return 0.0;
    }
    return textureSampleLevel(sdfDensityTex, sdfDensitySampler, uvw, 0.0).r;
}

// ADR-1142: the field of an object in density mode (after the prototype's approach E):
//     dRho = (iso - rho) * cell * 1.6
//     f    = sharpness > 0 && rho > 0.12 iso ? mix(dRho, max(sdf, (0.38 iso - rho) * cell * 3), sharpness) : dRho
// so with no matter there is no surface whatever the tree says, and the tree is evaluated only where
// matter already is. dRho is not a distance (it is a density difference scaled to about one cell per
// unit), so a density-mode object wants a step scale below 1 and a step budget that covers its bounds
// in steps of about iso * cell * 1.6 through empty space. There is exactly ONE call of sdfField here:
// a compiled field is inlined at every call site, and two would double its code at each of them.
fn sdfDensitySurfaceField(offset: u32, count: u32, p: vec3<f32>, t: f32, world: mat4x4<f32>) -> f32 {
    return sdfDensityFieldOf(offset, count, p, t, world, false);
}

// ADR-1150: the density as a cubic B-spline of the volume's texels (Sigg and Hadwiger 2005: eight linear
// fetches at offset positions). The trilinear density's gradient is constant within a texel and jumps at its
// faces, so a normal taken from it shows every texel as a facet -- the iteration-2 "blocky reflections". The
// B-spline is C2, so its gradient is continuous; it is read by the normal only (the march and its bisection
// stay on the trilinear field, which is what the depth prepass reaches too).
fn sdfDensitySmoothAt(pLocal: vec3<f32>) -> f32 {
    let w = (object.model * vec4<f32>(pLocal, 1.0)).xyz;
    let uvw = (w - sdf.density1.xyz) * sdf.density2.xyz;
    if (any(uvw < vec3<f32>(0.0)) || any(uvw > vec3<f32>(1.0))) {
        return 0.0;
    }
    let res = vec3<f32>(textureDimensions(sdfDensityTex, 0));
    let x = uvw * res - 0.5;
    let i = floor(x);
    let f = x - i;
    let f2 = f * f;
    let f3 = f2 * f;
    let w0 = (1.0 - 3.0 * f + 3.0 * f2 - f3) / 6.0;
    let w1 = (4.0 - 6.0 * f2 + 3.0 * f3) / 6.0;
    let w2 = (1.0 + 3.0 * f + 3.0 * f2 - 3.0 * f3) / 6.0;
    let w3 = f3 / 6.0;
    let g0 = w0 + w1;
    let g1 = w2 + w3;
    let h0 = (i - 0.5 + w1 / g0) / res; // texel centres at (i + 0.5) / res
    let h1 = (i + 1.5 + w3 / g1) / res;
    let s000 = textureSampleLevel(sdfDensityTex, sdfDensitySampler, vec3<f32>(h0.x, h0.y, h0.z), 0.0).r;
    let s100 = textureSampleLevel(sdfDensityTex, sdfDensitySampler, vec3<f32>(h1.x, h0.y, h0.z), 0.0).r;
    let s010 = textureSampleLevel(sdfDensityTex, sdfDensitySampler, vec3<f32>(h0.x, h1.y, h0.z), 0.0).r;
    let s110 = textureSampleLevel(sdfDensityTex, sdfDensitySampler, vec3<f32>(h1.x, h1.y, h0.z), 0.0).r;
    let s001 = textureSampleLevel(sdfDensityTex, sdfDensitySampler, vec3<f32>(h0.x, h0.y, h1.z), 0.0).r;
    let s101 = textureSampleLevel(sdfDensityTex, sdfDensitySampler, vec3<f32>(h1.x, h0.y, h1.z), 0.0).r;
    let s011 = textureSampleLevel(sdfDensityTex, sdfDensitySampler, vec3<f32>(h0.x, h1.y, h1.z), 0.0).r;
    let s111 = textureSampleLevel(sdfDensityTex, sdfDensitySampler, vec3<f32>(h1.x, h1.y, h1.z), 0.0).r;
    // g0 weighs the fetch at h0 and g1 = 1 - g0 the one at h1, on each axis
    let y0 = mix(mix(s110, s010, g0.x), mix(s100, s000, g0.x), g0.y);
    let y1 = mix(mix(s111, s011, g0.x), mix(s101, s001, g0.x), g0.y);
    return mix(y1, y0, g0.z);
}

// The density-mode field with the density read trilinearly (`smoothDensity` false: the march) or as the
// B-spline (true: the normal). One call of sdfField, for ADR-1142's reason.
fn sdfDensityFieldOf(offset: u32, count: u32, p: vec3<f32>, t: f32, world: mat4x4<f32>, smoothDensity: bool) -> f32 {
    var rho = 0.0;
    if (smoothDensity) {
        rho = sdfDensitySmoothAt(p);
    } else {
        rho = sdfDensityAt(p);
    }
    let iso = sdf.density0.x;
    let cell = sdf.density0.z;
    let dRho = (iso - rho) * cell * 1.6;
    if (!(sdf.density0.y > 0.0 && rho > 0.12 * iso)) {
        return dRho;
    }
    let tree = sdfField(offset, count, p, t, world);
    let dilated = (0.38 * iso - rho) * cell * 3.0;
    return mix(dRho, max(tree, dilated), sdf.density0.y);
}

// The field every march, normal, occlusion and shadow of this pass reads: ONE function for the depth
// prepass and the lit pass alike, so the two still reach bit-identical `t` (ADR-1002's speckle), and
// OUTSIDE the sdfField markers, so a compiled tree (ADR-1003) replaces sdfField under it and keeps it.
// By default it is sdfField and nothing else. An object in density mode (ADR-1142) is drawn by a
// pipeline variant whose module replaces the block between these two markers with a call of
// sdfDensitySurfaceField -- a variant rather than a branch here, because a branch changes the Metal
// compiler's arithmetic for every other object (ADR-388 measured that kind of drift) and a variant
// leaves their module exactly what it was.
// ADR-1150: the block also holds `kSdfDensityMode`, which the density variant sets, so the density march
// (sdfDensityMarch) and its normal are compiled into that variant only: in every other module the
// constant is false and the branches on it are removed at compile time, not taken at run time.
// @@SDF_SURFACE_BEGIN@@
const kSdfDensityMode: bool = false;
const kSdfEngraved: bool = false;
fn sdfSurfaceField(offset: u32, count: u32, p: vec3<f32>, t: f32, world: mat4x4<f32>) -> f32 {
    return sdfField(offset, count, p, t, world);
}
// @@SDF_SURFACE_END@@

fn sdfTetraTap(i: u32) -> vec3<f32> {
    var k = array<vec3<f32>, 4>(vec3<f32>(1.0, -1.0, -1.0), vec3<f32>(-1.0, -1.0, 1.0), vec3<f32>(-1.0, 1.0, -1.0),
                                vec3<f32>(1.0, 1.0, 1.0));
    return k[i];
}

// Tetrahedron-difference normal over sdfSurfaceField (sdf.wgsl's sdfNormal, for either field).
fn sdfFieldNormal(offset: u32, count: u32, p: vec3<f32>, t: f32, world: mat4x4<f32>, eps: f32) -> vec3<f32> {
    // A loop, not four calls: a compiled field is inlined at every call site (ADR-1003).
    var n = vec3<f32>(0.0);
    for (var i = 0u; i < 4u; i = i + 1u) {
        let k = sdfTetraTap(i);
        n = n + k * sdfSurfaceField(offset, count, p + k * eps, t, world);
    }
    return sdfSafeNormalize(n);
}

// The march's far end: the AABB exit, capped by `look1.w` when that is set (ADR-1002). The lit pass
// and the depth prepass must agree on it or the prepass depth rejects the lit surface.
fn sdfMarchEnd(slabFar: f32) -> f32 {
    return select(slabFar, min(slabFar, sdf.look1.w), sdf.look1.w > 0.0);
}

// ADR-1002: 5-tap SDF ambient occlusion along the normal (Quilez), local space. 1 = open.
fn sdfOcclusion(offset: u32, count: u32, p: vec3<f32>, n: vec3<f32>, t: f32, world: mat4x4<f32>,
                reach: f32) -> f32 {
    var occ = 0.0;
    var weight = 1.0;
    for (var i = 0; i < 5; i = i + 1) {
        let h = reach * (0.05 + f32(i) * 0.25);
        let d = sdfSurfaceField(offset, count, p + n * h, t, world);
        occ = occ + max(h - d, 0.0) * weight;
        weight = weight * 0.8;
    }
    return clamp(1.0 - 1.2 * occ / reach, 0.0, 1.0);
}

// ADR-1002: an edge mask from the angle between the surface normal (`nFine`, taken at the normal
// epsilon) and the normal taken over the tetrahedron at `width`. Flat surfaces agree exactly (the
// tetrahedron gradient of a linear field is exact whatever d(p) is), and so do coincident faces and
// the kinks of bound-only unions, which a Laplacian turned into speckle; creases and edges within
// `width` of the point do not. 0 = flat, 1 = a strong edge.
fn sdfEdge(offset: u32, count: u32, p: vec3<f32>, t: f32, world: mat4x4<f32>, width: f32, nFine: vec3<f32>,
           threshold: f32, softness: f32) -> f32 {
    let wide = sdfFieldNormal(offset, count, p, t, world, width);
    return smoothstep(threshold, threshold + softness, 1.0 - dot(wide, nFine));
}

// ADR-1002: soft shadow towards `dir` (local space) from the hit (Quilez, res = min(k h / t)).
// Bounded by `steps` and by the march end; 1 = lit.
fn sdfSoftShadow(offset: u32, count: u32, p: vec3<f32>, dir: vec3<f32>, t: f32, world: mat4x4<f32>,
                 k: f32, steps: u32, tMax: f32, eps: f32) -> f32 {
    var res = 1.0;
    var s = eps * 8.0;
    for (var i = 0u; i < steps; i = i + 1u) {
        let h = sdfSurfaceField(offset, count, p + dir * s, t, world);
        if (h < eps) {
            return 0.0;
        }
        res = min(res, k * h / s);
        s = s + clamp(h, eps, tMax * 0.1);
        if (s > tMax) {
            break;
        }
    }
    return clamp(res, 0.0, 1.0);
}

struct SdfVertexOut {
    @builtin(position) clip: vec4<f32>,
    @location(0) ndc: vec2<f32>,
};

@vertex
fn vs_sdf(@builtin(vertex_index) vertexIndex: u32) -> SdfVertexOut {
    var corners = array<vec2<f32>, 6>(
        vec2<f32>(0.0, 0.0), vec2<f32>(1.0, 0.0), vec2<f32>(1.0, 1.0),
        vec2<f32>(0.0, 0.0), vec2<f32>(1.0, 1.0), vec2<f32>(0.0, 1.0));
    let c = corners[vertexIndex];
    let ndc = mix(sdf.rect.xy, sdf.rect.zw, c);
    var out: SdfVertexOut;
    out.clip = vec4<f32>(ndc, 0.0, 1.0);
    out.ndc = ndc;
    return out;
}

struct SdfFragmentOut {
    @location(0) color: vec4<f32>,
    @location(1) normalRoughness: vec4<f32>,
    @location(2) velocity: vec2<f32>,
    @location(3) emission: vec4<f32>,
    @location(4) ids: u32,
    @builtin(frag_depth) depth: f32,
};

struct SdfDepthOut {
    @builtin(frag_depth) depth: f32,
};

// Ray/AABB slab test; returns (tNear, tFar) with tNear > tFar on a miss.
fn sdfSlab(ro: vec3<f32>, rd: vec3<f32>, lo: vec3<f32>, hi: vec3<f32>) -> vec2<f32> {
    let inv = 1.0 / rd; // +-inf on a zero component; the min/max below handle it
    let t0 = (lo - ro) * inv;
    let t1 = (hi - ro) * inv;
    let tmin = min(t0, t1);
    let tmax = max(t0, t1);
    return vec2<f32>(max(max(tmin.x, tmin.y), tmin.z), min(min(tmax.x, tmax.y), tmax.z));
}

// ADR-1002: the one sphere-tracing loop every entry point runs. The lit pass tests its depth
// LessEqual against the depth prepass's, so the two must reach bit-identical `t`; when the lit
// entry had a loop of its own (it counts steps for the statistics) the two compiled to different
// float code and about a third of the surface failed the depth test by an ulp, leaving dark speckle.
struct SdfMarch {
    t: f32,
    d: f32,       // the field at the hit (0 on a miss)
    steps: u32,
    hit: bool,
    left: bool,   // the ray left the bounds or passed the march cap (a miss that is not out of steps)
};

fn sdfMarch(roL: vec3<f32>, rdL: vec3<f32>, tStart: f32, tEnd: f32, maxSteps: u32, epsilon: f32) -> SdfMarch {
    if (kSdfDensityMode) {
        return sdfDensityMarch(roL, rdL, tStart, tEnd, maxSteps, epsilon); // ADR-1150
    }
    var m: SdfMarch;
    m.t = tStart;
    m.d = 0.0;
    m.steps = 0u;
    m.hit = false;
    m.left = false;
    let offset = sdf.info.x;
    let count = sdf.info.y;
    let stepScale = sdf.march.y;
    let time = sdf.march.w;
    for (var i = 0u; i < maxSteps; i = i + 1u) {
        m.steps = i + 1u;
        let d = sdfSurfaceField(offset, count, roL + rdL * m.t, time, object.model);
        if (d < epsilon * max(m.t, 1e-4)) {
            m.hit = true;
            m.d = d;
            break;
        }
        m.t = m.t + d * stepScale;
        if (m.t > tEnd) {
            m.left = true;
            break;
        }
    }
    return m;
}

// ADR-1150: the march of a density-mode object (ADR-1142), after the prototype's surface.wgsl. The density
// field is not a distance, so plain relaxed sphere tracing lands anywhere within the hit threshold of the
// iso level, and that scatter, quantised by the steps, is the contour banding the iteration-2 renders showed
// on every rounded form. Instead:
//   - the ray is clipped to the volume (no matter outside it, so no surface);
//   - a coarse block whose maximum density is below 0.3 iso is skipped to its exit: below 0.38 iso both the
//     density term and the dilated term of the field are positive, so no surface can be inside it;
//   - steps are the field times the step scale, clamped to [0.65, 3] cells, so no step crawls and none
//     jumps a shell of matter;
//   - the first step below the hit threshold is refined by 7 bisections on the field's sign, which puts
//     the hit on the iso-surface to 1/128 of a step.
// One function for the depth prepass and the lit pass alike, so both still reach bit-identical t.
fn sdfDensityMarch(roL: vec3<f32>, rdL: vec3<f32>, tStart: f32, tEnd: f32, maxSteps: u32, epsilon: f32) -> SdfMarch {
    var m: SdfMarch;
    m.t = tStart;
    m.d = 0.0;
    m.steps = 0u;
    m.hit = false;
    m.left = true;
    let offset = sdf.info.x;
    let count = sdf.info.y;
    let stepScale = sdf.march.y;
    let time = sdf.march.w;
    let cell = sdf.density0.z;
    // the ray in the volume's texture coordinates: linear in the local t (the model matrix is affine)
    let o = ((object.model * vec4<f32>(roL, 1.0)).xyz - sdf.density1.xyz) * sdf.density2.xyz;
    let dir = (object.model * vec4<f32>(rdL, 0.0)).xyz * sdf.density2.xyz;
    let inv = 1.0 / dir;
    let a = -o * inv;
    let b = (vec3<f32>(1.0) - o) * inv;
    let vNear = max(max(min(a.x, b.x), min(a.y, b.y)), min(a.z, b.z));
    let vFar = min(min(max(a.x, b.x), max(a.y, b.y)), max(a.z, b.z));
    var t = max(tStart, vNear);
    let tStop = min(tEnd, vFar);
    if (!(t <= tStop)) {
        m.t = t;
        return m;
    }
    let useCoarse = sdf.density2.w > 0.5;
    let blocks = sdf.density1.w / 8.0; // coarse blocks per unit of texture coordinate
    let coarseMax = vec3<i32>(textureDimensions(sdfDensityCoarse, 0)) - vec3<i32>(1);
    let skipBelow = 0.3 * sdf.density0.x;
    var tPrev = t;
    var hit = false;
    for (var i = 0u; i < maxSteps; i = i + 1u) {
        m.steps = i + 1u;
        if (t > tStop) {
            break;
        }
        if (useCoarse) {
            let cc = clamp(vec3<i32>(floor((o + dir * t) * blocks)), vec3<i32>(0), coarseMax);
            if (textureLoad(sdfDensityCoarse, cc, 0).r < skipBelow) {
                let e0 = (vec3<f32>(cc) / blocks - o) * inv;
                let e1 = ((vec3<f32>(cc) + vec3<f32>(1.0)) / blocks - o) * inv;
                let tExit = min(min(max(e0.x, e1.x), max(e0.y, e1.y)), max(e0.z, e1.z));
                tPrev = t;
                t = max(tExit, t) + cell * 0.3;
                continue;
            }
        }
        let f = sdfSurfaceField(offset, count, roL + rdL * t, time, object.model);
        if (f < epsilon * max(t, 1e-4)) {
            hit = true;
            break;
        }
        tPrev = t;
        t = t + clamp(f * stepScale, cell * 0.65, cell * 3.0);
    }
    if (!hit) {
        m.t = t;
        return m;
    }
    var lo = tPrev;
    var hi = t;
    for (var k = 0; k < 7; k = k + 1) {
        let mid = 0.5 * (lo + hi);
        if (sdfSurfaceField(offset, count, roL + rdL * mid, time, object.model) < 0.0) {
            hi = mid;
        } else {
            lo = mid;
        }
    }
    m.t = hi;
    m.hit = true;
    m.left = false;
    return m;
}

// ADR-1150: the normal of a density-mode surface: tetrahedral differences of the field with its density read
// as the B-spline (sdfDensitySmoothAt), so no texel shows as a facet. The tap distance is the prototype's
// mix(0.7 cell, fine, S): unsharpened, the surface is the density's, and features finer than a cell are not
// in it; sharpened toward the tree, the tree's own normal epsilon serves. A loop, so a compiled tree is
// inlined once here (ADR-1003).
fn sdfDensityNormal(offset: u32, count: u32, p: vec3<f32>, t: f32, world: mat4x4<f32>) -> vec3<f32> {
    let eps = mix(0.7 * sdf.density0.z, sdf.march.z, sdf.density0.y);
    var n = vec3<f32>(0.0);
    for (var i = 0u; i < 4u; i = i + 1u) {
        let k = sdfTetraTap(i);
        n = n + k * sdfDensityFieldOf(offset, count, p + k * eps, t, world, true);
    }
    return sdfSafeNormalize(n);
}

// ---- ADR-1152: the guilloche engraving ----------------------------------------------------------------
// After the prototype's engraveUV (latent.wgsl) and grooveFamily (surface.wgsl), in the object's local
// space: each layer gives surface coordinates (u, v); per octave o (frequency x 4^o, petals x 2^o) the line
// field is  L = f v + A sin(n u + drift f v)  -- fine lines carrying a wave ~1.5 spacings tall whose phase
// drifts across the lines (the rose-engine braid) -- faded out where a line cycle is smaller than ~0.12-0.35
// pixel, and cut as a V-groove that tilts the normal across the line. The records are packed after the
// object's nodes (sdf_renderer.cpp): a header, then one per layer. The scene/material_engraving.cpp
// `engravingUv` is the CPU twin of sdfEngraveUv.
struct SdfEngraveUv {
    uv: vec2<f32>,
    petals: f32,
    angular: bool,
    amp: f32,
};

fn sdfEngraveAtan2(y: f32, x: f32) -> f32 {
    return select(atan2(y, x), 0.0, abs(x) + abs(y) < 1e-12); // atan2(0, 0) is NaN on Metal
}

fn sdfEngraveUv(rec: SdfNodeGpu, q: vec3<f32>, crawl: f32) -> SdfEngraveUv {
    var o: SdfEngraveUv;
    let f = max(rec.p0.w, 1e-3);
    if (rec.kind == 0u) { // rosette: rings about the centre, u = the angle about local z, v = the radius
        let e = q - rec.p0.xyz;
        let r = length(e.xy);
        o.uv = vec2<f32>(sdfEngraveAtan2(e.y, e.x) + crawl, r);
        o.petals = rec.p1.w;
        o.angular = true;
        o.amp = smoothstep(rec.p2.z, rec.p2.z + 6.0 / f, r);
    } else if (rec.kind == 1u) { // contour: shells about the centre
        let d = q - rec.p0.xyz;
        o.uv = vec2<f32>(sdfEngraveAtan2(d.y, d.x) - crawl * 0.3, length(d) - crawl * 0.3);
        o.petals = rec.p1.w;
        o.angular = true;
        o.amp = smoothstep(2.0 / f, 8.0 / f, length(d.xy));
    } else { // engine-turned: lines across the axis, one wave per line
        let a = rec.p1.xyz;
        let up = select(vec3<f32>(0.0, 1.0, 0.0), vec3<f32>(1.0, 0.0, 0.0), abs(a.y) > 0.9);
        let b = normalize(up - a * dot(up, a));
        o.uv = vec2<f32>(dot(q, b) * 3.0 + crawl * 2.0, dot(q, a));
        o.petals = 1.0;
        o.angular = false;
        o.amp = 1.0;
    }
    return o;
}

fn sdfWrapPi(x: f32) -> f32 {
    return x - 6.28318531 * round(x / 6.28318531);
}

struct SdfGroove {
    n: vec3<f32>,      // the perturbed normal (local)
    tang: vec3<f32>,   // the strongest groove's line direction (local)
    across: vec3<f32>, // across it
    mask: f32,         // 0..1: how much of that groove the pixel sees
    cover: f32,        // 0..1: the strongest visibility of any octave cut here (0 = nothing engraved)
};

fn sdfGrooveLayer(rec: SdfNodeGpu, q: vec3<f32>, n0: vec3<f32>, crawl: f32, footprint: f32, weight: f32,
                  groove: ptr<function, SdfGroove>) {
    if (weight < 0.02) {
        return;
    }
    // the gradients of u and v by forward differences (an angular u unwrapped across its branch cut)
    let e = 2e-3;
    let e0 = sdfEngraveUv(rec, q, crawl);
    let ex = sdfEngraveUv(rec, q + vec3<f32>(e, 0.0, 0.0), crawl);
    let ey = sdfEngraveUv(rec, q + vec3<f32>(0.0, e, 0.0), crawl);
    let ez = sdfEngraveUv(rec, q + vec3<f32>(0.0, 0.0, e), crawl);
    var du = vec3<f32>(ex.uv.x - e0.uv.x, ey.uv.x - e0.uv.x, ez.uv.x - e0.uv.x);
    if (e0.angular) {
        du = vec3<f32>(sdfWrapPi(du.x), sdfWrapPi(du.y), sdfWrapPi(du.z));
    }
    du = du / e;
    let dv = vec3<f32>(ex.uv.y - e0.uv.y, ey.uv.y - e0.uv.y, ez.uv.y - e0.uv.y) / e;
    let u = e0.uv.x;
    let v = e0.uv.y;
    let amplitude = 1.5 * e0.amp; // the wave's height, in line spacings
    let drift = 0.35;             // its phase advance per line: the braid
    let depth = rec.p2.y;
    for (var o = 0; o < 4; o = o + 1) {
        let ff = rec.p0.w * pow(4.0, f32(o));
        let nn = e0.petals * pow(2.0, f32(o)); // petals double per octave while lines quadruple: no spokes
        let arg = nn * u + drift * ff * v;
        let lines = ff * v + amplitude * sin(arg);
        var grad = ff * dv + amplitude * cos(arg) * (nn * du + drift * ff * dv);
        grad = grad - n0 * dot(n0, grad);
        let gl = length(grad);
        if (gl < 1e-5) {
            continue;
        }
        let vis = (1.0 - smoothstep(0.12, 0.35, gl * footprint)) * weight; // line cycles per pixel
        if (vis < 0.01) {
            continue;
        }
        (*groove).cover = max((*groove).cover, vis);
        let across = grad / gl;
        let x = fract(lines) - 0.5;
        let w = 0.32;
        let h = max(0.0, 1.0 - abs(x) / w);
        let slope = select(0.0, -sign(x) / w, abs(x) < w);
        (*groove).n = normalize((*groove).n - across * (slope * depth * vis / pow(1.6, f32(o))));
        if (h * vis >= (*groove).mask) {
            (*groove).mask = h * vis;
            (*groove).tang = normalize(cross(n0, across));
            (*groove).across = across;
        }
    }
}

// Smooth value noise for the engraved panels.
fn sdfPanelNoise(p: vec3<f32>) -> f32 {
    let i = vec3<i32>(floor(p));
    let f = fract(p);
    let u = f * f * (3.0 - 2.0 * f);
    let a = mix(mix(hash01(i, 91u), hash01(i + vec3<i32>(1, 0, 0), 91u), u.x),
                mix(hash01(i + vec3<i32>(0, 1, 0), 91u), hash01(i + vec3<i32>(1, 1, 0), 91u), u.x), u.y);
    let b = mix(mix(hash01(i + vec3<i32>(0, 0, 1), 91u), hash01(i + vec3<i32>(1, 0, 1), 91u), u.x),
                mix(hash01(i + vec3<i32>(0, 1, 1), 91u), hash01(i + vec3<i32>(1, 1, 1), 91u), u.x), u.y);
    return mix(a, b, u.z);
}

// The engraving at local point q with local normal n0 and a pixel `footprint` (local units). Rosettes are
// cut in their annulus and polish their centre; contour layers are cut in the panels (everywhere without
// panels), engine-turned layers between them, both kept off the rosettes. Fills `sdfDetail` for the shading.
fn sdfEngrave(q: vec3<f32>, n0: vec3<f32>, footprint: f32, time: f32) -> vec3<f32> {
    let base = sdf.info.x + sdf.surfaces.z;
    let head = sdfNodes[base];
    let layers = min(u32(head.p1.y + 0.5), 6u);
    let crawl = head.p0.y * time;
    var g: SdfGroove;
    g.n = n0;
    g.mask = 0.0;
    g.cover = 0.0;
    let ref0 = select(vec3<f32>(0.0, 1.0, 0.0), vec3<f32>(1.0, 0.0, 0.0), abs(n0.y) > 0.9);
    g.tang = normalize(cross(n0, ref0));
    g.across = cross(g.tang, n0);
    var panel = 1.0;
    if (head.p1.x > 0.0) {
        panel = smoothstep(0.42, 0.58, sdfPanelNoise(q * head.p1.x + vec3<f32>(3.1, 0.0, 0.0)));
    }
    // the rosettes' cover: their annulus and the polished centre inside it
    var cover = 0.0;
    for (var k = 0u; k < layers; k = k + 1u) {
        let rec = sdfNodes[base + 1u + k];
        if (rec.kind == 0u) {
            let d = length(q - rec.p0.xyz);
            cover = max(cover, rec.p2.x * (1.0 - smoothstep(rec.p2.w * 0.7, rec.p2.w, d)));
        }
    }
    for (var k = 0u; k < layers; k = k + 1u) {
        let rec = sdfNodes[base + 1u + k];
        var weight = rec.p2.x;
        if (rec.kind == 0u) {
            let d = length(q - rec.p0.xyz);
            weight = weight * smoothstep(rec.p2.z * 0.85, rec.p2.z, d) * (1.0 - smoothstep(rec.p2.w * 0.7, rec.p2.w, d));
        } else if (rec.kind == 1u) {
            weight = weight * (1.0 - 0.8 * cover) * panel;
        } else {
            weight = weight * (1.0 - cover) * select(1.0, 1.0 - panel, head.p1.x > 0.0);
        }
        sdfGrooveLayer(rec, q, n0, crawl, footprint, weight, &g);
    }
    // to world space for the shading: the line direction is a tangent (the model matrix carries it), the
    // across direction is rebuilt from it and the world normal
    let nW = normalize((object.normalMatrix * vec4<f32>(g.n, 0.0)).xyz);
    var tW = (object.model * vec4<f32>(g.tang, 0.0)).xyz;
    tW = tW - nW * dot(nW, tW);
    let tl = length(tW);
    // where nothing is cut the line direction is no direction at all: the anisotropy keeps its reference
    tW = select(vec3<f32>(0.0), tW / tl, tl > 1e-6 && g.cover > 0.0);
    let aW = select(vec3<f32>(0.0), normalize(cross(tW, nW)), tl > 1e-6 && g.cover > 0.0);
    // no grating on the rosettes: their groove direction turns too fast per pixel and aliases to confetti
    sdfDetail = SurfaceDetail(tW, g.mask, aW, head.p0.z * g.mask * (1.0 - cover), head.p0.w);
    return g.n;
}

// The lit pass's depth, a few ulps nearer than the prepass's for the same `t`, so a last-bit
// disagreement between the two entries still passes LessEqual (4 ulps near 1.0 is ~2.4e-7).
fn sdfLitDepth(z: f32) -> f32 {
    let d = clamp(z, 0.0, 1.0);
    return select(d, bitcast<f32>(bitcast<u32>(d) - 4u), d > 1e-6);
}

@fragment
fn fs_sdf(in: SdfVertexOut) -> SdfFragmentOut {
    // The camera ray through this pixel, world space.
    let nearH = frame.invViewProj * vec4<f32>(in.ndc, 0.0, 1.0);
    let farH = frame.invViewProj * vec4<f32>(in.ndc, 1.0, 1.0);
    let nearW = nearH.xyz / nearH.w;
    let farW = farH.xyz / farH.w;
    let eye = frame.cameraPos.xyz;
    let rdW = normalize(farW - eye);
    let tNearPlane = length(nearW - eye);

    // Into the object's local space; t is measured in local units along the unit direction.
    let roL = (sdf.worldToLocal * vec4<f32>(eye, 1.0)).xyz;
    let rdScaled = (sdf.worldToLocal * vec4<f32>(rdW, 0.0)).xyz;
    let unitScale = max(length(rdScaled), 1e-8);
    let rdL = rdScaled / unitScale;
    let slab = sdfSlab(roL, rdL, sdf.boundsMin.xyz, sdf.boundsMax.xyz);
    let tStart = max(slab.x, tNearPlane * unitScale);
    let tEnd = sdfMarchEnd(slab.y);

    let offset = sdf.info.x;
    let count = sdf.info.y;
    let maxSteps = sdf.info.z;
    let epsilon = sdf.march.x;
    let stepScale = sdf.march.y;
    let time = sdf.march.w;

    var hit = false;
    var t = tStart;
    var steps = 0u;
    var dHit = 0.0;
    var left = true;
    if (slab.x <= slab.y && tEnd > 0.0) {
        let m = sdfMarch(roL, rdL, tStart, tEnd, maxSteps, epsilon);
        hit = m.hit;
        t = m.t;
        steps = m.steps;
        dHit = m.d;
        left = m.left;
    }
    // ADR-1002: sampled step statistics (every 4th pixel in x and y), before a miss discards.
    let pix = vec2<u32>(in.clip.xy);
    if (sdf.look2.w > 0.5 && (pix.x & 3u) == 0u && (pix.y & 3u) == 0u) {
        atomicAdd(&sdfStepStats.counters[0], steps);
        atomicMax(&sdfStepStats.counters[1], steps);
        atomicAdd(&sdfStepStats.counters[2], 1u);
        if (hit) {
            atomicAdd(&sdfStepStats.counters[3], 1u);
        } else if (!left) {
            atomicAdd(&sdfStepStats.counters[4], 1u);
        }
    }
    if (!hit) {
        discard;
    }

    // ADR-1002: shade at the hit refined by one more step (the hit sits anywhere within the
    // distance-scaled threshold of the surface, and across a rounded edge that jitter reads as speckle).
    // Depth stays at the unrefined `t`, which is what the depth prepass marched to.
    let pL = roL + rdL * (t + dHit * stepScale);
    let pDepth = roL + rdL * t;
    var nL = vec3<f32>(0.0, 0.0, 1.0);
    if (kSdfDensityMode) {
        nL = sdfDensityNormal(offset, count, pL, time, object.model); // ADR-1150
    } else {
        nL = sdfFieldNormal(offset, count, pL, time, object.model, sdf.march.z);
    }
    let worldPos = (object.model * vec4<f32>(pL, 1.0)).xyz;
    var nShade = nL;
    if (kSdfEngraved) {
        // ADR-1152: the engraving, with the hit's pixel footprint in local units (the angle one pixel
        // subtends, times the local t).
        let farUp = frame.invViewProj * vec4<f32>(in.ndc + vec2<f32>(0.0, 2.0 * frame.targetSize.w), 1.0, 1.0);
        let pixelAngle = length(normalize(farUp.xyz / farUp.w - eye) - rdW);
        nShade = sdfEngrave(pL, nL, pixelAngle * t, time);
    }
    var normal = normalize((object.normalMatrix * vec4<f32>(nShade, 0.0)).xyz);
    // Face the eye: an interior start (camera inside the surface) yields a back-facing gradient.
    if (dot(normal, eye - worldPos) < 0.0) {
        normal = -normal;
    }
    let clip = frame.viewProj * vec4<f32>((object.model * vec4<f32>(pDepth, 1.0)).xyz, 1.0);

    let screenUv = vec2<f32>(in.ndc.x * 0.5 + 0.5, 0.5 - in.ndc.y * 0.5);
    var out: SdfFragmentOut;
    // The local hit point is the ADR-030 `localPosition` material input.
    // ADR-1044: the hit's surface multiplies the material's base colour and emission.
    var colorMul = vec3<f32>(1.0);
    var emissiveMul = vec3<f32>(1.0);
    var edgeMul = vec3<f32>(1.0); // ADR-1047
    var rimMul = 1.0;             // ADR-1052
    if (sdf.surfaces.y > 0u) {
        let id = min(sdfSurface(offset, count, pL, time, object.model), sdf.surfaces.y - 1u);
        let rec = sdfNodes[offset + sdf.surfaces.x + id];
        colorMul = rec.p0.xyz;
        emissiveMul = rec.p1.xyz;
        edgeMul = rec.p2.xyz;
        rimMul = rec.p2.w;
        // ADR-1054: screen static -- a hash per local cell, re-rolled `rate` times a second (a pure function
        // of time, so seeks match play), with a bright bar rolling down the screen. Mean brightness ~1.
        let staticAmount = rec.p1.w;
        if (staticAmount > 0.0) {
            let cell = vec3<i32>(floor(pL / sdf.look6.x));
            let frameNo = u32(max(floor(time * sdf.look6.y), 0.0));
            let n = hash01(cell, frameNo + 17u);
            let bar = smoothstep(0.0, 0.15, fract(pL.y * 1.5 - time * 0.45)) * (1.0 - smoothstep(0.15, 0.3, fract(pL.y * 1.5 - time * 0.45)));
            let snow = n * 2.0 * (1.0 - sdf.look6.z * 0.5) + bar * sdf.look6.z * 1.5;
            let k = mix(1.0, snow, clamp(staticAmount, 0.0, 1.0));
            colorMul = colorMul * k;
            emissiveMul = emissiveMul * k;
        }
    }
    // ADR-1055: the world wave recolours this surface's emission (and, below, its edges) and adds its light.
    var wave: SdfWaveSample;
    let waveOn = sdf.wave4.w > 0.5;
    if (waveOn) {
        wave = sdfWaveAt(worldPos);
        emissiveMul = sdfWaveRecolor(emissiveMul, wave);
    }
    var shaded = shadeSurface(worldPos, normal, vec2<f32>(0.0), true, colorMul, emissiveMul,
                              materialInstanceZero(pL), screenUv);
    // ADR-1002: the field's own occlusion, soft shadow and edge emission. A cheap version: occlusion and
    // shadow scale the whole shaded colour (lighting, the material's emission and the fog alike); the
    // edges add emission afterwards, into the colour and the bloom target, so they are never occluded.
    let nFace = select(-nL, nL, dot(nL, roL - pL) >= 0.0);
    var visibility = 1.0;
    if (sdf.look0.x > 0.0) {
        visibility = visibility * mix(1.0, sdfOcclusion(offset, count, pL, nFace, time, object.model, sdf.look0.y),
                                      clamp(sdf.look0.x, 0.0, 1.0));
    }
    if (sdf.look2.x > 0.0) {
        let dirL = normalize((sdf.worldToLocal * vec4<f32>(sdfSafeNormalize(sdf.look3.xyz), 0.0)).xyz);
        let shadowT = select(tEnd - tStart, sdf.look1.w, sdf.look1.w > 0.0);
        let lit = sdfSoftShadow(offset, count, pL + nFace * epsilon * max(t, 1e-3) * 2.0, dirL, time, object.model,
                                sdf.look2.y, u32(sdf.look2.z), max(shadowT, 1e-3), epsilon * max(t, 1e-3));
        visibility = visibility * mix(1.0, lit, clamp(sdf.look2.x, 0.0, 1.0));
    }
    shaded.color = vec4<f32>(shaded.color.rgb * visibility, shaded.color.a);
    shaded.emission = shaded.emission * visibility;
    if (sdf.look0.z > 0.0) {
        // ADR-1047: a width in pixels follows the hit's distance (the angle one pixel subtends, times t),
        // so a line is as wide on screen in a small room as far down a corridor.
        var edgeWidth = sdf.look0.w;
        if (sdf.look4.x > 0.0) {
            let farUp = frame.invViewProj * vec4<f32>(in.ndc + vec2<f32>(0.0, 2.0 * frame.targetSize.w), 1.0, 1.0);
            let pixelAngle = length(normalize(farUp.xyz / farUp.w - eye) - rdW);
            edgeWidth = max(sdf.look4.x * pixelAngle * t, 1e-5);
        }
        let edge = sdfEdge(offset, count, pL, time, object.model, edgeWidth, nL, sdf.look4.y, sdf.look4.z);
        // ADR-1004: the edge light sits on the surface, so the air between the eye and the surface
        // dims it like the rest of the shaded colour. `applyFog` is `mix(fog, c, f)`, so its values at
        // c = 1 and c = 0 differ by exactly the transmittance `f`, whatever fog model is active. Unfogged,
        // a repeated structure's edges stayed at full strength to the march's end, which read as a flat
        // wireframe with no depth and aliased into moire where the edges shrank below a pixel.
        let transmittance = applyFog(vec3<f32>(1.0), worldPos).x - applyFog(vec3<f32>(0.0), worldPos).x;
        var edgeColor = sdf.look1.xyz * edgeMul;
        if (waveOn) {
            edgeColor = sdfWaveRecolor(edgeColor, wave); // ADR-1055
        }
        let glow = edgeColor * (sdf.look0.z * edge * transmittance);
        shaded.color = vec4<f32>(shaded.color.rgb + glow, shaded.color.a);
        shaded.emission = shaded.emission + glow;
    }
    if (sdf.look5.w > 0.0 && rimMul > 0.0) {
        // ADR-1052: a fresnel rim from the world normal and the view direction, fogged like the edges, added
        // into the colour and the bloom target so a smooth silhouette glows against the dark.
        let facing = clamp(abs(dot(normal, -rdW)), 0.0, 1.0);
        let rim = pow(1.0 - facing, sdf.look4.w);
        let transmittance = applyFog(vec3<f32>(1.0), worldPos).x - applyFog(vec3<f32>(0.0), worldPos).x;
        let glow = sdf.look5.xyz * (sdf.look5.w * rimMul * rim * transmittance);
        shaded.color = vec4<f32>(shaded.color.rgb + glow, shaded.color.a);
        shaded.emission = shaded.emission + glow;
    }
    if (waveOn && sdf.wave2.w > 0.0) {
        // ADR-1055: the band's own light on the surface, fogged like the edges.
        let transmittance = applyFog(vec3<f32>(1.0), worldPos).x - applyFog(vec3<f32>(0.0), worldPos).x;
        let glow = wave.color * (sdf.wave2.w * wave.band * transmittance);
        shaded.color = vec4<f32>(shaded.color.rgb + glow, shaded.color.a);
        shaded.emission = shaded.emission + glow;
    }
    out.color = shaded.color;
    out.normalRoughness = packNormalRoughness(shaded.normal, shaded.roughness, shaded.flags);
    // The hit point carried by last frame's object matrix and view-projection (ADR-035).
    let prevWorld = (object.prevModel * vec4<f32>(pL, 1.0)).xyz;
    out.velocity = screenVelocity(clip, frame.prevViewProj * vec4<f32>(prevWorld, 1.0));
    out.emission = vec4<f32>(shaded.emission, shaded.bloomWeight);
    out.ids = packIds(object.ids.x, object.ids.y);
    out.depth = sdfLitDepth(clip.z / clip.w);
    return out;
}

// Depth-only entries. `fs_sdf_depth` is the depth prepass: it must march exactly as the lit pass
// does, or the depth it writes differs and the lit pass's LessEqual test rejects the surface.
// `fs_sdf_shadow` is the shadow-map version (ADR-034): a quarter of the steps at a looser epsilon,
// bounded by the object's box, because a caster silhouette needs far less precision.
@fragment
fn fs_sdf_depth(in: SdfVertexOut) -> SdfDepthOut {
    return sdfDepthOnly(in, sdf.info.z, sdf.march.x);
}

@fragment
fn fs_sdf_shadow(in: SdfVertexOut) -> SdfDepthOut {
    // `info.w` is the tier's `sdfShadowSteps`, capped by the object's own march so a cheap object
    // does not get an expensive shadow. It used to be a bare `info.z / 4`, and the tier field was
    // read by nothing at all -- four numbers in the tier table that no path evaluated.
    return sdfDepthOnly(in, clamp(min(sdf.info.w, sdf.info.z), 8u, 1024u), sdf.march.x * 3.0);
}

fn sdfDepthOnly(in: SdfVertexOut, maxSteps: u32, epsilon: f32) -> SdfDepthOut {
    let nearH = frame.invViewProj * vec4<f32>(in.ndc, 0.0, 1.0);
    let farH = frame.invViewProj * vec4<f32>(in.ndc, 1.0, 1.0);
    let nearW = nearH.xyz / nearH.w;
    let farW = farH.xyz / farH.w;
    let eye = frame.cameraPos.xyz;
    let rdW = normalize(farW - eye);
    let tNearPlane = length(nearW - eye);

    let roL = (sdf.worldToLocal * vec4<f32>(eye, 1.0)).xyz;
    let rdScaled = (sdf.worldToLocal * vec4<f32>(rdW, 0.0)).xyz;
    let unitScale = max(length(rdScaled), 1e-8);
    let rdL = rdScaled / unitScale;
    let slab = sdfSlab(roL, rdL, sdf.boundsMin.xyz, sdf.boundsMax.xyz);
    let tStart = max(slab.x, tNearPlane * unitScale);
    let tEnd = sdfMarchEnd(slab.y);
    if (slab.x > slab.y || tEnd <= 0.0) {
        discard;
    }

    let m = sdfMarch(roL, rdL, tStart, tEnd, maxSteps, epsilon);
    if (!m.hit) {
        discard;
    }
    let worldPos = (object.model * vec4<f32>(roL + rdL * m.t, 1.0)).xyz;
    let clip = frame.viewProj * vec4<f32>(worldPos, 1.0);
    var out: SdfDepthOut;
    out.depth = clamp(clip.z / clip.w, 0.0, 1.0);
    return out;
}
