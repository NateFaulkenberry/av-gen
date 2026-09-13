// Debug display of the auxiliary render targets (ADR-035). A fullscreen pass drawn over the HDR
// target just before tone mapping, so a target can be inspected in the same frame that produced it.
// Modes: 1 normal, 2 roughness, 3 velocity, 4 emission, 5 identifiers, 6 occlusion, 7 depth,
// 8 linear depth, 9 depth edges, 10 the selected object's depth.
//
// Mode 7 is the display this pass has always had: an exponential ramp over linear depth, legible
// across a whole scene and useless for reading a number off it. Modes 8 to 10 are the readings a
// forensic question needs, and they are separate views rather than one with a knob because each
// answers a different question -- how far is that, where are the silhouettes, and is *this* object
// where I think it is.

struct AuxDebugUniforms {
    info: vec4<f32>,  // x = mode, y = scale, zw = target size
    depth: vec4<f32>, // x = near, y = far, z = selected pick id (-1 for none), w = 0
};

@group(0) @binding(0) var<uniform> aux: AuxDebugUniforms;
@group(0) @binding(1) var normalRoughness: texture_2d<f32>;
@group(0) @binding(2) var velocity: texture_2d<f32>;
@group(0) @binding(3) var emission: texture_2d<f32>;
@group(0) @binding(4) var identifiers: texture_2d<u32>;
@group(0) @binding(5) var occlusion: texture_2d<f32>;
@group(0) @binding(6) var linear: texture_2d<f32>;

struct FsIn {
    @builtin(position) clip: vec4<f32>,
    @location(0) uv: vec2<f32>,
};

@vertex
fn vs_aux(@builtin(vertex_index) index: u32) -> FsIn {
    var positions = array<vec2<f32>, 3>(vec2<f32>(-1.0, -1.0), vec2<f32>(3.0, -1.0), vec2<f32>(-1.0, 3.0));
    let p = positions[index];
    var out: FsIn;
    out.clip = vec4<f32>(p, 0.0, 1.0);
    out.uv = vec2<f32>(p.x * 0.5 + 0.5, 0.5 - p.y * 0.5);
    return out;
}

fn octDecodeAux(e: vec2<f32>) -> vec3<f32> {
    var n = vec3<f32>(e.x, e.y, 1.0 - abs(e.x) - abs(e.y));
    if (n.z < 0.0) {
        let signs = vec2<f32>(select(-1.0, 1.0, n.x >= 0.0), select(-1.0, 1.0, n.y >= 0.0));
        let xy = (vec2<f32>(1.0) - abs(vec2<f32>(n.y, n.x))) * signs;
        n = vec3<f32>(xy, n.z);
    }
    return normalize(n);
}

// A stable colour per identifier so neighbouring objects never share one.
fn idColor(id: u32) -> vec3<f32> {
    var h = id * 2654435761u;
    h = h ^ (h >> 15u);
    h = h * 2246822519u;
    return vec3<f32>(f32((h >> 0u) & 255u), f32((h >> 8u) & 255u), f32((h >> 16u) & 255u)) / 255.0;
}

