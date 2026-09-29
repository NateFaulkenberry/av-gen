// Stylized water (ADR-099). Its own pipeline inside the scene pass, drawn after the opaque
// geometry and blended over it.
//
// Why not pbr_shade.wgsl. Water is not a metallic-roughness surface with an alpha on it; it is a
// volume seen through its own boundary, and the three things it needs most are things the shared
// path cannot offer:
//   * the *thickness* of the water at this pixel, which comes from the depth of the bed behind it
//     and not from where the mesh's edge happens to fall. That is what turns a stair-stepped
//     polygon boundary into a shoreline, and a flat fill into a channel.
//   * a normal it computes for itself, from layered travelling ripples, rather than one it is
//     handed on the vertex.
//   * a reflection, which for night water is most of the image.
// It reuses lighting.wgsl -- the same packed lights, the same shadow atlas, the same cascade
// lookup -- so the moon that lights the bank is the moon that glints off the river.
//
// Bindings: group 0 is the shared frame group (frame uniforms, lights, shadows, the linear scene
// depth); group 1 is the object uniforms, for the model matrix; group 2 is this shader's own
// WaterUniforms; group 3 is the environment cube the reflection is sampled from.
//
// The vertex carries, from world::buildChunkWater:
//   position  the surface, at the water level
//   normal    xz = the downstream direction here, y = speed as a fraction of the world's fastest
//   uv        x = bed depth in metres, y = across the channel (1 centreline, 0 bank)
#include "common.wgsl"
#include "lighting.wgsl"
// ADR-207. Water is not shaded through pbr_shade.wgsl (see the header above for why), so it has to
// take the world-effect term itself. It does, and the reason is one frame: Glowmere's elder stands
// in a pool, and a ground ripple that stopped at the shoreline drew a hard straight edge across the
// exact shot the effect exists for. `kProceduralDraw` is what waveGain switches on and is
// declared by every module that includes the file.
const kProceduralDraw: bool = false;
#include "wave_effects.wgsl"
// ADR-230 §6. Water takes the sky's ground light for the same reason ADR-207 records for the world
// effects: Glowmere's elder stands in a pool, and an aurora that lit the bank and stopped at the
// waterline would draw a hard edge across the exact shot the effect exists for.
#include "atmosphere_ground.wgsl"

struct WaterUniforms {
    shallowColor: vec4<f32>,   // rgb, w = metres of depth over which the colour reaches deep
    deepColor: vec4<f32>,      // rgb, w = clarity (metres the bed stays visible through)
    foamColor: vec4<f32>,      // rgb, w = foam amount
    glowColor: vec4<f32>,      // rgb, w = glow amount
    sparkleColor: vec4<f32>,   // rgb, w = sparkle amount
    reflectTint: vec4<f32>,    // rgb, w = reflection multiplier
    emissive: vec4<f32>,       // rgb * intensity, w = 0
    surface: vec4<f32>,        // x = fresnel, y = specular, z = roughness, w = maxOpacity
    ripples: vec4<f32>,        // x = amplitude, y = scale (cycles/m), z = speed, w = chop
    shore: vec4<f32>,          // x = foamWidth (m), y = edgeFade (m), z = refraction (m),
                               //   w = cascade (the whitewater on a falls, ADR-985)
    life: vec4<f32>,           // x = glowScale, y = glowCoverage, z = glowDepth (m), w = swell (m)
    params: vec4<f32>,         // x = flow time (s), y = world's fastest body (m/s), z = 1 when the
                               //   linear-depth texture is real, w = 0
    // ---- tears (ADR-916) begin ----
    tears: vec4<f32>,          // x = amount (ripple amplitude a seam adds), y = shear (m), z = coverage,
                               //   w = lattice cell (m)
    tearShape: vec4<f32>,      // x = spacing (m), y = stretch, z = drift (m/s), w = wind coupling
    tearFrame: vec4<f32>,      // xy = seam direction in XZ, z = 1 when it follows the scene wind, w = 0
    // ---- tears (ADR-916) end ----
};

@group(2) @binding(0) var<uniform> water: WaterUniforms;

@group(3) @binding(0) var iblSampler: sampler;
@group(3) @binding(1) var irradianceMap: texture_cube<f32>;
@group(3) @binding(2) var prefilteredMap: texture_cube<f32>;
@group(3) @binding(3) var brdfLut: texture_2d<f32>;

struct WaterOut {
    @invariant @builtin(position) clip: vec4<f32>,
    @location(0) worldPos: vec3<f32>,
    @location(1) flow: vec3<f32>,   // xz = direction, y = speed fraction; NOT normalised
    @location(2) uv: vec2<f32>,
    @location(3) prevClip: vec4<f32>,
};

@vertex
fn vs_water(in: VertexIn) -> WaterOut {
    var out: WaterOut;
    var local = in.position;
    // The swell (§15): one slow standing rise over the whole body, for a transition event. It is
    // a pure function of world position and the flow clock, so it is identical live and offline.
    if (water.life.w > 0.0) {
        let t = water.params.x;
        let s = sin(local.x * 0.045 + t * 0.55) * cos(local.z * 0.037 - t * 0.41);
        local.y = local.y + s * water.life.w;
    }
    let world = object.model * vec4<f32>(local, 1.0);
    out.clip = frame.viewProj * world;
    out.worldPos = world.xyz;
    // Deliberately not normalised and deliberately not through the normal matrix's translation:
    // this is a direction in XZ plus a scalar in y, and normalising it would destroy the scalar.
    let dir = (object.normalMatrix * vec4<f32>(in.normal.x, 0.0, in.normal.z, 0.0)).xyz;
    out.flow = vec3<f32>(dir.x, in.normal.y, dir.z);
    out.uv = in.uv;
    out.prevClip = frame.prevViewProj * (object.prevModel * vec4<f32>(local, 1.0));
    return out;
}

// ---- ripples -------------------------------------------------------------------------------
//
// Value noise with its analytic gradient, so one evaluation gives both the height and the slope:
// a finite-difference normal would cost three evaluations per layer instead of one. Quintic fade,
// because the cubic smoothstep's second derivative is discontinuous at the cell boundary and that
// shows up in a specular highlight as a faint grid.
//
// The lattice hash is an integer one (ADR-914). The `fract(sin(dot(cell, k)) * 43758)` it replaces
// is only as good as `sin` is on a large argument, and the arguments here are large: the sparkle
// runs at 26 times the ripple scale, which on Glowmere's river is 135 cells a metre, so 350 m from
// the origin the hash was taking the sine of numbers near ten million -- where one float step is a
// whole radian and the "random" value is whatever the GPU's range reduction happens to return.
// Integer arithmetic is exact at any cell and identical on every device.
fn waterHashU(x: u32) -> u32 {
    // Wellons' lowbias32: every input bit reaches every output bit.
    var h = x;
    h = h ^ (h >> 16u);
    h = h * 0x7feb352du;
    h = h ^ (h >> 15u);
    h = h * 0x846ca68bu;
    h = h ^ (h >> 16u);
    return h;
}

