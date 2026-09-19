#pragma once

// The Sequence panel (ADR-089): the whole piece, in time, in one strip.
//
// The authoring loop this exists to make fast: load a song, press Sections so the strip fills with
// the music's own shape, press Add Shot on a section boundary, pick a camera preset, drag the shot
// until it looks right, drop a character on it, import the lyrics, scrub, play, render.
//
// ## Why a hand-drawn strip
//
// Because the thing an author needs to see is *simultaneity* -- which shot is on while which lyric
// is up while which clip is playing -- and a tree of collapsible rows cannot show that. The strip is
// one horizontal time axis with a lane per kind: markers, shots, one per actor, one for the
// overlays. Everything in it is at the same x for the same second, which is the only property that
// makes a music video authorable.
//
// ## The edit-then-install rule
//
// Dragging a shot edits `engine.sequence()`, which is a value and costs nothing to change. The bake
// happens on mouse release, not on every frame of the drag: a bake is cheap (see docs/sequencer.md)
// but it rebuilds tracks and layers, and doing that sixty times a second while a handle is moving
// would make the drag feel like the thing it is not.

#include "analysis/structure.hpp"
#include "app/engine.hpp"
#include "app/job_system.hpp"
#include "audio/arrangement.hpp"
#include "audio/waveform.hpp"
#include "seq/sequence.hpp"
#include "ui/edit_history.hpp"
#include "ui/ui_logic.hpp"

struct ImDrawList;
struct ImVec2;

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace avgen::app {
class EditSystem;
}

namespace avgen::ui {

class SequencePanel {
public:
    // Asks the host to open a file dialog for a lyric file (LRC/SRT/WebVTT). Optional; without it
    // the import row offers a path field instead.
    std::function<void()> onImportLyrics;
    // Asks the host to open a file dialog for an audio file. The *same* callback the File menu and
    // the O shortcut use (ADR-216): the Sequencer is where the button lives now, and all three
    // routes are one action rather than three that could drift apart.
    std::function<void()> onOpenAudio;
    // Where the song-structure analysis runs. Optional: with no job system it runs inline, which is
    // what a test wants and what a person never should get.
    app::JobSystem* jobs = nullptr;
    // The editor's one undo stack (ADR-092), so that Cmd+Z means the same thing in the sequencer as
    // it does in the viewport. Optional: without it the panel still edits, it simply cannot take an
    // edit back -- which is what it did before, and the reason the comment above `trimShotEnd` used
    // to say "this panel has no undo".
    //
    // **One history, not two.** A second stack owned by the panel would have been less plumbing and
    // would have got the *order* wrong: delete a shot, move a rock, press Cmd+Z, and the two stacks
    // cannot agree on which edit was last.
    app::EditSystem* edits = nullptr;
    // **Re-cut the film, because the sections just changed.**
    //
    // Song Mode is cut from `seq::Sequence::sectionTimeline` (ADR-247, and `songPlanForEngine`
    // since the film outranked the saved plan), and this panel is the only place that timeline is
    // edited. Without this callback a person could retype eight sections, watch nothing happen, and
    // conclude the section types were not wired to anything -- which is exactly how this was
    // reported. The Auto-director panel has re-cut on its own settings changes since the
    // "select Continuous shot and nothing changes" bug; this is the same rule for the other half of
    // the director's inputs, which happen to live in a different window.
    //
    // The host decides whether a re-cut is warranted -- it owns the mode and knows whether the
    // camera is directed -- so this is called on every settled section edit and may do nothing.
    // Called only for edits that change the *film*: a type, a treatment. Not on a boundary drag,
    // which fires every frame the pointer moves.
    std::function<void()> onSectionsEdited;

    void draw(app::Engine& engine);

    // What the strip has selected, so the host can show it elsewhere.
    enum class Selection : std::uint8_t { None, Shot, Actor, Overlay, Section, Clip };

    // One selected thing, in any lane. The strip's selection is a *set* of these, because a rubber
    // band across three lanes selects three kinds of object and "the selection" has to be able to
    // hold that.
    struct SelectedItem {
        Selection kind = Selection::None;
        int index = -1;
        friend bool operator==(const SelectedItem&, const SelectedItem&) = default;
    };
    [[nodiscard]] Selection selection() const { return selection_; }
    [[nodiscard]] int selectedIndex() const { return selected_; }

