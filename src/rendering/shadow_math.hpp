#pragma once

// The maths behind cascaded shadow maps (ADR-034), free of any GPU type so the tests can check it
// against a CPU reference without a device: the split scheme, the frustum corners, the stabilised
// per-cascade fit and the spot-light fit, plus the uniform block the shading pass reads.

#include "scene/scene_types.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace avgen::rendering {

constexpr std::uint32_t kMaxShadowViews = 8;
constexpr std::uint32_t kMaxCascades = 4;

// One shadow view: a light-space matrix, the world size of one of its texels (the normal-offset
// bias scale) and which light it belongs to.
struct ShadowView {
    glm::mat4 viewProj{1.0f};
    float texelWorldSize = 0.01f;
    float depthRange = 1.0f;  // world units the view's normalised depth spans (the bias scale)
    float farDistance = 0.0f; // cascade: the view depth this cascade covers up to
    // The near end of the same interval. The fit has always been given it and never published it,
    // so every reader that wanted to know which depths a cascade covers had to re-derive the split
    // scheme -- and a diagnostic that re-derives the number it is describing is the §37 trap.
    float nearDistance = 0.0f;
    // The volume the fit chose, in world space: the centre after texel snapping and the half-width
    // of the orthographic box about it. Both are already computed inside `fitDirectionalCascade`;
    // publishing them is what lets an overlay draw the cascade the renderer used rather than one it
    // fitted again for itself.
    glm::vec3 center{0.0f};
    float orthoRadius = 0.0f;
    // The two axes a camera-facing impostor must be built from while THIS view is the one being
    // rendered: the right and up of the view's own basis.
    //
    // They exist because the LOD ladder's rungs 2 and 3 are camera-facing quads
    // (`shaders/procedural.wgsl`, the `fieldInfo.z > 0.5` branch), built from
    // `frame.cameraRight` and `frame.cameraUp`. A shadow pass runs those vertex shaders against a
    // copy of the frame block with only the view-projection replaced, so the quad faced the
    // *camera* while being rasterised from the *light* -- and the shadow of a stationary tree under
    // a stationary sun then depended on where the camera was standing. See
    // docs/shadow-lab/README.md.
    glm::vec3 right{1.0f, 0.0f, 0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    int lightIndex = -1;      // index into the packed light buffer
    bool cascade = false;
    bool cube = false;        // one of six faces around a light with no single direction
};

// A light that shines in every direction needs six views, not one. They are ordinary views in the
// ordinary atlas -- the shading pass picks the face from the direction to the fragment -- so nothing
// else in the pipeline learns about cube maps: no cube texture, no cube sampler, and the depth
// passes the encoder already writes per view are unchanged.
constexpr std::uint32_t kPointShadowFaces = 6;

// `ShadowUniforms::views[i]`, mirrored by `shaders/lighting.wgsl`.
struct ShadowViewGpu {
    glm::mat4 viewProj;
    glm::vec4 params; // x = texel world size, y = depth bias, z = 1 when a cascade, w = far distance
};
static_assert(sizeof(ShadowViewGpu) == 80);

// Group 0 binding 3 of every scene pass (688 bytes).
struct ShadowUniforms {
    ShadowViewGpu views[kMaxShadowViews];
    glm::vec4 info;   // x = atlas resolution, y = cascade count, z = PCF taps, w = 1 when PCSS is on
    glm::vec4 splits; // the cascade far distances in view depth (unused entries hold the last one)
    // ADR-227. x = PCSS blocker-search taps. `info` and `splits` were both full, which is why
    // `QualitySettings::pcssBlockerTaps` sat unread for as long as it did -- wiring it was a
    // uniform-layout change rather than a line. yzw are free and deliberately named nothing:
    // the next field to want a lane takes one rather than overloading a meaning onto an existing
    // one, which is the mistake `info.z` serving two tap counts already was.
    glm::vec4 info2;
};
static_assert(sizeof(ShadowUniforms) == 688);

// ADR-112: the shadow-map resolution the shadowed range is sized against, whatever resolution the
// tier actually renders it at.
//
// The range decides *where the shadows stop*, which is composition, not quality. A preview that
// showed shadows to 40 m and a final that showed them to 150 m would not be the same shot, and a
// preview that does not predict the final is worse than no preview. So every tier gets the same
// range and they differ in how sharp it is: preview's 1024 gets twice the texel this reference
// implies, offline's 4096 gets half. This is the one place a tier is deliberately not allowed to
// scale something.
constexpr std::uint32_t kShadowRangeReference = 2048;

// ADR-112: how far the directional cascades should reach, in view depth.
//
// Two rules, and the smaller wins. The first is the old one: three scene radii, clamped to the
// camera's own planes -- enough to cover the world, never more than the camera can see. The second
// is new, and it is the one that makes a wide scene of small objects cast anything: the range at
// which the *coarsest* cascade's texel is still no larger than `texelTarget` metres.
//
// The second rule exists because the coarsest texel has exactly one lever. A camera frustum widens
// linearly with distance, so the last cascade's bounding sphere is close to `k * range` -- k is
// about 0.8 whatever the split scheme -- and its texel is `2 * k * range / resolution`. The split
// lambda and the cascade count redistribute texels among the *near* cascades and leave that
// unmoved: measured on the real fit, the range that yields an 8 cm coarsest texel is 101 m at two
// cascades, 105 m at three and 108 m at four. Shortening the range is the only thing that shortens
// the texel, and raising the resolution is the move this is here to avoid.
//
// `resolution` is `kShadowRangeReference`, not the tier's own map size -- see above.
// `texelTarget <= 0` disables the second rule and returns the first, which is the pre-ADR-112
// behaviour.
[[nodiscard]] float directionalShadowRange(float cameraNear, float cameraFar, float sceneRadius,
                                           std::uint32_t resolution, float texelTarget);

// Practical split scheme (Zhang et al. 2006): a blend of the logarithmic and uniform splits by
// `lambda` (0 = uniform, 1 = logarithmic). Returns `count` far distances, the last being `far`.
[[nodiscard]] std::vector<float> cascadeSplits(float nearPlane, float farPlane, std::uint32_t count,
                                               float lambda = 0.85f);

// The six faces around a point or area light, in the order the shader's dominant-axis test expects:
// +X, -X, +Y, -Y, +Z, -Z. Each is a 90-degree perspective from the light's own position, widened a
// hair so a PCF kernel at a face's edge still lands inside it rather than falling off and reading as
// unshadowed.
[[nodiscard]] std::array<ShadowView, kPointShadowFaces> fitPointShadow(const scene::PunctualLight& light,
                                                                       std::uint32_t resolution, float range);
// Which of the six a direction from the light falls in. Shared with the shader by construction:
// the test is the same one, and `tests/unit/test_shadow_math.cpp` walks a sphere of directions.
[[nodiscard]] std::uint32_t pointShadowFace(glm::vec3 fromLight);

// The eight world-space corners of a view frustum, near face first (counter-clockwise from the
// bottom left as seen from the camera), then the far face.
[[nodiscard]] std::array<glm::vec3, 8> frustumCorners(const glm::mat4& invViewProj);

// Fits one cascade to the camera sub-frustum between the view depths `nearDepth` and `farDepth`.
// `cameraNear` / `cameraFar` must be the planes `invViewProj` was built from, because the corners
// are interpolated between its near and far faces. The fit uses the sub-frustum's bounding sphere,
// so it is invariant to camera rotation, and the light-space origin is snapped to whole texels, so
// it is invariant to camera translation within a texel. `casterDistance` pulls the light's near
// plane back far enough to catch casters behind the visible range.
[[nodiscard]] ShadowView fitDirectionalCascade(const glm::mat4& invViewProj, float cameraNear,
                                               float cameraFar, float nearDepth, float farDepth,
                                               const glm::vec3& lightDirection, std::uint32_t resolution,
                                               float casterDistance);

// One perspective map covering a spot light's cone.
[[nodiscard]] ShadowView fitSpotShadow(const scene::PunctualLight& light, std::uint32_t resolution,
                                       float range);

// ---- who casts (Shadow Lab) ---------------------------------------------------------------------
//
// The shadow caster list is a **second cull**, and until this header it was written out longhand
// inside `SceneRenderer::render` -- `shadowEligible`, `anyCascadeSees` and the opaque-list branch,
// three lambdas over 180 lines. That is fine for a renderer and impossible for a diagnostic: an
// overlay that wants to colour an entity by whether the frame made it a caster either reaches into
// the renderer or writes the rule a second time, and a rule written twice is a rule that disagrees
// with itself the first time somebody edits one copy (§37).
//
// So the decision lives here, the renderer calls it, and the overlay calls the same function. It
// needs no device: it is an entity's own flags and an AABB against some planes.

// Why the shadow passes do or do not draw an entity. One value per branch that actually exists in
// the renderer; there is deliberately no "not submitted" catch-all, for the reason
// `rendering::VisibilityReason` gives -- every value but the first is one.
enum class CasterState : std::uint8_t {
    // In the opaque draw list and casting: the ordinary case.
    Caster,
    // The camera frustum rejected it and a shadow view still contains it, so it is added by the
    // second pass and casts anyway (ADR-046). A hill behind the camera.
    CasterOffScreen,
    // `Entity::visible` is false, or it has no mesh.
    NotDrawable,
    // `Entity::castsShadow` is false. Terrain writes this per chunk from its own shadow distance;
    // a scene can write it per node.
    ShadowDisabled,
    // A style or an alpha mode the shadow passes skip: Grid, Water, or a blended material.
    StyleExcluded,
    // Camera-culled, and no shadow view's frustum contains its world bounds either.
    OutsideEveryView,
    // Camera-culled and there are no shadow views at all this frame -- shadows off, or no casting
    // light. Distinct from the line above because the answer to "why" is the frame, not the object.
    NoShadowViews,
};
// True when any part of a world-space box is inside a frustum: the corner furthest along each
// plane's normal, rejected only when even that is behind the plane.
//
// It lives here rather than in `scene_renderer.cpp`, where it was, for the reason the caster rule
// moved: `casterState` needs exactly this test, and a second copy of it beside the first is the
// thing this header exists to stop. `world::aabbVisible` is the same construction for the world
// module, and rendering does not depend on world/.
[[nodiscard]] bool aabbInsideFrustum(const std::array<glm::vec4, 6>& planes, const glm::vec3& min,
                                     const glm::vec3& max);

[[nodiscard]] std::string_view casterStateName(CasterState state);
// True for the two states in which the entity is drawn into at least one shadow view.
[[nodiscard]] bool casts(CasterState state);

// The entity's own half of the rule: everything decidable without a frustum. Split out because the
// renderer applies exactly this much in its first pass over the opaque list, where the camera has
// already decided the object is on screen.
[[nodiscard]] CasterState casterEligibility(const scene::Entity& entity);

// The world-space AABB of an entity's mesh bounds under its transform: the eight corners
// transformed and re-bounded, which is what the second cull tests. `meshBounds` is the scene's
// own local-space pair for `entity.mesh`.
[[nodiscard]] std::pair<glm::vec3, glm::vec3> entityWorldBounds(const scene::Entity& entity,
                                                                const std::pair<glm::vec3, glm::vec3>& meshBounds);

// The whole verdict. `views` is this frame's shadow views (empty when shadows are off), and
// `viewPlanes` their frustum planes in the same order -- passed in rather than derived so the
// caller can build them once per frame instead of once per entity.
[[nodiscard]] CasterState casterState(const scene::Entity& entity,
                                      const std::pair<glm::vec3, glm::vec3>& meshBounds,
                                      std::span<const std::array<glm::vec4, 6>> viewPlanes);

// Which cascade covers a view depth. The CPU mirror of `cascadeFor` in shaders/shadows.wgsl, and a
// mirrored pair in the same sense `pointShadowFace`/`cubeFace` are: two implementations of one
// test, kept identical by hand and by a test that walks the interesting values. The comparison is
// `<=`, so a point exactly on a split belongs to the *nearer* cascade -- which matters, because
// that is the half of the boundary the crossfade band is measured from.
[[nodiscard]] std::uint32_t cascadeForDepth(float viewDepth, std::span<const float> splits);

// ---- what the frame did (Shadow Lab) ------------------------------------------------------------

// One shadow view, reported. Everything §15 asks a shadow diagnostic to expose about a cascade,
// taken from the fit rather than recomputed from it: cascade id, the depths it covers, the volume
// it covers them with, its texel, and both halves of its bias in the units each is expressed in.
struct ShadowViewReport {
    std::uint32_t index = 0;      // the atlas layer, and the cascade id the shader selects
    bool cascade = false;
    bool cube = false;
    int lightIndex = -1;
    float nearDepth = 0.0f;       // view depth this view covers from
    float farDepth = 0.0f;        // ... and to
    glm::vec3 center{0.0f};       // the snapped centre of the fitted volume, world space
    float orthoRadius = 0.0f;     // half the width of the orthographic box (0 for a perspective fit)
    float texelWorldSize = 0.0f;  // metres per texel
    float depthRange = 0.0f;      // world units the normalised depth spans
    // The two biases, in the units they are applied in. `worldBias` is what the renderer computes
    // (two texels plus 5 mm) and `depthBias` is that divided by `depthRange`, which is the number
    // the shader subtracts. Reported as a pair because a bias quoted in one of them alone cannot be
    // compared between two cascades: the whole point of the conversion is that the same visual
    // bias is a different number in each.
    float worldBias = 0.0f;
    float depthBias = 0.0f;
    // The normal offset the shader pushes the sample point along, at normal incidence. The shader's
    // full expression is `texel * (1 + 2 * (1 - NdotL)) * 1.4 + light.shadowBias`; this is its
    // value at NdotL = 1, which is the floor.
    float normalOffset = 0.0f;
    std::uint32_t entityDraws = 0;    // entity draws this view recorded
    std::uint32_t entitiesCulled = 0; // casters this view's own frustum rejected
};

// Fill a report from a view. The bias arithmetic is duplicated nowhere: `ShadowRenderer::update`
// calls this and writes the uniform from what it returns.
[[nodiscard]] ShadowViewReport reportView(const ShadowView& view, std::uint32_t index);

} // namespace avgen::rendering
