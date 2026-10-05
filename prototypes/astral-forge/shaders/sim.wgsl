// THE ASTRAL FORGE -- the matter. Particles bound to the latent anatomy by coherence, splatted into a u32
// fixed-point density grid (order-independent, so the field is deterministic), then blurred into a 3-D
// texture the surface pass raymarches.
//
// State per particle: P = (pos, theta), V = (vel, heat), A = (latent normal, binding).
// Roles by hash: eye 9%, mouth 7%, plate 42%, appendage 24%, drifter 18% (never binds: meta-scale dust).

@group(1) @binding(0) var<storage, read_write> P: array<vec4f>;
@group(1) @binding(1) var<storage, read_write> V: array<vec4f>;
@group(1) @binding(2) var<storage, read_write> A: array<vec4f>;
@group(1) @binding(3) var<storage, read_write> grid: array<atomic<u32>>; // 2 channels: density, heat
@group(1) @binding(4) var densTex: texture_3d<f32>;   // last frame's density (repulsion)
@group(1) @binding(5) var densSamp: sampler;

const DENS_SCALE: f32 = 1024.0;

fn roleOf(i: u32) -> i32 {
    let r = u01(hashu(i * 0x9e3779b9u + 0x632be5abu));
    if (r < 0.10) { return 1; }
    if (r < 0.18) { return 2; }
    if (r < 0.64) { return 3; }
    if (r < 0.91) { return 4; }
    return 5;
}
fn thetaFor(role: i32, h: f32) -> f32 {
    if (role == 1) { return mix(0.06, 0.42, h); }
    if (role == 2) { return mix(0.14, 0.55, h); }
    if (role == 3) { return mix(0.28, 0.93, h); }
    if (role == 4) { return mix(0.45, 0.99, h); }
    return 9.0;
}
fn ballPoint(i: u32, salt: u32) -> vec3f {
    let a = u01(hashu(i * 747796405u + salt));
    let b = u01(hashu(i * 2891336453u + salt * 3u));
    let c = u01(hashu(i * 1103515245u + salt * 7u));
    let z = 2.0 * a - 1.0;
    let ph = TAU * b;
    let s = sqrt(max(1.0 - z * z, 0.0));
    return vec3f(s * cos(ph), z, s * sin(ph)) * pow(c, 0.5);
}

@compute @workgroup_size(256)
fn cs_init(@builtin(global_invocation_id) gid: vec3u, @builtin(num_workgroups) nwg: vec3u) {
    let i = gid.x + gid.y * nwg.x * 256u;
    if (i >= u32(F.grid1.w)) { return; }
    let role = roleOf(i);
    let h = u01(hashu(i * 0x85ebca6bu + 0xc2b2ae35u));
    let R = select(9.0, 22.0, role == 5);
    let p = F.entity.xyz + ballPoint(i, 17u) * R * vec3f(1.2, 0.9, 1.0);
    P[i] = vec4f(p, thetaFor(role, h));
    V[i] = vec4f(curl(p * 0.2) * 0.3, 0.0);
    A[i] = vec4f(0.0, 0.0, 1.0, 0.0);
}

fn bindOf(theta: f32, c: f32) -> f32 { return smoothstep(theta - 0.08, theta + 0.08, c); }

// Tetrahedral gradient of the latent part: 4 evaluations give the distance and the direction.
struct DG { d: f32, g: vec3f, };
fn latentDG(p: vec3f, part: i32) -> DG {
    let e = 0.03 * F.entity.w;
    let k0 = vec3f(1.0, -1.0, -1.0); let k1 = vec3f(-1.0, -1.0, 1.0);
    let k2 = vec3f(-1.0, 1.0, -1.0); let k3 = vec3f(1.0, 1.0, 1.0);
    let d0 = latentPart(p + k0 * e, part); let d1 = latentPart(p + k1 * e, part);
    let d2 = latentPart(p + k2 * e, part); let d3 = latentPart(p + k3 * e, part);
    let g = k0 * d0 + k1 * d1 + k2 * d2 + k3 * d3;
    let gl = length(g);
    return DG(0.25 * (d0 + d1 + d2 + d3), select(vec3f(0.0, 0.0, 1.0), g / gl, gl > 1e-8));
}