    // Called by the host when a lyric file was chosen.
    void importLyrics(app::Engine& engine, const std::filesystem::path& path);

    // Analysis state, for a host that wants to show it elsewhere.
    [[nodiscard]] bool analyzing() const { return work_ != nullptr; }

    // Where the strip was last laid out, in screen points, for the scripted-interaction driver
    // (`--ui-script strip`). It is only knowable after a frame has been drawn, the same way
    // `ControlPanel::canvas()` is, and for the same reason: a benchmark that drags "the middle of
    // the strip" has to be told where that is rather than guessing at a docked panel's position.
    // All zero until the panel has been drawn once.
    struct StripRect {
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
        float gutter = 0.0f;
        float shotsTop = 0.0f; // relative to y
        float rulerHeight = 0.0f;
        // Whether Dear ImGui considered the strip hovered on the frame this was recorded, and how
        // much of it survived the panel's clip rectangle. Both are here because the strip arm's
        // first failures were invisible without them: the gesture was in the right place, the
        // button was down, and nothing happened.
        bool hovered = false;
        float visibleHeight = 0.0f;
        // How many points of the panel the toolbar above the strip consumed.
        float toolbarHeight = 0.0f;
        [[nodiscard]] bool valid() const { return width > 1.0f && height > 1.0f; }
    };
    [[nodiscard]] const StripRect& stripRect() const { return stripRect_; }

    // ---- what the arrow keys step by (ADR-357) ---------------------------------------------------
    //
    // The transport owns the arrow keys and always has; what it did not have was any idea what grid
    // the author was working on, so it stepped a frame plain and a beat with Shift whatever the
    // strip was snapped to. These two are how it asks. Read-only, and on the panel rather than
    // duplicated into the application, because the snap mode is the *sequencer's* setting and a
    // second copy of it is a second thing to keep in step.
    [[nodiscard]] int snapMode() const { return snapMode_; }
    // The stretch of the piece the strip is currently showing, in seconds. Zero until the strip has
    // been drawn once, which the Off arm treats as "no view yet" rather than as a zero-length step.
    [[nodiscard]] double visibleSpanSeconds() const { return lastSpan_; }

private:
    void drawToolbar(app::Engine& engine);
    void drawImportPopup(app::Engine& engine);
    void drawStrip(app::Engine& engine);
    // Snap, the two zooms, Fit and Rebuild: the toolbar's second column. See the note at the
    // definition for the two moves this block has made and why it ended up here.
    void drawStripControls(app::Engine& engine);
    // Unresolved bake targets, analysis progress, the last error: under the strip, because they
    // come and go and a toolbar that changes height moves the strip out from under the pointer.
    void drawStripStatus(app::Engine& engine);
    // The drag handle under the lanes: pull it down for taller lanes. Sets the same `laneZoom_` the
    // toolbar's slider does; see the note at the definition for why both exist.
    void drawLaneZoomGrip();
    // The time scrollbar under the lanes. `span` is the visible duration.
    void drawTimeScrollbar(double duration, double span);
    void drawInspector(app::Engine& engine);
    void drawShotInspector(app::Engine& engine, seq::Shot& shot);
    // The chase/orbit/POV controls, showing only what the chosen behaviour reads.
    void drawBehaviorInspector(app::Engine& engine, seq::Shot& shot);

    // ---- the selection set ---------------------------------------------------------------------
    [[nodiscard]] bool isChosen(Selection kind, int index) const;
    // Replaces the whole selection with one thing, which is what a plain click means.
    void chooseOne(Selection kind, int index);
    // Everything the rubber band touches, in strip-local points. Pure apart from reading the piece:
    // it works from the data and the lane geometry rather than from anything that was drawn, so it
    // cannot disagree with the blocks on screen about where they are.
    [[nodiscard]] std::vector<SelectedItem> itemsIn(const app::Engine& engine, const StripLanes& lanes,
                                                    float axisX, float width, double view, double span,
                                                    float x0, float y0, float x1, float y1) const;
    void drawActorInspector(app::Engine& engine, seq::Actor& actor);
    void drawOverlayInspector(app::Engine& engine, seq::OverlayCue& cue);
    void drawSectionInspector(app::Engine& engine, std::size_t index);
    void drawSceneSlots(app::Engine& engine);
    // ADR-216's performer rules, as an editable list. See the implementation for why every field
    // is a picker.
    void drawPerformerRules(app::Engine& engine);
    // The strip's right-click menu. Separate from `drawStrip` because a popup outlives the frame
    // that opened it and must therefore read remembered state rather than the live pointer.
    void drawStripContextMenu(app::Engine& engine);

