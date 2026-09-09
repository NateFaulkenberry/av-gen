// Simulated grid fields (ADR-032), CPU side: JSON round trip, validation, trilinear sampling
// with both wrap rules, the reference simulation step, and reading a grid through an ordinary
// FieldKind::Grid field.

#include "spatial/field.hpp"
#include "spatial/grid_field.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <numeric>

using namespace avgen;
using Catch::Approx;

namespace {

spatial::GridField makeGrid(const char* name = "smoke") {
    spatial::GridField g;
    g.name = name;
    g.resolution = {4, 4, 4};
    g.boundsMin = glm::vec3(0.0f);
    g.boundsMax = glm::vec3(4.0f); // one unit per cell, centres at 0.5, 1.5, 2.5, 3.5
    g.reset();
    return g;
}

float total(const spatial::GridField& g) {
    return std::accumulate(g.data.begin(), g.data.end(), 0.0f);
}

} // namespace

TEST_CASE("GridField JSON round trip keeps the settings and never the cell values", "[grid]") {
    spatial::GridField g = makeGrid("plume");
    g.mode = spatial::GridMode::Vector;
    g.wrap = spatial::GridWrap::Wrap;
    g.resolution = {8, 6, 4};
    g.boundsMin = {-2.0f, 0.0f, -3.0f};
    g.boundsMax = {6.0f, 6.0f, 1.0f};
    g.injectField = "heat";
    g.velocityField = "churn";
    g.injectRate = 2.5f;
    g.advect = 0.75f;
    g.diffusion = 0.4f;
    g.diffuseIterations = 6;
    g.dissipation = 0.3f;
    g.feed = 0.03f;
    g.kill = 0.062f;
    g.diffusionA = 0.9f;
    g.diffusionB = 0.45f;
    g.simRate = 90.0f;
    g.maxSubSteps = 3;
    g.seed = 4242;
    g.seedAmount = 0.2f;
    g.reset();
    REQUIRE(g.allocated());

    const nlohmann::json j = g.toJson();
    CHECK_FALSE(j.contains("data"));
    auto back = spatial::GridField::fromJson(j);
    REQUIRE(back.has_value());
    CHECK(back->data.empty()); // settings only
    CHECK(back->structuralHash() == g.structuralHash());
    CHECK(back->name == "plume");
    CHECK(back->mode == spatial::GridMode::Vector);
    CHECK(back->wrap == spatial::GridWrap::Wrap);
    CHECK(back->resolution == glm::ivec3(8, 6, 4));
    CHECK(back->injectField == "heat");
    CHECK(back->velocityField == "churn");
    CHECK(back->diffuseIterations == 6);
    CHECK(back->seed == 4242u);
    CHECK(back->toJson() == j);

    // A scalar resolution is accepted as a cube.
    nlohmann::json cube = j;
    cube["resolution"] = 16;
    auto cubeGrid = spatial::GridField::fromJson(cube);
    REQUIRE(cubeGrid.has_value());
    CHECK(cubeGrid->resolution == glm::ivec3(16));
}

TEST_CASE("GridField validation rejects impossible grids", "[grid]") {
    spatial::GridField g;
    CHECK(g.validate().has_value());

    g.name.clear();
    CHECK_FALSE(g.validate().has_value());
    g.name = "g";

    g.resolution = {0, 4, 4};
    CHECK_FALSE(g.validate().has_value());
    g.resolution = {spatial::kMaxGridResolution + 1, 4, 4};
    CHECK_FALSE(g.validate().has_value());
    g.resolution = {32, 32, 32};

    g.boundsMax = g.boundsMin;
    CHECK_FALSE(g.validate().has_value());
    g.boundsMax = g.boundsMin + glm::vec3(4.0f);

    g.simRate = 0.0f;
    CHECK_FALSE(g.validate().has_value());
    g.simRate = 60.0f;

    g.maxSubSteps = 0;
    CHECK_FALSE(g.validate().has_value());
    g.maxSubSteps = 4;

    // A 128^3 vector grid does not fit the shared table; a 128^3 scalar grid exactly does.
    g.mode = spatial::GridMode::Vector;
    g.resolution = glm::ivec3(128);
    CHECK_FALSE(g.validate().has_value());
    g.mode = spatial::GridMode::Scalar;
    CHECK(g.validate().has_value());
    CHECK(g.floatCount() == spatial::kMaxGridTableFloats);
}

