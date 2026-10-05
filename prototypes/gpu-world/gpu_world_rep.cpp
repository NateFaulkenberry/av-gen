// GPU World research spike, Phase 4 -- DISPOSABLE PROTOTYPE (docs/research/gpu-world-architecture-spike.md).
//
// One narrow procedural world, a bounded meadow, held two ways and drawn by the same renderer:
//
//   compact   The world is its description (MeadowDesc, a few hundred bytes). Every frame one compute
//             dispatch visits, per layer, only the cells within that layer's view distance of the
//             camera, hashes each into presence / position / size / yaw, culls and appends. Nothing is
//             stored. (Phase 3's Endless Meadow, bounded and stripped of audio and look.)
//   expanded  The conventional representation: at load the CPU expands the same description over the
//             whole bounded world into instance records (this is the "flatten"), uploads them once, and
//             every frame a compute pass culls all N records by frustum and per-layer view distance and
//             appends the survivors. This is production's shape (CPU scatter at flatten, GPU cull).
//
// The generating rule is written twice, in C++ and WGSL, operation for operation, so both arms place
// the same things. The cull rule, the meshes, the draw and the camera are shared.
//
// Not production code. Hard-coded on purpose. Not linked into anything.

#include "assets/image.hpp"
#include "gpu/context.hpp"
#include "gpu/frame_timeline.hpp"
#include "gpu/readback.hpp"
#include "organism/mushroom.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <mach/mach.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <numeric>
#include <string>
#include <vector>

using namespace avgen;
using Clock = std::chrono::steady_clock;

namespace {

double msSince(Clock::time_point a) { return std::chrono::duration<double, std::milli>(Clock::now() - a).count(); }

std::uint64_t footprintBytes() {
    task_vm_info_data_t info{};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS) return 0;
    return info.phys_footprint;
}

// ---- the world description: this IS the compact representation ----------------------------------

constexpr int kLayers = 4;
struct LayerDesc {        // 32 bytes
    float cell;           // metres per cell; at most one element per cell
    float presence;       // probability a cell holds one, before clustering
    float clusterScale;   // metres of the patch pattern
    float clustering;     // 0 even .. 1 entirely in patches
    float sizeMin, sizeMax;
    float viewDistance;   // metres; beyond it nothing of this layer is drawn
    float meshRadius;     // bounding radius of the unit mesh (set from the mesh)
};
struct MeadowDesc {       // 16 + 4 * 32 = 144 bytes
    std::uint32_t seed;
    float size;           // the world is the square [-size/2, size/2]^2
    float terrainAmp, terrainFreq;
    LayerDesc layers[kLayers];
};

MeadowDesc makeDesc(float size) {
    MeadowDesc d{};
    d.seed = 20261005u;
    d.size = size;
    d.terrainAmp = 30.0f;
    d.terrainFreq = 0.004f;
    //            cell   pres  clScale clust  sMin  sMax  viewDist radius
    d.layers[0] = {0.35f, 0.85f, 18.0f, 0.5f, 0.25f, 0.6f, 40.0f, 1.0f};   // grass   (~6 /m^2 at its densest)
    d.layers[1] = {2.0f, 0.5f, 60.0f, 0.7f, 0.9f, 2.2f, 150.0f, 1.0f};     // reeds   (~0.1 /m^2)
    d.layers[2] = {9.0f, 0.35f, 120.0f, 0.6f, 0.8f, 2.4f, 220.0f, 1.0f};   // mushrooms (~0.004 /m^2)
    d.layers[3] = {24.0f, 0.4f, 300.0f, 0.8f, 4.0f, 16.0f, 700.0f, 1.0f};  // spires  (~0.0007 /m^2)
    return d;
}

// ---- the generating rule, CPU side (must match kGenWgsl) ----------------------------------------

std::uint32_t hashu(std::uint32_t x) { x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x; }
std::uint32_t hash2i(int cx, int cy, std::uint32_t salt) {
    return hashu((static_cast<std::uint32_t>(cx) * 0x8da6b343u) ^ hashu((static_cast<std::uint32_t>(cy) * 0xd8163841u) ^ (salt * 0xcb1ab31fu)));
}
float u01(std::uint32_t h) { return static_cast<float>(h >> 8) / 16777216.0f; }
float vnoise(glm::vec2 p, std::uint32_t salt) {
    const int ix = static_cast<int>(std::floor(p.x)), iy = static_cast<int>(std::floor(p.y));
    const glm::vec2 f = p - glm::floor(p);
    const glm::vec2 u = f * f * (3.0f - 2.0f * f);
    const float a = u01(hash2i(ix, iy, salt)), b = u01(hash2i(ix + 1, iy, salt));
    const float c = u01(hash2i(ix, iy + 1, salt)), d = u01(hash2i(ix + 1, iy + 1, salt));
    return (a + (b - a) * u.x) + ((c + (d - c) * u.x) - (a + (b - a) * u.x)) * u.y;
}
float terrainH(glm::vec2 p, const MeadowDesc& d) {
    float a = 0.5f, s = 0.0f;
    glm::vec2 q = p * d.terrainFreq;
    for (int k = 0; k < 4; ++k) { s += a * vnoise(q, 11u); q = glm::vec2(1.6f * q.x - 1.2f * q.y, 1.2f * q.x + 1.6f * q.y); a *= 0.5f; }
    return (s - 0.5f) * d.terrainAmp;
}
float smooth01(float e0, float e1, float x) { const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f); return t * t * (3.0f - 2.0f * t); }

