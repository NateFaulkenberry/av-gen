#include "pathtrace/albedo_probe.hpp"

#include "core/rng.hpp"
#include "pathtrace/sampler.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>

namespace avgen::pathtrace {

AlbedoEstimate estimateDirectionalAlbedo(const SurfaceMaterial& m, const glm::vec3& n,
                                         const glm::vec3& v, std::uint32_t samples,
                                         std::uint64_t seed) {
    AlbedoEstimate out;
    if (samples == 0) return out;
    if (glm::dot(n, v) <= 0.0f) return out;

    // The probe's own generator. Deterministic in `seed`, so the diagnostic is reproducible, and
    // entirely separate from the integrator's `Sampler` so it cannot perturb the image.
    Rng rng(mixSeed(seed), mixSeed(seed ^ 0x9E3779B97F4A7C15ULL));

    // Estimated with the BSDF's OWN importance sampler, not cosine-hemisphere sampling.
    //
    // This is not a refinement, it is a correctness fix, and the first version of this probe got it
    // wrong in both directions at once. Cosine sampling barely finds a roughness-0.1 specular lobe,
    // so it reported the known 1.676 grazing case as **0.954** -- under-reporting the exact
    // situation the probe exists to catch -- while the rare samples that DID land in the lobe were
    // divided by a tiny cosine density and produced spikes up to **586**. Too low where it matters
    // and absurd where it does not. `sampleBsdf` draws from the lobe it is measuring, so the
    // estimator has low variance where the integrand is large, and its numbers match ADR-349's
    // table, which is the whole point of the instrument.
    double sum = 0.0;
    double sumSq = 0.0;
    for (std::uint32_t i = 0; i < samples; ++i) {
        const glm::vec2 u{rng.nextFloat(), rng.nextFloat()};
        const float pick = rng.nextFloat();
        const BsdfSample bs = sampleBsdf(m, n, v, u, pick);
        if (!bs.valid) continue;   // a lost sample is a real zero, not a skipped one
        // Luminance-weighted, so one report line covers a coloured material rather than three.
        const double w = 0.2126 * bs.weight.x + 0.7152 * bs.weight.y + 0.0722 * bs.weight.z;
        sum += w;
        sumSq += w * w;
    }
    const double n_ = samples;
    const double mean = sum / n_;
    const double variance = std::max(0.0, sumSq / n_ - mean * mean);
    out.mean = static_cast<float>(mean);
    out.stdError = static_cast<float>(std::sqrt(variance / n_));
    return out;
}

float directionalAlbedoAt(const SurfaceMaterial& m, const glm::vec3& n, const glm::vec3& v,
                          std::uint32_t samples, std::uint64_t seed) {
    return estimateDirectionalAlbedo(m, n, v, samples, seed).mean;
}

void AlbedoProbeReport::merge(const AlbedoProbeReport& other) {
    hitsProbed += other.hitsProbed;
    exceedances += other.exceedances;
    worstAlbedo = std::max(worstAlbedo, other.worstAlbedo);
    if (other.integralSamples != 0) integralSamples = other.integralSamples;
    if (other.hitStride != 0) hitStride = other.hitStride;
    if (other.sigmaGate != 0.0f) sigmaGate = other.sigmaGate;
    for (const auto& om : other.materials) {
        auto it = std::find_if(materials.begin(), materials.end(),
                               [&](const AlbedoProbeMaterial& m) { return m.materialId == om.materialId; });
        if (it == materials.end()) {
            materials.push_back(om);
            continue;
        }
        it->probed += om.probed;
        it->exceedances += om.exceedances;
        for (std::size_t d = 0; d < kProbeMaxDepthBuckets; ++d) {
            it->exceedancesByDepth[d] += om.exceedancesByDepth[d];
        }
        if (om.worstAlbedo > it->worstAlbedo) {
            it->worstAlbedo = om.worstAlbedo;
            it->worstAlbedoEstimate = om.worstAlbedoEstimate;
            it->worstViewCos = om.worstViewCos;
            it->worstDepth = om.worstDepth;
            it->worstRoughness = om.worstRoughness;
            it->worstMetallic = om.worstMetallic;
            it->worstBaseColor = om.worstBaseColor;
        }
    }
}

std::string AlbedoProbeReport::format() const {
    std::string out;
    out += fmt::format(
        "pathtrace: directional-albedo probe -- {} shading event(s) measured (1 in {}), {} integral "
        "sample(s) each; {} exceeded {:.3f} by more than {:.1f} standard errors\n",
        hitsProbed, hitStride, integralSamples, exceedances, 1.0f, sigmaGate);
    out += "  This is the TRUE hemispherical integral of f*cos at a sampled subset of hits, not a\n"
           "  throughput-growth proxy. 'worst' is a CONFIDENT LOWER BOUND (mean - sigma*stderr), so\n"
           "  the BRDF really does reach it. The glTF BRDF is faithful to spec and gains at grazing\n"
           "  (ADR-349); these are measurements of that, not of a bug in the integrator.\n";
    if (materials.empty()) {
        out += "  (nothing exceeded the threshold)\n";
        return out;
    }

    // Worst first: the material most worth looking at should not be buried.
    std::vector<AlbedoProbeMaterial> sorted = materials;
    std::sort(sorted.begin(), sorted.end(),
              [](const AlbedoProbeMaterial& a, const AlbedoProbeMaterial& b) {
                  return a.worstAlbedo > b.worstAlbedo;
              });

    out += fmt::format("  {:>10} {:>8} {:>10} {:>7} {:>8} {:>6} {:>6}  {}\n", "material", "worst",
                       "exceed/probed", "n.v", "rough", "metal", "depth", "baseColor");
    for (const auto& m : sorted) {
        if (m.exceedances == 0) continue;
        out += fmt::format("  {:>10} {:>8.3f} {:>5}/{:<6} {:>7.3f} {:>8.3f} {:>6.2f} {:>6}  "
                           "({:.2f},{:.2f},{:.2f})\n",
                           m.materialId, m.worstAlbedo, m.exceedances, m.probed, m.worstViewCos,
                           m.worstRoughness, m.worstMetallic, m.worstDepth, m.worstBaseColor.x,
                           m.worstBaseColor.y, m.worstBaseColor.z);
        // The depth distribution is the field that makes this actionable: the gain compounds, so
        // "1.02 at depth 1 and 3.4 at depth 6" is a different story from a flat 1.68 everywhere.
        std::string dist;
        for (std::size_t d = 0; d < kProbeMaxDepthBuckets; ++d) {
            if (m.exceedancesByDepth[d] == 0) continue;
            dist += fmt::format(" d{}={}", d, m.exceedancesByDepth[d]);
        }
        if (!dist.empty()) out += fmt::format("             by depth:{}\n", dist);
    }
    return out;
}

} // namespace avgen::pathtrace
