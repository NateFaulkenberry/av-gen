#include "pathtrace/path_tracer.hpp"

#include "assets/exr.hpp"
#include "core/log.hpp"
#include "pathtrace/bsdf.hpp"
#include "pathtrace/camera.hpp"
#include "pathtrace/lights.hpp"
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

struct Counters {
    std::uint64_t shadowRays = 0;
    std::uint64_t nonFinite = 0;
    std::uint64_t negative = 0;
    std::uint64_t roulette = 0;
    std::uint64_t bsdfHitsOnLights = 0;
};

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
    m.occlusion = glm::mix(1.0f, ao.x, glm::clamp(mat.occlusionStrength, 0.0f, 1.0f));

    return m;
}

// A shadow ray must clear the surface it leaves AND stop short of the surface it aims at. Getting
// the second half wrong is invisible in the code and total in the image.
//
// The bug this replaces: the origin was pushed off the surface by `eps` along the normal, and `tfar`
// was set to `distance - eps` -- but `distance` was measured from the ORIGINAL point, and pushing
// the origin forward already consumes about `eps * (n.dir)` of that budget. For a near-normal
// direction the two cancel to within 1e-5 of the target, which is inside Embree's watertight
// intersection tolerance at metre scale. Measured: 200 of 200 shadow rays to an area emitter
// reported themselves occluded BY THAT EMITTER, so next-event estimation returned zero and the
// emissive light sampler was 135x too dim while BSDF sampling -- which casts no shadow ray -- was
// right. Both strategies looked internally consistent; only comparing them found it.
//
// The fix measures the distance from the offset origin and takes a RELATIVE shortfall, which stays
// far above float precision at every scale.
[[nodiscard]] bool visible(const EmbreeScene& embree, const SurfaceHit& hit, const glm::vec3& dir,
                           float distance, Counters& counters) {
    const float eps = shadowEpsilon(hit.t);
    const glm::vec3 origin = hit.position + hit.geometricNormal * eps;
    if (std::isinf(distance)) {
        ++counters.shadowRays;
        return !embree.occluded(origin, dir, 0.0f, std::numeric_limits<float>::infinity());
    }
    // How far the target actually is from the offset origin.
    const float d = distance - glm::dot(hit.geometricNormal, dir) * eps;
    const float tfar = d * (1.0f - 1e-4f);
    if (!(tfar > 0.0f)) return true;   // the target is inside the epsilon shell
    ++counters.shadowRays;
    return !embree.occluded(origin, dir, 0.0f, tfar);
}

// ---- next-event estimation, with MIS (spec sections 43-45) ---------------------------------------
//
// Two kinds of emitter, deliberately treated differently:
//
//   * ANALYTIC lights (`scene::PunctualLight`) have no geometry in the BVH. A BSDF ray can never
//     hit one, so light sampling is the ONLY strategy that can find them and it takes MIS weight 1.
//     Weighting them against the BSDF's density would down-weight the only estimator that works and
//     lose energy that nothing else supplies.
//   * EMISSIVE GEOMETRY can be hit. Both strategies find it, so both get a power-heuristic weight
//     and the two halves sum to 1 for every direction.
[[nodiscard]] glm::vec3 nextEventEstimate(const Snapshot& snap, const EmbreeScene& embree,
                                          const SurfaceHit& hit, const SurfaceMaterial& m,
                                          const glm::vec3& view, Sampler& sampler,
                                          const TraceSettings& settings, Counters& counters) {
    glm::vec3 sum{0.0f};
    if (settings.strategy == TraceSettings::Strategy::BsdfOnly) return sum;

    // --- analytic lights: weight 1, always ---
    for (const auto& light : snap.lights) {
        const LightSample ls = sampleLight(light, hit.position, sampler.next2D());
        if (!ls.valid) continue;
        const float nDotL = glm::dot(hit.shadingNormal, ls.direction);
        if (nDotL <= 0.0f) continue;
        const glm::vec3 f = evaluateBsdf(m, hit.shadingNormal, view, ls.direction);
        if (f.x <= 0.0f && f.y <= 0.0f && f.z <= 0.0f) continue;
        if (light.castsShadow && !visible(embree, hit, ls.direction, ls.distance, counters)) continue;

        if (ls.delta) {
            sum += f * ls.radiance * nDotL;
        } else {
            // An analytic AREA light still has a density, but no BSDF sample can find it, so the
            // estimator is f * L * cos / pdf with no MIS weight.
            if (ls.pdf > 0.0f) sum += f * ls.radiance * nDotL / ls.pdf;
        }
    }

    // --- emissive geometry: MIS against BSDF sampling ---
    if (!snap.emissiveTriangles.empty()) {
        const EmissiveSample es = sampleEmissive(snap, hit.position, sampler.next2D(), sampler.next1D());
        if (es.valid) {
            const float nDotL = glm::dot(hit.shadingNormal, es.direction);
            if (nDotL > 0.0f) {
                const glm::vec3 f = evaluateBsdf(m, hit.shadingNormal, view, es.direction);
                if (f.x > 0.0f || f.y > 0.0f || f.z > 0.0f) {
                    if (visible(embree, hit, es.direction, es.distance, counters)) {
                        const float bPdf = bsdfPdf(m, hit.shadingNormal, view, es.direction);
                        const float w = settings.strategy == TraceSettings::Strategy::LightOnly
                                            ? 1.0f
                                            : powerHeuristic(es.pdf, bPdf);
                        sum += f * es.radiance * nDotL * (w / es.pdf);
                    }
                }
            }
        }
    }
    return sum;
}

