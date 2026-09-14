#pragma once

// The tree as a searchable subject: the `search::CandidateGenerator` contract, implemented.
//
// The split is the shared framework's. `src/search/candidate_search.hpp` owns sampling, the mesh
// hygiene gate, banded scoring, diversity selection and the serialised record, and knows nothing
// about trees. This file owns the parameter schema, the build, the feature axes and the domain
// scores, and knows nothing about searching.
//
// THE FINAL CAMERA IS THE AUTHORITY. The showcase camera never moves, so a tree that reads well from
// an orbiting viewpoint and mediocre from the one camera that will ever show it is not a successful
// candidate. Every silhouette metric here comes from a software rasterisation of the candidate
// through the scene's own view-projection, at the scene's own aspect ratio. A branch's screen
// thickness comes from its real radius at its real depth, because half the metrics are about the
// relationship between the trunk's mass and the canopy's.

#include "core/error.hpp"
#include "scene/tree.hpp"
#include "scene/tree_mesh.hpp"
#include "search/candidate_search.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace avgen::scene {

// The showcase camera, as the evaluator needs it: a matrix and a viewport. Deliberately not
// `scene::Camera`, so evaluation runs with no scene, no device and no composition.
struct TreeCameraView {
    glm::vec3 eye{9.0f, 9.0f, 41.0f};
    glm::vec3 target{0.0f, 16.0f, 0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    float fovYRadians = 0.7679f; // 44 degrees, matching the shipped world scenes
    int width = 480;
    int height = 270;
    [[nodiscard]] glm::mat4 viewProjection() const;
    [[nodiscard]] float aspect() const;
};

// The rasterised silhouette, kept so a debug view can show what the metrics were computed from
// rather than asking anyone to trust them.
struct TreeSilhouette {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> mask; // 0 empty, 1 branch, 2 foliage
    int filled = 0;
    int branchPixels = 0;
    // Projected AREA, in square pixels, accumulated analytically from the geometry rather than
    // counted off the mask. The mask cannot answer this: `stampDisc` clamps to a half-pixel minimum
    // so a thin branch does not vanish, which means every sub-pixel twig claims a whole pixel and
    // branch coverage comes out inflated by however many twigs the tree happens to have. These two
    // are resolution-independent and proportional to what is actually there.
    double branchArea = 0.0;
    double foliageArea = 0.0;
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
    float structureVisible = 0.0f;
    float depthSpread = 0.0f;
    float primaryCount = 0.0f;
    float secondaryPerPrimary = 0.0f;
    float tertiaryPerSecondary = 0.0f;
    float boleFraction = 0.0f;
    float trunkDominance = 0.0f;
    float silhouetteComplexity = 0.0f;
    float rootSpreadRatio = 0.0f;
};

[[nodiscard]] TreeMeasurements measureTree(const TreeGraph& graph, const TreeSilhouette& silhouette,
                                           const TreeCameraView& camera);

// A band plus the argument for it. The framework's `ScoreComponent` carries a name, a value and a
// weight but no reason; the reason is the first thing anyone retuning an evaluator needs, so it
// lives here beside the numbers rather than in a comment somewhere else.
struct TreeBand {
    std::string name;
    std::string rationale;
    float weight = 1.0f;
    search::ScoreBand band{};
};
[[nodiscard]] const std::vector<TreeBand>& treeBands();

// The human-defined design space (brief section 34): what kinds of tree are allowed to exist. The
// search decides where to look inside it; it does not get to leave it.
[[nodiscard]] search::GeneratorSchema treeSchema();

// Everything the schema does not vary. Recorded explicitly rather than left to defaults that may
// move, because a candidate record has to mean the same thing next month.
[[nodiscard]] TreeParams treeFixedParams();

// Maps a schema-ordered parameter vector into a full `TreeParams`.
[[nodiscard]] Result<TreeParams> treeParamsFrom(std::span<const float> values);

class TreeGenerator {
public:
    explicit TreeGenerator(TreeCameraView camera = {}, TreeMeshSettings mesh = {});

    [[nodiscard]] const search::GeneratorSchema& schema() const { return schema_; }
    [[nodiscard]] Result<search::Subject> build(const search::Parameters& params) const;
    [[nodiscard]] search::FeatureVector features(const search::Subject& subject,
                                                 const search::Parameters& params) const;
    [[nodiscard]] std::vector<search::ScoreComponent> domainScores(const search::Subject& subject,
                                                                   const search::Parameters& params) const;

    [[nodiscard]] const TreeCameraView& camera() const { return camera_; }
    // The graph behind the last `build` of these parameters, for callers that want the skeleton and
    // not only the meshes -- the debug view, the animation setup, the scene assembler.
    [[nodiscard]] const TreeGraph* lastGraph(const search::Parameters& params) const;

private:
    // `features` and `domainScores` are handed a Subject and a parameter vector, and both need the
    // skeleton rather than the meshes -- branch counts, axis orders and radii are not recoverable
    // from a triangle soup. Regenerating costs a few hundred milliseconds each, so `build` leaves
    // the graph here keyed on the parameters that produced it. The contract's methods are const,
    // hence mutable; the cache is a pure function of its key, so this is a memo and not state.
    struct Cache {
        search::Parameters key;
        TreeGraph graph;
        TreeMeasurements measured;
        bool measuredValid = false;
    };
    [[nodiscard]] const TreeGraph* ensureGraph(const search::Parameters& params) const;
    [[nodiscard]] const TreeMeasurements* ensureMeasured(const search::Parameters& params) const;

    search::GeneratorSchema schema_;
    TreeCameraView camera_;
    TreeMeshSettings mesh_;
    mutable std::optional<Cache> cache_;
};
static_assert(search::CandidateGenerator<TreeGenerator>);

// The population loop. The shared layer deliberately supplies no driver -- it owns sampling,
// hygiene, scoring and diversity, and leaves the loop to whoever owns the population policy. This
// one is generic in shape (nothing below reads a tree field) and belongs in the shared layer if the
// other project wants the same loop; it lives here until one of us needs it in both places.
struct TreeSearchSettings {
    std::uint32_t firstIndex = 0;
    int population = 64;
    int select = 12;
    // The quality/diversity trade, explicit because hiding it is how a search ends up returning
    // either N excellent near-identical trees or N diverse ugly ones with no knob to say which.
    float diversityAlpha = 0.62f;
    search::HygieneLimits hygiene{};
};

struct TreeSearchResult {
    std::vector<search::Candidate> candidates;         // every index tried, in order, rejections included
    std::vector<std::size_t> selected;                 // into `candidates`, in selection order
    std::vector<std::pair<std::string, int>> rejections; // rule -> count, for the diagnostic histogram
    int built = 0;
    double totalMs = 0.0;
    double buildMs = 0.0;
};

[[nodiscard]] Result<TreeSearchResult> searchTrees(const TreeGenerator& generator,
                                                   const TreeSearchSettings& settings);

} // namespace avgen::scene
