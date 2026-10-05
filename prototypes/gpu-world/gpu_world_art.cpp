// GPU World research spike, Phase 3 "New-Art Experiment" -- DISPOSABLE PROTOTYPE.
// (docs/research/gpu-world-architecture-spike.md, section "Phase 3".)
//
// Three experimental visual systems on one small, hard-coded GPU renderer (HDR, 4x MSAA, fog, sky,
// bloom, ACES). All audio is REAL: the track is analysed once with production's AnalysisTrack
// (art_audio.cpp) and handed to the GPU as a whole-song spectrogram plus per-frame onset lists.
//
//   --system field     A. Echo Field: N reeds; each reads the spectrogram at ITS OWN DELAY (distance
//                         from the source / wave speed), in its own band. The field is the song's last
//                         ~12 s laid out in space. Stateless: positions are a pure function of the index.
//   --system world     B. Endless Meadow: an unbounded landscape generated around the camera every
//                         frame from hashed cells (four layers, no stored instances at all). Kick
//                         "pings" ripple out from where the camera WAS when each kick landed.
//   --system organism  C. Mycelium: a stateful physarum organism (2-4M agents, a trail field, three
//                         species), driven by audio, neighbours (via the field), a procedural flow,
//                         previous-frame state and the camera. Fixed 60 Hz steps; seek = replay or
//                         checkpoint+replay. --seektest measures determinism and seek.
//
// Not production code. Not linked into anything. Built only with -DAVGEN_GPUWORLD_PROTOTYPE=ON.

#include "art_audio.hpp"

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
#include <filesystem>
#include <functional>
#include <memory>
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

// ---- the frame block every shader sees (layout mirrored in kCommonWgsl) --------------------------

struct FrameU {
    glm::mat4 viewProj;
    glm::mat4 invViewProj;
    glm::vec4 camPos;  // xyz eye, w time
    glm::vec4 camFwd;  // xyz forward, w pixel angle (2 tan(fov/2) / height)
    glm::vec4 planes[6];
    glm::vec4 audio0;  // bass, lowMid, mid, high
    glm::vec4 audio1;  // air, energy, beatPhase, hopF
    glm::vec4 audio2;  // rms, kickEnv, snareEnv, hatEnv
    glm::vec4 kickT[2];
    glm::vec4 kickS[2];
    glm::vec4 kickP[8];
    glm::vec4 snareT;
    glm::vec4 snareS;
    glm::vec4 misc;    // hopRate, hops, t0 (time of spectrogram row 0), sim step
    glm::vec4 sys[8];  // system-specific
    glm::vec4 fog;     // rgb, density
    glm::vec4 skyZenith;  // rgb, stars
    glm::vec4 skyHorizon; // rgb, horizon glow
    glm::vec4 sun;        // dir, intensity
    glm::vec4 sunColor;   // rgb, ambient
    glm::vec4 groundTint; // rgb, rim
};

struct Inst { // 64 B, written by the compute passes
    glm::vec4 posScale, quat, color, shape;
};

struct Vtx {
    float pos[3];
    float nrm[3];
    float col[4]; // albedo rgb, a = emissive mask
};

struct MeshCpu {
    std::vector<Vtx> v;
    std::vector<std::uint32_t> i;
};

// ---- WGSL shared by everything ------------------------------------------------------------------

const char* kCommonWgsl = R"(
struct Frame {
    viewProj: mat4x4f, invViewProj: mat4x4f,
    camPos: vec4f, camFwd: vec4f,
    planes: array<vec4f, 6>,
    audio0: vec4f, audio1: vec4f, audio2: vec4f,
    kickT: array<vec4f, 2>, kickS: array<vec4f, 2>, kickP: array<vec4f, 8>,
    snareT: vec4f, snareS: vec4f,
    misc: vec4f,
    sys: array<vec4f, 8>,
    fog: vec4f, skyZenith: vec4f, skyHorizon: vec4f, sun: vec4f, sunColor: vec4f, groundTint: vec4f,
};
struct Inst { posScale: vec4f, quat: vec4f, color: vec4f, shape: vec4f, };
@group(0) @binding(0) var<uniform> F: Frame;
@group(0) @binding(1) var<storage, read> spec: array<f32>;

const PI: f32 = 3.14159265;
const TAU: f32 = 6.2831853;

fn qmul(a: vec4f, b: vec4f) -> vec4f {
    return vec4f(a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                 a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z);
}
fn qrot(q: vec4f, v: vec3f) -> vec3f { return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v); }
fn qaxis(axis: vec3f, ang: f32) -> vec4f { return vec4f(axis * sin(0.5 * ang), cos(0.5 * ang)); }
// atan2(0, 0) is undefined in WGSL and NaN on Metal: a zero bend made every vertex NaN (invisible).
fn bendAngle(b: vec2f) -> f32 { return select(atan2(b.y, b.x), 0.0, dot(b, b) < 1e-12); }

fn hashu(x0: u32) -> u32 {
    var x = x0;
    x ^= x >> 16u; x *= 0x7feb352du; x ^= x >> 15u; x *= 0x846ca68bu; x ^= x >> 16u;
    return x;
}
fn hash2i(c: vec2i, salt: u32) -> u32 {
    return hashu((bitcast<u32>(c.x) * 0x8da6b343u) ^ hashu((bitcast<u32>(c.y) * 0xd8163841u) ^ (salt * 0xcb1ab31fu)));
}
fn u01(h: u32) -> f32 { return f32(h >> 8u) / 16777216.0; }

fn vnoise(p: vec2f) -> f32 {
    let i = vec2i(floor(p));
    let f = fract(p);
    let u = f * f * (3.0 - 2.0 * f);
    let a = u01(hash2i(i, 11u)); let b = u01(hash2i(i + vec2i(1, 0), 11u));
    let c = u01(hash2i(i + vec2i(0, 1), 11u)); let d = u01(hash2i(i + vec2i(1, 1), 11u));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}
fn fbm(p0: vec2f, oct: i32) -> f32 {
    var p = p0; var a = 0.5; var s = 0.0;
    for (var k = 0; k < oct; k++) { s += a * vnoise(p); p = mat2x2f(1.6, 1.2, -1.2, 1.6) * p; a *= 0.5; }
    return s;
}

// The whole song's spectrogram: row = hop, 64 log bins. Reading the past is just a smaller row.
fn specAt(hopF: f32, bin: i32) -> f32 {
    let n = i32(F.misc.y);
    if (hopF < 0.0 || n < 2) { return 0.0; }
    let h0 = min(i32(floor(hopF)), n - 1);
    let h1 = min(h0 + 1, n - 1);
    let b = clamp(bin, 0, 63);
    return mix(spec[h0 * 64 + b], spec[h1 * 64 + b], fract(hopF));
}
fn specAtTime(t: f32, bin: i32) -> f32 { return specAt((t - F.misc.z) * F.misc.x, bin); }
fn kickTime(k: i32) -> f32 { return F.kickT[k / 4][k % 4]; }
fn kickStr(k: i32) -> f32 { return F.kickS[k / 4][k % 4]; }

fn skyColor(dir: vec3f) -> vec3f { return skyColorM(dir, 1.0); }
fn skyColorM(dir: vec3f, moon: f32) -> vec3f {
    let y = dir.y;
    var c = mix(F.skyHorizon.rgb, F.skyZenith.rgb, pow(clamp(y, 0.0, 1.0), 0.45));
    c = mix(c, F.groundTint.rgb * 0.5, clamp(-y * 6.0, 0.0, 1.0));
    // a broad horizon glow toward the "sun" (a low moon) and a halo around it
    let sd = max(dot(dir, F.sun.xyz), 0.0);
    c += F.sunColor.rgb * (pow(sd, 6.0) * 0.06 * F.skyHorizon.w + moon * (pow(sd, 900.0) * 0.35 + pow(sd, 5000.0) * 9.0));
    // stars
    if (y > 0.0 && F.skyZenith.w > 0.0 && moon > 0.5) {
        let q = dir / max(abs(dir.x), max(abs(dir.y), abs(dir.z)));
        let g = vec2i(floor((q.xz + q.y * vec2f(0.37, 0.71)) * 700.0));
        let h = hash2i(g, 3u);
        let st = select(0.0, pow(u01(hashu(h)), 30.0), u01(h) > 0.985);
        c += vec3f(0.8, 0.9, 1.0) * st * F.skyZenith.w * smoothstep(0.0, 0.25, y);
    }
    return c;
}
fn applyFog(c: vec3f, wpos: vec3f) -> vec3f {
    let d = wpos - F.camPos.xyz;
    let dist = length(d);
    let dir = d / max(dist, 1e-4);
    // exponential height fog, integrated along the ray: dense at the ground, thin for a high camera
    let k = 0.035;
    let y0 = max(F.camPos.y, 0.0); let y1 = max(wpos.y, 0.0);
    let dy = y1 - y0;
    var avg = exp(-k * y0);
    if (abs(dy) > 0.01) { avg = (exp(-k * y0) - exp(-k * y1)) / (k * dy); }
    let f = 1.0 - exp(-dist * F.fog.w * (0.15 + 0.85 * avg));
    let fc = mix(F.fog.rgb, skyColorM(normalize(vec3f(dir.x, max(dir.y, 0.02), dir.z)), 0.0), 0.5);
    return mix(c, fc, f);
}
)";

