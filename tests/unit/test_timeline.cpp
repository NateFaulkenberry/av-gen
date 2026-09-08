#include "params/parameter_set.hpp"
#include "params/timeline.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

using namespace avgen;
using namespace avgen::params;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using nlohmann::json;

namespace {
double d(float v) {
    return static_cast<double>(v);
}

Key key(double time, float value, KeyInterp interp = KeyInterp::Linear) {
    Key k;
    k.time = time;
    k.value[0] = value;
    k.interp = interp;
    return k;
}

Key vecKey(double time, float x, float y, float z, KeyInterp interp = KeyInterp::Linear) {
    Key k;
    k.time = time;
    k.value = {x, y, z, 0.0f};
    k.interp = interp;
    return k;
}

TimelineClock at(double seconds, double beats = 0.0) {
    TimelineClock clock;
    clock.seconds = seconds;
    clock.beats = beats;
    return clock;
}

// A scalar track 0 -> 1 over [0, 1] with the given interp.
Track ramp(KeyInterp interp) {
    Track track;
    track.target = "orb/scale";
    track.addKey(key(0.0, 0.0f, interp));
    track.addKey(key(1.0, 1.0f, interp));
    return track;
}

struct Fixture {
    ParameterSet params;
    Parameter<float>& scale;
    Parameter<glm::vec3>& color;
    Parameter<int>& count;

    Fixture()
        : scale(params.add(
              ParamDesc<float>{.path = "orb/scale", .defaultValue = 1.0f, .hardMin = 0.0f, .hardMax = 4.0f}))
        , color(params.add(ParamDesc<glm::vec3>{.path = "orb/color",
                                                .defaultValue = glm::vec3(0.5f),
                                                .hardMin = glm::vec3(0.0f),
                                                .hardMax = glm::vec3(1.0f),
                                                .isColor = true}))
        , count(params.add(
              ParamDesc<int>{.path = "orb/count", .defaultValue = 3, .hardMin = 0, .hardMax = 10})) {}
};
} // namespace

// ---- names -------------------------------------------------------------------------------------

TEST_CASE("Timeline enum names round-trip", "[params][timeline]") {
    for (const KeyInterp interp : {KeyInterp::Step, KeyInterp::Linear, KeyInterp::Smooth, KeyInterp::EaseIn,
                                   KeyInterp::EaseOut, KeyInterp::EaseInOut, KeyInterp::Bezier}) {
        const auto back = keyInterpFromName(keyInterpName(interp));
        REQUIRE(back.has_value());
        CHECK(*back == interp);
    }
    CHECK(std::string(keyInterpName(KeyInterp::EaseInOut)) == "easeInOut");
    CHECK(std::string(keyInterpName(KeyInterp::Step)) == "step");
    CHECK_FALSE(keyInterpFromName("Linear").has_value());
    CHECK_FALSE(keyInterpFromName("").has_value());

    for (const TimeBase base : {TimeBase::Seconds, TimeBase::Beats}) {
        const auto back = timeBaseFromName(timeBaseName(base));
        REQUIRE(back.has_value());
        CHECK(*back == base);
    }
    CHECK(std::string(timeBaseName(TimeBase::Beats)) == "beats");
    CHECK_FALSE(timeBaseFromName("bars").has_value());

    for (const TrackMode mode : {TrackMode::Replace, TrackMode::Add, TrackMode::Multiply}) {
        const auto back = trackModeFromName(trackModeName(mode));
        REQUIRE(back.has_value());
        CHECK(*back == mode);
    }
    CHECK(std::string(trackModeName(TrackMode::Multiply)) == "multiply");
    CHECK_FALSE(trackModeFromName("min").has_value());
}

// ---- keys ---------------------------------------------------------------------------------------

TEST_CASE("Track::addKey inserts sorted and replaces within epsilon", "[params][timeline]") {
    Track track;
    CHECK(track.addKey(key(2.0, 2.0f)) == 0);
    CHECK(track.addKey(key(0.0, 0.0f)) == 0);
    CHECK(track.addKey(key(1.0, 1.0f)) == 1);
    CHECK(track.addKey(key(3.0, 3.0f)) == 3);
    REQUIRE(track.keys.size() == 4);
    for (std::size_t i = 0; i < track.keys.size(); ++i) {
        CHECK(track.keys[i].time == static_cast<double>(i));
        CHECK(track.keys[i].value[0] == static_cast<float>(i));
    }
    CHECK(track.firstKeyTime() == 0.0);
    CHECK(track.lastKeyTime() == 3.0);

    // Within 1e-6 of an existing time: the key is replaced in place, from either side, and the
    // existing time is kept (no drift).
    CHECK(track.addKey(key(1.0 + 5e-7, 10.0f, KeyInterp::Step)) == 1);
    CHECK(track.keys.size() == 4);
    CHECK(track.keys[1].time == 1.0);
    CHECK(track.keys[1].value[0] == 10.0f);
    CHECK(track.keys[1].interp == KeyInterp::Step);
    CHECK(track.addKey(key(2.0 - 5e-7, 20.0f)) == 2);
    CHECK(track.keys.size() == 4);
    CHECK(track.keys[2].time == 2.0);
    CHECK(track.keys[2].value[0] == 20.0f);
    // Just outside the epsilon: a new key.
    CHECK(track.addKey(key(2.0 + 1e-3, 21.0f)) == 3);
    CHECK(track.keys.size() == 5);

    // sortKeys repairs manual edits (stable).
    track.keys[0].time = 100.0;
    track.sortKeys();
    CHECK(track.keys.back().time == 100.0);
    CHECK(track.keys.front().time == 1.0);

    Track empty;
    CHECK(empty.firstKeyTime() == 0.0);
    CHECK(empty.lastKeyTime() == 0.0);
    CHECK(empty.evaluate(3.0) == KeyValue{});
}

