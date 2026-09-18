#pragma once

// Why an object is not on screen (Visibility / Culling Lab, §9).
//
// The culling decision is arithmetic over a camera, a bound and a set of limits. It reaches the GPU
// as shaders/cull.wgsl and it reaches a person as a reason code, and neither of those needs a
// device -- so it lives here, with `importance.cpp` and `representation.cpp`, for the same reason
// they do: a decision that can only be checked by rendering something is a decision nobody checks.
// `procedural_renderer.hpp` includes this and re-exports every name, so the renderer's callers are
// unchanged.
//
// The names below were *derived from* the decisions this engine actually makes, not from a
// vocabulary written in advance. Three of the suite's proposed codes have no decision behind them
// here and are deliberately absent -- see `VisibilityReason` for which, and why.

#include "scene/procedural.hpp"
#include "scene/scene_types.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace avgen::rendering {

// The six frustum planes of a view-projection in world space, in the order left, right, bottom,
// top, near, far; xyz is a unit normal pointing inwards, w the plane offset (a point p is inside
// when dot(n, p) + w >= 0). Gribb-Hartmann on a 0..1 depth clip range (WebGPU/Metal).
using FrustumPlanes = std::array<glm::vec4, 6>;
[[nodiscard]] FrustumPlanes frustumPlanes(const glm::mat4& viewProj);

// The camera terms the cull pass needs beyond the planes.
struct CullCamera {
    glm::vec3 position{0.0f};
    float projScale = 1.0f; // viewportHeight / (2 tan(fovY / 2)): pixels per world unit at 1 unit
};
[[nodiscard]] float cullProjScale(float fovYRadians, std::uint32_t viewportHeight);

// CPU reference of the per-instance decision in shaders/cull.wgsl: the LOD level 0..lodCount-1,
// or -1 when the instance is culled. `center`/`radius` are the world bounding sphere. Shared with
// the tests, which compare the GPU's compacted lists against it.
//
// `lodRadius` is the radius the **ladder** measures projected size with, which is not the one the
// rejection tests use. `radius` is centred on the record position and must reach the furthest
// corner of the source from its own origin, so that nothing is discarded while its geometry is on
// screen; the ladder is asking how large the object looks, and that question is answered by the
// tight sphere about the source's box. For geometry centred on its origin the two are equal. 0
// means "the same as `radius`", which is what this function did before the two were separated.
[[nodiscard]] int cullLodLevel(const scene::LodSettings& lod, const FrustumPlanes& planes, const CullCamera& camera,
                               glm::vec3 center, float radius, float lodRadius = 0.0f);

// The same decision for the **shadow caster list**: the level this instance is drawn into the
// shadow maps at, or -1 when no shadow view can see it.
//
// A shadow caster list is a second cull, and until ADR-265 named it as a stage this engine had one
// cull with three consumers: `drawShadow` issued its indirect draws against the args the *camera*
// cull wrote, so an instance the camera could not see cast nothing however plainly its shadow fell
// into shot. Entities have had a second pass over exactly this case since ADR-046
// (`rendering::casterState`); the ecology had nothing, and on Glowmere multicam that read as
// **62,284 procedural instances, 496 surviving the camera cull, and 496 drawn into the shadow
// maps** -- one number where there should be two.
//
// It differs from `cullLodLevel` in exactly one term, deliberately. The camera frustum test is
// replaced by "some shadow view's frustum contains this sphere"; the distance limit, the screen-
// size limit and the whole LOD ladder are the camera's own, unchanged. Two consequences worth
// stating because both are load-bearing:
//
//   * an instance in both lists is on the **same rung** in both, so a caster is never drawn into a
//     cascade at a different mesh from the one the frame shows -- which is what keeps a shadow the
//     shadow of the thing that is on screen;
//   * the shadow list is neither a subset nor a superset of the camera list. An instance behind
//     the camera that a cascade contains is a caster and not a draw; an instance the camera sees at
//     200 m is a draw and not a caster, because the cascades stop at the shadow range (ADR-112,
//     77 m on every scene here) and nothing beyond it can write to a map.
//
// `views` is empty when the frame has no shadow views, and every instance is then rejected -- there
// is no map for it to be drawn into.
//
// Mirrored by shaders/cull.wgsl, which builds both lists from one classification pass.
[[nodiscard]] int shadowCullLodLevel(const scene::LodSettings& lod, std::span<const FrustumPlanes> views,
                                     const CullCamera& camera, glm::vec3 center, float radius,
                                     float lodRadius = 0.0f);

