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

// One surface wave (a Ground Pulse or Travel Beam instance), as world::packWave writes it (ADR-207). The evaluation lives in
// wave_effects.wgsl; the layout lives here because it is part of the frame block.
struct Wave {
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

// ADR-230 atmospheric effects. Mirrors world::CometGpu / world::AuroraGpu; the C++ side asserts
// both sizes, and the sum assert on FrameUniforms is what makes a lane added on one side a compile
// error on the other rather than a silent misread.
struct AtmosComet {
    anchorTravel: vec4<f32>, // xyz = anchor (world), w = arc length flown (m)
    dir0Tail: vec4<f32>,     // xyz = unit direction to the launch point, w = tail length (m)
    dir1Path: vec4<f32>,     // xyz = unit direction to the destination, w = arc length (m)
    arc: vec4<f32>,          // x = distance (m), y = omega (rad), z = lift, w = curvature
                             // (both bows as fractions of the distance)
    core: vec4<f32>,         // rgb = core radiance (envelope folded in), w = head radius (m)
    halo: vec4<f32>,         // rgb = halo radiance, w = halo radius (m)
    tail: vec4<f32>,         // rgb = tail radiance, w = tail falloff exponent
    shape: vec4<f32>,        // x = tail width (m), y = wisp amount (m), z = wisp scale (1/m), w = flow phase
    sparkle: vec4<f32>,      // x = fragments per metre (0 = off), y = fragment radius (m),
                             // z = intensity, w = twinkle phase
    rainbow: vec4<f32>,      // x = cycles per metre, y = phase, z = saturation, w = brightness (0 = off)
};

struct AtmosAurora {
    config: vec4<f32>,  // x = curtain shells, y = nearest radius (m), z = base height (world Y), w = curtain height (m)
    shape: vec4<f32>,   // x = wave amplitude, y = wave scale, z = turbulence, w = complexity
    flow: vec4<f32>,    // x = flow phase, y = drift phase, z = vertical phase, w = layer spacing
    low: vec4<f32>,     // rgb = base radiance (intensity and envelope folded in), w = opacity
    mid: vec4<f32>,     // rgb = mid radiance, w = bloom weight
    top: vec4<f32>,     // rgb = top radiance, w = edge brightness
    detail: vec4<f32>,  // x = filaments, y = sparkle, z = spectrum shape amount, w = horizon glow
    audio: vec4<f32>,   // x = bass, y = lowMid, z = mid, w = high -- depths, already sensitised
    audio2: vec4<f32>,  // x = beat depth, y = rainbow amount, z = rainbow scale, w = rainbow phase
    anchor: vec4<f32>,  // xyz = shell centre (world), w = rainbow saturation
    band0: vec4<f32>,   // the spectrum, low to high: bins 0..3
    band1: vec4<f32>,   // bins 4..7
    band2: vec4<f32>,   // bins 8..11
    band3: vec4<f32>,   // bins 12..15
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
    // ADR-207 surface waves (ADR-702: the Ground Pulse and Travel Beam types). Appended after `lights` so no existing offset moved, and in the frame
    // block for the same reason the wind is (ADR-055): a phenomenon propagating through the world is
    // frame-global because the world is, and the shadow views inherit it with the rest of the block.
    // x = how many of the array below are live; the rest of the vector is spare.
    waveCount: vec4<f32>,
    waves: array<Wave, 8>,
    // ADR-230 atmospheric effects. Appended after the surface waves for the same reason those were
    // appended after `lights`: no offset above it moves, so nothing already reading this block can
    // be broken by adding to the end of it.
    // x = live comets, y = live auroras, z = how many samples the tail march takes, w = 0.
    atmosCount: vec4<f32>,
    comets: array<AtmosComet, 6>,
    auroras: array<AtmosAurora, 2>,
    // §6 ground illumination, already summed and already scaled by each effect's Off/Subtle/Strong
    // setting and lifetime envelope, so the surface shader adds rather than loops.
    skyGroundAmbient: vec4<f32>,    // rgb = hemispheric radiance, w = 0
    skyGroundPoint: vec4<f32>,      // xyz = the brightest comet's ground track point, w = radius (m)
    skyGroundPointColor: vec4<f32>, // rgb = its radiance, w = falloff exponent
    // ADR-345: the analytic sky's parameters, so the background pass can evaluate it without a
    // cube. Appended last, mirroring FrameUniforms in rendering/scene_renderer.hpp -- the sum in
    // that file's static_assert is what catches these two drifting apart.
    skyZenithColor: vec4<f32>,      // rgb = zenith, w = haze width
    skyHorizonColor: vec4<f32>,     // rgb = horizon, w = sun angular radius
    skyGroundColor: vec4<f32>,      // rgb = below the horizon, w = sun glow width
    skySunRadiance: vec4<f32>,      // rgb = sun/moon colour, w = 1 when the analytic sky is drawn
    // ADR-379: the cosmic vortex's light on what floats above it. Appended last, mirroring
    // FrameUniforms; the sum in scene_renderer.hpp's static_assert catches these drifting apart.
    vortexGlow: vec4<f32>,          // xyz = mouth centre, w = mouth radius
    vortexGlowColor: vec4<f32>,     // rgb = radiance, w = intensity (0 = no vortex)
    fogShape: vec4<f32>,            // ADR-568: x = fogUpperDensity, y = fogHeightCurve, zw = 0
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
    // ADR-360: mesh wind. windShape.y is the gate AND the amplitude; 0 means the deformation is not
    // evaluated and the draw is byte-identical to one from before this existed, which is the state
    // every object in every existing scene is in. Shared by every mesh of one body, deliberately:
    // see meshWindOffset below for why they may not each carry their own origin.
    windOrigin: vec4<f32>,     // xyz = the body's root in world space, w = 1 / body height
    windShape: vec4<f32>,      // x = 1 / body radius, y = strength (0 = off), z = branch influence,
                               // w = foliage influence
    windTune: vec4<f32>,       // x = trunk influence, y = leaf flutter, z = response lag (seconds),
                               // w = the previous frame's time, for the velocity target
    // ADR-376: tree energy and canopy shimmer. Both are emissive modulation keyed on WHERE a point
    // is in the body -- height above the root, distance from the axis -- which is exactly the frame
    // `windOrigin`/`windShape` already establish, so they are reused rather than duplicated. A
    // separate origin for the same tree is two things that can disagree.
    // energy0.x is the gate for both; zero means the fragment stage returns before any of it.
    energy0: vec4<f32>,        // x = energy intensity, y = pulse speed (Hz), z = pulse width,
                               // w = propagation speed (body heights per second)
    energy1: vec4<f32>,        // x = root, y = trunk, z = branch, w = canopy share
    energy2: vec4<f32>,        // x = noise amount, y = noise scale, z = noise speed,
                               // w = bloom contribution
    energy3: vec4<f32>,        // x = shimmer intensity, y = shimmer speed, z = shimmer scale,
                               // w = shimmer variation
    energyA: vec4<f32>,        // rgb = the pulse's near colour
    energyB: vec4<f32>,        // rgb = its far colour
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

// ADR-055's wind field, and ADR-360's reason for hoisting it here from procedural.wgsl: the mesh
// vertex stage below now deforms too, and wind.wgsl needs `frame`, so it has to come after the
// binding above and before vs_main. The include directive does not de-duplicate -- procedural.wgsl
// used to include this itself and no longer may.
#include "wind.wgsl"

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

// ADR-360. Wind deformation for imported meshes, which had none: `windDisplacement` above was
// reached only from the instanced procedural scatter, so the Tree of Life -- five GLBs -- could not
// move however high the scene's wind was turned, and a four-arm render of it at windSpeed 0, 4.0
// and with the field forced on in the scene file came back byte-identical.
//
// WHY THIS IS A FUNCTION OF WORLD POSITION AND NOT OF THE MESH. `bendDisplacement` bends a mesh
// about ITS OWN base, and scene/tree_rig.hpp already documents what that costs on this asset: two
// tiers bent about different origins separate at every joint between them, and no setting of the
// amounts fixes it, because the discontinuity is in the decomposition. Here every term reads the
// vertex's world position and a per-BODY origin that all five meshes share, so two vertices at the
// same point in space are displaced identically whichever mesh they came from. There are no cracks
// to tune away. What that buys is paid for in fidelity: the hierarchy below is a proxy built from
// height and radial distance, not the tree's real topology, so a branch that hangs low and close to
// the trunk moves like a trunk. At this scale nobody reads that; a skeleton would need a TreeGraph
// the GLB does not carry.
//
// Pure function of (uniforms, position, time), per ADR-091: scrubbing to a frame and playing to it
// put the crown in the same place. The owner's 2026-09-19 relaxation covers particles, not this.
const kMeshBendLimit: f32 = 0.60; // ADR-377: crown travel ceiling, in body heights
fn meshWindOffset(worldPos: vec3<f32>, t: f32) -> vec3<f32> {
    let strength = object.windShape.y;
    if (strength <= 0.0 || frame.windDir.w <= 0.0) {
        return vec3<f32>(0.0);
    }
    let root = object.windOrigin.xyz;
    let invHeight = object.windOrigin.w;
    let height = 1.0 / max(invHeight, 1e-6);
    // Height along the body, 0 at the root: the whole anchoring story, since every tier's profile
    // is a power of it and every power of 0 is 0.
    let h = clamp((worldPos.y - root.y) * invHeight, 0.0, 1.0);
    // Distance out from the body's axis, normalised. This is what separates a trunk from a twig
    // without knowing which mesh either is in.
    let r = clamp(length(worldPos.xz - root.xz) * object.windShape.x, 0.0, 1.0);

    // Two samples. The root's is what the body leans to as a whole -- a tree does lean as one
    // thing. The vertex's own is what stops the crown twitching as a unit: a limb on the windward
    // side meets a gust front before the one behind it, because the front travels.
    let wRoot = windSampleAt(root, t - object.windTune.z);
    let wHere = windSampleAt(worldPos, t);
    let dir = wRoot.direction;
    let perp = vec2<f32>(-wHere.direction.y, wHere.direction.x);

    // Three tiers, three profiles, three rates, and not one global sine between them.
    let trunk = pow(h, 2.4) * (1.0 - 0.65 * r) * object.windTune.x;
    let branch = pow(h, 1.7) * smoothstep(0.05, 0.60, r) * object.windShape.z;
    let foliage = pow(h, 1.3) * smoothstep(0.28, 1.00, r) * object.windShape.w;

    // The coefficients are fractions of the BODY'S HEIGHT, and the first calibration of them was
    // wrong in a way worth recording: 0.05 reads as "five per cent" and sounds modest, but this
    // tree is 138 metres, so it asked for seven metres of crown travel -- and because the tier
    // weights vary with position, seven metres of travel means a couple of metres of DIFFERENCE
    // across a single leaf card. The render came back with the canopy shredded into horizontal
    // dashes: every leaf stretched, because its own vertices had been pulled apart. The amplitude
    // that matters is not the one you can see at the crown, it is the gradient across the smallest
    // piece of geometry the mesh is made of. A monument moves about one per cent of its height.
    // Scaled so that `strength = 1` is a large tree in a light wind. The first calibration put the
    // usable value at about 5, which is two and a half times the control's soft maximum -- a knob
    // whose sane setting is off the end of its own slider is a mis-scaled knob, not a preference.
    let steady = wRoot.strength * (1.0 + 0.9 * wRoot.gust);
    var off = dir * (steady * (trunk + 0.55 * branch) * 0.18);
    // Secondary limbs travel on their own local front, so the crown is never in phase with itself.
    off = off + wHere.direction * (wHere.strength * (0.35 + wHere.gust) * branch * 0.12);
    // Foliage flutter: fast, small, and decorrelated by the field's spatial phase term, which is
    // what keeps neighbouring leaves rattling out of step instead of shimmering together. Two
    // orders of magnitude below the lean, because this is centimetres of leaf, not metres of limb.
    let flutter = sin(wHere.phase + t * (5.5 + 3.0 * wHere.strength) + h * 9.0);
    // ADR-377. The flutter's amplitude SATURATES; the lean above does not, and the difference is
    // the whole reason the canopy survives a gale. `WindSample::phase` turns a full cycle every
    // `flutterScale` metres, so the flutter's amplitude appears as a difference between one end of
    // a leaf card and the other (ADR-360 found this the first time). The lean varies only through
    // pow(h,k) and a smoothstep over radius and shreds nothing however large it gets.
    //
    // Unbounded, `wHere.strength` reaches about 4.9 at the slider's own maximum -- regional
    // variation multiplies the authored speed -- and at 3.4 the twelve-shot review caught the
    // canopy smeared into streaks. A slider that destroys the asset inside its own range is a
    // defect, so the flutter stops growing at 1.8 and the lean carries the rest. Below 1.8 this is
    // exactly what it was, which is why the shipped scene at 1.319 is byte-identical.
    let flutterDrive = min(wHere.strength, 2.2);
    off = off + perp * (flutter * foliage * object.windTune.y * 0.0016 * (0.35 + flutterDrive));

    // ADR-377. A SOFT CEILING on how far the crown may travel, as a fraction of the body's height.
    // ADR-055 gives procedural plants exactly this (`bendLimit`) and the mesh path never got one,
    // so the lean grew without bound: `wHere.strength` is the authored speed multiplied by the
    // regional variation and reaches about 4.9 at the slider's own maximum, and the twelve-shot
    // review caught the canopy smeared into streaks at 3.4 -- not from the flutter, which was the
    // obvious suspect and which capping barely changed, but from the lean. A large enough lean
    // makes even the smoothstep weights' gentle gradient amount to metres across a single leaf.
    //
    // The form is ADR-055's: identity for small offsets, asymptotic to the limit for large ones,
    // and no corner anywhere for a hard clamp to show as a crease.
    // A KNEE, not an asymptote. ADR-055's `off * L/(len+L)` is smooth but scales everything, even
    // a tiny lean, so it moves frames that were never in danger -- measured, it changed 839,162
    // channels of the shipped hero. This is exactly 1 below 0.7 of the limit, so a scene that never
    // approached the ceiling is untouched, and saturates to the limit above it with a smoothstep
    // across the join so there is no crease where the two halves meet.
    let bend = length(off);
    let s = bend / kMeshBendLimit;
    if (s > 0.7) {
        let hard = 1.0 / max(s, 1.0e-6);
        off = off * mix(1.0, hard, smoothstep(0.7, 1.4, s));
    }
    var disp = vec3<f32>(off.x, 0.0, off.y) * (strength * height);
    // A limb that bends keeps its length, so the tip drops. Without this the crown shears sideways,
    // which is the classic fake-wind read.
    let travel = length(disp);
    disp.y = disp.y - 0.5 * travel * travel / max(height * max(h, 0.05), 1e-4);
    return disp;
}

@vertex
fn vs_main(in: VertexIn) -> VertexOut {
    var out: VertexOut;
    var world = object.model * vec4<f32>(in.position, 1.0);
    world = vec4<f32>(world.xyz + meshWindOffset(world.xyz, frame.params.x), world.w);
    out.clip = frame.viewProj * world;
    out.worldPos = world.xyz;
    out.normal = normalize((object.normalMatrix * vec4<f32>(in.normal, 0.0)).xyz);
    out.uv = in.uv;
    out.localPos = in.position;
    // The velocity target needs where this vertex was, which for a swaying mesh is not where the
    // previous model matrix alone puts it (ADR-035).
    let prevWorld = object.prevModel * vec4<f32>(in.position, 1.0);
    let prevMoved = prevWorld.xyz + meshWindOffset(prevWorld.xyz, object.windTune.w);
    out.prevClip = frame.prevViewProj * vec4<f32>(prevMoved, prevWorld.w);
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

// ADR-376. Tree energy and canopy shimmer: what the tree does when it is not moving.
//
// Both are emissive terms, because the brief asks for the tree to "conduct energy" and to "feel
// alive even when stationary", and neither needs geometry. They share one function because they
// share one body frame and one early-out, and evaluating that frame twice for two effects on the
// same 3.16M-triangle asset is a cost with nothing to show for it.
//
// ENERGY is a travelling pulse: a band of brightness that leaves the roots and climbs to the
// canopy. `h` is height along the body, so the pulse is `h - t * speed` wrapped -- which makes it
// a function of position and time and nothing else, so scrubbing and playing agree (ADR-091). The
// four tier shares let an artist say where it is allowed to show; the brief asks for roots, trunk,
// branches and canopy to be separately controllable and this is that, over the same radial/height
// proxy the wind uses rather than a topology the GLB does not carry.
//
// SHIMMER is deliberately NOT a brightness pulse. The brief is explicit that the canopy must not
// simply flash: it asks for a travelling wave that is spatially coherent. So it is a low-frequency
// noise field advected across the crown, weighted to the foliage, at an amplitude that is small by
// default -- the point is that the canopy is never quite still, not that it twinkles.
fn treeEnergyAt(worldPos: vec3<f32>) -> vec3<f32> {
    if (object.energy0.x <= 0.0 && object.energy3.x <= 0.0) {
        return vec3<f32>(0.0);
    }
    let root = object.windOrigin.xyz;
    let invHeight = object.windOrigin.w;
    let h = clamp((worldPos.y - root.y) * invHeight, 0.0, 1.0);
    let r = clamp(length(worldPos.xz - root.xz) * object.windShape.x, 0.0, 1.0);
    let t = frame.params.x;

    // Which tier this point belongs to, as four overlapping weights rather than a hard partition:
    // a hard one would draw a visible line across the trunk where two shares met.
    let wRoot = (1.0 - smoothstep(0.0, 0.18, h)) * object.energy1.x;
    let wTrunk = (1.0 - smoothstep(0.08, 0.55, h)) * smoothstep(0.0, 0.12, h) * object.energy1.y;
    let wBranch = smoothstep(0.15, 0.6, h) * (1.0 - smoothstep(0.55, 0.95, h)) * object.energy1.z;
    let wCanopy = smoothstep(0.45, 0.9, h) * smoothstep(0.15, 0.55, r) * object.energy1.w;

    var lit = vec3<f32>(0.0);

    if (object.energy0.x > 0.0) {
        // The travelling band. `fract` on (h - t*speed) makes it repeat up the trunk; the pulse
        // width is how much of that cycle is lit, and the falloff is squared so the band has a
        // bright core and a soft tail rather than a hard edge at both ends.
        let phase = fract(h - t * object.energy0.w);
        let width = max(object.energy0.z, 1.0e-3);
        let band = max(1.0 - phase / width, 0.0);
        var pulse = band * band;
        // A slow breathing of the whole conduction, so the tree does not pulse like a metronome.
        pulse = pulse * (0.55 + 0.45 * sin(t * object.energy0.y * WIND_TAU));
        // Noise, so the current is uneven the way a living thing is.
        if (object.energy2.x > 0.0) {
            let n = hash21(floor(worldPos.xz * object.energy2.y) + vec2<f32>(0.0, floor(t * object.energy2.z)));
            pulse = pulse * mix(1.0, n, clamp(object.energy2.x, 0.0, 1.0));
        }
        let tier = wRoot + wTrunk + wBranch + wCanopy;
        // Colour travels with height: the near colour at the roots, the far one at the crown, which
        // is what makes it read as a current arriving rather than as a region brightening.
        let tint = mix(object.energyA.rgb, object.energyB.rgb, h);
        lit = lit + tint * (pulse * tier * object.energy0.x);
    }

    if (object.energy3.x > 0.0) {
        // A spatially coherent wave across the crown. Two octaves at incommensurate rates so the
        // pattern never repeats on any interval an eye can latch onto, advected along +X+Z so it
        // travels rather than throbs in place.
        let drift = t * object.energy3.y;
        let q = worldPos.xz * object.energy3.z + vec2<f32>(drift, drift * 0.73);
        let a = hash21(floor(q));
        let b = hash21(floor(q * 2.17 + vec2<f32>(19.0, 7.0)));
        let wave = mix(a, b, 0.4);
        // Centred on zero, so the shimmer takes light away as often as it adds it. A one-sided
        // shimmer is a brightening, which is the thing the brief says not to do.
        let signedWave = (wave - 0.5) * 2.0;
        let variation = mix(1.0, hash21(floor(worldPos.xz * 0.5)), clamp(object.energy3.w, 0.0, 1.0));
        lit = lit + object.energyB.rgb * (signedWave * wCanopy * object.energy3.x * variation);
    }
    return max(lit, vec3<f32>(0.0));
}


// ADR-567: the layer's profile and its antiderivative moved to `height_fog.wgsl`, unchanged, so
// that the march and this pass call the same two functions and a test can compile them with no
// uniform buffers. Included HERE and nowhere else -- `volume.wgsl` reaches them through its own
// include of this file, and a second include would compile two copies into one module.
#include "height_fog.wgsl"

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
        let upper = frame.fogShape.x;
        let curve = frame.fogShape.y;
        if (max(y0, y1) > 0.0) {
            // Both endpoints below the layer's top puts the whole segment below it, so the mean is
            // exactly one and this branch is skipped -- which is what keeps a scene that sits
            // inside its own fog bank bit-identical when the integration is switched on. Leaving
            // it to the quotient would give 1.0 only to within rounding, because the numerator and
            // the denominator are the same subtraction written twice and the compiler is free to
            // fuse one of them and not the other.
            mean = fogHeightProfile(y0, falloff, upper, curve); // a level ray never leaves its altitude
            if (abs(rise) > 1e-3) {
                mean = (fogHeightIntegral(y1, falloff, upper, curve) -
                        fogHeightIntegral(y0, falloff, upper, curve)) / rise;
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
