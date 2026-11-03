// ADR-1879: the portal pass. Another world, rendered by the portal renderer from the virtual camera (the same
// projection, the portal's transform applied), is laid into this frame where the camera's ray meets the corruption's
// opening (corruption.wgsl's aperture on the entry plane), in front of what this world drew there; once the camera has
// crossed the plane it fills the frame. It runs after the opaque scene and before the water and the refraction copy,
// so the river reflects the Moon in the opening and everything this pass covers stays covered (depth).
#include "common.wgsl"
#include "corruption.wgsl"

@group(1) @binding(0) var portalHdr: texture_2d<f32>;
@group(1) @binding(1) var portalSampler: sampler;
@group(1) @binding(2) var<uniform> pc: vec4<f32>; // x = 1 crossed (fill the frame), y = rim lens strength, z = depth margin (m)

struct PortalIn {
    @builtin(position) clip: vec4<f32>,
    @location(0) ndc: vec2<f32>,
};

struct PortalOut {
    @location(0) color: vec4<f32>,
    @location(1) normalRoughness: vec4<f32>,
    @location(2) velocity: vec4<f32>,
    @location(3) emission: vec4<f32>,
    @location(4) ids: u32,
    @builtin(frag_depth) depth: f32,
};

@vertex
fn vs_portal(@builtin(vertex_index) index: u32) -> PortalIn {
    var positions = array<vec2<f32>, 3>(vec2<f32>(-1.0, -1.0), vec2<f32>(3.0, -1.0), vec2<f32>(-1.0, 3.0));
    var out: PortalIn;
    out.clip = vec4<f32>(positions[index], 0.0, 1.0);
    out.ndc = positions[index];
    return out;
}

@fragment
fn fs_portal(in: PortalIn) -> PortalOut {
    var out: PortalOut;
    out.normalRoughness = vec4<f32>(0.0);
    out.velocity = vec4<f32>(0.0);
    out.emission = vec4<f32>(0.0);
    out.ids = 0u;
    let uv = in.clip.xy * frame.targetSize.zw;
    if (pc.x > 0.5) {
        out.color = vec4<f32>(textureSampleLevel(portalHdr, portalSampler, uv, 0.0).rgb, 1.0);
        out.depth = 0.0; // the other world is the frame now: nothing of this one is drawn over it
        return out;
    }
    if (!crOn() || frame.corruption2.w <= 0.0) {
        discard;
    }
    let ndc = vec2<f32>(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    let dir = cameraViewRay(ndc);
    let b = crBasis();
    let focus = frame.corruption0.xyz;
    // ADR-1903: the camera already past the opening's plane but before the crossing's instant (the crossing time is
    // found on a 1/60 s grid and the frame can land between): inside the aperture it is through, so the other world
    // fills the frame -- without this, that one frame showed the wall behind the opening (a one-frame pop)
    // (the crossing's own frame puts the camera ON the plane, where the surface the opening is cut into stands at the
    // eye: within half a metre of the plane counts as through)
    let camSide = dot(frame.cameraPos.xyz - focus, b.ax);
    if (camSide >= -0.5 && crAperture(frame.cameraPos.xyz - b.ax * camSide, b).e < 1.0) {
        out.color = vec4<f32>(textureSampleLevel(portalHdr, portalSampler, uv, 0.0).rgb, 1.0);
        out.depth = 0.0;
        return out;
    }
    let denom = dot(dir, b.ax);
    if (denom <= 1e-5) {
        discard;
    }
    let t = dot(focus - frame.cameraPos.xyz, b.ax) / denom;
    if (t <= 0.0) {
        discard;
    }
    let hit = frame.cameraPos.xyz + dir * t;
    let ap = crAperture(hit, b);
    if (ap.e >= 1.0) {
        discard;
    }
    // a lens at the rim: the far side bends a little toward the middle as the edge nears
    let lens = smoothstep(0.7, 1.0, ap.e) * pc.y;
    let centreClip = frame.viewProj * vec4<f32>(focus, 1.0);
    let centreUv = vec2<f32>(centreClip.x / centreClip.w * 0.5 + 0.5, 0.5 - centreClip.y / centreClip.w * 0.5);
    let suv = mix(uv, centreUv, lens * 0.08);
    out.color = vec4<f32>(textureSampleLevel(portalHdr, portalSampler, suv, 0.0).rgb, 1.0);
    // the plane, pulled toward the eye by a margin so the wall's own relief around the opening does not cover it,
    // while rock genuinely in front of the opening still does
    let pulled = frame.cameraPos.xyz + dir * max(t - pc.z, 0.05);
    let c = frame.viewProj * vec4<f32>(pulled, 1.0);
    out.depth = clamp(c.z / c.w, 0.0, 1.0);
    return out;
}
