#include "pathtrace/path_tracer.hpp"

#include "assets/exr.hpp"
#include "core/log.hpp"
#include "pathtrace/bsdf.hpp"
#include "pathtrace/camera.hpp"
#include "pathtrace/sampler.hpp"
#include "pathtrace/texture.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <span>
#include <thread>

namespace avgen::pathtrace {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kInvPi = 0.31830988618379067f;

[[nodiscard]] bool finite(const glm::vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

// The emitter's colour times its intensity, with the light's colour temperature already folded in.
// `colorTemperatureToRgb` is the project's own conversion, so a light looks the same in both
// renderers; applying it here rather than assuming `color` is final is what keeps that true.
[[nodiscard]] glm::vec3 lightRadiance(const scene::PunctualLight& l) {
    return l.color * scene::colorTemperatureToRgb(l.temperature, l.tint) * l.intensity;
}

// The range window the realtime path applies, transcribed from shaders/lighting.wgsl rather than
// reinvented, so the two renderers agree about where a light stops reaching.
[[nodiscard]] float rangeWindow(float dist2, float range) {
    if (range <= 0.0f) return 1.0f;
    const float ratio = dist2 / (range * range);
    const float w = std::clamp(1.0f - ratio * ratio, 0.0f, 1.0f);
    return w * w;
}

struct LightSample {
    glm::vec3 direction{0.0f}; // from the surface toward the light, normalised
    glm::vec3 radiance{0.0f};  // already includes attenuation and emitter area
    float distance = 0.0f;     // for the shadow ray's tfar; infinite for directional
    bool valid = false;
};

// One sample of one light. Area emitters are sampled over their surface -- that is what makes a
// soft shadow -- while the realtime path samples the centre and widens the result.
[[nodiscard]] LightSample sampleLight(const scene::PunctualLight& l, const glm::vec3& p, Sampler& sampler) {
    LightSample s;
    if (!l.enabled) return s;

    const glm::vec3 emit = lightRadiance(l);
    if (emit.x <= 0.0f && emit.y <= 0.0f && emit.z <= 0.0f) return s;

    using T = scene::PunctualLight::Type;
    if (l.type == T::Directional) {
        // `direction` is the direction the light travels, so the surface looks back along it.
        const float len = glm::length(l.direction);
        if (len < 1e-9f) return s;
        s.direction = -l.direction / len;
        s.radiance = emit;                 // illuminance; no distance falloff
        s.distance = std::numeric_limits<float>::infinity();
        s.valid = true;
        return s;
    }

    // Everything else has a position. Area kinds get a point sampled on the emitter.
    glm::vec3 target = l.position;
    float cosEmitter = 1.0f;
    bool isArea = false;

    if (l.type == T::Rect) {
        isArea = true;
        glm::vec3 n = glm::length(l.direction) > 1e-9f ? glm::normalize(l.direction) : glm::vec3(0, -1, 0);
        glm::vec3 up = glm::length(l.up) > 1e-9f ? glm::normalize(l.up) : glm::vec3(0, 1, 0);
        glm::vec3 tangent = glm::cross(n, up);
        if (glm::length(tangent) < 1e-6f) tangent = glm::cross(n, glm::vec3(1, 0, 0));
        tangent = glm::normalize(tangent);
        const glm::vec3 bitangent = glm::normalize(glm::cross(tangent, n));
        const glm::vec2 u = sampler.next2D();
        target = l.position + tangent * ((u.x - 0.5f) * l.width) + bitangent * ((u.y - 0.5f) * l.height);
        const glm::vec3 toSurface = glm::normalize(p - target);
        cosEmitter = std::max(0.0f, glm::dot(n, toSurface));
        if (cosEmitter <= 0.0f) return s;  // the surface is behind the emitter
    } else if (l.type == T::Disk || l.type == T::Sphere || l.type == T::Tube) {
        isArea = true;
        // A uniform point in the emitter's bounding disc, oriented to face the shading point. Crude
        // for a sphere and honest about it: section 44's proper solid-angle sampling is Phase 3.
        const glm::vec3 toP = glm::normalize(p - l.position);
        glm::vec3 t{};
        glm::vec3 b{};
        orthonormalBasis(toP, t, b);
        const glm::vec2 d = sampleUniformDisc(sampler.next2D());
        target = l.position + (t * d.x + b * d.y) * l.radius;
        cosEmitter = 1.0f;
    }

    const glm::vec3 delta = target - p;
    const float dist2 = glm::dot(delta, delta);
    if (dist2 < 1e-12f) return s;
    const float dist = std::sqrt(dist2);
    s.direction = delta / dist;
    s.distance = dist;

    float attenuation = rangeWindow(dist2, l.range) / dist2;

    if (l.type == T::Spot) {
        const float len = glm::length(l.direction);
        if (len < 1e-9f) return s;
        const glm::vec3 axis = l.direction / len;
        const float cosAngle = glm::dot(axis, -s.direction);
        const float cosOuter = std::cos(l.outerConeAngle);
        const float cosInner = std::cos(l.innerConeAngle);
        const float denom = std::max(1e-4f, cosInner - cosOuter);
        const float spot = std::clamp((cosAngle - cosOuter) / denom, 0.0f, 1.0f);
        if (spot <= 0.0f) return s;
        attenuation *= spot * spot;
    }

    if (isArea) {
        // nits -> intensity. `emitterArea` is the project's own conversion and is shared with
        // rendering::lightInfluenceRadius so a light's reach and its brightness agree.
        attenuation *= scene::emitterArea(l) * cosEmitter;
    }

    s.radiance = emit * attenuation;
    s.valid = attenuation > 0.0f;
    return s;
}

[[nodiscard]] glm::vec3 environmentRadiance(const Snapshot& snap, const glm::vec3& dir) {
    if (!snap.skyEnabled) return snap.backgroundColor;
    return scene::skyRadiance(snap.sky, dir);
}

// glTF: every texture MULTIPLIES its factor. Base colour and emissive arrive already linear from
// `sampleSlot` (it decodes iff the format is sRGB); metallic-roughness and occlusion are linear
// data and are never decoded. The channel assignment -- roughness in G, metallic in B -- is the
// glTF spec's, and getting it backwards is a silent, plausible-looking error.
[[nodiscard]] SurfaceMaterial resolveMaterial(const Snapshot& snap, const scene::Material& mat,
                                              glm::vec2 uv) {
    SurfaceMaterial m;
    const std::span<const scene::TextureData> textures{snap.textures};

    const glm::vec4 base = sampleSlot(textures, mat.baseColorTexture, uv, glm::vec4(1.0f));
    m.baseColor = mat.baseColor * glm::vec3(base);
    m.opacity = mat.opacity * base.w;

    const glm::vec4 mr = sampleSlot(textures, mat.metallicRoughnessTexture, uv, glm::vec4(1.0f));
    m.roughness = glm::clamp(mat.roughness * mr.y, 0.0f, 1.0f);
    m.metallic = glm::clamp(mat.metallic * mr.z, 0.0f, 1.0f);

    const glm::vec4 em = sampleSlot(textures, mat.emissiveTexture, uv, glm::vec4(1.0f));
    m.emission = mat.emissiveColor * mat.emissiveIntensity * glm::vec3(em);

    const glm::vec4 ao = sampleSlot(textures, mat.occlusionTexture, uv, glm::vec4(1.0f));
    // glTF: occlusion applies only to indirect light, and `occlusionStrength` interpolates it in.
    m.occlusion = glm::mix(1.0f, ao.x, glm::clamp(mat.occlusionStrength, 0.0f, 1.0f));

    return m;
}

struct Counters {
    std::uint64_t shadowRays = 0;
    std::uint64_t nonFinite = 0;
    std::uint64_t negative = 0;
};

// Direct lighting at a hit, Lambertian only. Returns outgoing radiance toward the ray's origin.
[[nodiscard]] glm::vec3 directLighting(const Snapshot& snap, const EmbreeScene& embree,
                                       const SurfaceHit& hit, const SurfaceMaterial& m,
                                       const glm::vec3& view, Sampler& sampler, Counters& counters) {
    glm::vec3 sum{0.0f};
    for (const auto& light : snap.lights) {
        const LightSample ls = sampleLight(light, hit.position, sampler);
        if (!ls.valid) continue;

        const float nDotL = glm::dot(hit.shadingNormal, ls.direction);
        if (nDotL <= 0.0f) continue;

        const glm::vec3 f = evaluateBsdf(m, hit.shadingNormal, view, ls.direction);
        if (f.x <= 0.0f && f.y <= 0.0f && f.z <= 0.0f) continue;

        if (light.castsShadow) {
            const float eps = shadowEpsilon(hit.t);
            const float tfar = std::isinf(ls.distance) ? std::numeric_limits<float>::infinity()
                                                       : ls.distance - eps;
            if (tfar > eps) {
                ++counters.shadowRays;
                if (embree.occluded(hit.position + hit.geometricNormal * eps, ls.direction, eps, tfar)) {
                    continue;
                }
            }
        }
        sum += f * ls.radiance * nDotL;
    }
    return sum;
}

// One camera path. Phase 1: emission + direct lighting, then a cosine-weighted bounce per extra
// depth. The cosine PDF cancels the cosine term exactly, which is why the throughput below is
// multiplied by albedo alone -- written out because the cancellation is the thing people get wrong.
[[nodiscard]] glm::vec3 radiance(const Snapshot& snap, const EmbreeScene& embree, Ray ray,
                                 const TraceSettings& settings, Sampler& sampler, Counters& counters) {
    glm::vec3 result{0.0f};
    glm::vec3 throughput{1.0f};

    for (std::uint32_t depth = 0; depth <= settings.maxDepth; ++depth) {
        const SurfaceHit hit =
            embree.intersect(snap, ray.origin, ray.direction, 0.0f, std::numeric_limits<float>::infinity());

        if (!hit.hit) {
            result += throughput * environmentRadiance(snap, ray.direction);
            break;
        }

        const scene::Material& mat = snap.meshes[hit.meshIndex].material;
        const SurfaceMaterial m = resolveMaterial(snap, mat, hit.uv);
        const glm::vec3 view = -ray.direction;

        // Emission. A surface that emits is visible whether or not anything lights it.
        if (m.emission.x > 0.0f || m.emission.y > 0.0f || m.emission.z > 0.0f) {
            result += throughput * m.emission;
        }

        if (mat.unlit) {
            // An unlit material is its base colour and nothing else -- no lighting, no bounce.
            result += throughput * m.baseColor;
            break;
        }

        result += throughput * directLighting(snap, embree, hit, m, view, sampler, counters);

        if (depth == settings.maxDepth) break;

        const BsdfSample bs = sampleBsdf(m, hit.shadingNormal, view, sampler.next2D(), sampler.next1D());
        if (!bs.valid) break;
        if (glm::dot(bs.direction, hit.geometricNormal) <= 0.0f) break; // below the geometric surface
        throughput *= bs.weight;

        const float eps = shadowEpsilon(hit.t);
        ray.origin = hit.position + hit.geometricNormal * eps;
        ray.direction = bs.direction;

        const float maxThroughput = std::max({throughput.x, throughput.y, throughput.z});
        if (maxThroughput <= 1e-6f) break; // nothing further can contribute
    }

    if (settings.debugCheckNonFinite) {
        if (!finite(result)) {
            ++counters.nonFinite;
            result = glm::vec3(0.0f);
        } else if (result.x < 0.0f || result.y < 0.0f || result.z < 0.0f) {
            ++counters.negative;
            result = glm::max(result, glm::vec3(0.0f));
        }
    }
    return result;
}

} // namespace

// ---- settings ------------------------------------------------------------------------------------

Result<void> TraceSettings::validate() const {
    if (width == 0 || height == 0) return fail("pathtrace: resolution must be non-zero (got {}x{})", width, height);
    if (width > 16384 || height > 16384) return fail("pathtrace: resolution {}x{} exceeds 16384", width, height);
    if (samplesPerPixel == 0) return fail("pathtrace: samplesPerPixel must be at least 1");
    if (maxDepth > 64) return fail("pathtrace: maxDepth {} exceeds 64", maxDepth);
    return {};
}

unsigned TraceSettings::resolvedThreads() const {
    if (threads > 0) return threads;
    const unsigned hw = std::thread::hardware_concurrency();
    return hw > 0 ? hw : 1;
}

// ---- framebuffer ---------------------------------------------------------------------------------

void Framebuffer::resize(std::uint32_t w, std::uint32_t h) {
    width = w;
    height = h;
    radiance.assign(static_cast<std::size_t>(w) * h, glm::vec3(0.0f));
    sampleCount = 0;
}

glm::vec3 Framebuffer::pixel(std::uint32_t x, std::uint32_t y) const {
    if (x >= width || y >= height || sampleCount == 0) return glm::vec3(0.0f);
    return radiance[static_cast<std::size_t>(y) * width + x] / static_cast<float>(sampleCount);
}

std::vector<float> Framebuffer::resolveRgba() const {
    std::vector<float> out(static_cast<std::size_t>(width) * height * 4, 0.0f);
    if (sampleCount == 0) return out;
    const float inv = 1.0f / static_cast<float>(sampleCount);
    for (std::size_t i = 0; i < radiance.size(); ++i) {
        out[i * 4 + 0] = radiance[i].x * inv;
        out[i * 4 + 1] = radiance[i].y * inv;
        out[i * 4 + 2] = radiance[i].z * inv;
        out[i * 4 + 3] = 1.0f;
    }
    return out;
}

bool Framebuffer::isBlack() const {
    return std::all_of(radiance.begin(), radiance.end(),
                       [](const glm::vec3& c) { return c.x <= 0.0f && c.y <= 0.0f && c.z <= 0.0f; });
}

double Framebuffer::meanLuminance() const {
    if (radiance.empty() || sampleCount == 0) return 0.0;
    double sum = 0.0;
    for (const auto& c : radiance) {
        sum += 0.2126 * c.x + 0.7152 * c.y + 0.0722 * c.z;
    }
    return sum / (static_cast<double>(radiance.size()) * sampleCount);
}

// ---- render --------------------------------------------------------------------------------------

Result<void> PathTracer::render(const Snapshot& snapshot, const TraceSettings& settings, Framebuffer& out,
                                CancelFn cancel) {
    stats_ = TraceStats{};

    if (auto ok = settings.validate(); !ok) return ok;
    if (snapshot.empty()) {
        // Section 54: fail explicitly rather than quietly delivering a black frame.
        return fail("pathtrace: the snapshot has no geometry; refusing to render a black frame");
    }

    const unsigned threadCount = settings.resolvedThreads();

    const auto buildStart = std::chrono::steady_clock::now();
    EmbreeScene embree;
    if (auto ok = embree.build(snapshot, threadCount); !ok) return ok;
    stats_.buildSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - buildStart).count();

