// Conformance for the temporal effect family (ADR-394).
//
// The check this file exists for is **bounded-k**: ADR-394's entire argument is that no temporal
// effect is an accumulator, and that promise is a convention a kind can forget silently. So the
// first thing proved here is that the check CAN FAIL -- ADR-182, a probe that cannot fail proves
// nothing. Only then does a pass over the real family mean anything.

#include "params/parameter_set.hpp"
#include "scene/temporal_conformance.hpp"
#include "scene/temporal_settings.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <string>
#include <vector>

using namespace avgen;
using namespace avgen::scene;
namespace conf = avgen::scene::conformance;

namespace {

[[nodiscard]] bool hasRule(const conf::Report& r, const std::string& rule) {
    return std::any_of(r.findings.begin(), r.findings.end(),
                       [&](const conf::Finding& f) { return f.rule == rule; });
}

[[nodiscard]] bool namesSubject(const conf::Report& r, const std::string& subject) {
    return std::any_of(r.findings.begin(), r.findings.end(),
                       [&](const conf::Finding& f) { return f.subject == subject; });
}

} // namespace

TEST_CASE("the bounded-k check fails for an effect that cannot state a bound", "[temporal][conformance]") {
    // The control that makes every other assertion in this file mean something. A checker that
    // computed the bounds itself could never be made to report, and its green would say only that
    // it ran.
    SECTION("enabled with no declared depth is the accumulator ADR-394 forbids") {
        const std::vector<conf::DeclaredBound> bounds{
            {"echo", true, 6},
            {"unbounded", true, 0}, // the kind that forgot
        };
        const auto r = conf::checkBounds(bounds);
        CHECK_FALSE(r.clean());
        INFO(r.summary());
        CHECK(hasRule(r, "bounded-k"));
        // By NAME, so a failure says which effect to go and look at rather than that something is
        // wrong somewhere.
        CHECK(namesSubject(r, "unbounded"));
        CHECK_FALSE(namesSubject(r, "echo"));
    }

    SECTION("a depth above the ring's ceiling is the same defect arriving the other way") {
        const std::vector<conf::DeclaredBound> bounds{{"greedy", true, kMaxTemporalFrames + 1}};
        const auto r = conf::checkBounds(bounds);
        CHECK_FALSE(r.clean());
        INFO(r.summary());
        CHECK(namesSubject(r, "greedy"));
    }

    SECTION("a disabled effect that still asks for history is a ring nobody is using") {
        const std::vector<conf::DeclaredBound> bounds{{"leaky", false, 8}};
        const auto r = conf::checkBounds(bounds);
        CHECK_FALSE(r.clean());
        INFO(r.summary());
        CHECK(namesSubject(r, "leaky"));
    }

    SECTION("and a well-formed family is clean, or the checks above prove nothing") {
        const std::vector<conf::DeclaredBound> bounds{{"echo", true, 6}, {"off", false, 0}};
        const auto r = conf::checkBounds(bounds);
        INFO(r.summary());
        CHECK(r.clean());
    }
}

TEST_CASE("the real temporal family conforms", "[temporal][conformance]") {
    TemporalSettings s;
    const auto r = conf::checkTemporal(s);
    INFO(r.summary());
    CHECK(r.clean());
}

TEST_CASE("every temporal kind declares a bound that the ring can hold", "[temporal][conformance]") {
    TemporalSettings s;
    s.echo.enabled = true;
    s.echo.frames = 12;
    const auto bounds = conf::declaredBounds(s);
    REQUIRE(bounds.size() == kTemporalEffectKinds.size());
    for (const auto& b : bounds) {
        INFO("kind: " << b.subject);
        CHECK(b.enabled);
        CHECK(b.frames >= 1u);
        CHECK(b.frames <= static_cast<std::uint32_t>(kMaxTemporalFrames));
    }
    // The bound tracks the setting rather than being a constant that happens to be in range.
    s.echo.frames = 3;
    CHECK(conf::declaredBounds(s).front().frames == 3u);
    // ...and an authored value beyond the ceiling is clamped, not honoured.
    s.echo.frames = 9999;
    CHECK(conf::declaredBounds(s).front().frames == static_cast<std::uint32_t>(kMaxTemporalFrames));
}

TEST_CASE("historyFrames is the max over live effects and zero when none is on", "[temporal]") {
    TemporalSettings s;
    // Zero is load-bearing, not merely tidy: it is what releases the ring, so a project with no
    // temporal effect pays no memory for the family at all (§8).
    CHECK(s.historyFrames() == 0u);
    CHECK_FALSE(s.anyEnabled());

    s.echo.enabled = true;
    s.echo.frames = 7;
    CHECK(s.historyFrames() == 7u);
    CHECK(s.anyEnabled());

    s.echo.enabled = false;
    CHECK(s.historyFrames() == 0u);
}

