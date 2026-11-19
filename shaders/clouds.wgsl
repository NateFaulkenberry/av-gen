// ADR-1289: sculptural clouds from authored lobes. ADR-2100: Cloud Environment v2 -- the march.
//
// A fullscreen pass at the march's resolution (rendering::CloudRenderer). Per pixel:
//   * The ray is cut to the union of the sea's height band (unbounded in x and z) and the lobes' bounds, then to the
//     opaque scene's depth (a checkerboard of the nearest and farthest of the full-resolution depths under this pixel,
//     so the upsample finds a match on both sides of an edge) and to `maxDistance`.
//   * It is marched with steps that grow with distance (at least `minStep`, at least `stepGrowth` of the distance),
//     doubled through empty air, the first offset by a per-pixel, per-frame jitter that the temporal resolve averages
//     away. The sample positions depend only on the distance from the eye, never on where a lobe begins -- the old
//     march split each ray's lobe span evenly, so the samples sat on shells around the lobes and drew them as rings.
//   * Density is the sea (a coverage map that opens breaks, a height profile, a Perlin-Worley shape) united with the
//     lobes near the sample (read from the xz bins), eroded at the edges by a finer detail noise. Every noise is a tap
//     of one tiling 64^3 volume, offset by the drift.
//   * Light: the key (moon) through a short shadow march, with a two-lobe phase and the powder term; a two-colour
//     ambient by height; the warm horizon light; and the GLOW emitters -- coloured lights inside or below the cloud,
//     falling off over their radius and dimmed by the cloud around the sample (`glowOcclusion`). Distance haze blends
//     each sample toward the haze colour.
// Outputs: premultiplied colour (alpha = 1 - transmittance) and (transmittance-weighted cloud distance, the axial
// scene depth this pixel was cut against) for the resolve and the upsample.

#include "common.wgsl"

struct CloudUniforms {
    light: vec4<f32>,
    lightColor: vec4<f32>,
    ambientTop: vec4<f32>,
    ambientBottom: vec4<f32>,
    warm: vec4<f32>,
    warmColor: vec4<f32>,
    haze: vec4<f32>,
    march: vec4<f32>,
    march2: vec4<f32>,
    sea: vec4<f32>,
    sea2: vec4<f32>,
    boxMin: vec4<f32>,
    boxMax: vec4<f32>,
    grid: vec4<f32>,
    grid2: vec4<f32>,
    drift: vec4<f32>,
    frameInfo: vec4<f32>,
    prevViewProj: mat4x4<f32>,
    glows: array<vec4<f32>, 32>,
};

struct Lobe {
    centreRadius: vec4<f32>,
    scaleDensity: vec4<f32>,
    soft: vec4<f32>,
};

@group(0) @binding(1) var<uniform> cu: CloudUniforms;
@group(0) @binding(2) var cloudDepth: texture_2d<f32>;
@group(0) @binding(3) var cloudNoise: texture_3d<f32>;
@group(0) @binding(4) var cloudSampler: sampler;
@group(0) @binding(5) var<storage, read> lobes: array<Lobe>;
@group(0) @binding(6) var<storage, read> bins: array<u32>;

struct VsOut {
    @builtin(position) clip: vec4<f32>,
};

@vertex
fn vs_clouds(@builtin(vertex_index) index: u32) -> VsOut {
    var positions = array<vec2<f32>, 3>(vec2<f32>(-1.0, -1.0), vec2<f32>(3.0, -1.0), vec2<f32>(-1.0, 3.0));
    var out: VsOut;
    out.clip = vec4<f32>(positions[index], 0.0, 1.0);
    return out;
}

fn remap01(v: f32, lo: f32, hi: f32) -> f32 {
    return clamp((v - lo) / max(hi - lo, 1e-4), 0.0, 1.0);
}

// The shape noise volume holds four features per tile, so a tile spans 4 / noiseScale metres.
fn noiseTap(p: vec3<f32>, cyclesPerMetre: f32) -> vec4<f32> {
    return textureSampleLevel(cloudNoise, cloudSampler, p * (cyclesPerMetre * 0.25), 0.0);
}

