// Cinematic events (ADR-098): the boundary between what bakes and what cannot, and the properties
// that boundary exists to protect.
//
// The brief's testing section asks for event ordering, shot edges firing exactly once, deterministic
// playback, scrubbing forwards and backwards without events being skipped or double-fired, beat and
// section events matching the analysis, and transitions. Every one of those is here, and most of
// them are checked twice: once for the property, and once for the *defect* the property exists to
// stop -- a value that holds backwards from t = 0, a shake that opens the piece already shaking, a
// seek that replays sixty seconds of intent in one frame. A test that would still pass with the
// guard removed is not guarding anything.

#include "comp/layer_stack.hpp"
#include "params/parameter_set.hpp"
#include "params/timeline.hpp"
#include "scene/camera.hpp"
#include "seq/director.hpp"
#include "seq/events.hpp"
#include "seq/layer_sink.hpp"
#include "seq/sequence.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace avgen;
using Catch::Approx;

namespace {

seq::Shot keyedShot(std::string name, double start, double duration) {
    seq::Shot shot;
    shot.name = std::move(name);
    shot.startSeconds = start;
    shot.durationSeconds = duration;
    shot.camera.kind = seq::CameraKind::Keys;
    shot.camera.keys.push_back(seq::CameraKey{.timeSeconds = 0.0, .position = {0.0f, 2.0f, 10.0f}});
    shot.camera.keys.push_back(
        seq::CameraKey{.timeSeconds = duration, .position = {0.0f, 2.0f, 6.0f}});
    return shot;
}

seq::SequenceEvent event(std::string id, seq::Trigger when, seq::EventAction what,
                         int priority = 0) {
    seq::SequenceEvent e;
    e.id = std::move(id);
    e.when = when;
    e.what = std::move(what);
    e.priority = priority;
    return e;
}

seq::EventAction setParam(std::string path, float value,
                          params::TrackMode mode = params::TrackMode::Replace) {
    seq::EventAction a;
    a.kind = seq::EventActionKind::SetParameter;
    a.target = std::move(path);
    a.amount = glm::vec4(value, 0.0f, 0.0f, 0.0f);
    a.mode = mode;
    return a;
}

// Reads the baked timeline back the way the engine does, so a test and a render agree.
params::Timeline timelineOf(const seq::BakeResult& baked) {
    params::Timeline timeline;
    REQUIRE(timeline.fromJson(baked.timeline));
    return timeline;
}

const params::Track* trackFor(const params::Timeline& timeline, const std::string& target,
                              params::TrackMode mode = params::TrackMode::Replace) {
    for (const params::Track& t : timeline.tracks()) {
        if (t.target == target && t.mode == mode) {
            return &t;
        }
    }
    return nullptr;
}

float valueAt(const params::Timeline& timeline, const std::string& target, double seconds,
              params::TrackMode mode = params::TrackMode::Replace) {
    const params::Track* track = trackFor(timeline, target, mode);
    REQUIRE(track != nullptr);
    return track->evaluate(seconds)[0];
}

bool hasTrack(const params::Timeline& timeline, const std::string& target) {
    return std::any_of(timeline.tracks().begin(), timeline.tracks().end(),
                       [&](const params::Track& t) { return t.target == target; });
}

} // namespace

// ---- the boundary --------------------------------------------------------------------------------

TEST_CASE("The bake/live boundary is a pure predicate an author can read", "[seq][events]") {
    // Both halves have to be knowable: a trigger says whether the event can be *scheduled*, an
    // action says whether it can be *baked*. This is the whole of ADR-098's decision, and it is
    // checkable without running anything -- which is the point.
    for (const seq::TriggerKind kind :
         {seq::TriggerKind::Time, seq::TriggerKind::Beat, seq::TriggerKind::Bar,
          seq::TriggerKind::Section, seq::TriggerKind::ShotStart, seq::TriggerKind::ShotEnd,
          seq::TriggerKind::Cue, seq::TriggerKind::ClipEnd}) {
        INFO(seq::triggerKindName(kind));
        CHECK(seq::triggerIsScheduled(kind));
    }
    for (const seq::TriggerKind kind :
         {seq::TriggerKind::ActionComplete, seq::TriggerKind::InteractionComplete,
          seq::TriggerKind::VolumeEnter, seq::TriggerKind::VolumeExit}) {
        INFO(seq::triggerKindName(kind));
        CHECK_FALSE(seq::triggerIsScheduled(kind));
    }
    for (const seq::EventActionKind kind :
         {seq::EventActionKind::SetParameter, seq::EventActionKind::CameraShake,
          seq::EventActionKind::PlayClip, seq::EventActionKind::Overlay,
          seq::EventActionKind::SceneTransition}) {
        INFO(seq::eventActionKindName(kind));
        CHECK(seq::actionIsBaked(kind));
    }
    CHECK_FALSE(seq::actionIsBaked(seq::EventActionKind::EntityAction));
    CHECK_FALSE(seq::actionIsBaked(seq::EventActionKind::Notify));

    // Every name round-trips, so a project file written today reads the same tomorrow.
    for (int i = 0; i <= static_cast<int>(seq::TriggerKind::VolumeExit); ++i) {
        const auto kind = static_cast<seq::TriggerKind>(i);
        CHECK(seq::triggerKindFromName(seq::triggerKindName(kind)) == kind);
    }
    for (int i = 0; i <= static_cast<int>(seq::EventActionKind::Notify); ++i) {
        const auto kind = static_cast<seq::EventActionKind>(i);
        CHECK(seq::eventActionKindFromName(seq::eventActionKindName(kind)) == kind);
    }
}

TEST_CASE("A known trigger with an imperative action is scheduled, not baked", "[seq][events]") {
    seq::Sequence piece;
    piece.shots.push_back(keyedShot("a", 0.0, 5.0));

    seq::EventAction walk;
    walk.kind = seq::EventActionKind::EntityAction;
    walk.target = "elder";
    walk.value = "walkTo";
    walk.argument = "door";
    piece.events.push_back(
        event("go", seq::Trigger{.kind = seq::TriggerKind::ShotStart, .name = "a"}, walk));
    piece.events.push_back(event("fog", seq::Trigger{.kind = seq::TriggerKind::Time, .timeSeconds = 2.0},
                                 setParam("scene/fogDensity", 0.4f, params::TrackMode::Add)));

    seq::NullLayerSink sink;
    auto baked = piece.bake(sink);
    REQUIRE(baked);
    // The time is known for both. Only the *parameter* one stopped being an event.
    CHECK(baked->events.baked.size() == 1);
    CHECK(baked->events.dispatches.size() == 1);
    CHECK(baked->events.live.empty());
    CHECK(baked->events.dispatches.front().eventIndex == 0);
    // ...and the bake says so out loud rather than leaving an author to discover it in a scrub.
    CHECK(std::any_of(baked->warnings.begin(), baked->warnings.end(), [](const std::string& w) {
        return w.find("cannot be baked") != std::string::npos;
    }));
}

TEST_CASE("A volume trigger is live and no amount of context schedules it", "[seq][events]") {
    seq::Sequence piece;
    piece.shots.push_back(keyedShot("a", 0.0, 5.0));
    piece.markers.push_back(seq::Marker{2.0, "hit", seq::MarkerKind::Cue});

    piece.events.push_back(event("door",
                                 seq::Trigger{.kind = seq::TriggerKind::VolumeEnter,
                                              .name = "porch",
                                              .subject = "elder"},
                                 setParam("scene/fogDensity", 2.0f)));
    seq::NullLayerSink sink;
    auto baked = piece.bake(sink);
    REQUIRE(baked);
    // The action would bake perfectly well. The trigger is what cannot be known, and that is
    // enough: an entity's position at t depends on how it got there (ADR-091).
    CHECK(baked->events.baked.empty());
    CHECK(baked->events.dispatches.empty());
    REQUIRE(baked->events.live.size() == 1);
    CHECK(baked->events.live.front() == 0);
    CHECK_FALSE(hasTrack(timelineOf(*baked), "scene/fogDensity"));
}

// ---- the baked tier ------------------------------------------------------------------------------

