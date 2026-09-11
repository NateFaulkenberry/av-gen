#include "entity/behavior.hpp"

#include "core/log.hpp"
#include "core/noise.hpp"
#include "entity/entity.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace avgen::entity {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 2.0f * kPi;
constexpr float kDegrees = 180.0f / kPi;

float readFloat(const nlohmann::json* j, const char* key, float fallback) {
    if (j == nullptr || !j->is_object() || !j->contains(key) || !(*j)[key].is_number()) {
        return fallback;
    }
    return (*j)[key].get<float>();
}

std::string readString(const nlohmann::json* j, const char* key, std::string fallback) {
    if (j == nullptr || !j->is_object() || !j->contains(key) || !(*j)[key].is_string()) {
        return fallback;
    }
    return (*j)[key].get<std::string>();
}

std::vector<std::string> readStrings(const nlohmann::json* j, const char* key) {
    std::vector<std::string> out;
    if (j == nullptr || !j->is_object() || !j->contains(key)) {
        return out;
    }
    const nlohmann::json& v = (*j)[key];
    if (v.is_string()) {
        out.push_back(v.get<std::string>());
    } else if (v.is_array()) {
        for (const auto& item : v) {
            if (item.is_string()) {
                out.push_back(item.get<std::string>());
            }
        }
    }
    return out;
}

params::ParamDesc<float> floatDesc(std::string path, float def, float lo, float hi) {
    return params::ParamDesc<float>{.path = std::move(path), .defaultValue = def, .hardMin = lo, .hardMax = hi};
}

// Smallest signed angle from `from` to `to`, in radians.
float angleDelta(float from, float to) {
    float d = std::fmod(to - from + kPi, kTwoPi);
    if (d < 0.0f) {
        d += kTwoPi;
    }
    return d - kPi;
}

// Two octaves of value-noise over time, decorrelated per channel. Deterministic (a pure function of
// time and seed), aperiodic, and -- unlike a sine -- it never lands on the same value at the same
// phase twice, which is the difference between a craft that hangs in the air and one that bobs.
glm::vec3 slowNoise(double time, float rate, std::uint32_t seed, float offset) {
    const auto t = static_cast<float>(time) * rate;
    const glm::vec3 a = noise::fbm3Vec(glm::vec3(t, offset, 0.0f), seed);
    const glm::vec3 b = noise::fbm3Vec(glm::vec3(t * 0.37f, offset + 11.0f, 0.0f), seed ^ 0x9E3779B9u);
    return a * 0.62f + b * 0.38f;
}

// A one-pole coefficient for a time constant in milliseconds, frame-rate independent. The same
// shape params::smoothingCoefficient uses; repeated here so behaviours do not depend on the
// modulation chain's internals.
float lerpRate(float ms, double dt) {
    if (ms <= 0.0f) {
        return 1.0f;
    }
    return 1.0f - std::exp(-static_cast<float>(dt) / (ms * 0.001f));
}

// ---- hover -----------------------------------------------------------------------------------
//
// Low-frequency vertical float with a matching tilt, both driven by the same noise field so the
// craft leans the way it drifts instead of nodding independently of it. The brief for this one is
// "cinematic, massive, mysterious": slow, small, and never repeating.
class Hover final : public IBehavior {
public:
    explicit Hover(const nlohmann::json* s)
        : amplitudeDefault_(readFloat(s, "amplitude", 0.6f)),
          rateDefault_(readFloat(s, "rate", 0.08f)),
          tiltDefault_(readFloat(s, "tilt", 1.6f)),
          seedOffset_(readFloat(s, "phase", 0.0f)) {}

    [[nodiscard]] std::string_view kind() const override { return "hover"; }

    void registerParameters(params::ParameterSet& params, const std::string& prefix) override {
        amplitude_ = &params.add(floatDesc(prefix + "amplitude", amplitudeDefault_, 0.0f, 200.0f));
        rate_ = &params.add(floatDesc(prefix + "rate", rateDefault_, 0.0f, 8.0f));
        tilt_ = &params.add(floatDesc(prefix + "tilt", tiltDefault_, 0.0f, 90.0f));
        paths_ = {prefix + "amplitude", prefix + "rate", prefix + "tilt"};
    }
    void collectParameterPaths(std::vector<std::string>& out) const override {
        out.insert(out.end(), paths_.begin(), paths_.end());
    }
    void reset(Rng& rng) override { seed_ = rng.nextU32(); }

