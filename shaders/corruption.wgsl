// ADR-1873: CORRUPTION, the world-space data mosh (scene/corruption.hpp has the design). Included by pbr.wgsl (the
// entity vertex stage, its shading and its depth passes) and mesh_wire.wgsl (the lines follow the same rock). The
// CPU twin is `scene::corruptPoint` (src/scene/corruption.cpp): keep the two in step.
//
// frame.corruption0..8 (scene::packCorruption):
//   0 focus.xyz, on        1 amount, radius, inner, previous time   2 axis.xyz (into the rock), opening
//   3 width, height, depth, seam       4 block, share, slip, rate     5 strip, stripSlip, slide, pull
//   6 lift, fold, lag, seed            7 glyph, glyphSize, glow, echoOffset    8 glowColor.rgb, echoes

// pcg4d (Jarzynski and Olano 2020): four integers in, one uniform float out. The CPU twin does the same u32 arithmetic.
fn crHash(a: vec4<u32>) -> f32 {
    var v = a * 1664525u + 1013904223u;
    v.x = v.x + v.y * v.w;
    v.y = v.y + v.z * v.x;
    v.z = v.z + v.x * v.y;
    v.w = v.w + v.y * v.z;
    v = v ^ (v >> vec4<u32>(16u));
    v.x = v.x + v.y * v.w;
    v.y = v.y + v.z * v.x;
    v.z = v.z + v.x * v.y;
    v.w = v.w + v.y * v.z;
    return f32(v.x >> 8u) / 16777216.0;
}

fn crOn() -> bool {
    return frame.corruption0.w > 0.5;
}

fn crField(p: vec3<f32>) -> f32 {
    let r = frame.corruption1.y;
    return frame.corruption1.x * (1.0 - smoothstep(r * frame.corruption1.z, r, distance(p, frame.corruption0.xyz)));
}

struct CrBasis {
    ax: vec3<f32>, // into the rock
    h: vec3<f32>,  // along the wall
    v: vec3<f32>,  // the aperture's up
};

fn crBasis() -> CrBasis {
    let ax = normalize(frame.corruption2.xyz);
    var h = cross(ax, vec3<f32>(0.0, 1.0, 0.0));
    if (dot(h, h) < 1e-6) {
        h = vec3<f32>(1.0, 0.0, 0.0);
    }
    h = normalize(h);
    return CrBasis(ax, h, cross(h, ax));
}

fn crSeed() -> u32 {
    return bitcast<u32>(i32(frame.corruption6.w));
}

// The aperture: e < 1 inside, the opening's two phases (seam growth, then the split).
struct CrAperture {
    e: f32,
    seamT: f32,
    openT: f32,
    u: f32,
    v: f32,
    w: f32,
};

fn crAperture(p: vec3<f32>, b: CrBasis) -> CrAperture {
    let o = frame.corruption2.w;
    let q = p - frame.corruption0.xyz;
    let u = dot(q, b.h);
    let v = dot(q, b.v);
    let w = dot(q, b.ax);
    let seam = max(frame.corruption3.w, 1e-3);
    let seamT = smoothstep(0.0, seam, o);
    let openT = smoothstep(seam, 1.0, o);
    let a = frame.corruption3.x * mix(0.08, 1.0, seamT);
    let hh = mix(0.35, frame.corruption3.y, openT);
    // torn: a smooth wander of the edge around the opening, plus a small step per 2.5 m cell of the wall (blocky)
    let c = vec2<i32>(floor(vec2<f32>(u, v) / 2.5));
    let ang = atan2(v / max(hh, 1e-3), u / max(a, 1e-3));
    let wander = 0.10 * sin(ang * 3.0 + 1.3) + 0.06 * sin(ang * 7.0 + 4.1) + 0.04 * sin(ang * 13.0 + 2.2);
    let rag = 1.0 + wander + 0.07 * (crHash(vec4<u32>(bitcast<u32>(c.x), bitcast<u32>(c.y), crSeed(), 11u)) - 0.5);
    let e = length(vec2<f32>(u / a, v / hh)) * rag;
    return CrAperture(e, seamT, openT, u, v, w);
}

