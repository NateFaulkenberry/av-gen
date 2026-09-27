#pragma once

// The Director panel's decisions, separated from its drawing (spec §35, ADR-762).
//
// The panel visualises the architecture rather than defining it: everything it shows is a view of a
// `directing::Compilation` -- the plan's items and what the validator said about each, the diff
// grouped by item, and the context the plan was resolved in. What it decides is small and exact:
// which mark an item gets, which buttons are live, and why a live-looking button is not. Asked here
// so a test can ask it without a window.

#include "core/error.hpp"
#include "directing/compiler.hpp"
#include "directing/plan.hpp"
#include "directing/scene_facts.hpp"
#include "directing/time_ref.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
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
    std::string kind;  // "shot", "performance", "cue", "marker", "retime", "route", "source"
    std::string label; // what the plan calls it: a shot's name, a performance's subject
    ItemMark mark = ItemMark::Ok;
    std::vector<std::string> lines; // the validator's messages for it, errors first
};

// One row per plan item, in plan order (shots, performances, markers, cues, retimes, then ADR-924's
// sources and routes, then ADR-929's set pieces), plus a row keyed "" for plan-wide findings when
// there are any.
[[nodiscard]] std::vector<PlanItemRow> planItemRows(const directing::Plan& plan, const directing::Validation& validation);

// ADR-924: a project plan's reactivity, as the panel lists it under "Plans in this project": one row
// per route item, micro first, then meso, then macro -- what answers which layer of the music, how,
// why, and whether the route it made is still the route it made.
struct ReactivityRow {
    std::string key;
    std::string level;  // "micro" | "meso" | "macro"
    std::string what;   // "kick -> elder-2-gills glow: add 0.60, delay 0 ms"
    std::string reason; // the item's reason
    // "as made"; "edited by hand" (a later revision leaves it alone); "not in the project" (deleted, or
    // the plan was never installed here).
    std::string state;
};
[[nodiscard]] std::vector<ReactivityRow> reactivityRows(const directing::Plan& plan,
                                                        const std::vector<params::ModRoute>& routes);
// "59 routes: micro 15, meso 31, macro 13" -- the heading of a plan's reactivity rows.
[[nodiscard]] std::string reactivityHeading(const directing::Plan& plan);

// ---- UFO set pieces (ADR-929) -------------------------------------------------------------------
//
// The Director panel's "UFO set pieces" section: every set piece in the project's plans, said the way
// somebody describing the picture would say it, with the values its controls edit. A control's edit
// is a revision of the plan (`editSetPiece`), compiled and applied as ONE undoable edit, exactly as an
// approved proposal is -- so the plan stays the truth and the Parameters panel's `staging/setpiece/
// <key>/` sliders stay what they are: fine tuning of the set piece the plan made.
struct SetPieceRow {
    std::string plan; // the plan's id
    std::string planTitle;
    std::string key;
    std::string templateName; // abduction | survey | flyby
    std::string craft;
    std::string what;  // "abduction: lifts 2 animals"
    std::string when;  // "beam at 01:02.500 (bar 34)" -- the placed moment and its time
    std::string where; // "over (62, 22)"; "near lantern"; "the nearest animal within 30 m of (150, 180)"
    std::string how;   // "comes in from 90 deg, hovers 30 m up, red beam, meant to be seen from about 40 m"
    // "as made"; "tuned by hand" (a `staging/setpiece/<key>/` slider moved since: a revision keeps
    // it, so the controls wait); "not in the project" (its scenario was deleted, or never installed).
    std::string state;
    bool editable = false;
    std::string whyNot; // when not editable: what to do about it
    std::vector<std::string> lines; // what the validator says about it now, errors first
    // What the controls edit, as the plan holds it.
    std::vector<std::string> moments; // the template's, in order
    std::string moment;               // the one its time places
    std::string timeText;             // the time as the plan wrote it ("bar 57", "12s")
    std::optional<double> seconds;    // ...as it resolves now; absent when it cannot be placed
    bool point = false;               // placed on a point (the only place the controls move)
    float x = 0.0f;
    float z = 0.0f;
    bool namedAnimals = false;        // lifts named animals: the count is theirs
    int animals = 0;                  // abduction
    float bearing = 0.0f;             // approachBearing (abduction, survey) or pathBearing (flyby)
    float height = 0.0f;              // hoverHeight (abduction, survey) or altitude (flyby)
    std::optional<std::array<float, 3>> beamColor;
    std::optional<float> framingMetres;
};
[[nodiscard]] std::vector<SetPieceRow> setPieceRows(const directing::Plan& plan, const directing::SceneFacts& facts);

// One control's change to one set piece. Unset fields are left as they are.
struct SetPieceEdit {
    std::optional<std::string> templateName;   // slots, animals, moment and colour the new one cannot
                                               // take are dropped; a region becomes its centre
    std::optional<std::string> moment;         // which moment the time places
    std::optional<double> seconds;             // the placed moment's time, written as seconds
    std::optional<std::pair<float, float>> point; // a point place, world x, z
    std::vector<std::pair<std::string, float>> slots; // template slots, each replacing its override
    std::optional<std::optional<std::array<float, 3>>> beamColor; // a colour, or nullopt to clear it
    std::optional<float> framingMetres;        // <= 0 clears it
};
// The plan with set piece `key` changed; not compiled (compiling makes it the next revision). Fails
// when the plan has no such set piece or the edit names something the template does not have.
[[nodiscard]] Result<directing::Plan> editSetPiece(const directing::Plan& plan, std::string_view key,
                                                   const SetPieceEdit& edit);
// The revision an edit makes, compiled against `facts` and ready for `app::applyCompilation`; or why
// it is not applied. Refused when the edited set piece would be blocked -- a revision that blocks an
// item takes out what its last revision made and builds nothing in its place, so one bad drag would
// delete the set piece -- and when nothing would change.
[[nodiscard]] Result<directing::Compilation> compileSetPieceEdit(const directing::Plan& plan, std::string_view key,
                                                                 const SetPieceEdit& edit,
                                                                 const directing::SceneFacts& facts);
// The undo label of an edit: "UFO set piece 'west': time".
[[nodiscard]] std::string setPieceEditLabel(std::string_view key, const SetPieceEdit& edit);

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
    bool liveToRecord = false;    // the proposal has live (goal) performances not yet recorded (ADR-765)
    bool recording = false;       // a recording is running
    bool followUp = false;        // the Modify box has text in it (ADR-770)
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
    Button record;     // bake the live performances by recording them (ADR-765)
    Button cancelRecording; // stop a running recording; the proposal stays as it was
    Button modify;     // revise the waiting plan from a follow-up request (ADR-770)
    Button regenerate; // propose afresh from the original request (ADR-770)
    bool revertPreviewFirst = false; // Accept/Reject undo the preview before acting
};
[[nodiscard]] PanelActions panelActions(const PanelState& state);

} // namespace avgen::ui
