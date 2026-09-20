// The Cosmic Ocean (ADR-390): a layered procedural celestial environment, per view ray.
//
// Included by `atmosphere_fx.wgsl` and evaluated inside the atmospheric sky-layer draw -- one
// fullscreen triangle at clip z = 1, depth-tested and never depth-written, blending additively into
// the HDR target and the emission target. Everything that follows is a function of a ray origin, a
// unit direction and the angular size of one pixel. There is no geometry, no instance, no particle
// and no texture anywhere in this file.
//
// **Why this pass and not a shader layer.** `glowmere-cosmos.wgsl`, which this supersedes for the
// Tree of Life, is a background shader layer, and a shader layer's uniforms are 96 bytes of time,
// size and audio: it cannot know where the camera is. So it cannot have parallax, cannot rotate
// with the camera, and its halo is pinned to screen coordinates and slides off the subject the
// moment the shot moves. Everything below needs `ro`, and this is the only sky pass that has it.
//
// **The one mechanism.** Every stratum is a shell of radius `depth` about the effect's centre, and
// is sampled from an eye interpolated towards that centre by the stratum's `parallax`:
//
//     E = centre + (ro - centre) * parallax
//     P = the point where the ray from E along rd meets the shell
//
// `parallax = 0` puts E at the centre, so the stratum depends on direction alone: rotating the
// camera reveals it and translating the camera does not move it. `parallax = 1` gives the true
// geometric parallax of something `depth` metres away. The apparent shift per metre of camera
// travel is `parallax / depth`, so the defaults separate the strata by about ninety to one between
// the dust and the planets -- which is §10's "there is an enormous volume here" as a ratio rather
// than as an adjective.
//
// It is also the whole of §26. Nothing is indexed by a world coordinate that grows without bound;
// every field is a function of a unit direction, so there is no wrap to author, no seam to hide,
// and no precision cliff at the floating island's scale or at any other.
//
// **Nothing here marches.** ADR-374, ADR-379 and ADR-381 were each a day lost to a quantity that
// had to be per metre and was not, and the call site in `volume.wgsl` names all three. This file
// cannot join them: a density here is the *coverage fraction of a shell*, in 0..1, resolved
// analytically. If you find yourself writing `exp(-density * stepLength)` below, you have misread
// the units.
//
// **Anti-aliasing is angular, not distance-based.** `pixelAngle` is the angular size of one pixel,
// taken once by the caller in uniform control flow. Every point-like thing -- a star, a mote, a
// glint -- is faded by the ratio of its solid angle to the pixel's rather than point-sampled, so a
// dense field of sub-pixel stars resolves to a smooth glow instead of a crawling mess. This is the
// same rule `atmosphere_fx.wgsl` applies to a comet's shed fragments, and reusing the pass is what
// makes the number free.
//
// **Its own hashes**, for the reason `atmosphere_fx.wgsl` and `world_effects.wgsl` both give: the
// modules that include this one do not all reach `noise.wgsl`, and a helper that exists in three of
// four consumers is a compile error waiting for the fourth.
//
// ---- why this is its own draw and not a term inside `atmosphere_fx.wgsl` ----
//
// ADR-390's design said one more term inside the existing sky-layer draw, and the implementation
// changed it to a second draw *inside the same render pass*. The reason is plumbing, not rendering.
//
// A term inside `atmosphere_fx.wgsl` reads its parameters from the frame block, and appending a
// forty-six-lane struct to `FrameUniforms` means declaring it in `common.wgsl` -- which every shader
// in the engine includes -- and then teaching `test_renderer_layout_guards.cpp` to flatten
// forty-six names, the way it already has to for the wind and the ground illumination. It also grows
// the block that `ShadowRenderer::upload` copies once per shadow view, for data no shadow view reads.
//
// A separate pipeline with its own bind group costs one pipeline bind and one `Draw(3)` and touches
// none of that. It is in the *same* render pass, immediately after the atmosphere draw, with the
// same depth state and the same additive blend, so on a tile-based GPU the second draw's
// read-modify-write of the HDR and emission targets stays in tile memory and never reaches DRAM --
// which is the whole reason a second draw here is cheap and a second *pass* would not be.
//
// What it also buys, which the design had given up: `--disable cosmic` and `--ab cosmic` can remove
// this draw entirely rather than switching a uniform to zero, so the A/B arm removes the fragment
// work and the uniform content together. That is the difference between an honest arm and one that
// measures the same frame through one more branch (ADR-182).

#include "common.wgsl"

const kCoTau: f32 = 6.28318531;
const kCoPi: f32 = 3.14159265;

// The same `{radiance, bloom}` pair `atmosphere_fx.wgsl` passes around. Declared here rather than
// shared, because this file no longer includes that one and a two-field struct is not worth a
// header dependency between two shaders that otherwise have nothing to say to each other.
struct AtmosResult {
    radiance: vec3<f32>,
    bloom: f32,
};

// ---- the uniform block -------------------------------------------------------------------------
//
// Mirrors `world::CosmicOceanGpu` in src/world/cosmic_ocean.hpp, lane for lane, and bound at
// group 1 binding 0 by `CosmicOceanRenderer`. Group 0 is `SceneRenderer`'s frame layout, so the
// camera, the sky's sun direction and the target size are read from `frame` exactly as every other
// pass reads them -- which is what makes §24 free: this follows whatever camera the renderer was
// given, whether that is the viewport, a director shot, a multicam angle or an offline render.
//
// Still passed *into* `cosmicOceanAt` as an argument rather than read from the global inside it, so
// that the entry point is a pure function of a block a test can construct.

struct CosmicOceanBlock {
    master: vec4<f32>,         // x intensity (envelope folded in), y brightness, z contrast, w saturation
    master2: vec4<f32>,        // x exposure gain, y global scale, z seed, w palette saturation
    centre: vec4<f32>,         // xyz centre (world), w mask amount
    mask: vec4<f32>,           // xyz mask direction, w cos(inner)
    mask2: vec4<f32>,          // x cos(outer), y suppression, z horizon bias, w 0
    colorDeep: vec4<f32>,      // rgb deep-space radiance, w hue-drift phase (turns)
    colorPrimary: vec4<f32>,   // rgb, w palette blend
    colorSecondary: vec4<f32>, // rgb, w medium-evolution phase
    colorAccent: vec4<f32>,    // rgb, w fast-evolution phase
    colorAtmos: vec4<f32>,     // rgb atmospheric tint, w fast-evolution amount

    nebFar0: vec4<f32>,        // x depth, y parallax, z density, w scale
    nebFar1: vec4<f32>,        // x octaves, y turbulence, z warp, w brightness
    nebFar2: vec4<f32>,        // x flow phase, y softness, z contrast, w colour mix
    nebFar3: vec4<f32>,        // x evolve phase, y shimmer, zw 0
    nebMid0: vec4<f32>,
    nebMid1: vec4<f32>,
    nebMid2: vec4<f32>,
    nebMid3: vec4<f32>,
    nebulaTint: vec4<f32>,     // rgb, w 0

    starUltra: vec4<f32>,      // x depth, y parallax, z density, w brightness
    starFar: vec4<f32>,
    starMid: vec4<f32>,
    starNear: vec4<f32>,
    starShape: vec4<f32>,      // x size, y size variance, z colour variation, w drift phase (rad)
    starTwinkle: vec4<f32>,    // x amount, y phase, z glint probability, w strata count
    starTint: vec4<f32>,       // rgb, w 0