struct Inst { glm::vec4 posScale, quat, color; }; // 48 bytes
static_assert(sizeof(Inst) == 48);

// One cell of one layer: does it hold an element, and what is it? Pure function of (desc, layer, cell).
bool generate(const MeadowDesc& d, int L, int cx, int cz, Inst& o) {
    const LayerDesc& P = d.layers[L];
    const std::uint32_t h = hash2i(cx, cz, d.seed + static_cast<std::uint32_t>(L) * 7919u);
    const float r0 = u01(h), r1 = u01(hashu(h + 1u)), r2 = u01(hashu(h + 2u)), r3 = u01(hashu(h + 3u)), r4 = u01(hashu(h + 4u));
    const glm::vec2 xz((static_cast<float>(cx) + 0.05f + 0.9f * r1) * P.cell, (static_cast<float>(cz) + 0.05f + 0.9f * r2) * P.cell);
    const float half = 0.5f * d.size;
    if (xz.x < -half || xz.x >= half || xz.y < -half || xz.y >= half) return false;
    const float cl = smooth01(0.35f, 0.65f, vnoise(xz / P.clusterScale + glm::vec2(5.3f * L, 1.7f * L), 23u));
    if (r0 >= P.presence * (1.0f - P.clustering + P.clustering * cl)) return false;
    const float s = P.sizeMin + (P.sizeMax - P.sizeMin) * r3;
    const float yaw = r4 * 6.2831853f;
    o.posScale = glm::vec4(xz.x, terrainH(xz, d), xz.y, s);
    o.quat = glm::vec4(0.0f, std::sin(0.5f * yaw), 0.0f, std::cos(0.5f * yaw));
    o.color = glm::vec4(0.6f + 0.4f * r1, 0.6f + 0.4f * r2, 0.6f + 0.4f * r3, r0);
    return true;
}

// ---- WGSL ---------------------------------------------------------------------------------------

const char* kCommonWgsl = R"(
struct Inst { posScale: vec4f, quat: vec4f, color: vec4f, };
struct Layer { a: vec4f, b: vec4f, };   // a: cell, presence, clusterScale, clustering; b: sizeMin, sizeMax, viewDist, meshRadius
struct World { hdr: vec4f, layers: array<Layer, 4>, };   // hdr: seed (bits), size, terrainAmp, terrainFreq
struct Frame {
    planes: array<vec4f, 6>,
    cam: vec4f,                 // xyz, t
    win: array<vec4i, 4>,       // per layer: window origin cell x, z, window dim, first thread
    counts: vec4u,              // total threads, cap, record count (expanded), -
    ranges: vec4u,              // expanded: first record of layers 1..3 in .x .y .z
};
@group(0) @binding(0) var<uniform> W: World;
@group(0) @binding(1) var<uniform> F: Frame;
fn qrot(q: vec4f, v: vec3f) -> vec3f { return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v); }
)";

const char* kGenWgsl = R"(
@group(0) @binding(2) var<storage, read_write> outInst: array<Inst>;
@group(0) @binding(3) var<storage, read_write> args: array<atomic<u32>, 20>;
@group(0) @binding(4) var<storage, read> records: array<Inst>;

fn hashu(x0: u32) -> u32 { var x = x0; x ^= x >> 16u; x *= 0x7feb352du; x ^= x >> 15u; x *= 0x846ca68bu; x ^= x >> 16u; return x; }
fn hash2i(c: vec2i, salt: u32) -> u32 { return hashu((u32(c.x) * 0x8da6b343u) ^ hashu((u32(c.y) * 0xd8163841u) ^ (salt * 0xcb1ab31fu))); }
fn u01(h: u32) -> f32 { return f32(h >> 8u) / 16777216.0; }
fn vnoise(p: vec2f, salt: u32) -> f32 {
    let i = vec2i(floor(p)); let f = p - floor(p); let u = f * f * (3.0 - 2.0 * f);
    let a = u01(hash2i(i, salt)); let b = u01(hash2i(i + vec2i(1, 0), salt));
    let c = u01(hash2i(i + vec2i(0, 1), salt)); let d = u01(hash2i(i + vec2i(1, 1), salt));
    return (a + (b - a) * u.x) + ((c + (d - c) * u.x) - (a + (b - a) * u.x)) * u.y;
}
fn terrainH(p: vec2f) -> f32 {
    var a = 0.5; var s = 0.0; var q = p * W.hdr.w;
    for (var k = 0; k < 4; k++) { s += a * vnoise(q, 11u); q = vec2f(1.6 * q.x - 1.2 * q.y, 1.2 * q.x + 1.6 * q.y); a *= 0.5; }
    return (s - 0.5) * W.hdr.z;
}
fn smooth01(e0: f32, e1: f32, x: f32) -> f32 { let t = clamp((x - e0) / (e1 - e0), 0.0, 1.0); return t * t * (3.0 - 2.0 * t); }