TEST_CASE("A shot edge fires exactly once, whichever way the playhead reaches it", "[seq][events]") {
    seq::Sequence piece;
    piece.shots.push_back(keyedShot("wide", 0.0, 6.0));
    piece.shots.push_back(keyedShot("close", 6.0, 6.0));
    piece.shots.push_back(keyedShot("wider", 12.0, 6.0));

    piece.events.push_back(event("open",
                                 seq::Trigger{.kind = seq::TriggerKind::ShotStart, .name = "close"},
                                 setParam("scene/fogDensity", 3.0f, params::TrackMode::Add)));
    piece.events.push_back(event("shut",
                                 seq::Trigger{.kind = seq::TriggerKind::ShotEnd, .name = "close"},
                                 setParam("env/sky/sunIntensity", 5.0f, params::TrackMode::Add)));

    seq::NullLayerSink sink;
    auto baked = piece.bake(sink);
    REQUIRE(baked);
    REQUIRE(baked->events.baked.size() == 2);
    CHECK(baked->events.baked[0].timeSeconds == Approx(6.0));
    CHECK(baked->events.baked[1].timeSeconds == Approx(12.0));

    const params::Timeline timeline = timelineOf(*baked);
    // "Exactly once" in a baked world is not a counter. It is that the value changes at one second
    // and holds on both sides of it -- which no scrub can skip and no scrub can do twice.
    CHECK(valueAt(timeline, "scene/fogDensity", 0.0, params::TrackMode::Add) == Approx(0.0f));
    CHECK(valueAt(timeline, "scene/fogDensity", 5.999, params::TrackMode::Add) == Approx(0.0f));
    CHECK(valueAt(timeline, "scene/fogDensity", 6.0, params::TrackMode::Add) == Approx(3.0f));
    CHECK(valueAt(timeline, "scene/fogDensity", 100.0, params::TrackMode::Add) == Approx(3.0f));
    CHECK(valueAt(timeline, "env/sky/sunIntensity", 11.9, params::TrackMode::Add) == Approx(0.0f));
    CHECK(valueAt(timeline, "env/sky/sunIntensity", 12.0, params::TrackMode::Add) == Approx(5.0f));

    // An unnamed shot trigger means every shot, which is the reading that makes "fade in on every
    // cut" one event rather than three.
    seq::Sequence all = piece;
    all.events.clear();
    all.events.push_back(event("each", seq::Trigger{.kind = seq::TriggerKind::ShotStart},
                               setParam("scene/fogDensity", 1.0f, params::TrackMode::Add)));
    auto everyShot = all.bake(sink);
    REQUIRE(everyShot);
    CHECK(everyShot->events.baked.size() == 3);
}

TEST_CASE("Scrubbing a baked event list is four pure evaluations", "[seq][events]") {
    seq::Sequence piece;
    piece.shots.push_back(keyedShot("a", 0.0, 60.0));
    for (int i = 0; i < 12; ++i) {
        piece.events.push_back(
            event("step" + std::to_string(i),
                  seq::Trigger{.kind = seq::TriggerKind::Time, .timeSeconds = 4.0 * (i + 1)},
                  setParam("scene/fogDensity", static_cast<float>(i + 1), params::TrackMode::Add)));
    }
    seq::NullLayerSink sink;
    auto baked = piece.bake(sink);
    REQUIRE(baked);
    const params::Timeline timeline = timelineOf(*baked);

    // The spec's own scrub: 10 -> 45 -> 3 -> 30, then the same four times in a different order.
    const std::vector<double> walk{10.0, 45.0, 3.0, 30.0};
    std::vector<float> first;
    for (const double t : walk) {
        first.push_back(valueAt(timeline, "scene/fogDensity", t, params::TrackMode::Add));
    }
    std::vector<float> second;
    for (const double t : {30.0, 3.0, 45.0, 10.0}) {
        second.push_back(valueAt(timeline, "scene/fogDensity", t, params::TrackMode::Add));
    }
    std::reverse(second.begin(), second.end());
    CHECK(first == second);
    // ...and each is the value the *list* says, not the value a fired counter would have reached.
    CHECK(first[0] == Approx(2.0f));  // two steps have happened by 10 s
    CHECK(first[1] == Approx(11.0f)); // eleven by 45 s
    CHECK(first[2] == Approx(0.0f));  // none by 3 s -- a backwards scrub un-fires, because nothing
                                      // ever fired
    CHECK(first[3] == Approx(7.0f));
}

TEST_CASE("A bake of the same events is byte-identical", "[seq][events]") {
    seq::Sequence piece;
    piece.shots.push_back(keyedShot("a", 0.0, 30.0));
    piece.setBeatMarkers(std::vector<double>{0.5, 1.0, 1.5, 2.0, 2.5, 3.0, 3.5, 4.0});
    piece.events.push_back(event("beat",
                                 seq::Trigger{.kind = seq::TriggerKind::Beat, .every = 2},
                                 setParam("scene/fogDensity", 1.0f, params::TrackMode::Add), 1));
    piece.events.push_back(event("time",
                                 seq::Trigger{.kind = seq::TriggerKind::Time, .timeSeconds = 1.0},
                                 setParam("env/sky/sunIntensity", 2.0f, params::TrackMode::Add), 0));
    seq::NullLayerSink sink;
    auto once = piece.bake(sink);
    REQUIRE(once);
    sink.clear();
    auto twice = piece.bake(sink);
    REQUIRE(twice);
    CHECK(once->timeline.dump() == twice->timeline.dump());
    CHECK(once->events.baked.size() == twice->events.baked.size());
}

TEST_CASE("Two events at one instant fire in priority then declaration order", "[seq][events]") {
    seq::Sequence piece;
    piece.shots.push_back(keyedShot("a", 0.0, 10.0));
    const seq::Trigger at5{.kind = seq::TriggerKind::Time, .timeSeconds = 5.0};
    piece.events.push_back(event("late", at5, setParam("a", 1.0f), 10));
    piece.events.push_back(event("early", at5, setParam("b", 1.0f), -10));
    piece.events.push_back(event("first-declared", at5, setParam("c", 1.0f), 0));
    piece.events.push_back(event("second-declared", at5, setParam("d", 1.0f), 0));

    seq::NullLayerSink sink;
    auto baked = piece.bake(sink);
    REQUIRE(baked);
    REQUIRE(baked->events.baked.size() == 4);
    CHECK(piece.events[baked->events.baked[0].eventIndex].id == "early");
    CHECK(piece.events[baked->events.baked[1].eventIndex].id == "first-declared");
    CHECK(piece.events[baked->events.baked[2].eventIndex].id == "second-declared");
    CHECK(piece.events[baked->events.baked[3].eventIndex].id == "late");
}

TEST_CASE("Beat, bar, section and cue events match the analysis", "[seq][events]") {
    seq::Sequence piece;
    piece.shots.push_back(keyedShot("a", 0.0, 16.0));
    std::vector<double> beats;
    for (int i = 0; i < 16; ++i) {
        beats.push_back(0.5 * i); // 120 bpm
    }
    piece.setBeatMarkers(beats);
    piece.markers.push_back(seq::Marker{4.0, "Drop", seq::MarkerKind::Section});
    piece.markers.push_back(seq::Marker{0.0, "Intro", seq::MarkerKind::Section});
    piece.markers.push_back(seq::Marker{7.25, "hit", seq::MarkerKind::Cue});

    piece.events.push_back(event("every-beat", seq::Trigger{.kind = seq::TriggerKind::Beat},
                                 setParam("a", 1.0f)));
    piece.events.push_back(event("every-bar", seq::Trigger{.kind = seq::TriggerKind::Bar},
                                 setParam("b", 1.0f)));
    piece.events.push_back(event("on-drop",
                                 seq::Trigger{.kind = seq::TriggerKind::Section, .name = "Drop"},
                                 setParam("c", 1.0f)));
    piece.events.push_back(event("on-cue",
                                 seq::Trigger{.kind = seq::TriggerKind::Cue, .name = "hit"},
                                 setParam("d", 1.0f)));

    seq::NullLayerSink sink;
    auto baked = piece.bake(sink);
    REQUIRE(baked);
    const auto count = [&](const std::string& id) {
        return std::count_if(baked->events.baked.begin(), baked->events.baked.end(),
                             [&](const seq::Firing& f) { return piece.events[f.eventIndex].id == id; });
    };
    CHECK(count("every-beat") == 16);
    CHECK(count("every-bar") == 4); // sixteen beats, four to the bar
    CHECK(count("on-drop") == 1);
    CHECK(count("on-cue") == 1);
    for (const seq::Firing& f : baked->events.baked) {
        if (piece.events[f.eventIndex].id == "on-drop") {
            CHECK(f.timeSeconds == Approx(4.0));
        }
        if (piece.events[f.eventIndex].id == "on-cue") {
            CHECK(f.timeSeconds == Approx(7.25));
        }
    }

    // Negative control for the thing that makes this useful: a beat event in a sequence whose
    // analysis has not been run fires nothing, and says so, rather than silently doing nothing.
    seq::Sequence unanalyzed;
    unanalyzed.shots.push_back(keyedShot("a", 0.0, 16.0));
    unanalyzed.events.push_back(
        event("every-beat", seq::Trigger{.kind = seq::TriggerKind::Beat}, setParam("a", 1.0f)));
    auto quiet = unanalyzed.bake(sink);
    REQUIRE(quiet);
    CHECK(quiet->events.baked.empty());
    CHECK(std::any_of(quiet->warnings.begin(), quiet->warnings.end(), [](const std::string& w) {
        return w.find("no beat times") != std::string::npos;
    }));
}

