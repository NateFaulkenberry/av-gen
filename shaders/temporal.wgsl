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
    // echo: x = strength, y = per-tap decay. mosh (ADR-1049): x = amount, y = block px, z = smear px,
    // w = channel shift px
    params: vec4<f32>,
    // mosh (ADR-1049): x = the epoch (floor(time * rate) + seed), yzw = 0
    // sample (ADR-1931): x, y = the map's direction, z = the scan offset (wrapped), w = mode
    extra: vec4<f32>,
    // sample (ADR-1931): x = stride, y = blend between stored frames, z = reverse, w = scanning
    more: vec4<f32>,
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

// ---- ADR-1049 data mosh and channel shift ------------------------------------------------------
//
// A block is replaced by the same block from `lag` frames ago (1..taps), dragged along one axis: the
// smear of a codec that lost its keyframe. Which blocks, how far back and which way are integer hashes
// of the block and the epoch, so the result is a function of the ring and the clock alone (FIR). The
// colour channels are pulled apart by `shift` pixels everywhere, and twice as far inside a corrupted
// block. The ring is half resolution, so a corrupted block is also softer: that reads as damage.

fn moshPcg(v: u32) -> u32 {
    let s = v * 747796405u + 2891336453u;
    let w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u;
    return (w >> 22u) ^ w;
}

fn moshRandom(a: u32, b: u32, c: u32) -> f32 {
    return f32(moshPcg(a ^ moshPcg(b ^ moshPcg(c))) >> 8u) / 16777216.0;
}

fn moshShifted(uv: vec2<f32>, shiftUv: f32) -> vec3<f32> {
    let r = textureSampleLevel(source, linearSampler, uv + vec2<f32>(shiftUv, 0.0), 0.0).r;
    let g = textureSampleLevel(source, linearSampler, uv, 0.0).g;
    let b = textureSampleLevel(source, linearSampler, uv - vec2<f32>(shiftUv, 0.0), 0.0).b;
    return vec3<f32>(r, g, b);
}

