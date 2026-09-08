/*{
  "NAME": "Feedback Trails",
  "DESCRIPTION": "A beat-driven blob leaves decaying trails in a persistent half-resolution buffer.",
  "CREDIT": "avgen examples",
  "CATEGORIES": ["Generator", "Feedback", "Audio Reactive"],
  "INPUTS": [
    {"NAME": "decay", "TYPE": "float", "DEFAULT": 0.95, "MIN": 0.8, "MAX": 0.999, "LABEL": "Trail decay"},
    {"NAME": "radius", "TYPE": "float", "DEFAULT": 0.06, "MIN": 0.01, "MAX": 0.3, "LABEL": "Blob radius"},
    {"NAME": "orbit", "TYPE": "point2D", "DEFAULT": [0.3, 0.25], "MIN": [0.0, 0.0], "MAX": [0.5, 0.5], "LABEL": "Orbit"},
    {"NAME": "blobColor", "TYPE": "color", "DEFAULT": [0.3, 0.8, 1.0, 1.0], "LABEL": "Blob colour"},
    {"NAME": "clear", "TYPE": "event", "LABEL": "Clear trails"}
  ],
  "PASSES": [
    {"TARGET": "trail", "PERSISTENT": true, "FLOAT": true, "WIDTH": "$WIDTH/2", "HEIGHT": "$HEIGHT/2"},
    {}
  ]
}*/

// Pass 0 (std.passIndex == 0) renders into `trail`, which persists across frames so the shader can
// sample its own previous contents. Pass 1 renders `trail` to the output.
// std.audio2 = (lowMid, highMid, onset, beatPhase); beatPhase runs 0..1 between beats.

fn blobPosition() -> vec2<f32> {
    // The blob jumps around the orbit ellipse once per beat and eases in on each beat.
    let phase = std.audio2.w;
    let eased = 1.0 - (1.0 - phase) * (1.0 - phase);
    let beatIndex = std.beat.y;
    let angle = (beatIndex + eased) * 1.618 * 6.28318;
    return vec2<f32>(0.5, 0.5) + inputs.orbit * vec2<f32>(cos(angle), sin(angle));
}

fn trailPass(uv: vec2<f32>) -> vec4<f32> {
    // Sample the previous frame slightly zoomed toward the centre so trails drift inward.
    let zoomUv = (uv - vec2<f32>(0.5, 0.5)) * 0.995 + vec2<f32>(0.5, 0.5);
    var previous = textureSample(trail, linearSampler, zoomUv).rgb * inputs.decay;
    previous = previous * (1.0 - inputs.clear); // the event flashes to 1 for one frame

    let aspect = std.passSize.x / max(std.passSize.y, 1.0);
    let d = (uv - blobPosition()) * vec2<f32>(aspect, 1.0);
    let dist = length(d);
    let onset = std.audio2.z;
    let flash = 1.0 + 2.0 * onset;
    let blob = smoothstep(inputs.radius, inputs.radius * 0.3, dist) * flash;

    let rgb = max(previous, inputs.blobColor.rgb * blob);
    return vec4<f32>(rgb, 1.0);
}

fn outputPass(uv: vec2<f32>) -> vec4<f32> {
    let trails = textureSample(trail, linearSampler, uv).rgb;
    // Mild tone curve so bright FLOAT trails do not clip harshly.
    let mapped = trails / (vec3<f32>(1.0, 1.0, 1.0) + trails);
    return vec4<f32>(mapped, 1.0);
}

fn mainImage(uv: vec2<f32>, fragCoord: vec2<f32>) -> vec4<f32> {
    if (std.passIndex < 0.5) {
        return trailPass(uv);
    }
    return outputPass(uv);
}