TEST_CASE("Track::evaluate clamps before and after the keys", "[params][timeline]") {
    Track track = ramp(KeyInterp::Linear);
    CHECK(track.evaluate(-5.0)[0] == 0.0f);
    CHECK(track.evaluate(0.0)[0] == 0.0f);
    CHECK(track.evaluate(1.0)[0] == 1.0f);
    CHECK(track.evaluate(7.0)[0] == 1.0f);

    Track single;
    single.addKey(key(4.0, 0.25f));
    CHECK(single.evaluate(0.0)[0] == 0.25f);
    CHECK(single.evaluate(9.0)[0] == 0.25f);
}

TEST_CASE("Track::evaluate interpolations", "[params][timeline]") {
    SECTION("step holds the starting key") {
        const Track track = ramp(KeyInterp::Step);
        CHECK(track.evaluate(0.0)[0] == 0.0f);
        CHECK(track.evaluate(0.5)[0] == 0.0f);
        CHECK(track.evaluate(0.999)[0] == 0.0f);
        CHECK(track.evaluate(1.0)[0] == 1.0f);
    }
    SECTION("linear midpoint is exact") {
        const Track track = ramp(KeyInterp::Linear);
        CHECK(track.evaluate(0.5)[0] == 0.5f);
        CHECK(track.evaluate(0.25)[0] == 0.25f);
    }
    SECTION("eases are monotone with exact endpoints") {
        for (const KeyInterp interp : {KeyInterp::EaseIn, KeyInterp::EaseOut, KeyInterp::EaseInOut}) {
            const Track track = ramp(interp);
            CHECK(track.evaluate(0.0)[0] == 0.0f);
            CHECK(track.evaluate(1.0)[0] == 1.0f);
            float previous = 0.0f;
            for (int i = 1; i <= 100; ++i) {
                const float v = track.evaluate(i / 100.0)[0];
                CHECK(v >= previous);
                CHECK(v <= 1.0f);
                previous = v;
            }
        }
        CHECK_THAT(d(ramp(KeyInterp::EaseIn).evaluate(0.5)[0]), WithinAbs(0.125, 1e-6));
        CHECK_THAT(d(ramp(KeyInterp::EaseOut).evaluate(0.5)[0]), WithinAbs(0.875, 1e-6));
        CHECK_THAT(d(ramp(KeyInterp::EaseInOut).evaluate(0.5)[0]), WithinAbs(0.5, 1e-6));
        CHECK_THAT(d(ramp(KeyInterp::EaseInOut).evaluate(0.25)[0]), WithinAbs(0.15625, 1e-6));
        // Ease-in starts slow, ease-out starts fast.
        CHECK(ramp(KeyInterp::EaseIn).evaluate(0.25)[0] < 0.25f);
        CHECK(ramp(KeyInterp::EaseOut).evaluate(0.25)[0] > 0.25f);
    }
    SECTION("smooth passes through keys and stays within the neighbour bounds") {
        Track track;
        track.addKey(key(0.0, 0.0f, KeyInterp::Smooth));
        track.addKey(key(1.0, 1.0f, KeyInterp::Smooth));
        track.addKey(key(2.0, 0.2f, KeyInterp::Smooth));
        track.addKey(key(4.0, 0.9f, KeyInterp::Smooth));
        for (const Key& k : track.keys) {
            CHECK(track.evaluate(k.time)[0] == k.value[0]);
        }
        for (std::size_t i = 0; i + 1 < track.keys.size(); ++i) {
            const float lo = std::min(track.keys[i].value[0], track.keys[i + 1].value[0]);
            const float hi = std::max(track.keys[i].value[0], track.keys[i + 1].value[0]);
            for (int s = 0; s <= 50; ++s) {
                const double t =
                    track.keys[i].time + (track.keys[i + 1].time - track.keys[i].time) * s / 50.0;
                const float v = track.evaluate(t)[0];
                CHECK(v >= lo);
                CHECK(v <= hi);
            }
        }
        // With a straight run of keys the curve is the straight line (Catmull-Rom reproduces lines).
        Track line;
        for (int i = 0; i < 4; ++i) {
            line.addKey(key(i, static_cast<float>(i), KeyInterp::Smooth));
        }
        CHECK_THAT(d(line.evaluate(1.5)[0]), WithinAbs(1.5, 1e-6));
        CHECK_THAT(d(line.evaluate(0.25)[0]), WithinAbs(0.25, 1e-6));
        // Two keys only: one-sided tangents give a smooth ramp that still hits the midpoint.
        const Track two = ramp(KeyInterp::Smooth);
        CHECK_THAT(d(two.evaluate(0.5)[0]), WithinAbs(0.5, 1e-6));
    }
    SECTION("bezier with zero tangents is the smoothstep ease; tangents change the shape") {
        const Track flat = ramp(KeyInterp::Bezier);
        const Track ease = ramp(KeyInterp::EaseInOut);
        for (int i = 0; i <= 20; ++i) {
            const double t = i / 20.0;
            CHECK_THAT(d(flat.evaluate(t)[0]), WithinAbs(d(ease.evaluate(t)[0]), 1e-6));
        }
        Track shaped;
        Key a = key(0.0, 0.0f, KeyInterp::Bezier);
        a.tangentOut[0] = 3.0f; // steep start (value per second)
        Key b = key(1.0, 1.0f, KeyInterp::Bezier);
        b.tangentIn[0] = 0.0f;
        shaped.addKey(a);
        shaped.addKey(b);
        CHECK(shaped.evaluate(0.0)[0] == 0.0f);
        CHECK(shaped.evaluate(1.0)[0] == 1.0f);
        CHECK(shaped.evaluate(0.25)[0] > flat.evaluate(0.25)[0]);
        // Hermite h10 at u = 0.5 is 0.125, so the slope contributes 3 * 0.125 = 0.375 over the ease.
        CHECK_THAT(d(shaped.evaluate(0.5)[0]), WithinAbs(0.5 + 0.375, 1e-6));
        // Tangents are scaled by the span: the same slope over a 2 s span doubles the offset.
        Track wide;
        Key a2 = a;
        Key b2 = b;
        b2.time = 2.0;
        wide.addKey(a2);
        wide.addKey(b2);
        CHECK_THAT(d(wide.evaluate(1.0)[0]), WithinAbs(0.5 + 0.75, 1e-6));
    }
    SECTION("the starting key's interp shapes the span") {
        Track track;
        track.addKey(key(0.0, 0.0f, KeyInterp::Step));
        track.addKey(key(1.0, 1.0f, KeyInterp::Linear));
        track.addKey(key(2.0, 0.0f, KeyInterp::Linear));
        CHECK(track.evaluate(0.5)[0] == 0.0f);
        CHECK(track.evaluate(1.5)[0] == 0.5f);
    }
}

