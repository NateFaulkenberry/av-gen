// ================================================================================================
// C. MYCELIUM -- a stateful GPU audiovisual organism (physarum), on the timeline.
// ================================================================================================
//
// 2M agents of three species crawl over a 1024^2 trail field (160 m square, a torus). Each step an
// agent senses the field ahead (its own species attracts, the others repel), turns, moves and
// deposits; then the field blurs and decays. Behaviour depends on:
//   - audio:        each agent deposits by ITS OWN band's energy (species 0 = lows, 1 = mids,
//                   2 = highs); kicks make the ember species surge and widen its gaze, snares make
//                   the teal species jitter, hats make the violet one flare;
//   - neighbours:   only through the field (stigmergy): every agent reads what every other wrote;
//   - a procedural field: a slow flow-noise heading bias that migrates the network;
//   - previous-frame state: the agents and the field ARE the previous step;
//   - the camera:   agents steer away from the point the camera is looking at on the ground, so a
//                   clearing opens in front of the viewer and heals behind.
//
// Determinism is designed in: deposits are u32 fixed-point atomics (integer adds commute, so the
// scatter order cannot matter -- WGSL has no float atomics, which is a helpful constraint here),
// sensing reads only the previous step's field, the blur has a fixed order, and randomness is
// hash(agent, step). The simulation runs in fixed 60 Hz steps; each step gets its own uniform block
// (dynamic offset) with the audio AND the camera at that step's time. So state(step k) is a pure
// function of k, independent of frame rate or batching -- and reaching step k means replaying from
// step 0 or from a checkpoint. --seektest measures exactly that.

const char* kOrganismCommonWgsl = R"(
fn trailIdx(p: vec2i, W: i32) -> u32 {
    let q = ((p % vec2i(W)) + vec2i(W)) % vec2i(W);
    return u32(q.y * W + q.x);
}
)";

const char* kOrganismComputeWgsl = R"(
struct StepU {
    a: vec4f,               // t, step, -, N
    b: vec4f,               // bass, mids, highs, energy
    c: vec4f,               // kickEnv, snareEnv, hatEnv, beatPhase
    d: vec4f,               // presence x, y (texels), radius (texels), strength
    e: vec4f,               // W, world size, decay, flow strength
    sp: array<vec4f, 3>,    // per species: sensor angle, sensor distance, turn, speed
    sq: array<vec4f, 3>,    // per species: deposit, repel, band lo, band hi
};
@group(1) @binding(0) var<uniform> S: StepU;
@group(1) @binding(1) var<storage, read_write> agents: array<vec4f>;
@group(1) @binding(2) var<storage, read> trailIn: array<vec4f>;
@group(1) @binding(3) var<storage, read_write> trailOut: array<vec4f>;
@group(1) @binding(4) var<storage, read_write> dep: array<atomic<u32>>;

fn sense(p: vec2f, sp: i32) -> f32 {
    let v = trailIn[trailIdx(vec2i(floor(p)), i32(S.e.x))];
    let own = v[sp];
    return own - S.sq[sp].y * (v.x + v.y + v.z - own);
}

@compute @workgroup_size(256)
fn cs_agents(@builtin(global_invocation_id) gid: vec3u, @builtin(num_workgroups) nwg: vec3u) {
    let i = gid.x + gid.y * nwg.x * 256u;
    if (i >= u32(S.a.w)) { return; }
    let W = S.e.x;
    var ag = agents[i];
    var p = ag.xy;
    var hd = ag.z;
    let sp = i32(floor(ag.w));
    let rnd = fract(ag.w);
    let P = S.sp[sp];
    let Q = S.sq[sp];
    let bin = i32(mix(Q.z, Q.w, rnd));
    let e = specAt((S.a.x - F.misc.z) * F.misc.x, bin);
    var sa = P.x; var turn = P.z; var speed = P.w;
    if (sp == 0) { speed *= 1.0 + 1.2 * S.c.x; sa *= 1.0 + 0.7 * S.c.x; }
    if (sp == 1) { turn *= 1.0 + 2.5 * S.c.y; }
    speed *= 0.55 + 0.9 * S.b.w;
    let sd = P.y;
    let fL = sense(p + sd * vec2f(cos(hd + sa), sin(hd + sa)), sp);
    let fC = sense(p + sd * vec2f(cos(hd), sin(hd)), sp);
    let fR = sense(p + sd * vec2f(cos(hd - sa), sin(hd - sa)), sp);
    let r = u01(hashu((i * 0x9E3779B1u) ^ (u32(S.a.y) * 0x85EBCA6Bu)));
    // the classic rule (Jones 2010): turn the full angle toward the stronger side
    if (fC > fL && fC > fR) {
    } else if (fC < fL && fC < fR) {
        hd += select(-turn, turn, r < 0.5);
    } else if (fL > fR) {
        hd += turn;
    } else if (fR > fL) {
        hd -= turn;
    }
    hd += (r - 0.5) * 0.08;
    // a slow procedural flow migrates the whole network
    let wp = p / W * S.e.y;
    let fa = vnoise(wp * 0.025 + vec2f(S.a.x * 0.02, 3.0)) * TAU * 2.0;
    hd += S.e.w * sin(fa - hd);
    // the camera's gaze point repels (torus-nearest image)
    var dv = p - S.d.xy;
    dv -= W * round(dv / W);
    let dl = length(dv);
    if (dl < S.d.z && dl > 1e-3) {
        let away = atan2(dv.y, dv.x);
        hd += S.d.w * (1.0 - dl / S.d.z) * sin(away - hd);
    }
    hd -= TAU * floor(hd / TAU);
    p += speed * vec2f(cos(hd), sin(hd));
    p -= W * floor(p / W);
    agents[i] = vec4f(p, hd, ag.w);
    var amt = Q.x * (0.3 + 1.4 * e * e);
    if (sp == 2) { amt *= 1.0 + 1.5 * S.c.z; }
    atomicAdd(&dep[trailIdx(vec2i(floor(p)), i32(W)) * 4u + u32(sp)], u32(amt * 256.0));
}

