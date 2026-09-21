#pragma once

// The provider fallback chain (ADR-541 corollary 1, Phase B §33, Phase E §36).
//
// **Neural → Motion Matching → Procedural/Clip**, tried in order, first one that produces a pose
// wins. Built now, with one implementation behind it, because the ordering and the fallback
// semantics are the part that is expensive to retrofit -- not the providers.
//
// ADR-541 corollary 1: *a provider is a sibling of `AnimationPlayer`, never a replacement. Every
// character can run in Clip mode; Clip mode is the fallback for every failure.* That makes the last
// entry in the chain special: it is the one that must not fail, and a chain whose last entry
// declines leaves the character posed by nothing. `resolve` reports that rather than hiding it.
//
// **Why not a weight or a blend.** A chain is a selection, not a mix. Two providers producing two
// poses and averaging them is a character doing neither thing; the reason to have a neural provider
// at all is that its answer is better, and the reason to fall back is that it has no answer. There
// is no meaningful halfway.

#include "entity/motion_provider.hpp"

#include <array>
#include <cstdint>

namespace avgen::entity {

// What happened when the chain was asked.
struct MotionChainResult {
    MotionResult result;
    // Which provider answered, as an index into the chain, or -1 when none did. Reported rather
    // than inferred, because "the clip player answered" and "the matcher answered" look identical
    // in the pose and completely different in a bug report.
    int provider = -1;
    // How many declined before one answered. Zero in the steady state; a number that climbs is the
    // tell that a preferred provider has quietly stopped working, which is the failure this chain
    // would otherwise hide perfectly.
    std::uint32_t fellThrough = 0;
    // The status of the FIRST provider that declined, kept because that is the interesting one:
    // the chain's own result is whatever the fallback said, which is always "Produced".
    MotionStatus firstDeclined = MotionStatus::Produced;
    [[nodiscard]] bool ok() const { return result.ok(); }
};

// An ordered, non-owning list of providers.
//
// Fixed capacity and no allocation: this is consulted once per character per frame. Four is the
// whole roadmap -- neural, matcher, procedural, clip -- with room to be wrong once.
class MotionChain {
public:
    static constexpr std::size_t kMaxProviders = 5;

    // Appends. Order is priority: the first added is tried first, and the last added is the
    // fallback that is expected never to fail. Returns false when full.
    bool add(const IMotionProvider* provider) {
        if (provider == nullptr || count_ >= kMaxProviders) {
            return false;
        }
        providers_[count_++] = provider;
        return true;
    }
    void clear() { count_ = 0; }
    [[nodiscard]] std::size_t size() const { return count_; }
    [[nodiscard]] const IMotionProvider* at(std::size_t i) const {
        return i < count_ ? providers_[i] : nullptr;
    }

    // **The simulation half.** Try each in order until one advances the memory.
    //
    // `next` is written by whichever provider answered, and records *which* one in
    // `MotionMemory::provider` so that `pose` below goes back to the same one. A provider that
    // declines has still written its own `next`, and that write is **discarded**: the memory
    // belongs to the character and to whatever is actually driving it, so a failed neural
    // provider must not leave its latent state where the clip player's local time should be.
    // That is why the scratch copy exists.
    [[nodiscard]] MotionChainResult advance(const MotionRequest& request, const MotionMemory& in,
                                            double time, float dt, MotionMemory& next) const {
        MotionChainResult chain;
        for (std::size_t i = 0; i < count_; ++i) {
            MotionMemory scratch = in;
            const MotionResult r = providers_[i]->advance(request, in, time, dt, scratch);
            if (r.ok()) {
                next = scratch;
                next.provider = static_cast<int>(i);
                chain.result = r;
                chain.provider = static_cast<int>(i);
                return chain;
            }
            if (chain.fellThrough == 0) {
                chain.firstDeclined = r.status;
            }
            ++chain.fellThrough;
        }
        // Nobody answered. `next` is left as it came in rather than zeroed: the character keeps
        // the memory it had, which is a frozen body -- bad, and enormously better than a body
        // snapped to its bind pose because a provider was still loading.
        next = in;
        chain.result.status = count_ == 0 ? MotionStatus::NoContent : chain.firstDeclined;
        return chain;
    }

    // **The presentation half.** Ask the provider that settled this memory to draw it.
    //
    // No fallback here, deliberately. If the provider that advanced the memory cannot pose it,
    // that is a bug in that provider, and quietly asking a different provider to interpret another
    // one's memory would turn it into a wrong pose instead of a visible failure (§64).
    [[nodiscard]] MotionResult pose(const MotionMemory& memory, const scene::Skeleton& skeleton,
                                    scene::Pose& out) const {
        MotionResult r;
        if (memory.provider < 0 || static_cast<std::size_t>(memory.provider) >= count_) {
            r.status = MotionStatus::NoContent;
            return r;
        }
        return providers_[static_cast<std::size_t>(memory.provider)]->pose(memory, skeleton, out);
    }

private:
    std::array<const IMotionProvider*, kMaxProviders> providers_{};
    std::size_t count_ = 0;
};

} // namespace avgen::entity
