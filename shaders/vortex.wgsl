// The vortex field (ADR-388): the WGSL transliteration of core/vortex.cpp. Every function here is
// a pure function of the packed uniforms, a world position and a time, and evaluates the same
// expressions in the same order as the CPU so the two agree within float rounding (tests compare
// them through the packed form, so both sides start from identical bytes).
//
// Built the way wind.wgsl is built, for the reason ADR-055 gives and one more: before this, the
// vortex existed only inside volume.wgsl, so the only thing that could ask where the funnel was
// was the volumetric march. Particles approximated it with an attractor and an orbit force, which
// is a different shape that happens to look similar.
//
// ADR-388, and it cost something worth writing down: taking the uniforms as a PARAMETER instead of
// reading `vol.vortexN` directly from the uniform buffer is not bit-neutral. The Metal compiler
// contracts the arithmetic differently when the operands arrive as function arguments, and the
// shipped Tree of Life frame moves by **62 of 518400 pixels -- 60 of them by one code value, one by
// 4 and one by 11, none of them adjacent to another**. They are rounding flips at the two early-out
// thresholds and in `pow`, not a change of shape, and no arrangement of the call site avoids them:
// a helper returning the struct, a struct constructed inline, and five scalar parameters all give
// the identical frame as each other and a different one from the version that read the uniform
// directly. The alternative was a second copy of this body living in volume.wgsl, and one body that
// is provably invisible different beats two bodies that can silently disagree.
//
// Needs noise.wgsl for fbm3. Include that first; this file does not include it, because a module
// that already has it would get a duplicate definition (the include directive does not
// de-duplicate -- ADR-360 learned that the expensive way).
//
//   v0  xyz = centre, w = radius (0 is off, and it is the gate)
//   v1  x = thickness, y = swirl, z = rotationSpeed, w = unused
//   v2  x = innerVoid, y = contrast, z = turbulence, w = turbulenceScale
//   v3  x = breathAmount, y = breathSpeed, z/w = appearance, unused here
//   v4  x = funnelDepth, y = throat, z = throatDensity, w = 0
//   v7  x = eyeWallWidth, y = eyeWallGain, z = cloudNoise, w = 0          (§9/§53)
//   v8  x = bandArms, y = cot(bandPitch), z = bandDepth, w = bandHarmonic  (§10/§11)

struct VortexUniformsWgsl {
    v0: vec4<f32>,
    v1: vec4<f32>,
    v2: vec4<f32>,
    v3: vec4<f32>,
    v4: vec4<f32>,
    v6: vec4<f32>, // ADR-389: smokeWarp, smokeBillow, detail, 0
    v7: vec4<f32>, // §9/§53: eyeWallWidth, eyeWallGain, cloudNoise, 0
    v8: vec4<f32>, // §10/§11: bandArms, band cotangent, bandDepth, bandHarmonic
};

// ADR-389. An octave whose world period falls below twice the distance between march samples
// cannot be resolved, and what it contributes is not detail: it is aliasing, which in a volumetric
// is salt-and-pepper grain that crawls when the camera moves. This fades such an octave out
// smoothly instead.
//
// It is correctness rather than taste, and it is deliberately NOT a knob. Nobody should have to
// find a slider called "stop aliasing", and the right answer changes whenever `volumeSteps` or
// `volumeMaxDistance` do -- which they do between Preview and Cinematic, and which the shipped
// scene already disagrees with itself about (the scene file says 48 steps, the project overrides
// it to 32, and 32 over 4 km is a sample every 125 metres).
//
// `filterWidth` of 0 means "point sample, not an integral" -- a particle asking where the medium is
// going is not integrating along a ray and has nothing to alias, so it takes every octave.
fn vortexOctaveWeight(periodMetres: f32, filterWidth: f32) -> f32 {
    if (filterWidth <= 0.0) {
        return 1.0;
    }
    return smoothstep(0.0, 1.0, periodMetres / (2.0 * filterWidth));
}