@compute @workgroup_size(16, 16)
fn cs_diffuse(@builtin(global_invocation_id) gid: vec3u) {
    let W = i32(S.e.x);
    if (i32(gid.x) >= W || i32(gid.y) >= W) { return; }
    let c = vec2i(gid.xy);
    var sum = vec4f(0.0);
    for (var dy = -1; dy <= 1; dy++) {
        for (var dx = -1; dx <= 1; dx++) { sum += trailIn[trailIdx(c + vec2i(dx, dy), W)]; }
    }
    let i = trailIdx(c, W);
    let d = vec4f(f32(atomicLoad(&dep[i * 4u])), f32(atomicLoad(&dep[i * 4u + 1u])), f32(atomicLoad(&dep[i * 4u + 2u])), 0.0) / 256.0;
    atomicStore(&dep[i * 4u], 0u); atomicStore(&dep[i * 4u + 1u], 0u); atomicStore(&dep[i * 4u + 2u], 0u);
    trailOut[i] = (mix(trailIn[i], sum / 9.0, 0.6)) * S.e.z + d;
}

// Spores: every k-th agent drawn as a tiny glowing mote, lifted by the kick (render only, not state).
@group(2) @binding(0) var<storage, read> agentsR: array<vec4f>;
@group(2) @binding(1) var<storage, read_write> outInst: array<Inst>;
@group(2) @binding(2) var<storage, read_write> args: array<atomic<u32>, 40>;
var<workgroup> wgCount: atomic<u32>;
var<workgroup> wgBase: u32;
@compute @workgroup_size(64)
fn cs_spores(@builtin(global_invocation_id) gid: vec3u, @builtin(local_invocation_index) li: u32, @builtin(num_workgroups) nwg: vec3u) {
    if (li == 0u) { atomicStore(&wgCount, 0u); }
    workgroupBarrier();
    let j = gid.x + gid.y * nwg.x * 64u;
    let stride = u32(F.sys[1].x);
    let i = j * stride;
    var vis = false;
    var slot = 0u;
    var o: Inst;
    if (i < u32(F.sys[1].y)) {
        let ag = agentsR[i];
        let sp = i32(floor(ag.w));
        let W = F.sys[0].x; let Sz = F.sys[0].y;
        let xz = (ag.xy / W - 0.5) * Sz;
        let h = hashu(i * 747796405u);
        let lift = 0.04 + 0.5 * u01(h) * F.audio2.y + 0.15 * u01(hashu(h)) ;
        let y = groundBase(xz) + 0.12 + lift;
        var col = vec3f(1.0, 0.45, 0.12);
        if (sp == 1) { col = vec3f(0.2, 1.0, 0.8); }
        if (sp == 2) { col = vec3f(0.8, 0.4, 1.0); }
        let band = select(select(F.audio0.w, F.audio0.y, sp == 1), F.audio0.x, sp == 0);
        o.posScale = vec4f(xz.x, y, xz.y, 0.035 + 0.03 * u01(hashu(h + 5u)));
        o.quat = vec4f(0.0, 0.0, 0.0, 1.0);
        o.color = vec4f(col, 1.5 + 6.0 * band * band);
        o.shape = vec4f(1.0, 0.0, 0.0, 1.0);
        let dist = length(vec3f(xz.x, y, xz.y) - F.camPos.xyz);
        let widen = max(1.0, 0.8 * dist * F.camFwd.w / o.posScale.w);
        o.shape.w = widen;
        o.color.a /= widen * widen;
        vis = dist < 140.0;
        for (var k = 0; k < 6; k++) { if (dot(F.planes[k].xyz, vec3f(xz.x, y, xz.y)) + F.planes[k].w < -0.2) { vis = false; } }
        if (vis) { slot = atomicAdd(&wgCount, 1u); }
    }
    workgroupBarrier();
    if (li == 0u) { wgBase = atomicAdd(&args[1], atomicLoad(&wgCount)); }
    workgroupBarrier();
    if (vis) { outInst[wgBase + slot] = o; }
}
)";

