#pragma once

// The Auto-director, connected (ADR-075).
//
// ADR-062 and ADR-071 built a shot vocabulary, a fold from musical moments into sections, and a
// director that turns sections into a `Sequence` which bakes down to ordinary timeline keys. All of
// it was tested and none of it was reachable: before this file, `cinematic.hpp` was included by
// exactly one other file in the repository, its own test. A Auto-director nothing calls is a
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
#include "app/song_director.hpp"
#include "app/song_plan.hpp"
#include "core/error.hpp"
#include "analysis/analysis_track.hpp"
#include "signals/musical_events.hpp"
#include "world/hero.hpp"

#include <nlohmann/json_fwd.hpp>

#include <span>
#include <string>
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
// What the Auto-director panel can set, and **only what it can set**.
//
// Every field here changes the film. Three that a panel would obviously want are deliberately
// absent, because a knob wired to nothing spends the user's trust:
//
//   * `CompositionProfile::framing` and `headroom` -- WIRED, despite what this comment used to say:
//     `framedAim` in cinematic.cpp reads both and three call sites use it. Left here corrected
//     rather than deleted, because ADR-245's migration table repeated the stale claim.
//   * `HeroPoint::preferredCameraElevationDegrees` -- ALSO WIRED now, via `preferredElevationDegrees`,
//     which cinematic.cpp reads. The note below describes how it used to be:
//     authored per hero and never read by the
//     director.
//   * `Shot::speed` -- real, but only through `Sequence::retime()`, which the director never calls;
//     exposing it as "camera speed" would move nothing.
//
// Those are documented as future extensions rather than shipped as inert sliders.
struct AutoDirectorSettings {
    DirectorMode mode = DirectorMode::ContinuousShot;
    // Shot timing, in seconds. A passage longer than `maxShotSeconds` becomes several shots inside
    // one section; below `minShotSeconds` a section is chopped rather than cut; a build is exempt
    // down to `minBuildShotSeconds` because a build exists to end.
    double minShotSeconds = 5.0;
    double minBuildShotSeconds = 2.0;
    double maxShotSeconds = 12.0;
    // The two lenses the film is shot on, in millimetres: the wide for establishing and drifting,
    // Same seed, same heroes, same structure, same film. Exposed because re-cutting with a different
    // seed is the one way to ask for a different edit of the same piece.
    std::uint32_t seed = 1;
    // The fastest the camera may travel, in metres per second. 0 -- the default -- leaves the cut
    // exactly as it was (ADR-200).
    //
    // What gives way is the *distance*, never the timing: a cut here lands on the music, so slowing
    // a shot by lengthening it would move every cut after it off the beat it was built for. A shot
    // that is too fast covers too much ground for the time the music gave it, and the ground is what
    // shrinks.
    float maxCameraSpeed = 0.0f;
    // The fastest the *view* may swing, in degrees per second. 0 is off (ADR-200).
    //
    // A second control rather than a refinement of the first, because measurement said they are two
    // problems. Capping the camera to 1 m/s on a reference cut took its travel from 23.2 to 1.0 and
    // left the view rotating at 63.7 deg/s -- *faster* than the 52.0 it started at, because a camera
    // that moves less still has to sweep its aim the same distance in the same time. What a viewer
    // calls "moving too fast" is nearly always this one.
    float maxViewRate = 0.0f;
    // Consecutive shots one subject keeps before the rotation moves on (ADR-203). 1 is a new
    // subject every shot.
    //
    // A second control rather than more importance, because importance cannot express this: it sets
    // how often a subject's turn comes round, and with a cast of eleven every turn was one shot long
    // however high the slider went. "I'm not sure I can control that enough with just the importance
    // param" -- correct, and this is the thing that was missing.
    int dwellShots = 1;
    // How far a key may be moved to restore a hero's line of sight, as a fraction of that key's
    // distance to the hero (ADR-080's missing half; see `world::clearSightlines`).
    //
    // 0 turns the pass into a pure measurement -- every obstruction is still found and reported, and
    // nothing is moved -- which is what the lab's control arm runs with. The default 0.25 is read
    // off `defaultsFor`'s own elevation table rather than chosen: `Shot::startElevation` is a height
    // as a multiple of the orbit radius, exactly this ratio, and every ordinary shot kind composes
    // below 0.25 of it (Establish 0.22, Orbit 0.24, Approach 0.20, Track 0.12). A larger correction
    // moves the eye further than the whole range of elevations the vocabulary works in, which is
    // what "this is no longer the shot anybody composed" means in numbers.
    float maxSightlineCorrection = 0.25f;
    // How much freedom Song Mode has (ADR-249). Read by `DirectorMode::Song` and by nothing else --
    // the other two modes have no authored intent to be more or less faithful to.
    //
    // A **ceiling**, not a setting: the effective autonomy of a section is the lesser of this and
    // the section's own, so this control can always be trusted to reduce. Defaults to `Expressive`
    // because a plan's own sections default to `Guided`, and a film-wide control whose default
    // silently overrode every section would be a control that appeared to do nothing when lowered
    // and everything when raised.
    Autonomy autonomy = Autonomy::Expressive;

    [[nodiscard]] Result<void> validate() const;
    void applyTo(DirectionBrief& brief) const;

    // ADR-225: the project's `autoDirector` block (`director` is the World Director's, ADR-088). Keyed by the names `--director` already uses for the
    // same fields, so one setting has one spelling whether it arrives from a command line or a
    // file. `fromJson` refuses a document `validate()` would refuse rather than clamping into one.
    [[nodiscard]] nlohmann::json toJson() const;
    [[nodiscard]] static Result<AutoDirectorSettings> fromJson(const nlohmann::json& doc);

