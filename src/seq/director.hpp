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
#include "scene/composition.hpp"
#include "seq/sequence.hpp"

#include <functional>
#include <optional>

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
    // What the sequence's events resolved to (seq/events.hpp). The baked tier is already in the
    // timeline tracks above; this is what the host needs to keep: the clips for `applyAnimation`,
    // and the scheduled and live halves for an `EventDispatcher`.
    EventSchedule events;
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

// ADR-758: an actor that moves an entity's node, as the performance the composition applies to that
// entity -- its span (first to last key or path time), and its pose at a time as a pure function of
// the actor: position; speed from the path's own velocity, for the gait; heading from explicit
// rotation keys where the actor has them, else from the direction of travel, else the last direction
// it travelled in within the span. Nothing when the actor has no span (clips only, or one key).
// `groundAt`, when the scene has terrain, gives the pose its height: a directed body is airborne to
// its `ground` behaviour, so the performance itself must stand on the ground.
//
// ADR-820: `scheduled` is the install's clip schedule. Wherever the actor -- by an authored cue or a
// scheduled one -- names a clip inside the span, the pose says the sequencer owns the rig, and
// the gait yields it; elsewhere the gait picks from the path's speed. `entrySeconds` comes from the
// actor.
// ADR-821: what a cue actually plays at `seconds`. `lookup` answers what a clip measures as (its
// loop and its length, `scene::clipSemantics`); without one, `Auto` keeps the state's own looping
// and a `then` never fires, because a clip with no known length has no known end.
using ClipLookup = std::function<const scene::ClipSemantics*(std::string_view clip)>;
struct ResolvedCue {
    std::string clip;                // the state to be in; empty when `gait`
    double startSeconds = 0.0;       // its phase origin: the cue's time less its offset, or the one-shot's end
    float speed = 1.0f;
    float blendSeconds = -1.0f;
    std::optional<bool> loop;        // unset: the state's own
    bool gait = false;               // a `then: gait` has handed the rig back
};
[[nodiscard]] std::optional<ResolvedCue> resolveCue(const AnimationCue& cue, double seconds,
                                                    const ClipLookup& lookup);
// A clip event (clip seconds from `scene::ClipSemantics::events`) on the timeline, under `cue`:
// the cue's time plus the event's clip time over the cue's speed. Plan-time and pure.
[[nodiscard]] double clipEventSeconds(const AnimationCue& cue, float eventClipSeconds);
// What the rig on `node` measures its clips as, or an empty lookup when it has none.
[[nodiscard]] ClipLookup clipLookupFor(const scene::Composition& composition, const std::string& node);

[[nodiscard]] std::optional<scene::Composition::Performer> performerFor(
    const Actor& actor, std::string entity, std::function<float(float x, float z)> groundAt = {},
    std::span<const ScheduledClip> scheduled = {}, ClipLookup lookup = {});
// ...including the clips a `PlayClip` event scheduled. Same guarantee: pure in `seconds`.
void applyAnimation(const Sequence& sequence, std::span<const ScheduledClip> scheduled,
                    scene::Composition& composition, double seconds);

} // namespace avgen::seq