// The whole object's records reduced to two numbers that do not change until the record set does:
// the AABB of the instance positions (record space, before the object matrix) and the largest
// |scale| any record carries. Cached per object and recomputed on a structureVersion change.
struct InstanceBounds {
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};
    float maxAbsScale = 1.0f;
    // The *smallest* |scale| any record carries. `maxAbsScale` bounds how big a record's sphere can
    // be, which is what a rejection test needs; this bounds how small it can be, which is what
    // `objectLevelRange` needs to know how far down the ladder the population could have gone.
    float minAbsScale = 1.0f;
    bool valid = false;
};
[[nodiscard]] InstanceBounds instanceBounds(const std::vector<scene::InstanceRecord>& records);

// True when shaders/cull.wgsl is certain to reject *every* record of the object: the conservative
// whole-object bound fails the same frustum / maxDistance / minScreenRadius tests the per-instance
// path applies, so every LOD level's instance count will be zero. Skipping the object's cull
// dispatches and all of its indirect draws is then bit-exact -- there is nothing to pop in,
// because a level that would have drawn nothing draws nothing either way.
//
// Only valid when the records the GPU culls are the ones these bounds were built from: an object
// with effectors moves its records on the GPU, so the caller must not use this for those.
[[nodiscard]] bool objectFullyCulled(const scene::LodSettings& lod, const FrustumPlanes& planes,
                                     const CullCamera& camera, const glm::mat4& objectToWorld,
                                     const InstanceBounds& bounds, float sourceRadius,
                                     bool limitDistance = true);

// The same proof for the shadow caster list: true when no record of the object can reach any shadow
// view. `objectFullyCulled` per view would be the obvious spelling and is what this is -- every
// view has to reject the whole object, because one that does not is a view something casts into --
// with the empty-`views` case meaning "no shadow maps this frame", which rejects everything.
[[nodiscard]] bool objectFullyCulledForShadows(const scene::LodSettings& lod,
                                               std::span<const FrustumPlanes> views,
                                               const CullCamera& camera, const glm::mat4& objectToWorld,
                                               const InstanceBounds& bounds, float sourceRadius,
                                               bool limitDistance = true);


// The radius of the sphere **centred on the source's own origin** that contains the source's
// bounds -- which is the sphere shaders/cull.wgsl actually tests, because it centres on the
// instance record's position and the record's rotation and scale act about that same origin.
//
// This is not the half-diagonal of the box. The half-diagonal is the radius about the box's
// *centre*, and it is only the right answer when the geometry is centred on its origin. A tree
// authored standing on the origin is not: CommonTree_1 measures 7.27 units tall with its trunk
// foot at y = -0.24, so its half-diagonal is 3.92 while the corner furthest from the origin is
// 7.72 away. A sphere of 3.92 about the foot does not contain the canopy, and a canopy outside the
// cull sphere is a canopy the frustum test is entitled to throw away while it is on screen.
//
// ADR-199 already reached this conclusion for `scene::ProceduralGeometry`'s own bounds --
// "`sourceHalfExtent` returns max(|vertex|), ... the conservative version stays for culling" --
// and the renderer's mesh cache did not follow. Same rule, stated once, in the place the cull
// pass reads. For geometry that *is* centred on its origin the two numbers are identical, so
// nothing centred changes.
[[nodiscard]] float sourceCullRadius(const glm::vec3& boundsMin, const glm::vec3& boundsMax);

