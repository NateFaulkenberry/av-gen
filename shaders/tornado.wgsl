// The tornado field (ADR-580): a pure function of packed uniforms, a world position and a time.
//
// Built the way `shaders/wind.wgsl` and `shaders/vortex.wgsl` are built, for the reason ADR-388
// gives: a field that only the volumetric march can evaluate is a field that particles, the grid
// solver and a debug overlay each have to approximate separately, and three approximations of one
// shape is three shapes.
//
// **What this is NOT.** It is not `vortex.wgsl` with different numbers. `vortex.wgsl` computes a
// cyclone: its entire macro structure is `vortexRadialProfile(rr)` times `vortexSpiralBands(rr,
// angle)`, both functions of the HORIZONTAL plane at a height, with height entering only as a
// Gaussian wall and a throat taper. An eye, an eye wall and spiral rainbands are features you look
// DOWN at. A tornado is the opposite phenomenon: its structure is in the vertical plane and is
// read from the side. Nothing here is a renamed anything.
//
// **The hierarchy, which is the brief's §45 written as control flow.** Everything in this file is
// analytic -- trigonometry, smoothsteps and one exponential -- and there is no noise in it at all.
// That is not a stage that has not been written yet; it is the architecture. The tornado has to be
// structurally a tornado before it is detailed, so §38's Mode 1 and §39's panel A are *this file
// evaluated on its own*, and the detail stages multiply it rather than create it.
//
// Band-limited by construction, so it survives the march's ~125 metre sample spacing intact, and
// cheap enough that it does not move the cost measurement the architecture decision turns on.
//
// **Needs `noise.wgsl` for `fbm3`, and deliberately does NOT include it.** The include directive
// does not de-duplicate (ADR-360 learned that the expensive way), and `volume.wgsl` already brings
// noise in through `fields.wgsl` before it includes this file. Any other consumer -- the parity
// harness is the only one today -- must prepend it.
//
// Until Phase 4 this file included nothing at all, because it contained no noise whatever. That
// was the architecture rather than an omission and it still is: everything above the detail stage
// below is analytic, and §38's Mode 1 is this file with `cloudAmount` at 0, which is a render and
// not a rebuild.
//
//   t0  xyz = the GROUND CONTACT point, w = height in metres (0 is off, and it is the gate)
//   t1  radiusBottom, radiusMidControl, radiusTop, taper
//   t2  shellWidth, shellGain, coreRadius, coreDensity
//   t3  edgeSoft, wallCloudGain, touchdown, footSoft
//   t4  skirtWidth, skirtHeight, skirtDensity, skirtFlare
//   t5  stripeCount, stripePitch, stripeDepth, stripeHarmonic
//   t6  circulation, coreRadiusMetres, inflow, lift
//   t7  leanX, leanZ, wobbleAmount, wobbleSpeed
//   t8  rotationBottom, rotationTop, rotationCurve, cloudWidth
//   t9  cloudHeight, cloudDensity, suctionCount, suctionStrength
//   t10 suctionRadius, suctionWidth, suctionSpeed, cloudAmount
//   t11 macroAmp, mesoAmp, microAmp, contrast
//   t12 detailScale, climbRate, erosion, (scattering -- appearance, not read here)

struct TornadoUniformsWgsl {
    t0: vec4<f32>,
    t1: vec4<f32>,
    t2: vec4<f32>,
    t3: vec4<f32>,
    t4: vec4<f32>,
    t5: vec4<f32>,
    t6: vec4<f32>,
    t7: vec4<f32>,
    t8: vec4<f32>,
    t9: vec4<f32>,
    t10: vec4<f32>,
    t11: vec4<f32>,
    t12: vec4<f32>,
};

// ---- shape ------------------------------------------------------------------------------------