// Ground functions for the render modules (`aux` is the current trail field, bound at group 0 binding 2).
const char* kOrganismRenderWgsl = R"(
fn trailAt(w: vec2f) -> vec4f {
    let W = F.sys[0].x; let Sz = F.sys[0].y;
    let p = (w / Sz + 0.5) * W - 0.5;
    let i = vec2i(floor(p));
    let f = fract(p);
    let Wi = i32(W);
    let a = aux[trailIdx(i, Wi)]; let b = aux[trailIdx(i + vec2i(1, 0), Wi)];
    let c = aux[trailIdx(i + vec2i(0, 1), Wi)]; let d = aux[trailIdx(i + vec2i(1, 1), Wi)];
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}
fn domainMask(w: vec2f) -> f32 {
    let Sz = F.sys[0].y;
    return smoothstep(0.5 * Sz, 0.40 * Sz, max(abs(w.x), abs(w.y)));
}
fn veinOf(v: f32) -> f32 { let x = 1.0 - exp(-v * F.sys[2].x); return x * x; }
fn groundPos(uv: vec2f) -> vec2f { return uv * 0.5 * F.sys[0].y * 1.25; }
fn groundHeight(p: vec2f) -> f32 {
    let tr = trailAt(p);
    return groundBase(p) + F.sys[2].y * veinOf(tr.x + tr.y + tr.z) * domainMask(p);
}
fn groundShade(w: vec3f, n: vec3f) -> vec3f {
    let tr = trailAt(w.xz);
    let m = domainMask(w.xz);
    let soil = vec3f(0.010, 0.009, 0.012) * (0.6 + 0.8 * fbm(w.xz * 0.6, 2));
    let amb = mix(F.groundTint.rgb, F.skyHorizon.rgb, n.y * 0.5 + 0.5) * F.sunColor.w;
    var c = soil * (amb + max(dot(n, F.sun.xyz), 0.0) * F.sunColor.rgb * F.sun.w);
    let pulse = vec3f(0.35 + 1.6 * F.audio0.x * F.audio0.x + 0.8 * F.audio2.y,
                      0.35 + 1.6 * F.audio0.y * F.audio0.y,
                      0.35 + 1.6 * F.audio0.w * F.audio0.w + 0.6 * F.audio2.w);
    c += vec3f(1.0, 0.36, 0.08) * veinOf(tr.x) * pulse.x * F.sys[2].z * m;
    c += vec3f(0.10, 0.95, 0.75) * veinOf(tr.y) * pulse.y * F.sys[2].z * m;
    c += vec3f(0.70, 0.30, 1.00) * veinOf(tr.z) * pulse.z * F.sys[2].z * m;
    return c;
}
)";

struct OrganismSystem final : System {
    std::uint32_t n = 2000000;
    std::uint32_t W = 1024;
    float worldSize = 160.0f;
    int shot = 0;
    std::uint32_t sporeStride = 47; // coprime with 3, so every species is sampled
    static constexpr std::uint32_t kStepSlots = 1024; // per-step uniform blocks, 256 B apart
    gpu::Context* ctx = nullptr;
    const gpuworld::SongAnalysis* song = nullptr;
    wgpu::Buffer stepBuf, agentBuf, trail[2], depBuf, ckAgent, ckTrail;
    wgpu::ComputePipeline agentPipe, diffusePipe, sporePipe;
    wgpu::BindGroup stepGroup[2], sporeGroup;
    wgpu::BindGroup frameGroupRef;
    int cur = 0;                 // which trail buffer holds the current field
    std::int64_t step = 0;       // the state is "after `step` steps"
    std::uint32_t agX = 1, agY = 1, spX = 1, spY = 1;
    std::int64_t pendingTo = 0;  // steps compute() should encode this frame
    double replayMsTotal = 0.0;
    std::int64_t replayedSteps = 0;

    struct StepU {
        glm::vec4 a, b, c, d, e;
        glm::vec4 sp[3];
        glm::vec4 sq[3];
        glm::vec4 pad[5]; // to 256 B
    };
    static_assert(sizeof(StepU) == 256);

