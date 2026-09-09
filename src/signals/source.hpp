#pragma once

// Modulation sources (milestone 0.3, roadmap "Source -> Processor -> Modulator -> Parameter").
// A Source publishes one or more signals on the SignalBus every frame and registers its own
// settings as parameters under "sources/<name>/...", so sources can modulate each other through
// ordinary routes (one frame of latency: sources read their parameters' finals from the
// previous modulation pass). Everything is a pure function of the SourceContext plus explicit
// state, so offline evaluation is deterministic (ADR-012).

#include "core/time.hpp"
#include "params/parameter_set.hpp"
#include "signals/signal_bus.hpp"

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace avgen::signals {

// Per-frame inputs shared by all sources; filled by the Engine.
struct SourceContext {
    FrameTime time;                // renderTime = audio position while playing (ADR-012)
    double audioPosition = 0.0;    // seconds
    double audioDuration = 0.0;    // seconds (0 when no audio)
    bool playing = false;
    float beatPhase = 0.0f;        // 0..1 within the current beat (extrapolated per frame)
    std::uint32_t beatCount = 0;   // beats since the last reset
    float tempoBpm = 0.0f;         // 0 = unknown
    bool beatEvent = false;        // a beat landed this frame
};

class Source {
public:
    virtual ~Source() = default;
    [[nodiscard]] virtual std::string kind() const = 0;      // "lfo", "envelope", "noise", "random", "timeline", "macro"
    [[nodiscard]] const std::string& name() const { return name_; }
    // Declares outputs on the bus and settings as parameters. Idempotent.
    virtual void attach(SignalBus& bus, params::ParameterSet& params) = 0;
    // Removes this source's parameters (signals stay declared; they simply stop updating).
    virtual void detach(params::ParameterSet& params) = 0;
    // Publishes outputs for this frame.
    virtual void update(SignalBus& bus, const SourceContext& context) = 0;
    // Clears integrated state (seek / stop). Pure-function sources may do nothing.
    virtual void reset() = 0;
    // Settings that are not parameters (trigger names, keyframes, shapes). Parameter values are
    // serialised with the ParameterSet, not here.
    [[nodiscard]] virtual nlohmann::json settingsToJson() const = 0;
    virtual Result<void> settingsFromJson(const nlohmann::json& j) = 0;
    // Output signal names (for UI listing), e.g. {"lfo.wobble", "lfo.wobble.bipolar"}.
    [[nodiscard]] virtual std::vector<std::string> outputs() const = 0;

    [[nodiscard]] std::string parameterPrefix() const { return "sources/" + name_ + "/"; }

protected:
    explicit Source(std::string name) : name_(std::move(name)) {}
    std::string name_;
};

// ---- concrete sources -----------------------------------------------------------------------

enum class LfoShape : std::uint8_t { Sine, Triangle, Saw, Square, SampleHold };

// Outputs: "lfo.<name>" in 0..1 and "lfo.<name>.bipolar" in -1..1.
// Parameters: rate (Hz), phase (0..1 offset), pulseWidth (square duty, 0..1), beatSync (bool),
// beatsPerCycle (used when beatSync). Free-running phase = frac(renderTime * rate + phase):
// a function of time, never integrated, so seeking is exact.
class LfoSource final : public Source {
public:
    explicit LfoSource(std::string name, LfoShape shape = LfoShape::Sine);
    [[nodiscard]] std::string kind() const override { return "lfo"; }
    void attach(SignalBus& bus, params::ParameterSet& params) override;
    void detach(params::ParameterSet& params) override;
    void update(SignalBus& bus, const SourceContext& context) override;
    void reset() override {}
    [[nodiscard]] nlohmann::json settingsToJson() const override;
    Result<void> settingsFromJson(const nlohmann::json& j) override;
    [[nodiscard]] std::vector<std::string> outputs() const override;

    [[nodiscard]] LfoShape shape() const { return shape_; }
    void setShape(LfoShape shape) { shape_ = shape; }
    // Waveform in 0..1 for a phase in [0,1). Exposed for tests.
    static float evaluate(LfoShape shape, float phase, float pulseWidth, std::uint64_t seed);

private:
    LfoShape shape_;
    SignalId unipolar_ = kInvalidSignal;
    SignalId bipolar_ = kInvalidSignal;
    params::Parameter<float>* rate_ = nullptr;
    params::Parameter<float>* phase_ = nullptr;
    params::Parameter<float>* pulseWidth_ = nullptr;
    params::Parameter<bool>* beatSync_ = nullptr;
    params::Parameter<float>* beatsPerCycle_ = nullptr;
};

// Output: "env.<name>" in 0..1. Triggered by an event signal (default "audio.onset"); ADSR with
// a hold: attack to 1, decay to sustain, hold for holdMs, release to 0. Retrigger restarts from
// the current level. Parameters: attackMs, decayMs, sustain, holdMs, releaseMs.
class EnvelopeSource final : public Source {
public:
    explicit EnvelopeSource(std::string name, std::string trigger = "audio.onset");
    [[nodiscard]] std::string kind() const override { return "envelope"; }
    void attach(SignalBus& bus, params::ParameterSet& params) override;
    void detach(params::ParameterSet& params) override;
    void update(SignalBus& bus, const SourceContext& context) override;
    void reset() override;
    [[nodiscard]] nlohmann::json settingsToJson() const override;
    Result<void> settingsFromJson(const nlohmann::json& j) override;
    [[nodiscard]] std::vector<std::string> outputs() const override;

    [[nodiscard]] const std::string& trigger() const { return trigger_; }
    void setTrigger(std::string trigger);
    [[nodiscard]] float level() const { return level_; }

private:
    enum class Stage : std::uint8_t { Idle, Attack, Decay, Hold, Release };
    std::string trigger_;
    SignalId triggerId_ = kInvalidSignal;
    SignalId output_ = kInvalidSignal;
    bool triggerResolved_ = false;
    Stage stage_ = Stage::Idle;
    float level_ = 0.0f;
    float stageTime_ = 0.0f;
    params::Parameter<float>* attackMs_ = nullptr;
    params::Parameter<float>* decayMs_ = nullptr;
    params::Parameter<float>* sustain_ = nullptr;
    params::Parameter<float>* holdMs_ = nullptr;
    params::Parameter<float>* releaseMs_ = nullptr;
};

// Output: "noise.<name>" in 0..1. Value noise over time: hashed lattice values at t * rate with
// cubic interpolation (smoothness 1) or steps (smoothness 0). Deterministic per (seed, time).
class NoiseSource final : public Source {
public:
    explicit NoiseSource(std::string name, std::uint32_t seed = 1);
    [[nodiscard]] std::string kind() const override { return "noise"; }
    void attach(SignalBus& bus, params::ParameterSet& params) override;
    void detach(params::ParameterSet& params) override;
    void update(SignalBus& bus, const SourceContext& context) override;
    void reset() override {}
    [[nodiscard]] nlohmann::json settingsToJson() const override;
    Result<void> settingsFromJson(const nlohmann::json& j) override;
    [[nodiscard]] std::vector<std::string> outputs() const override;

    [[nodiscard]] std::uint32_t seed() const { return seed_; }
    void setSeed(std::uint32_t seed) { seed_ = seed; }
    static float evaluate(double time, float rate, float smoothness, std::uint32_t seed);

private:
    std::uint32_t seed_;
    SignalId output_ = kInvalidSignal;
    params::Parameter<float>* rate_ = nullptr;
    params::Parameter<float>* smoothness_ = nullptr;
};

// Output: "random.<name>" in 0..1: a new uniform value on every trigger event (sample & hold),
// slewed towards the target over slewMs. Deterministic: the k-th trigger since reset yields the
// k-th value of the seeded sequence.
class RandomSource final : public Source {
public:
    explicit RandomSource(std::string name, std::string trigger = "audio.onset", std::uint32_t seed = 1);
    [[nodiscard]] std::string kind() const override { return "random"; }
    void attach(SignalBus& bus, params::ParameterSet& params) override;
    void detach(params::ParameterSet& params) override;
    void update(SignalBus& bus, const SourceContext& context) override;
    void reset() override;
    [[nodiscard]] nlohmann::json settingsToJson() const override;
    Result<void> settingsFromJson(const nlohmann::json& j) override;
    [[nodiscard]] std::vector<std::string> outputs() const override;

    void setTrigger(std::string trigger);
    [[nodiscard]] const std::string& trigger() const { return trigger_; }

private:
    std::string trigger_;
    std::uint32_t seed_;
    std::uint32_t count_ = 0;
    float target_ = 0.0f;
    float value_ = 0.0f;
    SignalId triggerId_ = kInvalidSignal;
    SignalId output_ = kInvalidSignal;
    bool triggerResolved_ = false;
    params::Parameter<float>* slewMs_ = nullptr;
};

enum class KeyInterpolation : std::uint8_t { Step, Linear, Smooth };

struct Keyframe {
    double time = 0.0;
    float value = 0.0f;
    KeyInterpolation interpolation = KeyInterpolation::Linear;
};

// Output: "timeline.<name>". Keyframes evaluated at renderTime (audio position while playing);
// before the first key the first value holds, after the last the last value holds, unless
// loopLength > 0 in which case time wraps. Parameters: offset (seconds), scale (time scale).
class TimelineSource final : public Source {
public:
    explicit TimelineSource(std::string name);
    [[nodiscard]] std::string kind() const override { return "timeline"; }
    void attach(SignalBus& bus, params::ParameterSet& params) override;
    void detach(params::ParameterSet& params) override;
    void update(SignalBus& bus, const SourceContext& context) override;
    void reset() override {}
    [[nodiscard]] nlohmann::json settingsToJson() const override;
    Result<void> settingsFromJson(const nlohmann::json& j) override;
    [[nodiscard]] std::vector<std::string> outputs() const override;

    [[nodiscard]] std::vector<Keyframe>& keys() { return keys_; } // keep sorted via sortKeys()
    [[nodiscard]] const std::vector<Keyframe>& keys() const { return keys_; }
    void addKey(Keyframe key); // inserts sorted
    void sortKeys();
    [[nodiscard]] double loopLength() const { return loopLength_; }
    void setLoopLength(double seconds) { loopLength_ = seconds; }
    [[nodiscard]] float evaluate(double time) const;

private:
    std::vector<Keyframe> keys_;
    double loopLength_ = 0.0;
    SignalId output_ = kInvalidSignal;
    params::Parameter<float>* offset_ = nullptr;
    params::Parameter<float>* scale_ = nullptr;
};

// Outputs: "control.<channel>" for named channels fed from outside the frame loop (MIDI, OSC,
// UI "learn", tests) through set()/pulse(): continuous channels hold their last value (0..1),
// event channels fire for one frame with a strength. Channels declared in settings exist before
// any message arrives so routes can bind to them; unknown channels are created on first set()
// and need a re-attach (the engine rebinds when the source reports new channels).
class ControlSource final : public Source {
public:
    explicit ControlSource(std::string name = "control");
    [[nodiscard]] std::string kind() const override { return "control"; }
    void attach(SignalBus& bus, params::ParameterSet& params) override;
    void detach(params::ParameterSet& params) override;
    void update(SignalBus& bus, const SourceContext& context) override;
    void reset() override;
    [[nodiscard]] nlohmann::json settingsToJson() const override;
    Result<void> settingsFromJson(const nlohmann::json& j) override;
    [[nodiscard]] std::vector<std::string> outputs() const override;

    // Declares a channel (idempotent). Returns true when it is new (attach() needed).
    bool addChannel(const std::string& channel, bool isEvent = false);
    [[nodiscard]] bool hasChannel(const std::string& channel) const;
    [[nodiscard]] const std::vector<std::string>& channels() const { return channels_; }
    [[nodiscard]] bool isEventChannel(const std::string& channel) const;
    // Value for a continuous channel (clamped 0..1); creates the channel when unknown.
    void set(const std::string& channel, float value);
    // Fires an event channel on the next update (strength 0..1); creates it when unknown.
    void pulse(const std::string& channel, float strength = 1.0f);
    [[nodiscard]] float value(const std::string& channel) const; // last set value (0 when unknown)
    // True when set()/pulse() created channels since the last attach(); cleared by attach().
    [[nodiscard]] bool needsAttach() const { return needsAttach_; }

private:
    std::vector<std::string> channels_;
    std::vector<bool> events_;
    std::vector<float> values_;
    std::vector<float> pending_;    // event strength queued for the next update (-1 = none)
    std::vector<SignalId> outputs_;
    bool needsAttach_ = false;
};

// Outputs: "macro.<knob>" for each knob, mirroring the parameter "macros/<knob>" (0..1). Macros
// are UI knobs that fan out through ordinary routes (with remap) to many targets, and because
// they are parameters they can themselves be modulated.
class MacroSource final : public Source {
public:
    explicit MacroSource(std::string name = "macros");
    [[nodiscard]] std::string kind() const override { return "macro"; }
    void attach(SignalBus& bus, params::ParameterSet& params) override;
    void detach(params::ParameterSet& params) override;
    void update(SignalBus& bus, const SourceContext& context) override;
    void reset() override {}
    [[nodiscard]] nlohmann::json settingsToJson() const override;
    Result<void> settingsFromJson(const nlohmann::json& j) override;
    [[nodiscard]] std::vector<std::string> outputs() const override;

    // Adds a knob (default 0.5). Requires re-attach if called after attach().
    void addKnob(std::string knob, float defaultValue = 0.5f);
    [[nodiscard]] const std::vector<std::string>& knobs() const { return knobs_; }

private:
    std::vector<std::string> knobs_;
    std::vector<float> defaults_;
    std::vector<SignalId> outputs_;
    std::vector<params::Parameter<float>*> params_;
};

// ---- rack -----------------------------------------------------------------------------------

class SourceRack {
public:
    // Takes ownership; attaches immediately when the rack has been attached. Names must be unique
    // per kind; a duplicate replaces the existing source.
    Source& add(std::unique_ptr<Source> source);
    bool remove(const std::string& kind, const std::string& name);
    [[nodiscard]] Source* find(const std::string& kind, const std::string& name);
    [[nodiscard]] const std::vector<std::unique_ptr<Source>>& sources() const { return sources_; }

    void attach(SignalBus& bus, params::ParameterSet& params); // attaches all, remembers targets
    void detachAll();
    void update(SignalBus& bus, const SourceContext& context);
    void reset();
    void clear();

    [[nodiscard]] nlohmann::json toJson() const;
    // Replaces the rack contents. Unknown kinds are skipped with a warning; malformed entries fail.
    Result<void> fromJson(const nlohmann::json& j);

    // Factory for the built-in kinds.
    static std::unique_ptr<Source> create(const std::string& kind, const std::string& name);

private:
    std::vector<std::unique_ptr<Source>> sources_;
    SignalBus* bus_ = nullptr;
    params::ParameterSet* params_ = nullptr;
};

} // namespace avgen::signals
