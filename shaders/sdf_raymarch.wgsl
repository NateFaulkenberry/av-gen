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
// 2 array<SdfNodeGpu> (read-only storage, every object's records concatenated), 3 FieldBlock};
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
};

// ADR-1002: step statistics, accumulated by the lit pass on every 4th pixel in x and y.
// [0] sum of steps, [1] max steps, [2] sampled rays, [3] hits, [4] rays that ran out of steps.
struct SdfStepStats {
    counters: array<atomic<u32>, 8>,
};

@group(1) @binding(1) var<uniform> sdf: SdfObjectUniforms;
@group(1) @binding(2) var<storage, read> sdfNodes: array<SdfNodeGpu>;
@group(1) @binding(3) var<uniform> fieldBlock: FieldBlock;
@group(1) @binding(4) var<storage, read_write> sdfStepStats: SdfStepStats;

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

fn sdfTetraTap(i: u32) -> vec3<f32> {
    var k = array<vec3<f32>, 4>(vec3<f32>(1.0, -1.0, -1.0), vec3<f32>(-1.0, -1.0, 1.0), vec3<f32>(-1.0, 1.0, -1.0),
                                vec3<f32>(1.0, 1.0, 1.0));
    return k[i];
}

// Tetrahedron-difference normal over sdfField (sdf.wgsl's sdfNormal, for either field).
fn sdfFieldNormal(offset: u32, count: u32, p: vec3<f32>, t: f32, world: mat4x4<f32>, eps: f32) -> vec3<f32> {
    // A loop, not four calls: a compiled field is inlined at every call site (ADR-1003).
    var n = vec3<f32>(0.0);
    for (var i = 0u; i < 4u; i = i + 1u) {
        let k = sdfTetraTap(i);
        n = n + k * sdfField(offset, count, p + k * eps, t, world);
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
        let d = sdfField(offset, count, p + n * h, t, world);
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
        let h = sdfField(offset, count, p + dir * s, t, world);
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
        let d = sdfField(offset, count, roL + rdL * m.t, time, object.model);
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
    let nL = sdfFieldNormal(offset, count, pL, time, object.model, sdf.march.z);
    let worldPos = (object.model * vec4<f32>(pL, 1.0)).xyz;
    var normal = normalize((object.normalMatrix * vec4<f32>(nL, 0.0)).xyz);
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
        let glow = sdf.look1.xyz * edgeMul * (sdf.look0.z * edge * transmittance);
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
