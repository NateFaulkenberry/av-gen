#include "seq/director.hpp"

#include "core/log.hpp"
#include "scene/composition.hpp"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <unordered_set>
#include <utility>

namespace avgen::seq {
namespace {

// Erases every track naming one of `targets`, and reports how many went.
int eraseTracks(params::Timeline& timeline, const std::unordered_set<std::string>& targets) {
    if (targets.empty()) {
        return 0;
    }
    auto& tracks = timeline.tracks();
    const std::size_t before = tracks.size();
    std::erase_if(tracks, [&](const params::Track& t) { return targets.contains(t.target); });
    return static_cast<int>(before - tracks.size());
}

} // namespace

Result<InstallReport> install(const Sequence& sequence, params::Timeline& timeline,
                              params::ParameterSet& params, LayerSink& sink,
                              std::span<const std::string> owned, const BakeOptions& options) {
    // 1. Retire the previous install's layers. Done before the bake, because the bake is what
    //    creates the new ones and a cue removed from the sequence has to take its layer with it.
    sink.clear();
    std::unordered_set<std::string> doomed(owned.begin(), owned.end());
    for (const std::string& path : sink.retiredPaths()) {
        doomed.insert(path);
    }

    // 2. Bake. Pure, and the only thing that can fail on the content of the sequence itself.
    auto baked = sequence.bake(sink, options);
    if (!baked) {
        // The sink has already been cleared, so leave the timeline consistent with that: the old
        // tracks named layers that no longer exist.
        (void)eraseTracks(timeline, doomed);
        return std::unexpected(baked.error());
    }

    InstallReport report;
    report.warnings = std::move(baked->warnings);
    report.overlays = std::move(baked->overlays);
    report.targets = baked->targets;
    report.events = std::move(baked->events);
    report.trackCount = baked->trackCount;
    report.keyCount = baked->keyCount;
    report.layersRealised = sink.realisedCount();
    for (const std::string& w : sink.sinkWarnings()) {
        report.warnings.push_back(w);
    }

    // 3. Out with the old. A target the new bake also writes is in both sets; erasing first and
    //    adding second is what keeps exactly one track per target.
    report.tracksReplaced = eraseTracks(timeline, doomed);

    // 4. In with the new, through Timeline::fromJson so a baked track and a hand-authored one are
    //    literally the same kind of object and cannot drift apart in what they support.
    params::Timeline scratch;
    if (auto ok = scratch.fromJson(baked->timeline); !ok) {
        return fail("sequence '{}': the bake produced a timeline the timeline cannot read: {}",
                    sequence.name, ok.error().message);
    }
    for (params::Track& track : scratch.tracks()) {
        (void)timeline.addTrack(std::move(track));
    }
    if (report.trackCount > 0) {
        timeline.enabled = true;
    }

    // 5. Bind, and make the misses visible. A track naming a parameter that does not exist
    //    evaluates correctly and writes nowhere, forever, in silence (ADR-075).
    if (auto ok = timeline.bind(params); !ok) {
        const std::vector<std::string>& unbound = timeline.unboundTargets();
        for (const std::string& target : unbound) {
            if (std::find(report.targets.begin(), report.targets.end(), target) != report.targets.end()) {
                report.unresolved.push_back(target);
            }
        }
        if (!report.unresolved.empty()) {
            std::string names;
            for (const std::string& target : report.unresolved) {
                names += names.empty() ? target : ", " + target;
            }
            log::warn("sequence '{}': {} target(s) name nothing in this scene: {}", sequence.name,
                      report.unresolved.size(), names);
            report.warnings.push_back(
                fmt::format("{} sequence target(s) name no parameter in this scene ({}); those "
                            "tracks evaluate and change nothing",
                            report.unresolved.size(), names));
        }
    }
    return report;
}

void uninstall(params::Timeline& timeline, params::ParameterSet& params, LayerSink& sink,
               std::span<const std::string> owned) {
    sink.clear();
    std::unordered_set<std::string> doomed(owned.begin(), owned.end());
    for (const std::string& path : sink.retiredPaths()) {
        doomed.insert(path);
    }
    (void)eraseTracks(timeline, doomed);
    (void)timeline.bind(params);
}

void applyAnimation(const Sequence& sequence, scene::Composition& composition, double seconds) {
    applyAnimation(sequence, {}, composition, seconds);
}

void applyAnimation(const Sequence& sequence, std::span<const ScheduledClip> scheduled,
                    scene::Composition& composition, double seconds) {
    if (sequence.actors.empty()) {
        return;
    }
    for (const AnimationCue& cue : sequence.animationAt(seconds, scheduled)) {
        // `rebase` is the whole point: the same clip cued twice in a piece is two different phase
        // origins, and AnimationPlayer::play() deliberately refuses to restart a state it is
        // already in. Without it, a character who walks at 0:12 and walks again at 1:04 would take
        // the second walk with the first one's phase, and a scrub backwards would be worse.
        (void)composition.setNodeAnimation(cue.node, cue.clip, cue.startSeconds, cue.blendSeconds,
                                           cue.speed, /*rebase=*/true);
    }
}

} // namespace avgen::seq