// The union of the binned lobes at `p`: x = density, y = the lobes' own height fraction (0 base .. 1 top).
// x = the union's density, y = metres of that lobe still above the sample (the analytic light's depth)
fn lobeBase(p: vec3<f32>) -> vec2<f32> {
    if (cu.boxMin.w < 0.5 || any(p < cu.boxMin.xyz) || any(p > cu.boxMax.xyz)) {
        return vec2<f32>(0.0);
    }
    let cell = vec2<i32>(floor((p.xz - cu.grid.xy) / cu.grid.z));
    let nx = i32(cu.grid.w + 0.5);
    let nz = i32(cu.grid2.x + 0.5);
    if (cell.x < 0 || cell.y < 0 || cell.x >= nx || cell.y >= nz) {
        return vec2<f32>(0.0);
    }
    let k = u32(cell.y * nx + cell.x);
    let offset = bins[2u * k];
    let count = bins[2u * k + 1u];
    var base = 0.0;
    var above = 0.0;
    for (var i = 0u; i < count; i = i + 1u) {
        let l = lobes[bins[offset + i]];
        let ry = l.centreRadius.w * l.scaleDensity.y;
        let d = (p - l.centreRadius.xyz) / (l.centreRadius.w * l.scaleDensity.xyz);
        let r2 = dot(d, d);
        if (r2 >= 1.0) {
            continue;
        }
        let shape = (1.0 - smoothstep(1.0 - l.soft.x, 1.0, sqrt(r2))) * l.scaleDensity.w;
        // the chord of this ellipsoid above the sample, vertically
        let chord = ry * (sqrt(max(1.0 - d.x * d.x - d.z * d.z, 0.0)) - d.y);
        if (shape > base) {
            base = shape;
        }
        above = max(above, chord * shape);
    }
    return vec2<f32>(base, above);
}

struct Density {
    base: f32,   // before the edge detail (the light march reads this)
    height: f32, // 0 at the floor of the cloud, 1 at its top (the ambient gradient)
    above: f32,  // metres of cloud above the sample, roughly (the analytic light when lightSteps is 0)
};

fn densityBase(p: vec3<f32>) -> Density {
    var out: Density;
    out.base = 0.0;
    out.height = 0.0;
    out.above = 0.0;
    let q = p + cu.drift.xyz;
    let swell = cu.sea2.w;
    var sea = 0.0;
    var seaH = -1.0;
    if (cu.sea2.y > 0.5) {
        seaH = (p.y - cu.sea.x) / max(cu.sea.y - cu.sea.x, 1.0);
    }
    let lobeAbove = lobeBase(p);
    let lobe = lobeAbove.x;
    let inSea = seaH >= 0.0 && seaH <= 1.0;
    if (!inSea && lobe <= 0.0) {
        return out;
    }
    let seed = vec3<f32>(cu.grid2.y * 7.13, 0.0, cu.grid2.y * 3.71);
    let shape = noiseTap(q + seed, cu.march.w);
    // Perlin-Worley, sharpened by its own cells
    let sn = remap01(shape.r, (shape.g * 0.625 + shape.b * 0.25) * 0.35, 1.0);
    if (inSea) {
        // the coverage map: two octaves of the slow channel, in xz only, so a break goes all the way down
        let c0 = noiseTap(vec3<f32>(q.x, 37.0, q.z), cu.sea.w).a;
        let c1 = noiseTap(vec3<f32>(q.x * 2.7 + 311.0, 91.0, q.z * 2.7 - 173.0), cu.sea.w).a;
        let cov = c0 * 0.7 + c1 * 0.3;
        let amount = remap01(cov, 1.0 - cu.sea.z - 0.12, 1.0 - cu.sea.z + 0.12);
        // a flat-ish floor and a top that rises where the coverage is dense; within that, only the strongest
        // noise survives near the top, so the tops are billows and the floor is a sheet
        let top = mix(0.3, 1.0, amount);
        let profile = remap01(seaH, 0.0, 0.08) * remap01(top - seaH, 0.0, 0.6);
        sea = remap01(sn, 1.0 - 0.85 * amount * profile, 1.0) * cu.sea2.x;
        out.above = max(top - seaH, 0.0) * (cu.sea.y - cu.sea.x) * 0.6 * amount * cu.sea2.x;
    }
    var lob = 0.0;
    if (lobe > 0.0) {
        lob = clamp(lobe * (1.0 + 0.5 * swell) - cu.warmColor.w * (1.0 - sn), 0.0, 1.0);
    }
    out.base = max(sea, lob);
    out.above = max(out.above, lobeAbove.y * 0.6);
    if (lob > sea) {
        out.height = remap01(p.y, cu.boxMin.y, cu.boxMax.y);
    } else {
        out.height = clamp(seaH, 0.0, 1.0);
    }
    return out;
}