// The funnel's radius at normalised height `h`, as a quadratic Bezier through three artist radii.
//
// One expression reaches every silhouette the references name: a **cone** (bottom < top), a
// **stovepipe** (all three equal), an **hourglass** (middle below both ends), a **wedge** (all
// three large -- the WMO defines a wedge as one at least as wide at the ground as it is tall) and
// a **rope** (all three small, with a large taper). §14 and §16 ask for exactly this and ask for it
// as a curve rather than as a cone with an exponent, because a cone with an exponent cannot be an
// hourglass at all.
//
// `t1.y` is the Bezier CONTROL point, not the radius at mid height -- those differ, and the
// difference is resolved on the CPU in `packTornado` so that an artist types a real radius and the
// shader still costs two multiplies. Doing it the other way round would put a division here, per
// sample, for a number that changes once a frame.
fn tornadoRadiusAt(v: TornadoUniformsWgsl, h: f32) -> f32 {
    // The taper warps WHERE along the height the control point bites, which is what separates a
    // funnel that flares high from one that flares low at the same three radii.
    let hh = pow(clamp(h, 0.0, 1.0), max(v.t1.w, 0.05));
    let u = 1.0 - hh;
    return u * u * v.t1.x + 2.0 * u * hh * v.t1.y + hh * hh * v.t1.z;
}

// Angular velocity at height `h`. §17 asks for rotation that varies over height rather than one
// uniform angular velocity, because a tornado's core spins faster where it is narrower and that is
// most of what makes the motion read as violent rather than as a turntable.
fn tornadoRotationAt(v: TornadoUniformsWgsl, h: f32) -> f32 {
    return mix(v.t8.x, v.t8.y, pow(clamp(h, 0.0, 1.0), max(v.t8.z, 0.05)));
}

// Where the axis is at height `h`. **A curve, not a line** (§16, §28).
//
// This is the single cheapest thing that stops the result being a mathematical cone, and it is the
// closed-form version of the guide spline every SideFX reference builds its tornado around. Two
// terms: a steady `lean`, quadratic in height so the column stands vertical where it meets the
// ground and tilts aloft; and a slow two-frequency `wobble` whose periods are incommensurate, so
// the column snakes instead of bowing.
//
// Both are scaled toward zero at the ground. A tornado is pinned at the surface by friction and
// swings aloft, and -- the reason that matters for composition rather than for physics -- a base
// that wanders is a base that leaves the frame while the shot is held on it.
fn tornadoAxisAt(v: TornadoUniformsWgsl, h: f32, t: f32) -> vec2<f32> {
    let hc = clamp(h, 0.0, 1.0);
    let lean = vec2<f32>(v.t7.x, v.t7.y) * (hc * hc);
    let amp = v.t7.z;
    if (amp <= 0.0) {
        return lean;
    }
    let s = t * v.t7.w;
    let w = vec2<f32>(sin(6.2831853 * hc * 0.7 + s), cos(6.2831853 * hc * 1.3 + s * 0.83));
    return lean + w * (amp * (0.12 + 0.88 * hc));
}

// §21's helical striations -- the tornado's analogue of a hurricane's spiral rainbands, and the
// transposition is the whole point. A cyclone's bands live in the (radius, angle) plane because
// that is where a cyclone's structure is; a tornado's live in the (angle, height) plane. A helix
// `theta = pitch * h - omega * t` is the streamline a parcel climbing the funnel actually follows,
// so a band held constant along it is a condensation stripe rather than a texture.
//
// This is what makes a STILL FRAME read as rotating. A smooth funnel is ambiguous about whether it
// is spinning at all, and no amount of motion in playback fixes a still that is not.
//
// Returns a multiplier whose mean over angle is EXACTLY 1 at any height. ADR-389's family rule, and
// it is not optional: `density` is a per-metre extinction coefficient calibrated against this
// field's mean, so a band term with mean 0.5 would silently halve the medium under it. Written
// `1 + depth * cos(...)` for that reason and not `mix(1, 0.5 + 0.5 cos(...), depth)`.
fn tornadoStripes(v: TornadoUniformsWgsl, rr: f32, h: f32, angle: f32, t: f32) -> f32 {
    if (v.t5.x < 0.5) {
        return 1.0;
    }
    // Clamped below 1 so the density stays non-negative whatever the harmonics do; the harmonic
    // series sums to at most 1 + 1/3 + 1/9, so the bound is the depth times that.
    let depth = clamp(v.t5.z, 0.0, 0.68);
    let harmonic = clamp(v.t5.w, 0.0, 1.0);
    let phase = angle - v.t5.y * h + tornadoRotationAt(v, h) * t;
    var band = cos(v.t5.x * phase);
    // Three nested scales, at a third and a ninth of the depth -- a structural hierarchy rather
    // than an octave sum. Summing equal-weight sinusoids is how noise is built, and noise is the
    // thing §45 forbids as a foundation.
    if (harmonic > 0.0) {
        band = band + harmonic * (cos(v.t5.x * 3.0 * phase) / 3.0 + cos(v.t5.x * 7.0 * phase) / 9.0);
    }
    // Striations are a property of the condensation SURFACE, so they fade out of the interior --
    // where, physically, there is no surface for them to be on.
    return 1.0 + depth * smoothstep(0.30, 0.85, rr) * band;
}

