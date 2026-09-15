// Shared declarations for scene passes. Included by pbr.wgsl, grid.wgsl and skybox.wgsl.
// Uniform layouts mirror rendering/scene_renderer.hpp (static_asserts guard the sizes).

const MAX_LIGHTS: u32 = 8u;
const PI: f32 = 3.14159265;

struct Light {
    positionType: vec4<f32>,   // xyz = position, w = type (0 directional, 1 point, 2 spot)
    directionRange: vec4<f32>, // xyz = direction the light travels (normalised), w = range (0 = inf)
    colorIntensity: vec4<f32>, // rgb = colour * intensity
    cone: vec4<f32>,           // x = cos(outer), y = 1 / max(cos(inner) - cos(outer), eps),
                               // z = volumetric strength (volume.wgsl), w = 0
};

// One world effect, as world::packWorldEffect writes it (ADR-204). The evaluation lives in
// world_effects.wgsl; the layout lives here because it is part of the frame block.
struct WorldEffect {
    originKind: vec4<f32>, // xyz = origin (world), w = kind (0 directional, 1 radial)
    axisFront: vec4<f32>,  // xyz = unit axis (directional only), w = metres the front has travelled
    shape: vec4<f32>,      // x = front width, y = trail length, z = range, w = trail falloff exponent
    vertical: vec4<f32>,   // x = vertical half-extent, y = growth per metre travelled,
                           // z = secondary ring count, w = beam radius (0 = a plane, not a beam)
    color: vec4<f32>,      // rgb = core radiance (intensity and the lifetime envelope folded in),
                           // w = metres at which sparkle has faded out
    edge: vec4<f32>,       // rgb = leading-edge radiance, w = 1 when the hue comes from the rainbow
    rainbow: vec4<f32>,    // x = cycles per metre, y = phase, z = saturation, w = brightness
    sparkle: vec4<f32>,    // x = cells per metre (0 = off), y = size, z = intensity, w = twinkle phase
    response: vec4<f32>,   // x = ground, y = foliage (the scatter), z = surface, w = emissive gain
};

struct FrameUniforms {
    viewProj: mat4x4<f32>,
    invViewProj: mat4x4<f32>,
    prevViewProj: mat4x4<f32>, // ADR-035: last frame's, for velocity and temporal reprojection
    cameraPos: vec4<f32>,      // xyz = world position
    cameraRight: vec4<f32>,    // xyz = camera right axis in world space (billboards)
    cameraUp: vec4<f32>,       // xyz = camera up axis in world space
    cameraForward: vec4<f32>,  // xyz = camera view axis in world space (froxel depth)
    params: vec4<f32>,         // x = time, y = gridIntensity, z = brightness, w = environmentIntensity
    envParams: vec4<f32>,      // x = env rotation (radians), y = prefiltered mip count - 1, z = light count, w = ibl enabled
    skyParams: vec4<f32>,      // rgb = background colour, w = skybox blur 0..1
    skyExtra: vec4<f32>,       // ADR-036: x = 1 when the IBL is the procedural sky, y = draw it as
                               // the background instead of the flat colour; ADR-049: z = the
                               // visible sky's intensity, w = how much of it the bloom mask sees
    skySun: vec4<f32>,         // xyz = direction *towards* the sky's sun/moon, w = 1 when it has one
    fogParams: vec4<f32>,      // rgb = fog colour, w = fog density (0 = off; exp2 fog by view distance)
    fogHeight: vec4<f32>,      // ADR-058: x = mist layer top (m), y = falloff per metre above it,
                               // z = how much of that layer the surface fog integrates, w = 0
    styledSky: vec4<f32>,      // ADR-058: rgb = styled ambient towards +Y, w = the AO floor
    styledGround: vec4<f32>,   // ADR-058: rgb = styled ambient towards -Y, w = 0
    audio: vec4<f32>,          // ADR-030 material inputs: rms, bass, mid, treble
    audioBands: vec4<f32>,     // lowMid, highMid, spectral centroid, flux
    beat: vec4<f32>,           // beat phase 0..1, pulse (1 - phase), onset strength, bar phase
    clusterParams: vec4<f32>,  // xyz = froxel grid dimensions, w = 1 when the clustered path is on
    clusterDepth: vec4<f32>,   // x = slice scale, y = slice bias, z = near, w = far
    lightCounts: vec4<f32>,    // x = directional lights, y = total lights, z = stylized shading
    shadowParams: vec4<f32>,   // x = atlas resolution, y = PCF radius (texels), z = contact steps, w = contact length
    aoParams: vec4<f32>,       // x = strength, y = 1 when AO is on, zw = the AO texture size
    targetSize: vec4<f32>,     // x = width, y = height, z = 1 / width, w = 1 / height
    // ADR-087: the half-resolution screen-space shadow mask. x = 1 when it was built this frame,
    // y = how many leading directional lights it covers (0..3), zw = its texture size in texels.
    shadowMaskParams: vec4<f32>,
    // Phase D material tiers (ADR-133). x = the tier every draw is forced to at least (0 full,
    // 1 reduced lights, 2 flat); y = the local-light budget of tier 1; z = the local-light budget
    // of tier 2; w = 0. The per-draw tier is object.ids.w and the effective tier is the larger of
    // the two, so a forced tier is a floor and never silently un-reduces a draw.
    materialTier: vec4<f32>,
    // ADR-055 the wind field. Frame-global because the air is: the same four vectors drive every
    // shader that wants to know what is blowing, and the shadow views inherit them with the rest of
    // the block so a swaying plant and its shadow cannot disagree. See shaders/wind.wgsl.
    windDir: vec4<f32>,        // xy = unit direction in XZ, z = speed, w = 1 when the wind is on
    windRegion: vec4<f32>,     // x = tau/regionScale, y = regionAmount, z = regionDrift*tau,
                               // w = turbulence (radians of local direction change)
    windGust: vec4<f32>,       // x = tau/gustScale, y = gustSpeed (m/s), z = gustAmount, w = sharpness
    windTurb: vec4<f32>,       // x = tau/turbulenceScale, y = turbulenceSpeed (m/s),
                               // z = tau/flutterScale, w = 0
    lights: array<Light, 8>,
    // ADR-204 world effects. Appended after `lights` so no existing offset moved, and in the frame
    // block for the same reason the wind is (ADR-055): a phenomenon propagating through the world is
    // frame-global because the world is, and the shadow views inherit it with the rest of the block.
    // x = how many of the array below are live; the rest of the vector is spare.
    worldEffectCount: vec4<f32>,
    worldEffects: array<WorldEffect, 8>,
};