    void update(const BehaviorContext& ctx, EntityState& state, MotionOffset& motion) override {
        const float amplitude = amplitude_ != nullptr ? amplitude_->value() : amplitudeDefault_;
        const float rate = rate_ != nullptr ? rate_->value() : rateDefault_;
        const float tilt = tilt_ != nullptr ? tilt_->value() : tiltDefault_;
        const glm::vec3 n = slowNoise(ctx.time, rate, seed_, seedOffset_);
        motion.position.y += n.x * amplitude;
        motion.rotation.x += n.y * tilt;
        motion.rotation.z += n.z * tilt;
        (void)state;
    }

private:
    float amplitudeDefault_, rateDefault_, tiltDefault_, seedOffset_;
    params::Parameter<float>* amplitude_ = nullptr;
    params::Parameter<float>* rate_ = nullptr;
    params::Parameter<float>* tilt_ = nullptr;
    std::vector<std::string> paths_;
    std::uint32_t seed_ = 1u;
};

// ---- drift -----------------------------------------------------------------------------------
//
// Lateral wander inside a radius, on its own noise field. Separate from `hover` because vertical
// and horizontal motion have different scales and different rates on anything that flies, and
// folding them into one knob makes both wrong.
class Drift final : public IBehavior {
public:
    explicit Drift(const nlohmann::json* s)
        : radiusDefault_(readFloat(s, "radius", 2.0f)), rateDefault_(readFloat(s, "rate", 0.045f)) {}

    [[nodiscard]] std::string_view kind() const override { return "drift"; }

    void registerParameters(params::ParameterSet& params, const std::string& prefix) override {
        radius_ = &params.add(floatDesc(prefix + "radius", radiusDefault_, 0.0f, 500.0f));
        rate_ = &params.add(floatDesc(prefix + "rate", rateDefault_, 0.0f, 8.0f));
        paths_ = {prefix + "radius", prefix + "rate"};
    }
    void collectParameterPaths(std::vector<std::string>& out) const override {
        out.insert(out.end(), paths_.begin(), paths_.end());
    }
    void reset(Rng& rng) override {
        seed_ = rng.nextU32();
        previous_ = glm::vec2(0.0f);
        started_ = false;
    }

    void update(const BehaviorContext& ctx, EntityState& state, MotionOffset& motion) override {
        const float radius = radius_ != nullptr ? radius_->value() : radiusDefault_;
        const float rate = rate_ != nullptr ? rate_->value() : rateDefault_;
        const glm::vec3 n = slowNoise(ctx.time, rate, seed_, 3.5f);
        const glm::vec2 offset(n.x * radius, n.z * radius);
        motion.position.x += offset.x;
        motion.position.z += offset.y;
        // The drift velocity is what `bank` leans into. Publishing it on the state rather than
        // recomputing it there keeps the two behaviours agreeing about which way the craft is
        // going even when an author changes the radius mid-shot.
        if (started_ && ctx.dt > 1e-6) {
            const glm::vec2 v = (offset - previous_) / static_cast<float>(ctx.dt);
            state.speed = glm::length(v);
            driftVelocity_ = v;
        }
        previous_ = offset;
        started_ = true;
    }

    [[nodiscard]] glm::vec2 velocity() const { return driftVelocity_; }

private:
    float radiusDefault_, rateDefault_;
    params::Parameter<float>* radius_ = nullptr;
    params::Parameter<float>* rate_ = nullptr;
    std::vector<std::string> paths_;
    std::uint32_t seed_ = 1u;
    glm::vec2 previous_{0.0f};
    glm::vec2 driftVelocity_{0.0f};
    bool started_ = false;
};

// ---- bank ------------------------------------------------------------------------------------
//
// Lean into the direction of travel. This is the cheapest thing that makes a floating object read
// as having mass: a craft that translates without leaning looks like a sprite being slid across
// the frame. Smoothed, because the lean should lag the movement, not track it.
class Bank final : public IBehavior {
public:
    explicit Bank(const nlohmann::json* s)
        : degreesDefault_(readFloat(s, "degrees", 6.0f)),
          responseDefault_(readFloat(s, "responseMs", 900.0f)) {}

    [[nodiscard]] std::string_view kind() const override { return "bank"; }

