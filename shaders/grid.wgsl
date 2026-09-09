// Procedural floor grid drawn on a plane: anti-aliased lines from world XZ, radial fade, additive.
#include "common.wgsl"

fn gridLine(coord: vec2<f32>, spacing: f32, width: f32) -> f32 {
    let scaled = coord / spacing;
    let fw = fwidth(scaled);
    let g = abs(fract(scaled - 0.5) - 0.5) / max(fw, vec2<f32>(1e-4));
    return 1.0 - min(min(g.x, g.y) / width, 1.0);
}

@fragment
fn fs_main(in: VertexOut) -> SceneOut {
    let p = in.worldPos.xz;
    let major = gridLine(p, 1.0, 1.0);
    let minor = gridLine(p, 0.25, 0.8) * 0.35;
    let dist = length(p);
    // Fade with distance so unresolved far lines do not alias into noise (no MSAA in 0.2).
    let fade = exp(-dist * dist * 0.03);
    let intensity = frame.params.y;
    let tint = vec3<f32>(0.35, 0.55, 1.0);
    let glow = object.emissive.rgb * object.emissive.w * exp(-dist * dist * 0.6) * 0.15;
    let color = (tint * (major + minor) * fade + glow) * intensity;
    var out: SceneOut;
    out.color = vec4<f32>(color, 1.0);
    // The grid is additive and unlit; it still fills the auxiliary targets so nothing behind it
    // leaks into occlusion or motion vectors at its silhouette (ADR-035).
    out.normalRoughness = packNormalRoughness(normalize(in.normal), 1.0, 2.0);
    out.velocity = screenVelocity(in.clip, in.prevClip);
    out.emission = vec4<f32>(color, 0.0);
    out.ids = packIds(object.ids.x, object.ids.y);
    return out;
}