TEST_CASE("A trigger window, a stride and a repeat limit all bound the firing list",
          "[seq][events]") {
    seq::Sequence piece;
    piece.shots.push_back(keyedShot("a", 0.0, 16.0));
    std::vector<double> beats;
    for (int i = 0; i < 16; ++i) {
        beats.push_back(0.5 * i);
    }
    piece.setBeatMarkers(beats);

    piece.events.push_back(event("windowed",
                                 seq::Trigger{.kind = seq::TriggerKind::Beat,
                                              .fromSeconds = 2.0,
                                              .toSeconds = 4.0},
                                 setParam("a", 1.0f)));
    piece.events.push_back(event("strided",
                                 seq::Trigger{.kind = seq::TriggerKind::Beat, .every = 4, .index = 1},
                                 setParam("b", 1.0f)));
    piece.events.push_back(event("limited",
                                 seq::Trigger{.kind = seq::TriggerKind::Beat, .repeat = 3},
                                 setParam("c", 1.0f)));
    piece.events.push_back(event("delayed",
                                 seq::Trigger{.kind = seq::TriggerKind::Time,
                                              .timeSeconds = 5.0,
                                              .delaySeconds = 0.25},
                                 setParam("d", 1.0f)));
    piece.events.push_back(event("disabled", seq::Trigger{.kind = seq::TriggerKind::Beat},
                                 setParam("e", 1.0f)));
    piece.events.back().enabled = false;

    seq::NullLayerSink sink;
    auto baked = piece.bake(sink);
    REQUIRE(baked);
    const auto firings = [&](const std::string& id) {
        std::vector<double> times;
        for (const seq::Firing& f : baked->events.baked) {
            if (piece.events[f.eventIndex].id == id) {
                times.push_back(f.timeSeconds);
            }
        }
        return times;
    };
    CHECK(firings("windowed") == std::vector<double>{2.0, 2.5, 3.0, 3.5, 4.0});
    CHECK(firings("strided") == std::vector<double>{0.5, 2.5, 4.5, 6.5});
    CHECK(firings("limited") == std::vector<double>{0.0, 0.5, 1.0});
    CHECK(firings("delayed") == std::vector<double>{5.25});
    CHECK(firings("disabled").empty());
}

TEST_CASE("An event past the end of the shots is not silently dropped", "[seq][events]") {
    // The window is an optional filter, not a default clip. Defaulting the upper bound to the
    // piece's derived duration would have made an event an author deliberately placed after the
    // last shot vanish without a word -- the exact silent no-op ADR-075 exists to record.
    seq::Sequence piece;
    piece.shots.push_back(keyedShot("a", 0.0, 6.0));
    piece.events.push_back(event("late",
                                 seq::Trigger{.kind = seq::TriggerKind::Time, .timeSeconds = 42.0},
                                 setParam("scene/fogDensity", 1.0f, params::TrackMode::Add)));
    seq::NullLayerSink sink;
    auto baked = piece.bake(sink);
    REQUIRE(baked);
    REQUIRE(baked->events.baked.size() == 1);
    CHECK(baked->events.baked[0].timeSeconds == Approx(42.0));
    // ...and the piece is now as long as its last event, so an editor's ruler and a render both
    // reach it.
    CHECK(piece.duration() == Approx(42.0));
    // The negative control: an author who *does* state a window still gets one.
    seq::Sequence bounded = piece;
    bounded.events[0].when.toSeconds = 10.0;
    auto clipped = bounded.bake(sink);
    REQUIRE(clipped);
    CHECK(clipped->events.baked.empty());
}

// ---- what "a change" means ------------------------------------------------------------------------

TEST_CASE("A replace event says out loud that its value holds backwards", "[seq][events]") {
    seq::Sequence piece;
    piece.shots.push_back(keyedShot("a", 0.0, 20.0));
    piece.events.push_back(event("fog",
                                 seq::Trigger{.kind = seq::TriggerKind::Time, .timeSeconds = 12.0},
                                 setParam("scene/fogDensity", 3.0f, params::TrackMode::Replace)));
    seq::NullLayerSink sink;
    auto baked = piece.bake(sink);
    REQUIRE(baked);
    CHECK(std::any_of(baked->warnings.begin(), baked->warnings.end(), [](const std::string& w) {
        return w.find("holds backwards") != std::string::npos;
    }));

    // The negative control: in add mode the identity is known, so the same event is a real change
    // and no warning is needed. If the warning were unconditional this check would fail.
    seq::Sequence relative;
    relative.shots.push_back(keyedShot("a", 0.0, 20.0));
    relative.events.push_back(event("fog",
                                    seq::Trigger{.kind = seq::TriggerKind::Time, .timeSeconds = 12.0},
                                    setParam("scene/fogDensity", 3.0f, params::TrackMode::Add)));
    auto additive = relative.bake(sink);
    REQUIRE(additive);
    CHECK(std::none_of(additive->warnings.begin(), additive->warnings.end(),
                       [](const std::string& w) {
                           return w.find("holds backwards") != std::string::npos;
                       }));
    const params::Timeline timeline = timelineOf(*additive);
    CHECK(valueAt(timeline, "scene/fogDensity", 0.0, params::TrackMode::Add) == Approx(0.0f));
    CHECK(valueAt(timeline, "scene/fogDensity", 11.99, params::TrackMode::Add) == Approx(0.0f));
    CHECK(valueAt(timeline, "scene/fogDensity", 12.0, params::TrackMode::Add) == Approx(3.0f));
}

TEST_CASE("An instantaneous set does not glide into itself from the previous key",
          "[seq][events]") {
    // Two events on one path. Without the millisecond hold before the second one, the segment
    // between them would interpolate and a "set at 8 s" would have been sliding since 4 s -- the
    // same defect the camera bake's one-millisecond cut exists to stop.
    seq::Sequence piece;
    piece.shots.push_back(keyedShot("a", 0.0, 20.0));
    piece.events.push_back(event("first",
                                 seq::Trigger{.kind = seq::TriggerKind::Time, .timeSeconds = 4.0},
                                 setParam("scene/fogDensity", 1.0f, params::TrackMode::Add)));
    piece.events.push_back(event("second",
                                 seq::Trigger{.kind = seq::TriggerKind::Time, .timeSeconds = 8.0},
                                 setParam("scene/fogDensity", 9.0f, params::TrackMode::Add)));
    seq::NullLayerSink sink;
    auto baked = piece.bake(sink);
    REQUIRE(baked);
    const params::Timeline timeline = timelineOf(*baked);
    CHECK(valueAt(timeline, "scene/fogDensity", 6.0, params::TrackMode::Add) == Approx(1.0f));
    CHECK(valueAt(timeline, "scene/fogDensity", 7.99, params::TrackMode::Add) == Approx(1.0f));
    CHECK(valueAt(timeline, "scene/fogDensity", 8.0, params::TrackMode::Add) == Approx(9.0f));
}

