#include "analysis/channel_analysis.hpp"

#include <algorithm>

namespace avgen::analysis {

namespace {

// The four event kinds a StemFrame carries, by index.
bool& flag(StemFrame& f, std::size_t k) {
    switch (k) {
    case 0: return f.onset;
    case 1: return f.kick;
    case 2: return f.snare;
    default: return f.hat;
    }
}
float& strength(StemFrame& f, std::size_t k) {
    switch (k) {
    case 0: return f.onsetStrength;
    case 1: return f.kickStrength;
    case 2: return f.snareStrength;
    default: return f.hatStrength;
    }
}

} // namespace

void LiveStemLatch::apply(LiveStemFrame& f) {
    if (f.serial < serial) {
        *this = LiveStemLatch{}; // a new analysis (the input was reopened)
    }
    serial = f.serial;
    for (std::size_t k = 0; k < 4; ++k) {
        bool& on = flag(f.frame, k);
        if (!on || f.stamps[k] == 0) {
            continue;
        }
        if (f.stamps[k] <= fired[k]) {
            on = false; // fired on an earlier render frame
        } else {
            fired[k] = f.stamps[k];
        }
    }
}

ChannelAnalysis::ChannelAnalysis(std::vector<Source> sources, std::uint32_t sampleRate, AnalyzerConfig base,
                                 std::unique_ptr<ChannelAnalysis> previous, BeatTrackerConfig beatConfig)
    : beatConfig_(beatConfig) {
    base.sampleRate = sampleRate;
    hopSize_ = base.hopSize;
    const auto hopSeconds = static_cast<float>(static_cast<double>(base.hopSize) / static_cast<double>(sampleRate));
    // The roles that can change without a reset (no thread runs while this does).
    const auto roles = [&](Slot& slot, const Source& s) {
        slot.source.gainDb = s.gainDb;
        slot.source.beat = s.beat;
        slot.source.feed = s.feed && s.hits;
        slot.gainDb.store(s.gainDb, std::memory_order_relaxed);
        if (s.beat && !slot.tracker) {
            slot.tracker = std::make_unique<BeatTracker>(beatConfig_, hopSeconds);
        } else if (!s.beat) {
            slot.tracker.reset();
        }
        TimedFeed stale;
        while (slot.feeds.pop(stale)) {
        }
    };
    if (previous) {
        previous->stop();
    }
    for (const Source& s : sources) {
        if (previous && s.stream != nullptr) {
            const auto kept = std::find_if(previous->slots_.begin(), previous->slots_.end(), [&](const auto& slot) {
                return slot && slot->source.stream == s.stream && slot->source.pair == s.pair &&
                       slot->source.hits == s.hits;
            });
            if (kept != previous->slots_.end()) {
                roles(**kept, s);
                slots_.push_back(std::move(*kept));
                continue;
            }
        }
        auto slot = std::make_unique<Slot>();
        slot->source = s;
        StemAnalysisConfig config;
        config.analyzer = base;
        config.hits = s.hits;
        config.gainDb = s.gainDb;
        slot->analyzer = std::make_unique<StemAnalyzer>(config);
        slot->appliedGainDb = s.gainDb;
        roles(*slot, s);
        const std::size_t per = s.pair ? 2 : 1;
        slot->read.assign(hopSize_ * per, 0.0f);
        slot->mono.assign(hopSize_, 0.0f);
        slot->side.assign(hopSize_, 0.0f);
        slots_.push_back(std::move(slot));
    }
}

ChannelAnalysis::~ChannelAnalysis() {
    stop();
}

void ChannelAnalysis::start() {
    if (!thread_.joinable()) {
        thread_ = std::jthread([this](std::stop_token token) { threadMain(token); });
    }
}

void ChannelAnalysis::stop() {
    if (thread_.joinable()) {
        thread_.request_stop();
        thread_.join();
    }
}

void ChannelAnalysis::setGainDb(std::size_t index, float gainDb) {
    if (index < slots_.size()) {
        slots_[index]->gainDb.store(gainDb, std::memory_order_relaxed);
    }
}

bool ChannelAnalysis::acquire(std::size_t index) {
    Slot& slot = *slots_[index];
    if (!slot.frames.acquire()) {
        return false;
    }
    slot.consumed.store(slot.frames.front().serial, std::memory_order_release);
    return true;
}

// One read of one source (up to a hop), analysed; every completed frame published. Returns whether it read anything.
bool ChannelAnalysis::step(Slot& slot) {
    if (slot.source.stream == nullptr) {
        return false;
    }
    const auto result = slot.source.stream->read(slot.read);
    if (result.count == 0 && !result.discontinuity) {
        return false;
    }
    if (result.discontinuity) {
        slot.analyzer->reset(result.startFrame); // a dropped block or a gap: never splice across it
        slot.pending = {};
        if (slot.tracker) {
            slot.tracker->reset(); // as the mix's runner does
        }
    }
    if (const float g = slot.gainDb.load(std::memory_order_relaxed); g != slot.appliedGainDb) {
        slot.analyzer->setGainDb(g); // ADR-1582: the trim moves; the analysis carries on
        slot.appliedGainDb = g;
    }
    if (slot.source.pair) {
        const std::size_t frames = result.count / 2;
        for (std::size_t i = 0; i < frames; ++i) {
            slot.mono[i] = slot.read[2 * i];
            slot.side[i] = slot.read[2 * i + 1];
        }
        slot.analyzer->push(std::span<const float>(slot.mono.data(), frames),
                            std::span<const float>(slot.side.data(), frames));
    } else {
        slot.analyzer->push(std::span<const float>(slot.read.data(), result.count));
    }
    LiveStemFrame out;
    while (slot.analyzer->pop(out.frame)) {
        out.serial = ++slot.serial;
        out.tracked = false;
        if (slot.tracker) {
            out.beat = slot.tracker->push(out.frame.beatOnsetStrength, out.frame.beatOnset); // ADR-1581
            out.tracked = true;
        }
        if (slot.source.feed && !slot.feeds.push({out.frame.timeSeconds, out.frame.feed})) {
            feedsDropped_.fetch_add(1, std::memory_order_relaxed); // ADR-1580: the render thread fell 5 s behind
        }
        const std::uint64_t consumed = slot.consumed.load(std::memory_order_acquire);
        for (std::size_t k = 0; k < 4; ++k) {
            bool& on = flag(out.frame, k);
            float& s = strength(out.frame, k);
            if (on) {
                slot.pending[k] = true;
                slot.pendingStrength[k] = s;
                slot.pendingStamp[k] = out.serial;
                out.stamps[k] = out.serial;
            } else if (slot.pending[k] && slot.pendingStamp[k] > consumed) {
                on = true; // not yet seen by the render thread: it rides on this frame too
                s = std::max(s, slot.pendingStrength[k]);
                out.stamps[k] = slot.pendingStamp[k];
            } else {
                slot.pending[k] = false;
                out.stamps[k] = 0;
            }
        }
        slot.frames.back() = out;
        slot.frames.publish();
        hops_.fetch_add(1, std::memory_order_relaxed);
    }
    return true;
}

LiveFeedMixer::LiveFeedMixer(ChannelAnalysis& channels, std::vector<std::size_t> slots, FeedRouting routing)
    : channels_(channels)
    , slots_(std::move(slots))
    , routing_(routing)
    , folders_(slots_.size())
    , pending_(slots_.size())
    , now_(slots_.size()) {
    for (auto& p : pending_) {
        p.reserve(2 * ChannelAnalysis::kFeedCapacity); // no allocation while it runs
    }
}

bool LiveFeedMixer::feedAt(double seconds, const CausalOnsets& mix, ResponseFeed& out) {
    if (!routing_.any()) {
        return false;
    }
    for (std::size_t s = 0; s < slots_.size(); ++s) {
        std::vector<TimedFeed>& pending = pending_[s];
        TimedFeed hop;
        while (pending.size() < pending.capacity() && slots_[s] < channels_.size() && channels_.popFeed(slots_[s], hop)) {
            pending.push_back(hop);
        }
        auto end = pending.begin();
        while (end != pending.end() && end->seconds <= seconds) {
            ++end;
        }
        now_[s] = folders_[s].fold(std::span<const TimedFeed>(pending.data(), static_cast<std::size_t>(end - pending.begin())),
                                   seconds);
        pending.erase(pending.begin(), end);
    }
    out = mergeFeeds(ResponseFeed::fromCausal(mix), now_, routing_);
    return true;
}

void ChannelAnalysis::threadMain(std::stop_token token) {
    using clock = std::chrono::steady_clock;
    while (!token.stop_requested()) {
        const auto started = clock::now();
        const std::uint64_t before = hops_.load(std::memory_order_relaxed);
        bool any = false;
        for (const auto& slot : slots_) {
            any |= step(*slot);
        }
        if (!any) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        const double us = std::chrono::duration<double, std::micro>(clock::now() - started).count();
        busy_.store(busy_.load(std::memory_order_relaxed) + us, std::memory_order_relaxed);
        const std::uint64_t made = hops_.load(std::memory_order_relaxed) - before;
        if (made > 0) {
            const double perHop = us / static_cast<double>(made);
            const double prev = hopMicros_.load(std::memory_order_relaxed);
            hopMicros_.store(prev == 0.0 ? perHop : prev + (perHop - prev) * 0.05, std::memory_order_relaxed);
        }
        if (us > passMax_.load(std::memory_order_relaxed)) {
            passMax_.store(us, std::memory_order_relaxed);
        }
    }
}

} // namespace avgen::analysis
