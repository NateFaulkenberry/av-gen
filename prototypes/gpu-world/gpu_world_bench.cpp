// GPU World research spike -- DISPOSABLE PROTOTYPE (docs/research/gpu-world-architecture-spike.md).
//
// One population of a simple asset, animated every frame, drawn three ways:
//
//   cpu    Control A, one thread: per instance, pre-cull on the base position, animate, exact
//          frustum/distance cull, pack a 48-byte record; then WriteBuffer the survivors and draw them.
//          This is Composition::updateFloaters' shape, plus a CPU cull so it is not a straw man.
//   cpumt  Control A on a persistent worker pool (--threads, default 8). The strongest CPU control.
//   cpugrid Control A with a uniform XZ grid built once: per frame only the cells that can reach the
//          frustum (and maxDistance) are visited, so CPU work scales with what is near the camera rather
//          than with N -- the same idea as ADR-056's VegetationSim. The fairest CPU control for a world.
//   gpu    Experimental B: the CPU writes one uniform block. A compute pass animates every instance,
//          culls it and appends survivors (workgroup-aggregated atomics) into a compact buffer whose
//          count lands in drawIndexedIndirect args.
//
// The draw shader, the mesh, the animation maths, the cull rule, the camera and the target are the
// same for all three arms. Only where the per-frame instance data comes from differs.
//
// --audio turns on the Phase 2 per-instance audio response (bass -> scale, kick -> travelling
// impulse, mids -> sway, highs -> emission from the instance's own spectral band, beat phase ->
// animation phase). Signals are synthesised from the clock, deterministically.
//
// Not production code. Hard-coded on purpose. Not linked into anything.

#include "gpu/context.hpp"
#include "gpu/frame_timeline.hpp"
#include "gpu/readback.hpp"
#include "assets/image.hpp"
#include "organism/mushroom.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <mach/mach.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <functional>
#include <mutex>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

using namespace avgen;
using Clock = std::chrono::steady_clock;

namespace {

double msSince(Clock::time_point a) {
    return std::chrono::duration<double, std::milli>(Clock::now() - a).count();
}

std::uint64_t footprintBytes() {
    task_vm_info_data_t info{};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS) {
        return 0;
    }
    return info.phys_footprint;
}

// ---- data layouts (identical on both sides) ----------------------------------------------------

struct Vtx {
    float pos[3];
    float nrm[3];
    float col[4]; // rgb albedo, a = emissive mask
};

struct Base { // 32 bytes, uploaded once
    glm::vec4 posScale; // xyz rest position, w rest scale
    glm::vec4 rand;     // x phase (radians), y,z,w uniform [0,1)
};

struct Inst { // 48 bytes, written every frame
    glm::vec4 posScale;
    glm::vec4 quat;
    glm::vec4 color; // rgb tint, a emission
};
static_assert(sizeof(Inst) == 48);

struct Params { // compute uniform, also the CPU arm's input
    glm::vec4 planes[6];
    glm::vec4 camPos;   // xyz, w maxDistance (0 = none)
    glm::vec4 tc;       // time, count, mesh radius, audio on
    glm::vec4 audio;    // bass, mids, highs, beat phase
    glm::vec4 kicks;    // the last four kick times (negative = none)
    glm::vec4 wave;     // centre x, centre z, wave speed, kick decay
    glm::vec4 spectrum[8]; // 32 bins
};

struct DrawUniforms {
    glm::mat4 viewProj;
    glm::vec4 light;
};

// ---- the animation, CPU side. Must match kAnimateWgsl operation for operation ------------------

