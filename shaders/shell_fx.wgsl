// SHELL, phase 2 (Effect Library Wave 3): the shading kinds of Light Beam, Halo, Bubble, Portal and
// Reality Tear (Charge-Up draws with Plasma and Glare). Each entry point is its own pipeline
// (ADR-118); rendering/shell_renderer.cpp says how each pipeline's state differs. This file includes
// shell.wgsl for the record, the bindings, `vs_shell` and the shared helpers, and adds:
//
//   fs_beam    a truncated cone of lit air integrated along the view ray inside a box proxy
//   vs_glare / fs_glare
//              a camera-facing glare disc, tail, rays and a dispersed ring, faded by the linear depth
//              at the SOURCE (not depth-tested per pixel: glare is in the eye, not in the world)
//   fs_ring    a glowing torus sphere-traced inside a box proxy
//   vs_bubble / fs_bubble
//              a wobbling thin-film sphere, composited OVER the frame (premultiplied, alpha = its
//              reflectance), with a hole racing round it when it pops
//   fs_portal  an opening on a disc: a noisy rim, a parallax interior; OVER, and it writes depth
//   fs_tear    a jagged crack on a quad (a polyline in the side data): edges and a void; OVER, writes depth
//
// The OVER kinds return premultiplied colour: `rgb` is light (already weighted by coverage), `a` how
// much of what is behind is hidden. They discard what they do not cover, because a fragment of a
// depth-writing kind that is not discarded writes depth.

#include "shell.wgsl"

// ---- shared -----------------------------------------------------------------------------------------

// Light ADDED at a point (fogged at `fogAt`): into every target, the bloom seeing `glow` of it.
fn fxAdd(in: ShellVaryings, radiance: vec3<f32>, glow: f32, fogAt: vec3<f32>) -> ShellOut {
    let lit = max(radiance, vec3<f32>(0.0)) * fogTransmit(fogAt);
    var out: ShellOut;
    out.color = vec4<f32>(lit, 0.0);
    out.normalRoughness = vec4<f32>(0.0);
    out.velocity = vec4<f32>(screenVelocityAt(in.clip, in.prevClip), 0.0, clamp(luminance(lit), 0.0, 1.0));
    let e = lit * glow;
    out.emission = vec4<f32>(e, clamp(luminance(e), 0.0, 1.0));
    out.ids = 0u;
    return out;
}

// A surface composited OVER the frame: `light` premultiplied, `alpha` its coverage. The fog is a
// surface's: its own light dimmed by the air in front of it, and the air's in-scatter over what it
// covers (what is behind was fogged when it was drawn).
fn fxOver(in: ShellVaryings, light: vec3<f32>, alpha: f32, glow: f32) -> ShellOut {
    let a = clamp(alpha, 0.0, 1.0);
    let lit = max(light, vec3<f32>(0.0)) * fogTransmit(in.world);
    let rgb = lit + a * applyFog(vec3<f32>(0.0), in.world);
    var out: ShellOut;
    out.color = vec4<f32>(rgb, a);
    out.normalRoughness = vec4<f32>(0.0);
    out.velocity = vec4<f32>(screenVelocityAt(in.clip, in.prevClip), 0.0, a);
    out.emission = vec4<f32>(lit * glow, a);
    out.ids = 0u;
    return out;
}

// A fixed per-pixel offset (interleaved gradient noise of the pixel, not of the frame): marched steps
// turn to fine grain instead of banding, and it does not shimmer.
fn fxJitter(p: vec2<f32>) -> f32 {
    return fract(52.9829189 * fract(dot(p, vec2<f32>(0.06711056, 0.00583715))));
}

// The ray's interval through the record's unit box [-1, 1]^3, in world metres along the unit
// direction `dirW` from the camera (the box-space ray `o + d t` with d NOT unit), from t >= 0.
fn fxBoxInterval(o: vec3<f32>, d: vec3<f32>) -> vec2<f32> {
    let safe = select(d, vec3<f32>(1e-8), abs(d) < vec3<f32>(1e-8));
    let inv = 1.0 / safe;
    let ta = (vec3<f32>(-1.0) - o) * inv;
    let tb = (vec3<f32>(1.0) - o) * inv;
    let lo = min(ta, tb);
    let hi = max(ta, tb);
    return vec2<f32>(max(max(max(lo.x, lo.y), lo.z), 0.0), min(min(hi.x, hi.y), hi.z));
}

fn fxInsideBox(o: vec3<f32>) -> bool {
    return all(abs(o) < vec3<f32>(1.0));
}

// The scene's distance along the unit ray `dirW` under this fragment (very large without the depth).
fn fxSceneT(fragCoord: vec4<f32>, dirW: vec3<f32>) -> f32 {
    if (!depthAvailable()) {
        return 1e30;
    }
    let along = max(dot(dirW, frame.cameraForward.xyz), 1e-4);
    return sceneViewDepth(fragCoord) / along;
}

fn fxRotate(p: vec2<f32>, a: f32) -> vec2<f32> {
    let c = cos(a);
    let s = sin(a);
    return vec2<f32>(c * p.x - s * p.y, s * p.x + c * p.y);
}

