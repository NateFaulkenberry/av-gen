#pragma once

// HIST: per-owner transform history on the simulation's step instants (Effect Library Wave 1,
// docs/design/effect-library/shared-infrastructure.md "HIST").
//
// **What it is.** One ring per SUBSCRIBED node. An owner is subscribed only while some effect needs
// its history (a Trail, a type that reads its owner's velocity, a route whose source is one of its
// `entity.<name>.*` signals), and its depth is the longest any subscriber asked for. Each sample is
// the node's DRAWN world transform -- the root fold, the parent chain and the parameter finals,
// the same arithmetic the flattening and `ReplayPlacement` use -- at one simulation step instant.
//
// **Why it is exact under seek.** A play records one sample per `Engine::update`, after the
// controller has stepped and flattened. A seek does not play: it restores the nearest ADR-700
// checkpoint and replays forward on the fixed 1/60 grid. So the bank is recorded by the replay too
// (`Composition::seekWithDirector`'s per-step hook) and is carried IN the checkpoint (the host's
// capture/restore half). A scrub to second N therefore holds exactly the samples a 60 Hz play to N
// holds. That is ADR-700's property and HIST inherits it rather than adding a new one: a play that
// does not step on the 1/60 grid (a 30 fps render, a 144 Hz window) records at its own instants,
// and readers interpolate, so the SHAPE agrees while the samples are not bitwise the same.
//
// **What it is not exact about.** The replay applies no modulation routes (ADR-671's stated limit:
// a seek has no signal history). A node moved by an audio route records its un-modulated transform
// in a scrub. A node moved by a timeline track is handled: the host hands the replay a
// `HistoryAutomation`, which re-applies the track at each replayed instant for the recording only
// (it never reaches the simulation, whose inputs ADR-700 keys).
//
// **Trimming is part of the determinism.** `record` keeps exactly one sample at or before
// `latest - depth` and drops everything older, so a played ring and a restored-and-replayed ring
// hold the same set, and a reader cannot see further back in one than in the other.

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::params {
class IParameter;
}

namespace avgen::world {

// One step instant. 48 bytes: the design's 40 B of transform plus the instant it was taken at,
// which is what lets a reader interpolate across a play whose steps are not on the grid.
struct HistorySample {
    double t = 0.0;
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};
};

struct HistorySubscription {
    std::string node;
    float seconds = 0.0f; // how far back somebody reads; clamped to [kMinSeconds, kMaxSeconds]
};

// The play's automation of a node-transform parameter, re-applied by the seek replay for the
// recording only. `delta` is what the play's automation adds to the parameter's BASE at `seconds`
// (for a Replace track, keyed - base), so the recorded value is `final + delta` exactly as the
// play's final is `automated + offsets`. False when nothing automates that parameter.
class HistoryAutomation {
public:
    virtual ~HistoryAutomation() = default;
    [[nodiscard]] virtual bool transformDelta(const params::IParameter& param, double seconds,
                                              glm::vec3& delta) const = 0;
    // Hashes whatever `transformDelta` would answer, for the checkpoint key (`HistoryBank::key`).
    [[nodiscard]] virtual std::uint64_t key() const = 0;
};

class HistoryBank {
public:
    // The simulation's step (ADR-700's grid). Velocity is a backward difference over one step.
    static constexpr double kGridStep = 1.0 / 60.0;
    // The baseline acceleration is differenced over: long enough that a 60 Hz second difference
    // of a body settling on the ground reads as a settle and not as noise.
    static constexpr double kAccelBaseline = 0.1;
    // Every subscriber gets at least this much, so velocity and acceleration can always be read.
    static constexpr float kMinSeconds = 0.25f;
    static constexpr float kMaxSeconds = 16.0f;
    // The ring is sized for a play this fast; a faster one keeps a shorter tail rather than growing.
    static constexpr double kMaxRateHz = 240.0;