fn gridUvw(p: vec3f) -> vec3f { return (p - F.grid0.xyz) / (F.grid0.w * F.grid1.x); }

@compute @workgroup_size(256)
fn cs_step(@builtin(global_invocation_id) gid: vec3u, @builtin(num_workgroups) nwg: vec3u) {
    let i = gid.x + gid.y * nwg.x * 256u;
    if (i >= u32(F.grid1.w)) { return; }
    let dt = F.sim.x;
    let t = F.cam.w;
    let approach = i32(F.sim.w);
    var p4 = P[i];
    var v4 = V[i];
    var p = p4.xyz;
    var v = v4.xyz;
    let theta = p4.w;
    var heat = v4.w;
    let role = roleOf(i);
    let hi = hashu(i * 0x27d4eb2fu + 0x165667b1u);

    let C = F.ent0.x;
    let Cprev = F.ent0.y;
    let flash = select(0.0, F.ent2.y, role == 1 || role == 2);
    var b = bindOf(theta, C + flash);
    let bPrev = bindOf(theta, Cprev + flash);
    let release = max(bPrev - b, 0.0);
    var acc = vec3f(0.0);
    var nrm = A[i].xyz;
    let chaos = F.ent3.z;

    if (role != 5 && approach != 1) {
        // the latent pulls bound matter onto the anatomy (a relaxed Witkin-Heckbert constraint)
        // a per-particle offset on the sampling point: on a medial ridge (between two eyes, say) the pulls
        // cancel and matter collects on the bisector plane; the offset makes each particle commit to a side
        let jo = jitter3(hi * 7u + 3u) * 0.16 * F.entity.w;
        let dg = latentDG(p + jo, role);
        let target_ = p - dg.g * dg.d;
        let K = 18.0 + 70.0 * C * C;
        acc += b * (K * (target_ - p) - 2.0 * sqrt(K) * 0.55 * v);
        // bound matter migrates over the surface: curl projected into the tangent plane
        var c = curl(p * 0.45 + vec3f(0.0, t * 0.08, 0.0));
        c -= dg.g * dot(c, dg.g);
        acc += b * F.ent2.z * c;
        // repulsion down the density gradient keeps matter from piling into a few voxels
        let uvw = gridUvw(p);
        let s = 1.0 / F.grid1.x;
        let gx = textureSampleLevel(densTex, densSamp, uvw + vec3f(s, 0, 0), 0.0).r - textureSampleLevel(densTex, densSamp, uvw - vec3f(s, 0, 0), 0.0).r;
        let gy = textureSampleLevel(densTex, densSamp, uvw + vec3f(0, s, 0), 0.0).r - textureSampleLevel(densTex, densSamp, uvw - vec3f(0, s, 0), 0.0).r;
        let gz = textureSampleLevel(densTex, densSamp, uvw + vec3f(0, 0, s), 0.0).r - textureSampleLevel(densTex, densSamp, uvw - vec3f(0, 0, s), 0.0).r;
        let gd = vec3f(gx, gy, gz);
        acc -= b * gd * 9.0 * (1.0 - 0.5 * dot(gd, dg.g) * dot(gd, dg.g));
        // the collapse: released matter is THROWN along the surface normal, with a twist, and heated
        if (release > 0.0) {
            let jitter = vec3f(u01(hi), u01(hi >> 5u), u01(hi >> 11u)) - 0.5;
            let sgn = select(1.0, -1.0, u01(hi >> 17u) < 0.2);
            v += release * F.ent3.x * (dg.g * sgn * (0.6 + 0.8 * u01(hi >> 3u)) + curl(p * 0.3) * 0.25 + jitter * 0.6);
            heat += release * F.ent3.y * (0.5 + u01(hi >> 7u));
        }
        nrm = dg.g;
        // a few bound flakes escape and are re-absorbed: the form is always being generated
        if (u01(hashu(hi + u32(t * 3.0))) < F.misc.y * dt) { v += (dg.g + jitter3(hi)) * 3.0; b *= 0.2; }
    } else if (approach == 1 && role != 5) {
        // Approach B: no latent anatomy; matter is drawn to a few wandering point attractors
        let k = f32(hi % 5u);
        let ap = F.entity.xyz + vec3f(3.0 * sin(t * 0.3 + k * 1.3), 2.5 * cos(t * 0.23 + k * 2.1), 2.0 * sin(t * 0.17 + k));
        b = bindOf(theta, C);
        let dvec = ap - p;
        acc += b * (normalize(dvec) * min(length(dvec), 3.0) * 12.0 - 3.0 * v);
    }

    // chaos: what remains when the god lets go. Curl keeps it moving; a weak pull onto the drifting zero set
    // of a noise field gives the free matter its own ambiguous shapes (sheets, clots, voids between) --
    // the brief's "loose clustering" and "suggestive structure" before any anatomy exists.
    acc += (1.0 - b) * chaos * curl(p * 0.16 + vec3f(t * 0.05, 0.0, t * 0.03));
    if (role != 5) {
        let np = p * 0.32 + vec3f(0.0, t * 0.04, t * 0.02);
        let e = 0.15;
        let n0 = vnoise3(np, 53u) - 0.5;
        let ng = vec3f(vnoise3(np + vec3f(e, 0, 0), 53u), vnoise3(np + vec3f(0, e, 0), 53u), vnoise3(np + vec3f(0, 0, e), 53u)) - 0.5 - n0;
        acc -= (1.0 - b) * 3.5 * n0 * ng / max(length(ng), 1e-4);
    }
    // the abyss: unbound matter falls inward toward the mouth
    if (F.misc.x > 0.0) {
        let m = F.entity.xyz + vec3f(0.0, -1.3, 0.5) * F.entity.w;
        let dm = m - p;
        acc += (1.0 - b) * F.misc.x * normalize(dm) * clamp(8.0 / (1.0 + dot(dm, dm) * 0.05), 0.0, 6.0);
    }
    // weak containment: the meta field stays around the entity
    let rel = p - F.entity.xyz;
    let rmax = select(11.0, 24.0, role == 5) * F.entity.w;
    acc -= rel * 0.6 * smoothstep(rmax * 0.8, rmax * 1.3, length(rel));

    let drag = 0.35 + 1.4 * b;
    v = (v + acc * dt) * exp(-drag * dt);
    p += v * dt;
    heat *= exp(-1.6 * dt);
    P[i] = vec4f(p, theta);
    V[i] = vec4f(v, heat);
    A[i] = vec4f(nrm, b);
}

