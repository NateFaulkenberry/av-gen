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
    AlbedoProbeReport probe;
    float pixelWorstAlbedo = 0.0f;
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
// The probe, called AFTER every shading decision at this vertex has been made and taken. It reads
// `m`, `n` and `v`, all of which are already fixed, and writes only into `counters.probe`. It draws
// from its own generator seeded on (probeSeed, pixel, sample, depth) and never touches `sampler`,
// which is what makes the image bit-identical with it on and off.
void probeAlbedo(const AlbedoProbeSettings& cfg, const SurfaceMaterial& m, const glm::vec3& n,
                 const glm::vec3& v, std::uint32_t depth, std::uint32_t materialId,
                 std::uint32_t pixelIndex, std::uint32_t sampleIndex, Counters& counters) {
    // Stride on a hash of the event's identity rather than a running counter: a counter would make
    // WHICH hits get measured depend on how rows were split across threads, and the report would
    // change from run to run for no reason.
    const std::uint64_t h = mixSeed((static_cast<std::uint64_t>(pixelIndex) << 24) ^
                                    (static_cast<std::uint64_t>(sampleIndex) << 8) ^ depth ^ cfg.seed);
    if (cfg.hitStride > 1 && (h % cfg.hitStride) != 0) return;

    const AlbedoEstimate est = estimateDirectionalAlbedo(m, n, v, cfg.integralSamples, h);
    const float albedo = est.mean;
    ++counters.probe.hitsProbed;

    auto& mats = counters.probe.materials;
    auto it = std::find_if(mats.begin(), mats.end(),
                           [&](const AlbedoProbeMaterial& e) { return e.materialId == materialId; });
    if (it == mats.end()) {
        mats.push_back(AlbedoProbeMaterial{});
        it = mats.end() - 1;
        it->materialId = materialId;
    }
    ++it->probed;

    // Statistically significant, not merely numerically above. See AlbedoProbeSettings::sigmaGate.
    if (albedo - cfg.sigmaGate * est.stdError <= cfg.threshold) return;

    ++counters.probe.exceedances;
    ++it->exceedances;
    it->exceedancesByDepth[std::min<std::size_t>(depth, kProbeMaxDepthBuckets - 1)]++;
    // Record the confident lower bound, not the raw estimate: a max over many noisy means is an
    // outlier of the estimator rather than of the BRDF. See AlbedoProbeMaterial::worstAlbedo.
    const float bound = albedo - cfg.sigmaGate * est.stdError;
    counters.probe.worstAlbedo = std::max(counters.probe.worstAlbedo, bound);
    counters.pixelWorstAlbedo = std::max(counters.pixelWorstAlbedo, bound);
    if (bound > it->worstAlbedo) {
        it->worstAlbedo = bound;
        it->worstAlbedoEstimate = albedo;
        it->worstViewCos = glm::dot(n, v);
        it->worstDepth = depth;
        it->worstRoughness = m.roughness;
        it->worstMetallic = m.metallic;
        it->worstBaseColor = m.baseColor;
    }
}

struct PathResult {
    glm::vec3 radiance{0.0f};
    glm::vec3 albedo{0.0f};
    glm::vec3 normal{0.0f};
    glm::vec3 emission{0.0f};
    float depth = -1.0f;      // view-space metres; negative means the ray hit nothing
    float objectId = -1.0f;
    glm::vec2 motion{0.0f};   // pixels
};

