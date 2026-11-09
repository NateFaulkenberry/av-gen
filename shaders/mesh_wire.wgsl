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
#include "corruption.wgsl" // ADR-1872/1873: the lines follow the corrupted rock, and the scan reads its field

struct MeshWireParams {
    color: vec4<f32>,  // rgb = colour (scene-linear), w = opacity
    shape: vec4<f32>,  // x width (px at 1080 lines), y fade near (m), z fade far (m), w min pixels
    crease: vec4<f32>, // x crease low (deg), y crease high (deg), z reveal (0..1), w structure intensity
    wave: vec4<f32>,   // x envelope (0..1), y radius (m), z width (m), w gain
    origin: vec4<f32>, // xyz the wave's origin (world), w the clip height (no line whose edge lies wholly below it)
    front: vec4<f32>,  // ADR-1872: rgb the fronts' leading colour, w = 1 when the wave grows from the corruption focus
    pulse: vec4<f32>,  // x radiance of the periodic fronts, y spacing (m), z speed (m/s), w half-width (m)
    extra: vec4<f32>,  // x focus gain, y rim gain
};

struct MeshWireDraw {
    model: mat4x4<f32>,
    normalModel: mat4x4<f32>,
    flags: vec4<f32>, // ADR-1872: x = 1 when the corruption displaces this mesh
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
    @location(4) @interpolate(flat) heat: f32, // ADR-1872: 0 the line's colour .. 1 the fronts' leading colour
};

struct MeshWireTargets {
    @location(0) color: vec4<f32>,
    @location(1) normalRoughness: vec4<f32>,
    @location(2) velocity: vec4<f32>,
    @location(3) emission: vec4<f32>,
    @location(4) ids: u32,
};

fn mwRestPosition(i: u32) -> vec3<f32> {
    let b = i * 8u;
    return (mwDraw.model * vec4<f32>(mwVertices[b], mwVertices[b + 1u], mwVertices[b + 2u], 1.0)).xyz;
}

// ADR-1872: where the lit pass draws the vertex -- displaced by the corruption when the mesh is one of its targets.
fn mwPosition(i: u32) -> vec3<f32> {
    let p = mwRestPosition(i);
    if (mwDraw.flags.x > 0.5 && crOn()) {
        return crDisplace(p, frame.params.x, 0u).p;
    }
    return p;
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
    out.heat = 0.0;
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
    let restMid = 0.5 * (mwRestPosition(i0) + mwRestPosition(i1)); // the corruption is a property of the rock's place
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
    var r = length((mid - mw.origin.xyz).xz);
    if (mw.front.w > 0.5) {
        r = distance(restMid, mw.origin.xyz); // ADR-1872: the front grows out of the focus (mw.origin)
    }
    let front = (1.0 - env) * mw.wave.y;
    let x = (r - front) / max(mw.wave.z, 1e-3);
    // the lead (outside the front) is hot, the tail cools: heat > 0 only ahead of the crest
    var band = exp(-x * x) * smoothstep(0.0, 0.25, env);
    if (mw.front.w > 0.5) {
        // a front out of the focus is born as a ring, not a disk lit all at once, and thins as it passes the camera,
        // so neither its birth nor its passing is a one-frame change of the whole picture
        // and dies away before it reaches the camera's own distance from the focus: a front sweeping over the eye at
        // the envelope's speed (hundreds of m/s) lights the whole near wall for two frames
        let camR = max(distance(frame.cameraPos.xyz, mw.origin.xyz), 1.0);
        band = band * smoothstep(0.0, 2.0 * mw.wave.z, front) * (1.0 - smoothstep(0.45, 0.8, r / camR));
    }
    var heat = band * smoothstep(-0.5, 0.8, x);
    // ADR-1872: the corruption field -- periodic fronts out of the focus, brighter and denser lines near it, and the
    // aperture's rim drawn along the rock's own edges
    var field = 0.0;
    var pulseBand = 0.0;
    var rimBand = 0.0;
    var inThroat = false;
    if (crOn()) {
        field = crField(restMid);
        if (mw.pulse.x > 0.0 && field > 0.0) {
            let rf = distance(restMid, frame.corruption0.xyz);
            let ph = fract((rf - frame.params.x * mw.pulse.z) / mw.pulse.y);
            let d = min(ph, 1.0 - ph) * mw.pulse.y;
            pulseBand = exp(-(d / mw.pulse.w) * (d / mw.pulse.w)) * smoothstep(0.0, 0.06, field) *
                        smoothstep(0.0, 3.0 * mw.pulse.w, rf) * smoothstep(20.0, 120.0, dist);
            heat = max(heat, pulseBand * smoothstep(0.0, 0.5, ph) * (1.0 - smoothstep(0.5, 1.0, ph)) * 2.0);
        }
        if (frame.corruption2.w > 0.0) {
            let ap = crAperture(restMid, crBasis());
            let wd = crReach(ap.w);
            // the opening is a void: no line inside it once it has split; its rim is drawn on the rock's own edges
            inThroat = ap.e < 0.97 && wd > 0.5 && ap.openT > 0.05;
            if (mw.extra.y > 0.0) {
                rimBand = exp(-pow((ap.e - 1.06) / 0.08, 2.0)) * wd * ap.openT;
                heat = max(heat, rimBand);
            }
        }
    }
    // the clip height's soft edge: lines fade in over the first metres above it (a shoreline's zigzag of short
    // edges stays quiet)
    let shore = smoothstep(mw.origin.w, mw.origin.w + 3.0, min(a.y, b.y));
    var strength = (mw.crease.w * shown * (1.0 + mw.extra.x * field) + mw.wave.w * band + mw.pulse.x * pulseBand +
                    mw.extra.y * rimBand) * fade * shore * mw.color.w;
    // density: a triangle small on screen fades its edges out (its size: the side of a square of its area), so dense
    // or sliver-thin far geometry never piles into a solid luminous mass
    let px = min(mwPixels(a, b), mwSize(a, b, p2));
    if (inThroat) {
        return mwCollapsed();
    }
    let minPx = mw.shape.w * (1.0 - 0.3 * clamp(field, 0.0, 1.0)); // a little denser near the focus
    strength = strength * smoothstep(minPx * 0.5, max(minPx, 1e-3), px);
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
    out.heat = clamp(heat, 0.0, 1.0);
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
    let radiance = mix(mw.color.rgb, mw.front.rgb, in.heat) * (in.strength * coverage) * transmit;
    let a = clamp(coverage * min(in.strength, 1.0), 0.0, 1.0);
    var out: MeshWireTargets;
    out.color = vec4<f32>(radiance, 0.0);
    out.normalRoughness = vec4<f32>(0.0);
    out.velocity = vec4<f32>(0.0);
    out.emission = vec4<f32>(radiance, a);
    out.ids = 0u;
    return out;
}

