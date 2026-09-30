#pragma once

// The Visual Interpreter (ADR-1020, brief §13/§26): a Source kind, "interpret", whose named mappings blend any bus
// signals -- Sonic Character, Musical Context, anything else -- into visual signals `visual.<mapping>`, 0..1, that
// ordinary routes carry to parameters.
//
// It is a semantic translation layer and nothing more: no rendering, no scene knowledge, no hard-coded mapping.
// Everything artistic is data in the project:
//
//   {"kind": "interpret", "name": "garden", "settings": {
//     "mappings": [
//       {"name": "organic", "combine": "mean", "group": "family",
//        "inputs": [{"signal": "sonic.warmth.slow", "weight": 1.0},
//                   {"signal": "sonic.roughness.slow", "weight": 0.5, "invert": true}],
//        "bias": 0.0, "gain": 1.0, "curve": 1.0}],
//     "groups": {"family": {"sharpness": 3.0}}}}
//
// A mapping's value is: combine(inputs) -> bias + gain * v -> clamp 0..1 -> v^curve. Combine is
//   mean     sum(w x) / sum(w)              (the default: a many-to-one blend)
//   sum      sum(w x)
//   product  prod(x^w)                      (an AND: every input must be present)
//   max, min of w x
// with x = 1 - value for an inverted input. Mappings in a GROUP then compete: their values are raised to the
// group's sharpness and renormalised to sum to 1 -- visual families as continuous weights rather than `if`s, so a
// sound drifting between families crossfades rather than switches (brief §26, §36).
//
// Weights, bias, gain, curve and sharpness are parameters (`sources/<name>/<mapping>/{in1..,bias,gain,curve}`,
// `sources/<name>/group/<group>/sharpness`), so they are tunable live and themselves modulatable.
//
// It is a pure function of the bus and its parameters, so it declares itself `pureInTime` and a seek replays the
// routes that read it. It reads other signals, so it updates in the rack's second pass (after the publishers).

#include "params/parameter_set.hpp"
#include "signals/source.hpp"

#include <string>
#include <vector>

namespace avgen::sonic {

enum class Combine : std::uint8_t { Mean, Sum, Product, Max, Min };

class InterpretSource final : public signals::Source {
public:
    explicit InterpretSource(std::string name);
    [[nodiscard]] std::string kind() const override { return "interpret"; }
    void attach(signals::SignalBus& bus, params::ParameterSet& params) override;
    void detach(params::ParameterSet& params) override;
    void update(signals::SignalBus& bus, const signals::SourceContext& context) override;
    void reset() override {}
    [[nodiscard]] nlohmann::json settingsToJson() const override;
    Result<void> settingsFromJson(const nlohmann::json& j) override;
    [[nodiscard]] std::vector<std::string> outputs() const override;
    [[nodiscard]] bool pureInTime() const override { return true; }
    void sample(signals::SignalBus& bus, const signals::SourceContext& context) const override;
    [[nodiscard]] bool readsTriggers() const override { return true; }

    struct Input {
        std::string signal;
        float weight = 1.0f;
        bool invert = false;
        mutable signals::SignalId id = signals::kInvalidSignal; // resolved lazily: the producer may come later
        params::Parameter<float>* weightParam = nullptr;
    };
    struct Mapping {
        std::string name;
        Combine combine = Combine::Mean;
        std::vector<Input> inputs;
        float bias = 0.0f;
        float gain = 1.0f;
        float curve = 1.0f;
        std::string group;
        int groupIndex = -1;
        signals::SignalId output = signals::kInvalidSignal;
        params::Parameter<float>* biasParam = nullptr;
        params::Parameter<float>* gainParam = nullptr;
        params::Parameter<float>* curveParam = nullptr;
    };
    struct Group {
        std::string name;
        float sharpness = 1.0f;
        params::Parameter<float>* sharpnessParam = nullptr;
    };

    [[nodiscard]] const std::vector<Mapping>& mappings() const { return mappings_; }

    // One mapping's value before group competition, from input values already inverted and paired with weights.
    [[nodiscard]] static float combine(Combine mode, const std::vector<std::pair<float, float>>& weighted);
    // Shaping: bias + gain * v, clamped to 0..1, then raised to curve.
    [[nodiscard]] static float shape(float v, float bias, float gain, float curve);
    // Competition: values^sharpness renormalised to sum 1 (all zero stays all zero).
    static void compete(std::vector<float>& values, float sharpness);

private:
    std::vector<Mapping> mappings_;
    std::vector<Group> groups_;
    mutable std::vector<float> values_;
    mutable std::vector<std::pair<float, float>> scratch_;
    mutable std::vector<float> groupScratch_;
};

[[nodiscard]] const char* combineName(Combine c);

} // namespace avgen::sonic
