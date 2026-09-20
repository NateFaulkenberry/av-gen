#pragma once

// A bounded, capped, frame-counted pre-roll, and the classification of what a frame's time means
// relative to the last one (ADR-397).
//
// Two subsystems arrived at the same mechanism independently. ADR-360 wanted it for particle pools:
// a render whose range opens at t > 0 starts with every pool empty and the field blooms in from
// nothing over one particle lifetime. The temporal-media work wants it for history buffers, on the
// framing that history is not state but a CACHE OF A PURE FUNCTION -- so a seek rebuilds it by
// re-running a bounded number of frames and nothing accumulates without bound. That is the same
// shape, and it is here once rather than twice.
//
// What is shared is the SCHEDULE, not the work. This header decides which timeline seconds a
// pre-roll consists of and what frame indices they carry; each subsystem re-runs its own simulation
// over them. That seam is deliberate: the schedule is the part that is easy to get subtly wrong,
// and it is checkable on the CPU with no device in the room.
//
// The bound is the whole point. A pre-roll is not a second unbounded re-simulation: it is n frames,
// n is capped, and a caller that asks for more gets the cap.

#include "core/time.hpp"

#include <cstdint>
#include <vector>

namespace avgen {

// How this frame's time relates to the previous frame's. The three that are not `First` need
// genuinely different treatment, and the failure this enum exists to prevent is treating two of
// them the same -- `ao_renderer.cpp` carries the scar (SYM-TERRAIN-1) where `Repeat` and `Jump`
// were one case.
enum class TimelineStep {
    // Nothing has been rendered yet. There is no history to keep or drop.
    First,
    // The same frame, rendered again: same index, same second. The first render legitimately had
    // history and used it, so dropping it now would make the second render a DIFFERENT PICTURE
    // from the first -- which is precisely the non-determinism the rule exists to prevent.
    // Re-rendering a frame has to reproduce it: restore the state the frame started with.
    Repeat,
    // The frame after the last one. History is valid and accumulates.
    Continuous,
    // A seek, a cut, a reverse. The previous frame is not this frame's past, so accumulating
    // against it smears across the jump. Drop the history -- and this is the case a pre-roll is
    // for, because dropping it leaves a hole somebody can see.
    Jump,
};

[[nodiscard]] const char* timelineStepName(TimelineStep step);

// `previous` is the last frame handed to this subsystem; `havePrevious` is false before the first.
//
// Both halves are read, and that matters. On frame index alone, a seek in the live application
// looks continuous: `RealtimeClock::seek` moves `renderTime` and leaves the counter climbing, so
// the frame after a scrub is index + 1 with a second from somewhere else. On render time alone, an
// offline re-render of one frame looks like a repeat of a frame that was never drawn. A step is
// `Continuous` only when the index advanced by one AND the second advanced by this frame's own
// delta; `Repeat` only when neither moved.
[[nodiscard]] TimelineStep classifyStep(bool havePrevious, const FrameTime& previous, const FrameTime& current);

// What a caller asks for.
struct PreRoll {
    std::uint32_t frames = 0;      // 0 = off, which is every caller that has not opted in
    std::uint32_t cap = 240;       // 4 s at 60 fps; a request above this is clamped, never honoured
    double stepSeconds = 0.0;      // 0 = take the arriving frame's own delta (1/60 when it has none)
};

struct PreRollPlan {
    // The frames to re-run, oldest first. Consecutive frame indices, each one `stepSeconds` after
    // the last, the newest landing exactly one step before the frame that is arriving.
    std::vector<FrameTime> frames;
    // The index the ARRIVING frame should carry so the whole sequence reads as continuous. It is
    // the arriving frame's own index whenever there is room below it for the roll, and is shifted
    // up only when there is not -- a render range that opens at frame 0 cannot have thirty-six
    // frames before it without going negative.
    //
    // A caller whose re-simulation does not key anything on the frame index can ignore this;
    // `ParticleRenderer` does. A caller that accumulates history across the roll and then wants the
    // arriving frame to continue it cannot: with the arriving frame still at its own low index,
    // `classifyStep` would call it a `Jump` and the history the roll just built would be dropped on
    // the frame it was built for.
    std::uint64_t arrivalFrameIndex = 0;
};

// The schedule for a pre-roll ending immediately before `time`. An empty plan when `roll.frames`
// is 0, which is the default and costs nothing.
[[nodiscard]] PreRollPlan planPreRoll(const PreRoll& roll, const FrameTime& time);

} // namespace avgen