// How far from the opening's plane its tear reaches: 10..30 m behind it (into the rock), and in front of it a third of
// `front` .. `front` (Gate C 2: a sloped wall's foot stands in front of a vertical plane and must not be torn)
fn crReach(w: f32) -> f32 {
    if (w >= 0.0) {
        return 1.0 - smoothstep(10.0, 30.0, w);
    }
    let f = max(frame.corruption9.y, 0.5);
    return 1.0 - smoothstep(f / 3.0, f, -w);
}

struct CrPoint {
    p: vec3<f32>,
    uvShift: f32,
    keep: bool,
};

// Where a target vertex at `p0` (world) is drawn at time `t`. `echo` 0 is the surface; k >= 1 an echo copy, which
// keeps only the slipped blocks, trailing k / (echoes + 1) of the way back toward where they were.
fn crDisplace(p0: vec3<f32>, t: f32, echo: u32) -> CrPoint {
    var out = CrPoint(p0, 0.0, echo == 0u);
    if (!crOn()) {
        return out;
    }
    let b = crBasis();
    let seed = crSeed();
    var off = vec3<f32>(0.0);
    var keepOfPull = 1.0;
    // ---- blocks ----
    let B = max(frame.corruption4.x, 0.25);
    let cell = vec3<i32>(floor(p0 / B));
    let cu = bitcast<vec3<u32>>(cell);
    let cc = (vec3<f32>(cell) + 0.5) * B;
    let fc = crField(cc);
    if (fc > 0.0) {
        let ph = t * frame.corruption4.w + crHash(vec4<u32>(cu, seed + 1u));
        let ep = floor(ph);
        let fr = ph - ep;
        let epu = bitcast<u32>(i32(ep));
        let key = vec4<u32>(cu.x, cu.y, cu.z ^ (epu * 747796405u), seed);
        if (crHash(key + vec4<u32>(0u, 0u, 0u, 2u)) < fc * frame.corruption4.y) {
            let env = smoothstep(0.0, 0.25, fr) * (1.0 - smoothstep(0.6, 1.0, fr));
            let r1 = crHash(key + vec4<u32>(0u, 0u, 0u, 3u));
            let r2 = crHash(key + vec4<u32>(0u, 0u, 0u, 4u));
            let r3 = crHash(key + vec4<u32>(0u, 0u, 0u, 5u));
            let r4 = crHash(key + vec4<u32>(0u, 0u, 0u, 6u));
            let dir = normalize(b.h * (2.0 * r1 - 1.0) + b.v * (r2 - 0.5) * 0.8 + vec3<f32>(1e-4, 0.0, 0.0));
            var slip = (dir * frame.corruption4.z - b.ax * frame.corruption6.x * r3) * fc * env;
            if (echo > 0u) {
                let k = f32(echo) / (frame.corruption8.w + 1.0);
                slip = slip * (1.0 - k * frame.corruption7.w);
                out.keep = env > 0.02;
            }
            off = off + slip;
            out.uvShift = (2.0 * r4 - 1.0) * frame.corruption5.z * fc * env;
        }
        if (crHash(vec4<u32>(cu, seed + 7u)) < fc * 0.3) {
            keepOfPull = 1.0 - frame.corruption6.z; // a frozen cell: stale, lagging its neighbours
        }
    }
    // ---- strips, pull and fold ----
    let f = crField(p0);
    if (f > 0.0) {
        let s = i32(floor(dot(p0, b.v) / max(frame.corruption5.x, 0.25)));
        let hs = crHash(vec4<u32>(bitcast<u32>(s), 0u, seed, 9u));
        off = off + b.h * frame.corruption5.y * f * (2.0 * hs - 1.0) * (0.6 + 0.4 * sin(t * 0.5 + hs * 6.2831853)) *
                        keepOfPull;
        let toF = frame.corruption0.xyz - p0;
        let dl = length(toF);
        // drawn toward the opening along the wall (not into a point): strongest a little outside the lips, nothing
        // inside them, so the opening stays a shape and never collapses into a star
        let q = p0 - frame.corruption0.xyz;
        let eFull = length(vec2<f32>(dot(q, b.h) / frame.corruption3.x, dot(q, b.v) / frame.corruption3.y));
        let ring = smoothstep(1.4, 2.2, eFull) * (1.0 - smoothstep(4.0, 9.0, eFull));
        let tang = toF - b.ax * dot(toF, b.ax);
        let pull = min(frame.corruption5.w * f * f * ring, 0.3 * dl);
        off = off + tang / max(length(tang), 1e-3) * pull;
        off = off + b.ax * frame.corruption6.y * f * sin(dl * 0.15 - t * 0.7);
    }
    // ---- the aperture (the surface only: an echo is a stale copy of a slipped block, kept clear of the opening) ----
    if (echo > 0u) {
        let q = p0 - frame.corruption0.xyz;
        let eFull = length(vec2<f32>(dot(q, b.h) / frame.corruption3.x, dot(q, b.v) / frame.corruption3.y));
        out.keep = out.keep && eFull > 1.6;
    } else if (frame.corruption2.w > 0.0) {
        let ap = crAperture(p0, b);
        let wd = crReach(ap.w);
        if (wd > 0.0 && ap.openT > 0.0) {
            let e = max(ap.e, 1e-3);
            if (ap.e < 1.0) {
                // the cut's own wall: the band just inside the edge folds back into a short tunnel through the rock
                // (`depth` m); inside it the rock is cut away (crSurface)
                let band = clamp((1.0 - ap.e) / 0.25, 0.0, 1.0);
                let k = 1.0 / e - 1.0;
                off = off + ((b.h * ap.u + b.v * ap.v) * k + b.ax * frame.corruption3.z * band) * ap.openT * wd;
            } else if (ap.e < 1.5) {
                // M3c (ADR-1901; the owner: Gate A was the best the portals looked): the lips fold back a little toward
                // the camera, as at Gate A -- the fan of torn rock round the opening
                let lip = (1.0 - (ap.e - 1.0) / 0.5);
                let l2 = lip * lip * ap.openT * wd;
                off = off - b.ax * l2 * 0.05 * frame.corruption3.x + (b.h * ap.u + b.v * ap.v) / e * l2 * 0.6;
            }
        }
    }
    // a river bed stays under its water: nothing at rest below the floor moves
    let keep = smoothstep(frame.corruption9.x, frame.corruption9.x + 2.0, p0.y);
    out.p = p0 + off * keep;
    out.uvShift = out.uvShift * keep;
    return out;
}

