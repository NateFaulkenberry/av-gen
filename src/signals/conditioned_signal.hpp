#pragma once

// ADR-1602: a conditioned signal, `signal.<name>` -- a route's chain whose output is a bus signal, published the same
// frame as its inputs.
//
// A scene's musical ideas ("a bass breath", "a drum hit, else the mix's onset", "the swell that fires the scan") used to
// be named by writing a route into a macro knob and reading `macro.<k>` back (one frame late: a macro publishes the
// finals its routes wrote the frame before), by copying the same chain onto every target (N states of one idea), or
// by doubling every stem route with a main-mix twin. This source names one once:
//
//   {"kind": "signal", "name": "swell", "settings": {
//     "combine": "firstActive",                         // firstActive (default) | sum | mean | max
//     "inputs": [
//       {"source": "stem.bass.onset", "chain": {...}, "amount": 1.0,
//        "depthSource": "stem.bass.level", "depthMin": -0.3, "depthMax": 1.9, "depthDefault": 0.136},
//       {"source": "response.bass", "chain": {...}},
//       {"constant": 1.0}],
//     "chain": {...},                                    // optional: after the combination
//     "event": false}}                                   // true: publish an event (fires with its selected input)
//
// Each input is exactly a route's front half -- polarity, the `ProcessorChain` routes run (the same code, the same
// state), `amount`, and ADR-900's depth -- applied to a target at 0 with `op: add`: so a one-input signal publishes,
// bit for bit, what that route would have added to its target. Inputs are then combined:
//   firstActive  the first ACTIVE input's value (and its event): the stem/mix fallback (ADR-1570) as one signal;
//   sum, mean, max  over the active inputs (0 with none).
// An input is active when its source is on the bus and its gate reads above 0.5. The gate is `gate` when given (and
// a declared gate the bus lacks makes the input inactive: what it gates is absent), else the source's own
// `<prefix>.active` (`stem.bass.onset` -> `stem.bass.active`) when the bus has one, else always. A constant input is
// always active. Every input's chain runs every frame, active or not, so switching to an input
// never starts its chain cold (a route's twin kept running the same way).
//
// It updates in the rack's second pass with the other readers (envelope, random, interpret), in list order, so routes,
// state triggers and effect signal-triggers see it on its inputs' frame; a signal reading another signal (or an
// envelope) is listed after it. It holds chain state, so a seek replays it (ADR-1168/1573's control replay, which
// `Engine::controlReplayNeeded` runs whenever a signal source exists). It registers no parameters.

#include "params/processor.hpp"
#include "signals/source.hpp"

#include <optional>
#include <string>
#include <vector>

namespace avgen::signals {

class ConditionedSignalSource final : public Source {
public:
    enum class Combine : std::uint8_t { FirstActive, Sum, Mean, Max };

    struct Input {
        std::string source;            // a bus signal; empty with `constant` set
        std::optional<float> constant; // a constant input (no source)
        bool bipolar = false;          // a route's "polarity": "bipolar" (0..1 -> -1..1 before the chain)
        float amount = 1.0f;
        params::ProcessorChain chain{};
        std::string depthSource;       // ADR-900, as a route's
        float depthMin = 0.0f;
        float depthMax = 1.0f;
        std::optional<float> depthDefault; // ADR-1570
        std::string gate;              // empty: `<prefix>.active` when the bus has it, else always active

        // Runtime
        params::ProcessorChain::State state{};
        SignalId id = kInvalidSignal;
        SignalId depthId = kInvalidSignal;
        SignalId gateId = kInvalidSignal;
        bool gateResolved = false; // looked for (whether or not found)
        float value = 0.0f;        // this frame's output (chain x amount x depth)
        bool event = false;        // this frame's source event
        bool active = false;       // this frame
    };

    explicit ConditionedSignalSource(std::string name);
    [[nodiscard]] std::string kind() const override { return "signal"; }
    void attach(SignalBus& bus, params::ParameterSet& params) override;
    void detach(params::ParameterSet& params) override {}
    void update(SignalBus& bus, const SourceContext& context) override;
    void reset() override;
    [[nodiscard]] nlohmann::json settingsToJson() const override;
    Result<void> settingsFromJson(const nlohmann::json& j) override;
    [[nodiscard]] std::vector<std::string> outputs() const override { return {"signal." + name_}; }
    // The rack's second pass: after every publisher (ADR-900's order), in list order with the other readers.
    [[nodiscard]] bool readsTriggers() const override { return true; }

    [[nodiscard]] Combine combine() const { return combine_; }
    [[nodiscard]] const std::vector<Input>& inputs() const { return inputs_; }
    [[nodiscard]] std::vector<Input>& inputs() { return inputs_; }
    void setCombine(Combine c) { combine_ = c; }
    void setOutputChain(std::optional<params::ProcessorChain> chain) { outputChain_ = std::move(chain); }
    [[nodiscard]] const std::optional<params::ProcessorChain>& outputChain() const { return outputChain_; }
    void setEventMode(bool event) { event_ = event; }
    [[nodiscard]] bool eventMode() const { return event_; }
    // This frame's published value, and which input firstActive chose (-1: none active, or another combine).
    [[nodiscard]] float value() const { return value_; }
    [[nodiscard]] int selected() const { return selected_; }

private:
    void resolve(SignalBus& bus);

    Combine combine_ = Combine::FirstActive;
    std::vector<Input> inputs_;
    std::optional<params::ProcessorChain> outputChain_;
    params::ProcessorChain::State outputState_{};
    bool event_ = false;
    SignalId output_ = kInvalidSignal;
    float value_ = 0.0f;
    int selected_ = -1;
};

[[nodiscard]] const char* combineName(ConditionedSignalSource::Combine c);

} // namespace avgen::signals
