// THE ASTRAL FORGE -- iteration 2: the CACHED LATENT. Once per frame the full latent anatomy (archetypes, morph,
// folds) is evaluated on a 128^3 lattice over the density box, but only in blocks that hold matter: the surface
// pass can only sharpen where density exists, so empty blocks are never needed. The raymarch then steps on this
// texture and evaluates the analytic latent only for the final bisection and the normal.

@group(1) @binding(0) var coarseTex: texture_3d<f32>;
@group(1) @binding(1) var cacheOut: texture_storage_3d<rgba16float, write>;
@group(1) @binding(2) var cacheOutCoarse: texture_storage_3d<rgba16float, write>;

const CACHE_RES: i32 = 128;

fn cacheValue(p: vec3f) -> f32 {
    let cc = vec3i(floor((p - F.grid0.xyz) / (F.grid0.w * 8.0)));
    let rc = vec3i(textureDimensions(coarseTex));
    if (any(cc < vec3i(0)) || any(cc >= rc)) { return 100.0; }
    if (textureLoad(coarseTex, cc, 0).r > F.grid1.z * 0.08) { return latent(p); }
    return 100.0;
}

// Two clipmap levels (iteration 2): level 0 is framed on the shot (cbox), level 1 covers the whole density box.
@compute @workgroup_size(4, 4, 4)
fn cs_latent_cache(@builtin(global_invocation_id) gid: vec3u) {
    let c = vec3i(gid);
    if (any(c >= vec3i(CACHE_RES))) { return; }
    let pf = F.cbox.xyz + (vec3f(c) + 0.5) * F.cbox.w / f32(CACHE_RES);
    textureStore(cacheOut, c, vec4f(cacheValue(pf), 0.0, 0.0, 0.0));
    let ext = F.grid0.w * F.grid1.x;
    let pc = F.grid0.xyz + (vec3f(c) + 0.5) * ext / f32(CACHE_RES);
    textureStore(cacheOutCoarse, c, vec4f(cacheValue(pc), 0.0, 0.0, 0.0));
}
