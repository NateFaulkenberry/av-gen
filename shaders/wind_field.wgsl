// The wind field (ADR-055), and the ONLY WGSL transliteration of `src/core/wind.cpp` there is.
//
// ADR-370 says a leaf and the branch it fell from must be reading one description of the air. For
// a while they were not: `wind.wgsl` sampled the field out of the frame uniforms, and
// `particles.wgsl` -- which binds no frame group and never has -- carried a hand-maintained second
// copy of the same nineteen lines called `particleWindAt`, coupled to the first by a comment. The
// copy had already lost a field (`WindSample::phase`, the flutter's spatial phase) and nothing
// could have said so: there was no test tying the two together, and the nearest thing to one
// measured a stalk's tip in a wind with every stochastic term switched off.
//
// So the arithmetic lives here, once, as a pure function of a `WindField`, a world position and a
// time -- the same shape `vortex.wgsl` uses and for the same reason. `wind.wgsl` adds the one
// frame-bound convenience (`windSampleAt`) on top; `particles.wgsl` includes this file directly and
// passes the copy of the field its own uniforms carry. Neither can drift from the other, because
// there is nothing left for them to drift in.
//
// Nothing here is `sin(time)`: every temporal term is also spatial, so a gust front crosses the
// world at a real speed instead of the whole meadow twitching at once.
//
// This file has no `#include` and reads no binding, deliberately: the include directive does not
// de-duplicate, so a module that already has `common.wgsl` (and therefore `wind.wgsl`) must not
// include this as well.

const WIND_TAU: f32 = 6.28318530718;

// The packed field, mirroring `wind::WindUniforms` (src/core/wind.hpp) member for member. Both
// sides of every parity test start from these bytes, which is why the CPU's `sampleWind` takes the
// packed form rather than the authored one.
//
//   dir        xy = unit direction in XZ, z = speed, w = 1 when the wind is on
//   region     x = tau/regionScale, y = regionAmount, z = regionDrift*tau, w = turbulence (radians)
//   gust       x = tau/gustScale, y = gustSpeed (m/s), z = gustAmount, w = gustSharpness
//   turbulence x = tau/turbulenceScale, y = turbulenceSpeed (m/s), z = tau/flutterScale, w = 0
struct WindField {
    dir: vec4<f32>,
    region: vec4<f32>,
    gust: vec4<f32>,
    turbulence: vec4<f32>,
};

struct WindSample {
    direction: vec2<f32>, // unit, in XZ, already turned by the turbulence
    strength: f32,        // >= 0, the steady flow modulated regionally
    gust: f32,            // 0..gustAmount, the travelling front's envelope here and now
    phase: f32,           // spatial phase of the flutter term (radians)
};

// `p` is a world position (only xz are read: the field is columnar, which is what a plant rooted in
// the ground experiences). `t` is render time, already delayed by the caller's species lag.
//
// Every expression below is `wind::sampleWind`'s, term for term and in the same order, so the two
// agree within float rounding. `tests/rendering/test_wind_parity_gpu.cpp` compares them over a grid
// of positions and times with every stochastic term ON, including `phase`.
fn windSampleFrom(w: WindField, p: vec3<f32>, t: f32) -> WindSample {
    let d = w.dir.xy;
    let perp = vec2<f32>(-d.y, d.x);
    let a = dot(p.xz, d); // metres downwind
    let c = dot(p.xz, perp); // metres across the wind

    // Regional strength: two long incommensurate travelling waves.
    let kr = w.region.x;
    let drift = w.region.z;
    let r1 = sin(kr * (0.94 * a + 0.34 * c) - t * drift);
    let r2 = sin(kr * 1.63 * (0.61 * a - 0.79 * c) + t * drift * 0.61 + 2.1);
    let region = max(1.0 + w.region.y * 0.5 * (r1 + r2), 0.0);

    // The gust front: a sharpened pulse advancing downwind at gustSpeed metres a second, its phase
    // bent across the wind so the front is a curve rather than a ruler sweeping the map.
    let kg = w.gust.x;
    let gp = kg * (a - t * w.gust.y) + 0.8 * sin(c * kg * 0.37);
    let envelope = pow(max(0.5 + 0.5 * sin(gp), 0.0), w.gust.w);

    // Turbulence turns the local direction rather than scaling it.
    let kt = w.turbulence.x;
    let ts = w.turbulence.y;
    let s1 = sin(kt * (0.31 * a + 0.95 * c) - t * ts * kt);
    let s2 = sin(kt * 1.41 * (-0.87 * a + 0.5 * c) + t * ts * kt * 0.83 + 1.3);
    let turn = w.region.w * 0.5 * (s1 + s2);
    let ct = cos(turn);
    let st = sin(turn);

    var out: WindSample;
    out.direction = d * ct + perp * st;
    out.strength = w.dir.z * region;
    out.gust = envelope * w.gust.z;
    out.phase = w.turbulence.z * (0.7 * a + 0.71 * c);
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

// The tip's horizontal offset, in units of the plant's height: the only thing the field and the
// species decide. Tier 1 (ADR-056) replaces this half with a number an integrator produced on the
// CPU and leaves the other half exactly as it is, which is why a plant can change tier between one
// frame and the next without moving.
fn windBend(w: WindSample, sway: vec4<f32>, timing: vec4<f32>, variance: f32, rnd: vec4<f32>,
            tFlutter: f32) -> vec2<f32> {
    // Per-instance amplitude, so neighbours differ while the region they share stays coherent.
    let amp = 1.0 + variance * (rnd.z * 2.0 - 1.0);
    let s = w.strength;
    let along = sway.x * s + sway.y * s * w.gust;
    // The flutter is the plant ringing at its own resonance, phase-offset per instance so a patch
    // rattles out of step even though it leans together.
    let flutter = sway.z * s * sin(w.phase + timing.y * tFlutter + rnd.x * WIND_TAU);
    let perp = vec2<f32>(-w.direction.y, w.direction.x);
    return (w.direction * along + perp * flutter) * amp;
}

// ...and what any such offset does to a vertex: the height profile, the soft ceiling, and the drop
// that keeps the stem's length.
fn bendDisplacement(objectY: f32, bend: vec2<f32>, timing: vec4<f32>, plant: vec4<f32>,
                    instanceScaleY: f32) -> vec3<f32> {
    // Height along the plant, 0 at the root. This is the whole anchoring story: whatever the
    // displacement is, it is multiplied by a curve that is exactly zero where the stem meets soil.
    let h = clamp((objectY - plant.x) * plant.y, 0.0, 1.0);
    let profile = pow(h, timing.z);
    let height = plant.z * instanceScaleY; // the plant's world height
    var off = bend * (profile * height);

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

fn windDisplacement(objectY: f32, w: WindSample, sway: vec4<f32>, timing: vec4<f32>, plant: vec4<f32>,
                    instanceScaleY: f32, rnd: vec4<f32>, tFlutter: f32) -> vec3<f32> {
    return bendDisplacement(objectY, windBend(w, sway, timing, plant.w, rnd, tFlutter), timing, plant,
                            instanceScaleY);
}