    out.resize(settings.width, settings.height);
    const CameraBasis basis = cameraBasis(snapshot.camera, settings.width, settings.height);

    const auto renderStart = std::chrono::steady_clock::now();
    std::atomic<bool> cancelled{false};
    std::vector<Counters> perThread(threadCount);

    // Banded scanline split, the convention this repo already uses for CPU parallelism
    // (src/world/terrain.cpp, src/world/ecology.cpp). Each row is written by exactly one thread and
    // every pixel's sampler is seeded from its own coordinates, so the image does not depend on how
    // the rows were divided -- which a test asserts by rendering at one thread and at many.
    auto renderRows = [&](unsigned threadIndex) {
        Counters& counters = perThread[threadIndex];
        for (std::uint32_t y = threadIndex; y < settings.height; y += threadCount) {
            if (cancelled.load(std::memory_order_relaxed)) return;
            if (cancel && cancel()) {
                cancelled.store(true, std::memory_order_relaxed);
                return;
            }
            for (std::uint32_t x = 0; x < settings.width; ++x) {
                const std::uint32_t pixelIndex = y * settings.width + x;
                glm::vec3 sum{0.0f};
                for (std::uint32_t s = 0; s < settings.samplesPerPixel; ++s) {
                    Sampler sampler(settings.seed, pixelIndex, s);
                    const glm::vec2 jitter = sampler.next2D();
                    const Ray ray = generateRay(basis, static_cast<float>(x) + jitter.x,
                                                static_cast<float>(y) + jitter.y, settings.width,
                                                settings.height);
                    sum += radiance(snapshot, embree, ray, settings, sampler, counters);
                }
                out.radiance[pixelIndex] = sum;
            }
        }
    };

