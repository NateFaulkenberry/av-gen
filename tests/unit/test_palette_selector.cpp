// ADR-1420: the palette selector -- a selected state reached by a direct OKLab blend, retargeted without a jump,
// locked, advanced by a bus signal with a hold, restarted by a jump in time; and a palette without a selector is
// exactly ADR-1043's.

#include "params/palette.hpp"
#include "params/parameter_set.hpp"
#include "signals/signal_bus.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <nlohmann/json.hpp>

#include <cmath>

using namespace avgen;
using Catch::Matchers::WithinAbs;

namespace {

float dist(glm::vec3 a, glm::vec3 b) { return glm::length(a - b); }

constexpr glm::vec3 kRed{0.8f, 0.05f, 0.05f};
constexpr glm::vec3 kGreen{0.05f, 0.7f, 0.1f};
constexpr glm::vec3 kBlue{0.05f, 0.1f, 0.9f};

// Three states; the middle one (green) must never appear in a red -> blue blend.
nlohmann::json paletteJson(bool withSelector, const char* advance = nullptr) {
    nlohmann::json j = nlohmann::json::parse(R"({
        "states": [ { "name": "red",   "colors": { "silk": [0.8, 0.05, 0.05] }, "scalars": { "rough": 0.2 } },
                    { "name": "green", "colors": { "silk": [0.05, 0.7, 0.1] },  "scalars": { "rough": 0.5 } },
                    { "name": "blue",  "colors": { "silk": [0.05, 0.1, 0.9] },  "scalars": { "rough": 0.8 } } ],
        "bindings": [ { "role": "silk", "target": "mat/color" }, { "role": "rough", "target": "mat/rough" },
                      { "role": "silk", "target": "mat/const4" } ] })");
    if (withSelector) {
        j["selector"] = {{"select", 0}, {"transition", 2.0}, {"lock", false}, {"hold", 5.0}};
        if (advance != nullptr) {
            j["selector"]["advance"] = {{"signal", advance}, {"threshold", 0.5}, {"order", {0, 2, 1}}};
        }
    }
    return j;
}

struct Rig {
    params::ParameterSet params;
    params::Palette palette;
    signals::SignalBus bus;
    signals::SignalId pulse = 0;
    signals::SignalId level = 0;

    explicit Rig(const nlohmann::json& j) {
        params::ParamDesc<glm::vec3> c;
        c.path = "mat/color";
        c.hardMin = glm::vec3(0.0f);
        c.hardMax = glm::vec3(100.0f);
        params.add(c);
        params::ParamDesc<float> r;
        r.path = "mat/rough";
        r.hardMax = 10.0f;
        params.add(r);
        params::ParamDesc<glm::vec4> v4;
        v4.path = "mat/const4";
        v4.defaultValue = glm::vec4(0.0f, 0.0f, 0.0f, 0.75f);
        v4.hardMin = glm::vec4(-100.0f);
        v4.hardMax = glm::vec4(100.0f);
        params.add(v4);
        pulse = bus.declare("test.pulse", 0.0f, 1.0f, true);
        level = bus.declare("test.level");
        auto p = params::Palette::fromJson(j);
        REQUIRE(p.has_value());
        palette = std::move(*p);
        palette.attach(params);
    }

    double last = -1.0;
    // One frame: the finals rebuilt from the bases, the selector stepped, the palette applied. A forward step longer
    // than half a second is filled with quiet frames first (a step over a second is a seek, ADR-1420); `pulse` > 0
    // fires the test pulse on the frame at `t` only.
    glm::vec3 frame(double t, float pulseStrength = 0.0f) {
        if (last >= 0.0 && t > last) {
            for (double u = last + 0.5; u < t - 1e-9; u += 0.5) {
                one(u);
            }
        }
        if (pulseStrength > 0.0f) {
            bus.setEvent(pulse, true, pulseStrength);
        }
        return one(t);
    }
    glm::vec3 one(double t) {
        params.resetFinals();
        palette.step(t, bus);
        palette.apply(params);
        bus.clearEvents();
        last = t;
        return params.findAs<glm::vec3>("mat/color")->value();
    }
    void select(float s) { palette.selectParam->setBase(s); }
};

