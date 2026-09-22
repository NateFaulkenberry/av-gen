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
