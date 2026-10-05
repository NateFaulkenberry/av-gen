// THE ASTRAL FORGE -- the LATENT anatomy. Never drawn on its own: it is the attractor the matter is pulled
// toward, the field that sharpens the reconstructed surface as coherence rises, and the coordinate system
// the engraving is cut in. Everything here is evaluated in the WARPED domain, so a fold bends the anatomy,
// the particles' targets and the engraving together.
//
// Units: the mask is about 6 units tall, facing +z. Archetypes:
//   0 MASK     a single face (TEST 01)
//   1 SERAPH   symmetric face + fanned blade wings
//   2 ABYSS    asymmetric face, huge tunnel mouth, hanging tendrils
//   3 CHIMERA  three faces at 120 degrees around a shared mass, horns folding through: no front
//   4 MACHINE  faceted face in counter-rotating toothed gyro rings
//   5 CHOIR    a shell of hundreds of small faces that sync, then merge into one face
//   6 HORNS    an abstract organism of spiralling horns and blades: no face (TEST 02)
// Parts (the particle roles): 0 all, 1 eyes, 2 mouth, 3 plate, 4 appendage.

const BIG: f32 = 1e5;

fn smin(a: f32, b: f32, k: f32) -> f32 {
    let h = max(k - abs(a - b), 0.0) / max(k, 1e-5);
    return min(a, b) - h * h * k * 0.25;
}
fn smax(a: f32, b: f32, k: f32) -> f32 { return -smin(-a, -b, k); }

fn sdEllipsoid(p: vec3f, r: vec3f) -> f32 {
    let k0 = length(p / r);
    let k1 = length(p / (r * r));
    return k0 * (k0 - 1.0) / max(k1, 1e-6);
}
// A thin blade (an ellipsoid with one tiny radius). Far away its distance bound is nearly a PLANE, so matter
// attracted from afar collapses onto that plane and draws a straight line across the frame (measured in TEST 01
// and 02). Beyond 1.2x its longest radius the guide is radial instead.
fn sdBlade(p: vec3f, r: vec3f) -> f32 {
    let R = max(r.x, max(r.y, r.z));
    let L = length(p);
    if (L > 1.2 * R) { return L - 0.85 * R; }
    return sdEllipsoid(p, r);
}
// A superellipsoid-ish "faceted" version: the 6-norm turns the plate into chamfered planes.
fn len6(v: vec3f) -> f32 { let w = v * v * v; return pow(dot(w, w), 1.0 / 6.0); }
fn sdFacet(p: vec3f, r: vec3f) -> f32 { return (len6(p / r) - 1.0) * min(r.x, min(r.y, r.z)); }
fn sdCapsule(p: vec3f, a: vec3f, b: vec3f, r: f32) -> f32 {
    let pa = p - a; let ba = b - a;
    let h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h) - r;
}
// Tapered capsule from a (radius ra) to b (radius rb).
fn sdCone(p: vec3f, a: vec3f, b: vec3f, ra: f32, rb: f32) -> f32 {
    let pa = p - a; let ba = b - a;
    let h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h) - mix(ra, rb, h);
}
fn sdTorusZ(p: vec3f, R: f32, r: f32) -> f32 { return length(vec2f(length(p.xy) - R, p.z)) - r; }
// Exact octahedron (Quilez). The cheap L1 bound is NOT a distance: its gradient is a sign pattern, and
// particles following it collapse onto the coordinate planes through the shape (measured: two bright lines
// across the whole frame through the geometric eye).
fn sdOcta(p0: vec3f, s: f32) -> f32 {
    let p = abs(p0);
    let m = p.x + p.y + p.z - s;
    var q: vec3f;
    if (3.0 * p.x < m) { q = p.xyz; }
    else if (3.0 * p.y < m) { q = p.yzx; }
    else if (3.0 * p.z < m) { q = p.zxy; }
    else { return m * 0.57735027; }
    let k = clamp(0.5 * (q.z - q.y + s), 0.0, s);
    return length(vec3f(q.x, q.y - s + k, q.z - k));
}

