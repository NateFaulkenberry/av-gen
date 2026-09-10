#include "spatial/vegetation_sim.hpp"

#include <algorithm>
#include <cmath>

namespace avgen::spatial {
namespace {

// About this many records per grid cell. Small enough that a query visits only what is near the
// camera, large enough that the cell rectangle around a fourteen-metre radius is a few dozen cells
// rather than a few thousand.
constexpr float kRecordsPerCell = 8.0f;
// Never look at more than this many records for every slot the layer is allowed. Without it a layer
// whose density spikes near the camera would turn a bounded cost into an unbounded one -- which is
// the single thing this whole design exists to prevent.
constexpr int kExamineFactor = 12;
// Two slot writes closer together than this are sent as one range: a four-byte write and a
// sixty-four-byte write cost the same, and the gaps are records nothing is looking at.
constexpr std::uint32_t kSpanGap = 16;
// A sleeping plant is asked whether the wind has picked up on one frame in this many, staggered so
// they do not all ask on the same frame. A wind sample is nine transcendentals; the point of sleep
// is not to pay them.
constexpr std::uint32_t kSleepPoll = 4;

[[nodiscard]] float maxAbsScale(const glm::vec4& s) {
    return std::max({std::fabs(s.x), std::fabs(s.y), std::fabs(s.z)});
}

[[nodiscard]] float matrixScale(const glm::mat4& m) {
    return std::max({glm::length(glm::vec3(m[0])), glm::length(glm::vec3(m[1])), glm::length(glm::vec3(m[2]))});
}

} // namespace

void VegetationSim::clear() {
    records_.clear();
    gridStart_.clear();
    gridItems_.clear();
    active_.clear();
    freeSlots_.clear();
    dynamics_.clear();
    slots_.clear();
    dirty_.clear();
    dirtyRecords_.clear();
    budget_ = 0;
    activeCount_ = 0;
    awakeCount_ = 0;
    examined_ = 0;
    slotsNew_ = true;
}

void VegetationSim::setInstances(std::span<const InstanceRecord> records) {
    clear();
    if (records.empty()) {
        return;
    }
    records_.reserve(records.size());
    for (const InstanceRecord& r : records) {
        records_.push_back(Plant{glm::vec3(r.position), maxAbsScale(r.scale), r.random});
    }
    slots_.assign(records.size(), 0u);

    glm::vec2 lo(records_[0].position.x, records_[0].position.z);
    glm::vec2 hi = lo;
    for (const Plant& r : records_) {
        lo = glm::min(lo, glm::vec2(r.position.x, r.position.z));
        hi = glm::max(hi, glm::vec2(r.position.x, r.position.z));
    }
    const glm::vec2 span = glm::max(hi - lo, glm::vec2(1e-3f));
    const float area = span.x * span.y;
    gridCell_ = std::clamp(std::sqrt(area * kRecordsPerCell / static_cast<float>(records.size())), 0.05f, 1e4f);
    gridOrigin_ = lo;
    gridX_ = std::clamp(static_cast<int>(span.x / gridCell_) + 1, 1, 4096);
    gridZ_ = std::clamp(static_cast<int>(span.y / gridCell_) + 1, 1, 4096);

    // Counting sort into CSR. Two passes over the records, at scatter time -- the same O(n) the
    // culler already spends there rebuilding its bounds, and never repeated per frame.
    const std::size_t cells = static_cast<std::size_t>(gridX_) * static_cast<std::size_t>(gridZ_);
    gridStart_.assign(cells + 1, 0u);
    const auto cellOf = [&](const Plant& r) {
        const int cx = std::clamp(static_cast<int>((r.position.x - gridOrigin_.x) / gridCell_), 0, gridX_ - 1);
        const int cz = std::clamp(static_cast<int>((r.position.z - gridOrigin_.y) / gridCell_), 0, gridZ_ - 1);
        return static_cast<std::size_t>(cz) * static_cast<std::size_t>(gridX_) + static_cast<std::size_t>(cx);
    };
    for (const Plant& r : records_) {
        ++gridStart_[cellOf(r) + 1];
    }
    for (std::size_t i = 1; i <= cells; ++i) {
        gridStart_[i] += gridStart_[i - 1];
    }
    gridItems_.resize(records.size());
    std::vector<std::uint32_t> cursor(gridStart_.begin(), gridStart_.end() - 1);
    for (std::size_t i = 0; i < records_.size(); ++i) {
        gridItems_[cursor[cellOf(records_[i])]++] = static_cast<std::uint32_t>(i);
    }
}

float VegetationSim::importanceOf(std::uint32_t record, const Frame& f, float objectScale,
                                  const glm::mat4& model) const {
    const Plant& r = records_[record];
    const glm::vec3 world = glm::vec3(model * glm::vec4(r.position, 1.0f));
    const float distance = std::max(glm::length(world - f.cameraPosition), 1e-4f);
    if (distance > f.motion.simulate.maxDistance) {
        return 0.0f;
    }
    // The same projected radius the culler and the geometric LOD ladder use (ADR-029), so "big
    // enough to be worth simulating" is measured in the same units as "big enough to draw at all"
    // and a threshold expressed in pixels means the same thing everywhere.
    const float radius = f.sourceRadius * r.radiusScale * objectScale;
    return radius / distance * f.projScale;
}

void VegetationSim::gatherCandidates(const Frame& f, float objectScale, float radius, int wanted) {
    candidates_.clear();
    examined_ = 0;
    if (wanted <= 0 || records_.empty()) {
        return;
    }
    const glm::mat4 model = f.objectToWorld;
    const glm::vec3 local = glm::vec3(glm::inverse(model) * glm::vec4(f.cameraPosition, 1.0f));
    const float localRadius = radius / std::max(objectScale, 1e-6f);
    const int minX = std::max(static_cast<int>((local.x - localRadius - gridOrigin_.x) / gridCell_), 0);
    const int maxX = std::min(static_cast<int>((local.x + localRadius - gridOrigin_.x) / gridCell_), gridX_ - 1);
    const int minZ = std::max(static_cast<int>((local.z - localRadius - gridOrigin_.y) / gridCell_), 0);
    const int maxZ = std::min(static_cast<int>((local.z + localRadius - gridOrigin_.y) / gridCell_), gridZ_ - 1);
    if (minX > maxX || minZ > maxZ) {
        return;
    }
    const int limit = wanted * kExamineFactor;
    for (int cz = minZ; cz <= maxZ && examined_ < static_cast<std::uint32_t>(limit); ++cz) {
        for (int cx = minX; cx <= maxX && examined_ < static_cast<std::uint32_t>(limit); ++cx) {
            const std::size_t cell = static_cast<std::size_t>(cz) * static_cast<std::size_t>(gridX_) +
                                     static_cast<std::size_t>(cx);
            for (std::uint32_t i = gridStart_[cell]; i < gridStart_[cell + 1]; ++i) {
                const std::uint32_t record = gridItems_[i];
                if (slots_[record] != 0u) {
                    continue; // already holds a slot; incumbents are handled before candidates
                }
                ++examined_;
                const float importance = importanceOf(record, f, objectScale, model);
                if (importance >= f.motion.simulate.minScreenRadius) {
                    candidates_.push_back({record, importance});
                }
            }
        }
    }
}

void VegetationSim::markDirty(std::uint32_t record) {
    dirtyRecords_.push_back(record);
}

void VegetationSim::releaseSlot(std::size_t slot) {
    Active& a = active_[slot];
    if (!a.alive) {
        return;
    }
    a.alive = false;
    slots_[a.record] = 0u;
    markDirty(a.record);
    dynamics_[slot] = glm::vec4(0.0f);
    freeSlots_.push_back(slot);
}

void VegetationSim::promote(std::uint32_t record, const Frame& f, const glm::mat4& model) {
    std::size_t slot;
    if (!freeSlots_.empty()) {
        slot = freeSlots_.back();
        freeSlots_.pop_back();
    } else {
        if (active_.size() >= static_cast<std::size_t>(std::max(budget_, 0))) {
            return; // the slot array is sized to the budget; it is a ceiling, not a target
        }
        slot = active_.size();
        active_.emplace_back();
        dynamics_.emplace_back(0.0f);
    }
    Active& a = active_[slot];
    a = Active{};
    a.record = record;
    a.alive = true;
    a.release = -1.0f;
    a.poll = static_cast<float>(record % kSleepPoll);

    // The continuity guarantee. The chain is initialised from the pose Tier 0 is drawing *this*
    // frame, and from the velocity Tier 0's own bend has right now (one extra field sample, at last
    // frame's time). The tip therefore starts exactly where the shader already had it and moving at
    // the speed it was already moving, so the switch is not a blend, a fade or a tolerance -- there
    // is nothing to see because nothing changed.
    const Plant& r = records_[record];
    const glm::vec3 world = glm::vec3(model * glm::vec4(r.position, 1.0f));
    const wind::WindSample now = wind::sampleWind(f.wind, world, f.renderTime - f.response.swayDelay);
    const glm::vec2 bend = wind::vegetationBend(now, f.response, r.random, f.renderTime);
    const float back = std::max(f.deltaTime, 1e-4f);
    const wind::WindSample then =
        wind::sampleWind(f.wind, world, f.renderTime - back - f.response.swayDelay);
    const glm::vec2 prev = wind::vegetationBend(then, f.response, r.random, f.renderTime - back);
    a.chain.setFromBend(bend, (bend - prev) / back);
    a.prevBend = prev;
    dynamics_[slot] = glm::vec4(bend.x, bend.y, prev.x, prev.y);
    slots_[record] = static_cast<std::uint32_t>(slot) + 1u;
    markDirty(record);
}

void VegetationSim::update(const Frame& f) {
    ++frame_;
    dirty_.clear();
    dirtyRecords_.clear();
    awakeCount_ = 0;
    examined_ = 0;
    const wind::SimLod& lod = f.motion.simulate;
    const int budget = std::min(f.budget, lod.budget);
    if (records_.empty() || budget <= 0 || !lod.enabled || !f.motion.active()) {
        for (std::size_t slot = 0; slot < active_.size(); ++slot) {
            releaseSlot(slot);
        }
        activeCount_ = 0;
        // Only the removals need to reach the GPU; everything else is already zero there.
        std::sort(dirtyRecords_.begin(), dirtyRecords_.end());
        for (std::uint32_t record : dirtyRecords_) {
            if (!dirty_.empty() && record < dirty_.back().first + dirty_.back().count + kSpanGap) {
                dirty_.back().count = record - dirty_.back().first + 1u;
            } else {
                dirty_.push_back({record, 1u});
            }
        }
        return;
    }

    const glm::mat4 model = f.objectToWorld;
    const float objectScale = matrixScale(model);
    const float dt = std::clamp(f.deltaTime, 1e-5f, 0.25f);

    wind::ChainParams cp;
    cp.omega0 = std::sqrt(std::max(f.motion.stiffness, 1e-3f) / std::max(f.motion.mass, 1e-6f));
    cp.zeta = f.motion.damping;
    cp.height = std::max(f.extentY * objectScale, 1e-3f);
    // The chain's own limit, not the visual one: the shader's soft ceiling is what a viewer sees,
    // and a chain leaning further than about two-thirds of its length is outside both the geometry
    // a four-segment stalk can express and the small-angle regime the spring was calibrated in.
    cp.maxLean = std::clamp(f.motion.bendLimit * 1.5f, 0.15f, 0.65f);
    tuning_ = wind::tuneChain(cp, dt);

    // ---- incumbents: keep, hand back, or drop -------------------------------------------------
    // Demotion uses a lower threshold than promotion (`hysteresis`), so a plant hovering on the
    // boundary is not promoted and demoted on alternate frames -- which would be a pop per frame
    // even though each individual transition is continuous.
    const float demote = lod.minScreenRadius * std::clamp(lod.hysteresis, 0.05f, 1.0f);
    std::uint32_t live = 0;
    for (std::size_t slot = 0; slot < active_.size(); ++slot) {
        Active& a = active_[slot];
        if (!a.alive) {
            continue;
        }
        if (a.release >= 0.0f) {
            a.release += dt;
            if (a.release >= std::max(lod.release, 1e-3f)) {
                releaseSlot(slot);
                continue;
            }
        } else if (importanceOf(a.record, f, objectScale, model) < demote) {
            a.release = 0.0f; // start handing the pose back to Tier 0
        }
        // A plant on its way down still holds its slot, so it still counts against the budget. It
        // has to: the slot array is sized to the budget, and a camera turning quickly can put a
        // whole active set into release at once.
        ++live;
    }

    // ---- promotions ---------------------------------------------------------------------------
    budget_ = budget;
    const int room = budget - static_cast<int>(live);
    if (room > 0) {
        gatherCandidates(f, objectScale, lod.maxDistance, room);
        if (static_cast<int>(candidates_.size()) > room) {
            std::nth_element(candidates_.begin(), candidates_.begin() + room, candidates_.end(),
                             [](const Candidate& a, const Candidate& b) { return a.importance > b.importance; });
            candidates_.resize(static_cast<std::size_t>(room));
        }
        for (const Candidate& c : candidates_) {
            promote(c.record, f, model);
        }
    }

    // ---- integrate ----------------------------------------------------------------------------
    const bool disturbed = f.disturbances != nullptr && !f.disturbances->empty();
    activeCount_ = 0;
    for (std::size_t slot = 0; slot < active_.size(); ++slot) {
        Active& a = active_[slot];
        if (!a.alive) {
            continue;
        }
        ++activeCount_;
        const Plant& r = records_[a.record];
        const glm::vec3 world = glm::vec3(model * glm::vec4(r.position, 1.0f));
        const bool poked = disturbed && f.disturbances->reaches(world);
        // A sleeper is not integrated and is not even asked what the wind is doing, except on one
        // frame in four -- and a disturbance always wakes it, because that test is two subtractions
        // and a compare rather than nine transcendentals.
        if (a.chain.asleep && !poked &&
            (frame_ + static_cast<std::uint32_t>(a.poll)) % kSleepPoll != 0u) {
            continue;
        }
        const wind::WindSample sample =
            wind::sampleWind(f.wind, world, f.renderTime - f.response.swayDelay);
        const glm::vec2 drive = wind::vegetationBend(sample, f.response, r.random, f.renderTime);
        if (a.chain.asleep) {
            // Wake when the field has moved away from where the plant is standing, which is the
            // only thing that could make it move.
            if (!poked && glm::length(drive - a.chain.bend()) < lod.sleepSpeed) {
                continue;
            }
            a.chain.asleep = false;
            a.chain.quiet = 0.0f;
        }
        const glm::vec3 push =
            disturbed ? f.disturbances->accelerationAt(world) * std::max(f.motion.windSensitivity, 0.0f)
                      : glm::vec3(0.0f);
        wind::stepChain(a.chain, tuning_, drive, push, cp.height, cp.maxLean, dt);
        ++awakeCount_;

        glm::vec2 bend = a.chain.bend();
        if (a.release >= 0.0f) {
            // The way down. The buffer carries a blend from the simulated pose to the one Tier 0 is
            // about to draw, so by the time the slot is freed the two are the same number and the
            // shader's switch changes nothing.
            const float u = std::clamp(a.release / std::max(lod.release, 1e-3f), 0.0f, 1.0f);
            bend = glm::mix(bend, drive, u);
        } else if (!poked && a.chain.speed() < lod.sleepSpeed &&
                   glm::length(bend - drive) < lod.sleepSpeed) {
            a.chain.quiet += dt;
            if (a.chain.quiet >= std::max(lod.sleepSeconds, 0.0f)) {
                a.chain.asleep = true;
            }
        } else {
            a.chain.quiet = 0.0f;
        }
        dynamics_[slot] = glm::vec4(bend.x, bend.y, a.prevBend.x, a.prevBend.y);
        a.prevBend = bend;
    }

    // ---- churn, coalesced ----------------------------------------------------------------------
    std::sort(dirtyRecords_.begin(), dirtyRecords_.end());
    dirtyRecords_.erase(std::unique(dirtyRecords_.begin(), dirtyRecords_.end()), dirtyRecords_.end());
    for (std::uint32_t record : dirtyRecords_) {
        if (!dirty_.empty() && record < dirty_.back().first + dirty_.back().count + kSpanGap) {
            dirty_.back().count = record - dirty_.back().first + 1u;
        } else {
            dirty_.push_back({record, 1u});
        }
    }
}

} // namespace avgen::spatial