    void registerParameters(params::ParameterSet& params, const std::string& prefix) override {
        degrees_ = &params.add(floatDesc(prefix + "degrees", degreesDefault_, 0.0f, 90.0f));
        response_ = &params.add(floatDesc(prefix + "responseMs", responseDefault_, 1.0f, 60000.0f));
        paths_ = {prefix + "degrees", prefix + "responseMs"};
    }
    void collectParameterPaths(std::vector<std::string>& out) const override {
        out.insert(out.end(), paths_.begin(), paths_.end());
    }
    void reset(Rng&) override {
        lean_ = glm::vec2(0.0f);
        previous_ = glm::vec3(0.0f);
        started_ = false;
    }

    void update(const BehaviorContext& ctx, EntityState& state, MotionOffset& motion) override {
        const float degrees = degrees_ != nullptr ? degrees_->value() : degreesDefault_;
        const float responseMs = response_ != nullptr ? response_->value() : responseDefault_;
        // Whatever the behaviours before this one produced, including navigation travel: bank
        // reads the accumulated motion rather than any one source, so it leans into a walk and a
        // drift alike.
        const glm::vec3 here = motion.position + state.travel;
        glm::vec2 target(0.0f);
        if (started_ && ctx.dt > 1e-6) {
            const glm::vec3 v = (here - previous_) / static_cast<float>(ctx.dt);
            target = glm::vec2(v.x, v.z) * degrees;
        }
        previous_ = here;
        started_ = true;
        const float k = lerpRate(responseMs, ctx.dt);
        lean_ += (target - lean_) * k;
        const float limit = std::max(degrees, 0.0f);
        lean_ = glm::clamp(lean_, glm::vec2(-limit), glm::vec2(limit));
        // Roll about Z for sideways travel, pitch about X for forward travel, nose down into the
        // direction of movement.
        motion.rotation.z += -lean_.x;
        motion.rotation.x += lean_.y;
    }

private:
    float degreesDefault_, responseDefault_;
    params::Parameter<float>* degrees_ = nullptr;
    params::Parameter<float>* response_ = nullptr;
    std::vector<std::string> paths_;
    glm::vec2 lean_{0.0f};
    glm::vec3 previous_{0.0f};
    bool started_ = false;
};

// ---- spin ------------------------------------------------------------------------------------
//
// A yaw *rate* that events push and damping pulls back, rather than a constant rotation. The
// difference is the whole point: a constant spin is a turntable, and an object that accelerates on
// a beat and coasts between them is reacting to the music. `baseRate` is deliberately 0 by default.
class Spin final : public IBehavior {
public:
    explicit Spin(const nlohmann::json* s)
        : signal_(readString(s, "signal", "audio.beat")),
          baseDefault_(readFloat(s, "baseRate", 0.0f)),
          impulseDefault_(readFloat(s, "impulse", 22.0f)),
          dampingDefault_(readFloat(s, "damping", 0.55f)),
          maxDefault_(readFloat(s, "maxRate", 90.0f)) {}

    [[nodiscard]] std::string_view kind() const override { return "spin"; }

    void registerParameters(params::ParameterSet& params, const std::string& prefix) override {
        base_ = &params.add(floatDesc(prefix + "baseRate", baseDefault_, -720.0f, 720.0f));
        impulse_ = &params.add(floatDesc(prefix + "impulse", impulseDefault_, -720.0f, 720.0f));
        damping_ = &params.add(floatDesc(prefix + "damping", dampingDefault_, 0.0f, 20.0f));
        max_ = &params.add(floatDesc(prefix + "maxRate", maxDefault_, 0.0f, 2000.0f));
        paths_ = {prefix + "baseRate", prefix + "impulse", prefix + "damping", prefix + "maxRate"};
    }
    void collectParameterPaths(std::vector<std::string>& out) const override {
        out.insert(out.end(), paths_.begin(), paths_.end());
    }
    void reset(Rng&) override {
        angle_ = 0.0f;
        rate_ = 0.0f;
    }