// ---- the warp: the dimensional distortion layer ------------------------------------------------
// World -> latent. Twist about y, bend about x, and a partial sphere inversion (the "folds inward,
// unfolds into something else" move). The per-feature folds (eye depth, mouth tunnel) live in the face.
fn warp(pw: vec3f) -> vec3f {
    var q = (pw - F.entity.xyz) / F.entity.w;
    let t = F.cam.w;
    // breath: a slow swell of the whole mass, from the low mids
    q *= 1.0 - 0.025 * F.ent1.w;
    // twist about y, growing with height
    let tw = F.fold0.x * (q.y * 0.35 + 0.25 * sin(t * 0.7));
    let xz = rot2(tw) * q.xz;
    q = vec3f(xz.x, q.y, xz.y);
    // bend: the upper half pitches about x
    let bd = F.fold1.x * 0.18 * q.y;
    let yz = rot2(bd) * q.yz;
    q = vec3f(q.x, yz.x, yz.y);
    // sphere inversion about a sphere of radius 3.2, blended
    let inv = F.fold0.w;
    if (inv > 1e-4) {
        let c = vec3f(0.0, 0.0, 0.6);
        let d = q - c;
        let r2 = max(dot(d, d), 0.05);
        let qi = c + d * (3.2 * 3.2) / r2;
        q = mix(q, qi, inv);
    }
    return q;
}

// ---- faces ---------------------------------------------------------------------------------------

struct FaceP {
    asym: f32,      // 0 = both halves organic, 1 = the +x half is faceted geometry
    eyeL: f32,      // eye scales
    eyeR: f32,
    open: f32,      // mouth opening
    tunnel: f32,    // the mouth stretches into a tunnel through depth
    eyeDepth: f32,  // the left eye travels backward through the skull
    inner: f32,     // the mouth holds a smaller face (0/1)
};

fn eyeCentreL() -> vec3f { return vec3f(-0.76, 0.72, 0.6); }
fn eyeCentreR() -> vec3f { return vec3f(0.76, 0.72, 0.6); }
fn mouthCentre() -> vec3f { return vec3f(0.0, -1.45, 0.62); }

// Filaments from the plate's rim out into the field: the mask has no clean edge, it frays into matter.
fn maskFilaments(q: vec3f) -> f32 {
    let t = F.cam.w;
    var d = BIG;
    for (var k = 0; k < 13; k++) {
        let fk = f32(k);
        let a = fk * TAU / 13.0 + 0.3 * sin(fk * 2.3);
        let rim = vec3f(2.05 * cos(a), 0.15 + 2.75 * sin(a), -0.45);
        let dir = normalize(vec3f(cos(a), sin(a), -0.9 + 0.7 * sin(fk * 2.7)));
        var lp = q - rim;
        let along = dot(lp, dir);
        let u = clamp(along, 0.0, 6.0);
        // wander off the straight path, more with distance from the rim
        let side = normalize(cross(dir, vec3f(0.0, 0.0, 1.0)) + vec3f(0.0, 0.0, 0.001));
        let off = side * 0.35 * sin(u * 1.1 + t * 0.7 + fk) * u / 3.0 + vec3f(0.0, 0.0, 0.25 * cos(u * 0.8 + fk)) * u / 3.0;
        let c = dir * u + off;
        let len = 3.0 + 3.0 * u01(hashu(u32(k) * 7717u));
        d = min(d, length(lp - c) - mix(0.05, 0.006, clamp(u / len, 0.0, 1.0)) + select(0.0, 10.0, along > len));
    }
    return d;
}