TEST_CASE("Track loopLength wraps time, including negative times", "[params][timeline]") {
    Track track;
    track.loopLength = 4.0;
    track.addKey(key(0.0, 0.0f));
    track.addKey(key(2.0, 1.0f));
    track.addKey(key(4.0, 0.0f));
    CHECK(track.localTime(5.0) == 1.0);
    CHECK(track.localTime(4.0) == 0.0);
    CHECK(track.localTime(-1.0) == 3.0);
    CHECK(track.evaluate(5.0)[0] == track.evaluate(1.0)[0]);
    CHECK(track.evaluate(1.0)[0] == 0.5f);
    CHECK(track.evaluate(-1.0)[0] == track.evaluate(3.0)[0]);
    CHECK(track.evaluate(3.0)[0] == 0.5f);
    CHECK(track.evaluate(8.0)[0] == 0.0f);
    CHECK(track.evaluate(-8.0)[0] == 0.0f);
    CHECK(track.evaluate(-3.0)[0] == 0.5f);

    track.loopLength = 0.0;
    CHECK(track.localTime(5.0) == 5.0);
    CHECK(track.localTime(-1.0) == -1.0);
    CHECK(track.evaluate(5.0)[0] == 0.0f); // clamps to the last key instead
}

TEST_CASE("Track evaluates every component; keyedComponents follows the binding", "[params][timeline]") {
    Fixture f;
    Track track;
    track.target = "orb/color";
    track.addKey(vecKey(0.0, 0.0f, 1.0f, 0.5f));
    track.addKey(vecKey(1.0, 1.0f, 0.0f, 0.5f));
    CHECK(track.keyedComponents() == 1); // unbound
    track.param = f.params.find("orb/color");
    CHECK(track.keyedComponents() == 3);
    const KeyValue mid = track.evaluate(0.5);
    CHECK(mid[0] == 0.5f);
    CHECK(mid[1] == 0.5f);
    CHECK(mid[2] == 0.5f);
    CHECK(mid[3] == 0.0f);

    Track single;
    single.target = "orb/color";
    single.component = 1;
    single.param = track.param;
    CHECK(single.keyedComponents() == 1);
    single.addKey(key(0.0, 0.0f));
    single.addKey(key(2.0, 1.0f));
    CHECK(single.evaluate(1.0)[0] == 0.5f);
}

// ---- apply --------------------------------------------------------------------------------------

TEST_CASE("Timeline::apply writes finals per mode and leaves bases alone", "[params][timeline]") {
    Fixture f;
    Timeline timeline;
    f.scale.setBase(2.0f);
    f.color.setBase(glm::vec3(0.5f));

    Track scaleTrack;
    scaleTrack.target = "orb/scale";
    scaleTrack.addKey(key(0.0, 0.0f));
    scaleTrack.addKey(key(2.0, 1.0f));
    timeline.addTrack(scaleTrack);

    Track colorTrack;
    colorTrack.target = "orb/color";
    colorTrack.addKey(vecKey(0.0, 0.0f, 0.2f, 0.4f));
    colorTrack.addKey(vecKey(2.0, 1.0f, 0.2f, 0.8f));
    timeline.addTrack(colorTrack);
    REQUIRE(timeline.bind(f.params).has_value());

    SECTION("replace sets the final, not the base") {
        f.params.resetFinals();
        timeline.apply(at(1.0));
        CHECK(f.scale.value() == 0.5f);
        CHECK(f.scale.base() == 2.0f);
        CHECK_THAT(d(f.color.value().x), WithinAbs(0.5, 1e-6));
        CHECK_THAT(d(f.color.value().y), WithinAbs(0.2, 1e-6));
        CHECK_THAT(d(f.color.value().z), WithinAbs(0.6, 1e-6));
        CHECK(f.color.base() == glm::vec3(0.5f));
    }
    SECTION("add and multiply combine with the base") {
        timeline.tracks()[0].mode = TrackMode::Add;
        f.params.resetFinals();
        timeline.apply(at(1.0));
        CHECK(f.scale.value() == 2.5f);
        timeline.tracks()[0].mode = TrackMode::Multiply;
        f.params.resetFinals();
        timeline.apply(at(1.0));
        CHECK(f.scale.value() == 1.0f);
        // Finals still clamp to the hard range.
        timeline.tracks()[0].mode = TrackMode::Add;
        timeline.tracks()[0].keys[1].value[0] = 10.0f;
        f.params.resetFinals();
        timeline.apply(at(2.0));
        CHECK(f.scale.value() == 4.0f);
    }
    SECTION("single component tracks only touch their component") {
        Track green;
        green.target = "orb/color";
        green.component = 1;
        green.addKey(key(0.0, 1.0f));
        timeline.tracks().clear();
        timeline.addTrack(green);
        REQUIRE(timeline.bind(f.params).has_value());
        f.params.resetFinals();
        timeline.apply(at(0.0));
        CHECK(f.color.value() == glm::vec3(0.5f, 1.0f, 0.5f));
    }
    SECTION("disabled track, disabled timeline, unbound track: nothing happens") {
        timeline.tracks()[0].enabled = false;
        f.params.resetFinals();
        timeline.apply(at(1.0));
        CHECK(f.scale.value() == 2.0f);
        CHECK_THAT(d(f.color.value().z), WithinAbs(0.6, 1e-6)); // the other track still runs
        timeline.tracks()[0].enabled = true;

        timeline.enabled = false;
        f.params.resetFinals();
        timeline.apply(at(1.0));
        CHECK(f.scale.value() == 2.0f);
        CHECK(f.color.value() == glm::vec3(0.5f));
        timeline.enabled = true;

        timeline.unbind();
        f.params.resetFinals();
        timeline.apply(at(1.0));
        CHECK(f.scale.value() == 2.0f);
        CHECK(f.color.value() == glm::vec3(0.5f));
    }
    SECTION("apply is pure: the same clock gives the same values, repeatedly") {
        timeline.tracks()[0].mode = TrackMode::Add;
        f.params.resetFinals();
        timeline.apply(at(0.75));
        const float first = f.scale.value();
        const glm::vec3 firstColor = f.color.value();
        f.params.resetFinals();
        timeline.apply(at(123.0));
        f.params.resetFinals();
        timeline.apply(at(0.75));
        CHECK(f.scale.value() == first);
        CHECK(f.color.value() == firstColor);
        f.params.resetFinals();
        timeline.apply(at(0.75));
        CHECK(f.scale.value() == first);
    }
    SECTION("beat-based tracks read the beat clock") {
        timeline.tracks()[0].timeBase = TimeBase::Beats;
        f.params.resetFinals();
        timeline.apply(at(0.0, 1.0));
        CHECK(f.scale.value() == 0.5f);
        CHECK_THAT(d(f.color.value().x), WithinAbs(0.0, 1e-6)); // seconds track at t = 0
    }
    SECTION("int parameters round the automated value") {
        Track countTrack;
        countTrack.target = "orb/count";
        countTrack.addKey(key(0.0, 0.0f));
        countTrack.addKey(key(1.0, 10.0f));
        timeline.addTrack(countTrack);
        REQUIRE(timeline.bind(f.params).has_value());
        f.params.resetFinals();
        timeline.apply(at(0.26));
        CHECK(f.count.value() == 3);
    }
}