// ---- which rungs this object's records could be on (the LOD Lab) -------------------------------

// The inclusive range of LOD levels any record of one object could be assigned this frame.
// Conservative in one direction only: it may name a level nothing is actually on, and it must never
// omit a level something is on. A level outside it is **provably** empty, so its indirect draw
// would draw nothing and leaving it unrecorded cannot change the frame.
//
// This exists because the alternative does change the frame. `ProceduralRenderer` used to decide
// which levels to record from how many frames a level had been empty *in the last completed cull
// readback* -- and that readback lags the drawn frame by one to three frames, so the rule was a
// prediction of the present from the past. It is wrong at exactly one moment: the frame an instance
// arrives on a level it has not been on. Measured on Glowmere's `canopy` tree, the frame it reached
// rung 1 drew **0 lit pixels** where the settled frame at the identical camera drew 89. That is one
// to three frames of nothing, at every rung change, for every object -- which is the popping the
// brief is about, and no counter in the frame reports it because the cull pass ran and its numbers
// were right.
//
// The bound is arithmetic over the same quantities `objectFullyCulled` uses -- the record AABB
// through the object matrix, the camera, and the extreme record scales -- widened by everything
// that can move a threshold for an individual instance: ADR-082's per-instance spread and dead
// zone, and ADR-038's depth-band `detail`. It is a proof rather than a margin (spec §28): every
// term is the range of a term the shader evaluates.
struct LevelRange {
    int lowest = 0;
    int highest = scene::kMaxLodLevels - 1;
    [[nodiscard]] bool contains(int level) const { return level >= lowest && level <= highest; }
};

// `detailMin` / `detailMax` are the extremes of ADR-038's depth-band `detail` over the bands the
// scene declares (1.0 when it declares none). `hysteresisActive` is whether the renderer is letting
// ADR-082's dead zone run this frame, because with it off the thresholds are not widened by it.
[[nodiscard]] LevelRange objectLevelRange(const scene::LodSettings& lod, const CullCamera& camera,
                                          const glm::mat4& objectToWorld, const InstanceBounds& bounds,
                                          float sourceRadius, float detailMin, float detailMax,
                                          bool hysteresisActive);

// ---- reason codes (§9) --------------------------------------------------------------------------
//
// One code per decision this engine really makes. Every one of these names a branch you can point
// at in shaders/cull.wgsl or in ProceduralRenderer::update; none of them is aspirational.
//
// Three codes the suite's proposed vocabulary asks for are **absent, on purpose**:
//
//   OCCLUSION_CULLED      This engine has no occlusion culling. No Hi-Z, no depth pyramid, no
//                         visibility buffer, no occlusion query -- the only `occluder` in the tree
//                         is GTAO's horizon march and the contact-shadow ray, which are shading.
//                         A code that can never be returned is a code that makes the diagnostic
//                         look more complete than the engine is (ADR-182).
//   OUTSIDE_RENDER_DISTANCE  The same decision as DistanceCulled. `LodSettings::maxDistance` is the
//                         only distance bar an instance meets; naming it twice would imply two.
//   LOD_REJECTED          The ladder never rejects. It *demotes* -- full mesh, half mesh,
//                         billboard, dot -- and a demoted instance is still drawn. What removes a
//                         small instance is ScreenSizeCulled, which is a different test with a
//                         different threshold and its own hysteresis.
//
// NOT_SUBMITTED is absent too, for a different reason: every code below except Visible *is* a
// not-submitted, so the word carries no information at the point a person is asking "why".
enum class VisibilityReason : std::uint8_t {
    // The instance survives and is compacted into its rung's visible list.
    Visible,
    // shaders/cull.wgsl cs_cull_classify: dot(n, centre) + w < -radius on one of the six planes.
    FrustumCulled,
    // cs_cull_classify: dist - radius > LodSettings::maxDistance. Lifted by
    // DetailLimits::proceduralDistanceCull.
    DistanceCulled,
    // cs_cull_classify: screenRadius < LodSettings::minScreenRadius, with ADR-082's asymmetric
    // hysteresis (it takes a larger radius to come back than to leave). Lifted by the same flag.
    ScreenSizeCulled,
    // ADR-038 depth layers: a band's `density` below 1 removes a stable hashed subset. Deliberately
    // outside the `cull enabled` block in the shader, so it fires even with culling off.
    DepthBandThinned,
    // ProceduralRenderer::update: the object's whole record set provably fails one of the three
    // tests above, so its cull dispatches and every indirect draw are never encoded.
    ObjectFullyCulled,
    // ProceduralGeometry::visible is false, or it has no records, or its mesh failed to generate.
    ObjectNotDrawable,
    // More than kMaxProceduralObjects visible objects: the rest are skipped with a warning.
    ObjectBudgetExceeded,
    // A shadow pass, and ProceduralGeometry::castsShadow is false.
    ShadowOnlyRejected,
    // The bounds are unusable (no records, or a non-finite matrix), so no sphere can be formed.
    InvalidBounds,
};

