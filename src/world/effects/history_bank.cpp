#include "world/effects/history_bank.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace avgen::world {

namespace {

std::uint64_t mixHash(std::uint64_t h, std::uint64_t v) {
    h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
    return h;
}

std::uint64_t floatBits(float f) {
    std::uint32_t u = 0;
    std::memcpy(&u, &f, sizeof u);
    return u;
}

HistorySample lerpSample(const HistorySample& a, const HistorySample& b, double t) {
    const double span = b.t - a.t;
    const float f = span > 0.0 ? static_cast<float>(std::clamp((t - a.t) / span, 0.0, 1.0)) : 1.0f;
    HistorySample out;
    out.t = t;
    out.position = glm::mix(a.position, b.position, f);
    out.rotation = glm::slerp(a.rotation, b.rotation, f);
    out.scale = glm::mix(a.scale, b.scale, f);
    return out;
}

} // namespace

std::size_t HistoryBank::capacityFor(float seconds) {
    // Every sample inside the depth at the fastest rate a play is sized for, plus the one kept at
    // or before the cut and a little slack for a frame that lands a hair late.
    return static_cast<std::size_t>(std::ceil(static_cast<double>(seconds) * kMaxRateHz)) + 4u;
}

bool HistoryBank::subscribe(std::span<const HistorySubscription> subscriptions) {
    // Merged and sorted by name, so the set -- and the key a checkpoint is taken under -- does not
    // depend on the order the effects happen to be listed in.
    std::vector<HistorySubscription> wanted;
    wanted.reserve(subscriptions.size());
    for (const HistorySubscription& s : subscriptions) {
        if (s.node.empty()) {
            continue;
        }
        const float seconds = std::clamp(std::isfinite(s.seconds) ? s.seconds : kMinSeconds, kMinSeconds, kMaxSeconds);
        auto it = std::find_if(wanted.begin(), wanted.end(),
                               [&](const HistorySubscription& w) { return w.node == s.node; });
        if (it == wanted.end()) {
            wanted.push_back(HistorySubscription{s.node, seconds});
        } else {
            it->seconds = std::max(it->seconds, seconds);
        }
    }
    std::sort(wanted.begin(), wanted.end(),
              [](const HistorySubscription& a, const HistorySubscription& b) { return a.node < b.node; });

    bool same = wanted.size() == rings_.size();
    for (std::size_t i = 0; same && i < wanted.size(); ++i) {
        same = wanted[i].node == rings_[i].node && wanted[i].seconds == rings_[i].seconds;
    }
    if (same) {
        return false;
    }

    std::vector<Ring> next;
    next.reserve(wanted.size());
    for (const HistorySubscription& w : wanted) {
        Ring r;
        r.node = w.node;
        r.seconds = w.seconds;
        r.buffer.resize(capacityFor(w.seconds));
        const std::size_t old = find(w.node);
        if (old < rings_.size()) {
            const Ring& from = rings_[old];
            // The newest samples that fit, oldest first, then re-trimmed to the new depth.
            const std::size_t keep = std::min(from.count, r.buffer.size());
            for (std::size_t i = from.count - keep; i < from.count; ++i) {
                r.buffer[r.count++] = at(from, i);
            }
            trim(r);
        }
        next.push_back(std::move(r));
    }
    rings_ = std::move(next);
    return true;
}

std::size_t HistoryBank::find(std::string_view node) const {
    for (std::size_t i = 0; i < rings_.size(); ++i) {
        if (rings_[i].node == node) {
            return i;
        }
    }
    return rings_.size();
}

void HistoryBank::trim(Ring& r) {
    if (r.count < 2) {
        return;
    }
    // Keep exactly one sample at or before the cut, so an interpolated read at `latest - depth`
    // is always bracketed -- and so a played ring and a replayed one hold the same set.
    const double cut = at(r, r.count - 1).t - static_cast<double>(r.seconds);
    while (r.count >= 2 && at(r, 1).t <= cut) {
        r.head = (r.head + 1) % r.buffer.size();
        --r.count;
    }
}

void HistoryBank::record(std::size_t ring, double t, const glm::vec3& position, const glm::quat& rotation,
                         const glm::vec3& scale) {
    if (ring >= rings_.size()) {
        return;
    }
    Ring& r = rings_[ring];
    if (r.buffer.empty()) {
        return;
    }
    HistorySample s;
    s.t = t;
    s.position = position;
    s.rotation = rotation;
    s.scale = scale;
    if (r.count > 0) {
        const double newest = at(r, r.count - 1).t;
        if (t < newest - 1e-9) {
            // Time went backwards: the transport looped or somebody seeked without the seek path.
            // The old samples describe a future this instant has not reached, so they go.
            r.head = 0;
            r.count = 0;
        } else if (std::abs(t - newest) <= 1e-9) {
            r.buffer[(r.head + r.count - 1) % r.buffer.size()] = s;
            return;
        }
    }
    if (r.count == r.buffer.size()) {
        // Faster than the ring was sized for: the oldest goes, and the tail is shorter.
        r.head = (r.head + 1) % r.buffer.size();
        --r.count;
    }
    r.buffer[(r.head + r.count) % r.buffer.size()] = s;
    ++r.count;
    trim(r);
}

void HistoryBank::clear() {
    for (Ring& r : rings_) {
        r.head = 0;
        r.count = 0;
    }
}

