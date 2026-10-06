// What the background pass draws behind the world, as a function of direction. Split out of
// skybox.wgsl (ADR-918) so the surface fog's view of the sky (fog_sky.wgsl) reads the sky through
// the same code the camera sees it through: the way two readers of one sky go wrong is two
// implementations drifting, which is ADR-345's warning about the analytic sky below.
//
// Include after common.wgsl. It declares the IBL group (group 3) exactly as the scene pipeline
// layout has it.

@group(3) @binding(0) var iblSampler: sampler;
@group(3) @binding(1) var irradianceMap: texture_cube<f32>;
@group(3) @binding(2) var prefilteredMap: texture_cube<f32>;
@group(3) @binding(3) var brdfLut: texture_2d<f32>;
@group(3) @binding(4) var skyEquirect: texture_2d<f32>;
@group(3) @binding(5) var skySampler: sampler; // as iblSampler, but wrapping in longitude

const SKY_PI: f32 = 3.14159265;

// ADR-345: the analytic sky, evaluated here rather than sampled from a cube.
//
// This is shaders/environment.wgsl's `skyRadiance` reading the frame block instead of the
// environment processor's, because the two passes cannot see each other's uniforms. It is
// deliberately the same maths: the sky the camera sees and the sky the IBL was built from have to
// be the same sky, and the way that goes wrong is two implementations drifting. The one difference
// is the trailing intensity multiply, which is left to the caller here -- the background pass
// already applies `skyExtra.z`, the visible sky's own intensity, and applying both would square it.
fn skySmoothstepF(e0: f32, e1: f32, x: f32) -> f32 {
    if (e0 == e1) { return select(1.0, 0.0, x < e0); }
    let t = saturate((x - e0) / (e1 - e0));
    return t * t * (3.0 - 2.0 * t);
}

fn skyRadianceFrame(dir: vec3<f32>, minRadius: f32) -> vec3<f32> {
    return skyRadianceFrameScaled(dir, minRadius, 1.0);
}

// ADR-1070: the same sky with the sun's intensity applied, as the cube applies it, for the live
// background of a procedural-sky IBL (the ADR-345 caller above has never applied it).
// ADR-1167: the sky above the horizon along d (gradient and sun, no band), for the mirror below it.
fn skyAboveFrame(d: vec3<f32>, minRadius: f32, sunScale: f32) -> vec3<f32> {
    let haze = exp(-saturate(d.y) / max(frame.skyZenithColor.w, 1e-3));
    let gradient = mix(frame.skyZenithColor.rgb, frame.skyHorizonColor.rgb, haze);
    let theta = acos(clamp(dot(d, frame.skySun.xyz), -1.0, 1.0));
    let sunRadius = max(frame.skyHorizonColor.w, 1e-3);
    let radius = max(sunRadius, max(minRadius, 0.0));
    let energy = (sunRadius / radius) * (sunRadius / radius);
    let disc = (1.0 - skySmoothstepF(radius * 0.85, radius * 1.15, theta)) * energy;
    let glow = exp(-theta / max(frame.skyGroundColor.w, 1e-3)) * 0.02;
    return gradient + frame.skySunRadiance.rgb * sunScale * (disc + glow);
}

fn skyRadianceFrameScaled(dir: vec3<f32>, minRadius: f32, sunScale: f32) -> vec3<f32> {
    let d = normalize(dir);
    let hazeWidth = max(frame.skyZenithColor.w, 1e-3);
    let haze = exp(-saturate(d.y) / hazeWidth);
    let gradient = mix(frame.skyZenithColor.rgb, frame.skyHorizonColor.rgb, haze);
    let band = skySmoothstepF(-0.03, 0.03, d.y);
    let base = mix(frame.skyGroundColor.rgb, gradient, band);

    let cosTheta = clamp(dot(d, frame.skySun.xyz), -1.0, 1.0);
    let theta = acos(cosTheta);
    let sunRadius = max(frame.skyHorizonColor.w, 1e-3);
    let radius = max(sunRadius, max(minRadius, 0.0));
    let energy = (sunRadius / radius) * (sunRadius / radius);
    let disc = (1.0 - skySmoothstepF(radius * 0.85, radius * 1.15, theta)) * energy;
    let glow = exp(-theta / max(frame.skyGroundColor.w, 1e-3)) * 0.02; // SKY_AUREOLE, scene/sky.cpp
    let sun = frame.skySunRadiance.rgb * sunScale * (disc + glow) * band;
    var mirrored = vec3<f32>(0.0);
    if (frame.skyMirror.x > 0.0) { // ADR-1167: below the horizon, the sky above it
        mirrored = min(frame.skyMirror.x, 1.0) * (1.0 - band) *
                   (skyAboveFrame(vec3<f32>(d.x, -d.y, d.z), minRadius, sunScale) - frame.skyGroundColor.rgb);
    }
    return base + sun + mirrored;
}

