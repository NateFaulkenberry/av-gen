// Signed distance fields on the GPU (ADR-027): the interpreter of the packed node program that
// spatial::packSdfTree emits and spatial::evaluatePacked runs on the CPU (src/spatial/sdf.cpp).
// Same formulas, same operation order, same stack discipline, so the two agree within float
// rounding (tests/rendering/test_sdf_gpu.cpp compares them within 1e-4, noise 1e-3).
//
// The including module declares the bindings itself, e.g.
//   @group(1) @binding(2) var<storage, read> sdfNodes: array<SdfNodeGpu>;
//   @group(1) @binding(3) var<uniform> fieldBlock: FieldBlock;
// This file includes fields.wgsl (which includes noise.wgsl); do not include either again.
//
// Program (records sdfNodes[offset .. offset + count)): a linear post-order program executed with
//   dist[8]  distance stack (sp entries), pts[8] saved points (pp entries), cur = current point.
//   primitive     childCount == 0       push d(kind, cur)
//   combination   childCount == n       n == 0: push FAR; else fold the top n entries in order
//                                       (d = dist[sp-n]; d = op(d, dist[sp-n+j])), drop them, push d
//   unary BEGIN   childCount == 0xFFFF  pts[pp++] = cur; domain ops warp cur (scale divides)
//   unary END     childCount == 1       cur = pts[--pp]; scale multiplies dist[sp-1] by scale,
//                                       displacements add their term at cur, others do nothing
// Stack over/underflow returns FAR (1e9), as the CPU does. Record layout: kind, childCount,
// fieldSlot, seed; p0 = (radius, height, rounding, offset); p1 = (size.xyz, scale); p2 =
// (axis.xyz, amount); p3 = (translation.xyz, smooth); p4 = forward rotation quaternion (xyzw) for
// rotate, else (frequency, 0, 0, 0); p5 = (frequency, speed, float(count), 0).
//
// DisplaceField samples fieldScalar(slot, worldPoint): fields live in world space, so the local
// point is taken to world with the object's localToWorld matrix (identity on the CPU/mesh path,
// which samples at the tree-local point).
#include "fields.wgsl"

struct SdfNodeGpu {
    kind: u32,
    childCount: u32,
    fieldSlot: i32,
    seed: u32,
    p0: vec4<f32>,
    p1: vec4<f32>,
    p2: vec4<f32>,
    p3: vec4<f32>,
    p4: vec4<f32>,
    p5: vec4<f32>,
};

const SDF_SPHERE: u32 = 0u;
const SDF_BOX: u32 = 1u;
const SDF_ROUNDED_BOX: u32 = 2u;
const SDF_CYLINDER: u32 = 3u;
const SDF_CAPSULE: u32 = 4u;
const SDF_TORUS: u32 = 5u;
const SDF_PLANE: u32 = 6u;
const SDF_CONE: u32 = 7u;
const SDF_UNION: u32 = 8u;
const SDF_INTERSECTION: u32 = 9u;
const SDF_DIFFERENCE: u32 = 10u;
const SDF_SMOOTH_UNION: u32 = 11u;
const SDF_SMOOTH_INTERSECTION: u32 = 12u;
const SDF_SMOOTH_DIFFERENCE: u32 = 13u;
const SDF_MORPH: u32 = 14u;          // ADR-1001
const SDF_TRANSLATE: u32 = 15u;
const SDF_ROTATE: u32 = 16u;
const SDF_SCALE: u32 = 17u;
const SDF_TWIST: u32 = 18u;
const SDF_BEND: u32 = 19u;
const SDF_REPEAT: u32 = 20u;
const SDF_POLAR_REPEAT: u32 = 21u;
const SDF_MIRROR: u32 = 22u;
const SDF_FOLD: u32 = 23u;           // ADR-1001
const SDF_RECURSE: u32 = 24u;        // ADR-1001
const SDF_DISPLACE_NOISE: u32 = 25u;
const SDF_DISPLACE_VORONOI: u32 = 26u;
const SDF_DISPLACE_WAVE: u32 = 27u;
const SDF_DISPLACE_FIELD: u32 = 28u;
const SDF_STAIRS: u32 = 29u;         // ADR-1040 (a primitive, appended)
const SDF_SCREW: u32 = 30u;          // ADR-1040
const SDF_WARP: u32 = 31u;           // ADR-1040

