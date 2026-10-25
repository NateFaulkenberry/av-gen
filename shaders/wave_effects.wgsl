// World effects (ADR-207): the per-fragment half of a spatial phenomenon propagating through the
// world. Included by pbr_shade.wgsl, which is the one file entities, skinned characters, the
// procedural scatter and raymarched SDF surfaces all shade through -- so terrain, vegetation, rocks,
// mushrooms, props and characters receive a wave without one line of per-asset code.
//
// The contract is **additive**: this file returns radiance to add to the shaded colour and to the
// emission target, and never touches base colour, roughness or the normal. `finalSurface =
// normalSurface + propagationContribution` is what keeps a material looking like itself under a
// wave, and it is what lets an existing emissive surface be *amplified* rather than replaced.
//
// The data comes from `frame.waves`, packed by world::packWave; the struct here and
// the one in rendering/scene_renderer.hpp are asserted to the same size on the C++ side.
//
// Two propagation kinds, because they are the two distance metrics:
//
//   DirectionalWave  d = dot(p - origin, axis)      a front sweeping along an axis
//   RadialWave       d = length((p - origin).xz)    a ripple in the ground plane
//
// ADR-1662 adds a third, PathWave: the arc along an eight-point chain of a river's own polyline round the
// front (`wavePathHit`). It is drawn three ways, each in its own function: on land (`wavesAtSurface`, the
// facets beside the path lit by their orientation to it), on water (`wavePathWater`: a core, a halo and a
// head, which the water draws under its reflection), and as the wake's ripples in the water's normal
// (`wavePathDisturb`). The water skips the land term (`wavesAtSurface(..., true)`).
//
// The radial metric is horizontal on purpose. Measuring in the ground plane and masking by height
// is what makes a ripple *follow the terrain* -- every fragment is evaluated where its own surface
// is, so the ring climbs a bank because the bank is there, with no terrain query, no projection and
// no screen-space circle.
//
// **Temporal stability.** Nothing here reads a frame counter or a wall clock: the front's position,
// the rainbow phase and the sparkle phase all arrive already computed from the transport clock. The
// sparkle cells are static in world space and each cell's brightness is a smooth function of how
// near the front is, so the pattern does not crawl when the camera moves -- and it is faded out with
// view distance, which is its entire anti-aliasing. A cell smaller than a pixel is removed, not
// sampled.

const kWaveTau: f32 = 6.28318531;

// Deliberately this module's own hash rather than noise.wgsl's `pcg3d`: noise.wgsl reaches pbr.wgsl
// and procedural.wgsl only through fields.wgsl, which sdf_raymarch.wgsl does not include, and a
// helper that exists in three of the four modules that shade is a compile error waiting for the
// fourth.
fn waveHash3(p: vec3<f32>) -> vec3<f32> {
    let q = vec3<f32>(dot(p, vec3<f32>(127.1, 311.7, 74.7)),
                      dot(p, vec3<f32>(269.5, 183.3, 246.1)),
                      dot(p, vec3<f32>(113.5, 271.9, 124.6)));
    return fract(sin(q) * 43758.5453123);
}

fn waveLuma(c: vec3<f32>) -> f32 { return dot(c, vec3<f32>(0.2126, 0.7152, 0.0722)); }

// A cosine palette (the Iñigo Quílez form): three phase-shifted cosines of one parameter. Procedural
// rather than a colour list, smooth by construction, and -- because it is a pure function of the
// distance travelled and of a phase that came from the transport clock -- it cannot shimmer.
fn waveRainbow(t: f32, saturation: f32, brightness: f32) -> vec3<f32> {
    let c = 0.5 + 0.5 * cos(kWaveTau * (vec3<f32>(t) + vec3<f32>(0.0, 0.33333, 0.66667)));
    return mix(vec3<f32>(1.0), c, clamp(saturation, 0.0, 1.0)) * max(brightness, 0.0);
}

struct WaveResult {
    radiance: vec3<f32>, // add to the shaded colour and to the emission target
    bloom: f32,          // take the max with the object's own bloom weight
};

