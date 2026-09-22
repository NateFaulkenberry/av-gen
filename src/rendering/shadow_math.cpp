#include "rendering/shadow_math.hpp"

#include "scene/scene.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace avgen::rendering {

namespace {

glm::vec3 safeNormalize(const glm::vec3& v, const glm::vec3& fallback) {
    const float len2 = glm::dot(v, v);
    return len2 > 1e-12f ? v * (1.0f / std::sqrt(len2)) : fallback;
}

glm::vec3 stableUp(const glm::vec3& direction) {
    return std::abs(direction.y) > 0.99f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
}

} // namespace

float directionalShadowRange(float cameraNear, float cameraFar, float sceneRadius,
                             std::uint32_t resolution, float texelTarget) {
    const float near = std::max(cameraNear, 1e-3f);
    // The world's size, clamped to the camera's. Unchanged from before ADR-112.
    const float world = std::clamp(std::max(sceneRadius * 3.0f, near * 20.0f), near * 4.0f,
                                   std::max(cameraFar, near * 4.0f));
    if (!(texelTarget > 0.0f) || resolution == 0) {
        return world;
    }
    // The inverse of `texel = 2 * k * range / resolution`. The k was measured against the real fit
    // rather than derived: over ranges from 50 m to 1 km and resolutions from 1024 to 4096 it sits
    // between 1.049 and 1.070 at a 16:9 frame, which is where the 2.12 below comes from.
    //
    // It varies with the frame's *aspect*, because a wider frustum has a wider bounding sphere for
    // the same depth slice -- k is 0.80 at 1:1 and about 1.3 at 2.39:1 -- and the aspect is not
    // passed here. So the realised texel is within about 25% of the target either way over the
    // aspects anyone shoots at, which is the accuracy this rule needs: it is choosing how far the
    // shadows reach, not calibrating an instrument.
    const float resolvable = texelTarget * static_cast<float>(resolution) / 2.12f;
    // A floor, so a tiny target or a tiny map cannot collapse the range to nothing. Twenty near
    // planes is the floor the world rule already uses, and below it there is no shadow worth
    // fitting either way.
    //
    // The floor is itself capped at `world`, which is not decoration. `world` is already clamped to
    // the camera's far plane, and a bare `max(..., near * 20)` would hand back a range *past* that
    // plane for a camera whose far plane is closer than twenty near planes -- a macro shot, or
    // anything with a deliberately shallow depth range. The rule would then be lengthening the
    // range, which is the one thing it must never do.
    return std::max(std::min(world, resolvable), std::min(world, near * 20.0f));
}

std::vector<float> cascadeSplits(float nearPlane, float farPlane, std::uint32_t count, float lambda) {
    const std::uint32_t n = std::clamp(count, 1u, kMaxCascades);
    const float near = std::max(nearPlane, 1e-3f);
    const float far = std::max(farPlane, near * 1.001f);
    const float ratio = far / near;
    std::vector<float> splits;
    splits.reserve(n);
    for (std::uint32_t i = 1; i <= n; ++i) {
        const float p = static_cast<float>(i) / static_cast<float>(n);
        const float logSplit = near * std::pow(ratio, p);
        const float uniformSplit = near + (far - near) * p;
        splits.push_back(std::clamp(lambda, 0.0f, 1.0f) * logSplit + (1.0f - std::clamp(lambda, 0.0f, 1.0f)) * uniformSplit);
    }
    splits.back() = far;
    return splits;
}

std::array<glm::vec3, 8> frustumCorners(const glm::mat4& invViewProj) {
    // WebGPU clip space: x, y in [-1, 1], z in [0, 1].
    constexpr std::array<glm::vec3, 8> ndc = {
        glm::vec3(-1.0f, -1.0f, 0.0f), glm::vec3(1.0f, -1.0f, 0.0f), glm::vec3(1.0f, 1.0f, 0.0f),
        glm::vec3(-1.0f, 1.0f, 0.0f),  glm::vec3(-1.0f, -1.0f, 1.0f), glm::vec3(1.0f, -1.0f, 1.0f),
        glm::vec3(1.0f, 1.0f, 1.0f),   glm::vec3(-1.0f, 1.0f, 1.0f)};
    std::array<glm::vec3, 8> out{};
    for (std::size_t i = 0; i < ndc.size(); ++i) {
        const glm::vec4 h = invViewProj * glm::vec4(ndc[i], 1.0f);
        out[i] = glm::vec3(h) / h.w;
    }
    return out;
}

