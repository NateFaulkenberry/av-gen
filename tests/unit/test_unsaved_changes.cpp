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