struct ObjectUniforms {
    model: mat4x4<f32>,
    normalMatrix: mat4x4<f32>,
    prevModel: mat4x4<f32>,    // ADR-035: last frame's model matrix, for the velocity target
    baseColor: vec4<f32>,      // rgb, a = opacity
    emissive: vec4<f32>,       // rgb = colour, w = intensity
    material: vec4<f32>,       // x = roughness, y = metallic, z = normalScale, w = occlusionStrength
    flags: vec4<f32>,          // x = alpha mode (0 opaque, 1 mask, 2 blend), y = alpha cutoff, z = unlit, w = texture mask
    ids: vec4<f32>,            // x = object id (ADR-030 `objectId`), y = material id, z = bloom weight,
                               // w = the skinned joint count (pbr_skinned.wgsl)
};

// The material tier this draw shades at (ADR-133; 0 full, 1 reduced lights, 2 flat). Uniform across
// the draw, which is the whole design -- ADR-118 measured a lane-varying branch around a loop with a
// dependent texture load in it running 4.4% *slower* than the loop it skipped.
//
// It comes from the *frame* and not from ObjectUniforms, and that is a limitation rather than a
// design: ADR-135 records that ObjectUniforms has no free lane for a per-draw tier -- `ids.w` is the
// skinned joint count and every other lane is live -- and that adding one is the object-layout work
// Phase E owns. Until then the tier is frame-global, which is exactly what an A/B arm needs and not
// what importance-driven assignment needs.
fn materialTierOf() -> u32 {
    return u32(frame.materialTier.x + 0.5);
}

// ADR-138's deciding arm. `forcedMaterialTier` is frame-global (ADR-135: `ObjectUniforms` has no
// free lane), but the question the arm has to answer is what tier assignment could realize -- and
// on Glowmere the assignable share is the procedural scatter, because authored entities are 77% of
// coverage and the terrain can never be demoted. `.w` carries a tier for procedural draws alone:
// negative means "no override, use the frame's". This measures the assignable share directly
// instead of estimating it from a coverage table.
fn proceduralMaterialTierOf() -> u32 {
    let override_ = frame.materialTier.w;
    if (override_ < 0.0) {
        return materialTierOf();
    }
    return u32(override_ + 0.5);
}

// ADR-155: per-rung assignment. The frame-wide override above is an *arm* -- it demotes every
// procedural draw at once, which measured 5.64 ms and failed §50 by flattening the foreground. This
// is the shipping form: the tier comes from the rung's own uniform slot, and rung tracks projected
// size, so the near ferns stay Full while the distant scatter does not. `tier` is uniform across
// the draw either way, which is the condition ADR-118 measured a saving to need.
fn proceduralRungTierOf(rungTier: f32) -> u32 {
    let frameTier = proceduralMaterialTierOf();
    return max(frameTier, u32(rungTier + 0.5));
}

