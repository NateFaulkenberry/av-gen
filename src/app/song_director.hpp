#pragma once

// Song Mode: the Auto-director directing *inside* an authored song structure (ADR-249).
//
// The third way the Auto-director cuts, beside Continuous shot and Edited sequence. What it adds is
// not a new kind of camera move and not a new camera-selection system -- it is a new *source of
// constraint*. Continuous and Edited fold the music into sections themselves and decide everything
// from the section kind. Song Mode is handed a plan somebody authored and decides everything the
// plan did not fix.
//
//     Song Section -> Shot Intent -> Auto Director -> camera selection -> framing -> movement -> cut timing
//                     `-- authored ---'              `------------- decided here ----------------'
//
// ## What makes it not a shot playback mode
//
// The brief is emphatic that Song Mode must not be a precomputed list of camera transforms played
// back, because that would make it a worse spelling of the shot track that already exists. Three
// things are decided here and are decided nowhere in the plan:
//
//   1. **Which camera.** The plan says "coverage wants two viewpoints"; this file scores every
//      camera the author made available against the intent and picks. A world with different
//      cameras gives a different film from the same plan.
//   2. **How many cuts, and where.** The plan says "cut often"; this file turns that into a shot
//      count against the director's own timing floor and ceiling and against the section's measured
//      energy.
//   3. **What each shot is of, and how it is framed.** The plan says "this is about the hero";
//      this file runs the cast rotation, picks the move, and derives the distances, the elevation
//      and the sweep.
//
// And the *same* section directed twice does not come out the same, because every decision above is
// a function of the section's `occurrence` as well as of the seed. Verse 1 and Verse 2 carry an
// identical intent and get different cameras, different framings and different cut points -- from
// one bake, with no per-frame randomness, so ADR-091's scrub determinism is untouched. See ADR-249.
//
// ## What it does not do
//
// **It does not select cameras at runtime.** It *authors* `scene::CameraShot`s, and
// `scene::resolveActiveCamera` remains the one thing in the engine that says which camera is live
// (ADR-245). The brief's section 12 refuses a second camera-selection system and there is not one:
// this is an editor writing to the shot track, exactly as a person does in the Cameras panel.
//
// **It does not read a label.** Everything it consumes is `app::SongPlan`, which is numbers and one
// opaque intent id (`song_plan.hpp`). There is no section vocabulary in this file and adding one
// would break `tests/unit/test_song_director.cpp`'s scramble test rather than merely being poor
// taste.

#include "app/cinematic.hpp"
#include "app/song_plan.hpp"
#include "core/error.hpp"
#include "scene/camera_rig.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace avgen::app {

// The director's own timing policy, which Song Mode shares with the other two modes.
//
// Separate from `AutoDirectorSettings` so this file can live in `avgen_core` beside the shot
// vocabulary it bakes into, rather than in the application layer beside the engine. The
// application's settings struct produces one of these; nothing else does.
struct SongDirectorOptions {
    // The band a shot's length is chosen from. `cutRate == 0` asks for `maxShotSeconds`, `1` for
    // `minShotSeconds`, and the section's measured energy moves it within the band when the
    // autonomy allows.
    double minShotSeconds = 5.0;
    double maxShotSeconds = 12.0;
    // Same seed, same heroes, same plan, same world, same film (ADR-249).
    std::uint32_t seed = 1;
    // **A ceiling on freedom, not a setting of it.** The effective autonomy of a section is the
    // lesser of this and the section's own, so a person who sets the whole film to Locked gets a
    // locked film whatever the plan says, and a section may still ask for less freedom than the
    // film allows. One control that can always be trusted to reduce, which is what an override
    // should be.
    Autonomy autonomy = Autonomy::Expressive;
    // The lens the framing bake is committed with, in millimetres; seeded from the camera the bake
    // is for. Here for the same reason `DirectionBrief::focalLength` is: a lens belongs to a camera
    // (ADR-245) and the bake has to commit to one.
    float focalLength = 35.0f;
};

