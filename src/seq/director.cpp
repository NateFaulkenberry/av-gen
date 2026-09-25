#include "seq/director.hpp"
#include <cmath>
#include <numbers>
#include <limits>

#include "core/log.hpp"
#include <fmt/format.h>
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

std::optional<ResolvedCue> resolveCue(const AnimationCue& cue, double seconds, const ClipLookup& lookup) {
    if (cue.clip.empty()) {
        return std::nullopt; // the sequence letting go of the rig
    }
    const scene::ClipSemantics* clip = lookup ? lookup(cue.clip) : nullptr;
    ResolvedCue out;
    out.clip = cue.clip;
    out.startSeconds = cue.startSeconds;
    out.speed = cue.speed;
    out.blendSeconds = cue.blendSeconds;
    switch (cue.playback) {
    case ClipPlayback::Loop: out.loop = true; break;
    case ClipPlayback::Once: out.loop = false; break;
    case ClipPlayback::Auto:
        // Unmeasured (no rig answers for it): the state's own default, exactly as before.
        if (clip != nullptr) {
            out.loop = clip->loops;
        }
        break;
    }
    // A one-shot with somewhere to go, once it is over. Its length is the measured one; a clip nobody
    // measured has no known end, and holds.
    if (out.loop == std::optional<bool>(false) && !cue.then.empty() && clip != nullptr && cue.speed > 0.0f) {
        const double end = cue.startSeconds + static_cast<double>(clip->length / cue.speed);
        if (seconds >= end) {
            if (cue.then == kThenGait) {
                out.gait = true;
                out.clip.clear();
                out.startSeconds = end;
                return out;
            }
            AnimationCue next{.node = cue.node, .clip = cue.then, .startSeconds = end};
            return resolveCue(next, seconds, lookup);
        }
    }
    return out;
}

double clipEventSeconds(const AnimationCue& cue, float eventClipSeconds) {
    const float speed = cue.speed > 0.0f ? cue.speed : 1.0f;
    return cue.startSeconds + static_cast<double>(eventClipSeconds / speed);
}

ClipLookup clipLookupFor(const scene::Composition& composition, const std::string& node) {
    const scene::ClipSemanticsTable* table = composition.clipSemanticsFor(node);
    if (table == nullptr) {
        return {};
    }
    return [table](std::string_view clip) { return table->find(clip); };
}

namespace {

bool outsidePerformance(const scene::Composition& composition, const std::string& node, double seconds) {
    for (const scene::Composition::Performer& p : composition.performers()) {
        const entity::Entity* e = composition.entityWorld().find(p.entity);
        if (e != nullptr && e->desc().driven() == node) {
            return seconds < p.from || seconds >= p.to;
        }
    }
    return false;
}

} // namespace

void applyAnimation(const Sequence& sequence, std::span<const ScheduledClip> scheduled,
                    scene::Composition& composition, double seconds) {
    if (sequence.actors.empty()) {
        return;
    }
    for (const AnimationCue& cue : sequence.animationAt(seconds, scheduled)) {
        // ADR-820: a performer's clips belong to its performance and last as long as it does. Past
        // the span the body is the entity's again, gait and all; before it, the actor has not taken
        // it yet. (An actor with clips and no span is not a performer and is applied as always.)
        if (outsidePerformance(composition, cue.node, seconds)) {
            continue;
        }
        // ADR-821: the cue's playback and its `then`, resolved against what the clip measures as.
        const std::optional<ResolvedCue> resolved = resolveCue(cue, seconds, clipLookupFor(composition, cue.node));
        if (!resolved || resolved->gait) {
            continue; // handed back: a performer's gait has the rig; anyone else's holds
        }
        // `rebase` is the whole point: the same clip cued twice in a piece is two different phase
        // origins, and AnimationPlayer::play() deliberately refuses to restart a state it is
        // already in. Without it, a character who walks at 0:12 and walks again at 1:04 would take
        // the second walk with the first one's phase, and a scrub backwards would be worse.
        (void)composition.setNodeAnimation(cue.node, resolved->clip, resolved->startSeconds,
                                           resolved->blendSeconds, resolved->speed, /*rebase=*/true,
                                           resolved->loop);
    }
}

namespace {

// ADR-820/821: the cue this actor is under at `t` -- the later of its authored cue and any clip
// scheduled for it, the same rule `Sequence::animationAt` applies.
std::optional<AnimationCue> actorCueAt(const Actor& actor, std::span<const ScheduledClip> scheduled, double t) {
    const ClipCue* authored = actor.clipAt(t);
    const ScheduledClip* latest = nullptr;
    for (const ScheduledClip& c : scheduled) {
        if (c.actor == actor.id && c.timeSeconds <= t &&
            (latest == nullptr || c.timeSeconds >= latest->timeSeconds)) {
            latest = &c;
        }
    }
    if (latest != nullptr && (authored == nullptr || authored->timeSeconds <= latest->timeSeconds)) {
        return AnimationCue{.node = actor.nodeName(), .clip = latest->clip, .startSeconds = latest->timeSeconds,
                            .speed = latest->speed, .blendSeconds = latest->blendSeconds};
    }
    if (authored == nullptr) {
        return std::nullopt;
    }
    return AnimationCue{.node = actor.nodeName(), .clip = authored->clip, .startSeconds = authored->timeSeconds,
                        .speed = authored->speed, .blendSeconds = authored->blendSeconds,
                        .playback = authored->playback, .then = authored->then};
}

} // namespace

