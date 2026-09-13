#pragma once

// ImportanceEvaluator (renderer upgrade, Deliverable 5 §5.6; ADR-122).
//
// What one drawable is worth this frame, in screen-space terms, computed from the scene and the
// camera and nothing else. Pure arithmetic: no device, no scene traversal, no state. The record it
// produces is the shared input of the two decisions Phase C adds -- which *representation* to draw
// (rendering/representation.hpp) and, later, which *material tier* to shade with -- and it is
// deliberately a set of measurements rather than a single score. Collapsing them into one number is
// how quality settings become a single unusable slider (§5.3): geometry wants pixels per triangle,
// shading wants pixels, and those two orderings genuinely disagree.
//
// ---- the number that matters is pixels per triangle, not pixels -------------------------------
//
// §4.5's sweep held coverage constant and varied only tessellation: 1.57 ms at 500 px/triangle
// against 7.73 ms at 0.49, with the knee at the 2x2-quad threshold. So a drawable's fragment cost
// is not predicted by how large it is on screen. It is predicted by how large its *triangles* are,
// and that needs a surface area and a triangle count -- which is what scene::MeshMetrics carries.
//
// `pixelsPerTriangle` is estimated, and the estimate is a geometric result rather than a fudge:
//
//   * Cauchy's formula: the mean projected area of a closed convex surface over all orientations
//     is a quarter of its surface area. Back-facing triangles are culled, so that quarter is also
//     the whole of what the front-facing half projects.
//   * Half the triangles of a closed mesh face the camera.
//
// So mean front-facing projected area per triangle = (A/4) / (N/2) = A / (2N), in world units,
// times the square of the pixels-per-world-unit at that distance. Two assumptions are stated where
// they are used: the mesh is closed-ish (a leaf card is not, and the estimate is then a factor of
// two pessimistic -- every triangle faces the camera and none is culled), and orientation is
// unbiased (a terrain chunk viewed at a grazing angle projects far less than a quarter).
// `MeshMetrics::surfaceArea` is measured from the mesh, so only the two factors are assumed.
//
// ---- what is deliberately absent --------------------------------------------------------------
//
// No occlusion, no history, no budget. Occlusion is deferred with its evidence (§6, "deliberately
// deferred"); history belongs to the selector, which is where the frame-independence trade lives;
// a budget is a policy and lives in QualityPolicy. An evaluator that is a pure function of (scene,
// camera) is one whose output can be asserted exactly, which is the point.

#include "scene/scene_types.hpp"

#include <cstdint>
#include <span>

namespace avgen::rendering {

// The camera constants one frame's importance is measured against, extracted once.
struct ViewContext {
    glm::vec3 cameraPosition{0.0f};
    glm::mat4 viewProjection{1.0f};
    // height / (2 tan(fovY/2)): pixels per world unit at one unit of distance. The same constant
    // shaders/cull.wgsl packs into `cameraPos.w`, so a scattered instance and an entity of the same
    // size at the same distance get the same answer from the GPU ladder and from this one. Two
    // subsystems disagreeing about how big something is on screen is a defect that shows up as a
    // seam between the scatter and the hand-placed copy of the same asset.
    float pixelsPerUnit = 0.0f;
    float viewportWidth = 0.0f;
    float viewportHeight = 0.0f;
    float nearPlane = 0.1f;

    [[nodiscard]] static ViewContext fromCamera(const scene::Camera& camera, std::uint32_t width,
                                                std::uint32_t height);
    // Pixels per world unit at `distance`. Clamped at the near plane: inside that the projection
    // diverges, and an object the camera is inside of is maximally important anyway.
    [[nodiscard]] float pixelsPerUnitAt(float distance) const;
    // Screen position in pixels (origin top-left), and whether the point is in front of the camera.
    [[nodiscard]] bool projectToScreen(glm::vec3 world, glm::vec2& outPixels) const;
};

// One drawable, described the way importance needs it. A scattered instance and an authored entity
// fill this in the same way, which is what lets one selector serve both.
struct ImportanceInput {
    glm::vec3 center{0.0f};  // world-space centre of the bounding sphere
    float radius = 0.0f;     // world-space radius of the bounding sphere
    // World-space surface area and triangle count of the representation currently drawn. Zero area
    // means "unknown": the estimate then falls back to the bounding disc, which is larger than the
    // truth and so biases towards keeping triangles, i.e. towards quality.
    float surfaceArea = 0.0f;
    std::uint32_t triangles = 0;
    bool hero = false;       // a designated hero (ADR-104); never demoted below its floor
    // Where this drawable's centre was last frame, for screen velocity. Equal to `center` (the
    // default) means "not moving, or not known to be", and yields zero velocity.
    glm::vec3 previousCenter{0.0f};
    bool hasPrevious = false;
};

// One frame's measurement of one drawable. Every field is derived; none is authoritative.
struct ImportanceRecord {
    std::uint32_t index = 0;
    float distance = 0.0f;          // eye to bounding-sphere centre, world units
    float projectedRadius = 0.0f;   // px; the bounding sphere's, the way cull.wgsl computes it
    float projectedArea = 0.0f;     // px^2; the Cauchy silhouette estimate, or the disc when unknown
    float pixelsPerTriangle = 0.0f; // projectedArea / front-facing triangles
    float screenVelocity = 0.0f;    // px since the previous frame, 0 when not known
    std::uint32_t triangles = 0;    // of the representation measured, carried through for weighting
    // Pixels per world unit at this record's distance, kept so a candidate representation can be
    // costed against the same projection without the ViewContext being passed around again.
    float pixelsPerUnit = 0.0f;
    bool hero = false;
    // False when the centre is behind the camera. Not a frustum test: culling is somebody else's
    // job and doing half of it here would be a second, disagreeing answer to the same question.
    bool inFront = true;

    // Whether this drawable's triangles are in quad-overdraw territory -- below the knee §4.5
    // located between 3.95 and 7.8 px/triangle. `kQuadThreshold` is the conservative end of that
    // bracket (a 2x2 quad is 4 px), so this says "certainly past the knee", not "past it or near".
    static constexpr float kQuadThreshold = 4.0f;
    [[nodiscard]] bool quadOverdrawn() const {
        return triangles > 0 && pixelsPerTriangle < kQuadThreshold;
    }
};

class ImportanceEvaluator {
public:
    // One drawable. `index` is the caller's own numbering and is copied through untouched.
    [[nodiscard]] static ImportanceRecord evaluate(const ViewContext& view, const ImportanceInput& in,
                                                  std::uint32_t index = 0);
    // A batch, in order. `out` must be at least as long as `inputs`.
    static void evaluate(const ViewContext& view, std::span<const ImportanceInput> inputs,
                         std::span<ImportanceRecord> out);

    // Pixels per triangle for a hypothetical representation of the same object with a different
    // triangle count and surface area -- what LOD n *would* cost if it were drawn at this record's
    // distance. This is what makes a representation choice a cost decision rather than a distance
    // one, and it is separate from `evaluate` because the selector asks it once per candidate rung.
    [[nodiscard]] static float pixelsPerTriangleFor(const ImportanceRecord& record, float surfaceArea,
                                                    std::uint32_t triangles);
};

} // namespace avgen::rendering