    // ---- what a shot, a lyric and an arrangement edit are, in one place each -------------------
    //
    // The toolbar and the context menu both add shots, and before this they each had their own idea
    // of what a new one looks like. Two places that create the same object is the drift the
    // addendum's section 10 is about -- the world editor solves it by routing everything through
    // `app::EditSystem`, and the sequencer, which has no such system and therefore no undo, gets
    // the next best thing: one function per operation, called by both.
    // ---- the slice tool (ADR-356) ----------------------------------------------------------------
    //
    // Cmd+click cuts whatever is under the pointer, on every lane that holds a span, on the grid
    // that lane's drags already obey. The three "Split..." items scattered across the context menu
    // and the section inspector are now the *same* operation reached a different way, which is the
    // rule this block of the header is about: two paths to one operation is how they drift, and
    // these two had already drifted -- neither Split item snapped, and neither was undoable.
    //
    // `planSliceAt` answers the whole question without mutating anything, so a menu item's enabled
    // test and the click ask it identically and cannot disagree about what is legal.
    [[nodiscard]] SlicePlan planSliceAt(const app::Engine& engine, StripLane lane, double rawSeconds,
                                        int actorRow) const;
    // Carries out a plan as one undo step, labelled, with `touchesAudio` taken from the plan rather
    // than from the call site. Writes the refusal to the status line and returns false when the plan
    // is not legal: a gesture that does nothing must still say why.
    bool performSlice(app::Engine& engine, const SlicePlan& plan);
    // The blocks a lane holds, as spans. The one place that knows a shot, a section, a clip, a lyric
    // and a cue are all "a thing occupying a stretch of the timeline".
    [[nodiscard]] std::vector<SliceBlock> sliceBlocksFor(const app::Engine& engine, StripLane lane,
                                                         int actorRow) const;

    void addShotAt(app::Engine& engine, double seconds);
    void addShotAtEnd(app::Engine& engine);
    void addLyricAt(app::Engine& engine, double seconds);
    void applyAudioClips(app::Engine& engine, std::vector<audio::AudioClip> clips);

    // ---- what a drag on the strip is -----------------------------------------------------------
    //
    // Was `int dragKind_` with a comment listing seven meanings. It became eight when the left-hand
    // trim arrived, and a switch over an int whose values are documented in a comment is one
    // off-by-one away from resizing the wrong end of the wrong thing.
    enum class Drag : std::uint8_t {
        None,
        Playhead,          // scrubbing: the ruler's press, held
        MoveShot,
        TrimShotStart,     // moves the start and keeps the end where it was
        TrimShotEnd,
        MoveOverlay,
        TrimOverlayStart,
        TrimOverlayEnd,
        SectionBoundary,
        Marquee,           // a rubber band over the lane backgrounds, selecting what it touches
    };


    void drawEdgeGrip(ImDrawList* draw, ImVec2 a, ImVec2 b, BlockZone zone, Drag active, bool isShot);

    // Marks the sequence as edited. The bake runs at the end of the frame the edit settled in.
    void touch() { dirty_ = true; }
    // Tells the host the section timeline changed, if anybody asked to be told.
    void sectionsEdited() const {
        if (onSectionsEdited) {
            onSectionsEdited();
        }
    }