fn waterHash(cell: vec2<f32>) -> f32 {
    let c = bitcast<vec2<u32>>(vec2<i32>(cell));
    let h = waterHashU(c.x ^ waterHashU(c.y + 0x9e3779b9u));
    return f32(h >> 8u) * (1.0 / 16777216.0);
}

// Returns (value in [-1, 1], d/dx, d/dy).
fn noiseD(p: vec2<f32>) -> vec3<f32> {
    let i = floor(p);
    let f = p - i;
    let u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    let du = 30.0 * f * f * (f * (f - 2.0) + 1.0);
    let a = waterHash(i);
    let b = waterHash(i + vec2<f32>(1.0, 0.0));
    let c = waterHash(i + vec2<f32>(0.0, 1.0));
    let d = waterHash(i + vec2<f32>(1.0, 1.0));
    let k1 = b - a;
    let k2 = c - a;
    let k3 = a - b - c + d;
    let value = a + k1 * u.x + k2 * u.y + k3 * u.x * u.y;
    let dx = du.x * (k1 + k3 * u.y);
    let dy = du.y * (k2 + k3 * u.x);
    return vec3<f32>(value * 2.0 - 1.0, dx * 2.0, dy * 2.0);
}

// ---- bounded advection (ADR-914) -----------------------------------------------------------
//
// Everything on this surface that travels -- the three ripple layers, the sparkle, the foam's
// break-up and the glow -- used to sample its noise at `p - v * t`: an offset that grows without
// limit. Wherever the baked flow `v` differs between two neighbouring vertices, the offset across the
// triangle between them differs by `|dv| * t`, and the noise there is compressed by that over the
// triangle's width. At the start of a film nothing shows; by three minutes Glowmere's pool boundary
// compressed its ripples 28-fold and every Chaikin node of the river drew a stepped seam. The whole
// channel streaked, too, because the bank shear is a smooth `dv` that grows the same way.
//
// So nothing travels for more than one period. Each effect keeps two samples of its field, half a
// period apart; each sample travels for one period and is then re-seeded at a new place in the noise,
// at the instant its weight is zero. The weights are triangles (1 at the middle of a sample's period,
// 0 at its ends), so one sample is always carrying the picture while the other resets. They are
// normalised by the root of their squares rather than by their sum: two uncorrelated fields averaged
// with weights summing to one have *less* contrast than either, and a linear blend pulses flat twice a
// period. `desync` shifts the cycle by up to two periods across the world, so the crossfade happens at
// different moments in different places and never across a whole frame at once.
//
// The travel at any instant is at most `v * period`, so what a flow discontinuity can do to the noise is
// bounded by `|dv| * period` whatever the second: the same at 10 s and at 200 s.
const kFlowPeriod: f32 = 8.0;

struct FlowPhases {
    travel: vec2<f32>, // seconds each sample has travelled this cycle: x = sample 0, y = sample 1
    weight: vec2<f32>, // their weights, variance-preserving
    jump0: vec2<f32>,  // where in the noise sample 0's current cycle reads, in noise cells
    jump1: vec2<f32>,
};

// A new, unrelated place in the noise for each cycle, so a sample that resets does not replay the
// patch it showed a period ago. A hash of the cycle's index, so it is bounded for any t.
fn flowJump(cycle: f32, salt: u32) -> vec2<f32> {
    let hx = waterHashU(bitcast<u32>(i32(cycle)) ^ (salt * 0x9e3779b9u));
    let hy = waterHashU(hx ^ 0x68e31da4u);
    return (vec2<f32>(f32(hx >> 8u), f32(hy >> 8u)) * (1.0 / 16777216.0) - 0.5) * 128.0;
}

// `period` is the cycle in seconds; a faster effect uses a shorter one, so its travel per cycle is
// bounded by the same distance a layer at the water's own speed covers in `kFlowPeriod`.
fn flowPhases(t: f32, period: f32, desync: f32, salt: u32) -> FlowPhases {
    var out: FlowPhases;
    // ---- flow phases (ADR-914) begin ----
    let c0 = t / period + desync;
    let c1 = c0 + 0.5;
    let f0 = fract(c0);
    let f1 = fract(c1);
    let w0 = 1.0 - abs(2.0 * f0 - 1.0);
    let w1 = 1.0 - abs(2.0 * f1 - 1.0);
    out.travel = vec2<f32>(f0, f1) * period;
    // w0 + w1 is always 1, so the root is never under 1/sqrt(2).
    out.weight = vec2<f32>(w0, w1) * inverseSqrt(w0 * w0 + w1 * w1);
    out.jump0 = flowJump(floor(c0), salt);
    out.jump1 = flowJump(floor(c1), salt);
    // ---- flow phases (ADR-914) end ----
    return out;
}

// Where in its cycle this part of the surface is, 0..2 periods, over about eighty metres. Slow on
// purpose: the travel is `v * period * fract(t / period + desync)`, so how fast `desync` changes
// across the world is a shear of its own -- at most 0.15 on a river as fast as Glowmere's, and a
// constant one that never grows.
fn flowDesync(p: vec2<f32>) -> f32 {
    return noiseD(p * (1.0 / 80.0) + vec2<f32>(-71.3, 23.9)).x + 1.0;
}
// ---- tears (ADR-916) begin ----
// Deliberate tears: thin, stepped seams across the water in which the ripples are compressed into
// dense parallel stripes. The owner's reference is the accident ADR-914 removed -- ripples sheared by
// an unbounded offset wherever the baked flow jumped between two vertices of the water mesh -- so this
// is that accident's mechanism with every quantity that made it an accident replaced by one somebody
// chose:
//
//   * where: a rigid lattice of `tearCell` metres aligned with the seam direction (the scene wind, or
//     an authored angle), drifting along it at `tearDrift`. Each lattice corner is on one side or the
//     other of the zero line of a smooth field whose seams are `tearSpacing` apart and `tearStretch`
//     times longer than wide; `S` interpolates the corners' sides over the lattice triangles with the
//     water mesh's own split, so where the side changes `S` ramps 0 -> 1 across one triangle. That is
//     the stepped band, one or two cells wide, running along the lattice axes and the anti-diagonal.
//   * how much: across a band the ripple layers' sample point moves back while the pixel moves
//     forward, so `tearShear` metres of ripple fold into the band: a compression of `tearShear /
//     tearCell`, `tearShear * rippleScale` stripes, all of them parallel to the band, as in the
//     reference. Bounded, and never a function of the timeline second. (A shift *along* the seam was
//     tried first: that is a shear, not a compression, and it tilts the ripples into a twisted rope at
//     `atan(cell / shear)` to the band instead of packing them.)
//   * how far it reaches: nowhere past the band. The shift goes out and comes back inside the band (a
//     tent in `S`: out across the first half, back across the second), so the band holds its ripple
//     twice, mirrored about its middle, and outside every band the shift is exactly zero. A shear that
//     changes -- a gust, a route, a seam fading in -- moves the ripples inside a band and nowhere else.
//     (A step in `S`, relaxed back to zero over the surrounding metres, was tried first. Value noise
//     sits near zero, so the relaxation reached most of the surface and restretched ripples everywhere,
//     which is the "whole surface busy" the brief rules out.)
//   * which seams show: `tearCoverage`, a threshold on a second slow field, so a lower coverage removes
//     seams rather than fading all of them.
//   * the wind: with `tearWind` above 0 the local wind's strength and gust envelope (`windSampleAt`, on
//     the same clock) scale the shear, so a gust front crossing the water tightens the seams it
//     crosses in step with the grass on the bank. A scene with no wind leaves them as authored.
//   * the slope: the band's own `tears` of ripple amplitude on top of the surface's. The analytic
//     gradient does not carry the shear's Jacobian, so compressed stripes keep the slope they had --
//     which on a calm surface (GV3's 0.05) is too faint to read. This is what makes a seam visible
//     without making the rest of the surface any busier.
//   * the pixels: the band's width and the compressed layers are both counted in reference pixels
//     (ADR-915). A layer compressed below three of them fades, as an uncompressed one does, and a band
//     narrower than about two fades out whole rather than breaking into a crawl of dots.
//
// A surface whose `tears` is 0 is not drawn by this code at all. Everything between these markers is
// compiled out of a second pipeline (`WaterRenderer`), and a material with no tears is drawn with that
// one: exactly the shader ADR-914 and ADR-915 left, so its pixels are the same bytes. (Gating the code
// on `tears > 0` inside one shader was tried and was not enough: with the tear written in `fs_water`
// and read in `rippleGradient`, the compiler built the shared ripple path differently and ~130 glint
// channels of the QA scene moved, by up to 0.125.) test_water_tears_gpu.cpp holds the renderer to it.
var<private> waterTearShift: vec2<f32>;
var<private> waterTearBlur: f32;

