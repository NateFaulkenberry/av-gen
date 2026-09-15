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
#include "ui/ui_logic.hpp"

struct ImDrawList;
struct ImVec2;

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

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

    void draw(app::Engine& engine);

    // What the strip has selected, so the host can show it elsewhere.
    enum class Selection : std::uint8_t { None, Shot, Actor, Overlay, Section };
    [[nodiscard]] Selection selection() const { return selection_; }
    [[nodiscard]] int selectedIndex() const { return selected_; }

    // Called by the host when a lyric file was chosen.
    void importLyrics(app::Engine& engine, const std::filesystem::path& path);

    // Analysis state, for a host that wants to show it elsewhere.
    [[nodiscard]] bool analysing() const { return work_ != nullptr; }

private:
    void drawToolbar(app::Engine& engine);
    void drawImportPopup(app::Engine& engine);
    void drawStrip(app::Engine& engine);
    void drawInspector(app::Engine& engine);
    void drawShotInspector(app::Engine& engine, seq::Shot& shot);
    void drawActorInspector(app::Engine& engine, seq::Actor& actor);
    void drawOverlayInspector(app::Engine& engine, seq::OverlayCue& cue);
    void drawSectionInspector(app::Engine& engine, std::size_t index);
    void drawSceneSlots(app::Engine& engine);
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
    };

    void drawEdgeGrip(ImDrawList* draw, ImVec2 a, ImVec2 b, BlockZone zone, Drag active, bool isShot);

    // Marks the sequence as edited. The bake runs at the end of the frame the edit settled in.
    void touch() { dirty_ = true; }
    void installIfDirty(app::Engine& engine);
    [[nodiscard]] double snap(const app::Engine& engine, double seconds) const;
    // Beat times from the analysed track, cached: the vector is thousands of doubles and the strip
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

    Selection selection_ = Selection::None;
    int selected_ = -1;
    bool dirty_ = false;
    int snapMode_ = 2; // Beats
    float zoom_ = 1.0f;
    double view_ = 0.0; // leftmost second shown
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
    // spectrum analyser.
    bool analyseOnImport_ = true;
    bool generateOnImport_ = false;
    int sectionSnap_ = 1; // 0 off, 1 beat, 2 bar
    std::shared_ptr<StructureWork> work_;
    app::JobId workJob_ = 0;
    // The audio the structure was last analysed for. 0 means "never", which is what makes a freshly
    // imported track analyse itself once and a reopened project not analyse at all.
    std::uint64_t structureRevision_ = 0;
    char labelBuffer_[96] = "";
};

} // namespace avgen::ui