// One span of the film, and every decision that produced it. The unit of the acceptance test: a
// Song Mode run is judged by reading these, not by looking at the numbers they baked into.
struct SongDecision {
    double startSeconds = 0.0;
    double endSeconds = 0.0;
    std::size_t sectionIndex = 0;
    std::string sectionLabel; // display only
    std::string intentId;     // display only
    int occurrence = 0;
    Autonomy autonomy = Autonomy::Guided;
    scene::CameraId camera = scene::kNoCamera;
    std::string cameraName;
    ShotKind kind = ShotKind::Establish;
    std::string subject;
    float startDistance = 0.0f; // in subject radii, as everywhere else in the director
    float endDistance = 0.0f;

    [[nodiscard]] std::string line() const;

    friend bool operator==(const SongDecision&, const SongDecision&) = default;
};

struct SongDirection {
    // The framing bake, for the camera the Auto-director owns. One shot per span, contiguous --
    // so the directed camera has a continuous life even across the spans another camera is showing,
    // and cutting back to it does not snap.
    Sequence sequence;
    // The camera track, for `scene::CameraDirection::shots`. Adjacent spans naming the same camera
    // are merged: a cut from a camera to itself is not a cut.
    std::vector<scene::CameraShot> shots;
    // Every decision, in order. What the panel shows and what the demo's acceptance test reads.
    std::vector<SongDecision> decisions;
    // Things the author should be told and that are not failures: an intent asking for more cameras
    // than the world has, a section too short for the cut rate it asked for.
    std::vector<std::string> warnings;

    // How many distinct cameras the film actually uses. The single number that answers "did
    // multi-camera happen", and the one an acceptance test should assert on.
    [[nodiscard]] std::size_t camerasUsed() const;
};

// The whole of Song Mode.
//
// Pure: same plan, same brief, same cameras, same options, same film -- on any thread, in any
// order, with no engine and no clock. That is what makes it testable without a GPU and what makes
// an offline render of a Song Mode cut identical to the live one.
//
// `eligible` is the cameras the author made available to the Auto-director
// (`CameraRig::autoDirectorEligible`), in the composition's own order. An empty span is refused:
// Song Mode with no camera to cut to is Continuous shot with extra steps, and saying so is better
// than silently being that.
[[nodiscard]] Result<SongDirection> directSong(const SongPlan& plan, const DirectionBrief& brief,
                                               std::span<const scene::CameraRig> eligible,
                                               const SongDirectorOptions& options);

// Which cameras of a collection the Auto-director may use. Exposed because two things need to agree
// about it -- what the director picks from and what a panel says is available -- and because a
// caller wanting to know why its film used one camera should be able to ask.
[[nodiscard]] std::vector<scene::CameraRig> eligibleCameras(const scene::CameraDirection& direction);

// ---- the two tables, exposed ---------------------------------------------------------------------
//
// Exposed for the same reason `shotKindForSection` is: they are the arguable part, and a caller who
// disagrees should be able to read them rather than reverse-engineer them from a film.

// Which move an intent asks for. **The whole of Song Mode's shot-kind decision**, and it reads
// three numbers: who the shot is about, how far away it is, and how much it travels.
//
// Five of the fourteen shot kinds are unreachable from here and deliberately so: `Entry`,
// `Passage`, `Descent`, `Ascent` and `Flyby` are moves about a *direction in the world* -- into,
// through, down, up, past -- and none of the six intent axes expresses a direction. Adding an axis
// to reach them would be adding a knob to make a table complete, which is the wrong reason. They
// remain available to an authored shot and to the other two director modes.
[[nodiscard]] ShotKind shotKindForIntent(const ShotIntentProfile& intent);

// How well a camera suits an intent, 0..1. Higher is better; ties break on the camera's own
// `priority` and then its id, so the answer never depends on vector order.
[[nodiscard]] float cameraMatch(const scene::CameraRig& camera, const ShotIntentProfile& intent);

} // namespace avgen::app