ShadowView fitDirectionalCascade(const glm::mat4& invViewProj, float cameraNear, float cameraFar,
                                 float nearDepth, float farDepth, const glm::vec3& lightDirection,
                                 std::uint32_t resolution, float casterDistance) {
    const std::array<glm::vec3, 8> corners = frustumCorners(invViewProj);
    const float span = std::max(cameraFar - cameraNear, 1e-4f);
    const float t0 = std::clamp((nearDepth - cameraNear) / span, 0.0f, 1.0f);
    const float t1 = std::clamp((farDepth - cameraNear) / span, 0.0f, 1.0f);

    // The sub-frustum corners: view depth varies linearly along each frustum edge.
    std::array<glm::vec3, 8> slice{};
    for (std::size_t i = 0; i < 4; ++i) {
        const glm::vec3 rayStart = corners[i];
        const glm::vec3 rayEnd = corners[i + 4];
        slice[i] = rayStart + (rayEnd - rayStart) * t0;
        slice[i + 4] = rayStart + (rayEnd - rayStart) * t1;
    }
    // Bounding sphere: stable under camera rotation, which is what stops the shadow edges swimming.
    glm::vec3 center(0.0f);
    for (const glm::vec3& c : slice) {
        center += c;
    }
    center /= static_cast<float>(slice.size());
    float radius = 0.0f;
    for (const glm::vec3& c : slice) {
        radius = std::max(radius, glm::length(c - center));
    }
    radius = std::max(std::ceil(radius * 16.0f) / 16.0f, 1e-3f); // quantised so it stops jittering

    const glm::vec3 dir = safeNormalize(lightDirection, glm::vec3(0.0f, -1.0f, 0.0f));
    // How far behind the cascade the light's near plane sits, so casters outside the visible range
    // still reach it. Every metre of pull-back costs depth precision, so it is kept tight.
    const float back = std::clamp(casterDistance, radius * 0.5f, radius * 3.0f);
    const float texel = 2.0f * radius / static_cast<float>(std::max(resolution, 1u));

    // Texel snapping: the cascade's centre is quantised to a light-space grid that is anchored to
    // the world origin, so the shadow map re-rasterises onto the same texel grid frame after frame
    // and edges stop crawling as the camera moves.
    //
    // The anchor is the whole point, and getting it wrong is silent. Snapping in a light space
    // built *around the cascade* -- `lookAt(eye, center, up)` -- cannot work, because that matrix
    // sends `center` to the light-space origin by construction: the value being floored is exactly
    // zero every frame, so the snap is a no-op. Worse than a no-op, in fact. Zero carries float
    // error of either sign, and `floor` maps the two signs to 0 and -texel, so the window jumped a
    // whole texel back and forth for a millimetre of camera motion -- shimmer generated by the code
    // written to prevent it.
    //
    // A rotation-only basis at the origin has no such degeneracy: `center` lands wherever the world
    // puts it, and flooring genuinely quantises.
    const glm::mat4 lightBasis = glm::lookAt(glm::vec3(0.0f), dir, stableUp(dir));
    glm::vec3 centerLs = glm::vec3(lightBasis * glm::vec4(center, 1.0f));
    centerLs.x = std::floor(centerLs.x / texel) * texel;
    centerLs.y = std::floor(centerLs.y / texel) * texel;
    const glm::vec3 snapped = glm::vec3(glm::inverse(lightBasis) * glm::vec4(centerLs, 1.0f));

    const glm::vec3 eye = snapped - dir * (radius + back);
    const glm::mat4 lightView = glm::lookAt(eye, snapped, stableUp(dir));
    // The view's own basis, read out of the matrix that defines it rather than rebuilt beside it:
    // `lookAt` puts s, u and -f in the rows of the rotation, so these two are exactly the axes the
    // depth pass rasterises along and cannot drift from them.
    const glm::vec3 viewRight(lightView[0][0], lightView[1][0], lightView[2][0]);
    const glm::vec3 viewUp(lightView[0][1], lightView[1][1], lightView[2][1]);

    const float zFar = back + 2.0f * radius;
    const glm::mat4 proj = glm::ortho(-radius, radius, -radius, radius, 0.01f, zFar);

    ShadowView view;
    view.viewProj = proj * lightView;
    view.texelWorldSize = texel;
    view.depthRange = zFar;
    view.farDistance = farDepth;
    view.nearDistance = nearDepth;
    // The snapped centre and the half-width, published rather than recomputed. `snapped` is where
    // the volume actually is -- up to one texel from `center`, because the snap floors -- and an
    // overlay drawn at the unsnapped centre would be showing a cascade the renderer did not use.
    view.center = snapped;
    view.orthoRadius = radius;
    view.right = viewRight;
    view.up = viewUp;
    view.cascade = true;
    return view;
}