glm::vec3 labMix(glm::vec3 a, glm::vec3 b, float f) {
    return params::Palette::fromOklab(glm::mix(params::Palette::toOklab(a), params::Palette::toOklab(b), f));
}

} // namespace

TEST_CASE("Palette selector: a palette without one is exactly ADR-1043's", "[palette][adr1420]") {
    Rig rig(paletteJson(false));
    CHECK(rig.params.find("palette/select") == nullptr);
    CHECK(rig.params.find("palette/transition") == nullptr);
    CHECK(rig.params.find("palette/lock") == nullptr);
    CHECK(rig.params.find("palette/hold") == nullptr);
    CHECK_FALSE(rig.palette.toJson().contains("selector"));
    rig.palette.positionParam->setBase(0.5f);
    // Stepping does nothing; the colour is position's, through the middle state (ADR-1043's ordered blend).
    for (int k = 0; k < 5; ++k) {
        const glm::vec3 c = rig.frame(k * 0.5, 1.0f);
        CHECK(dist(c, rig.palette.color("silk", 0.5f, 1.0f, 1.0f)) < 1e-6f);
        CHECK(dist(c, labMix(kRed, kGreen, 0.5f)) < 1e-4f);
    }
}

TEST_CASE("Palette selector: a change of state blends directly, never through the states between",
          "[palette][adr1420]") {
    Rig rig(paletteJson(true));
    REQUIRE(rig.params.find("palette/select") != nullptr);
    CHECK(dist(rig.frame(0.0), kRed) < 1e-4f);
    CHECK(dist(rig.frame(1.0), kRed) < 1e-4f);
    rig.select(2.0f); // red -> blue over 2 s, from t = 1.5
    CHECK(dist(rig.frame(1.5), kRed) < 1e-4f);
    // Half way in time is half way eased (smoothstep(0.5) = 0.5): the OKLab midpoint of red and blue.
    const glm::vec3 mid = rig.frame(2.5);
    CHECK(dist(mid, labMix(kRed, kBlue, 0.5f)) < 1e-3f);
    // Nowhere on the way does it pass near green (ADR-1043's position would: red -> green -> blue).
    for (double t = 1.5; t <= 3.5; t += 0.1) {
        const glm::vec3 c = rig.frame(t);
        CHECK(c.g < 0.2f);
    }
    CHECK(dist(rig.frame(3.6), kBlue) < 1e-4f);
    CHECK_THAT(static_cast<double>(rig.params.findAs<float>("mat/rough")->value()), WithinAbs(0.8, 1e-5));
    // A vec4 target (a material op's constant): rgb written, w left alone.
    const glm::vec4 v4 = rig.params.findAs<glm::vec4>("mat/const4")->value();
    CHECK(dist(glm::vec3(v4), kBlue) < 1e-4f);
    CHECK_THAT(static_cast<double>(v4.w), WithinAbs(0.75, 1e-6));
}

TEST_CASE("Palette selector: a retarget mid-blend starts from where the blend stands (no jump)",
          "[palette][adr1420]") {
    Rig rig(paletteJson(true));
    rig.frame(0.0);
    rig.select(2.0f);
    rig.frame(0.1);                  // red -> blue begins
    const glm::vec3 before = rig.frame(1.0);
    rig.select(1.0f);                // to green, from wherever it stands at t = 1.0
    const glm::vec3 at = rig.frame(1.0 + 1e-4);
    CHECK(dist(before, at) < 2e-3f);
    // and then on to green, directly
    CHECK(dist(rig.frame(3.5), kGreen) < 1e-4f);
    // Frame to frame the colour never steps by more than a smooth 2 s blend allows at 60 fps.
    Rig smooth(paletteJson(true));
    glm::vec3 last = smooth.frame(0.0);
    float worst = 0.0f;
    for (int k = 1; k < 300; ++k) {
        if (k == 30) smooth.select(2.0f);
        if (k == 75) smooth.select(1.0f);
        if (k == 110) smooth.select(0.0f);
        const glm::vec3 c = smooth.frame(k / 60.0);
        worst = std::max(worst, dist(c, last));
        last = c;
    }
    CHECK(worst < 0.03f);
}

