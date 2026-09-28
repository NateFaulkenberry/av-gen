// The unsaved-changes prompt, as a state machine (ADR-440).
//
// Everything here is pure: no Dear ImGui, no Engine, no Window. What is being asserted is the part
// that is easy to get wrong and impossible to see in a screenshot -- that **Cancel does nothing at
// all**, on both of the two Cancels this flow has, and that the pending action is never performed
// after one.
//
// ADR-182: a probe that cannot fail proves nothing. Every "Cancel did nothing" assertion below is
// paired with the Yes or No that *does* something from the identical starting state, so a gate that
// simply never proceeded would fail the suite rather than pass it.

#include "ui/unsaved_changes.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace avgen;
using ui::CloseIntent;
using ui::UnsavedAnswer;
using ui::UnsavedChangesGate;

namespace {

// "Did the close happen?", counted rather than flagged, so a close that happened twice is a
// different failure from a close that happened once.
struct Recorder {
    int performed = 0;
    void operator()() { ++performed; }
};

// Drives the gate the way `Application` does: request, then drain `takeReady()` for a few frames.
int runFrames(UnsavedChangesGate& gate, Recorder& rec, int frames = 5) {
    for (int i = 0; i < frames; ++i) {
        if (gate.takeReady()) {
            rec();
        }
    }
    return rec.performed;
}

} // namespace

TEST_CASE("a clean project closes with no prompt", "[unsaved][project]") {
    UnsavedChangesGate gate;
    CHECK(gate.requestClose(CloseIntent::Quit, /*dirty=*/false));
    CHECK(gate.state() == UnsavedChangesGate::State::Idle);
    CHECK_FALSE(gate.prompting());
    // And nothing is owed: `takeReady` must not also fire, or the close would happen twice.
    CHECK_FALSE(gate.takeReady());
}

TEST_CASE("a dirty project prompts instead of closing", "[unsaved][project]") {
    UnsavedChangesGate gate;
    CHECK_FALSE(gate.requestClose(CloseIntent::OpenProject, /*dirty=*/true, "b.json"));
    CHECK(gate.prompting());
    CHECK(gate.intent() == CloseIntent::OpenProject);
    CHECK(gate.path() == std::filesystem::path("b.json"));
    Recorder rec;
    CHECK(runFrames(gate, rec) == 0);
}

TEST_CASE("No discards and proceeds", "[unsaved][project]") {
    UnsavedChangesGate gate;
    Recorder rec;
    REQUIRE_FALSE(gate.requestClose(CloseIntent::NewProject, true));
    gate.answer(UnsavedAnswer::Discard);
    CHECK(gate.state() == UnsavedChangesGate::State::Ready);
    CHECK(runFrames(gate, rec) == 1);
    // Exactly once, however many frames run afterwards.
    CHECK(runFrames(gate, rec) == 1);
    CHECK(gate.state() == UnsavedChangesGate::State::Idle);
}

TEST_CASE("Yes waits for the save and only then proceeds", "[unsaved][project]") {
    UnsavedChangesGate gate;
    Recorder rec;
    REQUIRE_FALSE(gate.requestClose(CloseIntent::Quit, true));
    gate.answer(UnsavedAnswer::Save);
    // The save is in flight. This is the state a project with no path spends in the Save As dialog,
    // which is delivered asynchronously -- the close must not happen during it.
    CHECK(gate.state() == UnsavedChangesGate::State::AwaitingSave);
    CHECK(gate.busy());
    CHECK(runFrames(gate, rec) == 0);

    gate.saveFinished(true);
    CHECK(gate.state() == UnsavedChangesGate::State::Ready);
    CHECK(runFrames(gate, rec) == 1);
}

TEST_CASE("Cancel does nothing at all", "[unsaved][project][cancel]") {
    UnsavedChangesGate gate;
    Recorder rec;
    REQUIRE_FALSE(gate.requestClose(CloseIntent::OpenProject, true, "b.json"));
    gate.answer(UnsavedAnswer::Cancel);

    CHECK(gate.state() == UnsavedChangesGate::State::Idle);
    CHECK_FALSE(gate.prompting());
    CHECK_FALSE(gate.busy());
    // The pending action is never performed -- not on this frame and not on any later one. This is
    // the assertion the whole file exists for.
    CHECK(runFrames(gate, rec, 100) == 0);
    // And nothing is left behind that a later close could inherit.
    CHECK(gate.path().empty());

    // The control (ADR-182): from the identical starting state, No *does* proceed. Without this the
    // assertion above would pass on a gate that had simply stopped working.
    UnsavedChangesGate control;
    Recorder controlRec;
    REQUIRE_FALSE(control.requestClose(CloseIntent::OpenProject, true, "b.json"));
    control.answer(UnsavedAnswer::Discard);
    CHECK(runFrames(control, controlRec, 100) == 1);
}

