#include "seq/retime.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace avgen::seq {

double retimeMap(double t, double a, double b, float rate) {
    const double r = static_cast<double>(rate);
    if (t <= a) {
        return t;
    }
    if (t <= b) {
        return a + (t - a) / r;
    }
    return t + (b - a) * (1.0 / r - 1.0);
}

Result<void> retimeActor(Actor& actor, double a, double b, float rate) {
    if (!(rate > 0.0f)) {
        return fail("retime of '{}': the rate must be positive", actor.id);
    }
    if (!(b > a)) {
        return fail("retime of '{}': the window is empty", actor.id);
    }
    for (const Actor::TimeWarp& w : actor.timeWarps) {
        if (a < w.endSeconds && b > w.startSeconds) {
            return fail("retime of '{}': [{}, {}] overlaps an earlier retime [{}, {}]", actor.id, a, b,
                        w.startSeconds, w.endSeconds);
        }
    }
    if (actor.path.active) {
        const bool inside = actor.path.startSeconds >= a && actor.path.endSeconds <= b;
        const bool outside = actor.path.endSeconds <= a || actor.path.startSeconds >= b;
        if (!inside && !outside) {
            return fail("retime of '{}': the window cuts through its path; bake the path to keys first",
                        actor.id);
        }
    }
    const auto map = [&](double t) { return retimeMap(t, a, b, rate); };

    // ---- keys: an edge key where an interval straddles an edge, then the map ------------------
    std::vector<ActorKey> keys;
    keys.reserve(actor.keys.size() + 2);
    for (std::size_t i = 0; i < actor.keys.size(); ++i) {
        const ActorKey& k = actor.keys[i];
        if (i > 0) {
            const ActorKey& prev = actor.keys[i - 1];
            for (const double edge : {a, b}) {
                if (prev.timeSeconds < edge && k.timeSeconds > edge) {
                    ActorKey mid;
                    mid.timeSeconds = edge;
                    mid.position = actor.positionAt(edge);
                    mid.interp = prev.interp;
                    if (prev.rotationDegrees && k.rotationDegrees) {
                        const float w = static_cast<float>((edge - prev.timeSeconds) / (k.timeSeconds - prev.timeSeconds));
                        mid.rotationDegrees = glm::mix(*prev.rotationDegrees, *k.rotationDegrees, w);
                    }
                    keys.push_back(mid);
                }
            }
        }
        keys.push_back(k);
    }
    for (ActorKey& k : keys) {
        k.timeSeconds = map(k.timeSeconds);
    }

    // ---- clip cues: split at the edges, carrying clip time and scaling speed ------------------
    std::vector<ClipCue> cues;
    for (std::size_t i = 0; i < actor.clips.size(); ++i) {
        const ClipCue& c = actor.clips[i];
        const double until = i + 1 < actor.clips.size() ? actor.clips[i + 1].timeSeconds
                                                        : std::numeric_limits<double>::infinity();
        std::vector<double> starts{c.timeSeconds};
        for (const double edge : {a, b}) {
            if (edge > c.timeSeconds && edge < until) {
                starts.push_back(edge);
            }
        }
        for (std::size_t s = 0; s < starts.size(); ++s) {
            const double u = starts[s];
            ClipCue piece = c;
            piece.timeSeconds = map(u);
            // Clip time reached at `u`, at the cue's own speed.
            piece.offsetSeconds = c.offsetSeconds + static_cast<float>(u - c.timeSeconds) * c.speed;
            piece.speed = c.speed * (u >= a && u < b ? rate : 1.0f);
            if (s > 0) {
                piece.blendSeconds = 0.0f; // the same clip, continuing: nothing to cross-fade
            }
            cues.push_back(std::move(piece));
        }
    }

    // ---- everything else that lives on the timeline --------------------------------------------
    for (auto& [from, to] : actor.airborne) {
        from = map(from);
        to = map(to);
    }
    for (Actor::TimeWarp& w : actor.timeWarps) {
        w.startSeconds = map(w.startSeconds);
        w.endSeconds = map(w.endSeconds);
    }
    if (actor.path.active) {
        actor.path.startSeconds = map(actor.path.startSeconds);
        actor.path.endSeconds = map(actor.path.endSeconds);
    }
    actor.keys = std::move(keys);
    actor.clips = std::move(cues);
    actor.timeWarps.push_back(Actor::TimeWarp{a, map(b), rate});
    std::sort(actor.timeWarps.begin(), actor.timeWarps.end(),
              [](const Actor::TimeWarp& x, const Actor::TimeWarp& y) { return x.startSeconds < y.startSeconds; });
    return {};
}

} // namespace avgen::seq
