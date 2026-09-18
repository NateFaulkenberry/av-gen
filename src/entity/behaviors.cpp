#include "entity/behavior.hpp"

#include "core/log.hpp"
#include "core/noise.hpp"
#include "entity/entity.hpp"
#include "entity/airborne.hpp"
#include "entity/grounding.hpp"
#include "entity/decision.hpp"
#include "entity/nav_grid.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <span>

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

// ADR-162's walk back onto the navigable set, shared by every behaviour that steers (ADR-240).
//
// The situation it exists for: a body ends up just outside the navigable set -- a step down, the
// edge of the water, a slope that tipped over the limit on the way down it. From there `pathClear`
// fails in *every* direction, because it samples navigability from the body's own position
// outward, so the steering fan finds nothing however wide it reaches. The planner is no help
// either: it snaps an unwalkable start to the nearest walkable cell and returns a perfectly good
// route from a place the body is not, and nobody ever tells the body where it snapped to.
//
// So the body is walked toward the nearest point the *navigator* calls navigable, found by an
// outward spiral and clamped to a walking step so it reads as picking its way back onto the path
// rather than as a teleport. The predicate is the navigator's rather than the grid's on purpose:
// the grid is baked once from one sample per cell and the obstacle set moves afterwards, so
// grid-walkable is a claim about build time and `navigable` is a claim about now. An escape that
// satisfies the wrong predicate escapes to somewhere it is still stuck.
//
// **It turns onto the way out before travelling along it**, exactly as the ordinary walk does. The
// first version of this translated the body along the escape vector while leaving `yaw` untouched,
// and -- because it also wrote `speed` and `Activity::Walk` -- told the animation layer the body
// was walking while it did. That is the one branch in the whole locomotion path where the facing
// did not follow the body, and ADR-204's decomposition mistook its steps for pushes out of solids,
// which are a different mechanism making a different guarantee.
//
// False when the body is on navigable ground (so this is not its problem) or when no navigable
// point is within reach; the caller then does whatever it did before.
// The refuge a body is currently walking back to, remembered across frames.
//
// It has to be remembered, and that is the price of turning onto the way out rather than sliding
// along it. The spiral returns the first navigable candidate at the smallest radius that has one,
// and which candidate that is flips as the body moves -- so a body that re-asked every frame turned
// toward a new answer every frame and, with travel gated on facing the way it is going, spent its
// whole time pivoting and covered no ground. Measured on the shipped scene before this: one sheep
// fell from 63.6 m of travel in ninety seconds to 24.3 m, with 745 frames of a walk gait at an
// unrepresentable speed.
struct EscapeMemory {
    glm::vec2 refuge{0.0f};
    bool active = false;
    void reset() { active = false; }
};

