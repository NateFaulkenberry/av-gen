#include "entity/attention.hpp"

#include <algorithm>
#include <cmath>

namespace avgen::entity {
namespace {

const AttentionCandidate* find(const std::vector<AttentionCandidate>& candidates, std::uint64_t id) {
    if (id == 0) {
        return nullptr;
    }
    for (const AttentionCandidate& c : candidates) {
        if (c.id == id) {
            return &c;
        }
    }
    return nullptr;
}

// The strongest candidate, by salience. Ties are broken by **id**, not by list order: the list is
// rebuilt every frame by whoever perceives the world, and two equally salient things would
// otherwise swap places whenever that rebuild reordered them -- a thrash with no cause in the
// world at all.
const AttentionCandidate* strongest(const std::vector<AttentionCandidate>& candidates) {
    const AttentionCandidate* best = nullptr;
    for (const AttentionCandidate& c : candidates) {
        if (best == nullptr || c.salience > best->salience ||
            (c.salience == best->salience && c.id < best->id)) {
            best = &c;
        }
    }
    return best;
}

float smooth(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - (2.0f * t));
}

} // namespace

AttentionResult chooseAttention(const AttentionSettings& settings, const AttentionState& in,
                                const std::vector<AttentionCandidate>& candidates, double time,
                                AttentionState& next) {
    next = in;
    AttentionResult out;

    const AttentionCandidate* held = find(candidates, in.target);
    const double heldFor = in.started && in.target != 0 ? time - in.acquired : 0.0;
    const AttentionCandidate* challenger = strongest(candidates);

    // ---- should the current target be dropped? -------------------------------------------------
    //
    // Three ways out, and they are different questions. It vanished from the world; it stopped
    // being worth looking at; or it has been looked at long enough (§22's `duration`).
    bool drop = false;
    bool examined = false;
    if (held == nullptr) {
        drop = true;                                   // gone from the candidate list entirely
    } else if (held->salience < settings.releaseThreshold) {
        drop = true;                                   // no longer worth attending to
    } else {
        const float hold = held->duration > 0.0f ? held->duration : settings.maxHoldSeconds;
        if (heldFor >= static_cast<double>(hold)) {
            drop = true;                               // looked at it long enough
            examined = true;                           // ...and that is a fact with consequences
        }
    }

    // ---- may a challenger take over? -----------------------------------------------------------
    //
    // **Only after the dwell, and only by a margin.** The two catch different failures: the margin
    // stops a *near-equal* rival, and the dwell stops a *briefly stronger* one. A selector with
    // only the margin snaps away the instant something spikes and snaps back -- which is the thing
    // a viewer actually notices, and why the tests here assert on dwell as well as on count.
    const bool dwellSatisfied = !in.started || in.target == 0 ||
                                heldFor >= static_cast<double>(settings.minDwellSeconds);
    std::uint64_t chosen = drop ? 0u : in.target;
    // A target whose hold has just expired goes into a refractory period. Without it the same
    // candidate is re-acquired on the very frame it was released -- it is still the most salient
    // thing in the world -- and `maxHoldSeconds` does nothing at all.
    if (examined) {
        next.cooling = in.target;
        next.coolUntil = time + static_cast<double>(settings.refractorySeconds);
    }
    const bool cooling = next.cooling != 0 && time < next.coolUntil;
    if (challenger != nullptr && cooling && challenger->id == next.cooling) {
        // The loudest thing is the one we have just finished looking at. Take the next best, or
        // nothing -- which is a character looking away, and is the point.
        const AttentionCandidate* second = nullptr;
        for (const AttentionCandidate& c : candidates) {
            if (c.id == next.cooling) {
                continue;
            }
            if (second == nullptr || c.salience > second->salience ||
                (c.salience == second->salience && c.id < second->id)) {
                second = &c;
            }
        }
        challenger = second;
    }
    if (challenger != nullptr && challenger->salience >= settings.acquireThreshold) {
        if (chosen == 0) {
            chosen = challenger->id;                   // nothing held; take the best there is
        } else if (challenger->id != chosen && dwellSatisfied) {
            const float incumbent = held != nullptr ? held->salience : 0.0f;
            if (challenger->salience > incumbent * (1.0f + settings.switchMargin)) {
                chosen = challenger->id;
            }
        }
    }

    out.changed = chosen != in.target || !in.started;
    if (chosen != in.target) {
        next.target = chosen;
        next.acquired = time;
    }
    next.started = true;

    const AttentionCandidate* now = find(candidates, chosen);
    out.target = chosen;
    out.hasTarget = now != nullptr;
    out.dwell = static_cast<float>(chosen == in.target && in.started ? heldFor : 0.0);
    if (now != nullptr) {
        out.position = now->position;
        // Ease in on acquisition, and out as the hold runs down, so the head turns rather than
        // snapping and so a layer can multiply by this without knowing what attention is.
        const float hold = now->duration > 0.0f ? now->duration : settings.maxHoldSeconds;
        const float fadeIn = smooth(out.dwell / std::max(settings.minDwellSeconds * 0.5f, 1e-3f));
        const float remaining = std::max(hold - out.dwell, 0.0f);
        const float fadeOut = smooth(remaining / std::max(settings.minDwellSeconds * 0.5f, 1e-3f));
        out.weight = std::min(fadeIn, fadeOut);
    }
    return out;
}

} // namespace avgen::entity
