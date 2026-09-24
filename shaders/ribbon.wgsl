// RIBBON (Effect Library Wave 1): camera-facing strips built on the CPU (world/effects/ribbon_frame)
// and drawn in pass 1's blended section, beside the particles.
//
// Each strip arrives as a triangle strip of vertex PAIRS on its centre line: the same point twice,
// with side -1 and +1. The vertex stage turns each pair into a cross-section facing the camera,
// `normalize(cross(tangent, toCamera)) * halfWidth`, and holds it at no less than 1.5 pixels across
// -- scaling the opacity by what it had to add, so a thread far away fades instead of breaking into
// dashes. The fragment stage shapes the cross-section: a CORE (soft-ramped, or hard with a
// one-pixel anti-aliased edge) plus an optional Gaussian GLOW over the whole width.
//
// Every target of the scene pass is declared (ADR-035). Colour and emission are blended (the trail
// is light, or paint, laid over the world); velocity is the strip's own camera-only motion -- its
// points are static in world space -- blended in by coverage so a faint tail barely touches the
// motion vectors of what is behind it; the normal and identifier targets are left to the opaque
// geometry (masked off by the pipeline). Depth is tested and never written. Fog: the height-aware
// surface fog attenuates the strip exactly as it does the ground behind it.

#include "common.wgsl"

struct RibbonIn {
    @location(0) positionSide: vec4<f32>, // xyz centre line, w side (-1 / +1)
    @location(1) tangentWidth: vec4<f32>, // xyz tangent, w half-width (m)
    @location(2) color: vec4<f32>,        // rgb HDR radiance, a opacity
    @location(3) profile: vec4<f32>,      // core fraction, glow, core boost, soft edge (1) / hard (0)
};

struct RibbonVaryings {
    @builtin(position) clip: vec4<f32>,
    @location(0) color: vec4<f32>,
    @location(1) across: f32,
    @location(2) profile: vec4<f32>,
    @location(3) world: vec3<f32>,
    @location(4) prevClip: vec4<f32>,
};

// Every target of the scene pass. `velocity` is a vec4 so its blend can read a coverage alpha; the
// RG16Float target keeps the first two components.
struct RibbonOut {
    @location(0) color: vec4<f32>,
    @location(1) normalRoughness: vec4<f32>,
    @location(2) velocity: vec4<f32>,
    @location(3) emission: vec4<f32>,
    @location(4) ids: u32,
};

// Mirrors `world::kRibbonMinPixelWidth`.
const kMinPixelWidth: f32 = 1.5;

fn ribbonPixels(clip: vec4<f32>) -> vec2<f32> {
    return clip.xy / clip.w * 0.5 * frame.targetSize.xy;
}

@vertex
fn vs_ribbon(in: RibbonIn) -> RibbonVaryings {
    let centre = in.positionSide.xyz;
    let side = in.positionSide.w;
    let tangent = in.tangentWidth.xyz;
    let toCamera = frame.cameraPos.xyz - centre;

    // The cross-section faces the camera. Looking straight down the strip, every axis across it
    // faces the camera equally, and the camera's right is the one that does not flip.
    var axis = cross(tangent, toCamera);
    let axisLength = length(axis);
    if (axisLength <= 1e-6 * max(length(tangent) * length(toCamera), 1e-12)) {
        axis = frame.cameraRight.xyz;
    } else {
        axis = axis / axisLength;
    }

    var half = max(in.tangentWidth.w, 0.0);
    var coverage = 1.0;
    // The minimum on-screen width: measure how many pixels a metre across is at this depth, and
    // hold the half-width at half the minimum, paying for it in opacity.
    let probe = max(half, 1e-3);
    let c0 = frame.viewProj * vec4<f32>(centre, 1.0);
    let c1 = frame.viewProj * vec4<f32>(centre + axis * probe, 1.0);
    if (c0.w > 1e-4 && c1.w > 1e-4) {
        let pixelsPerMetre = length(ribbonPixels(c1) - ribbonPixels(c0)) / probe;
        let halfPixels = half * pixelsPerMetre;
        let minHalf = 0.5 * kMinPixelWidth;
        if (pixelsPerMetre > 0.0 && halfPixels < minHalf) {
            coverage = halfPixels / minHalf;
            half = minHalf / pixelsPerMetre;
        }
    }

    let world = centre + axis * (half * side);
    var out: RibbonVaryings;
    out.clip = frame.viewProj * vec4<f32>(world, 1.0);
    out.prevClip = frame.prevViewProj * vec4<f32>(world, 1.0);
    out.color = vec4<f32>(in.color.rgb, clamp(in.color.a, 0.0, 1.0) * coverage);
    out.across = side;
    out.profile = in.profile;
    out.world = world;
    return out;
}

