// ADR-917: the post chain's pixel-sized values follow the frame's height.
//
// The arithmetic half, without a device: how the pixel scale is formed, and how the two
// energy-conserving pyramids are planned for a frame finer or coarser than the reference. The GPU
// half -- that the picture then keeps its look -- is tests/rendering/test_post_resolution_gpu.cpp.

#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "params/serialization.hpp"
#include "scene/post_settings.hpp"
#include "ui/ui_logic.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <cmath>

using namespace avgen;
using Catch::Approx;

namespace {

double totalWeight(const scene::PyramidPlan& plan) {
    double sum = 0.0;
    for (std::uint32_t j = 0; j < plan.levels; ++j) {
        sum += static_cast<double>(plan.weight[j]);
    }
    return sum;
}

// Where the glow sits on the octave axis: the weighted mean level. Level j's texel is 2^j of the
// first level's, so a shift of this mean by one is the whole glow growing by one octave.
double meanLevel(const scene::PyramidPlan& plan) {
    double sum = 0.0;
    double weight = 0.0;
    for (std::uint32_t j = 0; j < plan.levels; ++j) {
        sum += static_cast<double>(j) * static_cast<double>(plan.weight[j]);
        weight += static_cast<double>(plan.weight[j]);
    }
    return weight > 0.0 ? sum / weight : 0.0;
}

// The weights fs_upsample's chain actually produces from a plan's blends -- level j keeps
// (1 - blend[j]) of what reaches it and passes blend[j] down -- so the tests can check that the
// blends and the weights say the same thing.
double chainWeight(const scene::PyramidPlan& plan, std::uint32_t level) {
    double reach = 1.0;
    for (std::uint32_t j = 0; j < level; ++j) {
        reach *= static_cast<double>(plan.blend[j]);
    }
    return level + 1 < plan.levels ? reach * (1.0 - static_cast<double>(plan.blend[level])) : reach;
}

constexpr std::uint32_t kBig = 1u << 14; // a base size no plan here is limited by

} // namespace

TEST_CASE("the post pixel scale is the chain's height over the reference height", "[post][resolution]") {
    scene::PostSettings s;
    CHECK(s.referenceHeight == 720.0f); // the height the older radii were already expressed at
    CHECK(scene::postPixelScale(s, 720) == 1.0f);
    CHECK(scene::postPixelScale(s, 1440) == 2.0f);
    CHECK(scene::postPixelScale(s, 360) == 0.5f);
    s.referenceHeight = 1080.0f; // a 960x540 preview at supersample 2
    CHECK(scene::postPixelScale(s, 1080) == 1.0f);
    CHECK(scene::postPixelScale(s, 2160) == 2.0f); // 1080p x2
    CHECK(scene::postPixelScale(s, 4320) == 4.0f); // 2160p x2
    // A nonsense reference is floored rather than dividing by zero.
    s.referenceHeight = 0.0f;
    CHECK(std::isfinite(scene::postPixelScale(s, 1080)));
}

TEST_CASE("at the reference height the pyramid is the authored one, blend for blend", "[post][resolution]") {
    // The claim ADR-917 rests on for every scene rendered at its reference: nothing moves. The
    // blends are compared with ==, not Approx -- an ulp here is a different frame.
    for (std::uint32_t levels = 1; levels <= 8; ++levels) {
        for (const float blend : {0.05f, 0.5f, 0.575f, 0.95f}) {
            const scene::PyramidPlan plan = scene::planPyramid(levels, blend, 0.0f, kBig, kBig);
            INFO("levels " << levels << " blend " << blend);
            REQUIRE(plan.levels == levels);
            for (std::uint32_t j = 0; j + 1 < plan.levels; ++j) {
                CHECK(plan.blend[j] == blend);
            }
            CHECK(totalWeight(plan) == Approx(1.0).margin(1e-6));
            // The renderer's upsample stops at the finest level with any weight
            // (PostProcessor::buildPyramid). At the reference -- and at any coarser frame -- that
            // must be level 0, or the chain would stop early and stop being the authored one. The
            // blend's clamp to 0.95 is what guarantees it: level 0 keeps 1 - blend >= 0.05.
            CHECK(plan.weight[0] > 0.0f);
            CHECK(scene::planPyramid(levels, blend, -1.5f, kBig, kBig).weight[0] > 0.0f);
        }
    }
}

