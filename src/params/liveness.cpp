#include "params/liveness.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace avgen::params::liveness {

namespace {

// THE table. Ids are stable: an evaluator ingests them and a validator refuses by them.
constexpr std::array<RuleInfo, 23> kRules{{
    {"disabled", "route|track", "dead", "The route or track is switched off (or the whole timeline is)."},
    {"unknown-source", "route", "dead", "The source is not a signal on the bus, so the route never binds."},
    {"unknown-depth-source", "route", "dead", "The depth source is not a signal on the bus, so the route never binds."},
    {"unknown-target", "route|track", "dead", "The target is not a registered parameter, so nothing is written."},
    {"not-modulatable", "route", "dead", "The target parameter refuses modulation."},
    {"component-out-of-range", "route|track", "dead", "The component index is past the parameter's last component."},
    {"zero-amount", "route", "dead", "The route's amount, or its whole depth range, is zero."},
    {"flat-chain", "route", "dead",
     "The chain settles to one output for every value the source can take (a threshold above its range, a "
     "flat remap, a zero gain)."},
    {"silent-source", "route", "dead", "The source cannot move in this project (an audio signal with no audio)."},
    {"live-only-source", "route", "hazard",
     "The source moves only with live MIDI/OSC input, so it is constant in an offline render."},
    {"event-swallowed", "route", "dead|hazard",
     "A one-frame event at the piece's typical strength reaches the target at less than half its amount, sampled "
     "at the project's frame rate (dead below a tenth, or when a threshold above the typical strength gates it "
     "out). Under ADR-900's chain no attack or decay can cause it; a threshold can."},
    {"pulse-swallowed", "route", "dead|hazard",
     "A value-mode timeline pulse reaches the target at less than half its amount through the chain."},
    {"pulse-between-frames", "route", "dead|hazard",
     "A value-mode timeline source's pulses are shorter than a frame, so some fall between frames (dead when "
     "all do at the project's rate). Event mode fires on the crossing instead."},
    {"delay-over-ceiling", "route", "hazard", "delayMs is above the chain's 4000 ms ceiling and is clamped."},
    {"phase-rate", "route|track", "hazard",
     "The target is a rate whose phase is time x rate: moving it jumps the pattern by elapsed time x change. "
     "Key or route a phase instead."},
    {"overridden-by-replace", "route|track", "dead|hazard",
     "A later Replace route writes the same component every frame (tracks apply before routes), so this one "
     "never shows (a hazard when the Replace route's depth varies)."},
    {"program-has-no-emission", "route|track", "dead",
     "material/<program>/emissionIntensity on a program that writes no emission register: the intensity "
     "multiplies nothing."},
    {"emission-lives-in-layer", "route|track", "dead",
     "material/<program>/emissionIntensity where the program's emission comes only from its layers, whose "
     "intensities are not registered parameters."},
    {"program-owns-emission", "route|track", "dead",
     "A material's own emissive intensity, gain or colour on a surface whose program writes emission: the "
     "program's output replaces it (ADR-179)."},
    {"program-not-uploaded", "route|track", "dead",
     "A parameter of a material program past the GPU table's eight slots: the program is never uploaded."},
    {"program-unused", "route|track", "dead", "A parameter of a material program no surface in the scene uses."},
    {"node-emits-nothing", "route|track", "dead",
     "nodes/<n>/emissiveBoost on a node none of whose surfaces emits anything. The boost is a post-program "
     "multiplier on every kind of node, so it is live on any node that emits."},
    {"effect-never-fires", "route|track|effect", "dead",
     "The effect's activation cannot open in this project (hero focus or camera travel with no matching shot "
     "span, a trigger with no events, a window outside the piece, an owner the scene does not have), so it "
     "and every route to it do nothing."},
}};

std::string componentText(int component) {
    return component < 0 ? std::string("all components") : fmt::format("component {}", component);
}

Finding make(std::string_view rule, Verdict verdict, std::string reason) {
    return Finding{std::string(rule), verdict, std::move(reason)};
}

float mapPolarity(float x, Polarity polarity) {
    return polarity == Polarity::Bipolar ? x * 2.0f - 1.0f : x;
}

// The chain's output for an input held at `x` from a fresh state: the first sample initialises the
// smoothing and a delay's history holds its first sample backwards, so the first frame is already
// the steady response. (An envelope held by a constant input oscillates by a frame's fall after its
// hold; the first frame is its top.)
float settled(const ProcessorChain& chain, float x) {
    ProcessorChain::State state;
    return chain.process(x, false, 0.0, state);
}

// The largest deviation from `rest` an input held at `x` produces over a few frames: the full-scale
// response an event's reach is measured against.
float sustainedDeviation(const ProcessorChain& chain, float x, float rest, double dt) {
    ProcessorChain::State state;
    float deviation = std::fabs(chain.process(x, false, 0.0, state) - rest);
    for (int i = 0; i < 8; ++i) {
        deviation = std::max(deviation, std::fabs(chain.process(x, false, dt, state) - rest));
    }
    return deviation;
}

// Frames of a pulse [a, b) sampled at `fps` on the grid k / fps: whether any frame lands inside it.
bool pulseSampled(double a, double b, double fps) {
    const double first = std::ceil(a * fps - 1e-9);
    return first / fps < b - 1e-9;
}

} // namespace

