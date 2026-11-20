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
    sea3: vec4<f32>,
    sea4: vec4<f32>,
    sea5: vec4<f32>,
    sea6: vec4<f32>,
    boxMin: vec4<f32>,
    boxMax: vec4<f32>,
    grid: vec4<f32>,
    grid2: vec4<f32>,
    drift: vec4<f32>,
    frameInfo: vec4<f32>,
    prevViewProj: mat4x4<f32>,
    glows: array<vec4<f32>, 64>, // 2 x kMaxCloudGlows (ADR-2104: 24; ADR-2107: 32)
    beams: array<vec4<f32>, 24>,
    beamInfo: vec4<f32>,
    halo: vec4<f32>,
    haloColor: vec4<f32>,
    glowShape: vec4<f32>, // ADR-2104: x = breakup, y = shadow, z = breakup scale, w = shadow tap distance (m)
    farGrid: vec4<f32>,   // ADR-2104: the far lobe set's grid: origin xz, cell size, cells along x
    farGrid2: vec4<f32>,  // cells along z, its lobes' lowest bottom, highest top, 1 when there is a far set
    heroSpan: vec4<f32>,  // the hero grid's own height range (x, y), z = where the far grid's cell words start
    erosion: vec4<f32>,   // ADR-2106: x = strength (0 = off), y = the coarse billow's wavelength (m), z = fine detail, w = curl (m)
    erosion2: vec4<f32>,  // ADR-2106: x = octaves (1..3), y = light tap spacing (1 = even, 2 = squared + jitter),
                          // z = multiple scattering (0..1), w = base flattening (0..1)
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

// ADR-2106: the erosion noise at one point, shared by every lobe there: three octaves of billow noise (0..1, high =
// the lobe's own surface kept, low = eaten), each a world-space tap of the shared volume, the coarse two distorted by
// a curl-like offset (tatters). `fine` is the third octave's weight after the pixel-footprint fade (no shimmer far off).
struct Ero {
    c: f32,
    m: f32,
    f: f32,
    on: f32,
    fp: f32, // metres: the larger of a pixel's footprint and half the march step here (no edge or octave finer)
};

fn noErosion() -> Ero {
    return Ero(1.0, 1.0, 1.0, 0.0, 0.0);
}

fn erosionAt(p: vec3<f32>, octaves: u32, footprint: f32) -> Ero {
    var e: Ero;
    e.c = 1.0;
    e.m = 1.0;
    e.f = 1.0;
    e.on = 0.0;
    e.fp = footprint;
    if (cu.erosion.x <= 0.0) {
        return e;
    }
    e.on = 1.0;
    let lam = max(cu.erosion.y, 1.0);
    // seek-safe: a pure function of position and the drift offset (itself a function of the timeline second); the
    // octaves move at different fractions of the wind so the billows evolve slowly instead of sliding as one sheet
    var q = p + cu.drift.xyz * 0.35;
    if (cu.erosion.w > 0.0) {
        let w = noiseTap(q * 0.37 + vec3<f32>(-71.0, 13.0, 29.0), 1.0 / lam);
        q = q + (vec3<f32>(w.r, w.a, w.g) - vec3<f32>(0.5, 0.5, 0.45)) * (2.0 * cu.erosion.w);
    }
    // the volume's G channel is inverted Worley (4/8/16 features a tile): rounded heads where it is high
    let nc = noiseTap(q, 1.0 / lam);
    // far off, the march's steps (a fiftieth of the distance) outgrow even the coarse octave: fade it there too, or it
    // aliases into diagonal stripes on the far towers
    e.c = mix(1.0, nc.g * 0.8 + nc.r * 0.2, 1.0 - smoothstep(0.6 * lam, 1.6 * lam, footprint));
    if (octaves > 1u) {
        let nm = noiseTap(q * 1.0 + cu.drift.xyz * 0.25 + vec3<f32>(37.0, -11.0, 5.0), 3.3 / lam);
        let lm = lam / 3.3;
        e.m = mix(1.0, nm.b * 0.75 + nm.g * 0.25, 1.0 - smoothstep(0.7 * lm, 2.2 * lm, footprint));
    }
    if (octaves > 2u) {
        let lf = lam / 10.0;
        let fade = 1.0 - smoothstep(0.25 * lf, 0.9 * lf, footprint);
        if (fade > 0.0) {
            let nf = noiseTap(p + cu.drift.xyz * 0.6 + vec3<f32>(-3.0, 19.0, 61.0), 1.0 / lf);
            e.f = mix(1.0, nf.b, fade);
        }
    }
    return e;
}