// Instanced geometry: one pipeline for every bucket of every system.
const char* kInstDrawWgsl = R"(
@group(0) @binding(2) var<storage, read> aux: array<vec4f>;
@group(1) @binding(0) var<storage, read> insts: array<Inst>;
struct VIn { @location(0) pos: vec3f, @location(1) nrm: vec3f, @location(2) col: vec4f, };
struct VOut { @builtin(position) clip: vec4f, @location(0) n: vec3f, @location(1) col: vec4f,
              @location(2) tint: vec4f, @location(3) wpos: vec3f, };
@vertex fn vs_inst(v: VIn, @builtin(instance_index) ii: u32) -> VOut {
    let I = insts[ii];
    let s = I.posScale.w;
    var lp = v.pos;
    lp.x *= I.shape.w; lp.z *= I.shape.w;  // width (thin things are widened to a pixel far away)
    lp.y *= I.shape.x;                      // height
    var p = qrot(I.quat, lp * s);
    let h = clamp(v.pos.y, 0.0, 1.0);
    let L = s * I.shape.x;
    let b = I.shape.y;
    p += vec3f(cos(I.shape.z), 0.0, sin(I.shape.z)) * (b * h * h * L);
    p.y -= 0.4 * b * b * h * h * h * L;
    var o: VOut;
    o.wpos = p + I.posScale.xyz;
    o.clip = F.viewProj * vec4f(o.wpos, 1.0);
    o.n = qrot(I.quat, v.nrm);
    o.col = v.col;
    o.tint = I.color;
    return o;
}
@fragment fn fs_inst(i: VOut) -> @location(0) vec4f {
    var n = normalize(i.n);
    let V = normalize(F.camPos.xyz - i.wpos);
    if (dot(n, V) < 0.0) { n = -n; }
    let albedo = i.col.rgb * mix(vec3f(1.0), i.tint.rgb, 0.3);
    let amb = mix(F.groundTint.rgb, F.skyZenith.rgb * 4.0 + F.skyHorizon.rgb, n.y * 0.5 + 0.5) * F.sunColor.w;
    let dif = max(dot(n, F.sun.xyz), 0.0) * F.sunColor.rgb * F.sun.w;
    let rim = pow(1.0 - max(dot(n, V), 0.0), 3.0) * F.fog.rgb * F.groundTint.w;
    let spec = pow(max(dot(reflect(-V, n), F.sun.xyz), 0.0), 48.0) * F.sunColor.rgb * F.sun.w * 1.5;
    var c = albedo * (amb + dif) + rim + spec + i.col.a * i.tint.rgb * i.tint.a;
    return vec4f(applyFog(c, i.wpos), 1.0);
}
)";

const char* kSkyWgsl = R"(
struct SOut { @builtin(position) clip: vec4f, @location(0) ndc: vec2f, };
@vertex fn vs_sky(@builtin(vertex_index) vi: u32) -> SOut {
    let xy = vec2f(f32((vi << 1u) & 2u), f32(vi & 2u)) * 2.0 - 1.0;
    var o: SOut;
    o.clip = vec4f(xy, 1.0, 1.0);
    o.ndc = xy;
    return o;
}
@fragment fn fs_sky(i: SOut) -> @location(0) vec4f {
    let p = F.invViewProj * vec4f(i.ndc, 1.0, 1.0);
    let dir = normalize(p.xyz / p.w - F.camPos.xyz);
    return vec4f(skyColor(dir), 1.0);
}
)";

// Ground: a grid whose world placement and shading the system provides (groundPos / groundHeight /
// groundShade), so each system can make it camera-relative (infinite) or fixed (a domain).
const char* kGroundWgsl = R"(
@group(0) @binding(2) var<storage, read> aux: array<vec4f>;
struct GOut { @builtin(position) clip: vec4f, @location(0) wpos: vec3f, };
@vertex fn vs_ground(@location(0) uv: vec2f) -> GOut {
    let w = groundPos(uv);
    var o: GOut;
    o.wpos = vec3f(w.x, groundHeight(w), w.y);
    o.clip = F.viewProj * vec4f(o.wpos, 1.0);
    return o;
}
@fragment fn fs_ground(i: GOut) -> @location(0) vec4f {
    let e = 0.05 + 0.002 * length(i.wpos - F.camPos.xyz);
    let hx = groundHeight(i.wpos.xz + vec2f(e, 0.0)) - groundHeight(i.wpos.xz - vec2f(e, 0.0));
    let hz = groundHeight(i.wpos.xz + vec2f(0.0, e)) - groundHeight(i.wpos.xz - vec2f(0.0, e));
    let n = normalize(vec3f(-hx, 2.0 * e, -hz));
    let c = groundShade(i.wpos, n);
    return vec4f(applyFog(c, i.wpos), 1.0);
}
)";

// ---- post: bloom chain + ACES composite --------------------------------------------------------

const char* kPostWgsl = R"(
struct PostU { a: vec4f, b: vec4f, };  // a: exposure, bloom, vignette, grain; b: frame, contrast, saturation, -
@group(0) @binding(0) var src: texture_2d<f32>;
@group(0) @binding(1) var samp: sampler;
@group(0) @binding(2) var<uniform> P: PostU;
@group(0) @binding(3) var bloomTex: texture_2d<f32>;
struct FOut { @builtin(position) clip: vec4f, @location(0) uv: vec2f, };
@vertex fn vs_full(@builtin(vertex_index) vi: u32) -> FOut {
    let xy = vec2f(f32((vi << 1u) & 2u), f32(vi & 2u));
    var o: FOut;
    o.clip = vec4f(xy * 2.0 - 1.0, 0.0, 1.0);
    o.uv = vec2f(xy.x, 1.0 - xy.y);
    return o;
}
fn tap(uv: vec2f) -> vec3f { return textureSampleLevel(src, samp, uv, 0.0).rgb; }
fn karis(c: vec3f) -> vec3f { return c / (1.0 + dot(c, vec3f(0.2126, 0.7152, 0.0722)) * 0.25); }
@fragment fn fs_down(i: FOut) -> @location(0) vec4f {
    let t = 1.0 / vec2f(textureDimensions(src));
    let uv = i.uv;
    let a = tap(uv + t * vec2f(-2, -2)); let b = tap(uv + t * vec2f(0, -2)); let c = tap(uv + t * vec2f(2, -2));
    let d = tap(uv + t * vec2f(-2, 0));  let e = tap(uv);                    let f = tap(uv + t * vec2f(2, 0));
    let g = tap(uv + t * vec2f(-2, 2));  let h = tap(uv + t * vec2f(0, 2));  let k = tap(uv + t * vec2f(2, 2));
    let j = tap(uv + t * vec2f(-1, -1)); let l = tap(uv + t * vec2f(1, -1));
    let m = tap(uv + t * vec2f(-1, 1));  let n = tap(uv + t * vec2f(1, 1));
    var o = e * 0.125 + (a + c + g + k) * 0.03125 + (b + d + f + h) * 0.0625 + (j + l + m + n) * 0.125;
    return vec4f(min(o, vec3f(200.0)), 1.0);
}
@fragment fn fs_down_first(i: FOut) -> @location(0) vec4f {
    // the first level de-fireflies with a Karis-style weight on four 2x2 groups
    let t = 1.0 / vec2f(textureDimensions(src));
    let uv = i.uv;
    let a = karis(tap(uv + t * vec2f(-1, -1))); let b = karis(tap(uv + t * vec2f(1, -1)));
    let c = karis(tap(uv + t * vec2f(-1, 1)));  let d = karis(tap(uv + t * vec2f(1, 1)));
    var o = (a + b + c + d) * 0.25;
    o = o / max(1.0 - dot(o, vec3f(0.2126, 0.7152, 0.0722)) * 0.25, 0.05);
    return vec4f(o, 1.0);
}
@fragment fn fs_up(i: FOut) -> @location(0) vec4f {
    let t = 1.0 / vec2f(textureDimensions(src));
    let uv = i.uv;
    let s = (tap(uv + t * vec2f(-1, -1)) + tap(uv + t * vec2f(1, -1)) + tap(uv + t * vec2f(-1, 1)) + tap(uv + t * vec2f(1, 1))) * 1.0
          + (tap(uv + t * vec2f(0, -1)) + tap(uv + t * vec2f(-1, 0)) + tap(uv + t * vec2f(1, 0)) + tap(uv + t * vec2f(0, 1))) * 2.0
          + tap(uv) * 4.0;
    return vec4f(s / 16.0, 1.0);
}
fn aces(x: vec3f) -> vec3f {
    let a = 2.51; let b = 0.03; let c = 2.43; let d = 0.59; let e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), vec3f(0.0), vec3f(1.0));
}
@fragment fn fs_composite(i: FOut) -> @location(0) vec4f {
    let hdr = textureSampleLevel(src, samp, i.uv, 0.0).rgb;
    let bl = textureSampleLevel(bloomTex, samp, i.uv, 0.0).rgb;
    var c = (hdr + bl * P.a.y) * P.a.x;
    // saturation in linear before the curve
    let l = dot(c, vec3f(0.2126, 0.7152, 0.0722));
    c = mix(vec3f(l), c, P.b.z);
    c = aces(c);
    c = pow(c, vec3f(P.b.y));
    let q = i.uv - 0.5;
    c *= 1.0 - P.a.z * dot(q, q) * 1.6;
    c = pow(max(c, vec3f(0.0)), vec3f(1.0 / 2.2));
    let px = vec2u(i.clip.xy);
    let g = f32(hashu(px.x * 1973u + px.y * 9277u + u32(P.b.x) * 26699u) >> 8u) / 16777216.0 - 0.5;
    c += g * P.a.w;
    return vec4f(c, 1.0);
}
fn hashu(x0: u32) -> u32 {
    var x = x0;
    x ^= x >> 16u; x *= 0x7feb352du; x ^= x >> 15u; x *= 0x846ca68bu; x ^= x >> 16u;
    return x;
}
)";