// How much of the effect this surface takes.
//
// Three weights and a derived groundness, not a named category per asset -- see ADR-207. What the
// shader knows for free is whether this draw is the procedural scatter (`kProceduralDraw`, which
// ADR-138 already put in every module) and which way the surface faces. Vegetation, rocks and
// mushroom caps are the scatter; a near-horizontal upward-facing authored surface is ground; and
// everything else authored -- props, heroes, characters -- is a surface. A finer table needs a
// semantic category on `scene::Entity` that nothing currently carries.
fn waveGain(response: vec4<f32>, normal: vec3<f32>) -> f32 {
    let groundness = smoothstep(0.55, 0.92, normal.y);
    let authored = mix(response.z, response.x, groundness);
    return select(authored, response.y, kProceduralDraw);
}


// ---- ADR-1662: path waves ----------------------------------------------------------------------

fn wavePathPoint(e: Wave, i: u32) -> vec2<f32> {
    let v = e.path[i / 2u];
    return select(v.zw, v.xy, (i & 1u) == 0u);
}

struct WavePathHit {
    u: f32,             // metres behind the front along the path (negative: ahead of it)
    lateral: f32,       // metres from the path, in the ground plane
    nearest: vec2<f32>, // the nearest point of the path (xz)
};

// The projection of q onto the chain: point 0 is ahead of the front, point 1 is the front, 2-7 the wake.
fn wavePathHit(e: Wave, q: vec2<f32>) -> WavePathHit {
    // Pass 11 (the owner's "seam in the wake"): the nearest point on the chain was taken from whichever segment was
    // nearest, so where two segments are equally near -- the line bisecting a bend in the chain, straight in the water
    // -- the arc length jumped from one segment's value to the other's, and everything keyed to it (the wake's decay,
    // its ripples' phase, the light's tail) stepped along that line. Now the segments are blended by a soft minimum of
    // their distances (a few metres wide), so the arc length, the lateral distance and the nearest point all vary
    // smoothly across the bisector.
    var h: WavePathHit;
    var a = wavePathPoint(e, 0u);
    var arc = -distance(a, wavePathPoint(e, 1u));
    var dMin = 1.0e9;
    var ds: array<f32, 7>;
    var us: array<f32, 7>;
    var cs: array<vec2<f32>, 7>;
    var valid: array<bool, 7>;
    for (var i: u32 = 1u; i < 8u; i = i + 1u) {
        let b = wavePathPoint(e, i);
        let ab = b - a;
        let len = length(ab);
        let k = i - 1u;
        valid[k] = len > 1.0e-3;
        if (valid[k]) {
            let t = clamp(dot(q - a, ab) / (len * len), 0.0, 1.0);
            let c = a + ab * t;
            ds[k] = distance(q, c);
            us[k] = arc + t * len;
            cs[k] = c;
            dMin = min(dMin, ds[k]);
            arc = arc + len;
        }
        a = b;
    }
    let soft = 6.0;
    var wSum = 0.0;
    var u = 0.0;
    var lat = 0.0;
    var nearest = vec2<f32>(0.0);
    for (var k: u32 = 0u; k < 7u; k = k + 1u) {
        if (valid[k]) {
            let w = exp(-(ds[k] - dMin) / soft);
            wSum = wSum + w;
            u = u + w * us[k];
            lat = lat + w * ds[k];
            nearest = nearest + w * cs[k];
        }
    }
    if (wSum <= 0.0) {
        h.lateral = 1.0e9;
        h.u = 0.0;
        h.nearest = q;
        return h;
    }
    h.u = u / wSum;
    h.lateral = lat / wSum;
    h.nearest = nearest / wSum;
    return h;
}

// Whether q can be touched by path wave e at all: within the wake plus the widest reach of the front.
fn wavePathNear(e: Wave, q: vec2<f32>, reach: f32) -> bool {
    return distance(q, e.originKind.xz) < e.shape.y + reach + 4.0 * e.shape.x + 60.0;
}

// The leading edge widens to the distance the front moves in about a frame, so a fast front does not strobe.
fn wavePathEdge(e: Wave) -> f32 {
    return max(e.shape.x, e.sparkle.y * 0.02);
}