[[nodiscard]] PathResult radiance(const Snapshot& snap, const EmbreeScene& embree, Ray ray,
                                  const TraceSettings& settings, Sampler& sampler, Counters& counters,
                                  const glm::vec3& viewOrigin, const glm::vec3& viewForward,
                                  std::uint32_t pixelIndex, std::uint32_t sampleIndex,
                                  const CameraBasis& currentBasis, const CameraBasis& previousBasis,
                                  std::uint32_t imageWidth, std::uint32_t imageHeight) {
    PathResult path;
    glm::vec3 result{0.0f};
    glm::vec3 throughput{1.0f};
    bool firstHitRecorded = false;

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
            const glm::vec3 env = environmentRadiance(snap, ray.direction);
            result += throughput * env;
            if (!firstHitRecorded) {
                // The background IS the albedo the denoiser should see for a pixel that hit nothing:
                // giving it black would tell the filter there is an edge where there is only sky.
                path.albedo = env;
                path.normal = -ray.direction;
                path.depth = -1.0f;      // a miss has no depth; 0 would read as "at the camera"
                path.objectId = -1.0f;
                firstHitRecorded = true;
            }
            break;
        }

        // A hit can come from either list, so never index `meshes` directly.
        const scene::Material& mat = EmbreeScene::materialOf(snap, hit);
        const SurfaceMaterial m = resolveMaterial(snap, mat, hit.uv);
        const glm::vec3 view = -ray.direction;

        if (!firstHitRecorded) {
            // A metal's "albedo" for denoising purposes is its F0, which is its base colour; a
            // dielectric's is its diffuse colour. Emission is added so an emitter does not look
            // like a black hole in the albedo buffer and get smoothed into its surroundings.
            path.albedo = m.diffuseAlbedo() + m.f0() * m.metallic + m.emission;
            path.normal = hit.shadingNormal;
            path.emission = m.emission;
            // View-space depth in metres, matching what `linearDepthTexture` holds, NOT the ray's
            // t: t is the distance travelled and grows toward the corners of the frame even across
            // a flat wall, which makes a depth pass that looks curved.
            path.depth = glm::dot(hit.position - viewOrigin, viewForward);
            path.objectId = static_cast<float>(
                scene::packPickId(scene::PickSpace::Entity,
                                  hit.instanced ? snap.instanced[hit.meshIndex].source.entityIndex
                                                : snap.meshes[hit.meshIndex].entityIndex));

            // Motion: the same surface POINT, one frame earlier, projected with the camera it was
            // seen by then. Interpolating the previous positions with the CURRENT barycentrics is
            // what makes it the same point on the surface rather than the same screen position.
            // Motion is only tracked for non-instanced entities: a scattered instance's previous
            // transform is not carried, and pairing instances between frames by index would be a
            // guess. Reported rather than silently zero.
            const TriangleMesh* hmPtr = hit.instanced ? nullptr : &snap.meshes[hit.meshIndex];
            if (snap.hasMotion && hmPtr != nullptr && !hmPtr->previousPositions.empty()) {
                const TriangleMesh& hm = *hmPtr;
                const std::size_t tri = static_cast<std::size_t>(hit.primIndex) * 3;
                if (tri + 2 < hm.indices.size()) {
                    const glm::vec3 prev = hm.previousPositions[hm.indices[tri + 0]] * hit.baryW +
                                           hm.previousPositions[hm.indices[tri + 1]] * hit.baryU +
                                           hm.previousPositions[hm.indices[tri + 2]] * hit.baryV;
                    glm::vec2 nowPx{};
                    glm::vec2 thenPx{};
                    if (projectToPixel(currentBasis, hit.position, imageWidth, imageHeight, nowPx) &&
                        projectToPixel(previousBasis, prev, imageWidth, imageHeight, thenPx)) {
                        path.motion = nowPx - thenPx;
                    }
                }
            }
            firstHitRecorded = true;
        }

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

        // Diagnostic only (ADR-352). Placed here, after emission and next-event estimation have
        // been added and before nothing that depends on them: it reads state that is already final.
        if (settings.albedoProbe.enabled) {
            probeAlbedo(settings.albedoProbe, m, hit.shadingNormal, view, depth,
                        static_cast<std::uint32_t>(scene::packPickId(
                            scene::PickSpace::Entity,
                            hit.instanced ? snap.instanced[hit.meshIndex].source.entityIndex
                                          : snap.meshes[hit.meshIndex].entityIndex)),
                        pixelIndex, sampleIndex, counters);
        }

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
    path.radiance = result;
    return path;
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
    albedo.clear();
    normal.clear();
    worstAlbedo.clear();
    emission.clear();
    motion.clear();
    depth.clear();
    objectId.clear();
    sampleCount = 0;
}

std::vector<glm::vec3> Framebuffer::resolvedRadiance() const {
    std::vector<glm::vec3> out(radiance.size(), glm::vec3(0.0f));
    if (sampleCount == 0) return out;
    const float inv = 1.0f / static_cast<float>(sampleCount);
    for (std::size_t i = 0; i < radiance.size(); ++i) out[i] = radiance[i] * inv;
    return out;
}

