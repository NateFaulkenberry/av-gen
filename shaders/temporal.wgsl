// Temporal media: the ring's capture pass and the effects that read it (ADR-410, brief §8-§17).
//
// Two things happen in this file and they must not be confused:
//
//   **Capture** downsamples the frame's signals into the ring's write layer. It runs at history
//   resolution and it captures the *clean* scene radiance -- the HDR target before the post chain,
//   never the post chain's output and never an effect's own result.
//
//   **Effects** read K taps out of the ring and write a full-resolution image.
//
// Capturing the clean signal rather than the displayed one is the whole determinism argument in
// one decision. Feeding an effect's output back into its own history is an IIR filter: frame N
// depends on every frame before it, K is infinite, and no warm-up can rebuild it. Capturing the
// clean frame makes every effect an FIR filter over the last K frames, which a warm-up CAN rebuild
// by re-rendering K frames. ADR-410 calls this "a cache of a pure function"; this is where it is
// actually true or false.

struct TemporalUniforms {
    // x = history width, y = history height, z = 1/width, w = 1/height
    sizes: vec4<f32>,
    // x = output width, y = output height, z = 1/width, w = 1/height
    outputSize: vec4<f32>,
    // x = ring depth, y = layer the NEXT capture writes, z = frames valid, w = taps to read
    ring: vec4<f32>,
    // x = strength, y = per-tap decay, z = unused, w = unused
    params: vec4<f32>,
};

@group(0) @binding(0) var<uniform> temporal: TemporalUniforms;
@group(0) @binding(1) var linearSampler: sampler;
@group(0) @binding(2) var source: texture_2d<f32>;        // capture: scene HDR / effects: scene HDR
@group(0) @binding(3) var colourHistory: texture_2d_array<f32>;

struct FsIn {
    @builtin(position) clip: vec4<f32>,
    @location(0) uv: vec2<f32>,
};

@vertex
fn vs_fullscreen(@builtin(vertex_index) i: u32) -> FsIn {
    var positions = array<vec2<f32>, 3>(vec2<f32>(-1.0, -1.0), vec2<f32>(3.0, -1.0), vec2<f32>(-1.0, 3.0));
    var out: FsIn;
    let p = positions[i];
    out.clip = vec4<f32>(p, 0.0, 1.0);
    out.uv = vec2<f32>(p.x * 0.5 + 0.5, 1.0 - (p.y * 0.5 + 0.5));
    return out;
}

// ---- capture ---------------------------------------------------------------------------------
//
// A four-tap box at the source's texel centres rather than one bilinear tap. At the 0.5 default
// the ring is exactly half size, so four taps are the four texels a history texel covers: a true
// average instead of a point sample of one of them. It costs three extra taps at a quarter of the
// pixels and removes the sparkle a point-sampled downsample gives a specular highlight, which
// would then be smeared across K frames by whatever reads it.

@fragment
fn fs_capture_colour(in: FsIn) -> @location(0) vec4<f32> {
    let texel = temporal.outputSize.zw;
    let o = texel * 0.5;
    var sum = textureSampleLevel(source, linearSampler, in.uv + vec2<f32>(-o.x, -o.y), 0.0).rgb;
    sum = sum + textureSampleLevel(source, linearSampler, in.uv + vec2<f32>(o.x, -o.y), 0.0).rgb;
    sum = sum + textureSampleLevel(source, linearSampler, in.uv + vec2<f32>(-o.x, o.y), 0.0).rgb;
    sum = sum + textureSampleLevel(source, linearSampler, in.uv + vec2<f32>(o.x, o.y), 0.0).rgb;
    // RG11B10Ufloat is unsigned: a negative radiance (which a bloom or a grade can produce
    // upstream) would wrap to a huge positive value and sit in the ring for K frames as a bright
    // speck. Clamped here, where the format is chosen, rather than trusted from the producer.
    return vec4<f32>(max(sum * 0.25, vec3<f32>(0.0)), 1.0);
}

// ---- §9 frame echo ---------------------------------------------------------------------------
//
// out = current + strength * Σ decay^i * history[i], i = 1..taps
//
// An FIR filter over the ring: bounded, declared, and identical whether the ring was filled by
// playing or by a warm-up. `taps` is clamped to `framesValid` by the CPU side, so a settling
// history produces a shorter echo rather than reading a layer that holds a frame from before the
// seek -- the failure that would put pre-seek geometry in a post-seek frame.
//
// Normalised by the weight sum, so raising the tap count lengthens the tail without brightening
// the image. Unnormalised, "more frames" would read as "more exposure" and every artist would
// compensate with the strength slider.

