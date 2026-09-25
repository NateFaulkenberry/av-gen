// SHELL (Effect Library Wave 3): analytic proxy shells, drawn in pass 1's blended section beside the
// particles and the ribbons. The CPU half is world/effects/shell_frame.hpp; the renderer is
// rendering/shell_renderer.cpp.
//
// A shell is a canonical mesh (unit sphere, box, quad, disc, cylinder, cone) instanced from ONE
// storage buffer of records -- a transform and eight vec4 parameters -- and shaded by one of the
// entry points below. Each entry point is its own pipeline (ADR-118: a switch over programs inside
// one shader pays for every branch any lane takes); they share the vertex stage and the helpers.
//
//   fs_plasma   an emission-only march through the shell's interior, clamped to the scene's depth
//   fs_shield   a rim, a cell pattern revealed at the rim and around impacts, rings travelling out
//               from each impact, and a bright line where the shell cuts the ground
//   fs_barrier  a scrolling pattern on a wall, box, cylinder or dome, faint until something comes
//               near it, bright where it meets the ground and along its edges
//
// Every target of the scene pass is declared (ADR-035): HDR colour and emission are added (a shell
// is light), velocity is the camera's motion blended in by coverage, the normal and identifier
// targets are masked off by the pipeline. Depth is tested and never written. Fog: what the surface
// fog keeps of a unit colour at the shell's point, so light added far away is dimmed, never tinted.
//
// Depth terms read the SEPARATE linear-depth target (binding 7 of the frame group) -- the depth
// attachment cannot be sampled inside the pass that draws into it. `shellInfo.flags.x` is 1 only
// when the prepass resolved it THIS frame; otherwise it holds another frame's depth, and the terms
// that read it are off.

#include "common.wgsl"
#include "noise.wgsl"

// Mirrors world::ShellInstance (192 bytes).
struct ShellRecord {
    model: mat4x4<f32>,
    params: array<vec4<f32>, 8>,
};

struct ShellInfo {
    flags: vec4<f32>, // x = 1 when the linear depth is this frame's; yzw spare
};

@group(0) @binding(7) var sceneLinearDepth: texture_2d<f32>;
@group(1) @binding(0) var<storage, read> shells: array<ShellRecord>;
@group(1) @binding(1) var<storage, read> shellExtra: array<vec4<f32>>;
@group(1) @binding(2) var<uniform> shellInfo: ShellInfo;
@group(2) @binding(0) var iblSampler: sampler;
@group(2) @binding(2) var prefilteredMap: texture_cube<f32>;

struct ShellIn {
    @location(0) position: vec3<f32>,
    @location(1) normal: vec3<f32>,
    @location(2) uv: vec2<f32>,
    @builtin(instance_index) instance: u32,
};

struct ShellVaryings {
    @builtin(position) clip: vec4<f32>,
    @location(0) world: vec3<f32>,
    @location(1) normal: vec3<f32>,
    @location(2) local: vec3<f32>,
    @location(3) @interpolate(flat) instance: u32,
    @location(4) prevClip: vec4<f32>,
    @location(5) meshNormal: vec3<f32>,
};

struct ShellOut {
    @location(0) color: vec4<f32>,
    @location(1) normalRoughness: vec4<f32>,
    @location(2) velocity: vec4<f32>,
    @location(3) emission: vec4<f32>,
    @location(4) ids: u32,
};

// ---- the record's space --------------------------------------------------------------------------
// The model's axis columns are orthogonal (rotation times per-axis scale), so the inverse is a
// projection onto them and the normal matrix is each column over its squared length.

fn axisScale(r: ShellRecord) -> vec3<f32> {
    return vec3<f32>(length(r.model[0].xyz), length(r.model[1].xyz), length(r.model[2].xyz));
}

fn toLocalDir(r: ShellRecord, d: vec3<f32>) -> vec3<f32> {
    let c0 = r.model[0].xyz;
    let c1 = r.model[1].xyz;
    let c2 = r.model[2].xyz;
    return vec3<f32>(dot(d, c0) / max(dot(c0, c0), 1e-20), dot(d, c1) / max(dot(c1, c1), 1e-20),
                     dot(d, c2) / max(dot(c2, c2), 1e-20));
}

