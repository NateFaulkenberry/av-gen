#include "world/effects/effect_trigger.hpp"

#include "analysis/analysis_track.hpp"
// The musical-event walk. Header-only, and deliberately the SAME object the engine publishes
// `music.*` from and the Auto-director folds structure with (`structureOfTrack`): a drop a trigger
// fires on is a drop the rest of the engine agrees is one.
#include "app/music_runtime.hpp"
#include "seq/sequence.hpp"
#include "signals/musical_events.hpp"
#include "world/effects/distortion_frame.hpp"
#include "world/effects/history_bank.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace avgen::world {
namespace {

using json = nlohmann::json;

// Writes `t` into `out[n]` when there is room; returns the new count.
std::size_t put(std::span<double> out, std::size_t n, double t) {
    if (n < out.size()) {
        out[n] = t;
        return n + 1;
    }
    return n;
}

// The index one past the last element <= t in an ascending list of times.
template <class T, class Time>
std::size_t upperIndex(std::span<const T> list, double t, Time time) {
    const auto it = std::upper_bound(list.begin(), list.end(), t,
                                     [&](double v, const T& e) { return v < time(e); });
    return static_cast<std::size_t>(it - list.begin());
}

// How far back a Proximity trigger (or a type reading its owner's past) must be able to look.
float lookbackSeconds(const EffectInstance& e) {
    float seconds = 2.0f;
    if (e.timing.lifetime > 0.0) {
        seconds = std::max(seconds, static_cast<float>(e.timing.lifetime + e.timing.delay));
    }
    if (const DistortionProducer* p = distortionProducer(e.kind); p != nullptr && p->historySeconds != nullptr) {
        seconds = std::max(seconds, p->historySeconds(e));
    }
    return std::clamp(seconds, HistoryBank::kMinSeconds, HistoryBank::kMaxSeconds);
}

double readNumber(const json& j, const char* key, double fallback, bool& bad) {
    if (!j.contains(key)) {
        return fallback;
    }
    if (!j.at(key).is_number()) {
        bad = true;
        return fallback;
    }
    return j.at(key).get<double>();
}

} // namespace

// ---- names and validation (declared in effect_timing.hpp) ----------------------------------------

const char* triggerSourceName(TriggerSource s) {
    switch (s) {
    case TriggerSource::Beat: return "beat";
    case TriggerSource::Onset: return "onset";
    case TriggerSource::MusicEvent: return "musicEvent";
    case TriggerSource::TimelineMarker: return "marker";
    case TriggerSource::Repeat: return "repeat";
    case TriggerSource::Proximity: return "proximity";
    }
    return "beat";
}

std::optional<TriggerSource> triggerSourceFromName(std::string_view name) {
    // One spelling each (ADR-441): a file that says "beats" or "timelineMarker" is refused by name
    // rather than quietly read as something.
    for (const TriggerSource s : {TriggerSource::Beat, TriggerSource::Onset, TriggerSource::MusicEvent,
                                  TriggerSource::TimelineMarker, TriggerSource::Repeat, TriggerSource::Proximity}) {
        if (name == triggerSourceName(s)) {
            return s;
        }
    }
    return std::nullopt;
}

Result<void> Trigger::validate() const {
    switch (source) {
    case TriggerSource::Beat:
        if (everyN < 1) { return fail("a beat trigger fires on every Nth beat; N must be at least 1 (is {})", everyN); }
        if (offset < 0) { return fail("a beat trigger's first beat must not be negative (is {})", offset); }
        break;
    case TriggerSource::Onset:
        if (!(threshold >= 0.0f) || !std::isfinite(threshold)) {
            return fail("an onset trigger's threshold must be a non-negative number");
        }
        break;
    case TriggerSource::MusicEvent:
        if (!signals::musicalEventFromName(name)) {
            return fail("a music-event trigger names '{}', which is not a musical event (beat, downbeat, "
                        "bar, phrase, section, energyRise, energyDrop, build, break, drop, impact)",
                        name);
        }
        break;
    case TriggerSource::TimelineMarker:
        if (name.empty()) { return fail("a marker trigger needs the name of the marker it fires on"); }
        break;
    case TriggerSource::Repeat:
        if (!(period > 0.0) || !std::isfinite(period)) { return fail("a repeat trigger's period must be positive"); }
        if (!std::isfinite(phase)) { return fail("a repeat trigger's phase must be a number"); }
        break;
    case TriggerSource::Proximity:
        if (entity.empty()) { return fail("a proximity trigger needs the entity it measures to"); }
        if (!(radius > 0.0f) || !std::isfinite(radius)) { return fail("a proximity trigger's radius must be positive"); }
        break;
    }
    return {};
}