    // ---- recording an edit for the undo stack --------------------------------------------------
    //
    // Bracket a mutation: `beginEdit` copies the sequence (and, when asked, the clips) as they
    // stand, the caller mutates them in place, and `commitEdit` copies them again and pushes the
    // pair. `touchesAudio` is not a convenience -- installing clips re-mixes the whole piece, so an
    // edit that did not touch the audio must say so; see `TimelineChange::clipsTouched`.
    //
    // Safe to call with no `edits`: both become no-ops and the mutation still happens.
    void beginEdit(app::Engine& engine, bool touchesAudio = false);
    void commitEdit(app::Engine& engine, std::string label);
    // Throws the bracketed edit away rather than pushing it. For the arm where the operation
    // declined after all: an undo step that restores the state it was already in is worse than no
    // step, because it makes Cmd+Z appear to do nothing once before it works.
    void abandonEdit() { pendingEdit_.reset(); }
    std::unique_ptr<TimelineChange> pendingEdit_;
    void installIfDirty(app::Engine& engine);
    [[nodiscard]] double snap(const app::Engine& engine, double seconds) const;
    // Beat times from the analyzed track, cached: the vector is thousands of doubles and the strip
    // asks for it every frame.
    [[nodiscard]] const std::vector<double>& beats(const app::Engine& engine);
    // The audio's drawable shape, cached on the file it came from. Summarising is a pass over every
    // sample -- five million of them for a three-minute song -- so it happens when the file changes
    // and never while drawing.
    [[nodiscard]] const audio::WaveformSummary& waveform(const app::Engine& engine);
    // The audio arrangement's editor (ADR-103): the clip list behind the toolbar's Audio... button.
    // Where a clip is moved and trimmed, because the strip's audio lane is a scrub and stays one.
    void drawAudioClips(app::Engine& engine);

    // ---- song structure (ADR-215, ADR-216) ------------------------------------------------------
    //
    // The detection is a background job. It is never run per frame and never on the UI thread: on a
    // four-minute track it is a self-similarity matrix and several novelty passes, and doing that
    // between two frames would stop the editor dead at exactly the moment somebody has just dropped
    // a file on it.
    struct StructureWork {
        std::shared_ptr<const analysis::AnalysisTrack> track;
        // The audio this was started for. Compared on completion, so a result for a file that has
        // since been replaced is discarded instead of being merged into the wrong piece.
        std::uint64_t revision = 0;
        bool merge = false; // apply the re-analysis policy rather than replacing outright
        std::atomic<bool> finished{false};
        analysis::SongStructure result; // written before `finished`, read after it
        std::string error;
    };
    void startStructureAnalysis(app::Engine& engine, bool merge);
    void pollStructureAnalysis(app::Engine& engine);
    // Beat- or bar-aware snapping for a section boundary, which is *optional* and separate from the
    // strip's own snap: a person dragging a section boundary and a person dragging a shot are not
    // necessarily asking for the same grid. Returns the snap point's own value, never a rounded one.
    [[nodiscard]] double snapSection(const app::Engine& engine, double seconds) const;

    // Which clip the pointer last landed on, for the highlight and the popup. Nothing else: the
    // audio lane holds no drag state, because it holds no drag.
    int audioSelected_ = -1;
    char audioPath_[512] = {};

    // The **primary** selection: the last thing clicked, and what the inspector shows. Kept as a
    // kind and an index rather than folded into `chosen_` because every existing caller -- the
    // inspector, the host's `selection()`, the context menus -- asks "what one thing is selected",
    // and a rubber band is the only thing that ever selects more.
    Selection selection_ = Selection::None;
    int selected_ = -1;
    // Everything selected, when that is more than one thing. Empty means the primary above is the
    // whole selection, which is the common case and costs nothing.
    //
    // A vector rather than a set: it is a handful of items, it is iterated far more often than it is
    // searched, and the order is the order the rubber band found them -- which is the order a
    // multiple delete should remove them in.
    std::vector<SelectedItem> chosen_;
    // The rubber band, in strip-local points, while `drag_ == Drag::Marquee`. Four floats rather
    // than two `ImVec2`s because this header is deliberately ImGui-free -- `ImVec2` is forward
    // declared and cannot be held by value.
    float marqueeFromX_ = 0.0f;
    float marqueeFromY_ = 0.0f;
    float marqueeToX_ = 0.0f;
    float marqueeToY_ = 0.0f;
    // Whether the band adds to what was already selected (shift) rather than replacing it, and what
    // that selection was when the band started. Remembered rather than merged as it goes, because
    // the band recomputes its own hits every frame and a merged set could only ever grow.
    bool marqueeAdds_ = false;
    std::vector<SelectedItem> marqueeKept_;
    bool dirty_ = false;
    int snapMode_ = 2; // Beats
    // Logic's "catch". When on, the strip follows the playhead during playback -- which the seek
    // rule deliberately does not do, because dragging the view under a pointer mid-edit is worse
    // than losing sight of the playhead -- and both zooms keep the playhead where it is on screen
    // instead of growing the window around its left edge.
    //
    // Off by default, and the seek-follow below stays regardless: Return going to 0:00 should take
    // you there whether or not you have asked to be followed.
    bool catchPlayhead_ = false;
    // Set when catch is switched on, so the first frame after catches up instead of waiting for the
    // playhead to reach the edge of its own accord.
    bool catchUpNow_ = false;
    // The visible span the current `view_` was chosen for, so a zoom can be detected after the fact
    // and the playhead kept where it was on screen.
    double lastSpan_ = 0.0;
    // The piece's duration as the strip last computed it, so the scrollbar drawn after it sizes its
    // thumb from the same number the axis used rather than recomputing one that could differ.
    double lastDuration_ = 0.0;
    // Where on the thumb a scrollbar drag was grabbed, so the thumb does not jump under the pointer.
    float scrollGrab_ = 0.0f;
    float zoom_ = 1.0f;
    // Vertical zoom: a multiplier on every lane's height. One number rather than a height per lane,
    // so the strip keeps its proportions and the lanes stay comparable.
    float laneZoom_ = 1.0f;
    double view_ = 0.0; // leftmost second shown
    // The transport clock as of the last frame, so a seek can be told from playback. See the note
    // in `drawStrip`.
    double lastClock_ = 0.0;
    Drag drag_ = Drag::None;
    int dragIndex_ = -1;
    double dragGrab_ = 0.0;
    // The edge a trim is holding still. Remembered at the press rather than recomputed, because
    // trimming a shot's start changes its duration, and a duration read back from the live object
    // mid-drag would move the end as well -- which is a move, not a trim.
    double dragAnchor_ = 0.0;

