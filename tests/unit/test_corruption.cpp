// ADR-1873: the corruption block, its CPU twin, and the composition's handling (targets, echoes, the live field and
// opening from the camera's distance). The drawing is tests/rendering/test_corruption_gpu.cpp.

#include "assets/asset_registry.hpp"
#include "core/time.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "scene/corruption.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <random>

using namespace avgen;
using Catch::Approx;

namespace {

scene::Corruption wall() {
    scene::Corruption c;
    c.targets = {"rock"};
    c.focus = glm::vec3(100.0f, 30.0f, -50.0f);
    c.axis = glm::vec3(0.0f, 0.0f, -1.0f);
    c.amount = 1.0f;
    c.liveAmount = 1.0f;
    c.liveOpen = 1.0f;
    c.radius = 200.0f;
    c.echoes = 2;
    return c;
}

glm::vec3 randomNear(std::mt19937& rng, glm::vec3 centre, float reach) {
    std::uniform_real_distribution<float> u(-reach, reach);
    return centre + glm::vec3(u(rng), u(rng), u(rng) * 0.2f);
}

} // namespace

TEST_CASE("the corruption block parses, round-trips and refuses what it does not know", "[corruption][adr1873]") {
    auto c = scene::corruptionFromJson(nlohmann::json::parse(
        R"({"targets": ["attractor.terrain"], "focus": [1, 2, 3], "axis": [0.8, 0, -0.6], "amount": 1, "echoes": 2,
            "slip": 3, "glowColor": [1, 0.5, 0.2]})"));
    REQUIRE(c.has_value());
    CHECK(c->targets == std::vector<std::string>{"attractor.terrain"});
    CHECK(c->focus == glm::vec3(1, 2, 3));
    CHECK(c->echoes == 2);
    CHECK(c->slip == Approx(3.0f));
    auto back = scene::corruptionFromJson(scene::corruptionToJson(*c));
    REQUIRE(back.has_value());
    CHECK(back->axis == c->axis);
    CHECK(back->glowColor == c->glowColor);
    CHECK(back->slip == Approx(3.0f));

    CHECK_FALSE(scene::corruptionFromJson(nlohmann::json::parse(R"({"slp": 1})")).has_value());
    CHECK_FALSE(scene::corruptionFromJson(nlohmann::json::parse(R"({"amount": 2})")).has_value());
    CHECK_FALSE(scene::corruptionFromJson(nlohmann::json::parse(R"({"echoes": 9})")).has_value());
    CHECK_FALSE(scene::corruptionFromJson(nlohmann::json::parse(R"({"axis": [0, 0, 0]})")).has_value());
    CHECK_FALSE(scene::corruptionFromJson(nlohmann::json::parse(R"({"focus": [1, 2]})")).has_value());
}

TEST_CASE("the corruption's field and opening follow the camera's approach, monotonically", "[corruption][adr1873]") {
    scene::Corruption c = wall();
    c.fieldFar = 700.0f;
    c.fieldNear = 40.0f;
    c.openFar = 320.0f;
    c.openNear = 35.0f;
    float lastField = -1.0f, lastOpen = -1.0f;
    for (float d = 1000.0f; d >= 0.0f; d -= 2.5f) {
        const float f = scene::corruptionFieldScale(c, d);
        const float o = scene::corruptionOpening(c, d);
        CHECK(f >= lastField);
        CHECK(o >= lastOpen);
        lastField = f;
        lastOpen = o;
    }
    CHECK(scene::corruptionFieldScale(c, 1000.0f) == 0.0f);
    CHECK(scene::corruptionFieldScale(c, 10.0f) == Approx(1.0f));
    CHECK(scene::corruptionOpening(c, 500.0f) == 0.0f);
    CHECK(scene::corruptionOpening(c, 20.0f) == Approx(1.0f));
    c.amount = 0.0f;
    CHECK(scene::corruptionFieldScale(c, 10.0f) == 0.0f);
}