    void update(const BehaviorContext& ctx, EntityState& state, MotionOffset& motion) override {
        const float base = base_ != nullptr ? base_->value() : baseDefault_;
        const float impulse = impulse_ != nullptr ? impulse_->value() : impulseDefault_;
        const float damping = damping_ != nullptr ? damping_->value() : dampingDefault_;
        const float maxRate = max_ != nullptr ? max_->value() : maxDefault_;
        if (ctx.event(signal_)) {
            rate_ += impulse * std::max(ctx.signal(signal_), 0.25f);
        }
        // Exponential return to the base rate; frame-rate independent so a 30 fps offline render
        // and a 120 fps window decelerate identically.
        const float k = 1.0f - std::exp(-damping * static_cast<float>(ctx.dt));
        rate_ += (base - rate_) * k;
        rate_ = std::clamp(rate_, -maxRate, maxRate);
        angle_ += rate_ * static_cast<float>(ctx.dt);
        angle_ = std::fmod(angle_, 360.0f);
        motion.rotation.y += angle_;
        state.turnRate = rate_ / kDegrees;
    }

private:
    std::string signal_;
    float baseDefault_, impulseDefault_, dampingDefault_, maxDefault_;
    params::Parameter<float>* base_ = nullptr;
    params::Parameter<float>* impulse_ = nullptr;
    params::Parameter<float>* damping_ = nullptr;
    params::Parameter<float>* max_ = nullptr;
    std::vector<std::string> paths_;
    float angle_ = 0.0f;
    float rate_ = 0.0f;
};

// ---- wander ----------------------------------------------------------------------------------
//
// Pick somewhere navigable, walk there, pause, repeat. The navigation layer answers "may I stand
// here" and "which way do I go"; this decides when to ask. Seeded throughout: the same scene, the
// same seed and the same frame produce the same walk, which is what an offline render needs.
class Wander final : public IBehavior {
public:
    explicit Wander(const nlohmann::json* s)
        : speedDefault_(readFloat(s, "speed", 1.6f)),
          runSpeedDefault_(readFloat(s, "runSpeed", 4.0f)),
          turnDefault_(readFloat(s, "turnRate", 140.0f)),
          arriveDefault_(readFloat(s, "arrive", 1.2f)),
          minRangeDefault_(readFloat(s, "minRange", 8.0f)),
          maxRangeDefault_(readFloat(s, "maxRange", 30.0f)),
          pauseMinDefault_(readFloat(s, "pauseMin", 1.5f)),
          pauseMaxDefault_(readFloat(s, "pauseMax", 6.0f)) {}

    [[nodiscard]] std::string_view kind() const override { return "wander"; }

    void registerParameters(params::ParameterSet& params, const std::string& prefix) override {
        speed_ = &params.add(floatDesc(prefix + "speed", speedDefault_, 0.0f, 40.0f));
        runSpeed_ = &params.add(floatDesc(prefix + "runSpeed", runSpeedDefault_, 0.0f, 60.0f));
        turn_ = &params.add(floatDesc(prefix + "turnRate", turnDefault_, 1.0f, 1440.0f));
        arrive_ = &params.add(floatDesc(prefix + "arrive", arriveDefault_, 0.05f, 20.0f));
        minRange_ = &params.add(floatDesc(prefix + "minRange", minRangeDefault_, 0.0f, 500.0f));
        maxRange_ = &params.add(floatDesc(prefix + "maxRange", maxRangeDefault_, 0.5f, 1000.0f));
        pauseMin_ = &params.add(floatDesc(prefix + "pauseMin", pauseMinDefault_, 0.0f, 300.0f));
        pauseMax_ = &params.add(floatDesc(prefix + "pauseMax", pauseMaxDefault_, 0.0f, 600.0f));
        paths_ = {prefix + "speed",    prefix + "runSpeed", prefix + "turnRate", prefix + "arrive",
                  prefix + "minRange", prefix + "maxRange", prefix + "pauseMin", prefix + "pauseMax"};
    }
    void collectParameterPaths(std::vector<std::string>& out) const override {
        out.insert(out.end(), paths_.begin(), paths_.end());
    }
    void reset(Rng& rng) override {
        hasDestination_ = false;
        pause_ = rng.range(0.0f, 2.0f);
    }