// The plate: a curved mask (thick shell of an ellipsoid) with no skull behind it. Its rim is torn.
fn facePlate(q: vec3f, fp: FaceP) -> f32 {
    let c = vec3f(0.0, 0.15, -1.25);
    let tear = 0.22 * (vnoise3(q * 1.7 + vec3f(0.0, 0.0, F.cam.w * 0.1), 61u) - 0.5);
    let r = vec3f(1.95, 3.35, 2.0) * (1.0 + tear * smoothstep(1.4, 2.4, length(q.xy * vec2f(1.0, 0.75))));
    let dRound = abs(sdEllipsoid(q - c, r)) - 0.15;
    let dGeo = abs(sdFacet(q - c, r * vec3f(0.97, 0.97, 1.0))) - 0.15;
    let g = smoothstep(-0.25, 0.35, q.x) * fp.asym;
    var d = mix(dRound, dGeo, g);
    d = smax(d, -0.55 - q.z, 0.25); // keep the front: it is a mask
    // eye sockets and the mouth slit cut through
    let eL = eyeCentreL() + vec3f(0.0, 0.0, -fp.eyeDepth * 2.2);
    // almond sockets, outer corners lifted
    let sl = q - eyeCentreL();
    let slr = vec3f(rot2(0.22) * sl.xy, sl.z);
    let sr = q - eyeCentreR();
    let srr = vec3f(rot2(-0.22) * sr.xy, sr.z);
    d = smax(d, -sdEllipsoid(slr, vec3f(0.62, 0.36, 0.55) * fp.eyeL), 0.1);
    d = smax(d, -sdEllipsoid(srr, vec3f(0.62, 0.36, 0.55) * fp.eyeR), 0.1);
    let m = mouthCentre();
    var mq = q - m;
    mq.z *= 1.0 / (1.0 + 3.0 * fp.tunnel);
    d = smax(d, -sdEllipsoid(mq, vec3f(0.95, 0.09 + 0.2 * fp.open, 0.85)), 0.06);
    // brow ridge and nose ridge
    let brow = min(sdCone(q, vec3f(0.0, 1.38, 0.78), vec3f(-1.75, 1.02, 0.28), 0.17, 0.07),
                   sdCone(q, vec3f(0.0, 1.38, 0.78), vec3f(1.75, 1.02, 0.28), 0.17, 0.07));
    d = smin(d, brow, 0.25);
    d = smin(d, sdCone(q, vec3f(0.0, 1.05, 0.86), vec3f(0.0, -0.55, 1.08), 0.07, 0.12), 0.2);
    return d;
}

// Eyes: a polished sphere in the socket with THREE concentric raised pupil rings. The geometric half's
// eye is an octahedron. The left eye can travel backward through depth (fold0.y).
fn faceEyes(q: vec3f, fp: FaceP) -> f32 {
    let t = F.cam.w;
    // far field: radial toward the nearer eye (same reason as the mouth)
    let farE = min(length(q - eyeCentreL() + vec3f(0.0, 0.0, fp.eyeDepth * 2.2)), length(q - eyeCentreR()));
    if (farE > 1.3) { return farE - 0.45; }
    let eL = eyeCentreL() + vec3f(0.0, 0.0, -0.2 - fp.eyeDepth * 2.2);
    let eR = eyeCentreR() + vec3f(0.0, 0.0, -0.2);
    let rL = 0.4 * fp.eyeL;
    let rR = 0.4 * fp.eyeR;
    var d = sdEllipsoid(vec3f(rot2(0.22) * (q - eL).xy, (q - eL).z), vec3f(rL * 1.25, rL * 0.72, rL));
    // three nested pupils: raised rings on the eye's front, slowly counter-rotating via phase
    let lq = q - eL;
    let fz = lq.z - rL * 0.86;
    for (var k = 1; k <= 3; k++) {
        let rr = rL * (0.2 * f32(k) + 0.02 * sin(t * (0.6 + 0.3 * f32(k))));
        d = min(d, length(vec2f(length(lq.xy) - rr, fz + 0.03 * f32(k))) - 0.028 * rL / 0.42);
    }
    let organicR = sdEllipsoid(vec3f(rot2(-0.22) * (q - eR).xy, (q - eR).z), vec3f(rR * 1.25, rR * 0.72, rR));
    let geoR = sdOcta((q - eR) * vec3f(1.0, 0.9, 1.0), rR * 1.25);
    let g = fp.asym;
    var dr = mix(organicR, geoR, g);
    let rq = q - eR;
    let fz2 = rq.z - rR * 0.86;
    for (var k = 1; k <= 3; k++) {
        let rr = rR * (0.22 * f32(k));
        dr = min(dr, length(vec2f(length(rq.xy) - rr, fz2 + 0.03 * f32(k))) - 0.028 * rR / 0.42);
    }
    return min(d, dr);
}