// The union of the binned lobes at `p`: x = density, y = the lobes' own height fraction (0 base .. 1 top).
// x = the union's density, y = metres of that lobe still above the sample (the analytic light's depth)
fn lobeBase(p: vec3<f32>, e: Ero) -> vec2<f32> {
    if (cu.boxMin.w < 0.5 || any(p < cu.boxMin.xyz) || any(p > cu.boxMax.xyz)) {
        return vec2<f32>(0.0);
    }
    var r = lobeGrid(p, cu.grid.xy, cu.grid.z, i32(cu.grid.w + 0.5), i32(cu.grid2.x + 0.5), cu.heroSpan.x,
                     cu.heroSpan.y, 0u, e);
    if (cu.farGrid2.w > 0.5) { // ADR-2104: the far set on its own grid
        let f = lobeGrid(p, cu.farGrid.xy, cu.farGrid.z, i32(cu.farGrid.w + 0.5), i32(cu.farGrid2.x + 0.5),
                         cu.farGrid2.y, cu.farGrid2.z, u32(cu.heroSpan.z + 0.5), e);
        r = vec2<f32>(max(r.x, f.x), max(r.y, f.y));
    }
    return r;
}

// One grid's lobes at p (ADR-2100 bins; ADR-2104: the hero and the far set each have one). yLo/yHi are the range the
// grid's column extents are quantised over.
fn lobeGrid(p: vec3<f32>, origin: vec2<f32>, cellSize: f32, nx: i32, nz: i32, yLo: f32, yHi: f32, base0: u32,
            e: Ero) -> vec2<f32> {
    let cell = vec2<i32>(floor((p.xz - origin) / cellSize));
    if (cell.x < 0 || cell.y < 0 || cell.x >= nx || cell.y >= nz) {
        return vec2<f32>(0.0);
    }
    let k = u32(cell.y * nx + cell.x);
    let offset = bins[base0 + 2u * k];
    let packed = bins[base0 + 2u * k + 1u];
    let count = packed & 0xFFu;
    // ADR-2101: outside the column's height range there is nothing to read
    let span = max(yHi - yLo, 1.0);
    let y0 = yLo + f32((packed >> 8u) & 0xFFFu) / 4095.0 * span;
    let y1 = yLo + f32(packed >> 20u) / 4095.0 * span;
    if (p.y < y0 || p.y > y1) {
        return vec2<f32>(0.0);
    }
    var base = 0.0;
    var above = 0.0;
    for (var i = 0u; i < count; i = i + 1u) {
        let l = lobes[bins[offset + i]];
        let ry = l.centreRadius.w * l.scaleDensity.y;
        var d = (p - l.centreRadius.xyz) / (l.centreRadius.w * l.scaleDensity.xyz);
        let r2 = dot(d, d);
        if (r2 >= 1.0) {
            continue;
        }
        var shape = 0.0;
        if (e.on > 0.0 && l.soft.y > 0.0) {
            // ADR-2106: the lobe's surface eaten in metres by the billow octaves, so it is no longer a sphere. Only
            // ever inward (the bins hold the lobe's own bounds). The lower half is flattened and its noise inverted
            // (wisps and tatters under a base, cauliflower heads over a crown), and its edge is softer.
            let k = cu.erosion.x * l.soft.y; // ADR-2107: the lobe's own share
            let hl = clamp(0.5 + 0.5 * d.y, 0.0, 1.0);
            if (d.y < 0.0) {
                d.y = d.y * (1.0 + cu.erosion2.w * k);
            }
            let rr = sqrt(dot(d, d));
            if (rr >= 1.0) {
                continue;
            }
            let rm = l.centreRadius.w * min(min(l.scaleDensity.x, l.scaleDensity.y), l.scaleDensity.z);
            let lam = cu.erosion.y;
            let crown = smoothstep(0.2, 0.65, hl);
            let c = smoothstep(0.15, 0.85, mix(1.0 - e.c, e.c, crown));
            let m = mix(1.0 - e.m, e.m, crown);
            let f = mix(1.0 - e.f, e.f, crown);
            // each octave's depth follows the lobe's size, so a head 15 m across is broken by the mid and fine
            // octaves and a 200 m tower by the coarse one
            let eaten = min(0.38 * rm, 0.6 * lam) * (1.0 - c) + min(0.3 * rm, 0.18 * lam) * (1.0 - m) * cu.erosion.z +
                        min(0.2 * rm, 0.06 * lam) * (1.0 - f) * cu.erosion.z;
            let re = rr + k * eaten / rm;
            // the edge: the authored softness, toward a few metres (crisp heads, softer bases) as the erosion rises
            let edgeM = mix(l.soft.x * rm, max(lam * mix(0.28, 0.07, crown), e.fp), k);
            shape = (1.0 - smoothstep(1.0 - clamp(edgeM / rm, 0.02, 1.0), 1.0, re)) * l.scaleDensity.w;
        } else {
            shape = (1.0 - smoothstep(1.0 - l.soft.x, 1.0, sqrt(r2))) * l.scaleDensity.w;
        }
        // the chord of this ellipsoid above the sample, vertically
        let chord = ry * (sqrt(max(1.0 - d.x * d.x - d.z * d.z, 0.0)) - d.y);
        if (shape > base) {
            base = shape;
        }
        above = max(above, chord * shape);
    }
    return vec2<f32>(base, above);
}