fn toLocalPoint(r: ShellRecord, p: vec3<f32>) -> vec3<f32> {
    return toLocalDir(r, p - r.model[3].xyz);
}

@vertex
fn vs_shell(in: ShellIn) -> ShellVaryings {
    let r = shells[in.instance];
    let world = (r.model * vec4<f32>(in.position, 1.0)).xyz;
    let c0 = r.model[0].xyz;
    let c1 = r.model[1].xyz;
    let c2 = r.model[2].xyz;
    let n = c0 * (in.normal.x / max(dot(c0, c0), 1e-20)) + c1 * (in.normal.y / max(dot(c1, c1), 1e-20)) +
            c2 * (in.normal.z / max(dot(c2, c2), 1e-20));
    var out: ShellVaryings;
    out.clip = frame.viewProj * vec4<f32>(world, 1.0);
    out.prevClip = frame.prevViewProj * vec4<f32>(world, 1.0);
    out.world = world;
    out.normal = normalize(n);
    out.local = in.position;
    out.instance = in.instance;
    out.meshNormal = in.normal;
    return out;
}

// ---- shared helpers ------------------------------------------------------------------------------

fn depthAvailable() -> bool {
    return shellInfo.flags.x > 0.5;
}

// The scene's view depth (metres along the camera's forward axis) under this fragment: what the
// depth prepass resolved. A very large value where nothing was drawn.
fn sceneViewDepth(fragCoord: vec4<f32>) -> f32 {
    let size = vec2<f32>(textureDimensions(sceneLinearDepth, 0));
    let texel = clamp(vec2<i32>(fragCoord.xy * frame.targetSize.zw * size), vec2<i32>(0), vec2<i32>(size) - 1);
    return textureLoad(sceneLinearDepth, texel, 0).r;
}

fn viewDepthOf(world: vec3<f32>) -> f32 {
    return dot(world - frame.cameraPos.xyz, frame.cameraForward.xyz);
}

// How close this fragment lies to the opaque surface behind it: 1 where they meet, 0 beyond
// `width` metres. 0 without this frame's depth.
fn intersectionGlow(fragCoord: vec4<f32>, world: vec3<f32>, width: f32) -> f32 {
    if (!depthAvailable() || width <= 0.0) {
        return 0.0;
    }
    let gap = sceneViewDepth(fragCoord) - viewDepthOf(world);
    let g = 1.0 - smoothstep(0.0, width, abs(gap));
    return g * g;
}

fn fogTransmit(world: vec3<f32>) -> vec3<f32> {
    return applyFog(vec3<f32>(1.0), world) - applyFog(vec3<f32>(0.0), world);
}

fn luminance(c: vec3<f32>) -> f32 {
    return dot(c, vec3<f32>(0.2126, 0.7152, 0.0722));
}

// Light added to the frame, into every target the pass has.
fn shellLight(in: ShellVaryings, radiance: vec3<f32>) -> ShellOut {
    let lit = max(radiance, vec3<f32>(0.0)) * fogTransmit(in.world);
    let coverage = clamp(luminance(lit), 0.0, 1.0);
    var out: ShellOut;
    out.color = vec4<f32>(lit, 0.0);
    out.normalRoughness = vec4<f32>(0.0);
    out.velocity = vec4<f32>(screenVelocityAt(in.clip, in.prevClip), 0.0, coverage);
    out.emission = vec4<f32>(lit, coverage);
    out.ids = 0u;
    return out;
}

fn shellHash(p: vec2<f32>, seed: f32) -> f32 {
    return fract(sin(dot(p, vec2<f32>(127.1, 311.7)) + seed * 74.7) * 43758.5453);
}

// A hexagonal tiling of the plane with cells of inradius 0.5: x = distance to the cell's border
// (0 on it, 0.5 at the centre), yz = the cell's centre (its identity).
fn hexCell(p: vec2<f32>) -> vec3<f32> {
    let s = vec2<f32>(1.0, 1.7320508);
    let h = s * 0.5;
    let a = p - s * floor(p / s) - h;
    let pb = p - h;
    let b = pb - s * floor(pb / s) - h;
    let g = select(b, a, dot(a, a) < dot(b, b));
    let q = abs(g);
    let d = max(dot(q, normalize(s)), q.x);
    return vec3<f32>(0.5 - d, p - g);
}