ShadowView fitSpotShadow(const scene::PunctualLight& light, std::uint32_t resolution, float range) {
    const glm::vec3 dir = safeNormalize(light.direction, glm::vec3(0.0f, -1.0f, 0.0f));
    const float far = std::max(range, 0.5f);
    const float near = std::max(far * 0.005f, 0.02f);
    const float fov = std::clamp(light.outerConeAngle * 2.0f * 1.05f, 0.05f, 3.0f);
    const glm::mat4 view = glm::lookAt(light.position, light.position + dir, stableUp(dir));
    const glm::mat4 proj = glm::perspective(fov, 1.0f, near, far);
    ShadowView out;
    out.viewProj = proj * view;
    out.right = glm::vec3(view[0][0], view[1][0], view[2][0]);
    out.up = glm::vec3(view[0][1], view[1][1], view[2][1]);
    // A texel at the far plane, which is the conservative end of the range.
    out.texelWorldSize = 2.0f * std::tan(fov * 0.5f) * far / static_cast<float>(std::max(resolution, 1u));
    out.depthRange = far - near;
    out.farDistance = far;
    out.nearDistance = near;
    out.center = light.position;
    out.cascade = false;
    return out;
}

std::uint32_t pointShadowFace(glm::vec3 fromLight) {
    const glm::vec3 a = glm::abs(fromLight);
    if (a.x >= a.y && a.x >= a.z) {
        return fromLight.x > 0.0f ? 0u : 1u;
    }
    if (a.y >= a.z) {
        return fromLight.y > 0.0f ? 2u : 3u;
    }
    return fromLight.z > 0.0f ? 4u : 5u;
}

std::array<ShadowView, kPointShadowFaces> fitPointShadow(const scene::PunctualLight& light,
                                                         std::uint32_t resolution, float range) {
    // The face axes in the order `pointShadowFace` returns, with an up that is never parallel to the
    // forward axis -- the ±Y faces take Z, everything else takes Y, which is the standard cube
    // convention and is why a lookAt here never degenerates.
    static constexpr glm::vec3 kForward[kPointShadowFaces] = {
        {1.0f, 0.0f, 0.0f},  {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f},
        {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f},  {0.0f, 0.0f, -1.0f}};
    static constexpr glm::vec3 kUp[kPointShadowFaces] = {
        {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f},
        {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}};

    const float far = std::max(range, 0.5f);
    const float near = std::max(far * 0.005f, 0.02f);
    // A shade over ninety degrees. Exactly ninety tiles the cube with no overlap, and a PCF kernel
    // at a face's edge then samples outside it -- which `shadowLookup` reports as invalid and the
    // shading pass reads as *unshadowed*, drawing a bright seam along every face boundary. The
    // overlap costs nothing: the face is still chosen by the dominant axis.
    constexpr float kFaceFov = 1.5707963f * 1.04f;

    std::array<ShadowView, kPointShadowFaces> out{};
    for (std::uint32_t f = 0; f < kPointShadowFaces; ++f) {
        const glm::mat4 view = glm::lookAt(light.position, light.position + kForward[f], kUp[f]);
        const glm::mat4 proj = glm::perspective(kFaceFov, 1.0f, near, far);
        out[f].viewProj = proj * view;
        out[f].right = glm::vec3(view[0][0], view[1][0], view[2][0]);
        out[f].up = glm::vec3(view[0][1], view[1][1], view[2][1]);
        out[f].texelWorldSize =
            2.0f * std::tan(kFaceFov * 0.5f) * far / static_cast<float>(std::max(resolution, 1u));
        out[f].depthRange = far - near;
        out[f].farDistance = far;
        out[f].nearDistance = near;
        out[f].center = light.position;
        out[f].cascade = false;
        out[f].cube = true;
    }
    return out;
}

