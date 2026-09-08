/*{
  "NAME": "Plasma",
  "DESCRIPTION": "Classic sine plasma that breathes with the bass.",
  "CREDIT": "avgen examples",
  "CATEGORIES": ["Generator", "Audio Reactive"],
  "INPUTS": [
    {"NAME": "speed", "TYPE": "float", "DEFAULT": 1.0, "MIN": 0.0, "MAX": 5.0, "LABEL": "Speed"},
    {"NAME": "scale", "TYPE": "float", "DEFAULT": 3.0, "MIN": 0.5, "MAX": 12.0, "LABEL": "Scale"},
    {"NAME": "tint", "TYPE": "color", "DEFAULT": [1.0, 0.6, 0.3, 1.0], "LABEL": "Tint"},
    {"NAME": "bassPulse", "TYPE": "float", "DEFAULT": 0.5, "MIN": 0.0, "MAX": 2.0, "LABEL": "Bass pulse"}
  ]
}*/

// Available from the generated prologue: std (Std), inputs (Inputs), linearSampler, inputImage,
// audioSpectrum. std.audio = (rms, bass, mid, treble).

fn palette(t: f32) -> vec3<f32> {
    // Cosine palette (Inigo Quilez) tinted by the user colour.
    let a = vec3<f32>(0.5, 0.5, 0.5);
    let b = vec3<f32>(0.5, 0.5, 0.5);
    let c = vec3<f32>(1.0, 1.0, 1.0);
    let d = vec3<f32>(0.0, 0.33, 0.67);
    return a + b * cos(6.28318 * (c * t + d));
}

fn mainImage(uv: vec2<f32>, fragCoord: vec2<f32>) -> vec4<f32> {
    let aspect = std.passSize.x / max(std.passSize.y, 1.0);
    let p = (uv - vec2<f32>(0.5, 0.5)) * vec2<f32>(aspect, 1.0) * inputs.scale;
    let t = std.time * inputs.speed;

    // Bass (0..1) pushes the pattern outward for a pulsing feel.
    let bass = std.audio.y;
    let pulse = 1.0 + bass * inputs.bassPulse;

    var v = 0.0;
    v = v + sin(p.x * pulse + t);
    v = v + sin((p.y * pulse + t) * 0.7);
    v = v + sin((p.x + p.y) * 0.5 + t * 1.3);
    let cx = p.x + 0.5 * sin(t * 0.4);
    let cy = p.y + 0.5 * cos(t * 0.3);
    v = v + sin(sqrt(cx * cx + cy * cy + 0.05) * 2.0 * pulse - t);
    v = v * 0.25; // -1..1

    let rgb = palette(v * 0.5 + 0.5) * inputs.tint.rgb;
    let bright = 0.85 + 0.3 * bass;
    return vec4<f32>(rgb * bright, inputs.tint.a);
}