// ---- the macro structure (Vortex 2.0 §7-§11) ------------------------------------------------
//
// The transliteration of `radialProfile` and `spiralBands` in core/vortex.cpp, in the same order
// and the same expressions, for the same reason every other function in this file is.
//
// Why it exists at all, because it is the finding the whole rebuild turns on: BEFORE this, the
// envelope was `voidMask * rim * vert` -- monotone in radius, completely UNIFORM IN ANGLE, smooth
// in height. It had no structure in it. Everything anybody has ever seen in this effect came out
// of the three fBMs below, which is why the owner reads it as noise: structurally, it is noise on
// a smooth cone. That is also why ADR-389's strict Nyquist clamp came back a flat teal wash --
// there was nothing underneath the noise for the clamp to leave behind.
//
// Everything here is analytic. No noise, a handful of ALU, band-limited by construction, so it
// survives the march's 125-metre sample spacing intact and it does not move the cost.

// §8/§9. The radial profile: a clear eye, a wall standing around it, the body falling to the rim.
fn vortexRadialProfile(v: VortexUniformsWgsl, rr: f32) -> f32 {
    let rim = 1.0 - smoothstep(0.72, 1.3, rr);
    // The eye. `innerVoid` IS its radius -- a second radius control would have left whichever of
    // the two was not in charge as a slider that silently does nothing, which the parity test's
    // per-field reachability probe caught on the first attempt. `eyeWallWidth` is the 0.22
    // ADR-374 hardcoded, now authored, and at 0.22 with gain 0 this is ADR-374's profile exactly.
    let eyeR = clamp(v.v2.x, 0.0, 0.95);
    let wallW = max(v.v7.x, 1e-3);
    // A hole, so its boundary is a RISE and not a fade. Smoothstep, not a step: ADR-369's rule is
    // that there must be no edge anywhere for a hard line to live on.
    let eye = smoothstep(eyeR, eyeR + wallW, rr);
    // §9: the wall -- a ring of extra density whose crest sits just outside the eye. This is what
    // makes the silhouette read as a hurricane rather than as a hole in a cloud.
    let crest = eyeR + wallW;
    let d = (rr - crest) / max(wallW * 1.5, 1e-3);
    let ring = exp(-d * d);
    return eye * rim * (1.0 + max(v.v7.y, 0.0) * ring);
}

// §10/§11. Logarithmic spiral bands at three nested scales.
//
// r = a e^{b theta}, so `theta - ln(r) / b` is constant along an arm: that expression IS the band
// coordinate and the rest is shaping. `b` arrives as the cotangent of the pitch angle, packed on
// the CPU so no trigonometry is needed to recover it.
//
// Returns a MULTIPLIER whose mean over angle is exactly 1 at any depth. ADR-389's family rule:
// `density` and `emission` are per-metre coefficients calibrated against this field's mean, and a
// band term with mean 0.5 would silently halve the medium under them.
fn vortexSpiralBands(v: VortexUniformsWgsl, rr: f32, angle: f32, t: f32) -> f32 {
    let arms = v.v8.x;
    if (arms < 0.5) {
        return 1.0;
    }
    let depth = clamp(v.v8.z, 0.0, 1.0);
    let harmonic = clamp(v.v8.w, 0.0, 1.0);
    // Clamped away from the axis: ln(rr) diverges there and the eye has removed that region from
    // the picture anyway. Without it the bands wind infinitely fast at the centre and alias
    // however many steps the march takes -- ADR-389's own mistake, repeated in a new place.
    let rClamped = max(rr, 0.06);
    let arm = angle - log(rClamped) * v.v8.y;
    // The arms turn with the structure, on the same `rotationSpeed` the noise uses, so the bands
    // and the filaments cannot drift apart into two storms.
    let spin = t * v.v1.z;
    var band = cos(arms * (arm - spin));
    // §11's nested scales: 3x and 7x the arm count at a third and a ninth of the depth. A
    // structural hierarchy, not an octave sum -- summing equal-weight sinusoids is how noise is
    // built, and noise is the thing §0 forbids.
    if (harmonic > 0.0) {
        band = band + harmonic * (cos(arms * 3.0 * (arm - spin * 1.3)) / 3.0 +
                                  cos(arms * 7.0 * (arm - spin * 1.7)) / 9.0);
    }
    // Bands wash out at the eye wall, where the flow is a solid ring, and at the outer edge where
    // the storm frays. Applied to the DEPTH rather than to the density, so the mean stays 1.
    let reach = smoothstep(0.0, 0.22, rr - clamp(v.v2.x, 0.0, 0.95)) *
                (1.0 - smoothstep(0.85, 1.25, rr));
    return 1.0 + depth * reach * band;
}

// The shape and the intermediates a velocity needs, in one evaluation so the two entry points
// cannot drift apart.
struct VortexShapeResult {
    density: f32,
    envelope: f32,
    radialT: f32,
    depthT: f32,
    rel: vec3<f32>,
    inside: bool,
};