TEST_CASE("Palette selector: the advance follows its order, waits out the hold, and stops when locked",
          "[palette][adr1420]") {
    Rig rig(paletteJson(true, "test.pulse"));
    rig.frame(0.0);
    REQUIRE(rig.palette.selectedState() == 0);
    // Within the hold (5 s from the landing) a pulse does nothing.
    rig.frame(2.0, 1.0f);
    CHECK(rig.palette.selectedState() == 0);
    // After it, the next in the order [0, 2, 1]: 2.
    rig.frame(6.0, 1.0f);
    CHECK(rig.palette.selectedState() == 2);
    // A weak pulse (under the threshold) does nothing.
    rig.frame(12.0, 0.3f);
    CHECK(rig.palette.selectedState() == 2);
    // Two pulses 2 s apart: only the first advances (2 -> 1).
    rig.frame(12.5, 1.0f);
    CHECK(rig.palette.selectedState() == 1);
    rig.frame(13.0, 1.0f);
    rig.frame(14.5, 1.0f);
    CHECK(rig.palette.selectedState() == 1);
    // Locked: nothing advances; unlocked, it wraps the order (1 -> 0).
    rig.palette.lockParam->setBase(1.0f);
    for (double t = 15.0; t < 30.0; t += 0.5) {
        rig.frame(t, 1.0f);
    }
    CHECK(rig.palette.selectedState() == 1);
    rig.palette.lockParam->setBase(0.0f);
    rig.frame(30.0, 1.0f);
    CHECK(rig.palette.selectedState() == 0);
    // A manual selection still works when locked.
    rig.palette.lockParam->setBase(1.0f);
    rig.select(2.0f);
    rig.frame(30.5);
    CHECK(rig.palette.selectedState() == 2);
}

TEST_CASE("Palette selector: a continuous advance signal fires on an upward crossing only", "[palette][adr1420]") {
    Rig rig(paletteJson(true, "test.level"));
    rig.frame(0.0);
    rig.bus.set(rig.level, 0.9f); // rises inside the hold: no change, and no edge left pending
    rig.frame(0.5);
    rig.frame(6.0);
    CHECK(rig.palette.selectedState() == 0);
    rig.bus.set(rig.level, 0.1f);
    rig.frame(6.5);
    rig.bus.set(rig.level, 0.7f);
    rig.frame(7.0);
    CHECK(rig.palette.selectedState() == 2);
}

TEST_CASE("Palette selector: a jump in time restarts at the selected state, deterministically", "[palette][adr1420]") {
    const auto run = [](double seekTo) {
        Rig rig(paletteJson(true, "test.pulse"));
        rig.frame(0.0);
        for (double t = 0.5; t <= 15.0; t += 0.5) {
            rig.frame(t, std::fmod(t, 3.0) < 0.25 ? 1.0f : 0.0f);
        }
        REQUIRE(rig.palette.selectedState() != 0); // it did advance before the seek
        // a seek back
        const glm::vec3 c = rig.frame(seekTo);
        return std::pair{rig.palette.selectedState(), c};
    };
    const auto [state, colour] = run(4.0);
    CHECK(state == 0); // palette/select's base
    CHECK(dist(colour, kRed) < 1e-4f);
    // Same input, same result.
    const auto again = run(4.0);
    CHECK(again.first == state);
    CHECK(dist(again.second, colour) < 1e-7f);
    // Round trip of the selector block.
    Rig rig(paletteJson(true, "test.pulse"));
    const auto back = params::Palette::fromJson(rig.palette.toJson());
    REQUIRE(back.has_value());
    CHECK(back->selector.enabled);
    CHECK(back->selector.advanceSignal == "test.pulse");
    CHECK(back->selector.order == std::vector<int>{0, 2, 1});
    CHECK_THAT(static_cast<double>(back->selector.hold), WithinAbs(5.0, 1e-6));
    nlohmann::json bad = paletteJson(true);
    bad["selector"]["select"] = 7;
    CHECK_FALSE(params::Palette::fromJson(bad).has_value());
}

