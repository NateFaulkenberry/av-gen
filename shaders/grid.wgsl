// Procedural floor grid drawn on a plane: anti-aliased lines from world XZ, radial fade, additive.
#include "common.wgsl"

fn gridLine(coord: vec2<f32>, spacing: f32, width: f32) -> f32 {
    let scaled = coord / spacing;
    let fw = fwidth(scaled);
    let g = abs(fract(scaled - 0.5) - 0.5) / max(fw, vec2<f32>(1e-4));
    let line = 1.0 - min(min(g.x, g.y) / width, 1.0);
    return line;
}

@fragment
fn fs_main(in: VertexOut) -> @location(0) vec4<f32> {
    let p = in.worldPos.xz;
    let major = gridLine(p, 1.0, 1.0);
    let minor = gridLine(p, 0.25, 0.8) * 0.35;
    let dist = length(p);
    let fade = exp(-dist * dist * 0.012);
    let intensity = frame.params.y;
    let tint = vec3<f32>(0.35, 0.55, 1.0);
    let glow = object.emissive.rgb * object.emissive.w * exp(-dist * dist * 0.6) * 0.15;
    let color = (tint * (major + minor) * fade + glow) * intensity;
    return vec4<f32>(color, 1.0);
}