// The light fades in with distance from the camera (`shape.z` metres; 0 = off): the first frames of a front
// would otherwise fill the bottom of the frame.
fn wavePathNearFade(e: Wave, viewDistance: f32) -> f32 {
    return wavePathFadeIn(e.shape.z, viewDistance);
}

fn wavePathFadeIn(distance: f32, viewDistance: f32) -> f32 {
    if (distance <= 0.0) {
        return 1.0;
    }
    return smoothstep(distance * 0.3, distance, viewDistance);
}

// The land beside the path, facet by facet: the core's colour (and a share of the head's) by how squarely
// the surface faces the path's nearest point, across `vertical.z` metres and up `vertical.y` (down
// `vertical.x`: a riverbed under the water takes it too).
fn wavePathLand(e: Wave, worldPos: vec3<f32>, normal: vec3<f32>, viewDistance: f32) -> vec3<f32> {
    let reach = e.vertical.z;
    let dy = worldPos.y - e.originKind.y;
    // Pass 11: the land's light (above the path's level) and the bed's (below it) cross-fade over the metre and a half
    // under the level instead of switching at it -- on a bank facet that runs into the water the switch was a straight
    // line along the waterline, seen through the ripples
    let aboveW = smoothstep(-1.5, 0.0, dy);
    if (reach <= 0.0 || (e.rainbow.z <= 0.0 && e.rainbow.w <= 0.0) || !wavePathNear(e, worldPos.xz, reach)) {
        return vec3<f32>(0.0);
    }
    let h = wavePathHit(e, worldPos.xz);
    let edgeW = wavePathEdge(e);
    let wake = max(e.shape.y, 1.0e-3);
    // (cut where every term below is already ~0: the head's edge term is exp(-16) at four edge widths ahead)
    if (h.u < -4.0 * edgeW || h.u > wake || h.lateral > reach) {
        return vec3<f32>(0.0);
    }
    let lead = smoothstep(-edgeW, 0.0, h.u);
    let tail = pow(clamp(1.0 - h.u / wake, 0.0, 1.0), e.shape.w);
    let t = h.u / edgeW;
    let edge = exp(-t * t);
    let light = e.color.rgb * (lead * tail) + e.edge.rgb * (edge * 0.35);
    // Above the path's level (banks, slopes, mountains): across `reach`, lit by how squarely each facet faces the
    // path's nearest point, sharpened so neighbouring planes differ. Below it (a riverbed seen through the water): a
    // band about the halo's width under the light, lit from the head itself -- a low, grazing light that catches the
    // planes tilted toward it and leaves the others dark.
    // ADR-1666: sparkle.w, how far the light reaches under the water (0: ADR-1663's 40 m)
    let bedReach = select(40.0, e.sparkle.w, e.sparkle.w > 0.0);
    var land = 0.0;
    if (aboveW > 0.0 && e.rainbow.z > 0.0) {
        let x = h.lateral / (reach * 0.5);
        let above = 1.0 - smoothstep(e.vertical.y * 0.35, max(e.vertical.y, 1.0e-3), max(dy, 0.0));
        // the light from the path's nearest point raised most of the way to the surface's own height (pass 4): mostly
        // level, so a valley wall facing the path takes it squarely
        let source = vec3<f32>(h.nearest.x, mix(e.originKind.y + 2.0, worldPos.y, 0.75), h.nearest.y);
        let toLight = source - worldPos;
        let facing = max(dot(normal, toLight / max(length(toLight), 1.0e-3)), 0.0);
        land = e.rainbow.z * exp(-x * x) * above * (0.05 + 0.95 * facing * facing);
    }
    var bed = 0.0;
    if (aboveW < 1.0 && e.rainbow.w > 0.0) {
        let x = h.lateral / max(e.vertical.w * 2.5, bedReach);
        let below = 1.0 - smoothstep(bedReach * 0.5, bedReach, -dy); // ADR-1663: 40 m below the level; ADR-1666
        let source = e.originKind.xyz + vec3<f32>(0.0, 2.0, 0.0);
        let toLight = source - worldPos;
        let facing = max(dot(normal, toLight / max(length(toLight), 1.0e-3)), 0.0);
        // Shallow under the water (the top 6 m: fish, the banks' first metres), the light is taken whatever the facing,
        // so a small body under a passing beam lights as a whole instead of showing as a silhouette against the lit bed
        let shallow = smoothstep(-8.0, -4.0, dy);
        bed = e.rainbow.w * exp(-x * x) * below * mix(2.5 * facing, 1.6, shallow);
    }
    // The land and the bed fade in over 40% of the water's near fade, and wait for the front to travel out past it
    // (axisFront.w, metres travelled): lit from a head just launched, the near bed and banks washed the bottom of the frame
    let launched = wavePathFadeIn(e.shape.z, e.axisFront.w);
    return light * (mix(bed, land, aboveW) * launched * wavePathFadeIn(e.shape.z * 0.4, viewDistance));
}