    std::string wgsl() const override {
        return std::string(kOrganismCommonWgsl) + "fn groundBase(p: vec2f) -> f32 { return (fbm(p * 0.03 + vec2f(4.0, 9.0), 3) - 0.5) * 3.0; }\n";
    }
    std::string computeWgsl() const override { return kOrganismComputeWgsl; }
    std::string renderWgsl() const override { return kOrganismRenderWgsl; }
    std::vector<MeshCpu> meshes() const override { return {makeOrb(glm::vec3(0.1f))}; }
    std::vector<Bucket> buckets() const override { return {{0}}; }
    std::uint32_t capacity() const override { return (n / sporeStride + 64 + 3) & ~3u; }
    Look look() const override {
        Look l;
        l.fog = {0.010f, 0.013f, 0.022f, 0.016f};
        l.skyZenith = {0.0015f, 0.002f, 0.005f, 0.5f};
        l.skyHorizon = {0.020f, 0.024f, 0.040f, 1.5f};
        l.sun = glm::vec4(glm::normalize(glm::vec3(-0.5f, 0.25f, -1.0f)), 0.12f);
        l.sunColor = {0.5f, 0.6f, 1.0f, 0.5f};
        l.groundTint = {0.006f, 0.006f, 0.008f, 0.4f};
        l.exposure = 1.2f; l.bloom = 0.10f; l.vignette = 0.5f; l.contrast = 1.05f; l.saturation = 1.1f;
        return l;
    }
    Cam camera(double t, int s) const override {
        Cam c;
        const float ft = static_cast<float>(t);
        if (s == 0) {          // a slow low glide across the network
            const float a = 0.018f * ft;
            c.eye = glm::vec3(38.0f * std::cos(a), 2.6f, 38.0f * std::sin(a) - 4.0f);
            const float a2 = a + 0.25f;
            c.target = glm::vec3(30.0f * std::cos(a2), 0.0f, 30.0f * std::sin(a2) - 4.0f);
            c.fovDeg = 58.0f;
        } else if (s == 1) {   // crane: the organism from above
            c.eye = glm::vec3(10.0f * std::sin(0.01f * ft), 85.0f, 55.0f);
            c.target = glm::vec3(0.0f, 0.0f, 4.0f);
            c.fovDeg = 44.0f;
        } else {               // macro, nearly on the ground
            const float a = 0.012f * ft + 1.0f;
            c.eye = glm::vec3(25.0f * std::cos(a), 0.7f, 25.0f * std::sin(a));
            c.target = glm::vec3(10.0f * std::cos(a + 0.6f), 0.0f, 10.0f * std::sin(a + 0.6f));
            c.fovDeg = 50.0f;
        }
        return c;
    }
    // where the camera is looking on the ground, in trail texels: the organism avoids it
    glm::vec2 presence(double t) const {
        const Cam c = camera(t, shot);
        glm::vec3 f = c.target - c.eye;
        glm::vec2 fw(f.x, f.z);
        const float l = glm::length(fw);
        glm::vec2 g = l > 1e-3f ? glm::vec2(c.eye.x, c.eye.z) + fw / l * 10.0f : glm::vec2(c.target.x, c.target.z);
        if (shot == 1) g = glm::vec2(c.target.x, c.target.z);
        return (g / worldSize + 0.5f) * static_cast<float>(W);
    }
    StepU stepParams(std::int64_t k) const {
        const double t = static_cast<double>(k) / 60.0;
        const gpuworld::AudioAtT au = gpuworld::sampleSong(*song, t);
        StepU s{};
        s.a = {static_cast<float>(t), static_cast<float>(k), 0.0f, static_cast<float>(n)};
        s.b = {au.env0.x, au.env0.z, au.env0.w, au.env1.y};
        s.c = {au.kickEnv, au.snareEnv, au.hatEnv, au.env1.z};
        const glm::vec2 pr = presence(t);
        s.d = {pr.x, pr.y, 7.0f / worldSize * W, 0.35f};
        s.e = {static_cast<float>(W), worldSize, 0.86f, 0.004f};
        // sensor angle, sensor distance (texels), turn, speed (texels/step)
        s.sp[0] = {0.45f, 9.0f, 0.70f, 1.0f};
        s.sp[1] = {0.39f, 6.0f, 0.60f, 0.8f};
        s.sp[2] = {0.60f, 12.0f, 0.80f, 1.2f};
        // deposit, repel, band lo, band hi
        s.sq[0] = {1.0f, 0.55f, 1.0f, 18.0f};
        s.sq[1] = {0.9f, 0.55f, 18.0f, 42.0f};
        s.sq[2] = {0.8f, 0.55f, 40.0f, 63.0f};
        return s;
    }

