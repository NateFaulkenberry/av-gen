#include "scene/motion_quality.hpp"

#include "scene/animation.hpp"

#include <algorithm>
#include <cmath>

#include <fmt/format.h>

namespace avgen::scene {
namespace {

void line(std::string& out, const char* name, const QualityMetric& m, const char* unit) {
    if (m.measured) {
        out += fmt::format("  {:<16} {:8.4f} {}\n", name, m.value, unit);
    } else {
        // **Said, not omitted.** A metric missing from a report reads as zero to anyone skimming,
        // and zero is the good answer for three of these.
        out += fmt::format("  {:<16} {:>8} (nothing to measure it against)\n", name, "--");
    }
}

} // namespace

std::string MotionQualityReport::report() const {
    std::string out = fmt::format("{}\n", clip);
    line(out, "foot slide", footSlide, "units");
    line(out, "contact height", contactHeight, "units");
    line(out, "limb extension", limbExtension, "of limb");
    line(out, "body correction", bodyCorrection, "units");
    line(out, "reach error", reachError, "units");
    line(out, "velocity error", velocityError, "m/s");
    return out;
}

std::string MotionQualityVerdict::report() const {
    if (pass) {
        return "PASS\n";
    }
    std::string out = "FAIL\n";
    for (const std::string& f : failures) {
        out += fmt::format("  {}\n", f);
    }
    return out;
}

MotionQualityReport measureMotionQuality(const Skeleton& skeleton, const AnimationClip& clip,
                                         const MotionQualityOptions& options) {
    MotionQualityReport out;
    out.clip = clip.name;
    if (!skeleton.valid() || clip.length() <= 0.0f) {
        return out;
    }

    // ---- foot slide and contact height, from the contact tracks -------------------------------
    //
    // `detectContacts` already computes the slide (ADR-546/552), so this reads it rather than
    // running a second detector. Two answers to "is this foot down" is how ADR-260 began.
    if (!options.contacts.empty()) {
        ContactSettings settings;
        settings.sampleRate = options.sampleRate;
        const std::vector<ContactTrack> tracks =
            detectContacts(skeleton, clip, options.contacts, settings);
        bool any = false;
        float worstSlide = 0.0f;
        for (const ContactTrack& t : tracks) {
            if (t.jointIndex < 0 || t.spans.empty()) {
                continue;
            }
            any = true;
            worstSlide = std::max(worstSlide, t.worstSlide);
        }
        if (any) {
            out.footSlide.value = worstSlide;
            out.footSlide.measured = true;
        }

        // Contact height: how far a planted joint's height wanders inside its own stance. A foot
        // that slides along the ground and one that bobs through it are different defects and the
        // slide metric cannot tell them apart.
        const float rate = std::max(options.sampleRate, 1.0f);
        const auto frames =
            static_cast<std::uint32_t>(std::max(2.0f, std::floor(clip.length() * rate + 0.5f) + 1.0f));
        Pose pose;
        std::vector<glm::mat4> model;
        float worstHeight = 0.0f;
        bool heightMeasured = false;
        for (const ContactTrack& t : tracks) {
            if (t.jointIndex < 0) {
                continue;
            }
            for (const ContactSpan& span : t.spans) {
                float lo = 0.0f;
                float hi = 0.0f;
                bool first = true;
                for (std::uint32_t f = 0; f < frames; ++f) {
                    const float local = static_cast<float>(f) / rate;
                    const bool inside = span.wraps() ? (local >= span.start || local <= span.end)
                                                     : (local >= span.start && local <= span.end);
                    if (!inside) {
                        continue;
                    }
                    setRestPose(skeleton, pose);
                    sampleClip(clip, std::min(clip.start + local, clip.duration), pose);
                    poseToModel(skeleton, pose, model);
                    const float y = model[static_cast<std::size_t>(t.jointIndex)][3].y;
                    if (first) {
                        lo = hi = y;
                        first = false;
                    }
                    lo = std::min(lo, y);
                    hi = std::max(hi, y);
                }
                if (!first) {
                    worstHeight = std::max(worstHeight, hi - lo);
                    heightMeasured = true;
                }
            }
        }
        out.contactHeight.value = worstHeight;
        out.contactHeight.measured = heightMeasured;
    }

    // ---- limb extension ------------------------------------------------------------------------
    //
    // The `reach` probe from ADR-553, as a metric: root-to-tip distance over the limb's own
    // length. 1.0 is a straight limb and past it the pose is asking for a length the bones do not
    // have. On a rig whose limbs are not ancestor chains this is measured in model space, which is
    // the only frame where the three joints are comparable at all.
    if (!options.limbs.empty()) {
        const float rate = std::max(options.sampleRate, 1.0f);
        const auto frames =
            static_cast<std::uint32_t>(std::max(2.0f, std::floor(clip.length() * rate + 0.5f) + 1.0f));
        Pose rest;
        std::vector<glm::mat4> restModel;
        setRestPose(skeleton, rest);
        poseToModel(skeleton, rest, restModel);
        Pose pose;
        std::vector<glm::mat4> model;
        float worst = 0.0f;
        bool measured = false;
        for (const QualityLimb& limb : options.limbs) {
            const int r = skeleton.find(limb.root);
            const int m = skeleton.find(limb.mid);
            const int t = skeleton.find(limb.tip);
            if (r < 0 || m < 0 || t < 0) {
                continue;
            }
            const glm::vec3 rr(restModel[static_cast<std::size_t>(r)][3]);
            const glm::vec3 rm(restModel[static_cast<std::size_t>(m)][3]);
            const glm::vec3 rt(restModel[static_cast<std::size_t>(t)][3]);
            const float length = glm::length(rm - rr) + glm::length(rt - rm);
            if (length < 1e-5f) {
                continue;
            }
            measured = true;
            for (std::uint32_t f = 0; f < frames; ++f) {
                setRestPose(skeleton, pose);
                sampleClip(clip, std::min(clip.start + (static_cast<float>(f) / rate), clip.duration),
                           pose);
                poseToModel(skeleton, pose, model);
                const glm::vec3 a(model[static_cast<std::size_t>(r)][3]);
                const glm::vec3 b(model[static_cast<std::size_t>(t)][3]);
                worst = std::max(worst, glm::length(b - a) / length);
            }
        }
        out.limbExtension.value = worst;
        out.limbExtension.measured = measured;
    }
    return out;
}

MotionQualityVerdict gateMotionQuality(const MotionQualityReport& report,
                                       const MotionQualityLimits& limits) {
    MotionQualityVerdict out;
    const auto check = [&](const char* name, const QualityMetric& m, float limit, const char* unit) {
        // An UNMEASURED metric never fails. Refusing a clip because nobody supplied a contact
        // track would reject every hand-authored clip in this repository, and "I cannot tell" is
        // not "this is bad".
        if (m.exceeds(limit)) {
            out.pass = false;
            out.failures.push_back(
                fmt::format("{} {:.4f} {} exceeds {:.4f}", name, m.value, unit, limit));
        }
    };
    check("foot slide", report.footSlide, limits.maxFootSlide, "units");
    check("contact height", report.contactHeight, limits.maxContactHeight, "units");
    check("limb extension", report.limbExtension, limits.maxLimbExtension, "of limb");
    check("body correction", report.bodyCorrection, limits.maxBodyCorrection, "units");
    return out;
}

} // namespace avgen::scene