// ADR-2101: the relief sea's top surface (metres) at xz: one rounded swell plus up to three octaves of billow noise
// (|n| inverted: rounded tops, creased troughs), lowered to the floor inside the breaks and where the coverage is thin.
fn seaSurface(xz: vec2<f32>, octaves: u32) -> f32 {
    let f = cu.sea3.y;
    let s0 = (noiseTap(vec3<f32>(xz.x, 211.0, xz.y), f).a - 0.5) * 2.5;
    var h = 0.55 * s0;
    if (octaves > 1u) {
        let s1 = 1.0 - abs((noiseTap(vec3<f32>(xz.x * 4.3 + 57.0, 401.0, xz.y * 4.3 - 91.0), f).a - 0.5) * 3.0);
        h = h + 0.28 * (s1 * 2.0 - 1.0);
    }
    if (octaves > 2u) {
        let s2 = 1.0 - abs((noiseTap(vec3<f32>(xz.x * 10.5 - 13.0, 613.0, xz.y * 10.5 + 29.0), f).a - 0.5) * 3.0);
        h = h + 0.17 * (s2 * 2.0 - 1.0);
    }
    if (octaves > 3u) {
        let s3 = 1.0 - abs((noiseTap(vec3<f32>(xz.x * 26.0 + 7.0, 877.0, xz.y * 26.0 + 3.0), f).a - 0.5) * 3.0);
        h = h + 0.09 * (s3 * 2.0 - 1.0);
    }
    var surface = cu.sea3.w + cu.sea3.x * h;
    // the breaks
    let floorY = cu.sea.x - 40.0;
    let nb = u32(cu.sea6.z + 0.5);
    for (var b = 0u; b < nb; b = b + 1u) {
        let br = select(cu.sea4, cu.sea5, b == 1u);
        let yaw = select(cu.sea6.x, cu.sea6.y, b == 1u);
        let v = xz - br.xy;
        let c = cos(yaw);
        let sn = sin(yaw);
        let lx = (c * v.x - sn * v.y) / max(br.w, 0.1);
        let lz = sn * v.x + c * v.y;
        let d = length(vec2<f32>(lx, lz)) / max(br.z, 1.0);
        surface = mix(floorY, surface, smoothstep(0.7, 1.2, d + 0.12 * h));
    }
    if (cu.sea.z < 0.999) {
        let cov = noiseTap(vec3<f32>(xz.x, 37.0, xz.y), cu.sea.w).a;
        let amount = remap01(cov, 1.0 - cu.sea.z - 0.12, 1.0 - cu.sea.z + 0.12);
        surface = mix(floorY, surface, amount);
    }
    return surface;
}

// ADR-2101: metres of sea along the ray toward the key, from the height field (crests lit, troughs in shadow)
fn seaShadow(p: vec3<f32>, L: vec3<f32>) -> f32 {
    var od = 0.0;
    var prev = 0.0;
    let ds = array<f32, 4>(10.0, 35.0, 100.0, 260.0);
    for (var i = 0u; i < 4u; i = i + 1u) {
        let d = ds[i];
        let q = p + L * d;
        let surf = seaSurface(q.xz + cu.drift.xz, 3u) + cu.sea3.z * 0.3;
        od = od + clamp((surf - q.y) / 12.0, 0.0, 1.0) * (d - prev);
        prev = d;
    }
    return od;
}

struct Density {
    base: f32,   // before the edge detail (the light march reads this)
    height: f32, // 0 at the floor of the cloud, 1 at its top (the ambient gradient)
    above: f32,  // metres of cloud above the sample, roughly (the analytic light when lightSteps is 0)
};