bool escapeToNavigable(const Navigator* navigator, EscapeMemory& memory, EntityState& state,
                       float speed, float turnRate, double dt) {
    if (navigator == nullptr) {
        memory.reset();
        return false;
    }
    const glm::vec3 wedged = state.position();
    const glm::vec2 here(wedged.x, wedged.z);
    if (navigator->navigable(here)) {
        memory.reset();
        return false;
    }
    // Keep walking to the one already chosen while it is still somewhere worth walking to. Re-asked
    // only when there is no answer yet, when the answer stopped being navigable (the obstacle set
    // moves; that is the whole reason this predicate is the navigator's and not the grid's), or
    // when the body has arrived at it and is somehow still off the set.
    if (!memory.active || !navigator->navigable(memory.refuge) ||
        glm::length(memory.refuge - here) < 0.75f) {
        memory.active = false;
        for (float radius = 2.0f; radius <= 24.0f && !memory.active; radius += 2.0f) {
            for (int k = 0; k < 12 && !memory.active; ++k) {
                const float a = static_cast<float>(k) * 0.5235987756f;
                const glm::vec2 candidate = here + glm::vec2(std::sin(a), std::cos(a)) * radius;
                if (navigator->navigable(candidate)) {
                    memory.refuge = candidate;
                    memory.active = true;
                }
            }
        }
    }
    if (!memory.active) {
        return false;
    }
    const glm::vec2 away = memory.refuge - here;
    const float span = glm::length(away);
    if (span <= 1e-4f) {
        memory.reset();
        return false;
    }
    const auto step = static_cast<float>(dt);
    const float wanted = std::atan2(away.x, away.y);
    const float delta = angleDelta(state.yaw, wanted);
    const float turned = std::clamp(delta, -turnRate * step, turnRate * step);
    state.yaw += turned;
    state.turnRate = turned / std::max(step, 1e-4f);
    // Travel along the body's own heading, at the fraction of the step the facing error allows, so
    // a body that has to turn round pivots first and walks out after. Every metre it covers is a
    // metre along the way it is drawn facing.
    const float alignment = std::max(0.0f, std::cos(angleDelta(state.yaw, wanted)));
    const float limit = std::min(span, std::max(speed, 1.0f) * step) * alignment;
    const glm::vec2 heading(std::sin(state.yaw), std::cos(state.yaw));
    state.travel.x += heading.x * limit;
    state.travel.z += heading.y * limit;
    state.speed = limit / std::max(step, 1e-4f);
    state.activity = state.speed > 0.05f ? Activity::Walk : Activity::Turn;
    return true;
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

    // `slowNoise(ctx.time, ...)` and nothing else: the offset at t is a function of t, the seed the
    // reset drew, and three parameters. There is no state between one update and the next to
    // accumulate, so a replay of the preceding ninety seconds produces a value the last step throws
    // away. The one step is the answer.
    [[nodiscard]] int historySteps() const override { return 1; }

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

// ---- liveliness ------------------------------------------------------------------------------
//
// Secondary motion: the small movement a body has that its animation clips do not (ADR-198).
//
// The alien pack's clips are good and they are also *finite* -- one walk cycle, played identically
// every stride, on four characters at once. What makes a crowd read as alive is not more clips, it
// is that no two bodies are doing exactly the same thing at exactly the same moment. This adds that
// without touching the skeleton: it writes `MotionOffset`, the additive channel the entity layer
// already composes on top of the authored pose, so the underlying animation is untouched and an
// author's own rotation still survives.
//
// **What it deliberately does not do.** Lean into a turn is `bank`, which already exists and does it
// properly with a response time. A second knob for the same thing on the same body would fight it.
// And "head movement" is not here, because nothing can address a head: `ISkeletonQuery` is declared,
// stored, and never implemented, so every socket resolves against the entity origin. A whole-body
// nod is what is honestly available, and that is what `nod` is.
//
// **The bounce is the interesting one.** Its rate follows the body's own speed rather than a fixed
// frequency, so a running character bobs faster than a walking one without anybody authoring the
// relationship -- and because `explore/speed` is a registered parameter that a scene already drives
// from `audio.rms`, the bounce becomes music-reactive through the chain that exists rather than
// through a second one. That is the whole audio story here: no signal is read in this file.
class Liveliness final : public IBehavior {
public:
    explicit Liveliness(const nlohmann::json* s)
        : bounceDefault_(readFloat(s, "bounce", 0.0f)),
          bounceRateDefault_(readFloat(s, "bounceRate", 0.9f)),
          swayDefault_(readFloat(s, "sway", 0.0f)),
          swayRateDefault_(readFloat(s, "swayRate", 0.15f)),
          nodDefault_(readFloat(s, "nod", 0.0f)),
          strideDefault_(readFloat(s, "stride", 1.6f)) {}

    [[nodiscard]] std::string_view kind() const override { return "liveliness"; }

    void registerParameters(params::ParameterSet& params, const std::string& prefix) override {
        // Every one registered: these are exactly the knobs an author wants under a signal. The
        // amount of life a character has is a performance decision, not a property of the model.
        bounce_ = &params.add(floatDesc(prefix + "bounce", bounceDefault_, 0.0f, 20.0f));
        bounceRate_ = &params.add(floatDesc(prefix + "bounceRate", bounceRateDefault_, 0.0f, 8.0f));
        sway_ = &params.add(floatDesc(prefix + "sway", swayDefault_, 0.0f, 45.0f));
        swayRate_ = &params.add(floatDesc(prefix + "swayRate", swayRateDefault_, 0.0f, 4.0f));
        nod_ = &params.add(floatDesc(prefix + "nod", nodDefault_, 0.0f, 45.0f));
        paths_ = {prefix + "bounce", prefix + "bounceRate", prefix + "sway", prefix + "swayRate",
                  prefix + "nod"};
    }
    void collectParameterPaths(std::vector<std::string>& out) const override {
        out.insert(out.end(), paths_.begin(), paths_.end());
    }
    void reset(Rng& rng) override {
        seed_ = rng.nextU32();
        // The stride's own phase, per body. Four characters walking at the same speed with the same
        // bounce would rise and fall together, which reads as one animation on four puppets rather
        // than as four creatures. This is the entire difference and it costs one number.
        phase_ = rng.range(0.0f, 6.2831853f);
    }

    void update(const BehaviorContext& ctx, EntityState& state, MotionOffset& motion) override {
        const float bounce = param(bounce_, bounceDefault_);
        const float sway = param(sway_, swayDefault_);
        const float nod = param(nod_, nodDefault_);

        // The stride bob. Twice a stride, because a body rises on each foot rather than once a
        // cycle, and scaled by how fast it is actually going -- a standing body does not bob, which
        // is what stops this fighting the idle clip.
        if (bounce > 0.0f && strideDefault_ > 0.0f) {
            const float travel = std::max(state.speed, 0.0f);
            const float cycles = travel / strideDefault_;
            phase_ += static_cast<float>(ctx.dt) * cycles * param(bounceRate_, bounceRateDefault_) *
                      6.2831853f;
            // Normalised against the clip's own authored speed, so `bounce` is "how much at a
            // normal walk" rather than a number that means something different per character.
            const float strength = std::min(travel / strideDefault_, 2.0f);
            // A rise from ground contact, not an oscillation about it.
            //
            // This was `sin(phase * 2)`, which is symmetric, and a symmetric bob on a *grounded*
            // body is a body drawn underground for half of every stride. Grounding cannot see it:
            // grounding writes `state.travel.y`, the simulation position, and this writes
            // `motion.position.y`, the visual offset added to it afterwards. The two never meet, so
            // the simulation reports a perfectly grounded character while the renderer draws it
            // buried. Measured on Glowmere's own numbers (bounce 0.32, stride 5.35 -- the `ember`
            // and `vane` aliens) the drawn body sank 0.1819 m below its own ground and rose
            // 0.1838 m above it, while `state().position()` never left the surface by more than
            // 0.0030 m. That gap is the whole lesson: the convenient variable was clean.
            //
            // `(1 - cos) / 2` has the same period as the sine it replaces and the same peak, so
            // `bounce` still means what an author tuned it to mean and the stride still reads as
            // twice a cycle. What changes is the phase reference: the trough is now ground contact
            // -- where a walking body's lowest point actually is, both feet planted -- instead of
            // the midpoint of a swing with nothing holding up its bottom half.
            //
            // The halving is not cosmetic and was not in the first version of this fix. `1 - cos`
            // spans [0, 2] where `sin` spanned [-1, 1]: lifting the trough to the ground without it
            // also doubles the peak, and measurement said exactly that -- the drawn rise went from
            // 0.1838 m to 0.3641 m, so every walking character in Glowmere bobbed twice as high as
            // anyone had asked it to. Fixing a floor is not a licence to move a ceiling.
            //
            // `hover` above keeps its symmetric noise deliberately: a craft oscillates about a
            // hover height and has no ground contact to be the floor of.
            motion.position.y += 0.5f * (1.0f - std::cos(phase_ * 2.0f)) * bounce * strength;
        }

        // The idle drift, on noise rather than a sine for the reason `slowNoise` gives: a sine lands
        // on the same value at the same phase every cycle, and a body that does that is a metronome.
        // Strongest when standing, because a walking body already has motion of its own.
        if (sway > 0.0f || nod > 0.0f) {
            const float still = 1.0f - std::min(std::max(state.speed, 0.0f) / std::max(strideDefault_, 0.1f), 1.0f);
            const glm::vec3 n = slowNoise(ctx.time, param(swayRate_, swayRateDefault_), seed_, 3.0f);
            motion.rotation.y += n.x * sway * (0.35f + 0.65f * still);
            motion.rotation.x += n.y * nod * (0.35f + 0.65f * still);
        }
    }

private:
    [[nodiscard]] static float param(const params::Parameter<float>* p, float fallback) {
        return p != nullptr ? p->value() : fallback;
    }

    float bounceDefault_, bounceRateDefault_, swayDefault_, swayRateDefault_, nodDefault_, strideDefault_;
    params::Parameter<float>* bounce_ = nullptr;
    params::Parameter<float>* bounceRate_ = nullptr;
    params::Parameter<float>* sway_ = nullptr;
    params::Parameter<float>* swayRate_ = nullptr;
    params::Parameter<float>* nod_ = nullptr;
    std::vector<std::string> paths_;
    std::uint32_t seed_ = 1u;
    float phase_ = 0.0f;
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

    // Two, not one. The offset is a pure function of the clock like `hover`'s, but the velocity it
    // publishes for `bank` is a backward difference against the previous step -- so the step before
    // the target has to have run, and `started_` has to be true, or a scrubbed frame reports a
    // stationary craft that a played one reports as moving. The step before is enough: `previous_`
    // after it holds `offset(t - dt)`, which is exactly what a full replay would have left there.
    [[nodiscard]] int historySteps() const override { return 2; }

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
          pauseMaxDefault_(readFloat(s, "pauseMax", 6.0f)),
          homeDefault_(readFloat(s, "homeRadius", 0.0f)) {}

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
        home_ = &params.add(floatDesc(prefix + "homeRadius", homeDefault_, 0.0f, 4000.0f));
        paths_ = {prefix + "speed",     prefix + "runSpeed", prefix + "turnRate", prefix + "arrive",
                  prefix + "minRange",  prefix + "maxRange", prefix + "pauseMin", prefix + "pauseMax",
                  prefix + "homeRadius"};
    }
    void collectParameterPaths(std::vector<std::string>& out) const override {
        out.insert(out.end(), paths_.begin(), paths_.end());
    }
    void reset(Rng& rng) override {
        hasDestination_ = false;
        pause_ = rng.range(0.0f, 2.0f);
        escape_.reset();
    }

    void update(const BehaviorContext& ctx, EntityState& state, MotionOffset& motion) override {
        (void)motion;
        // A higher authority is driving this body: an action, a schedule or a director override
        // (ADR-091, ADR-096). Yield by *keeping everything*: the destination and the pause timer
        // are untouched, so when the order ends the walk carries on to the same place rather than
        // picking a new one. Speed is deliberately not zeroed here -- whoever is driving has
        // already written it, and overwriting it would make a directed walk stand still.
        if (state.driven) {
            return;
        }
        // Something with the character's attention has it. Travel is what a character does when
        // nothing else is happening, so it yields rather than competing: the destination and the
        // pause timer are kept, and the walk resumes from where it stopped.
        if (state.activity == Activity::Observe || state.activity == Activity::React) {
            state.speed = 0.0f;
            return;
        }
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
            escape_.reset();
            return;
        }
        if (!hasDestination_) {
            const float lo = minRange_ != nullptr ? minRange_->value() : minRangeDefault_;
            const float hi = maxRange_ != nullptr ? maxRange_->value() : maxRangeDefault_;
            // An unleashed wander is a random walk, and a random walk leaves. When a home radius is
            // set, a character that has strayed past it picks its next destination around *home*
            // rather than around itself, so it drifts back without ever being pushed: the walk
            // stays a walk instead of becoming a return trip.
            const float home = home_ != nullptr ? home_->value() : homeDefault_;
            const glm::vec2 anchor(state.anchor.x, state.anchor.z);
            const glm::vec2 from =
                (home > 0.0f && glm::length(flat - anchor) > home) ? anchor : flat;
            if (ctx.nav != nullptr && ctx.rng != nullptr &&
                ctx.nav->pickDestination(*ctx.rng, from, lo, std::min(hi, home > 0.0f ? home : hi),
                                         destination_)) {
                hasDestination_ = true;
            } else if (escapeToNavigable(ctx.nav, escape_, state, speed, turnRate, ctx.dt)) {
                // Not boxed in: *off the navigable set*, where the rejection sampler can find
                // nothing in the annulus because the body's own neighbourhood is not walkable.
                // ADR-162's walk back onto the path, which `explore` has had since then and
                // `wander` never did -- see the note on the steering branch below.
                return;
            } else {
                // Nowhere to go. Wait a beat and ask again rather than retrying every frame: a
                // character boxed in by terrain should stand still, not burn the frame on
                // rejection sampling.
                //
                // And *say* it is standing still. `EntityState::speed` is not cleared between
                // frames -- whoever is driving owns it -- so returning without writing it leaves
                // last frame's number standing, and the gait picks a walk clip from it. A body
                // playing a walk cycle and going nowhere is the `sage` defect (ADR-199), in
                // miniature and repeated: measured over the shipped farm it was 54% of the frames
                // an animal was "commanded to move".
                pause_ = 1.0f;
                state.speed = 0.0f;
                state.activity = Activity::Idle;
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
            } else if (escapeToNavigable(ctx.nav, escape_, state, speed, turnRate, ctx.dt)) {
                // Not blocked by the world: standing *off the navigable set*, where `pathClear`
                // samples from the body's own position outward and so every ray fails whichever
                // way it points, however wide the fan reaches (ADR-162). The two branches are
                // indistinguishable from inside the steering fan and they are not the same
                // situation: one is a pause and the other is a wedge. `explore` was given the walk
                // back onto the path when ADR-162 found this and `wander` never was, so a farm
                // animal that stepped off the walkable set stood still in half-second increments
                // until something else moved it -- measured on the shipped scene at **22.28 s**
                // for one chick, against a worst of one authored pause for every other animal
                // (ADR-240).
                //
                // Both conditions are required, and the pair is what keeps this rare: a body can
                // be off the navigable set and still perfectly able to walk, because `pathClear`
                // starts sampling a quarter of a metre *ahead* of it. Firing on the predicate
                // alone hijacks a body that was travelling fine -- measured, it took one sheep
                // from 63.6 m of travel in ninety seconds to 24.3 m.
                //
                // The destination is kept: it may still be reachable once the body is back on the
                // path, and re-picking one every frame is rejection sampling nobody asked for.
                return;
            } else {
                // Every way out is blocked. Drop the destination rather than grinding into a
                // hillside; the next pick will be somewhere else -- and stop claiming to be moving,
                // for the reason above.
                hasDestination_ = false;
                pause_ = 0.5f;
                state.speed = 0.0f;
                state.activity = Activity::Idle;
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
    float minRangeDefault_, maxRangeDefault_, pauseMinDefault_, pauseMaxDefault_, homeDefault_;
    params::Parameter<float>* speed_ = nullptr;
    params::Parameter<float>* runSpeed_ = nullptr;
    params::Parameter<float>* turn_ = nullptr;
    params::Parameter<float>* arrive_ = nullptr;
    params::Parameter<float>* minRange_ = nullptr;
    params::Parameter<float>* maxRange_ = nullptr;
    params::Parameter<float>* pauseMin_ = nullptr;
    params::Parameter<float>* pauseMax_ = nullptr;
    params::Parameter<float>* home_ = nullptr;
    std::vector<std::string> paths_;
    glm::vec2 destination_{0.0f};
    bool hasDestination_ = false;
    float pause_ = 0.0f;
    EscapeMemory escape_;
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
        // A character that is going somewhere faces where it is going. Turning the *body* towards
        // something while walking elsewhere is not a compromise between the two, it is a character
        // that strafes -- and worse, it drives the travel speed to zero, because locomotion scales
        // its pace by how well the body is aligned with its heading. So while travelling this
        // publishes the look target and leaves the yaw alone: where the eyes and head go on top of
        // a walk cycle is the animation layer's business, and LocomotionState carries it there.
        if (state.activity == Activity::Walk || state.activity == Activity::Run) {
            return;
        }
        // And the same again when an action owns the body. A character told to walk to the
        // nightstand may look at the window on the way; it may not turn to face it.
        if (state.driven) {
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
          decayDefault_(readFloat(s, "reactionDecay", 1.4f)),
          cooldownDefault_(readFloat(s, "reactionCooldown", 3.0f)) {}

    [[nodiscard]] std::string_view kind() const override { return "interest"; }

    void registerParameters(params::ParameterSet& params, const std::string& prefix) override {
        observe_ = &params.add(floatDesc(prefix + "observeChance", observeDefault_, 0.0f, 1.0f));
        minDwell_ = &params.add(floatDesc(prefix + "minDwell", minDwellDefault_, 0.0f, 300.0f));
        maxDwell_ = &params.add(floatDesc(prefix + "maxDwell", maxDwellDefault_, 0.0f, 600.0f));
        threshold_ = &params.add(floatDesc(prefix + "alertThreshold", thresholdDefault_, 0.0f, 4.0f));
        decay_ = &params.add(floatDesc(prefix + "reactionDecay", decayDefault_, 0.01f, 40.0f));
        cooldown_ = &params.add(floatDesc(prefix + "reactionCooldown", cooldownDefault_, 0.0f, 120.0f));
        paths_ = {prefix + "observeChance",  prefix + "minDwell",      prefix + "maxDwell",
                  prefix + "alertThreshold", prefix + "reactionDecay", prefix + "reactionCooldown"};
    }
    void collectParameterPaths(std::vector<std::string>& out) const override {
        out.insert(out.end(), paths_.begin(), paths_.end());
    }
    void reset(Rng& rng) override {
        observing_ = false;
        dwell_ = rng.range(0.5f, 3.0f);
        subject_ = subjects_.empty() ? 0 : rng.nextU32() % static_cast<std::uint32_t>(subjects_.size());
        reaction_ = 0.0f;
        cooldownLeft_ = 0.0f;
    }

    void update(const BehaviorContext& ctx, EntityState& state, MotionOffset& motion) override {
        (void)motion;
        const float decay = decay_ != nullptr ? decay_->value() : decayDefault_;
        reaction_ = std::max(0.0f, reaction_ - decay * static_cast<float>(ctx.dt));

        // A strong event turns the character's attention, briefly. Deliberately *not* a state
        // change: an earlier version set `observing_` here and re-rolled the dwell, which on a
        // percussive track meant every impact interrupted the one before it and the character
        // never walked again -- it stood still for ninety seconds looking startled. A reaction is
        // punctuation. It decays on its own, it does not stop a walk (the animation layer blends a
        // flinch over whatever gait is playing, which is what LocomotionState::reaction is for),
        // and a cooldown stops a dense passage from firing it continuously.
        const float threshold = threshold_ != nullptr ? threshold_->value() : thresholdDefault_;
        cooldownLeft_ = std::max(0.0f, cooldownLeft_ - static_cast<float>(ctx.dt));
        if (cooldownLeft_ <= 0.0f && ctx.event(signal_) && ctx.signal(signal_) >= threshold) {
            reaction_ = std::min(1.0f, ctx.signal(signal_));
            cooldownLeft_ = cooldown_ != nullptr ? cooldown_->value() : cooldownDefault_;
            if (!subjects_.empty() && ctx.rng != nullptr) {
                subject_ = ctx.rng->nextU32() % static_cast<std::uint32_t>(subjects_.size());
                startled_ = true;
            }
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
        if (reaction_ <= 0.15f) {
            startled_ = false;
        }
        // Attend while observing, or while a fresh reaction is still fading. Only *observing*
        // stops the feet; a reaction turns the head and lets the walk carry on.
        if (!observing_ && !startled_) {
            return;
        }
        // Under orders, interest still names a subject and still raises a reaction -- a directed
        // character can flinch at a drum hit and glance at a window -- but it does not stop the
        // feet. Stopping is a decision, and the decision is not this behaviour's to make while
        // something above it is driving.
        if (observing_ && !state.driven) {
            state.speed = 0.0f;
            state.activity = Activity::Observe;
        }
        // What is worth attending to. `lookAt` does the turning; this only names the subject,
        // which is the division that lets either be replaced without touching the other.
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
    float cooldownDefault_;
    params::Parameter<float>* observe_ = nullptr;
    params::Parameter<float>* minDwell_ = nullptr;
    params::Parameter<float>* maxDwell_ = nullptr;
    params::Parameter<float>* threshold_ = nullptr;
    params::Parameter<float>* decay_ = nullptr;
    params::Parameter<float>* cooldown_ = nullptr;
    std::vector<std::string> paths_;
    bool observing_ = false;
    bool startled_ = false;
    float cooldownLeft_ = 0.0f;
    float dwell_ = 0.0f;
    float reaction_ = 0.0f;
    std::uint32_t subject_ = 0;
};

// ---- explore ---------------------------------------------------------------------------------
//
// The loop §6 asks for: IDLE -> SELECT INTEREST -> NAVIGATE -> WALK -> ARRIVE -> OBSERVE -> IDLE.
//
// `wander` is kept and is still the right behaviour for a background creature that should mill
// about near where it was placed. This is the other thing: a character that crosses a world on
// purpose, because something over there is worth looking at.
//
// What separates it from a waypoint debugger, which §6 is explicit about not wanting:
//
//   * It goes somewhere *for a reason*. Destinations are drawn from the interest registry -- the
//     heroes, the craft, a shoreline, a high point -- weighted by the character's own taste, with
//     recently visited places suppressed so it does not pace between two favourites.
//   * It plans. A route comes from the navigation graph, so a lake or a ridge is walked around
//     rather than discovered, refused and re-rolled. That is the difference between travelling and
//     the rejection-sampling twitch that was here before.
//   * Every duration is sampled, every branch is a roll. Two characters with the same behaviour and
//     different seeds do not do the same thing at the same time, and neither does the same
//     character twice around the loop.
//   * It arrives rather than stopping: it slows over the last few metres and turns to face what it
//     came for before it stands still.
//
// Seeded throughout, through `ctx.rng`. No wall clock anywhere.
class Explore final : public IBehavior {
public:
    explicit Explore(const nlohmann::json* s)
        : speedDefault_(readFloat(s, "speed", 1.7f)),
          runSpeedDefault_(readFloat(s, "runSpeed", 4.2f)),
          turnDefault_(readFloat(s, "turnRate", 150.0f)),
          arriveDefault_(readFloat(s, "arrive", 1.6f)),
          idleMinDefault_(readFloat(s, "idleMin", 0.6f)),
          idleMaxDefault_(readFloat(s, "idleMax", 3.0f)),
          observeDefault_(readFloat(s, "observeChance", 0.55f)),
          observeMinDefault_(readFloat(s, "observeMin", 2.0f)),
          observeMaxDefault_(readFloat(s, "observeMax", 6.5f)),
          minRangeDefault_(readFloat(s, "minRange", 12.0f)),
          maxRangeDefault_(readFloat(s, "maxRange", 160.0f)),
          homeDefault_(readFloat(s, "homeRadius", 0.0f)),
          runChanceDefault_(readFloat(s, "runChance", 0.18f)),
          slopeAlignDefault_(readFloat(s, "slopeAlign", 0.55f)),
          wadeDragDefault_(readFloat(s, "wadeDrag", 0.55f)),
          bodyRadiusDefault_(readFloat(s, "bodyRadius", 0.0f)),
          headroomDefault_(readFloat(s, "headroom", 0.0f)),
          footprintDefault_(readFloat(s, "footprint", 0.55f)),
          strollChance_(readFloat(s, "strollChance", 0.3f)),
          waypointRadius_(readFloat(s, "waypointRadius", 2.2f)),
          repathSeconds_(readFloat(s, "repathSeconds", 6.0f)),
          stuckSeconds_(readFloat(s, "stuckSeconds", 2.5f)),
          // ADR-194. Read here and held, like the affinities: the *shape* of a hop is a property of
          // the body -- how a particular creature moves -- while how far it will leap is the knob
          // registered above, because that is the one an author might put under a signal.
          jumpRangeDefault_(readFloat(s, "jumpRange", 0.0f)),
          jumpSignal_(readString(s, "jumpSignal", "")) {
        // Taste, and the novelty radius that goes with it. Read once and held, rather than
        // registered: an author sets what a character cares about when they build it, and "how
        // much does it like water" is not a thing anybody automates on a timeline.
        //
        // **They live on a considerer now** (ADR-330). The five affinities, the distance falloff
        // and the visited-place suppression were `Explore`'s goal model, inlined in `pickGoal`;
        // `entity::goalWeight` is that arithmetic with a name, and this class holds an
        // `InterestConsiderer` over it the way a character with a `decide` behaviour does. The
        // three range knobs stay registered parameters and are copied onto the taste each
        // selection, because those *are* things a scene keyframes.
        GoalTaste taste;
        taste.weight[0] = readFloat(s, "landmarkAffinity", 1.0f);
        taste.weight[1] = readFloat(s, "characterAffinity", 1.3f);
        taste.weight[2] = readFloat(s, "glowAffinity", 1.2f);
        taste.weight[3] = readFloat(s, "waterAffinity", 0.9f);
        taste.weight[4] = readFloat(s, "vistaAffinity", 0.7f);
        taste.noveltyRadius = readFloat(s, "noveltyRadius", 22.0f);
        goals_.setTaste(taste);
        // The omniscient list, deliberately and for now. ADR-270's finding is that reading
        // `interestPoints()` is why two characters in one world walk the same route, and the
        // considerer can read percepts instead with one word -- but `Explore` is what five
        // Glowmere characters are, and changing which list it scores is a changed film. The lab's
        // case 6 is the before-arm and case 14's explorer is the after; moving `Explore` across is
        // an owner's decision, not a refactor's.
        goals_.setSource(InterestConsiderer::Source::Omniscient);
        jump_.gravity = readFloat(s, "jumpGravity", 18.0f);
        jump_.apex = readFloat(s, "jumpApex", 1.1f);
        jump_.landSeconds = readFloat(s, "landSeconds", 0.3f);
        jump_.maxDistance = jumpRangeDefault_;
    }

    [[nodiscard]] std::string_view kind() const override { return "explore"; }

    // What this character is doing, for a debug overlay (§46) and for a diagnostic. The editor owns
    // the drawing; navigation owes it the data, and a route nobody outside this class can see is a
    // route nobody can tell is wrong.
    [[nodiscard]] std::span<const glm::vec2> route() const { return path_; }
    [[nodiscard]] std::size_t routeLeg() const { return leg_; }
    [[nodiscard]] glm::vec3 destination() const { return goal_; }
    [[nodiscard]] bool hasDestination() const { return hasGoal_; }
    [[nodiscard]] PathStatus lastPathStatus() const { return lastStatus_; }
    [[nodiscard]] std::string_view phaseName() const {
        switch (phase_) {
        case Phase::Idle: return "idle";
        case Phase::Select: return "select";
        case Phase::Navigate: return "navigate";
        case Phase::Walk: return "walk";
        case Phase::Arrive: return "arrive";
        case Phase::Observe: return "observe";
        }
        return "idle";
    }

    // The same six facts through the interface, so a caller that holds an IBehavior and cannot
    // name this class gets them (ADR-197). The accessors above stay: they are what a diagnostic
    // inside this file reads, and they are the definition this is a view of.
    [[nodiscard]] bool navDebug(NavDebug& out) const override {
        out.route = path_;
        out.leg = leg_;
        out.destination = goal_;
        out.hasDestination = hasGoal_;
        out.status = lastStatus_;
        out.confinedFor = confinedFor_;
        out.stuckFor = stuckFor_;
        out.phase = phaseName();
        out.goalName = goalName_;
        out.goalKind = interestKindName(goalKind_);
        return true;
    }

    void registerParameters(params::ParameterSet& params, const std::string& prefix) override {
        speed_ = &params.add(floatDesc(prefix + "speed", speedDefault_, 0.0f, 40.0f));
        runSpeed_ = &params.add(floatDesc(prefix + "runSpeed", runSpeedDefault_, 0.0f, 60.0f));
        turn_ = &params.add(floatDesc(prefix + "turnRate", turnDefault_, 1.0f, 1440.0f));
        arrive_ = &params.add(floatDesc(prefix + "arrive", arriveDefault_, 0.05f, 20.0f));
        idleMin_ = &params.add(floatDesc(prefix + "idleMin", idleMinDefault_, 0.0f, 300.0f));
        idleMax_ = &params.add(floatDesc(prefix + "idleMax", idleMaxDefault_, 0.0f, 600.0f));
        observe_ = &params.add(floatDesc(prefix + "observeChance", observeDefault_, 0.0f, 1.0f));
        observeMin_ = &params.add(floatDesc(prefix + "observeMin", observeMinDefault_, 0.0f, 300.0f));
        observeMax_ = &params.add(floatDesc(prefix + "observeMax", observeMaxDefault_, 0.0f, 600.0f));
        minRange_ = &params.add(floatDesc(prefix + "minRange", minRangeDefault_, 0.0f, 1000.0f));
        maxRange_ = &params.add(floatDesc(prefix + "maxRange", maxRangeDefault_, 0.5f, 4000.0f));
        home_ = &params.add(floatDesc(prefix + "homeRadius", homeDefault_, 0.0f, 4000.0f));
        runChance_ = &params.add(floatDesc(prefix + "runChance", runChanceDefault_, 0.0f, 1.0f));
        slopeAlign_ = &params.add(floatDesc(prefix + "slopeAlign", slopeAlignDefault_, 0.0f, 1.0f));
        // How much the water slows this character down, at the deepest water it will enter. A
        // parameter rather than a constant for the same reason `speed` is one: it is the kind of
        // thing a scene modulates -- something heavy fords a river at a crawl and something
        // long-legged hardly notices -- and the whole point of ADR-011 is that anything a shot
        // wants to shape over time can be keyframed rather than re-authored.
        //
        // Unlike `bodyRadius`, 0 here does not mean "take the world's number": it means water does
        // not slow this character at all. The default of 0.55 is deliberately non-zero, because a
        // scene that turned wading on and got a character skimming across a river at running speed
        // would have had to find and set a second knob to get the behaviour it asked for. It costs
        // nothing where nobody wades: depth is 0 on dry land, so the scale is exactly 1.
        wadeDrag_ = &params.add(floatDesc(prefix + "wadeDrag", wadeDragDefault_, 0.0f, 1.0f));
        // How big this character is. A world's navigator carries defaults for a person-sized
        // walker, and Glowmere's is nearly ten metres tall: it has to keep further from a trunk
        // than a person does, and it does not duck under anything. 0 keeps the world's own number,
        // so a scene that says nothing behaves exactly as it did.
        bodyRadius_ = &params.add(floatDesc(prefix + "bodyRadius", bodyRadiusDefault_, 0.0f, 40.0f));
        headroom_ = &params.add(floatDesc(prefix + "headroom", headroomDefault_, 0.0f, 60.0f));
        footprint_ = &params.add(floatDesc(prefix + "footprint", footprintDefault_, 0.0f, 20.0f));
        // ADR-194: how far this body will leap. 0 -- the default -- is a body that does not jump,
        // so every scene written before this behaves exactly as it did, and a hop is something an
        // author turns on for a character they meant to be able to.
        jumpRange_ = &params.add(floatDesc(prefix + "jumpRange", jumpRangeDefault_, 0.0f, 40.0f));
        paths_ = {prefix + "speed",         prefix + "runSpeed",   prefix + "turnRate",
                  prefix + "arrive",        prefix + "idleMin",    prefix + "idleMax",
                  prefix + "observeChance", prefix + "observeMin", prefix + "observeMax",
                  prefix + "minRange",      prefix + "maxRange",   prefix + "homeRadius",
                  prefix + "runChance",     prefix + "slopeAlign",  prefix + "bodyRadius",
                  prefix + "headroom",      prefix + "footprint",  prefix + "jumpRange",
                  prefix + "wadeDrag"};
    }
    void collectParameterPaths(std::vector<std::string>& out) const override {
        out.insert(out.end(), paths_.begin(), paths_.end());
    }

    void reset(Rng& rng) override {
        phase_ = Phase::Idle;
        timer_ = rng.range(0.0f, 1.5f);
        path_.clear();
        leg_ = 0;
        hasGoal_ = false;
        goalName_.clear();
        goalKind_ = InterestKind::Vista;
        sinceRepath_ = 0.0f;
        stuckFor_ = 0.0f;
        escape_.reset();
        bestProgress_ = std::numeric_limits<float>::max();
        failures_ = 0;
        running_ = false;
        confined_ = false;
        confinedFor_ = 0.0f;
        lastStatus_ = PathStatus::Ok;
        recent_.clear();
        ground_.reset();
        air_.reset(); // a seek must not carry an arc into a second the body never jumped in
        jumpArmed_ = 0.0f;
    }

    void update(const BehaviorContext& ctx, EntityState& state, MotionOffset& motion) override {
        refreshWalker(ctx);
        // Declare a body, so other characters have something to walk around (§11). Zero -- the
        // default -- means "not a body", which is the right answer for anything that flies.
        state.radius = param(bodyRadius_, bodyRadiusDefault_);
        // Another behaviour has stopped the feet -- `interest` looking at something, a scripted
        // beat. Travel yields rather than competing: the route and the phase are kept, and the walk
        // resumes from where it stopped rather than being re-rolled.
        if (state.activity == Activity::Observe) {
            state.speed = 0.0f;
            applyGrounding(ctx, state, motion, 0.0f);
            return;
        }
        // The phases are a chain, not a list: selecting a goal should plan a route in the same
        // frame, and planning one should start walking in it. The guard is what keeps a bug in a
        // transition from becoming an infinite loop inside one frame.
        if (confined_) {
            confinedFor_ += static_cast<float>(ctx.dt);
        }
        for (int guard = 0; guard < 5; ++guard) {
            if (!step(ctx, state, motion)) {
                break;
            }
        }
        applyGrounding(ctx, state, motion, state.speed);
    }

private:
    enum class Phase : std::uint8_t { Idle, Select, Navigate, Walk, Arrive, Observe };

    // The navigator this character should be using: the world's, resized to its own body. Rebuilt
    // only when something it depends on changed, because both of those are keyframeable and a
    // per-frame copy would be a per-frame copy for nothing.
    void refreshWalker(const BehaviorContext& ctx) {
        if (ctx.nav == nullptr) {
            return;
        }
        const float radius = param(bodyRadius_, bodyRadiusDefault_);
        const float headroom = param(headroom_, headroomDefault_);
        if (radius <= 0.0f && headroom <= 0.0f) {
            walkerSource_ = nullptr; // this character takes the world's defaults
            return;
        }
        if (walkerSource_ == ctx.nav && walkerRadius_ == radius && walkerHeadroom_ == headroom) {
            return;
        }
        walker_ = *ctx.nav;
        NavSettings settings = walker_.settings();
        if (radius > 0.0f) {
            settings.bodyRadius = radius;
            // Something this wide steps over more than a person does, and its stride is longer.
            settings.stepOver = std::max(settings.stepOver, radius * 0.75f);
            settings.stepHeight = std::max(settings.stepHeight, radius * 1.6f);
        }
        if (headroom > 0.0f) {
            settings.headroom = headroom;
            // The thicket band is "too tall to wade through, too low to walk under". Raising the
            // ceiling without raising the floor would turn every shrub into a wall, which is
            // precisely the bug that kept the old walker standing still in a meadow.
            settings.walkableVegetation = std::max(settings.walkableVegetation, headroom * 0.36f);
        }
        walker_.setSettings(settings);
        walkerSource_ = ctx.nav;
        walkerRadius_ = radius;
        walkerHeadroom_ = headroom;
    }
    [[nodiscard]] const Navigator* nav(const BehaviorContext& ctx) const {
        return walkerSource_ != nullptr ? &walker_ : ctx.nav;
    }

    // How much of its speed this character keeps at `p`. 1 on dry land, and 1 everywhere in a
    // world whose walker does not wade -- that early-out is also what keeps this free: without it
    // every walking frame of every existing scene would pay a water query to be told the answer is
    // zero.
    //
    // Linear in depth over the wade band, which is the honest shape: the resistance a body pushes
    // through is its submerged cross-section, and over ankle-to-thigh that is very close to linear
    // in depth. It is also the shape `NavPathCost::wadePenalty` was priced against, and the two
    // have to describe the same water or the planner and the mover disagree about which way is
    // quicker.
    //
    // Floored rather than allowed to reach zero. A drag of 1.0 at full depth would stop the
    // character dead in the middle of the ford, the stuck watchdog would fire, it would replan,
    // and it would walk back into the same water -- a knob at its documented maximum producing a
    // character that cannot move is a knob that is wrong at the end of its range.
    [[nodiscard]] float wadeScale(const Navigator* navigator, glm::vec2 p) const {
        if (navigator == nullptr || !navigator->valid()) {
            return 1.0f;
        }
        const float band = navigator->settings().wadeDepth;
        const float drag = param(wadeDrag_, wadeDragDefault_);
        if (band <= 0.0f || drag <= 0.0f) {
            return 1.0f;
        }
        const float depth = navigator->terrain().waterDepthAt(p);
        if (depth <= 0.0f) {
            return 1.0f;
        }
        return std::max(1.0f - drag * std::clamp(depth / band, 0.0f, 1.0f), 0.15f);
    }

    [[nodiscard]] float param(const params::Parameter<float>* p, float fallback) const {
        return p != nullptr ? p->value() : fallback;
    }
    [[nodiscard]] float sample(const BehaviorContext& ctx, float lo, float hi) const {
        if (ctx.rng == nullptr) {
            return lo;
        }
        return ctx.rng->range(std::min(lo, hi), std::max(lo, hi));
    }

    // Runs one phase. Returns true when the phase changed and the new one should run immediately.
    bool step(const BehaviorContext& ctx, EntityState& state, MotionOffset& motion) {
        switch (phase_) {
        case Phase::Idle: return stepIdle(ctx, state);
        case Phase::Select: return stepSelect(ctx, state);
        case Phase::Navigate: return stepNavigate(ctx, state);
        case Phase::Walk: return stepWalk(ctx, state, motion);
        case Phase::Arrive: return stepArrive(ctx, state);
        case Phase::Observe: return stepObserve(ctx, state);
        }
        return false;
    }

    bool stepIdle(const BehaviorContext& ctx, EntityState& state) {
        state.speed = 0.0f;
        state.activity = Activity::Idle;
        timer_ -= static_cast<float>(ctx.dt);
        if (timer_ > 0.0f) {
            return false;
        }
        phase_ = Phase::Select;
        return true;
    }

    bool stepSelect(const BehaviorContext& ctx, EntityState& state) {
        state.speed = 0.0f;
        if (!pickGoal(ctx, state)) {
            // Nowhere to go. Stand a moment and ask again rather than burning the frame on
            // rejection sampling, which is what the previous navigation did and why it never moved.
            //
            // And say so, because standing a moment and standing forever look identical from
            // outside (ADR-296). A body with nowhere to go never reaches `requestPath`, so no
            // `PathStatus` is ever published and the overlay reads whatever the last errand left
            // there. `confined_` is that fact, and `confinedFor_` is how long it has been true.
            confined_ = true;
            timer_ = sample(ctx, 0.8f, 2.0f);
            phase_ = Phase::Idle;
            return false;
        }
        confined_ = false;
        confinedFor_ = 0.0f;
        // Whether this trip is a walk or a run is decided once, on departure, rather than per
        // frame -- a character that re-rolled its gait every frame would flicker between them.
        running_ = ctx.rng != nullptr && ctx.rng->nextFloat() < param(runChance_, runChanceDefault_);
        phase_ = Phase::Navigate;
        return true;
    }

    bool stepNavigate(const BehaviorContext& ctx, EntityState& state) {
        state.speed = 0.0f;
        const glm::vec3 here = state.position();
        const glm::vec2 flat(here.x, here.z);
        const glm::vec2 target(goal_.x, goal_.z);
        const Navigator* navigator = nav(ctx);
        if (navigator == nullptr) {
            phase_ = Phase::Idle;
            timer_ = 1.0f;
            return false;
        }
        PathRequest request;
        request.from = flat;
        request.to = target;
        request.goalTolerance = param(arrive_, arriveDefault_) * 2.0f;
        const PathResult route = navigator->requestPath(request);
        lastStatus_ = route.status;
        if (route.ok() && !route.waypoints.empty()) {
            path_ = route.waypoints;
            goal_ = glm::vec3(route.goal.x, navigator->groundHeight(route.goal), route.goal.y);
            leg_ = 0;
            sinceRepath_ = 0.0f;
            stuckFor_ = 0.0f;
            bestProgress_ = glm::length(route.goal - flat);
            failures_ = 0;
            phase_ = Phase::Walk;
            return true;
        }
        if (route.status == PathStatus::AlreadyThere) {
            phase_ = Phase::Arrive;
            timer_ = 1.0f;
            return true;
        }
        // No route, and *why* decides what to do about it -- which is the whole reason the seam
        // returns a reason. §6: a character must not freeze when its destination becomes
        // unavailable, and one told only "no" has nothing to change.
        ++failures_;
        // An unreachable goal is not bad luck, it is a fact about the world: this character is on
        // one side of something and the goal is on the other, and re-rolling will keep landing over
        // there. Remembering it is what stops the loop from picking the same island repeatedly.
        if (route.status == PathStatus::Unreachable || route.status == PathStatus::NoGoal) {
            remember(goal_);
        }
        if (failures_ >= 3) {
            failures_ = 0;
            hasGoal_ = false;
            timer_ = sample(ctx, 0.6f, 1.8f);
            phase_ = Phase::Idle;
            return false;
        }
        phase_ = Phase::Select;
        return true;
    }

    bool stepWalk(const BehaviorContext& ctx, EntityState& state, MotionOffset& motion) {
        (void)motion;
        const auto dt = static_cast<float>(ctx.dt);
        sinceRepath_ += dt;
        const glm::vec3 here = state.position();
        const glm::vec2 flat(here.x, here.z);

        // ADR-194. A hop in progress owns the body: the arc is the movement, so nothing below --
        // steering, separation, the penetration resolve, the ground follower -- may touch it until
        // the feet are down. Committed rather than interruptible, which is what a jump is.
        if (const Navigator* airNav = nav(ctx); air_.active() && airNav != nullptr) {
            glm::vec3 at = here;
            if (air_.update(*airNav, ctx.dt, at, jump_)) {
                state.travel = at - state.anchor;
                state.speed = glm::length(glm::vec2(air_.position().x - here.x, air_.position().z - here.z)) /
                              std::max(dt, 1e-4f);
                state.activity = air_.activity();
                return false;
            }
            // Down. The ground follower has been ignored for the whole arc and still holds the
            // height the body left from, so it has to be re-primed or it glides down to the
            // surface from take-off height -- which its own header warns about in as many words.
            ground_.reset();
        }

        // A goal that moves. The craft drifts and hovers, and a character walking to where it was
        // a minute ago is a character walking to nothing.
        if (goalKind_ == InterestKind::Character && !goalName_.empty() && ctx.world != nullptr) {
            glm::vec3 now{0.0f};
            if (ctx.world->pointOfInterest(goalName_, now) &&
                glm::length(glm::vec2(now.x - goal_.x, now.z - goal_.z)) > 6.0f) {
                goal_ = now;
                phase_ = Phase::Navigate;
                return true;
            }
        }
        if (leg_ >= path_.size()) {
            phase_ = Phase::Arrive;
            return true;
        }

        const bool finalLeg = leg_ + 1 >= path_.size();
        const float arrive = param(arrive_, arriveDefault_);
        const glm::vec2 waypoint = path_[leg_];
        const float toWaypoint = glm::length(waypoint - flat);
        const float reached = finalLeg ? arrive : std::max(waypointRadius_, 0.5f);
        if (toWaypoint <= reached) {
            if (finalLeg) {
                phase_ = Phase::Arrive;
                return true;
            }
            ++leg_;
            return true; // take the next leg in this same frame rather than idling a step
        }

        const Navigator* navigator = nav(ctx);
        const float speed = running_ ? param(runSpeed_, runSpeedDefault_) : param(speed_, speedDefault_);
        const float turnRate = param(turn_, turnDefault_) / kDegrees;
        glm::vec2 direction = (waypoint - flat) / std::max(toWaypoint, 1e-4f);
        if (navigator != nullptr && navigator->valid()) {
            // Local steering on top of the plan. The route says which way the world is open; this
            // says which way the next four metres are, and the two together are what lets a
            // character walk a straight line between two trunks the grid called one open cell.
            const glm::vec2 steered = navigator->steer(flat, waypoint, std::max(speed * 1.6f, 3.0f));
            if (glm::length(steered) > 0.5f) {
                direction = steered;
            } else {
                // Every local way out is blocked. Re-plan rather than grinding, and count it: a
                // character that cannot make progress must eventually choose somewhere else.
                stuckFor_ += dt;
                // ...unless the body is not blocked but *off the navigable set*, where every ray
                // out of it fails because `pathClear` samples from the body outward. ADR-162's
                // walk back onto the path, now shared with `wander` (ADR-240) and, since ADR-240,
                // turning onto the way out before travelling along it.
                if (escapeToNavigable(navigator, escape_, state, speed, turnRate, dt)) {
                    return false;
                }
                if (stuckFor_ > stuckSeconds_) {
                    stuckFor_ = 0.0f;
                    phase_ = Phase::Navigate;
                    return true;
                }
                state.speed = 0.0f;
                state.activity = Activity::Idle;
                return false;
            }
        }

        // Turn towards the heading before travelling along it, and travel at the fraction of full
        // speed the facing error allows, so a character pivots rather than strafing.
        const float wanted = std::atan2(direction.x, direction.y);
        const float delta = angleDelta(state.yaw, wanted);
        const float step = turnRate * dt;
        const float turned = std::clamp(delta, -step, step);
        state.yaw += turned;
        state.turnRate = turned / std::max(dt, 1e-4f);

        const float alignment = std::max(0.0f, std::cos(angleDelta(state.yaw, wanted)));
        float travelSpeed = speed * alignment;
        // Slow into the last couple of metres instead of stopping dead on the waypoint.
        if (finalLeg) {
            travelSpeed *= std::clamp(toWaypoint / std::max(arrive * 2.5f, 0.5f), 0.25f, 1.0f);
        }
        travelSpeed *= wadeScale(navigator, flat);
        travelSpeed = std::min(travelSpeed, toWaypoint / std::max(dt, 1e-4f));
        const glm::vec2 heading(std::sin(state.yaw), std::cos(state.yaw));
        const glm::vec2 move = heading * travelSpeed * dt;

        // ADR-194: a gap the body could clear. Probed only when actually travelling and roughly
        // facing the way it is going -- a character does not leap sideways out of a turn -- and only
        // forwards along its own heading, so this cannot fire on the separation push or on the
        // penetration resolve below.
        //
        // The probe is two questions of the navigator the walk already asks every frame: is the
        // ground just ahead unwalkable, and is there walkable ground beyond it inside the body's
        // reach? That is a gap by definition, and it needs no gap-detection machinery of its own.
        const float jumpReach = param(jumpRange_, jumpRangeDefault_);

        // ADR-194: a hop because the music said so. Edge-triggered on the signal rising past a
        // half, with the previous value remembered, so a signal that sits high does not launch a
        // hop every frame -- the same shape `spin` uses for an impulse.
        //
        // Deliberately *not* conditional on there being a gap: this is the author's jump, and the
        // body hops forward along the way it is already going. A character that only ever jumped
        // when the terrain demanded it would be an obstacle-avoider rather than a performer.
        if (!jumpSignal_.empty() && jumpReach > 0.0f && navigator != nullptr && !air_.active()) {
            const float now = ctx.signal(jumpSignal_);
            const bool rising = now > 0.5f && jumpArmed_ <= 0.5f;
            jumpArmed_ = now;
            if (rising) {
                const float distance = std::min(jumpReach, std::max(travelSpeed, speed) * 0.55f);
                const glm::vec2 landing = flat + heading * distance;
                const NavSample beyond = navigator->sample(landing);
                // Only onto ground it can stand on. A hop that lands in a lake is worse than no
                // hop, and the navigator already knows the difference.
                if (beyond.navigable) {
                    jump_.maxDistance = jumpReach;
                    const glm::vec3 target(landing.x, beyond.ground, landing.y);
                    if (air_.launch(here, target, jump_)) {
                        state.speed = travelSpeed;
                        state.activity = air_.activity();
                        return false;
                    }
                }
            }
        }

        if (jumpReach > 0.0f && navigator != nullptr && travelSpeed > speed * 0.5f &&
            alignment > 0.8f && !air_.active()) {
            // The arc's reach follows the live parameter, so a signal raising `jumpRange` mid-scene
            // raises how far the body will actually commit to rather than only how far it looks.
            jump_.maxDistance = jumpReach;
            const float probe = std::max(state.radius, 0.5f) + travelSpeed * 0.25f;
            const glm::vec2 ahead = flat + heading * probe;
            if (!navigator->sample(ahead).navigable) {
                // Step out along the heading until the far side appears. Coarse on purpose: the
                // arc lands where the ground is, and a metre of resolution is finer than the body.
                const float reach = param(jumpRange_, jumpRangeDefault_);
                for (float d = probe + 1.0f; d <= reach; d += 1.0f) {
                    const glm::vec2 landing = flat + heading * d;
                    if (!navigator->sample(landing).navigable) {
                        continue;
                    }
                    const glm::vec3 target(landing.x, navigator->groundHeight(landing), landing.y);
                    if (air_.launch(here, target, jump_)) {
                        state.speed = travelSpeed;
                        state.activity = air_.activity();
                        return false;
                    }
                    break; // the near side of the far bank; nothing beyond it is a better landing
                }
            }
        }

        state.travel.x += move.x;
        state.travel.z += move.y;

        // Other characters. Separation rather than avoidance: a body that planned around everyone
        // else would replan every time anyone walked past, and two bodies that each waited for the
        // other would deadlock. A gentle push out of an overlap is what reads as people making room.
        if (ctx.world != nullptr && state.radius > 0.0f) {
            const glm::vec3 among = state.position();
            const glm::vec2 apart =
                ctx.world->crowdSeparation(ctx.self, glm::vec2(among.x, among.z), state.radius);
            const float distance = glm::length(apart);
            if (distance > 1e-4f) {
                // **A push may correct a walk; it may not replace one** (ADR-240).
                //
                // `crowdSeparation` returns half the overlap, and this used to move the body that
                // whole distance in a single frame, capped only at the body's own full walking
                // step. Two consequences, and the second is the visible one.
                //
                // It is frame-rate dependent: half the overlap per frame is twice the separation
                // speed at 120 Hz that it is at 60, which is exactly what ADR-161 says a
                // behaviour's motion must never be.
                //
                // And it is enormous. A fifth of a metre of overlap between two six-metre aliens
                // produces a tenth of a metre of push, which at 60 Hz is six metres a second --
                // more than `rook` walks at. So whenever the walk itself was slow (a turn, an
                // arrival) the push was the *whole* step, and the body was drawn walking forwards
                // while travelling sideways. Measured over ten simulated minutes of the shipped
                // scene with nothing culled: `rook` and `tide` travelled against their own facing
                // on 25.6% and 20.6% of their moving frames, and **every one of those steps was a
                // crowd overlap** -- not one was a solid, and not one was the body's own travel.
                //
                // So separation is a *speed* now, and the speed is the one that says what the
                // mechanism is for: **each body walks out of its own half of the overlap over half
                // a second**, integrated against the real dt, and never faster than it walks. Deep
                // overlaps still resolve at a walk -- two bodies placed inside each other have to
                // get out and be seen to -- and a brush in passing becomes a nudge of a few
                // centimetres a second instead of a shove at cruising speed.
                //
                // Not applied to the penetration resolve below, which is a different kind of
                // statement: a body may not end a frame inside a solid, and that is a guarantee
                // rather than a preference.
                constexpr float kSeparationSeconds = 0.5f;
                const float apartSpeed =
                    std::min(distance / kSeparationSeconds, std::max(speed, 1.0f));
                const float limit = std::min(distance, apartSpeed * dt);
                state.travel.x += apart.x / distance * limit;
                state.travel.z += apart.y / distance * limit;
            }
        }

        // Whatever the steering did not prevent, the field corrects. This is the guarantee rather
        // than the effort: a body may not end a frame inside a solid, however it got there --
        // terrain regenerated under it, an author dropped a rock on it, a seek put it somewhere.
        // Last, so a push out of a crowd can never leave a body inside a rock.
        if (navigator != nullptr) {
            const glm::vec3 after = state.position();
            const glm::vec2 push =
                navigator->resolvePenetration(glm::vec2(after.x, after.z), after.y);
            const float pushLength = glm::length(push);
            if (pushLength > 1e-4f) {
                // Clamped, so a body that somehow ends up deep inside something walks out over a
                // few frames rather than being flung across the valley in one.
                const float limit = std::min(pushLength, std::max(speed, 1.0f) * dt * 2.0f);
                state.travel.x += push.x / pushLength * limit;
                state.travel.z += push.y / pushLength * limit;
            }
        }

        state.speed = travelSpeed;
        const float runSpeed = param(runSpeed_, runSpeedDefault_);
        state.activity = travelSpeed > runSpeed * 0.7f  ? Activity::Run
                         : travelSpeed > 0.05f          ? Activity::Walk
                                                        : Activity::Turn;

        // Progress, and the watchdog on it. Measured against the goal rather than the waypoint, so
        // a character shuffling back and forth between two legs is still recognised as stuck.
        const glm::vec3 nowAt = state.position();
        const float remaining = glm::length(glm::vec2(goal_.x - nowAt.x, goal_.z - nowAt.z));
        if (remaining < bestProgress_ - 0.25f) {
            bestProgress_ = remaining;
            stuckFor_ = 0.0f;
        } else {
            stuckFor_ += dt;
        }
        if (stuckFor_ > stuckSeconds_) {
            stuckFor_ = 0.0f;
            phase_ = Phase::Navigate;
            return false;
        }
        // Re-plan when the route stops being walkable, rather than only on a timer. The world is
        // not static: the craft moves, an editor drops a rock across a leg, terrain is regenerated
        // under a character mid-walk. Checked on the timer's cadence because it re-walks the
        // remaining legs, which is cheap next to a search and not free.
        if (sinceRepath_ > std::max(repathSeconds_, 1.0f)) {
            if (navigator == nullptr || !navigator->pathValid(flat, path_, leg_)) {
                phase_ = Phase::Navigate;
                return false;
            }
            sinceRepath_ = 0.0f;
        }
        return false;
    }

    bool stepArrive(const BehaviorContext& ctx, EntityState& state) {
        const auto dt = static_cast<float>(ctx.dt);
        state.speed = 0.0f;
        const glm::vec3 here = state.position();
        // Turn to face what it came for. An arrival that ends facing whichever way the last leg
        // happened to point is the single clearest tell of a waypoint system.
        const glm::vec2 toGoal(goal_.x - here.x, goal_.z - here.z);
        const float distance = glm::length(toGoal);
        bool facing = true;
        if (distance > 0.3f) {
            const float wanted = std::atan2(toGoal.x, toGoal.y);
            const float delta = angleDelta(state.yaw, wanted);
            const float step = (param(turn_, turnDefault_) / kDegrees) * dt;
            const float turned = std::clamp(delta, -step, step);
            state.yaw += turned;
            state.turnRate = turned / std::max(dt, 1e-4f);
            state.activity = std::abs(delta) > 0.12f ? Activity::Turn : Activity::Idle;
            facing = std::abs(angleDelta(state.yaw, wanted)) <= 0.2f;
        } else {
            state.activity = Activity::Idle;
        }
        timer_ -= dt;
        if (!facing && timer_ > -1.5f) {
            return false; // still turning, and not yet out of patience
        }
        remember(goal_);
        if (ctx.rng != nullptr && ctx.rng->nextFloat() < param(observe_, observeDefault_)) {
            timer_ = sample(ctx, param(observeMin_, observeMinDefault_), param(observeMax_, observeMaxDefault_));
            phase_ = Phase::Observe;
            return true;
        }
        timer_ = sample(ctx, param(idleMin_, idleMinDefault_), param(idleMax_, idleMaxDefault_));
        phase_ = Phase::Idle;
        hasGoal_ = false;
        return false;
    }

    bool stepObserve(const BehaviorContext& ctx, EntityState& state) {
        state.speed = 0.0f;
        state.activity = Activity::Observe;
        // Name the subject; `lookAt` does the turning. The same division `interest` uses, so either
        // can be replaced without touching the other.
        state.lookTarget = goal_;
        state.hasLookTarget = true;
        if (goalKind_ == InterestKind::Character && !goalName_.empty() && ctx.world != nullptr) {
            glm::vec3 now{0.0f};
            if (ctx.world->pointOfInterest(goalName_, now)) {
                state.lookTarget = now; // it is watching the craft, and the craft is moving
            }
        }
        timer_ -= static_cast<float>(ctx.dt);
        if (timer_ > 0.0f) {
            return false;
        }
        hasGoal_ = false;
        timer_ = sample(ctx, param(idleMin_, idleMinDefault_), param(idleMax_, idleMaxDefault_));
        phase_ = Phase::Idle;
        return false;
    }

    void applyGrounding(const BehaviorContext& ctx, EntityState& state, MotionOffset& motion,
                        float speed) {
        if (ctx.nav == nullptr) {
            return;
        }
        GroundSettings settings;
        settings.slopeAlign = param(slopeAlign_, slopeAlignDefault_);
        // A body reads the ground over its own width. A ten-metre creature bridges what a person
        // trips on, and grounding it on a half-metre disc makes it follow detail it would not feel.
        settings.footprint = std::max(param(footprint_, footprintDefault_), 0.0f);
        const glm::vec3 here = state.position();
        const GroundResult ground = ground_.update(*nav(ctx), glm::vec2(here.x, here.z), state.yaw,
                                                   speed, ctx.dt, settings);
        state.travel.y = ground.height - state.anchor.y;
        // Pitch and roll ride on the motion offset rather than on the state's yaw, because the
        // entity composes the two differently: yaw is the body's facing and these are a lean on top
        // of it, and an author's authored rotation has to survive both.
        motion.rotation.x += ground.pitch;
        motion.rotation.z += ground.roll;
    }

    void remember(const glm::vec3& place) {
        recent_.push_back(place);
        if (recent_.size() > kRecent) {
            recent_.erase(recent_.begin());
        }
    }

    // Choose somewhere to go. Weighted over the interest registry, with a chance of simply going
    // for a walk instead -- a character that only ever moved between named places would visit the
    // same five spots forever, which is §6's "looks like a debugging waypoint system" in another
    // costume.
    bool pickGoal(const BehaviorContext& ctx, const EntityState& state) {
        const glm::vec3 here = state.position();
        const glm::vec2 flat(here.x, here.z);
        const float lo = param(minRange_, minRangeDefault_);
        const float hi = std::max(param(maxRange_, maxRangeDefault_), lo + 1.0f);
        const float home = param(home_, homeDefault_);
        const glm::vec2 anchor(state.anchor.x, state.anchor.z);

        const bool stroll = ctx.rng != nullptr && ctx.rng->nextFloat() < strollChance_;
        if (!stroll && ctx.world != nullptr) {
            // **The goal model, which now lives in `entity::goalWeight`** (ADR-330 §3). What was
            // here was the affinity switch, the distance falloff and the visited-place scan, all
            // inlined; what is here now is the same arithmetic called by name, so a guard and an
            // explorer weigh a place with one function instead of two copies of one.
            //
            // The three registered knobs are copied onto the taste each selection rather than
            // held, because a scene may drive `maxRange` from a signal and a taste read once would
            // be the thing ADR-225 calls a decoration.
            GoalTaste taste = goals_.taste();
            taste.minRange = lo;
            taste.maxRange = hi;
            taste.homeRadius = home;
            DecisionContext dctx;
            dctx.time = ctx.time;
            dctx.dt = ctx.dt;
            dctx.self = ctx.self;
            dctx.state = &state;
            dctx.visited = recent_;
            dctx.nav = ctx.nav;
            dctx.world = ctx.world;
            dctx.bus = ctx.bus;
            candidates_.clear();
            goals_.candidates(dctx, taste, candidates_);
            float total = 0.0f;
            for (const GoalCandidate& candidate : candidates_) {
                total += candidate.weight;
            }
            // **The weighted roll stays here, and that is not laziness.** ADR-269's selector takes
            // the highest score subject to a dwell and a margin; `Explore` takes a weighted draw
            // from `ctx.rng`, which is a *stream*. Replacing the draw with an argmax would change
            // every route in Glowmere, and moving the draw anywhere that consumed a different
            // number of values from the stream would re-cast every later choice it makes -- D2 is
            // the rule that exists because of exactly this. So the extraction is of the model and
            // not of the choice, and `tests/unit/test_decision_extraction.cpp` is what says the
            // difference is nothing at all. ADR-330 §4 is the argument for leaving the stream
            // where it is and what it would cost to unify.
            if (total > 0.0f && ctx.rng != nullptr) {
                float roll = ctx.rng->nextFloat() * total;
                for (std::size_t i = 0; i < candidates_.size(); ++i) {
                    roll -= candidates_[i].weight;
                    if (roll <= 0.0f) {
                        goal_ = candidates_[i].position;
                        goalName_ = candidates_[i].name;
                        goalKind_ = candidates_[i].kind;
                        hasGoal_ = true;
                        return true;
                    }
                }
                // Floating point ran out before the list did. Take the last one rather than
                // reporting failure, which would make a rounding error look like an empty world.
                goal_ = candidates_.back().position;
                goalName_ = candidates_.back().name;
                goalKind_ = candidates_.back().kind;
                hasGoal_ = true;
                return true;
            }
        }

        // A walk for its own sake, and the fallback when nothing in the registry is reachable.
        const Navigator* navigator = nav(ctx);
        if (navigator == nullptr || ctx.rng == nullptr) {
            return false;
        }
        const glm::vec2 from = (home > 0.0f && glm::length(flat - anchor) > home) ? anchor : flat;
        // Strolls are shorter than pilgrimages: a random point a hundred metres away is not a
        // stroll, and choosing one is how a character ends up crossing the world for nothing.
        const float strollHi = std::min(hi, std::max(lo + 8.0f, 45.0f));
        glm::vec2 destination{0.0f};
        if (!navigator->pickDestination(*ctx.rng, from, lo, home > 0.0f ? std::min(strollHi, home) : strollHi,
                                        destination)) {
            return false;
        }
        goal_ = glm::vec3(destination.x, navigator->groundHeight(destination), destination.y);
        goalName_.clear();
        goalKind_ = InterestKind::Vista;
        hasGoal_ = true;
        return true;
    }

    static constexpr std::size_t kRecent = 5;

    float speedDefault_, runSpeedDefault_, turnDefault_, arriveDefault_;
    float idleMinDefault_, idleMaxDefault_, observeDefault_, observeMinDefault_, observeMaxDefault_;
    float minRangeDefault_, maxRangeDefault_, homeDefault_, runChanceDefault_, slopeAlignDefault_;
    float bodyRadiusDefault_, headroomDefault_, footprintDefault_, wadeDragDefault_;
    float strollChance_, waypointRadius_, repathSeconds_, stuckSeconds_;
    // The goal model (ADR-330). Held by value because it is configuration -- one taste, one source
    // -- and holds no per-character state of its own; the two things that *are* per-character, the
    // visited list and the live ranges, are handed to it through the `DecisionContext`.
    InterestConsiderer goals_{nullptr};

    params::Parameter<float>* speed_ = nullptr;
    params::Parameter<float>* runSpeed_ = nullptr;
    params::Parameter<float>* turn_ = nullptr;
    params::Parameter<float>* arrive_ = nullptr;
    params::Parameter<float>* idleMin_ = nullptr;
    params::Parameter<float>* idleMax_ = nullptr;
    params::Parameter<float>* observe_ = nullptr;
    params::Parameter<float>* observeMin_ = nullptr;
    params::Parameter<float>* observeMax_ = nullptr;
    params::Parameter<float>* minRange_ = nullptr;
    params::Parameter<float>* maxRange_ = nullptr;
    params::Parameter<float>* home_ = nullptr;
    params::Parameter<float>* runChance_ = nullptr;
    params::Parameter<float>* slopeAlign_ = nullptr;
    params::Parameter<float>* bodyRadius_ = nullptr;
    params::Parameter<float>* headroom_ = nullptr;
    params::Parameter<float>* footprint_ = nullptr;
    params::Parameter<float>* wadeDrag_ = nullptr;
    std::vector<std::string> paths_;
    // This character's own view of the world: the host's navigator with its own size written onto
    // it. A copy is cheap -- the map, the obstacle field and the graph are all shared -- and it is
    // what makes "how big am I" a property of the character rather than of the world, which is the
    // difference between one walker and a cast of them.
    Navigator walker_;
    const Navigator* walkerSource_ = nullptr;
    float walkerRadius_ = -1.0f;
    float walkerHeadroom_ = -1.0f;

    Phase phase_ = Phase::Idle;
    float timer_ = 0.0f;
    std::vector<glm::vec2> path_;
    std::size_t leg_ = 0;
    glm::vec3 goal_{0.0f};
    std::string goalName_;
    InterestKind goalKind_ = InterestKind::Vista;
    bool hasGoal_ = false;
    bool running_ = false;
    float sinceRepath_ = 0.0f;
    float stuckFor_ = 0.0f;
    EscapeMemory escape_;
    float bestProgress_ = 0.0f;
    int failures_ = 0;
    // Whether the last attempt to choose a destination found nowhere to go, and for how long that
    // has been the answer. See the note on `NavDebug::confinedFor`.
    bool confined_ = false;
    float confinedFor_ = 0.0f;
    PathStatus lastStatus_ = PathStatus::Ok;
    std::vector<glm::vec3> recent_;
    GroundFollower ground_;
    // ADR-194: the hop. `jumpRange_` is registered because "how far will it leap" is a creative
    // control an author may want on a timeline or under a signal; `jump_` holds the arc's shape,
    // which is a property of the body rather than of the moment.
    Airborne air_;
    JumpSettings jump_;
    // ADR-194: the signal that makes this body hop, or empty for one that only jumps at gaps. This
    // is what makes a character a modulation target rather than only an obstacle-avoider -- a beat
    // is a reason to jump, and the engine already delivers beats to behaviours.
    std::string jumpSignal_;
    float jumpArmed_ = 0.0f;
    params::Parameter<float>* jumpRange_ = nullptr;
    float jumpRangeDefault_ = 0.0f;
    // Scratch for the weighted pick, kept so a selection every few seconds does not allocate.
    std::vector<GoalCandidate> candidates_;
};

// ---- ground ----------------------------------------------------------------------------------
//
// Grounding on its own, for a character whose horizontal motion comes from somewhere else: a
// keyframed walk, a spline, a future scripted sequence. `explore` already grounds itself, so this
// is not needed alongside it -- it is the same component (ADR-093, §4, §7) exposed as a behaviour
// so that "follow the terrain" is available without also taking a mind.
class Ground final : public IBehavior {
public:
    explicit Ground(const nlohmann::json* s)
        : alignDefault_(readFloat(s, "slopeAlign", 0.55f)),
          smoothDefault_(readFloat(s, "smoothingMs", 85.0f)),
          floatDefault_(readFloat(s, "maxFloat", 0.22f)),
          tiltDefault_(readFloat(s, "maxTilt", 34.0f)) {}

    [[nodiscard]] std::string_view kind() const override { return "ground"; }

    void registerParameters(params::ParameterSet& params, const std::string& prefix) override {
        align_ = &params.add(floatDesc(prefix + "slopeAlign", alignDefault_, 0.0f, 1.0f));
        smooth_ = &params.add(floatDesc(prefix + "smoothingMs", smoothDefault_, 0.0f, 4000.0f));
        float_ = &params.add(floatDesc(prefix + "maxFloat", floatDefault_, 0.0f, 20.0f));
        tilt_ = &params.add(floatDesc(prefix + "maxTilt", tiltDefault_, 0.0f, 90.0f));
        paths_ = {prefix + "slopeAlign", prefix + "smoothingMs", prefix + "maxFloat",
                  prefix + "maxTilt"};
    }
    void collectParameterPaths(std::vector<std::string>& out) const override {
        out.insert(out.end(), paths_.begin(), paths_.end());
    }
    void reset(Rng&) override { follower_.reset(); }

    void update(const BehaviorContext& ctx, EntityState& state, MotionOffset& motion) override {
        if (ctx.nav == nullptr) {
            return;
        }
        // The body is off the ground because a shot put it there (ADR-210). Pinning it back to the
        // surface -- and tilting it to a slope it is nowhere near -- is the one thing grounding must
        // not do to it. Yielding by *keeping* the follower's state, so a body set down again picks
        // up its smoothed height rather than snapping to the terrain.
        if (state.airborne) {
            return;
        }
        GroundSettings settings;
        settings.slopeAlign = align_ != nullptr ? align_->value() : alignDefault_;
        settings.heightSmoothingMs = smooth_ != nullptr ? smooth_->value() : smoothDefault_;
        settings.maxFloat = float_ != nullptr ? float_->value() : floatDefault_;
        settings.maxTilt = tilt_ != nullptr ? tilt_->value() : tiltDefault_;
        const glm::vec3 here = state.position();
        const GroundResult ground = follower_.update(*ctx.nav, glm::vec2(here.x, here.z), state.yaw,
                                                     state.speed, ctx.dt, settings);
        state.travel.y = ground.height - state.anchor.y;
        motion.rotation.x += ground.pitch;
        motion.rotation.z += ground.roll;
    }

private:
    float alignDefault_, smoothDefault_, floatDefault_, tiltDefault_;
    params::Parameter<float>* align_ = nullptr;
    params::Parameter<float>* smooth_ = nullptr;
    params::Parameter<float>* float_ = nullptr;
    params::Parameter<float>* tilt_ = nullptr;
    std::vector<std::string> paths_;
    GroundFollower follower_;
};

// ---- decide ----------------------------------------------------------------------------------
//
// The decider (ADR-269, ADR-330). **A character kind, expressed as scene data.**
//
// This is the one behaviour in the vocabulary that has no behaviour of its own. It scores options,
// commits to one, and pushes that option's `ActionDesc` list onto `Authority::Routine`; the
// existing queue does everything after that. There is no second interpreter here -- no
// running/succeeded/failed enum beside `ActionResult`, no tree walker, no per-frame re-evaluation
// of anything the queue is in the middle of.
//
// **The product claim this exists to make good on.** Before it, the only autonomous mind in this
// engine was `Explore`: 700 lines, one class, one personality, which is why every autonomous
// character in Glowmere is an explorer and why a guard would have been a second 700-line class. A
// guard is now this:
//
//     { "kind": "decide", "hertz": 2, "considerers": [
//         { "kind": "holdPost", "post": "gate", "pull": 0.30 },
//         { "kind": "investigate", "kinds": ["character"], "weight": 2.2 },
//         { "kind": "idle" } ] }
//
// and an explorer that reads its senses instead of the omniscient list is the same behaviour with
// `interest` in the list instead. Neither is a C++ class.
//
// **Two memories, and both live here rather than in a considerer.** ADR-269's rule is that a
// considerer holds no per-character state, which is what makes ADR-267's D4 free -- there is
// nothing in a considerer to checkpoint. So the two things a decider genuinely has to remember are
// held by the behaviour: `PerceptMemory`, the bounded fade over percepts that ADR-290 §7 named as
// the thing to revisit first, and `visited_`, the places this body has already been, which is the
// goal model's novelty term. Both are bounded, both are cleared by `reset`, and both are
// reconstructed by a replay rather than persisted.
//
// **R1/R3.** It reads `state().position()` and writes nothing: not `travel`, not `MotionOffset`,
// not a parameter. Scoring is a read and acting is the queue's job (R3, ADR-210). The only thing it
// writes anywhere is a list of intentions onto its own entity's queue.
class Decide final : public IBehavior {
public:
    explicit Decide(const nlohmann::json* s)
        : hertzDefault_(readFloat(s, "hertz", 2.0f)),
          dwellDefault_(readFloat(s, "dwellTicks", 2.0f)),
          marginDefault_(readFloat(s, "margin", 0.08f)),
          memorySecondsDefault_(readFloat(s, "memorySeconds", 0.0f)),
          memoryCapacity_(static_cast<std::uint16_t>(
              std::clamp(readFloat(s, "memoryCapacity", 8.0f), 0.0f, 64.0f))),
          visitedCapacity_(static_cast<std::size_t>(
              std::clamp(readFloat(s, "visitedCapacity", 5.0f), 0.0f, 64.0f))) {
        if (s != nullptr && s->is_object() && s->contains("considerers") &&
            (*s)["considerers"].is_array()) {
            for (const auto& entry : (*s)["considerers"]) {
                if (!entry.is_object() || !entry.contains("kind") || !entry["kind"].is_string()) {
                    continue;
                }
                const std::string kind = entry["kind"].get<std::string>();
                auto made = makeConsiderer(kind, &entry);
                if (made != nullptr) {
                    considerers_.push_back(std::move(made));
                    continue;
                }
                // Loud, by name, with the vocabulary. A misspelled considerer is a character that
                // silently loses one of the things it was meant to want, and a guard whose
                // `investigate` never loads is a guard that stands still for the right-looking
                // reason -- which is the shape of defect this repository has shipped five of
                // (entity.cpp does exactly this for a misspelled behaviour kind).
                std::string known;
                for (const std::string_view k : considererKinds()) {
                    if (!known.empty()) {
                        known += ", ";
                    }
                    known += k;
                }
                log::warn("decide: unknown considerer kind '{}' (known: {})", kind, known);
            }
        }
        views_.reserve(considerers_.size());
        for (const auto& considerer : considerers_) {
            views_.push_back(considerer.get());
        }
    }

    [[nodiscard]] std::string_view kind() const override { return "decide"; }

    void registerParameters(params::ParameterSet& params, const std::string& prefix) override {
        hertz_ = &params.add(floatDesc(prefix + "hertz", hertzDefault_, 0.0f, 60.0f));
        dwell_ = &params.add(floatDesc(prefix + "dwellTicks", dwellDefault_, 0.0f, 600.0f));
        margin_ = &params.add(floatDesc(prefix + "margin", marginDefault_, 0.0f, 100.0f));
        memorySeconds_ =
            &params.add(floatDesc(prefix + "memorySeconds", memorySecondsDefault_, 0.0f, 600.0f));
        paths_ = {prefix + "hertz", prefix + "dwellTicks", prefix + "margin",
                  prefix + "memorySeconds"};
        // Every considerer's knobs, under its own name. ADR-225: a weight an author wrote in a
        // scene file and the engine then read once from the JSON would be a decoration, not a
        // setting -- it could not be keyframed, modulated, saved or driven by a signal, which is
        // every one of the things this project means by a parameter.
        for (const auto& considerer : considerers_) {
            const std::string base = prefix + std::string(considerer->name()) + "/";
            considerer->registerParameters(params, base);
            considerer->collectParameterPaths(paths_);
        }
    }
    void collectParameterPaths(std::vector<std::string>& out) const override {
        out.insert(out.end(), paths_.begin(), paths_.end());
    }

    void reset(Rng&) override {
        selector_.reset();
        memory_.reset();
        visited_.clear();
        scored_.clear();
        queue_ = nullptr;
        lastMargin_ = 0.0f;
    }

    void update(const BehaviorContext& ctx, EntityState& state, MotionOffset& motion) override {
        (void)motion;
        queue_ = ctx.actions;
        if (ctx.actions == nullptr || considerers_.empty() || ctx.world == nullptr) {
            return;
        }
        const Entity* self = ctx.self < ctx.world->entities().size()
                                 ? ctx.world->entities()[ctx.self].get()
                                 : nullptr;

        SelectorSettings settings;
        settings.hertz = hertz_ != nullptr ? hertz_->value() : hertzDefault_;
        settings.dwellTicks = dwell_ != nullptr ? dwell_->value() : dwellDefault_;
        settings.margin = margin_ != nullptr ? margin_->value() : marginDefault_;
        selector_.setSettings(settings);
        memory_.setSettings(PerceptMemory::Settings{
            memorySeconds_ != nullptr ? memorySeconds_->value() : memorySecondsDefault_,
            memoryCapacity_});

        DecisionContext dctx;
        dctx.time = ctx.time;
        dctx.dt = ctx.dt;
        dctx.self = ctx.self;
        dctx.state = &state;
        dctx.percepts =
            memory_.merge(self != nullptr ? self->percepts() : std::span<const Percept>(), ctx.time);
        dctx.visited = visited_;
        dctx.nav = ctx.nav;
        dctx.world = ctx.world;
        dctx.bus = ctx.bus;
        dctx.seed = self != nullptr ? self->seed() : 0;

        // Republish the overlay's list whenever a tick actually fired -- including the very first
        // one, whose tick index may legitimately be the same 0 the selector starts at. Testing the
        // index alone left the first decision's options invisible, which is the one decision a
        // person watching a character start up is most likely to be looking at.
        const std::uint64_t before = selector_.tick();
        const bool startedBefore = selector_.started();
        const bool changed = selector_.select(dctx, views_);
        if (!startedBefore || selector_.tick() != before) {
            publish();
        }
        if (!changed) {
            return;
        }
        const std::size_t chosen = selector_.chosen();
        if (chosen >= selector_.options().size()) {
            return;
        }
        const Option& winner = selector_.options()[chosen];
        // `override` rather than `push`: a new decision *replaces* the routine, and whatever the
        // routine was in the middle of is reported Cancelled rather than dropped in silence. A
        // push would queue the new errand behind the abandoned one, which is the opposite of
        // changing your mind.
        //
        // The tiers above are untouched, so a director shot or a one-off action still preempts a
        // decider exactly as it preempts a schedule (ADR-091), and the decision resumes underneath
        // it afterwards.
        ctx.actions->override(std::vector<ActionDesc>(winner.actions.begin(), winner.actions.end()),
                              Authority::Routine, ctx.time);
        remember(state.position());
    }

    [[nodiscard]] bool decisionDebug(DecisionDebug& out) const override {
        out.options = scored_;
        out.chosen = selector_.current();
        out.tick = selector_.tick();
        out.committedTick = selector_.committedTick();
        out.margin = lastMargin_;
        const Selector::Counts counts = selector_.counts();
        out.decisions = counts.decisions;
        out.dwellRejections = counts.dwellRejections;
        out.marginRejections = counts.marginRejections;
        out.remembered = memory_.remembered();
        return true;
    }

    // The route the winning option is walking, borrowed from the queue that is walking it. A
    // decider owns no route of its own -- that is the whole point of pushing actions rather than
    // moving the body -- so the honest answer is the queue's, and the phase is the option's name.
    [[nodiscard]] bool navDebug(NavDebug& out) const override {
        if (queue_ == nullptr) {
            return false;
        }
        out.route = queue_->route();
        out.leg = queue_->routeLeg();
        out.hasDestination = !out.route.empty();
        if (out.hasDestination) {
            out.destination = glm::vec3(out.route.back().x, 0.0f, out.route.back().y);
        }
        out.phase = selector_.current();
        return true;
    }

private:
    // The scored list, flattened for the overlay, once per decision tick rather than per frame.
    void publish() {
        scored_.clear();
        const std::span<const Option> options = selector_.options();
        const std::size_t chosen = selector_.chosen();
        float best = 0.0f;
        float runnerUp = 0.0f;
        for (std::size_t i = 0; i < options.size(); ++i) {
            scored_.push_back(ScoredOption{options[i].name, options[i].score, i == chosen});
            if (options[i].score > best) {
                runnerUp = best;
                best = options[i].score;
            } else if (options[i].score > runnerUp) {
                runnerUp = options[i].score;
            }
        }
        lastMargin_ = best - runnerUp;
    }

    // **Where the body was when it changed its mind** -- not where it was going.
    //
    // The distinction cost a measurement to find. Recording the destination is the obvious thing
    // and it is wrong: the goal model suppresses a place in `visited_` to 0.12 of its weight, so a
    // body that recorded its errand on departure devalued the errand it had just set out on, and
    // the option it was walking to fell behind the three it was not. Measured on the guard fixture:
    // the explorer scored six options, changed its mind at every decision tick, and travelled
    // **0.00 m in 75 seconds**, with the winner in the overlay never being the highest score.
    //
    // `Explore` never had this, because `remember(goal_)` is called in `stepArrive` -- on arrival,
    // not on departure. The position is the same fact seen from the other end and it needs no
    // arrival event: a body that walked to a cairn is standing at the cairn when it decides what to
    // do next, and a body that gave up halfway records the halfway point, which is honest.
    void remember(const glm::vec3& place) {
        if (visitedCapacity_ == 0) {
            return;
        }
        visited_.push_back(place);
        while (visited_.size() > visitedCapacity_) {
            visited_.erase(visited_.begin());
        }
    }

    float hertzDefault_, dwellDefault_, marginDefault_, memorySecondsDefault_;
    std::uint16_t memoryCapacity_ = 8;
    std::size_t visitedCapacity_ = 5;
    params::Parameter<float>* hertz_ = nullptr;
    params::Parameter<float>* dwell_ = nullptr;
    params::Parameter<float>* margin_ = nullptr;
    params::Parameter<float>* memorySeconds_ = nullptr;
    std::vector<std::string> paths_;

    std::vector<std::unique_ptr<StockConsiderer>> considerers_;
    std::vector<const IConsiderer*> views_;
    Selector selector_;
    PerceptMemory memory_;
    std::vector<glm::vec3> visited_;
    std::vector<ScoredOption> scored_;
    float lastMargin_ = 0.0f;
    const ActionQueue* queue_ = nullptr;
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
    return {"hover",  "drift",  "bank",     "spin",  "wander",
            "explore", "ground", "liveliness", "lookAt", "interest", "orbit", "decide"};
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
    if (kind == "explore") {
        return std::make_unique<Explore>(settings);
    }
    if (kind == "ground") {
        return std::make_unique<Ground>(settings);
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
    if (kind == "liveliness") {
        return std::make_unique<Liveliness>(settings);
    }
    if (kind == "decide") {
        return std::make_unique<Decide>(settings);
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
    case Activity::Jump: return "jump";
    case Activity::Fall: return "fall";
    case Activity::Land: return "land";
    }
    return "idle";
}

bool activityFromName(std::string_view name, Activity& out) {
    constexpr Activity kAll[] = {Activity::Idle,  Activity::Walk,    Activity::Run,
                                 Activity::Turn,  Activity::Observe, Activity::React,
                                 Activity::Jump,  Activity::Fall,    Activity::Land};
    for (const Activity a : kAll) {
        if (name == activityName(a)) {
            out = a;
            return true;
        }
    }
    return false;
}

} // namespace avgen::entity
