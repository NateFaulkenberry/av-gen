// ADR-1141: a particle system's render-transient density volume.
//
// Each frame, after the system's compute pass: the u32 grid is cleared (a buffer clear on the CPU
// side), `cs_density_splat` deposits every live particle into it by trilinear cloud-in-cell with a
// u32 fixed-point atomicAdd -- integer addition is associative, so the order the particles arrive in
// cannot change the sum and the grid is deterministic -- and `cs_density_resolve` blurs it 3x3x3
// (binomial, (1 2 1)^3 / 64, edges clamped) into an rgba16float 3-D texture:
//     r = weight * blurred particles per cell,  g = b = 0,  a = 1.
// The volume is a picture of this frame's particles, not state: nothing reads last frame's.
// scene/particle_latent.cpp (`splatDensity`, `resolveDensity`) is the CPU reference.
//
// Bindings (its own layout; the particle compute layout has no storage slot left after ADR-1140):
//   0 DensityParams (uniform), 1 the pool (read-only storage), 2 the u32 grid, 3 the 3-D texture.

#include "particle_record.wgsl"

struct DensityParams {
    boundsMin: vec4<f32>, // xyz = world-space min corner, w = 0
    boundsMax: vec4<f32>, // xyz = world-space max corner, w = weight
    counts: vec4<u32>,    // x = pool capacity, y = resolution, zw = 0
};

const DENSITY_FIXED_SCALE: f32 = 1024.0; // scene::kDensityFixedScale

@group(0) @binding(0) var<uniform> density: DensityParams;
@group(0) @binding(1) var<storage, read> pool: array<Particle>;
@group(0) @binding(2) var<storage, read_write> grid: array<atomic<u32>>;
@group(0) @binding(3) var densityOut: texture_storage_3d<rgba16float, write>;

@compute @workgroup_size(64)
fn cs_density_splat(@builtin(global_invocation_id) gid: vec3<u32>) {
    let slot = gid.x;
    if (slot >= density.counts.x) { return; }
    let p = pool[slot];
    if (p.life <= 0.0) { return; }
    let res = i32(density.counts.y);
    let cell = (density.boundsMax.xyz - density.boundsMin.xyz) / f32(res);
    let g = (p.position - density.boundsMin.xyz) / cell - 0.5;
    let g0 = vec3<i32>(floor(g));
    if (any(g0 < vec3<i32>(0)) || any(g0 >= vec3<i32>(res - 1))) { return; }
    let f = g - vec3<f32>(g0);
    for (var k = 0; k < 8; k = k + 1) {
        let o = vec3<i32>(k & 1, (k >> 1) & 1, (k >> 2) & 1);
        let wx = select(1.0 - f.x, f.x, o.x == 1);
        let wy = select(1.0 - f.y, f.y, o.y == 1);
        let wz = select(1.0 - f.z, f.z, o.z == 1);
        let w = wx * wy * wz;
        let c = g0 + o;
        let idx = u32(c.x + res * (c.y + res * c.z));
        atomicAdd(&grid[idx], u32(w * DENSITY_FIXED_SCALE + 0.5));
    }
}

// The 3x3x3 blur reads a 6^3 apron tile from workgroup memory (216 cells, ~3.4 loads per thread)
// rather than 27 global loads per voxel (the prototype's measured iteration-2 form).
var<workgroup> tile: array<f32, 216>;

fn densityCell(c: vec3<i32>, res: i32) -> f32 {
    let q = clamp(c, vec3<i32>(0), vec3<i32>(res - 1));
    return f32(atomicLoad(&grid[u32(q.x + res * (q.y + res * q.z))]));
}

@compute @workgroup_size(4, 4, 4)
fn cs_density_resolve(@builtin(global_invocation_id) gid: vec3<u32>, @builtin(workgroup_id) wid: vec3<u32>,
                      @builtin(local_invocation_index) li: u32) {
    let res = i32(density.counts.y);
    let base = vec3<i32>(wid) * 4 - vec3<i32>(1);
    for (var k = li; k < 216u; k = k + 64u) {
        let o = vec3<i32>(i32(k % 6u), i32((k / 6u) % 6u), i32(k / 36u));
        tile[k] = densityCell(base + o, res);
    }
    workgroupBarrier();
    let c = vec3<i32>(gid);
    if (any(c >= vec3<i32>(res))) { return; }
    let l = c - base; // 1..4 on each axis
    var sum = 0.0;
    for (var z = -1; z <= 1; z = z + 1) {
        for (var y = -1; y <= 1; y = y + 1) {
            for (var x = -1; x <= 1; x = x + 1) {
                let w = f32((2 - abs(x)) * (2 - abs(y)) * (2 - abs(z)));
                let q = l + vec3<i32>(x, y, z);
                sum = sum + w * tile[u32(q.x + 6 * (q.y + 6 * q.z))];
            }
        }
    }
    let rho = sum / (64.0 * DENSITY_FIXED_SCALE) * density.boundsMax.w;
    textureStore(densityOut, c, vec4<f32>(rho, 0.0, 0.0, 1.0));
}
