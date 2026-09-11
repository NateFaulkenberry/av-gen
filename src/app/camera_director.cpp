#include "app/camera_director.hpp"

#include "app/engine.hpp"
#include "app/music_runtime.hpp"
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
constexpr std::array<std::string_view, 6> kCameraTargets{
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
} // namespace

std::span<const std::string_view> directedCameraTargets() { return kCameraTargets; }

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

    std::size_t added = 0;
    for (const auto& entry : baked) {
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
    log::info("camera director: {} shot(s), {} track(s) installed, {} replaced",
              sequence.shots.size(), added, removed);
    return added;
}

} // namespace avgen::app
