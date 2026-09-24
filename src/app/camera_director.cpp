#include "app/camera_director.hpp"

#include "app/engine.hpp"
#include "app/music_runtime.hpp"
#include "scene/composition.hpp"
#include "world/camera_clearance.hpp"
#include "core/log.hpp"
#include "params/timeline.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>

namespace avgen::app {
namespace {

// The parameters a directed sequence writes. Kept in one place because two things need to agree
// about it -- what `toTimelineTracks` emits and what `installSequence` is entitled to remove -- and
// a list that appears twice is a list that will disagree with itself.
// All six, in the order `Sequence::toTimelineTracks` emits them. The first version of this list
// named four and omitted the two lens tracks, so installing a second sequence would have left the
// previous direction's focal length and aperture in place alongside the new one -- two tracks
// writing the same parameter, which is not a blend but whichever the timeline applies last. A test
// that walks the installed tracks and checks each is on this list caught it immediately, which is
// the argument for the list existing at all rather than being implied.
constexpr std::array<std::string_view, 7> kCameraTargets{
    // The mode belongs to the director for as long as the director owns the camera: it is what
    // makes the other six readable at all. Handing the camera back removes this with them, so the
    // scene returns to whichever way of placing the camera it was authored with.
    "camera/mode",
    "camera/position",
    "camera/target",
    "camera/lens/focalLength",
    "camera/lens/aperture",
    "camera/lens/focusDistance",
    "camera/focus/emphasis",
};

bool isCameraTarget(std::string_view target) {
    return std::find(kCameraTargets.begin(), kCameraTargets.end(), target) != kCameraTargets.end();
}

// The scene's terrain, or nothing. The first one wins, for the same reason installWorld picks the
// first: a scene with two terrains is ambiguous and silently choosing between them is a bug found
// much later.
const scene::CompositionNode* terrainNodeOf(Engine& engine) {
    const scene::Composition* composition = engine.composition();
    if (composition == nullptr) {
        return nullptr;
    }
    for (const auto& node : composition->nodes()) {
        if (node && node->kind == scene::NodeKind::Terrain) {
            return node.get();
        }
    }
    return nullptr;
}

} // namespace


std::span<const std::string_view> directedCameraTargets() { return kCameraTargets; }

std::size_t directedCameraBakeSize(Engine& engine) {
    // What a `releaseDirectedCamera` would take off the camera right now: the same three things it
    // parks, counted the same way, so the lock cannot come to guard a different set than the one a
    // release moves. The shot spans are not counted any more (ADR-582): a release leaves them where
    // they are, because they are the film's focus schedule rather than the camera's steering.
    std::size_t n = 0;
    for (const params::Track& t : engine.timeline().tracks()) {
        if (isCameraTarget(t.target)) {
            ++n;
        }
    }
    if (const scene::Composition* composition = engine.composition()) {
        n += composition->aimFollow().size();
        for (const scene::CameraShot& s : composition->cameraDirection().shots) {
            if (s.origin == scene::CameraShot::Origin::Directed) {
                ++n;
            }
        }
    }
    return n;
}

namespace {

// Everything except the settings, which are the user's preferences rather than this cut's state.
// Resetting the whole struct wiped the panel's choices every time the camera went back to the
// viewport, so a shot mode chosen once survived until the first hand-back and no longer.
void endClaim(DirectorState& state) {
    const AutoDirectorSettings keep = state.settings;
    state = DirectorState{};
    state.settings = keep;
}

} // namespace

std::size_t releaseDirectedCamera(Engine& engine, DirectorState& state) {
    // ADR-582: taking the camera back PARKS the director's cut. It used to destroy it -- tracks,
    // aim-follow, directed shots and the shot spans -- and the owner's 18:04 save on 21 Sep wrote
    // that loss into the project: 45 spans, 39 follow entries, ~122 KB of camera tracks, and a Hero
    // Pulse that played in the window and was absent from every rendered frame. Re-baking was never
    // a recovery, because it re-photographs the hero anchors (ADR-344) and the cut comes back
    // different.
    //
    // What stops is the director *steering*: its camera tracks leave the timeline, its follow table
    // stops nudging the aim and its camera shots stop claiming the frame. What does not stop is the
    // film's focus schedule -- `Engine::shotSpans` is not touched -- so a HeroFocus or CameraTravel
    // effect keeps landing where and when the cut says, under a camera the owner is flying. That
    // overrules ADR-207's "a world effect gated on the camera travelling must not keep firing
    // against a cut that is no longer driving anything": the owner ruled that it must.
    //
    // A cut that is already parked is never overwritten. Parked means the director is not steering,
    // so any camera track on the timeline now was made by somebody after the hand-back; parking it
    // over the director's cut would destroy the one thing "Resume director" promises to restore.
    if (engine.directorParked()) {
        endClaim(state);
        return 0;
    }
    ParkedDirectorsCut parked;
    auto& tracks = engine.timeline().tracks();
    std::vector<params::Track> keep;
    keep.reserve(tracks.size());
    for (params::Track& t : tracks) {
        if (isCameraTarget(t.target)) {
            // Unbound: a parked track must hold no pointer into a parameter set that a scene swap
            // may destroy while it waits. `resumeDirectedCamera` binds it again.
            t.param = nullptr;
            parked.tracks.push_back(std::move(t));
        } else {
            keep.push_back(std::move(t));
        }
    }
    tracks = std::move(keep);
    if (scene::Composition* composition = engine.composition()) {
        // The follow table goes with the keys it belongs to. Left on the composition it would keep
        // nudging a camera the viewport has just been handed back ("the camera fights me").
        parked.aimFollow = composition->aimFollow();
        composition->setAimFollow({});
        // ADR-249's directed camera shots, likewise: parked, and the person's own shots stay.
        scene::CameraDirection direction = composition->cameraDirection();
        std::vector<scene::CameraShot> directed;
        std::erase_if(direction.shots, [&](const scene::CameraShot& s) {
            if (s.origin == scene::CameraShot::Origin::Directed) {
                directed.push_back(s);
                return true;
            }
            return false;
        });
        if (!directed.empty()) {
            if (auto ok = engine.setCameraDirection(std::move(direction)); !ok) {
                // Left on the camera rather than parked: a shot that is in neither place is lost.
                log::warn("auto-director: {}", ok.error().message);
            } else {
                parked.cameraShots = std::move(directed);
            }
        }
    }
    const std::size_t moved = parked.tracks.size();
    if (!parked.empty()) {
        log::info("auto-director: parked the cut ({} camera track(s), {} follow entr(ies), {} camera "
                  "shot(s)); {} shot span(s) still drive world effects. Resume director restores it.",
                  parked.tracks.size(), parked.aimFollow.size(), parked.cameraShots.size(),
                  engine.shotSpans().size());
        engine.setParkedCut(std::move(parked));
    }
    endClaim(state);
    return moved;
}

Result<std::size_t> resumeDirectedCamera(Engine& engine, DirectorState& state) {
    if (!engine.directorParked()) {
        return fail("there is no parked director's cut to resume");
    }
    const ParkedDirectorsCut& cut = engine.parkedCut();
    params::Timeline& timeline = engine.timeline();

    // Refused rather than overwritten: a camera track on the timeline while the cut is parked is
    // somebody's own work, and two tracks on one target are not a blend but whichever the timeline
    // applies last. Checked before anything moves, so a refusal changes nothing.
    std::string clash;
    for (const params::Track& t : timeline.tracks()) {
        if (isCameraTarget(t.target) && clash.find(t.target) == std::string::npos) {
            clash += clash.empty() ? t.target : ", " + t.target;
        }
    }
    if (!clash.empty()) {
        return fail("the camera has keys of its own now ({}); delete those tracks first, because "
                    "resuming over them would replace them",
                    clash);
    }
    scene::Composition* composition = engine.composition();
    if (composition == nullptr && (!cut.aimFollow.empty() || !cut.cameraShots.empty())) {
        return fail("this scene has no composition to put the director's shots back on");
    }
    if (!cut.cameraShots.empty()) {
        scene::CameraDirection direction = composition->cameraDirection();
        direction.shots.insert(direction.shots.end(), cut.cameraShots.begin(), cut.cameraShots.end());
        // The order `installSongDirection` leaves them in, so the list reads back as it was.
        std::stable_sort(direction.shots.begin(), direction.shots.end(),
                         [](const scene::CameraShot& a, const scene::CameraShot& b) {
                             return a.startSeconds < b.startSeconds;
                         });
        if (auto ok = direction.validate(); !ok) {
            return fail("the parked camera shots no longer fit this scene: {}", ok.error().message);
        }
        if (auto ok = engine.setCameraDirection(std::move(direction)); !ok) {
            return std::unexpected(ok.error());
        }
    }
    ParkedDirectorsCut restored = engine.parkedCut();
    engine.setParkedCut({});
    const std::size_t count = restored.tracks.size();
    for (params::Track& t : restored.tracks) {
        timeline.addTrack(std::move(t));
    }
    // What `installSequence` does for the same reason: a directed camera in a disabled timeline
    // does not move, and an unbound track is skipped in silence.
    timeline.enabled = true;
    if (auto bound = timeline.bind(engine.params()); !bound) {
        log::warn("auto-director: {}", bound.error().message);
    }
    if (composition != nullptr) {
        composition->setAimFollow(std::move(restored.aimFollow));
    }
    noteDirected(engine, state);
    log::info("auto-director: resumed the parked cut ({} camera track(s)), unchanged", count);
    return count;
}

std::size_t discardDirectorsCut(Engine& engine, DirectorState& state) {
    std::size_t removed = 0;
    if (engine.directorParked()) {
        // Parked: the director is not steering, so every camera track on the timeline now is
        // somebody's own and stays. Only the parked copy goes.
        const ParkedDirectorsCut& cut = engine.parkedCut();
        removed += cut.tracks.size() + cut.aimFollow.size() + cut.cameraShots.size();
        engine.setParkedCut({});
    } else {
        auto& tracks = engine.timeline().tracks();
        const std::size_t before = tracks.size();
        std::erase_if(tracks, [](const params::Track& t) { return isCameraTarget(t.target); });
        removed += before - tracks.size();
        if (scene::Composition* composition = engine.composition()) {
            removed += composition->aimFollow().size();
            composition->setAimFollow({});
            scene::CameraDirection direction = composition->cameraDirection();
            const std::size_t shots = direction.shots.size();
            std::erase_if(direction.shots, [](const scene::CameraShot& s) {
                return s.origin == scene::CameraShot::Origin::Directed;
            });
            if (direction.shots.size() != shots) {
                removed += shots - direction.shots.size();
                if (auto ok = engine.setCameraDirection(std::move(direction)); !ok) {
                    log::warn("auto-director: {}", ok.error().message);
                }
            }
        }
    }
    // The focus schedule goes too: this is the one action that is allowed to say "there is no cut".
    removed += engine.shotSpans().size();
    engine.setShotSpans({});
    endClaim(state);
    log::info("auto-director: the director's cut was discarded ({} item(s))", removed);
    return removed;
}

void noteDirected(Engine& engine, DirectorState& state) {
    const scene::Composition* composition = engine.composition();
    state.directed = composition != nullptr;
    state.heroRevision = composition != nullptr ? composition->heroRevision() : 0;
    state.placementRevision = composition != nullptr ? composition->heroPlacementRevision() : 0;
    state.wasPlaying = engine.transport().isPlaying();
    state.sceneGeneration = engine.sceneGeneration();
}

bool cameraLooksDirected(const Engine& engine) {
    // The director's signature, read off the timeline rather than remembered.
    //
    // `camera/mode` is the tell, and it is the one this file already explains: the mode belongs to
    // the director for as long as the director owns the camera, because it is what makes the other
    // six targets readable at all -- a composition ignores `camera/position` and `camera/target`
    // unless it is in free mode. Somebody hand-animating a camera keys position and target; they do
    // not key the mode, because in free mode it is already what they want and in orbit mode the
    // other two do nothing.
    //
    // Asking the tracks rather than a saved flag is what makes this work on a project written
    // before any of this existed -- which is every project that has the problem.
    const params::Timeline& timeline = engine.timeline();
    return timeline.isAutomated("camera/mode") && timeline.isAutomated("camera/position") &&
           timeline.isAutomated("camera/target");
}

Result<Redirect> refreshDirection(Engine& engine, DirectorState& state) {
    // A project can arrive with the camera already directed: the tracks are saved, the claim was
    // not. Without this the viewport's own hand-back never fires on a freshly opened project -- a
    // drag wrote `camera/position`, the timeline replaced it on the next frame, the camera appeared
    // to ignore the mouse, and the menu item was the only way out. Taking the claim up here rather
    // than on load means it does not matter *how* the timeline came to hold these tracks.
    if (!state.directed && engine.composition() != nullptr && cameraLooksDirected(engine)) {
        noteDirected(engine, state);
        log::info("camera: this project's camera is directed; reaching for it hands it back");
    }
    if (!state.directed) {
        return Redirect::Nothing;
    }
    const scene::Composition* composition = engine.composition();
    // No composition, or nothing driving the camera any more: a project was loaded, the camera was
    // handed back, an undo took the tracks, somebody deleted them by hand. Whatever happened, this
    // is no longer our camera and the next Enable Auto-director starts the relationship again.
    if (composition == nullptr || !engine.timeline().isAutomated("camera/position")) {
        endClaim(state);
        return Redirect::Released;
    }
    // ADR-582: a different scene is not a changed cast. The revision counters belong to one
    // composition, and a project load or a scene swap installs another whose counters say nothing
    // about the first -- so comparing them re-cut a freshly opened project whenever the numbers
    // happened to differ, replacing the cut it had just loaded. The claim is taken up afresh.
    if (engine.sceneGeneration() != state.sceneGeneration) {
        noteDirected(engine, state);
        return Redirect::Nothing;
    }
    const bool playing = engine.transport().isPlaying();
    const bool parked = !playing && !state.wasPlaying;
    state.wasPlaying = playing;
    const bool castChanged = composition->heroRevision() != state.heroRevision;
    const bool moved = composition->heroPlacementRevision() != state.placementRevision;
    if (!castChanged) {
        // A hero merely *moving* never re-cuts the film any more (ADR-582). It used to, while the
        // transport was parked -- and a paused scrub re-simulates the world, walkers move, their
        // heroes follow and settle, and the whole cut was silently replaced by a re-bake that
        // re-photographed every anchor at wherever the scrub had left them (ADR-344). Only an
        // explicit re-bake may replace a cut. So it is absorbed, and said once when parked, so
        // somebody who dragged a hero knows what to press.
        state.placementRevision = composition->heroPlacementRevision();
        return moved && parked ? Redirect::Stale : Redirect::Nothing;
    }
    // Recorded before the work, not after: a re-cut that fails must not be retried every frame for
    // the rest of the session, and the failure is the same one until the heroes change again.
    state.heroRevision = composition->heroRevision();
    state.placementRevision = composition->heroPlacementRevision();
    if (composition->heroes().empty()) {
        static_cast<void>(releaseDirectedCamera(engine, state));
        return Redirect::HandedBack;
    }
    if (auto installed = directEngine(engine, composition->heroes(), state.settings); !installed) {
        return std::unexpected(installed.error());
    }
    return Redirect::Recut;
}

Result<DirectionBrief> briefFromHeroes(std::span<const world::HeroPoint> heroes) {
    if (heroes.empty()) {
        return fail("the Auto-director needs something to point at: this world has no heroes");
    }
    const auto toTarget = [](const world::HeroPoint& h) {
        FocalTarget t;
        t.name = h.name;
        t.position = h.position;
        // The subject's radius sets how far "close" is, so a shot is reusable against a subject of
        // any size. A hero's horizontal radius alone understates a tall thin thing badly -- a
        // twenty-metre tree half a metre wide would be framed as if it were half a metre across --
        // so the larger of the two is what a camera has to fit in frame.
        t.radius = std::max(h.radius, h.height * 0.5f);
        t.importance = h.importance; // ADR-202: the only thing that decides how often it is cast
        t.preferredDistance = h.preferredCameraDistance;
        // `HeroPoint::preferredCameraElevationDegrees` was authored per hero and read by nothing --
        // the third of three dead properties the panel work turned up. It is wired here rather than
        // dropped, because "look up at this one and down into that one" is a real thing a hero has an
        // opinion about and there was already a field for it.
        t.preferredElevationDegrees = h.preferredCameraElevationDegrees;
        // `HeroPoint::yaw` carries the bearing the world says this hero is best approached from. It
        // was previously written by the composer and read by nobody in the director.
        t.preferredAzimuth = h.yaw;
        return t;
    };

    DirectionBrief brief;
    brief.hero = toTarget(heroes.front());
    brief.supporting.reserve(heroes.size() - 1);
    for (std::size_t i = 1; i < heroes.size(); ++i) {
        brief.supporting.push_back(toTarget(heroes[i]));
    }
    // The hero's own preferred stand-off is expressed in the shot distances the director picks, but
    // the lens is a property of the *hero*: a tall subject shot on a wide lens leans, and this is
    // the one place that knows the subject's proportions.
    brief.focalLength = heroes.front().height > heroes.front().radius * 4.0f ? 50.0f : 35.0f;
    return brief;
}

namespace {
// Not a member of anything, because the only consumer is a panel and the only producer is the
// function below. A plain string rather than a struct: it is a sentence for a human to read, and
// the numbers behind it are already in the log.
std::string gLastSummary;
void setDirectionSummary(std::string s) { gLastSummary = std::move(s); }
} // namespace

std::string lastDirectionSummary() { return gLastSummary; }

Result<Sequence> directHeroes(std::span<const world::HeroPoint> heroes,
                              const signals::MusicalStructure& structure,
                              const AutoDirectorSettings& settings) {
    auto brief = briefFromHeroes(heroes);
    if (!brief) {
        return std::unexpected(brief.error());
    }
    settings.applyTo(*brief);
    auto sequence = directFromStructure(structure, *brief);
    if (!sequence) {
        return std::unexpected(sequence.error());
    }
    // ADR-200. Applied after the cut is built rather than while it is being built, because the cap
    // is a judgement about the *result* -- "this is moving too fast to watch" -- and the shot
    // geometry that produces a speed is chosen from the music, the hero's size and the shot kind,
    // none of which should be bent to hit a number.
    if (settings.maxCameraSpeed > 0.0f) {
        if (const std::size_t shortened = sequence->limitCameraSpeed(settings.maxCameraSpeed);
            shortened > 0) {
            log::info("auto-director: {} of {} shot(s) shortened to hold {:.1f} m/s", shortened,
                      sequence->shots.size(), settings.maxCameraSpeed);
        }
    }
    // After the travel cap, because widening a swing is measured against the camera path the cap
    // has already settled.
    if (settings.maxViewRate > 0.0f) {
        if (const std::size_t widened = sequence->limitViewRate(settings.maxViewRate); widened > 0) {
            log::info("auto-director: {} of {} shot(s) given a longer swing to hold {:.0f} deg/s",
                      widened, sequence->shots.size(), settings.maxViewRate);
        }
    }
    // What the cut *does*, not what was asked for (ADR-203). A cap is a request, and in a
    // continuous take it is frequently one the geometry cannot grant: the camera has to cross the
    // ground between one subject's stand-off point and the next inside the time the music gave the
    // shot, and that is a floor. Saying so here is the difference between a control that looks
    // broken and one whose limit is visible -- which is how this was reported.
    const float peakSpeed = sequence->peakCameraSpeed();
    const float peakSwing = sequence->peakViewSwing();
    setDirectionSummary(fmt::format(
        "{} shots peaking at {:.1f} m/s and {:.0f} deg/s{}", sequence->shots.size(), peakSpeed,
        peakSwing,
        (settings.maxCameraSpeed > 0.0f && peakSpeed > settings.maxCameraSpeed * 1.02f) ||
                (settings.maxViewRate > 0.0f && peakSwing > settings.maxViewRate * 1.02f)
            ? " -- the caps could not be met; see the tooltip"
            : ""));
    if ((settings.maxCameraSpeed > 0.0f && peakSpeed > settings.maxCameraSpeed * 1.02f) ||
        (settings.maxViewRate > 0.0f && peakSwing > settings.maxViewRate * 1.02f)) {
        log::info("auto-director: the cut peaks at {:.1f} m/s and {:.0f} deg/s; the caps asked for "
                  "{:.1f} m/s and {:.0f} deg/s. A continuous take has to cross the ground between "
                  "subjects in the time the music gives it -- hold each subject for more shots, or "
                  "give the shots longer, to lower that floor",
                  peakSpeed, peakSwing,
                  settings.maxCameraSpeed > 0.0f ? settings.maxCameraSpeed : peakSpeed,
                  settings.maxViewRate > 0.0f ? settings.maxViewRate : peakSwing);
    } else {
        log::info("auto-director: the cut peaks at {:.1f} m/s and {:.0f} deg/s", peakSpeed, peakSwing);
    }
    return sequence;
}

Result<signals::MusicalStructure> structureOfTrack(const analysis::AnalysisTrack& track,
                                                   int phraseBars, int sectionPhrases) {
    const auto& frames = track.frames();
    if (frames.empty()) {
        return fail("the Auto-director needs an analyzed track: this one has no frames");
    }
    // A detector of its own, walked over every frame in order. Reusing the engine's would fold from
    // whatever state playback had reached, so the same track would produce a different structure
    // depending on when the button was pressed.
    MusicRuntime runtime;
    std::vector<signals::MusicalMoment> moments;
    for (const analysis::AnalysisFrame& frame : frames) {
        runtime.consume(frame, phraseBars, sectionPhrases);
        const auto found = runtime.lastMoments();
        moments.insert(moments.end(), found.begin(), found.end());
    }
    const double total = frames.back().timeSeconds;
    if (!(total > 0.0)) {
        return fail("the analyzed track has no duration");
    }
    auto structure = signals::MusicalStructure::fromMoments(moments, total);
    if (structure.sections.empty()) {
        // Not an empty sequence further down the line: a track the fold found no structure in is a
        // track the director cannot shoot, and saying so here names the cause.
        return fail("no musical structure was found in {:.1f}s of audio ({} moment(s)); the "
                    "director has nothing to cut to",
                    total, moments.size());
    }
    return structure;
}

Result<void> AutoDirectorSettings::validate() const {
    if (!(minShotSeconds > 0.0) || minShotSeconds > 120.0) {
        return fail("auto-director: minimum shot length must be in (0, 120] s");
    }
    // The relationship, not the value, and only in the modes that have builds.
    //
    // Song Mode has none: a section carries a cut rate and the floor it stops at is
    // `minShotSeconds`. Enforcing the pair there made the panel a trap -- Song Mode *disables*
    // `shortest build`, so lowering `shortest shot` past it produced a refusal whose only cure was
    // a slider the mode had greyed out. The pair is checked again the moment a mode that reads it
    // is chosen, which is the right time to be told.
    if (!(minBuildShotSeconds > 0.0) ||
        (mode != DirectorMode::Song && minBuildShotSeconds > minShotSeconds)) {
        return fail("auto-director: a build's minimum ({} s) must be positive and no longer than the "
                    "ordinary minimum ({} s)",
                    minBuildShotSeconds, minShotSeconds);
    }
    if (maxShotSeconds < minShotSeconds || maxShotSeconds > 600.0) {
        return fail("auto-director: maximum shot length must be between the minimum and 600 s");
    }
    // ADR-200: 0 is off. A negative is somebody's arithmetic, and a cap of a centimetre a second
    // would shrink every shot to a point rather than slowing anything.
    if (maxCameraSpeed < 0.0f || (maxCameraSpeed > 0.0f && maxCameraSpeed < 0.1f) ||
        maxCameraSpeed > 2000.0f) {
        return fail("auto-director: maximum camera speed {} must be 0 (off) or 0.1..2000 m/s",
                    maxCameraSpeed);
    }
    if (maxViewRate < 0.0f || (maxViewRate > 0.0f && maxViewRate < 1.0f) || maxViewRate > 720.0f) {
        return fail("auto-director: maximum view rate {} must be 0 (off) or 1..720 deg/s",
                    maxViewRate);
    }
    if (dwellShots < 1 || dwellShots > 12) {
        return fail("auto-director: a subject must be held for 1..12 shots, not {}", dwellShots);
    }
    // `autonomy` has no invalid value: it is an enum of three, and a JSON document naming a fourth
    // is refused by `fromJson` before it reaches a field. Nothing to check here, said out loud so
    // the omission does not read as one.
    return {};
}

nlohmann::json AutoDirectorSettings::toJson() const {
    return nlohmann::json{{"mode", directorModeName(mode)},
                          {"minShot", minShotSeconds},
                          {"minBuildShot", minBuildShotSeconds},
                          {"maxShot", maxShotSeconds},
                          {"maxSpeed", maxCameraSpeed},
                          {"maxSwing", maxViewRate},
                          {"dwell", dwellShots},
                          {"autonomy", autonomyName(autonomy)},
                          {"seed", seed}};
}

Result<AutoDirectorSettings> AutoDirectorSettings::fromJson(const nlohmann::json& doc) {
    if (!doc.is_object()) {
        return fail("director settings must be a JSON object");
    }
    AutoDirectorSettings out;
    if (const auto mode = doc.find("mode"); mode != doc.end()) {
        if (!mode->is_string()) {
            return fail("director.mode must be a string");
        }
        const auto parsed = directorModeFromName(mode->get<std::string>());
        if (!parsed) {
            return fail("director.mode '{}' is not a shot mode", mode->get<std::string>());
        }
        out.mode = *parsed;
    }
    // Field by field with the current value as the default, so a project written before a control
    // existed reads as "the author did not set that one" rather than as zero.
    out.minShotSeconds = doc.value("minShot", out.minShotSeconds);
    out.minBuildShotSeconds = doc.value("minBuildShot", out.minBuildShotSeconds);
    out.maxShotSeconds = doc.value("maxShot", out.maxShotSeconds);
    out.maxCameraSpeed = doc.value("maxSpeed", out.maxCameraSpeed);
    out.maxViewRate = doc.value("maxSwing", out.maxViewRate);
    out.dwellShots = doc.value("dwell", out.dwellShots);
    if (const auto autonomy = doc.find("autonomy"); autonomy != doc.end()) {
        if (!autonomy->is_string()) {
            return fail("director.autonomy must be a string");
        }
        const auto parsed = autonomyFromName(autonomy->get<std::string>());
        if (!parsed) {
            return fail("director.autonomy '{}' is not locked, guided or expressive",
                        autonomy->get<std::string>());
        }
        out.autonomy = *parsed;
    }
    out.seed = doc.value("seed", out.seed);
    // Refused rather than clamped. A project is written by this application, so the only route to a
    // value outside the range is a hand edit or a file from a build that meant something else by
    // the key -- and both are worth a message rather than a silently different film.
    if (auto ok = out.validate(); !ok) {
        return std::unexpected(ok.error());
    }
    return out;
}

void AutoDirectorSettings::applyTo(DirectionBrief& brief) const {
    brief.mode = mode;
    brief.minShotSeconds = minShotSeconds;
    brief.minBuildShotSeconds = minBuildShotSeconds;
    brief.maxShotSeconds = maxShotSeconds;
    brief.dwellShots = dwellShots;
    brief.seed = seed;
}

// ---- Song Mode (ADR-249) -------------------------------------------------------------------------

Result<SongPlan> songPlanForEngine(const Engine& engine) {
    // **The FILM first.** The section timeline, through the shot language -- what the person has in
    // front of them in the Sequence panel right now.
    //
    // This used to sit *below* `engine.songPlan()`, and that ordering was the bug: an authored plan
    // is a snapshot with its own start and end times, so a project carrying one ignored every
    // subsequent edit to the sections. Drag Verse 2 to twenty seconds and Song Mode kept listing it
    // where it used to be; split a Chorus in two and Song Mode still saw one. Reported exactly that
    // way, and `glowmere-valley-2-multicam` was carrying a ten-section plan that shadowed a timeline
    // the owner had been editing for days.
    //
    // ADR-247 already had the principle -- a timeline is what a person decided and a structure is
    // what a detector reported, and the decision outranks the report. What it did not say, because
    // the case had not arisen, is that a *saved* decision does not outrank a *live* one. The
    // sections are edited in the sequencer; the sequencer is therefore the authority.
    //
    // Nothing is lost by the reordering. A section's intent lives on its type and its per-section
    // treatment, both of which the timeline carries and `cueSheet` resolves -- so the deliberate
    // authoring a plan used to hold is held by the thing being edited instead.
    const seq::Sequence& piece = engine.sequence();
    if (!piece.sectionTimeline.sections.empty()) {
        const std::vector<song::SectionCue> cues =
            song::cueSheet(piece.sectionTimeline, piece.shotLanguage);
        if (!cues.empty()) {
            return songPlanFromCues(cues);
        }
    }
    // Then a plan somebody authored, for a project that has one and no film to derive from.
    if (!engine.songPlan().empty()) {
        return engine.songPlan();
    }
    // Otherwise, the analyzed structure read for its *measurements* -- never for its labels. Kept as
    // the fallback for a project that has a detection and no film, and as the thing that makes the
    // beginner path work the instant the analysis finishes.
    const analysis::SongStructure& structure = piece.structure;
    if (structure.sections.empty()) {
        return fail("Song Mode needs a song structure: analyze the track in the Sequence panel, or "
                    "author a song plan, before directing to it");
    }
    return songPlanFromMeasurements(structure);
}

Result<SongDirection> directSongFromPlan(std::span<const world::HeroPoint> heroes,
                                         const SongPlan& plan,
                                         const scene::CameraDirection& cameras,
                                         const AutoDirectorSettings& settings) {
    auto brief = briefFromHeroes(heroes);
    if (!brief) {
        return std::unexpected(brief.error());
    }
    settings.applyTo(*brief);
    const std::vector<scene::CameraRig> eligible = eligibleCameras(cameras);
    SongDirectorOptions options;
    options.minShotSeconds = settings.minShotSeconds;
    options.maxShotSeconds = settings.maxShotSeconds;
    options.seed = settings.seed;
    options.autonomy = settings.autonomy;
    // The lens the framing bake commits to, from the hero's own proportions -- the same choice
    // `briefFromHeroes` makes, carried through so Song Mode and the other two modes frame a tall
    // thin subject the same way.
    options.focalLength = brief->focalLength;
    auto direction = directSong(plan, *brief, eligible, options);
    if (!direction) {
        return std::unexpected(direction.error());
    }
    // ADR-200's caps apply to Song Mode exactly as they do to the other two: they are a judgement
    // about the *result*, and a Song Mode result is a camera moving through a world like any other.
    if (settings.maxCameraSpeed > 0.0f) {
        if (const std::size_t shortened =
                direction->sequence.limitCameraSpeed(settings.maxCameraSpeed);
            shortened > 0) {
            log::info("song director: {} of {} shot(s) shortened to hold {:.1f} m/s", shortened,
                      direction->sequence.shots.size(), settings.maxCameraSpeed);
        }
    }
    if (settings.maxViewRate > 0.0f) {
        if (const std::size_t widened = direction->sequence.limitViewRate(settings.maxViewRate);
            widened > 0) {
            log::info("song director: {} of {} shot(s) given a longer swing to hold {:.0f} deg/s",
                      widened, direction->sequence.shots.size(), settings.maxViewRate);
        }
    }
    setDirectionSummary(fmt::format(
        "{} section(s), {} shot(s), {} camera(s) -- {} at most", plan.sections.size(),
        direction->sequence.shots.size(), direction->camerasUsed(),
        autonomyName(settings.autonomy)));
    return direction;
}

Result<std::size_t> installSongDirection(Engine& engine, const SongDirection& direction,
                                         const AutoDirectorSettings& settings) {
    // **The framing half is baked; it does not become shots on the sequencer.**
    //
    // It briefly did. The reasoning was that a generated edit a person cannot select, drag or trim
    // is not an edit -- true of an *edited sequence*, and wrong about Song Mode, which is the thing
    // that was actually changed. Song Mode is a live directing mode: it decides coverage from the
    // song's own structure, and re-deciding is how it is used. Writing 43 shots into the Shots lane
    // every time it ran gave a person a lane full of objects they did not author, could not
    // meaningfully re-cut without the director overwriting them, and had to delete by hand to get
    // back to an empty timeline. It also quietly broke any hand-made shot that happened to overlap
    // the first generated one, because `Sequence::validate` refuses overlaps outright.
    //
    // So Song Mode goes back to what the other two director modes do: bake the framing onto the
    // timeline and leave the sequencer alone. `installSequence` is the same call `Continuous shot`
    // and `Edited sequence` make, which also means Song Mode regains the thing it had silently lost
    // by not calling it -- **`setShotSpans`**, the flattened cut that world effects gate on. Without
    // it the World Effects panel correctly reported "No directed camera: effects gated on the cut
    // cannot fire", because there was no cut to gate on.
    const auto installed = installSequence(engine, direction.sequence, settings);
    if (!installed) {
        // Reported rather than swallowed: a Song Mode that failed to bake its framing and then
        // installed a camera track anyway would cut between cameras that are not being moved.
        return std::unexpected(installed.error());
    }
    const std::size_t installedCount = *installed;

    scene::Composition* composition = engine.composition();
    if (composition == nullptr) {
        return fail("Song Mode needs a scene to put its camera track on");
    }
    scene::CameraDirection cameras = composition->cameraDirection();
    // Everything the director owns goes; everything a person authored stays. The identical rule the
    // timeline half follows a few lines above, and for the identical reason: two cuts on one track
    // is not a blend, it is whichever the resolver takes last (ADR-245's rule 2).
    const std::size_t before = cameras.shots.size();
    std::erase_if(cameras.shots, [](const scene::CameraShot& s) {
        return s.origin == scene::CameraShot::Origin::Directed;
    });
    const std::size_t replaced = before - cameras.shots.size();
    for (const scene::CameraShot& shot : direction.shots) {
        scene::CameraShot copy = shot;
        copy.origin = scene::CameraShot::Origin::Directed;
        cameras.shots.push_back(std::move(copy));
    }
    // In time order, because the resolver takes the *last* match and an unsorted list would make a
    // directed shot's precedence depend on the order the director happened to emit it in.
    std::stable_sort(cameras.shots.begin(), cameras.shots.end(),
                     [](const scene::CameraShot& a, const scene::CameraShot& b) {
                         return a.startSeconds < b.startSeconds;
                     });
    if (auto ok = engine.setCameraDirection(std::move(cameras)); !ok) {
        return std::unexpected(ok.error());
    }
    log::info("song director: {} camera shot(s) installed, {} replaced, {} camera(s) in use",
              direction.shots.size(), replaced, direction.camerasUsed());
    for (const SongDecision& decision : direction.decisions) {
        log::info("song director: {}", decision.line());
    }
    return installedCount;
}

Result<std::size_t> directEngine(Engine& engine, std::span<const world::HeroPoint> heroes,
                                 const AutoDirectorSettings& settings) {
    if (auto ok = settings.validate(); !ok) {
        return std::unexpected(ok.error());
    }
    // Song Mode does not fold audio: the fold has already happened and somebody has edited the
    // result. So it needs no analyzed track and works on a project whose audio is not loaded, which
    // is what makes an offline render of a Song Mode cut possible from the project alone.
    if (settings.mode == DirectorMode::Song) {
        auto plan = songPlanForEngine(engine);
        if (!plan) {
            return std::unexpected(plan.error());
        }
        const scene::Composition* composition = engine.composition();
        if (composition == nullptr) {
            return fail("Song Mode needs a scene to direct");
        }
        auto direction = directSongFromPlan(heroes, *plan, composition->cameraDirection(), settings);
        if (!direction) {
            return std::unexpected(direction.error());
        }
        log::info("song director: {:.0f}s of authored song in {} section(s)",
                  plan->durationSeconds(), plan->sections.size());
        return installSongDirection(engine, *direction, settings);
    }
    const analysis::AnalysisTrack* track = engine.track();
    if (track == nullptr) {
        return fail("the Auto-director needs analyzed audio; load a track first");
    }
    auto structure = structureOfTrack(*track);
    if (!structure) {
        return std::unexpected(structure.error());
    }
    auto sequence = directHeroes(heroes, *structure, settings);
    if (!sequence) {
        return std::unexpected(sequence.error());
    }
    log::info("auto-director: {:.0f}s of audio folded into {} section(s)",
              structure->durationSeconds(), structure->sections.size());
    return installSequence(engine, *sequence, settings);
}

Result<std::size_t> installSequence(Engine& engine, const Sequence& sequence,
                                   const AutoDirectorSettings& settings) {
    if (auto ok = sequence.validate(); !ok) {
        return std::unexpected(ok.error());
    }
    const nlohmann::json baked = sequence.toTimelineTracks();
    if (!baked.is_array() || baked.empty()) {
        return fail("the director produced no camera tracks");
    }

    params::Timeline& timeline = engine.timeline();
    // Everything the director owns goes; everything else stays. A project's automation of anything
    // that is not the camera is somebody's work, and directing the camera is not a reason to throw
    // it away -- but leaving the *old* camera tracks in place would be worse than either, because
    // two tracks writing camera/position is not a blend, it is whichever the timeline applies last.
    auto& tracks = timeline.tracks();
    const std::size_t before = tracks.size();
    std::erase_if(tracks, [](const params::Track& t) { return isCameraTarget(t.target); });
    const std::size_t removed = before - tracks.size();

    // Keep the path out of the scenery before any of it is installed (ADR-080).
    //
    // Shot geometry is orbit points at a distance in radii and knows nothing about what is in the
    // way, so a directed camera will fly through a hillside or a canopy as readily as through open
    // air. Glowmere's first directed pass went through the trees.
    nlohmann::json cleared = baked;
    std::size_t movedKeys = 0;
    world::SightlineResult sight;
    if (const scene::CompositionNode* terrain = terrainNodeOf(engine)) {
        world::ClearanceField field;
        field.map = &terrain->worldMap;
        field.ecology = &terrain->ecology;
        const std::vector<world::HeroPoint>& heroes = engine.composition()->heroes();
        field.heroes = heroes;
        // Which subject every baked position key is holding, from the sequence that produced them.
        const std::vector<const FocalTarget*> held = sequence.heldSubjectPerKey();
        for (auto& track : cleared) {
            if (track.value("target", std::string()) != "camera/position") {
                continue;
            }
            auto& keys = track["keys"];
            std::vector<glm::vec3> path;
            path.reserve(keys.size());
            for (const auto& key : keys) {
                const auto& v = key.at("value");
                path.emplace_back(v[0].get<float>(), v[1].get<float>(), v[2].get<float>());
            }
            // Heroes are pushed out sideways and terrain lifts, so this is no longer only a lift.
            //
            // `+=`, and the `=` it replaces was a real reporting bug: this loop runs over every
            // baked track and the count of the last one overwrote all the others. It read correctly
            // only because exactly one track is named `camera/position` today, which is a fact about
            // the bake and not about this loop.
            movedKeys += world::clearPath(field, path);

            // Then the half ADR-080 never had: whether anything stands *between* the cleared eye and
            // the hero it is framing. Run after `clearPath` on purpose -- a camera inside a hillside
            // has no sightline worth asking about, and the lift that gets it out changes the answer.
            std::vector<world::SightlineTarget> targets(path.size());
            for (std::size_t i = 0; i < path.size() && i < held.size(); ++i) {
                if (held[i] == nullptr) {
                    continue;   // this key's shot is aiming down its move, or mid-handoff-swing
                }
                targets[i].holds = true;
                targets[i].subject.name = held[i]->name;
                targets[i].subject.position = held[i]->position;
                targets[i].subject.radius = held[i]->radius;
                // A `FocalTarget` carries a radius and no height, because a shot composes in radii.
                // The hero it names carries both, so the capsule is the hero's own where the world
                // has one -- a sixteen-metre elder and a sixteen-metre-wide pool are not the same
                // silhouette, and a sightline that assumed they were would clear the wrong thing.
                targets[i].subject.height = held[i]->radius * 2.0f;
                for (const world::HeroPoint& hero : heroes) {
                    if (hero.name == held[i]->name) {
                        targets[i].subject.position = hero.position;
                        targets[i].subject.radius = hero.radius;
                        targets[i].subject.height = hero.height;
                        break;
                    }
                }
            }
            const world::SightlineResult r =
                world::clearSightlines(field, path, targets, settings.maxSightlineCorrection);
            sight.examined += r.examined;
            sight.obstructed += r.obstructed;
            sight.corrected += r.corrected;
            sight.refused += r.refused;
            sight.worstRefused = std::max(sight.worstRefused, r.worstRefused);
            sight.largestApplied = std::max(sight.largestApplied, r.largestApplied);

            for (std::size_t i = 0; i < keys.size() && i < path.size(); ++i) {
                keys[i]["value"] = {path[i].x, path[i].y, path[i].z};
            }
        }
    }
    if (movedKeys > 0) {
        log::info("auto-director: moved {} camera key(s) clear of the terrain, the canopy or a hero "
                  "-- up out of the ground, and sideways out of a subject",
                  movedKeys);
    }
    if (sight.obstructed > 0) {
        log::info("auto-director: {} of {} key(s) holding a hero could not see it; {} moved clear "
                  "(worst {:.2f} m), {} left as composed because clearing them would have cost more "
                  "than the shot (worst {:.2f} m)",
                  sight.obstructed, sight.examined, sight.corrected, sight.largestApplied,
                  sight.refused, sight.worstRefused);
    } else if (sight.examined > 0) {
        log::info("auto-director: all {} key(s) holding a hero can see it", sight.examined);
    }

    std::size_t added = 0;
    std::vector<std::string> unknown;
    for (const auto& entry : cleared) {
        // A track for a parameter this build does not have is a track that binds to nothing and is
        // skipped in silence by the timeline. Better to leave it out and name it once: the bake
        // emits camera/focus/emphasis for hero spotlighting, which nothing registers yet, and an
        // unbound track sitting in a saved project is a puzzle for whoever opens it next.
        const std::string target = entry.value("target", std::string());
        if (!target.empty() && engine.params().find(target) == nullptr) {
            unknown.push_back(target);
            continue;
        }
        // Parsed through the timeline's own reader rather than by hand, so a directed track and an
        // authored one are the same kind of object and cannot drift apart in what they support.
        nlohmann::json wrapper = nlohmann::json::object();
        wrapper["enabled"] = true;
        wrapper["tracks"] = nlohmann::json::array({entry});
        params::Timeline scratch;
        if (auto ok = scratch.fromJson(wrapper); !ok) {
            return fail("directed track: {}", ok.error().message);
        }
        for (params::Track& parsed : scratch.tracks()) {
            timeline.addTrack(std::move(parsed));
            ++added;
        }
    }
    // A directed camera in a disabled timeline is a camera that does not move, which looks
    // exactly like the director having failed.
    timeline.enabled = true;
    // Bind, or none of the above reaches a camera.
    //
    // `Track::param` is runtime state resolved by `bind()`, and the timeline's apply loop writes
    // only *bound* tracks -- an unbound one is skipped in silence. Tracks that arrive with a
    // project are bound when the engine rebinds after a load; these arrive afterwards and never
    // were, so the director reported six tracks installed, the keys were correct, the timeline was
    // enabled, and the camera sat perfectly still.
    if (!unknown.empty()) {
        std::string names;
        for (const std::string& t : unknown) {
            names += names.empty() ? t : ", " + t;
        }
        log::info("auto-director: {} baked track(s) name parameters this build does not have and "
                  "were left out: {}", unknown.size(), names);
    }
    if (auto bound = timeline.bind(engine.params()); !bound) {
        // A target that does not resolve is worth naming rather than swallowing: it means the
        // director is shooting at a parameter this scene does not have.
        log::warn("auto-director: {}", bound.error().message);
    }
    // ADR-158: which hero each shot was cut for, so its aim can follow that hero as it walks.
    //
    // Only shots that *hold* a subject. The other look modes are about somewhere the camera is
    // going rather than something it is watching: `Ahead` aims down the move, `Parallel` freezes a
    // direction so the parallax is the shot, `Fixed` holds a place, and `Handoff` leaves one subject
    // for another halfway through -- following the first through that swing would drag the very
    // thing the shot is trying to leave. A shot that is not about a subject does not follow one.
    if (scene::Composition* composition = engine.composition()) {
        std::vector<scene::AimFollow> follow;
        follow.reserve(sequence.shots.size());
        for (const Shot& shot : sequence.shots) {
            if (shot.lookMode() != LookMode::Subject || shot.subject.name.empty()) {
                continue;
            }
            follow.push_back(scene::AimFollow{.startSeconds = shot.startSeconds,
                                              .endSeconds = shot.endSeconds(),
                                              .hero = shot.subject.name,
                                              .heroAtCut = shot.subject.position});
        }
        log::info("auto-director: {} of {} shot(s) hold a subject and will follow it",
                  follow.size(), sequence.shots.size());
        composition->setAimFollow(std::move(follow));
    }
    // ADR-207: the cut, flattened for world effects to time-gate against. Installed with the keys
    // rather than derived per frame, for the same reason the keys exist at all -- a shot schedule is
    // a fold over a whole track and the frame you are on cannot know it. Nothing here runs per
    // frame; `resolveWaves` does a linear scan of single digits of spans.
    {
        std::vector<world::ShotSpan> spans = sequence.shotSpans();
        // At debug level, because "why is my world effect not firing" is otherwise unanswerable
        // without a debugger: an effect gated on the cut is gated on exactly these spans.
        std::size_t travelling = 0;
        std::size_t holding = 0;
        for (const world::ShotSpan& span : spans) {
            travelling += span.travel ? 1 : 0;
            holding += span.spotlight ? 1 : 0;
            log::debug("auto-director: span {:7.2f}s..{:7.2f}s {:<9} {}{}", span.start, span.end,
                       span.travel ? "travelling" : (span.spotlight ? "holding" : "--"), span.subject,
                       span.handoff.empty() ? "" : " -> " + span.handoff);
        }
        log::info("auto-director: {} span(s) for world effects: {} travelling, {} holding",
                  spans.size(), travelling, holding);
        engine.setShotSpans(std::move(spans));
    }
    // ADR-582: a new cut replaces a parked one. This is the explicit re-bake -- Enable
    // Auto-director, or a director control moved while it steers -- and it is the only way besides
    // `discardDirectorsCut` that a parked cut may go. Left behind, "Resume director" would put an
    // older film's camera back over this one's spans.
    if (engine.directorParked()) {
        log::info("auto-director: the new cut replaces the parked one");
        engine.setParkedCut({});
    }
    log::info("auto-director: {} shot(s), {} track(s) installed, {} replaced",
              sequence.shots.size(), added, removed);
    return added;
}

} // namespace avgen::app
