// The half-resolution screen-space shadow mask (ADR-087).
//
// One fullscreen pass over the linear depth target that the depth prepass already resolved. For
// each of the leading directional lights (at most three) it computes the *shadow map* term the lit
// pass would have computed -- `shadowFactor` in shadows.wgsl: the cascaded PCSS lookup with its
// crossfade for the key light, plain PCF for the others -- and writes it to one colour channel.
// The lit pass then reads the four nearest texels and upsamples them bilaterally instead of doing
// a cascade lookup per pixel. At the editor's 3.36 MP canvas that lookup is 34% of the scene pass.
//
// **The contact march is deliberately not here, and that is a measurement, not an oversight.**
// A penumbra survives being computed at half resolution; that is the whole reason this works. A
// screen-space contact shadow does not. Glowmere's ground is a field of grass and reed cards, and
// a march evaluated once per 2x2 applies whatever it hit to all four pixels -- every contact
// shadow doubles in width, and dense ground cover turns black. Measured at 2880x1166: masking both
// terms moved 37% of the frame's pixels and visibly blackened the near ground; masking the map
// term alone moves 15% and is indistinguishable from the unmasked frame at a glance. The march
// stays in the lit pass, at full resolution, where it always was (ADR-034).
//
// Output: rgb = the shadow-map visibility of directional lights 0, 1, 2; a = the view depth this
// was computed at, which is the bilateral filter's rejection key.
//
// Bind groups: group 0 is SceneRenderer's frame group with the mask binding replaced by a
// placeholder (a pass cannot read what it writes) and the real shadow atlas and linear depth
// bound, because the terms this pass computes read both through the frame group exactly as the
// lit pass does. Group 1 is this pass's own and holds nothing but ShadowMaskUniforms.
#include "common.wgsl"
#include "shadows.wgsl"

struct ShadowMaskUniforms {
    sizes: vec4<f32>,      // mask width, height, 1 / width, 1 / height
    fullSize: vec4<f32>,   // scene width, height, 1 / width, 1 / height
    projection: vec4<f32>, // tan(fovY/2) * aspect, tan(fovY/2), near, far
    params: vec4<f32>,     // x = directional lights covered, y = PCF taps, zw = 0
};

@group(1) @binding(0) var<uniform> mask: ShadowMaskUniforms;

struct FsIn {
    @builtin(position) clip: vec4<f32>,
    @location(0) uv: vec2<f32>,
};

@vertex
fn vs_fullscreen(@builtin(vertex_index) index: u32) -> FsIn {
    var positions = array<vec2<f32>, 3>(vec2<f32>(-1.0, -1.0), vec2<f32>(3.0, -1.0), vec2<f32>(-1.0, 3.0));
    let p = positions[index];
    var out: FsIn;
    out.clip = vec4<f32>(p, 0.0, 1.0);
    out.uv = vec2<f32>(p.x * 0.5 + 0.5, 0.5 - p.y * 0.5);
    return out;
}

// The full-resolution linear depth the prepass resolved, read through the frame group -- the same
// binding, and the same texel, the contact march below reads.
//
// Everything here is addressed by *integer full-resolution texel*, never by the mask texel's own
// uv, and that is load-bearing. A mask texel's centre does not coincide with the centre of any
// full-resolution texel: reconstruct a position from the mask's uv and a depth fetched at
// `floor(uv * fullSize)` and the two disagree by half a full-resolution pixel along the view ray.
// On ground running away from the camera half a pixel of ray is several centimetres of height, so
// the reconstructed point sits *below* the surface the depth buffer holds -- and the contact march
// starting there reads the ground itself as its own occluder. That was visible as vertical streaks
// of shadow down every grazing surface in the frame, and it is why the pair (texel, its own centre
// uv) is carried around together rather than either alone.
fn maskDepthTexel(texel: vec2<f32>) -> f32 {
    return textureLoad(sceneLinearDepth, vec2<i32>(texel), 0).r;
}

