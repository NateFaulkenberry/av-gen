// THE ASTRAL FORGE -- coarse max-occupancy grid (one cell per 8^3 voxels) for empty-space skipping.

@group(1) @binding(0) var densIn: texture_3d<f32>;
@group(1) @binding(1) var coarseOut: texture_storage_3d<r32float, write>;

@compute @workgroup_size(4, 4, 4)
fn cs_coarse(@builtin(global_invocation_id) gid: vec3u) {
    let res = i32(F.grid1.x);
    let cr = (res + 7) / 8;
    let c = vec3i(gid);
    if (any(c >= vec3i(cr))) { return; }
    var m = 0.0;
    // include a one-voxel apron so trilinear lookups at block edges are covered
    for (var z = -1; z <= 8; z++) {
        for (var y = -1; y <= 8; y++) {
            for (var x = -1; x <= 8; x++) {
                let q = clamp(c * 8 + vec3i(x, y, z), vec3i(0), vec3i(res - 1));
                m = max(m, textureLoad(densIn, q, 0).r);
            }
        }
    }
    textureStore(coarseOut, c, vec4f(m, 0.0, 0.0, 0.0));
}
