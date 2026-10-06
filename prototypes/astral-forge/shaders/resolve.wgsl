// THE ASTRAL FORGE -- density resolve: u32 splat grid -> blurred rgba16f 3-D texture (density, heat), and a
// coarse max-occupancy grid (one cell per 8^3) for empty-space skipping in the raymarch.

@group(1) @binding(0) var<storage, read> grid: array<u32>;
@group(1) @binding(1) var densOut: texture_storage_3d<rgba16float, write>;

const DENS_SCALE: f32 = 1024.0;

fn cellAt(c: vec3i, res: i32, ch: u32) -> f32 {
    let q = clamp(c, vec3i(0), vec3i(res - 1));
    return f32(grid[u32(q.x + res * (q.y + res * q.z)) * 2u + ch]);
}

// Iteration 2: the 3x3x3 blur reads a 6^3 apron tile from workgroup memory (216 cells, ~3.4 loads per thread)
// instead of 27 global loads per channel per voxel.
var<workgroup> tileD: array<f32, 216>;
var<workgroup> tileH: array<f32, 216>;

@compute @workgroup_size(4, 4, 4)
fn cs_resolve(@builtin(global_invocation_id) gid: vec3u, @builtin(workgroup_id) wid: vec3u, @builtin(local_invocation_index) li: u32) {
    let res = i32(F.grid1.x);
    let base = vec3i(wid) * 4 - vec3i(1);
    for (var k = li; k < 216u; k += 64u) {
        let o = vec3i(i32(k % 6u), i32((k / 6u) % 6u), i32(k / 36u));
        tileD[k] = cellAt(base + o, res, 0u);
        tileH[k] = cellAt(base + o, res, 1u);
    }
    workgroupBarrier();
    let c = vec3i(gid);
    if (any(c >= vec3i(res))) { return; }
    let l = c - base; // 1..4 in each axis
    var d = 0.0;
    var h = 0.0;
    for (var z = -1; z <= 1; z++) {
        for (var y = -1; y <= 1; y++) {
            for (var x = -1; x <= 1; x++) {
                let w = f32((2 - abs(x)) * (2 - abs(y)) * (2 - abs(z)));
                let q = l + vec3i(x, y, z);
                let k = u32(q.x + 6 * (q.y + 6 * q.z));
                d += w * tileD[k];
                h += w * tileH[k];
            }
        }
    }
    d /= 64.0 * DENS_SCALE;
    h /= 64.0 * DENS_SCALE;
    let norm = F.flags.z;
    textureStore(densOut, c, vec4f(d * norm, h / max(d, 1e-3), 0.0, 0.0));
}