// How many *local* (clustered) lights a fragment of `tier` may evaluate. The table lives here and
// in QualitySettings::localLightBudget and nowhere else.
fn materialTierLocalLights(tier: u32) -> u32 {
    if (tier >= 2u) { return u32(frame.materialTier.z + 0.5); }
    if (tier >= 1u) { return u32(frame.materialTier.y + 0.5); }
    return 0xffffffffu;
}

@group(0) @binding(0) var<uniform> frame: FrameUniforms;
@group(1) @binding(0) var<uniform> object: ObjectUniforms;

struct VertexIn {
    @location(0) position: vec3<f32>,
    @location(1) normal: vec3<f32>,
    @location(2) uv: vec2<f32>,
};

struct VertexOut {
    @invariant @builtin(position) clip: vec4<f32>,
    @location(0) worldPos: vec3<f32>,
    @location(1) normal: vec3<f32>,
    @location(2) uv: vec2<f32>,
    @location(3) localPos: vec3<f32>, // object space, for the ADR-030 `localPosition` material input
    @location(4) prevClip: vec4<f32>, // last frame's clip position, for the velocity target
};

@vertex
fn vs_main(in: VertexIn) -> VertexOut {
    var out: VertexOut;
    let world = object.model * vec4<f32>(in.position, 1.0);
    out.clip = frame.viewProj * world;
    out.worldPos = world.xyz;
    out.normal = normalize((object.normalMatrix * vec4<f32>(in.normal, 0.0)).xyz);
    out.uv = in.uv;
    out.localPos = in.position;
    out.prevClip = frame.prevViewProj * (object.prevModel * vec4<f32>(in.position, 1.0));
    return out;
}

// Rotates a world direction into the environment map's frame (rotation about +Y).
fn envRotate(dir: vec3<f32>) -> vec3<f32> {
    let a = frame.envParams.x;
    let c = cos(a);
    let s = sin(a);
    return vec3<f32>(c * dir.x + s * dir.z, dir.y, -s * dir.x + c * dir.z);
}

// Hash-based value noise, cheap and deterministic (no texture lookups).
fn hash21(p: vec2<f32>) -> f32 {
    let h = dot(p, vec2<f32>(127.1, 311.7));
    return fract(sin(h) * 43758.5453123);
}

// How much air sits below height `y`, measured relative to the mist layer's top and in metres of
// the layer's full density: the antiderivative of exp(-b * max(0, y)), zeroed at y = 0. It is
// linear inside the layer and saturates at 1/b above it, and it is C1 across the join, so a ray
// crossing the fog bank's surface has no seam where the two halves meet.
fn fogHeightIntegral(y: f32, b: f32) -> f32 {
    if (y <= 0.0) {
        return y;
    }
    return (1.0 - exp(-b * y)) / b;
}

// Exponential-squared distance fog towards frame.fogParams.rgb; density 0 leaves the colour
// untouched (the branch keeps the no-fog output bit-identical to the pre-fog shader).
//
// ADR-058: when frame.fogHeight.z is non-zero the geometric distance is first replaced by the
// distance *through the mist*, integrating the same flat-topped layer the volumetric marches along
// the view ray. Both endpoints inside the layer integrate to the ray's own length, so a scene that
// keeps everything below the fog bank is unchanged to the last bit; what moves is the ridge line
// and the canopy crowns standing out of it, which are now seen through the air that is actually
// between them and the eye rather than through a uniform slab.
fn applyFog(color: vec3<f32>, worldPos: vec3<f32>) -> vec3<f32> {
    let density = frame.fogParams.w;
    if (density <= 0.0) {
        return color;
    }
    var travel = distance(frame.cameraPos.xyz, worldPos);
    let amount = frame.fogHeight.z;
    let falloff = frame.fogHeight.y;
    if (amount > 0.0 && falloff > 0.0) {
        let y0 = frame.cameraPos.y - frame.fogHeight.x;
        let y1 = worldPos.y - frame.fogHeight.x;
        let rise = y1 - y0;
        // The mean of the layer's density along the ray. The difference quotient is the whole
        // integral because the ray climbs at a constant rate: metres of mist per metre travelled.
        var mean = 1.0;
        if (max(y0, y1) > 0.0) {
            // Both endpoints below the layer's top puts the whole segment below it, so the mean is
            // exactly one and this branch is skipped -- which is what keeps a scene that sits
            // inside its own fog bank bit-identical when the integration is switched on. Leaving
            // it to the quotient would give 1.0 only to within rounding, because the numerator and
            // the denominator are the same subtraction written twice and the compiler is free to
            // fuse one of them and not the other.
            mean = exp(-falloff * max(y0, 0.0)); // a level ray never leaves its own altitude
            if (abs(rise) > 1e-3) {
                mean = (fogHeightIntegral(y1, falloff) - fogHeightIntegral(y0, falloff)) / rise;
            }
        }
        travel = travel * mix(1.0, mean, amount);
    }
    let d = travel * density;
    let f = exp(-d * d);
    return mix(frame.fogParams.rgb, color, f);
}

