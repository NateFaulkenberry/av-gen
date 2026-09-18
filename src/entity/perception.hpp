#pragma once

// The sense stage (ADR-270, ADR-290). What one character notices, out of everything there is.
//
// `src/entity/character_ai.hpp` §2 is the normative declaration of `Percept`, `PerceptionSettings`
// and `IPerception`; it compiled and nothing included it. This is the first implementation of that
// contract, and the header it belongs to rather than a second copy of the types.
//
// The shape is ADR-270's and the reasoning is measured rather than assumed:
//
//   * **Grid-backed.** Candidates come from two `spatial::PointGrid` radius queries -- one over the
//     world's interest points, one over its bodies -- never from a sweep over the 505-entry global
//     list every character reads today, and never from the analytic world.
//   * **Cadenced at `hertz`.** Not because the scan is expensive: it is 0.098 us at 60 m over the
//     real 505 points, four hundred times cheaper than one `Navigator::sample`. Because a character
//     that re-senses every frame reacts instantaneously and reads as a machine, and because
//     `Percept::seenAt` is meaningless if it is always now. Staleness is the only thing that lets a
//     character be *wrong* about where something is, which is most of what this buys over the
//     omniscient list it replaces.
//   * **Occlusion budgeted, and off by default.** `world::heroSightline` is the only real occlusion
//     this engine owns and it costs 1.7 ms at 20 m. One per character per frame at a hundred
//     characters is 172 ms a frame. So `occlusionTestsPerSecond` defaults to 0, `visibility` stays
//     1, and `tested` stays **false** -- which is honest, free, and says so.
//
// **Which position (R1, ADR-260).** Everything here reads and reports `EntityState::position()` --
// the simulation's answer -- and never `visualPosition()`. Glowmere's saucer carries a 2.4 m drift;
// perceived from its drawn position it would be noticed somewhere it is not standing, and a
// character sent to meet it would walk to the wrong place for reasons nothing could explain.
// Nothing in this file writes any of the three.

#include "core/error.hpp"
#include "entity/character_ai.hpp"
#include "spatial/point_grid.hpp"
#include "world/camera_clearance.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace avgen::entity {

// The most percepts a body may ever be asked to hold, and so the size its working set is allocated
// at, once, at bind. `PerceptionSettings::capacity` defaults to 8 and is a parameter an author may
// keyframe; a ceiling is what lets the storage be allocated once rather than resized on a knob.
inline constexpr std::size_t kPerceptCapacityMax = 64;

// The spatial indices a `GridPerception` scans, handed over by whoever owns the world.
//
// A struct of borrowed views rather than a second copy of the world, for the reason `NavDebug` is
// spans: the sense stage runs inside the entity update and must not allocate, and the grids are
// rebuilt by the thing that knows when a body moved.
//
// Both grids are **snapshots**, built before anything in this step moved, exactly as the crowd
// field is and for the same reason: building them as the bodies go would make what a character
// notices depend on the order the entities happen to be stored in.
struct PerceptionIndex {
    // Over `EntityWorld::interestPoints()`, minus the entries of kind `Character` -- those are
    // bodies and are scanned through `bodies` instead, where they are at where they are *now*
    // rather than at where the landmark list last recorded them.
    const spatial::PointGrid* interests = nullptr;
    // Grid point index -> index into `interestPoints()`. The grid holds a filtered subset, so the
    // two are not the same number and `Percept::source` must be the second one.
    std::span<const std::uint32_t> interestSource;
    // Over every entity, in entity order, so a grid point index **is** an index into
    // `EntityWorld::entities()` and `Percept::source` needs no translation.
    const spatial::PointGrid* bodies = nullptr;
    // Each entity's simulation position in the same snapshot. Read rather than re-queried so the
    // percept's position and the grid that found it cannot disagree.
    std::span<const glm::vec3> bodyPositions;

    [[nodiscard]] bool empty() const { return interests == nullptr && bodies == nullptr; }
};

// Which sense tick `time` falls in for a character with this cadence and this seed.
//
// A pure function of (time, hertz, seed) -- no accumulator, no wall clock, no frame index (D1).
// An accumulator would drift with the frame rate and would make a replayed sense tick land on a
// different instant from the played one; this lands on the same instants whatever the step was,
// which is what lets `EntityWorld::seek` reconstruct a character's working set rather than
// persist it (D4).
//
// The seed contributes a **phase** in [0, 1) tick, so a hundred characters at 4 Hz do not all
// re-sense on the same frame. It shifts when each body senses and never how often.
[[nodiscard]] std::uint64_t senseTick(double time, float hertz, std::uint32_t seed);