// §27's secondary vortices -- and §20's vorticity, answered in the one form that is honest for an
// analytic field.
//
// **What this is.** Real violent tornadoes are multi-vortex: two to six *suction vortices* orbit
// the parent axis at roughly the radius of maximum wind, turning faster than the parent does, each
// a scaled copy of the same circulation. They are what makes the base ragged and they are the
// single most recognisable feature of a strong tornado after the silhouette. They are STRUCTURE --
// a real, named, physical thing -- not detail noise, which is why they are in this file and in this
// phase rather than in the breakup stage.
//
// **Why it is a cosine and not a loop over N Gaussians.** The obvious implementation sums N bumps
// orbiting the axis. It costs a loop per sample, and worse, its mean over angle is some number
// nobody can write down -- and `density` is a per-metre coefficient calibrated against this field's
// mean (ADR-389's family). A cosine in angle has mean EXACTLY zero, so `1 + strength * window *
// cos(...)` has mean exactly 1 at every radius and height, whatever the count, strength or speed.
// Same trick as the striations, same reason, and it is also about six times cheaper.
//
// **Why it is windowed at the radius of maximum wind.** Suction vortices do not live at the axis
// and they do not live out in the inflow; they ride the wall where the shear is. The Gaussian
// window in `rr` is that, and it is also what stops this from being a second set of striations.
//
// **Vorticity, stated where somebody will look for it.** ADR-580 refuses vorticity confinement in
// the analytic tier, because confinement restores energy that NUMERICAL DIFFUSION removed and an
// analytic field has none. `suctionStrength` is what §20's Vorticity control actually drives, and
// the visual consequence §20 asks for -- "preserving and enhancing swirling motion that otherwise
// dissipates" -- is delivered by a structure that cannot dissipate because it is evaluated rather
// than integrated.
fn tornadoSuction(v: TornadoUniformsWgsl, rr: f32, h: f32, angle: f32, t: f32) -> f32 {
    let count = v.t9.z;
    if (count < 0.5 || v.t9.w <= 0.0) {
        return 1.0;
    }
    // The window: a Gaussian ring centred on the radius of maximum wind.
    let d = (rr - clamp(v.t10.x, 0.0, 2.0)) / max(v.t10.y, 1e-3);
    let window = exp(-d * d);
    // They turn FASTER than the parent, which is the tell that there is more than one vortex:
    // their own rate plus the parent's, so they never sit still relative to the funnel.
    let spin = t * (v.t10.z + tornadoRotationAt(v, h)) + h * 2.0;
    // Clamped so the density cannot go negative, exactly as the striations are.
    return 1.0 + clamp(v.t9.w, 0.0, 0.9) * window * cos(count * (angle - spin));
}

// ---- detail (§21, §26, §29) ---------------------------------------------------------------------
//
// **The hierarchy, and it is the brief's §45 written as one multiply.** Everything above this point
// is the tornado. This returns a number whose mean is 1, and the density is `envelope * detail`. So
// noise makes the tornado look natural; it does not make the tornado exist. At `cloudAmount` 0 this
// returns exactly 1.0 and the field is byte-for-byte the analytic one -- which is why §38's Mode 1
// is a slider and not a build.
//
// **Mean exactly 1 is not tidiness, it is ADR-389's family rule.** `density` is a per-metre
// extinction coefficient calibrated against this field's mean. A detail term with mean 0.6 silently
// multiplies the medium's optical depth by 0.6 and every look tuned before it is wrong. So the
// shaped noise is divided by its own mean rather than used raw, and the blend toward 1 is a `mix`
// rather than a scale.
//
// **Temporal coherence (§29) is free here, and that is the whole payoff of an analytic flow.** The
// noise is not sampled at `p`. It is sampled in the column's own co-moving frame: the angle is
// advanced by `rotationAt(h) * t` and the height is dropped by `climbRate * t`, so a feature sits
// still in a frame that is itself rotating and rising. Detail RIDES the flow instead of scrolling
// through it. There is no advected texture, no history and no state -- it is a change of
// coordinates, correct at any `t`, evaluated not integrated, and therefore reproducible (ADR-360).
//
// **The band limit is not a knob** (ADR-389). An octave whose world period falls below twice the
// march's sample spacing cannot be resolved, and what it contributes is not detail but aliasing --
// salt-and-pepper grain that crawls when the camera moves. `filterWidth` is the caller's sample
// spacing and 0 means "point sample, nothing to alias". The right answer changes with `volumeSteps`
// and `volumeMaxDistance`, which change between quality tiers, so nobody should have to find a
// slider called "stop aliasing".
fn tornadoOctaveWeight(periodMetres: f32, filterWidth: f32) -> f32 {
    if (filterWidth <= 0.0) {
        return 1.0;
    }
    return smoothstep(0.0, 1.0, periodMetres / (2.0 * filterWidth));
}

