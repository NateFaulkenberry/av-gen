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
#include "pbr_shade.wgsl"
#include "sdf.wgsl"

struct SdfObjectUniforms {
    worldToLocal: mat4x4<f32>,
    boundsMin: vec4<f32>,   // xyz = local AABB min
    boundsMax: vec4<f32>,   // xyz = local AABB max
    info: vec4<u32>,        // x = node offset, y = node count, z = max steps, w = 0
    march: vec4<f32>,       // x = epsilon, y = step scale, z = normal epsilon, w = time
    rect: vec4<f32>,        // NDC rect of the projected bounds: xmin, ymin, xmax, ymax
};

@group(1) @binding(1) var<uniform> sdf: SdfObjectUniforms;
@group(1) @binding(2) var<storage, read> sdfNodes: array<SdfNodeGpu>;
@group(1) @binding(3) var<uniform> fieldBlock: FieldBlock;

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
    let tEnd = slab.y;

    let offset = sdf.info.x;
    let count = sdf.info.y;
    let maxSteps = sdf.info.z;
    let epsilon = sdf.march.x;
    let stepScale = sdf.march.y;
    let time = sdf.march.w;

    var hit = false;
    var t = tStart;
    if (slab.x <= slab.y && tEnd > 0.0) {
        for (var i = 0u; i < maxSteps; i = i + 1u) {
            let p = roL + rdL * t;
            let d = sdfEvaluate(offset, count, p, time, object.model);
            if (d < epsilon * max(t, 1e-4)) {
                hit = true;
                break;
            }
            t = t + d * stepScale;
            if (t > tEnd) {
                break;
            }
        }
    }
    if (!hit) {
        discard;
    }

    let pL = roL + rdL * t;
    let nL = sdfNormal(offset, count, pL, time, object.model, sdf.march.z);
    let worldPos = (object.model * vec4<f32>(pL, 1.0)).xyz;
    var normal = normalize((object.normalMatrix * vec4<f32>(nL, 0.0)).xyz);
    // Face the eye: an interior start (camera inside the surface) yields a back-facing gradient.
    if (dot(normal, eye - worldPos) < 0.0) {
        normal = -normal;
    }
    let clip = frame.viewProj * vec4<f32>(worldPos, 1.0);

    var out: SdfFragmentOut;
    // The local hit point is the ADR-030 `localPosition` material input.
    out.color = shadePbrInstanced(worldPos, normal, vec2<f32>(0.0), true, vec3<f32>(1.0), vec3<f32>(1.0),
                                  materialInstanceZero(pL));
    out.depth = clamp(clip.z / clip.w, 0.0, 1.0);
    return out;
}