const SDF_FAR: f32 = 1e9;
const SDF_BEGIN: u32 = 0xFFFFu;
const SDF_STACK: u32 = 8u;
const SDF_LOOPS: u32 = 2u;           // nested Recurse nodes (spatial::kMaxSdfLoops)
const SDF_TWO_PI: f32 = 6.283185307179586;

// ---- primitives (Quilez exact distances) ----------------------------------------------------------

fn sdfVmax(v: vec3<f32>) -> f32 {
    return max(v.x, max(v.y, v.z));
}

fn sdfSphere(p: vec3<f32>, r: f32) -> f32 {
    return length(p) - r;
}

fn sdfBox(p: vec3<f32>, b: vec3<f32>) -> f32 {
    let q = abs(p) - b;
    return length(max(q, vec3<f32>(0.0))) + min(sdfVmax(q), 0.0);
}

fn sdfRoundBox(p: vec3<f32>, b: vec3<f32>, r: f32) -> f32 {
    let q = abs(p) - b + r;
    return length(max(q, vec3<f32>(0.0))) + min(sdfVmax(q), 0.0) - r;
}

fn sdfCappedCylinder(p: vec3<f32>, r: f32, h: f32) -> f32 {
    let d = abs(vec2<f32>(length(p.xz), p.y)) - vec2<f32>(r, h);
    return min(max(d.x, d.y), 0.0) + length(max(d, vec2<f32>(0.0)));
}

fn sdfCapsule(p: vec3<f32>, r: f32, h: f32) -> f32 {
    let q = vec3<f32>(p.x, p.y - clamp(p.y, -h, h), p.z);
    return length(q) - r;
}

fn sdfTorus(p: vec3<f32>, major: f32, minor: f32) -> f32 {
    let q = vec2<f32>(length(p.xz) - major, p.y);
    return length(q) - minor;
}

// normalize() with the CPU's +Y fallback for a zero vector.
fn sdfSafeNormalize(v: vec3<f32>) -> vec3<f32> {
    let len = length(v);
    if (len > 1e-8) {
        return v / len;
    }
    return vec3<f32>(0.0, 1.0, 0.0);
}

fn sdfPlane(p: vec3<f32>, axis: vec3<f32>, offset: f32) -> f32 {
    return dot(p, sdfSafeNormalize(axis)) - offset;
}

// Quilez sdCone (exact), apex at +height/2, base of `radius` at -height/2.
fn sdfCone(p: vec3<f32>, radius: f32, height: f32) -> f32 {
    let q = vec2<f32>(radius, -height);
    let w = vec2<f32>(length(p.xz), p.y - height * 0.5);
    let qq = max(dot(q, q), 1e-12);
    let a = w - q * clamp(dot(w, q) / qq, 0.0, 1.0);
    let b = w - q * vec2<f32>(clamp(w.x / max(q.x, 1e-6), 0.0, 1.0), 1.0);
    let k = sign(q.y);
    let d = min(dot(a, a), dot(b, b));
    let s = max(k * (w.x * q.y - w.y * q.x), k * (w.y - q.y));
    return sqrt(d) * sign(s);
}

// ADR-1040: spatial::sdStairs, the same expressions in the same order.
fn sdfStairs(p: vec3<f32>, size: vec3<f32>, thickness: f32, count: i32) -> f32 {
    let run = max(size.x, 1e-4);
    let rise = max(size.y, 1e-4);
    let steps = f32(max(count, 1));
    let u = (p.x * run + p.y * rise) / (run * run + rise * rise);
    let k0 = floor(u);
    var best = SDF_FAR;
    for (var j = -1; j <= 1; j = j + 1) {
        let i = k0 + f32(j);
        let riser = vec2<f32>(i * run, clamp(p.y, i * rise, (i + 1.0) * rise));
        let tread = vec2<f32>(clamp(p.x, i * run, (i + 1.0) * run), (i + 1.0) * rise);
        best = min(best, min(length(p.xy - riser), length(p.xy - tread)));
    }
    let top = rise * (floor(p.x / run) + 1.0);
    let zig = select(best, -best, p.y < top);
    let slope = rise / run;
    let under = select(-p.y, (slope * p.x - p.y - thickness) / sqrt(1.0 + slope * slope), thickness > 0.0);
    let ends = max(-p.x, p.x - steps * run);
    let d2 = max(zig, max(ends, under));
    let dz = abs(p.z) - size.z;
    return min(max(d2, dz), 0.0) + length(max(vec2<f32>(d2, dz), vec2<f32>(0.0)));
}