const char* verdictName(Verdict v) {
    switch (v) {
    case Verdict::Live:
        return "live";
    case Verdict::Dead:
        return "dead";
    case Verdict::Hazard:
        return "hazard";
    }
    return "live";
}

Verdict verdictOf(std::span<const Finding> findings) {
    Verdict worst = Verdict::Live;
    for (const Finding& f : findings) {
        if (f.verdict == Verdict::Dead) {
            return Verdict::Dead;
        }
        if (f.verdict == Verdict::Hazard) {
            worst = Verdict::Hazard;
        }
    }
    return worst;
}

std::optional<Finding> Facts::deadTarget(std::string_view, int) const {
    return std::nullopt;
}

std::optional<Finding> Facts::phaseRate(std::string_view) const {
    return std::nullopt;
}

SignalFacts BusFacts::signal(std::string_view name) const {
    SignalFacts facts;
    const auto id = bus_.find(name);
    if (!id) {
        return facts;
    }
    const signals::SignalInfo& info = bus_.info(*id);
    facts.exists = true;
    facts.isEvent = info.isEvent;
    facts.minValue = info.minValue;
    facts.maxValue = info.maxValue;
    return facts;
}

const IParameter* BusFacts::parameter(std::string_view path) const {
    return params_.find(path);
}

std::optional<EventPassThrough> eventPassThrough(const ProcessorChain& chain, Polarity polarity, float strength,
                                                 double fps) {
    const double dt = 1.0 / std::max(fps, 1.0);
    const float rest = mapPolarity(0.0f, polarity);
    const float hit = mapPolarity(strength, polarity);

    // At rest, settled -- the state an event arrives into.
    ProcessorChain::State state;
    float yRest = chain.process(rest, false, 0.0, state);
    for (int i = 0; i < 8; ++i) {
        yRest = chain.process(rest, false, dt, state);
    }
    const float full = sustainedDeviation(chain, hit, yRest, dt);
    if (!(full > 1e-6f * std::max(1.0f, std::fabs(yRest)))) {
        return std::nullopt;
    }
    // One event, then rest for as long as the chain could still be rising: the delay, the attack, an
    // envelope's hold, and a few frames to spare.
    const double window = chain.delaySeconds() +
                          static_cast<double>(std::clamp(chain.attackMs, 0.0f, ProcessorChain::kMaxTimeMs)) / 1000.0 +
                          static_cast<double>(std::max(chain.envelopeHoldMs, 0.0f)) / 1000.0 + 4.0 * dt + 0.1;
    const int frames = static_cast<int>(std::min(std::ceil(window / dt), 200000.0));
    float peak = std::fabs(chain.process(hit, true, dt, state) - yRest);
    for (int i = 1; i < frames; ++i) {
        peak = std::max(peak, std::fabs(chain.process(rest, false, dt, state) - yRest));
    }
    return EventPassThrough{peak / full, peak, full};
}