glm::vec4 qmul(glm::vec4 a, glm::vec4 b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

Inst animate(const Base& b, const Params& p) {
    const float t = p.tc.x;
    float phase = b.rand.x;
    float swayAmp = 0.12f;
    float s = b.posScale.w;
    float scaleMul = 1.0f + 0.08f * std::sin(3.0f * t + phase);
    float emission = 0.5f + 0.5f * std::sin(1.7f * t + 2.0f * phase);
    float lift = 0.0f;
    if (p.tc.w > 0.5f) {
        const float bass = p.audio.x, mids = p.audio.y, highs = p.audio.z, beat = p.audio.w;
        // beat phase -> animation phase: half the population locks to the beat, half to half-time
        phase += 6.2831853f * beat * (b.rand.y < 0.5f ? 1.0f : 0.5f);
        swayAmp *= 1.0f + 2.0f * mids;
        // kick -> an impulse that travels outward from the centre
        const float dx = b.posScale.x - p.wave.x, dz = b.posScale.z - p.wave.y;
        const float arrive = std::sqrt(dx * dx + dz * dz) / p.wave.z;
        float impulse = 0.0f;
        for (int k = 0; k < 4; ++k) {
            const float kt = p.kicks[k];
            const float since = t - kt - arrive;
            if (kt >= 0.0f && since >= 0.0f) {
                impulse += std::exp(-p.wave.w * since);
            }
        }
        // bass -> scale; highs -> emission from the instance's own band
        const int band = std::min(31, static_cast<int>(b.rand.w * 32.0f));
        const float bandEnergy = p.spectrum[band >> 2][band & 3];
        scaleMul *= 1.0f + 0.35f * bass * b.rand.z + 0.4f * impulse;
        lift = 0.6f * impulse * s;
        emission = emission * (0.3f + 0.7f * highs) + 2.0f * bandEnergy;
    }
    const float tx = swayAmp * std::sin(1.3f * t + phase);
    const float tz = swayAmp * std::cos(0.9f * t + 1.7f * phase);
    const float yaw = b.rand.y * 6.2831853f + 0.25f * t * (b.rand.z - 0.5f);
    const glm::vec4 qy(0.0f, std::sin(0.5f * yaw), 0.0f, std::cos(0.5f * yaw));
    const glm::vec4 qx(std::sin(0.5f * tx), 0.0f, 0.0f, std::cos(0.5f * tx));
    const glm::vec4 qz(0.0f, 0.0f, std::sin(0.5f * tz), std::cos(0.5f * tz));
    Inst o;
    const float bob = 0.05f * s * std::sin(2.1f * t + phase);
    o.posScale = glm::vec4(b.posScale.x, b.posScale.y + bob + lift, b.posScale.z, s * scaleMul);
    o.quat = qmul(qy, qmul(qx, qz));
    const glm::vec3 a(0.95f, 0.55f, 0.25f), c(0.35f, 0.75f, 1.0f);
    const glm::vec3 tint = a + (c - a) * b.rand.w;
    o.color = glm::vec4(tint, emission);
    return o;
}

bool visible(const Inst& o, const Params& p) {
    const glm::vec3 c(o.posScale);
    const float r = o.posScale.w * p.tc.z;
    for (int k = 0; k < 6; ++k) {
        if (glm::dot(glm::vec3(p.planes[k]), c) + p.planes[k].w < -r) {
            return false;
        }
    }
    if (p.camPos.w > 0.0f && glm::length(c - glm::vec3(p.camPos)) - r > p.camPos.w) {
        return false;
    }
    return true;
}

// Conservative test on the rest position: everything animate() can do moves the centre by at most
// bob + lift (vertical) and scales by at most (1.08)(1 + 0.35 + 0.4*4); the margin covers both.
bool mayBeVisible(const Base& b, const Params& p) {
    const glm::vec3 c(b.posScale);
    const float growth = p.tc.w > 0.5f ? 1.08f * (1.0f + 0.35f + 1.6f) : 1.08f;
    const float lift = p.tc.w > 0.5f ? 2.4f * b.posScale.w : 0.0f;
    const float r = b.posScale.w * p.tc.z * growth + 0.05f * b.posScale.w + lift;
    for (int k = 0; k < 6; ++k) {
        if (glm::dot(glm::vec3(p.planes[k]), c) + p.planes[k].w < -r) {
            return false;
        }
    }
    if (p.camPos.w > 0.0f && glm::length(c - glm::vec3(p.camPos)) - r > p.camPos.w) {
        return false;
    }
    return true;
}

// ---- WGSL -------------------------------------------------------------------------------------

const char* kCommonWgsl = R"(
struct Base { posScale: vec4f, rand: vec4f, };
struct Inst { posScale: vec4f, quat: vec4f, color: vec4f, };
struct Params {
    planes: array<vec4f, 6>, camPos: vec4f, tc: vec4f, audio: vec4f, kicks: vec4f, wave: vec4f,
    spectrum: array<vec4f, 8>,
};
fn qmul(a: vec4f, b: vec4f) -> vec4f {
    return vec4f(a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                 a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z);
}
fn qrot(q: vec4f, v: vec3f) -> vec3f {
    return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v);
}
)";

const char* kComputeWgsl = R"(
@group(0) @binding(0) var<uniform> P: Params;
@group(0) @binding(1) var<storage, read> bases: array<Base>;
@group(0) @binding(2) var<storage, read_write> outInst: array<Inst>;
@group(0) @binding(3) var<storage, read_write> args: array<atomic<u32>, 5>;

fn animate(b: Base) -> Inst {
    let t = P.tc.x;
    var phase = b.rand.x;
    var swayAmp = 0.12;
    let s = b.posScale.w;
    var scaleMul = 1.0 + 0.08 * sin(3.0 * t + phase);
    var emission = 0.5 + 0.5 * sin(1.7 * t + 2.0 * phase);
    var lift = 0.0;
    if (P.tc.w > 0.5) {
        let bass = P.audio.x; let mids = P.audio.y; let highs = P.audio.z; let beat = P.audio.w;
        phase += 6.2831853 * beat * select(0.5, 1.0, b.rand.y < 0.5);
        swayAmp *= 1.0 + 2.0 * mids;
        let dx = b.posScale.x - P.wave.x; let dz = b.posScale.z - P.wave.y;
        let arrive = sqrt(dx * dx + dz * dz) / P.wave.z;
        var impulse = 0.0;
        for (var k = 0; k < 4; k++) {
            let kt = P.kicks[k];
            let since = t - kt - arrive;
            if (kt >= 0.0 && since >= 0.0) { impulse += exp(-P.wave.w * since); }
        }
        let band = min(31, i32(b.rand.w * 32.0));
        let bandEnergy = P.spectrum[band >> 2u][band & 3];
        scaleMul *= 1.0 + 0.35 * bass * b.rand.z + 0.4 * impulse;
        lift = 0.6 * impulse * s;
        emission = emission * (0.3 + 0.7 * highs) + 2.0 * bandEnergy;
    }
    let tx = swayAmp * sin(1.3 * t + phase);
    let tz = swayAmp * cos(0.9 * t + 1.7 * phase);
    let yaw = b.rand.y * 6.2831853 + 0.25 * t * (b.rand.z - 0.5);
    let qy = vec4f(0.0, sin(0.5 * yaw), 0.0, cos(0.5 * yaw));
    let qx = vec4f(sin(0.5 * tx), 0.0, 0.0, cos(0.5 * tx));
    let qz = vec4f(0.0, 0.0, sin(0.5 * tz), cos(0.5 * tz));
    var o: Inst;
    let bob = 0.05 * s * sin(2.1 * t + phase);
    o.posScale = vec4f(b.posScale.x, b.posScale.y + bob + lift, b.posScale.z, s * scaleMul);
    o.quat = qmul(qy, qmul(qx, qz));
    let a = vec3f(0.95, 0.55, 0.25); let c = vec3f(0.35, 0.75, 1.0);
    o.color = vec4f(a + (c - a) * b.rand.w, emission);
    return o;
}

fn isVisible(o: Inst) -> bool {
    let c = o.posScale.xyz;
    let r = o.posScale.w * P.tc.z;
    for (var k = 0; k < 6; k++) {
        if (dot(P.planes[k].xyz, c) + P.planes[k].w < -r) { return false; }
    }
    if (P.camPos.w > 0.0 && length(c - P.camPos.xyz) - r > P.camPos.w) { return false; }
    return true;
}

var<workgroup> wgCount: atomic<u32>;
var<workgroup> wgBase: u32;