fn sdfPrimitive(n: SdfNodeGpu, p: vec3<f32>) -> f32 {
    let kind = n.kind;
    if (kind == SDF_SPHERE) {
        return sdfSphere(p, n.p0.x);
    }
    if (kind == SDF_BOX) {
        return sdfBox(p, n.p1.xyz);
    }
    if (kind == SDF_ROUNDED_BOX) {
        return sdfRoundBox(p, n.p1.xyz, n.p0.z);
    }
    if (kind == SDF_CYLINDER) {
        return sdfCappedCylinder(p, n.p0.x, n.p0.y * 0.5);
    }
    if (kind == SDF_CAPSULE) {
        return sdfCapsule(p, n.p0.x, n.p0.y * 0.5);
    }
    if (kind == SDF_TORUS) {
        return sdfTorus(p, n.p0.x, n.p0.z);
    }
    if (kind == SDF_PLANE) {
        return sdfPlane(p, n.p2.xyz, n.p0.w);
    }
    if (kind == SDF_CONE) {
        return sdfCone(p, n.p0.x, n.p0.y);
    }
    if (kind == SDF_STAIRS) {
        return sdfStairs(p, n.p1.xyz, n.p0.y, i32(n.p5.z));
    }
    return SDF_FAR;
}

// ---- combinations --------------------------------------------------------------------------------

fn sdfSmin(a: f32, b: f32, k: f32) -> f32 {
    let h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
    return (b * (1.0 - h) + a * h) - k * h * (1.0 - h);
}

// `smoothK` (not `smooth`: a WGSL reserved word) is the node's `smooth` payload, p3.w.
fn sdfCombine(kind: u32, d: f32, c: f32, smoothK: f32) -> f32 {
    let k = max(smoothK, 1e-4);
    if (kind == SDF_UNION) {
        return min(d, c);
    }
    if (kind == SDF_INTERSECTION) {
        return max(d, c);
    }
    if (kind == SDF_DIFFERENCE) {
        return max(d, -c);
    }
    if (kind == SDF_SMOOTH_UNION) {
        return sdfSmin(d, c, k);
    }
    if (kind == SDF_SMOOTH_INTERSECTION) {
        return -sdfSmin(-d, -c, k);
    }
    if (kind == SDF_SMOOTH_DIFFERENCE) {
        return -sdfSmin(-d, c, k);
    }
    if (kind == SDF_MORPH) {
        return d + (c - d) * smoothK; // the packer stored this fold's weight in `smooth`
    }
    return d;
}

// ---- domain operations ---------------------------------------------------------------------------

// floor(x + 0.5) on both paths (WGSL round() is half-to-even).
fn sdfRnd(x: f32) -> f32 {
    return floor(x + 0.5);
}

// glm: q * v for a unit quaternion (x, y, z, w).
fn sdfQuatRotate(q: vec4<f32>, v: vec3<f32>) -> vec3<f32> {
    let uv = cross(q.xyz, v);
    let uuv = cross(q.xyz, uv);
    return v + ((uv * q.w) + uuv) * 2.0;
}

// The child's point of a domain op (displacements leave p unchanged).
fn sdfWarpTwist(n: SdfNodeGpu, p: vec3<f32>) -> vec3<f32> {
    let angle = n.p2.w * p.y;
    let c = cos(angle);
    let s = sin(angle);
    return vec3<f32>(c * p.x + s * p.z, p.y, -s * p.x + c * p.z);
}

fn sdfWarpBend(n: SdfNodeGpu, p: vec3<f32>) -> vec3<f32> {
    let c = cos(n.p2.w * p.x);
    let s = sin(n.p2.w * p.x);
    return vec3<f32>(c * p.x - s * p.y, s * p.x + c * p.y, p.z);
}

fn sdfWarpRepeat(n: SdfNodeGpu, p: vec3<f32>) -> vec3<f32> {
    let size = n.p1.xyz;
    let count = i32(n.p5.z);
    let limit = f32(count);
    var q = p;
    for (var i = 0u; i < 3u; i = i + 1u) {
        if (size[i] > 0.0) {
            var cell = sdfRnd(p[i] / size[i]);
            if (count > 0) {
                cell = clamp(cell, -limit, limit);
            }
            q[i] = p[i] - size[i] * cell;
        }
    }
    return q;
}

