#pragma once

// Installing a sequence into a running engine (ADR-089).
//
// `seq::Sequence` is a value: it knows how to bake itself into timeline JSON and it knows what every
// actor's rig should be playing at a given second. This file is the verb. It takes the bake, puts
// the tracks on a real `params::Timeline`, realises the overlay cues as real layers, binds, and
// says out loud what did not resolve.
//
// ## Two halves, and why
//
// `install()` runs when the sequence *changes*: on load, on an edit, on a scene swap. Everything it
// produces is ordinary timeline tracks, so between installs the per-frame cost of a sequence is the
// per-frame cost of the tracks it made -- which is the cost the timeline already had.
//
// `applyAnimation()` runs every frame and on every scrub. It exists because exactly one thing a
// sequence carries cannot be a track: which animation state a character is in, and *since when*.
// A track carries numbers; a clip's phase needs a time origin. So the cue's own absolute start
// second is pushed into the rig each frame, which makes the pose a pure function of the playhead
// and nothing else -- the same claim ADR-086 makes for the player itself.
//
// ## Ownership of tracks
//
// An install owns every parameter path its bake wrote. The next install is handed that list and
// erases those tracks before adding its own, so editing a shot replaces its automation instead of
// layering a second copy on top of it. Tracks the author wrote by hand against other paths are
// never touched. The one exception is a path that belonged to a layer the sink has just retired:
// the sink reports those, and they are erased too, because a track bound to a deleted layer is the
// silent-no-op failure ADR-075 exists to record.

#include "core/error.hpp"
#include "params/timeline.hpp"
#include "seq/layers.hpp"
#include "seq/sequence.hpp"

#include <span>
#include <string>
#include <vector>

namespace avgen::scene {
class Composition;
}

namespace avgen::seq {

struct InstallReport {
    int trackCount = 0;
    int keyCount = 0;
    // Every parameter path this install owns. Hand it back to the next install.
    std::vector<std::string> targets;
    // Things an author should be told. Not failures.
    std::vector<std::string> warnings;
    // Targets the parameter set has no parameter for. These tracks evaluate and write nothing, so
    // they are the one kind of problem that must never be left to the log alone.
    std::vector<std::string> unresolved;
    std::vector<OverlayBinding> overlays;
    int layersRealised = 0;
    int tracksReplaced = 0;
};

// Bakes and installs. `owned` is the previous install's `targets` (empty on the first).
//
// Order matters and is not arbitrary: the sink is cleared first so the bake realises fresh layers,
// the old tracks are erased before the new ones are added so a target is never written twice, and
// the bind happens last so every parameter the bake could possibly name already exists.
[[nodiscard]] Result<InstallReport> install(const Sequence& sequence, params::Timeline& timeline,
                                            params::ParameterSet& params, LayerSink& sink,
                                            std::span<const std::string> owned,
                                            const BakeOptions& options = {});

// Removes every track an install owns, without baking anything. Used when a sequence is cleared or
// a scene is about to be swapped out from under it.
void uninstall(params::Timeline& timeline, params::ParameterSet& params, LayerSink& sink,
               std::span<const std::string> owned);

// The state every animated actor's rig should be in at `seconds`, pushed into the composition.
// Pure in `seconds`: the same second produces the same pose whether it was played to or jumped to.
// Cheap enough to call unconditionally -- it is a handful of string comparisons per actor, and the
// composition ignores a request identical to the one already in force.
void applyAnimation(const Sequence& sequence, scene::Composition& composition, double seconds);

} // namespace avgen::seq