fn vortexEvaluate(v: VortexUniformsWgsl, p: vec3<f32>, t: f32, filterWidth: f32) -> VortexShapeResult {
    var s: VortexShapeResult;
    s.density = 0.0;
    s.envelope = 0.0;
    s.radialT = 0.0;
    s.depthT = 0.0;
    s.rel = vec3<f32>(0.0);
    s.inside = false;
    let radius = v.v0.w;
    if (radius <= 0.0) {
        return s;
    }
    s.rel = p - v.v0.xyz;
    // Breathing: the whole structure widens and narrows slowly. Applied to the radius rather than
    // to the density so the silhouette moves, which is what reads as breathing; scaling density
    // alone just pulses the brightness.
    let breath = 1.0 + v.v3.x * sin(t * v.v3.y);
    // ADR-374: a FUNNEL, not a flat disc. A level camera sees a slab edge-on and reads it as a band
    // of haze; a funnel has an inner wall it can see down into, which is the whole difference
    // between "there is something below" and "the island is hanging over a hole". Depth 0 keeps the
    // old slab, so the shape is a superset.
    let depth = max(v.v4.x, 1e-3);
    let yn = clamp(-s.rel.y / depth, 0.0, 1.0); // 0 at the mouth, 1 at the throat
    let mouth = mix(1.0, clamp(v.v4.y, 0.02, 1.0), yn * yn);
    let rr = length(s.rel.xz) / max(radius * breath * mouth, 1e-3);
    s.radialT = rr;
    s.depthT = yn;
    if (rr > 1.35) {
        return s; // outside the funnel entirely, and compactly so -- ADR-369's lesson
    }
    // The wall's thickness across the funnel surface. Gaussian, so there is no edge anywhere for a
    // hard line to live on (ADR-369 again).
    let wall = exp(-(s.rel.y * s.rel.y) / max(v.v1.x * v.v1.x, 1e-3));
    // `below` is not decoration. `yn` clamps to 0 for anything ABOVE the mouth, so without it the
    // throat term evaluated to its full value up there and the funnel extended *upward* as a
    // full-radius cylinder at `throatDensity` -- which is why every wide variant washed the top of
    // the frame as badly as the bottom. The measurement that found it was the contribution split by
    // band: a funnel that only descends cannot add +12 luminance to the sky above the island.
    let below = smoothstep(0.0, -v.v1.x, s.rel.y);
    let throatFade = 1.0 - smoothstep(0.55, 1.0, yn);
    let vert = max(wall, throatFade * v.v4.z * below);
    if (vert < 1e-4) {
        return s;
    }
    // ADR-374: the cheap masks BEFORE the noise. Measured, the vortex's cost is not the march
    // length and is barely the step count -- it is how many pixels have non-zero density and
    // therefore evaluate three fBMs. The void at the centre and everything past the rim are exactly
    // the places where the answer is already zero, and they were paying full price for it.
    // Every factor here is smooth, so the early-out fires only where the result was already
    // negligible and introduces no edge (ADR-369).
    //
    // §7-§11: the macro structure lives HERE, in the envelope, above the early-out and below any
    // noise. That ordering is the brief's hierarchy written as control flow -- macro cyclone
    // structure, then density, then detail -- and it is what makes §52-§56's failure tests
    // passable: turn every noise term off and the eye, the wall, the bands and the funnel remain.
    let angle = atan2(s.rel.z, s.rel.x);
    let envelope = vortexRadialProfile(v, rr) * vortexSpiralBands(v, rr, angle, t) * vert;
    if (envelope < 1.0e-6) {
        return s;
    }
    s.envelope = envelope;
    s.inside = true;
    // The shear. Angle advanced by radius makes a spiral; advanced by time makes it turn.
    let warped = angle + rr * v.v1.y + t * v.v1.z;
    // Back to a cartesian sample point, so the noise is sampled in a frame that winds with the
    // structure rather than across it.
    let q = vec3<f32>(cos(warped) * rr, s.rel.y / max(v.v1.x, 1e-3), sin(warped) * rr);
    let scale = max(v.v2.w, 1e-3);
    // ADR-389, the domain warp: the thing that makes this read as smoke rather than as noise.
    // Three independent fBMs summed at fixed rates give detail that sits ON the spiral instead of
    // being carried BY it, and uncorrelated detail on a smooth flow is what the eye calls grain.
    // Advecting the finer octaves through a low-frequency vector field drags them into the sheets
    // and curls of the big structure, which is what billowing gas actually is.
    var q1 = q;
    var q2 = q;
    let warpAmount = max(v.v6.x, 0.0);
    if (warpAmount > 0.0) {
        let flow = fbm3Vec(q * (min(scale, 1e9) * 0.7) + vec3<f32>(t * 0.02, 0.0, -t * 0.015), 11u);
        q1 = q + flow * warpAmount;
        q2 = q + flow * (warpAmount * 1.6);
    }
    // Three octaves at three rates: macro barely moves, fine detail moves fastest.
    // How fine this march can carry. `fbm3` is itself three octaves at 1, 2.03 and 4.11, so a call
    // at scale S has its finest content at period `radius / (S * 4.11)`, and Nyquist wants two
    // samples across that.
    //
    // The clamp is deliberately NOT applied at full strength, and the reason is a measurement. At
    // the shipped 32 steps over 4 km -- a sample every 125 metres -- the strict Nyquist scale is
    // 0.19 against an authored 2.4, and clamping to it erases the funnel: no filaments, no swirl,
    // a flat teal wash. That is the correct answer to "what can 32 samples carry", and it is the
    // proof that this march is starved rather than this noise being mis-designed. Watering the
    // signal processing down would hide that; refusing to clamp at all leaves the grain. So the
    // clamp is applied with a floor, which bites hard enough to take the worst of the aliasing and
    // leaves the rest of the problem visible where it belongs -- in the step count.
    let strict = radius / max(2.0 * filterWidth * 4.11, 1e-3);
    let resolvableScale = max(strict, scale * 0.45);
    var effScale0 = scale;
    if (filterWidth > 0.0) {
        effScale0 = min(scale, resolvableScale);
    }
    var n0 = fbm3(q * effScale0 + vec3<f32>(t * 0.013, 0.0, t * 0.009), 29u);
    var n1 = fbm3(q1 * (effScale0 * 3.1) + vec3<f32>(0.0, t * 0.055, 0.0), 53u);
    var n2 = fbm3(q2 * (effScale0 * 9.7) + vec3<f32>(t * 0.17, 0.0, -t * 0.13), 97u);
    // Billow: |2n-1| turns a wispy field into rounded masses with creases between them, which is
    // the difference between a nebula and a smoke column. Blended rather than switched, because the
    // vortex wants to be able to be both.
    let billow = clamp(v.v6.y, 0.0, 1.0);
    if (billow > 0.0) {
        n0 = mix(n0, abs(n0 * 2.0 - 1.0), billow);
        n1 = mix(n1, abs(n1 * 2.0 - 1.0), billow);
        n2 = mix(n2, abs(n2 * 2.0 - 1.0), billow);
    }
    // The band-limit, and the first version of it was wrong in a way worth recording: attenuating
    // the finer octaves and renormalising made the grain WORSE (|hf| 1.139 -> 1.307), because at
    // this march's 125-metre spacing even the COARSEST octave is undersampled -- its period is 83
    // metres and Nyquist wants 250. Averaging several aliased octaves cancels some of the error;
    // isolating one does not. So the fix cannot be a weight. It has to be a FREQUENCY: the base
    // scale is clamped to the finest this many samples can actually carry, and the octaves above it
    // fade out because they remain beyond it whatever the base does.
    let periodBase = radius / effScale0;
    let w0 = vortexOctaveWeight(periodBase, filterWidth);
    let w1 = vortexOctaveWeight(periodBase / 3.1, filterWidth);
    let w2 = vortexOctaveWeight(periodBase / 9.7, filterWidth);
    let detail = max(v.v6.z, 0.0);
    // Normalised by the weights actually used, so fading an octave out smooths the result instead
    // of darkening it -- the mean must not move when the step length changes.
    let wSum = max(w0 + 0.45 * w1 + detail * w2, 1e-4);
    var n = (n0 * w0 + 0.45 * n1 * w1 + detail * n2 * w2) / wSum;
    // Turbulence breaks the spiral's symmetry, because a real nebula is not a mathematical spiral
    // and the brief says so.
    n = mix(n, n * (0.55 + 0.9 * n1), clamp(v.v2.z, 0.0, 1.0));
    // ADR-389: a smoothstep remap, not `pow`, and compensated for the mean it moves.
    //
    // An exponent above 1 on a noise sum crushes everything toward black and leaves sparse isolated
    // peaks -- so every surviving aliased sample became a bright dot in a dark field, which is a
    // third of the measured grain on its own. It also throws away the midtones that are what make a
    // volume read as THICK rather than as sparks.
    //
    // The compensation is the part that is easy to forget and expensive to miss. `pow(n, c)` over a
    // roughly uniform n has mean 1/(c+1) -- 0.22 at the shipped contrast of 3.6 -- while a
    // smoothstep centred on 0.5 has mean 0.5 whatever its width. Swapping one for the other without
    // this factor multiplies the medium's mean density by about six, and the authored `density` and
    // `emission` are PER-METRE coefficients tuned against the old mean: the first render came back
    // a blown-out cyan glow filling the lower frame. Same family as ADR-374, 379 and 381 -- a
    // quantity changed underneath a coefficient that was calibrated against it.
    let contrast = max(v.v2.y, 0.05);
    let half = 0.5 / contrast;
    let curve = smoothstep(0.5 - half, 0.5 + half, clamp(n, 0.0, 1.0));
    let shaped = curve * (2.0 / (contrast + 1.0));
    // §53, and §5's diagnostic: `cloudNoise` is the weight of the whole fBM stack against a FLAT
    // field of the same mean. At 0 the density is the macro envelope alone, which is the render
    // the owner asks to be shown before any detail is added; at 1 it is ADR-389's field exactly.
    // Blended toward the noise's own mean rather than toward 1, so turning the detail down does
    // not brighten the medium -- the ADR-389 family again, and why this is a mix and not a scale.
    let flatLevel = 0.5 * (2.0 / (contrast + 1.0));
    let cloudNoise = clamp(v.v7.z, 0.0, 1.0);
    s.density = mix(flatLevel, shaped, cloudNoise) * envelope;
    return s;
}

