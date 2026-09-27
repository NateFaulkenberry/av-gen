#include "scene/follow_reference.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace avgen::scene {
namespace {

// The kernel's distribution function: the integral of w^2 tau e^(-w tau) from 0 to tau, a Gamma(2)
// distribution with rate w.
double kernelCdf(double w, double tau) {
    if (tau <= 0.0) {
        return 0.0;
    }
    const double x = w * tau;
    return 1.0 - (std::exp(-x) * (1.0 + x));
}

// The index of the last tap of a kernel of mean delay `seconds`. The kernel is cut at 4 T.
std::size_t lastTap(double seconds) {
    if (!(seconds > 0.0)) {
        return 0;
    }
    return static_cast<std::size_t>(std::ceil((kFollowKernelSupport * seconds / kFollowKernelStep) - 1e-9));
}

world::HistorySample lerpSample(const world::HistorySample& a, const world::HistorySample& b, double t) {
    const double span = b.t - a.t;
    const float f = span > 0.0 ? static_cast<float>(std::clamp((t - a.t) / span, 0.0, 1.0)) : 1.0f;
    world::HistorySample out;
    out.t = t;
    out.position = glm::mix(a.position, b.position, f);
    out.rotation = glm::slerp(a.rotation, b.rotation, f);
    out.scale = glm::mix(a.scale, b.scale, f);
    return out;
}

// An angle difference folded into (-pi, pi], so an unwrapped heading steps the short way round.
double wrapAngle(double a) {
    constexpr double kTwoPi = 2.0 * std::numbers::pi;
    a = std::fmod(a + std::numbers::pi, kTwoPi);
    if (a < 0.0) {
        a += kTwoPi;
    }
    return a - std::numbers::pi;
}

} // namespace

double followKernelReach(double seconds) {
    return static_cast<double>(lastTap(seconds)) * kFollowKernelStep;
}

double followKernelWeight(double seconds, std::size_t k) {
    if (!(seconds > 0.0)) {
        return k == 0 ? 1.0 : 0.0;
    }
    if (k > lastTap(seconds)) {
        return 0.0;
    }
    const double w = 2.0 / seconds;
    const double tau = static_cast<double>(k) * kFollowKernelStep;
    return kernelCdf(w, tau + (0.5 * kFollowKernelStep)) - kernelCdf(w, tau - (0.5 * kFollowKernelStep));
}

// ---- the trail ------------------------------------------------------------------------------------

SubjectTrail::SubjectTrail(const world::HistoryBank* bank, std::size_t ring, double now,
                           const world::HistorySample& head)
    : bank_(bank), ring_(ring), now_(now), head_(head) {
    head_.t = now;
    if (bank_ == nullptr || ring_ >= bank_->ringCount()) {
        return;
    }
    count_ = bank_->sampleCount(ring_);
    // The samples at or after `now` are the landing instant a seek replay recorded (or, after a jump
    // the seek path did not see, a future). The head stands in for them, as it does in a play.
    while (count_ > 0 && bank_->sample(ring_, count_ - 1).t >= now_ - 1e-9) {
        --count_;
    }
}

world::HistorySample SubjectTrail::at(double seconds) const {
    if (count_ == 0 || seconds >= now_) {
        world::HistorySample s = head_;
        s.t = seconds;
        return s;
    }
    const world::HistorySample& newest = bank_->sample(ring_, count_ - 1);
    if (seconds >= newest.t) {
        return lerpSample(newest, head_, seconds);
    }
    const world::HistorySample& oldest = bank_->sample(ring_, 0);
    if (seconds <= oldest.t) {
        world::HistorySample s = oldest;
        s.t = seconds;
        return s;
    }
    world::HistorySample out;
    if (!bank_->sampleAt(ring_, seconds, out)) {
        // Unreachable: `seconds` lies inside [oldest, newest] of the samples held. Answering with the
        // oldest keeps the result a function of the history rather than of an accident.
        out = oldest;
        out.t = seconds;
    }
    return out;
}

// ---- the reference --------------------------------------------------------------------------------

float footprintHeight(const FollowGround& ground, glm::vec2 xz, float radius) {
    if (!(radius > 0.0f)) {
        return ground.heightAt(xz);
    }
    const float centre = ground.heightAt(xz);
    const float sum = (2.0f * centre) + ground.heightAt(xz + glm::vec2(radius, 0.0f)) +
                      ground.heightAt(xz - glm::vec2(radius, 0.0f)) + ground.heightAt(xz + glm::vec2(0.0f, radius)) +
                      ground.heightAt(xz - glm::vec2(0.0f, radius));
    return sum / 6.0f;
}