struct WaterTear {
    shift: vec2<f32>, // metres the ripple layers' sample point moves
    blur: f32,        // metres per reference pixel the shear adds to the layers' footprint
    band: f32,        // ripple amplitude the seam adds here
};

// The seam field at a lattice point: negative on one side of a seam, positive on the other.
fn tearField(lattice: vec2<f32>, toNoise: vec2<f32>) -> f32 {
    return noiseD(lattice * toNoise + vec2<f32>(113.7, -57.1)).x;
}

fn tearAt(p: vec2<f32>, t: f32, dPdx: vec2<f32>, dPdy: vec2<f32>, refScale: f32) -> WaterTear {
    var out: WaterTear;
    // The seams' frame. The wind's is its steady direction, uniform across the frame; turbulence turns
    // the grass, not the lattice, or the seams would bend wherever the air does.
    var along = water.tearFrame.xy;
    if (water.tearFrame.z > 0.5) {
        along = frame.windDir.xy;
    }
    along = along * inverseSqrt(max(dot(along, along), 1e-8));
    let across = vec2<f32>(-along.y, along.x);
    let cell = water.tears.w;
    let spacing = water.tearShape.x;
    // Lattice coordinates, in cells, drifting rigidly along the seams.
    let lattice = vec2<f32>(dot(p, along) - water.tearShape.z * t, dot(p, across)) / cell;
    let toNoise = vec2<f32>(cell / (spacing * water.tearShape.y), cell / spacing);
    let base = floor(lattice);
    let f = lattice - base;
    let a = step(0.0, tearField(base, toNoise));
    let b = step(0.0, tearField(base + vec2<f32>(1.0, 0.0), toNoise));
    let c = step(0.0, tearField(base + vec2<f32>(0.0, 1.0), toNoise));
    let d = step(0.0, tearField(base + vec2<f32>(1.0, 1.0), toNoise));
    // The water mesh's split ({a, c, b} and {b, c, d}: the anti-diagonal), so a seam steps the way
    // the accident did.
    var s: f32;
    var slope: vec2<f32>; // dS per cell, along and across
    if (f.x + f.y < 1.0) {
        s = a + (b - a) * f.x + (c - a) * f.y;
        slope = vec2<f32>(b - a, c - a);
    } else {
        s = d + (d - c) * (f.x - 1.0) + (d - b) * (f.y - 1.0);
        slope = vec2<f32>(d - c, d - b);
    }
    // How much S changes from one pixel to the next: its world gradient against the pixel's own
    // footprint, which is what the band's width in pixels and the compressed layers' fade are made of.
    let gradient = (along * slope.x + across * slope.y) / cell;
    let perPixel = abs(dot(gradient, dPdx)) + abs(dot(gradient, dPdy));
    // Which seams show: a threshold on a slow field, mapped so `tearCoverage` is roughly the share of
    // the seam network that does (value noise is not uniform; the quintic puts its median at 0.5 and its
    // deciles at 0.19 and 0.81). Exactly none at 0 and all of it at 1.
    let x = water.tears.z - 0.5;
    let threshold = 0.5 - 0.6 * x - 9.6 * x * x * x * x * x;
    let m = noiseD(lattice * vec2<f32>(cell / spacing, cell / (spacing * 2.5)) + vec2<f32>(-41.9, 88.3)).x *
                0.5 + 0.5;
    let live = smoothstep(threshold - 0.1, threshold + 0.1, m);
    // The wind: 1 = the seams as authored; a fresh breeze (strength 1) with no gust leaves them there.
    var energy = 1.0;
    if (frame.windDir.w > 0.5 && water.tearShape.w > 0.0) {
        let w = windSampleAt(vec3<f32>(p.x, 0.0, p.y), t);
        energy = mix(1.0, clamp(w.strength * (1.0 + w.gust), 0.0, 2.0), water.tearShape.w);
    }
    // The shear comes in with the first quarter of the amount, so a route that lifts `tears` off 0 --
    // which is where a surface changes pipeline -- grows the seams instead of snapping the ripples
    // beside them sideways by the whole shear in one frame.
    let emerge = min(water.tears.x * 4.0, 1.0);
    // How far the sample point moves at the middle of a band: half the shear plus half the cell, so each
    // half of the band packs half the shear into half the cell and the band holds the whole shear. The
    // wind scales the shear; a seam that is hidden or still coming in moves nothing.
    let travel = 0.5 * (water.tears.y * energy + cell) * live * emerge;
    out.shift = across * (travel * (1.0 - abs(2.0 * s - 1.0)));
    out.blur = 2.0 * travel * perPixel * refScale;
    // The band: flat across the middle of the ramp, soft at its two edges so the added slope does not
    // start on a hard pixel line, and gone when the band is under a couple of reference pixels wide.
    let plateau = smoothstep(0.0, 0.3, s) * smoothstep(0.0, 0.3, 1.0 - s);
    let bandPixels = 1.0 / max(perPixel * refScale, 1e-6);
    out.band = water.tears.x * plateau * live * smoothstep(1.0, 3.0, bandPixels);
    return out;
}

// ---- tears (ADR-916) end ----