// The parameterisation shaders/environment.wgsl builds the cube with. One definition, so the sky
// you see and the lighting made from it cannot end up rotated differently.
fn skyEquirectUv(dir: vec3<f32>) -> vec2<f32> {
    let phi = atan2(dir.z, dir.x);
    let theta = acos(clamp(dir.y, -1.0, 1.0));
    return vec2<f32>(0.5 + phi / (2.0 * SKY_PI), theta / SKY_PI);
}

// Whether a sky stands behind the world at all, or the flat background colour. A function of the
// frame block alone, and kept apart from `skyBackgroundAt` on purpose: the background pass gates
// derivative-taking star fields on it, and WGSL's uniformity analysis only allows that while the
// gate depends on uniforms -- a flag returned from a call that was handed a derivative-derived LOD
// would taint it (the offline tint check caught exactly that).
fn skyBackgroundIsSky() -> bool {
    // ADR-036: a procedural sky is an IBL source first; it only stands behind the scene when the
    // scene asks for it (skyExtra.y), so existing looks keep their flat background.
    return frame.envParams.w >= 0.5 && frame.skyExtra.y >= 0.5;
}

// What the background pass draws in direction `dir` (world space, before the environment's
// rotation) ahead of its stars and its crisp moon disc: the flat background colour, or the sky the
// IBL was built from at its visible intensity (ADR-036, ADR-049, ADR-345). `equirectLod` is the
// HDRI's mip for this ray and `minRadius` the analytic sun disc's floor; both are the caller's,
// because both depend on how finely the caller samples the sky -- the background pass takes them
// from its screen derivatives, the fog's map from its own texel.
fn skyBackgroundAt(dir: vec3<f32>, equirectLod: f32, minRadius: f32) -> vec3<f32> {
    var color = frame.skyParams.rgb;
    if (skyBackgroundIsSky()) {
        let d = envRotate(dir);
        if (frame.skySunRadiance.w >= 0.5) {
            // ADR-345: an HDRI is lighting the scene and the scene has asked for the procedural
            // sky behind it. One pixel of sky is a few ALU here against a cube fetch, and it is
            // the only way to get a background whose colours move with a day/night cycle while
            // the lighting comes from a map.
            color = skyRadianceFrame(d, minRadius);
        } else if (frame.skyExtra.x >= 0.5) {
            // The analytic sky has no map behind it; the prefiltered cube is its only form, and
            // its own `intensity` (params.w) is what has always scaled it.
            if (frame.skyLive.x >= 0.5) {
                // ADR-1070, live: this frame's sky, not the cube its lighting is rebuilt into at a
                // capped rate. The cube's own factors: the sun's intensity, the sky's intensity, and
                // params.w on top as below; the disc floored as the source cube floors it.
                color = skyRadianceFrameScaled(d, max(minRadius, frame.skyLive.w), frame.skyLive.y)
                        * frame.skyLive.z * frame.params.w;
            } else {
                let mip = frame.skyParams.w * frame.envParams.y;
                color = textureSampleLevel(prefilteredMap, iblSampler, d, mip).rgb * frame.params.w;
            }
        } else {
            // ADR-049: read the HDRI itself. A 128 px prefiltered cube face is under 3 texels per
            // degree; a star is one texel of an 8K map and does not survive being resampled to
            // that, which is why the visible sky does not share the IBL's cube.
            color = textureSampleLevel(skyEquirect, skySampler, skyEquirectUv(d), equirectLod).rgb;
        }
        // ADR-049: the visible sky's own intensity. Shading multiplies by params.w instead, so a
        // dark sky can still cast a useful amount of light and vice versa.
        color *= frame.skyExtra.z;
    }
    return color;
}