    // So a host can tell "the user moved something" from "nothing happened" without a memcmp over
    // a struct with padding in it, which is what the panel used to do.
    friend bool operator==(const AutoDirectorSettings&, const AutoDirectorSettings&) = default;
};

// A one-line account of what the last cut does, for the panel. Empty until something is directed.
[[nodiscard]] std::string lastDirectionSummary();

[[nodiscard]] Result<Sequence> directHeroes(std::span<const world::HeroPoint> heroes,
                                            const signals::MusicalStructure& structure,
                                            const AutoDirectorSettings& settings = {});

// ---- Song Mode (ADR-249) -------------------------------------------------------------------------
//
// The same three steps as above, against an authored song plan instead of a fold of the audio: a
// brief from the heroes, a direction from the plan, and an install that touches the engine. What is
// different is that a Song Mode direction has two halves -- a framing bake for the Auto-director's
// own camera, and a *camera track* naming which camera is seen when -- so the install writes to the
// composition's `CameraDirection` as well as to the timeline.

// Heroes plus a plan plus the scene's cameras into a direction. Engine-free, like `directHeroes`:
// a direction can be inspected before anything is installed.
[[nodiscard]] Result<SongDirection> directSongFromPlan(std::span<const world::HeroPoint> heroes,
                                                       const SongPlan& plan,
                                                       const scene::CameraDirection& cameras,
                                                       const AutoDirectorSettings& settings);

// Installs both halves. The timeline half is `installSequence`; the camera-track half replaces every
// shot the director owns (`CameraShot::Origin::Directed`) and leaves every authored shot exactly
// where it was -- the same rule the timeline half follows, for the same reason.
//
// Main thread only: it mutates the timeline and the composition the renderer reads.
[[nodiscard]] Result<std::size_t> installSongDirection(Engine& engine,
                                                       const SongDirection& direction,
                                                       const AutoDirectorSettings& settings);

// The plan the engine should be directed to, which is the authored one when there is one and a
// measurement-derived stand-in when there is not (`songPlanFromMeasurements`). Separate from
// `directEngine` because "what would Song Mode direct" is a question a panel asks without wanting
// to cut anything.
[[nodiscard]] Result<SongPlan> songPlanForEngine(const Engine& engine);

// Installs a sequence's baked tracks on the engine's timeline, replacing any tracks that drive the
// same camera parameters and leaving every other track alone.
//
// Replacing rather than appending, because two tracks writing `camera/position` is not a blend --
// it is whichever the timeline applies last, which is a bug that looks like the director being
// ignored. Leaving other tracks alone, because a project's automation of anything that is not the
// camera is somebody's work and directing the camera is not a reason to discard it.
//
// Main thread only: it mutates the timeline the renderer reads.
// `settings` is read only for the parts that are not in the baked sequence -- at present ADR-217's
// hold, which is a live rule rather than a key. Omitted (the default) means no hold, which is what
// every call before ADR-217 meant and what an untouched project still means.
[[nodiscard]] Result<std::size_t> installSequence(Engine& engine, const Sequence& sequence,
                                                  const AutoDirectorSettings& settings = {});

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

// The whole path: an engine holding an analyzed track and a world holding heroes, to a camera that
// moves to the music. Returns how many tracks were installed.
//
// Main thread only, and offline-analyzed audio only: a live input has no future to fold.
// `settings` has no default, on purpose. It used to, and the Enable button omitted it -- so every
// control in the panel was bound to a struct the first cut never read, and choosing Continuous shot
// did nothing at all. A defaulted argument that silently means "ignore what the user chose" is worth
// a compile error at every call site instead of a test that has to remember to exist.
[[nodiscard]] Result<std::size_t> directEngine(Engine& engine,
                                               std::span<const world::HeroPoint> heroes,
                                               const AutoDirectorSettings& settings);

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
    // The panel's settings, carried on the state so a re-cut uses what the user last chose.
    AutoDirectorSettings settings;
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

// How much a `releaseDirectedCamera` would actually destroy, without destroying it: camera-owned
// timeline tracks, plus the `cameraAimFollow` and `cameraShotSpans` tables, plus the camera shots
// the director wrote. Zero means handing the camera back costs nothing.
//
// This exists because ADR-386's lock is a guard on live data LOSS, and a guard with nothing to
// guard is only a cost. Locked unconditionally, the viewport could not move the camera on any
// directed project -- including the Tree of Life, which carries 0 tracks, 0 aim-follow entries and
// 0 shot spans, so the gesture was being refused to protect nothing at all. The multicam film
// carries 42 and 37 and is exactly the case the lock was written for.
[[nodiscard]] std::size_t directedCameraBakeSize(Engine& engine);

// Records that the shot standing on the timeline *now* was cut from the heroes as they are *now*.
// Call after directing. One function rather than three assignments at every call site: a state that
// remembers the cast but not where it stood re-cuts on the very next frame, which is a mistake worth
// making impossible rather than documenting.
void noteDirected(Engine& engine, DirectorState& state);

// Whether the timeline carries the Auto-director's own signature: automation on `camera/mode`
// together with `camera/position` and `camera/target`. Read from the tracks, so it is true for a
// project saved long before anything recorded who wrote them. `refreshDirection` uses it to take up
// the claim on a project that arrives already directed.
[[nodiscard]] bool cameraLooksDirected(const Engine& engine);

// The camera parameters a directed sequence owns. Anything targeting one of these is replaced by
// `installSequence`; anything else survives.
[[nodiscard]] std::span<const std::string_view> directedCameraTargets();

} // namespace avgen::app
