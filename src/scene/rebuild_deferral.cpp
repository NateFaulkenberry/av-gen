#include "scene/rebuild_deferral.hpp"

#include <algorithm>

namespace avgen::scene {


// ---- interactive rebuild policy (docs/application-performance.md) -------------------------------
//
// Pure, so it can be reasoned about and tested without a scene, a clock or a GPU. `state` is the
// object's record, `wanted` the hash its inputs currently ask for, `built` the hash it is actually
// at, `elapsedMs` the wall time since the last poll. Returns true when the caller should regenerate
// now.
//
// The shape of it: an object whose inputs just moved restarts a settle timer, so a drag -- which
// moves them every frame -- never reaches the regeneration until it stops. Once they hold still for
// kSettleMs it regenerates. And because a drag can go on for seconds, there is a ceiling on how
// long it may be held back, scaled by what the object costs: something costing 150 ms refreshes at
// most every 600 ms while the drag continues, something costing 5 ms every 90, so no object spends
// more than about a fifth of the time regenerating. A fixed ceiling would either starve the cheap
// objects or hand the expensive ones the frame back again.
bool advanceRebuildDeferral(RebuildDeferral& state, std::uint64_t wanted, std::uint64_t built,
                            double elapsedMs) {
    // About five frames at 60 Hz: short enough that letting go of a slider feels immediate, long
    // enough that a drag cannot sneak a regeneration in between two of its own frames.
    constexpr double kSettleMs = 90.0;
    constexpr double kCostMultiple = 4.0;

    if (wanted == built) {
        state.deferring = false;
        state.settledForMs = 0.0;
        state.heldForMs = 0.0;
        return false;
    }
    if (!state.deferring) {
        state.deferring = true;
        state.deferredHash = wanted;
        state.settledForMs = 0.0;
        state.heldForMs = 0.0;
        return false;
    }
    state.heldForMs += elapsedMs;
    if (wanted != state.deferredHash) {
        state.deferredHash = wanted; // the inputs moved again: still wrong, but not yet still
        state.settledForMs = 0.0;
    } else {
        state.settledForMs += elapsedMs;
    }
    const double ceilingMs = std::max(kSettleMs, state.lastMs * kCostMultiple);
    if (state.settledForMs < kSettleMs && state.heldForMs < ceilingMs) {
        return false;
    }
    state.deferring = false;
    state.settledForMs = 0.0;
    state.heldForMs = 0.0;
    return true;
}

} // namespace avgen::scene
