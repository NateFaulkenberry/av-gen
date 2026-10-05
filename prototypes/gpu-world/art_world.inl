// ================================================================================================
// B. ENDLESS MEADOW -- an unbounded world generated around the camera, every frame, from nothing.
// ================================================================================================
//
// There are no instance records at all. Each frame one compute dispatch visits a square window of
// cells around the camera for each of five layers (grass, glow reeds, crystal spires, production
// mushrooms, motes), hashes each cell's integer coordinates into presence / position / size / band,
// places it on a procedural terrain, culls it and appends it to its layer's draw. The world exists
// only where the camera is looking; its memory is the window, not the distance travelled. The camera
// flies a path that is a pure function of t, so a frame at t is the same whether it was played or
// sought to, and the flight can go on forever.
//
// Audio: every reed and blade listens to its own band DELAYED by its distance from the traveller (the
// field carries a moving "now" around the camera and the past further out, A's idea made mobile); each
// kick sends a ping ring outward from where the camera WAS when the kick landed (distant spires flare
// as the front reaches them); snares flash a random subset of grass tips; motes twinkle on their own
// high bands with the hats.

const char* kWorldWgsl = R"(
@group(1) @binding(0) var<storage, read_write> outInst: array<Inst>;
@group(1) @binding(1) var<storage, read_write> args: array<atomic<u32>, 40>;
// sys[L] for layer L = 0..4: cell size, cells per side, first thread, max distance
// sys[5]: cap, ping speed, echo speed, total threads      sys[6]: minPx, kick decay, -, -
var<workgroup> wgCount: array<atomic<u32>, 8>;
var<workgroup> wgBase: array<u32, 8>;

fn terrainH(p: vec2f) -> f32 {
    let hills = fbm(p * 0.0055 + vec2f(13.1, 7.7), 4);
    let d = fbm(p * 0.045, 3);
    return (hills - 0.5) * 70.0 + d * 2.2;
}

fn bandColor(f: f32) -> vec3f {
    let c0 = vec3f(1.0, 0.20, 0.08); let c1 = vec3f(1.0, 0.45, 0.30); let c2 = vec3f(0.95, 0.55, 0.85);
    let c3 = vec3f(0.35, 0.80, 1.0); let c4 = vec3f(0.30, 1.0, 0.75);
    let x = clamp(f, 0.0, 1.0) * 4.0;
    if (x < 1.0) { return mix(c0, c1, x); }
    if (x < 2.0) { return mix(c1, c2, x - 1.0); }
    if (x < 3.0) { return mix(c2, c3, x - 2.0); }
    return mix(c3, c4, x - 3.0);
}

// Kick pings: each of the last eight kicks is a ring leaving the camera's position at that moment.
// Returns (strength, push direction xz).
fn ping(p: vec2f, width: f32) -> vec3f {
    var s = 0.0;
    var dir = vec2f(0.0);
    let t = F.camPos.w;
    for (var k = 0; k < 8; k++) {
        let since = t - kickTime(k);
        if (since >= 0.0 && since < 8.0) {
            let o = F.kickP[k].xz;
            let v = p - o;
            let d = length(v);
            let front = since * F.sys[5].y;
            let w = kickStr(k) * exp(-(d - front) * (d - front) / (width * width)) * exp(-0.45 * since) * smoothstep(3.0, 25.0, front);
            s += w;
            dir += v / max(d, 1e-3) * w;
        }
    }
    return vec3f(s, dir);
}