TEST_CASE("a whole octave finer adds a weightless level at the fine end and moves nothing else",
          "[post][resolution]") {
    const float blend = 0.575f; // Glowmere Valley 3's bloom radius 1.15, halved
    const scene::PyramidPlan reference = scene::planPyramid(6, blend, 0.0f, kBig, kBig);
    for (const int octaves : {1, 2, 3}) {
        const scene::PyramidPlan finer = scene::planPyramid(6, blend, static_cast<float>(octaves), kBig, kBig);
        INFO("octaves " << octaves);
        REQUIRE(finer.levels == reference.levels + static_cast<std::uint32_t>(octaves));
        for (int j = 0; j < octaves; ++j) {
            CHECK(finer.weight[static_cast<std::size_t>(j)] == 0.0f);
            CHECK(finer.blend[static_cast<std::size_t>(j)] == 1.0f); // a plain tent of what is below
        }
        for (std::uint32_t j = 0; j < reference.levels; ++j) {
            const std::size_t shifted = j + static_cast<std::size_t>(octaves);
            CHECK(finer.weight[shifted] == Approx(reference.weight[j]).margin(1e-7));
            if (j + 1 < reference.levels) {
                CHECK(finer.blend[shifted] == reference.blend[j]); // to the bit
            }
        }
        CHECK(meanLevel(finer) == Approx(meanLevel(reference) + octaves).margin(1e-6));
    }
}

TEST_CASE("a fractional shift shares each level between the two that bracket it", "[post][resolution]") {
    const float blend = 0.5f;
    const scene::PyramidPlan reference = scene::planPyramid(6, blend, 0.0f, kBig, kBig);
    // 1.5 and 2.585 are 1080p and 4320p against a 720-line reference; 0.585 is a 1080-line preview.
    for (const float octaves : {0.25f, 0.585f, 1.5f, 2.585f}) {
        const scene::PyramidPlan plan = scene::planPyramid(6, blend, octaves, kBig, kBig);
        INFO("octaves " << octaves);
        CHECK(plan.levels == 6u + static_cast<std::uint32_t>(std::ceil(octaves)));
        CHECK(totalWeight(plan) == Approx(1.0).margin(1e-6));
        // Splitting in proportion keeps the mean octave exact, which is what makes the glow's reach
        // follow the frame continuously rather than in octave steps.
        CHECK(meanLevel(plan) == Approx(meanLevel(reference) + static_cast<double>(octaves)).margin(1e-5));
        // And the blends reproduce the weights through the chain the shader runs.
        for (std::uint32_t j = 0; j < plan.levels; ++j) {
            CHECK(chainWeight(plan, j) == Approx(static_cast<double>(plan.weight[j])).margin(1e-5));
        }
    }
}

TEST_CASE("a frame coarser than the reference folds the levels it is too small for into its first",
          "[post][resolution]") {
    const float b = 0.5f;
    const double bd = static_cast<double>(b);
    // One octave coarser: the reference's first level (weight 1 - b) has no counterpart, so it
    // joins the second's (b (1 - b)) on the frame's first level, and the pyramid is a level shorter.
    const scene::PyramidPlan coarser = scene::planPyramid(6, b, -1.0f, kBig, kBig);
    REQUIRE(coarser.levels == 5u);
    CHECK(static_cast<double>(coarser.weight[0]) == Approx((1.0 - bd) + bd * (1.0 - bd)).margin(1e-7));
    CHECK(totalWeight(coarser) == Approx(1.0).margin(1e-6));
    // Far coarser than the whole pyramid: one level, all of the weight.
    const scene::PyramidPlan tiny = scene::planPyramid(6, b, -9.0f, kBig, kBig);
    CHECK(tiny.levels == 1u);
    CHECK(tiny.weight[0] == Approx(1.0).margin(1e-7));
}

