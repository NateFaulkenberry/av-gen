// Clustered forward light assignment (ADR-033). One thread per froxel of a 16x8x24 grid: build the
// froxel's view-space bounds, test every local light's sphere of influence against it and write the
// surviving indices. `shaders/lighting.wgsl` then reads only its own froxel, so meshes, procedural
// instances and SDF surfaces keep one shading path however many lights a world carries.
//
// Depth is sliced exponentially (Olsson et al. 2012), so slice k spans
// [near * (far/near)^(k/z), near * (far/near)^((k+1)/z)] and froxels stay roughly cubic.
//
// Output layout, one storage buffer: `count[cluster]` for every cluster, then a fixed block of
// MAX_LIGHTS_PER_CLUSTER indices per cluster. Indices are local-light indices; the shading pass
// adds the number of directional lights, which are evaluated for every fragment instead.

const MAX_PER_CLUSTER: u32 = 32u;
const MAX_CLUSTER_LIGHTS: u32 = 256u;

struct ClusterParams {
    grid: vec4<u32>,   // x, y, z froxel counts, w = local light count
    depth: vec4<f32>,  // near, far, tan(fovY/2) * aspect, tan(fovY/2)
    // xyz = the light's position in view space, w = its influence radius.
    lights: array<vec4<f32>, 256>,
};

@group(0) @binding(0) var<uniform> params: ClusterParams;
@group(0) @binding(1) var<storage, read_write> clusters: array<u32>;

fn sliceNear(k: u32) -> f32 {
    let near = max(params.depth.x, 1e-4);
    let ratio = max(params.depth.y / near, 1.0001);
    return near * pow(ratio, f32(k) / f32(params.grid.z));
}

@compute @workgroup_size(4, 4, 4)
fn cs_build(@builtin(global_invocation_id) gid: vec3<u32>) {
    if (gid.x >= params.grid.x || gid.y >= params.grid.y || gid.z >= params.grid.z) {
        return;
    }
    let cluster = (gid.z * params.grid.y + gid.y) * params.grid.x + gid.x;
    let clusterCount = params.grid.x * params.grid.y * params.grid.z;

    // The froxel's view-space bounds. The camera looks down -Z, so both z values are negative.
    let x0 = 2.0 * f32(gid.x) / f32(params.grid.x) - 1.0;
    let x1 = 2.0 * f32(gid.x + 1u) / f32(params.grid.x) - 1.0;
    let y0 = 2.0 * f32(gid.y) / f32(params.grid.y) - 1.0;
    let y1 = 2.0 * f32(gid.y + 1u) / f32(params.grid.y) - 1.0;
    let d0 = sliceNear(gid.z);
    let d1 = sliceNear(gid.z + 1u);
    let tx = params.depth.z;
    let ty = params.depth.w;
    let xs = vec4<f32>(x0 * tx * d0, x1 * tx * d0, x0 * tx * d1, x1 * tx * d1);
    let ys = vec4<f32>(y0 * ty * d0, y1 * ty * d0, y0 * ty * d1, y1 * ty * d1);
    let lo = vec3<f32>(min(min(xs.x, xs.y), min(xs.z, xs.w)), min(min(ys.x, ys.y), min(ys.z, ys.w)), -d1);
    let hi = vec3<f32>(max(max(xs.x, xs.y), max(xs.z, xs.w)), max(max(ys.x, ys.y), max(ys.z, ys.w)), -d0);

    var count = 0u;
    let lightCount = min(params.grid.w, MAX_CLUSTER_LIGHTS);
    for (var i = 0u; i < lightCount; i = i + 1u) {
        if (count >= MAX_PER_CLUSTER) {
            break;
        }
        let light = params.lights[i];
        if (light.w <= 0.0) {
            continue;
        }
        let closest = clamp(light.xyz, lo, hi);
        let delta = light.xyz - closest;
        if (dot(delta, delta) <= light.w * light.w) {
            clusters[clusterCount + cluster * MAX_PER_CLUSTER + count] = i;
            count = count + 1u;
        }
    }
    clusters[cluster] = count;
}