// A cell pattern on a surface point `p` (metres in the shell's scaled local space) with unit normal
// `n` (local), as x = border distance in cell units (0 on a border) and y = a per-cell random in
// [0, 1). Three planar projections blended by the normal, sharply, so a sphere shows one projection
// almost everywhere. `kind`: 1 hexagons, 2 organic cells (Worley F2 - F1), 3 a square grid.
fn cellPattern(p: vec3<f32>, n: vec3<f32>, kind: f32, seed: f32) -> vec2<f32> {
    if (kind > 1.5 && kind < 2.5) {
        let w = worleyF1F2(p, u32(seed));
        return vec2<f32>((w.y - w.x) * 0.5, w.z);
    }
    var w = pow(abs(n), vec3<f32>(8.0));
    w = w / max(w.x + w.y + w.z, 1e-6);
    var planes = array<vec2<f32>, 3>(p.yz, p.zx, p.xy);
    var edge = 0.0;
    var id = 0.0;
    for (var i = 0; i < 3; i = i + 1) {
        var c: vec3<f32>;
        if (kind > 2.5) {
            let g = abs(fract(planes[i]) - 0.5);
            c = vec3<f32>(0.5 - max(g.x, g.y), floor(planes[i]));
        } else {
            c = hexCell(planes[i]);
        }
        edge = edge + w[i] * c.x;
        id = id + w[i] * shellHash(c.yz + vec2<f32>(f32(i) * 17.0), seed);
    }
    return vec2<f32>(edge, id);
}

// A border line of `width` (cell units) on `edge`, anti-aliased over a pixel.
fn borderLine(edge: f32, width: f32) -> f32 {
    let aa = max(fwidth(edge), 1e-4);
    return 1.0 - smoothstep(width, width + aa * 1.5, edge);
}

// ---- Plasma ----------------------------------------------------------------------------------------
//   params[0] rgb = core colour, a = core brightness at the centre (HDR, envelope included)
//   params[2] rgb = strand colour, a = strand brightness where the strands are dense (envelope included)
//   params[3] x = turbulence (domain warp), y = strand scale (per unit radius), z = steps, w = limb softness
//   params[4] x = core size (fraction of the radius), y = strand sharpness (0..0.95), z = strand opacity,
//             w = limb glow
// Drawn with its FRONT faces: the ray enters there, leaves where the analytic sphere says or where the
// scene's depth stops it, whichever is nearer. A camera inside the orb sees no front face and draws
// nothing, which is documented rather than hidden.
//
// **Why it is not a light bulb.** The strands both emit and occlude (front to back, `L += c (1 - e^-s)`
// with transmittance carried), so a pixel's strand light saturates at the strand colour however deep the
// orb is: brightness says how much strand the ray met, not how long the ray was, and the gaps between
// strands stay dark. The core is a bounded Gaussian whose peak IS the core brightness, seen through the
// strands in front of it. Only the core writes the emission target at full weight -- the strands and the
// limb feed the bloom a little -- so the bloom haloes the orb without washing its strands out.

