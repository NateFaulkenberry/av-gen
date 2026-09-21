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

// Density at height `y` ABOVE the layer's top, as a fraction of the layer's full density.
// Below the top (y <= 0) the layer is uniform, which is what "flat-topped" means.
fn fogHeightProfile(y: f32, b: f32) -> f32 {
    return exp(-b * max(0.0, y));
}

// How much air sits below height `y`, measured relative to the layer's top and in metres of the
// layer's full density: the antiderivative of `fogHeightProfile`, zeroed at y = 0. It is linear
// inside the layer and saturates at 1/b above it, and it is C1 across the join, so a ray crossing
// the layer's surface has no seam where the two halves meet.
fn fogHeightIntegral(y: f32, b: f32) -> f32 {
    if (y <= 0.0) {
        return y;
    }
    return (1.0 - exp(-b * y)) / b;
}