    void update(const BehaviorContext& ctx, EntityState& state, MotionOffset& motion) override {
        (void)motion;
        const float speed = speed_ != nullptr ? speed_->value() : speedDefault_;
        const float arrive = arrive_ != nullptr ? arrive_->value() : arriveDefault_;
        const float turnRate = (turn_ != nullptr ? turn_->value() : turnDefault_) / kDegrees;
        const glm::vec3 here = state.position();
        const glm::vec2 flat(here.x, here.z);

        if (pause_ > 0.0f) {
            pause_ -= static_cast<float>(ctx.dt);
            state.speed = 0.0f;
            if (state.activity == Activity::Walk || state.activity == Activity::Run) {
                state.activity = Activity::Idle;
            }
            return;
        }
        if (!hasDestination_) {
            const float lo = minRange_ != nullptr ? minRange_->value() : minRangeDefault_;
            const float hi = maxRange_ != nullptr ? maxRange_->value() : maxRangeDefault_;
            if (ctx.nav != nullptr && ctx.rng != nullptr &&
                ctx.nav->pickDestination(*ctx.rng, flat, lo, hi, destination_)) {
                hasDestination_ = true;
            } else {
                // Nowhere to go. Wait a beat and ask again rather than retrying every frame: a
                // character boxed in by terrain should stand still, not burn the frame on
                // rejection sampling.
                pause_ = 1.0f;
                return;
            }
        }

        const glm::vec2 toGoal = destination_ - flat;
        const float distance = glm::length(toGoal);
        if (distance <= arrive) {
            hasDestination_ = false;
            const float lo = pauseMin_ != nullptr ? pauseMin_->value() : pauseMinDefault_;
            const float hi = pauseMax_ != nullptr ? pauseMax_->value() : pauseMaxDefault_;
            pause_ = ctx.rng != nullptr ? ctx.rng->range(std::min(lo, hi), std::max(lo, hi)) : lo;
            state.speed = 0.0f;
            state.activity = Activity::Idle;
            return;
        }

        glm::vec2 direction = toGoal / distance;
        if (ctx.nav != nullptr && ctx.nav->valid()) {
            const glm::vec2 steered = ctx.nav->steer(flat, destination_, std::max(speed * 1.5f, 2.0f));
            if (glm::length(steered) > 0.5f) {
                direction = steered;
            } else {
                // Every way out is blocked. Drop the destination rather than grinding into a
                // hillside; the next pick will be somewhere else.
                hasDestination_ = false;
                pause_ = 0.5f;
                return;
            }
        }

        // Turn towards the heading before travelling along it, and travel at the fraction of full
        // speed that the facing error allows -- so a character pivots rather than strafing.
        const float wanted = std::atan2(direction.x, direction.y); // direction is XZ in a vec2
        const float delta = angleDelta(state.yaw, wanted);
        const float step = turnRate * static_cast<float>(ctx.dt);
        state.yaw += std::clamp(delta, -step, step);
        state.turnRate = std::clamp(delta, -step, step) / std::max(static_cast<float>(ctx.dt), 1e-4f);

        const float alignment = std::max(0.0f, std::cos(angleDelta(state.yaw, wanted)));
        const float travelSpeed = std::min(speed * alignment, distance / std::max(static_cast<float>(ctx.dt), 1e-4f));
        const glm::vec2 heading(std::sin(state.yaw), std::cos(state.yaw));
        const glm::vec2 move = heading * travelSpeed * static_cast<float>(ctx.dt);
        state.travel.x += move.x;
        state.travel.z += move.y;
        state.speed = travelSpeed;
        const float runSpeed = runSpeed_ != nullptr ? runSpeed_->value() : runSpeedDefault_;
        state.activity = travelSpeed > runSpeed * 0.75f ? Activity::Run
                         : travelSpeed > 0.05f          ? Activity::Walk
                                                        : Activity::Turn;
        // Stay on the ground. The navigator's height query is the same one the terrain mesh was
        // built from, so a walker never floats above or sinks into the surface it is standing on.
        if (ctx.nav != nullptr && ctx.nav->valid()) {
            const glm::vec3 p = state.position();
            state.travel.y = ctx.nav->groundHeight(glm::vec2(p.x, p.z)) - state.anchor.y;
        }
    }

private:
    float speedDefault_, runSpeedDefault_, turnDefault_, arriveDefault_;
    float minRangeDefault_, maxRangeDefault_, pauseMinDefault_, pauseMaxDefault_;
    params::Parameter<float>* speed_ = nullptr;
    params::Parameter<float>* runSpeed_ = nullptr;
    params::Parameter<float>* turn_ = nullptr;
    params::Parameter<float>* arrive_ = nullptr;
    params::Parameter<float>* minRange_ = nullptr;
    params::Parameter<float>* maxRange_ = nullptr;
    params::Parameter<float>* pauseMin_ = nullptr;
    params::Parameter<float>* pauseMax_ = nullptr;
    std::vector<std::string> paths_;
    glm::vec2 destination_{0.0f};
    bool hasDestination_ = false;
    float pause_ = 0.0f;
};

