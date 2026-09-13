// The reference renderer's only shader (renderer forensics Phase 2.2).
//
// Deliberately the simplest thing that can put geometry in the right pixels: model-view-projection,
// a solid colour, and a single hard-coded headlight so a silhouette has some shape in it. No
// lighting model, no shadows, no image-based lighting, no tone mapping, no temporal anything.
//
// It exists to be *compared against*, so every line it does not have is a line that cannot explain a
// difference. If the production renderer and this one disagree about which pixels a triangle covers,
// the disagreement is in the transform chain or the camera -- there is nothing else here for it to
// be.

struct Object {
    mvp: mat4x4<f32>,
    model: mat4x4<f32>,
    color: vec4<f32>,
};

@group(0) @binding(0) var<uniform> object: Object;

struct VertexOut {
    @builtin(position) clip: vec4<f32>,
    @location(0) normal: vec3<f32>,
};

@vertex
fn vs(@location(0) position: vec3<f32>, @location(1) normal: vec3<f32>) -> VertexOut {
    var out: VertexOut;
    out.clip = object.mvp * vec4<f32>(position, 1.0);
    // The normal matrix is the model matrix here: the reference path does not support non-uniform
    // scale correctly and does not pretend to. Shading is a readability aid, not a claim.
    out.normal = normalize((object.model * vec4<f32>(normal, 0.0)).xyz);
    return out;
}

@fragment
fn fs(in: VertexOut) -> @location(0) vec4<f32> {
    let light = normalize(vec3<f32>(0.3, 0.8, 0.5));
    let lambert = clamp(dot(normalize(in.normal), light), 0.0, 1.0);
    return vec4<f32>(object.color.rgb * (0.25 + 0.75 * lambert), 1.0);
}