// Sparse stars on a plane: a bright point per lucky cell, twinkling. `density` 0..1.
fn fxStars(uv: vec2<f32>, scale: f32, density: f32, clock: f32, seed: f32) -> f32 {
    let g = uv * scale;
    let cell = floor(g);
    let h = shellHash(cell, seed);
    let lucky = step(1.0 - 0.06 * density, h);
    let at = cell + vec2<f32>(shellHash(cell + 3.1, seed), shellHash(cell + 7.7, seed));
    let d = length(g - at);
    let twinkle = 0.65 + 0.35 * sin(clock * (1.5 + 3.0 * h) + h * 40.0);
    return lucky * exp(-d * d * 60.0) * twinkle;
}

// The sky as the frame SHOWS it in direction `dir` (what a film or an opening reflects or reveals):
// the analytic sky's gradient when it is drawn analytically, else the prefiltered cube at `mip` with
// the environment's intensity, and the visible sky's own intensity when the sky stands behind the
// scene (shaders/skybox.wgsl, minus the sun's disc, which callers add as a glint).
fn fxSkySeen(dir: vec3<f32>, mip: f32) -> vec3<f32> {
    if (frame.envParams.w < 0.5) {
        return frame.skyParams.rgb;
    }
    let d = envRotate(dir);
    var c: vec3<f32>;
    if (frame.skySunRadiance.w >= 0.5) {
        let haze = exp(-saturate(d.y) / max(frame.skyZenithColor.w, 1e-3));
        let gradient = mix(frame.skyZenithColor.rgb, frame.skyHorizonColor.rgb, haze);
        c = mix(frame.skyGroundColor.rgb, gradient, smoothstep(-0.03, 0.03, d.y));
    } else {
        c = textureSampleLevel(prefilteredMap, iblSampler, d, mip).rgb * frame.params.w;
    }
    return c * select(1.0, frame.skyExtra.z, frame.skyExtra.y >= 0.5);
}

// ---- Light Beam ------------------------------------------------------------------------------------
//   model     the box bounding the cone: col1 = -axis * length / 2 (local +Y is the SOURCE), col0 and
//             col2 across it, scaled by the base radius
//   params[0] rgb = colour, a = brightness through the core near the source (envelope included)
//   params[2] x = core sharpness, y = edge softness, z = falloff along the length, w = end fade
//   params[3] x = dust, y = dust scale (1/m), z = depth fade (m), w = steps
//   params[4] xyz = dust drift (m/s), w = length (m)
//   params[5] x = the source radius over the base radius, y = the base radius (m)
// The cone in box space: x^2 + z^2 = (A - k y)^2 with k = (1 - a) / 2, A = a + k (radius a at the source,
// y = +1, and 1 at the end, y = -1).

@fragment
fn fs_beam(in: ShellVaryings, @builtin(front_facing) front: bool) -> ShellOut {
    let r = shells[in.instance];
    let cam = frame.cameraPos.xyz;
    let o = toLocalPoint(r, cam);
    // One face per pixel: the near faces from outside the box, the far faces from inside it.
    if (front == fxInsideBox(o)) {
        discard;
    }
    let dirW = normalize(in.world - cam);
    let d = toLocalDir(r, dirW);
    let box = fxBoxInterval(o, d);
    let sceneT = fxSceneT(in.clip, dirW);
    let t0 = box.x;
    let t1 = min(box.y, sceneT);
    if (t1 <= t0) {
        discard;
    }
    let a = clamp(r.params[5].x, 0.0, 0.95);
    let k = 0.5 * (1.0 - a);
    let big = a + k;
    let m = big - k * o.y;
    let nn = -k * d.y;
    let qa = d.x * d.x + d.z * d.z - nn * nn;
    let qb = 2.0 * (o.x * d.x + o.z * d.z - m * nn);
    let qc = o.x * o.x + o.z * o.z - m * m;
    // Inside the cone is f(t) <= 0. Split [t0, t1] at the roots and keep the sub-intervals inside; in
    // the box there is one nappe only, so they form one interval.
    var pts = array<f32, 4>(t0, t0, t1, t1);
    let disc = qb * qb - 4.0 * qa * qc;
    if (abs(qa) > 1e-9 && disc > 0.0) {
        let sq = sqrt(disc);
        let ra = (-qb - sq) / (2.0 * qa);
        let rb = (-qb + sq) / (2.0 * qa);
        pts[1] = clamp(min(ra, rb), t0, t1);
        pts[2] = clamp(max(ra, rb), t0, t1);
    } else if (abs(qa) <= 1e-9 && abs(qb) > 1e-9) {
        let ra = clamp(-qc / qb, t0, t1);
        pts[1] = ra;
        pts[2] = ra;
    }
    var lo = t1;
    var hi = t0;
    for (var i = 0; i < 3; i = i + 1) {
        let s0 = pts[i];
        let s1 = pts[i + 1];
        let tm = 0.5 * (s0 + s1);
        let inside = s1 > s0 && (qa * tm * tm + qb * tm + qc) <= 0.0;
        lo = select(lo, min(lo, s0), inside);
        hi = select(hi, max(hi, s1), inside);
    }
    if (hi <= lo) {
        discard;
    }

    let seed = u32(r.params[1].x);
    let clock = r.params[1].y;
    let shape = r.params[2];
    let air = r.params[3];
    let baseR = max(r.params[5].y, 1e-3);
    let steps = clamp(air.w, 4.0, 32.0);
    let n = u32(steps);
    let dt = (hi - lo) / steps;
    let jitter = fxJitter(in.clip.xy);
    let drift = r.params[4].xyz * clock;
    let soft = clamp(shape.y, 0.02, 1.0);
    var acc = 0.0;
    for (var i = 0u; i < n; i = i + 1u) {
        let t = lo + (f32(i) + jitter) * dt;
        let p = o + d * t;
        let rhoMax = max(big - k * p.y, 1e-4);
        let rho = length(p.xz) / rhoMax;
        let edge = 1.0 - smoothstep(1.0 - soft, 1.0, rho);
        let core = exp(-shape.x * rho * rho);
        let along = clamp(0.5 * (1.0 - p.y), 0.0, 1.0); // 0 at the source, 1 at the end
        // Brighter near its source (the light is densest where the cone is narrow), falling off along
        // it, fading out over its last stretch.
        let axial = (1.0 + 1.5 * exp(-along * 7.0)) * exp(-shape.z * along) *
                    (1.0 - smoothstep(1.0 - max(shape.w, 1e-3), 1.0, along));
        let wp = cam + dirW * t;
        let dust = fbm3(wp * air.y - drift, seed);
        let dustF = mix(1.0, smoothstep(0.2, 0.85, dust) * 2.2, air.x);
        var w = edge * core * axial * dustF;
        if (air.z > 0.0) {
            w = w * smoothstep(0.0, air.z, sceneT - t); // soft where it meets the ground
        }
        // Per metre, normalised by the local diameter: a chord straight across the beam integrates to
        // about its profile's mean, whatever the beam's width there.
        acc = acc + w * dt / (2.0 * rhoMax * baseR);
    }
    // Saturating: looking straight up the beam is bright, not unbounded.
    let radiance = r.params[0].rgb * r.params[0].a * (acc / (1.0 + 0.5 * acc));
    return fxAdd(in, radiance, 0.25, cam + dirW * (0.5 * (lo + hi)));
}