TEST_CASE("the corruption is the identity when off, and bounded, deterministic and continuous when on",
          "[corruption][adr1873]") {
    scene::Corruption c = wall();
    std::mt19937 rng(7);
    // off: the gate (inactive packs to zeros), and nothing moves
    {
        const auto packed = scene::packCorruption(c, false);
        for (const glm::vec4& v : packed) CHECK(v == glm::vec4(0.0f));
        for (int i = 0; i < 200; ++i) {
            const glm::vec3 p = randomNear(rng, c.focus, 150.0f);
            const auto q = scene::corruptPoint(packed, p, 3.0f, 0);
            CHECK(q.position == p);
            CHECK(q.uvShift == 0.0f);
            CHECK(q.keep);
        }
    }
    const auto packed = scene::packCorruption(c, true);
    const float bound = c.slip + c.lift + c.stripSlip + c.pull + c.fold + c.depth + c.width * 1.2f;
    float worstStep = 0.0f;
    int moved = 0;
    for (int i = 0; i < 4000; ++i) {
        const glm::vec3 p = randomNear(rng, c.focus, 120.0f);
        const float t = 10.0f * static_cast<float>(i) / 4000.0f;
        const auto a = scene::corruptPoint(packed, p, t, 0);
        const auto again = scene::corruptPoint(packed, p, t, 0);
        CHECK(a.position == again.position); // deterministic
        CHECK(glm::length(a.position - p) <= bound);
        moved += glm::length(a.position - p) > 0.01f ? 1 : 0;
        // continuous in time: one frame later the point has moved by a fraction of a block, never a jump
        const auto b = scene::corruptPoint(packed, p, t + 1.0f / 30.0f, 0);
        worstStep = std::max(worstStep, glm::length(b.position - a.position));
    }
    INFO("largest move in one frame: " << worstStep << " m; points moved: " << moved);
    CHECK(moved > 2000);
    CHECK(worstStep < 2.0f);
    // far outside the field and the opening: untouched
    const glm::vec3 far = c.focus + glm::vec3(600.0f, 0.0f, 0.0f);
    CHECK(scene::corruptPoint(packed, far, 2.0f, 0).position == far);
}

TEST_CASE("an echo copy keeps only slipped blocks, and never the opening", "[corruption][adr1873]") {
    scene::Corruption c = wall();
    const auto packed = scene::packCorruption(c, true);
    std::mt19937 rng(11);
    int kept = 0, total = 0;
    for (int i = 0; i < 3000; ++i) {
        const glm::vec3 p = randomNear(rng, c.focus, 150.0f);
        const auto e = scene::corruptPoint(packed, p, 4.0f, 1);
        ++total;
        if (e.keep) {
            ++kept;
            const glm::vec3 q = p - c.focus; // the opening (axis -z: along the wall is x, up is y)
            CHECK(std::sqrt(std::pow(q.x / c.width, 2.0f) + std::pow(q.y / c.height, 2.0f)) > 1.6f);
        }
    }
    INFO("echo kept " << kept << " of " << total);
    CHECK(kept > 0);
    CHECK(kept < total / 2);
}

TEST_CASE("the opening pushes the rock inside it back along the axis", "[corruption][adr1873]") {
    scene::Corruption c = wall();
    c.liveAmount = 0.0f; // the aperture alone
    c.liveOpen = 1.0f;
    const auto packed = scene::packCorruption(c, true);
    const glm::vec3 inside = c.focus + glm::vec3(1.0f, 0.5f, 0.0f);
    const auto q = scene::corruptPoint(packed, inside, 1.0f, 0);
    CHECK(glm::dot(q.position - inside, c.axis) > 0.5f * c.depth);
    c.liveOpen = 0.0f;
    const auto shut = scene::corruptPoint(scene::packCorruption(c, true), inside, 1.0f, 0);
    CHECK(shut.position == inside);
}