// A small face (choir member, or the face inside the mouth). Unit scale ~1, facing +z.
fn miniFace(q: vec3f) -> f32 {
    var d = abs(sdEllipsoid(q - vec3f(0.0, 0.0, -0.45), vec3f(0.75, 1.0, 0.7))) - 0.07;
    d = smax(d, -0.2 - q.z, 0.1);
    let e1 = vec3f(-0.3, 0.2, 0.2); let e2 = vec3f(0.3, 0.2, 0.2);
    d = smax(d, -(length(q - e1) - 0.2), 0.05);
    d = smax(d, -(length(q - e2) - 0.2), 0.05);
    d = min(d, length(q - e1 + vec3f(0.0, 0.0, 0.04)) - 0.13);
    d = min(d, length(q - e2 + vec3f(0.0, 0.0, 0.04)) - 0.13);
    d = smax(d, -sdEllipsoid(q - vec3f(0.0, -0.42, 0.2), vec3f(0.34, 0.07, 0.4)), 0.04);
    return d;
}

// Mouth: lip rim, teeth (tapered cones in a row, lengths by hash), and a smaller face inside.
fn faceMouth(q: vec3f, fp: FaceP) -> f32 {
    let m = mouthCentre();
    var mq = q - m;
    // far field: a radial guide. The slit is a very flat ellipsoid, and the ellipsoid bound's far gradient points
    // almost entirely along y, so distant matter fell onto the plane y = mouth first (a line across the frame)
    let far = length(mq * vec3f(1.0, 1.6, 1.0));
    if (far > 1.7) { return far - 1.0; }
    mq.z *= 1.0 / (1.0 + 3.0 * fp.tunnel);
    let hy = 0.09 + 0.2 * fp.open;
    var d = abs(sdEllipsoid(mq, vec3f(0.97, hy + 0.03, 0.85))) - 0.02;
    d = smax(d, abs(mq.z - 0.0) - 0.22, 0.03);
    // teeth: one cell per 0.17 units along x
    let cx = clamp(round(mq.x / 0.17), -5.0, 5.0);
    let h = u01(hashu(u32(cx + 20.0) * 7919u + 13u));
    let tx = cx * 0.17;
    let lenU = (0.18 + 0.22 * h) * (1.0 - 0.6 * abs(cx) / 5.0);
    let upper = sdCone(mq, vec3f(tx, hy + 0.06, 0.05), vec3f(tx, hy + 0.06 - lenU, 0.12), 0.05, 0.004);
    let h2 = u01(hashu(u32(cx + 20.0) * 104729u + 7u));
    let lenL = (0.15 + 0.25 * h2) * (1.0 - 0.6 * abs(cx) / 5.0);
    let lower = sdCone(mq, vec3f(tx + 0.08, -hy - 0.06, 0.05), vec3f(tx + 0.08, -hy - 0.06 + lenL, 0.12), 0.05, 0.004);
    d = min(d, min(upper, lower));
    if (fp.inner > 0.5) {
        let isc = 0.2 + 0.12 * clamp(fp.open - 0.2, 0.0, 1.0);
        let iq = (mq - vec3f(0.0, 0.0, -0.35 - 1.2 * fp.tunnel)) / isc;
        d = min(d, miniFace(iq) * isc);
    }
    return d;
}

fn defaultFace() -> FaceP { return FaceP(0.0, 1.0, 1.0, 0.18, 0.0, 0.0, 1.0); }

// ---- appendages -----------------------------------------------------------------------------------

// Wing blades: thin engraved plates fanned from behind the mask. Never feathers.
fn seraphWings(q: vec3f) -> f32 {
    let t = F.cam.w;
    var d = BIG;
    let side = sign(q.x + 1e-6);
    var p = q;
    p.x = abs(p.x);
    let root = vec3f(1.3, 0.6, -1.5);
    for (var k = 0; k < 3; k++) {
        let fk = f32(k);
        let open = 0.12 * F.ent1.w + 0.05 * sin(t * 0.8 + fk);
        let ang = 0.25 + 0.42 * fk + open; // rotating the sample point by +a turns the blade by -a: outward, fanning from vertical
        var lp = p - root;
        let xy = rot2(ang) * lp.xy;
        lp = vec3f(xy.x, xy.y, lp.z);
        let xz = rot2(0.35 + 0.1 * fk) * lp.xz;
        lp = vec3f(xz.x, lp.y, xz.y);
        let len = 4.6 - 0.6 * fk;
        // a blade: long thin ellipsoid, slightly curved
        lp.z += 0.04 * lp.y * lp.y;
        let b = sdBlade(lp - vec3f(0.0, len, 0.0), vec3f(0.42 - 0.06 * fk, len, 0.045));
        d = min(d, b);
    }
    return d;
}