bool flatChain(const ProcessorChain& chain, Polarity polarity, float lo, float hi) {
    if (!(hi > lo)) {
        hi = lo + 1.0f;
    }
    constexpr int kSamples = 17;
    float first = 0.0f;
    for (int i = 0; i < kSamples; ++i) {
        const float x = lo + (hi - lo) * static_cast<float>(i) / static_cast<float>(kSamples - 1);
        const float y = settled(chain, mapPolarity(x, polarity));
        if (i == 0) {
            first = y;
        } else if (std::fabs(y - first) > 1e-6f * std::max(1.0f, std::fabs(first))) {
            return false;
        }
    }
    return true;
}

const Registry& Registry::standard() {
    static const Registry registry;
    return registry;
}

std::span<const RuleInfo> Registry::rules() const {
    return kRules;
}

const RuleInfo* Registry::rule(std::string_view id) const {
    for (const RuleInfo& r : kRules) {
        if (r.id == id) {
            return &r;
        }
    }
    return nullptr;
}

std::vector<Finding> Registry::checkTarget(std::string_view path, int component, const Facts& facts) const {
    std::vector<Finding> out;
    const IParameter* param = facts.parameter(path);
    if (param == nullptr) {
        out.push_back(make("unknown-target", Verdict::Dead, fmt::format("'{}' is not a registered parameter", path)));
        return out;
    }
    if (component >= 0 && static_cast<std::size_t>(component) >= param->componentCount()) {
        out.push_back(make("component-out-of-range", Verdict::Dead,
                           fmt::format("component {} of '{}', which has {}", component, path,
                                       param->componentCount())));
        return out;
    }
    if (auto dead = facts.deadTarget(path, component)) {
        out.push_back(std::move(*dead));
    }
    if (auto rate = facts.phaseRate(path)) {
        out.push_back(std::move(*rate));
    }
    return out;
}