@compute @workgroup_size(64)
fn cs_population(@builtin(global_invocation_id) gid: vec3u, @builtin(local_invocation_index) li: u32,
                 @builtin(num_workgroups) nwg: vec3u) {
    if (li == 0u) { atomicStore(&wgCount, 0u); }
    workgroupBarrier();
    let i = gid.x + gid.y * nwg.x * 64u;
    var o: Inst;
    var vis = false;
    var slot = 0u;
    if (i < u32(P.tc.y)) {
        o = animate(bases[i]);
        vis = isVisible(o);
        if (vis) { slot = atomicAdd(&wgCount, 1u); }
    }
    workgroupBarrier();
    if (li == 0u) { wgBase = atomicAdd(&args[1], atomicLoad(&wgCount)); }
    workgroupBarrier();
    if (vis) { outInst[wgBase + slot] = o; }
}
)";

const char* kDrawWgsl = R"(
struct Draw { viewProj: mat4x4f, light: vec4f, };
@group(0) @binding(0) var<uniform> D: Draw;
@group(0) @binding(1) var<storage, read> insts: array<Inst>;
struct VIn { @location(0) pos: vec3f, @location(1) nrm: vec3f, @location(2) col: vec4f, };
struct VOut { @builtin(position) clip: vec4f, @location(0) n: vec3f, @location(1) col: vec4f,
              @location(2) tint: vec4f, };
@vertex fn vs(v: VIn, @builtin(instance_index) ii: u32) -> VOut {
    let inst = insts[ii];
    let p = qrot(inst.quat, v.pos * inst.posScale.w) + inst.posScale.xyz;
    var o: VOut;
    o.clip = D.viewProj * vec4f(p, 1.0);
    o.n = qrot(inst.quat, v.nrm);
    o.col = v.col;
    o.tint = inst.color;
    return o;
}
@fragment fn fs(i: VOut) -> @location(0) vec4f {
    let n = normalize(i.n);
    let albedo = i.col.rgb * mix(vec3f(1.0), i.tint.rgb, 0.5);
    let lit = albedo * (0.12 + 0.88 * max(dot(n, D.light.xyz), 0.0)) + i.col.a * i.tint.rgb * i.tint.a * 1.5;
    let m = lit / (1.0 + lit);
    return vec4f(pow(m, vec3f(1.0 / 2.2)), 1.0);
}
)";

// ---- a tiny persistent worker pool --------------------------------------------------------------

class Pool {
public:
    explicit Pool(int n) {
        for (int i = 0; i < n; ++i) {
            workers_.emplace_back([this, i] { loop(i); });
        }
    }
    ~Pool() {
        {
            std::lock_guard lock(m_);
            quit_ = true;
            ++gen_;
        }
        cv_.notify_all();
        for (auto& w : workers_) {
            w.join();
        }
    }
    int size() const { return static_cast<int>(workers_.size()); }
    void run(const std::function<void(int)>& job) {
        {
            std::lock_guard lock(m_);
            job_ = &job;
            pending_ = size();
            ++gen_;
        }
        cv_.notify_all();
        std::unique_lock lock(m_);
        done_.wait(lock, [this] { return pending_ == 0; });
    }

private:
    void loop(int index) {
        std::uint64_t seen = 0;
        for (;;) {
            const std::function<void(int)>* job = nullptr;
            {
                std::unique_lock lock(m_);
                cv_.wait(lock, [&] { return gen_ != seen; });
                seen = gen_;
                if (quit_) {
                    return;
                }
                job = job_;
            }
            (*job)(index);
            {
                std::lock_guard lock(m_);
                if (--pending_ == 0) {
                    done_.notify_one();
                }
            }
        }
    }
    std::vector<std::thread> workers_;
    std::mutex m_;
    std::condition_variable cv_, done_;
    const std::function<void(int)>* job_ = nullptr;
    std::uint64_t gen_ = 0;
    int pending_ = 0;
    bool quit_ = false;
};

// ---- helpers ----------------------------------------------------------------------------------

std::uint32_t hashU(std::uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
    return x;
}
float hashF(std::uint32_t i, std::uint32_t salt) {
    return static_cast<float>(hashU(i * 4u + salt * 0x9E3779B9u) >> 8) / 16777216.0f;
}

struct Stat {
    std::vector<double> v;
    void add(double x) { v.push_back(x); }
    double pct(double q) const {
        if (v.empty()) return -1.0;
        std::vector<double> s = v;
        std::sort(s.begin(), s.end());
        return s[static_cast<std::size_t>(std::clamp(q * (s.size() - 1) + 0.5, 0.0, double(s.size() - 1)))];
    }
    double mean() const { return v.empty() ? -1.0 : std::accumulate(v.begin(), v.end(), 0.0) / v.size(); }
};

struct MeshCpu {
    std::vector<Vtx> vertices;
    std::vector<std::uint32_t> indices;
    float radius = 1.0f;
};

MeshCpu makeMushroom() {
    const auto& specs = organism::mushroomSchema().parameters.specs();
    search::Parameters values;
    for (const auto& s : specs) {
        float v = 0.5f * (s.min + s.max);
        values.push_back(s.integral ? std::round(v) : v);
    }
    auto subject = organism::buildMushroom(values);
    MeshCpu m;
    m.radius = 0.0f;
    if (!subject) {
        std::fprintf(stderr, "mushroom: %s\n", subject.error().message.c_str());
        std::exit(2);
    }
    glm::vec3 lo(1e9f), hi(-1e9f);
    for (const auto& part : subject->parts) {
        for (const auto& v : part.mesh.vertices) {
            lo = glm::min(lo, v.position);
            hi = glm::max(hi, v.position);
        }
    }
    const float height = std::max(hi.y - lo.y, 1e-3f);
    const glm::vec3 centre((lo.x + hi.x) * 0.5f, lo.y, (lo.z + hi.z) * 0.5f);
    for (const auto& part : subject->parts) {
        const auto first = static_cast<std::uint32_t>(m.vertices.size());
        const float glow = std::min(part.emissiveIntensity, 1.0f);
        const glm::vec3 col = glm::mix(part.baseColor, part.emissiveColor, glow * 0.5f);
        for (const auto& v : part.mesh.vertices) {
            const glm::vec3 p = (v.position - centre) / height;
            m.vertices.push_back({{p.x, p.y, p.z}, {v.normal.x, v.normal.y, v.normal.z}, {col.x, col.y, col.z, glow}});
            m.radius = std::max(m.radius, glm::length(p));
        }
        for (auto i : part.mesh.indices) {
            m.indices.push_back(first + i);
        }
    }
    return m;
}