fn abyssTendrils(q: vec3f) -> f32 {
    let t = F.cam.w;
    var d = BIG;
    for (var k = 0; k < 7; k++) {
        let fk = f32(k);
        let rx = -1.3 + 0.43 * fk;
        let root = vec3f(rx, -2.6 + 0.2 * sin(fk * 2.1), -0.2 - 0.3 * cos(fk));
        var lp = q - root;
        let y = -lp.y;
        lp.x -= 0.35 * sin(y * 0.9 + t * 0.9 + fk * 1.7) * clamp(y / 3.0, 0.0, 1.0);
        lp.z -= 0.25 * cos(y * 0.7 + t * 0.6 + fk) * clamp(y / 3.0, 0.0, 1.0);
        let len = 3.5 + 1.5 * u01(hashu(u32(k) * 911u));
        d = min(d, sdCone(lp, vec3f(0.0), vec3f(0.0, -len, 0.0), 0.16, 0.015));
    }
    return d;
}

// A horn: a tapered cone bent along an arc (bend domain), rooted at `root`, oriented by `ang`.
fn horn(q: vec3f, root: vec3f, ang: vec2f, len: f32, curl_: f32, r0: f32) -> f32 {
    var lp = q - root;
    let yz = rot2(ang.x) * lp.yz;
    lp = vec3f(lp.x, yz.x, yz.y);
    let xy = rot2(ang.y) * lp.xy;
    lp = vec3f(xy.x, xy.y, lp.z);
    // bend about z, increasing along y
    let b = curl_ * clamp(lp.y, 0.0, len);
    let bxy = rot2(b * 0.6) * lp.xy;
    lp = vec3f(bxy.x, bxy.y, lp.z);
    return sdCone(lp, vec3f(0.0), vec3f(0.0, len, 0.0), r0, 0.01);
}

fn chimeraHorns(q: vec3f) -> f32 {
    var d = BIG;
    for (var k = 0; k < 4; k++) {
        let fk = f32(k);
        let a = fk * TAU / 4.0 + 0.4;
        let root = vec3f(1.1 * cos(a), 1.6, 1.1 * sin(a));
        d = min(d, horn(q, root, vec2f(0.4 * sin(a), -0.7 * cos(a)), 3.6, 0.9, 0.3));
    }
    return d;
}

// Machine god: counter-rotating toothed gyro rings and a fan of radial plates behind.
fn machineRings(q: vec3f) -> f32 {
    let t = F.cam.w;
    var d = BIG;
    for (var k = 0; k < 3; k++) {
        let fk = f32(k);
        var lp = q - vec3f(0.0, 0.2, -0.6);
        let a1 = t * (0.13 + 0.07 * fk) * select(1.0, -1.0, k == 1) + fk * 1.1;
        let yz = rot2(0.9 * fk + 0.25 * sin(t * 0.2)) * lp.yz;
        lp = vec3f(lp.x, yz.x, yz.y);
        let xy = rot2(a1) * lp.xy;
        lp = vec3f(xy.x, xy.y, lp.z);
        let R = 3.6 + 0.65 * fk;
        var ring = sdTorusZ(lp, R, 0.09);
        // teeth by polar repetition
        let n = 36.0 + 12.0 * fk;
        let ang = safeAtan2(lp.y, lp.x);
        let cell = round(ang / (TAU / n)) * (TAU / n);
        let tp = vec3f(rot2(-cell) * lp.xy, lp.z) - vec3f(R + 0.16, 0.0, 0.0);
        let tooth = max(max(abs(tp.x) - 0.12, abs(tp.y) - 0.035), abs(tp.z) - 0.07);
        ring = min(ring, tooth);
        d = min(d, ring);
    }
    // radial blade fan behind
    var bp = q - vec3f(0.0, 0.3, -2.4);
    let n = 14.0;
    let ang = safeAtan2(bp.y, bp.x) + 0.05 * t;
    let cell = round(ang / (TAU / n)) * (TAU / n);
    let r2 = rot2(-cell + 0.05 * t) * bp.xy;
    let bl = sdBlade(vec3f(r2.x - 4.3, r2.y, bp.z), vec3f(2.2, 0.16, 0.04));
    d = min(d, bl);
    return d;
}