@fragment
fn fs_plasma(in: ShellVaryings) -> ShellOut {
    let r = shells[in.instance];
    let core = r.params[0];
    let strand = r.params[2];
    let shape = r.params[3];
    let body = r.params[4];
    let seed = u32(r.params[1].x);
    let clock = r.params[1].y;

    let cam = frame.cameraPos.xyz;
    let dirW = normalize(in.world - cam);
    let o = toLocalPoint(r, cam);
    let d = toLocalDir(r, dirW); // not unit: the ray parameter stays in world metres
    let a = max(dot(d, d), 1e-12);
    let b = dot(o, d);
    let c = dot(o, o) - 1.0;
    let disc = b * b - a * c;
    var strands = vec3<f32>(0.0);
    var hotLight = vec3<f32>(0.0);
    var transmit = 1.0;
    if (disc > 0.0) {
        let sq = sqrt(disc);
        let t0 = max((-b - sq) / a, 0.0);
        var t1 = (-b + sq) / a;
        if (depthAvailable()) {
            let along = max(dot(dirW, frame.cameraForward.xyz), 1e-4);
            t1 = min(t1, sceneViewDepth(in.clip) / along);
        }
        if (t1 > t0) {
            let steps = clamp(shape.z, 4.0, 48.0);
            let n = u32(steps);
            let dt = (t1 - t0) / steps;
            let dtLocal = dt * sqrt(a);
            // A fixed per-pixel offset (interleaved gradient noise of the pixel, not the frame): the
            // step pattern turns to fine grain instead of banding, and does not shimmer.
            let jitter = fract(52.9829189 * fract(dot(in.clip.xy, vec2<f32>(0.06711056, 0.00583715))));
            let scale = max(shape.y, 0.05);
            let soft = clamp(shape.w, 0.02, 1.0);
            let coreSize = max(body.x, 0.02);
            let sharp = 2.0 + 12.0 * clamp(body.y, 0.0, 0.95);
            let opacity = max(body.z, 0.0) * 3.5;
            let strandRgb = strand.rgb * strand.a;
            let coreRgb = core.rgb * core.a;
            // The core Gaussian integrates to coreSize * sqrt(pi) through the centre, so dividing by it
            // makes the centre's brightness the core brightness.
            let coreNorm = 1.0 / (coreSize * 1.7724539);
            for (var i = 0u; i < n; i = i + 1u) {
                let t = t0 + (f32(i) + jitter) * dt;
                let p = o + d * t;
                let rr = length(p);
                let limb = 1.0 - smoothstep(1.0 - soft, 1.0, rr);
                if (limb <= 0.0) {
                    continue;
                }
                // Strands: ridged fBM, mostly a function of the direction from the centre (so they run
                // outward, as a plasma globe's do), through a divergence-free warp that evolves in place
                // (so they boil rather than scroll). A pure function of the transport clock.
                let warp = flowCurl(p * (scale * 0.5), clock, seed) * shape.x;
                let dir = p / max(rr, 1e-3);
                let q = dir * scale + p * (scale * 0.7) + warp + vec3<f32>(0.0, -clock * 0.35, 0.0);
                let f = fbm3(q, seed);
                let ridge = pow(1.0 - abs(2.0 * f - 1.0), sharp);
                let hot = exp(-(rr * rr) / (coreSize * coreSize));
                // Strands brighten towards the core: the plasma is hottest where it is densest.
                let tint = strandRgb * (1.0 + 1.5 * hot);
                let absorb = 1.0 - exp(-opacity * ridge * limb * dtLocal);
                strands = strands + tint * (absorb * transmit);
                hotLight = hotLight + coreRgb * (hot * coreNorm * dtLocal * transmit);
                transmit = transmit * (1.0 - absorb * 0.7); // strands veil the core, never black it out
            }
        }
    }
    // The limb: where the ray grazes the shell, a thin brighter rim, as a glowing ball's edge.
    let v = normalize(frame.cameraPos.xyz - in.world);
    let rim = pow(1.0 - clamp(abs(dot(in.normal, v)), 0.0, 1.0), 3.0) * body.w;
    let rimLight = strand.rgb * strand.a * rim;

    let fog = fogTransmit(in.world);
    let lit = (strands + hotLight + rimLight) * fog;
    let glow = (0.5 * hotLight + 0.15 * strands + 0.3 * rimLight) * fog;
    let coverage = clamp(luminance(glow), 0.0, 1.0);
    var out: ShellOut;
    out.color = vec4<f32>(lit, 0.0);
    out.normalRoughness = vec4<f32>(0.0);
    out.velocity = vec4<f32>(screenVelocityAt(in.clip, in.prevClip), 0.0, clamp(luminance(lit), 0.0, 1.0));
    out.emission = vec4<f32>(glow, coverage);
    out.ids = 0u;
    return out;
}

// ---- Shield ----------------------------------------------------------------------------------------
//   params[0] rgb = colour, a = intensity (envelope included)
//   params[1] zw = the hits in `shellExtra`: each xyz = unit direction in the shell's local space,
//             w = age (s)
//   params[2] x = rim intensity, y = rim power, z = far-side factor, w = idle reveal
//   params[3] x = pattern (0 none, 1 hex, 2 cells), y = cell size (m), z = pattern intensity,
//             w = mean radius (m)
//   params[4] x = hit intensity, y = ripple speed (m/s), z = ripple width (m), w = hit decay (1/s)
//   params[5] x = ground-line width (m), y = ground-line intensity, z = 1 cuts the shell at local
//             y = 0 (a dome), w = environment reflection
//   params[6] rgb = hit colour, w = cell flicker