@compute @workgroup_size(64)
fn cs_world(@builtin(global_invocation_id) gid: vec3u, @builtin(local_invocation_index) li: u32,
            @builtin(num_workgroups) nwg: vec3u) {
    if (li < 8u) { atomicStore(&wgCount[li], 0u); }
    workgroupBarrier();
    let id = gid.x + gid.y * nwg.x * 64u;
    let total = u32(F.sys[5].w);
    let cap = u32(F.sys[5].x);
    var bucket = -1;
    var slot = 0u;
    var o: Inst;
    if (id < total) {
        var L = 0;
        for (var k = 1; k < 5; k++) { if (id >= u32(F.sys[k].z)) { L = k; } }
        let P = F.sys[L];
        let cell = P.x;
        let dim = u32(P.y);
        let local = id - u32(P.z);
        let fw = F.camFwd.xz / max(length(F.camFwd.xz), 1e-3);
        let centre = F.camPos.xz + fw * (P.w * 0.45);
        let cc = vec2i(floor(centre / cell)) - vec2i(i32(dim / 2u)) + vec2i(i32(local % dim), i32(local / dim));
        let h = hash2i(cc, u32(L) * 7919u + 1u);
        let r0 = u01(h); let r1 = u01(hashu(h + 1u)); let r2 = u01(hashu(h + 2u));
        let r3 = u01(hashu(h + 3u)); let r4 = u01(hashu(h + 4u)); let r5 = u01(hashu(h + 5u));
        var xz = (vec2f(cc) + vec2f(0.05) + 0.9 * vec2f(r1, r2)) * cell;
        let t = F.camPos.w;
        let dcam = length(xz - F.camPos.xz);
        let fade = smoothstep(P.w, P.w * 0.8, length(xz - centre));
        let echo = t - dcam / F.sys[5].z;           // what this element hears: the past, by distance
        let pxW = length(vec3f(xz.x, terrainH(xz), xz.y) - F.camPos.xyz) * F.camFwd.w;
        var present = false;
        var s = 1.0;
        var hy = 1.0;
        var widen = 1.0;
        var bend = vec2f(0.0);
        var col = vec3f(1.0);
        var emis = 0.0;
        var yoff = 0.0;
        var q = qaxis(vec3f(0.0, 1.0, 0.0), r4 * TAU);
        let mids = F.audio0.z;
        let sway = 0.08 * (0.5 + 1.5 * mids) * vec2f(sin(1.3 * t + r4 * 9.0 + xz.x * 0.21), cos(0.9 * t + r3 * 7.0 + xz.y * 0.17));
        if (L == 0) {                 // grass
            let dens = vnoise(xz * 0.05);
            present = r0 < 0.45 + 0.5 * dens;
            s = (0.25 + 0.45 * r3) * (0.6 + 0.7 * dens) * fade;
            let bin = i32(r5 * 40.0);
            let e = smoothstep(0.1, 0.9, specAtTime(echo, bin));
            let pg = ping(xz, 1.6);
            col = mix(vec3f(0.25, 0.9, 0.7), bandColor(f32(bin) / 63.0), 0.5);
            emis = 0.02 + 0.9 * e * e + 2.0 * pg.x;
            if (r3 < F.audio2.z * 0.18) { emis += 1.2; col = vec3f(0.7, 0.95, 1.0); }
            bend = sway * 2.0 + pg.yz * 0.6;
            widen = max(1.0, F.sys[6].x * pxW / max(0.05 * s, 1e-4));
            bucket = 0;
        } else if (L == 1) {          // glow reeds in patches
            present = r0 < 0.15 + 0.85 * smoothstep(0.35, 0.6, vnoise(xz * 0.018 + vec2f(5.0, 1.0)));
            s = (0.9 + 1.5 * r3) * fade;
            let bin = i32(r5 * 63.0);
            let e = smoothstep(0.08, 0.9, specAtTime(echo, bin));
            let pg = ping(xz, 2.5);
            hy = 0.45 + 0.9 * e + 0.3 * pg.x;
            col = mix(bandColor(f32(bin) / 63.0), vec3f(1.0, 0.95, 0.9), clamp(pg.x * 0.3, 0.0, 0.5));
            emis = (0.03 + 1.6 * e * e) * (0.6 + 0.8 * r1) + 1.5 * pg.x * pg.x;
            bend = sway + pg.yz * 0.5;
            widen = max(1.0, F.sys[6].x * pxW / (s * 0.012));
            bucket = select(2, 1, dcam < 50.0);
        } else if (L == 2) {          // crystal spires: dark glass in clusters, tips lit by the low end, flaring when a ping arrives
            let cl = smoothstep(0.45, 0.80, vnoise(xz * 0.009 + vec2f(9.0, 3.0)));
            present = r0 < 0.14 * cl + 0.004;
            s = (3.0 + 22.0 * r3 * r3 * r3) * smoothstep(P.w, P.w * 0.9, length(xz - centre));
            q = qmul(q, qaxis(normalize(vec3f(r1 - 0.5, 0.0, r2 - 0.5)), 0.45 * r5 * r5));
            widen = 0.55 + 1.3 * r4;
            let bin = i32(r1 * 18.0);
            let e = specAtTime(t, bin);
            let pg = ping(xz, 9.0);
            col = mix(vec3f(0.55, 0.40, 1.0), vec3f(0.25, 0.85, 1.0), r2);
            emis = 0.15 + 1.2 * e * e * e + 4.0 * pg.x;
            yoff = -0.06 * s;
            bucket = 3;
        } else if (L == 3) {          // production mushrooms in the hollows
            let hh = terrainH(xz);
            present = r0 < 0.22 * smoothstep(0.0, -12.0, hh) + 0.015;
            s = (1.8 + 3.8 * r3 * r3) * fade;
            hy = 0.9 + 0.5 * r4;
            let bin = 18 + i32(r5 * 22.0);
            let e = smoothstep(0.1, 0.9, specAtTime(echo, bin));
            let pg = ping(xz, 3.0);
            s *= 1.0 + 0.12 * pg.x;
            col = mix(vec3f(1.0, 0.35, 0.12), vec3f(0.95, 0.2, 0.55), r2);
            emis = 0.6 + 3.0 * e * e + 2.0 * pg.x;
            bend = sway * 0.3;
            bucket = 4;
        } else {                      // motes over the meadow
            present = r0 < 0.4;
            s = (0.05 + 0.07 * r3) * fade;
            xz += 1.2 * vec2f(sin(0.31 * t + r1 * 20.0), cos(0.27 * t + r2 * 20.0));
            yoff = 0.5 + 3.5 * r3 + 0.3 * sin(1.1 * t + r4 * 30.0);
            let bin = 38 + i32(r5 * 25.0);
            let e = smoothstep(0.15, 0.95, specAtTime(t, bin));
            col = mix(vec3f(0.6, 0.95, 1.0), vec3f(1.0, 0.8, 0.5), r1);
            emis = 0.3 + 6.0 * e * e * (0.5 + F.audio2.w);
            widen = max(1.0, F.sys[6].x * pxW / max(s, 1e-4));
            bucket = 5;
        }
        if (present && s > 0.01) {
            let y = terrainH(xz) + yoff;
            o.posScale = vec4f(xz.x, y, xz.y, s);
            o.quat = q;
            o.color = vec4f(col, emis / widen);
            o.shape = vec4f(hy, length(bend), bendAngle(bend), widen);
            let c = vec3f(xz.x, y + 0.5 * s * hy, xz.y);
            let rad = s * hy * 0.75 + 0.2;
            var vis = length(xz - centre) < P.w;
            for (var k = 0; k < 6; k++) { if (dot(F.planes[k].xyz, c) + F.planes[k].w < -rad) { vis = false; } }
            if (vis) { slot = atomicAdd(&wgCount[bucket], 1u); } else { bucket = -1; }
        } else {
            bucket = -1;
        }
    }
    workgroupBarrier();
    if (li < 8u) { wgBase[li] = atomicAdd(&args[li * 5u + 1u], atomicLoad(&wgCount[li])); }
    workgroupBarrier();
    if (bucket >= 0 && wgBase[bucket] + slot < cap) { outInst[u32(bucket) * cap + wgBase[bucket] + slot] = o; }
}