float headingOf(const glm::quat& rotation, float fallback) {
    const glm::vec3 forward = rotation * glm::vec3(0.0f, 0.0f, 1.0f);
    if ((forward.x * forward.x) + (forward.z * forward.z) < 1e-10f) {
        return fallback;
    }
    return std::atan2(forward.x, forward.z);
}

float softFloor(float y, float floor, float width) {
    if (!(width > 0.0f)) {
        return std::max(y, floor);
    }
    const double x = (static_cast<double>(y) - static_cast<double>(floor)) / static_cast<double>(width);
    // softplus(x) = ln(1 + e^x), written so neither branch overflows.
    const double softplus = std::max(x, 0.0) + std::log1p(std::exp(-std::abs(x)));
    return static_cast<float>(static_cast<double>(floor) + (static_cast<double>(width) * softplus));
}

FollowReference followReference(const SubjectTrail& trail, double seconds, const FollowFilter& filter,
                                const FollowGround* ground) {
    const double th = std::max(filter.horizontalSeconds, 0.0);
    const double tv = std::max(filter.verticalSeconds, 0.0);
    const double tp = std::max(filter.headingSeconds, 0.0);
    const bool lead = filter.lead > 0.0f && th > 0.0;
    const std::size_t kh = lastTap(th);
    const std::size_t kv = lastTap(tv);
    const std::size_t kp = lastTap(tp);
    // The lead's difference kernel reaches one tap past the horizontal kernel's last.
    const std::size_t last = std::max({kh + (lead ? 1u : 0u), kv, kp});

    // One pass over the taps, every kernel accumulated from the same read of the trail. Doubles, so
    // a sum of a few hundred taps does not round away a millimetre of a camera 200 m from the origin.
    glm::dvec2 horizontal(0.0);
    glm::dvec2 velocity(0.0);
    glm::dvec2 verticalPath(0.0); // the horizontal path under the vertical kernel, for the ground term
    double height = 0.0;
    double headingSum = 0.0;
    double weightH = 0.0;
    double weightV = 0.0;
    double weightP = 0.0;
    double meanDelay = 0.0;
    double previousWh = 0.0;
    double unwrapped = 0.0;
    float previousYaw = 0.0f;
    for (std::size_t k = 0; k <= last; ++k) {
        const world::HistorySample s = trail.at(seconds - (static_cast<double>(k) * kFollowKernelStep));
        const glm::dvec2 xz(s.position.x, s.position.z);
        const double wh = k <= kh ? followKernelWeight(th, k) : 0.0;
        horizontal += wh * xz;
        weightH += wh;
        meanDelay += wh * static_cast<double>(k) * kFollowKernelStep;
        if (lead) {
            velocity += (wh - previousWh) * xz;
        }
        previousWh = wh;
        if (k <= kv) {
            const double wv = followKernelWeight(tv, k);
            height += wv * static_cast<double>(s.position.y);
            verticalPath += wv * xz;
            weightV += wv;
        }
        if (k <= kp) {
            const float yaw = headingOf(s.rotation, previousYaw);
            unwrapped = k == 0 ? static_cast<double>(yaw)
                               : unwrapped + wrapAngle(static_cast<double>(yaw) - static_cast<double>(previousYaw));
            previousYaw = yaw;
            const double wp = followKernelWeight(tp, k);
            headingSum += wp * unwrapped;
            weightP += wp;
        }
    }

    FollowReference out;
    const glm::dvec2 smoothed = horizontal / weightH;
    glm::dvec2 xz = smoothed;
    if (lead) {
        // The kernel's own mean delay rather than the nominal T, so `lead` 1 cancels a steady walk's
        // lag exactly and not to within the discretisation.
        const glm::dvec2 v = velocity / (weightH * kFollowKernelStep);
        xz += static_cast<double>(std::clamp(filter.lead, 0.0f, 1.0f)) * (meanDelay / weightH) * v;
    }
    double y = height / weightV;
    if (filter.ground && ground != nullptr) {
        // The ground under the horizontally SMOOTHED path, not the led one: the lead's extrapolation
        // overshoots every stop, and on a slope that overshoot is a vertical bounce. The height then
        // lags a descent by the horizontal constant rather than the vertical one -- 0.3 s instead of
        // 0.8 on GV3 -- which is the whole of what this term is for.
        const glm::vec2 at(static_cast<float>(smoothed.x), static_cast<float>(smoothed.y));
        const glm::dvec2 laggedPath = verticalPath / weightV;
        const glm::vec2 lagged(static_cast<float>(laggedPath.x), static_cast<float>(laggedPath.y));
        y += static_cast<double>(footprintHeight(*ground, at)) - static_cast<double>(footprintHeight(*ground, lagged));
    }
    out.position = glm::vec3(static_cast<float>(xz.x), static_cast<float>(y), static_cast<float>(xz.y));
    out.heading = static_cast<float>(headingSum / weightP);
    return out;
}

} // namespace avgen::scene
