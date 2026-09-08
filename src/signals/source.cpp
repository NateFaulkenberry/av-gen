#include "signals/source.hpp"

#include "core/log.hpp"
#include "core/rng.hpp"
#include "params/processor.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <nlohmann/json.hpp>
#include <numbers>
#include <string_view>
#include <utility>

namespace avgen::signals {

using nlohmann::json;
using params::ParamDesc;
using params::Parameter;
using params::ParameterSet;

namespace {

// ---- parameter helpers ---------------------------------------------------------------------

Parameter<float>* addFloat(ParameterSet& params, std::string path, float defaultValue, float hardMin,
                           float hardMax, float softMin = 0.0f, float softMax = 0.0f) {
    return &params.add(ParamDesc<float>{.path = std::move(path),
                                        .defaultValue = defaultValue,
                                        .hardMin = hardMin,
                                        .hardMax = hardMax,
                                        .softMin = softMin,
                                        .softMax = softMax});
}

Parameter<bool>* addBool(ParameterSet& params, std::string path, bool defaultValue) {
    return &params.add(ParamDesc<bool>{
        .path = std::move(path), .defaultValue = defaultValue, .hardMin = false, .hardMax = true});
}

template <typename T>
void removeParam(ParameterSet& params, Parameter<T>*& param) {
    if (param != nullptr) {
        params.remove(param->path());
        param = nullptr;
    }
}

// ---- hashing -------------------------------------------------------------------------------

// splitmix64 finaliser: turns structured integers (seed, lattice index) into well-mixed seeds.
constexpr std::uint64_t mix64(std::uint64_t x) {
    x ^= x >> 30u;
    x *= 0xBF58476D1CE4E5B9ULL;
    x ^= x >> 27u;
    x *= 0x94D049BB133111EBULL;
    x ^= x >> 31u;
    return x;
}

// Uniform lattice value in [0,1) for (seed, index); the same on every platform (PCG32).
float latticeValue(std::uint32_t seed, std::int64_t index) {
    const std::uint64_t key = (static_cast<std::uint64_t>(seed) << 32u) ^ static_cast<std::uint64_t>(index);
    return Rng(mix64(key)).nextFloat();
}

constexpr std::uint64_t kSampleHoldSalt = 0x5A17E5EEDC0FFEE1ULL;

// ---- enum names ----------------------------------------------------------------------------

template <typename E>
struct EnumName {
    E value;
    std::string_view name;
};

constexpr std::array<EnumName<LfoShape>, 5> kShapeNames{{{LfoShape::Sine, "sine"},
                                                         {LfoShape::Triangle, "triangle"},
                                                         {LfoShape::Saw, "saw"},
                                                         {LfoShape::Square, "square"},
                                                         {LfoShape::SampleHold, "samplehold"}}};
constexpr std::array<EnumName<KeyInterpolation>, 3> kInterpNames{{{KeyInterpolation::Step, "step"},
                                                                  {KeyInterpolation::Linear, "linear"},
                                                                  {KeyInterpolation::Smooth, "smooth"}}};

template <typename E, std::size_t N>
std::string_view enumToString(const std::array<EnumName<E>, N>& table, E value) {
    for (const auto& entry : table) {
        if (entry.value == value) {
            return entry.name;
        }
    }
    return table[0].name;
}

template <typename E, std::size_t N>
Result<E> enumFromString(const std::array<EnumName<E>, N>& table, std::string_view name, const char* key) {
    for (const auto& entry : table) {
        if (entry.name == name) {
            return entry.value;
        }
    }
    return fail("unknown value '{}' for '{}'", name, key);
}

// Optional keys: missing leaves `out` unchanged; wrong type or unknown name is an error.
template <typename E, std::size_t N>
Result<void> readEnum(const json& j, const char* key, const std::array<EnumName<E>, N>& table, E& out) {
    const auto it = j.find(key);
    if (it == j.end()) {
        return {};
    }
    if (!it->is_string()) {
        return fail("'{}' must be a string", key);
    }
    auto value = enumFromString(table, it->get<std::string>(), key);
    if (!value) {
        return std::unexpected(value.error());
    }
    out = *value;
    return {};
}

Result<void> readString(const json& j, const char* key, std::string& out) {
    const auto it = j.find(key);
    if (it == j.end()) {
        return {};
    }
    if (!it->is_string()) {
        return fail("'{}' must be a string", key);
    }
    out = it->get<std::string>();
    return {};
}

Result<void> readSeed(const json& j, const char* key, std::uint32_t& out) {
    const auto it = j.find(key);
    if (it == j.end()) {
        return {};
    }
    if (!it->is_number_integer() || it->get<std::int64_t>() < 0 ||
        it->get<std::int64_t>() > static_cast<std::int64_t>(UINT32_MAX)) {
        return fail("'{}' must be an unsigned 32-bit integer", key);
    }
    out = it->get<std::uint32_t>();
    return {};
}

Result<void> readDouble(const json& j, const char* key, double& out) {
    const auto it = j.find(key);
    if (it == j.end()) {
        return {};
    }
    if (!it->is_number()) {
        return fail("'{}' must be a number", key);
    }
    out = it->get<double>();
    return {};
}

Result<void> requireObject(const json& j, const char* what) {
    if (!j.is_object()) {
        return fail("{} settings must be a JSON object", what);
    }
    return {};
}

double fractionalPart(double x, double& integerPart) {
    integerPart = std::floor(x);
    return x - integerPart;
}

} // namespace

// ============================================================================================
// LfoSource
// ============================================================================================

LfoSource::LfoSource(std::string name, LfoShape shape)
    : Source(std::move(name))
    , shape_(shape) {}

void LfoSource::attach(SignalBus& bus, ParameterSet& params) {
    unipolar_ = bus.declare("lfo." + name_, 0.0f, 1.0f);
    bipolar_ = bus.declare("lfo." + name_ + ".bipolar", -1.0f, 1.0f);
    const std::string prefix = parameterPrefix();
    rate_ = addFloat(params, prefix + "rate", 0.5f, 0.0f, 100.0f, 0.01f, 20.0f);
    phase_ = addFloat(params, prefix + "phase", 0.0f, 0.0f, 1.0f);
    pulseWidth_ = addFloat(params, prefix + "pulseWidth", 0.5f, 0.0f, 1.0f);
    beatSync_ = addBool(params, prefix + "beatSync", false);
    beatsPerCycle_ = addFloat(params, prefix + "beatsPerCycle", 1.0f, 0.25f, 16.0f);
}

void LfoSource::detach(ParameterSet& params) {
    removeParam(params, rate_);
    removeParam(params, phase_);
    removeParam(params, pulseWidth_);
    removeParam(params, beatSync_);
    removeParam(params, beatsPerCycle_);
}

void LfoSource::update(SignalBus& bus, const SourceContext& context) {
    if (rate_ == nullptr) {
        return;
    }
    const auto offset = static_cast<double>(phase_->value());
    double position = 0.0;
    if (beatSync_->value() && context.tempoBpm > 0.0f) {
        const double beats = static_cast<double>(context.beatCount) + static_cast<double>(context.beatPhase);
        position = beats / static_cast<double>(std::max(beatsPerCycle_->value(), 0.25f)) + offset;
    } else {
        position = context.time.renderTime * static_cast<double>(rate_->value()) + offset;
    }
    double cycle = 0.0;
    const auto phase = static_cast<float>(fractionalPart(position, cycle));
    const auto cycleIndex = static_cast<std::uint64_t>(static_cast<std::int64_t>(cycle));
    const float u = evaluate(shape_, phase, pulseWidth_->value(), cycleIndex);
    bus.set(unipolar_, u);
    bus.set(bipolar_, 2.0f * u - 1.0f);
}

float LfoSource::evaluate(LfoShape shape, float phase, float pulseWidth, std::uint64_t seed) {
    switch (shape) {
    case LfoShape::Sine:
        return 0.5f - 0.5f * std::cos(2.0f * std::numbers::pi_v<float> * phase);
    case LfoShape::Triangle:
        return 1.0f - std::abs(2.0f * phase - 1.0f);
    case LfoShape::Saw:
        return phase;
    case LfoShape::Square:
        return phase < pulseWidth ? 1.0f : 0.0f;
    case LfoShape::SampleHold:
        // One hashed value per integer cycle: `seed` carries the cycle index.
        return Rng(kSampleHoldSalt ^ seed).nextFloat();
    }
    return 0.0f;
}

json LfoSource::settingsToJson() const {
    return json{{"shape", enumToString(kShapeNames, shape_)}};
}

Result<void> LfoSource::settingsFromJson(const json& j) {
    if (auto r = requireObject(j, "lfo"); !r) {
        return r;
    }
    LfoShape shape = shape_;
    if (auto r = readEnum(j, "shape", kShapeNames, shape); !r) {
        return fail("lfo '{}': {}", name_, r.error().message);
    }
    shape_ = shape;
    return {};
}

std::vector<std::string> LfoSource::outputs() const {
    return {"lfo." + name_, "lfo." + name_ + ".bipolar"};
}

// ============================================================================================
// EnvelopeSource
// ============================================================================================

EnvelopeSource::EnvelopeSource(std::string name, std::string trigger)
    : Source(std::move(name))
    , trigger_(std::move(trigger)) {}

void EnvelopeSource::attach(SignalBus& bus, ParameterSet& params) {
    output_ = bus.declare("env." + name_, 0.0f, 1.0f);
    if (const auto id = bus.find(trigger_)) {
        triggerId_ = *id;
        triggerResolved_ = true;
    }
    const std::string prefix = parameterPrefix();
    attackMs_ = addFloat(params, prefix + "attackMs", 10.0f, 0.0f, 60000.0f, 0.0f, 2000.0f);
    decayMs_ = addFloat(params, prefix + "decayMs", 150.0f, 0.0f, 60000.0f, 0.0f, 2000.0f);
    sustain_ = addFloat(params, prefix + "sustain", 0.4f, 0.0f, 1.0f);
    holdMs_ = addFloat(params, prefix + "holdMs", 100.0f, 0.0f, 60000.0f, 0.0f, 2000.0f);
    releaseMs_ = addFloat(params, prefix + "releaseMs", 300.0f, 0.0f, 60000.0f, 0.0f, 5000.0f);
}

void EnvelopeSource::detach(ParameterSet& params) {
    removeParam(params, attackMs_);
    removeParam(params, decayMs_);
    removeParam(params, sustain_);
    removeParam(params, holdMs_);
    removeParam(params, releaseMs_);
}

void EnvelopeSource::setTrigger(std::string trigger) {
    trigger_ = std::move(trigger);
    triggerId_ = kInvalidSignal;
    triggerResolved_ = false;
}

void EnvelopeSource::reset() {
    stage_ = Stage::Idle;
    level_ = 0.0f;
    stageTime_ = 0.0f;
}

// Linear segments with full-scale slopes: attack climbs at 1/attackMs so a fresh trigger reaches
// 1 exactly at attackMs (a retrigger continues from the current level without a jump), decay
// falls at (1 - sustain)/decayMs, hold sits at sustain for holdMs, release falls at
// sustain/releaseMs. One frame may cross several stages; leftover time carries over.
void EnvelopeSource::update(SignalBus& bus, const SourceContext& context) {
    if (attackMs_ == nullptr) {
        return;
    }
    if (!triggerResolved_) {
        if (const auto id = bus.find(trigger_)) {
            triggerId_ = *id;
            triggerResolved_ = true;
        }
    }

    float remaining = static_cast<float>(context.time.deltaTime * 1000.0);
    const float sustain = sustain_->value();
    for (int guard = 0; remaining > 0.0f && stage_ != Stage::Idle && guard < 8; ++guard) {
        switch (stage_) {
        case Stage::Attack: {
            const float attack = attackMs_->value();
            const float needed = attack <= 0.0f ? 0.0f : (1.0f - level_) * attack;
            if (remaining >= needed) {
                level_ = 1.0f;
                remaining -= needed;
                stage_ = Stage::Decay;
            } else {
                level_ += remaining / attack;
                remaining = 0.0f;
            }
            break;
        }
        case Stage::Decay: {
            const float decay = decayMs_->value();
            const float slope = decay <= 0.0f ? 0.0f : (1.0f - sustain) / decay; // per ms
            const float needed = slope <= 0.0f ? 0.0f : std::max(level_ - sustain, 0.0f) / slope;
            if (remaining >= needed) {
                level_ = sustain;
                remaining -= needed;
                stage_ = Stage::Hold;
                stageTime_ = 0.0f;
            } else {
                level_ -= remaining * slope;
                remaining = 0.0f;
            }
            break;
        }
        case Stage::Hold: {
            level_ = sustain;
            const float needed = std::max(holdMs_->value() - stageTime_, 0.0f);
            if (remaining >= needed) {
                remaining -= needed;
                stage_ = Stage::Release;
                stageTime_ = 0.0f;
            } else {
                stageTime_ += remaining;
                remaining = 0.0f;
            }
            break;
        }
        case Stage::Release: {
            const float release = releaseMs_->value();
            const float slope = (release <= 0.0f || sustain <= 0.0f) ? 0.0f : sustain / release; // per ms
            const float needed = slope <= 0.0f ? 0.0f : level_ / slope;
            if (remaining >= needed) {
                level_ = 0.0f;
                remaining = 0.0f;
                stage_ = Stage::Idle;
            } else {
                level_ -= remaining * slope;
                remaining = 0.0f;
            }
            break;
        }
        case Stage::Idle:
            break;
        }
    }
    level_ = std::clamp(level_, 0.0f, 1.0f);

    // The trigger lands at this frame's time: the new attack starts from the level reached now.
    if (triggerResolved_ && bus.event(triggerId_)) {
        stage_ = Stage::Attack;
        stageTime_ = 0.0f;
    }
    bus.set(output_, level_);
}

json EnvelopeSource::settingsToJson() const {
    return json{{"trigger", trigger_}};
}

Result<void> EnvelopeSource::settingsFromJson(const json& j) {
    if (auto r = requireObject(j, "envelope"); !r) {
        return r;
    }
    std::string trigger = trigger_;
    if (auto r = readString(j, "trigger", trigger); !r) {
        return fail("envelope '{}': {}", name_, r.error().message);
    }
    if (trigger != trigger_) {
        setTrigger(std::move(trigger));
    }
    return {};
}

std::vector<std::string> EnvelopeSource::outputs() const {
    return {"env." + name_};
}

// ============================================================================================
// NoiseSource
// ============================================================================================

NoiseSource::NoiseSource(std::string name, std::uint32_t seed)
    : Source(std::move(name))
    , seed_(seed) {}

void NoiseSource::attach(SignalBus& bus, ParameterSet& params) {
    output_ = bus.declare("noise." + name_, 0.0f, 1.0f);
    const std::string prefix = parameterPrefix();
    rate_ = addFloat(params, prefix + "rate", 1.0f, 0.01f, 100.0f, 0.01f, 20.0f);
    smoothness_ = addFloat(params, prefix + "smoothness", 1.0f, 0.0f, 1.0f);
}

void NoiseSource::detach(ParameterSet& params) {
    removeParam(params, rate_);
    removeParam(params, smoothness_);
}

void NoiseSource::update(SignalBus& bus, const SourceContext& context) {
    if (rate_ == nullptr) {
        return;
    }
    bus.set(output_, evaluate(context.time.renderTime, rate_->value(), smoothness_->value(), seed_));
}

float NoiseSource::evaluate(double time, float rate, float smoothness, std::uint32_t seed) {
    double cell = 0.0;
    const auto t = static_cast<float>(fractionalPart(time * static_cast<double>(rate), cell));
    const auto index = static_cast<std::int64_t>(cell);
    const float v0 = latticeValue(seed, index);
    const float v1 = latticeValue(seed, index + 1);
    const float blend = t * t * (3.0f - 2.0f * t);
    const float smooth = v0 + (v1 - v0) * blend;
    const float step = v0;
    const float mix = std::clamp(smoothness, 0.0f, 1.0f);
    return step + (smooth - step) * mix;
}

json NoiseSource::settingsToJson() const {
    return json{{"seed", seed_}};
}

Result<void> NoiseSource::settingsFromJson(const json& j) {
    if (auto r = requireObject(j, "noise"); !r) {
        return r;
    }
    std::uint32_t seed = seed_;
    if (auto r = readSeed(j, "seed", seed); !r) {
        return fail("noise '{}': {}", name_, r.error().message);
    }
    seed_ = seed;
    return {};
}

std::vector<std::string> NoiseSource::outputs() const {
    return {"noise." + name_};
}

// ============================================================================================
// RandomSource
// ============================================================================================

RandomSource::RandomSource(std::string name, std::string trigger, std::uint32_t seed)
    : Source(std::move(name))
    , trigger_(std::move(trigger))
    , seed_(seed) {}

void RandomSource::attach(SignalBus& bus, ParameterSet& params) {
    output_ = bus.declare("random." + name_, 0.0f, 1.0f);
    if (const auto id = bus.find(trigger_)) {
        triggerId_ = *id;
        triggerResolved_ = true;
    }
    slewMs_ = addFloat(params, parameterPrefix() + "slewMs", 0.0f, 0.0f, 5000.0f);
}

void RandomSource::detach(ParameterSet& params) {
    removeParam(params, slewMs_);
}

void RandomSource::setTrigger(std::string trigger) {
    trigger_ = std::move(trigger);
    triggerId_ = kInvalidSignal;
    triggerResolved_ = false;
}

void RandomSource::reset() {
    count_ = 0;
    target_ = 0.0f;
    value_ = 0.0f;
}

void RandomSource::update(SignalBus& bus, const SourceContext& context) {
    if (slewMs_ == nullptr) {
        return;
    }
    if (!triggerResolved_) {
        if (const auto id = bus.find(trigger_)) {
            triggerId_ = *id;
            triggerResolved_ = true;
        }
    }
    if (triggerResolved_ && bus.event(triggerId_)) {
        // The k-th trigger since reset yields the k-th value of the seeded sequence, whatever
        // happened in between (replayed from the seed; triggers are sparse so O(k) is fine).
        Rng rng(seed_);
        for (std::uint32_t i = 0; i < count_; ++i) {
            rng.nextFloat();
        }
        target_ = rng.nextFloat();
        ++count_;
    }
    const float slew = slewMs_->value();
    if (slew <= 0.0f) {
        value_ = target_;
    } else {
        value_ += (target_ - value_) * params::smoothingCoefficient(slew, context.time.deltaTime);
    }
    bus.set(output_, value_);
}

json RandomSource::settingsToJson() const {
    return json{{"trigger", trigger_}, {"seed", seed_}};
}

Result<void> RandomSource::settingsFromJson(const json& j) {
    if (auto r = requireObject(j, "random"); !r) {
        return r;
    }
    std::string trigger = trigger_;
    std::uint32_t seed = seed_;
    if (auto r = readString(j, "trigger", trigger); !r) {
        return fail("random '{}': {}", name_, r.error().message);
    }
    if (auto r = readSeed(j, "seed", seed); !r) {
        return fail("random '{}': {}", name_, r.error().message);
    }
    if (trigger != trigger_) {
        setTrigger(std::move(trigger));
    }
    seed_ = seed;
    return {};
}

std::vector<std::string> RandomSource::outputs() const {
    return {"random." + name_};
}

// ============================================================================================
// TimelineSource
// ============================================================================================

TimelineSource::TimelineSource(std::string name)
    : Source(std::move(name)) {}

void TimelineSource::attach(SignalBus& bus, ParameterSet& params) {
    output_ = bus.declare("timeline." + name_, 0.0f, 1.0f);
    const std::string prefix = parameterPrefix();
    offset_ = addFloat(params, prefix + "offset", 0.0f, -3600.0f, 3600.0f, -60.0f, 60.0f);
    scale_ = addFloat(params, prefix + "scale", 1.0f, 0.01f, 100.0f);
}

void TimelineSource::detach(ParameterSet& params) {
    removeParam(params, offset_);
    removeParam(params, scale_);
}

void TimelineSource::update(SignalBus& bus, const SourceContext& context) {
    if (offset_ == nullptr) {
        return;
    }
    const double time = (context.time.renderTime + static_cast<double>(offset_->value())) *
                        static_cast<double>(scale_->value());
    bus.set(output_, evaluate(time));
}

void TimelineSource::addKey(Keyframe key) {
    const auto pos = std::upper_bound(keys_.begin(), keys_.end(), key.time,
                                      [](double time, const Keyframe& k) { return time < k.time; });
    keys_.insert(pos, key);
}

void TimelineSource::sortKeys() {
    std::stable_sort(keys_.begin(), keys_.end(),
                     [](const Keyframe& a, const Keyframe& b) { return a.time < b.time; });
}

float TimelineSource::evaluate(double time) const {
    if (keys_.empty()) {
        return 0.0f;
    }
    if (loopLength_ > 0.0) {
        time = std::fmod(time, loopLength_);
        if (time < 0.0) {
            time += loopLength_;
        }
    }
    if (time <= keys_.front().time) {
        return keys_.front().value;
    }
    if (time >= keys_.back().time) {
        return keys_.back().value;
    }
    // Last key with key.time <= time; the segment's interpolation comes from that (left) key.
    const auto next = std::upper_bound(keys_.begin(), keys_.end(), time,
                                       [](double t, const Keyframe& k) { return t < k.time; });
    const Keyframe& right = *next;
    const Keyframe& left = *(next - 1);
    switch (left.interpolation) {
    case KeyInterpolation::Step:
        return left.value;
    case KeyInterpolation::Linear:
    case KeyInterpolation::Smooth: {
        const double span = right.time - left.time;
        auto u = static_cast<float>(span > 0.0 ? (time - left.time) / span : 1.0);
        if (left.interpolation == KeyInterpolation::Smooth) {
            u = u * u * (3.0f - 2.0f * u);
        }
        return left.value + (right.value - left.value) * u;
    }
    }
    return left.value;
}

json TimelineSource::settingsToJson() const {
    json keys = json::array();
    for (const Keyframe& key : keys_) {
        keys.push_back(json{{"time", key.time},
                            {"value", static_cast<double>(key.value)},
                            {"interp", enumToString(kInterpNames, key.interpolation)}});
    }
    return json{{"keys", std::move(keys)}, {"loopLength", loopLength_}};
}

Result<void> TimelineSource::settingsFromJson(const json& j) {
    if (auto r = requireObject(j, "timeline"); !r) {
        return r;
    }
    std::vector<Keyframe> keys = keys_;
    double loopLength = loopLength_;
    if (const auto it = j.find("keys"); it != j.end()) {
        if (!it->is_array()) {
            return fail("timeline '{}': 'keys' must be an array", name_);
        }
        keys.clear();
        for (const auto& entry : *it) {
            if (!entry.is_object()) {
                return fail("timeline '{}': keys[{}] must be an object", name_, keys.size());
            }
            const auto time = entry.find("time");
            const auto value = entry.find("value");
            if (time == entry.end() || !time->is_number() || value == entry.end() || !value->is_number()) {
                return fail("timeline '{}': keys[{}] needs numeric 'time' and 'value'", name_, keys.size());
            }
            Keyframe key;
            key.time = time->get<double>();
            key.value = value->get<float>();
            if (auto r = readEnum(entry, "interp", kInterpNames, key.interpolation); !r) {
                return fail("timeline '{}': keys[{}]: {}", name_, keys.size(), r.error().message);
            }
            keys.push_back(key);
        }
    }
    if (auto r = readDouble(j, "loopLength", loopLength); !r) {
        return fail("timeline '{}': {}", name_, r.error().message);
    }
    if (loopLength < 0.0) {
        return fail("timeline '{}': 'loopLength' must be >= 0", name_);
    }
    keys_ = std::move(keys);
    sortKeys();
    loopLength_ = loopLength;
    return {};
}

std::vector<std::string> TimelineSource::outputs() const {
    return {"timeline." + name_};
}

// ============================================================================================
// MacroSource
// ============================================================================================

MacroSource::MacroSource(std::string name)
    : Source(std::move(name)) {}

// Idempotent: knobs added since the last attach() get their signal and parameter here, existing
// ones are looked up again (declare/add return the existing objects).
void MacroSource::attach(SignalBus& bus, ParameterSet& params) {
    outputs_.resize(knobs_.size(), kInvalidSignal);
    params_.resize(knobs_.size(), nullptr);
    for (std::size_t i = 0; i < knobs_.size(); ++i) {
        outputs_[i] = bus.declare("macro." + knobs_[i], 0.0f, 1.0f);
        params_[i] = addFloat(params, "macros/" + knobs_[i], defaults_[i], 0.0f, 1.0f);
    }
}

void MacroSource::detach(ParameterSet& params) {
    for (auto*& param : params_) {
        removeParam(params, param);
    }
}

void MacroSource::update(SignalBus& bus, const SourceContext& /*context*/) {
    const std::size_t count = std::min(outputs_.size(), params_.size());
    for (std::size_t i = 0; i < count; ++i) {
        if (params_[i] != nullptr && outputs_[i] != kInvalidSignal) {
            bus.set(outputs_[i], params_[i]->value());
        }
    }
}

void MacroSource::addKnob(std::string knob, float defaultValue) {
    const auto clamped = std::clamp(defaultValue, 0.0f, 1.0f);
    for (std::size_t i = 0; i < knobs_.size(); ++i) {
        if (knobs_[i] == knob) {
            defaults_[i] = clamped;
            return;
        }
    }
    knobs_.push_back(std::move(knob));
    defaults_.push_back(clamped);
    // Keep the runtime vectors aligned; attach() fills the new slot.
    if (!outputs_.empty() || !params_.empty()) {
        outputs_.resize(knobs_.size(), kInvalidSignal);
        params_.resize(knobs_.size(), nullptr);
    }
}

json MacroSource::settingsToJson() const {
    json knobs = json::array();
    for (std::size_t i = 0; i < knobs_.size(); ++i) {
        knobs.push_back(json{{"name", knobs_[i]}, {"default", static_cast<double>(defaults_[i])}});
    }
    return json{{"knobs", std::move(knobs)}};
}

Result<void> MacroSource::settingsFromJson(const json& j) {
    if (auto r = requireObject(j, "macro"); !r) {
        return r;
    }
    const auto it = j.find("knobs");
    if (it == j.end()) {
        return {};
    }
    if (!it->is_array()) {
        return fail("macro '{}': 'knobs' must be an array", name_);
    }
    std::vector<std::string> knobs;
    std::vector<float> defaults;
    for (const auto& entry : *it) {
        if (!entry.is_object()) {
            return fail("macro '{}': knobs[{}] must be an object", name_, knobs.size());
        }
        const auto knobName = entry.find("name");
        if (knobName == entry.end() || !knobName->is_string() || knobName->get<std::string>().empty()) {
            return fail("macro '{}': knobs[{}] needs a non-empty string 'name'", name_, knobs.size());
        }
        float defaultValue = 0.5f;
        if (const auto def = entry.find("default"); def != entry.end()) {
            if (!def->is_number()) {
                return fail("macro '{}': knobs[{}] 'default' must be a number", name_, knobs.size());
            }
            defaultValue = std::clamp(def->get<float>(), 0.0f, 1.0f);
        }
        if (std::find(knobs.begin(), knobs.end(), knobName->get<std::string>()) != knobs.end()) {
            return fail("macro '{}': duplicate knob '{}'", name_, knobName->get<std::string>());
        }
        knobs.push_back(knobName->get<std::string>());
        defaults.push_back(defaultValue);
    }
    knobs_ = std::move(knobs);
    defaults_ = std::move(defaults);
    // Runtime bindings no longer match; attach() again to rebuild them.
    outputs_.clear();
    params_.clear();
    return {};
}

std::vector<std::string> MacroSource::outputs() const {
    std::vector<std::string> names;
    names.reserve(knobs_.size());
    for (const std::string& knob : knobs_) {
        names.push_back("macro." + knob);
    }
    return names;
}

// ============================================================================================
// SourceRack
// ============================================================================================

Source& SourceRack::add(std::unique_ptr<Source> source) {
    const bool attached = bus_ != nullptr && params_ != nullptr;
    for (auto& existing : sources_) {
        if (existing->kind() == source->kind() && existing->name() == source->name()) {
            if (attached) {
                existing->detach(*params_);
            }
            existing = std::move(source);
            if (attached) {
                existing->attach(*bus_, *params_);
            }
            return *existing;
        }
    }
    sources_.push_back(std::move(source));
    if (attached) {
        sources_.back()->attach(*bus_, *params_);
    }
    return *sources_.back();
}

bool SourceRack::remove(const std::string& kind, const std::string& name) {
    const auto it = std::find_if(sources_.begin(), sources_.end(), [&](const std::unique_ptr<Source>& s) {
        return s->kind() == kind && s->name() == name;
    });
    if (it == sources_.end()) {
        return false;
    }
    if (params_ != nullptr) {
        (*it)->detach(*params_);
    }
    sources_.erase(it);
    return true;
}

Source* SourceRack::find(const std::string& kind, const std::string& name) {
    for (auto& source : sources_) {
        if (source->kind() == kind && source->name() == name) {
            return source.get();
        }
    }
    return nullptr;
}

void SourceRack::attach(SignalBus& bus, ParameterSet& params) {
    if (params_ != nullptr && params_ != &params) {
        // Moving to a different parameter set: drop the parameters registered in the old one.
        for (auto& source : sources_) {
            source->detach(*params_);
        }
    }
    bus_ = &bus;
    params_ = &params;
    for (auto& source : sources_) {
        source->attach(bus, params);
    }
}

void SourceRack::detachAll() {
    if (params_ != nullptr) {
        for (auto& source : sources_) {
            source->detach(*params_);
        }
    }
    bus_ = nullptr;
    params_ = nullptr;
}

void SourceRack::update(SignalBus& bus, const SourceContext& context) {
    if (params_ == nullptr) {
        return;
    }
    for (auto& source : sources_) {
        source->update(bus, context);
    }
}

void SourceRack::reset() {
    for (auto& source : sources_) {
        source->reset();
    }
}

// Drops every source (removing their parameters) but stays attached, so later add()s register.
void SourceRack::clear() {
    if (params_ != nullptr) {
        for (auto& source : sources_) {
            source->detach(*params_);
        }
    }
    sources_.clear();
}

json SourceRack::toJson() const {
    json array = json::array();
    for (const auto& source : sources_) {
        array.push_back(
            json{{"kind", source->kind()}, {"name", source->name()}, {"settings", source->settingsToJson()}});
    }
    return array;
}

Result<void> SourceRack::fromJson(const json& j) {
    if (!j.is_array()) {
        return fail("'sources' must be an array");
    }
    std::vector<std::unique_ptr<Source>> parsed;
    for (std::size_t i = 0; i < j.size(); ++i) {
        const json& entry = j[i];
        if (!entry.is_object()) {
            return fail("sources[{}] must be an object", i);
        }
        const auto kind = entry.find("kind");
        const auto name = entry.find("name");
        if (kind == entry.end() || !kind->is_string()) {
            return fail("sources[{}] needs a string 'kind'", i);
        }
        if (name == entry.end() || !name->is_string() || name->get<std::string>().empty()) {
            return fail("sources[{}] needs a non-empty string 'name'", i);
        }
        auto source = create(kind->get<std::string>(), name->get<std::string>());
        if (!source) {
            log::warn("sources[{}]: unknown source kind '{}' skipped", i, kind->get<std::string>());
            continue;
        }
        if (const auto settings = entry.find("settings"); settings != entry.end()) {
            if (auto r = source->settingsFromJson(*settings); !r) {
                return fail("sources[{}]: {}", i, r.error().message);
            }
        }
        // Later duplicates replace earlier ones, as add() does.
        const auto duplicate =
            std::find_if(parsed.begin(), parsed.end(), [&](const std::unique_ptr<Source>& s) {
                return s->kind() == source->kind() && s->name() == source->name();
            });
        if (duplicate != parsed.end()) {
            *duplicate = std::move(source);
        } else {
            parsed.push_back(std::move(source));
        }
    }
    clear();
    for (auto& source : parsed) {
        add(std::move(source));
    }
    return {};
}

std::unique_ptr<Source> SourceRack::create(const std::string& kind, const std::string& name) {
    if (kind == "lfo") {
        return std::make_unique<LfoSource>(name);
    }
    if (kind == "envelope") {
        return std::make_unique<EnvelopeSource>(name);
    }
    if (kind == "noise") {
        return std::make_unique<NoiseSource>(name);
    }
    if (kind == "random") {
        return std::make_unique<RandomSource>(name);
    }
    if (kind == "timeline") {
        return std::make_unique<TimelineSource>(name);
    }
    if (kind == "macro") {
        return std::make_unique<MacroSource>(name);
    }
    return nullptr;
}

} // namespace avgen::signals