fn tornadoDetail(v: TornadoUniformsWgsl, rr: f32, h: f32, angle: f32, envelope: f32, radius: f32,
                 t: f32, filterWidth: f32) -> f32 {
    let amount = clamp(v.t10.w, 0.0, 1.0);
    if (amount <= 0.0) {
        return 1.0; // §38 Mode 1, and it is an early-out so it costs nothing to leave off
    }
    // The co-moving frame. `spin` winds the sample with the structure so detail follows the
    // striations rather than cutting across them; `climb` carries it upward, which is what reads as
    // material being lifted through the funnel rather than a texture scrolling on it.
    let spin = tornadoRotationAt(v, h) * t;
    let climb = v.t12.y * t;
    let q = vec3<f32>(cos(angle - spin) * rr, h * 3.0 - climb, sin(angle - spin) * rr);

    // Three scales, §21's macro / meso / micro, at fixed octave ratios rather than three authored
    // scales. The ratios are a structural decision and not a preference: an artist given three
    // independent scale sliders sets them to the same number and gets one octave at triple
    // amplitude, which is the failure §21 exists to prevent.
    let s0 = max(v.t12.x, 1e-3);
    let s1 = s0 * 3.1;
    let s2 = s0 * 9.7;
    // What this march can carry, per octave. The world period of an octave at scale S is
    // `radius / S`, since `q` is expressed in units of the funnel radius.
    let w0 = tornadoOctaveWeight(radius / s0, filterWidth);
    let w1 = tornadoOctaveWeight(radius / s1, filterWidth);
    let w2 = tornadoOctaveWeight(radius / s2, filterWidth);
    let a0 = max(v.t11.x, 0.0) * w0;
    let a1 = max(v.t11.y, 0.0) * w1;
    let a2 = max(v.t11.z, 0.0) * w2;
    let sum = a0 + a1 + a2;
    if (sum <= 1e-4) {
        return 1.0; // every octave faded out by the band limit: smooth is the correct answer
    }
    // Three rates, slowest for the biggest masses. A macro cloud that churns as fast as a wisp is
    // the single most recognisable tell of noise pretending to be smoke.
    let n0 = fbm3(q * s0 + vec3<f32>(t * 0.011, 0.0, t * 0.008), 71u);
    let n1 = fbm3(q * s1 + vec3<f32>(0.0, t * 0.043, 0.0), 131u);
    let n2 = fbm3(q * s2 + vec3<f32>(t * 0.15, 0.0, -t * 0.11), 197u);
    // Normalised by the weights actually used, so an octave fading out smooths the result rather
    // than darkening it -- the mean must not move when the step length changes.
    let n = (n0 * a0 + n1 * a1 + n2 * a2) / sum;

    // A smoothstep remap rather than `pow`, and ADR-389 records why: an exponent above 1 on a noise
    // sum crushes everything toward black and leaves sparse isolated peaks, so every surviving
    // aliased sample becomes a bright dot in a dark field. It also throws away the midtones, which
    // are what make a volume read as THICK rather than as sparks.
    let contrast = max(v.t11.w, 0.05);
    let half = 0.5 / contrast;
    let shaped = smoothstep(0.5 - half, 0.5 + half, clamp(n, 0.0, 1.0));
    // A smoothstep centred on 0.5 has mean 0.5 whatever its width, so this has mean 1.
    let unit = shaped * 2.0;

    // §26 and the brief's edge erosion: detail bites HARDER where the structure is already thin.
    // That is what makes wisps break away from the column instead of the whole thing fading evenly,
    // and it is one line because the envelope is right here and already says where the edges are.
    // A constant since lane 15 became the kind tag (ADR-562) and this field's 61st float had to
    // go. It is the default the control it replaced had settled on.
    let edge = 1.0 - smoothstep(0.0, 0.6, envelope);
    let bite = clamp(amount * (1.0 + max(v.t12.z, 0.0) * edge), 0.0, 1.0);
    return mix(1.0, unit, bite);
}

