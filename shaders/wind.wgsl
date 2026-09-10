// The wind field (ADR-055): the WGSL transliteration of core/wind.cpp. Every function here is a
// pure function of the frame's wind uniforms, a world position and a time, and evaluates the same
// expressions in the same order as the CPU so the two agree within float rounding (tests compare
// them). Nothing here is `sin(time)`: every temporal term is also spatial, so a gust front crosses
// the world at a real speed instead of the whole meadow twitching at once.
//
// Reads frame.windDir / windRegion / windGust / windTurb (common.wgsl), so it can only be included
// by a module that already includes common.wgsl.
//
//   windDir    xy = unit direction in XZ, z = speed, w = 1 when the wind is on
//   windRegion x = tau/regionScale, y = regionAmount, z = regionDrift*tau, w = turbulence (radians)
//   windGust   x = tau/gustScale, y = gustSpeed (m/s), z = gustAmount, w = gustSharpness
//   windTurb   x = tau/turbulenceScale, y = turbulenceSpeed (m/s), z = tau/flutterScale, w = 0

const WIND_TAU: f32 = 6.28318530718;

struct WindSample {
    direction: vec2<f32>, // unit, in XZ, already turned by the turbulence
    strength: f32,        // >= 0, the steady flow modulated regionally
    gust: f32,            // 0..gustAmount, the travelling front's envelope here and now
    phase: f32,           // spatial phase of the flutter term (radians)
};

// `p` is a world position (only xz are read: the field is columnar, which is what a plant rooted in
// the ground experiences). `t` is render time, already delayed by the caller's species lag.
fn windSampleAt(p: vec3<f32>, t: f32) -> WindSample {
    let d = frame.windDir.xy;
    let perp = vec2<f32>(-d.y, d.x);
    let a = dot(p.xz, d); // metres downwind
    let c = dot(p.xz, perp); // metres across the wind

    // Regional strength: two long incommensurate travelling waves.
    let kr = frame.windRegion.x;
    let drift = frame.windRegion.z;
    let r1 = sin(kr * (0.94 * a + 0.34 * c) - t * drift);
    let r2 = sin(kr * 1.63 * (0.61 * a - 0.79 * c) + t * drift * 0.61 + 2.1);
    let region = max(1.0 + frame.windRegion.y * 0.5 * (r1 + r2), 0.0);

    // The gust front: a sharpened pulse advancing downwind at gustSpeed metres a second, its phase
    // bent across the wind so the front is a curve rather than a ruler sweeping the map.
    let kg = frame.windGust.x;
    let gp = kg * (a - t * frame.windGust.y) + 0.8 * sin(c * kg * 0.37);
    let envelope = pow(max(0.5 + 0.5 * sin(gp), 0.0), frame.windGust.w);

    // Turbulence turns the local direction rather than scaling it.
    let kt = frame.windTurb.x;
    let ts = frame.windTurb.y;
    let s1 = sin(kt * (0.31 * a + 0.95 * c) - t * ts * kt);
    let s2 = sin(kt * 1.41 * (-0.87 * a + 0.5 * c) + t * ts * kt * 0.83 + 1.3);
    let turn = frame.windRegion.w * 0.5 * (s1 + s2);
    let ct = cos(turn);
    let st = sin(turn);

    var out: WindSample;
    out.direction = d * ct + perp * st;
    out.strength = frame.windDir.z * region;
    out.gust = envelope * frame.windGust.z;
    out.phase = frame.windTurb.z * (0.7 * a + 0.71 * c);
    return out;
}

// ---- Tier 0 vegetation deformation ------------------------------------------------------------
//
// One field sample per instance root, then a per-vertex height profile. The root is anchored
// (profile(0) = 0) and the tip carries the whole displacement; the mesh is never rotated rigidly,
// which is the classic fake-wind look. `sway`, `timing` and `plant` are the per-draw species
// response resolved on the CPU by wind::motionResponse -- the shader evaluates no physics.
//
//   sway   x = steady gain, y = gust gain, z = flutter gain, w = 1 when this draw sways
//   timing x = sway delay (seconds, applied by the caller when sampling), y = flutter omega (rad/s),
//          z = bend curve exponent, w = bend limit (fraction of the plant's height)
//   plant  x = base y in post-source object space, y = 1 / extent y, z = extent y,
//          w = per-instance amplitude variance

fn windDisplacement(objectY: f32, w: WindSample, sway: vec4<f32>, timing: vec4<f32>, plant: vec4<f32>,
                    instanceScaleY: f32, rnd: vec4<f32>, tFlutter: f32) -> vec3<f32> {
    // Height along the plant, 0 at the root. This is the whole anchoring story: whatever the
    // displacement is, it is multiplied by a curve that is exactly zero where the stem meets soil.
    let h = clamp((objectY - plant.x) * plant.y, 0.0, 1.0);
    let profile = pow(h, timing.z);
    let height = plant.z * instanceScaleY; // the plant's world height
    // Per-instance amplitude, so neighbours differ while the region they share stays coherent.
    let amp = 1.0 + plant.w * (rnd.z * 2.0 - 1.0);

    let s = w.strength;
    let along = sway.x * s + sway.y * s * w.gust;
    // The flutter is the plant ringing at its own resonance, phase-offset per instance so a patch
    // rattles out of step even though it leans together.
    let flutter = sway.z * s * sin(w.phase + timing.y * tFlutter + rnd.x * WIND_TAU);
    let perp = vec2<f32>(-w.direction.y, w.direction.x);
    var off = (w.direction * along + perp * flutter) * (amp * profile * height);

    // A soft ceiling on how far a tip may travel: len -> len for small offsets, -> maxLen for large,
    // with no corner where a hard clamp would make a stalk visibly hit a wall.
    let maxLen = timing.w * height * profile;
    let len = length(off);
    off = off * (maxLen / (len + maxLen + 1e-6));

    // A stem that bends keeps its length, so the tip also drops. Without this the plant stretches
    // sideways and reads as a shear rather than a bend.
    let dy = -0.5 * dot(off, off) / max(height * max(h, 0.05), 1e-4);
    return vec3<f32>(off.x, dy, off.y);
}