// ---- Halo: Glare -----------------------------------------------------------------------------------
//   params[0] rgb = colour, a = brightness at the centre (envelope included)
//   params[2] xyz = the source (world), w = the glare's radius (m)
//   params[3] x = core size, y = ring radius, z = ring width (all fractions of the radius), w = ring
//             brightness relative to the core
//   params[4] x = dispersion (0..1), y = the source depth slack (m), z = occlusion softness (m)
//   params[5] x = rays (0..1), y = how many

@vertex
fn vs_glare(in: ShellIn) -> ShellVaryings {
    let r = shells[in.instance];
    let centre = r.params[2].xyz;
    let radius = r.params[2].w;
    // The quad turned to face the camera that draws the frame.
    let world = centre + (frame.cameraRight.xyz * in.position.x + frame.cameraUp.xyz * in.position.y) * radius;
    var out: ShellVaryings;
    out.clip = frame.viewProj * vec4<f32>(world, 1.0);
    out.prevClip = frame.prevViewProj * vec4<f32>(world, 1.0);
    out.world = world;
    out.normal = -frame.cameraForward.xyz;
    out.local = in.position;
    out.instance = in.instance;
    out.meshNormal = in.normal;
    return out;
}

// How much of the source is visible: the linear depth in a 3x3 neighbourhood round its projection,
// against the source's own depth less its slack. 1 without the depth.
fn glareVisibility(src: vec3<f32>, slack: f32, softness: f32) -> f32 {
    if (!depthAvailable()) {
        return 1.0;
    }
    let clip = frame.viewProj * vec4<f32>(src, 1.0);
    if (clip.w <= 1e-4) {
        return 0.0;
    }
    let ndc = clip.xy / clip.w;
    let uv = vec2<f32>(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
    let size = vec2<f32>(textureDimensions(sceneLinearDepth, 0));
    let base = vec2<i32>(uv * size);
    let srcDepth = viewDepthOf(src) - slack;
    var sum = 0.0;
    for (var y = -1; y <= 1; y = y + 1) {
        for (var x = -1; x <= 1; x = x + 1) {
            let texel = clamp(base + vec2<i32>(x * 2, y * 2), vec2<i32>(0), vec2<i32>(size) - 1);
            let z = textureLoad(sceneLinearDepth, texel, 0).r;
            sum = sum + smoothstep(srcDepth - softness, srcDepth, z);
        }
    }
    // A source off the edge of the frame has no depth there to ask; it fades as it leaves.
    let outside = max(max(-uv.x, uv.x - 1.0), max(-uv.y, uv.y - 1.0));
    return (sum / 9.0) * (1.0 - smoothstep(0.0, 0.06, outside));
}

@fragment
fn fs_glare(in: ShellVaryings) -> ShellOut {
    let r = shells[in.instance];
    let q = in.local.xy;
    let rr = length(q);
    if (rr >= 1.0) {
        discard;
    }
    let p3 = r.params[3];
    let p4 = r.params[4];
    let p5 = r.params[5];
    let src = r.params[2].xyz;
    let vis = glareVisibility(src, p4.y, max(p4.z, 0.01));
    if (vis <= 1e-3) {
        discard;
    }
    let seed = u32(r.params[1].x);
    let coreSize = max(p3.x, 0.005);
    // A bounded core, then a soft glare tail windowed to reach zero at the quad's edge.
    let core = exp(-(rr * rr) / (coreSize * coreSize));
    let x = rr / (coreSize * 4.0);
    let tail = 1.0 / (1.0 + x * x) * (1.0 - smoothstep(0.35, 1.0, rr));
    // Fine radial streaks: noise of the angle only (a point on the unit circle, so it wraps).
    let ang = atan2(q.y, q.x);
    let streak = valueNoise(vec3<f32>(cos(ang), sin(ang), 0.0) * (p5.y * 0.16), seed);
    let rays = mix(1.0, 0.45 + 1.1 * streak * streak, p5.x);
    // The ring, its channels at slightly different radii: red outside, blue inside (a corona).
    let spread = p4.x * 0.08;
    let ringR = vec3<f32>(p3.y * (1.0 + spread), p3.y, p3.y * (1.0 - spread));
    let w = max(p3.z, 0.003);
    let dr = vec3<f32>(rr) - ringR;
    let ring = exp(-(dr * dr) / (w * w)) * p3.w;
    let col = r.params[0].rgb;
    let peak = r.params[0].a * vis;
    let hot = col * (core * peak);
    let soft = col * (0.3 * tail * rays * peak) + mix(col, vec3<f32>(1.0), 0.5) * ring * peak;
    let fog = fogTransmit(src);
    let lit = (hot + soft) * fog;
    var out: ShellOut;
    out.color = vec4<f32>(lit, 0.0);
    out.normalRoughness = vec4<f32>(0.0);
    out.velocity = vec4<f32>(screenVelocityAt(in.clip, in.prevClip), 0.0, clamp(luminance(lit), 0.0, 1.0) * 0.5);
    let e = (hot * 0.6 + soft * 0.15) * fog;
    out.emission = vec4<f32>(e, clamp(luminance(e), 0.0, 1.0));
    out.ids = 0u;
    return out;
}

// ---- Halo: Ring --------------------------------------------------------------------------------------
//   model     the box bounding the torus: col1 is the ring's axis (scaled by its half-height), col0
//             and col2 lie in its plane
//   params[0] rgb = colour, a = the tube's brightness (envelope included)
//   params[2] x = ring radius (m), y = tube radius (m), z = glow width (m), w = glow brightness
//   params[3] x = shimmer (0..1), y = shimmer speed, z = steps

@fragment
fn fs_ring(in: ShellVaryings, @builtin(front_facing) front: bool) -> ShellOut {
    let r = shells[in.instance];
    let cam = frame.cameraPos.xyz;
    let dirW = normalize(in.world - cam);
    // The angle one pixel subtends, taken before any branch (a derivative).
    let pixel = max(length(fwidth(dirW)), 1e-5);
    let o = toLocalPoint(r, cam);
    if (front == fxInsideBox(o)) {
        discard;
    }
    let d = toLocalDir(r, dirW);
    let box = fxBoxInterval(o, d);
    let sceneT = fxSceneT(in.clip, dirW);
    let t1 = min(box.y, sceneT);
    if (t1 <= box.x) {
        discard;
    }
    let sc = axisScale(r);
    let p2 = r.params[2];
    let ringR = p2.x;
    let tube = max(p2.y, 1e-4);
    let gw = max(p2.z, 1e-4);
    // Sphere tracing in metres (box space times the axis scales is a unit-speed ray), keeping the
    // nearest approach to the tube: the glow is a function of that distance alone, so the ring is no
    // brighter seen edge-on than face-on (a glow integrated along the ray would be).
    var t = box.x;
    var minD = 1e9;
    var minT = t;
    var minP = vec3<f32>(0.0);
    let n = u32(clamp(r.params[3].z, 8.0, 64.0));
    for (var i = 0u; i < n; i = i + 1u) {
        if (t >= t1) {
            break;
        }
        let pm = (o + d * t) * sc;
        let q = vec2<f32>(length(pm.xz) - ringR, pm.y);
        let sd = length(q) - tube;
        if (sd < minD) {
            minD = sd;
            minT = t;
            minP = pm;
        }
        if (sd < pixel * t * 0.25) {
            break;
        }
        t = t + max(sd * 0.8, gw * 0.2);
    }
    let footprint = pixel * minT;
    // The tube, anti-aliased by its distance; a hotter line along its middle; a soft glow round it.
    let cover = 1.0 - smoothstep(0.0, footprint * 1.5, minD);
    let centreD = max(minD + tube, 0.0) / max(tube, footprint);
    let hotLine = exp(-centreD * centreD * 2.0);
    let glow = exp(-max(minD, 0.0) / gw) * (1.0 - cover);
    let phi = atan2(minP.z, minP.x);
    let clock = r.params[1].y;
    let seed = u32(r.params[1].x);
    let run = valueNoise(vec3<f32>(cos(phi) * 3.0, sin(phi) * 3.0, clock * r.params[3].y), seed);
    let shimmer = mix(1.0, 0.55 + 0.9 * run, r.params[3].x);
    let col = r.params[0].rgb * r.params[0].a;
    let tubeLight = (col * 0.55 + mix(col, vec3<f32>(r.params[0].a), 0.5) * 0.6 * hotLine) * (cover * shimmer);
    let glowLight = col * (p2.w * 0.5 * glow);
    var out = fxAdd(in, tubeLight + glowLight, 0.0, cam + dirW * minT);
    let e = (tubeLight * 0.5 + glowLight * 0.2) * fogTransmit(cam + dirW * minT);
    out.emission = vec4<f32>(e, clamp(luminance(e), 0.0, 1.0));
    return out;
}

// ---- Bubble ------------------------------------------------------------------------------------------
//   params[0] rgb = tint, a = reflection (envelope included)
//   params[2] x = film thickness (nm), y = variation (nm), z = iridescence, w = film flow
//   params[3] x = wobble (fraction of the radius), y = wobble rate (Hz), z = pop progress (0 whole),
//             w = the envelope
//   params[4] xyz = where the pop starts (unit, the sphere's space), w = tint opacity
//   params[5] y = the far side's share, z = soft contact (m)

@vertex
fn vs_bubble(in: ShellIn) -> ShellVaryings {
    let r = shells[in.instance];
    let amount = r.params[3].x;
    let phase = r.params[1].y * r.params[3].y * 6.2831853 + r.params[1].x;
    let p = in.position;
    // Two low-order shape modes and a sway: a breathing prolate/oblate (l = 2), a twist and a lean.
    let y2 = 0.5 * (3.0 * p.y * p.y - 1.0);
    let s = amount * (sin(phase) * y2 + 0.7 * sin(phase * 1.37 + 1.7) * 2.0 * p.x * p.z +
                      0.5 * sin(phase * 0.83 + 4.1) * p.x * (2.5 * p.y * p.y - 0.5));
    let local = p * (1.0 + s);
    let world = (r.model * vec4<f32>(local, 1.0)).xyz;
    let c0 = r.model[0].xyz;
    let c1 = r.model[1].xyz;
    let c2 = r.model[2].xyz;
    let nrm = c0 * (in.normal.x / max(dot(c0, c0), 1e-20)) + c1 * (in.normal.y / max(dot(c1, c1), 1e-20)) +
              c2 * (in.normal.z / max(dot(c2, c2), 1e-20));
    var out: ShellVaryings;
    out.clip = frame.viewProj * vec4<f32>(world, 1.0);
    out.prevClip = frame.prevViewProj * vec4<f32>(world, 1.0);
    out.world = world;
    out.normal = normalize(nrm);
    out.local = p; // the film's pattern rides the undisplaced sphere
    out.instance = in.instance;
    out.meshNormal = in.normal;
    return out;
}

@fragment
fn fs_bubble(in: ShellVaryings, @builtin(front_facing) front: bool) -> ShellOut {
    let r = shells[in.instance];
    let film = r.params[2];
    let p3 = r.params[3];
    let p4 = r.params[4];
    let p5 = r.params[5];
    let seed = u32(r.params[1].x);
    let clock = r.params[1].y;
    let u = normalize(in.local);

    // The pop: a hole opening from one point and racing round the film behind a ragged, bright lip.
    var lip = 0.0;
    if (p3.z > 0.0) {
        let origin = normalize(p4.xyz);
        let ang = acos(clamp(dot(u, origin), -1.0, 1.0));
        let rag = (fbm3(u * 7.0 + vec3<f32>(clock * 3.0), seed) - 0.5) * 0.3;
        let gap = ang - p3.z * 3.4 - rag;
        if (gap < 0.0) {
            discard;
        }
        lip = exp(-gap / 0.07);
    }

    let v = normalize(frame.cameraPos.xyz - in.world);
    let n = select(-normalize(in.normal), normalize(in.normal), front);
    let cosI = clamp(abs(dot(n, v)), 0.0, 1.0);

    // Thickness: a base, swirling fBM, and drainage -- thicker low down, thinning to black at the top.
    let flow = flowCurl(u * 1.3, clock * film.w * 0.5, seed) * 0.35;
    let swirl = fbm3(u * 2.2 + flow + vec3<f32>(0.0, clock * film.w, 0.0), seed) * 2.0 - 1.0;
    let drain = 1.0 - 0.45 * u.y;
    let thick = max(film.x * drain + film.y * swirl, 0.0);
    // Two-beam thin-film interference at three wavelengths (soap: n = 1.33, a half-wave shift at the
    // first surface, so a vanishing film is black): R ~ 1 - cos(2 pi * OPD / lambda).
    let nFilm = 1.33;
    let cosT = sqrt(max(1.0 - (1.0 - cosI * cosI) / (nFilm * nFilm), 0.0));
    let opd = 2.0 * nFilm * thick * cosT;
    let lambda = vec3<f32>(650.0, 532.0, 450.0);
    let spectral = vec3<f32>(1.0) - cos(6.2831853 * opd / lambda);
    let grey = dot(spectral, vec3<f32>(1.0 / 3.0));
    let filmRgb = mix(vec3<f32>(grey), spectral, film.z);
    // Fresnel (Schlick; two air-water surfaces at normal incidence reflect about 4 %).
    let fres = 0.04 + 0.96 * pow(1.0 - cosI, 5.0);
    let refl = filmRgb * (fres * r.params[0].a);

    // What it reflects: the sky cubemap, and a pin-point of the sun or moon.
    let rdir = reflect(-v, n);
    let env = fxSkySeen(rdir, 0.0);
    let sunDot = max(dot(rdir, normalize(frame.skySun.xyz)), 0.0);
    let glint = pow(sunDot, 900.0) * 40.0 * frame.skySun.w;
    var light = env * refl + vec3<f32>(glint) * fres * filmRgb;
    var alpha = clamp(dot(refl, vec3<f32>(1.0 / 3.0)), 0.0, 1.0);
    // A tinted body (a water orb): it hides a little of what is behind and glows faintly with the sky.
    let body = p4.w;
    if (body > 0.0) {
        let ambient = fxSkySeen(n, frame.envParams.y);
        light = light + r.params[0].rgb * ambient * (body * 0.6);
        alpha = alpha + body * (1.0 - alpha);
    }
    // The pop's lip: the film bunching up as it retracts.
    light = light + (filmRgb * 0.5 + vec3<f32>(0.5)) * (lip * 0.6 * p3.w);
    alpha = max(alpha, lip * 0.5 * p3.w);
    if (!front) {
        light = light * p5.y;
        alpha = alpha * p5.y;
    }
    // Soft where it meets the ground.
    if (p5.z > 0.0 && depthAvailable()) {
        let gap = sceneViewDepth(in.clip) - viewDepthOf(in.world);
        let c = smoothstep(0.0, p5.z, gap);
        light = light * c;
        alpha = alpha * c;
    }
    return fxOver(in, light, alpha * p3.w, 0.35);
}

// ---- Portal -------------------------------------------------------------------------------------------
//   model     the disc proxy (1.3 x the opening): col0 = right, col1 = the normal, col2 = up x aspect
//   params[0] rgb = rim colour, a = the rim's hot edge (HDR)
//   params[2] rgb = interior colour, a = interior brightness
//   params[3] x = open (the opening's radius over the full radius), y = rim width, z = rim turbulence,
//             w = swirl
//   params[4] x = interior (0 nebula, 1 environment, 2 void), y = parallax depth (m), z = 1 / the
//             proxy's extent, w = aspect
//   params[5] rgb = second interior colour, w = stars
//   params[6] x = radius (m), y = the envelope

// The interior seen through the opening at `q` (units of the radius), `dirW` the view ray.
fn portalInterior(r: ShellRecord, q: vec2<f32>, dirW: vec3<f32>, clock: f32, seed: u32) -> vec3<f32> {
    let p4 = r.params[4];
    let p5 = r.params[5];
    let mode = p4.x;
    let swirl = r.params[3].w;
    let radius = max(r.params[6].x, 1e-3);
    let right = normalize(r.model[0].xyz);
    let nrm = normalize(r.model[1].xyz);
    let up = normalize(r.model[2].xyz);
    let dn = max(abs(dot(dirW, nrm)), 0.15);
    let slide = vec2<f32>(dot(dirW, right), dot(dirW, up) / max(p4.w, 1e-3)) / (dn * radius);
    let seedF = f32(seed);
    if (mode > 0.5 && mode < 1.5) {
        // Environment: the sky, turned about the portal's axis faster towards the middle.
        // A rigid turn plus a fixed spiral twist: a differential turn that grew with time would wind
        // the picture into ever tighter rings.
        let turn = clock * swirl * 0.5 + 0.8 * sign(swirl) / (0.3 + length(q));
        let c = cos(turn);
        let s = sin(turn);
        let sw = dirW * c + cross(nrm, dirW) * s + nrm * dot(nrm, dirW) * (1.0 - c);
        // The sky beyond, and a faint glow of the interior colour deepening towards the middle so a
        // dark sky still reads as an opening.
        // Another place's sky: the view is lifted above that world's horizon, so looking down into
        // a gate still shows sky rather than the ground under it.
        let lifted = normalize(vec3<f32>(sw.x, abs(sw.y) + 0.3, sw.z));
        let sky = fxSkySeen(lifted, 1.0);
        // Over it, the gate's own field: faint shimmering bands in its colour, strongest in the middle.
        let rq = length(q);
        let bands = fbm3(vec3<f32>(fxRotate(q, turn * 0.5) * 3.0, clock * 0.3), seed);
        let field = r.params[2].rgb * ((0.12 + 0.35 * smoothstep(0.45, 0.8, bands)) * (1.0 - 0.6 * rq));
        return (sky * mix(vec3<f32>(1.0), r.params[2].rgb, 0.35) + field) * r.params[2].a;
    }
    // Nebula (0) or void (2): three parallax layers, each turned by a differential swirl.
    var col = vec3<f32>(0.0);
    let clouds = select(1.0, 0.12, mode > 1.5);
    let starGain = select(0.6, 1.4, mode > 1.5) * p5.w;
    for (var i = 0; i < 3; i = i + 1) {
        let fi = f32(i);
        let depth = p4.y * (0.25 + 0.375 * fi);
        let uv = q + slide * depth;
        // Each layer turns rigidly (deeper ones slower) through a fixed spiral twist, so the arms
        // stream round without winding up over time.
        let turn = clock * swirl * (0.6 - 0.15 * fi) + 1.4 * sign(swirl) / (0.3 + length(uv)) + fi * 1.7;
        let w = fxRotate(uv, turn);
        let c1 = fbm3(vec3<f32>(w * 2.2, fi * 3.1 + clock * 0.05), seed);
        let c2 = fbm3(vec3<f32>(w * 4.3 + 7.0, fi * 5.3 - clock * 0.04), seed);
        let dens = smoothstep(0.32, 0.78, c1);
        let hue = mix(r.params[2].rgb, p5.rgb, smoothstep(0.3, 0.7, c2));
        let fade = 1.0 - 0.28 * fi;
        col = col + hue * (dens * clouds * fade * 0.9);
        col = col + mix(hue, vec3<f32>(1.0), 0.7) * fxStars(w, 9.0 + 5.0 * fi, starGain, clock, seedF + fi * 13.0);
    }
    return col * r.params[2].a;
}

@fragment
fn fs_portal(in: ShellVaryings) -> ShellOut {
    let r = shells[in.instance];
    let p3 = r.params[3];
    let q = in.local.xz / max(r.params[4].z, 1e-3);
    let rho = length(q);
    let aa = max(fwidth(rho), 1e-4);
    let seed = u32(r.params[1].x);
    let clock = r.params[1].y;
    let open = max(p3.x, 1e-3);
    let phi = atan2(q.y, q.x);
    let around = vec3<f32>(cos(phi), sin(phi), 0.0);
    // The rim's radius wobbles with its angle, the wobble turning and boiling with time.
    let wob = fbm3(around * 2.4 + vec3<f32>(0.0, 0.0, clock * 0.35), seed) * 2.0 - 1.0;
    let edgeR = open * (1.0 + p3.z * 0.14 * wob);
    let w = max(p3.y * (0.5 + 0.5 * open), 0.004);
    let d = rho - edgeR;
    if (d > w + 2.0 * aa) {
        discard;
    }
    // Filaments streaming round the rim.
    let spun = phi + clock * p3.w * 1.4;
    let fil = fbm3(vec3<f32>(cos(spun) * 4.0, sin(spun) * 4.0, rho * 9.0 - clock * 0.9), seed + 7u);
    let hot = exp(-(d * d) / (0.03 * w * w + aa * aa));
    let band = exp(-abs(d) / (0.5 * w)) * (0.2 + 0.8 * smoothstep(0.3, 0.7, fil));
    let rimCol = r.params[0].rgb;
    let rimLight = r.params[0].a * (mix(rimCol, vec3<f32>(1.0), 0.65) * hot + rimCol * band * 0.75);
    // Inside: the interior, darker towards the rim (it reads as a hole, not a painted disc).
    let insideMask = 1.0 - smoothstep(-aa, aa, d);
    let dirW = normalize(in.world - frame.cameraPos.xyz);
    var interior = vec3<f32>(0.0);
    if (insideMask > 0.0) {
        interior = portalInterior(r, q / open, dirW, clock, seed);
        interior = interior * mix(0.2, 1.0, 1.0 - smoothstep(0.15 * edgeR, edgeR, rho));
    }
    let alpha = 1.0 - smoothstep(0.3 * w, 0.85 * w + aa, d);
    if (alpha < 0.01) {
        discard;
    }
    let light = interior * insideMask + rimLight * alpha;
    var out = fxOver(in, light, alpha, 0.0);
    out.emission = vec4<f32>((rimLight * alpha * 0.6 + interior * insideMask * 0.08) * fogTransmit(in.world), alpha);
    return out;
}

// ---- Reality Tear ------------------------------------------------------------------------------------
//   model     the quad in the tear's plane: col0 = across * half-width, col1 = along * half-length
//   params[1] zw = the crack's 32 points in `shellExtra` (eight vec4, four half-float pairs each:
//             metres across and along)
//   params[0] rgb = edge colour, a = the hot line's brightness (flicker included)
//   params[2] rgb = interior colour, a = interior brightness
//   params[3] x = the gap's half-width (m, open included), y = edge width (m), z = hot line width (m),
//             w = chroma
//   params[4] x = interior (0 void, 1 other world, 2 glitch), y = points, z = length (m), w = open
//   params[5] x = half-width, y = half-length (m), z = parallax depth (m), w = this opening's seed
//   params[6] rgb = second interior colour, w = the envelope

fn tearPoint(first: u32, i: u32) -> vec2<f32> {
    let v = shellExtra[first + i / 4u];
    let lane = i % 4u;
    var bits = bitcast<u32>(v.x);
    bits = select(bits, bitcast<u32>(v.y), lane == 1u);
    bits = select(bits, bitcast<u32>(v.z), lane == 2u);
    bits = select(bits, bitcast<u32>(v.w), lane == 3u);
    return unpack2x16float(bits);
}

fn tearInterior(r: ShellRecord, p: vec2<f32>, dirW: vec3<f32>, clock: f32, seed: u32) -> vec3<f32> {
    let mode = r.params[4].x;
    let inside = r.params[2];
    let second = r.params[6].rgb;
    let seedF = r.params[5].w;
    if (mode > 1.5) {
        // Glitch: rows shifted by a hash of the row and the step, blocks of palette colour, scanlines.
        // In units of the gap's width, so the blocks are the same few across whatever its size.
        let unit = max(r.params[3].x, 0.02);
        let g = p / unit;
        let stepT = floor(clock * 9.0);
        let row = floor(g.y * 2.5);
        let shift = (shellHash(vec2<f32>(row, stepT), seedF) - 0.5) * 1.2;
        let cell = vec2<f32>(floor((g.x + shift) * 3.0), row);
        let h = shellHash(cell + vec2<f32>(stepT * 0.37), seedF + 3.0);
        var col = mix(inside.rgb, second, step(0.5, h));
        col = select(col, vec3<f32>(1.0), h > 0.93);
        col = select(col, vec3<f32>(0.0), h < 0.25);
        let scan = 0.75 + 0.25 * sin(g.y * 40.0);
        return col * scan * inside.a;
    }
    // Void (0) and Other World (1): parallax behind the plane.
    let across = normalize(r.model[0].xyz);
    let along = normalize(r.model[1].xyz);
    let nrm = normalize(r.model[2].xyz);
    let dn = max(abs(dot(dirW, nrm)), 0.15);
    let slide = vec2<f32>(dot(dirW, across), dot(dirW, along)) / dn;
    var col = inside.rgb * 0.02;
    // In units of the tear's half-length, so its clouds and stars keep their size on screen as the
    // tear is scaled.
    let unit = max(r.params[5].y, 0.1);
    for (var i = 0; i < 2; i = i + 1) {
        let fi = f32(i);
        let uv = (p + slide * (r.params[5].z * (0.5 + fi) * unit * 0.25)) / unit;
        if (mode > 0.5) {
            let c1 = fbm3(vec3<f32>(uv * 3.0, fi * 2.3 + clock * 0.06), seed);
            let c2 = fbm3(vec3<f32>(uv * 6.0 + 5.0, fi * 4.1 - clock * 0.05), seed);
            col = col + mix(inside.rgb, second, smoothstep(0.3, 0.7, c2)) * smoothstep(0.35, 0.8, c1) * (0.6 - 0.25 * fi);
        } else {
            col = col + inside.rgb * smoothstep(0.45, 0.9, fbm3(vec3<f32>(uv * 2.0, fi + clock * 0.03), seed)) * 0.25;
        }
        col = col + mix(inside.rgb, vec3<f32>(1.0), 0.75) * fxStars(uv, 14.0 + fi * 10.0, 0.9, clock, seedF + fi * 7.0) * 0.9;
    }
    return col * inside.a;
}

@fragment
fn fs_tear(in: ShellVaryings) -> ShellOut {
    let r = shells[in.instance];
    let p3 = r.params[3];
    let p4 = r.params[4];
    let p5 = r.params[5];
    let p = in.local.xy * p5.xy; // metres: (across, along)
    let aa = max(length(fwidth(p)), 1e-5);
    let seed = u32(r.params[1].x);
    let clock = r.params[1].y;
    let first = u32(r.params[1].z);
    let count = min(u32(p4.y), u32(r.params[1].w) * 4u);

    // Distance to the crack and where along it (its points advance along its length, so the nearest
    // point's `along` is where we are on it).
    var best = 1e9;
    var at = 0.0;
    var prev = tearPoint(first, 0u);
    for (var i = 1u; i < 32u; i = i + 1u) {
        let cur = tearPoint(first, min(i, max(count, 2u) - 1u));
        let ab = cur - prev;
        let t = clamp(dot(p - prev, ab) / max(dot(ab, ab), 1e-10), 0.0, 1.0);
        let c = prev + ab * t;
        let dd = length(p - c);
        let nearer = dd < best;
        best = select(best, dd, nearer);
        at = select(at, c.y, nearer);
        prev = cur;
    }
    let s = clamp(at / max(p4.z, 1e-3) + 0.5, 0.0, 1.0);
    let bandW = max(p3.y, 1e-3);
    // The gap: lens-shaped along the crack (pointed ends), its lips roughened by noise.
    let taper = pow(max(sin(3.14159265 * s), 0.0), 0.6);
    let rough = fbm3(vec3<f32>(at * 2.5, clock * 0.3, 0.0), seed);
    let gapHalf = p3.x * taper * (0.7 + 0.6 * rough);
    let e = best - gapHalf; // < 0 inside the gap
    if (e > bandW * 5.0 + aa) {
        discard;
    }
    // The edge: a white-hot line falling off to the edge colour, its channels split by Chroma.
    let off = p3.w * bandW * 0.35;
    let e3 = vec3<f32>(e - off, e, e + off);
    let hw = max(p3.z, 1e-4);
    let hot = exp(-(e3 * e3) / (hw * hw + aa * aa));
    let outside = exp(-max(e3, vec3<f32>(0.0)) / bandW);
    let lip = exp(min(e3, vec3<f32>(0.0)) / (bandW * 0.5));
    let body = select(lip, outside, e3 > vec3<f32>(0.0));
    let edgeCol = r.params[0].rgb;
    let hotCol = mix(edgeCol, vec3<f32>(1.0), 0.75);
    let edgeLight = r.params[0].a * (hotCol * hot + edgeCol * body * 0.45) * r.params[6].w;
    // Inside: the interior.
    let insideMask = 1.0 - smoothstep(-aa, aa, e);
    var interior = vec3<f32>(0.0);
    if (insideMask > 0.0) {
        interior = tearInterior(r, p, normalize(in.world - frame.cameraPos.xyz), clock, seed);
    }
    let edgeCover = clamp(max(max(hot.x, hot.y), hot.z) * 0.8, 0.0, 0.8);
    let alpha = insideMask + (1.0 - insideMask) * edgeCover;
    if (alpha < 0.01 && luminance(edgeLight) < 0.004) {
        discard;
    }
    let light = interior * insideMask + edgeLight;
    var out = fxOver(in, light, alpha, 0.0);
    out.emission = vec4<f32>((edgeLight * 0.55 + interior * insideMask * 0.05) * fogTransmit(in.world), alpha);
    return out;
}