std::vector<Finding> Registry::checkRoute(const ModRoute& route, const Facts& facts) const {
    std::vector<Finding> out;
    if (!route.enabled) {
        out.push_back(make("disabled", Verdict::Dead, "the route is switched off"));
    }
    const SignalFacts source = facts.signal(route.source);
    if (!source.exists) {
        out.push_back(make("unknown-source", Verdict::Dead,
                           fmt::format("'{}' is not a signal on the bus", route.source)));
    }
    if (!route.depthSource.empty() && !facts.signal(route.depthSource).exists) {
        out.push_back(make("unknown-depth-source", Verdict::Dead,
                           fmt::format("depth source '{}' is not a signal on the bus", route.depthSource)));
    }
    std::vector<Finding> target = checkTarget(route.target, route.component, facts);
    if (const IParameter* param = facts.parameter(route.target); param != nullptr && !param->flags().modulatable) {
        target.insert(target.begin(), make("not-modulatable", Verdict::Dead,
                                           fmt::format("'{}' refuses modulation", route.target)));
    }
    out.insert(out.end(), std::make_move_iterator(target.begin()), std::make_move_iterator(target.end()));
    if (!source.exists) {
        return out;
    }

    if (route.amount == 0.0f) {
        out.push_back(make("zero-amount", Verdict::Dead, "its amount is 0"));
    } else if (!route.depthSource.empty() && route.depthMin == 0.0f && route.depthMax == 0.0f) {
        out.push_back(make("zero-amount", Verdict::Dead,
                           fmt::format("its depth range on '{}' is [0, 0]", route.depthSource)));
    }
    if (!source.silentBecause.empty()) {
        out.push_back(make("silent-source", Verdict::Dead, source.silentBecause));
    }
    if (!source.liveOnlyBecause.empty()) {
        out.push_back(make("live-only-source", Verdict::Hazard, source.liveOnlyBecause));
    }
    if (route.chain.delayMs > ProcessorChain::kMaxDelayMs) {
        out.push_back(make("delay-over-ceiling", Verdict::Hazard,
                           fmt::format("delayMs {:g} is clamped to {:g}", route.chain.delayMs,
                                       ProcessorChain::kMaxDelayMs)));
    }

    const double fps = facts.frameRate();
    if (flatChain(route.chain, route.polarity, source.minValue, source.maxValue)) {
        out.push_back(make("flat-chain", Verdict::Dead,
                           fmt::format("the chain settles to one value for every input in [{:g}, {:g}]",
                                       source.minValue, source.maxValue)));
        return out;
    }
    if (source.isEvent) {
        // Judged at the full strength, and at the piece's typical strength when that is lower: a
        // threshold between the two answers the rare full-strength event and gates out the rest.
        const float full = source.maxValue > 0.0f ? source.maxValue : 1.0f;
        const float typical =
            source.typicalEventStrength > 0.0f && source.typicalEventStrength < full ? source.typicalEventStrength : full;
        const auto atFull = eventPassThrough(route.chain, route.polarity, full, fps);
        if (!atFull) {
            out.push_back(make("flat-chain", Verdict::Dead,
                               fmt::format("the chain gives no response to '{}' at its full strength", route.source)));
        } else {
            const auto atTypical = typical < full ? eventPassThrough(route.chain, route.polarity, typical, fps) : atFull;
            if (!atTypical) {
                out.push_back(make("event-swallowed", Verdict::Dead,
                                   fmt::format("the chain gives no response to a typical '{}' event (strength {:.2f}, "
                                               "the median of the piece's; a threshold above it?)",
                                               route.source, typical)));
            } else if (atTypical->ratio < kEventHazardRatio) {
                out.push_back(make("event-swallowed",
                                   atTypical->ratio < kEventDeadRatio ? Verdict::Dead : Verdict::Hazard,
                                   fmt::format("a one-frame '{}' event (strength {:.2f}) reaches {:.1f}% of its amount "
                                               "at {:g} fps (attack {:g} ms, decay {:g} ms)",
                                               route.source, typical, 100.0f * atTypical->ratio, fps,
                                               route.chain.attackMs, route.chain.decayMs)));
            }
        }
    }
    if (!source.pulses.empty()) {
        // Pulses written into a value-mode timeline: sampled on the frame grid, so one shorter than a
        // frame can fall between two frames and never happen.
        std::size_t missedHere = 0;
        std::size_t missedPreview = 0;
        double firstMissed = -1.0;
        double shortest = source.pulses.front().second - source.pulses.front().first;
        for (const auto& [a, b] : source.pulses) {
            shortest = std::min(shortest, b - a);
            if (!pulseSampled(a, b, fps)) {
                ++missedHere;
                if (firstMissed < 0.0) {
                    firstMissed = a;
                }
            }
            if (!pulseSampled(a, b, kPreviewFrameRate)) {
                ++missedPreview;
                if (firstMissed < 0.0) {
                    firstMissed = a;
                }
            }
        }
        if (missedHere > 0 || missedPreview > 0) {
            out.push_back(make(
                "pulse-between-frames", missedHere == source.pulses.size() ? Verdict::Dead : Verdict::Hazard,
                fmt::format("'{}' is scored as {} value pulses as short as {:.1f} ms: {} fall between frames at "
                            "{:g} fps and {} at {:g} fps (first at {:.3f} s); an event-mode source fires on the "
                            "crossing at any frame rate",
                            route.source, source.pulses.size(), 1000.0 * shortest, missedHere, fps,
                            missedPreview, kPreviewFrameRate, firstMissed)));
        }
        // The chain, fed the shortest pulse as the frames see it (the frames inside it at full height).
        const double dt = 1.0 / std::max(fps, 1.0);
        const auto inside = static_cast<int>(std::max(1.0, std::floor(shortest / dt + 1e-9)));
        const float hit = mapPolarity(source.maxValue > 0.0f ? source.maxValue : 1.0f, route.polarity);
        const float rest = mapPolarity(0.0f, route.polarity);
        ProcessorChain::State state;
        float yRest = route.chain.process(rest, false, 0.0, state);
        for (int i = 0; i < 8; ++i) {
            yRest = route.chain.process(rest, false, dt, state);
        }
        const float full = sustainedDeviation(route.chain, hit, yRest, dt);
        if (full > 1e-6f * std::max(1.0f, std::fabs(yRest))) {
            float peak = 0.0f;
            const double window = route.chain.delaySeconds() +
                                  static_cast<double>(std::clamp(route.chain.attackMs, 0.0f, ProcessorChain::kMaxTimeMs)) /
                                      1000.0 +
                                  static_cast<double>(inside) * dt + 4.0 * dt + 0.1;
            const int frames = static_cast<int>(std::min(std::ceil(window / dt), 200000.0));
            for (int i = 0; i < frames; ++i) {
                peak = std::max(peak, std::fabs(route.chain.process(i < inside ? hit : rest, false, dt, state) - yRest));
            }
            const float ratio = peak / full;
            if (ratio < kEventHazardRatio) {
                out.push_back(make("pulse-swallowed", ratio < kEventDeadRatio ? Verdict::Dead : Verdict::Hazard,
                                   fmt::format("its shortest pulse ({:.1f} ms, {} frame(s) at {:g} fps) reaches "
                                               "{:.1f}% of its amount (attack {:g} ms)",
                                               1000.0 * shortest, inside, fps, 100.0f * ratio, route.chain.attackMs)));
            }
        }
    }
    return out;
}

