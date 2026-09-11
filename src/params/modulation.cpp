#include "params/modulation.hpp"

#include "core/log.hpp"

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
        route.targetParam = nullptr;
        ++failed;
        if (!problems.empty()) {
            problems += "; ";
        }
        problems += fmt::format("route {} ({} -> {}): {}", index, route.source, route.target, reason);
        log::warn("modulation route {} ({} -> {}) unresolved: {}", index, route.source, route.target, reason);
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
        route.sourceId = *sourceId;
        route.targetParam = target;
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
    (void)params;
    if (!bound_) {
        return;
    }
    for (const ModOp op : kOpPriority) {
        for (ModRoute& route : routes_) {
            if (route.op != op || !route.enabled || route.targetParam == nullptr ||
                route.sourceId == signals::kInvalidSignal) {
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

            IParameter& target = *route.targetParam;
            const std::size_t count = target.componentCount();
            if (route.component < 0) {
                for (std::size_t c = 0; c < count; ++c) {
                    target.setFinalComponent(c, applyModOp(op, target.finalComponent(c), y));
                }
            } else if (static_cast<std::size_t>(route.component) < count) {
                const auto c = static_cast<std::size_t>(route.component);
                target.setFinalComponent(c, applyModOp(op, target.finalComponent(c), y));
            }
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
