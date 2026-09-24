// The height fog layer: ONE definition of the profile and ONE of its antiderivative (ADR-567).
//
// **The claim this file exists to make checkable.** ADR-058 says the volumetric march and the
// surface fog describe *the same air*: "a scene whose fog bank has one height in the march and
// another on the surfaces inside it does not read as one atmosphere." The march multiplies its
// density by a height term; the surface pass replaces its geometric distance with the distance
// *through* that same layer, using a closed-form antiderivative. Two expressions, in two files,
// that have to be a function and its integral.
//
// Nothing checked that. The surface path's sign was tested and its degenerate case was tested, and
// **that the antiderivative is the antiderivative was not** -- which is the same shape of hole
// ADR-566 found in the march's ray bound: a claim with no test, correct today for a reason that
// could stop being true in one edit. `tests/rendering/test_height_fog_gpu.cpp` closes it by
// integrating the profile numerically and comparing.
//
// **No bindings, on purpose.** Everything here takes its parameters as arguments, so a test can
// compile this file alone with no uniform buffers to assemble -- which is what makes the check
// cheap enough to exist.
//
// **Include it AT MOST ONCE PER MODULE.** The include directive does not de-duplicate (ADR-360),
// and WGSL is compiled at *load*, so two copies of these functions in one module is a defect that
// is invisible until something renders a frame. Today: `common.wgsl` includes it, so every module
// that includes `common.wgsl` -- the surface passes and `volume.wgsl` -- already has it and must
// not include it again; `particles.wgsl` includes neither, so it includes this file directly.
//
// Its three readers, which is the number that made this file worth extracting: the march
// (`volume.wgsl`), the surface fog's analytic integral (`common.wgsl`) and the particle
// transmittance estimate (`particles.wgsl`). All three used to write the profile out themselves.

// ADR-568, the brief's §7: `upper` and `curve`, and the constraint they were designed under.
//
// **Every term in this profile must have a closed-form antiderivative**, because the surface pass
// integrates it (ADR-567). That rules out the obvious spelling of a curve control,
// `exp(-(b*d)^k)`, which has no elementary integral -- the two passes would have silently parted
// company at exactly the setting an artist reached for. So the curve is a LINEAR BLEND between two
// families that each integrate, and because the blend is linear its integral is the blend of the
// integrals. The same shape of answer `domeShape` gives the fog bank's own lid (ADR-563).
//
//   upper  the fraction of the layer's density still there at ANY height: 0 is clear air above the
//          mist (what this always did), 1 is a uniform atmosphere with no layer at all. §7's
//          "Upper Density" -- and §7's "Ground Density" is `volumeDensity` itself, which is
//          already the layer's full density, so it is not a second control.
//   curve  0 = the exponential, which has a long soft tail and never quite ends: haze.
//          1 = a compact quadratic that reaches EXACTLY zero at 2/b: a layer with a definite top.
//          The two agree in value and in slope at d = 0, so the blend is smooth for every curve.
//
// At `upper` 0 and `curve` 0 both functions reduce to what they were, and exactly -- `mix(x, y, 0)`
// is `x*1 + y*0` and `0 + 1*shape` is `shape`, both exact in IEEE for finite inputs. Bit-identity
// is the property, not an approximation of it, and `test_height_fog_gpu.cpp` asserts it.

// Density at height `y` ABOVE the layer's top, as a fraction of the layer's full density.
// Below the top (y <= 0) the layer is uniform, which is what "flat-topped" means -- and it is
// uniform at 1 whatever `upper` and `curve` are, because both shapes are 1 at d = 0.
fn fogHeightProfile(y: f32, b: f32, upper: f32, curve: f32) -> f32 {
    let d = max(0.0, y);
    let falling = exp(-b * d);
    // Reaches zero at d = 2/b, with slope -b at d = 0 -- the same slope the exponential leaves
    // with, so no curve setting puts a corner at the layer's top.
    let capped = max(0.0, 1.0 - b * d * 0.5);
    let shape = mix(falling, capped * capped, curve);
    return upper + (1.0 - upper) * shape;
}

// How much air sits below height `y`, measured relative to the layer's top and in metres of the
// layer's full density: the antiderivative of `fogHeightProfile`, zeroed at y = 0. It is linear
// inside the layer and saturates above it, and it is C1 across the join, so a ray crossing the
// layer's surface has no seam where the two halves meet.
fn fogHeightIntegral(y: f32, b: f32, upper: f32, curve: f32) -> f32 {
    if (y <= 0.0) {
        return y;
    }
    let expPart = (1.0 - exp(-b * y)) / b;
    // The quadratic's integral, case-split at the height where it reaches zero: past that the
    // layer contributes nothing more, so the integral saturates at 2/(3b).
    let u = min(b * y * 0.5, 1.0);
    let g = 1.0 - u;
    let quadPart = (2.0 / (3.0 * b)) * (1.0 - g * g * g);
    return upper * y + (1.0 - upper) * mix(expPart, quadPart, curve);
}

// ---- ADR-715: the layer follows the ground (ADR-575 §18, "fog sits in valleys") ---------------
//
// `fogGroundFollow` moves the layer's REFERENCE, not its shape: the top of the layer is
// `top + follow * ground(x, z)`, where `ground` is the terrain's baked height. 0 is the flat plane
// (and every reader keeps its old expression on that branch, so 0 is bit-identical rather than
// equal to within rounding); 1 measures `fogHeight` from the ground under the sample.
//
// Still no bindings: the height texture and its two lanes of placement come in as ARGUMENTS, so a
// test can compile this file with a texture of its own and ask the same functions the frame runs.