@fragment
fn fs_aux(in: FsIn) -> @location(0) vec4<f32> {
    let mode = i32(aux.info.x + 0.5);
    let texel = vec2<i32>(in.clip.xy);
    if (mode == 1) {
        let nr = textureLoad(normalRoughness, texel, 0);
        return vec4<f32>(octDecodeAux(nr.rg) * 0.5 + vec3<f32>(0.5), 1.0);
    }
    if (mode == 2) {
        let nr = textureLoad(normalRoughness, texel, 0);
        return vec4<f32>(vec3<f32>(nr.b), 1.0);
    }
    if (mode == 3) {
        let v = textureLoad(velocity, texel, 0).rg * aux.info.y;
        return vec4<f32>(v.x * 0.5 + 0.5, v.y * 0.5 + 0.5, 0.5, 1.0);
    }
    if (mode == 4) {
        let e = textureLoad(emission, texel, 0);
        return vec4<f32>(e.rgb * aux.info.y, 1.0);
    }
    if (mode == 5) {
        let id = textureLoad(identifiers, texel, 0).r;
        return vec4<f32>(select(idColor(id), vec3<f32>(0.02), id == 0u), 1.0);
    }
    if (mode == 6) {
        let size = vec2<f32>(textureDimensions(occlusion, 0));
        let c = vec2<i32>(clamp(in.uv * size, vec2<f32>(0.0), size - vec2<f32>(1.0)));
        let s = textureLoad(occlusion, c, 0);
        return vec4<f32>(vec3<f32>(s.b), 1.0);
    }
    if (mode == 7) {
        let d = textureLoad(linear, texel, 0).r;
        return vec4<f32>(vec3<f32>(1.0 - exp(-d * aux.info.y * 0.02)), 1.0);
    }
    if (mode == 8) {
        // Metres, straight: the displayed value is *proportional to distance*, which is the one
        // thing mode 7's exponential cannot give. The ramp runs over far / scale, so a scale of 1
        // is the whole view distance and larger values zoom into the near part of it -- necessary
        // because a far plane is not a scene, and RendererQA's camera sees three kilometres of a
        // thirty-metre world. The constant is in the uniform, not in somebody's head.
        //
        // Nothing-drawn is the linear target's own 1e7 sentinel and is shown as full white, so an
        // empty sky and a surface at the far plane are not the same picture.
        let d = textureLoad(linear, texel, 0).r;
        if (d > 1.0e6) {
            return vec4<f32>(1.0, 1.0, 1.0, 1.0);
        }
        let range = max(aux.depth.y / max(aux.info.y, 1e-3), 1e-3);
        return vec4<f32>(vec3<f32>(clamp(d / range, 0.0, 1.0)), 1.0);
    }
    if (mode == 9) {
        // Where the depth buffer has a silhouette. The comparison is *relative* to the depth at the
        // pixel: an absolute threshold finds an edge at every surface once the camera is far enough
        // away, which makes the whole frame an edge and says nothing.
        let d = textureLoad(linear, texel, 0).r;
        let size = vec2<i32>(textureDimensions(linear, 0)) - vec2<i32>(1);
        let r = textureLoad(linear, clamp(texel + vec2<i32>(1, 0), vec2<i32>(0), size), 0).r;
        let l = textureLoad(linear, clamp(texel - vec2<i32>(1, 0), vec2<i32>(0), size), 0).r;
        let u = textureLoad(linear, clamp(texel + vec2<i32>(0, 1), vec2<i32>(0), size), 0).r;
        let b = textureLoad(linear, clamp(texel - vec2<i32>(0, 1), vec2<i32>(0), size), 0).r;
        // The sentinel is clamped to the far plane before comparing: 1e7 against a surface is an
        // enormous step everywhere the scene meets the sky, which would make every silhouette
        // saturate identically and hide the discontinuities inside the geometry -- the ones a
        // depth bug actually shows up as.
        let far = max(aux.depth.y, 1e-3);
        let dc = min(d, far);
        let step = max(max(abs(min(r, far) - dc), abs(min(l, far) - dc)),
                       max(abs(min(u, far) - dc), abs(min(b, far) - dc)));
        let relative = step / max(dc, 1e-3);
        return vec4<f32>(vec3<f32>(clamp(relative * 8.0 * aux.info.y, 0.0, 1.0)), 1.0);
    }
    if (mode == 10) {
        // The selected object's depth, with everything else removed -- the picture that answers
        // "is it behind that" without the rest of the scene arguing. Nothing selected is an empty
        // frame rather than object zero: a view that silently showed a different object than the
        // one named would be the kind of diagnostic this investigation exists to remove.
        if (aux.depth.z < 0.0) {
            return vec4<f32>(0.0, 0.0, 0.0, 1.0);
        }
        // The identifier target packs the object id in the low sixteen bits and the material id in
        // the high sixteen, and the material id is deliberately **one-based** -- which is the only
        // thing that makes an empty pixel distinguishable from entity zero, whose own pick id is
        // literally 0. So the empty test is on the whole word and the match is on the low half.
        // Comparing the low half alone paints the entire sky as entity zero, which is how this was
        // found: a culled object's view came back showing the object everywhere.
        let word = textureLoad(identifiers, texel, 0).r;
        if (word == 0u || (word & 0xFFFFu) != u32(aux.depth.z + 0.5)) {
            return vec4<f32>(0.02, 0.0, 0.04, 1.0);
        }
        let d = textureLoad(linear, texel, 0).r;
        let range = max(aux.depth.y / max(aux.info.y, 1e-3), 1e-3);
        return vec4<f32>(0.2, clamp(d / range, 0.0, 1.0), 0.9, 1.0);
    }
    return vec4<f32>(0.0, 0.0, 0.0, 0.0);
}