// ---- the clock -------------------------------------------------------------------------------------

void TriggerClock::setBeats(std::span<const double> ascending) { beats_.assign(ascending.begin(), ascending.end()); }
void TriggerClock::setOnsets(std::span<const TriggerOnset> ascending) { onsets_.assign(ascending.begin(), ascending.end()); }
void TriggerClock::setMusicEvents(std::span<const TriggerMoment> ascending) {
    moments_.assign(ascending.begin(), ascending.end());
}
void TriggerClock::setMarkers(std::span<const TriggerMarker> markers) {
    markers_.assign(markers.begin(), markers.end());
    std::stable_sort(markers_.begin(), markers_.end(),
                     [](const TriggerMarker& a, const TriggerMarker& b) { return a.t < b.t; });
}

void TriggerClock::setFrame(double seconds) {
    if (!haveFrame_) {
        haveFrame_ = true;
        seconds_ = seconds;
        edgeStart_ = seconds;
        return;
    }
    if (seconds == seconds_) {
        return; // the same frame asked again (both evaluator phases), or a paused redraw
    }
    const double step = seconds - seconds_;
    edgeStart_ = step > 0.0 && step <= kMaxEdgeSeconds ? seconds_ : seconds;
    seconds_ = seconds;
}

void TriggerClock::bind(const analysis::AnalysisTrack* track, std::span<const seq::Marker> markers,
                        const HistoryBank* history, double seconds, int phraseBars, int sectionPhrases) {
    history_ = history;
    setFrame(seconds);

    const std::size_t frames = track != nullptr ? track->frames().size() : 0;
    const std::size_t beatCount = track != nullptr ? track->beats().beatTimes.size() : 0;
    if (track != boundTrack_ || frames != boundFrames_ || beatCount != boundBeats_ ||
        phraseBars != boundPhraseBars_ || sectionPhrases != boundSectionPhrases_) {
        boundTrack_ = track;
        boundFrames_ = frames;
        boundBeats_ = beatCount;
        boundPhraseBars_ = phraseBars;
        boundSectionPhrases_ = sectionPhrases;
        beats_.clear();
        onsets_.clear();
        moments_.clear();
        if (track != nullptr) {
            beats_ = track->beats().beatTimes;
            std::sort(beats_.begin(), beats_.end());
            // The walk the Auto-director's structure fold does: a detector of its own, over every
            // frame from the first, so the moments are a property of the track and not of where a
            // playback started.
            app::MusicRuntime runtime;
            for (const analysis::AnalysisFrame& frame : track->frames()) {
                if (frame.onset) {
                    onsets_.push_back(TriggerOnset{frame.timeSeconds, frame.onsetStrength});
                }
                runtime.consume(frame, phraseBars, sectionPhrases);
                for (const signals::MusicalMoment& m : runtime.lastMoments()) {
                    moments_.push_back(TriggerMoment{m.timeSeconds, static_cast<std::uint8_t>(m.event)});
                }
            }
            std::stable_sort(moments_.begin(), moments_.end(),
                             [](const TriggerMoment& a, const TriggerMoment& b) { return a.t < b.t; });
        }
    }

    // Markers: compared in place and copied only when they differ, so an unchanged sequence costs a
    // walk and no allocation.
    bool same = markers.size() == markers_.size();
    if (same) {
        // `markers_` is sorted; the sequence's list need not be. Compare as multisets by a sorted walk
        // only when the cheap check fails.
        for (std::size_t i = 0; i < markers.size() && same; ++i) {
            same = markers[i].timeSeconds == markers_[i].t && markers[i].name == markers_[i].name;
        }
    }
    if (!same) {
        markers_.clear();
        markers_.reserve(markers.size());
        for (const seq::Marker& m : markers) {
            markers_.push_back(TriggerMarker{m.timeSeconds, m.name});
        }
        const bool sorted = std::is_sorted(markers_.begin(), markers_.end(),
                                           [](const TriggerMarker& a, const TriggerMarker& b) { return a.t < b.t; });
        if (!sorted) {
            // A sequence whose markers are out of time order compares unequal every frame and is
            // re-copied; correct, and no sequence the editor writes is.
            std::stable_sort(markers_.begin(), markers_.end(),
                             [](const TriggerMarker& a, const TriggerMarker& b) { return a.t < b.t; });
        }
    }
}

