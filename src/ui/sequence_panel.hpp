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

#include "app/engine.hpp"
#include "audio/arrangement.hpp"
#include "audio/waveform.hpp"
#include "seq/sequence.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace avgen::ui {

class SequencePanel {
public:
    // Asks the host to open a file dialog for a lyric file (LRC/SRT/WebVTT). Optional; without it
    // the import row offers a path field instead.
    std::function<void()> onImportLyrics;

    void draw(app::Engine& engine);

    // What the strip has selected, so the host can show it elsewhere.
    enum class Selection : std::uint8_t { None, Shot, Actor, Overlay };
    [[nodiscard]] Selection selection() const { return selection_; }
    [[nodiscard]] int selectedIndex() const { return selected_; }

    // Called by the host when a lyric file was chosen.
    void importLyrics(app::Engine& engine, const std::filesystem::path& path);

private:
    void drawToolbar(app::Engine& engine);
    void drawStrip(app::Engine& engine);
    void drawInspector(app::Engine& engine);
    void drawShotInspector(app::Engine& engine, seq::Shot& shot);
    void drawActorInspector(app::Engine& engine, seq::Actor& actor);
    void drawOverlayInspector(app::Engine& engine, seq::OverlayCue& cue);
    void drawSceneSlots(app::Engine& engine);

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
    // The audio arrangement's editor (ADR-103): the clip list in the toolbar, and the clip outlines
    // drawn over the waveform lane.
    void drawAudioClips(app::Engine& engine);
    // The clips as they should be drawn *now*: the engine's, or the in-progress drag's. A drag
    // edits a copy because applying one would re-mix the whole piece, sixty times a second.
    [[nodiscard]] const std::vector<audio::AudioClip>& clipsForDrawing(const app::Engine& engine) const;

    // The audio lane's own state. The clips being dragged are a copy: `Engine::setAudioClips`
    // re-mixes the arrangement, which is a pass over every sample, and doing that per drag frame
    // would turn a smooth drag into a slideshow. The copy is applied on release, exactly as the
    // sequence's own bake waits for the mouse.
    std::vector<audio::AudioClip> audioEdit_;
    float clipLaneY_ = -1.0f; // where the clip lane was drawn this frame, for hit testing
    int audioSelected_ = -1;
    char audioPath_[512] = {};

    Selection selection_ = Selection::None;
    int selected_ = -1;
    bool dirty_ = false;
    int snapMode_ = 2; // Beats
    float zoom_ = 1.0f;
    double view_ = 0.0; // leftmost second shown
    // 0 none, 1 move shot, 2 resize shot, 3 move overlay, 4 resize overlay, 5 move audio clip,
    // 6 trim an audio clip's end
    int dragKind_ = 0;
    int dragIndex_ = -1;
    double dragGrab_ = 0.0;
    std::string status_;
    std::string lyricPath_;
    char nameBuffer_[96] = "";
    char textBuffer_[512] = "";
    std::vector<double> beatCache_;
    const void* beatSource_ = nullptr;
    audio::WaveformSummary waveCache_;
    const void* waveSource_ = nullptr;
};

} // namespace avgen::ui