std::optional<scene::Composition::Performer> performerFor(const Actor& actor, std::string entity,
                                                          std::function<float(float x, float z)> groundAt,
                                                          std::span<const ScheduledClip> scheduled,
                                                          ClipLookup lookup) {
    double from = std::numeric_limits<double>::infinity();
    double to = -std::numeric_limits<double>::infinity();
    for (const ActorKey& k : actor.keys) {
        from = std::min(from, k.timeSeconds);
        to = std::max(to, k.timeSeconds);
    }
    if (actor.path.active) {
        from = std::min(from, actor.path.startSeconds);
        to = std::max(to, actor.path.endSeconds);
    }
    if (!(to > from)) {
        return std::nullopt;
    }
    scene::Composition::Performer out;
    out.entity = std::move(entity);
    out.from = from;
    out.to = to;
    {
        // The signature is the actor's own document: any edit to it is a different replay.
        Sequence one;
        one.actors.push_back(actor);
        std::uint64_t h = 1469598103934665603ULL;
        std::string document = one.toJson().dump() + "|" + out.entity;
        for (const ScheduledClip& c : scheduled) {
            if (c.actor == actor.id) {
                document += fmt::format("|{}@{}", c.clip, c.timeSeconds);
            }
        }
        for (const char c : document) {
            h ^= static_cast<unsigned char>(c);
            h *= 1099511628211ULL;
        }
        out.signature = h;
    }
    out.entrySeconds = actor.entrySeconds;
    std::vector<ScheduledClip> mine;
    for (const ScheduledClip& c : scheduled) {
        if (c.actor == actor.id) {
            mine.push_back(c);
        }
    }
    out.pose = [actor, from, to, groundAt, mine = std::move(mine), lookup = std::move(lookup)](double t) {
        constexpr double kH = 1.0 / 120.0;
        const auto velocity = [&](double at) {
            const double a = std::max(from, at - kH);
            const double b = std::min(to, at + kH);
            return b > a ? (actor.positionAt(b) - actor.positionAt(a)) / static_cast<float>(b - a) : glm::vec3(0.0f);
        };
        scene::Composition::PerformerPose pose;
        // ADR-821: owned while a cue plays; handed back once a one-shot's `then: gait` is reached.
        if (const std::optional<AnimationCue> cue = actorCueAt(actor, mine, t)) {
            const std::optional<ResolvedCue> resolved = resolveCue(*cue, t, lookup);
            pose.clipOwned = resolved.has_value() && !resolved->gait;
        }
        pose.position = actor.positionAt(t);
        // Standing on the terrain -- unless the actor says the body is in the air (ADR-822), where
        // the height is the arc's and snapping it to the ground would turn a jump into a skid.
        if (groundAt && !actor.airborneAt(t)) {
            pose.position.y = groundAt(pose.position.x, pose.position.z);
        }
        const glm::vec3 v = velocity(t);
        pose.speed = glm::length(glm::vec2(v.x, v.z));
        constexpr float kRadians = std::numbers::pi_v<float> / 180.0f;
        // Explicit headings win BETWEEN TWO CONSECUTIVE KEYS THAT BOTH STATE ONE -- a `look_at` is a
        // pair of rotation keys, not a direction of travel. Elsewhere the body faces where it goes.
        for (std::size_t i = 0; i + 1 < actor.keys.size(); ++i) {
            const ActorKey& a = actor.keys[i];
            const ActorKey& b = actor.keys[i + 1];
            if (a.rotationDegrees && b.rotationDegrees && t >= a.timeSeconds && t <= b.timeSeconds) {
                const double span = b.timeSeconds - a.timeSeconds;
                const float w = span > 1e-9 ? static_cast<float>((t - a.timeSeconds) / span) : 0.0f;
                pose.yawRadians = std::lerp(a.rotationDegrees->y, b.rotationDegrees->y, w) * kRadians;
                return pose;
            }
        }
        // The direction of travel, or the last one it travelled in (a performer who stops keeps
        // facing where it was going), or the next one (a performance that opens standing still).
        constexpr float kMoving = 0.05f;
        const auto yawOf = [](const glm::vec3& d) { return std::atan2(d.x, d.z); };
        if (pose.speed > kMoving) {
            pose.yawRadians = yawOf(v);
            return pose;
        }
        for (double s = t - (1.0 / 30.0); s >= from; s -= 1.0 / 30.0) {
            const glm::vec3 back = velocity(s);
            if (glm::length(glm::vec2(back.x, back.z)) > kMoving) {
                pose.yawRadians = yawOf(back);
                return pose;
            }
        }
        for (double s = t + (1.0 / 30.0); s <= to; s += 1.0 / 30.0) {
            const glm::vec3 ahead = velocity(s);
            if (glm::length(glm::vec2(ahead.x, ahead.z)) > kMoving) {
                pose.yawRadians = yawOf(ahead);
                return pose;
            }
        }
        return pose;
    };
    return out;
}

} // namespace avgen::seq