    planet0: vec4<f32>,        // x depth, y parallax, z density, w brightness
    planet1: vec4<f32>,        // x scale, y scale variance, z clustering, w colour variation
    planet2: vec4<f32>,        // x atmosphere glow, y terminator, z night side, w cloud bands
    planet3: vec4<f32>,        // x ring probability, y ring size, z ring brightness, w cell count
    planet4: vec4<f32>,        // x rotation phase, y drift phase, z drift dir x, w drift dir z
    planetTint: vec4<f32>,     // rgb, w 0

    dust0: vec4<f32>,          // x depth, y parallax, z density, w brightness
    dust1: vec4<f32>,          // x size, y size variance, z turbulence, w drift phase
    dust2: vec4<f32>,          // x fade distance, y cell count, zw 0

    galaxy0: vec4<f32>,        // x depth, y parallax, z density, w brightness
    galaxy1: vec4<f32>,        // x scale, y spiral, z core brightness, w inclination
    galaxy2: vec4<f32>,        // x rotation phase, yzw 0

    atmos0: vec4<f32>,         // x haze, y density, z brightness falloff, w saturation falloff
    atmos1: vec4<f32>,         // x contrast falloff, y scattering, z glow, w 0

    flow0: vec4<f32>,          // xyz unit flow direction, w strength
    flow1: vec4<f32>,          // x turbulence, y curl, z scale, w evolve phase

    event0: vec4<f32>,         // x shooting-star period (s), y comet, z flare, w pulse
    event1: vec4<f32>,         // x probability, y intensity, z size, w speed
    event2: vec4<f32>,         // x lifetime (s), y transport second, zw 0
    eventColor: vec4<f32>,     // rgb, w 0
};

@group(1) @binding(0) var<uniform> cosmic: CosmicOceanBlock;

// ---- hashing and noise ---------------------------------------------------------------------------

fn coHash1(p: f32) -> f32 { return fract(sin(p * 127.1) * 43758.5453123); }

fn coHash13(p: vec3<f32>) -> f32 {
    return fract(sin(dot(p, vec3<f32>(127.1, 311.7, 74.7))) * 43758.5453123);
}

fn coHash33(p: vec3<f32>) -> vec3<f32> {
    let q = vec3<f32>(dot(p, vec3<f32>(127.1, 311.7, 74.7)),
                      dot(p, vec3<f32>(269.5, 183.3, 246.1)),
                      dot(p, vec3<f32>(113.5, 271.9, 124.6)));
    return fract(sin(q) * 43758.5453123);
}

fn coLuma(c: vec3<f32>) -> f32 { return dot(c, vec3<f32>(0.2126, 0.7152, 0.0722)); }

// Trilinear value noise. **Three-dimensional on purpose**, and this is the decision that removes a
// whole class of artefact from the nebula: a 2D noise indexed by spherical coordinates has a pole
// singularity and a meridian seam, and a 2D noise on cube faces has four visible creases. A 3D
// noise sampled at a point *on* the shell has neither, because the sphere is simply a surface
// inside a field that is continuous everywhere.
fn coNoise3(p: vec3<f32>) -> f32 {
    let i = floor(p);
    let f = p - i;
    let u = f * f * (3.0 - 2.0 * f);
    let a = coHash13(i + vec3<f32>(0.0, 0.0, 0.0));
    let b = coHash13(i + vec3<f32>(1.0, 0.0, 0.0));
    let c = coHash13(i + vec3<f32>(0.0, 1.0, 0.0));
    let d = coHash13(i + vec3<f32>(1.0, 1.0, 0.0));
    let e = coHash13(i + vec3<f32>(0.0, 0.0, 1.0));
    let g = coHash13(i + vec3<f32>(1.0, 0.0, 1.0));
    let h = coHash13(i + vec3<f32>(0.0, 1.0, 1.0));
    let k = coHash13(i + vec3<f32>(1.0, 1.0, 1.0));
    let lo = mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
    let hi = mix(mix(e, g, u.x), mix(h, k, u.x), u.y);
    return mix(lo, hi, u.z);
}

// Fractal sum with a dynamic octave count. The loop bound is the constant 6 and the count breaks
// out of it, which is what keeps the compiler able to unroll it; `octaves` is a quality control
// (§23) and reducing it costs detail and never exposure, because the sum is normalised by the
// amplitude actually accumulated.
//
// `softness` (§12) is the cheap half of depth-of-field: it fades out the top octaves rather than
// blurring the result, so a distant stratum is genuinely soft and genuinely cheaper, instead of
// being blurred by a pass that costs more the further away the thing is.
fn coFbm(p0: vec3<f32>, octaves: f32, gain: f32, softness: f32) -> f32 {
    var p = p0;
    var sum = 0.0;
    var norm = 0.0;
    var amp = 0.5;
    for (var i = 0; i < 6; i = i + 1) {
        let level = f32(i);
        if (level >= octaves) { break; }
        // The last octave fades in over its final unit, so raising the count does not pop.
        let edge = clamp(octaves - level, 0.0, 1.0);
        // Softness removes detail from the top down: at 1.0 only the first octave survives.
        let soft = clamp(1.0 - softness * level, 0.0, 1.0);
        let w = amp * edge * soft;
        sum = sum + w * coNoise3(p);
        norm = norm + w;
        p = p * 2.03 + vec3<f32>(11.3, 7.7, 5.1);
        amp = amp * gain;
    }
    return select(0.5, sum / norm, norm > 1.0e-6);
}

// ---- the shell solve -----------------------------------------------------------------------------

struct CoShell {
    dir: vec3<f32>,  // unit direction from the centre to where the ray met the shell
    hit: bool,
};

// Intersect the ray with a stratum's shell from its parallax-interpolated eye.
//
// The eye is always well inside the shell -- a camera 200 m from the island against a nebula
// 400 km out -- so there is exactly one forward root and the usual two-root bookkeeping is not
// needed. The `max` on the discriminant is the only guard, and it exists for the case somebody
// authors a dust stratum closer than the camera's distance from the centre, which is legal and
// should degrade rather than produce a NaN.
fn coShell(ro: vec3<f32>, rd: vec3<f32>, centre: vec3<f32>, depth: f32, parallax: f32) -> CoShell {
    let eye = centre + (ro - centre) * clamp(parallax, 0.0, 1.0);
    let oc = eye - centre;
    let b = dot(oc, rd);
    let c = dot(oc, oc) - depth * depth;
    let h = b * b - c;
    var out: CoShell;
    out.hit = h > 0.0;
    let t = -b + sqrt(max(h, 0.0));
    out.dir = normalize((eye + rd * max(t, 0.0)) - centre);
    return out;
}

// ---- cells on the sphere -------------------------------------------------------------------------

