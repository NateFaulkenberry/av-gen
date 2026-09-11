// Stylized water (ADR-091). Its own pipeline inside the scene pass, drawn after the opaque
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
    shore: vec4<f32>,          // x = foamWidth (m), y = edgeFade (m), z = refraction (m), w = 0
    life: vec4<f32>,           // x = glowScale, y = glowCoverage, z = glowDepth (m), w = swell (m)
    params: vec4<f32>,         // x = flow time (s), y = world's fastest body (m/s), z = 1 when the
                               //   linear-depth texture is real, w = 0
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
fn waterHash(cell: vec2<f32>) -> f32 {
    let h = dot(cell, vec2<f32>(127.1, 311.7));
    return fract(sin(h) * 43758.5453123);
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

fn rippleGradient(p: vec2<f32>, flowDir: vec2<f32>, speed: f32, t: f32, footprint: f32) -> vec2<f32> {
    let across = vec2<f32>(-flowDir.y, flowDir.x);
    let scale = water.ripples.y;
    let chop = water.ripples.w;
    var g = vec2<f32>(0.0);

    // Each layer's *height* falls as roughly the square of its frequency, which is what keeps the
    // broad swell in charge of the shape. Amplitudes that fall only as fast as the frequency rises
    // give every layer the same slope, and a surface whose slope is dominated by its finest layer
    // reads as crazed glass rather than as water. Nature does this too: capillary waves are
    // millimetres tall on top of metre-long swell.
    // Layer 1: the broad swell of the current itself, travelling downstream at the water's speed.
    let o1 = flowDir * (speed * t);
    let n1 = noiseD((p - o1) * scale);
    g = g + n1.yz * scale * rippleLayerFade(scale, footprint);

    // Layer 2: shorter, quicker, angled off the current -- the wind-driven chop.
    let s2 = scale * 2.7;
    let o2 = (flowDir * 1.35 + across * chop) * (speed * t * 1.6);
    let n2 = noiseD((p - o2) * s2 + vec2<f32>(37.2, 11.9));
    g = g + n2.yz * s2 * 0.20 * rippleLayerFade(s2, footprint);

    // Layer 3: capillary detail, travelling the other way across the flow, so the two upper layers
    // interfere and the pattern never settles. This is the layer the sparkle rides.
    let s3 = scale * 7.4;
    let o3 = (flowDir * 0.6 - across * chop * 1.7) * (speed * t * 2.4);
    let n3 = noiseD((p - o3) * s3 + vec2<f32>(-19.4, 63.1));
    g = g + n3.yz * s3 * 0.05 * rippleLayerFade(s3, footprint);
    return g;
}

// The view-space distance the opaque scene was drawn at under this pixel, or a very large number
// where there was no depth prepass this frame (in which case the shader falls back to the vertex
// depth and the surface degrades to what it was before ADR-091 rather than to garbage).
fn bedDepthAt(uv: vec2<f32>) -> f32 {
    if (water.params.z < 0.5) {
        return 1.0e7;
    }
    let size = vec2<f32>(textureDimensions(sceneLinearDepth, 0));
    let texel = vec2<i32>(clamp(uv, vec2<f32>(0.0), vec2<f32>(1.0)) * size);
    return textureLoad(sceneLinearDepth, texel, 0).r;
}

@fragment
fn fs_water(in: WaterOut, @builtin(front_facing) frontFacing: bool) -> SceneOut {
    let screenUv = in.clip.xy * frame.targetSize.zw;
    let toEye = frame.cameraPos.xyz - in.worldPos;
    let viewDistance = length(toEye);
    let v = toEye / max(viewDistance, 1e-4);
    let viewDepth = max(dot(in.worldPos - frame.cameraPos.xyz, frame.cameraForward.xyz), 1e-4);
    // Seen from below (the camera is under the surface) the sheet's up is still +Y in the world;
    // what changes is which side of it the eye is on, and that is what `underwater` carries.
    let underwater = frame.cameraPos.y < in.worldPos.y;

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
    let footprint = length(dpdx(in.worldPos.xz)) + length(dpdy(in.worldPos.xz));
    let gradient = rippleGradient(in.worldPos.xz, dir, speed, t, footprint) * water.ripples.x;
    var n = normalize(vec3<f32>(-gradient.x, 1.0, -gradient.y));
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
    let offset = n.xz * water.shore.z;
    let distorted = screenUv + offset * (0.35 / max(viewDistance * 0.1, 1.0));
    let bed = bedDepthAt(distorted);
    // Thickness along the ray, floored at zero: where the bed is nearer than the surface the water
    // is behind something and contributes nothing, which the depth test has already handled.
    var thickness = max(bed - viewDepth, 0.0);
    let vertical = max(in.uv.x, 0.0);
    if (water.params.z < 0.5) {
        // No prepass: the best available thickness is the vertical depth along the view ray.
        thickness = vertical / max(abs(v.y), 0.15);
    }
    // A surface seen from a very grazing angle over a shallow bed reports a long thickness and goes
    // opaque a metre from the bank, which is right for a lake and wrong for a stream you can see the
    // stones in. Capping the ray's thickness at a few times the vertical depth keeps the shallows
    // shallow whatever angle they are seen from.
    thickness = min(thickness, vertical * 6.0 + water.shore.x);

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
        let drift = in.worldPos.xz - dir * (speed * t * 0.45);
        let patchField = noiseD(drift * water.life.x).x * 0.5 + 0.5;
        let edge = 1.0 - clamp(water.life.y, 0.0, 1.0);
        let mask = smoothstep(edge, min(edge + 0.22, 1.0), patchField);
        // A second, faster field breaks each patch into drifting organisms rather than one blob.
        let grain = noiseD(drift * water.life.x * 6.3 + vec2<f32>(11.0, -4.0)).x * 0.5 + 0.5;
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
    // The field is faded against the pixel footprint like the ripple layers are: sub-pixel sparkle
    // has nothing to resolve and crawls, and a river at fifty metres would otherwise be static.
    var sparkle = vec3<f32>(0.0);
    if (water.sparkleColor.w > 0.0) {
        let frequency = water.ripples.y * 26.0;
        let fade = sparkleBandFade(frequency, footprint);
        if (fade > 0.0) {
            let sp = in.worldPos.xz * frequency - dir * (speed * t * 2.1);
            let crest = noiseD(sp).x * 0.5 + 0.5;
            let cut = smoothstep(0.90, 0.995, crest);
            // And only where the surface is already turned toward the eye's reflection of the sky:
            // a sparkle on a part of the water that is not reflecting anything is paint.
            sparkle = water.sparkleColor.rgb * water.sparkleColor.w * cut * fade * (0.2 + fresnel * 1.6);
        }
    }

    // ---- the shoreline (§10) -------------------------------------------------------------------
    // Three things happen at the bank, and the reason it works is that all three read the *scene's*
    // depth rather than the mesh's edge, so they are smooth across a quad boundary the geometry
    // is not.
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
        let surf = noiseD(in.worldPos.xz * surfFrequency - dir * (speed * t * 0.9)).x * 0.5 + 0.5;
        // Two thresholds, not one: the band says "near the waterline" and the field says "and on a
        // crest", and a foam that fires on either reads as scum on still water.
        let broken = smoothstep(0.46, 0.88, surf * 0.55 + band * 0.55) *
                     rippleLayerFade(surfFrequency, footprint);
        foam = band * broken * water.foamColor.w * mix(0.45, 1.0, speedFraction);
    }

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
    color = applyFog(color, in.worldPos);

    var out: SceneOut;
    out.color = vec4<f32>(color * alpha, alpha); // premultiplied by the blend's SrcAlpha factor
    out.normalRoughness = packNormalRoughness(n, water.surface.z, 5.0);
    out.velocity = screenVelocityAt(in.clip, in.prevClip);
    // The glint and the sparkle are what should bloom; the body colour should not, or a wide river
    // washes the whole frame. The alpha lane is the bloom weight.
    out.emission = vec4<f32>((glint + sparkle) * alpha, 1.0);
    out.ids = packIds(object.ids.x, object.ids.y);
    return out;
}

// Depth-only entry, for the passes that want the surface's silhouette without shading it. Water is
// blended and so is in none of them today; the entry exists so a future opaque-water tier is a
// pipeline change and not a shader rewrite.
@fragment
fn fs_water_depth(in: WaterOut) {
}