TEST_CASE("A ramp and a hold are keys, not timers", "[seq][events]") {
    seq::Sequence piece;
    piece.shots.push_back(keyedShot("a", 0.0, 30.0));
    seq::EventAction flash = setParam("procedural/lamp/parts/1/emissiveGain", 4.0f,
                                      params::TrackMode::Multiply);
    flash.seconds = 0.5;     // ramp up over half a second
    flash.holdSeconds = 1.0; // hold, then come back
    flash.interp = params::KeyInterp::Linear;
    piece.events.push_back(
        event("flash", seq::Trigger{.kind = seq::TriggerKind::Time, .timeSeconds = 10.0}, flash));

    seq::NullLayerSink sink;
    auto baked = piece.bake(sink);
    REQUIRE(baked);
    const params::Timeline timeline = timelineOf(*baked);
    const std::string path = "procedural/lamp/parts/1/emissiveGain";
    // Multiply's identity is 1, so before the impulse the property is exactly what the author set.
    CHECK(valueAt(timeline, path, 0.0, params::TrackMode::Multiply) == Approx(1.0f));
    CHECK(valueAt(timeline, path, 10.0, params::TrackMode::Multiply) == Approx(1.0f));
    CHECK(valueAt(timeline, path, 10.25, params::TrackMode::Multiply) == Approx(2.5f));
    CHECK(valueAt(timeline, path, 10.5, params::TrackMode::Multiply) == Approx(4.0f));
    CHECK(valueAt(timeline, path, 11.5, params::TrackMode::Multiply) == Approx(4.0f));
    CHECK(valueAt(timeline, path, 12.0, params::TrackMode::Multiply) == Approx(1.0f));
    // ...and it stays back. An impulse that leaks is an impulse a scrub can catch halfway.
    CHECK(valueAt(timeline, path, 90.0, params::TrackMode::Multiply) == Approx(1.0f));
}

// ---- camera shake (section 14) ---------------------------------------------------------------------

TEST_CASE("Camera shake is a pure function of the playhead", "[seq][events][camera]") {
    scene::CameraShake shake;
    shake.amplitude = 0.3f;
    shake.frequency = 9.0f;
    shake.decaySeconds = 1.2f;
    shake.startSeconds = 10.0;

    // Before the impulse: nothing, however the playhead got here.
    CHECK(scene::cameraShakeEnvelope(shake, 9.99) == Approx(0.0f));
    CHECK(glm::length(scene::cameraShakeOffset(shake, 5.0)) == Approx(0.0f));
    // At the impulse: full.
    CHECK(scene::cameraShakeEnvelope(shake, 10.0) == Approx(1.0f));
    // After the decay: exactly zero, not an exponential tail nobody can predicate on.
    CHECK(scene::cameraShakeEnvelope(shake, 11.2) == Approx(0.0f).margin(1e-6));
    CHECK(scene::cameraShakeEnvelope(shake, 40.0) == Approx(0.0f).margin(1e-6));
    // ...and it is a function, so two evaluations of the same second agree whatever happened
    // between them. This is the property a timer cannot have.
    const glm::vec3 a = scene::cameraShakeOffset(shake, 10.4);
    (void)scene::cameraShakeOffset(shake, 10.9);
    (void)scene::cameraShakeOffset(shake, 3.0);
    const glm::vec3 b = scene::cameraShakeOffset(shake, 10.4);
    CHECK(a.x == Approx(b.x));
    CHECK(a.y == Approx(b.y));
    CHECK(a.z == Approx(b.z));
    CHECK(glm::length(a) > 1e-4f);
    CHECK(glm::length(a) <= 0.3f * 1.7321f + 1e-4f);

    // Camera space, not world space: the offset moves the eye along its own axes and the aim
    // travels with it, so a shake on a camera looking down +X is not the same world vector as one
    // looking down +Z.
    glm::vec3 position(0.0f, 2.0f, 0.0f);
    glm::vec3 target(0.0f, 2.0f, -10.0f);
    scene::applyCameraShake(shake, 10.4, position, target);
    const glm::vec3 alongZ = position;
    glm::vec3 position2(0.0f, 2.0f, 0.0f);
    glm::vec3 target2(-10.0f, 2.0f, 0.0f);
    scene::applyCameraShake(shake, 10.4, position2, target2);
    CHECK(glm::length(alongZ - position2) > 1e-4f);

    // A shake with no amplitude and no rotation does nothing at all, so every scene that never
    // asks for one pays nothing and renders identically to before this existed.
    scene::CameraShake still;
    glm::vec3 p(1.0f, 2.0f, 3.0f);
    glm::vec3 t(0.0f, 0.0f, 0.0f);
    scene::applyCameraShake(still, 12.0, p, t);
    CHECK(p == glm::vec3(1.0f, 2.0f, 3.0f));
    CHECK(t == glm::vec3(0.0f, 0.0f, 0.0f));
}

TEST_CASE("A shake event bakes to keys and the piece does not open shaking", "[seq][events]") {
    seq::Sequence piece;
    piece.shots.push_back(keyedShot("a", 0.0, 30.0));
    seq::EventAction shake;
    shake.kind = seq::EventActionKind::CameraShake;
    shake.amount = glm::vec4(0.25f, 12.0f, 0.5f, 0.0f);
    shake.seconds = 0.8; // decay
    piece.events.push_back(
        event("hit", seq::Trigger{.kind = seq::TriggerKind::Time, .timeSeconds = 12.0}, shake));

    seq::NullLayerSink sink;
    auto baked = piece.bake(sink);
    REQUIRE(baked);
    const params::Timeline timeline = timelineOf(*baked);
    // The negative control this test exists for: a track holds its first key's value backwards
    // forever, so without the explicit zero at t = 0 the piece would open at full amplitude.
    CHECK(valueAt(timeline, "camera/shake/amplitude", 0.0) == Approx(0.0f));
    CHECK(valueAt(timeline, "camera/shake/amplitude", 11.9) == Approx(0.0f));
    CHECK(valueAt(timeline, "camera/shake/amplitude", 12.0) == Approx(0.25f));
    CHECK(valueAt(timeline, "camera/shake/frequency", 12.0) == Approx(12.0f));
    CHECK(valueAt(timeline, "camera/shake/rotation", 12.0) == Approx(0.5f));
    CHECK(valueAt(timeline, "camera/shake/decay", 12.0) == Approx(0.8f));
    // The impulse's origin is itself a keyed parameter, which is what lets the decay be
    // `now - start` instead of an accumulated timer.
    CHECK(valueAt(timeline, "camera/shake/start", 12.0) == Approx(12.0f));
    CHECK(valueAt(timeline, "camera/shake/start", 5.0) == Approx(0.0f));

    // Read back the way the composition does, at three playhead positions reached out of order.
    const auto shakeAt = [&](double t) {
        scene::CameraShake s;
        s.amplitude = valueAt(timeline, "camera/shake/amplitude", t);
        s.frequency = valueAt(timeline, "camera/shake/frequency", t);
        s.decaySeconds = valueAt(timeline, "camera/shake/decay", t);
        s.startSeconds = valueAt(timeline, "camera/shake/start", t);
        return scene::cameraShakeEnvelope(s, t);
    };
    CHECK(shakeAt(12.0) == Approx(1.0f));
    CHECK(shakeAt(40.0) == Approx(0.0f));
    CHECK(shakeAt(12.4) == Approx(shakeAt(12.4)));
    CHECK(shakeAt(12.4) > 0.0f);
    CHECK(shakeAt(3.0) == Approx(0.0f));
}

// ---- transitions (section 35) -----------------------------------------------------------------------