TEST_CASE("a small frame stops the pyramid where the chain does and keeps every weight", "[post][resolution]") {
    // The chain stops before a level would be narrower than two texels: 40x24, 20x12, 10x6 and 5x3
    // are built and 2x1 is not -- four levels, whatever the plan wanted.
    const scene::PyramidPlan plan = scene::planPyramid(8, 0.5f, 2.0f, 40, 24);
    CHECK(plan.levels == 4u);
    CHECK(totalWeight(plan) == Approx(1.0).margin(1e-6));
    CHECK(plan.blend[plan.levels - 1] == 0.0f); // the coarsest level has no step
    // No level at all is what a frame too small for one gets, from the plan and the chain alike.
    CHECK(scene::planPyramid(6, 0.5f, 0.0f, 1, 1).levels == 0u);
}

TEST_CASE("post/referenceHeight is registered, round-trips and reaches the settings", "[post][resolution]") {
    params::ParameterSet params;
    params::Modulator modulator;
    auto p = scene::registerPostParameters(params, scene::PostSettings{});
    REQUIRE(p.referenceHeight != nullptr);
    CHECK(params.find("post/referenceHeight") != nullptr);
    CHECK(p.referenceHeight->value() == 720.0f);

    // A scene's own post block carries it (ADR-059), and the applied settings see it.
    REQUIRE(scene::applyPostJson(nlohmann::json::parse(R"({"referenceHeight": 1080})"), p).has_value());
    params.resetFinals();
    scene::PostSettings live;
    scene::applyPostParameters(p, live);
    CHECK(live.referenceHeight == 1080.0f);

    // ADR-350: set, save, load, save, by named key.
    p.referenceHeight->setBase(540.0f);
    const nlohmann::json first = params::saveProject(params, modulator);
    REQUIRE(first["parameters"].contains("post/referenceHeight"));
    params::ParameterSet reloaded;
    params::Modulator reloadedModulator;
    auto q = scene::registerPostParameters(reloaded, scene::PostSettings{});
    REQUIRE(params::loadProject(first, reloaded, reloadedModulator).has_value());
    reloaded.resetFinals();
    scene::PostSettings back;
    scene::applyPostParameters(q, back);
    CHECK(back.referenceHeight == 540.0f);

    // UI reach (the owner's standing rule): exposed, labelled in the words of the picture, and a
    // direct row of the Parameters panel's `post` group -- drawn above the sections whose sizes it
    // governs -- rather than a member of any one of them.
    params::IParameter* row = params.find("post/referenceHeight");
    REQUIRE(row != nullptr);
    CHECK(row->flags().exposed);
    CHECK(row->group() == "post");
    CHECK(ui::parameterSubGroup(row->path(), row->group()).empty());
    CHECK(std::string(row->label()).find("glow") != std::string::npos);

    // The description a render logs names the scale and what it did.
    back.referenceHeight = 1080.0f;
    const std::string line = scene::describePostScale(back, 4320);
    INFO(line);
    CHECK(line.find("4.00x") != std::string::npos);
    CHECK(line.find("box-filtered 2 octave(s) down, then 6 levels +0.00 octaves") != std::string::npos);
    // A preview at its reference boxes nothing; a 1080p frame against the default 720 is a fraction
    // of an octave the plan shares between levels.
    CHECK(scene::describePostScale(back, 1080).find("box-filtered 0 octave(s) down, then 6 levels +0.00") !=
          std::string::npos);
    back.referenceHeight = 720.0f;
    CHECK(scene::describePostScale(back, 1080).find("box-filtered 0 octave(s) down, then 6 levels +0.58") !=
          std::string::npos);
}