std::vector<glm::vec3> Framebuffer::resolvedAlbedo() const {
    std::vector<glm::vec3> out(albedo.size(), glm::vec3(0.0f));
    if (sampleCount == 0) return out;
    const float inv = 1.0f / static_cast<float>(sampleCount);
    for (std::size_t i = 0; i < albedo.size(); ++i) out[i] = albedo[i] * inv;
    return out;
}

std::vector<glm::vec3> Framebuffer::resolvedEmission() const {
    std::vector<glm::vec3> out(emission.size(), glm::vec3(0.0f));
    if (sampleCount == 0) return out;
    const float inv = 1.0f / static_cast<float>(sampleCount);
    for (std::size_t i = 0; i < emission.size(); ++i) out[i] = emission[i] * inv;
    return out;
}

std::vector<glm::vec3> Framebuffer::resolvedMotion() const {
    std::vector<glm::vec3> out(motion.size(), glm::vec3(0.0f));
    if (sampleCount == 0) return out;
    const float inv = 1.0f / static_cast<float>(sampleCount);
    for (std::size_t i = 0; i < motion.size(); ++i) out[i] = motion[i] * inv;
    return out;
}

std::vector<glm::vec3> Framebuffer::resolvedNormal() const {
    std::vector<glm::vec3> out(normal.size(), glm::vec3(0.0f));
    if (sampleCount == 0) return out;
    for (std::size_t i = 0; i < normal.size(); ++i) {
        // Averaged normals are shorter than unit; renormalise so the denoiser gets a direction.
        const float len = glm::length(normal[i]);
        out[i] = len > 1e-6f ? normal[i] / len : glm::vec3(0.0f);
    }
    return out;
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
                                CancelFn cancel, ProgressFn progress) {
    stats_ = TraceStats{};

    if (auto ok = settings.validate(); !ok) return ok;
    if (snapshot.empty()) {
        // Section 54: fail explicitly rather than quietly delivering a black frame.
        return fail("pathtrace: the snapshot has no geometry; refusing to render a black frame");
    }

    const unsigned threadCount = settings.resolvedThreads();

    if (stage_) stage_("acceleration");
    const auto buildStart = std::chrono::steady_clock::now();
    if (auto ok = embree_.update(snapshot, threadCount,
                                 settings.reuseAcceleration ? BvhReuse::Detect : BvhReuse::Rebuild);
        !ok) {
        embree_.reset();   // a half-updated structure must never be traced by the next render
        return ok;
    }
    const EmbreeScene& embree = embree_;
    stats_.buildSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - buildStart).count();
    stats_.bvh = embree_.lastUpdate();

    out.resize(settings.width, settings.height);
    if (settings.albedoProbe.enabled) {
        out.worstAlbedo.assign(static_cast<std::size_t>(settings.width) * settings.height, 0.0f);
    }
    if (settings.captureFeatures) {
        const std::size_t n = static_cast<std::size_t>(settings.width) * settings.height;
        out.albedo.assign(n, glm::vec3(0.0f));
        out.normal.assign(n, glm::vec3(0.0f));
        out.emission.assign(n, glm::vec3(0.0f));
        if (snapshot.hasMotion) out.motion.assign(n, glm::vec3(0.0f));
        out.depth.assign(n, -1.0f);
        out.objectId.assign(n, -1.0f);
    }
    const CameraBasis basis = cameraBasis(snapshot.camera, settings.width, settings.height);
    const CameraBasis prevBasis = snapshot.hasMotion
                                      ? cameraBasis(snapshot.previousCamera, settings.width, settings.height)
                                      : basis;

    if (stage_) stage_("rendering");
    const auto renderStart = std::chrono::steady_clock::now();
    std::atomic<bool> cancelled{false};
    std::vector<Counters> perThread(threadCount);

    // Banded scanline split, the convention this repo already uses for CPU parallelism
    // (src/world/terrain.cpp, src/world/ecology.cpp). Each row is written by exactly one thread and
    // every pixel's sampler is seeded from its own coordinates, so the image does not depend on how
    // the rows were divided -- which a test asserts by rendering at one thread and at many.
    std::uint32_t batchFirst = 0;
    std::uint32_t batchLast = 0;

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
                counters.pixelWorstAlbedo = 0.0f;
                glm::vec3 sum{0.0f};
                glm::vec3 albedoSum{0.0f};
                glm::vec3 normalSum{0.0f};
                glm::vec3 emissionSum{0.0f};
                glm::vec2 motionSum{0.0f};
                for (std::uint32_t s = batchFirst; s < batchLast; ++s) {
                    Sampler sampler(settings.seed, pixelIndex, s);
                    const glm::vec2 jitter = sampler.next2D();
                    const Ray ray = generateRay(basis, static_cast<float>(x) + jitter.x,
                                                static_cast<float>(y) + jitter.y, settings.width,
                                                settings.height);
                    const PathResult p = radiance(snapshot, embree, ray, settings, sampler, counters,
                                                  basis.origin, basis.forward, pixelIndex, s,
                                                  basis, prevBasis, settings.width, settings.height);
                    sum += p.radiance;
                    if (settings.captureFeatures) {
                        albedoSum += p.albedo;
                        normalSum += p.normal;
                        emissionSum += p.emission;
                        motionSum += p.motion;
                        if (s == 0) {
                            // First sample only: a mean of two depths at a silhouette is a distance
                            // to nothing, and a mean of two ids is a third object.
                            out.depth[pixelIndex] = p.depth;
                            out.objectId[pixelIndex] = p.objectId;
                        }
                    }
                }
                // ACCUMULATE, never assign: with batching this runs once per batch and the pixel
                // must keep what earlier batches put there.
                out.radiance[pixelIndex] += sum;
                if (settings.albedoProbe.enabled) {
                    out.worstAlbedo[pixelIndex] =
                        std::max(out.worstAlbedo[pixelIndex], counters.pixelWorstAlbedo);
                }
                if (settings.captureFeatures) {
                    out.albedo[pixelIndex] += albedoSum;
                    out.normal[pixelIndex] += normalSum;
                    out.emission[pixelIndex] += emissionSum;
                    if (snapshot.hasMotion) out.motion[pixelIndex] += glm::vec3(motionSum, 0.0f);
                }
            }
        }
    };

    // Sample batches (spec section 30). Each batch renders a contiguous range of sample indices
    // for every pixel, so progress is a count of finished samples and cancellation lands between
    // batches. The worker threads are created and JOINED inside each batch: they exist only while a
    // batch is running, so this is not a second long-lived pool competing with anything (spec
    // section 36) -- it is the same banded split src/world/terrain.cpp uses, run repeatedly.
    const std::uint32_t batchSize = std::max(1u, settings.samplesPerBatch);
    for (std::uint32_t done = 0; done < settings.samplesPerPixel;) {
        batchFirst = done;
        batchLast = std::min(settings.samplesPerPixel, done + batchSize);

        if (threadCount <= 1) {
            renderRows(0);
        } else {
            std::vector<std::thread> workers;
            workers.reserve(threadCount - 1);
            for (unsigned i = 1; i < threadCount; ++i) workers.emplace_back(renderRows, i);
            renderRows(0);
            for (auto& t : workers) t.join();
        }

        if (cancelled.load()) break;
        done = batchLast;
        out.sampleCount = done;
        if (progress) progress(done, settings.samplesPerPixel);
    }
    stats_.renderSeconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - renderStart).count();
    stats_.primaryRays =
        static_cast<std::uint64_t>(settings.width) * settings.height * settings.samplesPerPixel;
    probe_ = AlbedoProbeReport{};
    probe_.integralSamples = settings.albedoProbe.integralSamples;
    probe_.hitStride = settings.albedoProbe.hitStride;
    probe_.sigmaGate = settings.albedoProbe.sigmaGate;
    for (const auto& c : perThread) {
        probe_.merge(c.probe);
    }
    if (settings.albedoProbe.enabled && probe_.any()) {
        log::warn("pathtrace: directional albedo exceeded 1 at {} of {} measured shading events "
                  "(worst {:.3f}). The glTF BRDF is faithful to spec and gains at grazing; see ADR-352.",
                  probe_.exceedances, probe_.hitsProbed, probe_.worstAlbedo);
        // ADR-372: and the per-material breakdown, which until now was reachable only from a test.
        // `AlbedoProbeReport::format()` -- material, worst, exceedances, view angle, by-depth
        // histogram -- was written for ADR-352 and called from `test_pathtrace_albedo_probe.cpp`
        // and nowhere else, so a person running `--pt-probe` got the one-line summary above and
        // none of the detail the ADR describes. A report only a test can read is not a report.
        log::info("pathtrace: per-material directional albedo (ADR-352)\n{}", probe_.format());
    }
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

