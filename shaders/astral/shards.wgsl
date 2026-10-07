// THE ASTRAL FORGE -- iteration 2: near flakes as real lit shards. Each is an irregular four-cornered plate
// (a fan of four triangles around its centre), oriented by the flake's normal, with micro-grooves cut across it
// and a bevelled rim that catches the light bands. Depth-tested against itself and culled against the surface.

struct Shard { a: vec4f, b: vec4f, c: vec4f, };
@group(1) @binding(0) var<storage, read> shards: array<Shard>;
@group(1) @binding(1) var surfDepth: texture_2d<f32>;

struct VOut {
    @builtin(position) clip: vec4f,
    @location(0) wpos: vec3f,
    @location(1) @interpolate(flat) nrm: vec3f,
    @location(2) local: vec2f,     // position in the shard's plane, in shard radii
    @location(3) edge: f32,        // 0 at the centre, 1 at the rim
    @location(4) @interpolate(flat) data: vec4f, // heat, binding, hash, weight
    @location(5) @interpolate(flat) t1: vec3f,
};

fn corner(h: u32, k: u32) -> vec2f {
    // irregular slivers: uneven corner radii, then stretched along one axis (never a square)
    let a = (f32(k) + 0.5) * 1.5707963 + (u01(hashu(h + k * 97u)) - 0.5) * 1.1;
    let r = 0.3 + 0.7 * u01(hashu(h * 3u + k * 131u));
    let st = 1.3 + 1.2 * u01(h >> 9u);
    return vec2f(cos(a) * st, sin(a) / st) * r;
}

@vertex fn vs_shard(@builtin(vertex_index) vi: u32, @builtin(instance_index) ii: u32) -> VOut {
    let s = shards[ii];
    let h = hashu(u32(s.c.y * 16777216.0) + 911u);
    let tri = vi / 3u;
    let v = vi % 3u;
    var lp = vec2f(0.0);
    var edge = 0.0;
    if (v == 1u) { lp = corner(h, tri); edge = 1.0; }
    if (v == 2u) { lp = corner(h, (tri + 1u) % 4u); edge = 1.0; }
    let n = normalize(s.b.xyz);
    let up = select(vec3f(0.0, 1.0, 0.0), vec3f(1.0, 0.0, 0.0), abs(n.y) > 0.9);
    let rot = u01(h >> 5u) * 6.2831853;
    let e1 = normalize(cross(up, n));
    let e2 = cross(n, e1);
    let t1 = e1 * cos(rot) + e2 * sin(rot);
    let t2 = cross(n, t1);
    // shards are a little larger than the splat footprint: a flake that is resolved is a plate, not a point
    let size = s.a.w * 1.0;
    let wp = s.a.xyz + (t1 * lp.x + t2 * lp.y) * size;
    var o: VOut;
    o.clip = F.viewProj * vec4f(wp, 1.0);
    o.wpos = wp;
    o.nrm = n;
    o.local = lp;
    o.edge = edge;
    o.data = vec4f(s.b.w, s.c.x, s.c.y, s.c.w);
    o.t1 = t1;
    return o;
}

@fragment fn fs_shard(i: VOut) -> @location(0) vec4f {
    let px = vec2i(i.clip.xy);
    let sd = textureLoad(surfDepth, px, 0).r;
    let dist = length(i.wpos - F.cam.xyz);
    if (dist > sd + 0.01 * dist) { discard; }
    // the weight keeps or drops a WHOLE shard (by its own hash): a per-pixel dither read as sand
    if (i.data.w < u01(hashu(u32(i.data.z * 16777216.0) + 4242u))) { discard; }
    let V = normalize(F.cam.xyz - i.wpos);
    var n = i.nrm;
    if (dot(n, V) < 0.0) { n = -n; }
    let t1 = normalize(i.t1 - n * dot(i.t1, n));
    let t2 = cross(n, t1);
    // micro-grooves across the shard (engraved debris), wavy, with their own phase per shard
    let g = i.local.x * 40.0 + 0.8 * sin(i.local.y * 9.0 + i.data.z * 40.0);
    let x = fract(g) - 0.5;
    let slope = select(0.0, -sign(x) / 0.32, abs(x) < 0.32);
    var nn = normalize(n - t1 * slope * 0.06);
    // bevelled rim: the last 15% of the plate turns outward and catches the bands
    let rimW = smoothstep(0.82, 1.0, i.edge);
    let radial = normalize(t1 * i.local.x + t2 * i.local.y + n * 1e-4);
    nn = normalize(mix(nn, normalize(n * 0.35 + radial), rimW));
    let r = reflect(-V, nn);
    let cosV = clamp(dot(nn, V), 0.0, 1.0);
    let heat = i.data.x;
    let b = i.data.y;
    var film = thinFilm(cosV, F.ent0.w * b * (300.0 + 150.0 * i.data.z) + 160.0 * min(heat, 1.0));
    film = mix(vec3f(dot(film, vec3f(0.2126, 0.7152, 0.0722))), film, 0.6);
    let F0 = vec3f(0.5, 0.51, 0.54) * film;
    let Fr = F0 + (vec3f(0.95) - F0) * pow(1.0 - cosV, 5.0);
    // anisotropic: the grooves smear the reflection across them
    var spec = vec3f(0.0);
    // strips only: the soft-box sweep that describes the big surface would light every small plate a uniform grey
    for (var j = -1; j <= 1; j++) { spec += envBands(normalize(r + t1 * f32(j) * 0.06), 0.02); }
    // metal: dark unless a band is caught (no fill on debris, or it reads as grey confetti)
    var col = spec / 3.0 * Fr * (0.45 + 0.55 * b) + vec3f(0.002);
    col += heatColor(heat) * select(0.0, 1.4, i.data.z < 0.045);
    return vec4f(col, 1.0);
}
