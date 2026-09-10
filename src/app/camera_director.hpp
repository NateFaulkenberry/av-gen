#pragma once

// The camera director, connected (ADR-075).
//
// ADR-062 and ADR-071 built a shot vocabulary, a fold from musical moments into sections, and a
// director that turns sections into a `Sequence` which bakes down to ordinary timeline keys. All of
// it was tested and none of it was reachable: before this file, `cinematic.hpp` was included by
// exactly one other file in the repository, its own test. A camera director nothing calls is a
// camera that never moves.
//
// This is the join, and it is deliberately three small steps rather than one function, because each
// step is useful alone: a brief can be inspected before it is directed, a sequence can be looked at
// before it is installed, and installing is the only part that touches the engine.
//
// **Directing is a bake, not a per-frame decision.** A structure is a fold over a whole track --
// where the builds and drops are -- so it cannot be known from the frame you are on. Baking to
// timeline keys is also what keeps a directed camera identical between a 120 Hz window and a 30 fps
// offline render, which for a deterministic engine is the only acceptable answer. Nothing here runs
// per frame and nothing here runs on the audio thread.

#include "app/cinematic.hpp"
#include "core/error.hpp"
#include "signals/musical_events.hpp"
#include "world/hero.hpp"

#include <span>
#include <vector>

namespace avgen::app {

class Engine;

// Turns a world's heroes into something the director can shoot.
//
// The heroes arrive already ranked by importance (ADR-072 makes that ordering strict), so the
// brief's hero is simply the first and the rest are its supporting cast in the same order. That is
// the whole reason importance had to be strictly descending: a director choosing what a film is
// about cannot resolve a tie, and would otherwise pick by array position, which is not a decision
// anybody made.
[[nodiscard]] Result<DirectionBrief> briefFromHeroes(std::span<const world::HeroPoint> heroes);

// The whole path: heroes plus a track's structure into a validated sequence.
[[nodiscard]] Result<Sequence> directHeroes(std::span<const world::HeroPoint> heroes,
                                            const signals::MusicalStructure& structure,
                                            std::uint32_t seed = 1);

// Installs a sequence's baked tracks on the engine's timeline, replacing any tracks that drive the
// same camera parameters and leaving every other track alone.
//
// Replacing rather than appending, because two tracks writing `camera/position` is not a blend --
// it is whichever the timeline applies last, which is a bug that looks like the director being
// ignored. Leaving other tracks alone, because a project's automation of anything that is not the
// camera is somebody's work and directing the camera is not a reason to discard it.
//
// Main thread only: it mutates the timeline the renderer reads.
[[nodiscard]] Result<std::size_t> installSequence(Engine& engine, const Sequence& sequence);

// The camera parameters a directed sequence owns. Anything targeting one of these is replaced by
// `installSequence`; anything else survives.
[[nodiscard]] std::span<const std::string_view> directedCameraTargets();

} // namespace avgen::app