    // A right press that travels is a pan and a right press that stays put is a menu. Dear ImGui's
    // own context-menu helper cannot tell them apart -- it opens on the release and measures
    // nothing -- so the strip keeps its own tracker. See `ui::updateContextClick`.
    ContextClickTracker contextClick_;

    // What the open context menu is about, captured at the click. A popup is submitted on later
    // frames, by which time the pointer has moved; a menu that hit-tested live would act on
    // whatever happened to be under the cursor when an item was chosen.
    struct MenuTarget {
        StripLane lane = StripLane::None;   // the lane the click was in, on the time axis
        StripLane header = StripLane::None; // or the lane whose header it was in
        double seconds = 0.0;
        int index = -1;    // which block within the lane, or -1 for empty space
        int actorRow = -1; // which actor lane, when there is one
    };
    MenuTarget menu_;
    StripRect stripRect_;
    // Set by a menu item that wants the Audio... popup, which cannot be opened from inside another
    // popup's body.
    bool openAudioClips_ = false;
    std::string status_;
    std::string lyricPath_;
    char nameBuffer_[96] = "";
    char textBuffer_[512] = "";
    std::vector<double> beatCache_;
    // Keyed on the engine's audio revision, not on the address of the track or the file. Those are
    // freed and reallocated on every re-mix, and an allocator that hands back the same address made
    // the cache conclude "nothing changed" about a different mix -- which is how a waveform ended up
    // drawn under clips it did not belong to. 0 means "nothing cached yet".
    std::uint64_t beatRevision_ = 0;
    audio::WaveformSummary waveCache_;
    std::uint64_t waveRevision_ = 0;

    // Import options (the brief's section 4): two checkboxes and no third. There is deliberately no
    // FFT size, no hop, no confidence threshold and nothing about the Director's internals here --
    // a person importing a song is deciding whether to look at its shape, not configuring a
    // spectrum analyzer.
    bool analyzeOnImport_ = true;
    bool generateOnImport_ = false;
    int sectionSnap_ = 1; // 0 off, 1 beat, 2 bar
    std::shared_ptr<StructureWork> work_;
    app::JobId workJob_ = 0;
    // The audio the structure was last analyzed for. 0 means "never", which is what makes a freshly
    // imported track analyze itself once and a reopened project not analyze at all.
    std::uint64_t structureRevision_ = 0;
    char labelBuffer_[96] = "";
    // The "+ New type..." popup's fields. Plain buffers rather than std::string because ImGui's
    // InputText takes one, and cleared on open so the popup never reopens holding the last attempt.
    char newTypeName_[64] = "";
    char newTypeDescription_[160] = "";
    int newTypeIntent_ = 0;
};

} // namespace avgen::ui