// ---- who casts ---------------------------------------------------------------------------------

bool aabbInsideFrustum(const std::array<glm::vec4, 6>& planes, const glm::vec3& min,
                       const glm::vec3& max) {
    for (const glm::vec4& plane : planes) {
        // The corner furthest along the plane normal. If even that is behind the plane the whole
        // box is, which is the only rejection one plane can prove.
        const glm::vec3 positive(plane.x >= 0.0f ? max.x : min.x, plane.y >= 0.0f ? max.y : min.y,
                                 plane.z >= 0.0f ? max.z : min.z);
        if (glm::dot(glm::vec3(plane), positive) + plane.w < 0.0f) {
            return false;
        }
    }
    return true;
}

std::string_view casterStateName(CasterState state) {
    switch (state) {
    case CasterState::Caster: return "caster";
    case CasterState::CasterOffScreen: return "caster (off screen)";
    case CasterState::NotDrawable: return "not drawable";
    case CasterState::ShadowDisabled: return "castsShadow off";
    case CasterState::StyleExcluded: return "style or alpha mode excluded";
    case CasterState::OutsideEveryView: return "outside every shadow view";
    case CasterState::NoShadowViews: return "no shadow views this frame";
    }
    return "unknown";
}

bool casts(CasterState state) {
    return state == CasterState::Caster || state == CasterState::CasterOffScreen;
}

CasterState casterEligibility(const scene::Entity& entity) {
    if (!entity.visible || entity.mesh == scene::kInvalidMesh) {
        return CasterState::NotDrawable;
    }
    if (!entity.castsShadow) {
        return CasterState::ShadowDisabled;
    }
    // The exclusions the shadow passes apply: a grid is a wireframe, water is translucent
    // (ADR-099).
    if (entity.style == scene::MeshStyle::Grid || entity.style == scene::MeshStyle::Water) {
        return CasterState::StyleExcluded;
    }
    // A blended surface was excluded here too, on the grounds that "a blended surface casting a
    // hard silhouette is the bug that flag was added to avoid". It no longer casts a hard one:
    // `fs_depth` in pbr.wgsl discards an ordered fraction of its depth texels, so what it casts is
    // proportional to its alpha. Excluding it outright was the *other* bug -- under ADR-385 a
    // fading body is promoted to Blend for exactly the frames it is under 1.0 opacity, so this
    // line deleted an abducted animal's shadow on the first frame of a 1.2 s dissolve and left it
    // standing in a beam with no shadow for a second before it was gone.
    //
    // Fully transparent is still not a caster: there is nothing to cast, the dither would discard
    // every fragment of it anyway, and skipping it saves the draw. Still `StyleExcluded`, because
    // the reason is the material rather than the geometry or the frustum.
    if (entity.material.alphaMode == scene::AlphaMode::Blend && entity.material.opacity <= 0.0f) {
        return CasterState::StyleExcluded;
    }
    return CasterState::Caster;
}

