// ADR-1200: the ecosystem's emitters (rendering::EcosystemRenderer; scene/ecosystem.hpp for the model).
//
// Every emitter point of every host instance of a layer is one invocation of cs_emit. It is placed by its host's
// matrix, gets its light from the layer's rest behaviour and its response field, is depth-tested against the
// scene's linear depth (the prepass), and then either
//   - splatted: its energy (radiance x its projected area, at most one pixel's worth per pixel touched) added
//     bilinearly into a fixed-point accumulation buffer with u32 atomics (order-free, so deterministic), or
//   - when it covers more than `spriteRadius` pixels, appended to a sprite list that vs/fs_sprite draws as soft
//     discs with the hardware depth test.
// fs_resolve adds the accumulation into the HDR target and the emission target (so the bloom sees it as light).
#include "common.wgsl"
#include "fields.wgsl"

struct EcoFrame {
    size: vec4<f32>,        // x = width, y = height, z = 1 / width, w = 1 / height (the HDR target)
    misc: vec4<f32>,        // x = time (s), y = sprite radius (px), z = max sprites, w = fixed-point scale
};

struct Layer {
    color: vec4<f32>,       // rgb rest colour, w = intensity
    excited: vec4<f32>,     // rgb excited colour, w = excited intensity
    response: vec4<f32>,    // x = gain, y = threshold, z = travel, w = size
    rest: vec4<f32>,        // x = breath, y = breath rate, z = flicker, w = flicker rate
    pulse: vec4<f32>,       // x = pulses per minute, y = pulse decay (s), z = sparsity, w = max distance
    slots: vec4<i32>,       // x = response field slot (-1 none), y = lag field slot, z = point count, w = host count
    bound: vec4<f32>,       // x = host bounding radius (template extent, before host scale), y = wake field slot
                            // (-1 none), z = wake gain, w = near fade (m, 0 = off)
};

struct Host {
    row0: vec4<f32>,        // affine rows: world = (dot(row0, p1), dot(row1, p1), dot(row2, p1))
    row1: vec4<f32>,
    row2: vec4<f32>,
};

struct Point {
    px: f32, py: f32, pz: f32,
    v: f32, u: f32, radius: f32,
};

struct Sprite {
    centerRadius: vec4<f32>, // world centre, world radius
    radiance: vec4<f32>,     // rgb, w unused
};

@group(0) @binding(1) var<uniform> fieldBlock: FieldBlock;
@group(0) @binding(2) var linearDepth: texture_2d<f32>;
@group(0) @binding(3) var<storage, read_write> accum: array<atomic<u32>>;
@group(0) @binding(4) var<storage, read_write> sprites: array<Sprite>;
@group(0) @binding(5) var<storage, read_write> counters: array<atomic<u32>>; // 0 = sprites, 1 = splats
@group(0) @binding(6) var<uniform> eco: EcoFrame;

@group(2) @binding(0) var<uniform> layer: Layer;
@group(2) @binding(1) var<storage, read> hosts: array<Host>;
@group(2) @binding(2) var<storage, read> points: array<Point>;

fn ecoHash(x: u32) -> u32 {
    var h = x * 747796405u + 2891336453u;
    h = ((h >> ((h >> 28u) + 4u)) ^ h) * 277803737u;
    return (h >> 22u) ^ h;
}

fn ecoUnit(x: u32) -> f32 {
    return f32(ecoHash(x) >> 8u) / 16777216.0;
}

fn splatAdd(px: i32, py: i32, rgb: vec3<f32>) {
    let w = i32(eco.size.x);
    let h = i32(eco.size.y);
    if (px < 0 || py < 0 || px >= w || py >= h) {
        return;
    }
    let k = eco.misc.w;
    let base = u32(py * w + px) * 3u;
    let q = clamp(rgb * k, vec3<f32>(0.0), vec3<f32>(4.0e6));
    if (q.r >= 1.0) { atomicAdd(&accum[base + 0u], u32(q.r)); }
    if (q.g >= 1.0) { atomicAdd(&accum[base + 1u], u32(q.g)); }
    if (q.b >= 1.0) { atomicAdd(&accum[base + 2u], u32(q.b)); }
}

