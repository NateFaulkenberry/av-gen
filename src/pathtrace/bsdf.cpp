#include "pathtrace/bsdf.hpp"

#include "pathtrace/sampler.hpp"

#include <algorithm>
#include <cmath>

namespace avgen::pathtrace {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kInvPi = 0.31830988618379067f;
constexpr float kEps = 1e-6f;

// The probability of picking the specular lobe. A luminance-weighted Fresnel at normal incidence is
// a cheap, unbiased-in-expectation choice: it is only a sampling decision, and the combined PDF
// below undoes whatever it picks. Clamped away from 0 and 1 so neither lobe can become unreachable.
[[nodiscard]] float specularProbability(const SurfaceMaterial& m) {
    const glm::vec3 f0 = m.f0();
    const float spec = 0.2126f * f0.x + 0.7152f * f0.y + 0.0722f * f0.z;
    const glm::vec3 d = m.diffuseAlbedo();
    const float diff = 0.2126f * d.x + 0.7152f * d.y + 0.0722f * d.z;
    const float total = spec + diff;
    if (total <= kEps) return 0.5f;
    return std::clamp(spec / total, 0.1f, 0.9f);
}

} // namespace

float distributionGGX(float nDotH, float alpha) {
    if (nDotH <= 0.0f) return 0.0f;
    const float a2 = alpha * alpha;
    const float d = nDotH * nDotH * (a2 - 1.0f) + 1.0f;
    if (d <= 0.0f) return 0.0f;
    return a2 / (kPi * d * d);
}

float visibilitySmithGGX(float nDotV, float nDotL, float alpha) {
    if (nDotV <= 0.0f || nDotL <= 0.0f) return 0.0f;
    const float a2 = alpha * alpha;
    // Heitz's height-correlated form. The 0.5 / (lv + ll) already contains the 1/(4 nv nl).
    const float lv = nDotL * std::sqrt(nDotV * nDotV * (1.0f - a2) + a2);
    const float ll = nDotV * std::sqrt(nDotL * nDotL * (1.0f - a2) + a2);
    const float denom = lv + ll;
    return denom > 0.0f ? 0.5f / denom : 0.0f;
}

glm::vec3 fresnelSchlick(float vDotH, const glm::vec3& f0) {
    const float f = std::pow(std::clamp(1.0f - vDotH, 0.0f, 1.0f), 5.0f);
    return f0 + (glm::vec3(1.0f) - f0) * f;
}

glm::vec3 evaluateBsdf(const SurfaceMaterial& m, const glm::vec3& n, const glm::vec3& v,
                       const glm::vec3& l) {
    const float nDotL = glm::dot(n, l);
    const float nDotV = glm::dot(n, v);
    if (nDotL <= 0.0f || nDotV <= 0.0f) return glm::vec3(0.0f);

    const glm::vec3 h = glm::normalize(v + l);
    const float nDotH = std::max(0.0f, glm::dot(n, h));
    const float vDotH = std::max(0.0f, glm::dot(v, h));

    const float alpha = m.alpha();
    const glm::vec3 F = fresnelSchlick(vDotH, m.f0());
    const float D = distributionGGX(nDotH, alpha);
    const float V = visibilitySmithGGX(nDotV, nDotL, alpha);

    const glm::vec3 specular = F * (D * V);
    // Energy that the specular lobe reflected is not available to the diffuse one. This is the
    // glTF spec's own combination: diffuse is scaled by (1 - F).
    const glm::vec3 diffuse = (glm::vec3(1.0f) - F) * m.diffuseAlbedo() * kInvPi;
    return diffuse + specular;
}

float bsdfPdf(const SurfaceMaterial& m, const glm::vec3& n, const glm::vec3& v, const glm::vec3& l) {
    const float nDotL = glm::dot(n, l);
    const float nDotV = glm::dot(n, v);
    if (nDotL <= 0.0f || nDotV <= 0.0f) return 0.0f;

    const glm::vec3 h = glm::normalize(v + l);
    const float nDotH = std::max(0.0f, glm::dot(n, h));
    const float vDotH = std::max(0.0f, glm::dot(v, h));
    if (vDotH <= kEps) return 0.0f;

    const float pSpec = specularProbability(m);
    const float diffusePdf = nDotL * kInvPi;
    // Half-vector density converted to the outgoing-direction density by the Jacobian 1/(4 v.h).
    const float specPdf = distributionGGX(nDotH, m.alpha()) * nDotH / (4.0f * vDotH);
    return pSpec * specPdf + (1.0f - pSpec) * diffusePdf;
}

BsdfSample sampleBsdf(const SurfaceMaterial& m, const glm::vec3& n, const glm::vec3& v, glm::vec2 u,
                      float lobePick) {
    BsdfSample out;
    const float nDotV = glm::dot(n, v);
    if (nDotV <= 0.0f) return out;

    const float pSpec = specularProbability(m);
    const float alpha = m.alpha();

    glm::vec3 l{0.0f};
    if (lobePick < pSpec) {
        // Sample the GGX half-vector, then reflect. (Not VNDF: that is a Phase 3 refinement and
        // would change the PDF, so it arrives with the MIS work rather than alongside it.)
        const float phi = 2.0f * kPi * u.x;
        const float cosTheta = std::sqrt((1.0f - u.y) / (1.0f + (alpha * alpha - 1.0f) * u.y));
        const float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
        const glm::vec3 hLocal{sinTheta * std::cos(phi), sinTheta * std::sin(phi), cosTheta};
        const glm::vec3 h = toWorld(hLocal, n);
        l = glm::reflect(-v, h);
        out.specular = true;
    } else {
        l = toWorld(sampleCosineHemisphere(u), n);
    }

    const float nDotL = glm::dot(n, l);
    if (nDotL <= 0.0f) return out;   // reflected below the surface; the sample is lost, not biased

    const float pdf = bsdfPdf(m, n, v, l);
    if (pdf <= kEps) return out;

    out.direction = l;
    out.pdf = pdf;
    out.weight = evaluateBsdf(m, n, v, l) * nDotL / pdf;
    out.valid = true;
    return out;
}

} // namespace avgen::pathtrace