MeshCpu makeFirefly() { // an octahedron: 6 vertices duplicated per face for flat normals
    MeshCpu m;
    const glm::vec3 p[6] = {{0.5f, 0.5f, 0}, {-0.5f, 0.5f, 0}, {0, 1, 0}, {0, 0, 0}, {0, 0.5f, 0.5f}, {0, 0.5f, -0.5f}};
    const int f[8][3] = {{0, 2, 4}, {4, 2, 1}, {1, 2, 5}, {5, 2, 0}, {4, 3, 0}, {1, 3, 4}, {5, 3, 1}, {0, 3, 5}};
    for (auto& tri : f) {
        const glm::vec3 n = glm::normalize(glm::cross(p[tri[1]] - p[tri[0]], p[tri[2]] - p[tri[0]]));
        for (int k = 0; k < 3; ++k) {
            const glm::vec3 q = p[tri[k]];
            m.indices.push_back(static_cast<std::uint32_t>(m.vertices.size()));
            m.vertices.push_back({{q.x, q.y, q.z}, {n.x, n.y, n.z}, {1.0f, 0.9f, 0.6f, 1.0f}});
        }
    }
    m.radius = 1.0f;
    return m;
}

wgpu::ShaderModule compile(gpu::Context& ctx, const std::string& src, const char* label) {
    wgpu::ShaderSourceWGSL wgsl{};
    wgsl.code = src.c_str();
    wgpu::ShaderModuleDescriptor desc{};
    desc.nextInChain = &wgsl;
    desc.label = label;
    return ctx.device().CreateShaderModule(&desc);
}

wgpu::Buffer makeBuffer(gpu::Context& ctx, std::uint64_t size, wgpu::BufferUsage usage, const char* label) {
    wgpu::BufferDescriptor d{};
    d.size = std::max<std::uint64_t>((size + 15) & ~15ull, 16);
    d.usage = usage;
    d.label = label;
    return ctx.device().CreateBuffer(&d);
}

void framePlanes(const glm::mat4& vp, glm::vec4 out[6]) {
    const glm::mat4 m = glm::transpose(vp);
    // WebGPU clip z is [0, w]: near = row2, far = row3 - row2.
    out[0] = m[3] + m[0];
    out[1] = m[3] - m[0];
    out[2] = m[3] + m[1];
    out[3] = m[3] - m[1];
    out[4] = m[2];
    out[5] = m[3] - m[2];
    for (int k = 0; k < 6; ++k) {
        out[k] /= glm::length(glm::vec3(out[k]));
    }
}

// Deterministic synthetic signals: 120 bpm, a kick on every beat, bass/mids/highs envelopes and a
// 32-bin spectrum that sweeps.
void synthAudio(float t, Params& p) {
    const float beatLen = 0.5f;
    const float beat = std::fmod(t, beatLen) / beatLen;
    const float lastKick = std::floor(t / beatLen) * beatLen;
    for (int k = 0; k < 4; ++k) {
        p.kicks[k] = lastKick - beatLen * k;
    }
    p.audio = glm::vec4(0.5f + 0.5f * std::exp(-6.0f * beat), 0.5f + 0.5f * std::sin(t * 0.7f),
                        0.5f + 0.5f * std::sin(t * 2.3f + 1.0f), beat);
    for (int b = 0; b < 32; ++b) {
        const float centre = 16.0f + 14.0f * std::sin(t * 0.5f);
        const float e = std::exp(-0.02f * (b - centre) * (b - centre)) * (0.6f + 0.4f * std::exp(-4.0f * beat));
        p.spectrum[b >> 2][b & 3] = e;
    }
}

} // namespace