// The shape alone: what the volumetric march wants and all it wants, without the velocity
// trigonometry. The march evaluates this per step per pixel, which is the one place in this system
// where a few multiplies are worth a second entry point.
fn vortexShapeAt(v: VortexUniformsWgsl, p: vec3<f32>, t: f32, filterWidth: f32) -> f32 {
    return vortexEvaluate(v, p, t, filterWidth).density;
}

struct VortexSampleWgsl {
    density: f32,
    velocity: vec3<f32>,
    radialT: f32,
    depthT: f32,
    envelope: f32,
};

// Everything a consumer has asked for so far, in one evaluation. Splitting it would mean sampling
// the noise twice for a particle that wants both where it is and which way to go.
fn sampleVortex(v: VortexUniformsWgsl, p: vec3<f32>, t: f32) -> VortexSampleWgsl {
    // A point sample: nothing is being integrated, so nothing can alias.
    let s = vortexEvaluate(v, p, t, 0.0);
    var out: VortexSampleWgsl;
    out.density = s.density;
    out.envelope = s.envelope;
    out.radialT = s.radialT;
    out.depthT = s.depthT;
    out.velocity = vec3<f32>(0.0);
    if (!s.inside) {
        return out;
    }
    // Derived from the SAME geometry the shape is, which is the whole point of one sampler:
    //
    //   tangential  the swirl the angular shear already describes, as a real speed (v = omega x r)
    //   radial      inward, strongest at the rim and vanishing at the axis, because a funnel that
    //               pulls uniformly gathers everything to a point and stops looking like one
    //   vertical    down the throat, scaled by how far in the sample already is, so motes fall once
    //               they are caught rather than being dragged down from outside the mouth
    let planar = s.rel.xz;
    let planarLen = length(planar);
    var outward = vec2<f32>(1.0, 0.0);
    if (planarLen > 1e-4) {
        outward = planar / planarLen;
    }
    let tangent = vec2<f32>(-outward.y, outward.x);
    let omega = v.v1.z;
    let radius = max(v.v0.w, 1e-3);
    let tangential = omega * planarLen;
    let inward = radius * abs(omega) * abs(v.v1.y) * s.radialT *
                 (1.0 - smoothstep(1.0, 1.35, s.radialT));
    let descent = -radius * abs(omega) * v.v4.z * (1.0 - s.radialT) * s.envelope;
    out.velocity = vec3<f32>(tangent.x * tangential - outward.x * inward, descent,
                             tangent.y * tangential - outward.y * inward);
    return out;
}