// Abstract organism (TEST 02): no face. A twisted core with a lipped hollow, long curling horns, and a
// ribcage of curved blade fins. Parts: eyes -> fins (fine detail binds first), mouth -> the hollow's lip,
// plate -> the core, appendage -> horns.
fn hornsBody(q: vec3f) -> f32 {
    let t = F.cam.w;
    var p = q;
    let tw = 0.35 * p.y + 0.2 * sin(t * 0.3);
    let xz = rot2(tw) * p.xz;
    p = vec3f(xz.x, p.y, xz.y);
    var d = sdEllipsoid(p, vec3f(1.5, 2.9, 1.25));
    d = smax(d, -sdEllipsoid(p - vec3f(0.0, 0.5, 1.2), vec3f(0.75, 1.5, 0.75)), 0.35);
    return d;
}
fn hornsLip(q: vec3f) -> f32 {
    let t = F.cam.w;
    var p = q;
    let tw = 0.35 * p.y + 0.2 * sin(t * 0.3);
    let xz = rot2(tw) * p.xz;
    p = vec3f(xz.x, p.y, xz.y);
    let e = sdEllipsoid(p - vec3f(0.0, 0.5, 1.2), vec3f(0.85, 1.6, 0.85));
    return max(abs(e) - 0.05, abs(p.z - 0.95) - 0.25);
}
fn hornsAppendages(q: vec3f) -> f32 {
    let t = F.cam.w;
    var d = BIG;
    for (var k = 0; k < 5; k++) {
        let fk = f32(k);
        let a = fk * TAU / 5.0 + 0.35 + 0.12 * sin(t * 0.25 + fk);
        let root = vec3f(0.9 * cos(a), 0.6 + 0.4 * sin(fk * 1.7), 0.75 * sin(a));
        let len = 5.2 + 1.2 * sin(fk * 1.3);
        d = smin(d, horn(q, root, vec2f(0.75 * sin(a), -1.25 * cos(a) + 0.25), len, 1.9 + 0.3 * sin(fk), 0.42), 0.3);
    }
    return d;
}
fn hornsRings(q: vec3f) -> f32 {
    // the "ribcage": curved blade fins around the core, polar-repeated, each a thin bent plate
    let t = F.cam.w;
    var p = q;
    let n = 9.0;
    let ang = safeAtan2(p.z, p.x) + 0.08 * sin(t * 0.4);
    let cell = round(ang / (TAU / n)) * (TAU / n);
    let r2 = rot2(cell) * p.xz;
    var lp = vec3f(r2.x - 1.5, p.y, r2.y);
    // bend the fin backward along its height
    let b = rot2(0.25 * lp.y) * lp.xz;
    lp = vec3f(b.x, lp.y, b.y);
    return sdBlade(lp, vec3f(0.55, 2.6 - 0.4 * abs(sin(cell * 1.7)), 0.035));
}
// ---- choir: hundreds of small faces on a shell (cube-sphere cells), syncing, then merging ---------