// ---- auxiliary targets (ADR-035) ---------------------------------------------------------------
//
// Every pipeline in the scene pass writes the same five colour targets, so the auxiliary buffers
// agree at silhouettes: 0 = HDR radiance, 1 = normal (octahedral) + roughness + flags,
// 2 = velocity in UV units, 3 = emission with a bloom weight in alpha, 4 = packed identifiers.

struct SceneOut {
    @location(0) color: vec4<f32>,
    @location(1) normalRoughness: vec4<f32>,
    @location(2) velocity: vec2<f32>,
    @location(3) emission: vec4<f32>,
    @location(4) ids: u32,
};

// Cigolle et al. 2014: a unit vector in two components with no visible loss at 16 bits.
fn octEncode(nIn: vec3<f32>) -> vec2<f32> {
    let n = nIn / max(abs(nIn.x) + abs(nIn.y) + abs(nIn.z), 1e-8);
    if (n.z >= 0.0) {
        return n.xy;
    }
    let signs = vec2<f32>(select(-1.0, 1.0, n.x >= 0.0), select(-1.0, 1.0, n.y >= 0.0));
    return (vec2<f32>(1.0) - abs(vec2<f32>(n.y, n.x))) * signs;
}

fn octDecode(e: vec2<f32>) -> vec3<f32> {
    var n = vec3<f32>(e.x, e.y, 1.0 - abs(e.x) - abs(e.y));
    if (n.z < 0.0) {
        let signs = vec2<f32>(select(-1.0, 1.0, n.x >= 0.0), select(-1.0, 1.0, n.y >= 0.0));
        let xy = (vec2<f32>(1.0) - abs(vec2<f32>(n.y, n.x))) * signs;
        n = vec3<f32>(xy, n.z);
    }
    return normalize(n);
}

// Screen-space motion in UV units, from this frame's and last frame's clip positions. Both must
// be genuine clip-space vectors: a fragment's @builtin(position) is *not* one (it is in
// framebuffer pixels with w = 1 / clip.w), so use screenVelocityAt() for that.
fn screenVelocity(clip: vec4<f32>, prevClip: vec4<f32>) -> vec2<f32> {
    let now = clip.xy / max(abs(clip.w), 1e-6) * sign(max(clip.w, 1e-6));
    let before = prevClip.xy / max(abs(prevClip.w), 1e-6) * sign(max(prevClip.w, 1e-6));
    // NDC -> UV: x maps directly, y flips.
    return vec2<f32>((now.x - before.x) * 0.5, (before.y - now.y) * 0.5);
}

// Screen-space motion in UV units when the current position comes from the fragment stage's
// @builtin(position). Dividing that by its own w is meaningless - it yields pixel * clip.w, which
// is what made the velocity target read hundreds of screens per frame - so the UV comes straight
// from the framebuffer coordinate instead.
fn screenVelocityAt(fragCoord: vec4<f32>, prevClip: vec4<f32>) -> vec2<f32> {
    let nowUv = fragCoord.xy * frame.targetSize.zw;
    let before = prevClip.xy / max(abs(prevClip.w), 1e-6) * sign(max(prevClip.w, 1e-6));
    let beforeUv = vec2<f32>(before.x * 0.5 + 0.5, 0.5 - before.y * 0.5);
    return nowUv - beforeUv;
}

// The object id carries a two-bit tag naming which renderer numbered it (scene_types.hpp,
// `PickSpace`). The identifier target wants it; a material program does not -- `objectId` is an
// ADR-030 input whose meaning is "which object is this", and adding 16384 to every procedural would
// silently change what every per-object hash in every material program produced.
fn pickIndex(objectId: f32) -> f32 {
    return f32(u32(max(objectId, 0.0)) & 0x3FFFu);
}

fn packIds(objectId: f32, materialId: f32) -> u32 {
    return (u32(clamp(objectId, 0.0, 65535.0)) & 0xFFFFu) | ((u32(clamp(materialId, 0.0, 65535.0)) & 0xFFFFu) << 16u);
}

// `flags` in the normal target's alpha: 1 = lit surface, 2 = emissive, 4 = transparent, 8 = sky.
fn packNormalRoughness(normal: vec3<f32>, roughness: f32, flags: f32) -> vec4<f32> {
    return vec4<f32>(octEncode(normal), clamp(roughness, 0.0, 1.0), flags);
}