// ---- GPU helpers --------------------------------------------------------------------------------

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

enum class Bind { Uniform, ReadOnly, Storage, Texture, Sampler };

wgpu::BindGroupLayout makeLayout(gpu::Context& ctx, const std::vector<Bind>& kinds, wgpu::ShaderStage vis) {
    std::vector<wgpu::BindGroupLayoutEntry> e(kinds.size());
    for (std::size_t k = 0; k < kinds.size(); ++k) {
        e[k].binding = static_cast<std::uint32_t>(k);
        e[k].visibility = vis;
        switch (kinds[k]) {
        case Bind::Uniform: e[k].buffer.type = wgpu::BufferBindingType::Uniform; break;
        case Bind::ReadOnly: e[k].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage; break;
        case Bind::Storage: e[k].buffer.type = wgpu::BufferBindingType::Storage; break;
        case Bind::Texture:
            e[k].texture.sampleType = wgpu::TextureSampleType::Float;
            e[k].texture.viewDimension = wgpu::TextureViewDimension::e2D;
            break;
        case Bind::Sampler: e[k].sampler.type = wgpu::SamplerBindingType::Filtering; break;
        }
    }
    wgpu::BindGroupLayoutDescriptor d{};
    d.entryCount = e.size();
    d.entries = e.data();
    return ctx.device().CreateBindGroupLayout(&d);
}

struct BindRes {
    wgpu::Buffer buffer;
    std::uint64_t offset = 0, size = 0;
    wgpu::TextureView view;
    wgpu::Sampler sampler;
};
BindRes buf(const wgpu::Buffer& b, std::uint64_t size, std::uint64_t offset = 0) { BindRes r; r.buffer = b; r.size = size; r.offset = offset; return r; }
BindRes tex(const wgpu::TextureView& v) { BindRes r; r.view = v; return r; }
BindRes smp(const wgpu::Sampler& s) { BindRes r; r.sampler = s; return r; }

wgpu::BindGroup makeGroup(gpu::Context& ctx, const wgpu::BindGroupLayout& layout, const std::vector<BindRes>& res) {
    std::vector<wgpu::BindGroupEntry> e(res.size());
    for (std::size_t k = 0; k < res.size(); ++k) {
        e[k].binding = static_cast<std::uint32_t>(k);
        if (res[k].buffer) { e[k].buffer = res[k].buffer; e[k].offset = res[k].offset; e[k].size = res[k].size; }
        if (res[k].view) e[k].textureView = res[k].view;
        if (res[k].sampler) e[k].sampler = res[k].sampler;
    }
    wgpu::BindGroupDescriptor d{};
    d.layout = layout;
    d.entryCount = e.size();
    d.entries = e.data();
    return ctx.device().CreateBindGroup(&d);
}

wgpu::PipelineLayout makePipelineLayout(gpu::Context& ctx, const std::vector<wgpu::BindGroupLayout>& groups) {
    wgpu::PipelineLayoutDescriptor d{};
    d.bindGroupLayoutCount = groups.size();
    d.bindGroupLayouts = groups.data();
    return ctx.device().CreatePipelineLayout(&d);
}

wgpu::ComputePipeline makeCompute(gpu::Context& ctx, const wgpu::ShaderModule& m, const char* entry, const wgpu::PipelineLayout& pl) {
    wgpu::ComputePipelineDescriptor d{};
    d.layout = pl;
    d.compute.module = m;
    d.compute.entryPoint = entry;
    return ctx.device().CreateComputePipeline(&d);
}

void framePlanes(const glm::mat4& vp, glm::vec4 out[6]) {
    const glm::mat4 m = glm::transpose(vp);
    out[0] = m[3] + m[0]; out[1] = m[3] - m[0]; out[2] = m[3] + m[1];
    out[3] = m[3] - m[1]; out[4] = m[2];        out[5] = m[3] - m[2];
    for (int k = 0; k < 6; ++k) out[k] /= glm::length(glm::vec3(out[k]));
}

// ---- meshes (unit height, y up; col.a is the emissive mask) -------------------------------------

void addTri(MeshCpu& m, glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec4 ca, glm::vec4 cb, glm::vec4 cc, bool flat = true) {
    const glm::vec3 n = glm::normalize(glm::cross(b - a, c - a));
    const glm::vec3 pts[3] = {a, b, c};
    const glm::vec4 cols[3] = {ca, cb, cc};
    for (int k = 0; k < 3; ++k) {
        m.i.push_back(static_cast<std::uint32_t>(m.v.size()));
        glm::vec3 nn = n;
        if (!flat) nn = glm::normalize(glm::vec3(pts[k].x, 0.0f, pts[k].z) + glm::vec3(0, 0.001f, 0));
        m.v.push_back({{pts[k].x, pts[k].y, pts[k].z}, {nn.x, nn.y, nn.z}, {cols[k].r, cols[k].g, cols[k].b, cols[k].a}});
    }
}

// A reed: a tapered stalk and an elongated glowing bud on top. `sides` x `segs` stalk.
MeshCpu makeReed(int sides, int segs, float r0, float r1, float budR, float budLen, glm::vec3 stalkCol) {
    MeshCpu m;
    const float top = 1.0f - budLen;
    auto ring = [&](int k, int s) {
        const float y = top * static_cast<float>(k) / segs;
        const float r = r0 + (r1 - r0) * static_cast<float>(k) / segs;
        const float a = 6.2831853f * static_cast<float>(s) / sides;
        return glm::vec3(r * std::cos(a), y, r * std::sin(a));
    };
    for (int k = 0; k < segs; ++k) {
        const float e0 = 0.035f * std::pow(static_cast<float>(k) / segs, 3.0f);
        const float e1 = 0.035f * std::pow(static_cast<float>(k + 1) / segs, 3.0f);
        const glm::vec4 c0(stalkCol, e0), c1(stalkCol, e1);
        for (int s = 0; s < sides; ++s) {
            const glm::vec3 a = ring(k, s), b = ring(k, s + 1), c = ring(k + 1, s + 1), d = ring(k + 1, s);
            addTri(m, a, c, b, c0, c1, c0, false);
            addTri(m, a, d, c, c0, c1, c1, false);
        }
    }
    // bud: a hexagonal bipyramid, fully emissive
    const glm::vec4 bc(0.10f, 0.10f, 0.10f, 1.0f); // dark: the bud is lit only by the music
    const glm::vec3 lo(0, top - budLen * 0.15f, 0), hi(0, 1.0f, 0);
    for (int s = 0; s < 6; ++s) {
        const float a0 = 6.2831853f * s / 6, a1 = 6.2831853f * (s + 1) / 6;
        const float ym = top + budLen * 0.35f;
        const glm::vec3 p0(budR * std::cos(a0), ym, budR * std::sin(a0)), p1(budR * std::cos(a1), ym, budR * std::sin(a1));
        addTri(m, p0, hi, p1, bc, bc, bc);
        addTri(m, p0, p1, lo, bc, bc, bc);
    }
    return m;
}

MeshCpu makeBlade(glm::vec3 col) { // a curved tapered blade, 4 segments, double-sided by the shader
    MeshCpu m;
    const int segs = 4;
    for (int k = 0; k < segs; ++k) {
        const float y0 = static_cast<float>(k) / segs, y1 = static_cast<float>(k + 1) / segs;
        const float w0 = 0.05f * (1.0f - y0), w1 = 0.05f * (1.0f - y1);
        const float z0 = 0.18f * y0 * y0, z1 = 0.18f * y1 * y1;
        const glm::vec4 c0(col, 0.25f * y0 * y0 * y0), c1(col, 0.25f * y1 * y1 * y1);
        const glm::vec3 a(-w0, y0, z0), b(w0, y0, z0), c(w1, y1, z1), d(-w1, y1, z1);
        addTri(m, a, b, c, c0, c0, c1);
        addTri(m, a, c, d, c0, c1, c1);
    }
    return m;
}

MeshCpu makeCrystal(int sides, float r, float shoulder, glm::vec3 col, float emis) { // a long faceted spire
    // The body is split into rings so the light can gather toward the top (mask ~ h^3), and
    // alternate facets carry less of it, so a flare reads as faceted glass rather than a lit slab.
    MeshCpu m;
    const int segs = 5;
    auto ringPt = [&](int k, float a) {
        const float y = shoulder * static_cast<float>(k) / segs;
        const float rr = r * (1.0f - 0.2f * static_cast<float>(k) / segs);
        return glm::vec3(rr * std::cos(a), y, rr * std::sin(a));
    };
    auto mask = [&](int k, int s) {
        const float h = static_cast<float>(k) / segs;
        return emis * (0.45f * h * h * h) * ((s & 1) ? 0.55f : 1.0f);
    };
    const glm::vec3 hi(0, 1, 0), lo(0, 0, 0);
    for (int s = 0; s < sides; ++s) {
        const float a0 = 6.2831853f * s / sides, a1 = 6.2831853f * (s + 1) / sides;
        for (int k = 0; k < segs; ++k) {
            const glm::vec3 b0 = ringPt(k, a0), b1 = ringPt(k, a1), t0 = ringPt(k + 1, a0), t1 = ringPt(k + 1, a1);
            const glm::vec4 cb(col, mask(k, s)), ct(col, mask(k + 1, s));
            addTri(m, b0, t1, b1, cb, ct, cb);
            addTri(m, b0, t0, t1, cb, ct, ct);
        }
        const glm::vec3 t0 = ringPt(segs, a0), t1 = ringPt(segs, a1);
        const glm::vec4 ct(col, mask(segs, s)), ch(col, emis * ((s & 1) ? 0.7f : 1.0f));
        addTri(m, t0, hi, t1, ct, ch, ct);
        addTri(m, ringPt(0, a0), ringPt(0, a1), lo, glm::vec4(col, 0.0f), glm::vec4(col, 0.0f), glm::vec4(col, 0.0f));
    }
    return m;
}