TEST_CASE("Cancel in the Save As dialog cancels the whole close", "[unsaved][project][cancel]") {
    // The one the owner's brief singles out. "Yes" on a project with no path opens Save As; Cancel
    // *there* must cancel the close, not fall through to discarding the project.
    UnsavedChangesGate gate;
    Recorder rec;
    REQUIRE_FALSE(gate.requestClose(CloseIntent::Quit, true));
    gate.answer(UnsavedAnswer::Save);
    REQUIRE(gate.state() == UnsavedChangesGate::State::AwaitingSave);

    gate.saveFinished(false); // the dialog was dismissed, or the write failed
    CHECK(gate.state() == UnsavedChangesGate::State::Idle);
    CHECK(runFrames(gate, rec, 100) == 0);

    // Control: the same path with a save that succeeded does proceed.
    UnsavedChangesGate control;
    Recorder controlRec;
    REQUIRE_FALSE(control.requestClose(CloseIntent::Quit, true));
    control.answer(UnsavedAnswer::Save);
    control.saveFinished(true);
    CHECK(runFrames(control, controlRec, 100) == 1);
}

TEST_CASE("a second close request while the prompt is up is dropped", "[unsaved][project]") {
    // Three taps on Cmd-Q behind one dialog must not stack three quits, and must not replace the
    // pending open with a pending quit while the dialog is still asking about the open.
    UnsavedChangesGate gate;
    REQUIRE_FALSE(gate.requestClose(CloseIntent::OpenProject, true, "b.json"));
    CHECK_FALSE(gate.requestClose(CloseIntent::Quit, true));
    CHECK(gate.intent() == CloseIntent::OpenProject);
    CHECK(gate.path() == std::filesystem::path("b.json"));

    // Including the "nothing to lose" answer: a clean second request must not be allowed to proceed
    // past a prompt that is still open, or the application would close underneath its own dialog.
    CHECK_FALSE(gate.requestClose(CloseIntent::Quit, false));
}

TEST_CASE("an answer to a prompt that is not up is ignored", "[unsaved][project]") {
    UnsavedChangesGate gate;
    Recorder rec;
    gate.answer(UnsavedAnswer::Discard);
    CHECK(gate.state() == UnsavedChangesGate::State::Idle);
    CHECK(runFrames(gate, rec) == 0);
    // And a save report with no save in flight cannot manufacture a close.
    gate.saveFinished(true);
    CHECK(runFrames(gate, rec) == 0);
}

TEST_CASE("the question names the project and the thing about to happen", "[unsaved][project]") {
    CHECK(ui::unsavedChangesQuestion(CloseIntent::Quit, "glowmere-valley-2.json") ==
          "Save changes to glowmere-valley-2.json before quitting?");
    CHECK(ui::unsavedChangesQuestion(CloseIntent::OpenProject, "hero.json") ==
          "Save changes to hero.json before opening another project?");
    CHECK(ui::unsavedChangesQuestion(CloseIntent::NewProject, "hero.json") ==
          "Save changes to hero.json before starting a new project?");
    // A project that has never been saved has no name to give.
    CHECK(ui::unsavedChangesQuestion(CloseIntent::Quit, {}) ==
          "Save changes to this project before quitting?");
}

// ---- when the periodic sample runs (ADR-952) ---------------------------------------------------
//
// ADR-440's sample is a full serialisation, ~36 ms on Glowmere Valley 3, and the owner decided on
// 2026-09-28 that it does not run while the transport plays. What must not follow from that is a
// stale "clean": touches are still recorded while playing, and the first idle frame after a stop
// samples at once. Each "does not sample" below is paired with the frame that does, from the same
// schedule, so a schedule that never sampled would fail rather than pass (ADR-182).

using ui::DirtySampleSchedule;