TEST_CASE("Timeline::bind reports unknown targets but binds the known ones", "[params][timeline]") {
    Fixture f;
    Timeline timeline;
    Track a;
    a.target = "orb/scale";
    Track b;
    b.target = "ghost/x";
    Track c;
    c.target = "ghost/y";
    Track b2;
    b2.target = "ghost/x";
    b2.component = 1;
    timeline.addTrack(a);
    timeline.addTrack(b);
    timeline.addTrack(c);
    timeline.addTrack(b2);
    const auto bound = timeline.bind(f.params);
    REQUIRE_FALSE(bound.has_value());
    CHECK(bound.error().message == "timeline: unknown targets: ghost/x, ghost/y");
    CHECK(timeline.tracks()[0].param == f.params.find("orb/scale"));
    CHECK(timeline.tracks()[1].param == nullptr);
    CHECK(timeline.tracks()[2].param == nullptr);
    CHECK(timeline.tracks()[3].param == nullptr);
    CHECK(timeline.tracks().size() == 4); // kept for a later scene

    timeline.unbind();
    CHECK(timeline.tracks()[0].param == nullptr);
    f.params.add(ParamDesc<float>{.path = "ghost/x", .hardMin = 0.0f, .hardMax = 1.0f});
    f.params.add(ParamDesc<float>{.path = "ghost/y", .hardMin = 0.0f, .hardMax = 1.0f});
    REQUIRE(timeline.bind(f.params).has_value());
    CHECK(timeline.tracks()[2].param != nullptr);
}

TEST_CASE("Timeline track management: add, find, remove, clear", "[params][timeline]") {
    Timeline timeline;
    CHECK(timeline.empty());
    Track a;
    a.target = "orb/scale";
    Track b;
    b.target = "orb/color";
    b.component = 2;
    timeline.addTrack(a);
    timeline.addTrack(b);
    timeline.addTrack(a); // duplicate target/component: allowed, warned
    CHECK(timeline.tracks().size() == 3);
    CHECK(timeline.findTrack("orb/scale") == &timeline.tracks()[0]); // first match
    CHECK(timeline.findTrack("orb/color") == nullptr);
    CHECK(timeline.findTrack("orb/color", 2) == &timeline.tracks()[1]);
    const Timeline& constTimeline = timeline;
    CHECK(constTimeline.findTrack("orb/color", 2) == &constTimeline.tracks()[1]);
    CHECK_FALSE(timeline.removeTrack(3));
    CHECK(timeline.removeTrack(0));
    CHECK(timeline.tracks().size() == 2);
    CHECK(timeline.findTrack("orb/color", 2) == &timeline.tracks()[0]);
    CHECK_FALSE(timeline.empty());
    timeline.clear();
    CHECK(timeline.empty());
}

