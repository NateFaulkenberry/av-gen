/*{
    "INPUTS": [
        { "NAME": "pitch", "TYPE": "float", "DEFAULT": 4.0, "MIN": 2.0, "MAX": 16.0 },
        { "NAME": "viewerX", "TYPE": "float", "DEFAULT": 0.1, "MIN": -1.0, "MAX": 1.0 },
        { "NAME": "views", "TYPE": "float", "DEFAULT": 16.0, "MIN": 2.0, "MAX": 32.0 }
    ]
}*/
// COST STAND-IN ONLY (Cursed Slammers POC 2). The live lens lookup would read N cached views from a texture array,
// which a shader layer cannot declare today (no image inputs; the gap ADR-1690 would close). This pass does the
// same arithmetic and the same number of texture taps (two neighbouring views + two ghost taps, at the lens centre)
// against inputImage, so its GPU time prices the lookup. Its picture is meaningless.
fn mainImage(uv: vec2<f32>, fragCoord: vec2<f32>) -> vec4<f32> {
    let res = vec2<f32>(textureDimensions(inputImage));
    let lens = floor(fragCoord.x / inputs.pitch);
    let xc = (lens + 0.5) * inputs.pitch / res.x;          // lens centre: the lens magnifies one strip
    let xj = xc - 0.5;
    let theta = atan((inputs.viewerX - xj) / 4.0);
    let s = inputs.views * (0.5 + tan(theta) / (2.0 * 0.105)) - 0.5;
    let f = fract(s);
    let w = smoothstep(0.1, 0.9, f);
    let o = 1.0 / res.x;
    let a = textureSampleLevel(inputImage, linearSampler, vec2<f32>(xc, uv.y), 0.0);
    let b = textureSampleLevel(inputImage, linearSampler, vec2<f32>(xc + o, uv.y), 0.0);
    let g0 = textureSampleLevel(inputImage, linearSampler, vec2<f32>(xc - o, uv.y), 0.0);
    let g1 = textureSampleLevel(inputImage, linearSampler, vec2<f32>(xc + 2.0 * o, uv.y), 0.0);
    let u = fract(fragCoord.x / inputs.pitch);
    let phi = asin((u - 0.5) * 1.7);
    let glint = exp(-pow((phi - theta) / 0.07, 2.0));
    let c = mix(a, b, w) * 0.88 + 0.06 * (g0 + g1) + vec4<f32>(0.2 * glint);
    return vec4<f32>(c.rgb, 1.0);
}