[[nodiscard]] std::string_view visibilityReasonName(VisibilityReason reason);

// Everything the lab has to be able to answer about one instance, from one call: was it culled, at
// what stage, why, what bounds were used, what the camera was, the distance, the projected size.
struct InstanceVisibility {
    VisibilityReason reason = VisibilityReason::Visible;
    int lodLevel = -1;         // the rung a survivor draws at; -1 exactly when `reason != Visible`
    glm::vec3 center{0.0f};    // the world bounding-sphere centre the cull used
    float radius = 0.0f;       // the world bounding-sphere radius the cull used
    float distance = 0.0f;     // camera to `center`, metres
    float screenRadius = 0.0f; // projected radius of the sphere, pixels
    // Signed slack against each plane in the cull's own terms: dot(n, centre) + w + radius. The
    // shader rejects when this is negative, so the most negative entry names the plane that did it
    // and its magnitude says how far outside the sphere was. Order is the plane order above.
    std::array<float, 6> planeMargins{};
};

// The reason one instance is or is not drawn.
//
// The verdict is **not** re-derived here: it is `cullLodLevel`, the same function the renderer
// hands the GPU's parameters and the same one tests/rendering/test_culling_gpu.cpp pins against
// the GPU's compacted lists. The per-test comparisons below only *attribute* that verdict to a
// stage, and the function asserts the two agree -- so this can report a wrong stage but it cannot
// report a different answer from the pass it is describing (§37).
//
// Two decisions are outside one instance's arithmetic and cannot be reached from here:
// DepthBandThinned (the shader hashes the record index, which this signature has no room for) and
// everything above the instance -- ObjectFullyCulled, ObjectNotDrawable, ObjectBudgetExceeded,
// ShadowOnlyRejected. Those are the renderer's to report, and `proceduralVisibility` below is
// where an object-level verdict is turned into the same vocabulary.
[[nodiscard]] InstanceVisibility instanceVisibility(const scene::LodSettings& lod, const FrustumPlanes& planes,
                                                    const CullCamera& camera, glm::vec3 center, float radius,
                                                    float lodRadius = 0.0f);

// The object-level verdict, in the same vocabulary: what ProceduralRenderer::update decides before
// any instance is looked at. `shadowPass` applies the castsShadow gate the draw applies.
// Returns Visible when the object reaches the cull pass at all.
[[nodiscard]] VisibilityReason proceduralVisibility(const scene::ProceduralGeometry& object,
                                                    const FrustumPlanes& planes, const CullCamera& camera,
                                                    const glm::mat4& objectToWorld, const InstanceBounds& bounds,
                                                    float sourceRadius, bool limitDistance, bool shadowPass,
                                                    bool slotAvailable);

} // namespace avgen::rendering
