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

std::size_t releaseDirectedCamera(Engine& engine, DirectorState& state) {
    auto& tracks = engine.timeline().tracks();
    const std::size_t before = tracks.size();
    const auto owned = directedCameraTargets();
    std::erase_if(tracks, [&](const params::Track& t) {
        return std::find(owned.begin(), owned.end(), t.target) != owned.end();
    });
    state = DirectorState{};
    return before - tracks.size();
}

void noteDirected(Engine& engine, DirectorState& state) {
    const scene::Composition* composition = engine.composition();
    state.directed = composition != nullptr;
    state.heroRevision = composition != nullptr ? composition->heroRevision() : 0;
    state.placementRevision = composition != nullptr ? composition->heroPlacementRevision() : 0;
    state.wasPlaying = engine.transport().isPlaying();
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
    // is no longer our camera and the next Direct to Music starts the relationship again.
    if (composition == nullptr || !engine.timeline().isAutomated("camera/position")) {
        state = DirectorState{};
        return Redirect::Released;
    }
    const bool playing = engine.transport().isPlaying();
    const bool parked = !playing && !state.wasPlaying;
    state.wasPlaying = playing;
    const bool castChanged = composition->heroRevision() != state.heroRevision;
    const bool movedAndParked = composition->heroPlacementRevision() != state.placementRevision && parked;
    if (!castChanged && !movedAndParked) {
        // Absorbed rather than queued: a hero that walked around during playback has already been
        // followed by everything that reads a position, and re-cutting for it the moment somebody
        // pauses would be answering a question nobody asked.
        state.placementRevision = composition->heroPlacementRevision();
        return Redirect::Nothing;
    }
    // Recorded before the work, not after: a re-cut that fails must not be retried every frame for
    // the rest of the session, and the failure is the same one until the heroes change again.
    state.heroRevision = composition->heroRevision();
    state.placementRevision = composition->heroPlacementRevision();
    if (composition->heroes().empty()) {
        static_cast<void>(releaseDirectedCamera(engine, state));
        return Redirect::HandedBack;
    }
    if (auto installed = directEngine(engine, composition->heroes(), state.seed); !installed) {
        return std::unexpected(installed.error());
    }
    return Redirect::Recut;
}

Result<DirectionBrief> briefFromHeroes(std::span<const world::HeroPoint> heroes) {
    if (heroes.empty()) {
        return fail("the camera director needs something to point at: this world has no heroes");
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
        t.preferredDistance = h.preferredCameraDistance;
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
    brief.heroFocalLength = heroes.front().height > heroes.front().radius * 4.0f ? 50.0f : 35.0f;
    return brief;
}

Result<Sequence> directHeroes(std::span<const world::HeroPoint> heroes,
                              const signals::MusicalStructure& structure, std::uint32_t seed) {
    auto brief = briefFromHeroes(heroes);
    if (!brief) {
        return std::unexpected(brief.error());
    }
    brief->seed = seed;
    auto sequence = directFromStructure(structure, *brief);
    if (!sequence) {
        return std::unexpected(sequence.error());
    }
    return sequence;
}

Result<signals::MusicalStructure> structureOfTrack(const analysis::AnalysisTrack& track,
                                                   int phraseBars, int sectionPhrases) {
    const auto& frames = track.frames();
    if (frames.empty()) {
        return fail("the camera director needs an analysed track: this one has no frames");
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
        return fail("the analysed track has no duration");
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

Result<std::size_t> directEngine(Engine& engine, std::span<const world::HeroPoint> heroes,
                                 std::uint32_t seed) {
    const analysis::AnalysisTrack* track = engine.track();
    if (track == nullptr) {
        return fail("the camera director needs analysed audio; load a track first");
    }
    auto structure = structureOfTrack(*track);
    if (!structure) {
        return std::unexpected(structure.error());
    }
    auto sequence = directHeroes(heroes, *structure, seed);
    if (!sequence) {
        return std::unexpected(sequence.error());
    }
    log::info("camera director: {:.0f}s of audio folded into {} section(s)",
              structure->durationSeconds(), structure->sections.size());
    return installSequence(engine, *sequence);
}

Result<std::size_t> installSequence(Engine& engine, const Sequence& sequence) {
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
    std::size_t liftedKeys = 0;
    if (const scene::CompositionNode* terrain = terrainNodeOf(engine)) {
        world::ClearanceField field;
        field.map = &terrain->worldMap;
        field.ecology = &terrain->ecology;
        const std::vector<world::HeroPoint>& heroes = engine.composition()->heroes();
        field.heroes = heroes;
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
            liftedKeys = world::clearPath(field, path);
            for (std::size_t i = 0; i < keys.size() && i < path.size(); ++i) {
                keys[i]["value"] = {path[i].x, path[i].y, path[i].z};
            }
        }
    }
    if (liftedKeys > 0) {
        log::info("camera director: lifted {} camera key(s) clear of the terrain, canopy or a hero",
                  liftedKeys);
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
        log::info("camera director: {} baked track(s) name parameters this build does not have and "
                  "were left out: {}", unknown.size(), names);
    }
    if (auto bound = timeline.bind(engine.params()); !bound) {
        // A target that does not resolve is worth naming rather than swallowing: it means the
        // director is shooting at a parameter this scene does not have.
        log::warn("camera director: {}", bound.error().message);
    }
    log::info("camera director: {} shot(s), {} track(s) installed, {} replaced",
              sequence.shots.size(), added, removed);
    return added;
}

} // namespace avgen::app