// What the surface shows at its undisplaced point `p0` (fragment stage): the throat's darkness, the seam and rim's
// light, and the glyphs. `dpdx`/`dpdy` of the wall coordinates come in from uniform control flow.
struct CrSurface {
    darken: f32,
    emission: vec3<f32>,
    cut: bool, // inside the opening: the rock is gone
};

fn crGlyphCell(uv: vec2<f32>, duv: f32, t: f32, share: f32) -> f32 {
    // a cell is 5 x 7 dots inside a 7 x 9 grid; the glyph is hashed from the cell and an epoch that cycles it
    let ci = vec2<i32>(floor(uv));
    let cu = bitcast<vec2<u32>>(ci);
    let seed = crSeed();
    if (crHash(vec4<u32>(cu, seed, 21u)) > share) {
        return 0.0; // the share of lit cells grows with the field: sparse far away, a wall of code at the portal
    }
    let colT = crHash(vec4<u32>(cu.x, 0u, seed, 22u)) * 97.0 + t * (0.7 + 0.6 * crHash(vec4<u32>(cu.x, 1u, seed, 23u)));
    let rain = pow(1.0 - fract((f32(ci.y) * 0.07 + colT) / 0.9), 3.0);
    let gid = u32(floor(t * (2.0 + 4.0 * crHash(vec4<u32>(cu, seed, 24u)))));
    let g = fract(uv) * vec2<f32>(7.0, 9.0) - vec2<f32>(1.0, 1.0);
    let sub = floor(g);
    if (sub.x < 0.0 || sub.x > 4.0 || sub.y < 0.0 || sub.y > 6.0) {
        return 0.0;
    }
    let col = u32(min(sub.x, 4.0 - sub.x)); // mirrored strokes
    let bit = crHash(vec4<u32>(cu.x ^ (gid * 2654435761u), cu.y, col * 7u + u32(sub.y), seed + 25u)) < 0.5;
    if (!bit) {
        return 0.0;
    }
    let d = max(abs(g.x - sub.x - 0.5), abs(g.y - sub.y - 0.5)) - 0.4;
    let aa = max(duv * 7.0, 1e-4);
    return (1.0 - smoothstep(-aa, aa, d)) * (0.25 + 0.75 * rain);
}