fn choirShell(q: vec3f) -> f32 {
    // THE CHOIR: the great mask's skin is made of hundreds of small faces. Each lives in a cell of a 3-D
    // lattice; only cells whose centre lies within the host mask's shell hold one. Before sync each face is
    // turned and displaced on its own; synced, they all stare forward; merged, the host face takes over.
    let t = F.cam.w;
    let sync = F.fold1.z;
    // a lattice bent like scales: an axis-aligned grid of faces reads as tiles or windows (architecture), so the
    // cell coordinates are warped by a slow field before rounding; rows curve and crowd like skin
    let cs = 0.54;
    let wq = q + 0.32 * vec3f(sin(q.y * 1.3 + 0.7 * q.z), sin(q.x * 1.1 - 0.5 * q.z + 1.7), 0.4 * sin(q.x * 0.9 + q.y * 0.8));
    let cell = round(wq / cs);
    let c = cell * cs - (wq - q);
    let fp = defaultFace();
    let host = facePlate(c, fp);
    if (abs(host) > 0.42) { return max(abs(host) - 0.42, 0.05); } // outside the shell: a bound to the shell
    let id = hashu((u32(cell.x + 64.0) * 73856093u) ^ (u32(cell.y + 64.0) * 19349663u) ^ (u32(cell.z + 64.0) * 83492791u));
    let r0 = vec3f(u01(id), u01(id >> 8u), u01(id >> 16u)) - 0.5;
    let wob = (1.0 - sync);
    let off = r0 * (0.08 + 0.18 * wob);
    var lp = q - c - off;
    // independent turning (yaw, pitch, roll), each with its own rhythm, decaying to zero with sync
    let a1 = wob * (r0.x * 2.4 + 0.5 * sin(t * (0.7 + r0.y) + r0.z * 9.0));
    let a2 = wob * (r0.y * 1.6 + 0.4 * sin(t * (0.9 + r0.z) + r0.x * 7.0));
    let a3 = wob * (r0.z * 2.0);
    let xz = rot2(a1) * lp.xz; lp = vec3f(xz.x, lp.y, xz.y);
    let yz = rot2(a2) * lp.yz; lp = vec3f(lp.x, yz.x, yz.y);
    let xy = rot2(a3) * lp.xy; lp = vec3f(xy.x, xy.y, lp.z);
    let s = 0.2;
    return miniFace(lp / s) * s;
}

// ---- archetype assembly ---------------------------------------------------------------------------

fn faceParamsFor(arch: i32) -> FaceP {
    var fp = defaultFace();
    fp.eyeDepth = F.fold0.y;
    fp.tunnel = F.fold0.z;
    fp.open += F.fold1.y; // the mouth as a portal (TEST 05)
    if (arch == 0) { fp.asym = 0.85; }
    if (arch == 1) { fp.asym = 0.0; fp.open = 0.1; }
    if (arch == 2) { fp.asym = 0.5; fp.eyeL = 1.55; fp.eyeR = 0.55; fp.open = 1.6; fp.tunnel = max(fp.tunnel, 0.6); }
    if (arch == 4) { fp.asym = 1.0; fp.open = 0.0; fp.inner = 0.0; }
    return fp;
}

// part: 0 all, 1 eyes, 2 mouth, 3 plate, 4 appendage
fn archetypeD(q: vec3f, arch: i32, part: i32) -> f32 {
    let k = F.ent3.w;
    if (arch == 5) {
        let merge = F.fold1.w;
        var d = BIG;
        if (merge < 0.999) {
            d = choirShell(q);
        }
        if (merge > 0.001) {
            let fp = defaultFace();
            let big = min(min(facePlate(q, fp), faceEyes(q, fp)), faceMouth(q, fp));
            d = mix(d, big, smoothstep(0.0, 1.0, merge));
        }
        return d;
    }
    if (arch == 6) {
        if (part == 1) { return hornsRings(q); }
        if (part == 2) { return hornsLip(q); }
        if (part == 3) { return hornsBody(q); }
        if (part == 4) { return hornsAppendages(q); }
        return smin(smin(smin(hornsBody(q), hornsAppendages(q), k), hornsRings(q), k * 0.5), hornsLip(q), k * 0.3);
    }
    if (arch == 3) {
        // three faces at 120 degrees; each sample uses the face whose sector it is in (no front)
        let a = safeAtan2(q.x, q.z);
        let sector = round(a / (TAU / 3.0));
        let lq2 = rot2(sector * TAU / 3.0) * q.xz;
        var lq = vec3f(lq2.x, q.y, lq2.y) - vec3f(0.0, 0.0, 1.0);
        var fp = defaultFace();
        fp.asym = fract(sector * 0.37 + 0.2);
        fp.eyeDepth = F.fold0.y; fp.tunnel = F.fold0.z;
        let core = sdEllipsoid(q - vec3f(0.0, -0.2, 0.0), vec3f(2.1, 3.0, 2.1));
        if (part == 1) { return faceEyes(lq, fp); }
        if (part == 2) { return faceMouth(lq, fp); }
        if (part == 3) { return smin(facePlate(lq, fp), core, k); }
        if (part == 4) { return chimeraHorns(q); }
        return smin(smin(smin(facePlate(lq, fp), core, k), min(faceEyes(lq, fp), faceMouth(lq, fp)), k * 0.3), chimeraHorns(q), k);
    }
    let fp = faceParamsFor(arch);
    var app = BIG;
    if (part == 0 || part == 4) {
        if (arch == 1) { app = seraphWings(q); }
        if (arch == 2) { app = abyssTendrils(q); }
        if (arch == 4) { app = machineRings(q); }
        if (arch == 0) { app = select(facePlate(q, fp), maskFilaments(q), F.ext.y > 0.5); } // filaments it frays into (ext.y), or a thicker plate
    }
    if (part == 1) { return faceEyes(q, fp); }
    if (part == 2) { return faceMouth(q, fp); }
    if (part == 3) { return facePlate(q, fp); }
    if (part == 4) { return app; }
    let face = smin(smin(facePlate(q, fp), faceEyes(q, fp), k * 0.3), faceMouth(q, fp), k * 0.3);
    return smin(face, app, k);
}