// ---- lookAt ----------------------------------------------------------------------------------
//
// Turn to face something, at a limited rate. Reads `state.lookTarget` when another behaviour has
// set one this frame, and falls back to a fixed target named in the scene file. Writes yaw rather
// than a rotation offset so navigation and facing agree about which way the body points.
class LookAt final : public IBehavior {
public:
    explicit LookAt(const nlohmann::json* s)
        : target_(readString(s, "target", "")),
          turnDefault_(readFloat(s, "turnRate", 70.0f)),
          weightDefault_(readFloat(s, "weight", 1.0f)) {}

    [[nodiscard]] std::string_view kind() const override { return "lookAt"; }

    void registerParameters(params::ParameterSet& params, const std::string& prefix) override {
        turn_ = &params.add(floatDesc(prefix + "turnRate", turnDefault_, 0.0f, 1440.0f));
        weight_ = &params.add(floatDesc(prefix + "weight", weightDefault_, 0.0f, 1.0f));
        paths_ = {prefix + "turnRate", prefix + "weight"};
    }
    void collectParameterPaths(std::vector<std::string>& out) const override {
        out.insert(out.end(), paths_.begin(), paths_.end());
    }
    void reset(Rng&) override {}

    void update(const BehaviorContext& ctx, EntityState& state, MotionOffset& motion) override {
        (void)motion;
        glm::vec3 point{0.0f};
        bool have = state.hasLookTarget;
        if (have) {
            point = state.lookTarget;
        } else if (!target_.empty() && ctx.world != nullptr) {
            have = ctx.world->pointOfInterest(target_, point);
            if (have) {
                state.lookTarget = point;
                state.hasLookTarget = true;
            }
        }
        if (!have) {
            return;
        }
        const float weight = weight_ != nullptr ? weight_->value() : weightDefault_;
        if (weight <= 0.0f) {
            return;
        }
        const glm::vec3 here = state.position();
        const glm::vec2 delta(point.x - here.x, point.z - here.z);
        if (glm::length(delta) < 1e-3f) {
            return;
        }
        const float wanted = std::atan2(delta.x, delta.y);
        const float turnRate = (turn_ != nullptr ? turn_->value() : turnDefault_) / kDegrees;
        const float step = turnRate * weight * static_cast<float>(ctx.dt);
        const float d = angleDelta(state.yaw, wanted);
        state.yaw += std::clamp(d, -step, step);
        if (std::abs(d) > 0.05f && state.activity == Activity::Idle) {
            state.activity = Activity::Turn;
        }
    }

private:
    std::string target_;
    float turnDefault_, weightDefault_;
    params::Parameter<float>* turn_ = nullptr;
    params::Parameter<float>* weight_ = nullptr;
    std::vector<std::string> paths_;
};

// ---- interest --------------------------------------------------------------------------------
//
// What makes a character read as inhabited rather than driven: it stops for no reason anyone can
// see, looks at something, and carries on. Probabilistic with randomised dwell times, and seeded
// so an offline render of the same second is the same second every time.
//
// A strong audio event turns it towards whatever it finds interesting -- which is the reusable
// shape of "react to the music" for any character, not a rule about this one.
class Interest final : public IBehavior {
public:
    explicit Interest(const nlohmann::json* s)
        : signal_(readString(s, "signal", "music.impact")),
          subjects_(readStrings(s, "subjects")),
          observeDefault_(readFloat(s, "observeChance", 0.45f)),
          minDwellDefault_(readFloat(s, "minDwell", 2.0f)),
          maxDwellDefault_(readFloat(s, "maxDwell", 7.0f)),
          thresholdDefault_(readFloat(s, "alertThreshold", 0.45f)),
          decayDefault_(readFloat(s, "reactionDecay", 1.4f)) {}

    [[nodiscard]] std::string_view kind() const override { return "interest"; }