TEST_CASE("A match cut lands the incoming subject at the outgoing subject's size",
          "[seq][events][transitions]") {
    const auto moveShot = [](std::string name, double start, float radius, float distance,
                             float focal) {
        seq::Shot shot;
        shot.name = std::move(name);
        shot.startSeconds = start;
        shot.durationSeconds = 6.0;
        shot.camera.kind = seq::CameraKind::Move;
        shot.camera.samples = 2;
        shot.camera.move.kind = app::ShotKind::Establish;
        shot.camera.move.subject.radius = radius;
        shot.camera.move.subject.position = glm::vec3(0.0f);
        shot.camera.move.startDistance = distance;
        shot.camera.move.endDistance = distance;
        shot.camera.move.composition.focalLength = focal;
        shot.camera.move.composition.framing = glm::vec2(-0.3f, 0.2f);
        return shot;
    };
    seq::Sequence piece;
    piece.shots.push_back(moveShot("out", 0.0, 12.0f, 5.0f, 35.0f));
    // A subject a tenth the size, through a different lens. Authored at a distance that would read
    // completely differently; the match cut is what makes the two frames rhyme.
    seq::Shot incoming = moveShot("in", 6.0, 1.2f, 20.0f, 80.0f);
    incoming.in = seq::Transition{seq::TransitionKind::MatchCut, 0.0};
    piece.shots.push_back(std::move(incoming));

    seq::NullLayerSink sink;
    auto baked = piece.bake(sink);
    REQUIRE(baked);

    const float outgoing = piece.shots[0].camera.move.subjectCoverageAt(1.0f);
    // The bake does not mutate the sequence, so read the camera keys it actually wrote.
    const params::Timeline matched = timelineOf(*baked);
    const params::Track* position = trackFor(matched, "camera/position");
    REQUIRE(position != nullptr);
    const params::KeyValue at6 = position->evaluate(6.0);
    const glm::vec3 eye(at6[0], at6[1], at6[2]);
    const float distance = glm::length(eye - piece.shots[1].camera.move.subject.position);
    const float halfFrame = std::atan(12.0f / 80.0f);
    const float landed = std::atan(1.2f / distance) / halfFrame;
    CHECK(landed == Approx(outgoing).epsilon(0.02));

    // The negative control: the same cut without the match kind is left alone, so this test is
    // measuring the match rather than an accident of the two shots' numbers.
    seq::Sequence plain = piece;
    plain.shots[1].in = seq::Transition{seq::TransitionKind::Cut, 0.0};
    auto hardCut = plain.bake(sink);
    REQUIRE(hardCut);
    const params::Timeline plainTimeline = timelineOf(*hardCut);
    const params::Track* plainPosition = trackFor(plainTimeline, "camera/position");
    REQUIRE(plainPosition != nullptr);
    const params::KeyValue plainAt6 = plainPosition->evaluate(6.0);
    const float plainDistance =
        glm::length(glm::vec3(plainAt6[0], plainAt6[1], plainAt6[2]) -
                    piece.shots[1].camera.move.subject.position);
    CHECK(plainDistance > distance + 1.0f);

    // A match cut is a hard cut. It must not pull a brightness track into a sequence with no dips.
    CHECK_FALSE(hasTrack(timelineOf(*baked), "scene/brightness"));
    // ...and a real dip still produces one, so the guard above is not simply switching it off.
    seq::Sequence dipped = piece;
    dipped.shots[1].in = seq::Transition{seq::TransitionKind::FadeIn, 0.5};
    auto withDip = dipped.bake(sink);
    REQUIRE(withDip);
    CHECK(hasTrack(timelineOf(*withDip), "scene/brightness"));
}

TEST_CASE("A scene transition event cuts the slots and can dip through the cut",
          "[seq][events][transitions]") {
    seq::Sequence piece;
    piece.scenes.push_back(seq::SceneSlot{.id = "day", .node = "city-day"});
    piece.scenes.push_back(seq::SceneSlot{.id = "night", .node = "city-night"});
    seq::Shot only = keyedShot("a", 0.0, 20.0);
    only.scene = "day";
    piece.shots.push_back(std::move(only));

    seq::EventAction cut;
    cut.kind = seq::EventActionKind::SceneTransition;
    cut.target = "night";
    cut.value = "fadeIn";
    cut.seconds = 0.75;
    piece.events.push_back(
        event("go-dark", seq::Trigger{.kind = seq::TriggerKind::Time, .timeSeconds = 9.0}, cut));

    seq::NullLayerSink sink;
    auto baked = piece.bake(sink);
    REQUIRE(baked);
    const params::Timeline timeline = timelineOf(*baked);
    CHECK(valueAt(timeline, "nodes/city-day/visible", 8.9) == Approx(1.0f));
    CHECK(valueAt(timeline, "nodes/city-day/visible", 9.0) == Approx(0.0f));
    CHECK(valueAt(timeline, "nodes/city-night/visible", 8.9) == Approx(0.0f));
    CHECK(valueAt(timeline, "nodes/city-night/visible", 9.0) == Approx(1.0f));
    CHECK(valueAt(timeline, "scene/brightness", 9.0) == Approx(0.0f));
    CHECK(valueAt(timeline, "scene/brightness", 9.75) == Approx(1.0f));

    // A slot that does not exist is a warning, not a silently ignored event.
    seq::Sequence wrong = piece;
    wrong.events[0].what.target = "dawn";
    auto missed = wrong.bake(sink);
    REQUIRE(missed);
    CHECK(std::any_of(missed->warnings.begin(), missed->warnings.end(), [](const std::string& w) {
        return w.find("which does not exist") != std::string::npos;
    }));
}

// ---- shot-driven quality (section 34) ---------------------------------------------------------------

TEST_CASE("A spotlight raises its subject's level-of-detail floor for the length of the shot",
          "[seq][events][lod]") {
    seq::Sequence piece;
    seq::Shot hero;
    hero.name = "hero";
    hero.startSeconds = 10.0;
    hero.durationSeconds = 8.0;
    hero.camera.kind = seq::CameraKind::Move;
    hero.camera.samples = 2;
    hero.camera.move.kind = app::ShotKind::Approach;
    hero.camera.move.subject.name = "elder";
    hero.camera.move.subject.radius = 1.4f;
    hero.camera.move.spotlight.active = true;
    hero.camera.move.spotlight.emphasis = 1.0f;
    piece.shots.push_back(std::move(hero));

    seq::NullLayerSink sink;
    auto baked = piece.bake(sink);
    REQUIRE(baked);
    const params::Timeline timeline = timelineOf(*baked);
    for (const char* suffix : {"lod/minScreenRadius", "lod/distance1", "lod/distance2",
                               "lod/distance3"}) {
        const std::string path = std::string("procedural/elder/") + suffix;
        INFO(path);
        // Multiply, so the bake never has to know -- or restore -- the values the author chose.
        CHECK(valueAt(timeline, path, 0.0, params::TrackMode::Multiply) == Approx(1.0f));
        CHECK(valueAt(timeline, path, 9.9, params::TrackMode::Multiply) == Approx(1.0f));
        CHECK(valueAt(timeline, path, 14.0, params::TrackMode::Multiply) == Approx(0.0f));
        // ...and it hands the subject back afterwards. A spotlight that never lets go would make
        // every hero of the piece expensive for the whole of it.
        CHECK(valueAt(timeline, path, 18.0, params::TrackMode::Multiply) == Approx(1.0f));
        CHECK(valueAt(timeline, path, 60.0, params::TrackMode::Multiply) == Approx(1.0f));
    }

    // Emphasis scales it, so a shot that spotlights weakly asks for less.
    seq::Sequence half = piece;
    half.shots[0].camera.move.spotlight.emphasis = 0.5f;
    auto softer = half.bake(sink);
    REQUIRE(softer);
    CHECK(valueAt(timelineOf(*softer), "procedural/elder/lod/distance1", 14.0,
                  params::TrackMode::Multiply) == Approx(0.5f));

    // The negative controls. A shot with no spotlight touches nothing...
    seq::Sequence plain = piece;
    plain.shots[0].camera.move.spotlight.active = false;
    auto untouched = plain.bake(sink);
    REQUIRE(untouched);
    CHECK_FALSE(hasTrack(timelineOf(*untouched), "procedural/elder/lod/distance1"));
    // ...and the option turns it off for a project that would rather manage its own budget.
    seq::BakeOptions off;
    off.spotlightQuality = false;
    auto disabled = piece.bake(sink, off);
    REQUIRE(disabled);
    CHECK_FALSE(hasTrack(timelineOf(*disabled), "procedural/elder/lod/distance1"));
}

// ---- clips ----------------------------------------------------------------------------------------

