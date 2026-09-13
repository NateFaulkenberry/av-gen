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
#include "analysis/analysis_track.hpp"
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

// Folds a whole precomputed analysis into a musical structure.
//
// The whole track, not the frames so far. A structure is where the builds and drops *are*, which
// cannot be known from the frame you are on -- so directing is only possible against an analysis
// that has already been computed end to end, which is what offline mode does at load.
//
// A fresh detector is used rather than the engine's live one: the engine's has been walking the
// track as it plays and its state reflects wherever playback happens to be, so folding from it would
// give a different structure depending on when you pressed the button.
[[nodiscard]] Result<signals::MusicalStructure> structureOfTrack(const analysis::AnalysisTrack& track,
                                                                 int phraseBars = 4,
                                                                 int sectionPhrases = 4);

// The whole path: an engine holding an analysed track and a world holding heroes, to a camera that
// moves to the music. Returns how many tracks were installed.
//
// Main thread only, and offline-analysed audio only: a live input has no future to fold.
[[nodiscard]] Result<std::size_t> directEngine(Engine& engine,
                                               std::span<const world::HeroPoint> heroes,
                                               std::uint32_t seed = 1);

// ---- keeping a directed camera in step with the heroes -----------------------------------------
//
// Directing bakes: `installSequence` writes timeline tracks, and from then on the camera is those
// keyframes. So starring an object after a shot was cut changed nothing until the shot was cut
// again, and the way to do that was to hand the camera back to the viewport and re-direct it --
// two menu items to see the effect of one click.
//
// The fix is not to make the director live. A directed camera *is* a bake, deliberately: it is
// scrubbable, renderable offline and identical every time, none of which survives a camera that
// re-derives itself per frame. What can be automatic is noticing that the bake is out of date.

// What the application remembers about the shot it cut.
struct DirectorState {
    bool directed = false;            // the camera's automation is the director's, not somebody's work
    std::uint64_t heroRevision = 0;   // the hero set it was cut from (Composition::heroRevision)
    std::uint64_t placementRevision = 0;   // ...and where those heroes were
    // Whether the transport was playing last time this was asked. The settle that moves a hero
    // lands *inside* the frame's update, after this has run, so the last one of a playing stretch
    // arrives on the first parked frame -- and re-cutting for it there would be re-cutting for
    // something that happened while the piece was playing, one frame after somebody pressed pause.
    bool wasPlaying = false;
    std::uint32_t seed = 1;           // so a re-cut is the same shot minus what actually changed
};

enum class Redirect : std::uint8_t {
    Nothing,      // not directed, or the heroes have not moved on
    Recut,        // the shot was cut again from the heroes as they are now
    HandedBack,   // the last hero went, so the camera went back to the viewport
    Released,     // somebody else took the camera: the automation is gone or no longer ours
};

// Re-cuts the shot when the heroes have changed since it was cut. Call once a frame; it is a
// counter comparison until something actually changes.
//
// Self-healing rather than notified: it checks that the camera is still automated at all, so a
// project load, an undo, or a hand-deleted track leaves the state correct without every one of
// those places having to know that a director exists.
//
// Changing the *cast* re-cuts at once, wherever the playhead is: that is somebody clicking a star and
// asking to see the result. A hero merely *moving* re-cuts only while the transport is parked --
// during playback that is the world moving rather than an edit (Glowmere's wanderer walks), and
// replacing the whole film every time it stops for breath is what made the director look stuck on
// one hero. Movement during playback is absorbed, not queued, so pausing does not fire a re-cut for
// something that happened three minutes ago.
//
// An empty hero set hands the camera back instead of failing. A shot with nothing to point at is
// not a shot, and leaving the last trajectory running would be a camera flying a path towards
// something the user has just said is not there.
[[nodiscard]] Result<Redirect> refreshDirection(Engine& engine, DirectorState& state);

// Takes the camera back from the director: removes the tracks it owns and ends its claim. Returns
// how many tracks went.
//
// Everything that hands the camera back goes through here -- the menu item, the last hero being
// unstarred, and a viewport drag -- so there is one answer to what handing it back means. Tracks
// driving anything that is not the camera are somebody's work and are left alone.
std::size_t releaseDirectedCamera(Engine& engine, DirectorState& state);

// Records that the shot standing on the timeline *now* was cut from the heroes as they are *now*.
// Call after directing. One function rather than three assignments at every call site: a state that
// remembers the cast but not where it stood re-cuts on the very next frame, which is a mistake worth
// making impossible rather than documenting.
void noteDirected(Engine& engine, DirectorState& state);

// The camera parameters a directed sequence owns. Anything targeting one of these is replaced by
// `installSequence`; anything else survives.
[[nodiscard]] std::span<const std::string_view> directedCameraTargets();

} // namespace avgen::app
