#pragma once

// The interactive-rebuild policy (ADR-084 §9.2), as a pure function: state in, decision out, no
// clock read inside.
//
// It lived in composition.hpp beside its one caller until a second caller appeared with exactly the
// same problem in a different subsystem -- the procedural sky's IBL, which a lighting drag rebuilt
// inside the frame, sixty times a second, at forty to seventy milliseconds a time. Nothing about
// the arithmetic was ever about procedural geometry: it is about *any* expensive derived thing
// whose inputs a person is dragging. So it is here, in a header light enough for the renderer to
// include, and `Composition::ProceduralRebuildState` is an alias for the state so nothing that
// already named it has to change.
//
// The shape of it: a thing whose inputs just moved restarts a settle timer, so a drag -- which
// moves them every frame -- never reaches the rebuild until it stops. Once they hold still for
// kSettleMs it rebuilds. And because a drag can go on for seconds, there is a ceiling on how long
// it may be held back, scaled by what the rebuild costs: something costing 150 ms refreshes at most
// every 600 ms while the drag continues, something costing 5 ms every 90, so nothing spends more
// than about a fifth of the time rebuilding. A fixed ceiling would either starve the cheap things
// or hand the expensive ones the frame back again.
//
// **Two timers, not one, and they answer different questions.** `settledForMs` asks "have the
// inputs stopped moving?" and restarts whenever they move again. `heldForMs` asks "how long has
// this been wrong?" and only resets on an actual rebuild. With one timer the second question cannot
// be asked at all: the first thing a moving target does is reset it, which is how the first version
// of this never refreshed during a drag at all.

#include <cstdint>

namespace avgen::scene {

struct RebuildDeferral {
    double lastMs = 0.0;            // what the last rebuild actually cost
    double settledForMs = 0.0;      // since the inputs last moved
    double heldForMs = 0.0;         // since it first wanted to rebuild and was not let
    bool deferring = false;
    std::uint64_t deferredHash = 0; // the hash it is waiting to reach
};

// True means rebuild now. Pinned in tests/unit/test_composition.cpp, because the behaviour that
// matters -- a drag never reaching a rebuild, and a released slider always reaching one -- is a
// property of this arithmetic and not of any scene.
[[nodiscard]] bool advanceRebuildDeferral(RebuildDeferral& state, std::uint64_t wanted, std::uint64_t built,
                                          double elapsedMs);

// The same arithmetic, for a playhead. A scrub is a person dragging the input to an expensive
// derived thing -- `EntityWorld::seek` re-integrates every entity from `target - 90 s` -- which is
// precisely what the header above says this policy is about, so the seek borrows it rather than
// growing a second copy beside it.
//
// One thing a seek needs that a slider does not: **release**. A slider that stops moving might
// still be held, and there is no way for the arithmetic to tell; a pointer that comes up is an
// unambiguous end of gesture, and waiting kSettleMs after it would make a single click 90 ms slower
// for no reason at all. So `held` short-circuits: a request that is not part of a held gesture is
// evaluated on the frame it arrives, which makes a click behave exactly as it does today and
// confines the whole change to the drag.
//
// The positions are compared bit for bit, via the same "did the input move" test the hash form
// uses. Two seconds that differ in the last bit are two different requests, which is right: the
// evaluated state genuinely differs between them and pretending otherwise would be the start of a
// tolerance nobody could justify a value for.
[[nodiscard]] bool advanceSeekDeferral(RebuildDeferral& state, double requestedSeconds,
                                       double evaluatedSeconds, bool held, double elapsedMs);

} // namespace avgen::scene