struct WavePathWater {
    under: vec3<f32>,   // drawn under the reflection (the water mixes it into its body)
    surface: vec3<f32>, // drawn on top of it
};

// The light in the water: a narrow core (or twin filaments), a soft halo, a white-hot head at the leading
// edge; `rainbow.x` of it under the reflection and the rest on top, so it reads at grazing distance too.
fn wavePathWater(worldPos: vec3<f32>, footprint: f32) -> WavePathWater {
    var out: WavePathWater;
    out.under = vec3<f32>(0.0);
    out.surface = vec3<f32>(0.0);
    let count = u32(frame.waveCount.x + 0.5);
    if (count == 0u) {
        return out;
    }
    let viewDistance = distance(frame.cameraPos.xyz, worldPos);
    for (var i: u32 = 0u; i < count; i = i + 1u) {
        let e = frame.waves[i];
        if (abs(e.originKind.w - 2.0) > 0.5) { // path waves only
            continue;
        }
        // ADR-1666: the splash throws light -- a flash where the front launched and a bright ring running out with the
        // splash's rings, in the wave's own colour, gone in about a second (sparkle.x, its gain over the splash: 0 none)
        if (e.extra.z > 0.0 && e.sparkle.x > 0.0) {
            let age = e.vertical.x;
            let rs = distance(worldPos.xz, e.extra.xy);
            let ringR = 25.0 + kSplashSpeed * age;
            if (age < 4.0 && rs < ringR + 80.0) {
                let k = e.extra.z * e.sparkle.x;
                let x = (rs - ringR) / (6.0 + 10.0 * age);
                let ring = exp(-x * x) * exp(-age / (0.65 * kSplashLife));
                let cf = rs / 24.0;
                let flash = exp(-cf * cf) * exp(-age / 0.18);
                // dimmed under 250 m from the camera, so the ring running toward it does not wash the bottom of the frame
                let nearDim = 0.25 + 0.75 * smoothstep(80.0, 250.0, viewDistance);
                let splashLight = (e.color.rgb * (0.8 * ring + 2.0 * flash) + e.edge.rgb * (0.3 * flash)) * (k * nearDim);
                out.under = out.under + splashLight * 0.4;
                out.surface = out.surface + splashLight * 0.6;
            }
        }
        let halo = max(e.vertical.w, 1.0e-3);
        if (!wavePathNear(e, worldPos.xz, 4.0 * halo + e.edge.w)) {
            continue;
        }
        let h = wavePathHit(e, worldPos.xz);
        let edgeW = wavePathEdge(e);
        let wake = max(e.shape.y, 1.0e-3);
        // (cut where the light is already ~0: exp(-16) at four halos across and four edge widths ahead; pass 11, were
        // three and two, where the head's edge term still showed a faint straight line)
        if (h.u < -4.0 * edgeW || h.u > wake || h.lateral > 4.0 * halo + e.edge.w) {
            continue;
        }
        let lead = smoothstep(-edgeW, 0.0, h.u);
        let tail = pow(clamp(1.0 - h.u / wake, 0.0, 1.0), e.shape.w);
        let t = h.u / edgeW;
        let edge = exp(-t * t);
        // the core never narrower than about a pixel and a half, so a far beam does not break up
        let coreW = max(e.color.w, footprint * 1.5);
        let lat = select(h.lateral, abs(h.lateral - e.edge.w), e.edge.w > 0.0);
        let c = lat / coreW;
        let core = exp(-c * c);
        let g = h.lateral / halo;
        let haloAmt = exp(-g * g);
        let light = (e.color.rgb * (core + 0.15 * haloAmt) * (lead * tail) + e.edge.rgb * (core * edge))
                    * wavePathNearFade(e, viewDistance);
        out.under = out.under + light;
        out.surface = out.surface + light * (1.0 - e.rainbow.x);
    }
    return out;
}