// ---- ADR-1872: shards -------------------------------------------------------------------------------------------
// The opening sheds its rock: a share of the triangles on its lips (mw.extra.z, at full opening) break off as free
// triangles -- read from the same buffers, de-indexed on the fly -- and are drawn in toward the focus and back into the
// throat, turning and shrinking, then gone; each on its own cycle (mw.extra.w per second), so the shedding is
// continuous and a pure function of the clock. Opaque, lit by the strongest directional light, edged in the glow.
struct ShardOut {
    @builtin(position) clip: vec4<f32>,
    @location(0) worldPos: vec3<f32>,
    @location(1) @interpolate(flat) normal: vec3<f32>,
    @location(2) bary: vec3<f32>,
    @location(3) @interpolate(flat) life: f32,
};

fn shardRotate(p: vec3<f32>, axis: vec3<f32>, angle: f32) -> vec3<f32> {
    let c = cos(angle);
    let s = sin(angle);
    return p * c + cross(axis, p) * s + axis * dot(axis, p) * (1.0 - c);
}

@vertex
fn vs_mesh_shard(@builtin(vertex_index) vi: u32, @builtin(instance_index) tri: u32) -> ShardOut {
    var out: ShardOut;
    out.clip = vec4<f32>(0.0, 0.0, -2.0, 1.0);
    out.worldPos = vec3<f32>(0.0);
    out.normal = vec3<f32>(0.0, 1.0, 0.0);
    out.bary = vec3<f32>(0.0);
    out.life = 0.0;
    if (!crOn() || mw.extra.z <= 0.0 || frame.corruption2.w <= 0.0 || mwDraw.flags.x < 0.5) {
        return out;
    }
    let r0 = mwRestPosition(mwIndices[tri * 3u]);
    let r1 = mwRestPosition(mwIndices[tri * 3u + 1u]);
    let r2 = mwRestPosition(mwIndices[tri * 3u + 2u]);
    let cR = (r0 + r1 + r2) / 3.0;
    // ADR-1904: no projectile shards. A sliver (its longest edge over six times its height on that edge) never breaks
    // off, and a shard's longest edge is capped at a share of the opening's half-width, so a large source triangle (a
    // sand drift's, a hull's box face) becomes a shard of the storm's own size, not a rod across the frame.
    let longest = max(length(r1 - r0), max(length(r2 - r1), length(r0 - r2)));
    let area2 = length(cross(r1 - r0, r2 - r0));
    if (area2 <= 1e-6 || longest * longest > 6.0 * area2) {
        return out;
    }
    let sizeCap = min(1.0, max(1.0, 0.15 * frame.corruption3.x) / longest);
    let b = crBasis();
    let ap = crAperture(cR, b);
    if (ap.e < 0.95 || ap.e > 2.4 || abs(ap.w) > 15.0 || ap.openT <= 0.0) {
        return out;
    }
    let seed = crSeed();
    let h = crHash(vec4<u32>(tri, mwIndices[tri * 3u], seed, 31u));
    let ring = 1.0 - smoothstep(1.3, 2.4, ap.e);
    // M3c (ADR-1901): Gate A's storm -- the shards stay to the crossing
    if (h > mw.extra.z * ap.openT * ring) {
        return out;
    }
    let rate = max(mw.extra.w, 0.01);
    let ph = fract(frame.params.x * rate * (0.7 + 0.6 * crHash(vec4<u32>(tri, 1u, seed, 32u))) + crHash(vec4<u32>(tri, 2u, seed, 33u)));
    // where it breaks from: the lip as drawn now
    let c0 = crDisplace(cR, frame.params.x, 0u).p;
    let focus = frame.corruption0.xyz;
    let ease = ph * ph * (3.0 - 2.0 * ph);
    // M3c (ADR-1901): Gate A's burst -- in toward the focus and back into the tunnel, a storm of gold round the
    // opening; the other world shows through behind it
    let centre = c0 + (focus - c0) * ease * 0.85 + b.ax * frame.corruption3.z * 0.7 * ph * ph;
    let axis = normalize(vec3<f32>(crHash(vec4<u32>(tri, 3u, seed, 34u)) - 0.5, crHash(vec4<u32>(tri, 4u, seed, 35u)) - 0.5,
                                   crHash(vec4<u32>(tri, 5u, seed, 36u)) - 0.5) + vec3<f32>(1e-3, 0.0, 0.0));
    let angle = ph * 3.1415927 * (1.0 + 2.0 * crHash(vec4<u32>(tri, 6u, seed, 37u)));
    let scale = (1.0 - smoothstep(0.55, 1.0, ph)) * 0.92 * sizeCap;
    var corners = array<vec3<f32>, 3>(r0, r1, r2);
    var barys = array<vec3<f32>, 3>(vec3<f32>(1.0, 0.0, 0.0), vec3<f32>(0.0, 1.0, 0.0), vec3<f32>(0.0, 0.0, 1.0));
    let k = vi % 3u;
    let local = shardRotate((corners[k] - cR) * scale, axis, angle);
    let p = centre + local;
    let fn0 = normalize(cross(r1 - r0, r2 - r0) + vec3<f32>(0.0, 1e-6, 0.0));
    // ADR-1901: a shard in the tunnel, behind the plane the destination is composited on, is still drawn over the
    // glimpse (Gate A's storm, with the other world behind it): its depth is pulled toward the eye, along its own ray
    // (so it stays where it is on screen), to just in front of that plane
    let margin = max(1.0, 0.3 * frame.corruption3.x);
    let behind = dot(p - focus, b.ax) + margin + 0.5;
    var pd = p;
    if (behind > 0.0) {
        let toEye = frame.cameraPos.xyz - p;
        let dist = length(toEye);
        pd = p + toEye / max(dist, 1e-3) * min(behind / max(abs(dot(normalize(toEye), b.ax)), 0.2), dist * 0.95);
    }
    out.clip = frame.viewProj * vec4<f32>(pd, 1.0);
    out.worldPos = p;
    out.normal = shardRotate(fn0, axis, angle);
    out.bary = barys[k];
    out.life = ph;
    return out;
}