struct TornadoShape {
    density: f32,
    envelope: f32,
    radialT: f32, // distance from the axis over the funnel radius at this height
    heightT: f32, // 0 at the ground contact, 1 at the wall cloud
    rel: vec3<f32>,
    axis: vec2<f32>, // the axis offset at this height, so a velocity need not recompute it
    inside: bool,
};

// ADR-706: how far below the ground contact the debris cloud's rounded underside reaches, as a
// fraction of `skirtHeight`.
const kTornadoDebrisUnder: f32 = 0.3;

// ADR-706: how far below `h = 0` the field has support, in heights. The funnel's tip reaches
// `-footSoft` at most and the debris underside `-kTornadoDebrisUnder * skirtHeight`; 0.02 is the
// floor the field always had. `mediumBoundOf` in `volume.wgsl` and `world::mediumBound` carry the
// same expression, and `test_medium_bound.cpp` fails when one of the three moves alone.
fn tornadoSupportBelow(v: TornadoUniformsWgsl) -> f32 {
    return max(0.02, max(max(v.t3.w, 1e-3), kTornadoDebrisUnder * max(v.t4.y, 1e-3)));
}

// The condensation shell's cross-section (§15, §22), as a function of a normalised distance `x`
// from an axis.
//
// A tornado's condensation funnel is a SURFACE, not a filled cone: water condenses where the
// pressure drop is steepest, which is the sheath around the core, and the literature is explicit
// that a funnel is "initially transparent, only becoming opaque when it kicks up dust, debris or
// rain". So the density peaks in a shell at `x = 1` and the interior is a separate, artist-owned
// amount. `coreDensity` at 0 is a hollow tube whose far wall shows through its near one, at 1 a
// solid smoke column, and `coreRadius` slides the boundary between them.
//
// ADR-706 made this a function because it now has two callers: the funnel, around the funnel
// radius, and the debris cloud, around its own. A debris cloud is the same kind of thing -- a
// rotating sheath of particulate around a core -- so it takes the same cross-section rather than
// a fifth set of rules.
fn tornadoSheath(v: TornadoUniformsWgsl, x: f32) -> f32 {
    let edgeSoft = max(v.t3.x, 1e-3);
    let shellWidth = max(v.t2.x, 1e-3);
    let d = (x - 1.0) / shellWidth;
    let shell = exp(-d * d) * max(v.t2.y, 0.0);
    let interior = max(v.t2.w, 0.0) * (1.0 - smoothstep(clamp(v.t2.z, 0.0, 1.0), 1.0, x));
    // The outer fade starts beyond the shell's crest, or it would eat the outer half of the very
    // feature it is there to bound.
    let outer = 1.0 - smoothstep(1.0 + shellWidth, 1.0 + shellWidth + edgeSoft, x);
    return (shell + interior) * outer;
}