fn jitter3(h: u32) -> vec3f { return vec3f(u01(h >> 2u), u01(h >> 9u), u01(h >> 13u)) - 0.5; }

// ---- density splat: trilinear cloud-in-cell, u32 fixed point (order-independent) -------------------
@compute @workgroup_size(256)
fn cs_splat(@builtin(global_invocation_id) gid: vec3u, @builtin(num_workgroups) nwg: vec3u) {
    let i = gid.x + gid.y * nwg.x * 256u;
    if (i >= u32(F.grid1.w)) { return; }
    let role = roleOf(i);
    let p = P[i].xyz;
    let res = i32(F.grid1.x);
    let g = (p - F.grid0.xyz) / F.grid0.w - 0.5;
    let g0 = vec3i(floor(g));
    if (any(g0 < vec3i(0)) || any(g0 >= vec3i(res - 1))) { return; }
    let f = g - vec3f(g0);
    // drifters weigh little: dust, not body
    // drifters weigh little (dust, not body); appendage matter weighs misc.w: below 1 its strands stay under
    // the iso level and read as streams of flakes rather than tubes
    var w = select(1.0, 0.25, role == 5) * F.flags.y;
    if (role == 4) { w *= F.misc.w; }
    let heat = V[i].w;
    for (var k = 0; k < 8; k++) {
        let o = vec3i(k & 1, (k >> 1) & 1, (k >> 2) & 1);
        let wf = select(1.0 - f.x, f.x, o.x == 1) * select(1.0 - f.y, f.y, o.y == 1) * select(1.0 - f.z, f.z, o.z == 1);
        let c = g0 + o;
        let idx = u32(c.x + res * (c.y + res * c.z));
        atomicAdd(&grid[idx * 2u], u32(w * wf * DENS_SCALE));
        if (heat > 0.01) { atomicAdd(&grid[idx * 2u + 1u], u32(heat * wf * DENS_SCALE)); }
    }
}