// A direction, as a cell on one of six cube faces plus the position inside it.
//
// Cube faces rather than spherical coordinates, because `dir.xz / (dir.y + 1)` -- which is what
// `skybox.wgsl` uses for its own stars -- has a singularity at one pole and stretches cells badly
// away from it, and a star field whose density visibly changes with elevation is one of the failure
// modes §0 lists by name.
//
// **One cell, not a neighbourhood.** Each cell holds at most one body, placed at a hashed position
// *inset* from its edges, so no body ever crosses a boundary and no neighbour search is needed.
// That makes a star four hashes instead of thirty-six, and it removes the cube's four creases at
// the same time: a seam can only show where a body spans two cells, and none does.
struct CoCell {
    id: vec3<f32>,    // the cell's integer coordinate, including its face, for hashing
    local: vec2<f32>, // position within the cell, 0..1
    scale: f32,       // how many radians of arc one unit of `local` spans, near enough
};

fn coCell(dir: vec3<f32>, cellsPerFace: f32) -> CoCell {
    let a = abs(dir);
    var uv = vec2<f32>(0.0);
    var face = 0.0;
    var major = 1.0;
    if (a.x >= a.y && a.x >= a.z) {
        major = a.x;
        uv = vec2<f32>(dir.z, dir.y) / major;
        face = select(1.0, 0.0, dir.x > 0.0);
    } else if (a.y >= a.z) {
        major = a.y;
        uv = vec2<f32>(dir.x, dir.z) / major;
        face = select(3.0, 2.0, dir.y > 0.0);
    } else {
        major = a.z;
        uv = vec2<f32>(dir.x, dir.y) / major;
        face = select(5.0, 4.0, dir.z > 0.0);
    }
    let grid = (uv * 0.5 + vec2<f32>(0.5)) * cellsPerFace;
    let cell = floor(grid);
    var out: CoCell;
    out.id = vec3<f32>(cell.x, cell.y + face * 1024.0, face);
    out.local = grid - cell;
    // A cube face spans 90 degrees at its centre and rather less at its corners; one number for the
    // whole face is close enough for sizing a body that is a handful of pixels across.
    out.scale = (kCoPi * 0.5) / max(cellsPerFace, 1.0);
    return out;
}

// How bright a point-like body of angular radius `r` should be when it is smaller than a pixel.
//
// Not a clamp and not a discard: the energy is conserved by spreading it over the pixel, so a field
// of sub-pixel stars resolves to a steady glow whose brightness does not change when the window is
// resized. Point-sampling instead is what makes a star field crawl.
fn coPointFade(r: f32, pixelAngle: f32) -> f32 {
    let ratio = r / max(pixelAngle * 0.5, 1.0e-7);
    return min(ratio * ratio, 1.0);
}

// ---- colour --------------------------------------------------------------------------------------

// The palette, evolving on three independent timescales (§15).
//
// Slow drifts the hue of the whole environment over minutes; medium moves where between the primary
// and secondary colours it sits, over tens of seconds; fast is a small second-scale wobble that
// keeps the picture from ever being exactly still. They are three because the brief asks for three
// and one rate cannot be all of them.
fn coHueShift(c: vec3<f32>, turns: f32) -> vec3<f32> {
    // A rotation in YIQ-ish space: cheaper than an RGB->HSV->RGB round trip and smooth through grey,
    // which matters because the deep-space colour is nearly grey and a hue rotation that snaps
    // there would strobe.
    let angle = turns * kCoTau;
    let s = sin(angle);
    let k = cos(angle);
    let luma = coLuma(c);
    let m = mat3x3<f32>(vec3<f32>(0.299, 0.587, 0.114), vec3<f32>(0.701, -0.587, -0.114),
                        vec3<f32>(-0.300, -0.587, 0.886));
    let i = dot(c, vec3<f32>(0.596, -0.274, -0.322));
    let q = dot(c, vec3<f32>(0.211, -0.523, 0.312));
    let i2 = i * k - q * s;
    let q2 = i * s + q * k;
    return max(vec3<f32>(luma) + vec3<f32>(0.956, -0.272, -1.106) * i2 +
                   vec3<f32>(0.621, -0.647, 1.703) * q2,
               vec3<f32>(0.0));
}

fn coPalette(co: CosmicOceanBlock, t: f32) -> vec3<f32> {
    let medium = 0.5 + 0.5 * sin(co.colorSecondary.w * kCoTau);
    let blend = clamp(co.colorPrimary.w + (medium - 0.5) * 0.35, 0.0, 1.0);
    var c = mix(co.colorPrimary.rgb, co.colorSecondary.rgb, blend);
    c = mix(c, co.colorAccent.rgb, clamp(t, 0.0, 1.0));
    let fast = sin(co.colorAccent.w * kCoTau) * co.colorAtmos.w;
    c = coHueShift(c, co.colorDeep.w + fast * 0.1);
    // Palette saturation, applied here and not in the master grade: this desaturates what the
    // nebulae and the dust are tinted *from*, so "Deep Void" can be muted without also draining the
    // stars, which are on their own tint. The master's saturation still grades the composite.
    let grey = coLuma(c);
    return max(mix(vec3<f32>(grey), c, co.master2.w), vec3<f32>(0.0));
}

// ---- the nebula (§4, §21) --------------------------------------------------------------------------

struct CoLayer {
    radiance: vec3<f32>,
    coverage: f32,
};

fn coNebula(co: CosmicOceanBlock, ro: vec3<f32>, rd: vec3<f32>, l0: vec4<f32>, l1: vec4<f32>,
            l2: vec4<f32>, l3: vec4<f32>, pixelAngle: f32) -> CoLayer {
    var out: CoLayer;
    out.radiance = vec3<f32>(0.0);
    out.coverage = 0.0;
    if (l0.z <= 1.0e-4 || l1.w <= 1.0e-6) { return out; }

    let shell = coShell(ro, rd, co.centre.xyz, l0.x, l0.y);
    let scale = l0.w;
    let flow = l2.x;
    let evolve = l3.x;

    // The flow field carries the whole ocean one way (§16). Applied as a translation of the noise
    // domain along the authored direction, plus a curl that turns straight drift into eddies -- a
    // nebula that only translates reads as a texture sliding, which is §0's failure list again.
    let wind = co.flow0.xyz * (flow * co.flow0.w * 4.0);
    let base = shell.dir * scale + wind;

    // Domain warp: one vector of noise displacing the sampling point of another. This is the single
    // thing that separates "clouds" from "blobs", and it is why §4 says the clouds must not simply
    // be 2D noise.
    let w = coHash33(vec3<f32>(co.master2.z, 3.7, 9.1)) * 8.0;
    let warpAmount = l1.z;
    var warp = vec3<f32>(0.0);
    if (warpAmount > 1.0e-4) {
        warp = vec3<f32>(coFbm(base + w, 2.0, 0.5, l2.y),
                         coFbm(base + w.yzx + vec3<f32>(17.3), 2.0, 0.5, l2.y),
                         coFbm(base + w.zxy + vec3<f32>(31.1), 2.0, 0.5, l2.y)) - vec3<f32>(0.5);
        warp = warp * warpAmount * 1.8;
    }
    // `evolve` moves the field through a fourth dimension rather than translating it, so the nebula
    // changes shape where it stands instead of sliding past. §39's "wait 30-60 seconds and the
    // environment should have evolved subtly" is this term and not the flow above.
    let q = base + warp + vec3<f32>(0.0, 0.0, evolve * 6.0) + co.master2.z * 4.0;

    let field = coFbm(q, l1.x, mix(0.5, 0.62, clamp(l1.y, 0.0, 1.0)), l2.y);

    // Raised to a power so most of the sky stays empty: the haze should be somewhere, not
    // everywhere. The thresholds straddle the field's actual distribution rather than its nominal
    // range -- a normalised fBM of value noise centres near 0.5 with a spread around 0.12, and
    // `smoothstep(0.42, 0.92, ...)` on that selects almost nothing and renders a flat frame, which
    // is a mistake `glowmere-cosmos.wgsl` records making and is worth not making twice.
    let coverage = pow(smoothstep(0.42, 0.68, field), max(l2.z, 0.05)) * l0.z;
    if (coverage <= 1.0e-5) { return out; }

    // Two colours across the field's own gradient, so the cloud has internal structure rather than
    // being one hue at one opacity.
    let mixAmount = clamp((field - 0.42) / 0.34, 0.0, 1.0);
    var tint = mix(co.nebulaTint.rgb, coPalette(co, mixAmount), clamp(l2.w, 0.0, 1.0));

    // §21: a high-frequency emissive sparkle inside the densest parts only. Gated on coverage so it
    // reads as material catching light rather than as noise laid over the frame, and faded by the
    // pixel angle so it cannot alias.
    let shimmer = l3.y;
    if (shimmer > 1.0e-4) {
        let fine = coFbm(q * 9.0 + vec3<f32>(co.starTwinkle.y * 3.0), 2.0, 0.5, 0.0);
        let glint = pow(smoothstep(0.62, 0.86, fine), 3.0) * coverage;
        let aa = coPointFade(shell.dir.y * 0.0 + 0.004 * scale, pixelAngle);
        tint = tint + co.colorAccent.rgb * glint * shimmer * 2.5 * aa;
    }

    out.coverage = coverage;
    out.radiance = tint * coverage * l1.w;
    return out;
}