TEST_CASE("Timeline::recordKey creates tracks from the base value and replaces at the same time",
          "[params][timeline]") {
    Fixture f;
    Timeline timeline;
    CHECK(timeline.recordKey(f.params, "nope/x", -1, 0.0) == nullptr);
    CHECK(timeline.recordKey(f.params, "orb/color", 3, 0.0) == nullptr); // out of range
    CHECK(timeline.empty());

    f.scale.setBase(1.5f);
    f.scale.setFinalComponent(0, 3.0f); // finals are not recorded
    Track* track = timeline.recordKey(f.params, "orb/scale", -1, 1.0);
    REQUIRE(track != nullptr);
    CHECK(track == timeline.findTrack("orb/scale"));
    CHECK(track->param == f.params.find("orb/scale"));
    CHECK(track->timeBase == TimeBase::Seconds);
    REQUIRE(track->keys.size() == 1);
    CHECK(track->keys[0].time == 1.0);
    CHECK(track->keys[0].value[0] == 1.5f);
    CHECK(track->keys[0].interp == KeyInterp::Linear);

    f.scale.setBase(2.5f);
    CHECK(timeline.recordKey(f.params, "orb/scale", -1, 1.0 + 1e-8, KeyInterp::Step) == track);
    REQUIRE(track->keys.size() == 1);
    CHECK(track->keys[0].value[0] == 2.5f);
    CHECK(track->keys[0].interp == KeyInterp::Step);
    CHECK(timeline.recordKey(f.params, "orb/scale", -1, 0.0) == track);
    REQUIRE(track->keys.size() == 2);
    CHECK(track->keys[0].time == 0.0);
    CHECK(track->keys[0].value[0] == 2.5f);

    f.color.setBase(glm::vec3(0.1f, 0.2f, 0.3f));
    Track* all = timeline.recordKey(f.params, "orb/color", -1, 4.0, KeyInterp::Smooth, TimeBase::Beats);
    REQUIRE(all != nullptr);
    CHECK(all->timeBase == TimeBase::Beats);
    CHECK(all->keys[0].value == KeyValue{0.1f, 0.2f, 0.3f, 0.0f});
    Track* green = timeline.recordKey(f.params, "orb/color", 1, 0.0);
    REQUIRE(green != nullptr);
    CHECK(green != all);
    CHECK(green->component == 1);
    CHECK(green->keys[0].value == KeyValue{0.2f, 0.0f, 0.0f, 0.0f});
    CHECK(timeline.tracks().size() == 3);

    // Recording onto an existing unbound track binds it.
    timeline.unbind();
    CHECK(timeline.recordKey(f.params, "orb/scale", -1, 5.0) != nullptr);
    CHECK(timeline.findTrack("orb/scale")->param != nullptr);
}

TEST_CASE("Timeline::isAutomated", "[params][timeline]") {
    Fixture f;
    Timeline timeline;
    CHECK_FALSE(timeline.isAutomated("orb/scale"));
    REQUIRE(timeline.recordKey(f.params, "orb/scale", -1, 0.0) != nullptr);
    REQUIRE(timeline.recordKey(f.params, "orb/color", 1, 0.0) != nullptr);
    CHECK(timeline.isAutomated("orb/scale"));
    CHECK(timeline.isAutomated("orb/scale", 0));
    CHECK(timeline.isAutomated("orb/color"));
    CHECK(timeline.isAutomated("orb/color", 1));
    CHECK_FALSE(timeline.isAutomated("orb/color", 0));
    CHECK_FALSE(timeline.isAutomated("orb/count"));

    timeline.findTrack("orb/scale")->enabled = false;
    CHECK_FALSE(timeline.isAutomated("orb/scale"));
    timeline.findTrack("orb/scale")->enabled = true;
    timeline.unbind();
    CHECK_FALSE(timeline.isAutomated("orb/scale"));
    CHECK_FALSE(timeline.isAutomated("orb/color", 1));
    REQUIRE(timeline.bind(f.params).has_value());
    CHECK(timeline.isAutomated("orb/color", 1));
}

// ---- cues ---------------------------------------------------------------------------------------

TEST_CASE("Timeline cues are sorted and located on the clock", "[params][timeline]") {
    Timeline timeline;
    CHECK(timeline.cueAt(at(0.0)).index == -1);
    Cue drop;
    drop.time = 8.0;
    drop.name = "drop";
    drop.preset = "big";
    drop.morphSeconds = 2.0;
    Cue intro;
    intro.time = 0.0;
    intro.name = "intro";
    Cue outro;
    outro.time = 16.0;
    outro.name = "outro";
    outro.preset = "calm";
    timeline.addCue(drop);
    timeline.addCue(outro);
    Cue& added = timeline.addCue(intro);
    CHECK(added.name == "intro");
    CHECK(&added == &timeline.cues()[0]);
    REQUIRE(timeline.cues().size() == 3);
    CHECK(timeline.cues()[0].name == "intro");
    CHECK(timeline.cues()[1].name == "drop");
    CHECK(timeline.cues()[2].name == "outro");

    SECTION("before, at, between and after") {
        CHECK(timeline.cueAt(at(-0.5)).index == -1);
        CHECK(timeline.cueAt(at(-0.5)).progress == 1.0f);
        Timeline::CueState s = timeline.cueAt(at(0.0));
        CHECK(s.index == 0);
        CHECK(s.progress == 1.0f); // instant cue
        CHECK(timeline.cueAt(at(7.999)).index == 0);
        CHECK(timeline.cueAt(at(8.0)).index == 1);
        CHECK(timeline.cueAt(at(12.0)).index == 1);
        CHECK(timeline.cueAt(at(16.0)).index == 2);
        CHECK(timeline.cueAt(at(1000.0)).index == 2);
        CHECK(timeline.cueAt(at(1000.0)).progress == 1.0f);
    }
    SECTION("morph progress runs 0..1 over morphSeconds") {
        CHECK(timeline.cueAt(at(8.0)).progress == 0.0f);
        CHECK_THAT(d(timeline.cueAt(at(8.5)).progress), WithinAbs(0.25, 1e-6));
        CHECK_THAT(d(timeline.cueAt(at(9.0)).progress), WithinAbs(0.5, 1e-6));
        CHECK(timeline.cueAt(at(10.0)).progress == 1.0f);
        CHECK(timeline.cueAt(at(15.0)).progress == 1.0f);
    }
    SECTION("beat-based cues compare against beats and morph in beats") {
        Cue bar;
        bar.time = 4.0;
        bar.name = "bar2";
        bar.timeBase = TimeBase::Beats;
        bar.morphSeconds = 2.0; // beats, for a beat-based cue
        timeline.addCue(bar);
        CHECK(timeline.cues()[1].name == "bar2"); // sorted by raw time among the others
        // Seconds are far along but the beat clock has not reached beat 4: the seconds cue wins.
        Timeline::CueState s = timeline.cueAt(at(12.0, 3.0));
        CHECK(s.index == 2); // "drop"
        // Beat 5: the latest reached cue by index is still "drop" (index 2 > 1).
        CHECK(timeline.cueAt(at(12.0, 5.0)).index == 2);
        // Before the drop, with beats past 4: the bar cue is the latest reached.
        s = timeline.cueAt(at(1.0, 5.0));
        CHECK(s.index == 1);
        CHECK_THAT(d(s.progress), WithinAbs(0.5, 1e-6));
        CHECK(timeline.cueAt(at(1.0, 4.0)).progress == 0.0f);
        CHECK(timeline.cueAt(at(1.0, 7.0)).progress == 1.0f);
        CHECK(timeline.cueAt(at(1.0, 3.9)).index == 0);
    }
    SECTION("remove and sort") {
        CHECK_FALSE(timeline.removeCue(3));
        CHECK(timeline.removeCue(1));
        CHECK(timeline.cues().size() == 2);
        CHECK(timeline.cues()[1].name == "outro");
        timeline.cues()[0].time = 20.0;
        timeline.sortCues();
        CHECK(timeline.cues()[0].name == "outro");
        CHECK(timeline.cues()[1].name == "intro");
    }
}

