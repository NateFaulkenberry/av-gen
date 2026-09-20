#pragma once

// "Do you want to save changes? Yes / No / Cancel" -- the state machine, with no Dear ImGui, no
// Engine and no Window in it (ADR-440).
//
// **Why a state machine rather than three `if`s at the call site.** The save this dialog offers is
// not synchronous. A project with no path has to go through Save As, and Save As is SDL's native
// dialog: `Window::saveFileDialog` returns immediately and delivers the chosen path (or an empty
// string for cancel) on a *later frame*, from inside `pollEvents`. So "Yes" cannot be `save();
// proceed();` -- there is a gap of unknown length in the middle, during which the application must
// neither close the project nor forget what it was about to do. Three `if`s at the call site is how
// the second cancel gets lost.
//
// **The second cancel is the whole point.** There are two of them and they mean the same thing:
// Cancel in this dialog, and Cancel in the Save As dialog that "Yes" opens. Both must abandon the
// close entirely -- not save-and-close, not close-without-saving, and not "the close already
// happened, this only decided whether to save first". `saveFinished(false)` therefore lands in
// `Idle` with the pending action dropped, exactly where `answer(Cancel)` lands. That is asserted in
// tests/unit/test_unsaved_changes.cpp rather than described here, because ADR-385: a stated reason
// is not evidence.
//
// The gate holds *what* was intended (an enum and, for an open, a path) and never *how* to do it.
// The effect belongs to the host: `Application` keeps the continuation, because performing a close
// means touching the engine, the window and the panel, and a gate that could do any of that could
// not be tested without all three.

#include <filesystem>
#include <string>

namespace avgen::ui {

// What the current project is about to be lost to. Named after what the user asked for, because
// the modal says it back to them: "Glowmere Valley 2 has unsaved changes." / "...before quitting?"
enum class CloseIntent {
    NewProject,   // File > New
    OpenProject,  // File > Open, Open Recent, Examples, Engineering Labs, a dropped project
    Quit,         // the window's close button, Cmd-Q, SDL_EVENT_QUIT
};

// The three buttons, in the owner's words: Yes / No / Cancel.
enum class UnsavedAnswer {
    Save,     // Yes -- write, then proceed
    Discard,  // No  -- proceed, losing the changes
    Cancel,   // Cancel -- do nothing at all
};

class UnsavedChangesGate {
public:
    enum class State {
        Idle,          // nothing pending; the application is free
        Prompting,     // the modal is up
        AwaitingSave,  // "Yes" was pressed and a write (possibly a Save As dialog) is in flight
        Ready,         // the pending action is owed one performance
    };

    // Ask to close. `dirty` is measured by the host at this instant -- the gate never guesses.
    //
    // Returns true when the caller may go ahead immediately (nothing to lose). Returns false when
    // the modal has been raised *or* when one was already up: a second close request while a
    // prompt is open is dropped, so hammering Cmd-Q cannot stack three pending quits.
    bool requestClose(CloseIntent intent, std::filesystem::path path = {}) {
        return requestClose(intent, false, std::move(path));
    }
    bool requestClose(CloseIntent intent, bool dirty, std::filesystem::path path = {}) {
        if (state_ != State::Idle) {
            return false;
        }
        if (!dirty) {
            return true;
        }
        intent_ = intent;
        path_ = std::move(path);
        state_ = State::Prompting;
        return false;
    }

    // The modal's three buttons. Only meaningful while `Prompting`; anything else is ignored, so a
    // stale key binding cannot answer a dialog that is not on screen.
    void answer(UnsavedAnswer a) {
        if (state_ != State::Prompting) {
            return;
        }
        switch (a) {
        case UnsavedAnswer::Save:
            state_ = State::AwaitingSave;
            return;
        case UnsavedAnswer::Discard:
            state_ = State::Ready;
            return;
        case UnsavedAnswer::Cancel:
            reset();
            return;
        }
    }

    // The save the host started for `UnsavedAnswer::Save` has finished.
    //
    // `written == false` covers both ways it can fail to happen -- the Save As dialog was
    // cancelled, and the write itself errored -- and both abandon the close. Losing the project
    // because the disk was full would be the worst outcome this dialog could produce.
    void saveFinished(bool written) {
        if (state_ != State::AwaitingSave) {
            return;
        }
        if (written) {
            state_ = State::Ready;
        } else {
            reset();
        }
    }

    // Consumes the permission to proceed. True at most once per `requestClose`, so a host that
    // calls it every frame performs the close exactly once.
    [[nodiscard]] bool takeReady() {
        if (state_ != State::Ready) {
            return false;
        }
        reset();
        return true;
    }

    // Abandon whatever is pending, from anywhere -- Escape, a project that closed itself, shutdown.
    void reset() {
        state_ = State::Idle;
        path_.clear();
    }

    [[nodiscard]] State state() const { return state_; }
    [[nodiscard]] bool prompting() const { return state_ == State::Prompting; }
    // True whenever the application must not start another close, open or quit: the modal is up or
    // a save is in flight. `Ready` is deliberately not busy -- the host is about to act on it.
    [[nodiscard]] bool busy() const {
        return state_ == State::Prompting || state_ == State::AwaitingSave;
    }
    [[nodiscard]] CloseIntent intent() const { return intent_; }
    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
    State state_ = State::Idle;
    CloseIntent intent_ = CloseIntent::Quit;
    std::filesystem::path path_;
};

// What the modal says. `inline`, and in the header rather than beside the drawing, so the wording
// is testable without linking Dear ImGui -- the same reason `ui_logic.hpp` is header-only.
[[nodiscard]] inline const char* closeIntentVerb(CloseIntent intent) {
    switch (intent) {
    case CloseIntent::NewProject:
        return "starting a new project";
    case CloseIntent::OpenProject:
        return "opening another project";
    case CloseIntent::Quit:
        return "quitting";
    }
    return "closing";
}

// "Save changes to <project> before <verb>?" -- `projectName` empty for a project never saved.
//
// Named, because "Save changes?" over a window with four projects in the recent list does not say
// *which* project is about to lose them. A project that has never been saved has no name to give,
// and "this project" is the honest thing to put there -- it is also the case where Yes means Save
// As, so the next dialog asks for the name anyway.
[[nodiscard]] inline std::string unsavedChangesQuestion(CloseIntent intent,
                                                        const std::string& projectName) {
    const std::string subject = projectName.empty() ? "this project" : projectName;
    return "Save changes to " + subject + " before " + closeIntentVerb(intent) + "?";
}

// Draws the modal for a gate that is prompting, and reports which button was pressed.
//
// The gate is passed by const reference and is *not* advanced here: the caller applies the answer
// with `gate.answer(...)`, because "Yes" has to start a save the drawing code knows nothing about.
// `*answered` is false on every frame the dialog is merely on screen; the return value means
// nothing until it is true.
UnsavedAnswer drawUnsavedChangesModal(const UnsavedChangesGate& gate, const std::string& projectName,
                                      bool* answered);

} // namespace avgen::ui