// View-space position of a screen point at view depth `d` (the camera looks down -Z).
fn maskViewPosition(uv: vec2<f32>, d: f32) -> vec3<f32> {
    let ndc = vec2<f32>(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    return vec3<f32>(ndc.x * mask.projection.x * d, ndc.y * mask.projection.y * d, -d);
}

// The view-space position of one full-resolution texel, reconstructed along the ray through its
// own centre.
fn maskPositionAt(texel: vec2<f32>) -> vec3<f32> {
    let clamped = clamp(texel, vec2<f32>(0.0), mask.fullSize.xy - vec2<f32>(1.0));
    let uv = (clamped + vec2<f32>(0.5)) * mask.fullSize.zw;
    return maskViewPosition(uv, maskDepthTexel(clamped));
}

fn maskViewToWorld(v: vec3<f32>) -> vec3<f32> {
    return frame.cameraRight.xyz * v.x + frame.cameraUp.xyz * v.y - frame.cameraForward.xyz * v.z;
}

// Best-fit normal from the depth buffer: the closer neighbour on each axis, so a silhouette does
// not smear one surface's normal across another. This pass runs before the scene pass, so the
// normal+roughness target does not exist yet and there is nothing else to read.
//
// The orientation comes from the *winding* of the two screen-space differences, never from a test
// against the view direction. gtao.wgsl orients its own reconstruction with `normal.z < 0`, and on
// a surface the camera sees nearly edge-on -- which is most of the ground in a landscape -- that
// component is within rounding of zero and the test picks a side at random. It picked the wrong
// one here: the ground's normal came back pointing straight down, so the shadow lookup's normal
// offset pushed its sample point *into* the ground instead of off it, and the frame filled with
// bands of acne aligned to the shadow map's texel grid. Two screen-space differences taken in a
// known order cannot be ambiguous: `dx` runs along +x in view space and `dy` along -y, so their
// cross product in that order is the side facing away from the camera and `cross(dy, dx)` is the
// side facing it, whatever the surface's angle.
//
// The normal is used only for bias here (the normal offset of the shadow lookup and the origin
// offset of the contact march), never for shading, so a reconstruction that is a degree or two out
// on a curved surface changes nothing visible. Where it is badly wrong -- one pixel of a leaf
// against the sky -- the fragment's own shadow term is being multiplied into a sub-pixel element,
// and the bilateral upsample is already looking for a better tap.
fn maskDepthNormal(texel: vec2<f32>, p: vec3<f32>) -> vec3<f32> {
    let left = maskPositionAt(texel - vec2<f32>(1.0, 0.0));
    let right = maskPositionAt(texel + vec2<f32>(1.0, 0.0));
    let down = maskPositionAt(texel - vec2<f32>(0.0, 1.0));
    let up = maskPositionAt(texel + vec2<f32>(0.0, 1.0));
    let dx = select(right - p, p - left, abs(left.z - p.z) < abs(right.z - p.z));
    let dy = select(up - p, p - down, abs(down.z - p.z) < abs(up.z - p.z));
    let n = cross(dy, dx);
    if (dot(n, n) < 1e-12) {
        return vec3<f32>(0.0, 0.0, 1.0); // degenerate: face the camera and move on
    }
    return normalize(n);
}

@fragment
fn fs_shadow_mask(in: FsIn) -> @location(0) vec4<f32> {
    // The full-resolution texel this mask texel stands on, and the only screen position anything
    // below is reconstructed from.
    let texel = clamp(floor(in.uv * mask.fullSize.xy), vec2<f32>(0.0), mask.fullSize.xy - vec2<f32>(1.0));
    let centreUv = (texel + vec2<f32>(0.5)) * mask.fullSize.zw;
    let depth = maskDepthTexel(texel);
    // Nothing the prepass drew: the sky, or a frame with no depth at all. Unshadowed, and the
    // depth is written through so the upsample rejects it for any surface that does exist.
    if (depth <= 0.0 || depth >= mask.projection.w * 0.999) {
        return vec4<f32>(1.0, 1.0, 1.0, depth);
    }
    let viewPos = maskViewPosition(centreUv, depth);
    let normal = normalize(maskViewToWorld(maskDepthNormal(texel, viewPos)));
    let worldPos = frame.cameraPos.xyz + maskViewToWorld(viewPos);

    // The same per-pixel dither the lit pass uses, indexed by the *full-resolution* pixel this
    // texel sits on, so the PCF rotation stays in phase with the one the unmasked path would have
    // picked. Deterministic in the pixel, never in a clock.
    let rotation = gradientNoise(centreUv * frame.targetSize.xy) * 6.28318531;

    // More taps than a shading pass would use. At a quarter of the pixels the mask can afford
    // them, and it needs them: see the note on pcf() in shadows.wgsl.
    let taps = u32(clamp(mask.params.y, 1.0, 32.0));
    let covered = min(u32(mask.params.x + 0.5), 3u);
    var out = vec3<f32>(1.0, 1.0, 1.0);
    for (var i = 0u; i < covered; i = i + 1u) {
        let light = sceneLights[i];
        let toLight = -light.directionRange.xyz;
        let v = shadowFactor(light, worldPos, normal, toLight, depth, rotation, taps);
        if (i == 0u) {
            out.r = v;
        } else if (i == 1u) {
            out.g = v;
        } else {
            out.b = v;
        }
    }
    return vec4<f32>(out, depth);
}