// ---- stars (§7, §8) --------------------------------------------------------------------------------

// One stratum. Four calls, differing only in their numbers -- which is §7's point: the depth comes
// from the layers disagreeing about distance, density and brightness, not from four techniques.
fn coStars(co: CosmicOceanBlock, ro: vec3<f32>, rd: vec3<f32>, lane: vec4<f32>, cellsPerFace: f32,
           twinkleScale: f32, pixelAngle: f32) -> vec3<f32> {
    if (lane.z <= 1.0e-4 || lane.w <= 1.0e-6) { return vec3<f32>(0.0); }

    // A bulk rotation of the whole stratum about the vertical, so the sky can turn without anything
    // moving relative to anything else in it.
    let spin = co.starShape.w;
    let cs = cos(spin);
    let sn = sin(spin);
    let rotated = vec3<f32>(rd.x * cs - rd.z * sn, rd.y, rd.x * sn + rd.z * cs);
    let shell = coShell(ro, rotated, co.centre.xyz, lane.x, lane.y);
    let cell = coCell(shell.dir, cellsPerFace);

    let h = coHash33(cell.id + co.master2.z * 7.13);
    // Occupancy. `density` is the fraction of cells that hold a star, so the control means the same
    // thing at every cell count and a quality tier that changed the grid would not change the sky.
    if (h.x > lane.z) { return vec3<f32>(0.0); }

    let h2 = coHash33(cell.id * 1.37 + 5.7);
    // Inset so the star is fully inside its own cell and no neighbour search is needed.
    let centre = vec2<f32>(0.25 + 0.5 * h.y, 0.25 + 0.5 * h.z);
    let d = length(cell.local - centre) * cell.scale;

    let sizeJitter = 1.0 + (h2.x - 0.5) * 2.0 * clamp(co.starShape.y, 0.0, 1.0);
    let radius = max(co.starShape.x * sizeJitter * 0.0016, 1.0e-6);

    // §8: every star has its own twinkle phase, hashed from its cell. Synchronous pulsing is the
    // failure the brief names -- "glistening, not blinking LEDs" -- and a per-star phase offset is
    // the whole of the fix.
    var brightness = 1.0;
    let twinkle = co.starTwinkle.x * twinkleScale;
    if (twinkle > 1.0e-4) {
        let phase = (co.starTwinkle.y + h2.y) * kCoTau;
        brightness = brightness * (1.0 + twinkle * 0.5 * sin(phase * (0.6 + h2.z)));
        // A sparse, brief four-point glint on a small fraction of stars: the thing that makes a
        // field read as glistening rather than as dots of varying brightness.
        if (h2.z < co.starTwinkle.z) {
            let burst = pow(max(sin(phase * 0.31), 0.0), 24.0);
            brightness = brightness + burst * 3.0;
        }
    }
    brightness = max(brightness, 0.0);

    // The profile: a core that is one pixel at most, and a small halo that is what actually makes a
    // star look like a light source rather than a dot.
    let core = 1.0 - smoothstep(radius * 0.4, radius, d);
    // The halo has to reach zero BEFORE the cell boundary, or the cell becomes the halo.
    //
    // This was `exp(-d / (radius * (1 + glow * 2)))`, which never reaches zero. A star is evaluated
    // only inside its own cell -- that is the whole point of the one-cell scheme above -- so
    // whatever the exponential still had left at the cell edge was cut off there, by a straight
    // line, in the two axis-aligned directions the cube face's grid runs. The result is a soft
    // SQUARE around every star, four of them meeting at each cell corner. The first render of the
    // Tree of Life with this effect on was a sky of boxes, and it is visible at a glance rather than
    // subtly: see `examples/treeisland/renders/`.
    //
    // A compactly supported profile instead. The star is inset to 0.25..0.75 of its cell, so 0.24
    // of a cell is the largest reach that cannot cross a boundary; the halo is capped there and
    // falls to exactly zero at its own edge, so there is nothing left for the boundary to cut.
    // Capped rather than scaled, so a coarse stratum does not get halos the size of its cells.
    let haloReach = min(radius * (1.0 + co.atmos1.z * 2.0) * 4.0, cell.scale * 0.24);
    let haloT = clamp(1.0 - d / max(haloReach, 1.0e-6), 0.0, 1.0);
    let halo = haloT * haloT * haloT * 0.35;
    let fade = coPointFade(radius, pixelAngle);

    // Colour variation about the star tint: blue-white one way, amber the other, which is the range
    // real stars actually occupy and is enough to stop the field reading as one material.
    let warm = (h2.x - 0.5) * 2.0 * clamp(co.starShape.z, 0.0, 1.0);
    let tint = co.starTint.rgb * (vec3<f32>(1.0) + vec3<f32>(warm, warm * 0.15, -warm) * 0.5);

    return max(tint, vec3<f32>(0.0)) * (core + halo) * fade * brightness * lane.w;
}

// ---- planets (§5, §6) ------------------------------------------------------------------------------