// The layered surface (§11). Three travelling layers, each finer, weaker and faster than the one
// below it, each carried *along* the local flow and pushed a different amount *across* it. The
// result is one gradient, in metres of rise per metre of travel, which becomes the normal.
//
// Layers rather than one texture because one scrolling pattern reads as a sheet being dragged
// across the world: everything moves at the same speed in the same direction and the eye finds the
// repeat immediately. Three that disagree never line up, and the slowest of them is the only one
// travelling at the water's real speed -- the finer ones are the wind on it.
// `footprint` is the world-space size of one pixel on this surface, so `wavelength / footprint` is
// how many pixels one cycle of a layer spans. A layer under a couple of pixels cannot be resolved
// and only aliases -- and on water that aliasing is not a static shimmer but a crawling one, because
// the pattern is travelling. Fading each layer out as it approaches the footprint is the whole of
// the level of detail here, and it is why the far reach of a river stays smooth instead of boiling.
//
// The pixels are *reference* pixels (ADR-915): a 1080-row frame's, or the frame's own when it has
// fewer rows. Counted in the frame's own pixels, a render at twice the resolution kept detail out to
// twice the distance -- GV3's previews (960x540 at 2x supersampling, 1080 rows) showed its far river
// as a mirror and its final (1920x1080 at 2x, 2160 rows) showed ripple texture right across it. At or
// above 1080 rows every resolution now fades the same world-space detail, so a preview is a preview
// of the final; below 1080 rows the frame's own pixels still set the limit, because detail finer than
// a real pixel can only alias.
const kWaterReferenceRows: f32 = 1080.0;

fn rippleLayerFade(frequency: f32, footprint: f32) -> f32 {
    let pixels = 1.0 / (max(frequency, 1e-4) * max(footprint, 1e-4));
    return smoothstep(1.0, 3.0, pixels);
}

// The sparkle wants the *other* end of that too. A glint is a point of light; a glint field whose
// cells are eighty pixels across is a handful of white ovals lying on the water, which is what the
// near field gives when the frequency is fixed in world space. This is a band pass in screen space:
// a sparkle cell shows between about three and seventy pixels wide and nowhere else, so the effect
// looks the same size whatever the resolution and whatever the distance, and simply is not there a
// metre from the lens -- where a real surface shows you its ripples, not its glitter.
fn sparkleBandFade(frequency: f32, footprint: f32) -> f32 {
    let pixels = 1.0 / (max(frequency, 1e-4) * max(footprint, 1e-4));
    return smoothstep(2.0, 5.0, pixels) * (1.0 - smoothstep(28.0, 70.0, pixels));
}

// Each layer is two samples of its field under ADR-914's bounded advection; its gradient is their
// variance-preserving blend. `desync` is `flowDesync` at this point.
fn rippleGradient(p: vec2<f32>, flowDir: vec2<f32>, speed: f32, t: f32, footprint: f32,
                  desync: f32) -> vec2<f32> {
    let across = vec2<f32>(-flowDir.y, flowDir.x);
    let scale = water.ripples.y;
    let chop = water.ripples.w;
    // The point every layer samples around, and the footprint every layer fades against. Named
    // rather than read from the arguments, because a tear (ADR-916) moves the first and widens the
    // second.
    var at = p;
    var blur = footprint;
    // ---- tears (ADR-916) begin ----
    at = at - waterTearShift;
    blur = blur + waterTearBlur;
    // ---- tears (ADR-916) end ----
    var g = vec2<f32>(0.0);

    // Each layer's *height* falls as roughly the square of its frequency, which is what keeps the
    // broad swell in charge of the shape. Amplitudes that fall only as fast as the frequency rises
    // give every layer the same slope, and a surface whose slope is dominated by its finest layer
    // reads as crazed glass rather than as water. Nature does this too: capillary waves are
    // millimetres tall on top of metre-long swell.
    //
    // Each layer's period is `kFlowPeriod` over its speed multiplier, so the distance any layer
    // travels in one cycle is the same: the water's own speed times `kFlowPeriod`.
    // Layer 1: the broad swell of the current itself, travelling downstream at the water's speed.
    let v1 = flowDir * speed;
    let ph1 = flowPhases(t, kFlowPeriod, desync, 1u);
    let a1 = noiseD((at - v1 * ph1.travel.x) * scale + ph1.jump0);
    let b1 = noiseD((at - v1 * ph1.travel.y) * scale + ph1.jump1);
    g = g + (a1.yz * ph1.weight.x + b1.yz * ph1.weight.y) * scale * rippleLayerFade(scale, blur);

    // Layer 2: shorter, quicker, angled off the current -- the wind-driven chop.
    let s2 = scale * 2.7;
    let v2 = (flowDir * 1.35 + across * chop) * (speed * 1.6);
    let ph2 = flowPhases(t, kFlowPeriod / 1.6, desync + 0.37, 2u);
    let a2 = noiseD((at - v2 * ph2.travel.x) * s2 + vec2<f32>(37.2, 11.9) + ph2.jump0);
    let b2 = noiseD((at - v2 * ph2.travel.y) * s2 + vec2<f32>(37.2, 11.9) + ph2.jump1);
    g = g + (a2.yz * ph2.weight.x + b2.yz * ph2.weight.y) * s2 * 0.20 * rippleLayerFade(s2, blur);

    // Layer 3: capillary detail, travelling the other way across the flow, so the two upper layers
    // interfere and the pattern never settles. This is the layer the sparkle rides.
    let s3 = scale * 7.4;
    let v3 = (flowDir * 0.6 - across * chop * 1.7) * (speed * 2.4);
    let ph3 = flowPhases(t, kFlowPeriod / 2.4, desync + 0.71, 3u);
    let a3 = noiseD((at - v3 * ph3.travel.x) * s3 + vec2<f32>(-19.4, 63.1) + ph3.jump0);
    let b3 = noiseD((at - v3 * ph3.travel.y) * s3 + vec2<f32>(-19.4, 63.1) + ph3.jump1);
    g = g + (a3.yz * ph3.weight.x + b3.yz * ph3.weight.y) * s3 * 0.05 * rippleLayerFade(s3, blur);
    return g;
}

// The view-space distance the opaque scene was drawn at under this pixel, or a very large number
// where there was no depth prepass this frame (in which case the shader falls back to the vertex
// depth and the surface degrades to what it was before ADR-099 rather than to garbage).
fn bedDepthAt(uv: vec2<f32>) -> f32 {
    if (water.params.z < 0.5) {
        return 1.0e7;
    }
    // Bilinear, by hand, because the linear depth target is r32float and not filterable.
    //
    // Nearest was adequate while this number only tinted the water. It is not adequate now that the
    // shoreline, the foam and the depth colour are all derived from it: the bed under a river bank
    // drops fast, so one texel of quantisation is tens of centimetres of water depth, and at a
    // clarity of about a metre that is a visible step in how much bed shows through. The same
    // quantisation is what makes the number move when the camera turns -- the point lands on a
    // different texel and reports a different depth -- which is a property a depth ought not have.
    //
    // Bleeding across a silhouette is the usual objection to filtering a depth buffer. It does not
    // bite here: what is being read is the bed *under* the water, a continuous surface, and the
    // half-texel of bleed at its far edge is behind the water's own shore fade.
    let size = vec2<f32>(textureDimensions(sceneLinearDepth, 0));
    let at = clamp(uv, vec2<f32>(0.0), vec2<f32>(1.0)) * size - vec2<f32>(0.5);
    let base = floor(at);
    let f = at - base;
    let hi = vec2<i32>(size) - vec2<i32>(1);
    let i0 = clamp(vec2<i32>(base), vec2<i32>(0), hi);
    let i1 = clamp(vec2<i32>(base) + vec2<i32>(1), vec2<i32>(0), hi);
    let d00 = textureLoad(sceneLinearDepth, vec2<i32>(i0.x, i0.y), 0).r;
    let d10 = textureLoad(sceneLinearDepth, vec2<i32>(i1.x, i0.y), 0).r;
    let d01 = textureLoad(sceneLinearDepth, vec2<i32>(i0.x, i1.y), 0).r;
    let d11 = textureLoad(sceneLinearDepth, vec2<i32>(i1.x, i1.y), 0).r;
    return mix(mix(d00, d10, f.x), mix(d01, d11, f.x), f.y);
}