fn generate(L: i32, c: vec2i, o: ptr<function, Inst>) -> bool {
    let P = W.layers[L];
    let h = hash2i(c, bitcast<u32>(W.hdr.x) + u32(L) * 7919u);
    let r0 = u01(h); let r1 = u01(hashu(h + 1u)); let r2 = u01(hashu(h + 2u)); let r3 = u01(hashu(h + 3u)); let r4 = u01(hashu(h + 4u));
    let xz = vec2f((f32(c.x) + 0.05 + 0.9 * r1) * P.a.x, (f32(c.y) + 0.05 + 0.9 * r2) * P.a.x);
    let half = 0.5 * W.hdr.y;
    if (xz.x < -half || xz.x >= half || xz.y < -half || xz.y >= half) { return false; }
    let cl = smooth01(0.35, 0.65, vnoise(xz / P.a.z + vec2f(5.3 * f32(L), 1.7 * f32(L)), 23u));
    if (r0 >= P.a.y * (1.0 - P.a.w + P.a.w * cl)) { return false; }
    let s = P.b.x + (P.b.y - P.b.x) * r3;
    let yaw = r4 * 6.2831853;
    (*o).posScale = vec4f(xz.x, terrainH(xz), xz.y, s);
    (*o).quat = vec4f(0.0, sin(0.5 * yaw), 0.0, cos(0.5 * yaw));
    (*o).color = vec4f(0.6 + 0.4 * r1, 0.6 + 0.4 * r2, 0.6 + 0.4 * r3, r0);
    return true;
}

fn visible(L: i32, o: Inst) -> bool {
    let P = W.layers[L];
    let r = o.posScale.w * P.b.w;
    let c = o.posScale.xyz + vec3f(0.0, 0.5 * o.posScale.w, 0.0);
    if (length(o.posScale.xyz - F.cam.xyz) > P.b.z) { return false; }
    for (var k = 0; k < 6; k++) { if (dot(F.planes[k].xyz, c) + F.planes[k].w < -r) { return false; } }
    return true;
}

var<workgroup> wgCount: array<atomic<u32>, 4>;
var<workgroup> wgBase: array<u32, 4>;

fn append(li: u32, L: i32, vis: bool, o: Inst) {
    var slot = 0u;
    if (vis) { slot = atomicAdd(&wgCount[L], 1u); }
    workgroupBarrier();
    if (li < 4u) { wgBase[li] = atomicAdd(&args[li * 5u + 1u], atomicLoad(&wgCount[li])); }
    workgroupBarrier();
    let cap = F.counts.y;
    if (vis && wgBase[L] + slot < cap) { outInst[u32(L) * cap + wgBase[L] + slot] = o; }
}

@compute @workgroup_size(64)
fn cs_compact(@builtin(global_invocation_id) gid: vec3u, @builtin(local_invocation_index) li: u32, @builtin(num_workgroups) nwg: vec3u) {
    if (li < 4u) { atomicStore(&wgCount[li], 0u); }
    workgroupBarrier();
    let id = gid.x + gid.y * nwg.x * 64u;
    var L = 0;
    for (var k = 1; k < 4; k++) { if (id >= u32(F.win[k].w)) { L = k; } }
    var o: Inst;
    var vis = false;
    if (id < F.counts.x) {
        let w = F.win[L];
        let local = id - u32(w.w);
        let c = vec2i(w.x + i32(local % u32(w.z)), w.y + i32(local / u32(w.z)));
        if (generate(L, c, &o)) { vis = visible(L, o); }
    }
    append(li, L, vis, o);
}

@compute @workgroup_size(64)
fn cs_expanded(@builtin(global_invocation_id) gid: vec3u, @builtin(local_invocation_index) li: u32, @builtin(num_workgroups) nwg: vec3u) {
    if (li < 4u) { atomicStore(&wgCount[li], 0u); }
    workgroupBarrier();
    let id = gid.x + gid.y * nwg.x * 64u;
    var L = 0;
    if (id >= F.ranges.x) { L = 1; }
    if (id >= F.ranges.y) { L = 2; }
    if (id >= F.ranges.z) { L = 3; }
    var o: Inst;
    var vis = false;
    if (id < F.counts.z) { o = records[id]; vis = visible(L, o); }
    append(li, L, vis, o);
}
)";