fn groundPos(uv: vec2f) -> vec2f {
    let a = abs(uv);
    let w = sign(uv) * (40.0 * a + 640.0 * a * a);
    return floor(F.camPos.xz / 0.2) * 0.2 + w;
}
fn groundHeight(p: vec2f) -> f32 { return terrainH(p) - 0.03; }
fn groundShade(w: vec3f, n: vec3f) -> vec3f {
    let t = F.camPos.w;
    let dcam = length(w.xz - F.camPos.xz);
    let albedo = vec3f(0.012, 0.02, 0.018) * (0.6 + 0.8 * fbm(w.xz * 0.3, 2));
    let amb = mix(F.groundTint.rgb, F.skyHorizon.rgb, n.y * 0.5 + 0.5) * F.sunColor.w;
    var c = albedo * (amb + max(dot(n, F.sun.xyz), 0.0) * F.sunColor.rgb * F.sun.w);
    // glowing root-veins in the soil, carrying the bass outward from the traveller
    let rp = mat2x2f(0.8, 0.6, -0.6, 0.8) * w.xz;
    let v = pow(1.0 - abs(fbm(rp * 0.05, 2) * 2.0 - 1.0), 40.0);
    let bass = (specAtTime(t - dcam / F.sys[5].z, 2) + specAtTime(t - dcam / F.sys[5].z, 5)) * 0.5;
    c += vec3f(1.0, 0.22, 0.08) * v * (0.002 + 0.3 * bass * bass * bass);
    // the kick pings as thin rings on the ground
    let pg = ping(w.xz, 0.6);
    c += vec3f(0.55, 0.85, 1.0) * pg.x * 0.18;
    return c;
}
)";