@fragment
fn fs_mesh_shard(in: ShardOut, @builtin(front_facing) frontFacing: bool) -> MeshWireTargets {
    var n = normalize(in.normal);
    if (!frontFacing) {
        n = -n;
    }
    // the strongest directional light
    var lit = vec3<f32>(0.0);
    let directional = u32(frame.lightCounts.x + 0.5);
    for (var i = 0u; i < min(directional, 8u); i = i + 1u) {
        let l = frame.lights[i];
        lit = lit + l.colorIntensity.rgb * max(dot(n, -l.directionRange.xyz), 0.0);
    }
    let rock = vec3<f32>(0.32, 0.17, 0.10);
    let edge = 1.0 - smoothstep(0.0, 0.08, min(in.bary.x, min(in.bary.y, in.bary.z)));
    let glow = frame.corruption8.xyz * frame.corruption7.z * 0.6 * edge * (0.3 + 0.7 * in.life);
    let colour = applyFog(rock * (lit * 0.6 + vec3<f32>(0.03)) + glow, in.worldPos);
    var out: MeshWireTargets;
    out.color = vec4<f32>(colour, 1.0);
    out.normalRoughness = packNormalRoughness(n, 0.9, 0.0);
    out.velocity = vec4<f32>(0.0);
    out.emission = vec4<f32>(glow, min(length(glow), 1.0));
    out.ids = 0u;
    return out;
}