std::vector<Finding> Registry::checkTrack(const Track& track, const Facts& facts) const {
    std::vector<Finding> out;
    if (!track.enabled) {
        out.push_back(make("disabled", Verdict::Dead, "the track is switched off"));
    }
    for (Finding& f : checkTarget(track.target, track.component, facts)) {
        if (f.rule == "phase-rate") {
            // A rate held constant by its track is only a different constant rate; one that changes
            // over the track is the jump.
            bool varies = false;
            const std::size_t n = track.keyedComponents();
            for (std::size_t k = 1; k < track.keys.size() && !varies; ++k) {
                for (std::size_t c = 0; c < n && c < 4; ++c) {
                    if (track.keys[k].value[c] != track.keys[0].value[c]) {
                        varies = true;
                        break;
                    }
                }
            }
            if (!varies) {
                continue;
            }
        }
        out.push_back(std::move(f));
    }
    return out;
}

Registry::SetFindings Registry::checkSet(std::span<const ModRoute> routes, std::span<const Track> tracks) const {
    SetFindings out;
    out.routes.resize(routes.size());
    out.tracks.resize(tracks.size());
    const auto overlaps = [](int a, int b) { return a < 0 || b < 0 || a == b; };
    // Covers: `a` writes every component `b` writes.
    const auto covers = [](int a, int b) { return a < 0 || a == b; };
    const auto describe = [](std::size_t index, const ModRoute& r) {
        return fmt::format("route {} ({} -> {}, {})", index, r.source, r.target, componentText(r.component));
    };
    for (std::size_t t = 0; t < tracks.size(); ++t) {
        const Track& track = tracks[t];
        if (!track.enabled) {
            continue;
        }
        for (std::size_t r = 0; r < routes.size(); ++r) {
            const ModRoute& route = routes[r];
            if (!route.enabled || route.op != ModOp::Replace || route.target != track.target ||
                !overlaps(route.component, track.component)) {
                continue;
            }
            const bool full = covers(route.component, track.component) && route.depthSource.empty();
            out.tracks[t].push_back(make("overridden-by-replace", full ? Verdict::Dead : Verdict::Hazard,
                                         fmt::format("{} replaces it every frame, and tracks apply before routes",
                                                     describe(r, route))));
            break;
        }
    }
    for (std::size_t r = 0; r < routes.size(); ++r) {
        const ModRoute& route = routes[r];
        if (!route.enabled || route.op != ModOp::Replace) {
            continue;
        }
        for (std::size_t later = r + 1; later < routes.size(); ++later) {
            const ModRoute& next = routes[later];
            if (!next.enabled || next.op != ModOp::Replace || next.target != route.target ||
                !overlaps(next.component, route.component)) {
                continue;
            }
            const bool full = covers(next.component, route.component) && next.depthSource.empty();
            out.routes[r].push_back(make("overridden-by-replace", full ? Verdict::Dead : Verdict::Hazard,
                                         fmt::format("{} is a later Replace on the same target and wins",
                                                     describe(later, next))));
            break;
        }
    }
    return out;
}

} // namespace avgen::params::liveness
