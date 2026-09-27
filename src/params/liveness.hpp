#pragma once

// ADR-902: route and parameter liveness -- one registry of rules for "can this reach the picture?"
//
// This codebase keeps producing routes, tracks and parameters that bind with no warning and never
// reach the output: event routes whose attack swallowed the event, `emissiveBoost` on a node that
// emits nothing, a speed routed where the phase is time x speed, an arc on a program output nothing
// reads. Each was found by a person looking at a frame. These rules find them by reading the
// project instead, and they are the "configured" tier of the owner's three (brief §16):
//
//   * **live**   -- the route binds and no rule can show it failing to reach the picture. It is
//                   connected; whether it visibly moves anything (behavioural) and whether that helps
//                   (meaningful) are the evaluator's tiers, not these.
//   * **dead**   -- a rule shows it cannot reach the picture, with the evidence in the reason.
//   * **hazard** -- it reaches the picture, in a way that is almost certainly not what was meant
//                   (a pattern that jumps, an event that arrives at a fraction of its amount).
//
// The registry is queryable on its own: `Registry::standard().checkTarget(path, ...)` is the question
// a validator asks before it proposes a route (the Director's, recommendation 2 of the director
// audit); `checkRoute` and `checkTrack` are what `Modulator::bind`, a project load and
// `avgen --audit-routes` ask. What a rule needs to know about the world comes through `Facts`, so the
// same rules run on a bare modulator (`BusFacts`: the bus and the parameters) and on an engine
// (`app::EngineFacts`: the scene, its programs, its nodes, its effects).

#include "params/modulation.hpp"
#include "params/timeline.hpp"
#include "signals/signal_bus.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace avgen::params::liveness {

enum class Verdict : std::uint8_t { Live, Dead, Hazard };
[[nodiscard]] const char* verdictName(Verdict v);

struct Finding {
    std::string rule;     // stable id from the registry's table, e.g. "event-swallowed"
    Verdict verdict = Verdict::Hazard;
    std::string reason;   // one sentence, naming the evidence
};

// Dead over hazard over live.
[[nodiscard]] Verdict verdictOf(std::span<const Finding> findings);

// What the rules may know about a signal.
struct SignalFacts {
    bool exists = false;
    bool isEvent = false;      // a one-frame event (the bus's flag, or a timeline source in event mode)
    float minValue = 0.0f;     // the declared range
    float maxValue = 1.0f;
    // Why the signal cannot move at all in this project (no audio track, ...); empty when it can.
    std::string silentBecause;
    // Why it moves only with live input (MIDI, OSC) and is therefore constant in a render.
    std::string liveOnlyBecause;
    // A value-mode timeline source's nonzero spans [start, end) in seconds of the piece: the pulses a
    // score was written as. Empty for anything else.
    std::vector<std::pair<double, double>> pulses;
    // An event source's typical strength in this piece -- the median of its events (the track's
    // onsets, a scored timeline's hits) -- when known; 0 = judge at `maxValue`. A chain can respond to a
    // full-strength event and still gate out the events the piece actually has.
    float typicalEventStrength = 0.0f;
};

class Facts {
public:
    virtual ~Facts() = default;
    [[nodiscard]] virtual SignalFacts signal(std::string_view name) const = 0;
    [[nodiscard]] virtual const IParameter* parameter(std::string_view path) const = 0;
    // Why nothing written to `path` can reach the output, as a finding (its rule id says which rule);
    // nothing when no rule can show it. `component` -1 = all.
    [[nodiscard]] virtual std::optional<Finding> deadTarget(std::string_view path, int component) const;
    // Why moving `path` over time makes its pattern jump (its phase is time x value); nothing when
    // it does not, or when that is not known.
    [[nodiscard]] virtual std::optional<Finding> phaseRate(std::string_view path) const;
    // The frame rate a render of the project runs at. Event rules sample at it.
    [[nodiscard]] virtual double frameRate() const { return 60.0; }
};

// What a modulator knows by itself: the signals on its bus and the parameters in its set.
class BusFacts : public Facts {
public:
    BusFacts(const signals::SignalBus& bus, const ParameterSet& params) : bus_(bus), params_(params) {}
    [[nodiscard]] SignalFacts signal(std::string_view name) const override;
    [[nodiscard]] const IParameter* parameter(std::string_view path) const override;

private:
    const signals::SignalBus& bus_;
    const ParameterSet& params_;
};

// One rule, as the audit's table and a validator's catalogue list it.
struct RuleInfo {
    std::string_view id;
    std::string_view appliesTo;   // "route", "track", "route|track", "effect"
    std::string_view verdicts;    // "dead", "hazard", "dead|hazard"
    std::string_view summary;
};

// How much of a one-frame event the chain lets through, sampled at a frame rate: the largest
// deviation from the chain's resting output after one event, over the deviation an input held at
// the event's strength settles to. `ratio` 1 = the event arrives in full.
struct EventPassThrough {
    float ratio = 0.0f;
    float peak = 0.0f;   // largest |output - rest| after the event
    float full = 0.0f;   // |settled output at the event's strength - rest|
};
// Nothing when the chain has no response at that strength at all (see `flatChain`).
[[nodiscard]] std::optional<EventPassThrough> eventPassThrough(const ProcessorChain& chain, Polarity polarity,
                                                               float strength, double fps);
// Whether the chain settles to the same output for every input in [lo, hi] (sampled): a route that
// cannot move its target whatever its source does.
[[nodiscard]] bool flatChain(const ProcessorChain& chain, Polarity polarity, float lo, float hi);

class Registry {
public:
    [[nodiscard]] static const Registry& standard();

    // Every rule, including the ones a host evaluates through its facts (target rules) or itself
    // (effect rules), so one table documents the vocabulary.
    [[nodiscard]] std::span<const RuleInfo> rules() const;
    [[nodiscard]] const RuleInfo* rule(std::string_view id) const;

    // One route on its own: binding, chain, source and target rules.
    [[nodiscard]] std::vector<Finding> checkRoute(const ModRoute& route, const Facts& facts) const;
    // One timeline track on its own: binding and target rules.
    [[nodiscard]] std::vector<Finding> checkTrack(const Track& track, const Facts& facts) const;
    // Everything about a target alone -- unknown, not modulatable, out of range, dead, phase-rate:
    // the question to ask before proposing a route or a track to `path`.
    [[nodiscard]] std::vector<Finding> checkTarget(std::string_view path, int component, const Facts& facts) const;

    // The rules that need the whole set: a track under a Replace route, a Replace route under a later
    // one. Returns per-item findings, index-aligned with `routes` and `tracks`, to be merged with the
    // single-item checks.
    struct SetFindings {
        std::vector<std::vector<Finding>> routes;
        std::vector<std::vector<Finding>> tracks;
    };
    [[nodiscard]] SetFindings checkSet(std::span<const ModRoute> routes, std::span<const Track> tracks) const;
};

// The reach below which an event route is dead rather than a hazard.
constexpr float kEventDeadRatio = 0.10f;
// The reach below which an event route is flagged at all (the brief: "passes less than 50%").
constexpr float kEventHazardRatio = 0.50f;
// The frame rate a preview is rendered at; a pulse that falls between its frames is a hazard even
// when the project's own rate catches it.
constexpr double kPreviewFrameRate = 30.0;

} // namespace avgen::params::liveness