fn tornadoEvaluate(v: TornadoUniformsWgsl, p: vec3<f32>, t: f32, filterWidth: f32) -> TornadoShape {
    var s: TornadoShape;
    s.density = 0.0;
    s.envelope = 0.0;
    s.radialT = 0.0;
    s.heightT = 0.0;
    s.rel = vec3<f32>(0.0);
    s.axis = vec2<f32>(0.0);
    s.inside = false;

    let height = v.t0.w;
    if (height <= 0.0) {
        return s; // the gate: a scene that authors no tornado pays for nothing
    }
    s.rel = p - v.t0.xyz;
    let h = s.rel.y / height;
    s.heightT = h;

    // Compact support in height, and compactly so (ADR-369's lesson about early-outs that are
    // exact rather than approximate). Nothing above the wall cloud; nothing below the funnel's tip
    // or the debris cloud's underside, whichever reaches further (ADR-706).
    let skirtHeight = max(v.t4.y, 1e-3);
    let footSoft = max(v.t3.w, 1e-3);
    if (h > 1.08 || h < -tornadoSupportBelow(v)) {
        return s;
    }

    let hc = clamp(h, 0.0, 1.0);
    s.axis = tornadoAxisAt(v, hc, t);
    let planar = s.rel.xz - s.axis;
    let dist = length(planar);
    let radius = max(tornadoRadiusAt(v, hc), 1e-3);
    s.radialT = dist / radius;

    let edgeSoft = max(v.t3.x, 1e-3);
    let shellWidth = max(v.t2.x, 1e-3);
    // The debris cloud reaches further out than the funnel does, so the early-out has to clear
    // both or it clips the one feature that most says "tornado".
    let skirtRadius = max(v.t1.x * max(v.t4.x, 1.0) * (1.0 + max(v.t4.w, 0.0)), 1e-3);
    // The cloud reaches further out than either of the other two, so all three bounds go into the
    // early-out. A bound that clips the widest feature is a bound that deletes the silhouette it
    // exists to make cheap.
    let cloudRadius = max(v.t1.z * max(v.t8.w, 1.0), 1e-3);
    if (dist > max(max(radius * (1.0 + shellWidth + edgeSoft), skirtRadius), cloudRadius)) {
        return s;
    }

    // ---- the funnel ----------------------------------------------------------------------------
    //
    // §16's lifecycle, as one control. `touchdown` is how far down the funnel has condensed: at 1
    // it reaches the ground, and below that it hangs, with only the debris cloud marking the
    // circulation at the surface. That is not a stylisation -- it is the order real tornadoes do it
    // in, and the references are blunt that "debris swirls are usually evident PRIOR TO the
    // condensation funnel reaching the surface".
    //
    // ADR-706: and the funnel ENDS IN A TIP, not a plane. This used to fade the density across a
    // horizontal band at full radius, which at any camera above the base is the column's lower
    // rim seen as an ellipse -- "a column that stops". The radius now closes with the same ramp
    // (a square root, so the end is rounded rather than pointed), which is the shape a condensation
    // funnel actually has where it stops: a hanging funnel tapers, it is not sawn off.
    let reach = 1.0 - clamp(v.t3.z, 0.0, 1.0);
    let foot = smoothstep(reach - footSoft, reach + footSoft, h);
    let tip = sqrt(foot);
    let rr = dist / max(radius * tip, 1e-3);

    var funnel = tornadoSheath(v, rr);
    // A slight thickening of the funnel ITSELF toward the top. The mass of the wall cloud is a
    // separate term further down; this is only the funnel widening into it.
    let wallCloud = 1.0 + max(v.t3.y, 0.0) * smoothstep(0.55, 1.0, hc);
    let cap = 1.0 - smoothstep(1.0, 1.06, h);
    funnel = funnel * wallCloud * cap * foot;

    let angle = atan2(planar.y, planar.x);
    funnel = funnel * tornadoStripes(v, rr, hc, angle, t);
    // Applied to the funnel and to the debris below, but not to the cloud: suction vortices are a
    // feature of the column and of where it meets the ground, and putting lobes on the wall cloud
    // would read as a fairground ride.
    let suction = tornadoSuction(v, rr, hc, angle, t);
    funnel = funnel * suction;

    // ---- the debris cloud (ADR-706) ------------------------------------------------------------
    //
    // Where the column meets the ground, and the strongest read cue after the silhouette. It was a
    // radially filled disc whose density fell as the square of height -- most of its mass in the
    // bottom few metres and a hard plane under it, which from a camera above the base is a pool on
    // the floor. It is now the SAME sheath cross-section as the funnel, around a radius that is a
    // MOUND in height:
    //
    //   * widest at the ground by `skirtFlare`, a LINEAR flare. The first version squared it, and
    //     rendered as a concave trumpet foot -- a lamp base on the cone, and a hard-edged pyramid on
    //     the dust devil, which had been a soft mound before. Debris is a convex mass;
    //   * superelliptic shoulders (`(1 - hd^4)^(1/4)`), full for most of the height and then
    //     rounding over. The plain `sqrt(1 - hd^2)` dome, rendered, still came back a cone at the
    //     dust devil's proportions;
    //   * thinning over its upper two thirds, the way lifted dust does, so its top is soft while its
    //     sides are a silhouette;
    //   * a short rounded UNDERSIDE, `kTornadoDebrisUnder` of its height, so a column standing on
    //     nothing does not end in a disc -- and one standing on terrain loses nothing, because the
    //     ground hides it.
    //
    // It carries the striations and the suction lobes, so it turns with the column, and it is
    // deliberately not multiplied by `foot`: the debris is there whether or not the funnel has
    // condensed to the ground. `u` is scaled so the sheath's whole outer fade lies inside the
    // mound, which keeps `skirtWidth` meaning the cloud's outer width and keeps the bound unchanged.
    var skirt = 0.0;
    if (v.t4.z > 0.0 && h < skirtHeight) {
        let hd = h / skirtHeight;
        let rTop = v.t1.x * max(v.t4.x, 1.0);
        var rb = 0.0;
        if (hd >= 0.0) {
            let hd2 = hd * hd;
            let shoulder = sqrt(sqrt(max(1.0 - hd2 * hd2, 0.0)));
            rb = rTop * (1.0 + max(v.t4.w, 0.0) * (1.0 - hd)) * shoulder;
        } else {
            let q = hd / kTornadoDebrisUnder;
            rb = skirtRadius * sqrt(max(1.0 - q * q, 0.0));
        }
        let u = dist * (1.0 + shellWidth + edgeSoft) / max(rb, 1e-3);
        let thin = 1.0 - smoothstep(0.3, 1.0, max(hd, 0.0));
        skirt = v.t4.z * tornadoSheath(v, u) * thin * tornadoStripes(v, u, hc, angle, t) * suction;
    }

    // ---- the wall cloud ---------------------------------------------------------------------
    //
    // A separate mass at the top, and separating it is a CORRECTION rather than an addition.
    //
    // The first version of this field made the wall cloud the funnel's own top radius, so the
    // funnel flared continuously from about a third of its height upward. Rendered, that came back
    // a **champagne flute**: a smooth trumpet, unmistakably a vortex of some kind and just as
    // unmistakably not a tornado. The silhouette was wrong in exactly the way the brief's §37 A
    // is about, and no amount of detail would have repaired it.
    //
    // The references say why. A classic funnel is NARROW for most of its height, and the wall
    // cloud is a distinct, much wider, much flatter mass that it descends from -- three to ten
    // times the funnel's width. Continuity between the two is what destroys the read, because the
    // one proportion that says "tornado" and nothing else is a thin column under a broad cloud.
    //
    // So the funnel's top radius is the FUNNEL's now, the cloud is its own term with its own width
    // and height, and the silhouette has a shoulder where they meet instead of a curve.
    var cloud = 0.0;
    let cloudHeight = clamp(v.t9.x, 1e-3, 1.0);
    if (h > 1.0 - cloudHeight && v.t9.y > 0.0) {
        // Rising to full within the LOWER THIRD of the cloud band and holding, rather than
        // easing across the whole of it. A cloud whose density ramps smoothly from its underside
        // to its top renders as a lens -- a flying saucer -- because the only edge in it is the
        // silhouette of an ellipsoid. A real wall cloud has a flat, heavy base and its interest is
        // underneath. This is the cheapest structural version of that.
        let ch = smoothstep(1.0 - cloudHeight, 1.0 - cloudHeight * 0.66, h);
        // Widening upward, so the underside is a bowl the funnel hangs out of rather than a disc
        // it happens to meet.
        let hb = smoothstep(1.0 - cloudHeight, 1.0, h);
        let cr = max(v.t1.z * max(v.t8.w, 1.0) * (0.62 + 0.38 * hb), 1e-3);
        cloud = v.t9.y * (1.0 - smoothstep(0.55, 1.0, dist / cr)) * ch * cap;
    }

    let envelope = funnel + skirt + cloud;
    if (envelope < 1.0e-6) {
        return s;
    }
    s.envelope = envelope;
    s.inside = true;
    // §45 as one multiply. The envelope above IS the tornado; this makes it look natural. At
    // `cloudAmount` 0 the detail term is exactly 1 and the density is the analytic field, which is
    // §38's Mode 1 -- a slider rather than a rebuild, and the render the brief asks to be shown
    // before any detail is added.
    s.density = envelope * tornadoDetail(v, s.radialT, hc, angle, envelope, radius, t, filterWidth);
    return s;
}