    void registerParameters(params::ParameterSet& params, const std::string& prefix) override {
        observe_ = &params.add(floatDesc(prefix + "observeChance", observeDefault_, 0.0f, 1.0f));
        minDwell_ = &params.add(floatDesc(prefix + "minDwell", minDwellDefault_, 0.0f, 300.0f));
        maxDwell_ = &params.add(floatDesc(prefix + "maxDwell", maxDwellDefault_, 0.0f, 600.0f));
        threshold_ = &params.add(floatDesc(prefix + "alertThreshold", thresholdDefault_, 0.0f, 4.0f));
        decay_ = &params.add(floatDesc(prefix + "reactionDecay", decayDefault_, 0.01f, 40.0f));
        paths_ = {prefix + "observeChance", prefix + "minDwell", prefix + "maxDwell",
                  prefix + "alertThreshold", prefix + "reactionDecay"};
    }
    void collectParameterPaths(std::vector<std::string>& out) const override {
        out.insert(out.end(), paths_.begin(), paths_.end());
    }
    void reset(Rng& rng) override {
        observing_ = false;
        dwell_ = rng.range(0.5f, 3.0f);
        subject_ = subjects_.empty() ? 0 : rng.nextU32() % static_cast<std::uint32_t>(subjects_.size());
        reaction_ = 0.0f;
    }

    void update(const BehaviorContext& ctx, EntityState& state, MotionOffset& motion) override {
        (void)motion;
        const float decay = decay_ != nullptr ? decay_->value() : decayDefault_;
        reaction_ = std::max(0.0f, reaction_ - decay * static_cast<float>(ctx.dt));

        // A strong event interrupts whatever was happening and points the character at it. The
        // threshold is a parameter so it is itself keyframeable: what counts as startling is a
        // direction an editor gives, not a constant this file gets to decide.
        const float threshold = threshold_ != nullptr ? threshold_->value() : thresholdDefault_;
        if (ctx.event(signal_) && ctx.signal(signal_) >= threshold) {
            reaction_ = std::min(1.0f, ctx.signal(signal_));
            observing_ = true;
            dwell_ = pickDwell(ctx);
            if (!subjects_.empty() && ctx.rng != nullptr) {
                subject_ = ctx.rng->nextU32() % static_cast<std::uint32_t>(subjects_.size());
            }
            state.activity = Activity::React;
        }

        dwell_ -= static_cast<float>(ctx.dt);
        if (dwell_ <= 0.0f) {
            const float chance = observe_ != nullptr ? observe_->value() : observeDefault_;
            const float roll = ctx.rng != nullptr ? ctx.rng->nextFloat() : 0.0f;
            observing_ = roll < chance;
            dwell_ = pickDwell(ctx);
            if (observing_ && !subjects_.empty() && ctx.rng != nullptr) {
                subject_ = ctx.rng->nextU32() % static_cast<std::uint32_t>(subjects_.size());
            }
        }

        state.reaction = std::max(state.reaction, reaction_);
        if (!observing_) {
            return;
        }
        // While observing, hold still and attend to the subject. `lookAt` does the turning; this
        // only says what is worth turning towards, which is the division that lets either be
        // replaced without touching the other.
        state.speed = 0.0f;
        if (state.activity != Activity::React) {
            state.activity = Activity::Observe;
        }
        if (subjects_.empty() || ctx.world == nullptr) {
            return;
        }
        glm::vec3 point{0.0f};
        if (ctx.world->pointOfInterest(subjects_[subject_], point)) {
            state.lookTarget = point;
            state.hasLookTarget = true;
        }
    }

private:
    [[nodiscard]] float pickDwell(const BehaviorContext& ctx) const {
        const float lo = minDwell_ != nullptr ? minDwell_->value() : minDwellDefault_;
        const float hi = maxDwell_ != nullptr ? maxDwell_->value() : maxDwellDefault_;
        if (ctx.rng == nullptr) {
            return lo;
        }
        return ctx.rng->range(std::min(lo, hi), std::max(lo, hi));
    }

    std::string signal_;
    std::vector<std::string> subjects_;
    float observeDefault_, minDwellDefault_, maxDwellDefault_, thresholdDefault_, decayDefault_;
    params::Parameter<float>* observe_ = nullptr;
    params::Parameter<float>* minDwell_ = nullptr;
    params::Parameter<float>* maxDwell_ = nullptr;
    params::Parameter<float>* threshold_ = nullptr;
    params::Parameter<float>* decay_ = nullptr;
    std::vector<std::string> paths_;
    bool observing_ = false;
    float dwell_ = 0.0f;
    float reaction_ = 0.0f;
    std::uint32_t subject_ = 0;
};