@compute @workgroup_size(64)
fn cs_emit(@builtin(workgroup_id) wg: vec3<u32>, @builtin(local_invocation_id) lid: vec3<u32>) {
    let pointIndex = wg.x * 64u + lid.x;
    let hostIndex = wg.y + wg.z * 65535u;
    let pointCount = u32(layer.slots.z);
    let hostCount = u32(layer.slots.w);
    if (hostIndex >= hostCount || pointIndex >= pointCount) {
        return;
    }
    let host = hosts[hostIndex];
    let origin = vec3<f32>(host.row0.w, host.row1.w, host.row2.w);
    let hostScale = length(vec3<f32>(host.row0.x, host.row1.x, host.row2.x));
    let reach = layer.bound.x * hostScale;
    let toHost = origin - frame.cameraPos.xyz;
    let maxDistance = layer.pulse.w;
    let hostDistance = length(toHost);
    // The whole organism: too far, or entirely behind the camera.
    if (hostDistance - reach > maxDistance || dot(toHost, frame.cameraForward.xyz) < -reach) {
        return;
    }

    let pt = points[pointIndex];
    let local = vec4<f32>(pt.px, pt.py, pt.pz, 1.0);
    let p = vec3<f32>(dot(host.row0, local), dot(host.row1, local), dot(host.row2, local));
    let toP = p - frame.cameraPos.xyz;
    let dist = length(toP);
    let viewDepth = dot(toP, frame.cameraForward.xyz);
    if (dist > maxDistance || viewDepth < 0.05) {
        return;
    }
    let clip = frame.viewProj * vec4<f32>(p, 1.0);
    if (clip.w <= 0.0) {
        return;
    }
    let ndc = clip.xyz / clip.w;
    if (abs(ndc.x) > 1.05 || abs(ndc.y) > 1.05) {
        return;
    }
    let fx = (ndc.x * 0.5 + 0.5) * eco.size.x;
    let fy = (0.5 - ndc.y * 0.5) * eco.size.y;

    // Occlusion: the scene's linear depth (view-space distance along the camera axis) at the point's pixel.
    let radius = pt.radius * layer.response.w * hostScale;
    let texel = vec2<i32>(clamp(i32(fx), 0, i32(eco.size.x) - 1), clamp(i32(fy), 0, i32(eco.size.y) - 1));
    let sceneDepth = textureLoad(linearDepth, texel, 0).r;
    if (viewDepth - radius > sceneDepth * 1.002 + 0.02) {
        return;
    }

    // ---- light ----
    let t = eco.misc.x;
    let id = hostIndex * 65537u + pointIndex * 2654435761u;
    let hHost = ecoUnit(hostIndex * 9781u + 17u);
    let hPoint = ecoUnit(id);
    let hPoint2 = ecoUnit(id ^ 0x9e3779b9u);
    let tau = 6.2831853;

    var rest = layer.color.w;
    if (hPoint < layer.pulse.z) {
        rest = 0.0; // sparse: dark at rest, still answers the field
    }
    let breath = 1.0 + layer.rest.x * sin(tau * (layer.rest.y * t * (0.75 + 0.5 * hHost) + hHost + 0.15 * pt.v));
    let flicker = 1.0 + layer.rest.z * sin(tau * (layer.rest.w * t * (0.6 + 0.8 * hPoint2) + hPoint));
    rest = rest * max(breath, 0.0) * max(flicker, 0.0);
    fieldElement = hPoint;
    let wakeSlot = i32(layer.bound.y);
    if (wakeSlot >= 0) {
        rest = rest * (1.0 + layer.bound.z * max(fieldScalar(wakeSlot, p), 0.0));
    }
    // Spontaneous flashes: one chance per period, at the point's own phase; a hash decides whether it fires.
    if (layer.pulse.x > 0.0) {
        let period = 60.0 / layer.pulse.x;
        let phase = t / period + hPoint2;
        let cycle = u32(floor(phase));
        let age = fract(phase) * period;
        if (ecoUnit(cycle * 3266489917u + id) < 0.5) {
            rest = rest + 4.0 * layer.color.w * exp(-age / max(layer.pulse.y, 0.01));
        }
    }

    var response = 0.0;
    fieldElement = hPoint;
    if (layer.slots.x >= 0) {
        var f = fieldScalar(layer.slots.x, p);
        if (layer.slots.y >= 0 && layer.response.z > 0.0) {
            let w = smoothstep(0.0, 1.0, pt.v) * layer.response.z;
            f = mix(f, fieldScalar(layer.slots.y, p), w);
        }
        response = layer.response.x * max(f - layer.response.y, 0.0);
    }
    let colour = mix(layer.color.rgb, layer.excited.rgb, saturate(response));
    var fade = 1.0 - smoothstep(0.75 * maxDistance, maxDistance, dist);
    if (layer.bound.w > 0.0) {
        fade = fade * smoothstep(0.4 * layer.bound.w, layer.bound.w, dist);
    }
    let radiance = colour * (rest + layer.excited.w * response) * fade;
    if (max(radiance.r, max(radiance.g, radiance.b)) <= 1.0e-5) {
        return;
    }

    // ---- footprint ----
    let clipUp = frame.viewProj * vec4<f32>(p + frame.cameraUp.xyz * radius, 1.0);
    let upY = (0.5 - (clipUp.y / clipUp.w) * 0.5) * eco.size.y;
    let rPx = abs(upY - fy);
    if (rPx > eco.misc.y) {
        let slot = atomicAdd(&counters[0], 1u);
        if (slot < u32(eco.misc.z)) {
            sprites[slot].centerRadius = vec4<f32>(p, radius);
            sprites[slot].radiance = vec4<f32>(radiance, 0.0);
        }
        return;
    }
    atomicAdd(&counters[1], 1u);
    // Energy: radiance over the projected disc, at least a tenth of a pixel so the far field still adds up.
    let area = max(3.14159265 * rPx * rPx, 0.1);
    let energy = radiance * area;
    let sx = fx - 0.5;
    let sy = fy - 0.5;
    let ix = i32(floor(sx));
    let iy = i32(floor(sy));
    let ax = sx - f32(ix);
    let ay = sy - f32(iy);
    splatAdd(ix, iy, energy * (1.0 - ax) * (1.0 - ay));
    splatAdd(ix + 1, iy, energy * ax * (1.0 - ay));
    splatAdd(ix, iy + 1, energy * (1.0 - ax) * ay);
    splatAdd(ix + 1, iy + 1, energy * ax * ay);
}