    void initialAgents(std::vector<glm::vec4>& a) const {
        a.resize(n);
        for (std::uint32_t i = 0; i < n; ++i) {
            const std::uint32_t h = worldcpu::hashu(i * 2654435761u + 12345u);
            const float x = worldcpu::u01(h) * W, y = worldcpu::u01(worldcpu::hashu(h + 1)) * W;
            const float hd = worldcpu::u01(worldcpu::hashu(h + 2)) * 6.2831853f;
            const float sp = static_cast<float>(i % 3);
            const float r = std::min(0.999f, worldcpu::u01(worldcpu::hashu(h + 3)));
            a[i] = glm::vec4(x, y, hd, sp + r);
        }
    }
    void reset(std::int64_t atStep = 0) {
        std::vector<glm::vec4> a;
        initialAgents(a);
        auto& q = ctx->queue();
        q.WriteBuffer(agentBuf, 0, a.data(), a.size() * sizeof(glm::vec4));
        std::vector<std::uint8_t> zeros(static_cast<std::size_t>(W) * W * 16, 0);
        q.WriteBuffer(trail[0], 0, zeros.data(), zeros.size());
        q.WriteBuffer(trail[1], 0, zeros.data(), zeros.size());
        q.WriteBuffer(depBuf, 0, zeros.data(), zeros.size());
        cur = 0;
        step = atStep;
    }
    // Encodes steps [step, to) into one compute pass. The caller must have written their uniforms.
    void encodeSteps(wgpu::CommandEncoder& enc, std::int64_t to, std::int64_t slotBase, const wgpu::PassTimestampWrites* tw) {
        wgpu::ComputePassDescriptor d{};
        d.timestampWrites = tw;
        auto cp = enc.BeginComputePass(&d);
        cp.SetBindGroup(0, frameGroupRef);
        for (std::int64_t k = step; k < to; ++k) {
            const std::uint32_t off = static_cast<std::uint32_t>((k - slotBase) % kStepSlots) * 256u;
            cp.SetBindGroup(1, stepGroup[cur], 1, &off);
            cp.SetPipeline(agentPipe);
            cp.DispatchWorkgroups(agX, agY, 1);
            cp.SetPipeline(diffusePipe);
            cp.DispatchWorkgroups(W / 16, W / 16, 1);
            cur ^= 1;
        }
        cp.End();
        step = to;
    }
    void writeStepUniforms(std::int64_t from, std::int64_t to) {
        std::vector<StepU> u(static_cast<std::size_t>(to - from));
        for (std::int64_t k = from; k < to; ++k) u[static_cast<std::size_t>(k - from)] = stepParams(k);
        ctx->queue().WriteBuffer(stepBuf, 0, u.data(), u.size() * sizeof(StepU));
    }
    // Replays to `to` in batches of `batch` steps per submit (a seek, or the head of a render).
    void replayTo(std::int64_t to, std::int64_t batch = kStepSlots) {
        const auto t0 = Clock::now();
        const std::int64_t from = step;
        while (step < to) {
            const std::int64_t end = std::min<std::int64_t>(to, step + std::min<std::int64_t>(batch, kStepSlots));
            writeStepUniforms(step, end);
            wgpu::CommandEncoder enc = ctx->device().CreateCommandEncoder();
            encodeSteps(enc, end, step, nullptr);
            wgpu::CommandBuffer cb = enc.Finish();
            ctx->queue().Submit(1, &cb);
            if (batch < kStepSlots) ctx->waitForQueue(); // the "one frame at a time" schedule
        }
        ctx->waitForQueue();
        replayMsTotal += msSince(t0);
        replayedSteps += to - from;
    }