TEST_CASE("Timeline::durationSeconds ignores beat-based tracks and cues", "[params][timeline]") {
    Timeline timeline;
    CHECK(timeline.durationSeconds() == 0.0);
    Track seconds;
    seconds.target = "a";
    seconds.addKey(key(0.0, 0.0f));
    seconds.addKey(key(6.5, 1.0f));
    Track beats;
    beats.target = "b";
    beats.timeBase = TimeBase::Beats;
    beats.addKey(key(0.0, 0.0f));
    beats.addKey(key(64.0, 1.0f));
    Track emptyTrack;
    emptyTrack.target = "c";
    timeline.addTrack(seconds);
    timeline.addTrack(beats);
    timeline.addTrack(emptyTrack);
    CHECK(timeline.durationSeconds() == 6.5);
    Cue late;
    late.time = 9.0;
    timeline.addCue(late);
    CHECK(timeline.durationSeconds() == 9.0);
    Cue beatCue;
    beatCue.time = 128.0;
    beatCue.timeBase = TimeBase::Beats;
    timeline.addCue(beatCue);
    CHECK(timeline.durationSeconds() == 9.0);
}

// ---- JSON ---------------------------------------------------------------------------------------

namespace {
Timeline buildRichTimeline(ParameterSet& params) {
    Timeline timeline;
    timeline.enabled = false;
    Track scale;
    scale.target = "orb/scale";
    scale.mode = TrackMode::Add;
    scale.loopLength = 4.0;
    scale.enabled = false;
    scale.addKey(key(0.0, 0.25f, KeyInterp::Step));
    scale.addKey(key(0.5, 0.5f, KeyInterp::Linear));
    scale.addKey(key(1.0, 0.75f, KeyInterp::Smooth));
    scale.addKey(key(1.5, 1.0f, KeyInterp::EaseIn));
    scale.addKey(key(2.0, 0.75f, KeyInterp::EaseOut));
    scale.addKey(key(2.5, 0.5f, KeyInterp::EaseInOut));
    Key bez = key(3.0, 0.125f, KeyInterp::Bezier);
    bez.tangentIn[0] = -1.5f;
    bez.tangentOut[0] = 2.25f;
    scale.addKey(bez);
    timeline.addTrack(scale);

    Track color;
    color.target = "orb/color";
    color.timeBase = TimeBase::Beats;
    color.mode = TrackMode::Multiply;
    Key c0 = vecKey(0.0, 0.1f, 0.2f, 0.3f, KeyInterp::Bezier);
    c0.tangentOut = {1.0f, -1.0f, 0.5f, 0.0f};
    Key c1 = vecKey(8.0, 0.9f, 0.8f, 0.7f, KeyInterp::Linear);
    c1.tangentIn = {0.25f, 0.0f, -0.25f, 0.0f};
    color.addKey(c0);
    color.addKey(c1);
    timeline.addTrack(color);

    Track green;
    green.target = "orb/color";
    green.component = 1;
    green.addKey(key(1.0, 0.33f));
    timeline.addTrack(green);

    Cue drop;
    drop.time = 8.0;
    drop.name = "drop";
    drop.preset = "big";
    drop.morphSeconds = 0.5;
    Cue bar;
    bar.time = 16.0;
    bar.name = "bar 5";
    bar.timeBase = TimeBase::Beats;
    timeline.addCue(bar);
    timeline.addCue(drop);
    REQUIRE(timeline.bind(params).has_value());
    return timeline;
}
} // namespace

