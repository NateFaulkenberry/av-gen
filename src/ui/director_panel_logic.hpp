#pragma once

// The Director panel's decisions, separated from its drawing (spec §35, ADR-762).
//
// The panel visualises the architecture rather than defining it: everything it shows is a view of a
// `directing::Compilation` -- the plan's items and what the validator said about each, the diff
// grouped by item, and the context the plan was resolved in. What it decides is small and exact:
// which mark an item gets, which buttons are live, and why a live-looking button is not. Asked here
// so a test can ask it without a window.

#include "directing/compiler.hpp"
#include "directing/plan.hpp"
#include "directing/time_ref.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace avgen::ui {

// ✓ / ! / ✗ (the panel draws them as shapes: the UI font is ASCII).
enum class ItemMark : std::uint8_t {
    Ok,      // compiles as planned
    Warning, // compiles, with something the person should read
    Blocked, // will not be compiled; the lines say why
};
[[nodiscard]] const char* itemMarkName(ItemMark mark);

struct PlanItemRow {
    std::string key;
    std::string kind;  // "shot", "performance", "cue", "marker", "retime"
    std::string label; // what the plan calls it: a shot's name, a performance's subject
    ItemMark mark = ItemMark::Ok;
    std::vector<std::string> lines; // the validator's messages for it, errors first
};

// One row per plan item, in plan order (shots, performances, markers, cues, retimes), plus a row
// keyed "" for plan-wide findings when there are any.
[[nodiscard]] std::vector<PlanItemRow> planItemRows(const directing::Plan& plan, const directing::Validation& validation);

// The proposed changes, grouped by the item that makes them, in the order the diff lists them.
// Findings ('!' lines) are the rows' business and are left out.
struct ChangeGroup {
    std::string item;
    std::vector<directing::DiffLine> lines;
};
[[nodiscard]] std::vector<ChangeGroup> changeGroups(const std::vector<directing::DiffLine>& diff);

// What the plan was resolved against: the time the person is looking at and what the song is doing
// there, and every subject the plan names with what it resolved to.
struct ContextView {
    std::string time;    // "01:30.250"
    std::string section; // "chorus 2", or "" when the song has no sections there
    std::vector<std::string> subjects; // "rook -> rook (entity)"; "umbra -> ? (unresolved)"
};
[[nodiscard]] ContextView contextView(const directing::Plan& resolved, const directing::MusicalContext& music,
                                      double playheadSeconds);
[[nodiscard]] std::string clockText(double seconds);

// ---- the buttons -------------------------------------------------------------------------------
//
// Preview is the proposal installed as an ordinary, labelled edit on the history, so the person can
// play it where it will be. It is ended by undoing that edit -- which only makes sense while it is
// still the newest edit: once something else has been done, undoing would take that instead. Accept
// and Reject end a preview that is still newest before they act, and otherwise leave it in the
// history, saying so, rather than undo somebody's later work.

struct PanelState {
    bool proposal = false;        // a task has proposed a plan
    bool awaiting = false;        // ...and is waiting for the person (ADR-757)
    bool changesAnything = false; // the proposal's dry run would change the project
    bool previewing = false;      // a preview edit was made for this proposal
    bool previewIsNewest = false; // ...and nothing has been done or undone since
};

struct Button {
    bool enabled = false;
    std::string why; // shown when disabled, or as a note beside an enabled one
};

struct PanelActions {
    Button preview;    // install it temporarily
    Button endPreview; // take the preview out again
    Button accept;
    Button reject;
    bool revertPreviewFirst = false; // Accept/Reject undo the preview before acting
};
[[nodiscard]] PanelActions panelActions(const PanelState& state);

} // namespace avgen::ui