std::size_t TriggerClock::lastTriggers(const Trigger& trig, std::string_view owner, double t,
                                       std::span<double> out) const {
    if (out.empty() || !std::isfinite(t)) {
        return 0;
    }
    std::size_t n = 0;
    switch (trig.source) {
    case TriggerSource::Beat: {
        const std::size_t every = static_cast<std::size_t>(std::max(trig.everyN, 1));
        const std::size_t first = static_cast<std::size_t>(std::max(trig.offset, 0));
        std::size_t end = upperIndex<double>(beats_, t, [](double v) { return v; });
        if (end == 0 || end - 1 < first) {
            return 0;
        }
        // The newest qualifying index at or below end - 1, then every `every` below it.
        std::size_t i = end - 1;
        i -= (i - first) % every;
        while (n < out.size()) {
            n = put(out, n, beats_[i]);
            if (i < first + every) {
                break;
            }
            i -= every;
        }
        return n;
    }
    case TriggerSource::Onset: {
        std::size_t i = upperIndex<TriggerOnset>(onsets_, t, [](const TriggerOnset& o) { return o.t; });
        while (i > 0 && n < out.size()) {
            --i;
            if (onsets_[i].strength >= trig.threshold) {
                n = put(out, n, onsets_[i].t);
            }
        }
        return n;
    }
    case TriggerSource::MusicEvent: {
        const auto event = signals::musicalEventFromName(trig.name);
        if (!event) {
            return 0;
        }
        const auto want = static_cast<std::uint8_t>(*event);
        std::size_t i = upperIndex<TriggerMoment>(moments_, t, [](const TriggerMoment& m) { return m.t; });
        while (i > 0 && n < out.size()) {
            --i;
            if (moments_[i].event == want) {
                n = put(out, n, moments_[i].t);
            }
        }
        return n;
    }
    case TriggerSource::TimelineMarker: {
        std::size_t i = upperIndex<TriggerMarker>(markers_, t, [](const TriggerMarker& m) { return m.t; });
        while (i > 0 && n < out.size()) {
            --i;
            if (markers_[i].name == trig.name) {
                n = put(out, n, markers_[i].t);
            }
        }
        return n;
    }
    case TriggerSource::Repeat: {
        if (!(trig.period > 0.0) || t < trig.phase) {
            return 0;
        }
        // The index is floored from the second itself, so the answer at t depends on nothing else.
        double k = std::floor((t - trig.phase) / trig.period);
        while (k >= 0.0 && n < out.size()) {
            n = put(out, n, trig.phase + k * trig.period);
            k -= 1.0;
        }
        return n;
    }
    case TriggerSource::Proximity: return proximity(trig, owner, t, out);
    }
    return 0;
}

// The owner's ring, walked newest to oldest over the samples at or before `t`; the other entity is
// read at each of those instants (interpolated, so two rings recorded at the same instants compare
// sample for sample). An entry edge is a step whose older end is outside the radius and whose newer
// end is inside; its time is the linear crossing between the two. Everything read is in the
// HistoryBank, which a play records and a seek restores and replays -- so this is exact under seek.
std::size_t TriggerClock::proximity(const Trigger& trig, std::string_view owner, double t,
                                    std::span<double> out) const {
    if (history_ == nullptr || owner.empty() || trig.entity.empty()) {
        return 0;
    }
    const std::size_t ring = history_->find(owner);
    const std::size_t other = history_->find(trig.entity);
    if (ring >= history_->ringCount() || other >= history_->ringCount()) {
        return 0;
    }
    const std::size_t count = history_->sampleCount(ring);
    std::size_t n = 0;
    double newerT = 0.0;
    float newerD = 0.0f;
    bool haveNewer = false;
    for (std::size_t k = count; k > 0 && n < out.size(); --k) {
        const HistorySample& s = history_->sample(ring, k - 1);
        if (s.t > t) {
            continue;
        }
        HistorySample o;
        if (!history_->sampleAt(other, s.t, o)) {
            haveNewer = false; // the other ring does not reach back this far: no edge across the gap
            continue;
        }
        const float d = glm::length(s.position - o.position);
        if (haveNewer && d > trig.radius && newerD <= trig.radius) {
            const double f = static_cast<double>((d - trig.radius) / std::max(d - newerD, 1e-6f));
            n = put(out, n, s.t + std::clamp(f, 0.0, 1.0) * (newerT - s.t));
        }
        newerT = s.t;
        newerD = d;
        haveNewer = true;
    }
    return n;
}

