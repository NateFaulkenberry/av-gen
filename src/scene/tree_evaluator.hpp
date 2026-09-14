#pragma once

// Measuring a candidate tree, from the camera that will actually show it.
//
// THE FINAL CAMERA IS THE AUTHORITY (brief section 17). A tree that reads beautifully from an
// orbiting viewpoint and mediocre from the fixed showcase camera is not a successful candidate, and
// since the showcase camera never moves there is no reason to measure anything else. So every
// silhouette metric here comes from a software rasterisation of the candidate through the scene's
// own view-projection matrix, at the scene's own aspect ratio.
//
// The rasterisation is deliberately crude -- discs stamped along each branch segment, plus a disc
// per foliage cluster -- because it is measuring coverage and shape, not shading. What it must get
// right is that a branch's screen thickness comes from its real radius at its real depth, because
// half the metrics below are about the relationship between the trunk's mass and the canopy's.
//
// WHY THESE NUMBERS AND NOT OTHERS. Each component carries its own rationale string, which is the
// honest place for the argument, and `treeScoreComponents()` is where they live. The short version:
// the brief's section 4 asks for silhouette and hierarchy measures, section 5 lists the failure
// modes to discourage, and every band below has its ideal interval placed to sit away from one
// named failure on each side. None of them is validated against a human judgement yet -- that is
// what the contact sheet in Phase 6 is for, and until then these are arguments, not findings.

#include "core/error.hpp"
#include "scene/candidate_search.hpp"
#include "scene/tree.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace avgen::scene {

// The showcase camera, as the evaluator needs it. Kept separate from `scene::Camera` so the
// evaluator can run with no scene, no device and no composition -- it is a matrix and a viewport.
struct TreeCameraView {
    glm::vec3 eye{7.0f, 8.0f, 34.0f};
    glm::vec3 target{0.0f, 11.0f, 0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    float fovYRadians = 0.7679f; // 44 degrees, matching the shipped world scenes
    int width = 480;
    int height = 270;
    [[nodiscard]] glm::mat4 viewProjection() const;
    [[nodiscard]] float aspect() const;
};

// The rasterised silhouette, kept so the debug view and the contact sheet can show what the metrics
// were computed from rather than asking anyone to trust them.
struct TreeSilhouette {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> mask;     // 0 empty, 1 branch, 2 foliage
    int filled = 0;
    int minX = 0, minY = 0, maxX = 0, maxY = 0;
    [[nodiscard]] bool empty() const { return filled == 0; }
};

[[nodiscard]] TreeSilhouette rasteriseTree(const TreeGraph& graph, const TreeCameraView& camera);

struct TreeMeasurements {
    float frameFill = 0.0f;
    float boxFill = 0.0f;
    float aspect = 0.0f;
    float balance = 0.0f;
    float verticalCentroid = 0.0f;
    float openness = 0.0f;
    float depthSpread = 0.0f;
    float primaryCount = 0.0f;
    float secondaryPerPrimary = 0.0f;
    float tertiaryPerSecondary = 0.0f;
    float boleFraction = 0.0f;
    float trunkDominance = 0.0f;
    float silhouetteComplexity = 0.0f;
    float rootSpreadRatio = 0.0f;
    [[nodiscard]] std::vector<ScoredMetric> asMetrics() const;
    [[nodiscard]] std::vector<float> asFeatures() const;
};

[[nodiscard]] TreeMeasurements measureTree(const TreeGraph& graph, const TreeSilhouette& silhouette,
                                           const TreeCameraView& camera);

// The weighted score definition. One place, so retuning it is a diff in one function.
[[nodiscard]] const std::vector<ScoreComponent>& treeScoreComponents();

// The human-defined design space (brief section 34): the ranges inside which the search is allowed
// to look. We decide what kinds of tree are permitted to exist; the algorithm searches inside that.
struct TreeDesignSpace {
    float heightMin = 20.0f, heightMax = 27.0f;
    float crownRadiusMin = 7.5f, crownRadiusMax = 11.0f;
    float boleFractionMin = 0.26f, boleFractionMax = 0.42f;
    float lambdaYoungMin = 0.56f, lambdaYoungMax = 0.68f;
    float lambdaOldMin = 0.36f, lambdaOldMax = 0.48f;
    float shoulderMin = 0.15f, shoulderMax = 0.70f;
    float lumpinessMin = 0.25f, lumpinessMax = 0.75f;
    float branchAngleMin = 0.62f, branchAngleMax = 1.05f;
    float outwardBiasMin = 0.18f, outwardBiasMax = 0.52f;
    float branchDropMin = -0.65f, branchDropMax = -0.18f;
    float shedMin = 0.06f, shedMax = 0.20f;
    float alphaMin = 2.9f, alphaMax = 3.9f;
    float rootCoupleMin = 0.35f, rootCoupleMax = 0.85f;
    std::uint32_t seedCount = 4096; // seeds the search may draw from
    // Everything the search does not vary. The winner's parameters are a full `TreeParams`, so the
    // fixed half is recorded here rather than being re-derived from defaults that may move.
    TreeParams fixed{};
};

// The tree as a search subject. Owns nothing but the design space and the camera.
class TreeSubject final : public CandidateSubject {
public:
    TreeSubject(TreeDesignSpace space, TreeCameraView camera);

    [[nodiscard]] std::string name() const override { return "tree"; }
    [[nodiscard]] const std::vector<ScoreComponent>& components() const override;
    [[nodiscard]] int dimensions() const override;
    [[nodiscard]] nlohmann::json sample(std::span<const float> u) const override;
    [[nodiscard]] nlohmann::json perturb(const nlohmann::json& base, std::span<const float> u,
                                         float scale) const override;
    [[nodiscard]] Result<void> measure(const nlohmann::json& parameters, CandidateRecord& out) const override;

    [[nodiscard]] const TreeCameraView& camera() const { return camera_; }
    [[nodiscard]] const TreeDesignSpace& space() const { return space_; }

private:
    [[nodiscard]] TreeParams paramsFrom(std::span<const float> u) const;
    TreeDesignSpace space_;
    TreeCameraView camera_;
};

} // namespace avgen::scene