// ---- cascade (ADR-985) begin ----
// Water on a steep course is a cascade (ADR-985). Everything in `fs_water` was written for a sheet that
// lies flat: the ripple normal is built about +Y, the depth under a pixel is measured straight down, the
// side of the surface the eye is on is read from heights, and the tears lie on a lattice in XZ. On a
// falls -- GV3's runs thirty metres down its hill at up to 44 degrees -- each of those is wrong, and
// together they drew the hill water as a flat panel with a brick wall printed on it: seen from the
// valley, the part of the falls above the camera's own height was shaded as if from under the water (a
// dark ceiling), its depth was read almost edge-on (so it thinned toward transparent, like a shore), its
// light was a level pool's, and the XZ lattice of the tears stood up on it as rows of bricks.
//
// So a surface steeper than 20 degrees DOWN ITS OWN FLOW is shaded about its own plane, blended in up to
// 30 degrees so nothing switches at a line: the face's normal comes from the position's derivatives
// (constant over a triangle, and the water mesh's triangles are 1.2 m), the eye's side is the plane's, the
// depth is measured across the sheet, the ripples are turned onto the slope, the tears fade out (a
// flat-water look), and `cascade` lays whitewater on it -- streaks along the flow, running faster than the
// river. "Down its flow" because the mesh's bank has slivers of its own: the edge triangles between a wet
// corner and a fitted dry one tilt ACROSS the flow, 13-14 degrees on the QA scene's river, and taken for a
// cascade one of them changed its shoreline from frame to frame (the depth forensics' water6_2 test).
//
// Flat water never enters any of it: `steep` is exactly 0 under 20 degrees and every branch is skipped.
// test_water_cascade_gpu.cpp holds the renderer to that: flat water is, byte for byte, what the shader
// with these blocks removed draws.
const kCascadeFlatCos: f32 = 0.9396926;   // cos 20 degrees: flatter than this is a river, untouched
const kCascadeSteepCos: f32 = 0.8660254;  // cos 30 degrees: steeper than this is all cascade
const kCascadeGrain: f32 = 0.45;          // metres: the whitewater's grain across the flow
const kCascadeComb: f32 = 0.2;            // metres between the samples a streak is combed from (under half
                                          //   a grain, so a streak is smooth along its length, not a ladder)
const kCascadeCombSamples: u32 = 12u;     // so a streak is about 2.4 m long
const kCascadeRush: f32 = 3.0;            // how many times its river's speed the whitewater runs

// `v` turned by the rotation that takes +Y onto the unit vector `to` (to.y > 0), in closed form.
fn cascadeTurn(v: vec3<f32>, to: vec3<f32>) -> vec3<f32> {
    let axis = vec3<f32>(to.z, 0.0, -to.x); // cross(+Y, to): its length is the sine of the turn
    return v * to.y + cross(axis, v) + axis * (dot(axis, v) / (1.0 + to.y));
}

// How white the cascade is here, 0..1. Streaks along the flow, combed: each is the mean of twelve samples
// of the value noise stepped back up the local flow (a line integral), so a streak needs no frame of its
// own. A frame turned to the local flow would swing the whole pattern about the world origin wherever
// the flow bends -- 300 m out, a degree of meander moves it five metres. It travels under ADR-914's
// bounded advection like everything else here, and where a streak is too fine to resolve it fades to
// the streaks' mean white, so a distant falls is a pale sheet rather than a crawl.
fn cascadeWhite(p: vec2<f32>, dir: vec2<f32>, speed: f32, t: f32, desync: f32, footprint: f32) -> f32 {
    let ph = flowPhases(t, kFlowPeriod / kCascadeRush, desync + 0.29, 7u);
    let run = dir * (speed * kCascadeRush);
    let f = 1.0 / kCascadeGrain;
    var a = 0.0;
    var b = 0.0;
    for (var k = 0u; k < kCascadeCombSamples; k = k + 1u) {
        let up = p - dir * (f32(k) * kCascadeComb);
        a = a + noiseD((up - run * ph.travel.x) * f + ph.jump0).x;
        b = b + noiseD((up - run * ph.travel.y) * f + ph.jump1).x;
    }
    // The comb's mean has a standard deviation of about 0.21 on this noise, so a quarter of the sheet is
    // bright streak; 0.57 is the mean white, measured on the same noise, that a far falls fades to.
    let field = clamp((a * ph.weight.x + b * ph.weight.y) / f32(kCascadeCombSamples) + 0.5, 0.0, 1.0);
    let white = 0.35 + 0.65 * smoothstep(0.45, 0.75, field);
    return mix(0.57, white, rippleLayerFade(f, footprint));
}
// ---- cascade (ADR-985) end ----

