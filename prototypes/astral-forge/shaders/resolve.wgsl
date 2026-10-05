// THE ASTRAL FORGE -- density resolve: u32 splat grid -> blurred rgba16f 3-D texture (density, heat), and a
// coarse max-occupancy grid (one cell per 8^3) for empty-space skipping in the raymarch.

@group(1) @binding(0) var<storage, read> grid: array<u32>;
@group(1) @binding(1) var densOut: texture_storage_3d<rgba16float, write>;

const DENS_SCALE: f32 = 1024.0;

fn cellAt(c: vec3i, res: i32, ch: u32) -> f32 {
    let q = clamp(c, vec3i(0), vec3i(res - 1));
    return f32(grid[u32(q.x + res * (q.y + res * q.z)) * 2u + ch]);
}

@compute @workgroup_size(4, 4, 4)
fn cs_resolve(@builtin(global_invocation_id) gid: vec3u) {
    let res = i32(F.grid1.x);
    let c = vec3i(gid);
    if (any(c >= vec3i(res))) { return; }
    // 3x3x3 binomial blur (1 2 1)^3 / 64
    var d = 0.0;
    var h = 0.0;
    for (var z = -1; z <= 1; z++) {
        for (var y = -1; y <= 1; y++) {
            for (var x = -1; x <= 1; x++) {
                let w = f32((2 - abs(x)) * (2 - abs(y)) * (2 - abs(z)));
                let cc = c + vec3i(x, y, z);
                d += w * cellAt(cc, res, 0u);
                h += w * cellAt(cc, res, 1u);
            }
        }
    }
    d /= 64.0 * DENS_SCALE;
    h /= 64.0 * DENS_SCALE;
    // particles per cell -> a normalised density; the iso threshold is ~1 for a formed surface
    let norm = F.flags.z;
    textureStore(densOut, c, vec4f(d * norm, h / max(d, 1e-3), 0.0, 0.0));
}