    void init(gpu::Context& c, const wgpu::ShaderModule& m, const wgpu::BindGroupLayout& frameLayout, const wgpu::Buffer& out,
              const wgpu::Buffer& args) override {
        ctx = &c;
        const auto st = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::CopySrc;
        stepBuf = makeBuffer(c, kStepSlots * 256ull, wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst, "steps");
        agentBuf = makeBuffer(c, static_cast<std::uint64_t>(n) * 16, st, "agents");
        trail[0] = makeBuffer(c, static_cast<std::uint64_t>(W) * W * 16, st, "trailA");
        trail[1] = makeBuffer(c, static_cast<std::uint64_t>(W) * W * 16, st, "trailB");
        depBuf = makeBuffer(c, static_cast<std::uint64_t>(W) * W * 16, st, "deposit");

        std::array<wgpu::BindGroupLayoutEntry, 5> e{};
        for (std::uint32_t k = 0; k < 5; ++k) { e[k].binding = k; e[k].visibility = wgpu::ShaderStage::Compute; }
        e[0].buffer.type = wgpu::BufferBindingType::Uniform;
        e[0].buffer.hasDynamicOffset = true;
        e[0].buffer.minBindingSize = 256;
        e[1].buffer.type = wgpu::BufferBindingType::Storage;
        e[2].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
        e[3].buffer.type = wgpu::BufferBindingType::Storage;
        e[4].buffer.type = wgpu::BufferBindingType::Storage;
        wgpu::BindGroupLayoutDescriptor ld{};
        ld.entryCount = e.size();
        ld.entries = e.data();
        wgpu::BindGroupLayout stepLayout = c.device().CreateBindGroupLayout(&ld);
        auto pl = makePipelineLayout(c, {frameLayout, stepLayout});
        agentPipe = makeCompute(c, m, "cs_agents", pl);
        diffusePipe = makeCompute(c, m, "cs_diffuse", pl);
        const std::uint64_t tb = static_cast<std::uint64_t>(W) * W * 16;
        for (int k = 0; k < 2; ++k)
            stepGroup[k] = makeGroup(c, stepLayout, {buf(stepBuf, 256), buf(agentBuf, static_cast<std::uint64_t>(n) * 16), buf(trail[k], tb),
                                                     buf(trail[k ^ 1], tb), buf(depBuf, tb)});
        auto sporeLayout = makeLayout(c, {Bind::ReadOnly, Bind::Storage, Bind::Storage}, wgpu::ShaderStage::Compute);
        auto emptyLayout = makeLayout(c, {}, wgpu::ShaderStage::Compute);
        sporePipe = makeCompute(c, m, "cs_spores", makePipelineLayout(c, {frameLayout, emptyLayout, sporeLayout}));
        sporeGroup = makeGroup(c, sporeLayout, {buf(agentBuf, static_cast<std::uint64_t>(n) * 16), buf(out, static_cast<std::uint64_t>(capacity()) * sizeof(Inst)), buf(args, 160)});
        emptyGroup = makeGroup(c, emptyLayout, {});
        std::uint32_t groups = (n + 255) / 256;
        agX = std::min<std::uint32_t>(groups, 32768); agY = (groups + agX - 1) / agX;
        groups = (n / sporeStride + 63) / 64 + 1;
        spX = std::min<std::uint32_t>(groups, 32768); spY = (groups + spX - 1) / spX;
        reset(0);
    }
    wgpu::BindGroup emptyGroup;

    void setFrameGroup(const wgpu::BindGroup& g) { frameGroupRef = g; }
    void beginFrame(double t) override {
        const std::int64_t target = static_cast<std::int64_t>(std::floor(t * 60.0));
        if (target < step) reset(0);                 // a backward seek: replay from genesis
        if (target - step > 4) replayTo(target - 2); // a forward jump: catch up off the frame
        pendingTo = std::max(target, step);
    }
    void setParams(FrameU& f, double) override {
        f.sys[0] = {static_cast<float>(W), worldSize, 0.0f, 0.0f};
        f.sys[1] = {static_cast<float>(sporeStride), static_cast<float>(n), 0.0f, 0.0f};
        f.sys[2] = {0.035f, 0.10f, 1.6f, 0.0f}; // vein response, vein height (m), vein glow
        f.misc.w = static_cast<float>(step);
    }
    void compute(wgpu::CommandEncoder& enc, const wgpu::BindGroup& frameGroup, gpu::FrameTimeline& tl) override {
        frameGroupRef = frameGroup;
        if (pendingTo > step) {
            writeStepUniforms(step, pendingTo);
            encodeSteps(enc, pendingTo, step, tl.mark("compute", gpu::FrameTimeline::PassKind::Compute));
        }
        wgpu::ComputePassDescriptor d{};
        d.timestampWrites = tl.mark("spores", gpu::FrameTimeline::PassKind::Compute);
        auto cp = enc.BeginComputePass(&d);
        cp.SetPipeline(sporePipe);
        cp.SetBindGroup(0, frameGroup);
        cp.SetBindGroup(1, emptyGroup);
        cp.SetBindGroup(2, sporeGroup);
        cp.DispatchWorkgroups(spX, spY, 1);
        cp.End();
    }
    std::vector<wgpu::Buffer> auxBuffers() const override { return {trail[0], trail[1]}; }
    int currentAux() const override { return cur; }
    std::uint64_t auxBytes() const override { return static_cast<std::uint64_t>(W) * W * 16; }
    std::uint64_t gpuStateBytes() const override { return static_cast<std::uint64_t>(n) * 16 + 3ull * W * W * 16; }
    std::string extraJson() const override {
        char b[256];
        std::snprintf(b, sizeof(b), ",\"x_agents\":%u,\"x_field\":%u,\"x_replayed_steps\":%lld,\"x_replay_ms\":%.1f", n, W,
                      static_cast<long long>(replayedSteps), replayMsTotal);
        return b;
    }

