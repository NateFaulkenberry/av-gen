// Atmospheric effects (ADR-230): the per-ray half of a celestial phenomenon.
//
// ADR-207's surface waves are an additive term on a *shaded surface*. These are not, and cannot be:
// the sky has no surface, so a comet and an aurora are things you see instead of geometry. What they
// have instead of a surface position is a **view ray**, and everything in this file is a function of
// one -- an origin at the camera and a unit direction -- evaluated in a direction-space draw that
// sits at the far plane with depth test on and depth write off.
//
// Three consequences of that choice are worth stating, because they are the reasons for it:
//
//  * **The far plane does not apply.** `scene::Camera::farPlane` is 200 m in this engine and a comet
//    is two kilometres away. Real geometry there would be clipped; a ray is not.
//  * **Terrain occludes it for free.** The draw is depth-tested against the scene that already
//    rendered, so a ridge in front of the aurora silhouettes it and the aurora's base disappears
//    behind the valley wall. There is no horizon seam to author because there is no horizon edge --
//    the depth buffer is the horizon.
//  * **It costs nothing where it is not.** Only unoccluded sky fragments run at all, and a comet
//    rejects in three instructions on every ray that misses its trail.
//
// **Temporal stability.** Nothing here reads a frame counter or a wall clock. Every phase -- the
// comet's position along its curve, its wisp flow, its fragment twinkle, the aurora's drift -- is
// packed on the CPU from the transport second, so an offline render of second N is identical to a
// realtime playthrough of second N. The two patterns that could crawl are both anchored to the
// world rather than to the phenomenon: a comet's wisps are displaced by a function of the trail
// point's *world position*, and its shed fragments are indexed by *absolute arc length from the
// launch point*, so a fragment stays where it was shed and the comet flies on past it.
//
// **Anti-aliasing is angular, not distance-based.** `pixelAngle` is the screen-space derivative of
// the view ray, taken once in uniform control flow at the top of `atmosphereSkyAt`. A comet's tail
// is never thinner than a pixel and a shed fragment smaller than one is faded out rather than
// sampled -- which is the same rule `wave_effects.wgsl` applies to its sparkle, stated in the units
// that actually govern it rather than in metres of view distance.

const kAtmosTau: f32 = 6.28318531;
const kAtmosPi: f32 = 3.14159265;

// Tuned so a tail intensity of ~5 lands just over Glowmere's bloom threshold of 1.0 in exposed
// units. It is a normalisation over the march, not an artistic control: the accumulation is divided
// by the step count so that the quality setting changes smoothness and never exposure.
const kAtmosTailGain: f32 = 6.0;

// ---- shared helpers ----------------------------------------------------------------------------

// This module's own hashes rather than noise.wgsl's, for the reason wave_effects.wgsl gives: the
// modules that include this one do not all reach noise.wgsl, and a helper that exists in three of
// four consumers is a compile error waiting for the fourth.
fn atmosHash21(p: vec2<f32>) -> f32 {
    return fract(sin(dot(p, vec2<f32>(127.1, 311.7))) * 43758.5453123);
}

fn atmosHash3(p: vec3<f32>) -> vec3<f32> {
    let q = vec3<f32>(dot(p, vec3<f32>(127.1, 311.7, 74.7)),
                      dot(p, vec3<f32>(269.5, 183.3, 246.1)),
                      dot(p, vec3<f32>(113.5, 271.9, 124.6)));
    return fract(sin(q) * 43758.5453123);
}

fn atmosLuma(c: vec3<f32>) -> f32 { return dot(c, vec3<f32>(0.2126, 0.7152, 0.0722)); }

// The cosine palette ADR-207 chose, unchanged: three phase-shifted cosines of one parameter, smooth
// by construction, and -- because its argument is a distance plus a phase that came from the
// transport clock -- unable to shimmer.
fn atmosRainbow(t: f32, saturation: f32, brightness: f32) -> vec3<f32> {
    let c = 0.5 + 0.5 * cos(kAtmosTau * (vec3<f32>(t) + vec3<f32>(0.0, 0.33333, 0.66667)));
    return mix(vec3<f32>(1.0), c, clamp(saturation, 0.0, 1.0)) * max(brightness, 0.0);
}