MeshCpu makeOrb(glm::vec3 col) { // icosahedron centred at y = 0.5
    MeshCpu m;
    const float t = (1.0f + std::sqrt(5.0f)) / 2.0f;
    std::vector<glm::vec3> p = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t},
                                {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
    for (auto& q : p) q = glm::normalize(q) * 0.5f + glm::vec3(0, 0.5f, 0);
    const int f[20][3] = {{0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11}, {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
                          {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9}, {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1}};
    const glm::vec4 c(col, 1.0f);
    for (auto& tri : f) addTri(m, p[tri[0]], p[tri[1]], p[tri[2]], c, c, c);
    return m;
}

// The production mushroom generator (organism::buildMushroom), normalised to unit height. Parameters
// not named in `pick` sit at the schema midpoint.
MeshCpu makeMushroom(const std::vector<std::pair<std::string, float>>& pick = {}) {
    const auto& specs = organism::mushroomSchema().parameters.specs();
    search::Parameters values;
    for (const auto& s : specs) {
        float v = 0.5f * (s.min + s.max);
        for (const auto& [name, value] : pick) if (name == s.name) v = value;
        values.push_back(s.integral ? std::round(v) : v);
    }
    auto subject = organism::buildMushroom(values);
    MeshCpu m;
    if (!subject) { std::fprintf(stderr, "mushroom: %s\n", subject.error().message.c_str()); std::exit(2); }
    glm::vec3 lo(1e9f), hi(-1e9f);
    for (const auto& part : subject->parts)
        for (const auto& v : part.mesh.vertices) { lo = glm::min(lo, v.position); hi = glm::max(hi, v.position); }
    const float height = std::max(hi.y - lo.y, 1e-3f);
    const glm::vec3 centre((lo.x + hi.x) * 0.5f, lo.y, (lo.z + hi.z) * 0.5f);
    for (const auto& part : subject->parts) {
        const auto first = static_cast<std::uint32_t>(m.v.size());
        const float glow = std::min(part.emissiveIntensity, 1.0f);
        const glm::vec3 col = glm::mix(part.baseColor, glm::vec3(0.5f, 0.3f, 0.25f), 0.5f);
        for (const auto& v : part.mesh.vertices) {
            const glm::vec3 p = (v.position - centre) / height;
            m.v.push_back({{p.x, p.y, p.z}, {v.normal.x, v.normal.y, v.normal.z}, {col.x, col.y, col.z, glow}});
        }
        for (auto i : part.mesh.indices) m.i.push_back(first + i);
    }
    return m;
}

struct MeshGpu {
    wgpu::Buffer vb, ib;
    std::uint32_t indexCount = 0;
};
MeshGpu upload(gpu::Context& ctx, const MeshCpu& m) {
    MeshGpu g;
    g.vb = makeBuffer(ctx, m.v.size() * sizeof(Vtx), wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst, "vb");
    ctx.queue().WriteBuffer(g.vb, 0, m.v.data(), m.v.size() * sizeof(Vtx));
    g.ib = makeBuffer(ctx, m.i.size() * 4, wgpu::BufferUsage::Index | wgpu::BufferUsage::CopyDst, "ib");
    ctx.queue().WriteBuffer(g.ib, 0, m.i.data(), m.i.size() * 4);
    g.indexCount = static_cast<std::uint32_t>(m.i.size());
    return g;
}

// ---- the camera ---------------------------------------------------------------------------------

struct Cam {
    glm::vec3 eye{0, 5, 20}, target{0, 0, 0};
    float fovDeg = 50.0f;
};

float smooth01(float x) { x = std::clamp(x, 0.0f, 1.0f); return x * x * (3.0f - 2.0f * x); }

// ---- the systems --------------------------------------------------------------------------------
//
// A system owns its WGSL (compute + groundPos/groundHeight/groundShade), its buffers and its compute
// passes, and declares its draw buckets: (mesh, capacity). Bucket b's instances live at
// [b * cap, (b + 1) * cap) of one output buffer, and its indirect args at args[b * 5 .. b * 5 + 4].

struct Bucket {
    int mesh = 0;
};

struct Look { // the per-system grade and atmosphere
    glm::vec4 fog, skyZenith, skyHorizon, sun, sunColor, groundTint;
    float exposure = 1.0f, bloom = 0.06f, vignette = 0.35f, grain = 0.012f, contrast = 1.0f, saturation = 1.0f;
};

struct System {
    virtual ~System() = default;
    virtual std::string wgsl() const = 0;            // compute kernels + ground functions
    virtual std::vector<MeshCpu> meshes() const = 0;
    virtual std::vector<Bucket> buckets() const = 0;
    virtual std::uint32_t capacity() const = 0;      // per bucket
    virtual Cam camera(double t, int shot) const = 0;
    virtual Look look() const = 0;
    virtual void init(gpu::Context&, const wgpu::ShaderModule&, const wgpu::BindGroupLayout& frameLayout, const wgpu::Buffer& out,
                      const wgpu::Buffer& args) = 0;
    virtual void setParams(FrameU& f, double t) = 0;   // fills f.sys, kick origins, ...
    virtual void compute(wgpu::CommandEncoder& enc, const wgpu::BindGroup& frameGroup, gpu::FrameTimeline& tl) = 0;
    virtual wgpu::Buffer auxBuffer() const { return {}; } // read by the draw/ground shaders as `aux`
    virtual std::uint64_t auxBytes() const { return 0; }
    virtual std::uint64_t gpuStateBytes() const { return 0; }
    virtual std::string extraJson() const { return ""; }
    virtual void beginFrame(double /*t*/) {}
};

// ================================================================================================
// A. ECHO FIELD
// ================================================================================================
//
// N reeds on a disc (a sunflower spiral, so position is a pure function of the index: no base buffer
// at all). Each reed:
//   - listens to its own spectral bin (angle around the source -> frequency, mirrored, +- jitter);
//   - hears the song LATE by (distance from the source) / waveSpeed, so the field holds the last
//     R / waveSpeed seconds of the song as rings: the present at the centre, the past at the rim;
//   - is struck by each of the last 8 kicks when its front arrives (white flash, leans outward);
//   - sways with the mids.
// Its height and bud brightness are its band's energy at its moment.

const char* kFieldWgsl = R"(
@group(1) @binding(0) var<storage, read_write> outInst: array<Inst>;
@group(1) @binding(1) var<storage, read_write> args: array<atomic<u32>, 20>;
// sys[0]: count, waveSpeed, radius, cap        sys[1]: nearDist, maxDist, minPx, kickDecay
// sys[2]: binJitter, heightGain, emisGain, sway  sys[3]: inner radius, freqTurns, -, -
var<workgroup> wgCount: array<atomic<u32>, 4>;
var<workgroup> wgBase: array<u32, 4>;

fn bandColor(f: f32) -> vec3f {
    // lows burn ember, the middle is a pale gold-white, highs are ice, the air is a cold violet
    let c0 = vec3f(1.0, 0.16, 0.03); let c1 = vec3f(1.0, 0.48, 0.10); let c2 = vec3f(1.0, 0.74, 0.36);
    let c3 = vec3f(0.36, 0.72, 1.0); let c4 = vec3f(0.48, 0.36, 1.0);
    let x = clamp(f, 0.0, 1.0) * 4.0;
    if (x < 1.0) { return mix(c0, c1, x); }
    if (x < 2.0) { return mix(c1, c2, x - 1.0); }
    if (x < 3.0) { return mix(c2, c3, x - 2.0); }
    return mix(c3, c4, x - 3.0);
}

@compute @workgroup_size(64)
fn cs_field(@builtin(global_invocation_id) gid: vec3u, @builtin(local_invocation_index) li: u32,
            @builtin(num_workgroups) nwg: vec3u) {
    if (li < 4u) { atomicStore(&wgCount[li], 0u); }
    workgroupBarrier();
    let i = gid.x + gid.y * nwg.x * 64u;
    let N = u32(F.sys[0].x);
    let cap = u32(F.sys[0].w);
    var bucket = -1;
    var slot = 0u;
    var o: Inst;
    if (i < N) {
        let t = F.camPos.w;
        let R = F.sys[0].z; let r0 = F.sys[3].x;
        let h0 = hashu(i * 2654435761u + 17u);
        let rnd = vec4f(u01(h0), u01(hashu(h0 + 1u)), u01(hashu(h0 + 2u)), u01(hashu(h0 + 3u)));
        // sunflower spiral with a little jitter
        let fi = f32(i) + 0.5;
        let rs = sqrt(r0 * r0 + (R * R - r0 * r0) * fi / f32(N));
        let as_ = f32((i * 1640531527u) >> 8u) / 16777216.0 * TAU; // golden angle, exact in fixed point
        // jitter in a disc about one spacing wide, so the spiral's parastichies never read as rows
        let spacing = sqrt(PI * R * R / f32(N));
        let ja = u01(hashu(h0 + 7u)) * TAU;
        let jr = sqrt(u01(hashu(h0 + 9u))) * spacing * 0.9;
        let pxz = vec2f(rs * cos(as_), rs * sin(as_)) + jr * vec2f(cos(ja), sin(ja));
        let pos = vec3f(pxz.x, 0.0, pxz.y);
        let r = length(pxz);
        let ang = atan2(pxz.y, pxz.x);
        // the spectrum wraps around the source `freqTurns` times, mirrored, so lows and highs alternate
        let turns = F.sys[3].y;
        let a01 = fract(ang / TAU * turns);
        let f01 = 1.0 - abs(a01 * 2.0 - 1.0);
        let bin = i32(clamp(f01 * 63.0 + (rnd.z - 0.5) * F.sys[2].x, 0.0, 63.0));
        let delay = r / F.sys[0].y;
        var e = specAtTime(t - delay, bin);
        e = smoothstep(0.08, 0.9, e);
        // the last eight kicks, each a front leaving the source at its own moment
        var imp = 0.0;
        for (var k = 0; k < 8; k++) {
            let since = t - kickTime(k) - delay;
            if (since >= 0.0) { imp += kickStr(k) * exp(-F.sys[1].w * since) * (1.0 - exp(-40.0 * since)); }
        }
        let s = 0.55 + 0.75 * rnd.w;
        let mids = F.audio0.z;
        let hy = 0.35 + F.sys[2].y * e + 0.45 * imp;
        let emis = (0.03 + F.sys[2].z * e * e) * (0.6 + 0.8 * rnd.x) + 1.6 * imp * imp;
        let ph = rnd.y * TAU;
        let sw = F.sys[2].w * (0.4 + 1.6 * mids) * vec2f(sin(1.1 * t + ph + r * 0.15), cos(0.8 * t + 1.7 * ph));
        let radial = pos.xz / max(r, 1e-3);
        let bv = sw + radial * (0.55 * imp);
        o.posScale = vec4f(pos, s);
        o.quat = qaxis(vec3f(0.0, 1.0, 0.0), rnd.z * TAU);
        o.color = vec4f(mix(bandColor(f01), vec3f(1.0, 0.9, 0.8), clamp(imp * 0.25, 0.0, 0.5)), emis);
        let dist = length(pos - F.camPos.xyz);
        let pxW = dist * F.camFwd.w;              // metres per pixel at this distance
        let widen = max(1.0, F.sys[1].z * pxW / (s * 0.012));
        o.shape = vec4f(hy, length(bv), bendAngle(bv), widen);
        o.color.a = o.color.a / widen;
        // cull: frustum on a bounding sphere, and distance
        let c = pos + vec3f(0.0, 0.5 * s * hy, 0.0);
        let rad = s * hy * 0.8 + 0.1;
        var vis = dist < F.sys[1].y;
        for (var k = 0; k < 6; k++) { if (dot(F.planes[k].xyz, c) + F.planes[k].w < -rad) { vis = false; } }
        if (i < 14u) {
            // the source: a ring of crystal spires that breathe with the whole song's loudness
            let a = f32(i) / 14.0 * TAU;
            let rr = 2.6 + 0.4 * rnd.x;
            o.posScale = vec4f(rr * cos(a), -0.2, rr * sin(a), 1.0);
            let lean = 0.22 + 0.1 * rnd.y;
            o.quat = qmul(qaxis(vec3f(0.0, 1.0, 0.0), -a), qaxis(vec3f(0.0, 0.0, 1.0), lean));
            o.shape = vec4f(2.2 + 2.6 * rnd.z + 1.2 * F.audio2.x, 0.0, 0.0, 1.0);
            o.color = vec4f(1.0, 0.82, 0.62, 0.6 + 4.0 * F.audio2.x + 3.0 * F.audio2.y);
            vis = true;
            bucket = 2;
            slot = atomicAdd(&wgCount[2], 1u);
        } else if (vis) {
            bucket = select(1, 0, dist < F.sys[1].x);
            slot = atomicAdd(&wgCount[bucket], 1u);
        }
    }
    workgroupBarrier();
    if (li < 4u) { wgBase[li] = atomicAdd(&args[li * 5u + 1u], atomicLoad(&wgCount[li])); }
    workgroupBarrier();
    if (bucket >= 0) { outInst[u32(bucket) * cap + wgBase[bucket] + slot] = o; }
}

fn groundPos(uv: vec2f) -> vec2f {
    let w = sign(uv) * uv * uv * 520.0;
    return F.camPos.xz + w;
}
fn groundHeight(p: vec2f) -> f32 { return -0.02 * fbm(p * 0.05, 3); }
fn groundShade(w: vec3f, n: vec3f) -> vec3f {
    let r = length(w.xz);
    let R = F.sys[0].z;
    let t = F.camPos.w;
    let delay = r / F.sys[0].y;
    // under the reeds the low end of the song glows in the soil, a step behind the reeds
    let low = (specAtTime(t - delay, 3) + specAtTime(t - delay, 6) + specAtTime(t - delay, 10)) / 3.0;
    let inField = smoothstep(R + 4.0, R - 6.0, r) * smoothstep(F.sys[3].x - 1.0, F.sys[3].x + 3.0, r);
    var c = vec3f(0.004, 0.005, 0.007) * (0.5 + 0.5 * fbm(w.xz * 0.7, 3));
    c += vec3f(1.0, 0.25, 0.05) * 0.025 * low * low * inField;
    // each kick front as a thin bright ring on the ground
    var ring = 0.0;
    for (var k = 0; k < 8; k++) {
        let since = t - kickTime(k);
        if (since >= 0.0) {
            let front = since * F.sys[0].y;
            let d = r - front;
            ring += kickStr(k) * exp(-d * d / 0.35) * exp(-0.35 * since);
        }
    }
    c += vec3f(1.0, 0.75, 0.5) * ring * 0.9 * smoothstep(R + 30.0, R - 10.0, r);
    // the source: a pool of light at the centre
    let src = exp(-r * r / 4.0) * (0.3 + 1.5 * F.audio2.x + 1.5 * F.audio2.y);
    c += vec3f(1.0, 0.7, 0.45) * src;
    return c;
}
)";