fn detailErode(p: vec3<f32>, base: f32) -> f32 {
    let q = p + cu.drift.xyz * 1.7;
    let d = noiseTap(q, cu.march2.w);
    let dn = d.b * 0.7 + d.g * 0.3;
    // thin, low-density edges lose most; the solid interior keeps its density
    return remap01(base, dn * cu.sea2.z, 1.0);
}

fn cPhase(cosT: f32, g: f32) -> f32 {
    let g2 = g * g;
    return (1.0 - g2) / (12.566371 * pow(max(1.0 + g2 - 2.0 * g * cosT, 1e-4), 1.5));
}

// the ray's [t0, t1] inside the slab lo.y..hi.y (and the box, if `box`)
fn slab(ro: vec3<f32>, rd: vec3<f32>, lo: vec3<f32>, hi: vec3<f32>, box: bool) -> vec2<f32> {
    let inv = 1.0 / select(rd, vec3<f32>(1e-6), abs(rd) < vec3<f32>(1e-6));
    let a = (lo - ro) * inv;
    let b = (hi - ro) * inv;
    let tmin = min(a, b);
    let tmax = max(a, b);
    if (box) {
        return vec2<f32>(max(max(tmin.x, tmin.y), max(tmin.z, 0.0)), min(min(tmax.x, tmax.y), tmax.z));
    }
    return vec2<f32>(max(tmin.y, 0.0), tmax.y);
}

struct MarchOut {
    @location(0) colour: vec4<f32>,
    @location(1) depth: vec4<f32>,
};