// A distant world, intersected analytically and shaded from a real surface normal.
//
// **Not a billboard.** §0 lists "a collection of obvious billboard sprites" as a failure, and §6
// asks for a dark side, an illuminated side, a terminator, an atmospheric rim, optional rings and
// optional cloud bands -- all of which need a normal. A sphere subtending well under a degree is
// effectively orthographic, so the normal is recovered from the offset within the disc without a
// quadratic: at a normalised offset (x, y) from the centre, the surface normal is
// (x, y, sqrt(1 - x^2 - y^2)) in the frame that faces the camera. That is a ray-sphere intersection
// with the parallax terms dropped because they are below a pixel, and it costs four instructions.
// `sunDir` is the direction *towards* the light, which is what `frame.skySun.xyz` carries -- the
// same convention `skybox.wgsl` reads it under. Getting this backwards lights the night side and
// looks almost right, which is why the convention is named here rather than assumed.
fn coPlanets(co: CosmicOceanBlock, ro: vec3<f32>, rd: vec3<f32>, sunDir: vec3<f32>,
             pixelAngle: f32) -> vec3<f32> {
    let lane = co.planet0;
    if (lane.z <= 1.0e-4 || lane.w <= 1.0e-6) { return vec3<f32>(0.0); }

    let shell = coShell(ro, rd, co.centre.xyz, lane.x, lane.y);
    // Bulk drift across the sky (§17), as a small rotation of the sampling direction.
    let drift = co.planet4.y;
    let axis = normalize(vec3<f32>(co.planet4.z, 0.35, co.planet4.w) + vec3<f32>(1.0e-5));
    let dir = normalize(shell.dir + cross(axis, shell.dir) * drift);

    let cells = max(co.planet3.w, 1.0);
    let cell = coCell(dir, cells * 2.0);
    let h = coHash33(cell.id * 2.17 + co.master2.z * 3.91);

    // §5's clustering: a low-frequency field modulates the per-cell probability, so worlds arrive in
    // groups instead of being evenly sprinkled. At 0 the field is ignored and the distribution is
    // the plain hash.
    let clump = mix(1.0, smoothstep(0.35, 0.75, coFbm(dir * 2.2 + co.master2.z, 2.0, 0.5, 0.0)) * 2.0,
                    clamp(co.planet1.z, 0.0, 1.0));
    if (h.x > lane.z * clump) { return vec3<f32>(0.0); }

    let h2 = coHash33(cell.id * 3.71 + 11.3);
    let h3 = coHash33(cell.id * 5.13 + 23.9);

    let centre2 = vec2<f32>(0.3 + 0.4 * h.y, 0.3 + 0.4 * h.z);
    let offset = (cell.local - centre2) * cell.scale;
    let sizeJitter = 1.0 + (h2.x - 0.5) * 2.0 * clamp(co.planet1.y, 0.0, 1.0);
    let radius = max(co.planet1.x * sizeJitter * 0.010, 1.0e-6);

    let r2 = dot(offset, offset) / (radius * radius);
    let fade = coPointFade(radius, pixelAngle);

    // The frame the planet is seen in: the disc's two tangent axes plus the direction to it.
    let toPlanet = dir;
    let right = normalize(cross(vec3<f32>(0.0, 1.0, 0.0), toPlanet) + vec3<f32>(1.0e-5, 0.0, 0.0));
    let up = cross(toPlanet, right);

    var out = vec3<f32>(0.0);
    let tint = co.planetTint.rgb *
               (vec3<f32>(1.0) + (h3 - vec3<f32>(0.5)) * 2.0 * clamp(co.planet1.w, 0.0, 1.0));

    // ---- the ring (§5, §6), drawn before the planet so the planet occludes its near half ----
    let hasRing = h2.y < clamp(co.planet3.x, 0.0, 1.0);
    if (hasRing && co.planet3.z > 1.0e-4) {
        // The ring plane's normal, tilted per planet. Solved in the planet's local frame under the
        // same orthographic approximation the surface uses: the view ray is -toPlanet, so a point
        // at tangent offset (x, y) lies on the ring plane at depth z = -(x*n.x + y*n.y)/n.z.
        let tilt = normalize(vec3<f32>((h3.x - 0.5) * 1.4, 1.0, (h3.y - 0.5) * 1.4));
        let n = normalize(right * tilt.x + up * tilt.y + toPlanet * tilt.z * 0.25);
        let nx = dot(n, right);
        let ny = dot(n, up);
        let nz = dot(n, toPlanet);
        if (abs(nz) > 0.05) {
            let x = offset.x / radius;
            let y = offset.y / radius;
            let z = -(x * nx + y * ny) / nz;
            let ringR = sqrt(x * x + y * y + z * z);
            let inner = 1.25;
            let outer = max(co.planet3.y, inner + 0.05);
            if (ringR > inner && ringR < outer) {
                // Banding, so it reads as a ring system rather than a washer.
                let band = 0.55 + 0.45 * sin(ringR * 28.0 + h3.z * 30.0);
                // Occluded where the ring passes behind the planet.
                let behind = select(1.0, 0.0, z < 0.0 && (x * x + y * y) < 1.0);
                // ...and shadowed where the planet is between it and the light.
                let p3 = right * x + up * y + toPlanet * z;
                let along = dot(p3, sunDir);
                let perp = length(p3 - sunDir * along);
                let shadow = select(1.0, 0.25, along < 0.0 && perp < 1.0);
                let edge = smoothstep(inner, inner + 0.08, ringR) *
                           (1.0 - smoothstep(outer - 0.12, outer, ringR));
                out = out + tint * co.planet3.z * band * behind * shadow * edge * 0.35 * fade;
            }
        }
    }

    if (r2 < 1.0) {
        // ---- the body ----
        let x = offset.x / radius;
        let y = offset.y / radius;
        let z = sqrt(max(1.0 - r2, 0.0));
        let normal = normalize(right * x + up * y + toPlanet * (-z));

        // §6's terminator: a soft, controllable day/night boundary, not a hard cut and not a
        // uniformly lit ball. `terminator` is the width of the transition as a fraction of the
        // radius, so an artist adjusts the softness of the line rather than a lighting model.
        let ndl = dot(normal, sunDir);
        let day = smoothstep(-co.planet2.y, co.planet2.y, ndl);

        // Cloud bands in the planet's own rotating frame: latitude stripes, warped so they are not
        // perfectly straight. Rotating with `planet4.x` means the surface turns under the
        // terminator, which is what makes a planet look like a world rather than a decal.
        var surface = vec3<f32>(1.0);
        if (co.planet2.w > 1.0e-3 && h2.z < co.planet2.w) {
            let lat = normal.y * 3.0;
            let lon = atan2(normal.z, normal.x) + co.planet4.x;
            let bands = sin(lat * 6.0 + coNoise3(vec3<f32>(lon, lat, h3.z * 10.0) * 2.0) * 2.2);
            surface = mix(vec3<f32>(0.82, 0.86, 0.95), vec3<f32>(1.12, 1.04, 0.88),
                          0.5 + 0.5 * bands);
        }

        // The limb: brighter towards the edge of the lit side, which is what an atmosphere does and
        // is most of why a shaded sphere stops looking like a shaded sphere.
        let rim = pow(1.0 - z, 3.0) * co.planet2.x * max(ndl, 0.0);
        let lit = day * (0.25 + 0.75 * max(ndl, 0.0));
        out = out + tint * surface * (lit + rim + co.planet2.z) * lane.w * fade;
    } else {
        // ---- the atmospheric halo, just outside the limb ----
        let d = sqrt(r2);
        // The same cut the star halo had, and it showed as a square around every planet in the
        // same render. `exp(-(d - 1) * 6)` is still finite at the cell edge, and this branch runs
        // over the whole of the cell outside the disc, so the edge is where it stopped. Tapered to
        // zero inside the cell: the planet's centre is inset to 0.3..0.7, so 0.28 of a cell is the
        // reach that cannot cross a boundary, expressed here in planet radii.
        let glowLimit = max(cell.scale * 0.28 / radius, 1.05);
        let glowWindow = clamp(1.0 - (d - 1.0) / max(glowLimit - 1.0, 1.0e-3), 0.0, 1.0);
        let glow = exp(-(d - 1.0) * 6.0) * co.planet2.x * 0.5 * glowWindow * glowWindow;
        // Brightest when the planet is between the camera and the light: that is forward
        // scattering through its atmosphere, and it is what gives a back-lit world a bright thin
        // rim instead of a uniform ring. `scattering` chooses how directional it is.
        let forward = max(dot(toPlanet, sunDir), 0.0);
        let bias = mix(1.0, pow(forward, 3.0) * 3.0, clamp(co.atmos1.y, 0.0, 1.0));
        out = out + tint * glow * bias * lane.w * fade;
    }
    return out;
}