// The latent distance in WORLD units, morphing between two archetypes.
fn latentPart(pw: vec3f, part: i32) -> f32 {
    let q = warp(pw);
    let a = i32(F.ent1.x);
    let b = i32(F.ent1.y);
    let m = F.ent1.z;
    var d = archetypeD(q, a, part);
    if (m > 0.001 && b != a) {
        d = mix(d, archetypeD(q, b, part), smoothstep(0.0, 1.0, m));
    }
    // the warp is not distance-preserving: a conservative factor keeps steps and springs stable
    let lip = 1.0 + 0.6 * abs(F.fold0.x) + 1.5 * F.fold0.w + 0.3 * abs(F.fold1.x);
    return d * F.entity.w / lip;
}
fn latent(pw: vec3f) -> f32 { return latentPart(pw, 0); }

// A feature weight: how close to an eye or the mouth (for edge energy, polish and binding density).
fn featureWeight(q: vec3f) -> f32 {
    let e = min(length(q - eyeCentreL()), length(q - eyeCentreR()));
    let m = length((q - mouthCentre()) * vec3f(0.8, 2.0, 1.0));
    return max(exp(-e * e * 4.0), 0.6 * exp(-m * m * 3.0));
}

// ---- engraving: guilloche line fields cut in the warped domain --------------------------------
// Each family gives SURFACE COORDINATES (u, v); the line pattern is composed per octave in the surface
// shader as  L = f v + A sin(n u + drift f v),  with f and n multiplied by 4 per octave: fine lines carrying
// a wave ~1.5 spacings tall and ~8 long, whose phase drifts across lines (the rose-engine braid / moire),
// self-similar under zoom.
//   0: rosettes around each eye (u = angle, v = radius), n = 12 / 18
//   1: contour rosettes about a point behind the mask (u = angle, v = distance), n = 7
//   2: engine-turned waves (u, v = two oblique axes), n = 1 (a plain wave)
struct EUV { uv: vec2f, n: f32, angular: f32, amp: f32, };  // amp fades the wave near a rosette's centre
fn engraveUV(q: vec3f, fam: i32) -> EUV {
    let t = F.cam.w;
    let crawl = t * (0.03 + 0.12 * F.ent2.w);
    if (fam == 0) {
        let eL = q - eyeCentreL();
        let eR = q - eyeCentreR();
        let useL = dot(eL, eL) < dot(eR, eR);
        let e = select(eR, eL, useL);
        let r = length(e.xy);
        return EUV(vec2f(safeAtan2(e.y, e.x) + crawl, r), select(18.0, 12.0, useL), 1.0, smoothstep(0.15, 0.5, r));
    }
    if (fam == 1) {
        let d = q - vec3f(0.0, 0.2, -3.0);
        return EUV(vec2f(safeAtan2(d.y, d.x) - crawl * 0.3, length(d) - crawl * 0.3), 7.0, 1.0, smoothstep(0.4, 1.6, length(d.xy)));
    }
    let s = dot(q, normalize(vec3f(0.94, 0.30, 0.17)));
    let s2 = dot(q, normalize(vec3f(-0.25, 0.95, 0.2)));
    return EUV(vec2f(s2 * 3.0 + crawl * 2.0, s), 1.0, 0.0, 1.0);
}