const char* kDrawWgsl = R"(
@group(1) @binding(0) var<storage, read> insts: array<Inst>;
struct VIn { @location(0) pos: vec3f, @location(1) nrm: vec3f, @location(2) col: vec4f, };
struct VOut { @builtin(position) clip: vec4f, @location(0) n: vec3f, @location(1) col: vec4f, @location(2) tint: vec4f, @location(3) wpos: vec3f, };
struct Draw { viewProj: mat4x4f, light: vec4f, };
@group(1) @binding(1) var<uniform> D: Draw;
@vertex fn vs(v: VIn, @builtin(instance_index) ii: u32) -> VOut {
    let inst = insts[ii];
    var lp = v.pos;
    // wind: a stateless sway, a pure function of (position, time), the same in both arms
    let t = F.cam.w;
    let ph = inst.posScale.x * 0.21 + inst.posScale.z * 0.17;
    lp += vec3f(sin(1.3 * t + ph), 0.0, cos(0.9 * t + 1.7 * ph)) * 0.08 * lp.y * lp.y;
    let p = qrot(inst.quat, lp * inst.posScale.w) + inst.posScale.xyz;
    var o: VOut;
    o.clip = D.viewProj * vec4f(p, 1.0);
    o.n = qrot(inst.quat, v.nrm);
    o.col = v.col;
    o.tint = inst.color;
    o.wpos = p;
    return o;
}
@fragment fn fs(i: VOut, @builtin(front_facing) ff: bool) -> @location(0) vec4f {
    var n = normalize(i.n);
    if (!ff) { n = -n; }
    let albedo = i.col.rgb * i.tint.rgb;
    var lit = albedo * (0.15 + 0.85 * max(dot(n, D.light.xyz), 0.0)) + i.col.a * i.tint.rgb * 0.6;
    let fogT = 1.0 - exp(-length(i.wpos - F.cam.xyz) * 0.004);
    lit = mix(lit, vec3f(0.05, 0.06, 0.09), fogT);
    let m = lit / (1.0 + lit);
    return vec4f(pow(m, vec3f(1.0 / 2.2)), 1.0);
}
)";

// ---- meshes ---------------------------------------------------------------------------------------

struct Vtx { float pos[3]; float nrm[3]; float col[4]; };
struct MeshCpu { std::vector<Vtx> v; std::vector<std::uint32_t> i; float radius = 0.0f; };

void addTri(MeshCpu& m, glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec4 col) {
    const glm::vec3 n = glm::normalize(glm::cross(b - a, c - a));
    for (const glm::vec3& p : {a, b, c}) {
        m.i.push_back(static_cast<std::uint32_t>(m.v.size()));
        m.v.push_back({{p.x, p.y, p.z}, {n.x, n.y, n.z}, {col.x, col.y, col.z, col.w}});
        m.radius = std::max(m.radius, glm::length(p));
    }
}
MeshCpu makeBlade() {
    MeshCpu m;
    for (int k = 0; k < 3; ++k) {
        const float y0 = k / 3.0f, y1 = (k + 1) / 3.0f, w0 = 0.04f * (1 - y0), w1 = 0.04f * (1 - y1);
        const glm::vec4 c(0.25f, 0.55f, 0.3f, 0.0f);
        addTri(m, {-w0, y0, 0.1f * y0 * y0}, {w0, y0, 0.1f * y0 * y0}, {w1, y1, 0.1f * y1 * y1}, c);
        addTri(m, {-w0, y0, 0.1f * y0 * y0}, {w1, y1, 0.1f * y1 * y1}, {-w1, y1, 0.1f * y1 * y1}, c);
    }
    return m;
}
MeshCpu makePrism(int sides, float r, float topR, glm::vec4 col) { // reed stalks and spires
    MeshCpu m;
    for (int s = 0; s < sides; ++s) {
        const float a0 = 6.2831853f * s / sides, a1 = 6.2831853f * (s + 1) / sides;
        const glm::vec3 b0(r * std::cos(a0), 0, r * std::sin(a0)), b1(r * std::cos(a1), 0, r * std::sin(a1));
        const glm::vec3 t0(topR * std::cos(a0), 0.85f, topR * std::sin(a0)), t1(topR * std::cos(a1), 0.85f, topR * std::sin(a1));
        addTri(m, b0, t1, b1, col);
        addTri(m, b0, t0, t1, col);
        addTri(m, t0, {0, 1, 0}, t1, glm::vec4(glm::vec3(col), 1.0f));
    }
    return m;
}
MeshCpu makeMushroom() {
    const auto& specs = organism::mushroomSchema().parameters.specs();
    search::Parameters values;
    for (const auto& s : specs) { const float v = 0.5f * (s.min + s.max); values.push_back(s.integral ? std::round(v) : v); }
    auto subject = organism::buildMushroom(values);
    MeshCpu m;
    if (!subject) { std::fprintf(stderr, "mushroom: %s\n", subject.error().message.c_str()); std::exit(2); }
    glm::vec3 lo(1e9f), hi(-1e9f);
    for (const auto& part : subject->parts) for (const auto& v : part.mesh.vertices) { lo = glm::min(lo, v.position); hi = glm::max(hi, v.position); }
    const float height = std::max(hi.y - lo.y, 1e-3f);
    const glm::vec3 centre((lo.x + hi.x) * 0.5f, lo.y, (lo.z + hi.z) * 0.5f);
    for (const auto& part : subject->parts) {
        const auto first = static_cast<std::uint32_t>(m.v.size());
        const float glow = std::min(part.emissiveIntensity, 1.0f);
        for (const auto& v : part.mesh.vertices) {
            const glm::vec3 p = (v.position - centre) / height;
            m.v.push_back({{p.x, p.y, p.z}, {v.normal.x, v.normal.y, v.normal.z}, {part.baseColor.x, part.baseColor.y, part.baseColor.z, glow}});
            m.radius = std::max(m.radius, glm::length(p));
        }
        for (auto i : part.mesh.indices) m.i.push_back(first + i);
    }
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
    d.size = std::max<std::uint64_t>((size + 255) & ~255ull, 256);
    d.usage = usage;
    d.label = label;
    return ctx.device().CreateBuffer(&d);
}
void framePlanes(const glm::mat4& vp, glm::vec4 out[6]) {
    const glm::mat4 m = glm::transpose(vp);
    out[0] = m[3] + m[0]; out[1] = m[3] - m[0]; out[2] = m[3] + m[1]; out[3] = m[3] - m[1]; out[4] = m[2]; out[5] = m[3] - m[2];
    for (int k = 0; k < 6; ++k) out[k] /= glm::length(glm::vec3(out[k]));
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
};