// ---- galaxies (§19) --------------------------------------------------------------------------------

fn coGalaxies(co: CosmicOceanBlock, ro: vec3<f32>, rd: vec3<f32>, pixelAngle: f32) -> vec3<f32> {
    let lane = co.galaxy0;
    if (lane.z <= 1.0e-4 || lane.w <= 1.0e-6) { return vec3<f32>(0.0); }
    let shell = coShell(ro, rd, co.centre.xyz, lane.x, lane.y);
    let cell = coCell(shell.dir, 3.0);
    let h = coHash33(cell.id * 7.31 + co.master2.z * 1.77);
    if (h.x > lane.z) { return vec3<f32>(0.0); }

    let h2 = coHash33(cell.id * 9.11 + 4.4);
    let centre2 = vec2<f32>(0.3 + 0.4 * h.y, 0.3 + 0.4 * h.z);
    var p = (cell.local - centre2) / max(co.galaxy1.x * 0.16, 1.0e-4);

    // Inclination: squash one axis and rotate, so the population is not all face-on discs. §19's
    // "do not make them look like dozens of obvious photographs pasted into the sky" is mostly this
    // plus keeping the brightness low.
    let incl = mix(1.0, 0.18, clamp(co.galaxy1.w * h2.x, 0.0, 1.0));
    let ang = h2.y * kCoTau;
    let cs = cos(ang);
    let sn = sin(ang);
    p = vec2<f32>(p.x * cs - p.y * sn, (p.x * sn + p.y * cs) / max(incl, 0.05));

    let r = length(p);
    if (r > 1.6) { return vec3<f32>(0.0); }
    // ...and taper into that cut rather than stepping off it. Same family as the two above: the
    // profile is still ~0.004 of its peak at r = 1.6, which is a visible edge once the master
    // brightness and the bloom have had it.
    let rim = smoothstep(1.6, 1.15, r);

    // A logarithmic spiral in the disc: the arm term is a function of angle minus log-radius, which
    // is what makes arms wind rather than radiate.
    let theta = atan2(p.y, p.x) + co.galaxy2.x + log(max(r, 0.02)) * 3.4;
    let arms = 0.5 + 0.5 * cos(theta * 2.0);
    let spiral = mix(1.0, arms, clamp(co.galaxy1.y, 0.0, 1.0));

    let disc = exp(-r * r * 2.2) * spiral;
    let core = exp(-r * r * 26.0) * co.galaxy1.z;
    let dust = 1.0 - 0.35 * smoothstep(0.2, 0.9, coNoise3(vec3<f32>(p * 6.0, h2.z * 8.0)));

    let tint = mix(co.nebulaTint.rgb, vec3<f32>(1.0, 0.94, 0.82), 0.45);
    let aa = coPointFade(co.galaxy1.x * 0.02, pixelAngle);
    return tint * (disc * dust + core) * lane.w * aa * rim * 0.25;
}

// ---- cosmic dust (§9) ------------------------------------------------------------------------------

// The near field, and the reason the camera feels inside the environment rather than in front of
// it. Sampled from a **three-dimensional lattice about the camera** rather than from a shell,
// because at this distance a shell reads as a dome, and because motes have to pass the camera.
fn coDust(co: CosmicOceanBlock, ro: vec3<f32>, rd: vec3<f32>, pixelAngle: f32) -> vec3<f32> {
    let lane = co.dust0;
    let cells = co.dust2.y;
    if (lane.z <= 1.0e-4 || lane.w <= 1.0e-6 || cells < 1.0) { return vec3<f32>(0.0); }

    let eye = co.centre.xyz + (ro - co.centre.xyz) * clamp(lane.y, 0.0, 1.0);
    let reach = lane.x;
    let cellSize = reach / max(cells, 1.0);

    var acc = vec3<f32>(0.0);
    let half = i32(floor(cells * 0.5));
    // A neighbourhood here and not for the stars, because a mote is *near*: it subtends a real
    // angle, it can be anywhere in its cell, and the cells adjacent to the ray's own are the ones
    // whose motes are in frame. The count is the quality control, and 0 turns dust off through the
    // same field an artist can reach rather than by hiding the control (§23).
    for (var i = -2; i <= 2; i = i + 1) {
        if (i < -half || i > half) { continue; }
        for (var j = -2; j <= 2; j = j + 1) {
            if (j < -half || j > half) { continue; }
            for (var k = 1; k <= 3; k = k + 1) {
                // Only forward cells: a mote behind the camera cannot be on this ray.
                let centreCell = floor((eye + rd * (f32(k) * cellSize)) / cellSize);
                let id = centreCell + vec3<f32>(f32(i), f32(j), 0.0);
                let h = coHash33(id * 1.91 + co.master2.z * 5.53);
                if (h.x > lane.z) { continue; }

                let h2 = coHash33(id * 4.07 + 13.1);
                // Drift, with a curl so motes wander rather than sliding in formation.
                let drift = co.dust1.w;
                let curl = vec3<f32>(sin(h2.x * kCoTau + drift * kCoTau),
                                     sin(h2.y * kCoTau + drift * kCoTau * 0.7),
                                     sin(h2.z * kCoTau + drift * kCoTau * 1.3)) *
                           co.dust1.z * 0.3;
                let pos = (id + h + curl) * cellSize;

                let toMote = pos - eye;
                let dist = length(toMote);
                if (dist < cellSize * 0.25 || dist > reach) { continue; }
                let along = dot(toMote, rd);
                if (along <= 0.0) { continue; }
                let perp = length(toMote - rd * along);

                let sizeJitter = 1.0 + (h2.x - 0.5) * 2.0 * clamp(co.dust1.y, 0.0, 1.0);
                let radius = max(co.dust1.x * sizeJitter * 0.5, 1.0e-4);
                let angular = radius / dist;
                let profile = exp(-(perp / max(radius * 2.2, 1.0e-4)) *
                                  (perp / max(radius * 2.2, 1.0e-4)));
                // Fade in and out at the far end so motes do not pop at the lattice's edge.
                let fadeBand = max(co.dust2.x, 1.0e-3) * reach;
                let near = smoothstep(0.0, fadeBand * 0.35, dist);
                let far = 1.0 - smoothstep(reach - fadeBand, reach, dist);
                acc = acc + vec3<f32>(profile * near * far * coPointFade(angular, pixelAngle));
            }
        }
    }
    let tint = mix(co.starTint.rgb, coPalette(co, 0.6), 0.5);
    return tint * acc * lane.w;
}

