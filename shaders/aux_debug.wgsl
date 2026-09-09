// Debug display of the auxiliary render targets (ADR-035). A fullscreen pass drawn over the HDR
// target just before tone mapping, so a target can be inspected in the same frame that produced it.
// Modes: 1 normal, 2 roughness, 3 velocity, 4 emission, 5 identifiers, 6 occlusion, 7 depth.

struct AuxDebugUniforms {
    info: vec4<f32>, // x = mode, y = scale, zw = target size
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
    return vec4<f32>(0.0, 0.0, 0.0, 0.0);
}