fn atmosNoise2(p: vec2<f32>) -> f32 {
    let i = floor(p);
    let f = p - i;
    let u = f * f * (3.0 - 2.0 * f);
    let a = atmosHash21(i);
    let b = atmosHash21(i + vec2<f32>(1.0, 0.0));
    let c = atmosHash21(i + vec2<f32>(0.0, 1.0));
    let d = atmosHash21(i + vec2<f32>(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

fn atmosFbm2(p: vec2<f32>, octaves: i32) -> f32 {
    var sum = 0.0;
    var amp = 0.5;
    var q = p;
    for (var i = 0; i < octaves; i = i + 1) {
        sum = sum + amp * atmosNoise2(q);
        q = q * 2.03 + vec2<f32>(11.3, 7.7);
        amp = amp * 0.5;
    }
    return sum;
}

// Smooth vector turbulence from six sines rather than a value-noise gradient.
//
// This is a deliberate trade and not a shortcut. It is evaluated once per march step per comet --
// up to 144 times on a pixel with six comets on it -- and a trilinear value noise is eight hashes a
// sample where this is six sines. What the effect needs from it is *a smooth, divergence-ish,
// world-anchored displacement*, and sines give that with no lattice to show through. A noise with
// visible cells is the failure mode here; a sine field's failure mode is periodicity at a scale far
// larger than a comet.
fn atmosCurl(p: vec3<f32>) -> vec3<f32> {
    return vec3<f32>(sin(p.y * 1.7 + p.z * 0.9) + 0.5 * sin(p.y * 3.9 - p.x * 1.3),
                     sin(p.z * 1.3 + p.x * 1.1) + 0.5 * sin(p.z * 4.3 - p.y * 1.7),
                     sin(p.x * 1.9 + p.y * 0.7) + 0.5 * sin(p.x * 3.1 - p.z * 1.1));
}

struct AtmosResult {
    radiance: vec3<f32>, // add to the sky colour
    bloom: f32,          // how much of it the emission target should carry
};

// ---- comets ------------------------------------------------------------------------------------

// Where the comet is at arc length `s` from its launch point. The same curve the CPU evaluates in
// `world::cometPositionAt`, so the ground track and the visible head cannot disagree.
//
// A **great-circle arc at constant distance**, not a chord. A chord between two points on a sphere
// passes through the interior, so a comet on one dives towards the anchor and its apparent elevation
// swings far outside the two numbers that were authored -- the first implementation launched at 31
// degrees, aimed at 5, and passed overhead at 49. Slerp keeps the distance fixed, so what an artist
// types is what the sky shows.
//
// It extrapolates correctly past either end: the sines carry on round the circle, which is what a
// comet outliving its crossing should do. The bow is clamped to the crossing so it does not bend the
// comet back through its own path afterwards.
fn atmosCometCurve(c: AtmosComet, s: f32) -> vec3<f32> {
    let pathLength = max(c.dir1Path.w, 1.0);
    let u = s / pathLength;
    let omega = c.arc.y;
    var d = c.dir0Tail.xyz;
    let sinOmega = sin(omega);
    if (omega > 1.0e-4 && abs(sinOmega) > 1.0e-6) {
        d = (sin((1.0 - u) * omega) * c.dir0Tail.xyz + sin(u * omega) * c.dir1Path.xyz) / sinOmega;
    }
    if (u > 0.0 && u < 1.0) {
        let bow = sin(kAtmosPi * u);
        // Perpendicular to the arc's plane, for the sideways bow; the zenith for the lift.
        let normal = normalize(cross(c.dir0Tail.xyz, c.dir1Path.xyz) + vec3<f32>(0.0, 1.0e-5, 0.0));
        d = d + vec3<f32>(0.0, 1.0, 0.0) * (c.arc.z * bow) + normal * (c.arc.w * bow);
    }
    return c.anchorTravel.xyz + normalize(d) * c.arc.x;
}

fn atmosphereCometsAt(ro: vec3<f32>, rd: vec3<f32>, pixelAngle: f32) -> AtmosResult {
    var out: AtmosResult;
    out.radiance = vec3<f32>(0.0);
    out.bloom = 0.0;
    // Uniform across the draw, so a frame with no comets executes this compare and nothing else.
    let count = u32(frame.atmosCount.x + 0.5);
    if (count == 0u) {
        return out;
    }
    let steps = i32(frame.atmosCount.z + 0.5);
    let fsteps = f32(steps);

    for (var i: u32 = 0u; i < count; i = i + 1u) {
        let c = frame.comets[i];
        let travelled = c.anchorTravel.w;
        let tailLen = max(c.dir0Tail.w, 1.0);
        let headR = max(c.core.w, 0.5);
        let tailW = max(c.shape.x, 0.5);

        let head = atmosCometCurve(c, travelled);
        let back = atmosCometCurve(c, max(travelled - tailLen, 0.0));

        // ---- the coarse rejection ----
        // A sphere around the whole trail, tested against the ray. Most sky pixels are nowhere near
        // any comet, and without this every one of them would march every tail: this is the
        // difference between the effect costing something where it is and costing it everywhere.
        let centre = (head + back) * 0.5;
        let bound = length(head - back) * 0.5 + tailW * 3.0 + c.halo.w * 2.0 + c.arc.x * (abs(c.arc.z) + abs(c.arc.w));
        let toC = centre - ro;
        let tc = dot(toC, rd);
        if (tc < -bound) {
            continue; // entirely behind the camera
        }
        if (length(toC - rd * max(tc, 0.0)) > bound) {
            continue;
        }

        // A frame across the trail, for the shed fragments below. Once per comet rather than once
        // per fragment: it is the same basis for all of them. Taken from the head's own direction of
        // travel -- a finite difference along the arc -- because the arc has no single axis.
        let tangent = normalize(head - back + vec3<f32>(1.0e-4, 0.0, 0.0));
        var side = cross(tangent, vec3<f32>(0.0, 1.0, 0.0));
        if (length(side) < 1.0e-4) {
            side = vec3<f32>(1.0, 0.0, 0.0);
        }
        side = normalize(side);
        let lift = normalize(cross(side, tangent));

        var radiance = vec3<f32>(0.0);

        // The head's own place in the hue ramp. Rainbow mode used to tint only the tail, which left
        // a white-hot core dragging a coloured streak -- the one part of the effect an eye goes
        // straight to was the one part with no colour in it. The head takes the ramp at its own arc
        // position, so the whole comet is one hue that travels with it.
        var rainbowHead = vec3<f32>(1.0);
        if (c.rainbow.w > 0.0) {
            rainbowHead = atmosRainbow(travelled * c.rainbow.x + c.rainbow.y, c.rainbow.z, c.rainbow.w);
        }

        // ---- the head ----
        let toH = head - ro;
        let th = dot(toH, rd);
        if (th > 0.0) {
            let perp = length(toH - rd * th);
            // A gaussian core rather than a disc, so the brightest thing in the sky has no edge to
            // alias, and never narrower than a pixel for the same reason.
            let coreR = max(headR, th * pixelAngle * 0.8);
            let core = exp(-(perp * perp) / (coreR * coreR));
            // The coma: a soft inverse-square wash squared into a tighter falloff. This is the part
            // bloom has something wide to work with, and the part that makes the head read as a
            // light source rather than as a bright dot.
            let hr = max(c.halo.w, 1.0);
            let halo = (hr * hr) / (perp * perp + hr * hr);
            // ADR-369. The coma is an inverse-square wash SQUARED, and it is never zero at any
            // distance -- while the `th > 0.0` above it is a half-space test through the eye, whose
            // boundary projects to an exactly straight line across the frame. So a small but finite
            // wash was being cut to nothing along that line, and the owner saw a crisp diagonal
            // terminator across the sky with a brighter wedge on one side.
            //
            // The fix is not to widen the test. It is to make the falloff compactly supported, so
            // the test discards only what is already zero -- the same discipline as the ADR-367
            // soft-particle fade: "off" has to be off AT the boundary, not near it. `facing` is the
            // cosine of the angle off the head, so the window closes smoothly well before the ray
            // turns side-on and the cut fires. Within about 20 degrees of the head, where the coma
            // is anything anyone can see, `facing` is above 0.94 and this multiplies by exactly 1.
            let facing = clamp(th / max(length(toH), 1.0e-4), 0.0, 1.0);
            let coma = halo * halo * smoothstep(0.0, 0.30, facing);
            radiance = radiance + (c.core.rgb * core + c.halo.rgb * coma) * rainbowHead;
        }

        // ---- the tail ----
        let spacing = tailLen / fsteps;
        var accum = vec3<f32>(0.0);
        for (var s = 0; s < steps; s = s + 1) {
            // 0 at the head, 1 at the tail's end.
            let f = (f32(s) + 0.5) / fsteps;
            let arc = travelled - f * tailLen;
            if (arc < 0.0) {
                continue; // before the launch point: the tail grows out of it rather than popping in
            }
            var q = atmosCometCurve(c, arc);
            if (c.shape.y > 0.0) {
                // World-anchored wisps: a function of where the trail point *is*, so the flowing
                // structure does not crawl when the camera moves. The phase came from the transport
                // clock, so two renders of the same second displace identically.
                q = q + atmosCurl(q * c.shape.z + vec3<f32>(c.shape.w)) * (c.shape.y * f);
            }
            let toQ = q - ro;
            let tq = dot(toQ, rd);
            if (tq <= 0.0) {
                continue;
            }
            let perp = length(toQ - rd * tq);
            // The width grows down the tail, is never below the sample spacing, and is never below
            // a pixel. The first floor is what stops a low step count printing the tail as a string
            // of beads -- which is why lowering the quality costs smoothness and not exposure. The
            // second is the anti-aliasing.
            let width = max(max(headR + tailW * pow(f, 0.7), spacing * 0.6), tq * pixelAngle * 0.75);
            let g = exp(-(perp * perp) / (width * width));
            let bright = pow(1.0 - f, c.tail.w);
            var tint = c.tail.rgb;
            if (c.rainbow.w > 0.0) {
                // The hue ramps along the *absolute* arc length, so the colours belong to places in
                // the trail rather than to positions behind the head -- the band stays put in the
                // sky as the comet flies through it.
                tint = tint * atmosRainbow(arc * c.rainbow.x + c.rainbow.y, c.rainbow.z, c.rainbow.w);
            }
            accum = accum + tint * (g * bright);
        }
        radiance = radiance + accum * (kAtmosTailGain / fsteps);

        // ---- shed fragments ----
        if (c.sparkle.x > 0.0 && c.sparkle.z > 0.0) {
            let density = c.sparkle.x;
            let fragR = max(c.sparkle.y, 0.5);
            let k1 = floor(travelled * density);
            let k0 = max(floor((travelled - tailLen) * density), 0.0);
            // Bounded, because a long tail with a fine density would otherwise be an unbounded loop
            // in a fragment shader. 24 fragments along a trail is already more than reads as
            // individual sparks.
            let n = min(k1 - k0, 24.0);
            var spark = vec3<f32>(0.0);
            for (var m = 0.0; m < n; m = m + 1.0) {
                // Cells on the **absolute arc grid**: fragment k sits where the comet was when it
                // had flown (k + 0.5) / density metres, which is a fixed world position. Indexing
                // from the head instead would slide every fragment along with the comet, which is
                // exactly the crawl §3.4 forbids.
                let k = k0 + m;
                let r = atmosHash3(vec3<f32>(k, k * 1.37 + 5.1, k * 2.71 + 9.3));
                let r2 = atmosHash3(vec3<f32>(k * 0.73 + 2.9, k * 1.91 + 7.7, k * 3.17 + 1.3));
                // The cell's own position is jittered *along* the arc as well as across it. Evenly
                // spaced fragments on a straight tail read as a string of pearls -- a runway, not a
                // comet -- and no amount of lateral scatter fixes a regular rhythm. Still a pure
                // function of the cell index, so the fragment is still nailed to its world position.
                let arc = (k + 0.5 + (r2.x - 0.5) * 0.85) / density;
                let f = clamp((travelled - arc) / tailLen, 0.0, 1.0);
                let spread = (headR + tailW * pow(f, 0.7)) * 1.5;
                let q = atmosCometCurve(c, arc) + side * ((r.x - 0.5) * 2.0 * spread) +
                        lift * ((r.y - 0.5) * 2.0 * spread);
                let toQ = q - ro;
                let tq = dot(toQ, rd);
                if (tq <= 0.0) {
                    continue;
                }
                let perp = length(toQ - rd * tq);
                // Fragments are not all the same size either, for the same reason.
                let rr = fragR * (0.45 + 1.1 * r2.y);
                let blob = exp(-(perp * perp) / (rr * rr));
                // The angular-size fade is the whole anti-aliasing: a fragment smaller than a pixel
                // is removed rather than sampled, because sampling it is how a sparkle becomes a
                // shimmer.
                let aa = smoothstep(0.5, 1.5, (rr / tq) / pixelAngle);
                let twinkle = 0.35 + 0.65 * sin(kAtmosTau * (r.z + c.sparkle.w));
                spark = spark + vec3<f32>(blob * max(twinkle, 0.0) * aa * pow(1.0 - f, 1.5));
            }
            // Fragments take the core's colour: they are pieces of the comet, and giving them a
            // colour of their own is a control that only ever gets set back to this.
            radiance = radiance + c.core.rgb * rainbowHead * spark * (c.sparkle.z / max(c.core.w, 1.0));
        }

        out.radiance = out.radiance + radiance;
    }
    // A comet is a light source and should bloom whatever the sky's own bloom weight is (Glowmere
    // grants the sky 0.1, which would all but erase it). Clamped, because the emission target's
    // alpha is a weight and not a radiance.
    out.bloom = clamp(atmosLuma(out.radiance) * 0.5, 0.0, 1.0);
    return out;
}

// ---- aurora ------------------------------------------------------------------------------------

// One of the sixteen spectrum bins. Copied to a local first because dynamic indexing of a vector
// read straight out of a uniform struct member is the one thing in this file worth not finding out
// about per backend.
fn atmosAuroraBand(a: AtmosAurora, index: i32) -> f32 {
    let k = clamp(index, 0, 15);
    if (k < 4) { let v = a.band0; return v[k]; }
    if (k < 8) { let v = a.band1; return v[k - 4]; }
    if (k < 12) { let v = a.band2; return v[k - 8]; }
    let v = a.band3;
    return v[k - 12];
}

// The spectrum at a normalised bearing, smoothly. Smoothstep between bins rather than linear
// because §4.3 asks for something organic rather than a bar chart, and a linear ramp between
// sixteen values has sixteen visible corners in it.
fn atmosAuroraSpectrum(a: AtmosAurora, m: f32) -> f32 {
    let p = clamp(m, 0.0, 1.0) * 15.0;
    let i = floor(p);
    let f = p - i;
    let s = f * f * (3.0 - 2.0 * f);
    return mix(atmosAuroraBand(a, i32(i)), atmosAuroraBand(a, i32(i) + 1), s);
}

// Ray against a vertical cylinder of radius `radius` centred on `centre.xz`. The camera is inside it
// (the radius is kilometres), so the root in front is the far one.
fn atmosAuroraShell(ro: vec3<f32>, rd: vec3<f32>, centre: vec3<f32>, radius: f32) -> f32 {
    let o = ro.xz - centre.xz;
    let d = rd.xz;
    let a = dot(d, d);
    if (a < 1.0e-8) {
        return -1.0; // straight up or straight down: it never meets the shell
    }
    let b = dot(o, d);
    let c = dot(o, o) - radius * radius;
    let disc = b * b - a * c;
    if (disc < 0.0) {
        return -1.0;
    }
    return (-b + sqrt(disc)) / a;
}

fn atmosphereAuroraAt(ro: vec3<f32>, rd: vec3<f32>) -> AtmosResult {
    var out: AtmosResult;
    out.radiance = vec3<f32>(0.0);
    out.bloom = 0.0;
    let count = u32(frame.atmosCount.y + 0.5);
    if (count == 0u) {
        return out;
    }
    // Nothing below the horizon. The curtains stand on a world height and the terrain in front of
    // them has already written depth, so this is only about the sky a ray sees looking downhill.
    let horizon = smoothstep(-0.07, 0.015, rd.y);
    if (horizon <= 0.0) {
        return out;
    }

    // The bands already in the frame block (ADR-030), which is the whole of "do not create a second
    // audio-analysis system": bass, mid and treble from `audio`, low-mid from `audioBands`, the
    // beat's pulse from `beat`.
    let bass = frame.audio.y;
    let lowMid = frame.audioBands.x;
    let midBand = frame.audio.z;
    let high = frame.audio.w;
    let pulse = frame.beat.y;

    var emission = 0.0;
    for (var i: u32 = 0u; i < count; i = i + 1u) {
        let a = frame.auroras[i];
        let shells = i32(a.config.x + 0.5);
        let baseRadius = max(a.config.y, 1.0);
        let baseY = a.config.z;
        let height = max(a.config.w, 1.0);

        var radiance = vec3<f32>(0.0);
        let beatGain = 1.0 + a.audio2.x * pulse;

        for (var k = 0; k < shells; k = k + 1) {
            let fk = f32(k);
            let radius = baseRadius * (1.0 + fk * a.flow.w);
            let t = atmosAuroraShell(ro, rd, a.anchor.xyz, radius);
            if (t <= 0.0) {
                continue;
            }
            let hit = ro + rd * t;
            let h = (hit.y - baseY) / height;
            // Above the tallest a curtain can reach, or below its feet. The upper bound has slack
            // for the wave, which can push the top past 1.
            if (h < 0.0 || h > 2.3) {
                continue;
            }
            let rel = hit - a.anchor.xyz;
            let theta = atan2(rel.z, rel.x);

            // The azimuth as a point on a circle, so every noise below wraps with no seam at the
            // antimeridian -- the failure skybox.wgsl has to hand-pick a mip level to avoid.
            let flowAngle = a.flow.x * kAtmosTau + fk * 1.7;
            let flowed = vec2<f32>(cos(theta + flowAngle), sin(theta + flowAngle));

            // Where the spectrum says this bearing should reach. **Mirrored** across the sky, so
            // bin 0 and bin 15 meet at the back instead of stepping: a wrap would put a visible
            // discontinuity in the curtain exactly where the eye follows it round.
            let m = 1.0 - abs(1.0 - 2.0 * (theta / kAtmosTau + 0.5));
            let spec = atmosAuroraSpectrum(a, m);

            // The curtain's top. `detail.z` blends between a flat curtain and a full visualiser;
            // 0.5 is the neutral spectrum value the CPU fills in when there is no music, so silence
            // gives an ordinary aurora rather than a collapsed one.
            //
            // The ranges are deliberately narrow. An earlier pass let the spectrum and the bass
            // between them push the top to 2.2 curtain-heights, and on a loud passage every bearing
            // saturated at once: the curtains stopped being curtains and became a ceiling. A
            // spectrum that moves the top between about a third and a full height is legible as a
            // spectrum; one that moves it past the top of frame is just brightness.
            let shaped = mix(1.0, 0.30 + 0.95 * spec, a.detail.z);
            let lift = 1.0 + a.audio.x * bass * 0.5;
            let wave = a.shape.x * (atmosFbm2(flowed * a.shape.y, 3) - 0.5) * 2.0 *
                       (1.0 + a.audio.y * lowMid);
            let top = clamp(shaped * lift + wave, 0.08, 1.5);

            // **Where there is a curtain at all.** Without this the azimuthal noise never reaches
            // zero, every bearing is lit, and the result is a fog bank filling the lower sky rather
            // than an aurora -- which is exactly what the first render produced. The dark gaps
            // between curtains are the silhouette, and the silhouette is the effect.
            let presence = smoothstep(0.30, 0.72,
                                      atmosFbm2(flowed * (a.shape.y * 0.85) + vec2<f32>(3.1, 1.7), 3));
            if (presence <= 1.0e-3) {
                continue;
            }

            let hn = h / top;
            // A crisp lower edge and a diffuse top. That asymmetry is the entire silhouette of an
            // aurora; symmetrical falloff reads as a fog band.
            let body = smoothstep(0.0, 0.03, h) * (1.0 - smoothstep(0.25, 0.95, hn)) * presence;
            if (body <= 1.0e-3) {
                continue;
            }

            // Vertical ray structure: high frequency around the azimuth, nearly constant in height.
            // This is the feature that makes a curtain read as a curtain rather than as a gradient,
            // and it is what the mid band thickens.
            let rayCoord = vec2<f32>(cos(theta + flowAngle), sin(theta + flowAngle)) *
                           (a.shape.w / kAtmosTau);
            let rays = 0.35 + 0.65 * atmosFbm2(rayCoord + vec2<f32>(a.flow.y * 0.3), 2);
            // Folds: larger in azimuth, drifting upward, so the curtain churns rather than sliding.
            let folds = atmosFbm2(flowed * (a.shape.y * 1.7) + vec2<f32>(0.0, h * 2.0 + a.flow.z), 3);
            let foldGain = mix(1.0, 0.45 + 1.1 * folds,
                               clamp(a.shape.z + a.audio.z * midBand, 0.0, 1.5));

            var col = mix(a.low.rgb, a.mid.rgb, smoothstep(0.0, 0.45, hn));
            col = mix(col, a.top.rgb, smoothstep(0.42, 1.0, hn));
            if (a.audio2.y > 0.0) {
                // Rainbow ramps around the *bearing*, not up the curtain: an aurora's vertical hue
                // ramp is its signature and overwriting that is what makes a colour cycle look
                // broken. §4.4's colour-cycle mode is a sweep across the sky.
                col = col * atmosRainbow(m * a.audio2.z + a.audio2.w, a.anchor.w, a.audio2.y);
            }

            // The bright lower rim. Scaled well down from the authored number because it is
            // multiplied by an already-HDR colour: at 1:1 an edge brightness of 3 put a radiance of
            // 9 along the whole base of every curtain, which is a bar of light and not a rim.
            let edge = exp(-hn * hn * 34.0) * a.top.w * 0.28;
            // Filaments: fine luminous threads, which is what the high band actually drives.
            let filament = pow(rays, 5.0) * a.detail.x * (0.4 + 1.6 * a.audio.w * high);
            var glint = 0.0;
            if (a.detail.y > 0.0) {
                let g = atmosNoise2(rayCoord * 6.0 + vec2<f32>(a.flow.y * 1.7, h * 9.0));
                glint = pow(max(g, 0.0), 12.0) * a.detail.y * (0.3 + 2.0 * high) * 6.0;
            }

            // Further shells are dimmer and hazier, which is what reads as depth.
            let depth = 1.0 / (1.0 + fk * 0.55);
            let layer = body * rays * foldGain * a.low.w * depth;
            radiance = radiance + col * (layer + (edge + filament + glint) * body * depth) * beatGain;
        }
        // **More shells are more detail, not more light.** Four curtains summing un-normalised is
        // four times the radiance of one, so raising the layer count -- an artistic choice about
        // depth -- silently became an exposure change, and the aurora blew out. Normalising here
        // means `curtains` does what its name says and nothing else.
        radiance = radiance / (0.6 * f32(shells) + 0.4);

        // The broad wash the curtains stand in. Once per aurora rather than per shell, because it is
        // light the atmosphere itself scatters and it has no surface to be on.
        // Steep and faint: this is scattered light close to the horizon, and a shallow falloff put
        // a solid colour across the bottom third of the sky that read as coloured fog.
        let glow = exp(-max(rd.y, 0.0) * 16.0) * a.detail.w;
        radiance = radiance + mix(a.low.rgb, a.mid.rgb, 0.4) * (glow * 0.16 * beatGain);

        out.radiance = out.radiance + radiance;
        emission = max(emission, a.mid.w);
    }
    out.radiance = out.radiance * horizon;
    out.bloom = clamp(atmosLuma(out.radiance) * 0.5 * emission, 0.0, 1.0);
    return out;
}

// ---- the sky-layer entry point -------------------------------------------------------------------

// Everything the sky-layer draw adds, for one view ray.
//
// `pixelAngle` is the angular size of one pixel and arrives as an argument rather than being taken
// here. Two reasons, both hard: `dpdx` is only valid in a fragment shader and this file has no
// business assuming it is in one, and a derivative taken inside the comet loop would sit in
// non-uniform control flow, where it is undefined. The caller takes it once, at the top, and it is
// the same number for every effect anyway.
fn atmosphereSkyAt(ro: vec3<f32>, rd: vec3<f32>, pixelAngle: f32) -> AtmosResult {
    let comets = atmosphereCometsAt(ro, rd, pixelAngle);
    let aurora = atmosphereAuroraAt(ro, rd);
    var out: AtmosResult;
    out.radiance = comets.radiance + aurora.radiance;
    out.bloom = max(comets.bloom, aurora.bloom);
    return out;
}