// The terrain's height at world XZ, in world metres. The CPU twin is `world::TerrainGround::groundAt`.
//
//   map0  (world origin x, world origin z, 1 / spacing x, 1 / spacing z) of the vertex-aligned grid
//   map1  (height scale, height offset, fade metres, 1 when there is a terrain else 0)
//
// A manual bilinear of four texel loads: R32F is not filterable without an optional device
// feature, and four loads are the same arithmetic on every GPU. Outside the footprint the EDGE's
// height fades to 0 across `fade` metres, so the layer eases back to the flat plane instead of
// stepping at the world's edge -- and past that it IS the flat plane.
fn terrainGroundAt(tex: texture_2d<f32>, map0: vec4<f32>, map1: vec4<f32>, xz: vec2<f32>) -> f32 {
    if (map1.w < 0.5) {
        return 0.0;
    }
    let dims = textureDimensions(tex);
    let last = vec2<f32>(f32(dims.x - 1u), f32(dims.y - 1u));
    let g = (xz - map0.xy) * map0.zw;
    let c = clamp(g, vec2<f32>(0.0), last);
    let i0 = min(floor(c), last - vec2<f32>(1.0));
    let f = c - i0;
    let ij = vec2<i32>(i0);
    let h00 = textureLoad(tex, ij, 0).r;
    let h10 = textureLoad(tex, ij + vec2<i32>(1, 0), 0).r;
    let h01 = textureLoad(tex, ij + vec2<i32>(0, 1), 0).r;
    let h11 = textureLoad(tex, ij + vec2<i32>(1, 1), 0).r;
    let h = mix(mix(h00, h10, f.x), mix(h01, h11, f.x), f.y);
    let outside = length((g - c) / map0.zw);
    let keep = 1.0 - clamp(outside / map1.z, 0.0, 1.0);
    return (h * map1.x + map1.y) * keep;
}

// A point's altitude relative to the layer's top once the top follows the ground. Only ever called
// with `follow > 0`; the flat branch stays each reader's own `y - top`.
fn fogLayerAltitude(y: f32, top: f32, follow: f32, ground: f32) -> f32 {
    return y - (top + follow * ground);
}

// THE MARCH's height term with the layer on the ground (and the particle estimate's): the profile
// at this sample's altitude above the followed top. Exactly per sample -- the march already pays
// for a density evaluation here, and four texel loads are small beside its noise.
fn fogGroundProfileAt(tex: texture_2d<f32>, map0: vec4<f32>, map1: vec4<f32>, p: vec3<f32>, top: f32,
                      follow: f32, b: f32, upper: f32, curve: f32) -> f32 {
    let ground = terrainGroundAt(tex, map0, map1, p.xz);
    return fogHeightProfile(fogLayerAltitude(p.y, top, follow, ground), b, upper, curve);
}

// How many pieces the surface pass cuts a ray into (ADR-715). See `fogGroundMean`. Measured on a
// V-shaped valley with rays swept across its fold (`test_height_fog_gpu.cpp`), the worst gap
// between the surface pass and the march is 9.1% of the air at 4 pieces, 2.6% at 8 and 0.5% at
// 16. 8 costs 9 ground reads of 4 texel loads each, and only on the follow branch.
const kFogGroundSegments: i32 = 8;

// THE SURFACE PASS's mean density along the segment `a -> e` with the layer on the ground: the
// quantity `applyFog` multiplies the ray's length by. The march's integrand is
// `profile(y - top - follow * ground(x, z))`, and along a straight ray `y` is linear but
// `ground` is not -- so no closed form exists for an arbitrary terrain, and ADR-567's rule is
// that the surface pass integrates rather than samples.
//
// The resolution: treat the GROUND as piecewise linear along the ray, between samples at
// `kFogGroundSegments + 1` evenly spaced points. On each piece the altitude above the followed top
// is then linear in the distance travelled, which is exactly the case ADR-058's difference
// quotient integrates in closed form -- the same `fogHeightIntegral`, applied piece by piece.
// Where the terrain IS linear along the ray (a plane, a slope, any affine ground) this equals the
// march's integral exactly; where it is not, the error is the gap between the terrain and its
// chord over an eighth of the ray, which `test_height_fog_gpu.cpp` measures on a valley.
//
// Each piece keeps the flat pass's two guards: wholly inside the layer is exactly 1, and a piece
// that does not climb takes the profile at its start.
fn fogGroundMean(tex: texture_2d<f32>, map0: vec4<f32>, map1: vec4<f32>, a: vec3<f32>, e: vec3<f32>,
                 top: f32, follow: f32, b: f32, upper: f32, curve: f32) -> f32 {
    var sum = 0.0;
    var d0 = fogLayerAltitude(a.y, top, follow, terrainGroundAt(tex, map0, map1, a.xz));
    for (var k = 1; k <= kFogGroundSegments; k = k + 1) {
        let p1 = mix(a, e, f32(k) / f32(kFogGroundSegments));
        let d1 = fogLayerAltitude(p1.y, top, follow, terrainGroundAt(tex, map0, map1, p1.xz));
        var mean = 1.0;
        if (max(d0, d1) > 0.0) {
            mean = fogHeightProfile(d0, b, upper, curve);
            let rise = d1 - d0;
            if (abs(rise) > 1e-3) {
                mean = (fogHeightIntegral(d1, b, upper, curve) - fogHeightIntegral(d0, b, upper, curve)) / rise;
            }
        }
        sum = sum + mean;
        d0 = d1;
    }
    return sum / f32(kFogGroundSegments);
}