const char* TriggerClock::silence(const Trigger& trig, std::string_view owner) const {
    switch (trig.source) {
    case TriggerSource::Beat:
        return beats_.empty() ? "No beats: the trigger fires on the analysed track's beats, and no audio is "
                                "analysed (or the beat tracker found none)."
                              : nullptr;
    case TriggerSource::Onset:
        if (onsets_.empty()) {
            return "No onsets: the trigger fires on the analysed track's onsets, and no audio is analysed.";
        }
        for (const TriggerOnset& o : onsets_) {
            if (o.strength >= trig.threshold) {
                return nullptr;
            }
        }
        return "No onset in the track reaches this trigger's threshold.";
    case TriggerSource::MusicEvent: {
        const auto event = signals::musicalEventFromName(trig.name);
        if (!event) {
            return "The trigger names a musical event that does not exist.";
        }
        for (const TriggerMoment& m : moments_) {
            if (m.event == static_cast<std::uint8_t>(*event)) {
                return nullptr;
            }
        }
        return moments_.empty() && boundFrames_ == 0
                   ? "No musical events: no audio is analysed."
                   : "The analysed track has no musical event of this kind.";
    }
    case TriggerSource::TimelineMarker:
        for (const TriggerMarker& m : markers_) {
            if (m.name == trig.name) {
                return nullptr;
            }
        }
        return "The sequence has no marker with this trigger's name.";
    case TriggerSource::Repeat: return nullptr;
    case TriggerSource::Proximity:
        if (history_ == nullptr || history_->find(owner) >= history_->ringCount() ||
            history_->find(trig.entity) >= history_->ringCount()) {
            return "The owner or the other entity has no recorded motion (is the entity in this scene?).";
        }
        return nullptr;
    }
    return nullptr;
}

// ---- the activation window (declared in effect_timing.hpp) ---------------------------------------

std::optional<ActivationWindow> resolveActivationWindow(Activation activation, const Timing& timing,
                                                        const EffectContext& ctx, bool followsFocus,
                                                        std::string_view subject, std::string_view owner) {
    if (activation != Activation::Trigger) {
        return resolveActivationWindow(activation, timing, ctx.seconds, ctx.shots, followsFocus, subject);
    }
    if (ctx.triggers == nullptr) {
        return std::nullopt;
    }
    std::array<double, 1> last{};
    if (ctx.triggers->lastTriggers(timing.trigger, owner, ctx.seconds, last) == 0) {
        return std::nullopt;
    }
    const double end = timing.lifetime > 0.0 ? last[0] + timing.delay + timing.lifetime
                                             : std::numeric_limits<double>::infinity();
    if (ctx.seconds >= end) {
        return std::nullopt;
    }
    return ActivationWindow{last[0], end, nullptr};
}

// ---- consumers' helpers ----------------------------------------------------------------------------

std::size_t effectEventTimes(const EffectInstance& e, const EffectContext& ctx, std::span<double> out) {
    if (out.empty()) {
        return 0;
    }
    const std::string_view owner = e.owner.kind == EffectTarget::Entity ? std::string_view(e.owner.name)
                                                                          : std::string_view();
    if (e.activation == Activation::Trigger) {
        if (ctx.triggers == nullptr) {
            return 0;
        }
        const std::size_t n = ctx.triggers->lastTriggers(e.timing.trigger, owner, ctx.seconds, out);
        std::size_t kept = 0;
        for (std::size_t i = 0; i < n; ++i) {
            const double start = out[i] + e.timing.delay;
            if (start > ctx.seconds) {
                continue; // released, but its delay has not run out
            }
            if (e.timing.lifetime > 0.0 && ctx.seconds >= start + e.timing.lifetime) {
                break; // older ones are older still
            }
            out[kept++] = start;
        }
        return kept;
    }
    const bool world = e.owner.kind == EffectTarget::World;
    const auto window = resolveActivationWindow(e.activation, e.timing, ctx, world || e.owner.kind != EffectTarget::Entity,
                                                world ? std::string_view() : std::string_view(e.owner.name), owner);
    if (!window) {
        return 0;
    }
    const double first = window->start + e.timing.delay;
    if (ctx.seconds < first) {
        return 0;
    }
    if (!(e.timing.repeatSeconds > 0.0)) {
        out[0] = first;
        return 1;
    }
    double k = std::floor((ctx.seconds - first) / e.timing.repeatSeconds);
    std::size_t n = 0;
    while (k >= 0.0 && n < out.size()) {
        n = put(out, n, first + k * e.timing.repeatSeconds);
        k -= 1.0;
    }
    return n;
}

