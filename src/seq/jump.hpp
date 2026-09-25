#pragma once

// A jump as a scripted performance (ADR-822): the shared arc (`entity::planJump`) turned into what a
// `seq::Actor` carries, so a director compiles a jump with no arithmetic of its own and a play, a
// scrub and a bake of it are the one curve an autonomous hop also flies.

#include "entity/airborne.hpp"
#include "scene/clip_semantics.hpp"
#include "seq/sequence.hpp"

#include <optional>
#include <utility>
#include <vector>

namespace avgen::seq {

// Keys sampling `arc` from take-off at `launchSeconds` to touchdown, at `hz`, Linear -- so
// `Actor::positionAt` between them is the chord of a 1/hz slice of the arc. The last key is exactly
// on the landing point.
[[nodiscard]] std::vector<ActorKey> jumpKeys(const entity::JumpArc& arc, double launchSeconds, float hz = 60.0f);

// The actor's airborne span for that arc: [take-off, touchdown].
[[nodiscard]] std::pair<double, double> jumpSpan(const entity::JumpArc& arc, double launchSeconds);

// The cue that plays `clip` once with its measured flight laid over the arc's: its `takeoff` at the
// launch and its `touchdown` at the landing, by scaling its speed. Nothing when the clip has no
// measured flight to align (`ClipSemantics::event`). `then` follows the clip's end, as any cue's.
[[nodiscard]] std::optional<ClipCue> jumpClipCue(const scene::ClipSemantics& clip, const entity::JumpArc& arc,
                                                 double launchSeconds, std::string then = std::string(kThenGait));

// The three moments a director hangs things on, in timeline seconds.
struct JumpTimes {
    double takeoff = 0.0;
    double peak = 0.0;
    double touchdown = 0.0;
};
[[nodiscard]] JumpTimes jumpTimes(const entity::JumpArc& arc, double launchSeconds);

} // namespace avgen::seq