@fragment
fn fs_shield(in: ShellVaryings, @builtin(front_facing) front: bool) -> ShellOut {
    let r = shells[in.instance];
    let p0 = r.params[0];
    let p2 = r.params[2];
    let p3 = r.params[3];
    let p4 = r.params[4];
    let p5 = r.params[5];
    let p6 = r.params[6];
    let seed = r.params[1].x;
    let clock = r.params[1].y;

    let scaleAxes = axisScale(r);
    let lp = in.local * scaleAxes; // metres, in the shell's frame
    let u = normalize(in.local);
    let cut = p5.z > 0.5 && in.local.y < -0.002;

    let n = select(-in.normal, in.normal, front);
    let v = normalize(frame.cameraPos.xyz - in.world);
    let nv = clamp(abs(dot(in.normal, v)), 0.0, 1.0);
    let rim = p2.x * pow(1.0 - nv, max(p2.y, 0.1));

    // The cells, computed for every fragment ahead of any per-instance loop (their anti-aliasing
    // takes a derivative).
    let cells = cellPattern(lp / max(p3.y, 0.01), normalize(in.meshNormal), max(p3.x, 1.0), seed);
    let cellLine = borderLine(cells.x, 0.06) * select(0.0, 1.0, p3.x > 0.5);
    let cellId = cells.y;

    // Impacts: a ring travelling out along the surface from each, a flash where it struck, and the
    // cells revealed inside the ring as it passes.
    var ring = 0.0;
    var flash = 0.0;
    var revealed = 0.0;
    let meanRadius = max(p3.w, 0.01);
    let width = max(p4.z, 0.01);
    let firstHit = u32(r.params[1].z);
    let hitCount = u32(r.params[1].w);
    for (var i = 0u; i < hitCount; i = i + 1u) {
        let hit = shellExtra[firstHit + i];
        let age = max(hit.w, 0.0);
        let dist = acos(clamp(dot(u, hit.xyz), -1.0, 1.0)) * meanRadius;
        let reach = p4.y * age;
        let fade = exp(-p4.w * age);
        let x = (dist - reach) / width;
        ring = ring + exp(-x * x) * fade;
        let y = dist / (width * 1.5);
        flash = flash + exp(-y * y) * exp(-p4.w * age * 2.5);
        // The cells show in a band trailing the ring, as if the shell were lit up by the wave
        // passing through it, and settle behind it.
        let behind = reach - dist;
        revealed = revealed + smoothstep(-width, 0.0, behind) * (1.0 - smoothstep(0.0, 5.0 * width, behind)) * fade;
    }

    let ground = intersectionGlow(in.clip, in.world, p5.x);
    let flicker = 1.0 - p6.w * (0.5 + 0.5 * sin(clock * 6.2831853 * (0.6 + cellId) + cellId * 40.0));
    // Squared: the cells come up sharply where something reveals them and stay out of the way
    // everywhere else, which is what keeps a shield "mostly invisible".
    let reveal = clamp(p2.w + revealed + 0.5 * ground + 0.2 * rim, 0.0, 1.0);
    var radiance = p0.rgb * (rim + cellLine * p3.z * reveal * reveal * flicker + ground * p5.y) +
                   p6.rgb * (p4.x * (ring + 0.6 * flash));
    if (front && p5.w > 0.0 && frame.envParams.w > 0.5) {
        // A glassy sheen: the sky the shell reflects, strongest at grazing angles.
        let refl = reflect(-v, n);
        let env = textureSampleLevel(prefilteredMap, iblSampler, envRotate(refl), 0.15 * frame.envParams.y).rgb;
        radiance = radiance + env * (p5.w * (0.04 + 0.96 * pow(1.0 - nv, 5.0)));
    }
    if (!front) {
        radiance = radiance * p2.z;
    }
    radiance = radiance * p0.a;
    if (cut) {
        radiance = vec3<f32>(0.0);
    }
    return shellLight(in, radiance);
}