struct FieldSystem final : System {
    std::uint32_t n = 1000000;
    float radius = 110.0f, inner = 5.0f, waveSpeed = 9.0f;
    wgpu::ComputePipeline pipe;
    wgpu::BindGroup group;
    std::uint32_t wgX = 1, wgY = 1;
    std::uint64_t outBytes = 0;

    std::string wgsl() const override { return kFieldWgsl; }
    std::vector<MeshCpu> meshes() const override {
        const glm::vec3 stalk(0.07f, 0.075f, 0.06f);
        return {makeReed(5, 4, 0.010f, 0.004f, 0.016f, 0.12f, stalk), makeReed(3, 1, 0.010f, 0.005f, 0.018f, 0.14f, stalk), makeCrystal(6, 0.22f, 0.8f, glm::vec3(0.9f, 0.85f, 0.8f), 1.0f)};
    }
    std::vector<Bucket> buckets() const override { return {{0}, {1}, {2}}; }
    std::uint32_t capacity() const override { return (n + 3) & ~3u; }
    Look look() const override {
        Look l;
        l.fog = {0.010f, 0.016f, 0.032f, 0.0085f};
        l.skyZenith = {0.0015f, 0.0025f, 0.007f, 1.0f};
        l.skyHorizon = {0.020f, 0.030f, 0.060f, 1.0f};
        l.sun = glm::vec4(glm::normalize(glm::vec3(-0.6f, 0.12f, -0.8f)), 0.25f);
        l.sunColor = {0.55f, 0.65f, 1.0f, 0.6f};
        l.groundTint = {0.006f, 0.007f, 0.01f, 0.5f};
        l.exposure = 1.25f; l.bloom = 0.09f; l.vignette = 0.45f; l.contrast = 1.05f; l.saturation = 1.08f;
        return l;
    }
    Cam camera(double t, int shot) const override {
        Cam c;
        const float ft = static_cast<float>(t);
        if (shot == 0) {        // elevated slow orbit
            const float a = 0.035f * ft + 0.8f;
            c.eye = glm::vec3(std::cos(a) * 62.0f, 15.0f, std::sin(a) * 62.0f);
            c.target = glm::vec3(std::cos(a) * 18.0f, 0.0f, std::sin(a) * 18.0f);
            c.fovDeg = 52.0f;
        } else if (shot == 1) { // high crane, the whole field
            const float a = 0.02f * ft;
            c.eye = glm::vec3(std::cos(a) * 95.0f, 120.0f, std::sin(a) * 95.0f);
            c.target = glm::vec3(0.0f, 0.0f, 0.0f);
            c.fovDeg = 55.0f;
        } else if (shot == 2) { // low, at the rim, looking in toward the source
            const float a = 0.01f * ft + 2.2f;
            c.eye = glm::vec3(std::cos(a) * 96.0f, 2.2f, std::sin(a) * 96.0f);
            c.target = glm::vec3(0.0f, 1.0f, 0.0f);
            c.fovDeg = 42.0f;
        } else if (shot == 4) { // the clip move: low at the rim looking in, rising into the orbit
            const float u = smooth01((ft - 35.5f) / 14.0f);
            const float a = 2.2f + 0.5f * u;
            const float r = 98.0f - 40.0f * u;
            c.eye = glm::vec3(std::cos(a) * r, 2.4f + 20.0f * std::pow(u, 1.4f), std::sin(a) * r);
            c.target = glm::vec3(0.0f, 1.5f - 1.5f * u, 0.0f);
            c.fovDeg = 46.0f + 6.0f * u;
        } else {                // low, near the source, looking out across the rings
            const float a = 0.03f * ft + 4.0f;
            c.eye = glm::vec3(std::cos(a) * 9.0f, 3.2f, std::sin(a) * 9.0f);
            c.target = glm::vec3(std::cos(a) * 60.0f, 0.0f, std::sin(a) * 60.0f);
            c.fovDeg = 58.0f;
        }
        return c;
    }
    void init(gpu::Context& ctx, const wgpu::ShaderModule& m, const wgpu::BindGroupLayout& frameLayout, const wgpu::Buffer& out,
              const wgpu::Buffer& args) override {
        auto l1 = makeLayout(ctx, {Bind::Storage, Bind::Storage}, wgpu::ShaderStage::Compute);
        pipe = makeCompute(ctx, m, "cs_field", makePipelineLayout(ctx, {frameLayout, l1}));
        outBytes = static_cast<std::uint64_t>(capacity()) * 3 * sizeof(Inst);
        group = makeGroup(ctx, l1, {buf(out, outBytes), buf(args, 80)});
        const std::uint32_t groups = (n + 63) / 64;
        wgX = std::min<std::uint32_t>(groups, 32768);
        wgY = (groups + wgX - 1) / wgX;
    }
    void setParams(FrameU& f, double) override {
        f.sys[0] = {static_cast<float>(n), waveSpeed, radius, static_cast<float>(capacity())};
        f.sys[1] = {38.0f, 400.0f, 0.6f, 0.9f};
        f.sys[2] = {7.0f, 1.25f, 1.5f, 0.10f};
        f.sys[3] = {inner, 2.0f, 0.0f, 0.0f};
        for (auto& p : f.kickP) p = glm::vec4(0.0f);
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
    std::uint64_t gpuStateBytes() const override { return 0; } // stateless: nothing persists between frames
};

// (systems B and C are appended below)
#include "art_world.inl"
#include "art_organism.inl"

} // namespace

