// Atmospheric ground illumination (ADR-230, §6): what the sky pours onto the world below it.
//
// Its own file, separate from `atmosphere_fx.wgsl`, because the two have different audiences. The
// comet marching and the aurora shells are evaluated once per *sky* fragment from one dedicated
// draw; this is evaluated once per *surface* fragment from the shader that terrain, vegetation,
// rocks, mushrooms, props, characters and water all shade through. Pasting four hundred lines of
// ray integration into every one of those to reach thirty is a compile-time cost with no upside --
// and `atmosphere_fx.wgsl` would additionally drag its derivative-taking caller into modules that
// have vertex entry points, where derivatives are not legal.
//
// Included by pbr_shade.wgsl (so entities, the procedural scatter, skinned characters and
// raymarched SDFs all take it) and separately by water.wgsl, which shades through its own pipeline
// -- exactly the split ADR-207 records for world effects, and for exactly the same reason.

// ---- ground illumination (§6) ---------------------------------------------------------------------

// What the sky pours onto a surface below it.
//
// Deliberately not a light: an aurora is a source with no position to cast a shadow from, and §6
// asks for cinematic atmospheric illumination rather than for physically accurate lighting. What
// this is instead is a hemispheric wash weighted by how much of the sky the surface faces, plus one
// moving pool under the brightest comet. Both are already scaled on the CPU by the effect's
// three-word Off/Subtle/Strong setting and by its lifetime envelope, so this is an add and a
// multiply -- there is no loop here and no per-effect cost in the surface shader.
fn atmosphereGroundAt(worldPos: vec3<f32>, normal: vec3<f32>) -> vec3<f32> {
    var out = vec3<f32>(0.0);
    let ambient = frame.skyGroundAmbient.rgb;
    if (ambient.r + ambient.g + ambient.b > 0.0) {
        // An upward-facing surface sees all of it; a wall sees about a third. Never zero, because a
        // downward face in a valley still sees light bounced off everything around it, and a hard
        // zero there reads as a shading bug.
        out = out + ambient * (0.35 + 0.65 * clamp(normal.y * 0.5 + 0.5, 0.0, 1.0));
    }
    let pointColor = frame.skyGroundPointColor;
    if (pointColor.r + pointColor.g + pointColor.b > 0.0) {
        let p = frame.skyGroundPoint;
        // Measured in the ground plane: the comet is kilometres up, so the distance that matters is
        // how far the surface is from the point under it.
        let d = length(worldPos.xz - p.xz);
        let falloff = pow(clamp(1.0 - d / max(p.w, 1.0), 0.0, 1.0), pointColor.w);
        out = out + pointColor.rgb * falloff * (0.4 + 0.6 * clamp(normal.y, 0.0, 1.0));
    }
    // ADR-379: light spilling UP out of the cosmic vortex. The brief's §10 asks for the island's
    // underside to catch "subtle colored light from below" and §11 for the island to be visibly
    // part of a chain from tree to vortex; this is the half of that which costs nothing, because it
    // is the same shape as the comet pool above with the sign of the normal term reversed.
    //
    // Weighted by how much of the surface faces DOWN, so the island's flat top is untouched and its
    // underside takes nearly all of it -- which is the whole point, and is why this reads as the
    // vortex lighting the rock rather than as ambient being turned up.
    let vortexColor = frame.vortexGlowColor;
    if (vortexColor.w > 0.0) {
        let v = frame.vortexGlow;
        let radial = length(worldPos.xz - v.xz) / max(v.w, 1.0);
        // Above the mouth only: the vortex lights what floats over it, and a surface below the
        // mouth plane is inside the funnel rather than above it.
        let above = clamp((worldPos.y - v.y) / max(v.w, 1.0), 0.0, 1.0);
        let reach = clamp(1.0 - radial * 0.8, 0.0, 1.0) * (1.0 - smoothstep(0.0, 1.0, above));
        let facing = clamp(-normal.y, 0.0, 1.0);
        out = out + vortexColor.rgb * (vortexColor.w * reach * reach * (0.12 + 0.88 * facing));
    }
    return out;
}
