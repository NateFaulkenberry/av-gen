#pragma once

// Simulation level of detail for vegetation (ADR-056): which specimens of a scatter layer are worth
// integrating, and the bookkeeping that lets the result reach the GPU without a pass over the
// population.
//
// The constraint this class exists to satisfy is that *nothing here may scale with the size of the
// layer*. A valley holds twenty-six thousand grass clumps and about twelve hundred of them are
// drawn; a few hundred are close enough for a viewer to see a tip overshoot. So:
//
//  * The instance positions are indexed into a uniform XZ grid once, when the scatter changes --
//    the same place, and the same O(n), as the bounds the culler already rebuilds there. Per frame
//    only the cells the camera can reach are visited, and even that is capped.
//  * The result is a compact array of at most `budget` entries plus a slot map. The slot map is a
//    per-record array, but it is *written* only where the active set changed, which is a handful of
//    entries a frame even when the camera is moving.
//  * A plant that has stopped moving stops being integrated, and is only polled for a reason to
//    wake.
//
// Nothing in this header touches the GPU; rendering/procedural_renderer.cpp owns the buffer and
// tests drive this class directly.

#include "core/plant_chain.hpp"
#include "core/wind.hpp"
#include "spatial/point_cloud.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace avgen::spatial {

class VegetationSim {
public:
    // Everything that changes per frame. The species side is Tier 0's own resolved response, so the
    // two tiers cannot be given different numbers by accident.
    struct Frame {
        glm::mat4 objectToWorld{1.0f};
        glm::vec3 cameraPosition{0.0f};
        float projScale = 1.0f;   // pixels per world unit at one unit (rendering::cullProjScale)
        float sourceRadius = 1.0f; // the source mesh's bounding radius, object units
        float extentY = 1.0f;      // the plant's height at unit instance scale
        float renderTime = 0.0f;
        float deltaTime = 1.0f / 60.0f;
        int budget = 0;            // slots this layer may hold this frame (the global budget's share)
        wind::WindUniforms wind{};
        wind::MotionResponse response{};
        wind::VegetationMotion motion{};
        const wind::DisturbanceField* disturbances = nullptr;
    };

    // Rebuilt when the scatter changes, never per frame.
    void setInstances(std::span<const InstanceRecord> records);
    void clear();
    [[nodiscard]] bool empty() const { return records_.empty(); }

    void update(const Frame& f);

    // (bend.xy, previous frame's bend.xy) per slot, in units of the plant's height: exactly the
    // vector shaders/wind.wgsl multiplies by the height profile. Slots the active set is not using
    // are zero and nothing references them.
    [[nodiscard]] std::span<const glm::vec4> dynamics() const { return dynamics_; }
    // Per record: 0 for "Tier 0, as before", otherwise the slot index plus one.
    [[nodiscard]] std::span<const std::uint32_t> slots() const { return slots_; }

    // Ranges of `slots` that changed this frame, coalesced. The renderer uploads these and nothing
    // else, which is what keeps the per-frame GPU traffic proportional to churn rather than to the
    // size of the layer.
    struct DirtySpan {
        std::uint32_t first = 0;
        std::uint32_t count = 0;
    };
    [[nodiscard]] std::span<const DirtySpan> dirtySlots() const { return dirty_; }
    // True when `slots` has never been uploaded (a fresh scatter): the renderer sends all of it once.
    [[nodiscard]] bool slotsDirtyAll() const { return slotsNew_; }
    void markSlotsUploaded() { slotsNew_ = false; }

    [[nodiscard]] std::uint32_t slotCount() const { return static_cast<std::uint32_t>(dynamics_.size()); }
    [[nodiscard]] std::uint32_t activeCount() const { return activeCount_; }   // holding a slot
    [[nodiscard]] std::uint32_t awakeCount() const { return awakeCount_; }     // integrated this frame
    [[nodiscard]] std::uint32_t examinedCount() const { return examined_; }    // records looked at
    [[nodiscard]] const wind::ChainTuning& tuning() const { return tuning_; }

private:
    struct Active {
        std::uint32_t record = 0;
        bool alive = false;
        float release = -1.0f; // < 0 live, else seconds into the hand-back
        float poll = 0.0f;     // stagger for the sleeping-plant wind poll
        glm::vec2 prevBend{0.0f};
        wind::PlantChain chain;
    };
    struct Candidate {
        std::uint32_t record = 0;
        float importance = 0.0f;
    };

    [[nodiscard]] float importanceOf(std::uint32_t record, const Frame& f, float objectScale,
                                     const glm::mat4& model) const;
    void gatherCandidates(const Frame& f, float objectScale, float radius, int wanted);
    void promote(std::uint32_t record, const Frame& f, const glm::mat4& model);
    void releaseSlot(std::size_t slot);
    void markDirty(std::uint32_t record);

    // What the simulation needs from a record, and nothing else: 32 bytes instead of 96, because
    // this is a copy of data the scene already holds and it is walked every frame.
    struct Plant {
        glm::vec3 position{0.0f};
        float radiusScale = 1.0f; // largest |scale| component: the record's contribution to size
        glm::vec4 random{0.0f};
    };
    std::vector<Plant> records_;
    // Uniform XZ grid over the record positions, in the object's own space (CSR).
    glm::vec2 gridOrigin_{0.0f};
    float gridCell_ = 1.0f;
    int gridX_ = 1, gridZ_ = 1;
    std::vector<std::uint32_t> gridStart_;
    std::vector<std::uint32_t> gridItems_;

    std::vector<Active> active_;
    std::vector<std::size_t> freeSlots_;
    std::vector<glm::vec4> dynamics_;
    std::vector<std::uint32_t> slots_;
    std::vector<DirtySpan> dirty_;
    std::vector<std::uint32_t> dirtyRecords_;
    std::vector<Candidate> candidates_;
    wind::ChainTuning tuning_;
    int budget_ = 0;
    std::uint32_t activeCount_ = 0;
    std::uint32_t awakeCount_ = 0;
    std::uint32_t examined_ = 0;
    std::uint32_t frame_ = 0;
    bool slotsNew_ = true;
};

} // namespace avgen::spatial
