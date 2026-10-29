// ADR-1820: one level of the glossy SSR's min-depth pyramid from the level below it. Each texel takes the nearest
// (smallest) linear view depth of the 2x2 texels it covers. The pyramid is sized so every level halves exactly (its
// first level is the depth's size rounded up to a multiple of 2^levels, halved), so a texel of level k covers exactly
// 2^k x 2^k pixels of the depth; a read past the depth's edge takes the edge texel.

@group(0) @binding(0) var source: texture_2d<f32>;

@vertex
fn vs_hiz(@builtin(vertex_index) index: u32) -> @builtin(position) vec4<f32> {
    var positions = array<vec2<f32>, 3>(vec2<f32>(-1.0, -1.0), vec2<f32>(3.0, -1.0), vec2<f32>(-1.0, 3.0));
    return vec4<f32>(positions[index], 0.0, 1.0);
}

@fragment
fn fs_hiz(@builtin(position) clip: vec4<f32>) -> @location(0) f32 {
    let last = vec2<i32>(textureDimensions(source, 0)) - vec2<i32>(1);
    let base = vec2<i32>(clip.xy) * 2;
    let a = textureLoad(source, min(base, last), 0).r;
    let b = textureLoad(source, min(base + vec2<i32>(1, 0), last), 0).r;
    let c = textureLoad(source, min(base + vec2<i32>(0, 1), last), 0).r;
    let d = textureLoad(source, min(base + vec2<i32>(1, 1), last), 0).r;
    return min(min(a, b), min(c, d));
}
