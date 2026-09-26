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

#include "analysis/meter.hpp"
#include "app/cinematic.hpp"
#include "app/song_plan.hpp"
#include "core/error.hpp"
#include "scene/camera_rig.hpp"

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace avgen::app {

// The beat grid a Song Mode cut lands on (ADR-921).
//
// **The engine's one musical time, not a second one** (ADR-896): the analysed track's tracked beats
// and the meter the engine resolves, so a cut on "bar 97" is on the bar `music.downbeat` fires on,
// the bar the sequencer's ruler draws and the bar the Director Agent's "bar 97 beat 1" names. Empty
// when there is no analysed track, and then Song Mode cuts in seconds as it always did.
struct MusicalGrid {
    std::vector<double> beatTimes; // seconds, ascending: `OfflineBeats::beatTimes`
    int downbeat = 0;              // `beatTimes[downbeat]` is bar 1 beat 1 (`Meter::downbeat`)
    int beatsPerBar = 4;
    int phraseBars = 4;

    [[nodiscard]] bool empty() const { return beatTimes.size() < 2; }
    // The median spacing of the tracked beats, seconds; 0 when empty.
    [[nodiscard]] double beatSeconds() const;
    [[nodiscard]] double barSeconds() const { return beatSeconds() * static_cast<double>(beatsPerBar); }
    // The musical beat of tracked beat `k`: 0 on bar 1 beat 1, negative in a pickup.
    [[nodiscard]] std::int64_t musicalBeat(std::size_t k) const {
        return static_cast<std::int64_t>(k) - downbeat;
    }
    // How strong a place tracked beat `k` is to cut: 4 a phrase line, 3 half a phrase in, 2 a bar
    // line, 1 the bar's middle beat, 0 any other beat.
    [[nodiscard]] int strength(std::size_t k) const;
    [[nodiscard]] static const char* strengthName(int strength);
    // The tracked beat nearest `seconds` within `within` seconds, and the same among bar lines.
    [[nodiscard]] std::optional<std::size_t> nearestBeat(double seconds, double within) const;
    [[nodiscard]] std::optional<std::size_t> nearestDownbeat(double seconds, double within) const;
    // 1-based bar and beat of tracked beat `k`, the way a person counts ("bar 97 beat 1").
    [[nodiscard]] std::int64_t barNumber(std::size_t k) const;
    [[nodiscard]] int beatInBar(std::size_t k) const;
    // Tracked beats inside (a, b), exclusive at both ends, as indices.
    [[nodiscard]] std::vector<std::size_t> beatsBetween(double a, double b) const;

    friend bool operator==(const MusicalGrid&, const MusicalGrid&) = default;
};

// The grid of an analysed track under a meter: what `songInputsForEngine` builds from
// `Engine::meter()` and the track's tracked beats.
[[nodiscard]] MusicalGrid musicalGridFrom(std::span<const double> beatTimes, const analysis::Meter& meter);

// The director's own timing policy, which Song Mode shares with the other two modes.
//
// Separate from `AutoDirectorSettings` so this file can live in `avgen_core` beside the shot
// vocabulary it bakes into, rather than in the application layer beside the engine. The
// application's settings struct produces one of these; nothing else does.
struct SongDirectorOptions {
    // The band a shot's length is chosen from (ADR-921): a cut rate of 0 aims at `maxShotSeconds`,
    // 1 at `minShotSeconds`, geometrically between -- a step in rate is a ratio of lengths, which is
    // how pace is heard. `minShotSeconds` is the floor for every section except an accelerating one.
    double minShotSeconds = 5.0;
    double maxShotSeconds = 12.0;
    // ADR-921: how short a cut may get in a section whose treatment accelerates (a Rising or Burst
    // arc) -- the only sections allowed below `minShotSeconds`. The Auto-director panel's
    // "shortest build", which Song Mode used to grey out. Never below one beat of the grid, and
    // never above `minShotSeconds` (a build may go below the floor, not above it).
    double minBuildShotSeconds = 2.0;
    // ADR-921: the beat grid every cut lands on, and every section boundary on its downbeat. Empty:
    // no analysed track, so cuts fall where the pace puts them in seconds.
    MusicalGrid grid;
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

// Why a shot is as long as it is (ADR-921): every term of the duration rule, as it applied to this
// shot, and where its cut landed on the grid. What the cut report hands a generator and what the
// Director panel's tooltip reads, so "why is this shot four seconds" has an answer that is not a
// debugger.
struct ShotTiming {
    double aimSeconds = 0.0; // the length the rule aimed at, before the grid placed the cut
    float rate = 0.0f;       // the cut rate the aim came from: the arc, density and visual density applied
    float density = 0.0f;    // the section's level-free density, 0..1 (onset rate and energy composite)
    float motion = 0.0f;     // how much the subject moves on its own, 0..1
    float scale = 0.0f;      // how much this shot establishes scale, 0..1 (a section's wide opener)
    float contrast = 1.0f;   // the hold a section's opener earns for arriving after a busier one
    // What the shot's end landed on: "phrase", "half-phrase", "bar", "half-bar", "beat", "section"
    // (a section's own boundary), "end" (the film's), or "free" (no grid).
    std::string endsOn;
    std::int64_t startBar = 0; // 1-based musical position of the start; 0 without a grid
    int startBeat = 0;
    double beats = 0.0;        // the length in beats; 0 without a grid
    bool visible = true;       // false when the cut before it falls between two spans of one placed camera
    std::string why;           // one sentence

    friend bool operator==(const ShotTiming&, const ShotTiming&) = default;
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
    std::string subject;        // who the shot opens on
    std::string handoff;        // for a transition, who it travels to; "" otherwise
    float startDistance = 0.0f; // in subject radii, as everywhere else in the director
    float endDistance = 0.0f;
    // ADR-920..922.
    song::Arc arc = song::Arc::Steady;
    bool peak = false;          // the section is one of the film's peaks (ADR-922)
    std::string subjectReason;  // "rotation", "event 'name' at 177.71 s", "the film's hero (peak)"
    scene::ShotTransition transition = scene::ShotTransition::Cut; // how this span is entered
    ShotTiming timing;

    [[nodiscard]] std::string line() const;

    friend bool operator==(const SongDecision&, const SongDecision&) = default;
};

// One section as the director cut it (ADR-921): where its boundaries landed, what it measured, and
// what it was given.
struct SongSectionCut {
    std::size_t index = 0;
    std::string label;          // display only
    std::string intentId;       // display only
    double authoredStart = 0.0; // the plan's own boundaries...
    double authoredEnd = 0.0;
    double startSeconds = 0.0;  // ...and where the director put them: on a downbeat, with a grid
    double endSeconds = 0.0;
    song::Arc arc = song::Arc::Steady;
    float density = 0.0f;       // level-free, 0..1
    float musicEnergy = 0.0f;   // the section's energy share of the piece, 0..1
    float push = 0.0f;          // the treatment's energy
    float rateOpen = 0.0f;      // the cut rate at the section's start and end, arc applied
    float rateClose = 0.0f;
    bool peak = false;
    std::string eventSubject;   // who a peak section was given to, when an event said so
    std::string eventName;
    std::size_t firstShot = 0;  // index into `SongDirection::decisions`
    std::size_t shotCount = 0;

    friend bool operator==(const SongSectionCut&, const SongSectionCut&) = default;
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
    // ADR-921: every section, as cut.
    std::vector<SongSectionCut> sections;
    // Things the author should be told and that are not failures: an intent asking for more cameras
    // than the world has, a section too short for the cut rate it asked for.
    std::vector<std::string> warnings;
    // What it was cut with, for the report: the band, the build floor, the seed, the autonomy, and
    // the grid (without its beat list, which the report summarises).
    SongDirectorOptions options;
    std::string planName;

    // How many distinct cameras the film actually uses. The single number that answers "did
    // multi-camera happen", and the one an acceptance test should assert on.
    [[nodiscard]] std::size_t camerasUsed() const;

    // **The cut report** (ADR-923): every shot's span, subject, arc, camera and the reason for its
    // duration, every section as cut, the grid, and the statistics a reviewer asks first -- shot
    // lengths per section, how many cuts land on a downbeat. The JSON a generator reads.
    [[nodiscard]] nlohmann::json report() const;
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