TEST_CASE("GridField sampling is trilinear and honours the wrap rule", "[grid]") {
    spatial::GridField g = makeGrid();
    // A ramp along x: cell (i, j, k) holds i.
    for (int k = 0; k < 4; ++k) {
        for (int j = 0; j < 4; ++j) {
            for (int i = 0; i < 4; ++i) {
                g.data[g.index(i, j, k)] = static_cast<float>(i);
            }
        }
    }
    // Cell centres read back exactly.
    CHECK(g.sampleScalar({0.5f, 0.5f, 0.5f}) == Approx(0.0f));
    CHECK(g.sampleScalar({2.5f, 1.5f, 3.5f}) == Approx(2.0f));
    // Halfway between two centres is the average.
    CHECK(g.sampleScalar({1.0f, 0.5f, 0.5f}) == Approx(0.5f));
    CHECK(g.sampleScalar({2.25f, 0.5f, 0.5f}) == Approx(1.75f));

    // Clamp: outside the bounds the edge value repeats.
    CHECK(g.sampleScalar({-5.0f, 0.5f, 0.5f}) == Approx(0.0f));
    CHECK(g.sampleScalar({9.0f, 0.5f, 0.5f}) == Approx(3.0f));

    // Wrap: one cell past the last centre is halfway back to the first.
    g.wrap = spatial::GridWrap::Wrap;
    CHECK(g.sampleScalar({4.0f, 0.5f, 0.5f}) == Approx(1.5f)); // between i = 3 and i = 0
    CHECK(g.sampleScalar({0.0f, 0.5f, 0.5f}) == Approx(1.5f));  // and the same from the other side
    CHECK(g.sampleScalar({-0.5f, 0.5f, 0.5f}) == Approx(3.0f)); // exactly the wrapped cell -1 = 3

    // Vector grids sample all three channels; a scalar read is their length.
    spatial::GridField v = makeGrid("vel");
    v.mode = spatial::GridMode::Vector;
    v.reset();
    for (int k = 0; k < 4; ++k) {
        for (int j = 0; j < 4; ++j) {
            for (int i = 0; i < 4; ++i) {
                const std::size_t base = v.index(i, j, k);
                v.data[base] = 3.0f;
                v.data[base + 1] = 4.0f;
                v.data[base + 2] = 0.0f;
            }
        }
    }
    CHECK(v.sampleVector({2.0f, 2.0f, 2.0f}).x == Approx(3.0f));
    CHECK(v.sampleVector({2.0f, 2.0f, 2.0f}).y == Approx(4.0f));
    CHECK(v.sampleScalar({2.0f, 2.0f, 2.0f}) == Approx(5.0f));
}

TEST_CASE("A Grid field reads its grid through the ordinary field interface", "[grid]") {
    spatial::FieldSet set;
    spatial::GridField g = makeGrid("density");
    for (float& value : g.data) {
        value = 2.0f;
    }
    set.grids.push_back(g);

    spatial::FieldSpec f;
    f.name = "readGrid";
    f.kind = spatial::FieldKind::Grid;
    f.reference = "density";
    REQUIRE(f.validate().has_value());
    set.fields.push_back(f);

    CHECK(spatial::sampleScalar(f, {2.0f, 2.0f, 2.0f}, 0.0, &set) == Approx(2.0f));
    CHECK(spatial::sampleScalar(f, {2.0f, 2.0f, 2.0f}, 0.0, nullptr) == Approx(0.0f)); // unbound
    // Inverted: 1 - value.
    spatial::FieldSpec inverted = f;
    inverted.invert = true;
    CHECK(spatial::sampleScalar(inverted, {2.0f, 2.0f, 2.0f}, 0.0, &set) == Approx(-1.0f));
    // A grid without a reference is invalid.
    spatial::FieldSpec broken = f;
    broken.reference.clear();
    CHECK_FALSE(broken.validate().has_value());
    // Scalar as vector: value * axis.
    const glm::vec3 v = spatial::sampleVector(f, {2.0f, 2.0f, 2.0f}, 0.0, &set);
    CHECK(v.y == Approx(2.0f));
}