@fragment
fn fs_mosh(in: FsIn) -> @location(0) vec4<f32> {
    let current = textureSampleLevel(source, linearSampler, in.uv, 0.0);
    let amount = clamp(temporal.params.x, 0.0, 1.0);
    let shiftUv = temporal.params.w * temporal.outputSize.z;
    var colour = current.rgb;
    if (shiftUv > 0.0) {
        colour = moshShifted(in.uv, shiftUv);
    }
    let taps = i32(temporal.ring.w + 0.5);
    if (amount <= 0.0 || taps <= 0) {
        return vec4<f32>(colour, current.a);
    }
    let block = max(temporal.params.y, 1.0);
    let cell = vec2<u32>(max(floor(in.uv * temporal.outputSize.xy / block), vec2<f32>(0.0)));
    let epoch = bitcast<u32>(i32(floor(temporal.extra.x)));
    if (moshRandom(cell.x, cell.y, epoch) >= amount) {
        return vec4<f32>(colour, current.a);
    }
    let depth = i32(temporal.ring.x + 0.5);
    let writeLayer = i32(temporal.ring.y + 0.5);
    let lag = clamp(1 + i32(floor(moshRandom(cell.x + 7919u, cell.y, epoch) * f32(taps))), 1, taps);
    let layer = ((writeLayer - lag) % depth + depth) % depth;
    let pick = moshRandom(cell.x, cell.y + 104729u, epoch);
    let sign = select(-1.0, 1.0, moshRandom(cell.y, cell.x, epoch + 31u) < 0.5);
    let dir = select(vec2<f32>(sign, 0.0), vec2<f32>(0.0, sign), pick < 0.3);
    let drag = temporal.params.z * (0.25 + 0.75 * moshRandom(cell.x + 3u, cell.y + 5u, epoch)) * temporal.outputSize.zw;
    let uv = clamp(in.uv - dir * drag, vec2<f32>(0.0), vec2<f32>(1.0));
    var moshed = textureSampleLevel(colourHistory, linearSampler, uv, layer, 0.0).rgb;
    if (shiftUv > 0.0) {
        let s = 2.0 * shiftUv;
        moshed = vec3<f32>(textureSampleLevel(colourHistory, linearSampler, uv + vec2<f32>(s, 0.0), layer, 0.0).r,
                           moshed.g,
                           textureSampleLevel(colourHistory, linearSampler, uv - vec2<f32>(s, 0.0), layer, 0.0).b);
    }
    return vec4<f32>(moshed, current.a);
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

// ---- ADR-1066 feedback, unrolled over the clean ring -------------------------------------------------------------
//
// MilkDrop's loop F = C + d g(F_prev(T uv)) is an IIR filter, which ADR-410 forbids. Unrolled K taps over the CLEAN
// ring it is an FIR with the same first K terms:
//   out = C + amount (1 - decay) sum_k decay^(k-1) hue^k(R_k(T^k uv))
// where T zooms by `zoom`, turns by `rotate` and drifts by `drift` per frame about the centre (in aspect-correct
// space), so frame k back is read where it would have been carried to by now, and hue^k turns its colour k x `hue`
// turns in OKLab, which holds lightness where an HSV turn would flash. Samples from outside the frame are black.
//   params: amount, decay, zoom, rotate (rad/frame)    extra: driftX, driftY (frame/frame), hue (turns/frame), aspect

fn fbToOklab(c: vec3<f32>) -> vec3<f32> {
    let l = 0.4122214708 * c.r + 0.5363325363 * c.g + 0.0514459929 * c.b;
    let m = 0.2119034982 * c.r + 0.6806995451 * c.g + 0.1073969566 * c.b;
    let s = 0.0883024619 * c.r + 0.2817188376 * c.g + 0.6299787005 * c.b;
    let l3 = pow(max(l, 0.0), 1.0 / 3.0);
    let m3 = pow(max(m, 0.0), 1.0 / 3.0);
    let s3 = pow(max(s, 0.0), 1.0 / 3.0);
    return vec3<f32>(0.2104542553 * l3 + 0.7936177850 * m3 - 0.0040720468 * s3,
                     1.9779984951 * l3 - 2.4285922050 * m3 + 0.4505937099 * s3,
                     0.0259040371 * l3 + 0.7827717662 * m3 - 0.8086757660 * s3);
}

fn fbFromOklab(c: vec3<f32>) -> vec3<f32> {
    let l3 = c.x + 0.3963377774 * c.y + 0.2158037573 * c.z;
    let m3 = c.x - 0.1055613458 * c.y - 0.0638541728 * c.z;
    let s3 = c.x - 0.0894841775 * c.y - 1.2914855480 * c.z;
    let l = l3 * l3 * l3;
    let m = m3 * m3 * m3;
    let s = s3 * s3 * s3;
    return max(vec3<f32>(4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
                         -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
                         -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s), vec3<f32>(0.0));
}

fn fbHue(c: vec3<f32>, turns: f32) -> vec3<f32> {
    if (abs(turns) < 1e-6) {
        return c;
    }
    let lab = fbToOklab(c);
    let a = turns * 6.2831853;
    let ab = vec2<f32>(lab.y * cos(a) - lab.z * sin(a), lab.y * sin(a) + lab.z * cos(a));
    return fbFromOklab(vec3<f32>(lab.x, ab));
}

@fragment
fn fs_feedback(in: FsIn) -> @location(0) vec4<f32> {
    let current = textureSampleLevel(source, linearSampler, in.uv, 0.0);
    let taps = i32(temporal.ring.w + 0.5);
    if (taps <= 0) {
        return current;
    }
    let depth = i32(temporal.ring.x + 0.5);
    let writeLayer = i32(temporal.ring.y + 0.5);
    let amount = temporal.params.x;
    let decay = clamp(temporal.params.y, 0.0, 0.99);
    let zoom = max(temporal.params.z, 1e-3);
    let turn = temporal.params.w;
    let drift = temporal.extra.xy;
    let aspect = max(temporal.extra.w, 1e-3);
    let q = (in.uv - vec2<f32>(0.5)) * vec2<f32>(aspect, 1.0);
    var tail = vec3<f32>(0.0);
    var weight = 1.0 - decay;
    for (var k = 1; k <= taps; k = k + 1) {
        let fk = f32(k);
        // Where the content that is at `uv` now was k frames ago: the k-fold transform, inverted.
        let back = q - drift * fk * vec2<f32>(aspect, 1.0);
        let a = -turn * fk;
        let r = vec2<f32>(back.x * cos(a) - back.y * sin(a), back.x * sin(a) + back.y * cos(a)) / pow(zoom, fk);
        let uv = r / vec2<f32>(aspect, 1.0) + vec2<f32>(0.5);
        if (all(uv >= vec2<f32>(0.0)) && all(uv <= vec2<f32>(1.0))) {
            let layer = ((writeLayer - k) % depth + depth) % depth;
            let s = textureSampleLevel(colourHistory, linearSampler, uv, layer, 0.0).rgb;
            tail = tail + fbHue(s, temporal.extra.z * fk) * weight;
        }
        weight = weight * decay;
    }
    return vec4<f32>(current.rgb + tail * amount, current.a);
}

// ---- ADR-1931: the sampler -- slit scan as a time map over the clean ring ------------------------------------------
//
// Each pixel shows the moment `age(uv)` frames ago. The age comes from the slit map:
//   d(uv) in 0..1: mode 0 along the direction (extra.xy), normalised so the frame spans exactly 0..1 at any angle;
//                  mode 1 radial (centre 0); mode 2 luminance (bright 0);
//   the scan offset (extra.z) wraps d when scanning (more.w), so the seam between now and the oldest moment travels;
//   reverse (more.z) flips it;
//   strips (params.z) quantise it, with soft edges over params.w of each strip;
//   age = q * span (params.y).
// The ring is addressed by age: ages up to `newestAge` (ring.w) blend the current frame into the newest stored layer;
// beyond that, layer j (0 = newest) is `newestAge + j * stride` frames back. `blend` (more.y) is linear between the two
// nearest stored frames at 1 and the nearest at 0. Ages past what is stored clamp to the oldest valid layer, so a
// settling ring shows a shorter span, never a pre-seek frame.
//   ring: depth, write layer, frames valid (layers), newest age    params: amount, span, strips, edge

fn sampleLayer(uv: vec2<f32>, j: i32, writeLayer: i32, depth: i32) -> vec3<f32> {
    let layer = ((writeLayer - 1 - j) % depth + depth) % depth;
    return textureSampleLevel(colourHistory, linearSampler, uv, layer, 0.0).rgb;
}

fn sampleAge(uv: vec2<f32>, age: f32, current: vec3<f32>) -> vec3<f32> {
    let depth = max(i32(temporal.ring.x + 0.5), 1);
    let writeLayer = i32(temporal.ring.y + 0.5);
    let valid = i32(temporal.ring.z + 0.5);
    let newest = max(temporal.ring.w, 1.0);
    let stride = max(temporal.more.x, 1.0);
    let blend = clamp(temporal.more.y, 0.0, 1.0);
    if (valid <= 0 || age <= 0.0) {
        return current;
    }
    if (age < newest) {
        var f = age / newest;
        f = mix(step(0.5, f), f, blend);
        return mix(current, sampleLayer(uv, 0, writeLayer, depth), f);
    }
    let j = (age - newest) / stride;
    let last = valid - 1;
    let j0 = min(i32(floor(j)), last);
    let j1 = min(j0 + 1, last);
    var f = clamp(j - floor(j), 0.0, 1.0);
    if (j0 == j1) {
        f = 0.0;
    }
    f = mix(step(0.5, f), f, blend);
    let a = sampleLayer(uv, j0, writeLayer, depth);
    if (f <= 0.0) {
        return a;
    }
    return mix(a, sampleLayer(uv, j1, writeLayer, depth), f);
}

@fragment
fn fs_sample(in: FsIn) -> @location(0) vec4<f32> {
    let current = textureSampleLevel(source, linearSampler, in.uv, 0.0);
    let mode = i32(temporal.extra.w + 0.5);
    var d = 0.0;
    if (mode == 1) {
        d = clamp(length(in.uv - vec2<f32>(0.5)) * 1.41421356, 0.0, 1.0);
    } else if (mode == 2) {
        let l = dot(current.rgb, vec3<f32>(0.2126, 0.7152, 0.0722));
        d = 1.0 - l / (1.0 + l);
    } else {
        // Along the direction, normalised by its support over the unit square so the frame spans 0..1 at any angle.
        let dir = temporal.extra.xy;
        let support = max(abs(dir.x) + abs(dir.y), 1.0e-4);
        d = clamp(dot(in.uv - vec2<f32>(0.5), dir) / support + 0.5, 0.0, 1.0);
    }
    if (temporal.more.w > 0.5) {
        d = fract(d + temporal.extra.z);
    }
    if (temporal.more.z > 0.5) {
        d = 1.0 - d;
    }
    let strips = temporal.params.z;
    var q = d;
    if (strips >= 1.0) {
        // Strip i of N shows age i / (N - 1) of the span; `edge` ramps into the next over the end of the strip.
        let x = d * strips;
        let i = min(floor(x), strips - 1.0);
        let f = x - i;
        let w = clamp(temporal.params.w, 0.0, 1.0);
        var t = 0.0;
        if (w > 0.0) {
            t = smoothstep(1.0 - w, 1.0, f);
        }
        q = min(i + t, strips - 1.0) / max(strips - 1.0, 1.0);
    }
    let age = q * temporal.params.y;
    let displaced = sampleAge(in.uv, age, current.rgb);
    return vec4<f32>(mix(current.rgb, displaced, clamp(temporal.params.x, 0.0, 1.0)), current.a);
}