namespace {

// Drives the schedule the way `Application` does, one call per 16 ms frame, and counts the samples
// it asks for. `sampleCostMs` is what each one "costs", so the throttle is the real one.
int framesOfSampling(DirtySampleSchedule& s, double& nowMs, int frames, bool playing,
                     bool widgetActive = false, double sampleCostMs = 36.0) {
    int samples = 0;
    for (int i = 0; i < frames; ++i) {
        nowMs += 16.0;
        if (s.due(nowMs, playing, widgetActive, /*editsDirty=*/false)) {
            ++samples;
            s.sampled(nowMs, sampleCostMs);
        }
    }
    return samples;
}

} // namespace

TEST_CASE("the dirty sample does not run while the transport is playing", "[unsaved][project][adr952]") {
    DirtySampleSchedule s;
    double now = 0.0;
    s.restart(now); // a project was just opened

    // The control first: paused for 10 s at 36 ms a sample is one sample every 360 ms -- the ~2.6 a
    // second W1 measured on GV3 (docs/qa-pass/perf.md).
    const int paused = framesOfSampling(s, now, 625, /*playing=*/false);
    CHECK(paused >= 25);
    CHECK(paused <= 30);

    // The same 10 s playing: none at all.
    CHECK(framesOfSampling(s, now, 625, /*playing=*/true) == 0);
}

TEST_CASE("the dirty sample runs on the first idle frame after playback stops",
          "[unsaved][project][adr952]") {
    DirtySampleSchedule s;
    double now = 0.0;
    s.restart(now);
    REQUIRE(framesOfSampling(s, now, 30, /*playing=*/true) == 0);

    // A short playback (480 ms) and a stop: due at once, not after the throttle.
    now += 16.0;
    CHECK(s.due(now, /*playing=*/false, false, false));
    s.sampled(now, 36.0);

    // ...once. The frame after it is back on the ordinary throttle.
    now += 16.0;
    CHECK_FALSE(s.due(now, false, false, false));

    // A stop under a held widget waits for the widget, then samples on the first frame it can.
    REQUIRE(framesOfSampling(s, now, 30, /*playing=*/true) == 0);
    now += 16.0;
    CHECK_FALSE(s.due(now, /*playing=*/false, /*widgetActive=*/true, false));
    now += 16.0;
    CHECK(s.due(now, false, false, false));
}

TEST_CASE("a touch during playback is kept for the sample or close that follows it",
          "[unsaved][project][adr952]") {
    // The failure this guards is the one that loses work: the sample skipped, and the touch that
    // would have told it the change was the user's forgotten with it.
    DirtySampleSchedule s;
    double now = 0.0;
    s.restart(now);
    REQUIRE(framesOfSampling(s, now, 60, /*playing=*/true) == 0);
    CHECK_FALSE(s.touched()); // the control: playing alone is not a touch

    // A slider dragged mid-playback, then released, then more playback.
    REQUIRE(framesOfSampling(s, now, 10, /*playing=*/true, /*widgetActive=*/true) == 0);
    REQUIRE(framesOfSampling(s, now, 300, /*playing=*/true) == 0);
    // What a close mid-playback passes to `Engine::projectDirty`.
    CHECK(s.touched());

    // And the stop sample is attributed to it too.
    now += 16.0;
    REQUIRE(s.due(now, false, false, false));
    CHECK(s.touched());
    s.sampled(now, 36.0);
    CHECK_FALSE(s.touched()); // the window restarts after a sample

    // An edit history with unsaved commands counts as a touch while playing, as it does paused.
    now += 16.0;
    CHECK_FALSE(s.due(now, /*playing=*/true, false, /*editsDirty=*/true));
    CHECK(s.touched());
}

TEST_CASE("a close measured on demand restarts the window, playing or not",
          "[unsaved][project][adr952]") {
    DirtySampleSchedule s;
    double now = 0.0;
    s.restart(now);
    REQUIRE(framesOfSampling(s, now, 30, /*playing=*/true, /*widgetActive=*/true) == 0);
    REQUIRE(s.touched());
    // `Application::requestClose` measured, and the answer was Cancel: the baseline moved, so the
    // click that raised the prompt is not an edit to what the engine writes next.
    s.restart(now);
    CHECK_FALSE(s.touched());
    // Still playing: still no periodic sample.
    CHECK(framesOfSampling(s, now, 120, /*playing=*/true) == 0);
}