TEST_CASE("The reference step injects, advects, diffuses and dissipates", "[grid]") {
    spatial::FieldSet set;
    // A constant scalar source and a constant velocity along +x.
    spatial::FieldSpec source;
    source.name = "source";
    source.kind = spatial::FieldKind::Box;
    source.position = {0.5f, 2.5f, 2.5f}; // the centre of cell (0, 2, 2)
    source.size = glm::vec3(0.4f);
    source.softness = 0.1f;
    set.fields.push_back(source);
    spatial::FieldSpec wind;
    wind.name = "wind";
    wind.kind = spatial::FieldKind::Direction;
    wind.axis = {1.0f, 0.0f, 0.0f};
    wind.strength = 1.0f;
    set.fields.push_back(wind);

    SECTION("injection then advection moves the density downwind") {
        spatial::GridField g = makeGrid();
        g.injectField = "source";
        g.injectRate = 60.0f;
        g.velocityField = "wind";
        g.advect = 1.0f;
        g.simRate = 60.0f;
        const float dt = 1.0f / g.simRate;
        g.step(dt, 0.0, &set); // inject only reaches the first cell column
        const float seeded = g.at(0, 2, 2, 0);
        CHECK(seeded > 0.5f);
        CHECK(g.at(1, 2, 2, 0) < seeded * 0.5f);
        for (int i = 0; i < 60; ++i) {
            g.step(dt, 0.0, &set);
        }
        // One second of wind at one unit per second: the plume reached the next cells.
        CHECK(g.at(1, 2, 2, 0) > 0.5f * seeded);
        CHECK(g.at(2, 2, 2, 0) > 0.0f);
        // and nothing moved upwind (there is nothing to the left of column 0).
        CHECK(g.at(0, 0, 0, 0) == Approx(0.0f).margin(1e-6));
    }

    SECTION("diffusion conserves mass and spreads a spike") {
        spatial::GridField g = makeGrid();
        g.diffusion = 4.0f;
        g.diffuseIterations = 8;
        g.wrap = spatial::GridWrap::Wrap; // no boundary to lose mass at
        g.data[g.index(2, 2, 2)] = 1.0f;
        const float before = total(g);
        for (int i = 0; i < 20; ++i) {
            g.step(1.0f / 60.0f, 0.0, &set);
        }
        const float after = total(g);
        CHECK(after == Approx(before).epsilon(0.01)); // within 1%
        CHECK(g.at(2, 2, 2, 0) < 1.0f);
        CHECK(g.at(1, 2, 2, 0) > 0.0f);
    }

    SECTION("dissipation decays the whole grid") {
        spatial::GridField g = makeGrid();
        for (float& v : g.data) {
            v = 1.0f;
        }
        g.dissipation = 6.0f;
        g.step(1.0f / 60.0f, 0.0, &set);
        CHECK(g.at(0, 0, 0, 0) == Approx(0.9f));
    }

    SECTION("reaction-diffusion keeps A and B in range and makes a non-uniform pattern") {
        spatial::GridField g;
        g.name = "rd";
        g.mode = spatial::GridMode::ReactionDiffusion;
        g.resolution = glm::ivec3(8);
        g.boundsMin = glm::vec3(0.0f);
        g.boundsMax = glm::vec3(8.0f);
        g.wrap = spatial::GridWrap::Wrap;
        g.seedAmount = 1.0f;
        g.simRate = 60.0f;
        g.diffusionA = 0.16f;
        g.diffusionB = 0.08f;
        g.reset();
        CHECK(g.at(0, 0, 0, 0) <= 1.0f); // A starts full outside the seeded blobs
        CHECK(g.at(0, 0, 0, 0) >= 0.5f);
        float minB = 1.0f;
        float maxB = 0.0f;
        for (int i = 0; i < 200; ++i) {
            g.step(1.0f, 0.0, &set); // Gray-Scott's own unit step
        }
        for (int k = 0; k < 8; ++k) {
            for (int j = 0; j < 8; ++j) {
                for (int i = 0; i < 8; ++i) {
                    const float a = g.at(i, j, k, 0);
                    const float b = g.at(i, j, k, 1);
                    CHECK(a >= 0.0f);
                    CHECK(a <= 1.0f);
                    CHECK(b >= 0.0f);
                    CHECK(b <= 1.0f);
                    CHECK(std::isfinite(a));
                    CHECK(std::isfinite(b));
                    minB = std::min(minB, b);
                    maxB = std::max(maxB, b);
                }
            }
        }
        CHECK(maxB > minB); // a pattern, not a flat field
    }
}

TEST_CASE("A grid table lays every grid out back to back", "[grid]") {
    std::vector<spatial::GridField> grids;
    spatial::GridField a = makeGrid("a"); // 4^3 scalar = 64 floats
    spatial::GridField b = makeGrid("b");
    b.mode = spatial::GridMode::Vector; // 4^3 x 4 = 256 floats
    b.reset();
    spatial::GridField c = makeGrid("c");
    c.mode = spatial::GridMode::ReactionDiffusion; // 4^3 x 2 = 128 floats
    c.reset();
    grids = {a, b, c};
    CHECK(spatial::gridTableOffset(grids, 0) == 0);
    CHECK(spatial::gridTableOffset(grids, 1) == 64);
    CHECK(spatial::gridTableOffset(grids, 2) == 64 + 256);
    CHECK(spatial::gridTableFloats(grids) == 64 + 256 + 128);
}