TEST_CASE("the composition registers the corruption, marks its targets, makes echoes and drives the field",
          "[corruption][adr1873]") {
    const nlohmann::json scene = {
        {"format", "avgen-scene"},
        {"version", 1},
        {"name", "corrupt"},
        {"camera", {{"mode", 1}, {"position", {0.0, 30.0, 400.0}}, {"target", {0.0, 30.0, 0.0}}}},
        {"nodes",
         {{{"name", "rock"}, {"kind", "grid"}},
          {{"name", "other"}, {"kind", "grid"}, {"position", {5000.0, 0.0, 0.0}}}}},
        {"corruption",
         {{"targets", {"rock"}}, {"focus", {0.0, 0.0, 0.0}}, {"axis", {0.0, -1.0, 0.0}}, {"amount", 1.0}, {"echoes", 2},
          {"radius", 300.0}, {"fieldFar", 1000.0}, {"fieldNear", 50.0}, {"openFar", 600.0}, {"openNear", 100.0}}}};
    assets::AssetRegistry registry{testsupport::processTempDir()};
    auto c = scene::Composition::fromJson(scene, registry);
    REQUIRE(c.has_value());
    params::ParameterSet params;
    params::Modulator modulator;
    (*c)->attach(params, modulator);
    for (const char* leaf : {"amount", "radius", "slip", "share", "glyph", "open", "openNear", "openFar", "pull"}) {
        INFO(leaf);
        CHECK(params.find(std::string("corruption/") + leaf) != nullptr);
    }
    params.resetFinals();
    FrameTime ft{};
    ft.renderTime = 1.0;
    (*c)->update(ft);
    const scene::Scene& s = (*c)->scene();
    int targets = 0, echoes = 0;
    for (const scene::Entity& e : s.entities) {
        if (e.corruptTarget && e.corruptEcho == 0) {
            ++targets;
            CHECK(e.name.rfind("rock", 0) == 0);
        }
        if (e.corruptEcho > 0) {
            ++echoes;
            CHECK(e.visible == s.corruption.active());
        }
    }
    CHECK(targets >= 1);
    CHECK(echoes == 2 * targets);
    // the camera is 400 m from the focus: inside the field's reach, outside the opening's
    CHECK(s.corruption.liveAmount > 0.0f);
    CHECK(s.corruption.liveAmount < 1.0f);
    CHECK(s.corruption.liveOpen > 0.0f);
    CHECK(s.corruption.liveOpen < 1.0f);
    CHECK(s.corruption.active());
    // a parameter's final reaches the scene
    params.findAs<float>("corruption/amount")->setBase(0.0f);
    params.resetFinals();
    (*c)->update(ft);
    CHECK((*c)->scene().corruption.liveAmount == 0.0f);
}

TEST_CASE("nothing at rest below the floor moves (a river bed stays under its water)", "[corruption][adr1873]") {
    scene::Corruption c = wall();
    c.floor = c.focus.y - 10.0f;
    const auto packed = scene::packCorruption(c, true);
    std::mt19937 rng(5);
    int below = 0, movedAbove = 0;
    for (int i = 0; i < 3000; ++i) {
        const glm::vec3 p = randomNear(rng, c.focus, 60.0f);
        const auto q = scene::corruptPoint(packed, p, 2.5f, 0);
        if (p.y < c.floor) {
            ++below;
            CHECK(q.position == p);
            CHECK(q.uvShift == 0.0f);
        } else if (p.y > c.floor + 2.0f && glm::length(q.position - p) > 0.01f) {
            ++movedAbove;
        }
    }
    INFO("below " << below << ", moved above " << movedAbove);
    CHECK(below > 100);
    CHECK(movedAbove > 100);
    // the default floor moves everything as before
    scene::Corruption d = wall();
    CHECK(scene::packCorruption(d, true)[9].x <= -1.0e5f);
}