// CPU mirror of the terrain (for the camera only): the same integer hash and value noise.
namespace worldcpu {
std::uint32_t hashu(std::uint32_t x) { x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x; }
std::uint32_t hash2i(int cx, int cy, std::uint32_t salt) {
    return hashu((static_cast<std::uint32_t>(cx) * 0x8da6b343u) ^ hashu((static_cast<std::uint32_t>(cy) * 0xd8163841u) ^ (salt * 0xcb1ab31fu)));
}
float u01(std::uint32_t h) { return static_cast<float>(h >> 8) / 16777216.0f; }
float vnoise(glm::vec2 p) {
    const int ix = static_cast<int>(std::floor(p.x)), iy = static_cast<int>(std::floor(p.y));
    const glm::vec2 f = p - glm::floor(p);
    const glm::vec2 u = f * f * (3.0f - 2.0f * f);
    const float a = u01(hash2i(ix, iy, 11u)), b = u01(hash2i(ix + 1, iy, 11u));
    const float c = u01(hash2i(ix, iy + 1, 11u)), d = u01(hash2i(ix + 1, iy + 1, 11u));
    return glm::mix(glm::mix(a, b, u.x), glm::mix(c, d, u.x), u.y);
}
float fbm(glm::vec2 p, int oct) {
    float a = 0.5f, s = 0.0f;
    for (int k = 0; k < oct; ++k) { s += a * vnoise(p); p = glm::vec2(1.6f * p.x - 1.2f * p.y, 1.2f * p.x + 1.6f * p.y); a *= 0.5f; }
    return s;
}
float terrainH(glm::vec2 p) { return (fbm(p * 0.0055f + glm::vec2(13.1f, 7.7f), 4) - 0.5f) * 70.0f + fbm(p * 0.045f, 3) * 2.2f; }
} // namespace worldcpu

struct WorldSystem final : System {
    struct Layer { float cell, half; };
    // grass, reeds, spires, mushrooms, motes
    std::array<Layer, 5> layers{{{0.11f, 30.0f}, {0.9f, 140.0f}, {7.0f, 560.0f}, {5.0f, 160.0f}, {2.6f, 70.0f}}};
    std::array<std::uint32_t, 5> dims{}, firsts{};
    std::uint32_t totalThreads = 0, cap = 0;
    float speed = 16.0f, pingSpeed = 30.0f, echoSpeed = 14.0f;
    wgpu::ComputePipeline pipe;
    wgpu::BindGroup group;
    std::uint32_t wgX = 1, wgY = 1;