TEST_CASE("every path the temporal panel asks for is a path the family registers", "[temporal][params]") {
    TemporalSettings s;
    params::ParameterSet params;
    (void)registerTemporalParameters(params, s);

    // Computed the way a panel computes it -- prefix from `temporalParameterPrefix`, leaf appended
    // -- rather than written out a second time (ADR-382). A hard-coded string here would agree
    // with itself and with nothing else.
    const std::string echo = temporalParameterPrefix(TemporalEffectKind::FrameEcho);
    CHECK(echo == "temporal/echo/");
    for (const char* leaf : {"enabled", "frames", "strength", "decay"}) {
        INFO("leaf: " << leaf);
        CHECK(conf::registered(params, echo + leaf));
    }

    // The control. Without it this test passes against a `registered()` that returns true for
    // everything, which is the failure mode of every "does the path exist" check.
    CHECK_FALSE(conf::registered(params, echo + "nonesuch"));
    CHECK_FALSE(conf::registered(params, "temporal/nosuchkind/enabled"));

    // The group is the first path segment, which is what the Parameters panel sorts by, and it is
    // `temporal` rather than `post` -- the family is its own category (§51).
    const auto* p = params.find(echo + "strength");
    REQUIRE(p != nullptr);
    CHECK(p->group() == "temporal");
    CHECK(p->flags().modulatable);
    CHECK(p->flags().serialized);
    CHECK(p->flags().exposed);
}

TEST_CASE("temporal settings survive a save, a load, and a second save", "[temporal][params]") {
    TemporalSettings s;
    s.echo.enabled = true;
    s.echo.frames = 11;
    s.echo.strength = 0.31f;
    s.echo.decay = 0.83f;

    const auto once = temporalFromJson(temporalToJson(s));
    REQUIRE(once.has_value());
    CHECK(once->echo.enabled);
    CHECK(once->echo.frames == 11);
    CHECK_THAT(once->echo.strength, Catch::Matchers::WithinAbs(0.31, 1e-5));
    CHECK_THAT(once->echo.decay, Catch::Matchers::WithinAbs(0.83, 1e-5));

    // The second trip is the control for a writer that emits a block its own reader ignores: that
    // asymmetry survives one trip and not two (ADR-350).
    const auto twice = temporalFromJson(temporalToJson(*once));
    REQUIRE(twice.has_value());
    CHECK(twice->echo.frames == 11);
    CHECK_THAT(twice->echo.strength, Catch::Matchers::WithinAbs(0.31, 1e-5));

    SECTION("turning the effect OFF survives too") {
        // The asymmetric bug this catches: a writer that omits the block when the effect is
        // disabled leaves the reader with its default, so disabling never sticks.
        TemporalSettings off = s;
        off.echo.enabled = false;
        const auto back = temporalFromJson(temporalToJson(off));
        REQUIRE(back.has_value());
        CHECK_FALSE(back->echo.enabled);
        // ...and the other values are still carried, so re-enabling restores the authored look
        // rather than the defaults.
        CHECK(back->echo.frames == 11);
    }

    SECTION("an out-of-range authored value is clamped on the way in, not honoured") {
        auto j = temporalToJson(s);
        j["echo"]["frames"] = 1000;
        j["echo"]["strength"] = 5.0;
        const auto back = temporalFromJson(j);
        REQUIRE(back.has_value());
        CHECK(back->echo.frames == kMaxTemporalFrames);
        CHECK_THAT(back->echo.strength, Catch::Matchers::WithinAbs(1.0, 1e-5));
    }

    SECTION("a malformed block is refused rather than silently defaulted") {
        nlohmann::json j;
        j["echo"] = 7; // not an object
        CHECK_FALSE(temporalFromJson(j).has_value());
    }
}

TEST_CASE("a modulated frame count rounds rather than truncates", "[temporal][params]") {
    TemporalSettings s;
    s.echo.enabled = true;
    params::ParameterSet params;
    const TemporalParameters p = registerTemporalParameters(params, s);

    // A route landing on 5.999 must mean six frames. Truncation would silently shorten every
    // modulated echo by one tap, which is invisible in a picture and wrong in a render.
    p.echoFrames->setBase(5.999f);
    params.resetFinals();
    applyTemporalParameters(p, s);
    CHECK(s.echo.frames == 6);

    p.echoFrames->setBase(5.2f);
    params.resetFinals();
    applyTemporalParameters(p, s);
    CHECK(s.echo.frames == 5);
}