bool effectTriggerEdge(const EffectInstance& e, const EffectContext& ctx) {
    if (e.activation != Activation::Trigger || ctx.triggers == nullptr) {
        return false;
    }
    const double from = ctx.triggers->edgeStart();
    if (!(ctx.seconds > from)) {
        return false;
    }
    std::array<double, 1> last{};
    const std::string_view owner = e.owner.kind == EffectTarget::Entity ? std::string_view(e.owner.name)
                                                                          : std::string_view();
    if (ctx.triggers->lastTriggers(e.timing.trigger, owner, ctx.seconds, last) == 0) {
        return false;
    }
    return last[0] > from;
}

const char* effectTriggerDormancy(const EffectInstance& e, const EffectContext& ctx) {
    if (e.activation != Activation::Trigger) {
        return nullptr;
    }
    if (ctx.triggers == nullptr) {
        return "Waits for a trigger, and nothing supplies events here.";
    }
    const std::string_view owner = e.owner.kind == EffectTarget::Entity ? std::string_view(e.owner.name)
                                                                          : std::string_view();
    if (const char* why = ctx.triggers->silence(e.timing.trigger, owner)) {
        return why;
    }
    std::array<double, 1> last{};
    if (ctx.triggers->lastTriggers(e.timing.trigger, owner, ctx.seconds, last) == 0) {
        return "Waiting for its first trigger.";
    }
    return "Between triggers: the last one's lifetime has run out.";
}

void appendEffectHistoryNeeds(std::span<const EffectInstance> effects, std::vector<HistorySubscription>& wanted) {
    for (const EffectInstance& e : effects) {
        const bool entityOwned = e.owner.kind == EffectTarget::Entity && !e.owner.name.empty();
        if (e.activation == Activation::Trigger && e.timing.trigger.source == TriggerSource::Proximity &&
            entityOwned && !e.timing.trigger.entity.empty()) {
            const float seconds = lookbackSeconds(e);
            wanted.push_back(HistorySubscription{e.owner.name, seconds});
            wanted.push_back(HistorySubscription{e.timing.trigger.entity, seconds});
        }
        if (entityOwned) {
            if (const DistortionProducer* p = distortionProducer(e.kind); p != nullptr && p->historySeconds != nullptr) {
                wanted.push_back(HistorySubscription{e.owner.name, lookbackSeconds(e)});
            }
        }
    }
}

// ---- JSON ------------------------------------------------------------------------------------------

json triggerToJson(const Trigger& t) {
    return json{{"source", triggerSourceName(t.source)},
                {"everyN", t.everyN},
                {"offset", t.offset},
                {"threshold", t.threshold},
                {"name", t.name},
                {"period", t.period},
                {"phase", t.phase},
                {"entity", t.entity},
                {"radius", t.radius}};
}

Result<Trigger> triggerFromJson(const json& j) {
    if (!j.is_object()) {
        return fail("a trigger must be an object ({{\"source\": \"beat\", ...}})");
    }
    Trigger t;
    if (!j.contains("source") || !j.at("source").is_string()) {
        return fail("a trigger needs a 'source' (beat, onset, musicEvent, marker, repeat, proximity)");
    }
    const std::string source = j.at("source").get<std::string>();
    const auto kind = triggerSourceFromName(source);
    if (!kind) {
        return fail("unknown trigger source '{}' (one of beat, onset, musicEvent, marker, repeat, proximity)",
                    source);
    }
    t.source = *kind;
    bool bad = false;
    t.everyN = static_cast<int>(readNumber(j, "everyN", t.everyN, bad));
    t.offset = static_cast<int>(readNumber(j, "offset", t.offset, bad));
    t.threshold = static_cast<float>(readNumber(j, "threshold", static_cast<double>(t.threshold), bad));
    t.period = readNumber(j, "period", t.period, bad);
    t.phase = readNumber(j, "phase", t.phase, bad);
    t.radius = static_cast<float>(readNumber(j, "radius", static_cast<double>(t.radius), bad));
    for (const char* key : {"name", "entity"}) {
        if (j.contains(key) && !j.at(key).is_string()) {
            bad = true;
        }
    }
    if (bad) {
        return fail("a trigger field has the wrong type (numbers for everyN/offset/threshold/period/phase/"
                    "radius, text for name/entity)");
    }
    if (j.contains("name")) { t.name = j.at("name").get<std::string>(); }
    if (j.contains("entity")) { t.entity = j.at("entity").get<std::string>(); }
    if (auto ok = t.validate(); !ok) {
        return fail("trigger: {}", ok.error().message);
    }
    return t;
}

} // namespace avgen::world