    if (threadCount <= 1) {
        renderRows(0);
    } else {
        std::vector<std::thread> workers;
        workers.reserve(threadCount - 1);
        for (unsigned i = 1; i < threadCount; ++i) workers.emplace_back(renderRows, i);
        renderRows(0);
        for (auto& t : workers) t.join();
    }

    out.sampleCount = settings.samplesPerPixel;
    stats_.renderSeconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - renderStart).count();
    stats_.primaryRays =
        static_cast<std::uint64_t>(settings.width) * settings.height * settings.samplesPerPixel;
    for (const auto& c : perThread) {
        stats_.shadowRays += c.shadowRays;
        stats_.nonFiniteSamples += c.nonFinite;
        stats_.negativeSamples += c.negative;
    }

    if (stats_.nonFiniteSamples > 0 || stats_.negativeSamples > 0) {
        log::warn("pathtrace: {} non-finite and {} negative radiance samples were clamped",
                  stats_.nonFiniteSamples, stats_.negativeSamples);
    }
    if (cancelled.load()) log::info("pathtrace: render cancelled; partial accumulation kept");
    return {};
}

Result<void> writeFramebufferExr(const Framebuffer& fb, const std::filesystem::path& path, bool half) {
    if (fb.width == 0 || fb.height == 0) return fail("pathtrace: cannot write an empty framebuffer");
    const std::vector<float> rgba = fb.resolveRgba();
    return assets::writeExr(path, fb.width, fb.height, rgba, half);
}

void logCapabilities(const Snapshot& snapshot) {
    log::info("pathtrace: capability report -- {} mesh(es), {} triangle(s), {} light(s)",
              snapshot.meshes.size(), snapshot.triangleCount(), snapshot.lights.size());
    const std::string body = snapshot.capabilities.format();
    if (body.empty()) {
        log::info("  (nothing in this scene is unsupported or approximated)");
    } else {
        for (const auto& line : std::vector<std::string>{body}) log::info("{}", line);
    }
    if (snapshot.capabilities.anyUnsupported()) {
        log::warn("pathtrace: some scene features cannot be represented and were omitted; see the report above");
    }
}

} // namespace avgen::pathtrace