// ---- ADR-1421: the change trigger (a large, sustained change in the music while the primary is quiet) -------------

namespace {

nlohmann::json withChange(nlohmann::json j, float maxHold = 0.0f, float threshold = 0.2f) {
    j["selector"]["advance"]["change"] = {{"signals", {"test.level"}}, {"fast", 1.0}, {"slow", 8.0},
                                          {"threshold", threshold}, {"rearm", 0.08}, {"primaryTimeout", 12.0},
                                          {"maxHold", maxHold}};
    return j;
}

// Steps the rig every 0.1 s from `from` to `to` with the level at `level` (and a primary pulse at the times listed).
void hold(Rig& rig, double from, double to, float level, std::initializer_list<double> pulses = {}) {
    rig.bus.set(rig.level, level);
    for (double t = from; t < to - 1e-9; t += 0.1) {
        bool pulse = false;
        for (double p : pulses) {
            pulse = pulse || std::abs(p - t) < 0.05;
        }
        rig.frame(t, pulse ? 1.0f : 0.0f);
    }
}

} // namespace

TEST_CASE("Palette change trigger: with no primary pulse an energy change advances, the hold and the lock still apply",
          "[palette][adr1421]") {
    Rig rig(withChange(paletteJson(true, "test.pulse")));
    hold(rig, 0.0, 20.0, 0.2); // steady: the primary has been quiet 12 s, but nothing has changed
    CHECK(rig.palette.selectedState() == 0);
    hold(rig, 20.0, 24.0, 0.9); // a build arrives: fast pulls away from slow -> advance (order 0, 2, 1)
    CHECK(rig.palette.selectedState() == 2);
    // Still loud: the change stays above the re-arm level for a while, and the hold (5 s) is running -- no re-fire
    hold(rig, 24.0, 27.0, 0.9);
    CHECK(rig.palette.selectedState() == 2);
    // It settles (the averages meet, re-arming), then a breakdown: one more advance, after the hold
    hold(rig, 27.0, 60.0, 0.9);
    CHECK(rig.palette.selectedState() == 2);
    hold(rig, 60.0, 64.0, 0.1);
    CHECK(rig.palette.selectedState() == 1);
    // Locked: a change does nothing
    rig.palette.lockParam->setBase(1.0f);
    hold(rig, 64.0, 100.0, 0.1);
    hold(rig, 100.0, 104.0, 0.9);
    CHECK(rig.palette.selectedState() == 1);
}

TEST_CASE("Palette change trigger: two changes within the hold advance once", "[palette][adr1421]") {
    nlohmann::json j = withChange(paletteJson(true, "test.pulse"));
    j["selector"]["hold"] = 24.0;
    Rig rig(j);
    hold(rig, 0.0, 30.0, 0.2);
    hold(rig, 30.0, 34.0, 0.9); // advance 0 -> 2
    REQUIRE(rig.palette.selectedState() == 2);
    hold(rig, 34.0, 45.0, 0.9); // settle and re-arm
    hold(rig, 45.0, 50.0, 0.1); // a second change 15 s later: inside the 24 s hold
    CHECK(rig.palette.selectedState() == 2);
}