    WorldSystem() {
        for (int L = 0; L < 5; ++L) {
            dims[L] = static_cast<std::uint32_t>(std::ceil(2.0f * layers[L].half / layers[L].cell)) | 1u;
            firsts[L] = totalThreads;
            totalThreads += dims[L] * dims[L];
        }
        cap = 200000; // per bucket; grass is the largest visible layer
    }
    std::string wgsl() const override { return kWorldWgsl; }
    std::vector<MeshCpu> meshes() const override {
        const glm::vec3 stalk(0.05f, 0.06f, 0.055f);
        return {makeBlade(glm::vec3(0.03f, 0.06f, 0.05f)), makeReed(5, 4, 0.010f, 0.004f, 0.016f, 0.12f, stalk),
                makeReed(3, 1, 0.010f, 0.005f, 0.018f, 0.14f, stalk), makeCrystal(6, 0.13f, 0.78f, glm::vec3(0.035f, 0.035f, 0.05f), 1.0f),
                makeMushroom({{"aspect", 0.62f}, {"rimTangentDeg", -58.0f}, {"centreTangentDeg", 28.0f}, {"capThickness", 0.22f},
                              {"stemCurvature", 0.28f}, {"stemTaper", 0.75f}, {"stemBulgeWidth", 0.12f}, {"lobeCount", 0.0f},
                              {"lobeDepth", 0.0f}, {"capTiltDeg", 9.0f}, {"edgeWaviness", 0.04f}, {"surfaceNoiseAmp", 0.015f},
                              {"gillCount", 64.0f}, {"emissionStructure", 1.0f}, {"emissionIntensity", 3.0f}}),
                makeOrb(glm::vec3(0.2f))};
    }
    std::vector<Bucket> buckets() const override { return {{0}, {1}, {2}, {3}, {4}, {5}}; }
    std::uint32_t capacity() const override { return cap; }
    Look look() const override {
        Look l;
        l.fog = {0.022f, 0.022f, 0.045f, 0.0058f};
        l.skyZenith = {0.003f, 0.004f, 0.012f, 0.7f};
        l.skyHorizon = {0.050f, 0.036f, 0.085f, 3.0f};
        l.sun = glm::vec4(glm::normalize(glm::vec3(1.0f, 0.07f, 0.22f)), 0.30f);
        l.sunColor = {0.55f, 0.32f, 0.32f, 0.55f};
        l.groundTint = {0.010f, 0.012f, 0.014f, 0.6f};
        l.exposure = 1.15f; l.bloom = 0.085f; l.vignette = 0.5f; l.contrast = 1.06f; l.saturation = 1.05f;
        return l;
    }
    glm::vec2 pathXZ(double t) const {
        const float x = static_cast<float>(speed * t);
        return {x, 70.0f * std::sin(x / 260.0f) + 22.0f * std::sin(x / 91.0f + 1.3f)};
    }
    float pathY(double t) const {
        float avg = 0.0f;
        for (int k = -4; k <= 4; ++k) avg += worldcpu::terrainH(pathXZ(t + 0.35 * k));
        avg /= 9.0f;
        return std::max(avg + 1.2f, worldcpu::terrainH(pathXZ(t)) + 1.6f) + 2.0f;
    }
    glm::vec3 pathPos(double t) const { const glm::vec2 p = pathXZ(t); return {p.x, pathY(t), p.y}; }
    Cam camera(double t, int shot) const override {
        Cam c;
        if (shot == 0) {          // ride: eye-level flight
            c.eye = pathPos(t);
            const glm::vec3 a = pathPos(t + 2.2);
            c.target = a + glm::vec3(0.0f, -1.6f, 0.0f);
            c.fovDeg = 60.0f;
        } else if (shot == 1) {   // high and wide: the world's scale, the pings spreading
            const glm::vec3 p = pathPos(t);
            c.eye = p + glm::vec3(-30.0f, 38.0f, 26.0f);
            c.target = pathPos(t + 6.0) + glm::vec3(0.0f, -2.0f, 0.0f);
            c.fovDeg = 55.0f;
        } else if (shot == 9) {   // debug: straight down from 150 m
            const glm::vec3 p = pathPos(t);
            c.eye = p + glm::vec3(0.0f, 150.0f, 0.01f);
            c.target = p;
            c.fovDeg = 60.0f;
        } else {                  // low over the grass, slightly to the side
            const glm::vec3 p = pathPos(t);
            c.eye = glm::vec3(p.x, worldcpu::terrainH(glm::vec2(p.x, p.z + 3.0f)) + 0.9f, p.z + 3.0f);
            c.target = pathPos(t + 1.6) + glm::vec3(0.0f, -0.8f, -2.0f);
            c.fovDeg = 64.0f;
        }
        return c;
    }
    void init(gpu::Context& ctx, const wgpu::ShaderModule& m, const wgpu::BindGroupLayout& frameLayout, const wgpu::Buffer& out,
              const wgpu::Buffer& args) override {
        auto l1 = makeLayout(ctx, {Bind::Storage, Bind::Storage}, wgpu::ShaderStage::Compute);
        pipe = makeCompute(ctx, m, "cs_world", makePipelineLayout(ctx, {frameLayout, l1}));
        group = makeGroup(ctx, l1, {buf(out, static_cast<std::uint64_t>(cap) * 6 * sizeof(Inst)), buf(args, 160)});
        const std::uint32_t groups = (totalThreads + 63) / 64;
        wgX = std::min<std::uint32_t>(groups, 32768);
        wgY = (groups + wgX - 1) / wgX;
    }
    void setParams(FrameU& f, double t) override {
        for (int L = 0; L < 5; ++L) f.sys[L] = {layers[L].cell, static_cast<float>(dims[L]), static_cast<float>(firsts[L]), layers[L].half};
        f.sys[5] = {static_cast<float>(cap), pingSpeed, echoSpeed, static_cast<float>(totalThreads)};
        f.sys[6] = {0.7f, 0.45f, 0.0f, 0.0f};
        // each kick's ring leaves from where the camera was when it landed: a pure function of t
        for (int k = 0; k < 8; ++k) {
            const float kt = f.kickT[k / 4][k % 4];
            f.kickP[k] = kt > -1000.0f ? glm::vec4(pathPos(kt), 0.0f) : glm::vec4(0.0f);
        }
        (void)t;
    }
    void compute(wgpu::CommandEncoder& enc, const wgpu::BindGroup& frameGroup, gpu::FrameTimeline& tl) override {
        wgpu::ComputePassDescriptor d{};
        d.timestampWrites = tl.mark("compute", gpu::FrameTimeline::PassKind::Compute);
        auto cp = enc.BeginComputePass(&d);
        cp.SetPipeline(pipe);
        cp.SetBindGroup(0, frameGroup);
        cp.SetBindGroup(1, group);
        cp.DispatchWorkgroups(wgX, wgY, 1);
        cp.End();
    }
    std::string extraJson() const override {
        char b[256];
        std::snprintf(b, sizeof(b), ",\"x_cells_per_frame\":%u,\"x_stored_instances\":0", totalThreads);
        return b;
    }
};