fn densityBase(p: vec3<f32>, e: Ero) -> Density {
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
    let lobeAbove = lobeBase(p, e);
    let lobe = lobeAbove.x;
    let inSea = seaH >= 0.0 && seaH <= 1.0;
    if (!inSea && lobe <= 0.0) {
        return out;
    }
    let seed = vec3<f32>(cu.grid2.y * 7.13, 0.0, cu.grid2.y * 3.71);
    let shape = noiseTap(q + seed, cu.march.w);
    // Perlin-Worley, sharpened by its own cells
    let sn = remap01(shape.r, (shape.g * 0.625 + shape.b * 0.25) * 0.35, 1.0);
    if (inSea && cu.sea3.x > 0.0) {
        // ADR-2101: the relief sea -- solid below a billowed top surface
        let surface = seaSurface(q.xz, 4u) + cu.sea3.z * (sn - 0.55) * 2.0;
        let below = surface - p.y;
        sea = remap01(below, 0.0, 4.0 + 0.25 * cu.sea3.z) * remap01(p.y - cu.sea.x, 0.0, 40.0) * cu.sea2.x;
        out.above = -1.0; // the march asks the height field (seaShadow)
    } else if (inSea) {
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
    out.above = select(max(out.above, lobeAbove.y * 0.6), -1.0 - lobeAbove.y * 0.6, out.above < 0.0 && sea >= lob);
    if (lob > sea) {
        // ADR-2105: over the hero lobes' own height range (the box also holds the far set, which put every hero
        // formation in its bottom tenth: all base, no crown); the same range as the box when there is no far set
        out.height = remap01(p.y, cu.heroSpan.x, cu.heroSpan.y);
    } else {
        out.height = clamp(seaH, 0.0, 1.0);
    }
    return out;
}

// ADR-2109: ADR-1289's density, for a block written for its march (evenSteps): the lobes' soft ellipsoids (the same
// union lobeBase reads, uneroded) shaped by billows of domain-warped value noise and eroded into wisps by a finer fbm.
// Ported unchanged from the ADR-1289 shader so the biovalley shots keep the look they were authored with.
fn legacyHash3(p: vec3<f32>) -> f32 {
    let q = fract(p * vec3<f32>(0.1031, 0.1030, 0.0973));
    let r = q + dot(q, q.yxz + 33.33);
    return fract((r.x + r.y) * r.z);
}

fn legacyNoise(p: vec3<f32>) -> f32 {
    let i = floor(p);
    let f = p - i;
    let u = f * f * (3.0 - 2.0 * f);
    let a = mix(mix(legacyHash3(i), legacyHash3(i + vec3<f32>(1.0, 0.0, 0.0)), u.x),
                mix(legacyHash3(i + vec3<f32>(0.0, 1.0, 0.0)), legacyHash3(i + vec3<f32>(1.0, 1.0, 0.0)), u.x), u.y);
    let b = mix(mix(legacyHash3(i + vec3<f32>(0.0, 0.0, 1.0)), legacyHash3(i + vec3<f32>(1.0, 0.0, 1.0)), u.x),
                mix(legacyHash3(i + vec3<f32>(0.0, 1.0, 1.0)), legacyHash3(i + vec3<f32>(1.0, 1.0, 1.0)), u.x), u.y);
    return mix(a, b, u.z);
}

fn legacyFbm(p: vec3<f32>, octaves: u32) -> f32 {
    var s = 0.0;
    var a = 0.5;
    var q = p;
    for (var o = 0u; o < octaves; o = o + 1u) {
        s = s + a * legacyNoise(q);
        q = q * 2.03 + vec3<f32>(17.1, -9.3, 4.7);
        a = a * 0.5;
    }
    return s / (1.0 - pow(0.5, f32(octaves)));
}

fn legacyDensity(p: vec3<f32>, detail: u32) -> f32 {
    let base = lobeBase(p, noErosion()).x;
    if (base <= 0.0) {
        return 0.0;
    }
    let ns = cu.march.w;
    let seed = vec3<f32>(cu.grid2.y * 7.13, cu.grid2.y * 3.71, 0.0);
    let warp = vec3<f32>(legacyNoise(p * ns * 0.5 + seed), legacyNoise(p * ns * 0.5 + seed + 11.0),
                         legacyNoise(p * ns * 0.5 + seed + 23.0)) - 0.5;
    let n2 = legacyFbm(p * ns + warp * 2.5 + seed, detail);
    let billow = legacyFbm(p * ns * 0.35 + warp + seed * 0.5, 3u);
    let a = cu.warmColor.w;
    let shaped = base * mix(1.0, smoothstep(0.25, 0.7, billow), a);
    return clamp((shaped - a * (1.0 - n2) * 0.9) * (1.0 + a), 0.0, 1.0);
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


// ADR-2101: how far an empty sample can jump. Above the sea band, a sample in a bin column with no lobes (or outside the
// lobes' box) can go straight to the nearer of the sea's top, the column's exit and the box's entry: the march spent
// its whole budget crossing kilometres of empty air before the sea when the eye was inside the lobes' box.
fn emptySkip(ro: vec3<f32>, rd: vec3<f32>, t: f32, seaEntry: f32, boxEntry: f32) -> f32 {
    let p = ro + rd * t;
    if (cu.sea2.y > 0.5 && p.y <= cu.sea.y) {
        return t;
    }
    var goal = seaEntry;
    let inBox = cu.boxMin.w > 0.5 && all(p >= cu.boxMin.xyz) && all(p <= cu.boxMax.xyz);
    if (inBox) {
        goal = skipGrid(ro, rd, p, goal, cu.grid.xy, cu.grid.z, i32(cu.grid.w + 0.5), i32(cu.grid2.x + 0.5),
                        cu.heroSpan.x, cu.heroSpan.y, 0u);
        if (goal < 0.0) {
            return t;
        }
        if (cu.farGrid2.w > 0.5) { // ADR-2104: and the far set's grid
            goal = skipGrid(ro, rd, p, goal, cu.farGrid.xy, cu.farGrid.z, i32(cu.farGrid.w + 0.5),
                            i32(cu.farGrid2.x + 0.5), cu.farGrid2.y, cu.farGrid2.z, u32(cu.heroSpan.z + 0.5));
            if (goal < 0.0) {
                return t;
            }
        }
    } else if (cu.boxMin.w > 0.5 && boxEntry > t) {
        goal = min(goal, boxEntry);
    }
    return max(t, goal);
}

// One grid's limit on an empty-air jump from p (ADR-2101; ADR-2104 made it per grid): -1 when p is inside a column's
// lobes (no jump), else `goal` shortened to where the ray meets the column's band or leaves the column.
fn skipGrid(ro: vec3<f32>, rd: vec3<f32>, p: vec3<f32>, goalIn: f32, origin: vec2<f32>, cellSize: f32, nx: i32,
            nz: i32, yLo: f32, yHi: f32, base0: u32) -> f32 {
    var goal = goalIn;
    let cell = vec2<i32>(floor((p.xz - origin) / cellSize));
    if (cell.x >= 0 && cell.y >= 0 && cell.x < nx && cell.y < nz) {
        let k = u32(cell.y * nx + cell.x);
        let packed = bins[base0 + 2u * k + 1u];
        if ((packed & 0xFFu) > 0u) {
            let span = max(yHi - yLo, 1.0);
            let y0 = yLo + f32((packed >> 8u) & 0xFFFu) / 4095.0 * span;
            let y1 = yLo + f32(packed >> 20u) / 4095.0 * span;
            if (p.y >= y0 && p.y <= y1) {
                return -1.0;
            }
            // above or below this column's lobes: go no further than where the ray meets their band
            if (p.y > y1 && rd.y < 0.0) {
                goal = min(goal, (y1 - ro.y) / rd.y);
            } else if (p.y < y0 && rd.y > 0.0) {
                goal = min(goal, (y0 - ro.y) / rd.y);
            }
        }
    }
    // the column's exit in x and z
    let lo = origin + vec2<f32>(cell) * cellSize;
    let hi = lo + vec2<f32>(cellSize);
    let inv = 1.0 / select(rd.xz, vec2<f32>(1e-6), abs(rd.xz) < vec2<f32>(1e-6));
    let tx = max((lo - ro.xz) * inv, (hi - ro.xz) * inv);
    return min(goal, min(tx.x, tx.y) + 0.25);
}

// ADR-2102: the fall of a beam's light along its length: full at the lamp, gone at its end.
fn beamFade(s: f32, len: f32) -> f32 {
    let u = clamp(s / len, 0.0, 1.0);
    return (1.0 - u) * (1.0 - u) / (1.0 + 3.0 * u);
}

// ADR-2102: a beam arm's light on a cloud sample at p (the soft cone's profile at p, faded along the beam).
fn beamOnCloud(p: vec3<f32>, arm: u32) -> vec3<f32> {
    let o = cu.beams[3u * arm];
    let d = cu.beams[3u * arm + 1u];
    let v = p - o.xyz;
    let s = dot(v, d.xyz);
    if (s <= 0.0 || s >= o.w) {
        return vec3<f32>(0.0);
    }
    let c = cu.beams[3u * arm + 2u];
    let w = c.w + s * d.w;
    let r2 = max(dot(v, v) - s * s, 0.0);
    return c.rgb * (exp(-2.0 * r2 / (w * w)) * beamFade(s, o.w));
}

// ADR-2102: a beam arm's scatter in the air along the view ray, as one lump at the ray's closest approach to the arm's
// axis: (distance along the ray, light). The profile integrated across the cone grows as the ray grazes the axis
// (1 / sin of the angle between them, capped where the ray would run down the beam).
fn beamAir(ro: vec3<f32>, rd: vec3<f32>, arm: u32) -> vec4<f32> {
    let o = cu.beams[3u * arm];
    let d = cu.beams[3u * arm + 1u];
    let c = cu.beams[3u * arm + 2u];
    let w0 = ro - o.xyz;
    let b = dot(rd, d.xyz);
    let dd = dot(rd, w0);
    let e = dot(d.xyz, w0);
    let den = max(1.0 - b * b, 1e-6);
    var sb = clamp((e - b * dd) / den, 0.0, o.w);
    let q = o.xyz + d.xyz * sb;
    let tv = max(dot(q - ro, rd), 0.0);
    let r = ro + rd * tv - q;
    let w = c.w + sb * d.w;
    let prof = exp(-2.0 * dot(r, r) / (w * w));
    let sinT = max(sqrt(den), 0.12);
    return vec4<f32>(tv, c.rgb * (prof * beamFade(sb, o.w) / sinT));
}

// ADR-2102: the glow around a bright body in the sky (the moon's corona): an inner and a four times wider outer glow.
fn haloLight(rd: vec3<f32>) -> vec3<f32> {
    let ang = acos(clamp(dot(rd, cu.halo.xyz), -1.0, 1.0));
    let w = max(cu.halo.w, 1e-4);
    return cu.haloColor.rgb * (exp(-ang / w) + 0.3 * exp(-ang / (4.0 * w)));
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
    // ADR-2106: the angle one march pixel spans (the erosion's finest octave fades out below it)
    let pixAngle = length(cameraViewRay(ndc + vec2<f32>(2.0 / cu.frameInfo.z, 0.0)) - rd);
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
    var seaEntry = 1e30;
    var boxEntry = 1e30;
    if (cu.sea2.y > 0.5) {
        let s = slab(ro, rd, vec3<f32>(0.0, cu.sea.x, 0.0), vec3<f32>(0.0, cu.sea.y, 0.0), false);
        if (s.y > s.x) {
            tA = min(tA, s.x);
            tB = max(tB, s.y);
            seaEntry = s.x;
        }
    }
    if (cu.boxMin.w > 0.5) {
        let s = slab(ro, rd, cu.boxMin.xyz, cu.boxMax.xyz, true);
        if (s.y > s.x) {
            tA = min(tA, s.x);
            tB = max(tB, s.y);
            boxEntry = s.x;
        }
    }
    let reach = min(sceneT, cu.march2.z);
    tB = min(tB, reach);
    // ADR-2102: the beams' air scatter waits at its distance along the ray for the march to say how much cloud is in
    // front of it; the halo sits behind everything, where the ray reaches the sky.
    let arms = u32(cu.beamInfo.x + 0.5);
    var airT: array<f32, 8>;
    var airC: array<vec3<f32>, 8>;
    for (var k = 0u; k < 8u; k = k + 1u) {
        airT[k] = 1e30;
        airC[k] = vec3<f32>(0.0);
    }
    if (cu.beamInfo.y > 0.0) {
        for (var k = 0u; k < arms; k = k + 1u) {
            let a = beamAir(ro, rd, k);
            if (a.x < sceneT && dot(a.yzw, vec3<f32>(1.0)) > 1e-5) {
                airT[k] = a.x;
                airC[k] = a.yzw * cu.beamInfo.y;
            }
        }
    }
    var sky = vec3<f32>(0.0);
    if (cu.beamInfo.w > 0.5 && sceneT >= cu.march2.z) {
        sky = haloLight(rd);
    }
    if (tB <= tA) {
        var add = sky;
        for (var k = 0u; k < arms; k = k + 1u) {
            add = add + airC[k];
        }
        out.colour = vec4<f32>(add, 0.0);
        return out;
    }

    let minStep = cu.march2.x;
    let growth = cu.march2.y;
    // interleaved gradient noise, rotated each frame by the golden-ratio jitter
    let ign = fract(52.9829189 * fract(dot(in.clip.xy, vec2<f32>(0.06711056, 0.00583715))));
    let jitter = fract(ign + cu.frameInfo.x);
    let ign2 = fract(52.9829189 * fract(dot(in.clip.yx + vec2<f32>(11.0, 5.0), vec2<f32>(0.06711056, 0.00583715))));
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
    // ADR-2109: growth < 0 is ADR-1289's march: `steps` even steps across the span, no stride or skip
    let even = growth < 0.0;
    let evenDt = (tB - tA) / f32(max(steps, 1u));
    var dt = select(max(minStep, tA * growth), evenDt, even);
    var t = tA + dt * jitter;
    for (var s = 0u; s < steps; s = s + 1u) {
        if (t >= tB) {
            break;
        }
        let p = ro + rd * t;
        let octaves = u32(clamp(cu.erosion2.x, 1.0, 3.0) + 0.5);
        let ero = erosionAt(p, octaves, max(t * pixAngle, 0.5 * max(minStep, t * growth)));
        var db = densityBase(p, ero);
        dt = select(max(minStep, t * growth), evenDt, even);
        if (even && db.base > 0.0) {
            db.base = legacyDensity(p, 4u); // ADR-2109
        }
        if (db.base > 0.0) {
            // ADR-2111: the cloud within two minimum steps of the eye fades out: there the reprojection has nothing to
            // hold (parallax of metres a frame) and a fast camera leaving a cloud saw its last metres as a stipple
            let dens = select(detailErode(p, db.base), db.base, even) *
                       select(smoothstep(0.0, 2.0 * minStep, t), 1.0, even);
            if (dens > 0.003) {
                // the key's light reaching this point (the light march reads the undetailed density)
                var od = 0.0;
                if (even) {
                    // ADR-2109: ADR-1289's light march -- even taps through the eroded density, no 0.7
                    for (var j = 1u; j <= lightSteps; j = j + 1u) {
                        od = od + legacyDensity(p + L * (f32(j) * lightDt), 2u) * lightDt;
                    }
                    od = od / 0.7;
                } else if (lightSteps == 0u) {
                    if (db.above < 0.0) {
                        // ADR-2101: the relief sea shadows itself through its height field
                        od = seaShadow(p, L) + (-1.0 - db.above) / max(L.y, 0.25);
                    } else {
                        // analytic: the cloud above the sample, slanted toward the key
                        od = db.above / max(L.y, 0.25);
                    }
                }
                if (even) {
                    // ADR-2109: taken above
                } else if (cu.erosion2.y > 1.5) {
                    // ADR-2106: taps spaced by the square of their index (dense near the sample, where a billow
                    // shadows its neighbour; sparse far out), jittered per pixel and frame so the temporal resolve
                    // integrates between them. Even taps of tens of metres read the shadow as hard silhouettes of
                    // whole lobes cast on the cloud below (the dark blotches of Neon Dreams iteration 6).
                    // ADR-2111: per pixel (a second interleaved-gradient pattern, transposed so it does not follow the
                    // step jitter) plus the frame's own low-discrepancy offset. One value for the whole frame was a slow
                    // ramp at 30 fps that every cloud's shadow followed and snapped back from three times a second.
                    let lj = 0.5 + 0.35 * (fract(ign2 + cu.heroSpan.w) - 0.5);
                    let n = f32(lightSteps);
                    var prevS = 0.0;
                    for (var j = 0u; j < lightSteps; j = j + 1u) {
                        let u1 = (f32(j) + 1.0) / n;
                        let s1 = cu.march.z * u1 * u1;
                        let sm = mix(prevS, s1, lj);
                        let lp = p + L * sm;
                        let le = erosionAt(lp, min(octaves, 2u), 4.0); // a light tap keeps a crisp edge (the shadow shapes the heads)
                        od = od + densityBase(lp, le).base * (s1 - prevS);
                        prevS = s1;
                    }
                } else {
                    for (var j = 0u; j < lightSteps; j = j + 1u) {
                        od = od + densityBase(p + L * ((f32(j) + 0.5) * lightDt), noErosion()).base * lightDt;
                    }
                }
                var shadow = exp(-ext * od * 0.7);
                var keyLight = shadow * phase;
                if (cu.erosion2.z > 0.0) {
                    // ADR-2106: multiple scattering (Wrenninge's octaves): light that scattered more than once is
                    // attenuated less and spread more evenly, so a body in its own shadow is dim, not black
                    let ms = cu.erosion2.z;
                    keyLight = keyLight + 0.5 * ms * exp(-ext * od * 0.7 * 0.35) *
                                   (mix(cPhase(cosT, cu.lightColor.w * 0.5), cPhase(cosT, -0.1), 0.25) * 12.566371);
                    keyLight = keyLight + 0.25 * ms * exp(-ext * od * 0.7 * 0.12);
                    shadow = max(shadow, 0.5 * ms * exp(-ext * od * 0.7 * 0.35));
                }
                let powder = mix(1.0, 1.0 - exp(-ext * dens * 160.0), cu.ambientBottom.w * (1.0 - max(cosT, 0.0)));
                let ambient = mix(cu.ambientBottom.rgb, cu.ambientTop.rgb, db.height);
                // warm: low in the sky (by height) and toward the glow (by the ray's own heading)
                let warmLow = 1.0 - smoothstep(0.0, cu.warm.w, p.y - frame.cameraPos.y);
                let warmFace = smoothstep(-0.2, 0.9, dot(normalize(vec3<f32>(rd.x, 0.0, rd.z) + 1e-5), cu.warm.xyz));
                let warm = cu.warmColor.rgb * warmLow * warmFace * (1.0 - 0.6 * db.height) * mix(0.4, 1.0, shadow);
                // the glows: inverse-square-like within their radius, gone by four radii, dimmed by the cloud here
                var glow = vec3<f32>(0.0);
                var glowTo = vec3<f32>(0.0); // ADR-2104: toward the glow that lights this sample most
                var glowBest = 0.0;
                var glowBestShadow = 1.0; // ADR-2107: that glow's share of the shadow
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
                    let c = cu.glows[2u * g + 1u].rgb * (fall * occ);
                    glow = glow + c;
                    let w = dot(c, vec3<f32>(0.3, 0.5, 0.2));
                    if (w > glowBest) {
                        glowBest = w;
                        glowBestShadow = cu.glows[2u * g + 1u].w;
                        glowTo = v * inverseSqrt(max(d2, 1e-4));
                    }
                }
                glow = glow * cu.grid2.z;
                // ADR-2104: a glow lights the cloud's own structure, not a sphere around itself. Breakup: pockets of
                // the cloud's coarse noise take more of it and the gaps between them less (inner-lit pockets);
                // shadow: one tap of the cloud toward the strongest glow, so billows facing it are lit and the far
                // sides fall dark. Both off (0) by default.
                if (glowBest > 1e-5 && (cu.glowShape.x > 0.0 || cu.glowShape.y > 0.0)) {
                    if (cu.glowShape.x > 0.0) {
                        let n = noiseTap(p + cu.drift.xyz * 0.6 + vec3<f32>(17.0, 3.0, -41.0), cu.glowShape.z);
                        let pocket = smoothstep(0.3, 0.78, n.r * 0.7 + n.g * 0.3);
                        glow = glow * mix(1.0, 0.12 + 1.9 * pocket * pocket, cu.glowShape.x);
                    }
                    if (cu.glowShape.y > 0.0) {
                        // ADR-2105: the lobes alone (no sea relief, no noise): the glows sit among the lobes, and the
                        // full density cost ~25 ms a frame in the Neon Dreams Veil
                        let toward = min(lobeBase(p + glowTo * cu.glowShape.w, noErosion()).x, 1.0);
                        glow = glow * exp(-cu.glowShape.y * glowBestShadow * toward * 3.0);
                    }
                }
                // ADR-2102: the beams on the cloud
                for (var k = 0u; k < arms; k = k + 1u) {
                    glow = glow + beamOnCloud(p, k) * cu.beamInfo.z;
                }
                var radiance = cu.lightColor.rgb * cu.light.w * keyLight * powder + ambient + warm + glow;
                let hz = 1.0 - exp(-t / max(cu.haze.w, 1.0));
                radiance = mix(radiance, cu.haze.rgb, hz);
                var a = 1.0 - exp(-ext * dens * dt);
                if (cu.sea6.w > 0.0) {
                    a = a * (1.0 - smoothstep(cu.sea6.w, cu.march2.z, t)); // ADR-2101: the far sea fades out
                }
                let w = trans * a;
                // ADR-2102: a beam's air scatter in front of this sample is seen through the cloud so far
                for (var k = 0u; k < arms; k = k + 1u) {
                    if (airT[k] < t) {
                        col = col + airC[k] * trans;
                        airT[k] = 1e30;
                    }
                }
                col = col + w * radiance;
                tSum = tSum + w * t;
                wSum = wSum + w;
                trans = trans * (1.0 - a);
                if (trans < 0.01) {
                    // ADR-2105: opaque from here: the 1 % left used to composite whatever was behind (a bright
                    // emitter showed through any thickness of cloud)
                    trans = 0.0;
                    break;
                }
            }
        } else if (!even) {
            dt = dt * 2.0; // empty air: stride
            // ADR-2101: and jump empty columns above the sea outright
            let skip = emptySkip(ro, rd, t + dt, seaEntry, boxEntry);
            if (skip > t + dt) {
                dt = skip - t + dt * jitter;
            }
        }
        t = t + dt;
    }
    let alpha = 1.0 - trans;
    // ADR-2102: the air scatter the march never passed (beyond the last sample, or the ray ended before the cloud)
    if (trans >= 0.01) {
        for (var k = 0u; k < arms; k = k + 1u) {
            if (airT[k] < 1e29) {
                col = col + airC[k] * trans;
            }
        }
        col = col + sky * trans;
    }
    out.colour = vec4<f32>(col, alpha);
    out.depth = vec4<f32>(select(0.0, tSum / max(wSum, 1e-6), wSum > 0.0), axial, 0.0, 0.0);
    return out;
}