struct WorldU { glm::vec4 hdr; glm::vec4 layers[8]; };
struct FrameU { glm::vec4 planes[6]; glm::vec4 cam; glm::ivec4 win[4]; glm::uvec4 counts; glm::uvec4 ranges; };
struct DrawU { glm::mat4 viewProj; glm::vec4 light; };

} // namespace

int main(int argc, char** argv) {
    std::string rep = "compact", png;
    float size = 500.0f;
    std::uint32_t warm = 60, frames = 300, width = 1920, height = 1080;
    int shot = 0;
    float startTime = 10.0f;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--rep") rep = next();
        else if (a == "--size") size = std::stof(next());
        else if (a == "--frames") frames = static_cast<std::uint32_t>(std::stoul(next()));
        else if (a == "--warm") warm = static_cast<std::uint32_t>(std::stoul(next()));
        else if (a == "--shot") shot = std::stoi(next());
        else if (a == "--png") png = next();
        else if (a == "--start") startTime = std::stof(next());
        else { std::fprintf(stderr, "unknown arg %s\n", a.c_str()); return 2; }
    }
    if (rep != "compact" && rep != "expanded") { std::fprintf(stderr, "--rep compact|expanded\n"); return 2; }
    const bool compact = rep == "compact";
    const std::uint64_t footprint0 = footprintBytes();

    MeadowDesc desc = makeDesc(size);
    std::array<MeshCpu, kLayers> meshes = {makeBlade(), makePrism(5, 0.02f, 0.006f, {0.4f, 0.5f, 0.35f, 0.0f}), makeMushroom(),
                                           makePrism(6, 0.12f, 0.08f, {0.35f, 0.3f, 0.6f, 0.0f})};
    for (int L = 0; L < kLayers; ++L) desc.layers[L].meshRadius = meshes[L].radius;

    // Camera: inside the world, at eye height, looking across it (shot 0); or high and wide (shot 1).
    // Placed relative to the world's centre so every size sees the same neighbourhood.
    const glm::vec2 camXZ(-0.15f * std::min(size, 400.0f), 0.0f);
    glm::vec3 eye, target;
    if (shot == 0) {
        eye = glm::vec3(camXZ.x, terrainH(camXZ, desc) + 1.7f, camXZ.y);
        const glm::vec2 t2 = camXZ + glm::vec2(40.0f, 12.0f);
        target = glm::vec3(t2.x, terrainH(t2, desc) + 0.5f, t2.y);
    } else {
        eye = glm::vec3(camXZ.x, terrainH(camXZ, desc) + 60.0f, camXZ.y);
        const glm::vec2 t2 = camXZ + glm::vec2(200.0f, 60.0f);
        target = glm::vec3(t2.x, terrainH(t2, desc), t2.y);
    }
    const glm::mat4 view = glm::lookAt(eye, target, glm::vec3(0, 1, 0));
    const glm::mat4 proj = glm::perspectiveZO(glm::radians(60.0f), static_cast<float>(width) / height, 0.1f, 3000.0f);
    DrawU du{proj * view, glm::vec4(glm::normalize(glm::vec3(0.4f, 0.8f, 0.3f)), 0.0f)};

    // Per-layer camera windows (compact) -- fixed size, so the work is the same wherever the camera is.
    FrameU fu{};
    framePlanes(du.viewProj, fu.planes);
    std::uint32_t totalThreads = 0, cap = 0;
    for (int L = 0; L < kLayers; ++L) {
        const LayerDesc& P = desc.layers[L];
        const int dim = static_cast<int>(std::ceil(2.0f * P.viewDistance / P.cell)) + 2;
        fu.win[L] = glm::ivec4(static_cast<int>(std::floor((eye.x - P.viewDistance) / P.cell)) - 1,
                               static_cast<int>(std::floor((eye.z - P.viewDistance) / P.cell)) - 1, dim, static_cast<int>(totalThreads));
        totalThreads += static_cast<std::uint32_t>(dim * dim);
        cap = std::max<std::uint32_t>(cap, static_cast<std::uint32_t>(dim * dim));
    }
    cap = (cap + 15u) & ~15u; // 16 * 48 B = 768 B: each layer's slice starts 256-aligned

    // ---- expanded: the flatten. Expand the description over the whole bounded world on the CPU. ----
    std::vector<Inst> records;
    std::array<std::uint32_t, kLayers + 1> firstRecord{};
    double expandMs = 0.0;
    if (!compact) {
        const auto e0 = Clock::now();
        for (int L = 0; L < kLayers; ++L) {
            firstRecord[L] = static_cast<std::uint32_t>(records.size());
            const float c = desc.layers[L].cell;
            const int lo = static_cast<int>(std::floor(-0.5f * size / c)) - 1, hi = static_cast<int>(std::ceil(0.5f * size / c)) + 1;
            for (int cz = lo; cz <= hi; ++cz)
                for (int cx = lo; cx <= hi; ++cx) {
                    Inst o;
                    if (generate(desc, L, cx, cz, o)) records.push_back(o);
                }
        }
        firstRecord[kLayers] = static_cast<std::uint32_t>(records.size());
        expandMs = msSince(e0);
        fu.ranges = glm::uvec4(firstRecord[1], firstRecord[2], firstRecord[3], 0);
    }
    const std::uint64_t recordCount = records.size();
    fu.counts = glm::uvec4(totalThreads, cap, static_cast<std::uint32_t>(recordCount), 0);
    const std::uint32_t dispatchThreads = compact ? totalThreads : static_cast<std::uint32_t>(recordCount);

    // ---- GPU ----
    auto ctxR = gpu::Context::create(gpu::ContextDesc{});
    if (!ctxR) { std::fprintf(stderr, "context: %s\n", ctxR.error().message.c_str()); return 3; }
    gpu::Context& ctx = **ctxR;
    const wgpu::Device& dev = ctx.device();
    const wgpu::Queue& queue = ctx.queue();
    gpu::FrameTimeline timeline(ctx);
    const auto maxBinding = ctx.capabilities().limits.maxStorageBufferBindingSize;
    if (recordCount * sizeof(Inst) > maxBinding) {
        std::printf("{\"rep\":\"%s\",\"size\":%.0f,\"records\":%llu,\"record_bytes\":%llu,\"expand_ms\":%.1f,\"error\":\"records exceed maxStorageBufferBindingSize (%llu MB)\"}\n",
                    rep.c_str(), size, static_cast<unsigned long long>(recordCount), static_cast<unsigned long long>(recordCount * sizeof(Inst)), expandMs,
                    static_cast<unsigned long long>(maxBinding >> 20));
        return 6;
    }

    std::uint64_t gpuBytes = 0;
    auto track = [&](std::uint64_t b) { gpuBytes += (b + 255) & ~255ull; return b; };
    std::array<wgpu::Buffer, kLayers> vb, ib;
    for (int L = 0; L < kLayers; ++L) {
        vb[L] = makeBuffer(ctx, track(meshes[L].v.size() * sizeof(Vtx)), wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst, "vb");
        queue.WriteBuffer(vb[L], 0, meshes[L].v.data(), meshes[L].v.size() * sizeof(Vtx));
        ib[L] = makeBuffer(ctx, track(meshes[L].i.size() * 4), wgpu::BufferUsage::Index | wgpu::BufferUsage::CopyDst, "ib");
        queue.WriteBuffer(ib[L], 0, meshes[L].i.data(), meshes[L].i.size() * 4);
    }
    WorldU wu{};
    std::uint32_t seedBits = desc.seed;
    std::memcpy(&wu.hdr.x, &seedBits, 4);
    wu.hdr.y = desc.size; wu.hdr.z = desc.terrainAmp; wu.hdr.w = desc.terrainFreq;
    for (int L = 0; L < kLayers; ++L) {
        const LayerDesc& P = desc.layers[L];
        wu.layers[2 * L] = {P.cell, P.presence, P.clusterScale, P.clustering};
        wu.layers[2 * L + 1] = {P.sizeMin, P.sizeMax, P.viewDistance, P.meshRadius};
    }
    wgpu::Buffer worldBuf = makeBuffer(ctx, track(sizeof(WorldU)), wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst, "world");
    queue.WriteBuffer(worldBuf, 0, &wu, sizeof(wu));
    wgpu::Buffer frameBuf = makeBuffer(ctx, track(sizeof(FrameU)), wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst, "frame");
    const std::uint64_t outBytes = static_cast<std::uint64_t>(cap) * kLayers * sizeof(Inst);
    wgpu::Buffer outBuf = makeBuffer(ctx, track(outBytes), wgpu::BufferUsage::Storage, "out");
    wgpu::Buffer argsBuf = makeBuffer(ctx, track(80), wgpu::BufferUsage::Storage | wgpu::BufferUsage::Indirect | wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::CopySrc, "args");
    const std::uint64_t recBytes = std::max<std::uint64_t>(recordCount * sizeof(Inst), 48);
    wgpu::Buffer recBuf = makeBuffer(ctx, track(compact ? 48 : recBytes), wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst, "records");
    const auto u0 = Clock::now();
    if (!compact && recordCount) queue.WriteBuffer(recBuf, 0, records.data(), recordCount * sizeof(Inst));
    const double recordUploadMs = msSince(u0);
    wgpu::Buffer drawBuf = makeBuffer(ctx, track(sizeof(DrawU)), wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst, "draw");
    queue.WriteBuffer(drawBuf, 0, &du, sizeof(du));

    wgpu::TextureDescriptor td{};
    td.size = {width, height, 1};
    td.format = wgpu::TextureFormat::RGBA8Unorm;
    td.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc;
    wgpu::Texture color = dev.CreateTexture(&td);
    td.format = wgpu::TextureFormat::Depth32Float;
    td.usage = wgpu::TextureUsage::RenderAttachment;
    wgpu::Texture depth = dev.CreateTexture(&td);
    wgpu::TextureView colorView = color.CreateView(), depthView = depth.CreateView();

    const std::string common = kCommonWgsl;
    wgpu::ShaderModule genMod = compile(ctx, common + kGenWgsl, "gen");
    wgpu::ComputePipelineDescriptor cpd{};
    cpd.compute.module = genMod;
    cpd.compute.entryPoint = compact ? "cs_compact" : "cs_expanded";
    wgpu::ComputePipeline compPipe = dev.CreateComputePipeline(&cpd);
    std::vector<wgpu::BindGroupEntry> ce(compact ? 4 : 5);
    ce[0].binding = 0; ce[0].buffer = worldBuf; ce[0].size = sizeof(WorldU);
    ce[1].binding = 1; ce[1].buffer = frameBuf; ce[1].size = sizeof(FrameU);
    ce[2].binding = 2; ce[2].buffer = outBuf; ce[2].size = outBytes;
    ce[3].binding = 3; ce[3].buffer = argsBuf; ce[3].size = 80;
    if (!compact) { ce[4].binding = 4; ce[4].buffer = recBuf; ce[4].size = recBytes; }
    wgpu::BindGroupDescriptor cbd{};
    cbd.layout = compPipe.GetBindGroupLayout(0);
    cbd.entryCount = ce.size();
    cbd.entries = ce.data();
    wgpu::BindGroup compBg = dev.CreateBindGroup(&cbd);
    const std::uint32_t groups = std::max<std::uint32_t>(1, (dispatchThreads + 63) / 64);
    const std::uint32_t wgX = std::min<std::uint32_t>(groups, 32768), wgY = (groups + wgX - 1) / wgX;

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
    // group 0 for the draw: World + Frame only (auto layout keeps just what the module uses)
    std::array<wgpu::BindGroupEntry, 1> d0{};
    d0[0].binding = 1; d0[0].buffer = frameBuf; d0[0].size = sizeof(FrameU);
    wgpu::BindGroupDescriptor d0d{};
    d0d.layout = drawPipe.GetBindGroupLayout(0);
    d0d.entryCount = d0.size();
    d0d.entries = d0.data();
    wgpu::BindGroup drawBg0 = dev.CreateBindGroup(&d0d);
    std::array<wgpu::BindGroup, kLayers> drawBg1;
    for (int L = 0; L < kLayers; ++L) {
        std::array<wgpu::BindGroupEntry, 2> e{};
        e[0].binding = 0; e[0].buffer = outBuf; e[0].offset = static_cast<std::uint64_t>(L) * cap * sizeof(Inst); e[0].size = static_cast<std::uint64_t>(cap) * sizeof(Inst);
        e[1].binding = 1; e[1].buffer = drawBuf; e[1].size = sizeof(DrawU);
        wgpu::BindGroupDescriptor d{};
        d.layout = drawPipe.GetBindGroupLayout(1);
        d.entryCount = e.size();
        d.entries = e.data();
        drawBg1[L] = dev.CreateBindGroup(&d);
    }
    ctx.waitForQueue();
    if (ctx.errorCount() != 0) { std::fprintf(stderr, "setup errors: %s\n", ctx.lastError().c_str()); return 4; }
    const std::uint64_t footprintReady = footprintBytes();

    // ---- loop ----
    std::array<std::uint32_t, 20> argsInit{};
    for (int L = 0; L < kLayers; ++L) argsInit[L * 5] = static_cast<std::uint32_t>(meshes[L].i.size());
    Stat sInterval, sCpu, sGpuFrame, sGpuCompute, sGpuDraw;
    std::array<wgpu::Future, 2> inflight{};
    std::array<bool, 2> inflightValid{false, false};
    std::uint64_t lastCompleted = 0;
    Clock::time_point prev{};
    for (std::uint32_t f = 0; f < warm + frames; ++f) {
        const bool measuring = f >= warm;
        const auto start = Clock::now();
        if (measuring && f > warm) sInterval.add(std::chrono::duration<double, std::milli>(start - prev).count());
        prev = start;
        const int slot = static_cast<int>(f % 2);
        if (inflightValid[slot]) (void)ctx.waitFor(inflight[slot]);
        const auto c0 = Clock::now();
        fu.cam = glm::vec4(eye, startTime + static_cast<float>(f) / 60.0f);
        queue.WriteBuffer(frameBuf, 0, &fu, sizeof(fu));
        queue.WriteBuffer(argsBuf, 0, argsInit.data(), sizeof(argsInit));
        timeline.beginFrame();
        wgpu::CommandEncoder enc = dev.CreateCommandEncoder();
        {
            wgpu::ComputePassDescriptor cd{};
            cd.timestampWrites = timeline.mark("population", gpu::FrameTimeline::PassKind::Compute);
            auto cp = enc.BeginComputePass(&cd);
            cp.SetPipeline(compPipe);
            cp.SetBindGroup(0, compBg);
            if (dispatchThreads) cp.DispatchWorkgroups(wgX, wgY, 1);
            cp.End();
        }
        wgpu::RenderPassColorAttachment ca{};
        ca.view = colorView; ca.loadOp = wgpu::LoadOp::Clear; ca.storeOp = wgpu::StoreOp::Store; ca.clearValue = {0.05, 0.06, 0.09, 1.0};
        wgpu::RenderPassDepthStencilAttachment da{};
        da.view = depthView; da.depthLoadOp = wgpu::LoadOp::Clear; da.depthStoreOp = wgpu::StoreOp::Store; da.depthClearValue = 1.0f;
        wgpu::RenderPassDescriptor rp{};
        rp.colorAttachmentCount = 1; rp.colorAttachments = &ca; rp.depthStencilAttachment = &da;
        rp.timestampWrites = timeline.mark("draw", gpu::FrameTimeline::PassKind::Render);
        auto r = enc.BeginRenderPass(&rp);
        r.SetPipeline(drawPipe);
        r.SetBindGroup(0, drawBg0);
        for (int L = 0; L < kLayers; ++L) {
            r.SetBindGroup(1, drawBg1[L]);
            r.SetVertexBuffer(0, vb[L]);
            r.SetIndexBuffer(ib[L], wgpu::IndexFormat::Uint32);
            r.DrawIndexedIndirect(argsBuf, static_cast<std::uint64_t>(L) * 20);
        }
        r.End();
        timeline.resolve(enc);
        wgpu::CommandBuffer cb = enc.Finish();
        queue.Submit(1, &cb);
        inflight[slot] = queue.OnSubmittedWorkDone(wgpu::CallbackMode::WaitAnyOnly, [](wgpu::QueueWorkDoneStatus, wgpu::StringView) {});
        inflightValid[slot] = true;
        timeline.collect();
        const double cpuMs = msSince(c0);
        if (measuring) {
            sCpu.add(cpuMs);
            if (timeline.completedFrames() != lastCompleted) {
                lastCompleted = timeline.completedFrames();
                sGpuFrame.add(timeline.frameMs());
                sGpuCompute.add(timeline.msFor("population"));
                sGpuDraw.add(timeline.msFor("draw"));
            }
        }
    }
    ctx.waitForQueue();
    const std::uint64_t footprintEnd = footprintBytes();

    std::array<std::uint32_t, 20> argsOut{};
    {
        wgpu::Buffer rb = makeBuffer(ctx, 80, wgpu::BufferUsage::MapRead | wgpu::BufferUsage::CopyDst, "rb");
        wgpu::CommandEncoder enc = dev.CreateCommandEncoder();
        enc.CopyBufferToBuffer(argsBuf, 0, rb, 0, 80);
        wgpu::CommandBuffer cb = enc.Finish();
        queue.Submit(1, &cb);
        auto fut = rb.MapAsync(wgpu::MapMode::Read, 0, 80, wgpu::CallbackMode::WaitAnyOnly, [](wgpu::MapAsyncStatus, wgpu::StringView) {});
        (void)ctx.waitFor(fut);
        std::memcpy(argsOut.data(), rb.GetConstMappedRange(0, 80), 80);
    }
    if (!png.empty()) {
        auto img = gpu::readTexture8(ctx, color, width, height, false);
        if (img) (void)assets::writePng(png, width, height, img->rgba);
    }
    if (ctx.errorCount() != 0) { std::fprintf(stderr, "gpu errors: %s\n", ctx.lastError().c_str()); return 5; }

    auto j = [](const char* name, const Stat& s) {
        std::printf("\"%s\":{\"p10\":%.4f,\"p50\":%.4f,\"p90\":%.4f,\"n\":%zu},", name, s.pct(0.1), s.pct(0.5), s.pct(0.9), s.v.size());
    };
    std::uint64_t visible = 0;
    std::printf("{\"rep\":\"%s\",\"size\":%.0f,\"shot\":%d,\"frames\":%u,", rep.c_str(), size, shot, frames);
    j("interval_ms", sInterval); j("cpu_ms", sCpu); j("gpu_frame_ms", sGpuFrame); j("gpu_compute_ms", sGpuCompute); j("gpu_draw_ms", sGpuDraw);
    std::printf("\"visible\":[");
    for (int L = 0; L < kLayers; ++L) { std::printf("%s%u", L ? "," : "", argsOut[L * 5 + 1]); visible += argsOut[L * 5 + 1]; }
    std::printf("],\"visible_total\":%llu,\"desc_bytes\":%zu,\"records\":%llu,\"records_per_layer\":[%u,%u,%u,%u],\"cpu_record_bytes\":%llu,"
                "\"expand_ms\":%.2f,\"record_upload_ms\":%.2f,\"gpu_buffer_bytes\":%llu,\"threads_per_frame\":%u,\"cap_per_layer\":%u,"
                "\"footprint_start_mb\":%.1f,\"footprint_ready_mb\":%.1f,\"footprint_end_mb\":%.1f,\"draw_calls\":%d,\"dispatches\":1}\n",
                static_cast<unsigned long long>(visible), sizeof(MeadowDesc), static_cast<unsigned long long>(recordCount),
                firstRecord[1] - firstRecord[0], firstRecord[2] - firstRecord[1], firstRecord[3] - firstRecord[2], firstRecord[4] - firstRecord[3],
                static_cast<unsigned long long>(recordCount * sizeof(Inst)), expandMs, recordUploadMs, static_cast<unsigned long long>(gpuBytes),
                dispatchThreads, cap, footprint0 / 1048576.0, footprintReady / 1048576.0, footprintEnd / 1048576.0, kLayers);
    return 0;
}