@fragment
fn fs_echo(in: FsIn) -> @location(0) vec4<f32> {
    let current = textureSampleLevel(source, linearSampler, in.uv, 0.0);
    let taps = i32(temporal.ring.w + 0.5);
    if (taps <= 0) {
        return current;
    }
    let depth = i32(temporal.ring.x + 0.5);
    let writeLayer = i32(temporal.ring.y + 0.5);
    let decay = clamp(temporal.params.y, 0.0, 0.999);

    var accum = vec3<f32>(0.0);
    var weightSum = 0.0;
    var weight = 1.0;
    for (var i = 1; i <= taps; i = i + 1) {
        weight = weight * decay;
        // One implementation of the ring mapping, here, shared by every effect that reads it.
        // `writeLayer` is where the NEXT capture goes, so one frame back is writeLayer - 1.
        let layer = ((writeLayer - i) % depth + depth) % depth;
        let s = textureSampleLevel(colourHistory, linearSampler, in.uv, layer, 0.0).rgb;
        accum = accum + s * weight;
        weightSum = weightSum + weight;
    }
    if (weightSum <= 1.0e-6) {
        return current;
    }
    let echo = accum / weightSum;
    let strength = clamp(temporal.params.x, 0.0, 1.0);
    return vec4<f32>(current.rgb + echo * strength, current.a);
}

// ---- §53 debug view: history state ------------------------------------------------------------
//
// Not a prettier echo -- a picture of the ring itself. The frame is tiled with the ring's layers
// in order, so "is the history filling", "is a layer stale" and "did the seek actually clear it"
// are things to look at rather than infer from a counter. The valid layers are shown as they are;
// the layers beyond `framesValid` are tinted red, because an empty layer and a black frame are
// otherwise identical to the eye -- the same trap as a hash over a nearly-black frame.

@fragment
fn fs_debug_history(in: FsIn) -> @location(0) vec4<f32> {
    let depth = max(i32(temporal.ring.x + 0.5), 1);
    // A roughly square tiling, wide enough for 32 layers in 6 columns.
    var cols = 1;
    loop {
        if (cols * cols >= depth || cols >= 8) { break; }
        cols = cols + 1;
    }
    let rows = (depth + cols - 1) / cols;
    let cell = vec2<f32>(in.uv.x * f32(cols), in.uv.y * f32(rows));
    let cx = i32(floor(cell.x));
    let cy = i32(floor(cell.y));
    let index = cy * cols + cx;
    if (index >= depth) {
        return vec4<f32>(0.02, 0.02, 0.02, 1.0);
    }
    let writeLayer = i32(temporal.ring.y + 0.5);
    let valid = i32(temporal.ring.z + 0.5);
    // Counted backwards from the write cursor, so tile 0 (top left) is the MOST RECENT captured
    // frame and the tiles run newest -> oldest in reading order. Anchoring to the cursor is what
    // stops the tiles shuffling every frame as the ring rotates.
    //
    // This comment said "oldest-to-newest" until the capture was looked at: the mover's position
    // within each tile decreases monotonically from tile 0, which is the opposite. Nothing failed,
    // and nothing would have -- the ordering is a reading convention, not a computation, so only
    // rendering it and looking could have caught it (ADR-385).
    let layer = ((writeLayer - 1 - index) % depth + depth) % depth;
    let local = fract(cell);
    var c = textureSampleLevel(colourHistory, linearSampler, local, layer, 0.0).rgb;
    // Tone down so an HDR ring is judgeable at a glance; this view is diagnostic, not graded.
    c = c / (c + vec3<f32>(1.0));
    if (index >= valid) {
        c = mix(c, vec3<f32>(0.35, 0.0, 0.0), 0.75);
    }
    // A one-texel grid so the tiles are countable.
    let edge = min(min(local.x, local.y), min(1.0 - local.x, 1.0 - local.y));
    if (edge < 0.01) {
        c = vec3<f32>(0.12, 0.12, 0.14);
    }
    return vec4<f32>(c, 1.0);
}
