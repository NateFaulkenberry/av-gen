// Debug drawing (ADR-031): world-space lines and camera-facing point sprites for the inspection
// modes. Vertices come from one storage buffer of DebugVertex records; lines draw as a line list,
// points expand to a quad in the vertex shader using the frame's camera basis.

#include "common.wgsl"

struct DebugVertex {
    position: vec3<f32>,
    size: f32,      // points: world-space diameter; lines: unused
    color: vec4<f32>,
};

@group(1) @binding(0) var<storage, read> debugVertices: array<DebugVertex>;

struct VsOut {
    @builtin(position) clip: vec4<f32>,
    @location(0) color: vec4<f32>,
    @location(1) uv: vec2<f32>,
};

@vertex
fn vs_lines(@builtin(vertex_index) vertexIndex: u32) -> VsOut {
    let v = debugVertices[vertexIndex];
    var out: VsOut;
    out.clip = frame.viewProj * vec4<f32>(v.position, 1.0);
    out.color = v.color;
    out.uv = vec2<f32>(0.5, 0.5);
    return out;
}

// Six vertices per point: two triangles of a camera-facing quad.
@vertex
fn vs_points(@builtin(vertex_index) vertexIndex: u32, @builtin(instance_index) instance: u32) -> VsOut {
    let v = debugVertices[instance];
    let corners = array<vec2<f32>, 6>(vec2<f32>(-1.0, -1.0), vec2<f32>(1.0, -1.0), vec2<f32>(1.0, 1.0),
                                      vec2<f32>(-1.0, -1.0), vec2<f32>(1.0, 1.0), vec2<f32>(-1.0, 1.0));
    let corner = corners[vertexIndex];
    let half = max(v.size, 0.0001) * 0.5;
    let world = v.position + frame.cameraRight.xyz * (corner.x * half) + frame.cameraUp.xyz * (corner.y * half);
    var out: VsOut;
    out.clip = frame.viewProj * vec4<f32>(world, 1.0);
    out.color = v.color;
    out.uv = corner * 0.5 + 0.5;
    return out;
}

@fragment
fn fs_main(in: VsOut) -> @location(0) vec4<f32> {
    // Round points: discard outside the disc so dense clouds stay readable.
    let d = distance(in.uv, vec2<f32>(0.5, 0.5));
    let alpha = in.color.a * (1.0 - smoothstep(0.42, 0.5, d));
    if (alpha <= 0.002) {
        discard;
    }
    return vec4<f32>(in.color.rgb * alpha, alpha);
}
