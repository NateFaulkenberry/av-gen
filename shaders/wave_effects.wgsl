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

fn wavesAt(worldPos: vec3<f32>, normal: vec3<f32>, emission: vec3<f32>) -> WaveResult {
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