fn sdfWarpPolar(n: SdfNodeGpu, p: vec3<f32>) -> vec3<f32> {
    let count = i32(n.p5.z);
    if (count <= 0) {
        return p;
    }
    let sector = SDF_TWO_PI / f32(count);
    let r = length(p.xz);
    // atan2(0, 0) is undefined in WGSL (NaN on Metal) but +0 in std::atan2: match the CPU.
    let a = select(atan2(p.z, p.x), 0.0, r == 0.0);
    let a2 = a - sector * sdfRnd(a / sector);
    return vec3<f32>(cos(a2) * r, p.y, sin(a2) * r);
}

fn sdfWarpMirror(n: SdfNodeGpu, p: vec3<f32>) -> vec3<f32> {
    let m = n.p1.xyz;
    return vec3<f32>(select(p.x, abs(p.x), m.x > 0.0), select(p.y, abs(p.y), m.y > 0.0),
                     select(p.z, abs(p.z), m.z > 0.0));
}

fn sdfWarpFold(n: SdfNodeGpu, p: vec3<f32>) -> vec3<f32> {
    let axis = sdfSafeNormalize(n.p2.xyz);
    return p - 2.0 * min(dot(p, axis) - n.p0.w, 0.0) * axis;
}

// ADR-1040: spatial::screwPoint.
fn sdfWarpScrew(n: SdfNodeGpu, p: vec3<f32>) -> vec3<f32> {
    let count = i32(n.p5.z);
    let t = n.p3.xyz;
    if (count <= 0) {
        let len2 = dot(t, t);
        if (len2 < 1e-12) {
            return p;
        }
        let k = sdfRnd(dot(p, t) / len2);
        return p - k * t;
    }
    let cells = f32(count);
    let sector = SDF_TWO_PI / cells;
    let r = length(p.xz);
    let a = select(atan2(p.z, p.x), 0.0, r == 0.0);
    let k0 = sdfRnd(a / sector);
    let rise = t.y;
    let turn = cells * rise;
    let w = select(0.0, sdfRnd((p.y - k0 * rise) / turn), abs(turn) > 1e-6);
    let a2 = a - sector * k0;
    return vec3<f32>(cos(a2) * r, p.y - (k0 + cells * w) * rise, sin(a2) * r);
}

// ADR-1040: the warp, p + amount * size * (vector value noise * 2 - 1) at p * frequency + translation.
fn sdfWarpNoise(n: SdfNodeGpu, p: vec3<f32>) -> vec3<f32> {
    let x = p * n.p5.x + n.p3.xyz;
    let w = vec3<f32>(valueNoise(x, n.seed), valueNoise(x + vec3<f32>(31.7), n.seed),
                      valueNoise(x + vec3<f32>(67.3), n.seed));
    return p + n.p2.w * n.p1.xyz * (w * 2.0 - 1.0);
}

fn sdfWarp(n: SdfNodeGpu, p: vec3<f32>) -> vec3<f32> {
    let kind = n.kind;
    if (kind == SDF_TRANSLATE) {
        return p - n.p3.xyz;
    }
    if (kind == SDF_ROTATE) {
        return sdfQuatRotate(vec4<f32>(-n.p4.xyz, n.p4.w), p); // conj(R) * p
    }
    if (kind == SDF_SCALE) {
        return p / n.p1.w;
    }
    if (kind == SDF_TWIST) {
        return sdfWarpTwist(n, p);
    }
    if (kind == SDF_BEND) {
        return sdfWarpBend(n, p);
    }
    if (kind == SDF_REPEAT) {
        return sdfWarpRepeat(n, p);
    }
    if (kind == SDF_POLAR_REPEAT) {
        return sdfWarpPolar(n, p);
    }
    if (kind == SDF_MIRROR) {
        return sdfWarpMirror(n, p);
    }
    if (kind == SDF_FOLD) {
        return sdfWarpFold(n, p);
    }
    if (kind == SDF_SCREW) {
        return sdfWarpScrew(n, p);
    }
    if (kind == SDF_WARP) {
        return sdfWarpNoise(n, p);
    }
    return p; // SDF_RECURSE: level 0 is the node's own point
}

