// ADR-1862: a mesh entity's own triangle edges as lines (scene/mesh_wire.hpp has the design).
//
// One instance per triangle, eighteen vertices: three edges of one quad (two triangles) each. The vertex stage reads
// the triangle straight from the mesh's own index and vertex buffers (bound as storage), so there is no edge list.
// An interior edge belongs to two triangles that walk it in opposite directions; only the one that walks it from the
// lexicographically smaller endpoint draws it, so nothing doubles, across a chunk seam too. The quad itself is
// ADR-1073's (wire.wgsl): expanded across the projected segment, pulled toward the eye so it passes the depth test
// against the surface it lies on, antialiased over one pixel.

#include "common.wgsl"
#include "wire.wgsl"

struct MeshWireParams {
    color: vec4<f32>,  // rgb = colour (scene-linear), w = opacity
    shape: vec4<f32>,  // x width (px at 1080 lines), y fade near (m), z fade far (m), w min pixels
    crease: vec4<f32>, // x crease low (deg), y crease high (deg), z reveal (0..1), w structure intensity
    wave: vec4<f32>,   // x envelope (0..1), y radius (m), z width (m), w gain
    origin: vec4<f32>, // xyz the wave's origin (world), w the clip height (no line whose edge lies wholly below it)
};

struct MeshWireDraw {
    model: mat4x4<f32>,
    normalModel: mat4x4<f32>,
};

@group(1) @binding(0) var<uniform> mw: MeshWireParams;
@group(1) @binding(1) var<storage, read> mwVertices: array<f32>; // scene::Vertex: 8 floats (position, normal, uv)
@group(1) @binding(2) var<storage, read> mwIndices: array<u32>;
@group(1) @binding(3) var<uniform> mwDraw: MeshWireDraw;

struct MeshWireOut {
    @builtin(position) clip: vec4<f32>,
    @location(0) worldPos: vec3<f32>,
    @location(1) across: f32,
    @location(2) @interpolate(flat) widths: vec3<f32>,
    @location(3) @interpolate(flat) strength: f32,
};

struct MeshWireTargets {
    @location(0) color: vec4<f32>,
    @location(1) normalRoughness: vec4<f32>,
    @location(2) velocity: vec4<f32>,
    @location(3) emission: vec4<f32>,
    @location(4) ids: u32,
};

fn mwPosition(i: u32) -> vec3<f32> {
    let b = i * 8u;
    return (mwDraw.model * vec4<f32>(mwVertices[b], mwVertices[b + 1u], mwVertices[b + 2u], 1.0)).xyz;
}

fn mwNormal(i: u32) -> vec3<f32> {
    let b = i * 8u + 3u;
    return normalize((mwDraw.normalModel * vec4<f32>(mwVertices[b], mwVertices[b + 1u], mwVertices[b + 2u], 0.0)).xyz);
}

// Whether `a` sorts before `b` (x, then z, then y): the edge's owner is the triangle that walks it from a to b.
fn mwBefore(a: vec3<f32>, b: vec3<f32>) -> bool {
    if (a.x != b.x) { return a.x < b.x; }
    if (a.z != b.z) { return a.z < b.z; }
    return a.y < b.y;
}

fn mwCollapsed() -> MeshWireOut {
    var out: MeshWireOut;
    out.clip = vec4<f32>(0.0, 0.0, -2.0, 1.0);
    out.worldPos = vec3<f32>(0.0);
    out.across = 0.0;
    out.widths = vec3<f32>(1.0, 0.0, 0.0);
    out.strength = 0.0;
    return out;
}

// The pixel length of a segment on screen (0 when either end is behind the eye).
fn mwPixels(a: vec3<f32>, b: vec3<f32>) -> f32 {
    let ca = frame.viewProj * vec4<f32>(a, 1.0);
    let cb = frame.viewProj * vec4<f32>(b, 1.0);
    if (ca.w < 1e-3 || cb.w < 1e-3) {
        return 1e6; // crossing the eye: near, long
    }
    let half = frame.targetSize.xy * 0.5;
    return length(ca.xy / ca.w * half - cb.xy / cb.w * half);
}

// The side of a square with the triangle's screen area (pixels); large when it crosses the eye.
fn mwSize(a: vec3<f32>, b: vec3<f32>, c: vec3<f32>) -> f32 {
    let ca = frame.viewProj * vec4<f32>(a, 1.0);
    let cb = frame.viewProj * vec4<f32>(b, 1.0);
    let cc = frame.viewProj * vec4<f32>(c, 1.0);
    if (ca.w < 1e-3 || cb.w < 1e-3 || cc.w < 1e-3) {
        return 1e6;
    }
    let half = frame.targetSize.xy * 0.5;
    let sa = ca.xy / ca.w * half;
    let sb = cb.xy / cb.w * half;
    let sc = cc.xy / cc.w * half;
    let d1 = sb - sa;
    let d2 = sc - sa;
    return sqrt(abs(d1.x * d2.y - d1.y * d2.x));
}