const HistorySample& HistoryBank::sample(std::size_t ring, std::size_t i) const { return at(rings_[ring], i); }

bool HistoryBank::latest(std::string_view node, HistorySample& out) const {
    const std::size_t ring = find(node);
    if (ring >= rings_.size() || rings_[ring].count == 0) {
        return false;
    }
    out = at(rings_[ring], rings_[ring].count - 1);
    return true;
}

bool HistoryBank::sampleAt(std::string_view node, double t, HistorySample& out) const {
    return sampleAt(find(node), t, out);
}

bool HistoryBank::sampleAt(std::size_t ring, double t, HistorySample& out) const {
    if (ring >= rings_.size()) {
        return false;
    }
    const Ring& r = rings_[ring];
    if (r.count == 0) {
        return false;
    }
    const HistorySample& oldest = at(r, 0);
    const HistorySample& newest = at(r, r.count - 1);
    if (t > newest.t + 1e-9 || t < oldest.t - 1e-9) {
        return false;
    }
    if (t >= newest.t) {
        out = newest;
        out.t = t;
        return true;
    }
    // The first sample at or after `t`: the ring is ascending in time.
    std::size_t lo = 0;
    std::size_t hi = r.count - 1;
    while (lo < hi) {
        const std::size_t mid = (lo + hi) / 2;
        if (at(r, mid).t < t) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    if (lo == 0) {
        out = oldest;
        out.t = t;
        return true;
    }
    out = lerpSample(at(r, lo - 1), at(r, lo), t);
    return true;
}

bool HistoryBank::velocity(std::string_view node, glm::vec3& out) const { return velocity(find(node), out); }

bool HistoryBank::velocity(std::size_t ring, glm::vec3& out) const {
    if (ring >= rings_.size() || rings_[ring].count == 0) {
        return false;
    }
    const Ring& r = rings_[ring];
    out = glm::vec3(0.0f);
    if (r.count < 2) {
        return true;
    }
    const HistorySample& newest = at(r, r.count - 1);
    const HistorySample& oldest = at(r, 0);
    const double back = newest.t - kGridStep;
    HistorySample before;
    double dt = kGridStep;
    if (back >= oldest.t - 1e-12 && sampleAt(ring, back, before)) {
        dt = kGridStep;
    } else {
        // Younger than one step (the first frame after the ring started): the whole span there is.
        before = oldest;
        dt = newest.t - oldest.t;
    }
    if (dt <= 0.0) {
        return true;
    }
    out = (newest.position - before.position) / static_cast<float>(dt);
    return true;
}

bool HistoryBank::acceleration(std::size_t ring, glm::vec3& out) const {
    out = glm::vec3(0.0f);
    if (ring >= rings_.size() || rings_[ring].count == 0) {
        return false;
    }
    const Ring& r = rings_[ring];
    if (r.count < 3) {
        return true;
    }
    const HistorySample& newest = at(r, r.count - 1);
    const HistorySample& oldest = at(r, 0);
    glm::vec3 now(0.0f);
    static_cast<void>(velocity(ring, now));
    // The earlier velocity, one baseline back, differenced over one step exactly as `velocity`
    // does -- clamped to what the ring holds, so a young ring reads a shorter baseline.
    const double tb = std::max(newest.t - kAccelBaseline, oldest.t + kGridStep);
    const double baseline = newest.t - tb;
    if (baseline <= 1e-9) {
        return true;
    }
    HistorySample b1;
    HistorySample b0;
    if (!sampleAt(ring, tb, b1) || !sampleAt(ring, tb - kGridStep, b0)) {
        return true;
    }
    const glm::vec3 then = (b1.position - b0.position) / static_cast<float>(kGridStep);
    out = (now - then) / static_cast<float>(baseline);
    return true;
}

HistoryBank::Snapshot HistoryBank::snapshot() const {
    Snapshot s;
    s.counts.reserve(rings_.size());
    std::size_t total = 0;
    for (const Ring& r : rings_) {
        total += r.count;
    }
    s.samples.reserve(total);
    for (const Ring& r : rings_) {
        s.counts.push_back(static_cast<std::uint32_t>(r.count));
        for (std::size_t i = 0; i < r.count; ++i) {
            s.samples.push_back(at(r, i));
        }
    }
    return s;
}

void HistoryBank::restore(const Snapshot& snapshot) {
    clear();
    if (snapshot.counts.size() != rings_.size()) {
        return;
    }
    std::size_t from = 0;
    for (std::size_t i = 0; i < rings_.size(); ++i) {
        Ring& r = rings_[i];
        const std::size_t n = snapshot.counts[i];
        if (from + n > snapshot.samples.size()) {
            clear();
            return;
        }
        const std::size_t keep = std::min(n, r.buffer.size());
        for (std::size_t k = from + (n - keep); k < from + n; ++k) {
            r.buffer[r.count++] = snapshot.samples[k];
        }
        from += n;
    }
}

std::uint64_t HistoryBank::key() const {
    std::uint64_t h = 0x48495354ull; // "HIST"
    for (const Ring& r : rings_) {
        for (const char c : r.node) {
            h = mixHash(h, static_cast<unsigned char>(c));
        }
        h = mixHash(h, floatBits(r.seconds));
    }
    h = mixHash(h, rings_.size());
    h = mixHash(h, automationKey_);
    return h;
}

} // namespace avgen::world