std::pair<glm::vec3, glm::vec3> entityWorldBounds(const scene::Entity& entity,
                                                  const std::pair<glm::vec3, glm::vec3>& meshBounds) {
    const glm::mat4 model = entity.transform.matrix();
    glm::vec3 lo(std::numeric_limits<float>::max());
    glm::vec3 hi(std::numeric_limits<float>::lowest());
    for (int c = 0; c < 8; ++c) {
        const glm::vec3 corner((c & 1) ? meshBounds.second.x : meshBounds.first.x,
                               (c & 2) ? meshBounds.second.y : meshBounds.first.y,
                               (c & 4) ? meshBounds.second.z : meshBounds.first.z);
        const glm::vec3 w = glm::vec3(model * glm::vec4(corner, 1.0f));
        lo = glm::min(lo, w);
        hi = glm::max(hi, w);
    }
    return {lo, hi};
}

CasterState casterState(const scene::Entity& entity, const std::pair<glm::vec3, glm::vec3>& meshBounds,
                        std::span<const std::array<glm::vec4, 6>> viewPlanes) {
    const CasterState eligible = casterEligibility(entity);
    if (eligible != CasterState::Caster) {
        return eligible;
    }
    if (!entity.cameraCulled) {
        return CasterState::Caster;
    }
    if (viewPlanes.empty()) {
        return CasterState::NoShadowViews;
    }
    const auto [lo, hi] = entityWorldBounds(entity, meshBounds);
    for (const auto& planes : viewPlanes) {
        if (aabbInsideFrustum(planes, lo, hi)) {
            return CasterState::CasterOffScreen;
        }
    }
    return CasterState::OutsideEveryView;
}

std::uint32_t cascadeForDepth(float viewDepth, std::span<const float> splits) {
    if (splits.empty()) {
        return 0;
    }
    for (std::size_t i = 0; i < splits.size(); ++i) {
        if (viewDepth <= splits[i]) {
            return static_cast<std::uint32_t>(i);
        }
    }
    return static_cast<std::uint32_t>(splits.size() - 1);
}

ShadowViewReport reportView(const ShadowView& view, std::uint32_t index) {
    ShadowViewReport out;
    out.index = index;
    out.cascade = view.cascade;
    out.cube = view.cube;
    out.lightIndex = view.lightIndex;
    out.nearDepth = view.nearDistance;
    out.farDepth = view.farDistance;
    out.center = view.center;
    out.orthoRadius = view.orthoRadius;
    out.texelWorldSize = view.texelWorldSize;
    out.depthRange = view.depthRange;
    // The bias, stated once. `ShadowRenderer::update` used to compute these two lines inline and
    // then there was nowhere to read them from; now it writes the uniform from this report, so the
    // number a diagnostic prints is the number the shader subtracts and not a second opinion.
    //
    // Why the bias exists and why it is this (spec §28, which asks for exactly this paragraph):
    // a depth map samples the caster's surface at texel centres, so a receiver that *is* the
    // caster reads its own depth quantised to a texel and shadows itself in stripes. The constant
    // has to be at least the depth error one texel of slope produces, which is why it is expressed
    // as a multiple of the view's own texel (two of them) rather than as a number: a cascade
    // covering 10 m and one covering 1 km would otherwise need different constants for the same
    // picture. The 5 mm floor is for a view whose texel is tiny, where two texels is not enough to
    // clear the depth buffer's own quantisation. The division by `depthRange` converts the whole
    // thing from metres into the normalised depth the comparison happens in.
    out.worldBias = view.texelWorldSize * 2.0f + 0.005f;
    out.depthBias = out.worldBias / std::max(view.depthRange, 1e-3f);
    // shaders/shadows.wgsl: `normal * (texelWorld * (1 + 2 * (1 - NdotL)) * 1.4 + light.shadowBias)`.
    // At normal incidence the bracket is 1, so this is the floor of the offset; at grazing angles
    // it reaches three times this. The light's own `shadowBias` is added on top and is not here,
    // because it belongs to the light and this report is about the view.
    out.normalOffset = view.texelWorldSize * 1.4f;
    return out;
}

} // namespace avgen::rendering