// ADR-1001: one Recurse level, conj(R) * fold(p) * scale - translation (spatial::recurseStep).
fn sdfRecurseStep(n: SdfNodeGpu, p: vec3<f32>) -> vec3<f32> {
    let m = n.p1.xyz;
    var q = vec3<f32>(select(p.x, abs(p.x), m.x > 0.0), select(p.y, abs(p.y), m.y > 0.0),
                      select(p.z, abs(p.z), m.z > 0.0));
    q = sdfQuatRotate(vec4<f32>(-n.p4.xyz, n.p4.w), q);
    return q * n.p1.w - n.p3.xyz;
}

// ---- displacements (the END half of a unary op) --------------------------------------------------

// `p` is the op's own local point; `world` takes it to world space for field sampling.
fn sdfFinishUnaryNoise(n: SdfNodeGpu, d: f32, p: vec3<f32>, t: f32, world: mat4x4<f32>) -> f32 {
    return d + n.p2.w * (fbm3(p * n.p5.x + vec3<f32>(n.p5.y * t), n.seed) * 2.0 - 1.0);
}

fn sdfFinishUnaryVoronoi(n: SdfNodeGpu, d: f32, p: vec3<f32>, t: f32, world: mat4x4<f32>) -> f32 {
    return d + n.p2.w * (voronoiF1(p * n.p5.x, n.seed) - 0.5);
}

fn sdfFinishUnaryWave(n: SdfNodeGpu, d: f32, p: vec3<f32>, t: f32, world: mat4x4<f32>) -> f32 {
    return d + n.p2.w * sin(dot(p, n.p2.xyz) * n.p5.x + n.p5.y * t);
}

fn sdfFinishUnaryField(n: SdfNodeGpu, d: f32, p: vec3<f32>, t: f32, world: mat4x4<f32>) -> f32 {
    if (n.fieldSlot < 0) {
        return d;
    }
    let wp = (world * vec4<f32>(p, 1.0)).xyz;
    return d + n.p2.w * fieldScalar(n.fieldSlot, wp);
}

// ADR-1040: spatial::screwSeam -- cap the distance at the cell boundary plus `offset` (p0.w).
fn sdfFinishScrew(n: SdfNodeGpu, d: f32, p: vec3<f32>) -> f32 {
    let margin = n.p0.w;
    if (!(margin > 0.0)) {
        return d;
    }
    let count = i32(n.p5.z);
    let t = n.p3.xyz;
    var boundary = SDF_FAR;
    if (count <= 0) {
        let len2 = dot(t, t);
        if (len2 < 1e-12) {
            return d;
        }
        let u = dot(p, t) / len2;
        boundary = (0.5 - abs(u - sdfRnd(u))) * sqrt(len2);
    } else {
        let sector = SDF_TWO_PI / f32(count);
        let r = length(p.xz);
        let a = select(atan2(p.z, p.x), 0.0, r == 0.0);
        let k0 = sdfRnd(a / sector);
        boundary = r * sin(max(0.5 * sector - abs(a - sector * k0), 0.0));
        let rise = t.y;
        let turn = f32(count) * rise;
        if (abs(turn) > 1e-6) {
            let v = (p.y - k0 * rise) / turn;
            boundary = min(boundary, (0.5 - abs(v - sdfRnd(v))) * abs(turn));
        }
    }
    return min(d, max(boundary, 0.0) + margin);
}

fn sdfFinishUnary(n: SdfNodeGpu, d: f32, p: vec3<f32>, t: f32, world: mat4x4<f32>) -> f32 {
    let kind = n.kind;
    if (kind == SDF_SCALE) {
        return d * n.p1.w;
    }
    if (kind == SDF_SCREW) {
        return sdfFinishScrew(n, d, p);
    }
    if (kind == SDF_DISPLACE_NOISE) {
        return sdfFinishUnaryNoise(n, d, p, t, world);
    }
    if (kind == SDF_DISPLACE_VORONOI) {
        return sdfFinishUnaryVoronoi(n, d, p, t, world);
    }
    if (kind == SDF_DISPLACE_WAVE) {
        return sdfFinishUnaryWave(n, d, p, t, world);
    }
    if (kind == SDF_DISPLACE_FIELD) {
        return sdfFinishUnaryField(n, d, p, t, world);
    }
    return d;
}

// ---- the interpreter -----------------------------------------------------------------------------

