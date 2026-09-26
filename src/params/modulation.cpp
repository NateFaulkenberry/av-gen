#include "params/modulation.hpp"

#include "core/log.hpp"
#include "params/liveness.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <array>

namespace avgen::params {

namespace {
// Evaluation order: Replace establishes a base, Multiply scales it, Add offsets it and Min/Max
// finally bound it. Routes with the same op are applied in insertion order.
constexpr std::array<ModOp, 5> kOpPriority{ModOp::Replace, ModOp::Multiply, ModOp::Add, ModOp::Min,
                                           ModOp::Max};
} // namespace

float applyModOp(ModOp op, float current, float modulation) {
    switch (op) {
    case ModOp::Add:
        return current + modulation;
    case ModOp::Multiply:
        return current * modulation;
    case ModOp::Replace:
        return modulation;
    case ModOp::Min:
        return std::min(current, modulation);
    case ModOp::Max:
        return std::max(current, modulation);
    }
    return current;
}

// The returned reference is only valid until the next addRoute()/clearRoutes(): routes live in
// a vector that may reallocate. Adding a route invalidates the binding; call bind() again.
ModRoute& Modulator::addRoute(ModRoute route) {
    route.sourceId = signals::kInvalidSignal;
    route.depthId = signals::kInvalidSignal;
    route.targetParam = nullptr;
    routes_.push_back(std::move(route));
    bound_ = false;
    return routes_.back();
}

void Modulator::clearRoutes() {
    routes_.clear();
    bound_ = false;
}

Result<void> Modulator::bind(const signals::SignalBus& bus, ParameterSet& params) {
    std::string problems;
    std::size_t failed = 0;
    // Unresolved routes keep their `enabled` flag (the user's intent) but no ids, so evaluate()
    // skips them until a later bind() resolves them (scene swaps, control channels that appear
    // when the first message arrives).
    auto reject = [&](ModRoute& route, std::size_t index, std::string reason) {
        route.sourceId = signals::kInvalidSignal;
        route.depthId = signals::kInvalidSignal;
        route.targetParam = nullptr;
        ++failed;
        if (!problems.empty()) {
            problems += "; ";
        }
        problems += fmt::format("route {} ({} -> {}): {}", index, route.source, route.target, reason);
        // ADR-902: once, not once per rebind -- the returned error still names every failure each time.
        if (firstReport(fmt::format("unresolved|{}|{}|{}|{}", route.source, route.target, route.component, reason))) {
            log::warn("modulation route {} ({} -> {}) unresolved: {}", index, route.source, route.target, reason);
        }
    };

    for (std::size_t i = 0; i < routes_.size(); ++i) {
        ModRoute& route = routes_[i];
        const auto sourceId = bus.find(route.source);
        IParameter* target = params.find(route.target);
        if (!sourceId) {
            reject(route, i, fmt::format("unknown source signal '{}'", route.source));
            continue;
        }
        if (target == nullptr) {
            reject(route, i, fmt::format("unknown target parameter '{}'", route.target));
            continue;
        }
        if (!target->flags().modulatable) {
            reject(route, i, fmt::format("parameter '{}' is not modulatable", route.target));
            continue;
        }
        if (route.component >= 0 && static_cast<std::size_t>(route.component) >= target->componentCount()) {
            reject(route, i,
                   fmt::format("component {} out of range for '{}' ({} components)", route.component,
                               route.target, target->componentCount()));
            continue;
        }
        signals::SignalId depthId = signals::kInvalidSignal;
        if (!route.depthSource.empty()) {
            const auto found = bus.find(route.depthSource);
            if (!found) {
                reject(route, i, fmt::format("unknown depth signal '{}'", route.depthSource));
                continue;
            }
            depthId = *found;
        }
        route.sourceId = *sourceId;
        route.depthId = depthId;
        route.targetParam = target;
    }

    // ADR-902: every route that bound goes through the liveness rules. A route that binds and cannot
    // move the picture is the failure this codebase keeps shipping, and bind is the one moment every
    // route passes through.
    const liveness::BusFacts busFacts(bus, params);
    const liveness::Facts& facts = facts_ != nullptr ? *facts_ : static_cast<const liveness::Facts&>(busFacts);
    for (std::size_t i = 0; i < routes_.size(); ++i) {
        const ModRoute& route = routes_[i];
        if (route.targetParam == nullptr || !route.enabled) {
            continue;
        }
        for (const liveness::Finding& finding : liveness::Registry::standard().checkRoute(route, facts)) {
            if (firstReport(fmt::format("{}|{}|{}|{}", finding.rule, route.source, route.target, route.component))) {
                log::warn("modulation route {} ({} -> {}) is {}: {} [{}]", i, route.source, route.target,
                          liveness::verdictName(finding.verdict), finding.reason, finding.rule);
            }
        }
    }

    // Resolvable routes evaluate even when others failed.
    bound_ = true;
    if (failed > 0) {
        return fail("{} modulation route(s) could not be bound: {}", failed, problems);
    }
    return {};
}

void Modulator::evaluate(const signals::SignalBus& bus, ParameterSet& params, double dt) {
    params.resetFinals();
    applyRoutes(bus, params, dt);
}

void Modulator::applyRoutes(const signals::SignalBus& bus, ParameterSet& params, double dt) {
    applyRoutesWhere(bus, params, dt, nullptr);
}

void Modulator::applyRoutesWhere(const signals::SignalBus& bus, ParameterSet& params, double dt,
                                 bool (*pick)(const ModRoute&)) {
    (void)params;
    if (!bound_) {
        return;
    }
    for (const ModOp op : kOpPriority) {
        for (ModRoute& route : routes_) {
            if (route.op != op || !route.enabled || route.targetParam == nullptr ||
                route.sourceId == signals::kInvalidSignal || route.sourceId >= bus.size() ||
                (pick != nullptr && !pick(route))) {
                continue;
            }
            // ADR-900: a depth signal the bus does not carry (a seek replay's bus) skips the route,
            // exactly as a source the bus does not carry does.
            const bool scaled = !route.depthSource.empty();
            if (scaled && (route.depthId == signals::kInvalidSignal || route.depthId >= bus.size())) {
                continue;
            }
            float x = bus.value(route.sourceId);
            if (route.polarity == Polarity::Bipolar) {
                x = x * 2.0f - 1.0f; // 0..1 -> -1..1 before the chain
            }
            const bool event = bus.event(route.sourceId);
            const float y =
                route.chain.process(x, event, dt, route.state) * route.amount * route.spatialGain * masterGain;
            route.lastOutput = y;
            const float depth =
                scaled ? route.depthMin + (route.depthMax - route.depthMin) * bus.value(route.depthId) : 1.0f;

            IParameter& target = *route.targetParam;
            const std::size_t count = target.componentCount();
            const auto write = [&](std::size_t c) {
                const float current = target.finalComponent(c);
                const float full = applyModOp(op, current, y);
                // Unscaled routes write exactly what they always wrote (no a + (b - a) rounding).
                target.setFinalComponent(c, scaled ? current + (full - current) * depth : full);
            };
            if (route.component < 0) {
                for (std::size_t c = 0; c < count; ++c) {
                    write(c);
                }
            } else if (static_cast<std::size_t>(route.component) < count) {
                write(static_cast<std::size_t>(route.component));
            }
        }
    }
}

void Modulator::advanceChains(const signals::SignalBus& bus, double dt,
                              const std::function<bool(const ModRoute&)>& pick) {
    if (!bound_) {
        return;
    }
    for (ModRoute& route : routes_) {
        if (!route.enabled || route.targetParam == nullptr || route.sourceId == signals::kInvalidSignal ||
            route.sourceId >= bus.size() || !pick(route)) {
            continue;
        }
        float x = bus.value(route.sourceId);
        if (route.polarity == Polarity::Bipolar) {
            x = x * 2.0f - 1.0f;
        }
        route.lastOutput = route.chain.process(x, bus.event(route.sourceId), dt, route.state) * route.amount *
                           route.spatialGain * masterGain;
    }
}

std::vector<ProcessorChain::State> Modulator::chainStates(const std::function<bool(const ModRoute&)>& pick) const {
    std::vector<ProcessorChain::State> out;
    for (const ModRoute& route : routes_) {
        if (pick(route)) {
            out.push_back(route.state);
        }
    }
    return out;
}

void Modulator::restoreChainStates(const std::function<bool(const ModRoute&)>& pick,
                                   const std::vector<ProcessorChain::State>& states) {
    std::size_t i = 0;
    for (ModRoute& route : routes_) {
        if (pick(route) && i < states.size()) {
            route.state = states[i++];
        }
    }
}

void Modulator::resetChainStates(const std::function<bool(const ModRoute&)>& pick) {
    for (ModRoute& route : routes_) {
        if (pick(route)) {
            route.state = ProcessorChain::State{};
            route.lastOutput = 0.0f;
        }
    }
}

void Modulator::resetState() {
    for (ModRoute& route : routes_) {
        route.state = ProcessorChain::State{};
        route.lastOutput = 0.0f;
    }
}

} // namespace avgen::params
