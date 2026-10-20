#include "analysis/response_feed.hpp"

#include <algorithm>

namespace avgen::analysis {

ResponseFeed ResponseFeed::fromCausal(const CausalOnsets& c) {
    ResponseFeed f;
    f.valid = c.valid;
    for (std::size_t k = 0; k < kHitClassCount; ++k) {
        f.hit[k] = c.valid && c.hit[k]; // a hit needs the frame it was decided on
    }
    f.strength = c.strength;
    f.drumness = c.drumness;
    f.onsetRatio = c.ratio[static_cast<std::size_t>(HitClass::Onset)];
    f.levelDb = c.levelDb;
    f.bassDb = c.bassDb;
    return f;
}

ResponseFeed ResponseFeed::inactive() {
    ResponseFeed f;
    f.valid = true;
    return f;
}

bool FeedRouting::any() const {
    return levels >= 0 || std::any_of(hit.begin(), hit.end(), [](int s) { return s >= 0; });
}

ResponseFeed FeedFolder::fold(std::span<const TimedFeed> hops, double to, bool active) {
    ResponseFeed out;
    for (const TimedFeed& h : hops) {
        if (!h.feed.valid) {
            continue;
        }
        for (std::size_t k = 0; k < kHitClassCount; ++k) {
            if (!h.feed.hit[k]) {
                continue;
            }
            // The strongest event in the window, with the least drumness of any (the response model's pending rule).
            out.drumness[k] = out.hit[k] ? std::min(out.drumness[k], h.feed.drumness[k]) : h.feed.drumness[k];
            out.strength[k] = std::max(out.strength[k], h.feed.strength[k]);
            out.hit[k] = true;
        }
        newest_ = h;
        have_ = true;
    }
    if (!active || !have_ || to - newest_.seconds > kStaleSeconds) {
        return ResponseFeed::inactive();
    }
    out.valid = true;
    out.onsetRatio = newest_.feed.onsetRatio;
    out.levelDb = newest_.feed.levelDb;
    out.bassDb = newest_.feed.bassDb;
    return out;
}

ResponseFeed mergeFeeds(const ResponseFeed& mix, std::span<const ResponseFeed> sources, const FeedRouting& routing) {
    ResponseFeed out = mix;
    const auto source = [&](int i) -> const ResponseFeed* {
        return i >= 0 && static_cast<std::size_t>(i) < sources.size() ? &sources[static_cast<std::size_t>(i)] : nullptr;
    };
    if (const ResponseFeed* s = source(routing.levels)) {
        out.valid = s->valid;
        out.onsetRatio = s->onsetRatio;
        out.levelDb = s->levelDb;
        out.bassDb = s->bassDb;
    }
    for (std::size_t k = 0; k < kHitClassCount; ++k) {
        if (const ResponseFeed* s = source(routing.hit[k])) {
            out.hit[k] = s->valid && s->hit[k];
            out.strength[k] = s->strength[k];
            out.drumness[k] = s->drumness[k];
        }
    }
    return out;
}

} // namespace avgen::analysis