@fragment
fn fs_water(in: WaterOut, @builtin(front_facing) frontFacing: bool) -> SceneOut {
    let screenUv = in.clip.xy * frame.targetSize.zw;
    let toEye = frame.cameraPos.xyz - in.worldPos;
    let viewDistance = length(toEye);
    let v = toEye / max(viewDistance, 1e-4);
    let viewDepth = max(dot(in.worldPos - frame.cameraPos.xyz, frame.cameraForward.xyz), 1e-4);
    // Seen from below (the camera is under the surface) the sheet's up is still +Y in the world;
    // what changes is which side of it the eye is on, and that is what `underwater` carries.
    var underwater = frame.cameraPos.y < in.worldPos.y;

    let flowDir2 = in.flow.xz;
    let flowLen = length(flowDir2);
    let dir = select(vec2<f32>(1.0, 0.0), flowDir2 / max(flowLen, 1e-4), flowLen > 1e-4);
    let speedFraction = clamp(in.flow.y, 0.0, 1.0);
    // Still water still has a surface. A body with no flow gets the ripple pattern at a small fixed
    // rate, which is what wind does to a pond; without this floor a tarn is a mirror and reads as
    // glass rather than as water.
    let speed = max(speedFraction * water.params.y, 0.08) * water.ripples.z;
    let t = water.params.x;

    // ---- the surface normal ------------------------------------------------------------------
    // The world-space size of one pixel on this surface, which is what the ripple layers fade
    // against. Derivatives, so it is taken here in uniform control flow and nowhere inside a branch.
    let dPdx = dpdx(in.worldPos.xz);
    let dPdy = dpdy(in.worldPos.xz);
    let footprint = length(dPdx) + length(dPdy);
    // ...and of one reference pixel (ADR-915), which is what every fade on this surface counts: a
    // 1080-row frame's pixel, or this frame's own when it has fewer rows than that.
    let refScale = max(frame.targetSize.y / kWaterReferenceRows, 1.0);
    let lodFootprint = footprint * refScale;
    // Where this part of the surface is in the advection cycle (ADR-914), shared by every effect
    // below that travels, each at its own offset into it.
    let desync = flowDesync(in.worldPos.xz);
    // ---- cascade (ADR-985) begin ----
    // The face's own up, from the derivatives (so here, in uniform control flow), and how steep it is down
    // its flow: exactly 0 under 20 degrees or across the flow, 1 past 30 along it. A camera below a falls
    // sees its upper side, so on a slope the eye's side of the surface is the plane's, not a comparison of
    // heights.
    let faceCross = cross(vec3<f32>(dPdx.x, dpdx(in.worldPos.y), dPdx.y),
                          vec3<f32>(dPdy.x, dpdy(in.worldPos.y), dPdy.y));
    let faceSide = faceCross * inverseSqrt(max(dot(faceCross, faceCross), 1e-30));
    let faceUp = select(faceSide, -faceSide, faceSide.y < 0.0);
    var steep = 0.0;
    if (faceUp.y < kCascadeFlatCos) {
        // The face's up leans toward its downhill side; a cascade's downhill is its flow.
        let downhill = faceUp.xz * inverseSqrt(max(dot(faceUp.xz, faceUp.xz), 1e-12));
        steep = (1.0 - smoothstep(kCascadeSteepCos, kCascadeFlatCos, faceUp.y)) *
                smoothstep(0.5, 0.8, dot(downhill, dir));
    }
    let cascadeUp = normalize(mix(vec3<f32>(0.0, 1.0, 0.0), faceUp, steep));
    if (steep > 0.0) {
        underwater = dot(toEye, cascadeUp) < 0.0;
    }
    // ---- cascade (ADR-985) end ----
    var rippleAmplitude = water.ripples.x;
    // ---- tears (ADR-916) begin ----
    if (water.tears.x > 0.0) {
        let tear = tearAt(in.worldPos.xz, t, dPdx, dPdy, refScale);
        waterTearShift = tear.shift;
        waterTearBlur = tear.blur;
        rippleAmplitude = rippleAmplitude + tear.band;
        // ---- cascade (ADR-985) begin ----
        // A lattice in XZ stood up on a slope is a wall of bricks: the tears fade out with the steepness.
        if (steep > 0.0) {
            waterTearShift = tear.shift * (1.0 - steep);
            waterTearBlur = tear.blur * (1.0 - steep);
            rippleAmplitude = water.ripples.x + tear.band * (1.0 - steep);
        }
        // ---- cascade (ADR-985) end ----
    }
    // ---- tears (ADR-916) end ----
    let gradient = rippleGradient(in.worldPos.xz, dir, speed, t, lodFootprint, desync) * rippleAmplitude;
    var n = normalize(vec3<f32>(-gradient.x, 1.0, -gradient.y));
    // ---- cascade (ADR-985) begin ----
    // The ripples, built about +Y, turned onto the slope.
    if (steep > 0.0) {
        n = cascadeTurn(n, cascadeUp);
    }
    // ---- cascade (ADR-985) end ----
    if (underwater) {
        n = vec3<f32>(-n.x, -n.y, -n.z);
    }
    let nDotV = max(dot(n, v), 1e-4);

    // ---- how much water is under this pixel ---------------------------------------------------
    // The vertical depth of the bed below the surface arrives on the vertex; the *thickness* the
    // view ray crosses comes from the scene's own depth, which is the bed because a blended surface
    // is not in the depth prepass. Reading the depth at a point the ripple normal displaces is the
    // whole of the refraction here: it wobbles the shoreline and the depth colour by a few
    // centimetres of world space, which is what water does to the things under it.
    // The displacement is authored in metres, so it is applied in metres: the surface point is
    // pushed along the ripple normal's horizontal part and re-projected, which is what makes
    // `refraction` mean the same thing at two metres and at fifty. Adding a world offset straight
    // to a screen uv, which is the obvious shortcut, makes it mean neither.
    let refracted = in.worldPos + vec3<f32>(n.x, 0.0, n.z) * water.shore.z;
    let refractedClip = frame.viewProj * vec4<f32>(refracted, 1.0);
    let ndc = refractedClip.xy / max(refractedClip.w, 1e-4);
    let distorted = vec2<f32>(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
    let bed = bedDepthAt(distorted);
    // Thickness along the ray, floored at zero: where the bed is nearer than the surface the water
    // is behind something and contributes nothing, which the depth test has already handled.
    //
    // Both depths are measured along the camera's *axis* (`viewDepth` is a dot with
    // `cameraForward`, and the linear depth target is written the same way), so their difference is
    // an axis interval and not a ray length. The two are equal only down the middle of the frame:
    // at the corner of a 60-degree frame the ray is a sixth longer than the interval. Dividing by
    // the cosine is what makes this number mean the same thing everywhere in the frame -- without
    // it the same patch of river is a different colour when the camera turns, which is a bug that
    // only shows up when something moves.
    let cosAxis = max(dot(frame.cameraForward.xyz, -v), 1e-3);
    var thickness = max(bed - viewDepth, 0.0) / cosAxis;
    // The vertical depth of the water under this pixel, which is what the shoreline, the foam and
    // the depth colour are all made of.
    //
    // It comes from the scene's depth, not from the vertex. The thickness above already did; this
    // did not, and that was the whole of the jagged waterline: `in.uv.x` is a per-vertex baked
    // depth, so every shoreline effect was quantised to the water mesh's own tessellation and a
    // river drawn on a coarse grid got a stair-stepped bank. The comment further down this file
    // says all three read the scene's depth "so they are smooth across a quad boundary the geometry
    // is not" -- it describes the intent, and until now only one of the three did it.
    //
    // The conversion is the inverse of the fallback's: thickness is measured along the view ray, so
    // the vertical drop is that times the ray's vertical component.
    //
    // No floor on `v.y` here, which is the difference between this and the fallback's division. The
    // product is self-correcting: a shallower ray travels proportionally further before it reaches
    // the bed, so `thickness * abs(v.y)` recovers the same depth at any angle -- which is the whole
    // point of measuring it this way. Flooring the multiplier breaks exactly that, overstating the
    // depth by the ratio at a grazing view, and it also makes the grazing-angle cap below inert,
    // because that cap is `6 * vertical` and a floor of 0.15 keeps `6 * vertical` above
    // `thickness` whatever the angle. One floor in the wrong place disabled a guard three lines
    // down without touching it.
    var vertical = thickness * abs(v.y);
    if (water.params.z < 0.5) {
        // No prepass: the vertex depth is the only thickness available, and the surface degrades to
        // what it was before ADR-099 -- a waterline on the mesh's edge -- rather than to garbage.
        vertical = max(in.uv.x, 0.0);
        thickness = vertical / max(abs(v.y), 0.15);
    }
    // ---- cascade (ADR-985) begin ----
    // On a slope the depth that matters is across the sheet, not straight down: seen from the valley, a
    // falls is crossed almost edge-on to the vertical, and its vertical depth read it as a shore.
    if (steep > 0.0 && water.params.z >= 0.5) {
        vertical = thickness * abs(dot(v, cascadeUp));
    }
    // ---- cascade (ADR-985) end ----
    // A surface seen from a very grazing angle over a shallow bed reports a long thickness and goes
    // opaque a metre from the bank, which is right for a lake and wrong for a stream you can see the
    // stones in. Capping the ray's thickness at a few times the vertical depth keeps the shallows
    // shallow whatever angle they are seen from. The constant is a shaping number, not a knob:
    // there is no shot where the right answer is "six times, but for this river eleven".
    thickness = min(thickness, vertical * 6.0 + 0.25);

    // ---- colour by depth ----------------------------------------------------------------------
    let depthMix = 1.0 - exp(-vertical / max(water.shallowColor.w, 1e-3));
    var body = mix(water.shallowColor.rgb, water.deepColor.rgb, depthMix);

    // ---- bioluminescence under the surface (§16) ----------------------------------------------
    // Sparse patches drifting downstream, visible only through water deep enough to hold them and
    // faded out again where the water is too thick to see into. `glowCoverage` is a threshold on a
    // noise field rather than a multiplier on it, so turning it down removes patches instead of
    // dimming the whole river -- which is the difference between "something is glowing under there"
    // and "the river is green".
    if (water.glowColor.w > 0.0) {
        // ADR-914: two samples of the patch field, each drifting for one cycle.
        let phg = flowPhases(t, kFlowPeriod / 0.45, desync + 0.83, 5u);
        let vg = dir * (speed * 0.45);
        let drift0 = in.worldPos.xz - vg * phg.travel.x;
        let drift1 = in.worldPos.xz - vg * phg.travel.y;
        let patchField = clamp((noiseD(drift0 * water.life.x + phg.jump0).x * phg.weight.x +
                                noiseD(drift1 * water.life.x + phg.jump1).x * phg.weight.y) * 0.5 + 0.5,
                               0.0, 1.0);
        let edge = 1.0 - clamp(water.life.y, 0.0, 1.0);
        let mask = smoothstep(edge, min(edge + 0.22, 1.0), patchField);
        // A second, faster field breaks each patch into drifting organisms rather than one blob.
        let grainOffset = vec2<f32>(11.0, -4.0);
        let grain = clamp((noiseD(drift0 * water.life.x * 6.3 + grainOffset + phg.jump0).x * phg.weight.x +
                           noiseD(drift1 * water.life.x * 6.3 + grainOffset + phg.jump1).x * phg.weight.y) *
                              0.5 + 0.5,
                          0.0, 1.0);
        let depthGate = smoothstep(water.life.z * 0.35, water.life.z, vertical) *
                        (1.0 - smoothstep(water.life.z * 6.0, water.life.z * 14.0, vertical));
        // And it keeps to the channel. Depth alone does not say where the middle of a river is -- a
        // wide shallow reach is shallow all the way across -- so the vertex carries the cross-channel
        // coordinate and the glow lives where the water is, not where the bed happens to dip.
        let midstream = smoothstep(0.12, 0.55, in.uv.y);
        body = body + water.glowColor.rgb * water.glowColor.w * mask *
                          mix(0.45, 1.0, grain) * depthGate * midstream;
    }
    body = body + water.emissive.rgb * depthMix;

    // ---- reflection ---------------------------------------------------------------------------
    // Schlick, but with the *normal-incidence* reflectance authored rather than fixed at water's
    // real 0.02. That number is right and it is not what this is for: at the angle a camera looks
    // at a river from, physical water returns five per cent of a night sky and the other ninety-five
    // is the black bed, which is exactly the flat dark ribbon this work exists to replace. Raising
    // f0 is the one stylisation here that is a deliberate lie, and it is the one that turns the
    // surface back into a surface.
    let f0 = clamp(water.surface.x, 0.0, 1.0);
    let fresnel = f0 + (1.0 - f0) * pow(1.0 - nDotV, 5.0);
    var reflected = water.reflectTint.rgb * frame.styledSky.rgb;
    if (frame.envParams.w > 0.5) {
        let r = reflect(-v, n);
        // Deliberately a mid mip rather than mip 0: a stylized surface wants the *shape* of the sky
        // and the moon in it, not a sharp second copy of the star field, and the roughness the
        // water carries is the right amount of blur to ask for.
        let lod = frame.envParams.y * clamp(water.surface.z * 2.2, 0.0, 1.0);
        reflected = textureSampleLevel(prefilteredMap, iblSampler, envRotate(r), lod).rgb *
                    water.reflectTint.rgb;
    }
    reflected = reflected * water.reflectTint.w;

    // ---- the moon's own glint -----------------------------------------------------------------
    // Directional lights only, with the shared shadow lookup, so a reach of river under a hill goes
    // dark with the bank beside it. The clustered local lights are deliberately not walked: they
    // are 18% of the scene pass on this world and the water is fullscreen-adjacent, and what they
    // would add to a specular this tight is a handful of pixels.
    var glint = vec3<f32>(0.0);
    let directional = u32(frame.lightCounts.x + 0.5);
    let alphaR = max(water.surface.z * water.surface.z, 1e-3);
    for (var i = 0u; i < directional; i = i + 1u) {
        if (i >= 4u) { break; }
        let light = sceneLights[i];
        let l = -normalize(light.directionRange.xyz);
        let nDotL = dot(n, l);
        if (nDotL <= 0.0) { continue; }
        let h = normalize(l + v);
        let d = distributionGgxL(max(dot(n, h), 0.0), alphaR);
        let vis = visibilitySmithL(nDotV, nDotL, alphaR);
        var shadow = 1.0;
        if ((u32(light.cone.w + 0.5) & FLAG_CASTS_SHADOW) != 0u) {
            shadow = shadowFactor(light, in.worldPos, n, l, viewDepth,
                                  gradientNoise(screenUv * frame.targetSize.xy) * 6.28318531,
                                  shadowTaps());
        }
        glint = glint + light.colorIntensity.rgb * d * vis * nDotL * shadow;
    }
    glint = glint * water.surface.y;

    // ---- sparkle (§15 highs) -------------------------------------------------------------------
    // A glint that rides a field far finer than any ripple layer, thresholded hard so it is a few
    // points of light rather than a shimmer. The threshold is the point: a sparkle multiplied out
    // of a smooth field is glitter paint, and what a real surface does is catch the light on a
    // handful of facets that happen to be turned the right way at that instant.
    //
    // Band-passed in screen space (see `sparkleBandFade`): sub-pixel sparkle crawls, and sparkle
    // whose cells are eighty pixels across is a row of white ovals lying on the water.
    var sparkle = vec3<f32>(0.0);
    if (water.sparkleColor.w > 0.0) {
        let frequency = water.ripples.y * 26.0;
        let fade = sparkleBandFade(frequency, lodFootprint);
        if (fade > 0.0) {
            // ADR-914: two samples, each scrolling through the noise for one cycle. The scroll is in
            // noise cells, not metres, as it always was: at any frequency a glint field turns over
            // about as often.
            let phs = flowPhases(t, kFlowPeriod / 2.1, desync + 0.19, 4u);
            let base = in.worldPos.xz * frequency;
            let vs = dir * (speed * 2.1);
            let crest = clamp((noiseD(base - vs * phs.travel.x + phs.jump0).x * phs.weight.x +
                               noiseD(base - vs * phs.travel.y + phs.jump1).x * phs.weight.y) * 0.5 + 0.5,
                              0.0, 1.0);
            let cut = smoothstep(0.90, 0.995, crest);
            // And only where the surface is already turned toward the eye's reflection of the sky:
            // a sparkle on a part of the water that is not reflecting anything is paint.
            sparkle = water.sparkleColor.rgb * water.sparkleColor.w * cut * fade * (0.2 + fresnel * 1.6);
        }
    }

    // ---- the shoreline (§10) -------------------------------------------------------------------
    // Three things happen at the bank, and the reason it works is that all three read the *scene's*
    // depth rather than the mesh's edge, so they are smooth across a quad boundary the geometry
    // is not. (They read it through `vertical`, which is derived from the depth buffer above. It
    // was a vertex attribute until the bank turned out to be stair-stepped on a coarse channel.)
    //   1. the surface fades out as the water thins, over `edgeFade` metres of vertical depth
    //   2. a foam band sits on the waterline, broken up by the ripple field so it is a line of
    //      surf and not a contour
    //   3. the foam is brightest where the water is moving, which puts it on the outside of the
    //      bends and in the shallows, where a real river puts it
    let shoreFade = smoothstep(0.0, max(water.shore.y, 1e-3), vertical);
    var foam = 0.0;
    if (water.foamColor.w > 0.0) {
        let band = 1.0 - smoothstep(0.0, max(water.shore.x, 1e-3), vertical);
        // The break-up: the same travelling field as the ripples, so the surf moves with the water
        // rather than sitting painted on the terrain, at a frequency fine enough to read as surf and
        // faded against the pixel footprint like everything else here. A coarse break-up field is
        // what turns a foam line into a row of white blobs lying on the shallows.
        let surfFrequency = water.ripples.y * 7.0;
        // ADR-914: two samples, each scrolling for one cycle, in noise cells as the sparkle does.
        let phf = flowPhases(t, kFlowPeriod / 0.9, desync + 0.53, 6u);
        let surfBase = in.worldPos.xz * surfFrequency;
        let vf = dir * (speed * 0.9);
        let surf = clamp((noiseD(surfBase - vf * phf.travel.x + phf.jump0).x * phf.weight.x +
                          noiseD(surfBase - vf * phf.travel.y + phf.jump1).x * phf.weight.y) * 0.5 + 0.5,
                         0.0, 1.0);
        // Two thresholds, not one: the band says "near the waterline" and the field says "and on a
        // crest", and a foam that fires on either reads as scum on still water.
        let broken = smoothstep(0.46, 0.88, surf * 0.55 + band * 0.55) *
                     rippleLayerFade(surfFrequency, lodFootprint);
        foam = band * broken * water.foamColor.w * mix(0.45, 1.0, speedFraction);
    }
    // ---- cascade (ADR-985) begin ----
    // Whitewater on the slope, in the foam's colour and composited as foam is, thinning out where the
    // water does so the bank stays the shoreline's.
    if (steep > 0.0 && water.shore.w > 0.0 && !underwater) {
        foam = foam + cascadeWhite(in.worldPos.xz, dir, speed, t, desync, lodFootprint) * water.shore.w * steep *
                          shoreFade;
    }
    // ---- cascade (ADR-985) end ----

    // ---- composite ------------------------------------------------------------------------------
    // Transmission first: how much of the bed survives. Beer-Lambert over the ray's thickness, so
    // the same surface is glass at the edge and solid in the channel with nothing authored per
    // pixel to make it so.
    var alpha = 1.0 - exp(-thickness / max(water.deepColor.w, 1e-3));
    alpha = min(alpha, water.surface.w);
    // Fresnel adds opacity rather than colour: at a grazing angle you stop seeing the bed because
    // the sky is in the way, which is the cue that reads as "this is a surface".
    alpha = mix(alpha, 1.0, fresnel * 0.85);
    alpha = alpha * shoreFade;
    // Foam is opaque; it is the one part of the water that is not seen through.
    alpha = clamp(alpha + foam * 0.85, 0.0, 1.0);

    if (underwater) {
        // Seen from beneath, the surface is a dark ceiling with the sky in a cone overhead. Cheap
        // and deliberately not a full total-internal-reflection model: what matters is that a
        // camera that dips below the waterline does not see the same picture it saw above it.
        body = water.deepColor.rgb * 0.6;
        alpha = mix(0.55, 0.9, 1.0 - nDotV);
    }

    var color = mix(body, reflected, fresnel) + glint + sparkle + water.foamColor.rgb * foam;
    // Ambient occlusion from the banks: water in a cut channel is darker than water in the open,
    // and the AO buffer already knows which is which.
    let occlusion = sampleAmbientOcclusion(screenUv, viewDepth, vec3<f32>(0.0, 1.0, 0.0));
    let ao = mix(frame.styledSky.w, 1.0, clamp(occlusion.visibility, 0.0, 1.0));
    color = color * mix(ao, 1.0, fresnel);
    // ADR-207: additive and pre-fog, as on every other surface. The surface normal is the water's
    // own, so a ripple crossing a pool reads as ground-facing and takes the same response weight the
    // bank beside it does -- which is what makes the crossing invisible.
    let fx = wavesAt(in.worldPos, n, glint + sparkle);
    // ADR-230 §6. Water's albedo is its own shaded colour rather than a base-colour texture, so the
    // wash is scaled by a constant instead: a pool is a dark mirror, and multiplying the sky's light
    // by an already-lit surface would double-count it.
    let skyLit = atmosphereGroundAt(in.worldPos, n) * 0.35;
    color = applyFog(color + fx.radiance + skyLit, in.worldPos);

    var out: SceneOut;
    // The pipeline uses the conventional non-premultiplied SrcAlpha blend state. Keep alpha in
    // the target and let the blend state apply it once; multiplying RGB here would apply alpha
    // twice and make shallow water and shoreline transitions too dark.
    out.color = vec4<f32>(color, alpha);
    out.normalRoughness = packNormalRoughness(n, water.surface.z, 5.0);
    out.velocity = screenVelocityAt(in.clip, in.prevClip);
    // The glint and the sparkle are what should bloom; the body colour should not, or a wide river
    // washes the whole frame. The alpha lane is the bloom weight.
    out.emission = vec4<f32>((glint + sparkle) * alpha + fx.radiance, 1.0);
    out.ids = packIds(object.ids.x, object.ids.y);
    return out;
}

// Depth-only entry, for the passes that want the surface's silhouette without shading it. Water is
// blended and so is in none of them today; the entry exists so a future opaque-water tier is a
// pipeline change and not a shader rewrite.
@fragment
fn fs_water_depth(in: WaterOut) {
}