int main(int argc, char** argv) {
    std::string arm = "gpu", scenario = "field", meshName = "mushroom", png, label;
    std::uint32_t n = 10000, warm = 60, frames = 300, width = 1920, height = 1080;
    int threads = 8;
    bool audio = false;
    float startTime = 10.0f;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--arm") arm = next();
        else if (a == "--n") n = static_cast<std::uint32_t>(std::stoul(next()));
        else if (a == "--scenario") scenario = next();
        else if (a == "--mesh") meshName = next();
        else if (a == "--frames") frames = static_cast<std::uint32_t>(std::stoul(next()));
        else if (a == "--warm") warm = static_cast<std::uint32_t>(std::stoul(next()));
        else if (a == "--threads") threads = std::stoi(next());
        else if (a == "--audio") audio = true;
        else if (a == "--png") png = next();
        else if (a == "--start") startTime = std::stof(next());
        else if (a == "--size") { const auto s = next(); std::sscanf(s.c_str(), "%ux%u", &width, &height); }
        else { std::fprintf(stderr, "unknown arg %s\n", a.c_str()); return 2; }
    }
    if (arm != "cpu" && arm != "cpumt" && arm != "cpugrid" && arm != "gpu") { std::fprintf(stderr, "--arm cpu|cpumt|cpugrid|gpu\n"); return 2; }

    const MeshCpu mesh = meshName == "firefly" ? makeFirefly() : makeMushroom();

    // ---- population (the "N entities" the CPU arm owns) ----
    const bool world = scenario == "world";
    const float side = world ? std::sqrt(static_cast<float>(n)) * 1.0f : 200.0f;
    const auto k = static_cast<std::uint32_t>(std::ceil(std::sqrt(static_cast<double>(n))));
    const float cell = side / static_cast<float>(k);
    std::vector<Base> bases(n);
    for (std::uint32_t i = 0; i < n; ++i) {
        const float gx = static_cast<float>(i % k), gz = static_cast<float>(i / k);
        const float jx = hashF(i, 1) - 0.5f, jz = hashF(i, 2) - 0.5f;
        const float s0 = cell * (meshName == "firefly" ? 0.25f : 0.55f) * (0.7f + 0.6f * hashF(i, 3));
        const float y = meshName == "firefly" ? cell * (0.5f + 2.0f * hashF(i, 4)) : 0.0f;
        bases[i].posScale = glm::vec4(-side * 0.5f + (gx + 0.5f + 0.8f * jx) * cell, y,
                                      -side * 0.5f + (gz + 0.5f + 0.8f * jz) * cell, s0);
        bases[i].rand = glm::vec4(hashF(i, 5) * 6.2831853f, hashF(i, 6), hashF(i, 7), hashF(i, 8));
    }

    // ---- camera, identical across arms ----
    glm::vec3 eye, target;
    float maxDistance = 0.0f;
    if (world) {
        eye = glm::vec3(0.0f, 2.5f, 0.0f);
        target = glm::vec3(20.0f, 0.0f, 6.0f);
        maxDistance = 120.0f;
    } else {
        eye = glm::vec3(0.0f, 45.0f, 125.0f);
        target = glm::vec3(0.0f, 0.0f, 10.0f);
    }
    const glm::mat4 view = glm::lookAt(eye, target, glm::vec3(0, 1, 0));
    glm::mat4 proj = glm::perspectiveZO(glm::radians(50.0f), static_cast<float>(width) / height, 0.1f, 1000.0f);
    DrawUniforms du{proj * view, glm::vec4(glm::normalize(glm::vec3(0.4f, 0.8f, 0.3f)), 0.0f)};

    // ---- GPU ----
    auto ctxR = gpu::Context::create(gpu::ContextDesc{});
    if (!ctxR) { std::fprintf(stderr, "context: %s\n", ctxR.error().message.c_str()); return 3; }
    gpu::Context& ctx = **ctxR;
    const wgpu::Device& dev = ctx.device();
    const wgpu::Queue& queue = ctx.queue();
    gpu::FrameTimeline timeline(ctx);

    const std::uint64_t instBytes = static_cast<std::uint64_t>(n) * sizeof(Inst);
    wgpu::Buffer vbuf = makeBuffer(ctx, mesh.vertices.size() * sizeof(Vtx), wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst, "vtx");
    queue.WriteBuffer(vbuf, 0, mesh.vertices.data(), mesh.vertices.size() * sizeof(Vtx));
    wgpu::Buffer ibuf = makeBuffer(ctx, mesh.indices.size() * 4, wgpu::BufferUsage::Index | wgpu::BufferUsage::CopyDst, "idx");
    queue.WriteBuffer(ibuf, 0, mesh.indices.data(), mesh.indices.size() * 4);
    wgpu::Buffer drawU = makeBuffer(ctx, sizeof(DrawUniforms), wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst, "drawU");
    queue.WriteBuffer(drawU, 0, &du, sizeof(du));
    wgpu::Buffer instBuf = makeBuffer(ctx, instBytes, wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst, "inst");
    wgpu::Buffer baseBuf, paramBuf, argsBuf;
    std::uint64_t gpuBufferBytes = mesh.vertices.size() * sizeof(Vtx) + mesh.indices.size() * 4 + instBytes;
    if (arm == "gpu") {
        baseBuf = makeBuffer(ctx, bases.size() * sizeof(Base), wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst, "base");
        queue.WriteBuffer(baseBuf, 0, bases.data(), bases.size() * sizeof(Base));
        paramBuf = makeBuffer(ctx, sizeof(Params), wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst, "params");
        argsBuf = makeBuffer(ctx, 32, wgpu::BufferUsage::Storage | wgpu::BufferUsage::Indirect | wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::CopySrc, "args");
        gpuBufferBytes += bases.size() * sizeof(Base) + sizeof(Params) + 32;
    }

    // targets
    wgpu::TextureDescriptor td{};
    td.size = {width, height, 1};
    td.format = wgpu::TextureFormat::RGBA8Unorm;
    td.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc;
    wgpu::Texture color = dev.CreateTexture(&td);
    td.format = wgpu::TextureFormat::Depth32Float;
    td.usage = wgpu::TextureUsage::RenderAttachment;
    wgpu::Texture depth = dev.CreateTexture(&td);
    wgpu::TextureView colorView = color.CreateView(), depthView = depth.CreateView();

    // pipelines
    const std::string common = kCommonWgsl;
    wgpu::ShaderModule drawMod = compile(ctx, common + kDrawWgsl, "draw");
    std::array<wgpu::VertexAttribute, 3> attrs{};
    attrs[0] = {nullptr, wgpu::VertexFormat::Float32x3, 0, 0};
    attrs[1] = {nullptr, wgpu::VertexFormat::Float32x3, 12, 1};
    attrs[2] = {nullptr, wgpu::VertexFormat::Float32x4, 24, 2};
    wgpu::VertexBufferLayout vbl{};
    vbl.arrayStride = sizeof(Vtx);
    vbl.attributeCount = attrs.size();
    vbl.attributes = attrs.data();
    wgpu::ColorTargetState cts{};
    cts.format = wgpu::TextureFormat::RGBA8Unorm;
    wgpu::FragmentState fsState{};
    fsState.module = drawMod;
    fsState.entryPoint = "fs";
    fsState.targetCount = 1;
    fsState.targets = &cts;
    wgpu::DepthStencilState ds{};
    ds.format = wgpu::TextureFormat::Depth32Float;
    ds.depthWriteEnabled = wgpu::OptionalBool::True;
    ds.depthCompare = wgpu::CompareFunction::Less;
    wgpu::RenderPipelineDescriptor rpd{};
    rpd.vertex.module = drawMod;
    rpd.vertex.entryPoint = "vs";
    rpd.vertex.bufferCount = 1;
    rpd.vertex.buffers = &vbl;
    rpd.fragment = &fsState;
    rpd.depthStencil = &ds;
    rpd.primitive.cullMode = wgpu::CullMode::None;
    wgpu::RenderPipeline drawPipe = dev.CreateRenderPipeline(&rpd);
    std::array<wgpu::BindGroupEntry, 2> dbe{};
    dbe[0].binding = 0; dbe[0].buffer = drawU; dbe[0].size = sizeof(DrawUniforms);
    dbe[1].binding = 1; dbe[1].buffer = instBuf; dbe[1].size = std::max<std::uint64_t>(instBytes, 48);
    wgpu::BindGroupDescriptor dbgd{};
    dbgd.layout = drawPipe.GetBindGroupLayout(0);
    dbgd.entryCount = dbe.size();
    dbgd.entries = dbe.data();
    wgpu::BindGroup drawBg = dev.CreateBindGroup(&dbgd);

    wgpu::ComputePipeline compPipe;
    wgpu::BindGroup compBg;
    std::uint32_t wgX = 0, wgY = 0;
    if (arm == "gpu") {
        wgpu::ShaderModule cm = compile(ctx, common + kComputeWgsl, "population");
        wgpu::ComputePipelineDescriptor cpd{};
        cpd.compute.module = cm;
        cpd.compute.entryPoint = "cs_population";
        compPipe = dev.CreateComputePipeline(&cpd);
        std::array<wgpu::BindGroupEntry, 4> e{};
        e[0].binding = 0; e[0].buffer = paramBuf; e[0].size = sizeof(Params);
        e[1].binding = 1; e[1].buffer = baseBuf; e[1].size = bases.size() * sizeof(Base);
        e[2].binding = 2; e[2].buffer = instBuf; e[2].size = std::max<std::uint64_t>(instBytes, 48);
        e[3].binding = 3; e[3].buffer = argsBuf; e[3].size = 20;
        wgpu::BindGroupDescriptor d{};
        d.layout = compPipe.GetBindGroupLayout(0);
        d.entryCount = e.size();
        d.entries = e.data();
        compBg = dev.CreateBindGroup(&d);
        const std::uint32_t groups = (n + 63) / 64;
        wgX = std::min<std::uint32_t>(groups, 32768);
        wgY = (groups + wgX - 1) / wgX;
    }
    ctx.waitForQueue();
    if (ctx.errorCount() != 0) {
        std::fprintf(stderr, "setup errors: %s\n", ctx.lastError().c_str());
        return 4;
    }

    // ---- cpugrid: bucket the rest positions once ----
    struct Grid { float cell = 8.0f; float minX = 0, minZ = 0; int nx = 1, nz = 1; std::vector<std::uint32_t> start, items; float maxY = 0; float maxR = 0; };
    Grid grid;
    if (arm == "cpugrid") {
        float lx = 1e30f, lz = 1e30f, hx = -1e30f, hz = -1e30f;
        for (const auto& b : bases) {
            lx = std::min(lx, b.posScale.x); hx = std::max(hx, b.posScale.x);
            lz = std::min(lz, b.posScale.z); hz = std::max(hz, b.posScale.z);
            grid.maxY = std::max(grid.maxY, b.posScale.y);
            grid.maxR = std::max(grid.maxR, b.posScale.w);
        }
        grid.minX = lx; grid.minZ = lz;
        grid.nx = std::max(1, static_cast<int>((hx - lx) / grid.cell) + 1);
        grid.nz = std::max(1, static_cast<int>((hz - lz) / grid.cell) + 1);
        std::vector<std::uint32_t> counts(static_cast<std::size_t>(grid.nx) * grid.nz + 1, 0);
        auto cellOf = [&](const Base& b) {
            const int cx = std::clamp(static_cast<int>((b.posScale.x - grid.minX) / grid.cell), 0, grid.nx - 1);
            const int cz = std::clamp(static_cast<int>((b.posScale.z - grid.minZ) / grid.cell), 0, grid.nz - 1);
            return static_cast<std::size_t>(cz) * grid.nx + cx;
        };
        for (const auto& b : bases) ++counts[cellOf(b) + 1];
        for (std::size_t c = 1; c < counts.size(); ++c) counts[c] += counts[c - 1];
        grid.start = counts;
        grid.items.resize(bases.size());
        std::vector<std::uint32_t> fill(counts.begin(), counts.end() - 1);
        for (std::uint32_t i = 0; i < n; ++i) grid.items[fill[cellOf(bases[i])]++] = i;
    }

    // ---- loop ----
    Params params{};
    framePlanes(du.viewProj, params.planes);
    params.camPos = glm::vec4(eye, maxDistance);
    params.wave = glm::vec4(0.0f, 0.0f, world ? 25.0f : 60.0f, 5.0f);
    std::vector<Inst> staging(arm == "gpu" ? 0 : n);
    std::unique_ptr<Pool> pool;
    std::vector<std::uint32_t> chunkCount;
    if (arm == "cpumt") {
        pool = std::make_unique<Pool>(threads);
        chunkCount.assign(threads, 0);
    }
    Stat sInterval, sCpu, sUpdate, sUpload, sEncode, sSubmit, sWait, sGpuFrame, sGpuCompute, sGpuDraw, sVisible;
    std::array<wgpu::Future, 2> inflight{};
    std::array<bool, 2> inflightValid{false, false};
    std::uint64_t lastCompleted = 0;
    Clock::time_point prevStart{};
    std::uint32_t visibleCpu = 0;
    const std::uint32_t total = warm + frames;
    const std::uint32_t argsInit[5] = {static_cast<std::uint32_t>(mesh.indices.size()), 0, 0, 0, 0};
    std::uint64_t footprintBefore = 0;

    for (std::uint32_t f = 0; f < total; ++f) {
        const bool measuring = f >= warm;
        if (f == warm) footprintBefore = footprintBytes();
        const auto start = Clock::now();
        if (measuring && f > warm) sInterval.add(std::chrono::duration<double, std::milli>(start - prevStart).count());
        prevStart = start;

        const int slot = static_cast<int>(f % 2);
        const auto w0 = Clock::now();
        if (inflightValid[slot]) { (void)ctx.waitFor(inflight[slot]); }
        const double waitMs = msSince(w0);
        const auto cpu0 = Clock::now();

        const float t = startTime + static_cast<float>(f) / 60.0f;
        params.tc = glm::vec4(t, static_cast<float>(n), mesh.radius, audio ? 1.0f : 0.0f);
        if (audio) synthAudio(t, params); else { params.audio = glm::vec4(0); params.kicks = glm::vec4(-1); }

        // update
        const auto u0 = Clock::now();
        if (arm == "cpu") {
            std::uint32_t count = 0;
            for (std::uint32_t i = 0; i < n; ++i) {
                if (!mayBeVisible(bases[i], params)) continue;
                const Inst o = animate(bases[i], params);
                if (visible(o, params)) staging[count++] = o;
            }
            visibleCpu = count;
        } else if (arm == "cpumt") {
            const int T = pool->size();
            std::function<void(int)> job = [&](int w) {
                const std::uint32_t lo = static_cast<std::uint32_t>(static_cast<std::uint64_t>(n) * w / T);
                const std::uint32_t hi = static_cast<std::uint32_t>(static_cast<std::uint64_t>(n) * (w + 1) / T);
                std::uint32_t c = lo;
                for (std::uint32_t i = lo; i < hi; ++i) {
                    if (!mayBeVisible(bases[i], params)) continue;
                    const Inst o = animate(bases[i], params);
                    if (visible(o, params)) staging[c++] = o;
                }
                chunkCount[w] = c - lo;
            };
            pool->run(job);
        } else if (arm == "cpugrid") {
            // Conservative: a cell is visited if its box, grown by the largest instance's reach, can
            // touch the frustum and lies within maxDistance. Same per-instance tests as `cpu` after that.
            std::uint32_t count = 0;
            const float growth = params.tc.w > 0.5f ? 1.08f * (1.0f + 0.35f + 1.6f) : 1.08f;
            const float reach = grid.maxR * params.tc.z * growth + 0.05f * grid.maxR + (params.tc.w > 0.5f ? 2.4f * grid.maxR : 0.0f);
            int x0 = 0, x1 = grid.nx - 1, z0 = 0, z1 = grid.nz - 1;
            if (params.camPos.w > 0.0f) {
                const float rr = params.camPos.w + reach;
                x0 = std::max(0, static_cast<int>((params.camPos.x - rr - grid.minX) / grid.cell));
                x1 = std::min(grid.nx - 1, static_cast<int>((params.camPos.x + rr - grid.minX) / grid.cell));
                z0 = std::max(0, static_cast<int>((params.camPos.z - rr - grid.minZ) / grid.cell));
                z1 = std::min(grid.nz - 1, static_cast<int>((params.camPos.z + rr - grid.minZ) / grid.cell));
            }
            for (int cz = z0; cz <= z1; ++cz) {
                for (int cx = x0; cx <= x1; ++cx) {
                    const glm::vec3 lo(grid.minX + cx * grid.cell - reach, -reach, grid.minZ + cz * grid.cell - reach);
                    const glm::vec3 hi(lo.x + grid.cell + 2 * reach, grid.maxY + reach + 2.0f * grid.maxR, lo.z + grid.cell + 2 * reach);
                    bool out = false;
                    for (int k = 0; k < 6 && !out; ++k) {
                        const glm::vec3 nrm(params.planes[k]);
                        const glm::vec3 pv(nrm.x >= 0 ? hi.x : lo.x, nrm.y >= 0 ? hi.y : lo.y, nrm.z >= 0 ? hi.z : lo.z);
                        out = glm::dot(nrm, pv) + params.planes[k].w < 0.0f;
                    }
                    if (out) continue;
                    const std::size_t c = static_cast<std::size_t>(cz) * grid.nx + cx;
                    for (std::uint32_t q = grid.start[c]; q < grid.start[c + 1]; ++q) {
                        const Base& b = bases[grid.items[q]];
                        if (!mayBeVisible(b, params)) continue;
                        const Inst o = animate(b, params);
                        if (visible(o, params)) staging[count++] = o;
                    }
                }
            }
            visibleCpu = count;
        }
        const double updateMs = msSince(u0);

        // upload
        const auto up0 = Clock::now();
        if (arm == "cpu" || arm == "cpugrid") {
            if (visibleCpu) queue.WriteBuffer(instBuf, 0, staging.data(), visibleCpu * sizeof(Inst));
        } else if (arm == "cpumt") {
            std::uint32_t off = 0;
            const int T = pool->size();
            for (int w = 0; w < T; ++w) {
                const std::uint32_t lo = static_cast<std::uint32_t>(static_cast<std::uint64_t>(n) * w / T);
                if (chunkCount[w]) queue.WriteBuffer(instBuf, off * sizeof(Inst), staging.data() + lo, chunkCount[w] * sizeof(Inst));
                off += chunkCount[w];
            }
            visibleCpu = off;
        } else {
            queue.WriteBuffer(paramBuf, 0, &params, sizeof(params));
            queue.WriteBuffer(argsBuf, 0, argsInit, sizeof(argsInit));
        }
        const double uploadMs = msSince(up0);

        // encode
        const auto e0 = Clock::now();
        timeline.beginFrame();
        wgpu::CommandEncoder enc = dev.CreateCommandEncoder();
        if (arm == "gpu" && n > 0) {
            wgpu::ComputePassDescriptor cpd{};
            cpd.timestampWrites = timeline.mark("population", gpu::FrameTimeline::PassKind::Compute);
            wgpu::ComputePassEncoder cp = enc.BeginComputePass(&cpd);
            cp.SetPipeline(compPipe);
            cp.SetBindGroup(0, compBg);
            cp.DispatchWorkgroups(wgX, wgY, 1);
            cp.End();
        }
        wgpu::RenderPassColorAttachment ca{};
        ca.view = colorView;
        ca.loadOp = wgpu::LoadOp::Clear;
        ca.storeOp = wgpu::StoreOp::Store;
        ca.clearValue = {0.02, 0.025, 0.04, 1.0};
        wgpu::RenderPassDepthStencilAttachment da{};
        da.view = depthView;
        da.depthLoadOp = wgpu::LoadOp::Clear;
        da.depthStoreOp = wgpu::StoreOp::Store;
        da.depthClearValue = 1.0f;
        wgpu::RenderPassDescriptor rp{};
        rp.colorAttachmentCount = 1;
        rp.colorAttachments = &ca;
        rp.depthStencilAttachment = &da;
        rp.timestampWrites = timeline.mark("draw", gpu::FrameTimeline::PassKind::Render);
        wgpu::RenderPassEncoder r = enc.BeginRenderPass(&rp);
        r.SetPipeline(drawPipe);
        r.SetBindGroup(0, drawBg);
        r.SetVertexBuffer(0, vbuf);
        r.SetIndexBuffer(ibuf, wgpu::IndexFormat::Uint32);
        if (arm == "gpu") {
            r.DrawIndexedIndirect(argsBuf, 0);
        } else if (visibleCpu) {
            r.DrawIndexed(static_cast<std::uint32_t>(mesh.indices.size()), visibleCpu);
        }
        r.End();
        timeline.resolve(enc);
        wgpu::CommandBuffer cb = enc.Finish();
        const double encodeMs = msSince(e0);

        const auto s0 = Clock::now();
        queue.Submit(1, &cb);
        inflight[slot] = queue.OnSubmittedWorkDone(wgpu::CallbackMode::WaitAnyOnly, [](wgpu::QueueWorkDoneStatus, wgpu::StringView) {});
        inflightValid[slot] = true;
        const double submitMs = msSince(s0);
        timeline.collect();
        const double cpuMs = msSince(cpu0);

        if (measuring) {
            sCpu.add(cpuMs); sUpdate.add(updateMs); sUpload.add(uploadMs); sEncode.add(encodeMs);
            sSubmit.add(submitMs); sWait.add(waitMs);
            if (arm != "gpu") sVisible.add(visibleCpu);
            if (timeline.completedFrames() != lastCompleted) {
                lastCompleted = timeline.completedFrames();
                sGpuFrame.add(timeline.frameMs());
                sGpuDraw.add(timeline.msFor("draw"));
                if (arm == "gpu") sGpuCompute.add(timeline.msFor("population"));
            }
        }
    }
    ctx.waitForQueue();
    const std::uint64_t footprintAfter = footprintBytes();

    std::uint32_t visibleGpu = 0;
    if (arm == "gpu") {
        wgpu::Buffer rb = makeBuffer(ctx, 32, wgpu::BufferUsage::MapRead | wgpu::BufferUsage::CopyDst, "rb");
        wgpu::CommandEncoder enc = dev.CreateCommandEncoder();
        enc.CopyBufferToBuffer(argsBuf, 0, rb, 0, 32);
        wgpu::CommandBuffer cb = enc.Finish();
        queue.Submit(1, &cb);
        auto fut = rb.MapAsync(wgpu::MapMode::Read, 0, 32, wgpu::CallbackMode::WaitAnyOnly, [](wgpu::MapAsyncStatus, wgpu::StringView) {});
        (void)ctx.waitFor(fut);
        visibleGpu = static_cast<const std::uint32_t*>(rb.GetConstMappedRange(0, 32))[1];
    }
    if (!png.empty()) {
        auto img = gpu::readTexture8(ctx, color, width, height, false);
        if (img) (void)assets::writePng(png, width, height, img->rgba);
    }
    if (ctx.errorCount() != 0) {
        std::fprintf(stderr, "gpu errors: %s\n", ctx.lastError().c_str());
        return 5;
    }

    const std::uint64_t cpuStateBytes = bases.size() * sizeof(Base) + staging.size() * sizeof(Inst) + grid.items.size() * 4 + grid.start.size() * 4;
    auto j = [](const char* name, const Stat& s) {
        std::printf("\"%s\":{\"p10\":%.4f,\"p50\":%.4f,\"p90\":%.4f,\"mean\":%.4f,\"n\":%zu},", name, s.pct(0.1), s.pct(0.5),
                    s.pct(0.9), s.mean(), s.v.size());
    };
    std::printf("{\"arm\":\"%s\",\"scenario\":\"%s\",\"mesh\":\"%s\",\"n\":%u,\"audio\":%d,\"threads\":%d,"
                "\"triangles_per_instance\":%zu,\"frames\":%u,",
                arm.c_str(), scenario.c_str(), meshName.c_str(), n, audio ? 1 : 0, arm == "cpumt" ? threads : (arm == "gpu" ? 0 : 1),
                mesh.indices.size() / 3, frames);
    j("interval_ms", sInterval); j("cpu_ms", sCpu); j("update_ms", sUpdate); j("upload_ms", sUpload);
    j("encode_ms", sEncode); j("submit_ms", sSubmit); j("wait_ms", sWait); j("gpu_frame_ms", sGpuFrame);
    j("gpu_draw_ms", sGpuDraw); j("gpu_compute_ms", sGpuCompute);
    std::printf("\"visible\":%u,\"upload_bytes_per_frame\":%llu,\"gpu_buffer_bytes\":%llu,\"cpu_state_bytes\":%llu,"
                "\"footprint_mb\":%.1f,\"footprint_growth_mb\":%.1f,\"draw_calls\":1,\"dispatches\":%d,\"workgroups\":%u}\n",
                arm == "gpu" ? visibleGpu : visibleCpu,
                static_cast<unsigned long long>(arm == "gpu" ? sizeof(Params) + 20 : static_cast<std::uint64_t>(visibleCpu) * sizeof(Inst)),
                static_cast<unsigned long long>(gpuBufferBytes), static_cast<unsigned long long>(cpuStateBytes),
                footprintAfter / 1048576.0, (static_cast<double>(footprintAfter) - footprintBefore) / 1048576.0,
                arm == "gpu" ? 1 : 0, wgX * wgY);
    return 0;
}