fn crSurface(p0: vec3<f32>, n: vec3<f32>, t: f32, duv: f32) -> CrSurface {
    var s = CrSurface(0.0, vec3<f32>(0.0), false);
    if (!crOn()) {
        return s;
    }
    let b = crBasis();
    let glowColor = frame.corruption8.xyz;
    if (frame.corruption2.w > 0.0) {
        let ap = crAperture(p0, b);
        let wd = crReach(ap.w);
        let inside = 1.0 - smoothstep(0.96, 1.0, ap.e);
        let rim = exp(-pow((ap.e - 1.04) / 0.035, 2.0));
        // the seam is light leaking through a hairline; once it splits, the throat is dark and the rim burns
        // (once open, the rim's light is the mesh wire's, drawn on the rock's real edges: a surface band would be
        // smeared over the curled lips)
        let shut = 1.0 - ap.openT;
        let light = (rim * (0.3 + 0.7 * ap.seamT) + inside * 0.8) * shut * shut * shut + rim * 0.08 * ap.openT;
        s.emission = glowColor * frame.corruption7.z * light * wd;
        s.darken = inside * ap.openT * wd;
        // inside the tunnel's back ring the rock is a black void (Gate A); a portal view will be composited there
        s.darken = max(s.darken, (1.0 - smoothstep(0.7, 0.8, ap.e)) * ap.openT * wd);
    }
    // glyphs (owner, Gate A: more, sooner, dense around the portal): world-anchored code on every face in the field,
    // the share of lit cells and their brightness growing with the field -- sparse at its edge, a wall of code at the
    // opening -- with brighter bands travelling out of the focus over it
    let f = crField(p0);
    if (frame.corruption7.x > 0.0 && f > 0.04) {
        let r = distance(p0, frame.corruption0.xyz);
        let ph = fract((r - t * 32.0) / 120.0);
        let dd = min(ph, 1.0 - ph) * 120.0;
        let band = exp(-(dd / 14.0) * (dd / 14.0)) * smoothstep(0.0, 25.0, r); // born as a ring, never a disk
        let grow = smoothstep(0.04, 0.85, f);
        // each face reads the world plane it faces most: walls along their horizontal, flats from above
        let an = abs(n);
        var plane = vec2<f32>(p0.x, p0.z);
        if (an.y < 0.7) {
            plane = select(vec2<f32>(p0.x, p0.y), vec2<f32>(p0.z, p0.y), an.x > an.z);
        }
        // the cell grows by powers of two with distance so a glyph is never under ~16 px; the two nearest
        // sizes are crossfaded so nothing pops as the camera closes in
        let pix = distance(frame.cameraPos.xyz, p0) * 2.0 * frame.cameraUp.w * frame.targetSize.w;
        let size = max(frame.corruption7.y, 0.05);
        let lv = log2(max(pix * 22.0 / size, 1.0));
        let l0 = floor(lv);
        let c0 = size * exp2(l0);
        let c1 = c0 * 2.0;
        let share = mix(0.12, 0.72, grow);
        let a0 = crGlyphCell(plane / vec2<f32>(c0 * 0.72, c0), duv / c0, t, share);
        let a1 = crGlyphCell(plane / vec2<f32>(c1 * 0.72, c1) + vec2<f32>(37.0, 11.0), duv / c1, t, share);
        let gl = mix(a0, a1, lv - l0) * grow * (0.45 + 0.55 * band + 0.6 * band * grow);
        let tint = mix(vec3<f32>(1.0, 0.42, 0.12), vec3<f32>(1.0, 0.8, 0.42), clamp(gl, 0.0, 1.0));
        s.emission = s.emission + tint * gl * frame.corruption7.x;
        // the rock under the code darkens as the field takes it, so the code reads and the place is still the place
        s.darken = max(s.darken, 0.55 * grow + 0.15 * band * grow);
    }
    return s;
}

// Whether a target's fragment at its undisplaced point lies in the cut-away rock of the opening.
fn crCut(p0: vec3<f32>) -> bool {
    if (!crOn() || frame.corruption2.w <= 0.0) {
        return false;
    }
    let ap = crAperture(p0, crBasis());
    return false && ap.e < 0.75; // kept for the portal pass (Gate B); the void is drawn dark for now
}

// An echo copy's fragment: kept only on its slipped blocks (the same choice crDisplace made for its vertices).
fn crEchoKeeps(p0: vec3<f32>, t: f32, echo: u32) -> bool {
    return crDisplace(p0, t, echo).keep;
}