// Distance of the program sdfNodes[offset .. offset + count) at the tree-local point p, time t.
// `world` = the object's local-to-world matrix (field displacements only).
fn sdfEvaluate(offset: u32, count: u32, p: vec3<f32>, t: f32, world: mat4x4<f32>) -> f32 {
    var dist: array<f32, 8>;
    var pts: array<vec3<f32>, 8>;
    // ADR-1001: Recurse loop frames (level, running union, 1 / scale^level).
    var loopLevel: array<i32, 2>;
    var loopAcc: array<f32, 2>;
    var loopInv: array<f32, 2>;
    var sp = 0u;
    var pp = 0u;
    var lp = 0u;
    var cur = p;
    let total = arrayLength(&sdfNodes);
    for (var i = 0u; i < count; i = i + 1u) {
        let index = offset + i;
        if (index >= total) {
            return SDF_FAR;
        }
        let n = sdfNodes[index];
        let kind = n.kind;
        if (kind <= SDF_CONE || kind == SDF_STAIRS) {
            if (sp >= SDF_STACK) {
                return SDF_FAR;
            }
            dist[sp] = sdfPrimitive(n, cur);
            sp = sp + 1u;
        } else if (kind <= SDF_MORPH) {
            let children = n.childCount;
            if (children == 0u) {
                if (sp >= SDF_STACK) {
                    return SDF_FAR;
                }
                dist[sp] = SDF_FAR;
                sp = sp + 1u;
                continue;
            }
            if (children > sp) {
                return SDF_FAR;
            }
            let base = sp - children;
            var d = dist[base];
            for (var j = 1u; j < children; j = j + 1u) {
                d = sdfCombine(kind, d, dist[base + j], n.p3.w);
            }
            sp = base;
            dist[sp] = d;
            sp = sp + 1u;
        } else if (n.childCount == SDF_BEGIN) {
            if (pp >= SDF_STACK) {
                return SDF_FAR;
            }
            pts[pp] = cur;
            pp = pp + 1u;
            if (kind == SDF_RECURSE) {
                if (lp >= SDF_LOOPS) {
                    return SDF_FAR;
                }
                loopLevel[lp] = 0;
                loopAcc[lp] = SDF_FAR;
                loopInv[lp] = 1.0;
                lp = lp + 1u;
            }
            cur = sdfWarp(n, cur);
        } else if (kind == SDF_RECURSE) {
            // END of a level: `cur` is this level's point again (the subtree restored it).
            if (lp == 0u || sp == 0u || pp == 0u) {
                return SDF_FAR;
            }
            let f = lp - 1u;
            sp = sp - 1u;
            loopAcc[f] = min(loopAcc[f], dist[sp] * loopInv[f]);
            if (loopLevel[f] < i32(n.p5.z) && n.fieldSlot >= 0 && u32(n.fieldSlot) < i) {
                loopLevel[f] = loopLevel[f] + 1;
                cur = sdfRecurseStep(n, cur);
                loopInv[f] = loopInv[f] / n.p1.w;
                i = u32(n.fieldSlot); // the loop's increment resumes after BEGIN
                continue;
            }
            pp = pp - 1u;
            cur = pts[pp];
            dist[sp] = loopAcc[f];
            sp = sp + 1u;
            lp = lp - 1u;
        } else {
            if (pp == 0u || sp == 0u) {
                return SDF_FAR;
            }
            pp = pp - 1u;
            cur = pts[pp];
            dist[sp - 1u] = sdfFinishUnary(n, dist[sp - 1u], cur, t, world);
        }
    }
    if (sp > 0u) {
        return dist[sp - 1u];
    }
    return SDF_FAR;
}

// Tetrahedron-difference normal (four evaluations), +Y when degenerate; matches SdfTree::normal.
fn sdfNormal(offset: u32, count: u32, p: vec3<f32>, t: f32, world: mat4x4<f32>, eps: f32) -> vec3<f32> {
    let k0 = vec3<f32>(1.0, -1.0, -1.0);
    let k1 = vec3<f32>(-1.0, -1.0, 1.0);
    let k2 = vec3<f32>(-1.0, 1.0, -1.0);
    let k3 = vec3<f32>(1.0, 1.0, 1.0);
    let n = k0 * sdfEvaluate(offset, count, p + k0 * eps, t, world) +
            k1 * sdfEvaluate(offset, count, p + k1 * eps, t, world) +
            k2 * sdfEvaluate(offset, count, p + k2 * eps, t, world) +
            k3 * sdfEvaluate(offset, count, p + k3 * eps, t, world);
    return sdfSafeNormalize(n);
}