    // ---- the seek / determinism test ----------------------------------------------------------
    std::vector<std::uint8_t> readBuffer(const wgpu::Buffer& b, std::uint64_t bytes) {
        wgpu::Buffer rb = makeBuffer(*ctx, bytes, wgpu::BufferUsage::MapRead | wgpu::BufferUsage::CopyDst, "rb");
        wgpu::CommandEncoder enc = ctx->device().CreateCommandEncoder();
        enc.CopyBufferToBuffer(b, 0, rb, 0, bytes);
        wgpu::CommandBuffer cb = enc.Finish();
        ctx->queue().Submit(1, &cb);
        auto fut = rb.MapAsync(wgpu::MapMode::Read, 0, bytes, wgpu::CallbackMode::WaitAnyOnly, [](wgpu::MapAsyncStatus, wgpu::StringView) {});
        (void)ctx->waitFor(fut);
        const auto* p = static_cast<const std::uint8_t*>(rb.GetConstMappedRange(0, bytes));
        return std::vector<std::uint8_t>(p, p + bytes);
    }
    static std::uint64_t fnv(const std::vector<std::uint8_t>& v, std::uint64_t h = 1469598103934665603ull) {
        for (auto c : v) { h ^= c; h *= 1099511628211ull; }
        return h;
    }
    std::uint64_t stateHash() {
        const auto a = readBuffer(agentBuf, static_cast<std::uint64_t>(n) * 16);
        const auto t = readBuffer(trail[cur], static_cast<std::uint64_t>(W) * W * 16);
        return fnv(t, fnv(a));
    }
    std::vector<float> trailFloats() {
        const auto t = readBuffer(trail[cur], static_cast<std::uint64_t>(W) * W * 16);
        std::vector<float> f(t.size() / 4);
        std::memcpy(f.data(), t.data(), t.size());
        return f;
    }
    void writeTrailPng(const std::vector<float>& f, const std::string& path) const {
        std::vector<std::uint8_t> rgba(static_cast<std::size_t>(W) * W * 4);
        const glm::vec3 c0(1.0f, 0.36f, 0.08f), c1(0.10f, 0.95f, 0.75f), c2(0.70f, 0.30f, 1.0f);
        for (std::size_t i = 0; i < static_cast<std::size_t>(W) * W; ++i) {
            auto v = [&](int s) { const float x = 1.0f - std::exp(-f[i * 4 + s] * 0.035f); return x * x; };
            glm::vec3 c = c0 * v(0) + c1 * v(1) + c2 * v(2);
            c = c / (glm::vec3(1.0f) + c);
            for (int k = 0; k < 3; ++k) rgba[i * 4 + k] = static_cast<std::uint8_t>(std::clamp(std::pow(c[k], 1.0f / 2.2f) * 255.0f, 0.0f, 255.0f));
            rgba[i * 4 + 3] = 255;
        }
        (void)assets::writePng(path, W, W, rgba);
    }
    static double corr(const std::vector<float>& a, const std::vector<float>& b) {
        double ma = 0, mb = 0;
        for (std::size_t i = 0; i < a.size(); ++i) { ma += a[i]; mb += b[i]; }
        ma /= a.size(); mb /= b.size();
        double sab = 0, saa = 0, sbb = 0;
        for (std::size_t i = 0; i < a.size(); ++i) { const double x = a[i] - ma, y = b[i] - mb; sab += x * y; saa += x * x; sbb += y * y; }
        return sab / std::sqrt(saa * sbb + 1e-30);
    }
    int seekTest(gpu::Context&, const wgpu::Buffer& frameBuf, const wgpu::BindGroup& frameGroup, const gpuworld::SongAnalysis& s,
                 const std::string& spec, gpu::FrameTimeline&) {
        frameGroupRef = frameGroup;
        double T = 60.0;
        std::string outDir = ".";
        std::sscanf(spec.c_str(), "%lf", &T);
        if (const auto c = spec.find(':'); c != std::string::npos) outDir = spec.substr(c + 1);
        FrameU fu{};
        fu.misc = glm::vec4(s.hopRate, static_cast<float>(s.hops), s.t0, 0.0f);
        ctx->queue().WriteBuffer(frameBuf, 0, &fu, sizeof(fu));
        const std::int64_t K = static_cast<std::int64_t>(std::llround(T * 60.0));
        auto timed = [&](auto&& fn) { const auto t0 = Clock::now(); fn(); return msSince(t0); };

        // 1. the reference: replay 0 -> K in 1024-step submits
        reset(0);
        const double msBatched = timed([&] { replayTo(K); });
        const std::uint64_t hRef = stateHash();
        const auto fRef = trailFloats();
        writeTrailPng(fRef, outDir + "/c-seek-reference.png");
        // 2. the same, again (run to run, same process)
        reset(0);
        replayTo(K);
        const std::uint64_t hRepeat = stateHash();
        // 3. one step per submit, waiting each time (what a live loop at 60 fps does), up to min(K, 3600)
        const std::int64_t K1 = std::min<std::int64_t>(K, 1200);
        reset(0);
        const double msOnePer = timed([&] { replayTo(K1, 1); });
        const std::uint64_t hOnePer = stateHash();
        reset(0);
        replayTo(K1);
        const std::uint64_t hOnePerRef = stateHash();
        // 4. checkpoint 5 s back, continue, restore, replay
        const std::int64_t Kc = std::max<std::int64_t>(0, K - 300);
        reset(0);
        replayTo(Kc);
        const std::uint64_t agentBytes = static_cast<std::uint64_t>(n) * 16, trailBytes = static_cast<std::uint64_t>(W) * W * 16;
        ckAgent = makeBuffer(*ctx, agentBytes, wgpu::BufferUsage::CopySrc | wgpu::BufferUsage::CopyDst, "ckA");
        ckTrail = makeBuffer(*ctx, trailBytes, wgpu::BufferUsage::CopySrc | wgpu::BufferUsage::CopyDst, "ckT");
        const int ckCur = cur;
        const double msCkSave = timed([&] {
            wgpu::CommandEncoder enc = ctx->device().CreateCommandEncoder();
            enc.CopyBufferToBuffer(agentBuf, 0, ckAgent, 0, agentBytes);
            enc.CopyBufferToBuffer(trail[cur], 0, ckTrail, 0, trailBytes);
            wgpu::CommandBuffer cb = enc.Finish();
            ctx->queue().Submit(1, &cb);
            ctx->waitForQueue();
        });
        const double msCkReadback = timed([&] { (void)readBuffer(agentBuf, agentBytes); (void)readBuffer(trail[cur], trailBytes); });
        replayTo(K);
        const std::uint64_t hContinue = stateHash();
        const double msRestoreReplay = timed([&] {
            wgpu::CommandEncoder enc = ctx->device().CreateCommandEncoder();
            enc.CopyBufferToBuffer(ckAgent, 0, agentBuf, 0, agentBytes);
            enc.CopyBufferToBuffer(ckTrail, 0, trail[ckCur], 0, trailBytes);
            wgpu::CommandBuffer cb = enc.Finish();
            ctx->queue().Submit(1, &cb);
            cur = ckCur;
            step = Kc;
            replayTo(K);
        });
        const std::uint64_t hRestored = stateHash();
        // 5. production's particle answer (ADR-360): start fresh 240 steps back, warm up, compare
        reset(K - 240);
        replayTo(K);
        const std::uint64_t hWarm = stateHash();
        const auto fWarm = trailFloats();
        writeTrailPng(fWarm, outDir + "/c-seek-warmup240.png");
        // 6. a fresh start 30 s back (a longer warm-up): does it converge toward the played state?
        reset(std::max<std::int64_t>(0, K - 1800));
        replayTo(K);
        const auto fWarm30 = trailFloats();
        writeTrailPng(fWarm30, outDir + "/c-seek-warmup1800.png");

        std::printf("{\"seektest\":1,\"t\":%.2f,\"steps\":%lld,\"agents\":%u,\"field\":%u,"
                    "\"hash_reference\":\"%016llx\",\"hash_repeat\":\"%016llx\",\"repeat_equal\":%d,"
                    "\"one_step_per_submit_steps\":%lld,\"hash_one_per_submit\":\"%016llx\",\"hash_batched_same_k\":\"%016llx\",\"batching_equal\":%d,"
                    "\"checkpoint_step\":%lld,\"hash_continue\":\"%016llx\",\"hash_restored\":\"%016llx\",\"checkpoint_equal\":%d,\"continue_equals_reference\":%d,"
                    "\"hash_warmup240\":\"%016llx\",\"warmup_equal\":%d,\"corr_warmup240\":%.4f,\"corr_warmup1800\":%.4f,"
                    "\"replay_ms_batched\":%.1f,\"replay_ms_per_sim_second\":%.2f,\"ms_per_step_batched\":%.4f,\"ms_per_step_one_per_submit\":%.4f,"
                    "\"checkpoint_mb\":%.1f,\"checkpoint_save_gpu_copy_ms\":%.2f,\"checkpoint_readback_to_cpu_ms\":%.1f,\"restore_and_replay_5s_ms\":%.1f}\n",
                    T, static_cast<long long>(K), n, W, (unsigned long long)hRef, (unsigned long long)hRepeat, hRef == hRepeat ? 1 : 0,
                    static_cast<long long>(K1), (unsigned long long)hOnePer, (unsigned long long)hOnePerRef, hOnePer == hOnePerRef ? 1 : 0,
                    static_cast<long long>(Kc), (unsigned long long)hContinue, (unsigned long long)hRestored, hContinue == hRestored ? 1 : 0,
                    hContinue == hRef ? 1 : 0, (unsigned long long)hWarm, hWarm == hRef ? 1 : 0, corr(fRef, fWarm), corr(fRef, fWarm30),
                    msBatched, msBatched / T, msBatched / static_cast<double>(K), msOnePer / static_cast<double>(K1),
                    (agentBytes + trailBytes) / 1048576.0, msCkSave, msCkReadback, msRestoreReplay);
        return ctx->errorCount() != 0 ? 5 : 0;
    }
};