// ================================================================================================

int main(int argc, char** argv) {
    std::string systemName = "field", png, clipDir, songPath, json;
    std::uint32_t width = 1920, height = 1080, frames = 300, warm = 30, n = 0;
    int shot = 0;
    double start = 40.0, fps = 60.0;
    bool seektest = false;
    std::string seekArgs;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--system") systemName = next();
        else if (a == "--n") n = static_cast<std::uint32_t>(std::stoul(next()));
        else if (a == "--frames") frames = static_cast<std::uint32_t>(std::stoul(next()));
        else if (a == "--warm") warm = static_cast<std::uint32_t>(std::stoul(next()));
        else if (a == "--png") png = next();
        else if (a == "--clip") clipDir = next();
        else if (a == "--fps") fps = std::stod(next());
        else if (a == "--start") start = std::stod(next());
        else if (a == "--shot") shot = std::stoi(next());
        else if (a == "--song") songPath = next();
        else if (a == "--seektest") { seektest = true; seekArgs = next(); }
        else if (a == "--size") { const auto s = next(); std::sscanf(s.c_str(), "%ux%u", &width, &height); }
        else { std::fprintf(stderr, "unknown arg %s\n", a.c_str()); return 2; }
    }
    if (songPath.empty()) songPath = std::string(std::getenv("HOME")) + "/Desktop/All You Got.wav";

    // ---- audio: production's offline analysis of the real track, cached ----
    std::filesystem::create_directories(std::filesystem::path(argv[0]).parent_path() / "cache");
    const std::string cache = (std::filesystem::path(argv[0]).parent_path() / "cache" / "song.bin").string();
    const auto ta = Clock::now();
    const gpuworld::SongAnalysis song = gpuworld::loadSong(songPath, cache);
    const double songLoadMs = msSince(ta);
    std::fprintf(stderr, "song: %d rows at %.2f/s, %.1f s, %zu kicks, %zu snares, %zu hats (analysis %.1f s, load %.0f ms)\n",
                 song.hops, song.hopRate, song.duration, song.kickT.size(), song.snareT.size(), song.hatT.size(), song.analyseSeconds,
                 songLoadMs);

    std::unique_ptr<System> sys;
    if (systemName == "field") { auto s = std::make_unique<FieldSystem>(); if (n) s->n = n; sys = std::move(s); }
    else if (systemName == "world") { auto s = std::make_unique<WorldSystem>(); if (n) s->speed = static_cast<float>(n); sys = std::move(s); }
    else if (systemName == "organism") { auto s = std::make_unique<OrganismSystem>(); if (n) s->n = n; sys = std::move(s); }
    else { std::fprintf(stderr, "--system field|world|organism\n"); return 2; }

    auto ctxR = gpu::Context::create(gpu::ContextDesc{});
    if (!ctxR) { std::fprintf(stderr, "context: %s\n", ctxR.error().message.c_str()); return 3; }
    gpu::Context& ctx = **ctxR;
    const wgpu::Device& dev = ctx.device();
    const wgpu::Queue& queue = ctx.queue();
    gpu::FrameTimeline timeline(ctx);

    // ---- shared buffers ----
    wgpu::Buffer frameBuf = makeBuffer(ctx, sizeof(FrameU), wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst, "frame");
    wgpu::Buffer specBuf = makeBuffer(ctx, song.spec.size() * 4, wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst, "spec");
    queue.WriteBuffer(specBuf, 0, song.spec.data(), song.spec.size() * 4);
    const auto bucketList = sys->buckets();
    const std::uint32_t cap = sys->capacity();
    const std::uint64_t outBytes = static_cast<std::uint64_t>(cap) * std::max<std::size_t>(bucketList.size(), 1) * sizeof(Inst);
    wgpu::Buffer outBuf = makeBuffer(ctx, outBytes, wgpu::BufferUsage::Storage, "insts");
    wgpu::Buffer argsBuf = makeBuffer(ctx, 160, wgpu::BufferUsage::Storage | wgpu::BufferUsage::Indirect | wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::CopySrc, "args");
    wgpu::Buffer dummyAux = makeBuffer(ctx, 256, wgpu::BufferUsage::Storage, "aux0");

    // ---- shaders ----
    const std::string common = kCommonWgsl;
    wgpu::ShaderModule sysMod = compile(ctx, common + sys->wgsl(), "system");
    wgpu::ShaderModule drawMod = compile(ctx, common + sys->wgsl() + kInstDrawWgsl, "draw");
    wgpu::ShaderModule groundMod = compile(ctx, common + sys->wgsl() + kGroundWgsl + kSkyWgsl, "ground");
    wgpu::ShaderModule postMod = compile(ctx, kPostWgsl, "post");

    const auto vfc = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment | wgpu::ShaderStage::Compute;
    auto frameLayout = makeLayout(ctx, {Bind::Uniform, Bind::ReadOnly}, vfc);
    auto renderLayout0 = makeLayout(ctx, {Bind::Uniform, Bind::ReadOnly, Bind::ReadOnly}, wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment);
    auto instLayout = makeLayout(ctx, {Bind::ReadOnly}, wgpu::ShaderStage::Vertex);
    wgpu::BindGroup frameGroup = makeGroup(ctx, frameLayout, {buf(frameBuf, sizeof(FrameU)), buf(specBuf, song.spec.size() * 4)});

    sys->init(ctx, sysMod, frameLayout, outBuf, argsBuf);
    wgpu::Buffer aux = sys->auxBuffer() ? sys->auxBuffer() : dummyAux;
    const std::uint64_t auxBytes = sys->auxBuffer() ? sys->auxBytes() : 256;
    wgpu::BindGroup renderGroup0 = makeGroup(ctx, renderLayout0, {buf(frameBuf, sizeof(FrameU)), buf(specBuf, song.spec.size() * 4), buf(aux, auxBytes)});
    std::vector<wgpu::BindGroup> instGroups;
    for (std::size_t b = 0; b < bucketList.size(); ++b)
        instGroups.push_back(makeGroup(ctx, instLayout, {buf(outBuf, static_cast<std::uint64_t>(cap) * sizeof(Inst), b * static_cast<std::uint64_t>(cap) * sizeof(Inst))}));

    std::vector<MeshGpu> meshes;
    std::vector<std::uint32_t> triCounts;
    for (const auto& m : sys->meshes()) { meshes.push_back(upload(ctx, m)); triCounts.push_back(static_cast<std::uint32_t>(m.i.size() / 3)); }

    // ground grid
    const int G = 400;
    std::vector<float> gv;
    std::vector<std::uint32_t> gi;
    for (int z = 0; z <= G; ++z)
        for (int x = 0; x <= G; ++x) { gv.push_back(-1.0f + 2.0f * x / G); gv.push_back(-1.0f + 2.0f * z / G); }
    for (int z = 0; z < G; ++z)
        for (int x = 0; x < G; ++x) {
            const std::uint32_t a = z * (G + 1) + x, b = a + 1, c = a + (G + 1), d = c + 1;
            gi.insert(gi.end(), {a, c, b, b, c, d});
        }
    wgpu::Buffer gvb = makeBuffer(ctx, gv.size() * 4, wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst, "gvb");
    queue.WriteBuffer(gvb, 0, gv.data(), gv.size() * 4);
    wgpu::Buffer gib = makeBuffer(ctx, gi.size() * 4, wgpu::BufferUsage::Index | wgpu::BufferUsage::CopyDst, "gib");
    queue.WriteBuffer(gib, 0, gi.data(), gi.size() * 4);

    // ---- targets ----
    const std::uint32_t msaa = 4;
    auto makeTex = [&](std::uint32_t w, std::uint32_t h, wgpu::TextureFormat f, wgpu::TextureUsage u, std::uint32_t samples = 1) {
        wgpu::TextureDescriptor td{};
        td.size = {w, h, 1};
        td.format = f;
        td.usage = u;
        td.sampleCount = samples;
        return dev.CreateTexture(&td);
    };
    const auto hdrFmt = wgpu::TextureFormat::RGBA16Float;
    wgpu::Texture msColor = makeTex(width, height, hdrFmt, wgpu::TextureUsage::RenderAttachment, msaa);
    wgpu::Texture msDepth = makeTex(width, height, wgpu::TextureFormat::Depth32Float, wgpu::TextureUsage::RenderAttachment, msaa);
    wgpu::Texture hdr = makeTex(width, height, hdrFmt, wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding);
    wgpu::Texture ldr = makeTex(width, height, wgpu::TextureFormat::RGBA8Unorm, wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc);
    constexpr int kBloomLevels = 6;
    std::vector<wgpu::Texture> bloom;
    std::vector<wgpu::TextureView> bloomViews;
    for (int k = 0; k < kBloomLevels; ++k) {
        const std::uint32_t w = std::max(1u, width >> (k + 1)), h = std::max(1u, height >> (k + 1));
        bloom.push_back(makeTex(w, h, hdrFmt, wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding));
        bloomViews.push_back(bloom.back().CreateView());
    }
    wgpu::TextureView msColorView = msColor.CreateView(), msDepthView = msDepth.CreateView(), hdrView = hdr.CreateView(), ldrView = ldr.CreateView();
    wgpu::SamplerDescriptor sd{};
    sd.minFilter = wgpu::FilterMode::Linear;
    sd.magFilter = wgpu::FilterMode::Linear;
    sd.addressModeU = sd.addressModeV = wgpu::AddressMode::ClampToEdge;
    wgpu::Sampler sampler = dev.CreateSampler(&sd);

    // ---- pipelines ----
    std::array<wgpu::VertexAttribute, 3> attrs{};
    attrs[0] = {nullptr, wgpu::VertexFormat::Float32x3, 0, 0};
    attrs[1] = {nullptr, wgpu::VertexFormat::Float32x3, 12, 1};
    attrs[2] = {nullptr, wgpu::VertexFormat::Float32x4, 24, 2};
    wgpu::VertexBufferLayout vbl{};
    vbl.arrayStride = sizeof(Vtx);
    vbl.attributeCount = attrs.size();
    vbl.attributes = attrs.data();
    wgpu::VertexAttribute gattr{nullptr, wgpu::VertexFormat::Float32x2, 0, 0};
    wgpu::VertexBufferLayout gvbl{};
    gvbl.arrayStride = 8;
    gvbl.attributeCount = 1;
    gvbl.attributes = &gattr;

    auto scenePipe = [&](const wgpu::ShaderModule& m, const char* vs, const char* fs, const wgpu::VertexBufferLayout* layout,
                         const std::vector<wgpu::BindGroupLayout>& groups, bool depthWrite, wgpu::CompareFunction cmp) {
        wgpu::ColorTargetState cts{};
        cts.format = hdrFmt;
        wgpu::FragmentState fst{};
        fst.module = m;
        fst.entryPoint = fs;
        fst.targetCount = 1;
        fst.targets = &cts;
        wgpu::DepthStencilState ds{};
        ds.format = wgpu::TextureFormat::Depth32Float;
        ds.depthWriteEnabled = depthWrite ? wgpu::OptionalBool::True : wgpu::OptionalBool::False;
        ds.depthCompare = cmp;
        wgpu::RenderPipelineDescriptor rpd{};
        rpd.layout = makePipelineLayout(ctx, groups);
        rpd.vertex.module = m;
        rpd.vertex.entryPoint = vs;
        rpd.vertex.bufferCount = layout ? 1 : 0;
        rpd.vertex.buffers = layout;
        rpd.fragment = &fst;
        rpd.depthStencil = &ds;
        rpd.multisample.count = msaa;
        rpd.primitive.cullMode = wgpu::CullMode::None;
        return dev.CreateRenderPipeline(&rpd);
    };
    wgpu::RenderPipeline instPipe = scenePipe(drawMod, "vs_inst", "fs_inst", &vbl, {renderLayout0, instLayout}, true, wgpu::CompareFunction::Less);
    wgpu::RenderPipeline groundPipe = scenePipe(groundMod, "vs_ground", "fs_ground", &gvbl, {renderLayout0}, true, wgpu::CompareFunction::Less);
    wgpu::RenderPipeline skyPipe = scenePipe(groundMod, "vs_sky", "fs_sky", nullptr, {renderLayout0}, false, wgpu::CompareFunction::LessEqual);

    auto postLayout = makeLayout(ctx, {Bind::Texture, Bind::Sampler, Bind::Uniform, Bind::Texture}, wgpu::ShaderStage::Fragment | wgpu::ShaderStage::Vertex);
    auto postPL = makePipelineLayout(ctx, {postLayout});
    auto postPipe = [&](const char* fs, wgpu::TextureFormat fmt, bool additive) {
        wgpu::BlendState blend{};
        blend.color = {wgpu::BlendOperation::Add, wgpu::BlendFactor::One, wgpu::BlendFactor::One};
        blend.alpha = {wgpu::BlendOperation::Add, wgpu::BlendFactor::One, wgpu::BlendFactor::One};
        wgpu::ColorTargetState cts{};
        cts.format = fmt;
        if (additive) cts.blend = &blend;
        wgpu::FragmentState fst{};
        fst.module = postMod;
        fst.entryPoint = fs;
        fst.targetCount = 1;
        fst.targets = &cts;
        wgpu::RenderPipelineDescriptor rpd{};
        rpd.layout = postPL;
        rpd.vertex.module = postMod;
        rpd.vertex.entryPoint = "vs_full";
        rpd.fragment = &fst;
        return dev.CreateRenderPipeline(&rpd);
    };
    wgpu::RenderPipeline downFirst = postPipe("fs_down_first", hdrFmt, false);
    wgpu::RenderPipeline down = postPipe("fs_down", hdrFmt, false);
    wgpu::RenderPipeline up = postPipe("fs_up", hdrFmt, true);
    wgpu::RenderPipeline composite = postPipe("fs_composite", wgpu::TextureFormat::RGBA8Unorm, false);
    wgpu::Buffer postBuf = makeBuffer(ctx, 32, wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst, "post");
    std::vector<wgpu::BindGroup> downGroups, upGroups;
    wgpu::Texture dummyTex = makeTex(1, 1, hdrFmt, wgpu::TextureUsage::TextureBinding);
    wgpu::TextureView dummyView = dummyTex.CreateView();
    downGroups.push_back(makeGroup(ctx, postLayout, {tex(hdrView), smp(sampler), buf(postBuf, 32), tex(dummyView)}));
    for (int k = 1; k < kBloomLevels; ++k) downGroups.push_back(makeGroup(ctx, postLayout, {tex(bloomViews[k - 1]), smp(sampler), buf(postBuf, 32), tex(dummyView)}));
    for (int k = kBloomLevels - 1; k >= 1; --k) upGroups.push_back(makeGroup(ctx, postLayout, {tex(bloomViews[k]), smp(sampler), buf(postBuf, 32), tex(dummyView)}));
    wgpu::BindGroup compGroup = makeGroup(ctx, postLayout, {tex(hdrView), smp(sampler), buf(postBuf, 32), tex(bloomViews[0])});

    ctx.waitForQueue();
    if (ctx.errorCount() != 0) { std::fprintf(stderr, "setup errors: %s\n", ctx.lastError().c_str()); return 4; }

    const Look look = sys->look();
    std::vector<std::uint32_t> argsInit(40, 0);
    for (std::size_t b = 0; b < bucketList.size(); ++b) argsInit[b * 5] = meshes[bucketList[b].mesh].indexCount;

    // ---- the seek / determinism test (organism only) ----
    if (seektest) {
        auto* org = dynamic_cast<OrganismSystem*>(sys.get());
        if (!org) { std::fprintf(stderr, "--seektest needs --system organism\n"); return 2; }
        return org->seekTest(ctx, frameBuf, frameGroup, song, seekArgs, timeline);
    }

    Stat sInterval, sCpu, sGpuFrame, sCompute, sScene, sPost;
    std::array<wgpu::Future, 2> inflight{};
    std::array<bool, 2> inflightValid{false, false};
    std::uint64_t lastCompleted = 0;
    Clock::time_point prevStart{};
    const std::uint32_t total = warm + frames;
    const bool clip = !clipDir.empty();
    if (clip) std::filesystem::create_directories(clipDir);
    std::uint64_t footprint0 = 0;
    FrameU fu{};
    std::uint32_t lastVisible[8] = {0, 0, 0, 0, 0, 0, 0, 0};

    for (std::uint32_t f = 0; f < total; ++f) {
        const bool measuring = f >= warm;
        if (f == warm) footprint0 = footprintBytes();
        const auto t0 = Clock::now();
        if (measuring && f > warm) sInterval.add(std::chrono::duration<double, std::milli>(t0 - prevStart).count());
        prevStart = t0;
        const int slot = static_cast<int>(f % 2);
        if (inflightValid[slot]) (void)ctx.waitFor(inflight[slot]);
        const auto cpu0 = Clock::now();

        // in clip mode the first measured frame is `start`; warm-up frames run before it
        const double t = start + (static_cast<double>(f) - static_cast<double>(warm)) / fps;
        sys->beginFrame(t);
        const Cam cam = sys->camera(t, shot);
        const glm::mat4 view = glm::lookAt(cam.eye, cam.target, glm::vec3(0, 1, 0));
        const glm::mat4 proj = glm::perspectiveZO(glm::radians(cam.fovDeg), static_cast<float>(width) / height, 0.1f, 3000.0f);
        fu.viewProj = proj * view;
        fu.invViewProj = glm::inverse(fu.viewProj);
        fu.camPos = glm::vec4(cam.eye, static_cast<float>(t));
        fu.camFwd = glm::vec4(glm::normalize(cam.target - cam.eye), 2.0f * std::tan(glm::radians(cam.fovDeg) * 0.5f) / height);
        framePlanes(fu.viewProj, fu.planes);
        const gpuworld::AudioAtT au = gpuworld::sampleSong(song, t);
        fu.audio0 = au.env0;
        fu.audio1 = glm::vec4(au.env1.x, au.env1.y, au.env1.z, au.hopF);
        fu.audio2 = glm::vec4(au.rms, au.kickEnv, au.snareEnv, au.hatEnv);
        for (int k = 0; k < 8; ++k) { fu.kickT[k / 4][k % 4] = au.kickT[k]; fu.kickS[k / 4][k % 4] = au.kickS[k]; }
        for (int k = 0; k < 4; ++k) { fu.snareT[k] = au.snareT[k]; fu.snareS[k] = au.snareS[k]; }
        fu.misc = glm::vec4(song.hopRate, static_cast<float>(song.hops), song.t0, 0.0f);
        fu.fog = look.fog; fu.skyZenith = look.skyZenith; fu.skyHorizon = look.skyHorizon; fu.sun = look.sun;
        fu.sunColor = look.sunColor; fu.groundTint = look.groundTint;
        sys->setParams(fu, t);
        queue.WriteBuffer(frameBuf, 0, &fu, sizeof(fu));
        queue.WriteBuffer(argsBuf, 0, argsInit.data(), argsInit.size() * 4);
        const float post[8] = {look.exposure, look.bloom, look.vignette, look.grain, static_cast<float>(f), look.contrast, look.saturation, 0.0f};
        queue.WriteBuffer(postBuf, 0, post, sizeof(post));

        timeline.beginFrame();
        wgpu::CommandEncoder enc = dev.CreateCommandEncoder();
        sys->compute(enc, frameGroup, timeline);
        {
            wgpu::RenderPassColorAttachment ca{};
            ca.view = msColorView;
            ca.resolveTarget = hdrView;
            ca.loadOp = wgpu::LoadOp::Clear;
            ca.storeOp = wgpu::StoreOp::Discard;
            ca.clearValue = {0, 0, 0, 1};
            wgpu::RenderPassDepthStencilAttachment da{};
            da.view = msDepthView;
            da.depthLoadOp = wgpu::LoadOp::Clear;
            da.depthStoreOp = wgpu::StoreOp::Discard;
            da.depthClearValue = 1.0f;
            wgpu::RenderPassDescriptor rp{};
            rp.colorAttachmentCount = 1;
            rp.colorAttachments = &ca;
            rp.depthStencilAttachment = &da;
            rp.timestampWrites = timeline.mark("scene", gpu::FrameTimeline::PassKind::Render);
            auto r = enc.BeginRenderPass(&rp);
            r.SetBindGroup(0, renderGroup0);
            r.SetPipeline(instPipe);
            for (std::size_t b = 0; b < bucketList.size(); ++b) {
                const MeshGpu& m = meshes[bucketList[b].mesh];
                r.SetBindGroup(1, instGroups[b]);
                r.SetVertexBuffer(0, m.vb);
                r.SetIndexBuffer(m.ib, wgpu::IndexFormat::Uint32);
                r.DrawIndexedIndirect(argsBuf, b * 20);
            }
            r.SetPipeline(groundPipe);
            r.SetVertexBuffer(0, gvb);
            r.SetIndexBuffer(gib, wgpu::IndexFormat::Uint32);
            r.DrawIndexed(static_cast<std::uint32_t>(gi.size()));
            r.SetPipeline(skyPipe);
            r.Draw(3);
            r.End();
        }
        auto fullPass = [&](const wgpu::TextureView& target, const wgpu::RenderPipeline& p, const wgpu::BindGroup& g, bool load, const char* mark) {
            wgpu::RenderPassColorAttachment ca{};
            ca.view = target;
            ca.loadOp = load ? wgpu::LoadOp::Load : wgpu::LoadOp::Clear;
            ca.storeOp = wgpu::StoreOp::Store;
            ca.clearValue = {0, 0, 0, 1};
            wgpu::RenderPassDescriptor rp{};
            rp.colorAttachmentCount = 1;
            rp.colorAttachments = &ca;
            if (mark) rp.timestampWrites = timeline.mark(mark, gpu::FrameTimeline::PassKind::Render);
            auto r = enc.BeginRenderPass(&rp);
            r.SetPipeline(p);
            r.SetBindGroup(0, g);
            r.Draw(3);
            r.End();
        };
        fullPass(bloomViews[0], downFirst, downGroups[0], false, "post");
        for (int k = 1; k < kBloomLevels; ++k) fullPass(bloomViews[k], down, downGroups[k], false, nullptr);
        for (int k = kBloomLevels - 1, u = 0; k >= 1; --k, ++u) fullPass(bloomViews[k - 1], up, upGroups[u], true, nullptr);
        fullPass(ldrView, composite, compGroup, false, nullptr);
        timeline.resolve(enc);
        wgpu::CommandBuffer cb = enc.Finish();
        queue.Submit(1, &cb);
        inflight[slot] = queue.OnSubmittedWorkDone(wgpu::CallbackMode::WaitAnyOnly, [](wgpu::QueueWorkDoneStatus, wgpu::StringView) {});
        inflightValid[slot] = true;
        timeline.collect();
        const double cpuMs = msSince(cpu0);

        if (clip && measuring) {
            auto img = gpu::readTexture8(ctx, ldr, width, height, false);
            char name[64];
            std::snprintf(name, sizeof(name), "/f%05u.png", f - warm);
            if (img) (void)assets::writePng(clipDir + name, width, height, img->rgba);
        }
        if (measuring) {
            sCpu.add(cpuMs);
            if (timeline.completedFrames() != lastCompleted) {
                lastCompleted = timeline.completedFrames();
                sGpuFrame.add(timeline.frameMs());
                sCompute.add(timeline.msFor("compute"));
                sScene.add(timeline.msFor("scene"));
                sPost.add(timeline.msFor("post"));
            }
        }
    }
    ctx.waitForQueue();
    {
        wgpu::Buffer rb = makeBuffer(ctx, 160, wgpu::BufferUsage::MapRead | wgpu::BufferUsage::CopyDst, "rb");
        wgpu::CommandEncoder enc = dev.CreateCommandEncoder();
        enc.CopyBufferToBuffer(argsBuf, 0, rb, 0, 160);
        wgpu::CommandBuffer cb = enc.Finish();
        queue.Submit(1, &cb);
        auto fut = rb.MapAsync(wgpu::MapMode::Read, 0, 160, wgpu::CallbackMode::WaitAnyOnly, [](wgpu::MapAsyncStatus, wgpu::StringView) {});
        (void)ctx.waitFor(fut);
        const auto* a = static_cast<const std::uint32_t*>(rb.GetConstMappedRange(0, 160));
        for (int b = 0; b < 8; ++b) lastVisible[b] = a[b * 5 + 1];
    }
    if (!png.empty()) {
        auto img = gpu::readTexture8(ctx, ldr, width, height, false);
        if (img) (void)assets::writePng(png, width, height, img->rgba);
    }
    if (ctx.errorCount() != 0) { std::fprintf(stderr, "gpu errors: %s\n", ctx.lastError().c_str()); return 5; }

    std::uint64_t tris = 0;
    for (std::size_t b = 0; b < bucketList.size(); ++b) tris += static_cast<std::uint64_t>(lastVisible[b]) * triCounts[bucketList[b].mesh];
    auto j = [](const char* name, const Stat& s) {
        std::printf("\"%s\":{\"p10\":%.3f,\"p50\":%.3f,\"p90\":%.3f},", name, s.pct(0.1), s.pct(0.5), s.pct(0.9));
    };
    std::printf("{\"system\":\"%s\",\"n\":%u,\"shot\":%d,\"start\":%.2f,\"frames\":%u,\"clip\":%d,", systemName.c_str(), n, shot, start, frames, clip ? 1 : 0);
    j("interval_ms", sInterval); j("cpu_ms", sCpu); j("gpu_frame_ms", sGpuFrame); j("gpu_compute_ms", sCompute); j("gpu_scene_ms", sScene); j("gpu_post_ms", sPost);
    std::printf("\"visible\":[%u,%u,%u,%u,%u,%u,%u,%u],\"triangles\":%llu,\"inst_buffer_mb\":%.1f,\"gpu_state_mb\":%.1f,\"spec_mb\":%.1f,\"footprint_mb\":%.1f,"
                "\"footprint_growth_mb\":%.1f,\"upload_bytes_per_frame\":%zu%s}\n",
                lastVisible[0], lastVisible[1], lastVisible[2], lastVisible[3], lastVisible[4], lastVisible[5], lastVisible[6], lastVisible[7], static_cast<unsigned long long>(tris), outBytes / 1048576.0,
                sys->gpuStateBytes() / 1048576.0, song.spec.size() * 4 / 1048576.0, footprintBytes() / 1048576.0,
                (static_cast<double>(footprintBytes()) - footprint0) / 1048576.0, sizeof(FrameU) + 80 + 32, sys->extraJson().c_str());
    return 0;
}
