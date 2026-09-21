#include "scene/motion_variants.hpp"

#include "scene/animation.hpp"

#include <algorithm>
#include <cmath>

#include <fmt/format.h>

namespace avgen::scene {

const char* variantKindName(VariantKind kind) {
    switch (kind) {
    case VariantKind::Source: return "source";
    case VariantKind::SpeedWarp: return "speed";
    case VariantKind::StrideWarp: return "stride";
    case VariantKind::Mirror: return "mirror";
    }
    return "source";
}

std::string VariantProvenance::describe() const {
    return fmt::format("{} <- {} x{:.3f} for {:.2f} m/s [{}]", variantKindName(kind), source,
                       parameter, targetSpeed, tool);
}

std::string CoverageReport::report() const {
    return fmt::format(
        "coverage {:.0f}% ({} of {} speeds)   cost {} samples, {:.1f} us/scan\n", coverage * 100.0f,
        covered, total, samples, scanMicroseconds);
}

CoverageReport measureCoverage(const std::vector<MotionVariant>& variants,
                               const std::vector<float>& targets, float tolerance,
                               float sampleRate) {
    CoverageReport out;
    out.total = static_cast<std::uint32_t>(targets.size());
    for (const float target : targets) {
        const float band = std::max(std::abs(target) * tolerance, 1e-3f);
        for (const MotionVariant& v : variants) {
            if (std::abs(v.provenance.targetSpeed - target) <= band) {
                ++out.covered;
                break;
            }
        }
    }
    out.coverage = out.total > 0 ? static_cast<float>(out.covered) / out.total : 0.0f;

    // The opposing quantity. Every variant is samples in the database, and Phase C measured a
    // linear scan at **5.9 ns per sample** over 85,610 real samples. So the cost of coverage is
    // not disk, it is the scan every character pays ten times a second.
    const float rate = std::max(sampleRate, 1.0f);
    for (const MotionVariant& v : variants) {
        out.samples += static_cast<std::uint32_t>(std::max(0.0f, v.clip.length() * rate)) + 1u;
    }
    out.scanMicroseconds = static_cast<float>(out.samples) * 5.9e-3f;
    return out;
}

namespace {

// Time-warp: the same motion at a different pace. The keys move in time and nothing moves in
// space, which is what distinguishes this from a stride warp.
AnimationClip timeWarp(const AnimationClip& source, float factor) {
    AnimationClip out = source;
    const float scale = 1.0f / std::max(factor, 1e-3f);
    for (AnimationChannel& channel : out.channels) {
        for (float& t : channel.times) {
            t = (t - source.start) * scale + source.start;
        }
    }
    out.duration = (source.duration - source.start) * scale + source.start;
    return out;
}

// Stride warp: the step lengthened or shortened in space at the same pace. Only the translation
// channels of the joints that move horizontally are touched, and **the vertical is left alone**
// for the reason §37 gives -- scaling Y sinks a foot through the ground it was authored on.
AnimationClip strideWarp(const AnimationClip& source, float factor) {
    AnimationClip out = source;
    for (AnimationChannel& channel : out.channels) {
        if (channel.path != AnimationPath::Translation) {
            continue;
        }
        // Scale the deviation from the channel's own first key, so a limb keeps its rest offset
        // and only its excursion changes. Scaling the absolute value would move the whole limb.
        if (channel.values.empty()) {
            continue;
        }
        const glm::vec4 base = channel.values.front();
        for (glm::vec4& v : channel.values) {
            v.x = base.x + ((v.x - base.x) * factor);
            v.z = base.z + ((v.z - base.z) * factor);
        }
    }
    return out;
}

} // namespace

std::vector<MotionVariant> generateVariants(const Skeleton& skeleton, const AnimationClip& source,
                                            const VariantOptions& options) {
    std::vector<MotionVariant> out;
    if (source.length() <= 0.0f) {
        return out;
    }

    // The source always goes in, at the speed it was authored for. A generated set that does not
    // contain its own source is one nobody can compare against.
    MotionVariant original;
    original.clip = source;
    original.provenance.source = source.name;
    original.provenance.kind = VariantKind::Source;
    original.provenance.parameter = 1.0f;
    original.provenance.tool = options.tool;
    original.provenance.targetSpeed = options.sourceSpeed;
    out.push_back(std::move(original));

    if (options.sourceSpeed <= 1e-4f) {
        // **ADR-540: a clip whose root nets zero cannot say what speed it means.** Without an
        // authored source speed there is no factor to warp by, and inventing one would produce a
        // pack full of variants labelled with a number nobody measured.
        return out;
    }

    // The caller's metrics, not a fresh default set: a default `MotionQualityOptions` names no
    // contacts and no limbs, so every metric comes back unmeasured and the gate below refuses
    // nothing at all.
    MotionQualityOptions quality = options.quality;
    if (quality.sampleRate <= 0.0f) {
        quality.sampleRate = 30.0f;
    }
    const MotionQualityLimits limits = options.limits;

    for (const float target : options.targetSpeeds) {
        // Already covered? Then this variant is one of the pointless thousands.
        const float band = std::max(std::abs(target) * options.coverageTolerance, 1e-3f);
        const bool already = std::any_of(out.begin(), out.end(), [&](const MotionVariant& v) {
            return std::abs(v.provenance.targetSpeed - target) <= band;
        });
        if (already) {
            continue;
        }
        const float factor = target / options.sourceSpeed;
        if (factor < options.minWarp || factor > options.maxWarp) {
            // Past the warp bounds the honest answer is a different source clip, not a more
            // extreme warp -- the same rule §12's locomotion modes follow one tier up.
            continue;
        }
        MotionVariant v;
        v.clip = timeWarp(source, factor);
        v.clip.name = fmt::format("{}@{:.2f}", source.name, target);
        v.provenance.source = source.name;
        v.provenance.kind = VariantKind::SpeedWarp;
        v.provenance.parameter = factor;
        v.provenance.tool = options.tool;
        v.provenance.targetSpeed = target;
        v.quality = measureMotionQuality(skeleton, v.clip, quality);
        v.verdict = gateMotionQuality(v.quality, limits);
        if (!v.verdict.pass) {
            continue;   // §44: the gate refuses, and the variant does not enter the set
        }
        out.push_back(std::move(v));
    }
    return out;
}

} // namespace avgen::scene