// One camera path.
[[nodiscard]] glm::vec3 radiance(const Snapshot& snap, const EmbreeScene& embree, Ray ray,
                                 const TraceSettings& settings, Sampler& sampler, Counters& counters) {
    glm::vec3 result{0.0f};
    glm::vec3 throughput{1.0f};

    // Carried from the previous bounce so an emitter found by a BSDF ray can be MIS-weighted
    // against the light sampler that could also have found it. `specularBounce` starts true because
    // a camera ray has no preceding BSDF density -- whatever it hits is seen directly and takes
    // weight 1. Getting that wrong makes emitters invisible to the camera, which is very obvious,
    // or double counted, which is not.
    float prevBsdfPdf = 0.0f;
    bool takeFullEmission = true;

    for (std::uint32_t depth = 0;; ++depth) {
        const SurfaceHit hit =
            embree.intersect(snap, ray.origin, ray.direction, 0.0f, std::numeric_limits<float>::infinity());

        if (!hit.hit) {
            result += throughput * environmentRadiance(snap, ray.direction);
            break;
        }

        const scene::Material& mat = snap.meshes[hit.meshIndex].material;
        const SurfaceMaterial m = resolveMaterial(snap, mat, hit.uv);
        const glm::vec3 view = -ray.direction;

        // --- emission, MIS-weighted against the light sampler that could also have found it ---
        if (m.emission.x > 0.0f || m.emission.y > 0.0f || m.emission.z > 0.0f) {
            float w = 1.0f;
            if (!takeFullEmission && settings.strategy != TraceSettings::Strategy::BsdfOnly) {
                const float lPdf = emissivePdf(snap, hit.meshIndex, hit.primIndex, ray.direction, hit.t);
                if (lPdf > 0.0f) {
                    ++counters.bsdfHitsOnLights;
                    // LightOnly must drop this entirely: the light sampler already counted this
                    // emitter, and adding the BSDF path too would double count it.
                    w = settings.strategy == TraceSettings::Strategy::LightOnly
                            ? 0.0f
                            : powerHeuristic(prevBsdfPdf, lPdf);
                }
            }
            result += throughput * m.emission * w;
        }

        if (mat.unlit) {
            result += throughput * m.baseColor;
            break;
        }

        result += throughput * nextEventEstimate(snap, embree, hit, m, view, sampler, settings, counters);

        if (depth >= settings.maxDepth) break;

        // --- extend the path ---
        const BsdfSample bs = sampleBsdf(m, hit.shadingNormal, view, sampler.next2D(), sampler.next1D());
        if (!bs.valid) break;
        if (glm::dot(bs.direction, hit.geometricNormal) <= 0.0f) break;
        throughput *= bs.weight;
        prevBsdfPdf = bs.pdf;
        takeFullEmission = false;

        // --- Russian roulette (spec section 50) ---
        //
        // Terminate with probability (1 - q) and divide the survivors by q. The division is what
        // keeps the estimator unbiased: without it, roulette removes energy and the image gets
        // DARKER while also getting less noisy, which reads as an improvement and is not one.
        // `russianRouletteCompensation` exists only so a test can set it wrong and watch the energy
        // check fail; it is 1.0 everywhere else.
        if (settings.russianRouletteDepth > 0 && depth + 1 >= settings.russianRouletteDepth) {
            const float q = std::clamp(std::max({throughput.x, throughput.y, throughput.z}), 0.02f, 0.95f);
            if (sampler.next1D() >= q) {
                ++counters.roulette;
                break;
            }
            throughput /= (q * settings.russianRouletteCompensation);
        }

        const float eps = shadowEpsilon(hit.t);
        ray.origin = hit.position + hit.geometricNormal * eps;
        ray.direction = bs.direction;

        if (std::max({throughput.x, throughput.y, throughput.z}) <= 1e-6f) break;
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
        stats_.pathsTerminatedByRoulette += c.roulette;
        stats_.bsdfHitsOnLights += c.bsdfHitsOnLights;
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
