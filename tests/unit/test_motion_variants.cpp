// Offline procedural generation (Phase B §42) and augmentation (§43).
//
// **The constraint is "do not generate thousands of pointless clips", and a count cannot enforce
// it** -- a count rewards generation. Coverage rewards *useful* generation, so that is what is
// measured. But coverage is monotone in variant count (ADR-559's trap), so it recommends
// generating forever unless something opposes it: here, **search cost**, since every variant is
// samples in the motion database and Phase C measured a linear scan at 5.9 ns per sample.
//
// These tests therefore report both and look for the knee, rather than asserting a threshold
// somebody picked.

#include "scene/motion_variants.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace avgen;
using Catch::Approx;

namespace {

scene::Skeleton legRig() {
    scene::Skeleton sk;
    sk.name = "walker";
    sk.joints.push_back(scene::Joint{"root.x", -1, scene::Transform{}});
    scene::Transform hip;
    hip.position = glm::vec3(0.0f, 1.0f, 0.0f);
    sk.joints.push_back(scene::Joint{"hip", 0, hip});
    scene::Transform knee;
    knee.position = glm::vec3(0.0f, 0.5f, 0.35f);   // well bent: see testing.md #24
    sk.joints.push_back(scene::Joint{"knee", 0, knee});
    sk.joints.push_back(scene::Joint{"foot", 0, scene::Transform{}});
    sk.palette = {0, 1, 2, 3};
    sk.inverseBind.assign(4, glm::mat4(1.0f));
    return sk;
}

scene::AnimationClip walk() {
    scene::AnimationClip clip;
    clip.name = "Walk";
    scene::AnimationChannel foot;
    foot.joint = 3;
    foot.path = scene::AnimationPath::Translation;
    foot.interpolation = scene::Interpolation::Linear;
    for (int i = 0; i <= 30; ++i) {
        const float t = static_cast<float>(i) / 30.0f;
        const float y = t < 0.5f ? 0.0f : 0.25f * std::sin(((t - 0.5f) / 0.5f) * 3.14159265f);
        const float z = t < 0.5f ? 0.0f : 0.5f * ((t - 0.5f) / 0.5f);
        foot.times.push_back(t);
        foot.values.emplace_back(0.0f, y, z, 0.0f);
    }
    clip.start = 0.0f;
    clip.duration = 1.0f;
    clip.channels.push_back(std::move(foot));
    return clip;
}

} // namespace

TEST_CASE("a variant carries where it came from and what was done to it", "[variants][phaseB]") {
    // §43: every generated clip retains provenance -- source, transformation, parameters, tool.
    // A generated clip that cannot say what it came from is one nobody can regenerate or reject.
    scene::VariantOptions o;
    o.sourceSpeed = 1.6f;
    o.targetSpeeds = {1.0f, 2.2f};
    const std::vector<scene::MotionVariant> v = scene::generateVariants(legRig(), walk(), o);
    REQUIRE(v.size() >= 2);
    for (const scene::MotionVariant& x : v) {
        INFO(x.provenance.describe());
        CHECK(x.provenance.source == "Walk");
        CHECK_FALSE(x.provenance.tool.empty());
    }
    CHECK(v.front().provenance.kind == scene::VariantKind::Source);
    CHECK(v[1].provenance.kind == scene::VariantKind::SpeedWarp);
    // The warp is the ratio it says it is, not a label.
    CHECK(v[1].provenance.parameter == Approx(v[1].provenance.targetSpeed / 1.6f).margin(1e-3));
}

TEST_CASE("a speed already covered generates nothing", "[variants][phaseB]") {
    // The direct answer to "thousands of pointless clips": a target the source already sits on
    // produces no variant at all.
    scene::VariantOptions o;
    o.sourceSpeed = 1.6f;
    o.targetSpeeds = {1.6f, 1.65f, 1.55f};   // all inside the tolerance of the source
    const std::vector<scene::MotionVariant> v = scene::generateVariants(legRig(), walk(), o);
    INFO("generated " << v.size() << " clip(s) for three near-identical targets");
    CHECK(v.size() == 1);                      // the source, and nothing else
    CHECK(v.front().provenance.kind == scene::VariantKind::Source);
}

TEST_CASE("a target outside the warp bounds is refused rather than stretched",
          "[variants][phaseB]") {
    // Past the bounds the honest answer is a different source clip, not a more extreme warp --
    // the same rule §12's locomotion modes follow one tier up. A generator that warped anyway
    // would fill the pack with clips that look wrong and pass every structural check.
    scene::VariantOptions o;
    o.sourceSpeed = 1.6f;
    o.targetSpeeds = {6.0f, 0.2f};   // 3.75x and 0.125x
    const std::vector<scene::MotionVariant> v = scene::generateVariants(legRig(), walk(), o);
    CHECK(v.size() == 1);            // only the source survived
}