TEST_CASE("Timeline JSON round trip is exact", "[params][timeline][json]") {
    Fixture f;
    const Timeline original = buildRichTimeline(f.params);
    const json j = original.toJson();
    CHECK(j["enabled"] == false);
    REQUIRE(j["tracks"].is_array());
    REQUIRE(j["tracks"].size() == 3);
    CHECK(j["tracks"][0]["target"] == "orb/scale");
    CHECK(j["tracks"][0]["component"] == -1);
    CHECK(j["tracks"][0]["timeBase"] == "seconds");
    CHECK(j["tracks"][0]["mode"] == "add");
    CHECK(j["tracks"][0]["loopLength"] == 4.0);
    CHECK(j["tracks"][0]["enabled"] == false);
    REQUIRE(j["tracks"][0]["keys"].size() == 7);
    CHECK(j["tracks"][0]["keys"][0]["interp"] == "step");
    CHECK(j["tracks"][0]["keys"][0]["value"] == json::array({0.25}));
    CHECK_FALSE(j["tracks"][0]["keys"][0].contains("tangentIn")); // tangents only on bezier keys
    CHECK(j["tracks"][0]["keys"][6]["interp"] == "bezier");
    CHECK(j["tracks"][0]["keys"][6]["tangentIn"] == json::array({-1.5}));
    CHECK(j["tracks"][0]["keys"][6]["tangentOut"] == json::array({2.25}));
    CHECK(j["tracks"][1]["timeBase"] == "beats");
    CHECK(j["tracks"][1]["mode"] == "multiply");
    CHECK(j["tracks"][1]["keys"][0]["value"].size() == 3); // bound vec3: three components
    CHECK(j["tracks"][1]["keys"][0]["tangentOut"].size() == 3);
    CHECK(j["tracks"][2]["component"] == 1);
    CHECK(j["tracks"][2]["keys"][0]["value"].size() == 1);
    REQUIRE(j["cues"].size() == 2);
    CHECK(j["cues"][0]["name"] == "drop");
    CHECK(j["cues"][0]["preset"] == "big");
    CHECK(j["cues"][0]["morphSeconds"] == 0.5);
    CHECK(j["cues"][1]["timeBase"] == "beats");

    Timeline back;
    REQUIRE(back.fromJson(j).has_value());
    CHECK(back.enabled == false);
    REQUIRE(back.tracks().size() == 3);
    CHECK(back.tracks()[0].param == nullptr); // bind() afterwards
    REQUIRE(back.bind(f.params).has_value());
    CHECK(back.toJson() == j);
    for (std::size_t t = 0; t < 3; ++t) {
        const Track& a = original.tracks()[t];
        const Track& b = back.tracks()[t];
        CHECK(a.target == b.target);
        CHECK(a.component == b.component);
        CHECK(a.timeBase == b.timeBase);
        CHECK(a.mode == b.mode);
        CHECK(a.loopLength == b.loopLength);
        CHECK(a.enabled == b.enabled);
        REQUIRE(a.keys.size() == b.keys.size());
        for (std::size_t k = 0; k < a.keys.size(); ++k) {
            CHECK(a.keys[k].time == b.keys[k].time);
            CHECK(a.keys[k].value == b.keys[k].value);
            CHECK(a.keys[k].interp == b.keys[k].interp);
            if (a.keys[k].interp == KeyInterp::Bezier) {
                CHECK(a.keys[k].tangentIn == b.keys[k].tangentIn);
                CHECK(a.keys[k].tangentOut == b.keys[k].tangentOut);
            }
        }
        // Evaluation of the round-tripped track is bit-identical.
        for (int s = 0; s <= 40; ++s) {
            const double time = s * 0.2;
            CHECK(a.evaluate(time) == b.evaluate(time));
        }
    }
    // The key after a Bezier key carries the span's arriving tangent, so it is serialised too.
    CHECK(j["tracks"][1]["keys"][1]["tangentIn"] == json::array({0.25, 0.0, -0.25}));
    CHECK(back.tracks()[1].keys[1].tangentIn == KeyValue{0.25f, 0.0f, -0.25f, 0.0f});
    CHECK_FALSE(j["tracks"][0]["keys"][1].contains("tangentIn")); // linear after step: none
    REQUIRE(back.cues().size() == 2);
    CHECK(back.cues()[0].time == 8.0);
    CHECK(back.cues()[0].morphSeconds == 0.5);
    CHECK(back.cues()[1].name == "bar 5");
    CHECK(back.cues()[1].timeBase == TimeBase::Beats);

    // An unbound vector track keeps every non-zero component on save.
    Timeline unbound;
    Track v;
    v.target = "orb/color";
    v.addKey(vecKey(0.0, 0.0f, 0.0f, 0.75f));
    unbound.addTrack(v);
    const json uj = unbound.toJson();
    CHECK(uj["tracks"][0]["keys"][0]["value"] == json::array({0.0, 0.0, 0.75}));
    Timeline unboundBack;
    REQUIRE(unboundBack.fromJson(uj).has_value());
    CHECK(unboundBack.tracks()[0].keys[0].value == v.keys[0].value);

    // Minimal documents: defaults fill in, short value arrays are zero-padded, bare numbers accepted.
    Timeline minimal;
    REQUIRE(minimal.fromJson(json::object()).has_value());
    CHECK(minimal.enabled);
    CHECK(minimal.empty());
    REQUIRE(minimal
                .fromJson(json{
                    {"tracks",
                     json::array({json{
                         {"target", "x"},
                         {"keys", json::array({json{{"time", 1.0}, {"value", 2.0}},
                                               json{{"time", 0.0}, {"value", json::array({1.0, 3.0})}}})}}})},
                    {"cues", json::array({json{{"time", 3.0}}})}})
                .has_value());
    REQUIRE(minimal.tracks().size() == 1);
    const Track& x = minimal.tracks()[0];
    CHECK(x.component == -1);
    CHECK(x.timeBase == TimeBase::Seconds);
    CHECK(x.mode == TrackMode::Replace);
    CHECK(x.loopLength == 0.0);
    CHECK(x.enabled);
    REQUIRE(x.keys.size() == 2);
    CHECK(x.keys[0].time == 0.0); // sorted on load
    CHECK(x.keys[0].value == KeyValue{1.0f, 3.0f, 0.0f, 0.0f});
    CHECK(x.keys[1].value == KeyValue{2.0f, 0.0f, 0.0f, 0.0f});
    CHECK(x.keys[1].interp == KeyInterp::Linear);
    REQUIRE(minimal.cues().size() == 1);
    CHECK(minimal.cues()[0].name.empty());
    CHECK(minimal.cues()[0].morphSeconds == 0.0);
}

