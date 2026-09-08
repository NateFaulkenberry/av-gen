#pragma once

// Modulation routes: signal -> chain -> amount -> op -> parameter component (ADR-011).

#include "core/error.hpp"
#include "params/parameter_set.hpp"
#include "params/processor.hpp"
#include "signals/signal_bus.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace avgen::params {

enum class ModOp : std::uint8_t { Add, Multiply, Replace, Min, Max };
enum class Polarity : std::uint8_t { Unipolar, Bipolar }; // Bipolar maps the source 0..1 -> -1..1 before the chain

struct ModRoute {
    std::string source;            // signal name, e.g. "audio.bass"
    std::string target;            // parameter path, e.g. "orb/scale"
    int component = -1;            // -1 = all components
    float amount = 1.0f;           // bipolar; negative inverts
    ModOp op = ModOp::Add;
    Polarity polarity = Polarity::Unipolar;
    ProcessorChain chain{};
    bool enabled = true;

    // Runtime (not serialised)
    ProcessorChain::State state{};
    signals::SignalId sourceId = signals::kInvalidSignal;
    IParameter* targetParam = nullptr;
    float lastOutput = 0.0f;       // for UI display
};

class Modulator {
public:
    ModRoute& addRoute(ModRoute route);
    void clearRoutes();
    [[nodiscard]] std::vector<ModRoute>& routes() { return routes_; }
    [[nodiscard]] const std::vector<ModRoute>& routes() const { return routes_; }

    // Resolves source/target names. Unresolvable routes are disabled and reported.
    [[nodiscard]] Result<void> bind(const signals::SignalBus& bus, ParameterSet& params);
    [[nodiscard]] bool bound() const { return bound_; }

    // params.resetFinals(), then applies every enabled bound route in op-priority order
    // (Replace, Multiply, Add, Min, Max).
    void evaluate(const signals::SignalBus& bus, ParameterSet& params, double dt);

    void resetState(); // clears smoothing/envelope state (on seek)

    // Multiplies every route's amount. The UI's "master visualization gain".
    float masterGain = 1.0f;

private:
    std::vector<ModRoute> routes_;
    bool bound_ = false;
};

float applyModOp(ModOp op, float current, float modulation);

} // namespace avgen::params