// ---- Barrier ---------------------------------------------------------------------------------------
//   params[0] rgb = colour, a = intensity (envelope included)
//   params[1] zw = revealer positions in `shellExtra`: xyz world, w = weight
//   params[2] x = pattern (1 hex, 2 cells, 3 grid), y = cell size (m), z = scroll offset (m), w = idle
//   params[3] x = reveal radius (m), y = reveal gain, z = ground-line width (m), w = ground-line intensity
//   params[4] x = shape (0 wall, 1 box, 2 cylinder, 3 dome), y = edge glow, z = edge width (m),
//             w = rim
//   params[5] x = line width (cell units), y = cell flicker, z = far-side factor, w = fill

@fragment
fn fs_barrier(in: ShellVaryings, @builtin(front_facing) front: bool) -> ShellOut {
    let r = shells[in.instance];
    let p0 = r.params[0];
    let p2 = r.params[2];
    let p3 = r.params[3];
    let p4 = r.params[4];
    let p5 = r.params[5];
    let seed = r.params[1].x;
    let clock = r.params[1].y;
    let shape = p4.x;

    let scaleAxes = axisScale(r);
    let lp = in.local * scaleAxes;
    let cut = shape > 2.5 && in.local.y < -0.002;

    // Pattern coordinates in metres on the surface, scrolled upward. A cylinder is unrolled (its
    // circumference in metres by its height) so its cells do not stretch round the side.
    var q = lp;
    var nrm = normalize(in.meshNormal);
    if (shape > 1.5 && shape < 2.5) {
        let around = atan2(in.local.z, in.local.x) * 0.5 * (scaleAxes.x + scaleAxes.z);
        q = vec3<f32>(around, lp.y, 0.0);
        nrm = vec3<f32>(0.0, 0.0, 1.0);
    }
    q.y = q.y - p2.z;
    let cells = cellPattern(q / max(p2.y, 0.01), nrm, max(p2.x, 1.0), seed);
    let cellLine = borderLine(cells.x, max(p5.x, 0.005));

    // Where it borders nothing: a wall's frame, a box's edges, a cylinder's two rims, a dome's base.
    let room = scaleAxes - abs(lp);
    var border = 1e6;
    if (shape < 0.5) {
        border = min(room.x, room.y);
    } else if (shape < 1.5) {
        let lo = min(room.x, min(room.y, room.z));
        let hi = max(room.x, max(room.y, room.z));
        border = room.x + room.y + room.z - lo - hi; // the middle one: distance to the nearest edge
    } else if (shape < 2.5) {
        border = room.y;
    } else {
        border = max(lp.y, 0.0);
    }
    let edge = exp(-max(border, 0.0) / max(p4.z, 0.01));

    // Proximity: what stands near the barrier lights it.
    var near = 0.0;
    let firstRevealer = u32(r.params[1].z);
    let revealers = u32(r.params[1].w);
    let radius = max(p3.x, 0.01);
    for (var i = 0u; i < revealers; i = i + 1u) {
        let e = shellExtra[firstRevealer + i];
        near = near + e.w * (1.0 - smoothstep(0.0, radius, distance(in.world, e.xyz)));
    }
    near = clamp(near * p3.y, 0.0, 4.0);

    let v = normalize(frame.cameraPos.xyz - in.world);
    let nv = clamp(abs(dot(in.normal, v)), 0.0, 1.0);
    let rim = p4.w * pow(1.0 - nv, 3.0);
    let ground = intersectionGlow(in.clip, in.world, p3.z);
    let flicker = 1.0 - p5.y * (0.5 + 0.5 * sin(clock * 6.2831853 * (0.4 + cells.y) + cells.y * 50.0));
    let reveal = p2.w + near + 0.6 * ground;
    var radiance = p0.rgb * (cellLine * reveal * flicker + p5.w * reveal + edge * p4.y + ground * p3.w + rim);
    if (!front && shape > 0.5) {
        radiance = radiance * p5.z;
    }
    radiance = radiance * p0.a;
    if (cut) {
        radiance = vec3<f32>(0.0);
    }
    return shellLight(in, radiance);
}