TEST_CASE("Timeline::fromJson rejects malformed documents and leaves the timeline untouched",
          "[params][timeline][json]") {
    Fixture f;
    Timeline timeline = buildRichTimeline(f.params);
    const json before = timeline.toJson();

    auto check = [&](const json& doc, const char* expected) {
        const auto result = timeline.fromJson(doc);
        REQUIRE_FALSE(result.has_value());
        CHECK_THAT(result.error().message, ContainsSubstring(expected));
        CHECK(timeline.toJson() == before);
        CHECK(timeline.tracks()[0].param != nullptr); // bindings survive a rejected load
    };

    check(json(5), "object");
    check(json{{"enabled", "yes"}}, "'enabled'");
    check(json{{"tracks", json::object()}}, "'tracks' must be an array");
    check(json{{"tracks", json::array({1})}}, "tracks[0]");
    check(json{{"tracks", json::array({json{{"keys", json::array()}}})}}, "'target'");
    check(json{{"tracks", json::array({json{{"target", 3}}})}}, "'target' must be a string");
    check(json{{"tracks", json::array({json{{"target", "a"}, {"keys", json::object()}}})}},
          "'keys' must be an array");
    check(json{{"tracks", json::array({json{{"target", "a"}, {"component", 1.5}}})}}, "'component'");
    check(json{{"tracks", json::array({json{{"target", "a"}, {"component", -2}}})}}, "'component'");
    check(json{{"tracks", json::array({json{{"target", "a"}, {"timeBase", "bars"}}})}},
          "unknown value 'bars'");
    check(json{{"tracks", json::array({json{{"target", "a"}, {"mode", "min"}}})}}, "unknown value 'min'");
    check(json{{"tracks", json::array({json{{"target", "a"}, {"loopLength", -1.0}}})}}, "'loopLength'");
    check(json{{"tracks", json::array({json{{"target", "a"}, {"enabled", 1}}})}}, "'enabled'");
    check(
        json{
            {"tracks",
             json::array(
                 {json{{"target", "ok"}},
                  json{{"target", "a"},
                       {"keys", json::array({json{{"time", 0.0}, {"value", json::array({1.0})}},
                                             json{{"time", 1.0}, {"value", 1.0}, {"interp", "cubic"}}})}}})}},
        "tracks[1]: keys[1]: unknown value 'cubic'");
    check(
        json{{"tracks", json::array({json{{"target", "a"}, {"keys", json::array({json{{"value", 1.0}}})}}})}},
        "keys[0]: missing required key 'time'");
    check(
        json{{"tracks", json::array({json{{"target", "a"}, {"keys", json::array({json{{"time", 1.0}}})}}})}},
        "keys[0]: missing required key 'value'");
    check(json{{"tracks", json::array({json{{"target", "a"},
                                            {"keys", json::array({json{{"time", "0"}, {"value", 1.0}}})}}})}},
          "'time' must be a number");
    check(json{{"tracks",
                json::array({json{
                    {"target", "a"},
                    {"keys", json::array({json{{"time", 0.0}, {"value", json::array({1.0, "x"})}}})}}})}},
          "'value' must be an array of numbers");
    check(json{{"tracks",
                json::array({json{
                    {"target", "a"},
                    {"keys", json::array({json{{"time", 0.0},
                                               {"value", json::array({1.0, 2.0, 3.0, 4.0, 5.0})}}})}}})}},
          "'value' must have 1 to 4 entries");
    check(json{{"tracks",
                json::array({json{
                    {"target", "a"},
                    {"keys", json::array({json{{"time", 0.0}, {"value", 1.0}, {"tangentIn", "x"}}})}}})}},
          "'tangentIn'");
    check(json{{"tracks", json::array({json{{"target", "a"}, {"keys", json::array({7})}}})}},
          "keys[0]: key must be");
    check(json{{"cues", 1}}, "'cues' must be an array");
    check(json{{"cues", json::array({json{{"name", "x"}}})}}, "cues[0]: missing required key 'time'");
    check(json{{"cues", json::array({json{{"time", 1.0}, {"preset", 4}}})}}, "'preset' must be a string");
    check(json{{"cues", json::array({json{{"time", 1.0}, {"morphSeconds", -1.0}}})}}, "'morphSeconds'");
    check(json{{"cues", json::array({json{{"time", 1.0}, {"timeBase", 2}}})}}, "'timeBase' must be a string");
}

TEST_CASE("Timeline evaluation is deterministic across instances and orderings", "[params][timeline]") {
    Fixture f;
    Timeline a = buildRichTimeline(f.params);
    a.enabled = true;
    for (Track& track : a.tracks()) {
        track.enabled = true;
    }
    Timeline b;
    REQUIRE(b.fromJson(a.toJson()).has_value());
    REQUIRE(b.bind(f.params).has_value());

    std::vector<double> times{0.0, 0.3, 1.7, 2.9, 3.0, 5.25, -2.0, 100.0};
    std::vector<float> scaleA;
    std::vector<glm::vec3> colorA;
    for (const double t : times) {
        f.params.resetFinals();
        a.apply(at(t, t * 2.0));
        scaleA.push_back(f.scale.value());
        colorA.push_back(f.color.value());
    }
    // Evaluate in a different order on the other instance; every value is bit-identical.
    for (std::size_t i = times.size(); i-- > 0;) {
        f.params.resetFinals();
        b.apply(at(times[i], times[i] * 2.0));
        CHECK(f.scale.value() == scaleA[i]);
        CHECK(f.color.value() == colorA[i]);
    }
    CHECK(a.cueAt(at(8.2, 16.0)).index == b.cueAt(at(8.2, 16.0)).index);
    CHECK(a.cueAt(at(8.2, 16.0)).progress == b.cueAt(at(8.2, 16.0)).progress);
}