TEST_CASE("An event that plays a clip becomes a clip cue, not a track", "[seq][events]") {
    seq::Sequence piece;
    piece.shots.push_back(keyedShot("a", 0.0, 20.0));
    seq::Actor elder;
    elder.id = "elder";
    elder.node = "walker";
    elder.keys.push_back(seq::ActorKey{.timeSeconds = 0.0, .position = glm::vec3(0.0f)});
    elder.clips.push_back(seq::ClipCue{.timeSeconds = 0.0, .clip = "Idle"});
    piece.actors.push_back(std::move(elder));
    piece.markers.push_back(seq::Marker{6.0, "wake", seq::MarkerKind::Cue});

    seq::EventAction play;
    play.kind = seq::EventActionKind::PlayClip;
    play.target = "elder";
    play.value = "Dance";
    play.amount = glm::vec4(1.25f, 0.0f, 0.0f, 0.0f);
    play.seconds = 0.3;
    piece.events.push_back(
        event("dance", seq::Trigger{.kind = seq::TriggerKind::Cue, .name = "wake"}, play));

    seq::NullLayerSink sink;
    auto baked = piece.bake(sink);
    REQUIRE(baked);
    REQUIRE(baked->events.clips.size() == 1);
    CHECK(baked->events.clips[0].actor == "elder");
    CHECK(baked->events.clips[0].clip == "Dance");
    CHECK(baked->events.clips[0].timeSeconds == Approx(6.0));
    CHECK(baked->events.clips[0].speed == Approx(1.25f));

    // The whole reason a clip is not baked (ADR-089): the pose needs the second the state was
    // entered, and that survives a jump to any playhead position.
    const auto cues = piece.animationAt(9.0, baked->events.clips);
    REQUIRE(cues.size() == 1);
    CHECK(cues[0].node == "walker");
    CHECK(cues[0].clip == "Dance");
    CHECK(cues[0].startSeconds == Approx(6.0));
    // Before the event the authored cue is still the actor's state...
    const auto before = piece.animationAt(3.0, baked->events.clips);
    REQUIRE(before.size() == 1);
    CHECK(before[0].clip == "Idle");
    // ...and jumping back to it produces the same answer, because neither list moved.
    CHECK(piece.animationAt(3.0, baked->events.clips)[0].clip == "Idle");

    // An authored cue *after* the event wins, because a scheduled clip and an authored one are the
    // same object and the later of the two is simply what the actor is doing.
    seq::Sequence later = piece;
    later.actors[0].clips.push_back(seq::ClipCue{.timeSeconds = 8.0, .clip = "Walk"});
    auto again = later.bake(sink);
    REQUIRE(again);
    CHECK(later.animationAt(9.0, again->events.clips)[0].clip == "Walk");

    // A clip aimed at an actor that does not exist is a warning rather than a silent no-op.
    seq::Sequence nobody = piece;
    nobody.events[0].what.target = "ghost";
    auto missing = nobody.bake(sink);
    REQUIRE(missing);
    CHECK(std::any_of(missing->warnings.begin(), missing->warnings.end(), [](const std::string& w) {
        return w.find("which the sequence does not have") != std::string::npos;
    }));
}

TEST_CASE("A clip-end trigger is scheduled when the sequence bounds it and live when it does not",
          "[seq][events]") {
    seq::Sequence piece;
    piece.shots.push_back(keyedShot("a", 0.0, 20.0));
    seq::Actor elder;
    elder.id = "elder";
    elder.keys.push_back(seq::ActorKey{.timeSeconds = 0.0, .position = glm::vec3(0.0f)});
    elder.clips.push_back(seq::ClipCue{.timeSeconds = 2.0, .clip = "Wave"});
    elder.clips.push_back(seq::ClipCue{.timeSeconds = 5.0, .clip = "Walk"});
    piece.actors.push_back(std::move(elder));

    piece.events.push_back(event("after-wave",
                                 seq::Trigger{.kind = seq::TriggerKind::ClipEnd,
                                              .name = "Wave",
                                              .subject = "elder"},
                                 setParam("a", 1.0f, params::TrackMode::Add)));
    seq::NullLayerSink sink;
    auto baked = piece.bake(sink);
    REQUIRE(baked);
    REQUIRE(baked->events.baked.size() == 1);
    // The sequence states when Wave stops being the state: the next cue.
    CHECK(baked->events.baked[0].timeSeconds == Approx(5.0));

    // The last cue has no successor, so nothing in the piece knows when it ends -- only the asset
    // does, and an asset's clip length is not a fact about the piece. The event drops to live
    // rather than being guessed at.
    seq::Sequence open;
    open.shots.push_back(keyedShot("a", 0.0, 20.0));
    seq::Actor lone;
    lone.id = "elder";
    lone.keys.push_back(seq::ActorKey{.timeSeconds = 0.0, .position = glm::vec3(0.0f)});
    lone.clips.push_back(seq::ClipCue{.timeSeconds = 2.0, .clip = "Wave"});
    open.actors.push_back(std::move(lone));
    open.events.push_back(event("after-wave",
                                seq::Trigger{.kind = seq::TriggerKind::ClipEnd,
                                             .name = "Wave",
                                             .subject = "elder"},
                                setParam("a", 1.0f, params::TrackMode::Add)));
    auto unbounded = open.bake(sink);
    REQUIRE(unbounded);
    CHECK(unbounded->events.baked.empty());
    CHECK(unbounded->events.live.size() == 1);
    CHECK(std::any_of(unbounded->warnings.begin(), unbounded->warnings.end(),
                      [](const std::string& w) {
                          return w.find("live rather than scheduled") != std::string::npos;
                      }));
}

// ---- the live and scheduled tiers -------------------------------------------------------------------

namespace {

// A pair of events the dispatcher can be driven with: one live, one scheduled.
seq::Sequence dispatchPiece() {
    seq::Sequence piece;
    piece.shots.push_back(keyedShot("a", 0.0, 60.0));

    seq::EventAction enter;
    enter.kind = seq::EventActionKind::EntityAction;
    enter.target = "elder";
    enter.value = "react";
    piece.events.push_back(event("on-porch",
                                 seq::Trigger{.kind = seq::TriggerKind::VolumeEnter,
                                              .name = "porch"},
                                 enter));

    seq::EventAction leave = enter;
    leave.value = "resume";
    piece.events.push_back(event("off-porch",
                                 seq::Trigger{.kind = seq::TriggerKind::VolumeExit,
                                              .name = "porch",
                                              .subject = "elder"},
                                 leave));
    return piece;
}

} // namespace

TEST_CASE("A live event is dispatched by key, in order, and never polled", "[seq][events]") {
    seq::Sequence piece = dispatchPiece();
    seq::NullLayerSink sink;
    auto baked = piece.bake(sink);
    REQUIRE(baked);
    REQUIRE(baked->events.live.size() == 2);

    seq::EventDispatcher dispatcher;
    dispatcher.setEvents(piece.events, baked->events);
    CHECK(dispatcher.liveEventCount() == 2);

    // A signal naming a volume nobody listens to costs a failed lookup and changes nothing.
    dispatcher.post(seq::TriggerSignal{seq::TriggerKind::VolumeEnter, "garden", "elder", 1.0});
    CHECK(dispatcher.drain().empty());

    // The subject filter: the exit event names the elder, so a cat leaving the porch is not it.
    dispatcher.post(seq::TriggerSignal{seq::TriggerKind::VolumeExit, "porch", "cat", 2.0});
    CHECK(dispatcher.drain().empty());
    // ...but the enter event names no subject, so it listens for anyone.
    dispatcher.post(seq::TriggerSignal{seq::TriggerKind::VolumeEnter, "porch", "cat", 3.0});
    auto fired = dispatcher.drain();
    REQUIRE(fired.size() == 1);
    CHECK(fired[0].eventIndex == 0);
    CHECK(fired[0].subject == "cat");
    CHECK_FALSE(fired[0].restored);

    // Two signals in one frame come out in time order and are each delivered once.
    dispatcher.post(seq::TriggerSignal{seq::TriggerKind::VolumeExit, "porch", "elder", 6.0});
    dispatcher.post(seq::TriggerSignal{seq::TriggerKind::VolumeEnter, "porch", "elder", 5.0});
    auto both = dispatcher.drain();
    REQUIRE(both.size() == 2);
    CHECK(both[0].timeSeconds == Approx(5.0));
    CHECK(both[1].timeSeconds == Approx(6.0));
    CHECK(dispatcher.drain().empty()); // a drain drains
}