TEST_CASE("Palette change trigger: while the primary pulses, energy changes are not heard", "[palette][adr1421]") {
    Rig rig(withChange(paletteJson(true, "test.pulse")));
    // a phrase every 8 s; the hold is 5 s, so each pulse advances (0 -> 2 -> 1 -> 0 ...)
    hold(rig, 0.0, 20.0, 0.2, {7.0, 15.0});
    const int afterPulses = rig.palette.selectedState();
    CHECK(afterPulses == 1);
    // a big energy change between pulses: ignored (the primary fired 5 s ago, inside its 12 s timeout)
    hold(rig, 20.0, 22.5, 0.9);
    CHECK(rig.palette.selectedState() == afterPulses);
    // the primary goes quiet: 12 s after its last pulse the change trigger takes over
    hold(rig, 22.5, 40.0, 0.9);
    hold(rig, 40.0, 44.0, 0.1);
    CHECK(rig.palette.selectedState() != afterPulses);
}

TEST_CASE("Palette change trigger: maxHold advances a long uniform passage", "[palette][adr1421]") {
    Rig rig(withChange(paletteJson(true, "test.pulse"), 30.0f));
    hold(rig, 0.0, 29.0, 0.4);
    CHECK(rig.palette.selectedState() == 0);
    hold(rig, 29.0, 31.0, 0.4);
    CHECK(rig.palette.selectedState() == 2);
}

TEST_CASE("Palette change trigger: a selector without one (ADR-1420 as authored) is unchanged", "[palette][adr1421]") {
    // The JSON: no `change` key appears, and the round trip is exact.
    const nlohmann::json plain = paletteJson(true, "test.pulse");
    auto parsed = params::Palette::fromJson(plain);
    REQUIRE(parsed.has_value());
    CHECK_FALSE(parsed->selector.change.enabled);
    const nlohmann::json out = parsed->toJson();
    CHECK_FALSE(out["selector"]["advance"].contains("change"));
    CHECK(out["selector"] == params::Palette::fromJson(out)->toJson()["selector"]);
    // The behaviour: a scripted run (pulses, levels, a lock, a manual select) is bit-identical to the same run with a
    // change block that can never fire -- the primary path is untouched by the new code.
    const auto script = [](Rig& rig) {
        std::vector<glm::vec3> colours;
        for (int k = 0; k <= 600; ++k) {
            const double t = 0.1 * k;
            rig.bus.set(rig.level, 0.5f + 0.4f * static_cast<float>(std::sin(t * 0.7)));
            if (k == 300) {
                rig.palette.lockParam->setBase(1.0f);
            }
            if (k == 420) {
                rig.select(1.0f);
            }
            const bool pulse = k % 70 == 35;
            colours.push_back(rig.frame(t, pulse ? 1.0f : 0.0f));
        }
        return colours;
    };
    Rig a(plain);
    Rig b(withChange(plain, 0.0f, 1.0e6f));
    const auto ca = script(a);
    const auto cb = script(b);
    REQUIRE(ca.size() == cb.size());
    bool identical = true;
    for (std::size_t i = 0; i < ca.size(); ++i) {
        identical = identical && ca[i] == cb[i];
    }
    CHECK(identical);
    CHECK(a.palette.selectedState() == b.palette.selectedState());
}

TEST_CASE("Palette change trigger: JSON round trip and validation", "[palette][adr1421]") {
    auto p = params::Palette::fromJson(withChange(paletteJson(true, "test.pulse"), 75.0f));
    REQUIRE(p.has_value());
    CHECK(p->selector.change.enabled);
    CHECK(p->changeSignals() == std::vector<std::string>{"test.level"});
    const auto back = params::Palette::fromJson(p->toJson());
    REQUIRE(back.has_value());
    CHECK(back->toJson() == p->toJson());
    nlohmann::json bad = withChange(paletteJson(true, "test.pulse"));
    bad["selector"]["advance"]["change"]["rearm"] = 0.5; // above the threshold: no hysteresis
    CHECK_FALSE(params::Palette::fromJson(bad).has_value());
    bad = withChange(paletteJson(true, "test.pulse"));
    bad["selector"]["advance"]["change"].erase("signals");
    CHECK_FALSE(params::Palette::fromJson(bad).has_value());
}