@fragment
fn fs_clouds(in: VsOut) -> MarchOut {
    var out: MarchOut;
    out.colour = vec4<f32>(0.0);
    let msize = cu.frameInfo.zw;
    let uv = in.clip.xy / msize;
    let ndc = vec2<f32>(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    let ro = frame.cameraPos.xyz;
    let rd = cameraViewRay(ndc); // ADR-1807
    let fwd = max(dot(rd, frame.cameraForward.xyz), 1e-3);

    // the scene depth under this pixel: the nearest or the farthest of its full-resolution footprint, alternating
    let fsize = vec2<i32>(textureDimensions(cloudDepth, 0));
    let fp = uv * vec2<f32>(fsize) - 0.5;
    let f0 = clamp(vec2<i32>(floor(fp)), vec2<i32>(0), fsize - vec2<i32>(1));
    let f1 = min(f0 + vec2<i32>(1), fsize - vec2<i32>(1));
    let z00 = textureLoad(cloudDepth, f0, 0).r;
    let z10 = textureLoad(cloudDepth, vec2<i32>(f1.x, f0.y), 0).r;
    let z01 = textureLoad(cloudDepth, vec2<i32>(f0.x, f1.y), 0).r;
    let z11 = textureLoad(cloudDepth, f1, 0).r;
    let ip = vec2<i32>(in.clip.xy);
    var axial = min(min(z00, z10), min(z01, z11));
    if (((ip.x + ip.y) & 1) == 1) {
        axial = max(max(z00, z10), max(z01, z11));
    }
    out.depth = vec4<f32>(0.0, axial, 0.0, 0.0);
    let sceneT = axial / fwd;

    // the span: the sea band and the lobes' box, cut at the scene and the reach
    var tA = 1e30;
    var tB = -1e30;
    if (cu.sea2.y > 0.5) {
        let s = slab(ro, rd, vec3<f32>(0.0, cu.sea.x, 0.0), vec3<f32>(0.0, cu.sea.y, 0.0), false);
        if (s.y > s.x) {
            tA = min(tA, s.x);
            tB = max(tB, s.y);
        }
    }
    if (cu.boxMin.w > 0.5) {
        let s = slab(ro, rd, cu.boxMin.xyz, cu.boxMax.xyz, true);
        if (s.y > s.x) {
            tA = min(tA, s.x);
            tB = max(tB, s.y);
        }
    }
    tB = min(tB, min(sceneT, cu.march2.z));
    if (tB <= tA) {
        return out;
    }

    let minStep = cu.march2.x;
    let growth = cu.march2.y;
    // interleaved gradient noise, rotated each frame by the golden-ratio jitter
    let ign = fract(52.9829189 * fract(dot(in.clip.xy, vec2<f32>(0.06711056, 0.00583715))));
    let jitter = fract(ign + cu.frameInfo.x);
    let L = cu.light.xyz;
    let cosT = dot(rd, L);
    let phase = mix(cPhase(cosT, cu.lightColor.w), cPhase(cosT, -0.25), 0.25) * 12.566371;
    let ext = cu.ambientTop.w;
    let lightSteps = u32(cu.march.y + 0.5);
    let lightDt = cu.march.z / max(f32(lightSteps), 1.0);
    let glowCount = u32(cu.boxMax.w + 0.5);
    let steps = u32(cu.march.x + 0.5);

    var trans = 1.0;
    var col = vec3<f32>(0.0);
    var tSum = 0.0;
    var wSum = 0.0;
    var dt = max(minStep, tA * growth);
    var t = tA + dt * jitter;
    for (var s = 0u; s < steps; s = s + 1u) {
        if (t >= tB) {
            break;
        }
        let p = ro + rd * t;
        let db = densityBase(p);
        dt = max(minStep, t * growth);
        if (db.base > 0.0) {
            let dens = detailErode(p, db.base);
            if (dens > 0.003) {
                // the key's light reaching this point (the light march reads the undetailed density)
                var od = 0.0;
                if (lightSteps == 0u) {
                    // analytic: the cloud above the sample, slanted toward the key
                    od = db.above / max(L.y, 0.25);
                }
                for (var j = 0u; j < lightSteps; j = j + 1u) {
                    od = od + densityBase(p + L * ((f32(j) + 0.5) * lightDt)).base * lightDt;
                }
                let shadow = exp(-ext * od * 0.7);
                let powder = mix(1.0, 1.0 - exp(-ext * dens * 160.0), cu.ambientBottom.w * (1.0 - max(cosT, 0.0)));
                let ambient = mix(cu.ambientBottom.rgb, cu.ambientTop.rgb, db.height);
                // warm: low in the sky (by height) and toward the glow (by the ray's own heading)
                let warmLow = 1.0 - smoothstep(0.0, cu.warm.w, p.y - frame.cameraPos.y);
                let warmFace = smoothstep(-0.2, 0.9, dot(normalize(vec3<f32>(rd.x, 0.0, rd.z) + 1e-5), cu.warm.xyz));
                let warm = cu.warmColor.rgb * warmLow * warmFace * (1.0 - 0.6 * db.height) * mix(0.4, 1.0, shadow);
                // the glows: inverse-square-like within their radius, gone by four radii, dimmed by the cloud here
                var glow = vec3<f32>(0.0);
                for (var g = 0u; g < glowCount; g = g + 1u) {
                    let gp = cu.glows[2u * g];
                    let v = gp.xyz - p;
                    let d2 = dot(v, v);
                    let r2 = gp.w * gp.w;
                    if (d2 >= 16.0 * r2) {
                        continue;
                    }
                    let fall = max(1.0 / (1.0 + d2 / r2) - 0.0588, 0.0) * 1.0625;
                    let occ = exp(-ext * cu.grid2.w * dens * sqrt(d2) * 0.5);
                    glow = glow + cu.glows[2u * g + 1u].rgb * (fall * occ);
                }
                glow = glow * cu.grid2.z;
                var radiance = cu.lightColor.rgb * cu.light.w * shadow * phase * powder + ambient + warm + glow;
                let hz = 1.0 - exp(-t / max(cu.haze.w, 1.0));
                radiance = mix(radiance, cu.haze.rgb, hz);
                let a = 1.0 - exp(-ext * dens * dt);
                let w = trans * a;
                col = col + w * radiance;
                tSum = tSum + w * t;
                wSum = wSum + w;
                trans = trans * (1.0 - a);
                if (trans < 0.01) {
                    break;
                }
            }
        } else {
            dt = dt * 2.0; // empty air: stride
        }
        t = t + dt;
    }
    let alpha = 1.0 - trans;
    out.colour = vec4<f32>(col, alpha);
    out.depth = vec4<f32>(select(0.0, tSum / max(wSum, 1e-6), wSum > 0.0), axial, 0.0, 0.0);
    return out;
}
