#pragma once

// Where a scatter-anchored particle system's clusters are (see `ScatterAnchor` in particles.hpp).
//
// Nothing here places a tree. The positions are read off `ProceduralGeometry::instances`, the
// records `ProceduralGeometry::rebuild` projected from the cloud `world::scatter` produced and the
// renderer uploads as-is -- so a swarm is centred on the instance the GPU draws, and the gate sees
// the very `instanceRandom` and `instanceEmissive` a material program is handed for that instance.
// A second derivation of where the trees stand would be a second copy of the placement algorithm,
// free to drift from the first; this has no copy to drift.
//
// Two steps, because they change at different rates. The candidate set -- every instance that
// passes the gate, as the world-space centre of its crown -- changes only when the forest is
// rebuilt. Which of them are in use changes with the camera, every frame, and is a pure function
// of the camera position (never of frame history), which is what ADR-360 asks of anything a scrub
// has to land on.

#include "scene/particles.hpp"
#include "scene/procedural.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::scene {

// The procedural object a terrain's scatter layer becomes (composition.cpp). One definition, used
// both to name the object and to find it again.
[[nodiscard]] std::string scatterObjectName(std::string_view terrain, std::string_view layer);

// Whether an instance carries a swarm: `instanceRandom.w < randomBelow`, and with `litOnly` a
// non-zero instance emissive. The test a material program repeats with a `threshold` on the same
// lane, so the two agree instance for instance.
[[nodiscard]] bool scatterAnchorGate(const spatial::InstanceRecord& record, const ScatterAnchor& spec);

struct ScatterAnchorPoint {
    glm::vec3 centre{0.0f}; // the crown's centre, world space
    std::uint64_t key = 0;  // (layer ordinal << 32) | instance index: a stable tie-break
};

struct ScatterAnchorSet {
    std::vector<ScatterAnchorPoint> points; // every instance that passed the gate
    std::size_t considered = 0;             // every instance of the named layers that was examined
    std::size_t lit = 0;                    // of those, the ones with a non-zero instance emissive
    std::vector<std::string> missing;       // named layers with no scatter object (or no instances)
    // The structure versions of every object read, so a caller can tell when to build it again.
    std::uint64_t version = 0;
};

// Every gated instance of the spec's layers in `objects`, each as the centre of its crown. The
// crown is the part of the asset that does not reach the ground: of an object's material parts
// (the lead and every object whose `partOf` names it), the one whose box starts highest -- the
// leaves, for a tree whose bark runs to the root. An asset with one part (a dead tree) takes the
// upper half of its box.
[[nodiscard]] ScatterAnchorSet scatterAnchorPoints(const std::vector<ProceduralGeometry>& objects,
                                                   const ScatterAnchor& spec);

// The same combined structure version `scatterAnchorPoints` records, without building anything.
[[nodiscard]] std::uint64_t scatterAnchorVersion(const std::vector<ProceduralGeometry>& objects,
                                                 const ScatterAnchor& spec);

// The anchors in use for a camera at `eye`: points within `viewDistance`, nearest first (ties by
// key), at most `maxAnchors`. Pure: the same points and eye give the same table.
[[nodiscard]] std::vector<glm::vec3> nearestScatterAnchors(std::span<const ScatterAnchorPoint> points,
                                                           const glm::vec3& eye, float viewDistance,
                                                           std::uint32_t maxAnchors);

} // namespace avgen::scene