Result<void> writeFramebufferAovExr(const Framebuffer& fb, const std::filesystem::path& path) {
    if (fb.width == 0 || fb.height == 0) return fail("pathtrace: cannot write an empty framebuffer");
    const std::size_t pixels = static_cast<std::size_t>(fb.width) * fb.height;

    // tinyexr wants planar channels, so the interleaved vec3 buffers are deinterleaved once here.
    std::vector<std::vector<float>> planes;
    std::vector<assets::ExrChannel> channels;

    const auto addVec3 = [&](const std::vector<glm::vec3>& src, const char* c0, const char* c1,
                             const char* c2, bool half) {
        if (src.size() != pixels) return;
        const std::size_t base = planes.size();
        planes.emplace_back(pixels);
        planes.emplace_back(pixels);
        planes.emplace_back(pixels);
        for (std::size_t i = 0; i < pixels; ++i) {
            planes[base + 0][i] = src[i].x;
            planes[base + 1][i] = src[i].y;
            planes[base + 2][i] = src[i].z;
        }
        channels.push_back({c0, {}, half});
        channels.push_back({c1, {}, half});
        channels.push_back({c2, {}, half});
    };

    const std::vector<glm::vec3> colour = fb.resolvedRadiance();
    // Beauty is R/G/B: it IS colour, and every compositor expects to find it under those names.
    // Float, not half: the emissive range in a path trace routinely exceeds half's precision where
    // it matters, and this is the deliverable.
    addVec3(colour, "R", "G", "B", false);

    const std::vector<glm::vec3> albedo = fb.resolvedAlbedo();
    if (!albedo.empty()) addVec3(albedo, "albedo.R", "albedo.G", "albedo.B", true);

    const std::vector<glm::vec3> normal = fb.resolvedNormal();
    // `normal.X/Y/Z`, never `normal.R/G/B`: a direction is not a colour (spec section 33). Half is
    // enough for a unit vector and is what every renderer writes.
    if (!normal.empty()) addVec3(normal, "normal.X", "normal.Y", "normal.Z", true);

    const std::vector<glm::vec3> emissionAov = fb.resolvedEmission();
    if (!emissionAov.empty()) addVec3(emissionAov, "emission.R", "emission.G", "emission.B", true);

    // Depth and id are single channels and both are FLOAT, never half: half quantises depth
    // visibly past a few hundred metres, and an id is an integer that must survive exactly.
    const auto addScalar = [&](const std::vector<float>& src, const char* name) {
        if (src.size() != pixels) return;
        planes.emplace_back(src);
        channels.push_back({name, {}, false});
    };
    const std::vector<glm::vec3> motionAov = fb.resolvedMotion();
    if (!motionAov.empty()) {
        // `motion.X/Y`, never `motion.R/G` -- a displacement is not a colour (spec section 33).
        // Float, because a motion vector can be tens of pixels and half quantises it visibly.
        addVec3(motionAov, "motion.X", "motion.Y", "motion.Z", false);
    }

    addScalar(fb.rawDepth(), "depth.Z");
    addScalar(fb.rawObjectId(), "id.X");
    // ADR-352's per-pixel diagnostic, present only when `--pt-probe` ran. `.X` and not `.R`: a
    // directional albedo is a ratio, not a colour (spec section 33), and a compositor that colour-
    // manages it would be transforming a number. Until now this buffer was filled every probe run
    // and written nowhere, so the "points at the offending region" the header promises could not be
    // looked at; the aggregate table says a material gains energy and this says which pixels.
    addScalar(fb.rawWorstAlbedo(), "worstAlbedo.X");

    // Bind the spans only once `planes` has stopped growing, or a reallocation dangles them.
    for (std::size_t i = 0; i < channels.size(); ++i) {
        channels[i].data = std::span<const float>(planes[i]);
    }
    return assets::writeExrLayers(path, fb.width, fb.height, channels);
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