    // Replaces the subscription set. A node that stays subscribed keeps its samples (re-trimmed to
    // its new depth); a node that leaves loses them. Duplicates are merged (the deeper wins).
    // Returns true when the set -- names or depths -- changed, which is what invalidates the
    // checkpoints that were taken without it (see `key`).
    bool subscribe(std::span<const HistorySubscription> subscriptions);
    [[nodiscard]] bool empty() const { return rings_.empty(); }
    [[nodiscard]] std::size_t ringCount() const { return rings_.size(); }
    [[nodiscard]] std::string_view ringNode(std::size_t ring) const { return rings_[ring].node; }
    [[nodiscard]] float ringSeconds(std::size_t ring) const { return rings_[ring].seconds; }
    // Index of the ring for `node`, or `ringCount()`.
    [[nodiscard]] std::size_t find(std::string_view node) const;
    // A slot the recording host may keep its own lookup in (the node's index), so a replay of
    // thirteen thousand steps does not search the node list by name at every one. Never read here.
    [[nodiscard]] std::size_t& hostSlot(std::size_t ring) const { return rings_[ring].hostSlot; }

    // Appends one sample. An instant earlier than the ring's newest starts the ring again (the
    // transport looped or jumped); the same instant replaces the newest (a paused frame redrawn).
    void record(std::size_t ring, double t, const glm::vec3& position, const glm::quat& rotation,
                const glm::vec3& scale);
    // Drops every sample and keeps the subscriptions. A seek's first act.
    void clear();

    // ---- reading -----------------------------------------------------------------------------
    [[nodiscard]] std::size_t sampleCount(std::size_t ring) const { return rings_[ring].count; }
    // `i` = 0 is the oldest.
    [[nodiscard]] const HistorySample& sample(std::size_t ring, std::size_t i) const;
    [[nodiscard]] bool latest(std::string_view node, HistorySample& out) const;
    // The transform at `t`, interpolated (lerp, slerp). False outside the samples held.
    [[nodiscard]] bool sampleAt(std::string_view node, double t, HistorySample& out) const;
    [[nodiscard]] bool sampleAt(std::size_t ring, double t, HistorySample& out) const;
    // Metres per second at the newest sample: a backward difference over one grid step. False with
    // no sample; zero with one.
    [[nodiscard]] bool velocity(std::string_view node, glm::vec3& out) const;
    [[nodiscard]] bool velocity(std::size_t ring, glm::vec3& out) const;
    // Metres per second squared at the newest sample, over `kAccelBaseline`.
    [[nodiscard]] bool acceleration(std::size_t ring, glm::vec3& out) const;

    // ---- the checkpoint half (ADR-700) ---------------------------------------------------------
    struct Snapshot {
        std::vector<std::uint32_t> counts; // one per ring, in ring order
        std::vector<HistorySample> samples; // every ring's samples, oldest first, concatenated
        [[nodiscard]] std::size_t bytes() const {
            return counts.size() * sizeof(std::uint32_t) + samples.size() * sizeof(HistorySample);
        }
    };
    [[nodiscard]] Snapshot snapshot() const;
    // Refuses (and leaves the bank cleared) a snapshot taken with a different ring count -- which
    // the checkpoint key already rules out; this is the belt to that braces.
    void restore(const Snapshot& snapshot);
    // Hashes everything that decides what a replay records: the subscribed names and depths and
    // the automation's own key. Folded into the host's checkpoint key, so a new subscriber (whose
    // history no checkpoint holds) or an edited transform track drops the set.
    [[nodiscard]] std::uint64_t key() const;

    void setAutomation(const HistoryAutomation* automation) { automation_ = automation; }
    [[nodiscard]] const HistoryAutomation* automation() const { return automation_; }
    void setAutomationKey(std::uint64_t key) { automationKey_ = key; }

private:
    struct Ring {
        std::string node;
        float seconds = kMinSeconds;
        std::vector<HistorySample> buffer; // fixed capacity, circular
        std::size_t head = 0;              // index of the oldest
        std::size_t count = 0;
        mutable std::size_t hostSlot = static_cast<std::size_t>(-1);
    };
    [[nodiscard]] static std::size_t capacityFor(float seconds);
    [[nodiscard]] const HistorySample& at(const Ring& r, std::size_t i) const {
        return r.buffer[(r.head + i) % r.buffer.size()];
    }
    void trim(Ring& r);

    std::vector<Ring> rings_;
    const HistoryAutomation* automation_ = nullptr;
    std::uint64_t automationKey_ = 0;
};

} // namespace avgen::world