// The wake's disturbance as a slope in the water plane (stage 3 of the owner's river brief): behind each
// front, crests spread out across the river from the path at 260 m/s and die in about half a second; they
// wander and break with position, so they never read as ruled rings, and the mirror heals behind them.
// ADR-1663 / ADR-1666: the splash's rings run out at kSplashSpeed m/s and fade over about kSplashLife seconds (pass 10: 300 and
// 1.2, were 170 and 0.8 -- the splash now lands behind the camera and its rings must reach well into view)
const kSplashSpeed: f32 = 300.0;
const kSplashLife: f32 = 1.2;

fn wavePathDisturb(worldPos: vec3<f32>) -> vec2<f32> {
    var g = vec2<f32>(0.0);
    let count = u32(frame.waveCount.x + 0.5);
    let near = 1.0 - smoothstep(150.0, 700.0, distance(frame.cameraPos.xyz, worldPos));
    for (var i: u32 = 0u; i < count; i = i + 1u) {
        let e = frame.waves[i];
        if (abs(e.originKind.w - 2.0) > 0.5) { // path waves only
            continue;
        }
        // ADR-1663: the splash, rings spreading from where the front launched, as if something heavy had been dropped
        // in the water there; strongest at once, gone in about two seconds
        if (e.extra.z > 0.0) {
            let age = e.vertical.x;
            let r = distance(worldPos.xz, e.extra.xy);
            let front = 25.0 + kSplashSpeed * age;
            if (r < front + 40.0) {
                let pp = worldPos.xz;
                // broken and wandering, as the wake's crests are: never a ruled set of rings (brief s8)
                let wobble = 2.6 * sin(pp.x * 0.019 + pp.y * 0.023) + 1.4 * sin(pp.y * 0.041 - pp.x * 0.013);
                let rings = cos(kWaveTau / 22.0 * (r - kSplashSpeed * age) + wobble);
                let broken = smoothstep(0.15, 0.85, 0.5 + 0.5 * sin(pp.x * 0.047 + sin(pp.y * 0.021) * 2.6) *
                                                             sin(pp.y * 0.039 + sin(pp.x * 0.017) * 2.2));
                let shape = smoothstep(front + 40.0, front, r) * exp(-age / kSplashLife) * (0.35 + 0.65 * broken);
                let away = (pp - e.extra.xy) / max(r, 1.0e-3);
                g = g + away * (e.extra.z * shape * rings);
            }
        }
        if (e.rainbow.y <= 0.0 || !wavePathNear(e, worldPos.xz, 300.0)) {
            continue;
        }
        let h = wavePathHit(e, worldPos.xz);
        if (h.u <= 0.0 || h.u > e.shape.y) {
            continue;
        }
        let tau = h.u / max(e.sparkle.z, 100.0);     // seconds since the front passed this point
        // pass 10 (the owner's "ripple seam"): the wake's ripples ended at a hard line where the wake does (u = its
        // length) and began at one at the front (u = 0): straight edges across the ripple field trailing each pulse.
        // Feathered in over the first 25 m and out over the last 40% of the wake, so no line shows.
        let feather = smoothstep(0.0, 25.0, h.u) * smoothstep(e.shape.y, e.shape.y * 0.6, h.u);
        let reach = 30.0 + 260.0 * tau;
        let d = h.lateral;
        if (d > reach) {
            continue;
        }
        let p = worldPos.xz;
        let decay = exp(-tau / 0.45) * smoothstep(reach, reach * 0.7, d);
        let broken = smoothstep(0.15, 0.85, 0.5 + 0.5 * sin(p.x * 0.047 + sin(p.y * 0.021) * 2.6) *
                                                       sin(p.y * 0.039 + sin(p.x * 0.017) * 2.2));
        let phase = kWaveTau / 14.0 * (d - 260.0 * tau) + 2.4 * sin(p.x * 0.019 + p.y * 0.023);
        let radial = (p - h.nearest) / max(d, 1.0e-3);
        // ADR-1663: the wake heavier near the camera (extra.w times at the camera, 1x by 700 m)
        let gain = 1.0 + e.extra.w * near;
        g = g + radial * (e.rainbow.y * gain * decay * broken * feather * cos(phase));
    }
    return g;
}