// ---- orbit -----------------------------------------------------------------------------------
//
// Travel slowly around a named point. Separate from `drift` because a craft holding station over
// something is a composed shot and a craft wandering is not, and an author wants to choose.
class Orbit final : public IBehavior {
public:
    explicit Orbit(const nlohmann::json* s)
        : around_(readString(s, "around", "")),
          radiusDefault_(readFloat(s, "radius", 12.0f)),
          rateDefault_(readFloat(s, "rate", 2.0f)),
          phaseDefault_(readFloat(s, "phase", 0.0f)) {}

    [[nodiscard]] std::string_view kind() const override { return "orbit"; }

    void registerParameters(params::ParameterSet& params, const std::string& prefix) override {
        radius_ = &params.add(floatDesc(prefix + "radius", radiusDefault_, 0.0f, 2000.0f));
        rate_ = &params.add(floatDesc(prefix + "rate", rateDefault_, -360.0f, 360.0f));
        phase_ = &params.add(floatDesc(prefix + "phase", phaseDefault_, -360.0f, 360.0f));
        paths_ = {prefix + "radius", prefix + "rate", prefix + "phase"};
    }
    void collectParameterPaths(std::vector<std::string>& out) const override {
        out.insert(out.end(), paths_.begin(), paths_.end());
    }
    void reset(Rng&) override { angle_ = 0.0f; }

    void update(const BehaviorContext& ctx, EntityState& state, MotionOffset& motion) override {
        const float radius = radius_ != nullptr ? radius_->value() : radiusDefault_;
        const float rate = rate_ != nullptr ? rate_->value() : rateDefault_;
        const float phase = phase_ != nullptr ? phase_->value() : phaseDefault_;
        angle_ += rate * static_cast<float>(ctx.dt);
        const float a = (angle_ + phase) / kDegrees;
        glm::vec3 centre = state.anchor;
        if (!around_.empty() && ctx.world != nullptr) {
            glm::vec3 found{0.0f};
            if (ctx.world->pointOfInterest(around_, found)) {
                centre = found;
            }
        }
        const glm::vec3 offset(std::cos(a) * radius, 0.0f, std::sin(a) * radius);
        motion.position += (centre - state.anchor) + offset;
    }

private:
    std::string around_;
    float radiusDefault_, rateDefault_, phaseDefault_;
    params::Parameter<float>* radius_ = nullptr;
    params::Parameter<float>* rate_ = nullptr;
    params::Parameter<float>* phase_ = nullptr;
    std::vector<std::string> paths_;
    float angle_ = 0.0f;
};

} // namespace

float BehaviorContext::signal(std::string_view name) const {
    if (bus == nullptr) {
        return 0.0f;
    }
    const auto id = bus->find(name);
    return id ? bus->value(*id) : 0.0f;
}

bool BehaviorContext::event(std::string_view name) const {
    if (bus == nullptr) {
        return false;
    }
    const auto id = bus->find(name);
    return id && bus->event(*id);
}

std::vector<std::string_view> behaviorKinds() {
    return {"hover", "drift", "bank", "spin", "wander", "lookAt", "interest", "orbit"};
}

std::unique_ptr<IBehavior> makeBehavior(std::string_view kind, const nlohmann::json* settings) {
    if (kind == "hover") {
        return std::make_unique<Hover>(settings);
    }
    if (kind == "drift") {
        return std::make_unique<Drift>(settings);
    }
    if (kind == "bank") {
        return std::make_unique<Bank>(settings);
    }
    if (kind == "spin") {
        return std::make_unique<Spin>(settings);
    }
    if (kind == "wander") {
        return std::make_unique<Wander>(settings);
    }
    if (kind == "lookAt") {
        return std::make_unique<LookAt>(settings);
    }
    if (kind == "interest") {
        return std::make_unique<Interest>(settings);
    }
    if (kind == "orbit") {
        return std::make_unique<Orbit>(settings);
    }
    return nullptr;
}

const char* activityName(Activity activity) {
    switch (activity) {
    case Activity::Idle: return "idle";
    case Activity::Walk: return "walk";
    case Activity::Run: return "run";
    case Activity::Turn: return "turn";
    case Activity::Observe: return "observe";
    case Activity::React: return "react";
    }
    return "idle";
}

} // namespace avgen::entity