// The sprite draw's indirect arguments: six vertices per sprite, as many sprites as were appended (capped).
@group(0) @binding(7) var<storage, read_write> spriteArgs: array<u32, 4>;

@compute @workgroup_size(1)
fn cs_sprite_args() {
    spriteArgs[0] = 6u;
    spriteArgs[1] = min(atomicLoad(&counters[0]), u32(eco.misc.z));
    spriteArgs[2] = 0u;
    spriteArgs[3] = 0u;
}

// ---- resolve: the accumulation into the HDR and emission targets (additive) ----

@group(0) @binding(8) var<storage, read> accumRead: array<u32>;

struct FsResolve {
    @builtin(position) clip: vec4<f32>,
};

@vertex
fn vs_resolve(@builtin(vertex_index) index: u32) -> FsResolve {
    var positions = array<vec2<f32>, 3>(vec2<f32>(-1.0, -1.0), vec2<f32>(3.0, -1.0), vec2<f32>(-1.0, 3.0));
    var out: FsResolve;
    out.clip = vec4<f32>(positions[index], 0.0, 1.0);
    return out;
}

struct ResolveOut {
    @location(0) color: vec4<f32>,
    @location(1) emission: vec4<f32>,
};

@fragment
fn fs_resolve(in: FsResolve) -> ResolveOut {
    let px = vec2<i32>(in.clip.xy);
    let base = u32(px.y * i32(eco.size.x) + px.x) * 3u;
    let rgb = vec3<f32>(f32(accumRead[base]), f32(accumRead[base + 1u]), f32(accumRead[base + 2u])) / eco.misc.w;
    var out: ResolveOut;
    out.color = vec4<f32>(rgb, 0.0);
    out.emission = vec4<f32>(rgb, 1.0);
    return out;
}

// ---- sprites: near emitters as soft discs ----

@group(0) @binding(9) var<storage, read> spritesRead: array<Sprite>;

struct FsSprite {
    @builtin(position) clip: vec4<f32>,
    @location(0) offset: vec2<f32>,
    @location(1) radiance: vec3<f32>,
};

@vertex
fn vs_sprite(@builtin(vertex_index) vi: u32, @builtin(instance_index) ii: u32) -> FsSprite {
    var corners = array<vec2<f32>, 6>(vec2<f32>(-1.0, -1.0), vec2<f32>(1.0, -1.0), vec2<f32>(1.0, 1.0),
                                      vec2<f32>(-1.0, -1.0), vec2<f32>(1.0, 1.0), vec2<f32>(-1.0, 1.0));
    let s = spritesRead[ii];
    let c = corners[vi];
    // The quad is twice the core's radius: the halo falls off inside it.
    let r = s.centerRadius.w * 2.0;
    let p = s.centerRadius.xyz + (frame.cameraRight.xyz * c.x + frame.cameraUp.xyz * c.y) * r;
    var out: FsSprite;
    out.clip = frame.viewProj * vec4<f32>(p, 1.0);
    out.offset = c;
    out.radiance = s.radiance.rgb;
    return out;
}

@fragment
fn fs_sprite(in: FsSprite) -> ResolveOut {
    let d2 = dot(in.offset, in.offset) * 4.0; // 1 at the core's rim
    if (d2 > 4.0) {
        discard;
    }
    // A bright core with a soft rim, and a faint halo out to twice the radius.
    let core = 1.0 - smoothstep(0.55, 1.0, d2);
    let halo = 0.12 * exp(-1.5 * d2);
    let rgb = in.radiance * (core + halo);
    var out: ResolveOut;
    out.color = vec4<f32>(rgb, 0.0);
    out.emission = vec4<f32>(rgb, 1.0);
    return out;
}