TEST_CASE("A delayed live event is held until its time, and a repeat limit holds", "[seq][events]") {
    seq::Sequence piece = dispatchPiece();
    piece.events[0].when.delaySeconds = 1.5;
    piece.events[0].when.repeat = 2;
    seq::NullLayerSink sink;
    auto baked = piece.bake(sink);
    REQUIRE(baked);
    seq::EventDispatcher dispatcher;
    dispatcher.setEvents(piece.events, baked->events);

    dispatcher.post(seq::TriggerSignal{seq::TriggerKind::VolumeEnter, "porch", "elder", 10.0});
    CHECK(dispatcher.drain(11.0).empty()); // not yet: the delay is a second and a half
    auto late = dispatcher.drain(11.5);
    REQUIRE(late.size() == 1);
    CHECK(late[0].timeSeconds == Approx(11.5));

    dispatcher.post(seq::TriggerSignal{seq::TriggerKind::VolumeEnter, "porch", "elder", 20.0});
    CHECK(dispatcher.drain(30.0).size() == 1);
    dispatcher.post(seq::TriggerSignal{seq::TriggerKind::VolumeEnter, "porch", "elder", 30.0});
    CHECK(dispatcher.drain(40.0).empty()); // the limit is two
    CHECK(dispatcher.firedCount(0) == 2);
}

TEST_CASE("A seek does not replay a scheduled tier; it restores the standing intent",
          "[seq][events]") {
    seq::Sequence piece;
    piece.shots.push_back(keyedShot("a", 0.0, 120.0));
    seq::EventAction act;
    act.kind = seq::EventActionKind::EntityAction;
    act.target = "elder";
    for (int i = 0; i < 10; ++i) {
        act.value = "step" + std::to_string(i);
        piece.events.push_back(
            event("s" + std::to_string(i),
                  seq::Trigger{.kind = seq::TriggerKind::Time, .timeSeconds = 5.0 * (i + 1)}, act));
    }
    // ...and one event on a second entity, so "per target" is actually being tested.
    seq::EventAction other = act;
    other.target = "cat";
    other.value = "sit";
    piece.events.push_back(
        event("cat", seq::Trigger{.kind = seq::TriggerKind::Time, .timeSeconds = 7.0}, other));

    seq::NullLayerSink sink;
    auto baked = piece.bake(sink);
    REQUIRE(baked);
    REQUIRE(baked->events.dispatches.size() == 11);

    SECTION("forward play delivers each exactly once") {
        seq::EventDispatcher dispatcher;
        dispatcher.setEvents(piece.events, baked->events);
        int delivered = 0;
        int restored = 0;
        for (int frame = 0; frame <= 6000; ++frame) {
            const double now = frame / 100.0; // 100 fps to 60 s
            dispatcher.advanceTo(now);
            for (const seq::FiredEvent& f : dispatcher.drain(now)) {
                ++delivered;
                restored += f.restored ? 1 : 0;
            }
        }
        // Every dispatch up to 50 s for the elder, plus the cat's one.
        CHECK(delivered == 11);
        CHECK(restored == 0);
        // The negative control for double-firing: running the same frames again from where it
        // stopped delivers nothing, because the playhead does not move backwards.
        for (int frame = 6000; frame <= 6100; ++frame) {
            dispatcher.advanceTo(frame / 100.0);
        }
        CHECK(dispatcher.drain(61.0).empty());
    }

    SECTION("a jump restores one standing intent per target and drops the rest") {
        seq::EventDispatcher dispatcher;
        dispatcher.setEvents(piece.events, baked->events);
        dispatcher.advanceTo(0.0);
        (void)dispatcher.drain(0.0);
        // Straight to 48 s. Ten of the elder's actions and one of the cat's lie behind us; an
        // entity cannot perform nine of them in one frame and the ones it "did" would be in the
        // wrong order relative to a world that never ran.
        dispatcher.advanceTo(48.0);
        auto fired = dispatcher.drain(48.0);
        REQUIRE(fired.size() == 2);
        for (const seq::FiredEvent& f : fired) {
            CHECK(f.restored);
            CHECK(f.timeSeconds == Approx(48.0));
        }
        CHECK(piece.events[fired[0].eventIndex].what.value == "step8"); // 45 s, the latest
        CHECK(piece.events[fired[1].eventIndex].what.target == "cat");

        // Scrubbing backwards is the same statement: the standing intent at 12 s, not a rewind.
        dispatcher.advanceTo(12.0);
        auto back = dispatcher.drain(12.0);
        REQUIRE(back.size() == 2);
        CHECK(piece.events[back[0].eventIndex].what.value == "step1"); // 10 s
        CHECK(back[0].restored);

        // And playing to 60 s leaves the same standing intents a jump to 60 s does. That is the
        // guarantee ADR-098 offers for this tier, and the one it explicitly does not extend.
        seq::EventDispatcher played;
        played.setEvents(piece.events, baked->events);
        std::string playedLast;
        for (int frame = 0; frame <= 6000; ++frame) {
            const double now = frame / 100.0;
            played.advanceTo(now);
            for (const seq::FiredEvent& f : played.drain(now)) {
                if (piece.events[f.eventIndex].what.target == "elder") {
                    playedLast = piece.events[f.eventIndex].what.value;
                }
            }
        }
        seq::EventDispatcher jumped;
        jumped.setEvents(piece.events, baked->events);
        jumped.advanceTo(60.0);
        std::string jumpedLast;
        for (const seq::FiredEvent& f : jumped.drain(60.0)) {
            if (piece.events[f.eventIndex].what.target == "elder") {
                jumpedLast = piece.events[f.eventIndex].what.value;
            }
        }
        CHECK(playedLast == "step9");
        CHECK(jumpedLast == playedLast);
    }

    SECTION("a reset throws away pending live firings but rebases the scheduled tier") {
        seq::EventDispatcher dispatcher;
        dispatcher.setEvents(piece.events, baked->events);
        dispatcher.advanceTo(0.0);
        (void)dispatcher.drain(0.0);
        dispatcher.advanceTo(5.0);
        CHECK(dispatcher.pendingCount() == 1);
        dispatcher.reset(30.0);
        CHECK(dispatcher.pendingCount() == 0);
        // The next advance is a seek from 30 s, not a replay from 5 s.
        dispatcher.advanceTo(30.0);
        auto fired = dispatcher.drain(30.0);
        REQUIRE(fired.size() == 2);
        CHECK(fired[0].restored);
    }
}

// ---- the fourth wall (brief section 3) ---------------------------------------------------------------