// What the volumetric march wants and all it wants.
fn tornadoDensityAt(v: TornadoUniformsWgsl, p: vec3<f32>, t: f32, filterWidth: f32) -> f32 {
    return tornadoEvaluate(v, p, t, filterWidth).density;
}

// ---- motion -----------------------------------------------------------------------------------

struct TornadoSampleWgsl {
    density: f32,
    velocity: vec3<f32>,
    radialT: f32,
    heightT: f32,
    envelope: f32,
};

// The medium's velocity, as a **Burgers-Rott vortex** (ADR-580 §5).
//
//   u_r     = -a r / 2
//   u_z     = +a z
//   u_theta = (Gamma / 2 pi r) (1 - exp(-r^2 / Rc^2))
//
// Three properties earn it the place over the brief's §13 sketch of three independent knobs:
//
//   * It is **divergence-free by construction**: div u = (1/r) d/dr(r u_r) + du_z/dz = -a + a = 0.
//     Free incompressibility, with no pressure solve. That is most of what a real-time fluid
//     cannot afford, and it is the reason the SAME `a` appears in the radial and axial terms.
//     `lift` scales the axial term independently because artists ask for it; anything other than
//     1 breaks the cancellation, and that is stated on the control rather than discovered.
//   * It is **steady**. Lamb-Oseen's core grows as sqrt(4 nu t) and the storm dissipates on its
//     own, which is wrong for a phenomenon that has to hold for the length of a shot.
//   * Rankine -- solid-body inside a core radius, potential flow outside -- is rejected as the base
//     because it has no radial and no axial component at all (it is a spinning tube), and because
//     its velocity gradient is discontinuous at the core boundary, which is an edge for a hard line
//     to live on (ADR-369).
//
// The `(1 - exp(-r^2/Rc^2))` factor is what makes this finite on the axis. Without it the
// tangential term diverges as 1/r and a particle at the centre is thrown to infinity.
fn tornadoVelocityAt(v: TornadoUniformsWgsl, p: vec3<f32>, t: f32) -> vec3<f32> {
    let height = v.t0.w;
    if (height <= 0.0) {
        return vec3<f32>(0.0);
    }
    let rel = p - v.t0.xyz;
    let hc = clamp(rel.y / height, 0.0, 1.0);
    let planar = rel.xz - tornadoAxisAt(v, hc, t);
    let dist = length(planar);
    var outward = vec2<f32>(1.0, 0.0);
    if (dist > 1e-4) {
        outward = planar / dist;
    }
    let tangent = vec2<f32>(-outward.y, outward.x);
    let rc = max(v.t6.y, 1e-3);
    let x = dist / rc;
    // Guarded against the 0/0 at the axis: the limit of (1 - exp(-x^2))/x is 0, so the tangential
    // speed goes to zero there, which is the physically right answer and the numerically safe one.
    var swirlSpeed = 0.0;
    if (dist > 1e-4) {
        swirlSpeed = v.t6.x * (1.0 - exp(-x * x)) / dist;
    }
    // Scaled by the height profile, so the core spins faster where it is narrower (§17).
    swirlSpeed = swirlSpeed * tornadoRotationAt(v, hc);
    let a = v.t6.z;
    let radialSpeed = -a * dist * 0.5;
    let axialSpeed = a * rel.y * max(v.t6.w, 0.0);
    return vec3<f32>(tangent.x * swirlSpeed + outward.x * radialSpeed, axialSpeed,
                     tangent.y * swirlSpeed + outward.y * radialSpeed);
}

// Everything a consumer has asked for so far, in one evaluation.
fn sampleTornado(v: TornadoUniformsWgsl, p: vec3<f32>, t: f32) -> TornadoSampleWgsl {
    // A point sample: nothing is being integrated, so nothing can alias, so every scale is taken.
    let s = tornadoEvaluate(v, p, t, 0.0);
    var out: TornadoSampleWgsl;
    out.density = s.density;
    out.envelope = s.envelope;
    out.radialT = s.radialT;
    out.heightT = s.heightT;
    out.velocity = vec3<f32>(0.0);
    if (!s.inside) {
        return out;
    }
    out.velocity = tornadoVelocityAt(v, p, t);
    return out;
}