// The grid-backed sense stage: the real one.
//
// One instance per world, shared by every character in it. It holds no per-character state -- the
// round-robin occlusion cursor is derived from the tick index rather than remembered, which is
// what keeps `perceive` reproducible under a replay. The only mutable members are scratch buffers
// and the counters below, so **it is not safe to call `perceive` on one instance from two threads**
// and the entity update does not.
class GridPerception final : public IPerception {
public:
    // Structural quantities, not milliseconds (ADR-170). "It scanned 61 candidates and performed 2
    // occlusion tests" survives a change of machine in a way that "it took 40 us" does not, and it
    // is what a test can assert on -- the zero-tests control arm is exactly `occlusionTests == 0`.
    struct Counts {
        std::size_t calls = 0;          // `perceive` invocations
        std::size_t candidates = 0;     // points the two grid queries returned, before any filter
        std::size_t percepts = 0;       // percepts written to `out`, after the capacity cut
        std::size_t dropped = 0;        // percepts the capacity cut discarded
        std::size_t occlusionTests = 0; // `world::heroSightline` calls actually performed
        // Percepts left with `tested == false` because the budget was spent. **Never dropped**
        // (ADR-270): a percept silently discarded because a budget ran out makes a character's
        // behaviour depend on how many other characters exist.
        std::size_t occlusionDeferred = 0;
    };

    // The indices to scan, and the world the occlusion tests are cast through. `clearance` may be
    // a field with a null `map`, and then no occlusion test is ever performed and no percept
    // claims to have been tested -- which is the honest answer for a scene with no terrain, and is
    // not the same as testing and finding nothing in the way.
    void setIndex(const PerceptionIndex& index) { index_ = index; }
    void setClearance(const world::ClearanceField* clearance) { clearance_ = clearance; }

    [[nodiscard]] std::size_t perceive(const EntityWorld& world, std::size_t self,
                                       const PerceptionSettings& settings, std::uint32_t seed,
                                       double time, std::span<Percept> out) const override;

    [[nodiscard]] Counts counts() const { return counts_; }
    void resetCounts() const { counts_ = Counts{}; }

private:
    PerceptionIndex index_{};
    const world::ClearanceField* clearance_ = nullptr;
    // Scratch, reused across characters so a sense tick allocates nothing after the first one that
    // did. `perceive` is const because the interface is; these are the cost of that.
    mutable std::vector<std::uint32_t> hits_;
    mutable std::vector<Percept> candidates_;
    mutable Counts counts_{};
};

// The scripted sense stage: percepts a test writes by hand.
//
// The reason `IPerception` is an interface at all (character_ai.hpp §2): a decision layer has to be
// assertable without a world, exactly as `IPathProvider` lets an action be. It is also the control
// arm for the real one -- an assertion that passes against both is an assertion about the decider
// and not about the grid.
//
// Honours `capacity` and the salience order, and nothing else: no range, no facing, no occlusion.
// A scripted percept says what the script said it says, `tested` included.
class ScriptedPerception final : public IPerception {
public:
    void set(std::size_t self, std::vector<Percept> percepts);
    void clear() { scripted_.clear(); }

    [[nodiscard]] std::size_t perceive(const EntityWorld& world, std::size_t self,
                                       const PerceptionSettings& settings, std::uint32_t seed,
                                       double time, std::span<Percept> out) const override;

private:
    std::vector<std::pair<std::size_t, std::vector<Percept>>> scripted_;
};

// ---- JSON -------------------------------------------------------------------------------------

[[nodiscard]] Result<PerceptionSettings> perceptionFromJson(const nlohmann::json& j);
[[nodiscard]] nlohmann::json perceptionToJson(const PerceptionSettings& settings);

// The five `PerceptionSettings::weight` slots, in `InterestKind` order, as the names the parameter
// paths and the JSON use. One list, so a weight cannot be registered under one name and read under
// another -- which is how a setting stops being a setting (ADR-225).
[[nodiscard]] std::span<const std::string_view> perceptionWeightNames();

} // namespace avgen::entity