TEST_CASE("A scripted cursor is an overlay plus events, with nothing cursor-shaped in the engine",
          "[seq][events][overlay]") {
    // The storyboard breaks the fourth wall: a pointer appears, crosses the frame and clicks a
    // character. Nothing below is a cursor feature -- it is a shape cue, two moves on its position
    // and a Notify. The word "cursor" appears in this test and nowhere in `src/`.
    seq::Sequence piece;
    piece.shots.push_back(keyedShot("a", 0.0, 20.0));

    seq::OverlayCue pointer;
    pointer.id = "pointer";
    pointer.kind = seq::OverlayKind::Shape;
    pointer.content = "ellipse";
    pointer.startSeconds = 4.0;
    pointer.endSeconds = 12.0;
    pointer.anchor = glm::vec2(0.1f, 0.9f);
    pointer.order = 20;
    piece.overlays.push_back(std::move(pointer));

    const auto move = [](const char* property, float to, double seconds) {
        seq::EventAction a;
        a.kind = seq::EventActionKind::Overlay;
        a.target = "pointer";
        a.value = property;
        a.amount = glm::vec4(to, 0.0f, 0.0f, 0.0f);
        a.seconds = seconds;
        a.interp = params::KeyInterp::EaseInOut;
        return a;
    };
    piece.events.push_back(event("slide-x",
                                 seq::Trigger{.kind = seq::TriggerKind::Time, .timeSeconds = 5.0},
                                 move(std::string(seq::overlay_property::kPositionX).c_str(), 0.62f,
                                      2.0)));
    piece.events.push_back(event("slide-y",
                                 seq::Trigger{.kind = seq::TriggerKind::Time, .timeSeconds = 5.0},
                                 move(std::string(seq::overlay_property::kPositionY).c_str(), 0.44f,
                                      2.0)));
    seq::EventAction click;
    click.kind = seq::EventActionKind::Overlay;
    click.target = "pointer";
    click.value = std::string(seq::overlay_property::kScaleX);
    click.amount = glm::vec4(0.7f, 0.0f, 0.0f, 0.0f);
    click.seconds = 0.08;
    click.holdSeconds = 0.05;
    piece.events.push_back(
        event("click", seq::Trigger{.kind = seq::TriggerKind::Time, .timeSeconds = 7.2}, click));

    seq::EventAction select;
    select.kind = seq::EventActionKind::Notify;
    select.target = "selection";
    select.value = "elder";
    piece.events.push_back(
        event("select", seq::Trigger{.kind = seq::TriggerKind::Time, .timeSeconds = 7.3}, select));

    comp::LayerStack stack;
    params::ParameterSet params;
    stack.attach(params);
    seq::CompositionLayerSink sink(stack, &params);
    auto baked = piece.bake(sink);
    REQUIRE(baked);
    REQUIRE(baked->overlays.size() == 1);
    const std::string layerId = baked->overlays[0].layerId;
    REQUIRE_FALSE(layerId.empty());

    const params::Timeline timeline = timelineOf(*baked);
    const seq::LayerTarget x = sink.target(layerId, seq::overlay_property::kPositionX);
    REQUIRE(x.valid());
    const params::Track* xTrack = nullptr;
    for (const params::Track& t : timeline.tracks()) {
        if (t.target == x.path && t.component == x.component) {
            xTrack = &t;
        }
    }
    REQUIRE(xTrack != nullptr);
    CHECK(xTrack->evaluate(5.0)[0] == Approx(0.1f));
    CHECK(xTrack->evaluate(7.0)[0] == Approx(0.62f));
    CHECK(xTrack->evaluate(11.0)[0] == Approx(0.62f)); // and it stays where it was put

    // The click is a value change too, and the selection is the only part that leaves the baked
    // tier -- because "the host now considers the elder selected" is not a value over time.
    CHECK(baked->events.dispatches.size() == 1);
    CHECK(piece.events[baked->events.dispatches[0].eventIndex].id == "select");
    CHECK(baked->events.dispatches[0].timeSeconds == Approx(7.3));

    // An overlay property the layer system does not expose is a warning, not a silent miss.
    seq::Sequence wrong = piece;
    wrong.events[0].what.value = "chirality";
    seq::CompositionLayerSink sink2(stack, &params);
    auto missed = wrong.bake(sink2);
    REQUIRE(missed);
    CHECK(std::any_of(missed->warnings.begin(), missed->warnings.end(), [](const std::string& w) {
        return w.find("does not expose") != std::string::npos;
    }));
}

// ---- serialisation -----------------------------------------------------------------------------------

TEST_CASE("Events survive a round trip through the project file", "[seq][events]") {
    seq::Sequence piece;
    piece.shots.push_back(keyedShot("a", 0.0, 20.0));
    seq::EventAction shake;
    shake.kind = seq::EventActionKind::CameraShake;
    shake.amount = glm::vec4(0.4f, 14.0f, 1.2f, 0.0f);
    shake.seconds = 0.9;
    piece.events.push_back(event("hit",
                                 seq::Trigger{.kind = seq::TriggerKind::Bar,
                                              .every = 8,
                                              .index = 2,
                                              .name = "",
                                              .subject = "",
                                              .fromSeconds = 4.0,
                                              .toSeconds = 40.0,
                                              .delaySeconds = 0.1,
                                              .repeat = 3},
                                 shake, 7));
    seq::EventAction act;
    act.kind = seq::EventActionKind::EntityAction;
    act.target = "elder";
    act.value = "walkTo";
    act.argument = "door";
    act.mode = params::TrackMode::Multiply;
    act.holdSeconds = 2.0;
    piece.events.push_back(event("go",
                                 seq::Trigger{.kind = seq::TriggerKind::InteractionComplete,
                                              .name = "kettle",
                                              .subject = "elder"},
                                 act));
    piece.events.back().enabled = false;

    const nlohmann::json doc = piece.toJson();
    auto again = seq::Sequence::fromJson(doc);
    REQUIRE(again);
    REQUIRE(again->events.size() == 2);
    CHECK(again->events[0].id == "hit");
    CHECK(again->events[0].priority == 7);
    CHECK(again->events[0].when.kind == seq::TriggerKind::Bar);
    CHECK(again->events[0].when.every == 8);
    CHECK(again->events[0].when.index == 2);
    CHECK(again->events[0].when.fromSeconds == Approx(4.0));
    CHECK(again->events[0].when.toSeconds == Approx(40.0));
    CHECK(again->events[0].when.delaySeconds == Approx(0.1));
    CHECK(again->events[0].when.repeat == 3);
    CHECK(again->events[0].what.kind == seq::EventActionKind::CameraShake);
    CHECK(again->events[0].what.amount.y == Approx(14.0f));
    CHECK(again->events[0].what.seconds == Approx(0.9));
    CHECK(again->events[1].what.kind == seq::EventActionKind::EntityAction);
    CHECK(again->events[1].what.argument == "door");
    CHECK(again->events[1].what.mode == params::TrackMode::Multiply);
    CHECK(again->events[1].what.holdSeconds == Approx(2.0));
    CHECK(again->events[1].when.kind == seq::TriggerKind::InteractionComplete);
    CHECK_FALSE(again->events[1].enabled);
    CHECK(again->toJson().dump() == doc.dump());
}

// ---- installation ------------------------------------------------------------------------------------

TEST_CASE("Installing a sequence hands the event schedule to the host", "[seq][events]") {
    seq::Sequence piece;
    piece.shots.push_back(keyedShot("a", 0.0, 20.0));
    piece.events.push_back(event("fog",
                                 seq::Trigger{.kind = seq::TriggerKind::Time, .timeSeconds = 8.0},
                                 setParam("scene/fogDensity", 2.0f, params::TrackMode::Add)));
    seq::EventAction act;
    act.kind = seq::EventActionKind::Notify;
    act.target = "selection";
    piece.events.push_back(
        event("pick", seq::Trigger{.kind = seq::TriggerKind::Time, .timeSeconds = 9.0}, act));

    params::ParameterSet params;
    params.add(params::ParamDesc<glm::vec3>{
        .path = "camera/position", .defaultValue = glm::vec3(0.0f),
        .hardMin = glm::vec3(-1e4f), .hardMax = glm::vec3(1e4f)});
    params.add(params::ParamDesc<glm::vec3>{
        .path = "camera/target", .defaultValue = glm::vec3(0.0f),
        .hardMin = glm::vec3(-1e4f), .hardMax = glm::vec3(1e4f)});
    params.add(params::ParamDesc<float>{
        .path = "camera/mode", .defaultValue = 0.0f, .hardMin = 0.0f, .hardMax = 2.0f});
    params.add(params::ParamDesc<float>{
        .path = "scene/fogDensity", .defaultValue = 0.0f, .hardMin = 0.0f, .hardMax = 8.0f});

    params::Timeline timeline;
    seq::NullLayerSink sink;
    auto report = seq::install(piece, timeline, params, sink, {});
    REQUIRE(report);
    CHECK(report->events.baked.size() == 1);
    CHECK(report->events.dispatches.size() == 1);
    CHECK(report->unresolved.empty());

    // Re-installing replaces rather than stacks -- the same ownership rule everything else in a
    // sequence follows, now that events write tracks too.
    const int tracksAfterFirst = report->trackCount;
    auto second = seq::install(piece, timeline, params, sink, report->targets);
    REQUIRE(second);
    CHECK(second->trackCount == tracksAfterFirst);
    CHECK(static_cast<int>(timeline.tracks().size()) == tracksAfterFirst);
}
