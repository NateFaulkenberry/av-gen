// The atmospheric sky layer (ADR-230): one fullscreen triangle at the far plane, additive.
//
// This is a *separate draw from the skybox* rather than three added lines inside it, for four
// reasons that are each worth the extra pipeline:
//
//  1. **It runs when the skybox does not.** `SceneRenderer` only draws the sky when the scene has an
//     IBL and has asked for it as a background. A comet should not require a procedural sky to
//     exist, and folding it into `skybox.wgsl` would have made it silently invisible in every scene
//     with a flat background.
//  2. **It blooms at its own weight.** The sky's contribution to the emission target is gated by
//     `Environment::skyBloom`, which is 0.1 in Glowmere -- a sensible number for an environment map
//     and a catastrophic one for a comet, which *is* a light source. This draw blends additively
//     into target 3 and carries its own weight.
//  3. **It costs nothing when there is nothing.** The renderer skips the draw entirely when no
//     effect is live, so an unused feature is not even a uniform branch.
//  4. **It cannot regress the sky.** `skybox.wgsl` keeps the star field, the moon disc and the
//     equirect LOD selection that ADR-049 had to hand-tune, untouched.
//
// Depth: the triangle sits at clip z = 1 with `LessEqual` and no depth write, exactly as the skybox
// does, so terrain that has already written depth occludes it. That is the whole of §5's "the aurora
// should convincingly originate from the horizon" -- there is no horizon to author, because the
// depth buffer already is one.
#include "common.wgsl"
#include "atmosphere_fx.wgsl"

struct AtmosphereOut {
    @builtin(position) clip: vec4<f32>,
    @location(0) ndc: vec2<f32>,
};

@vertex
fn vs_atmosphere(@builtin(vertex_index) index: u32) -> AtmosphereOut {
    var positions = array<vec2<f32>, 3>(vec2<f32>(-1.0, -1.0), vec2<f32>(3.0, -1.0), vec2<f32>(-1.0, 3.0));
    var out: AtmosphereOut;
    let p = positions[index];
    out.clip = vec4<f32>(p, 1.0, 1.0); // z = far plane, as skybox.wgsl
    out.ndc = p;
    return out;
}

@fragment
fn fs_atmosphere(in: AtmosphereOut) -> SceneOut {
    // The same unprojection the skybox uses. The two unprojected points differ by the camera
    // position, so their difference depends on the camera's orientation alone -- but unlike the sky,
    // what we do with it depends on the camera's *position* too, because a comet is a finite
    // distance away and is supposed to move against the stars when the camera crosses the valley.
    let near = frame.invViewProj * vec4<f32>(in.ndc, 0.0, 1.0);
    let far = frame.invViewProj * vec4<f32>(in.ndc, 1.0, 1.0);
    let dir = normalize(far.xyz / far.w - near.xyz / near.w);

    // The angular size of one pixel, from the screen-space derivative of the view ray. Taken here,
    // at the top of a fragment shader and in uniform control flow: derivatives are illegal in a
    // vertex shader and undefined inside the comet loop's non-uniform branches. It is the exact,
    // resolution- and field-of-view-aware number the anti-aliasing needs, and it is the same for
    // every effect, so it is taken once and passed down.
    let pixelAngle = max(length(dpdx(dir)) + length(dpdy(dir)), 1.0e-7);
    let atmos = atmosphereSkyAt(frame.cameraPos.xyz, dir, pixelAngle);

    var out: SceneOut;
    // Additive into the HDR target and into emission; the other three targets have their write mask
    // set to None by the pipeline, for the reason `water_renderer.cpp` gives -- a normal or a
    // velocity averaged over a transparency is worse than none at all. They are written here only
    // because a fragment shader has to return the struct its pipeline declares.
    out.color = vec4<f32>(atmos.radiance, 1.0);
    out.normalRoughness = vec4<f32>(0.0);
    out.velocity = vec2<f32>(0.0);
    out.emission = vec4<f32>(atmos.radiance, atmos.bloom);
    out.ids = 0u;
    return out;
}
