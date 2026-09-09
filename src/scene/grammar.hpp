#pragma once

// Compositional grammar (brief §13, ADR-028 follow-up): a small deterministic rewriting system
// that expands named rules into a point cloud of placements. Rules: Place emits a point; Repeat
// applies its step transform k times (k = 0..count-1) and expands its children at each step;
// Branch expands every child at the current transform; Alternate expands child[k % n] at step k;
// Mirror expands the children as they are and mirrored across `mirrorAxis`; Choice picks one
// child by a seeded hash of the expansion path (weighted by the children's `weight`s);
// Conditional expands the children only while depth < `depthLimit` (else its `elseChildren`).
// Recursion is a rule naming an ancestor; expansion stops at `maxDepth` and `maxInstances`
// (truncating deterministically in expansion order). Emitted attributes: depth (int),
// rule (int index), branch (int, the repetition index of the nearest Repeat/Alternate), and the
// core columns (position/rotation/scale from the accumulated transform, id = emission order).

#include "core/error.hpp"
#include "scene/scene_types.hpp"
#include "spatial/point_cloud.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::scene {

enum class GrammarOp : std::uint8_t { Place, Repeat, Branch, Alternate, Mirror, Choice, Conditional };
[[nodiscard]] const char* grammarOpName(GrammarOp op);
[[nodiscard]] std::optional<GrammarOp> grammarOpFromName(std::string_view name);

struct GrammarRule {
    std::string name;
    GrammarOp op = GrammarOp::Place;
    int count = 4;                       // Repeat / Alternate repetitions
    Transform step;                      // applied cumulatively per repetition (offset, rotation, scale)
    Transform pre;                       // applied once before the children (e.g. an offset for Branch)
    std::vector<std::string> children;   // rule names
    std::vector<float> weights;          // Choice (defaults to equal)
    std::vector<std::string> elseChildren; // Conditional
    int depthLimit = 3;                  // Conditional
    glm::vec3 mirrorAxis{1.0f, 0.0f, 0.0f};
    std::uint32_t seed = 0;              // Choice (mixed with the grammar seed and the path hash)
    float scaleAttribute = 1.0f;         // multiplies the emitted point's scale (Place)
};

struct Grammar {
    std::string axiom;                   // start rule name
    std::vector<GrammarRule> rules;
    int maxDepth = 8;
    int maxInstances = 100000;
    std::uint32_t seed = 1;

    [[nodiscard]] Result<void> validate() const; // axiom and every child resolve; counts >= 0; limits
    [[nodiscard]] const GrammarRule* find(std::string_view name) const;
    [[nodiscard]] std::uint64_t structuralHash() const;
    // Expands into a cloud (positions/rotations/scales in grammar space; ids in emission order).
    // Pure and deterministic; never exceeds maxInstances points.
    [[nodiscard]] spatial::PointCloud expand() const;
    [[nodiscard]] nlohmann::json toJson() const;
    static Result<Grammar> fromJson(const nlohmann::json& j);
};

} // namespace avgen::scene