// ---- occasional events (§18) -------------------------------------------------------------------------

// Sparse, stateless, deterministic.
//
// Event `i` of a kind is `floor(t / period)`, and everything about it -- whether it fires at all,
// where it comes from, where it goes, how big and how bright -- is a hash of `i`. There is no
// accumulator and no random state, so a seek cannot desynchronise it, an offline render of second N
// is a realtime frame at second N, and two outputs showing the same timeline show the same meteor.
// An events system with state would have to be rewound, and nothing in this engine rewinds.
fn coEvents(co: CosmicOceanBlock, rd: vec3<f32>, pixelAngle: f32) -> vec3<f32> {
    let t = co.event2.y;
    let life = max(co.event2.x, 0.01);
    var out = vec3<f32>(0.0);

    // Shooting stars: a short bright streak on a great circle.
    let period = co.event0.x;
    if (period < 1.0e8) {
        for (var n = 0; n < 2; n = n + 1) {
            let slot = floor(t / period) - f32(n);
            let h = coHash33(vec3<f32>(slot, 17.3, co.master2.z));
            if (h.x > co.event1.x) { continue; }
            let age = t - slot * period;
            if (age < 0.0 || age > life) { continue; }
            let h2 = coHash33(vec3<f32>(slot, 91.7, co.master2.z + 3.0));

            // A start direction and a perpendicular to travel along: a great-circle arc, the same
            // construction `atmosphere_fx.wgsl` uses for a comet and for the same reason -- a chord
            // through the sphere's interior would swing the apparent elevation outside its ends.
            let origin = normalize(h * 2.0 - vec3<f32>(1.0) + vec3<f32>(1.0e-4));
            let side = normalize(cross(origin, normalize(h2 * 2.0 - vec3<f32>(1.0) + vec3<f32>(1.0e-3))));
            let u = age / life;
            let sweep = (0.25 + 0.5 * h2.x) * co.event1.w;
            let head = normalize(origin * cos(u * sweep) + side * sin(u * sweep));

            // The trail: distance from the ray to the arc behind the head, faded along its length.
            let d = acos(clamp(dot(rd, head), -1.0, 1.0));
            let width = max(0.0016 * co.event1.z, pixelAngle * 0.5);
            let along = dot(rd, side) - dot(head, side);
            let trail = exp(-max(-along, 0.0) * 40.0 / max(co.event1.z, 0.05));
            let core = exp(-(d / width) * (d / width));
            // In and out, so a meteor arrives and leaves rather than appearing at full brightness.
            let env = sin(clamp(u, 0.0, 1.0) * kCoPi);
            out = out + co.eventColor.rgb * (core * (0.35 + trail)) * env * co.event1.y * 2.0;
        }
    }

    // Distant flares: a brief, soft, stationary brightening somewhere in the field. The quiet half
    // of §18 -- "something exists out there", not "the sky is performing tricks".
    let flarePeriod = co.event0.z;
    if (flarePeriod < 1.0e8) {
        let slot = floor(t / flarePeriod);
        let h = coHash33(vec3<f32>(slot, 41.1, co.master2.z + 7.0));
        if (h.x <= co.event1.x) {
            let age = t - slot * flarePeriod;
            if (age >= 0.0 && age < life * 2.0) {
                let dir = normalize(h * 2.0 - vec3<f32>(1.0) + vec3<f32>(1.0e-4));
                let d = acos(clamp(dot(rd, dir), -1.0, 1.0));
                let env = sin(clamp(age / (life * 2.0), 0.0, 1.0) * kCoPi);
                let width = 0.02 * co.event1.z;
                out = out + co.eventColor.rgb * exp(-(d / width) * (d / width)) * env *
                                co.event1.y * 0.8;
            }
        }
    }
    return out;
}

// ---- atmospheric perspective (§13) -------------------------------------------------------------------

// Push what has accumulated so far further away: lose brightness, lose saturation towards the
// atmospheric tint, lose contrast towards the mid grey. Three separate controls because §13 asks
// for them separately and because they do genuinely different things -- dimming alone makes a
// distant thing dark, desaturating alone makes it grey, and only flattening its contrast makes it
// read as *far* rather than as underexposed.
//
// `amount` is how much environment is in front of this stratum, 0..1. Called once between layers
// rather than per layer, which is the same result for one call instead of nine.
fn coRecede(colour: vec3<f32>, tint: vec3<f32>, amount: f32, fade: f32, desat: f32,
            flatten: f32) -> vec3<f32> {
    let a = clamp(amount, 0.0, 1.0);
    var c = colour * (1.0 - fade * a);
    let grey = coLuma(c);
    c = mix(c, mix(vec3<f32>(grey), tint * grey * 2.0, 0.5), desat * a);
    // Contrast towards the layer's own mean: a distant structure keeps its shape and loses its
    // edges, which is what atmosphere actually does to one.
    c = mix(c, vec3<f32>(grey), flatten * a * 0.5);
    return max(c, vec3<f32>(0.0));
}

// ---- the entry point -------------------------------------------------------------------------------