// x = radiance weight, y = coverage, at |across| = u.
fn ribbonShape(u: f32, profile: vec4<f32>, aa: f32) -> vec2<f32> {
    let glow = max(profile.y, 0.0);
    let boost = max(profile.z, 0.0);
    // Every profile is gone at the rim, over one pixel.
    let rim = 1.0 - smoothstep(1.0 - aa, 1.0, u);
    var core: f32;
    if (profile.w > 0.5) {
        // Soft: full inside the core, a smooth ramp from there to the rim.
        let c = clamp(profile.x, 0.0, 0.995);
        core = (1.0 - smoothstep(c, 1.0, u)) * rim;
    } else {
        // Hard: a crisp edge at the core's width, anti-aliased over one pixel.
        let c = clamp(profile.x, aa, 1.0);
        core = (1.0 - smoothstep(c - aa, c + aa, u)) * rim;
    }
    // The halo: a Gaussian with sigma a third of the half-width, so it has fallen to 1% at the rim.
    let halo = exp(-4.5 * u * u) * rim;
    return vec2<f32>(core * boost + glow * halo, clamp(max(core, glow * halo), 0.0, 1.0));
}

fn ribbonVelocity(in: RibbonVaryings, weight: f32) -> vec4<f32> {
    return vec4<f32>(screenVelocityAt(in.clip, in.prevClip), 0.0, weight);
}

// Light added to the frame: glow, energy, a lamp's streak.
@fragment
fn fs_ribbon_additive(in: RibbonVaryings) -> RibbonOut {
    let u = abs(in.across);
    let aa = max(fwidth(in.across), 1e-4);
    let shape = ribbonShape(u, in.profile, aa);
    let a = in.color.a;
    // Transmittance through the surface fog to this point, per channel: what `applyFog` keeps of a
    // unit colour and adds of nothing. Light added far away is dimmed, never tinted fog-coloured.
    let transmit = applyFog(vec3<f32>(1.0), in.world) - applyFog(vec3<f32>(0.0), in.world);
    let radiance = in.color.rgb * (shape.x * a) * transmit;
    let coverage = clamp(shape.y * a, 0.0, 1.0);
    var out: RibbonOut;
    out.color = vec4<f32>(radiance, 0.0);
    out.normalRoughness = vec4<f32>(0.0);
    out.velocity = ribbonVelocity(in, coverage);
    // All of it is emitted: the strip is a light source, and the bloom should know.
    out.emission = vec4<f32>(radiance, coverage);
    out.ids = 0u;
    return out;
}

// A covering strip: smoke, paint, a trail that hides what is behind it.
@fragment
fn fs_ribbon_alpha(in: RibbonVaryings) -> RibbonOut {
    let u = abs(in.across);
    let aa = max(fwidth(in.across), 1e-4);
    let shape = ribbonShape(u, in.profile, aa);
    let coverage = clamp(shape.y * in.color.a, 0.0, 1.0);
    let rgb = in.color.rgb * (shape.x / max(shape.y, 1e-3));
    var out: RibbonOut;
    out.color = vec4<f32>(applyFog(rgb, in.world), coverage);
    out.normalRoughness = vec4<f32>(0.0);
    out.velocity = ribbonVelocity(in, coverage);
    let transmit = applyFog(vec3<f32>(1.0), in.world) - applyFog(vec3<f32>(0.0), in.world);
    out.emission = vec4<f32>(rgb * transmit, coverage);
    out.ids = 0u;
    return out;
}
