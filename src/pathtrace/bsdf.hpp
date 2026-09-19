#pragma once

// The glTF 2.0 metallic-roughness BSDF (spec section 19).
//
// This is the glTF specification's model, not an arbitrary "pretty" BRDF: a Lambertian diffuse lobe
// and a Cook-Torrance specular lobe with the GGX/Trowbridge-Reitz normal distribution, the Smith
// height-correlated visibility term, and Schlick's Fresnel approximation. Metals have no diffuse
// lobe and take their F0 from the base colour; dielectrics have F0 = 0.04 and keep their base colour
// in the diffuse lobe. Those two sentences are the whole model and everything here serves them.
//
// Conventions, stated because half the bugs in a BSDF are convention bugs:
//   * `n`, `v`, `l` are all unit and all point AWAY from the surface.
//   * `v` points toward the viewer (back along the incoming ray), `l` toward the light.
//   * "Visibility" V is the Smith G term already divided by 4*(n.v)*(n.l), so the specular BRDF is
//     D * V * F with no further denominator. Forgetting that division is a classic 4x error.

#include "scene/scene_types.hpp"

#include <glm/glm.hpp>

namespace avgen::pathtrace {

// A material with its textures already sampled at one point. The integrator resolves this once per
// hit and the BSDF functions below are pure in it.
struct SurfaceMaterial {
    glm::vec3 baseColor{1.0f};   // linear, sRGB already decoded
    float opacity = 1.0f;
    glm::vec3 emission{0.0f};    // linear radiance, emissiveColor * emissiveIntensity
    float metallic = 0.0f;
    float roughness = 1.0f;      // perceptual roughness, as glTF authors it
    float occlusion = 1.0f;

    // glTF clamps roughness away from zero because a perfectly smooth GGX lobe is a delta function
    // and the sampling below cannot represent it. 0.045^2 keeps alpha above float noise.
    [[nodiscard]] float alpha() const {
        const float r = glm::clamp(roughness, 0.045f, 1.0f);
        return r * r;
    }
    [[nodiscard]] glm::vec3 f0() const { return glm::mix(glm::vec3(0.04f), baseColor, metallic); }
    [[nodiscard]] glm::vec3 diffuseAlbedo() const { return baseColor * (1.0f - metallic); }
};

// GGX / Trowbridge-Reitz normal distribution. Integrates to 1 over the projected hemisphere.
[[nodiscard]] float distributionGGX(float nDotH, float alpha);

// Smith height-correlated visibility, ALREADY divided by 4*(n.v)*(n.l).
[[nodiscard]] float visibilitySmithGGX(float nDotV, float nDotL, float alpha);

// Schlick's Fresnel. `f0` at normal incidence, white at grazing.
[[nodiscard]] glm::vec3 fresnelSchlick(float vDotH, const glm::vec3& f0);

// The full BSDF value f(v, l). Does NOT include the n.l cosine -- the integrator applies that, so
// that the same function can be used for both light sampling and BSDF sampling.
[[nodiscard]] glm::vec3 evaluateBsdf(const SurfaceMaterial& m, const glm::vec3& n, const glm::vec3& v,
                                     const glm::vec3& l);

// The PDF the sampler below uses, for the same directions. Needed by MIS in Phase 3, and needed now
// so that `sampleBsdf` and `evaluateBsdf` can be checked against each other.
[[nodiscard]] float bsdfPdf(const SurfaceMaterial& m, const glm::vec3& n, const glm::vec3& v,
                            const glm::vec3& l);

struct BsdfSample {
    glm::vec3 direction{0.0f};
    glm::vec3 weight{0.0f};  // f * cos / pdf, ready to multiply into the throughput
    float pdf = 0.0f;
    bool valid = false;
    bool specular = false;
};

// Samples an outgoing direction. Chooses between the diffuse and specular lobes by their relative
// Fresnel weight, then samples that lobe; the returned PDF is the combined one, so the estimator
// stays unbiased whichever lobe was picked.
[[nodiscard]] BsdfSample sampleBsdf(const SurfaceMaterial& m, const glm::vec3& n, const glm::vec3& v,
                                    glm::vec2 u, float lobePick);

} // namespace avgen::pathtrace