TEST_CASE("a clip that cannot say its own speed generates nothing", "[variants][phaseB]") {
    // **ADR-540.** Every locomotion clip in this repository is authored in place and its root nets
    // zero, so a clip cannot say what speed it means. Without an authored source speed there is no
    // factor to warp by, and inventing one produces a pack of variants labelled with a number
    // nobody measured.
    scene::VariantOptions o;
    o.sourceSpeed = 0.0f;            // unknown
    o.targetSpeeds = {1.0f, 2.0f, 3.0f};
    const std::vector<scene::MotionVariant> v = scene::generateVariants(legRig(), walk(), o);
    CHECK(v.size() == 1);
    CHECK(v.front().provenance.kind == scene::VariantKind::Source);
}

TEST_CASE("coverage rises with variants and so does the scan, so the knee is the answer",
          "[variants][phaseB][coverage]") {
    // **ADR-559's trap, checked rather than assumed.** Coverage is monotone in variant count, so
    // it alone recommends generating forever. The opposing quantity is the scan every character
    // pays ten times a second.
    const scene::Skeleton sk = legRig();
    const scene::AnimationClip source = walk();
    const std::vector<float> used = {1.0f, 1.3f, 1.6f, 2.0f, 2.4f};

    struct Point {
        std::size_t asked;
        float coverage;
        std::uint32_t samples;
        float micros;
    };
    std::vector<Point> curve;
    // **Sampled where the curve actually flattens, which is later than I first guessed.** The
    // generator skips a target already covered by an existing variant, so asking for four across
    // the range yields only three clips and 0.6 coverage of the used set -- the saturation is not
    // between 4 and 16, it is past 16. Measuring the knee means sampling where the knee is.
    for (const std::size_t n : {std::size_t{0}, std::size_t{2}, std::size_t{4}, std::size_t{16},
                                std::size_t{48}}) {
        scene::VariantOptions o;
        o.sourceSpeed = 1.6f;
        // **`n` targets spread across the SAME range**, at increasing density. The first version
        // walked upward in 0.1 steps from 0.9, so asking for more targets extended the range and
        // the extra variants were genuinely useful -- coverage rose 0.4 to 1.0 and there was no
        // knee to find, because the experiment varied two things at once. Holding the range fixed
        // and varying only the density is the experiment that can show saturation.
        for (std::size_t i = 0; i < n; ++i) {
            const float t = n == 1 ? 0.5f : static_cast<float>(i) / static_cast<float>(n - 1);
            o.targetSpeeds.push_back(1.0f + (t * 1.4f));   // 1.0 .. 2.4, the range in use
        }
        const std::vector<scene::MotionVariant> v = scene::generateVariants(sk, source, o);
        const scene::CoverageReport c = scene::measureCoverage(v, used, 0.18f, 30.0f);
        curve.push_back({n, c.coverage, c.samples, c.scanMicroseconds});
        INFO("asked " << n << ": " << c.report());
    }

    // Coverage is monotone: it never falls as more variants are asked for.
    for (std::size_t i = 1; i < curve.size(); ++i) {
        INFO("step " << i << ": coverage " << curve[i - 1].coverage << " -> " << curve[i].coverage);
        CHECK(curve[i].coverage >= curve[i - 1].coverage - 1e-4f);
    }
    // ...and so is the cost, which is the point. A metric that only rises cannot pick a value.
    CHECK(curve.back().samples > curve.front().samples);

    // **The knee: coverage saturates, cost does not.** Four targets across the range already
    // cover most of it; sixteen cover no more and cost measurably more scan. That is the shape
    // that lets a threshold be chosen, and it is invisible to either number alone.
    const Point& sixteen = curve[3];
    const Point& fortyEight = curve[4];
    INFO("16 asked: " << sixteen.coverage << " coverage, " << sixteen.micros << " us; "
                      << "48 asked: " << fortyEight.coverage << " coverage, " << fortyEight.micros
                      << " us");
    CHECK(fortyEight.micros > sixteen.micros);             // the cost keeps climbing
    CHECK(fortyEight.coverage - sixteen.coverage < 0.05f); // ...and the coverage has flattened
}

TEST_CASE("a generated variant that fails the gate never enters the set", "[variants][phaseB][gate]") {
    // §44's consequence, through the generator. The gate was written and fixed before this
    // existed, so it is not codifying whatever the generator happened to produce.
    const scene::Skeleton sk = legRig();
    scene::AnimationClip broken = walk();
    // Drag the foot far below the hip: every pose asks for more limb than the skeleton has.
    for (glm::vec4& v : broken.channels.front().values) {
        v.y -= 1.4f;
    }
    scene::VariantOptions o;
    o.sourceSpeed = 1.6f;
    o.targetSpeeds = {1.0f, 2.2f};
    // **The gate must be told what to measure.** Without this every metric is unmeasured, and an
    // unmeasured metric never fails -- so the gate would refuse nothing. This test is what found
    // that: the generator built its own empty `MotionQualityOptions` and passed three broken
    // variants.
    o.quality.limbs = {{"hip", "knee", "foot"}};
    const std::vector<scene::MotionVariant> v = scene::generateVariants(sk, broken, o);
    INFO("survivors: " << v.size());
    // Only the source, which is always kept: a set that does not contain its own source is one
    // nobody can compare against. Every *generated* variant was refused.
    CHECK(v.size() == 1);
    CHECK(v.front().provenance.kind == scene::VariantKind::Source);
}