// ---- ADR-1663: light balls -------------------------------------------------------------------------

// The light a ball casts on a surface: falling to half at `color.w` metres, cut from 70% to 100% of its range, and
// weighted by how squarely the surface faces it (a little reaches every surface, so a facet turned away still
// shows the ball has passed). Facets, the riverbed through the water, and the water itself all take it.
fn waveBallLight(e: Wave, worldPos: vec3<f32>, normal: vec3<f32>) -> vec3<f32> {
    let toBall = e.originKind.xyz - worldPos;
    let d = length(toBall);
    if (d > e.shape.y) {
        return vec3<f32>(0.0);
    }
    let r = d / max(e.color.w, 1.0e-3);
    let fall = (1.0 / (1.0 + r * r)) * (1.0 - smoothstep(e.shape.y * 0.7, e.shape.y, d));
    let facing = max(dot(normal, toBall / max(d, 1.0e-3)), 0.0);
    return e.color.rgb * (fall * (0.12 + 0.88 * facing));
}

fn wavesAt(worldPos: vec3<f32>, normal: vec3<f32>, emission: vec3<f32>) -> WaveResult {
    return wavesAtSurface(worldPos, normal, emission, false);
}

// `onWater`: the caller is the water, which draws a path wave itself (`wavePathWater`, `wavePathDisturb`).
fn wavesAtSurface(worldPos: vec3<f32>, normal: vec3<f32>, emission: vec3<f32>, onWater: bool) -> WaveResult {
    var out: WaveResult;
    out.radiance = vec3<f32>(0.0);
    out.bloom = 0.0;
    // Uniform across the draw, so a frame with no effects executes this compare and nothing else.
    let count = u32(frame.waveCount.x + 0.5);
    if (count == 0u) {
        return out;
    }
    let viewDistance = distance(frame.cameraPos.xyz, worldPos);
    for (var i: u32 = 0u; i < count; i = i + 1u) {
        let e = frame.waves[i];
        if (e.originKind.w > 2.5) { // ADR-1663: a light ball
            out.radiance = out.radiance + waveBallLight(e, worldPos, normal) * waveGain(e.response, normal);
            let toP = worldPos - frame.cameraPos.xyz;
            let dist = length(toP);
            out.radiance = out.radiance + waveBallGlow(e, frame.cameraPos.xyz, toP / max(dist, 1.0e-3), dist);
            continue;
        }
        if (e.originKind.w > 1.5) { // ADR-1662: a path wave
            if (!onWater) {
                let land = wavePathLand(e, worldPos, normal, viewDistance);
                out.radiance = out.radiance + land * waveGain(e.response, normal);
            }
            continue;
        }
        let toP = worldPos - e.originKind.xyz;

        // ---- the distance metric ----
        var d: f32;
        var lateral: f32;
        if (e.originKind.w < 0.5) { // DirectionalWave
            d = dot(toP, e.axisFront.xyz);
            lateral = length(toP - e.axisFront.xyz * d);
        } else {                    // RadialWave
            d = length(toP.xz);
            lateral = 0.0;
        }

        let frontWidth = max(e.shape.x, 1.0e-3);
        let trail = max(e.shape.y, 1.0e-3);
        // How far the front has passed this point by. Negative: the wave has not arrived.
        let u = e.axisFront.w - d;
        if (u < -frontWidth || u > trail || d > e.shape.z) {
            continue;
        }

        // ---- the band ----
        // Both edges are smooth: §21 rules out a harsh boundary, and a hard edge on a wave crossing
        // a 200 m valley is the single most obvious way to make it read as a decal.
        let lead = smoothstep(-frontWidth, 0.0, u);
        let tail = pow(clamp(1.0 - u / trail, 0.0, 1.0), e.shape.w);
        var body = lead * tail;
        let rings = e.vertical.z;
        if (rings > 0.0) {
            // Secondary ripples inside the trail, never below zero, so the wave breathes rather than
            // banding into separate discs.
            let phase = clamp(u / trail, 0.0, 1.0);
            body = body * (0.55 + 0.45 * cos(kWaveTau * rings * phase));
        }
        // The leading edge itself: a narrow bright band at the front, which is what makes a front
        // read as a front rather than as a gradient.
        let edgeWidth = max(frontWidth * 0.32, 0.05);
        let t = u / edgeWidth;
        let edgeBand = exp(-t * t);

        // ---- masks ----
        // Past the range the wave is gone; the fade starts well before it so nothing pops.
        let rangeFade = 1.0 - smoothstep(e.shape.z * 0.65, e.shape.z, d);
        // Never behind the source along the axis: a beam that paints the world behind the camera is
        // a beam nobody asked for.
        let ahead = select(1.0, smoothstep(-frontWidth * 0.5, frontWidth * 0.5, d), e.originKind.w < 0.5);
        // The vertical band, opening out with distance so a ripple can climb a bank instead of
        // clipping at the first slope.
        let halfExtent = max(e.vertical.x + e.vertical.y * d, 0.05);
        let vmask = 1.0 - smoothstep(halfExtent * 0.55, halfExtent, abs(worldPos.y - e.originKind.y));
        // Lateral falloff, for a directional wave that wants to be a beam rather than a plane.
        var lat = 1.0;
        if (e.vertical.w > 0.0) {
            lat = 1.0 - smoothstep(e.vertical.w * 0.45, e.vertical.w, lateral);
        }
        let mask = rangeFade * ahead * vmask * lat;
        if (mask <= 1.0e-4) {
            continue;
        }

        // ---- colour ----
        // `e.color.rgb` already carries intensity and the lifetime envelope. Rainbow replaces the
        // *hue* and keeps that magnitude, which is why the Rainbow style leaves the colour white.
        var tint = e.color.rgb;
        if (e.edge.w > 0.5) {
            tint = tint * waveRainbow(d * e.rainbow.x + e.rainbow.y, e.rainbow.z, e.rainbow.w);
        }

        // ---- sparkle ----
        var spark = 0.0;
        if (e.sparkle.x > 0.0 && edgeBand > 2.0e-3) {
            // The fade is the anti-aliasing: past here a cell is smaller than a pixel and sampling
            // it would print exactly the shimmer §8 forbids.
            let fade = 1.0 - smoothstep(e.color.w * 0.55, e.color.w, viewDistance);
            if (fade > 0.0) {
                let p = worldPos * e.sparkle.x;
                let cell = floor(p);
                let r = waveHash3(cell);
                // A jittered centre inside the cell, so the pattern is not a lattice.
                let centre = cell + 0.25 + 0.5 * r;
                let blob = 1.0 - smoothstep(e.sparkle.y * 0.35, max(e.sparkle.y, 1.0e-3), length(p - centre));
                // Per-cell twinkle. The phase came from the transport clock, so two renders of the
                // same second twinkle identically.
                let twinkle = 0.45 + 0.55 * sin(kWaveTau * (r.x + e.sparkle.w));
                spark = blob * max(twinkle, 0.0) * e.sparkle.z * edgeBand * fade;
            }
        }

        let gain = waveGain(e.response, normal);
        // Existing emission is amplified rather than overwritten (§10): a mushroom that already
        // glows glows harder as the wave crosses it.
        let amplified = emission * e.response.w * body;
        let contribution = (tint * body + e.edge.rgb * edgeBand + e.edge.rgb * spark) * gain + amplified;
        out.radiance = out.radiance + contribution * mask;
    }
    // A bright effect blooms even on a surface whose own bloom weight is zero, which is most of the
    // ground. Clamped, because the bloom mask is a weight and not a radiance.
    out.bloom = clamp(waveLuma(out.radiance) * 0.6, 0.0, 1.0);
    return out;
}