// Everything the Cosmic Ocean adds, for one view ray.
//
// The order below is the composition order §32 asks for: the quiet background first, the midground
// over it, the sparse foreground last, and the atmosphere unifying all three. It is also back to
// front in depth, which is what lets each stratum be attenuated by the haze in front of it.
fn cosmicOceanAt(co: CosmicOceanBlock, ro: vec3<f32>, rd: vec3<f32>, sunDir: vec3<f32>,
                 pixelAngle: f32) -> AtmosResult {
    var out: AtmosResult;
    out.radiance = vec3<f32>(0.0);
    out.bloom = 0.0;
    if (co.master.x <= 1.0e-5) { return out; }

    // ---- layer 0: deep space ----
    // Not black. A gradient that lifts very slightly along the flow direction, so the void reads as
    // a volume rather than as a hole, and so the frame's corners are not identical to each other.
    let gradient = 0.82 + 0.35 * (dot(rd, co.flow0.xyz) * 0.5 + 0.5);
    var colour = co.colorDeep.rgb * gradient;

    // ---- depth attenuation (§13) ----
    //
    // Each stratum is dimmed, desaturated and flattened by however much of the environment sits in
    // front of it. Expressed as one factor per stratum, not as an integral: the strata are discrete
    // and the haze between them is an artistic control rather than a medium, so integrating it
    // would be inventing physics to justify a number an artist is going to set by eye anyway.
    //
    // It is applied as each layer is composited below, by receding what is already accumulated
    // before the next layer is added -- which is the same thing as attenuating each layer by what
    // is in front of it, and costs one call instead of nine.
    let hazeDensity = co.atmos0.y;

    // ---- layer 1: galaxies ----
    var far = coGalaxies(co, ro, rd, pixelAngle);

    // ---- layer 2 and 3: the two nebulae ----
    let nebFar = coNebula(co, ro, rd, co.nebFar0, co.nebFar1, co.nebFar2, co.nebFar3, pixelAngle);
    far = far * (1.0 - nebFar.coverage * 0.6) + nebFar.radiance;

    // ---- layers 4-7: the star strata, far to near, occluded by the nebulae in front of them ----
    let strata = co.starTwinkle.w;
    var stars = vec3<f32>(0.0);
    stars = stars + coStars(co, ro, rd, co.starUltra, 190.0, 0.0, pixelAngle);
    if (strata >= 2.0) { stars = stars + coStars(co, ro, rd, co.starFar, 120.0, 0.45, pixelAngle); }
    if (strata >= 3.0) { stars = stars + coStars(co, ro, rd, co.starMid, 64.0, 0.8, pixelAngle); }
    if (strata >= 4.0) { stars = stars + coStars(co, ro, rd, co.starNear, 34.0, 1.0, pixelAngle); }
    far = far + stars * (1.0 - nebFar.coverage * 0.75);

    // Everything above is the background. Recede it by the full depth of the environment before the
    // midground is laid over it: this is the single term that makes the far strata feel far rather
    // than merely dim, and it is the one §39 is describing by "the atmosphere should unify the
    // layers".
    far = coRecede(far, co.colorAtmos.rgb, hazeDensity, clamp(co.atmos0.z, 0.0, 0.95),
                   clamp(co.atmos0.w, 0.0, 1.0), clamp(co.atmos1.x, 0.0, 1.0));

    let nebMid = coNebula(co, ro, rd, co.nebMid0, co.nebMid1, co.nebMid2, co.nebMid3, pixelAngle);
    var mid = far * (1.0 - nebMid.coverage * 0.7) + nebMid.radiance;

    // ---- layer 8: planets ----
    // Receded by half the environment, because they sit in the middle of it. The planets themselves
    // are added *after*, so a near world keeps its contrast while the nebula behind it loses its.
    mid = coRecede(mid, co.colorAtmos.rgb, hazeDensity * 0.45, clamp(co.atmos0.z, 0.0, 0.95),
                   clamp(co.atmos0.w, 0.0, 1.0), clamp(co.atmos1.x, 0.0, 1.0));
    mid = mid + coPlanets(co, ro, rd, sunDir, pixelAngle);

    // ---- the haze that unifies them (§13, §20) ----
    // A broad luminous fog filling the volume, brightest towards the flow direction and towards
    // whatever is bright behind it. This is the term §39 means by "the atmosphere should unify the
    // layers": without it the strata read as separate pictures stacked up.
    let hazeShape = pow(clamp(dot(rd, co.flow0.xyz) * 0.5 + 0.5, 0.0, 1.0),
                        mix(1.0, 4.0, clamp(co.atmos1.y, 0.0, 1.0)));
    let haze = co.colorAtmos.rgb * co.atmos0.x * (0.35 + 0.65 * hazeShape) * hazeDensity;
    mid = mid + haze;

    // ---- layer 9: cosmic dust, in front of everything ----
    colour = colour + mid + coDust(co, ro, rd, pixelAngle);

    // ---- events (§18) ----
    colour = colour + coEvents(co, rd, pixelAngle);

    // ---- composition mask (§33) ----
    // A world-direction cone, not a screen-space circle: it stays behind the subject when the
    // camera moves, which is exactly what `glowmere-cosmos.wgsl`'s `haloCenter` could not do.
    if (co.centre.w > 1.0e-4) {
        let c = dot(rd, co.mask.xyz);
        // Inside the inner angle the mask is at full strength; outside the outer angle it is gone.
        let inside = smoothstep(co.mask2.x, co.mask.w, c);
        let bias = 1.0 + co.mask2.z * rd.y;
        let suppress = inside * co.mask2.y * co.centre.w * clamp(bias, 0.0, 2.0);
        colour = colour * (1.0 - clamp(suppress, 0.0, 1.0));
    }

    // ---- grade (§28's master) ----
    colour = colour * co.master.y * co.master2.x;
    let grey = coLuma(colour);
    colour = mix(vec3<f32>(grey), colour, co.master.w);
    colour = pow(max(colour, vec3<f32>(0.0)), vec3<f32>(max(co.master.z, 0.05)));
    colour = colour * co.master.x;

    out.radiance = max(colour, vec3<f32>(0.0));
    // §30's "restrained bloom": only what is genuinely a light source reaches the emission target,
    // so the nebula does not glow and the stars and planets do. The threshold is Glowmere's, 1.0 in
    // exposed units, and the curve above it is gentle so a bright sky does not blow out the tree.
    out.bloom = clamp((coLuma(out.radiance) - 0.6) * 0.5, 0.0, 1.0);
    return out;
}


// ---- the pass ----------------------------------------------------------------------------------
//
// One fullscreen triangle at clip z = 1, exactly as `skybox.wgsl` and `atmosphere.wgsl` do, drawn
// immediately after the atmosphere layer inside the scene pass. Depth-tested so the island and the
// tree occlude it, never depth-written so the water and the particles after it still composite.
//
// Additive into target 0 (HDR) and target 3 (emission); the pipeline masks writes to the normal,
// velocity and identifier targets off, for the reason `water_renderer.cpp` gives -- a normal or a
// velocity averaged over a transparency is worse than none at all.

struct CosmicOut {
    @builtin(position) clip: vec4<f32>,
    @location(0) ndc: vec2<f32>,
};

@vertex
fn vs_cosmic(@builtin(vertex_index) index: u32) -> CosmicOut {
    var positions = array<vec2<f32>, 3>(vec2<f32>(-1.0, -1.0), vec2<f32>(3.0, -1.0), vec2<f32>(-1.0, 3.0));
    var out: CosmicOut;
    let p = positions[index];
    out.clip = vec4<f32>(p, 1.0, 1.0);
    out.ndc = p;
    return out;
}

@fragment
fn fs_cosmic(in: CosmicOut) -> SceneOut {
    // The same unprojection the skybox and the atmosphere layer use. The two unprojected points
    // differ by the camera position, so their difference depends on orientation alone -- but what
    // is done with it depends on the camera's *position* too, because every stratum below is a
    // finite distance away and is supposed to move against the others when the camera travels.
    let near = frame.invViewProj * vec4<f32>(in.ndc, 0.0, 1.0);
    let far = frame.invViewProj * vec4<f32>(in.ndc, 1.0, 1.0);
    let dir = normalize(far.xyz / far.w - near.xyz / near.w);

    // Taken here, at the top of a fragment shader and in uniform control flow: derivatives are
    // illegal in a vertex shader and undefined inside the cell loops below. It is the exact,
    // resolution- and field-of-view-aware angular size of one pixel, and every point-like body in
    // this file is faded by it rather than point-sampled.
    let pixelAngle = max(length(dpdx(dir)) + length(dpdy(dir)), 1.0e-7);

    // Where the *sky* says its sun or moon is (ADR-345), not where the first light in the scene
    // happens to point. The planets are lit by the same thing the island is, which is most of why
    // they sit in the scene rather than on top of it.
    let sunDir = normalize(frame.skySun.xyz + vec3<f32>(0.0, 1.0e-5, 0.0));

    let result = cosmicOceanAt(cosmic, frame.cameraPos.xyz, dir, sunDir, pixelAngle);

    var out: SceneOut;
    out.color = vec4<f32>(result.radiance, 1.0);
    out.normalRoughness = vec4<f32>(0.0);
    out.velocity = vec2<f32>(0.0);
    out.emission = vec4<f32>(result.radiance, result.bloom);
    out.ids = 0u;
    return out;
}