@vertex
fn vs_mesh_wire(@builtin(vertex_index) vi: u32, @builtin(instance_index) tri: u32) -> MeshWireOut {
    let e = vi / 6u;
    let k = vi % 6u;
    var quadCorner = array<u32, 6>(0u, 1u, 2u, 2u, 1u, 3u);
    let c = quadCorner[k];
    let i0 = mwIndices[tri * 3u + e];
    let i1 = mwIndices[tri * 3u + (e + 1u) % 3u];
    let i2 = mwIndices[tri * 3u + (e + 2u) % 3u];
    let a = mwPosition(i0);
    let b = mwPosition(i1);
    if (!mwBefore(a, b)) {
        return mwCollapsed(); // the twin triangle draws it
    }
    if (min(a.y, b.y) < mw.origin.w) {
        return mwCollapsed(); // below the clip height (under water, for one)
    }
    let mid = 0.5 * (a + b);
    let dist = distance(frame.cameraPos.xyz, mid);
    let fade = 1.0 - smoothstep(mw.shape.y, max(mw.shape.z, mw.shape.y + 1e-3), dist);
    if (fade <= 0.0) {
        return mwCollapsed();
    }
    // structure: how far this face turns from the smooth surface at the edge
    let p2 = mwPosition(i2);
    let fc = cross(b - a, p2 - a);
    let fl = length(fc);
    if (fl < 1e-12) {
        return mwCollapsed();
    }
    let faceN = fc / fl;
    let na = mwNormal(i0);
    let nb = mwNormal(i1);
    // Two measures of how much the surface turns at this edge, the larger kept: the smooth normals at its ends
    // (the surface bends across it: a ridge, a lip, a fold) and the face against them (the face cuts a corner).
    let bend = degrees(acos(clamp(dot(na, nb), -1.0, 1.0)));
    let cut = degrees(acos(clamp(abs(dot(faceN, normalize(na + nb + 1e-6))), 0.0, 1.0))) * 2.0;
    let turn = max(bend, cut);
    let structure = smoothstep(mw.crease.x, max(mw.crease.y, mw.crease.x + 1e-3), turn);
    let reveal = clamp(mw.crease.z, 0.0, 1.0);
    let threshold = 1.0 - reveal;
    let shown = smoothstep(threshold - 0.25, threshold + 0.02, structure);
    // the travelling front: a ring about the origin, radius (1 - envelope) x radius
    let env = clamp(mw.wave.x, 0.0, 1.0);
    let r = length((mid - mw.origin.xyz).xz);
    let front = (1.0 - env) * mw.wave.y;
    let x = (r - front) / max(mw.wave.z, 1e-3);
    let band = exp(-x * x) * smoothstep(0.0, 0.25, env);
    // the clip height's soft edge: lines fade in over the first metres above it (a shoreline's zigzag of short
    // edges stays quiet)
    let shore = smoothstep(mw.origin.w, mw.origin.w + 3.0, min(a.y, b.y));
    var strength = (mw.crease.w * shown + mw.wave.w * band) * fade * shore * mw.color.w;
    // density: a triangle small on screen fades its edges out (its size: the side of a square of its area), so dense
    // or sliver-thin far geometry never piles into a solid luminous mass
    let px = min(mwPixels(a, b), mwSize(a, b, p2));
    strength = strength * smoothstep(mw.shape.w * 0.5, max(mw.shape.w, 1e-3), px);
    if (strength <= 1e-4) {
        return mwCollapsed();
    }
    // the quad: corners 0/1 at a, 2/3 at b (wire.wgsl's convention: y +1 at this end A)
    var corner = vec2<f32>(-1.0, 1.0);
    var pThis = a;
    var pOther = b;
    if (c == 1u) {
        corner = vec2<f32>(1.0, 1.0);
    } else if (c == 2u) {
        corner = vec2<f32>(-1.0, -1.0);
        pThis = b;
        pOther = a;
    } else if (c == 3u) {
        corner = vec2<f32>(1.0, -1.0);
        pThis = b;
        pOther = a;
    }
    let q = wireExpand(pThis, pOther, corner, mw.shape.x);
    var out: MeshWireOut;
    out.clip = q.clip;
    out.worldPos = q.worldPos;
    out.across = q.across;
    out.widths = q.widths;
    out.strength = strength;
    return out;
}

@fragment
fn fs_mesh_wire(in: MeshWireOut) -> MeshWireTargets {
    var w: WireVertexOut;
    w.clip = in.clip;
    w.worldPos = in.worldPos;
    w.across = in.across;
    w.widths = in.widths;
    let coverage = wireCoverage(w);
    // light added far away is dimmed by the surface fog, never tinted by it (ribbon.wgsl's rule)
    let transmit = applyFog(vec3<f32>(1.0), in.worldPos) - applyFog(vec3<f32>(0.0), in.worldPos);
    let radiance = mw.color.rgb * (in.strength * coverage) * transmit;
    let a = clamp(coverage * min(in.strength, 1.0), 0.0, 1.0);
    var out: MeshWireTargets;
    out.color = vec4<f32>(radiance, 0.0);
    out.normalRoughness = vec4<f32>(0.0);
    out.velocity = vec4<f32>(0.0);
    out.emission = vec4<f32>(radiance, a);
    out.ids = 0u;
    return out;
}
