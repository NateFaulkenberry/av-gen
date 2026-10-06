// THE ASTRAL FORGE -- iteration 2: the CACHED LATENT. Once per frame the full latent anatomy (archetypes, morph,
// folds) is evaluated on a 128^3 lattice over the density box, but only in blocks that hold matter: the surface
// pass can only sharpen where density exists, so empty blocks are never needed. The raymarch then steps on this
// texture and evaluates the analytic latent only for the final bisection and the normal.

@group(1) @binding(0) var coarseTex: texture_3d<f32>;
@group(1) @binding(1) var cacheOut: texture_storage_3d<rgba16float, write>;

const CACHE_RES: i32 = 128;

@compute @workgroup_size(4, 4, 4)
fn cs_latent_cache(@builtin(global_invocation_id) gid: vec3u) {
    let c = vec3i(gid);
    if (any(c >= vec3i(CACHE_RES))) { return; }
    let ext = F.grid0.w * F.grid1.x;
    let p = F.grid0.xyz + (vec3f(c) + 0.5) * ext / f32(CACHE_RES);
    // the coarse occupancy grid is one cell per 8 density voxels
    let cc = vec3i(floor((p - F.grid0.xyz) / (F.grid0.w * 8.0)));
    let rc = vec3i(textureDimensions(coarseTex));
    let occ = textureLoad(coarseTex, clamp(cc, vec3i(0), rc - 1), 0).r;
    var d = 100.0;
    if (occ > F.grid1.z * 0.08) { d = latent(p); }
    textureStore(cacheOut, c, vec4f(d, 0.0, 0.0, 0.0));
}
