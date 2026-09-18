#include "core/interaction_latency.hpp"
#include "ui/sequence_panel.hpp"

#include "ui/shortcuts.hpp"
#include "ui/style.hpp"
#include "ui/theme.hpp"
#include "ui/ui_logic.hpp"

#include <chrono>

#include "analysis/analysis_track.hpp"
#include "analysis/structure.hpp"
#include "core/log.hpp"
#include "app/edit_system.hpp"
#include "scene/composition.hpp"
#include "seq/layer_sink.hpp"
#include "seq/lyrics.hpp"
#include "seq/section_performance.hpp"
#include "seq/song_structure.hpp"
#include "song/from_analysis.hpp"

#include <glm/trigonometric.hpp> // degrees/radians for the drift control

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

namespace avgen::ui {
namespace {

std::string clock(double seconds) {
    if (seconds < 0.0) {
        seconds = 0.0;
    }
    const int minutes = static_cast<int>(seconds / 60.0);
    const double rest = seconds - minutes * 60.0;
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%d:%05.2f", minutes, rest);
    return buffer;
}

// Lane geometry. A lane is a row of the strip; everything in it shares one time axis.
//
// The heights themselves live in `ui_logic.hpp` as `kStrip*` and are assembled by `stripLanesFor`,
// where a test can reach them -- the ruler and marker are still here because only the drawing uses
// them, and only from this file.
constexpr float kRulerHeight = 22.0f;
constexpr float kMarkerHeight = 16.0f;
// How far inside an edge still grabs it. Seven rather than five: five points is a comfortable
// target with a mouse and a fiddly one on a trackpad, and `blockZoneAt` shrinks it on a narrow
// block anyway, so the generous number costs nothing where it would have hurt.
constexpr float kEdgeGrab = 7.0f;
// The header column down the left (the brief's section 12). Wide enough for an actor's id and the
// visibility dot beside it, and no wider: every point here is a point of music not shown.
constexpr float kGutterWidth = 112.0f;
// The playhead's handle.
constexpr float kPlayheadHalfWidth = 6.0f;
constexpr float kPlayheadHandleHeight = 13.0f;
// The shortest a block may be trimmed to. A block of zero length cannot be grabbed again, so a
// trim that reached zero would be a delete with no way back -- and this panel has no undo.
constexpr double kMinBlockSeconds = 0.25;
// `splitSection`'s own minimum, named here so the menu's enabled test and the operation agree.
constexpr double kMinSectionSeconds = 0.25;
constexpr const char* kStripContextId = "strip-context";
// How much of the panel is kept back for the inspector under the strip, so that compressing the
// lanes to fit never squeezes the thing they are inspected in down to nothing.
constexpr float kStripBottomReserve = 46.0f + 12.0f; // + the lane-zoom grip under the lanes
// What the toolbar's second column needs before it is worth putting beside the buttons rather than
// under them. Two combos, two sliders and two buttons at their set widths, plus the spacing between
// them and the labels to their right -- measured from the row rather than guessed, and deliberately
// a little generous so the column is never drawn touching the panel's edge.
constexpr float kStripControlsWidth = 960.0f;
// A selected audio clip's outline. Amber rather than the shared accent: the waveform is drawn in the
// same blue family the accent belongs to, so the accent vanished into it.
constexpr ImU32 kClipSelected = IM_COL32(255, 196, 64, 255);
// The drag handle under the lanes. Tall enough to hit without aiming, short enough that it is not a
// lane of its own -- and its height is reserved out of the strip, so adding it did not push the
// shots lane back below the fold it was rescued from.
constexpr float kLaneZoomGripHeight = 12.0f;
// How much a point of vertical drag is worth. 1/160th means the full range is a drag of about 560
// points, which is a deliberate, controllable gesture rather than a flick.
constexpr float kLaneZoomPerPoint = 1.0f / 160.0f;
constexpr float kLaneZoomMin = 0.5f;
constexpr float kLaneZoomMax = 4.0f;
// How far the clock has to move in one frame before it counts as a seek rather than as playback.
// Several frames at 30 fps, so a stutter is never read as a jump.
constexpr double kSeekJumpSeconds = 0.25;
// Where in the view a seeked-to position lands, as a fraction from the left. An eighth in, so the
// moment before it is visible too.
constexpr double kSeekLead = 0.125;
// The floor the lanes compress to. Two thirds still reads as lanes; below that the panel's own
// scrollbar is the better answer, because a lane a few points high is a line rather than a lane.
constexpr float kMinLaneScale = 0.66f;

// A shot's resting colour. Alternating, so a cut between two shots on the same scene is visible as
// a cut rather than as a join.
//
// The selected and hovered variants are *not* here any more: `ui::interactionFill` owns those for
// the whole application, which is what makes a selected shot, a selected list row and a selected
// tab the same colour. This function's only remaining job is the alternation.
ImU32 shotColour(int index) {
    const Palette& p = palette();
    return mixColour(p.lane, p.raised, index % 2 == 0 ? 0.85f : 0.55f);
}

const char* kSnapNames[] = {"Off", "Frames", "Beats", "Markers"};
const char* kSectionSnapNames[] = {"free", "beat", "bar"};

// One colour per section function, so the shape of a song is readable without reading any of it.
// Warm for the payoffs, cool for the passages, grey for "the detector did not claim anything".
// Coloured by the type's CATEGORY rather than by the detector's function, because the lane now shows
// the film's sections and a custom type has no detector function to colour by. Five categories
// instead of thirteen functions is also the more useful grouping at a glance: what a passage is FOR
// reads faster than which label the analyzer reached for.
ImU32 sectionColour(avgen::song::SectionCategory c, bool selected, bool hovered) {
    const avgen::ui::Palette& pal = avgen::ui::palette();
    ImU32 base = pal.panel;
    switch (c) {
    case avgen::song::SectionCategory::Structural: base = pal.accent; break;
    case avgen::song::SectionCategory::Energy:     base = pal.warning; break;
    case avgen::song::SectionCategory::Texture:    base = pal.processing; break;
    case avgen::song::SectionCategory::Cinematic:  base = pal.success; break;
    case avgen::song::SectionCategory::Custom:     base = pal.accentMuted; break;
    }
    return avgen::ui::interactionFill(base, hovered, selected, false);
}

} // namespace

void SequencePanel::draw(app::Engine& engine) {
    // Before anything is drawn, so a finished analysis is on screen in the frame it finished in
    // rather than the one after.
    pollStructureAnalysis(engine);
    // The one automatic trigger: audio arrived, the option is on, and this piece has no structure
    // of its own yet. A project that was saved with a structure is *not* re-analyzed on open --
    // that is the whole point of caching it -- and re-running is an explicit button.
    if (analyzeOnImport_ && work_ == nullptr && engine.track() != nullptr &&
        engine.audioRevision() != structureRevision_ &&
        engine.sequence().sectionTimeline.sections.empty()) {
        startStructureAnalysis(engine, false);
    }
    // A menu item cannot open another popup from inside the first one's body, so it leaves a flag
    // and the request is honoured here, at the top of the frame, before anything is submitted.
    // Before the toolbar, because the toolbar is where that popup's `BeginPopup` lives and a popup
    // opened after its Begin has been submitted does not appear until the frame after.
    if (std::exchange(openAudioClips_, false)) {
        ImGui::OpenPopup("audio-clips");
    }
    drawToolbar(engine);
    ImGui::Separator();
    drawStrip(engine);
    drawLaneZoomGrip();
    drawStripStatus(engine);
    ImGui::Separator();
    drawInspector(engine);
    installIfDirty(engine);
}

// ---- the toolbar -------------------------------------------------------------------------------

void SequencePanel::drawToolbar(app::Engine& engine) {
    seq::Sequence& piece = engine.sequence();

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 4.0f));
    // The toolbar is two columns of two rows each, and both columns read the same way: a line of
    // text that says what you are looking at, then the row of controls that act on it. The right
    // column is built below, after the left one has been drawn and its width is therefore known.
    const float toolbarTop = ImGui::GetCursorPosY();
    float leftEdge = 0.0f;
    const auto widen = [&leftEdge]() {
        leftEdge = std::max(leftEdge, ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x +
                                          ImGui::GetScrollX());
    };
    ImGui::PushItemWidth(160);
    std::strncpy(nameBuffer_, piece.name.c_str(), sizeof(nameBuffer_) - 1);
    nameBuffer_[sizeof(nameBuffer_) - 1] = '\0';
    if (ImGui::InputText("##name", nameBuffer_, sizeof(nameBuffer_))) {
        piece.name = nameBuffer_;
    }
    ImGui::PopItemWidth();
    ImGui::SameLine();
    ImGui::TextDisabled("%s | %zu shot(s), %zu actor(s), %zu cue(s)", clock(piece.duration()).c_str(),
                        piece.shots.size(), piece.actors.size(), piece.overlays.size());
    widen();

    if (ImGui::Button("Add Shot")) {
        // A new shot starts where the piece currently ends, so shots never overlap by accident --
        // which validate() would refuse anyway, loudly, on the next install.
        const double start = piece.shots.empty() ? 0.0 : piece.shots.back().endSeconds();
        seq::Shot shot;
        shot.name = fmt::format("shot {}", piece.shots.size() + 1);
        shot.startSeconds = start;
        shot.durationSeconds = 8.0;
        if (!piece.shots.empty()) {
            shot.scene = piece.shots.back().scene;
        } else if (!piece.scenes.empty()) {
            shot.scene = piece.scenes.front().id;
        }
        // A camera by default, because a shot that inherits one is invisible in the strip's
        // camera lane and reads as a shot that does nothing.
        app::FocalTarget subject;
        subject.radius = 10.0f;
        if (!piece.actors.empty()) {
            subject.position = piece.actors.front().positionAt(start);
            subject.name = piece.actors.front().id;
        }
        shot.camera = seq::cameraFromPreset(seq::CameraPreset::Wide, subject);
        if (!piece.actors.empty()) {
            shot.camera.lookAtActor = piece.actors.front().id;
        }
        piece.shots.push_back(std::move(shot));
        selection_ = Selection::Shot;
        selected_ = static_cast<int>(piece.shots.size()) - 1;
        touch();
    }
    ImGui::SameLine();
    if (ImGui::Button("Add Lyric")) {
        const double at = engine.timelineClock().seconds;
        seq::OverlayCue cue;
        cue.id = fmt::format("cue{:03}", piece.overlays.size() + 1);
        cue.content = "LYRIC";
        cue.style = "lyric";
        cue.startSeconds = at;
        cue.endSeconds = at + 3.0;
        cue.anchor = glm::vec2(0.5f, 0.16f);
        cue.preset = seq::OverlayPreset::FadeInOut;
        cue.order = 10;
        piece.overlays.push_back(std::move(cue));
        selection_ = Selection::Overlay;
        selected_ = static_cast<int>(piece.overlays.size()) - 1;
        touch();
    }
    ImGui::SameLine();
    if (ImGui::Button("Add Actor")) {
        ImGui::OpenPopup("add-actor");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Take over a node in the scene: its transform becomes keyframes and, if it\n"
                          "carries a rig, its animation states become cues.");
    }
    if (ImGui::BeginPopup("add-actor")) {
        // Every node the scene has, so the one thing an actor needs -- a node that exists -- cannot
        // be got wrong by typing.
        scene::Composition* composition = engine.composition();
        if (composition == nullptr) {
            ImGui::TextDisabled("The current scene is not a composition.");
        } else {
            ImGui::TextUnformatted("Drive which node?");
            for (const auto& nodePtr : composition->nodes()) {
                const scene::CompositionNode* n = nodePtr.get();
                if (n == nullptr) {
                    continue;
                }
                const bool taken = std::any_of(piece.actors.begin(), piece.actors.end(),
                                               [&](const seq::Actor& a) { return a.nodeName() == n->name; });
                ImGui::BeginDisabled(taken);
                const std::string label =
                    n->rigs.empty() ? n->name : fmt::format("{}  (rig)", n->name);
                if (ImGui::Selectable(label.c_str())) {
                    seq::Actor actor;
                    actor.id = n->name;
                    actor.node = n->name;
                    // One key where the node already stands, so the actor starts by changing
                    // nothing -- an actor that teleported its node to the origin on creation would
                    // be a feature nobody used twice.
                    actor.keys.push_back(seq::ActorKey{.timeSeconds = 0.0,
                                                       .position = n->transform.position});
                    if (!n->rigs.empty() && n->rigs.front() < engine.scene().rigs.size()) {
                        const auto& states = engine.scene().rigs[n->rigs.front()].player.states();
                        if (!states.empty()) {
                            actor.clips.push_back(seq::ClipCue{.timeSeconds = 0.0,
                                                               .clip = states.front().name});
                        }
                    }
                    piece.actors.push_back(std::move(actor));
                    selection_ = Selection::Actor;
                    selected_ = static_cast<int>(piece.actors.size()) - 1;
                    touch();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndDisabled();
            }
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Add Border")) {
        seq::OverlayCue cue;
        cue.id = "border";
        cue.kind = seq::OverlayKind::Shape;
        cue.content = "rectangle";
        cue.startSeconds = 0.0;
        cue.endSeconds = std::max(piece.duration(), 1.0);
        cue.order = -10; // under the lyrics: spec 26 wants the ordering explicit
        cue.anchor = glm::vec2(0.5f, 0.5f);
        cue.color = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f); // no fill; the stroke is the frame
        cue.extra = nlohmann::json{{"size", nlohmann::json::array({1.70, 0.94})},
                                   {"strokeWidth", 0.004},
                                   {"strokeColor", nlohmann::json::array({1.0, 1.0, 1.0, 0.45})}};
        piece.overlays.push_back(std::move(cue));
        touch();
    }
    ImGui::SameLine();
    const bool busy = work_ != nullptr;
    ImGui::BeginDisabled(engine.track() == nullptr || busy);
    if (ImGui::Button(busy ? "Analyzing..." : "Analyze Song")) {
        // Re-running merges rather than replacing: `seq::reanalyze` puts back what a person moved
        // or named and reports it (ADR-215). Pressing this twice is safe, which is the property
        // that makes it worth having a button at all.
        startStructureAnalysis(engine, !piece.sectionTimeline.sections.empty());
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Find the song's sections and lay them on the timeline, and draw its beats.\n"
                          "Runs in the background. Anything you have edited is kept.\n"
                          "Needs audio.");
    }

    ImGui::SameLine();
    if (ImGui::Button("Import Audio...")) {
        ImGui::OpenPopup("import-audio");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Bring a song in. Also on the File menu, and on O.");
    }

    ImGui::SameLine();
    if (ImGui::Button("Audio...")) {
        ImGui::OpenPopup("audio-clips");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("The files this piece is made of. Several are allowed: they are mixed into "
                          "one, and the strip shows where each one starts.");
    }
    ImGui::SameLine();
    if (ImGui::Button("Import Lyrics...")) {
        if (onImportLyrics) {
            onImportLyrics();
        } else {
            ImGui::OpenPopup("import-lyrics");
        }
    }
    widen();

    // ---- the second column ---------------------------------------------------------------------
    //
    // The strip's own controls -- snap, the two zooms, Fit, Rebuild -- used to sit *under* the
    // strip, and the note at `drawStripControls` records why they were moved there: they cost two
    // rows above a panel that had none to spare, and the shots lane fell below the fold.
    //
    // Beside the buttons they cost **no rows at all**, because the left column is already two rows
    // tall and the space to its right was empty. So the strip gains back the two rows it lost under
    // it as well, and the original complaint is answered more completely than moving them down
    // answered it.
    //
    // The fallback matters: on a narrow window the column will not fit, and a control row that runs
    // off the right edge is worse than one on its own line. Below the width it needs, the block goes
    // back under the buttons -- still above the strip, still two rows, simply stacked.
    const float afterLeft = ImGui::GetCursorPosY();
    const float columnX = leftEdge + ImGui::GetStyle().ItemSpacing.x * 4.0f;
    if (ImGui::GetWindowContentRegionMax().x - columnX >= kStripControlsWidth) {
        ImGui::SetCursorPos(ImVec2(columnX, toolbarTop));
        drawStripControls(engine);
        ImGui::SetCursorPosY(std::max(afterLeft, ImGui::GetCursorPosY()));
    } else {
        drawStripControls(engine);
    }
    ImGui::PopStyleVar();
    if (ImGui::BeginPopup("import-audio")) {
        drawImportPopup(engine);
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("audio-clips")) {
        ImGui::SetNextItemWidth(460.0f);
        drawAudioClips(engine);
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("import-lyrics")) {
        ImGui::TextUnformatted("LRC, SRT or WebVTT");
        char buffer[512];
        std::strncpy(buffer, lyricPath_.c_str(), sizeof(buffer) - 1);
        buffer[sizeof(buffer) - 1] = '\0';
        ImGui::SetNextItemWidth(420);
        if (ImGui::InputText("path", buffer, sizeof(buffer))) {
            lyricPath_ = buffer;
        }
        if (ImGui::Button("Import")) {
            importLyrics(engine, lyricPath_);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

}

// ---- the view's own controls, under the strip (the brief's section 22) ------------------------
//
// These were above the strip, and that is what made the sequencer unusable at the size the editor
// opens at. Measured with `--ui-script strip` on a 1440x900 window: **190 points of toolbar above
// a 272-point panel**, leaving 82 points for a 195-point strip -- the ruler, the sections and the
// waveform fitted and the shots lane did not, so the one gesture the panel exists for could not be
// performed until somebody dragged the divider.
//
// Snap, zoom and the bake's report are all things you consult *about* the strip rather than things
// you reach for before touching it, so they belong under it. The buttons that make something -- add
// a shot, import a song -- stay above, where a toolbar belongs. That is roughly where a timeline's
// zoom control sits in every editor this borrows from anyway.
void SequencePanel::drawStripControls(app::Engine& engine) {
    // Where this block starts, remembered because the second row has to start there too. A new
    // ImGui line begins at the window's own left edge, not at wherever the previous line began, so
    // a two-row block placed in a second column comes apart on its second row unless it is told.
    const float columnX = ImGui::GetCursorPosX();

    // The text row first, then the controls -- the same shape as the column beside it. Aligned to a
    // frame's padding so that this row is as tall as the left column's, whose first row contains an
    // input field; without it the two columns' second rows sit at different heights.
    const seq::InstallReport& report = engine.sequenceReport();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%d track(s), %d key(s), %d layer(s)", report.trackCount, report.keyCount,
                        report.layersRealised);

    ImGui::SetCursorPosX(columnX);
    ImGui::SetNextItemWidth(110);
    ImGui::Combo("snap", &snapMode_, kSnapNames, IM_ARRAYSIZE(kSnapNames));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    ImGui::Combo("sections", &sectionSnap_, kSectionSnapNames, IM_ARRAYSIZE(kSectionSnapNames));
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("What a section boundary snaps to while you drag it.\n"
                          "`free` keeps whatever you land on, to the millisecond.\n"
                          "A snapped boundary takes the beat's own time, not a rounded one.");
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110);
    ImGui::SliderFloat("horizontal zoom", &zoom_, 0.25f, 8.0f, "%.2fx");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("How much of the piece fits across the strip.");
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110);
    // Scales every lane together rather than one of them, so the strip keeps its proportions and a
    // taller waveform does not come at the cost of the lanes beside it.
    ImGui::SliderFloat("vertical zoom", &laneZoom_, 0.5f, 4.0f, "%.2fx");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("How tall the lanes are. Useful for reading the waveform, or for fitting\n"
                          "a long cast on screen at once.");
    }
    ImGui::SameLine();
    if (ImGui::Button("Fit")) {
        zoom_ = 1.0f;
        laneZoom_ = 1.0f;
        view_ = 0.0;
    }
    ImGui::SameLine();
    if (ImGui::Button("Rebuild")) {
        dirty_ = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Bake the sequence onto the timeline again.\n"
                          "Happens on its own after an edit; this is for after a scene change.");
    }

}

// The grip under the lanes: drag it to make them taller.
//
// The slider in the toolbar sets the same number and stays, because a slider is how you get to a
// value you can name. This is how you get to the one that looks right, and it is where the hand
// already is -- at the bottom edge of the thing being resized, which is where every other resize
// handle in every other editor lives.
//
// Down is taller, matching the direction of the edge being pulled. The rate is per point of drag
// rather than per frame, so the result is the same whether the drag took six frames or sixty.
void SequencePanel::drawLaneZoomGrip() {
    const float width = std::max(ImGui::GetContentRegionAvail().x, 40.0f);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("lane-zoom-grip", ImVec2(width, kLaneZoomGripHeight));
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    if (hovered || active) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    }
    if (active) {
        const float dy = ImGui::GetIO().MouseDelta.y;
        if (dy != 0.0f) {
            laneZoom_ = std::clamp(laneZoom_ + dy * kLaneZoomPerPoint, kLaneZoomMin, kLaneZoomMax);
        }
    }

    // Three short rules rather than a bar: a filled bar across the whole panel reads as a divider
    // between two things, and this is a handle on one of them.
    const Palette& pal = palette();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImU32 tint = active ? pal.accent : (hovered ? pal.borderStrong : pal.border);
    const float midY = origin.y + kLaneZoomGripHeight * 0.5f;
    const float midX = origin.x + width * 0.5f;
    for (int i = -1; i <= 1; ++i) {
        const float y = midY + static_cast<float>(i) * 3.0f;
        draw->AddLine(ImVec2(midX - 18.0f, y), ImVec2(midX + 18.0f, y), tint, 1.0f);
    }
    if (hovered || active) {
        ImGui::SetTooltip("Drag down for taller lanes, up for shorter. (%.2fx)",
                          static_cast<double>(laneZoom_));
    }
}

// What the panel has to say for itself: a bake that bound to nothing, an analysis still running, the
// last thing that went wrong. Drawn *under* the strip rather than in the toolbar, because all three
// are transient and a toolbar that changes height as they come and go moves the strip under the
// pointer.
void SequencePanel::drawStripStatus(app::Engine& engine) {
    const seq::InstallReport& report = engine.sequenceReport();
    if (!report.unresolved.empty()) {
        // The one failure that must never be left to the log: a track that binds to nothing
        // evaluates perfectly and changes nothing, forever (ADR-075).
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "%zu target(s) name nothing in this scene",
                           report.unresolved.size());
        if (ImGui::IsItemHovered()) {
            std::string all;
            for (const std::string& target : report.unresolved) {
                all += target + "\n";
            }
            ImGui::SetTooltip("%s", all.c_str());
        }
    }
    if (work_ != nullptr) {
        // The honest progress report job_system.hpp asks for: the stage, because the detector does
        // not report a fraction of itself and a bar invented here would be indistinguishable from
        // a real one.
        app::JobStatus job;
        const bool known = jobs != nullptr && workJob_ != 0 && jobs->status(workJob_, job);
        ImGui::TextColored(ImVec4(0.6f, 0.82f, 0.95f, 1.0f), "analyzing the song%s%s",
                           known && !job.stageName.empty() ? ": " : "...",
                           known && !job.stageName.empty() ? job.stageName.c_str() : "");
    }
    if (!status_.empty()) {
        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.45f, 1.0f), "%s", status_.c_str());
    }
}

void SequencePanel::drawImportPopup(app::Engine& engine) {
    ImGui::TextUnformatted("Import audio");
    ImGui::Separator();
    ImGui::Checkbox("Analyze song structure", &analyzeOnImport_);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Find the sections -- intro, verse, chorus -- and lay them on the\n"
                          "timeline, where you can move and rename them.");
    }
    // **Two systems, and they are not a chain.** Analysis above feeds the Song Director -- sections,
    // shot intents, cameras -- and needs nothing else to produce a first film. What follows is the
    // *performer* side: character actions from the same section boundaries, going to the entity
    // action system and never to a camera.
    //
    // They used to sit together with the second one called "Generate initial Director sequence"
    // (the historical label, kept here only to explain the change),
    // which made a disabled checkbox about characters read as a blocked camera workflow. The
    // separator and the heading are the fix: the Song Director path is complete above this line.
    ImGui::Separator();
    ImGui::TextDisabled("Performers (optional -- not needed for Song Mode)");

    const std::size_t performerRows = engine.sequence().sectionPerformance.entries.size();
    ImGui::BeginDisabled(performerRows == 0);
    ImGui::Checkbox("Generate performer actions", &generateOnImport_);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        if (performerRows == 0) {
            ImGui::SetTooltip(
                "Makes characters move, pose and react on section boundaries.\n\n"
                "This project has no performer rules yet, so there is nothing to generate. Rules\n"
                "say what a kind of section should make happen -- \"on a Drop, the alien reacts\"\n"
                "-- and live in the project's `sectionPerformance`.\n\n"
                "Song Mode does not use these. Analyze above is all the Auto-director needs.");
        } else {
            ImGui::SetTooltip("Makes characters move, pose and react on section boundaries, using\n"
                              "this project's %zu rule(s). Sections whose kind the rules do not\n"
                              "name are skipped, and the panel says which.\n\n"
                              "Independent of Song Mode, which directs cameras rather than cast.",
                              performerRows);
        }
    }
    ImGui::Separator();
    if (ImGui::Button("Choose file...")) {
        if (onOpenAudio) {
            onOpenAudio();
        } else {
            status_ = "no file dialog available in this build";
        }
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", engine.hasAudio()
                                  ? engine.audioPath().filename().string().c_str()
                                  : "nothing loaded (you can also drop a file on the window)");
}

// ---- the strip ---------------------------------------------------------------------------------

void SequencePanel::drawStrip(app::Engine& engine) {
    seq::Sequence& piece = engine.sequence();
    const double duration = std::max({piece.duration(), engine.durationSeconds(), 1.0});
    const Palette& pal = palette();

    const float total = std::max(ImGui::GetContentRegionAvail().x, 160.0f);
    const bool hasAudio = engine.audioFile() != nullptr;
    // One description of where the lanes are, for the height, the drawing and the hit test alike.
    // They were three separate calculations, and when two of them disagreed a click meant to scrub
    // the music moved the music instead (ADR-103); the arithmetic is in `ui_logic.hpp` so it can be
    // checked without a window, and `tests/unit/test_ui_logic.cpp` checks it. The header column is
    // in there for the same reason: it moves where the time axis starts, and both sides ask.
    StripLanes lanes = stripLanesFor(hasAudio, !piece.sectionTimeline.sections.empty(),
                                     piece.actors.size(), !piece.overlays.empty(), laneZoom_);
    // A narrow panel loses the headers rather than losing the music: below about four hundred
    // points a header column costs more of the time axis than the names are worth.
    lanes.gutter = total >= 400.0f ? kGutterWidth : 0.0f;

    // ---- the strip fits the panel, or it is not a strip ---------------------------------------
    //
    // Found by `--ui-script strip`, which is exactly what a scripted arm is for. On a 1440x900
    // window with the default layout, a piece with audio, sections, five shots and two actors laid
    // out a 195-point strip into 82 points of visible panel: the ruler, the section lane and the
    // waveform fitted, and **the shots lane did not**. The arm's press on a shot landed on clipped
    // geometry, ImGui reported the strip as not hovered, and nothing happened -- which is precisely
    // what a person dragging a shot at the default window size would have experienced.
    //
    // The panel scrolls, so the lanes were reachable; they were simply not *there* until somebody
    // discovered that the sequencer needed its divider dragged before it could be used. A tool
    // whose main surface is below the fold at the size it opens at is not finished.
    //
    // So the lanes compress to fit what there is, down to a floor. Below the floor the panel's own
    // scrollbar takes over again, because a two-point lane is not a smaller lane, it is a line.
    // Everything stays proportional, and `StripLanes` still answers for the geometry -- the heights
    // it is given are simply smaller, so the drawing and the hit testing agree as they always did.
    const float available = ImGui::GetContentRegionAvail().y - kStripBottomReserve;
    if (const float wanted = lanes.height(); wanted > available && available > 0.0f) {
        const float scale = std::max(available / wanted, kMinLaneScale);
        lanes.rulerHeight *= scale;
        lanes.markerHeight *= scale;
        lanes.laneHeight *= scale;
        lanes.audioLaneHeight *= scale;
        lanes.sectionLaneHeight *= scale;
    }
    const float height = lanes.height();
    const float width = std::max(total - lanes.gutter, 80.0f);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float axisX = origin.x + lanes.gutter;
    ImGui::InvisibleButton("strip", ImVec2(total, height),
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    // Time axis. `view_` is the leftmost second; `zoom_` how many screens the piece takes.
    const double span = duration / static_cast<double>(std::max(zoom_, 0.01f));

    // ---- the view follows a seek ---------------------------------------------------------------
    //
    // Return sends the playhead to 0:00. Zoomed in at 2:14 that used to leave the strip exactly
    // where it was, showing two minutes of timeline with no playhead anywhere in it, and the only
    // way back was to drag -- so the one key whose whole job is "go to the start" did not take you
    // there.
    //
    // A *seek*, not playback. Time advancing by about a frame is the transport doing its job and
    // must not drag the strip around under a pointer that is mid-edit; time jumping is somebody
    // asking to be somewhere else. `kSeekJumpSeconds` is a generous several frames, so a stutter or
    // a slow frame is never mistaken for a seek.
    //
    // And only when the playhead is *off screen*. A seek to somewhere already visible leaves the
    // view alone, because scrolling under a person who can already see where they landed is motion
    // for its own sake.
    //
    // Zoom is untouched by all of this -- the request was to follow the playhead, not to reframe.
    {
        const double now = engine.timelineClock().seconds;
        const bool seeked = std::abs(now - lastClock_) > kSeekJumpSeconds;
        lastClock_ = now;
        if (seeked && (now < view_ || now > view_ + span)) {
            // Landed a little in from the left edge rather than hard against it, so what comes
            // immediately before the new position is visible too -- which is what you want after
            // seeking to a section boundary or a cut.
            view_ = now - span * kSeekLead;
        }
    }

    view_ = std::clamp(view_, 0.0, std::max(0.0, duration - span));
    const auto toX = [&](double seconds) {
        return axisX + static_cast<float>((seconds - view_) / span) * width;
    };
    const auto toTime = [&](float x) {
        return view_ + static_cast<double>((x - axisX) / width) * span;
    };
    const float axisRight = axisX + width;

    draw->AddRectFilled(origin, ImVec2(origin.x + total, origin.y + height), pal.ground, 4.0f);

    stripRect_ = StripRect{.x = origin.x,
                           .y = origin.y,
                           .width = total,
                           .height = height,
                           .gutter = lanes.gutter,
                           .shotsTop = lanes.shotsTop(),
                           .rulerHeight = kRulerHeight,
                           .hovered = hovered,
                           .visibleHeight = std::min(height, ImGui::GetWindowPos().y +
                                                                 ImGui::GetWindowSize().y - origin.y),
                           .toolbarHeight = origin.y - (ImGui::GetWindowPos().y - ImGui::GetScrollY())};

    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const float localX = mouse.x - origin.x;
    const float localY = mouse.y - origin.y;
    const StripLane hoverLane = hovered ? lanes.at(localY) : StripLane::None;
    const bool overGutter = hovered && lanes.inGutter(localX);
    const bool overAxis = hovered && !overGutter;
    const double mouseTime = toTime(mouse.x);

    // Hit-tested once, while the lanes are drawn, and then used three times: for the hover
    // treatment, for the cursor, and for what a right-click's menu is about. They were three
    // separate tests, which is the shape ADR-103 records as having produced a click that scrubbed
    // and moved the audio at the same time.
    ImGuiMouseCursor wantCursor = ImGuiMouseCursor_Arrow;
    int hoverBlock = -1;    // index within whichever lane the pointer is over
    int hoverActorRow = -1; // which actor lane, when the pointer is over one

    // ---- lane backgrounds and the header column (the brief's section 12) ------------------------
    //
    // Structure rather than borders. Each lane gets a surface a shade off the strip's own and the
    // gaps between them stay the ground colour, so the rows separate without a grid drawn over the
    // music. A border per lane was tried and it reads as a spreadsheet.
    const auto laneBackground = [&](float top, float h, bool emphasised) {
        draw->AddRectFilled(ImVec2(axisX, origin.y + top), ImVec2(axisRight, origin.y + top + h),
                            emphasised ? mixColour(pal.lane, pal.panel, 0.45f) : pal.lane, 2.0f);
    };
    // The header itself. `accentLine` marks the lane the selection is in, which is the one piece of
    // state a header carries that the lane cannot show on its own when it happens to be empty.
    const auto laneHeader = [&](float top, float h, const char* label, bool active) {
        if (lanes.gutter <= 0.0f) {
            return std::pair<ImVec2, ImVec2>{ImVec2(0, 0), ImVec2(0, 0)};
        }
        const ImVec2 a(origin.x, origin.y + top);
        const ImVec2 b(origin.x + lanes.gutter - 1.0f, origin.y + top + h);
        const bool over = hovered && overGutter && localY >= top && localY < top + h;
        draw->AddRectFilled(a, b, interactionFill(pal.panel, over, active, false), 2.0f);
        if (active) {
            // A three-point bar rather than a fill: the header's job is to name the lane, and a
            // header that lights up as strongly as a selected clip competes with the clip.
            draw->AddRectFilled(a, ImVec2(a.x + 3.0f, b.y), pal.accent, 1.0f);
        }
        draw->PushClipRect(ImVec2(a.x + 7.0f, a.y), ImVec2(b.x - 4.0f, b.y), true);
        draw->AddText(ImVec2(a.x + 9.0f, a.y + (h - ImGui::GetTextLineHeight()) * 0.5f),
                      active ? pal.text : pal.textMuted, label);
        draw->PopClipRect();
        return std::pair<ImVec2, ImVec2>{a, b};
    };

    // ---- the ruler (the brief's section 11) -----------------------------------------------------
    //
    // Two tick sizes, because one is a row of scratches. The major ticks carry the labels and run
    // the full height of the strip as a hairline, so a boundary can be read against them; the minor
    // ticks live in the ruler only. Which two is `rulerTicks`, over a 1-2-5 ladder, so the labels
    // never collide and the minors never close into a grey band -- both of which the single fixed
    // ladder here before did at the ends of its zoom range.
    draw->AddRectFilled(ImVec2(axisX, origin.y), ImVec2(axisRight, origin.y + kRulerHeight + kMarkerHeight),
                        mixColour(pal.ground, pal.panel, 0.7f), 0.0f);
    const RulerTicks ticks = rulerTicks(span, width);
    if (ticks.minor > 0.0) {
        for (double t = std::floor(view_ / ticks.minor) * ticks.minor; t <= view_ + span; t += ticks.minor) {
            const float x = toX(t);
            if (x < axisX || x > axisRight) {
                continue;
            }
            draw->AddLine(ImVec2(x, origin.y + kRulerHeight - 4.0f), ImVec2(x, origin.y + kRulerHeight),
                          pal.border);
        }
    }
    for (double t = std::floor(view_ / ticks.major) * ticks.major; t <= view_ + span; t += ticks.major) {
        const float x = toX(t);
        if (x < axisX - 1.0f || x > axisRight) {
            continue;
        }
        draw->AddLine(ImVec2(x, origin.y + kRulerHeight - 8.0f), ImVec2(x, origin.y + kRulerHeight),
                      pal.textMuted);
        // The hairline through the lanes, faint enough to read a clip over.
        draw->AddLine(ImVec2(x, origin.y + lanes.lanesTop()), ImVec2(x, origin.y + height),
                      mixColour(pal.ground, pal.border, 0.45f));
        char label[24];
        // Sub-second divisions get a decimal, because "0:03" three times running is not a ruler.
        if (ticks.major < 1.0) {
            std::snprintf(label, sizeof(label), "%d:%05.2f", static_cast<int>(t / 60.0),
                          std::fmod(t, 60.0));
        } else {
            std::snprintf(label, sizeof(label), "%d:%02d", static_cast<int>(t / 60.0),
                          static_cast<int>(std::fmod(t, 60.0)));
        }
        draw->AddText(ImVec2(x + 4.0f, origin.y + 3.0f), pal.textMuted, label);
    }
    if (lanes.gutter > 0.0f) {
        // The ruler's own header names the unit, so nobody has to infer it from the labels.
        draw->AddRectFilled(origin, ImVec2(origin.x + lanes.gutter - 1.0f, origin.y + kRulerHeight + kMarkerHeight),
                            pal.panel, 2.0f);
        draw->AddText(ImVec2(origin.x + 9.0f, origin.y + 3.0f), pal.textMuted, "min:sec");
    }

    // ---- beats and markers (spec 19, 20) ----
    const float markerTop = origin.y + kRulerHeight;
    const std::vector<double>& beatTimes = beats(engine);
    if (!beatTimes.empty()) {
        // Only draw beats when they are far enough apart to read as beats rather than as a fill.
        const double firstGap = beatTimes.size() > 1 ? beatTimes[1] - beatTimes[0] : 1.0;
        if (firstGap / span * static_cast<double>(width) >= 3.0) {
            for (const double beat : beatTimes) {
                const float x = toX(beat);
                if (x < axisX || x > axisRight) {
                    continue;
                }
                draw->AddLine(ImVec2(x, markerTop + kMarkerHeight - 5.0f),
                              ImVec2(x, markerTop + kMarkerHeight), mixColour(pal.lane, pal.accent, 0.5f));
            }
        }
    }
    for (const seq::Marker& marker : piece.markers) {
        if (marker.kind != seq::MarkerKind::Section && marker.kind != seq::MarkerKind::Cue) {
            continue;
        }
        // Section markers are derived from the structure, and when the structure has a lane of its
        // own that lane already draws every one of them with a name and a boundary line. Drawing
        // both is the same information twice, in two places, with the labels overlapping.
        if (marker.kind == seq::MarkerKind::Section && lanes.hasSections) {
            continue;
        }
        const float x = toX(marker.timeSeconds);
        if (x < axisX - 40.0f || x > axisRight) {
            continue;
        }
        const ImU32 colour = marker.kind == seq::MarkerKind::Section ? pal.success : pal.warning;
        draw->AddLine(ImVec2(x, markerTop), ImVec2(x, origin.y + height), colour);
        draw->AddText(ImVec2(x + 3.0f, markerTop), colour, marker.name.c_str());
    }

    // ---- the section lane (ADR-216) ----
    //
    // The song's own shape, directly under the ruler. Each section is a block; the line between two
    // blocks is the boundary and is what a drag grabs. A boundary is drawn as a full-height line
    // through the whole strip because that is what it is for: seeing whether a cut lands on one.
    // ADR-247: the FILM's sections, not the detector's report.
    //
    // This lane edited `piece.structure` until now -- the analysis -- while everything downstream
    // cut from `piece.sectionTimeline`. So dragging a boundary here moved a number nothing read:
    // the lane looked authoritative and was decorative. The two models are deliberately separate
    // (a detection is evidence, a timeline is what a person decided), and the lane belongs to the
    // one a person is deciding in.
    const auto& sections = piece.sectionTimeline.sections;
    if (!sections.empty()) {
        const float top = origin.y + lanes.sectionsTop();
        const float bottom = top + lanes.sectionLaneHeight;
        laneBackground(lanes.sectionsTop(), lanes.sectionLaneHeight, false);
        laneHeader(lanes.sectionsTop(), lanes.sectionLaneHeight, "Sections",
                   selection_ == Selection::Section);
        for (std::size_t i = 0; i < sections.size(); ++i) {
            const song::Section& section = sections[i];
            ImVec2 a(toX(section.startSeconds), top);
            ImVec2 b(toX(section.endSeconds), bottom);
            if (b.x < axisX || a.x > axisRight) {
                continue;
            }
            a.x = std::max(a.x, axisX);
            b.x = std::min(b.x, axisRight);
            const bool isSelected = selection_ == Selection::Section && selected_ == static_cast<int>(i);
            const bool isHovered = hoverLane == StripLane::Sections && overAxis && mouse.x >= a.x &&
                                   mouse.x <= b.x;
            if (isHovered) {
                hoverBlock = static_cast<int>(i);
            }
            const song::SectionType* type = piece.shotLanguage.type(section.type);
            draw->AddRectFilled(
                a, b,
                sectionColour(type != nullptr ? type->category : song::SectionCategory::Custom,
                              isSelected, isHovered),
                2.0f);
            if (section.authored || section.edited != song::SectionField::None) {
                // A person's section is marked, because whether the analyzer or a person decided a
                // boundary is the single most useful thing to know before pressing Analyze again.
                draw->AddRect(ImVec2(a.x + 1.0f, a.y + 1.0f), ImVec2(b.x - 1.0f, b.y - 1.0f),
                              pal.warning, 2.0f);
            }
            if (b.x - a.x > 26.0f) {
                draw->PushClipRect(a, ImVec2(b.x - 3.0f, b.y), true);
                draw->AddText(ImVec2(a.x + 5.0f, a.y + 3.0f), pal.text,
                              piece.shotLanguage.displayName(section).c_str());
                draw->PopClipRect();
            }
        }
        // The boundaries, over the blocks and down the whole strip.
        for (std::size_t i = 1; i < sections.size(); ++i) {
            const float x = toX(sections[i].startSeconds);
            if (x < axisX || x > axisRight) {
                continue;
            }
            const bool dragging = drag_ == Drag::SectionBoundary && dragIndex_ == static_cast<int>(i);
            const bool nearPointer = overAxis && hoverLane == StripLane::Sections &&
                                     std::fabs(mouse.x - x) <= kEdgeGrab;
            if (nearPointer) {
                // A boundary is the one thing in this lane that resizes, and it beats the block
                // underneath it for the cursor as well as for the click.
                wantCursor = ImGuiMouseCursor_ResizeEW;
            }
            draw->AddLine(ImVec2(x, top), ImVec2(x, origin.y + height),
                          dragging || nearPointer ? pal.warning : pal.borderStrong,
                          dragging || nearPointer ? 2.0f : 1.0f);
        }
    }

    // ---- the audio lane ----
    //
    // First, above the shots, because the song is what everything below it is cut to: a shot
    // boundary that does not land on anything in the waveform is the thing an author most needs to
    // be able to see, and it is only visible when the two are adjacent.
    //
    // **A clip is a box and its waveform is drawn inside it.** That is what an audio clip looks like
    // in every tool that has one, and both of the ways this was got wrong came from separating them:
    // clips drawn as blocks *on* a full-width waveform stole the click that scrubs, and clips moved
    // to a lane of their own left the waveform floating above the box it belongs to.
    float laneY = origin.y + lanes.audioTop();
    if (hasAudio) {
        const audio::WaveformSummary& wave = waveform(engine);
        laneBackground(lanes.audioTop(), lanes.audioLaneHeight, false);
        laneHeader(lanes.audioTop(), lanes.audioLaneHeight, "Audio", audioSelected_ >= 0);
        const float mid = laneY + lanes.audioLaneHeight * 0.5f;
        const float halfHeight = lanes.audioLaneHeight * 0.5f - 3.0f;
        const double perPixel = span / static_cast<double>(width);

        for (std::size_t i = 0; i < engine.audioClips().size(); ++i) {
            const audio::AudioClip& clip = engine.audioClips()[i];
            const double end = audio::clipEndSeconds(clip, engine.clipSource(clip.file).get());
            const ImVec2 lo(std::max(toX(clip.startSeconds), axisX), laneY);
            const ImVec2 hi(std::min(toX(end), axisRight), laneY + lanes.audioLaneHeight);
            if (hi.x <= lo.x) {
                continue;
            }
            const bool chosen = isChosen(Selection::Clip, static_cast<int>(i));
            const bool over = hoverLane == StripLane::Audio && overAxis && mouse.x >= lo.x &&
                              mouse.x <= hi.x;
            if (over) {
                hoverBlock = static_cast<int>(i);
            }
            // The clip's body is not draggable (see the interaction block below), so it gets the
            // hover *lift* that says "this one is under the pointer" and none of the grip treatment
            // the shots get. Showing a resize edge on something that cannot be resized is the
            // clearest possible way to lie about an affordance.
            const ImU32 body = clip.enabled ? mixColour(pal.lane, pal.accentMuted, 0.42f)
                                            : mixColour(pal.lane, pal.panel, 0.5f);
            draw->AddRectFilled(lo, hi, interactionFill(body, over, chosen, false), 3.0f);

            // The waveform, inside the box and clipped to it.
            draw->PushClipRect(lo, hi, true);
            const ImU32 ink = clip.enabled ? mixColour(pal.accent, pal.text, 0.25f)
                                           : pal.textDisabled;
            for (int px = static_cast<int>(lo.x); px <= static_cast<int>(hi.x); ++px) {
                const double from = toTime(static_cast<float>(px));
                const auto [low, high] = wave.peak(from, from + perPixel);
                if (low == 0.0f && high == 0.0f) {
                    continue;
                }
                const float x = static_cast<float>(px) + 0.5f;
                // Clamped rather than scaled by the peak: a waveform whose height depends on the
                // loudest moment in view changes shape as you scroll, which makes it useless for
                // finding a moment again.
                const float top = mid - std::min(high, 1.0f) * halfHeight;
                const float bottom = mid - std::max(low, -1.0f) * halfHeight;
                draw->AddLine(ImVec2(x, top), ImVec2(x, std::max(bottom, top + 1.0f)), ink);
            }
            draw->AddLine(ImVec2(lo.x, mid), ImVec2(hi.x, mid), mixColour(pal.lane, pal.text, 0.18f));
            const std::string clipLabel = clip.name.empty() ? clip.file.filename().string() : clip.name;
            draw->AddText(ImVec2(lo.x + 5.0f, lo.y + 2.0f),
                          clip.enabled ? pal.text : pal.textDisabled, clipLabel.c_str());
            if (!clip.enabled) {
                draw->AddText(ImVec2(lo.x + 5.0f, lo.y + 2.0f + ImGui::GetTextLineHeight()),
                              pal.textDisabled, "muted");
            }
            draw->PopClipRect();

            if (chosen) {
                // **Yellow, and thicker.** The shared `interactionOutline` accent is a blue that
                // reads well against the shot lane's blues and disappears against the waveform,
                // which is the same blue family -- a selected clip was indistinguishable from an
                // unselected one. This is the one lane whose content is a picture rather than a
                // label, so its selection has to be a colour the picture does not contain.
                draw->AddRect(lo, hi, kClipSelected, 3.0f, 0, 2.5f);
            } else if (const ImU32 outline = interactionOutline(over, false, false); outline != 0) {
                draw->AddRect(lo, hi, outline, 3.0f, 0, 1.0f);
            } else {
                draw->AddRect(lo, hi, pal.border, 3.0f);
            }
        }
    }
    laneY = origin.y + lanes.shotsTop();

    // ---- shots (spec 29) ----
    //
    // The one lane whose blocks move and resize, so it is the one lane that has to *say* so. The
    // grips are drawn only under the pointer: two vertical pips on every shot edge at rest would be
    // a picket fence, and the affordance is needed at the moment somebody reaches for it.
    laneBackground(lanes.shotsTop(), lanes.laneHeight, true);
    laneHeader(lanes.shotsTop(), lanes.laneHeight, "Shots", selection_ == Selection::Shot);
    const double now = engine.timelineClock().seconds;
    for (std::size_t i = 0; i < piece.shots.size(); ++i) {
        const seq::Shot& shot = piece.shots[i];
        ImVec2 a(toX(shot.startSeconds), laneY);
        ImVec2 b(toX(shot.endSeconds()), laneY + lanes.laneHeight);
        if (b.x < axisX || a.x > axisRight) {
            continue;
        }
        const float trueLeft = a.x;
        const float trueRight = b.x;
        a.x = std::max(a.x, axisX);
        b.x = std::min(b.x, axisRight);
        const bool isSelected = isChosen(Selection::Shot, static_cast<int>(i));
        const bool dragging = dragIndex_ == static_cast<int>(i) &&
                              (drag_ == Drag::MoveShot || drag_ == Drag::TrimShotStart ||
                               drag_ == Drag::TrimShotEnd);
        const BlockZone zone = (hoverLane == StripLane::Shots && overAxis)
                                   ? blockZoneAt(mouse.x, trueLeft, trueRight, kEdgeGrab)
                                   : BlockZone::None;
        const bool over = zone != BlockZone::None;
        if (over) {
            hoverBlock = static_cast<int>(i);
            wantCursor = cursorForBlockZone(zone);
        }
        // A shot that is on screen right now: a wash, not an outline, so it does not compete with
        // the selection.
        const bool playing = now >= shot.startSeconds && now < shot.endSeconds();
        draw->AddRectFilled(a, b, interactionFill(shotColour(static_cast<int>(i)), over, isSelected,
                                                  dragging), 3.0f);
        if (playing) {
            draw->AddRectFilled(a, b, pal.playing, 3.0f);
        }
        if (b.x - a.x > 24.0f) {
            draw->PushClipRect(a, ImVec2(b.x - 3.0f, b.y), true);
            draw->AddText(ImVec2(a.x + 6.0f, a.y + 4.0f), pal.text, shot.name.c_str());
            draw->PopClipRect();
        }
        // A fade is drawn where it happens, so a dip to black is visible without opening anything.
        if (shot.in.kind == seq::TransitionKind::FadeIn && shot.in.seconds > 0.0) {
            draw->AddRectFilled(a, ImVec2(toX(shot.startSeconds + shot.in.seconds), b.y),
                                IM_COL32(0, 0, 0, 140), 3.0f);
        }
        if (shot.out.kind == seq::TransitionKind::FadeOut && shot.out.seconds > 0.0) {
            draw->AddRectFilled(ImVec2(toX(shot.endSeconds() - shot.out.seconds), a.y), b,
                                IM_COL32(0, 0, 0, 140), 3.0f);
        }
        drawEdgeGrip(draw, a, b, zone, dragging ? drag_ : Drag::None, true);
        if (const ImU32 outline = interactionOutline(over, isSelected, dragging); outline != 0) {
            draw->AddRect(a, b, outline, 3.0f, 0, (isSelected || dragging) ? 2.0f : 1.0f);
        } else {
            draw->AddRect(a, b, pal.border, 3.0f);
        }
    }
    laneY = origin.y + lanes.actorsTop();

    // ---- one lane per actor, showing its clip cues (spec 13) ----
    for (std::size_t ai = 0; ai < piece.actors.size(); ++ai) {
        const seq::Actor& actor = piece.actors[ai];
        const float laneTop = lanes.actorsTop() + static_cast<float>(ai) * (lanes.laneHeight + lanes.gap);
        const bool isSelected = isChosen(Selection::Actor, static_cast<int>(ai));
        laneBackground(laneTop, lanes.laneHeight, ai % 2 == 1);
        // The actor's own header: its id, and a dot that says whether it is visible. `Actor::visible`
        // is real state the bake reads, so the dot is a control rather than a decoration -- which is
        // why it is here and why there is no mute dot on the shots lane, where there is nothing for
        // one to mean.
        if (hovered && localY >= laneTop && localY < laneTop + lanes.laneHeight && hoverActorRow < 0) {
            hoverActorRow = static_cast<int>(ai);
        }
        const auto [ha, hb] = laneHeader(laneTop, lanes.laneHeight, actor.id.c_str(), isSelected);
        if (lanes.gutter > 0.0f) {
            const ImVec2 dot(hb.x - 12.0f, (ha.y + hb.y) * 0.5f);
            draw->AddCircleFilled(dot, 4.0f, actor.visible ? pal.success : pal.textDisabled, 12);
            if (!actor.visible) {
                draw->AddCircle(dot, 4.0f, pal.textMuted, 12);
            }
        }
        const double endOfPiece = piece.duration();
        for (std::size_t c = 0; c < actor.clips.size(); ++c) {
            const seq::ClipCue& cue = actor.clips[c];
            const double to = c + 1 < actor.clips.size() ? actor.clips[c + 1].timeSeconds : endOfPiece;
            if (cue.clip.empty() || to <= cue.timeSeconds) {
                continue;
            }
            ImVec2 a(toX(cue.timeSeconds), origin.y + laneTop);
            ImVec2 b(toX(to), origin.y + laneTop + lanes.laneHeight);
            if (b.x < axisX || a.x > axisRight) {
                continue;
            }
            a.x = std::max(a.x, axisX);
            b.x = std::min(b.x, axisRight);
            const bool over = overAxis && hoverLane == StripLane::Actors && mouse.x >= a.x &&
                              mouse.x <= b.x && localY >= laneTop && localY < laneTop + lanes.laneHeight;
            if (over) {
                hoverBlock = static_cast<int>(c);
                hoverActorRow = static_cast<int>(ai);
            }
            draw->AddRectFilled(a, b,
                                interactionFill(mixColour(pal.lane, pal.success, 0.34f), over,
                                                isSelected, false),
                                3.0f);
            if (b.x - a.x > 22.0f) {
                draw->PushClipRect(a, ImVec2(b.x - 3.0f, b.y), true);
                draw->AddText(ImVec2(a.x + 6.0f, a.y + 4.0f), pal.text, cue.clip.c_str());
                draw->PopClipRect();
            }
            if (const ImU32 outline = interactionOutline(over, isSelected, false); outline != 0) {
                draw->AddRect(a, b, outline, 3.0f);
            }
        }
    }

    // ---- the overlay lane (spec 23, 26) ----
    if (!piece.overlays.empty()) {
        laneBackground(lanes.overlaysTop(), lanes.laneHeight, false);
        laneHeader(lanes.overlaysTop(), lanes.laneHeight, "Overlays", selection_ == Selection::Overlay);
        for (std::size_t i = 0; i < piece.overlays.size(); ++i) {
            const seq::OverlayCue& cue = piece.overlays[i];
            ImVec2 a(toX(cue.startSeconds), origin.y + lanes.overlaysTop());
            ImVec2 b(toX(cue.endSeconds), origin.y + lanes.overlaysTop() + lanes.laneHeight);
            if (b.x < axisX || a.x > axisRight) {
                continue;
            }
            const float trueLeft = a.x;
            const float trueRight = b.x;
            a.x = std::max(a.x, axisX);
            b.x = std::min(b.x, axisRight);
            const bool isSelected = isChosen(Selection::Overlay, static_cast<int>(i));
            const bool dragging = dragIndex_ == static_cast<int>(i) &&
                                  (drag_ == Drag::MoveOverlay || drag_ == Drag::TrimOverlayStart ||
                                   drag_ == Drag::TrimOverlayEnd);
            const BlockZone zone = (hoverLane == StripLane::Overlays && overAxis)
                                       ? blockZoneAt(mouse.x, trueLeft, trueRight, kEdgeGrab)
                                       : BlockZone::None;
            const bool over = zone != BlockZone::None;
            if (over) {
                hoverBlock = static_cast<int>(i);
                wantCursor = cursorForBlockZone(zone);
            }
            const ImU32 base = cue.kind == seq::OverlayKind::Shape
                                   ? mixColour(pal.lane, pal.textMuted, 0.3f)
                                   : mixColour(pal.lane, IM_COL32(150, 120, 190, 255), 0.42f);
            draw->AddRectFilled(a, b, interactionFill(base, over, isSelected, dragging), 3.0f);
            if (b.x - a.x > 22.0f) {
                draw->PushClipRect(a, ImVec2(b.x - 3.0f, b.y), true);
                draw->AddText(ImVec2(a.x + 6.0f, a.y + 4.0f), pal.text, cue.content.c_str());
                draw->PopClipRect();
            }
            drawEdgeGrip(draw, a, b, zone, dragging ? drag_ : Drag::None, false);
            if (const ImU32 outline = interactionOutline(over, isSelected, dragging); outline != 0) {
                draw->AddRect(a, b, outline, 3.0f, 0, (isSelected || dragging) ? 2.0f : 1.0f);
            }
        }
    }

    // ---- snapping feedback (the brief's section 7) ----------------------------------------------
    //
    // While a block is being dragged with a grid on, the grid line it is about to land on is drawn
    // bright. Without it a snap is invisible until it has already happened, and a person cannot
    // tell a snap from a hand that happened to be steady.
    if (drag_ != Drag::None && snapMode_ != 0) {
        const double target = drag_ == Drag::SectionBoundary ? snapSection(engine, mouseTime)
                                                             : snap(engine, mouseTime);
        const float x = toX(target);
        if (x >= axisX && x <= axisRight) {
            draw->AddLine(ImVec2(x, origin.y + lanes.lanesTop()), ImVec2(x, origin.y + height),
                          pal.warning, 1.0f);
        }
    }

    // ---- the rubber band -------------------------------------------------------------------------
    //
    // Drawn over the lanes and under the playhead: it is a transient thing about the lanes, and the
    // playhead is the one mark that should never be obscured.
    if (drag_ == Drag::Marquee) {
        const ImVec2 a(origin.x + std::min(marqueeFromX_, marqueeToX_),
                       origin.y + std::min(marqueeFromY_, marqueeToY_));
        const ImVec2 b(origin.x + std::max(marqueeFromX_, marqueeToX_),
                       origin.y + std::max(marqueeFromY_, marqueeToY_));
        // Half transparent, so what the band is over stays readable while it is being dragged --
        // the point of the gesture is seeing what you are about to catch.
        draw->AddRectFilled(a, b, withAlpha(pal.accent, 0.25f), 2.0f);
        draw->AddRect(a, b, withAlpha(pal.accent, 0.9f), 2.0f, 0, 1.0f);
    }

    // ---- the playhead (spec 30, and the brief's section 10) --------------------------------------
    //
    // A line was all it was, and a line is what every other vertical mark on this strip also is: a
    // section boundary, a marker, a major tick, the snap guide. The handle is what makes it the
    // playhead rather than one more of those -- a shape at the top, in the one warm hue nothing
    // else on the strip uses, wide enough to grab.
    const float playX = toX(now);
    if (playX >= axisX - kPlayheadHalfWidth && playX <= axisRight + kPlayheadHalfWidth) {
        const float clampedX = std::clamp(playX, axisX, axisRight);
        draw->PushClipRect(ImVec2(axisX, origin.y), ImVec2(axisRight, origin.y + height), true);
        // The line: a dark under-stroke first, so it stays legible over a pale section block as
        // well as over the dark ground.
        draw->AddLine(ImVec2(clampedX, origin.y + kRulerHeight), ImVec2(clampedX, origin.y + height),
                      IM_COL32(0, 0, 0, 120), 3.0f);
        draw->AddLine(ImVec2(clampedX, origin.y + kRulerHeight), ImVec2(clampedX, origin.y + height),
                      pal.playhead, 1.0f);
        // The handle: a flat-topped teardrop sitting in the ruler.
        const float top = origin.y + 2.0f;
        const float shoulder = top + kPlayheadHandleHeight * 0.62f;
        const float tip = top + kPlayheadHandleHeight;
        const bool grabbing = drag_ == Drag::Playhead;
        const ImU32 handle = grabbing ? mixColour(pal.playhead, pal.text, 0.4f) : pal.playhead;
        draw->PathClear();
        draw->PathLineTo(ImVec2(clampedX - kPlayheadHalfWidth, top));
        draw->PathLineTo(ImVec2(clampedX + kPlayheadHalfWidth, top));
        draw->PathLineTo(ImVec2(clampedX + kPlayheadHalfWidth, shoulder));
        draw->PathLineTo(ImVec2(clampedX, tip));
        draw->PathLineTo(ImVec2(clampedX - kPlayheadHalfWidth, shoulder));
        draw->PathFillConvex(handle);
        draw->PathClear();
        draw->PathLineTo(ImVec2(clampedX - kPlayheadHalfWidth, top));
        draw->PathLineTo(ImVec2(clampedX + kPlayheadHalfWidth, top));
        draw->PathLineTo(ImVec2(clampedX + kPlayheadHalfWidth, shoulder));
        draw->PathLineTo(ImVec2(clampedX, tip));
        draw->PathLineTo(ImVec2(clampedX - kPlayheadHalfWidth, shoulder));
        draw->PathStroke(IM_COL32(0, 0, 0, 90), ImDrawFlags_Closed, 1.0f);
        draw->PopClipRect();
    }

    // The hover hairline: where a click on the ruler would put the playhead, *after* snapping, so
    // the grid is visible before it is committed to rather than discovered afterwards.
    if (overAxis && hoverLane == StripLane::Ruler && drag_ == Drag::None) {
        const float x = toX(snap(engine, mouseTime));
        if (x >= axisX && x <= axisRight) {
            draw->AddLine(ImVec2(x, origin.y + kRulerHeight), ImVec2(x, origin.y + height),
                          mixColour(pal.ground, pal.playhead, 0.45f));
        }
    }

    // ---- cursors (the brief's section 14) ---------------------------------------------------------
    //
    // Said once, from the hover state the drawing above already worked out, rather than by each
    // lane on its own. The pointer is the only part of the interface that is always visible, and
    // until this pass it said "arrow" over every draggable block in the application.
    // The ruler and the playhead: both move the playhead along one axis, so both say so.
    if (overAxis && hoverLane == StripLane::Ruler) {
        wantCursor = ImGuiMouseCursor_ResizeEW;
    }
    // A header is a thing to click, not a thing to drag.
    if (overGutter) {
        wantCursor = ImGuiMouseCursor_Hand;
    }
    // A drag in progress outranks whatever the pointer is over now: a gesture that began on a clip
    // edge is still a resize after the pointer has left the clip behind, and the cursor flickering
    // back to an arrow mid-drag is the tell that an editor has lost track of what it is doing.
    switch (drag_) {
    case Drag::TrimShotStart:
    case Drag::TrimShotEnd:
    case Drag::TrimOverlayStart:
    case Drag::TrimOverlayEnd:
    case Drag::SectionBoundary:
    case Drag::Playhead:
        wantCursor = ImGuiMouseCursor_ResizeEW;
        break;
    case Drag::MoveShot:
    case Drag::MoveOverlay:
        wantCursor = ImGuiMouseCursor_ResizeAll;
        break;
    case Drag::None:
        break;
    }
    if (hovered || drag_ != Drag::None) {
        setHoverCursor(wantCursor);
    }

    // ---- interaction ----
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        // Which lane was hit decides what the click means. The ruler, the marker row and the
        // **waveform** are always a scrub, so there is one place on the strip that is guaranteed not
        // to grab a block. The waveform holds no blocks and is deliberately left that way: clicking
        // a moment in the music to hear it is worth more than anything a block there could offer.
        drag_ = Drag::None;
        dragIndex_ = -1;
        bool hitBlock = false;
        // Did the press land on something selectable? Separate from `hitBlock`, which means "this
        // press was consumed". The audio lane sets one and not the other on purpose: clicking a clip
        // selects it *and* still scrubs, because this lane's promise is that clicking a moment in
        // the music plays it. Conflating the two is what made a clip click select a clip and then
        // immediately start a rubber band over the top of it, clearing what it had just selected.
        bool selectedSomething = false;
        // **A press replaces the selection.** Cleared here, once, rather than in each of the eight
        // branches below that set `selection_` -- a click on a shot is a single selection whether or
        // not a rubber band was up a moment ago, and an invariant enforced in one place cannot be
        // forgotten by the ninth branch somebody adds later. The band's own branch re-populates it.
        chosen_.clear();
        const StripLane lane = lanes.at(localY);
        if (overGutter) {
            // A header click selects the lane rather than scrubbing. It is the one place on the
            // strip where a click means "this row" instead of "this moment", which is exactly why
            // the headers are outside the time axis rather than floating over it.
            hitBlock = true;
            if (lane == StripLane::Actors) {
                const int row = static_cast<int>((localY - lanes.actorsTop()) / (lanes.laneHeight + lanes.gap));
                if (row >= 0 && row < static_cast<int>(piece.actors.size())) {
                    selection_ = Selection::Actor;
                    selected_ = row;
                    // The visibility dot, which is a control and not a label.
                    if (mouse.x >= origin.x + lanes.gutter - 20.0f) {
                        piece.actors[static_cast<std::size_t>(row)].visible =
                            !piece.actors[static_cast<std::size_t>(row)].visible;
                        touch();
                    }
                }
            }
        } else if (lane == StripLane::Ruler) {
            // The playhead's handle is a grab, and everything else in the ruler is a scrub that
            // *becomes* a grab -- pressing anywhere on the ruler moves the playhead there and then
            // keeps it under the pointer, which is how scrubbing works in every editor and is what
            // the old code already did by accident through its no-block fallthrough. Naming it lets
            // the cursor and the handle's pressed state say so.
            drag_ = Drag::Playhead;
            hitBlock = true;
            // T2: the press has been read as "move the playhead there". Opened here rather than
            // inside `seekSeconds`, because the engine cannot tell a person's click from the
            // transport's own end-of-piece wrap, and an instrument that cannot tell them apart
            // reports the second as the first.
            core::interactions().beginFromInput(core::Interaction::TimelineClick);
            core::interactions().markCommand();
            // A press is not yet a held gesture: it is a click until the pointer moves with the
            // button down. Requesting it unheld means a click evaluates on the frame it arrives,
            // exactly as it did before this change -- the deferral is confined to the drag.
            engine.requestSeek(std::clamp(snap(engine, mouseTime), 0.0, duration), false);
        } else if (lane == StripLane::Sections) {
            // A boundary first, because it is a five-point target inside a block and the block is
            // the thing you get when you miss it.
            auto& structure = piece.sectionTimeline;
            for (std::size_t i = 1; i < structure.sections.size(); ++i) {
                if (std::fabs(mouse.x - toX(structure.sections[i].startSeconds)) <= kEdgeGrab) {
                    drag_ = Drag::SectionBoundary;
                    dragIndex_ = static_cast<int>(i);
                    // The section the boundary opens, so grabbing an edge also shows you what you
                    // are about to move the start of.
                    selection_ = Selection::Section;
                    selected_ = static_cast<int>(i);
                    hitBlock = true;
                    break;
                }
            }
            if (!hitBlock) {
                for (std::size_t i = 0; i < structure.sections.size(); ++i) {
                    if (mouseTime < structure.sections[i].startSeconds ||
                        mouseTime > structure.sections[i].endSeconds) {
                        continue;
                    }
                    selection_ = Selection::Section;
                    selected_ = static_cast<int>(i);
                    hitBlock = true;
                    break;
                }
            }
        } else if (lane == StripLane::Audio) {
            // The clip under the pointer is highlighted, and the click still scrubs: `hitBlock` stays
            // false on purpose. Clips are not dragged here. Making them draggable is what stole the
            // click that moves the playhead, and this lane's promise -- clicking a moment in the
            // music to hear it -- is worth more than a gesture the Audio... popup already offers as
            // numbers. Trimming and moving live there until this has somewhere safe to put them.
            audioSelected_ = -1;
            for (std::size_t i = 0; i < engine.audioClips().size(); ++i) {
                const audio::AudioClip& clip = engine.audioClips()[i];
                const double end = audio::clipEndSeconds(clip, engine.clipSource(clip.file).get());
                if (mouse.x >= toX(clip.startSeconds) && mouse.x <= toX(end)) {
                    audioSelected_ = static_cast<int>(i);
                    // A clip is a selection like a shot is, so the inspector and the Delete key have
                    // one thing to ask rather than two. The click still scrubs -- `hitBlock` stays
                    // false -- because this lane's promise is that clicking a moment plays it.
                    selection_ = Selection::Clip;
                    selected_ = static_cast<int>(i);
                    selectedSomething = true;
                    break;
                }
            }
        } else if (lane == StripLane::Shots) {
            for (std::size_t i = 0; i < piece.shots.size(); ++i) {
                const seq::Shot& shot = piece.shots[i];
                const float a = toX(shot.startSeconds);
                const float b = toX(shot.endSeconds());
                const BlockZone zone = blockZoneAt(mouse.x, a, b, kEdgeGrab);
                if (zone == BlockZone::None) {
                    continue;
                }
                selection_ = Selection::Shot;
                selected_ = static_cast<int>(i);
                hitBlock = true;
                dragIndex_ = static_cast<int>(i);
                drag_ = zone == BlockZone::RightEdge  ? Drag::TrimShotEnd
                        : zone == BlockZone::LeftEdge ? Drag::TrimShotStart
                                                      : Drag::MoveShot;
                dragGrab_ = mouseTime - shot.startSeconds;
                // Trimming the start must not move the end, so the end is remembered rather than
                // recomputed from a duration that is about to change underneath it.
                dragAnchor_ = shot.endSeconds();
                break;
            }
        } else if (lane == StripLane::Overlays) {
            for (std::size_t i = 0; i < piece.overlays.size(); ++i) {
                const seq::OverlayCue& cue = piece.overlays[i];
                const float a = toX(cue.startSeconds);
                const float b = toX(cue.endSeconds);
                const BlockZone zone = blockZoneAt(mouse.x, a, b, kEdgeGrab);
                if (zone == BlockZone::None) {
                    continue;
                }
                selection_ = Selection::Overlay;
                selected_ = static_cast<int>(i);
                hitBlock = true;
                dragIndex_ = static_cast<int>(i);
                drag_ = zone == BlockZone::RightEdge  ? Drag::TrimOverlayEnd
                        : zone == BlockZone::LeftEdge ? Drag::TrimOverlayStart
                                                      : Drag::MoveOverlay;
                dragGrab_ = mouseTime - cue.startSeconds;
                dragAnchor_ = cue.endSeconds;
                break;
            }
        } else if (lane == StripLane::Actors) {
            const int row = static_cast<int>((localY - lanes.actorsTop()) / (lanes.laneHeight + lanes.gap));
            if (row >= 0 && row < static_cast<int>(piece.actors.size())) {
                selection_ = Selection::Actor;
                selected_ = row;
                hitBlock = true;
            }
        }
        if (!hitBlock && !selectedSomething) {
            // **A press on empty lane space starts a rubber band; it does not scrub.**
            //
            // It used to scrub. The ruler branch above scrubs deliberately, and this fall-through
            // scrubbed as well -- so missing a shot by three pixels, or pressing in the gap after
            // the last one, moved the playhead. Scrubbing now belongs to the ruler and the marker
            // band above the lanes, where a timeline's scrub bar lives in every editor this borrows
            // from.
            //
            // What the empty background is for instead is selecting across it. Shift keeps what was
            // already selected, which is the convention everywhere else that has a band.
            drag_ = Drag::Marquee;
            marqueeAdds_ = ImGui::GetIO().KeyShift;
            marqueeKept_.clear();
            if (marqueeAdds_) {
                marqueeKept_ = chosen_;
                if (chosen_.empty() && selection_ != Selection::None) {
                    marqueeKept_.push_back({selection_, selected_});
                }
            }
            marqueeFromX_ = localX;
            marqueeFromY_ = localY;
            marqueeToX_ = localX;
            marqueeToY_ = localY;
            if (!marqueeAdds_) {
                chooseOne(Selection::None, -1);
            }
        }
    }

    // **Delete, which the context menus have been advertising and nothing implemented.**
    //
    // Every one of those menus draws the shortcut beside its Delete row, so the key has looked bound
    // since the menus were written. It was not: there was no keyboard handler on this panel at all.
    //
    // Gated on the strip being hovered rather than on focus, because that is where the selection is
    // visible and a Delete pressed while typing a shot's name must reach the text field instead --
    // `WantTextInput` is what separates the two.
    if (hovered && !ImGui::GetIO().WantTextInput &&
        (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace)) &&
        chosen_.size() > 1) {
        // **A multiple selection is deleted as one edit**, which is what makes the rubber band worth
        // having: sweep four shots and a clip, press Delete, take it all back with one Cmd+Z.
        //
        // Descending by index, because every erase shifts everything after it. Grouped by kind
        // first so the audio is installed once rather than once per clip -- `setAudioClips` re-opens
        // the sources and re-mixes the whole piece (ADR-103), and doing that five times would be
        // five passes over every sample in the track.
        std::vector<int> shots, overlays, actors, clips;
        for (const SelectedItem& item : chosen_) {
            switch (item.kind) {
            case Selection::Shot: shots.push_back(item.index); break;
            case Selection::Overlay: overlays.push_back(item.index); break;
            case Selection::Actor: actors.push_back(item.index); break;
            case Selection::Clip: clips.push_back(item.index); break;
            case Selection::Section: break; // a landmark, not an object; never swept into the bin
            case Selection::None: break;
            }
        }
        const auto descending = [](std::vector<int>& v) {
            std::sort(v.begin(), v.end(), std::greater<int>());
            v.erase(std::unique(v.begin(), v.end()), v.end());
        };
        descending(shots);
        descending(overlays);
        descending(actors);
        descending(clips);

        beginEdit(engine, /*touchesAudio=*/!clips.empty());
        for (const int i : shots) {
            if (i >= 0 && i < static_cast<int>(piece.shots.size())) {
                seq::removeShot(piece.shots, static_cast<std::size_t>(i));
            }
        }
        for (const int i : overlays) {
            if (i >= 0 && i < static_cast<int>(piece.overlays.size())) {
                piece.overlays.erase(piece.overlays.begin() + i);
            }
        }
        for (const int i : actors) {
            if (i >= 0 && i < static_cast<int>(piece.actors.size())) {
                piece.actors.erase(piece.actors.begin() + i);
            }
        }
        if (!clips.empty()) {
            std::vector<audio::AudioClip> remaining(engine.audioClips().begin(),
                                                    engine.audioClips().end());
            for (const int i : clips) {
                if (i >= 0 && i < static_cast<int>(remaining.size())) {
                    remaining.erase(remaining.begin() + i);
                }
            }
            applyAudioClips(engine, std::move(remaining));
        }
        chooseOne(Selection::None, -1);
        touch();
        commitEdit(engine, fmt::format("Delete {} item(s)",
                                       shots.size() + overlays.size() + actors.size() + clips.size()));
    } else if (hovered && !ImGui::GetIO().WantTextInput &&
               (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace))) {
        switch (selection_) {
        case Selection::Shot:
            if (selected_ >= 0 && selected_ < static_cast<int>(piece.shots.size())) {
                beginEdit(engine);
                seq::removeShot(piece.shots, static_cast<std::size_t>(selected_));
                selection_ = Selection::None;
                selected_ = -1;
                touch();
                commitEdit(engine, "Delete shot");
            }
            break;
        case Selection::Overlay:
            if (selected_ >= 0 && selected_ < static_cast<int>(piece.overlays.size())) {
                beginEdit(engine);
                piece.overlays.erase(piece.overlays.begin() + selected_);
                selection_ = Selection::None;
                selected_ = -1;
                touch();
                commitEdit(engine, "Delete lyric");
            }
            break;
        case Selection::Actor:
            if (selected_ >= 0 && selected_ < static_cast<int>(piece.actors.size())) {
                beginEdit(engine);
                piece.actors.erase(piece.actors.begin() + selected_);
                selection_ = Selection::None;
                selected_ = -1;
                touch();
                commitEdit(engine, "Delete performer");
            }
            break;
        case Selection::Clip: {
            // Through the same call the menu's "Remove clip" makes, so a clip removed by key and one
            // removed by menu are the same edit -- `applyAudioClips` is what re-opens the sources.
            std::vector<audio::AudioClip> clips(engine.audioClips().begin(), engine.audioClips().end());
            if (audioSelected_ >= 0 && audioSelected_ < static_cast<int>(clips.size())) {
                beginEdit(engine, /*touchesAudio=*/true);
                clips.erase(clips.begin() + audioSelected_);
                audioSelected_ = -1;
                selection_ = Selection::None;
                selected_ = -1;
                applyAudioClips(engine, std::move(clips));
                commitEdit(engine, "Remove audio clip");
            }
            break;
        }
        case Selection::Section:
            // Deliberately not deletable by key. A section is a landmark rather than an object, and
            // removing one gives its span to a neighbour -- a bigger, less obvious change than
            // deleting a block, and one the context menu should have to ask for.
            break;
        case Selection::None:
            break;
        }
    }

    if (drag_ != Drag::None && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        const double t = snap(engine, mouseTime);
        const auto overlay = [&]() -> seq::OverlayCue* {
            return dragIndex_ >= 0 && dragIndex_ < static_cast<int>(piece.overlays.size())
                       ? &piece.overlays[static_cast<std::size_t>(dragIndex_)]
                       : nullptr;
        };
        switch (drag_) {
        case Drag::Playhead:
            // One record per outstanding *batch* of requests, not one per frame. A held drag issues
            // a request every frame; if one is already outstanding the new one is folded into it
            // and counted, so T0 stays at the batch's oldest request -- the pessimistic end, which
            // is the one a deferral has to be measured against. When nothing is outstanding a new
            // batch opens, which is what happens on the frame after each evaluation.
            if (core::interactions().openKind() == core::Interaction::TimelineDrag) {
                core::interactions().noteSuperseded();
            } else {
                core::interactions().beginFromInput(core::Interaction::TimelineDrag);
                core::interactions().markCommand();
            }
            engine.requestSeek(std::clamp(t, 0.0, duration), true);
            break;
        case Drag::Marquee: {
            marqueeToX_ = mouse.x - origin.x;
            marqueeToY_ = mouse.y - origin.y;
            // Recomputed every frame rather than accumulated, so shrinking the band unselects what
            // it no longer covers. An accumulating band can only ever grow, which is the behaviour
            // people complain about in tools that have it.
            std::vector<SelectedItem> hit = itemsIn(engine, lanes, axisX, width, view_, span,
                                                    marqueeFromX_, marqueeFromY_, marqueeToX_,
                                                    marqueeToY_);
            if (marqueeAdds_) {
                for (const SelectedItem& had : marqueeKept_) {
                    if (std::find(hit.begin(), hit.end(), had) == hit.end()) {
                        hit.push_back(had);
                    }
                }
            }
            chosen_ = std::move(hit);
            // The primary follows the band so the inspector shows something while it is dragged --
            // the first thing found, which is the topmost lane's leftmost block.
            if (!chosen_.empty()) {
                selection_ = chosen_.front().kind;
                selected_ = chosen_.front().index;
                audioSelected_ = chosen_.front().kind == Selection::Clip ? chosen_.front().index : -1;
            } else {
                selection_ = Selection::None;
                selected_ = -1;
                audioSelected_ = -1;
            }
            break;
        }
        // The three shot gestures live in `seq/sequence.hpp` now, so what a drag does and what a
        // test can check are the same code rather than two spellings of it.
        // The neighbour-aware forms: a drag cannot push a shot into the one beside it, and lands
        // flush when it gets close. Overlap is refused by `Sequence::validate` anyway, so clamping
        // here is the difference between a control that will not do the wrong thing and one that
        // does it and then reports a failure.
        case Drag::MoveShot:
            if (dragIndex_ >= 0) {
                seq::moveShot(piece.shots, static_cast<std::size_t>(dragIndex_), t - dragGrab_);
            }
            break;
        case Drag::TrimShotEnd:
            if (dragIndex_ >= 0) {
                seq::trimShotEnd(piece.shots, static_cast<std::size_t>(dragIndex_), t,
                                 kMinBlockSeconds);
            }
            break;
        case Drag::TrimShotStart:
            if (dragIndex_ >= 0) {
                // `dragAnchor_` is the end remembered when the gesture began -- see `trimShotStart`.
                seq::trimShotStart(piece.shots, static_cast<std::size_t>(dragIndex_), t, dragAnchor_,
                                   kMinBlockSeconds);
            }
            break;
        case Drag::MoveOverlay:
            if (seq::OverlayCue* c = overlay(); c != nullptr) {
                const double length = c->durationSeconds();
                c->startSeconds = std::max(0.0, t - dragGrab_);
                c->endSeconds = c->startSeconds + length;
            }
            break;
        case Drag::TrimOverlayEnd:
            if (seq::OverlayCue* c = overlay(); c != nullptr) {
                c->endSeconds = std::max(c->startSeconds + kMinBlockSeconds, t);
            }
            break;
        case Drag::TrimOverlayStart:
            if (seq::OverlayCue* c = overlay(); c != nullptr) {
                c->startSeconds = std::clamp(t, 0.0, dragAnchor_ - kMinBlockSeconds);
                c->endSeconds = dragAnchor_;
            }
            break;
        case Drag::SectionBoundary:
            if (dragIndex_ > 0) {
                // The section boundary has its *own* snap, not the strip's: `t` above has already
                // been through the shot grid, so this starts again from the raw time. A snapped
                // boundary takes the beat's own value, bit for bit; a free one takes the
                // millisecond it landed on. Neither is rounded.
                (void)song::moveBoundary(piece.sectionTimeline, static_cast<std::size_t>(dragIndex_),
                                        snapSection(engine, mouseTime));
            }
            break;
        case Drag::None:
            break;
        }
    } else if (drag_ == Drag::None && hovered && overAxis &&
               ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        if (core::interactions().openKind() == core::Interaction::TimelineDrag) {
            core::interactions().noteSuperseded();
        } else {
            core::interactions().beginFromInput(core::Interaction::TimelineDrag);
            core::interactions().markCommand();
        }
        engine.requestSeek(std::clamp(snap(engine, mouseTime), 0.0, duration), true);
    }
    if (drag_ != Drag::None && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        if (drag_ == Drag::Playhead) {
            // The gesture is over. Re-requesting the same second unheld is what turns the deferral
            // off for the last one: `advanceSeekDeferral` short-circuits on release, so letting go
            // of a scrub evaluates on that frame rather than kSettleMs later. Without this the
            // playhead would land and the world would arrive five frames afterwards, which is
            // exactly the kind of small lie a deferral makes easy to ship.
            engine.requestSeek(engine.transport().positionSeconds(), false);
        }
        // The bake waits for the mouse. A drag is sixty edits a second and a bake rebuilds tracks
        // and layers; doing both together would make a smooth drag feel like a stutter. The
        // arrangement waits for the same reason and costs more: a re-mix is a pass over every
        // sample in the piece.
        // The markers are derived from the structure, and re-deriving them means re-sorting a list
        // that holds every beat in the song. Once, when the drag ends, rather than sixty times a
        // second while it is moving.
        const Drag finished = drag_;
        if (finished == Drag::SectionBoundary) {
            piece.refreshSectionMarkers();
        }
        drag_ = Drag::None;
        dragIndex_ = -1;
        // A scrub changes nothing the bake reads, so it does not ask for one. Before this every
        // release of a scrub rebuilt every track in the piece. A rubber band changes nothing either
        // -- it selects; it does not edit.
        if (finished != Drag::Playhead && finished != Drag::Marquee) {
            touch();
        }
        if (finished == Drag::Marquee) {
            marqueeKept_.clear();
            // A band that never travelled is a click on the background, which means "select
            // nothing" -- and `chosen_` is already empty, so there is nothing more to do.
            if (chosen_.size() == 1) {
                // One thing caught is a plain selection. Collapsing it keeps the common case out of
                // the multiple-selection paths entirely.
                const SelectedItem only = chosen_.front();
                chooseOne(only.kind, only.index);
            }
        }
    }

    // ---- the view: pan and zoom -------------------------------------------------------------------
    //
    // Right-drag pans and the wheel zooms about the pointer: the two gestures a time strip needs and
    // the two everyone already knows. A right press that does *not* travel is a context menu, and
    // `updateContextClick` is what separates the two -- Dear ImGui's own context-menu helper opens
    // on the release of the right button with no test of how far it moved, so without this every pan
    // would end by opening a menu (the addendum's section 13).
    const bool wantsMenu = updateContextClick(contextClick_, hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right),
                                              ImGui::IsMouseReleased(ImGuiMouseButton_Right), mouse.x,
                                              mouse.y);
    if (contextClick_.down && contextClick_.travelled && ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
        view_ -= static_cast<double>(ImGui::GetIO().MouseDelta.x / width) * span;
    }
    // **The wheel belongs to the panel, not to the strip.**
    //
    // It used to zoom the time axis, and the strip does not consume the event -- so one flick both
    // zoomed the lanes and scrolled the panel under them, which is two answers to one gesture and
    // neither is the one being asked for. The panel is taller than it is on screen whenever there
    // are more than a couple of lanes, so scrolling it is the common intent by a wide margin.
    //
    // Zoom is still one gesture away and now says which axis it means: the two sliders in the
    // toolbar, or the grip under the lanes for the vertical.
    view_ = std::clamp(view_, 0.0, std::max(0.0, duration - duration / static_cast<double>(zoom_)));

    if (wantsMenu) {
        // What the menu is about is decided *here*, at the click, and remembered. ImGui keeps a
        // popup open across frames, and by the time its body is submitted the pointer has usually
        // moved somewhere else -- so a menu that hit-tested from the live pointer would act on
        // whatever the mouse happened to be over when an item was chosen, which is the single most
        // damaging bug a context menu can have.
        menu_.lane = overGutter ? StripLane::None : hoverLane;
        menu_.header = overGutter ? hoverLane : StripLane::None;
        menu_.seconds = mouseTime;
        menu_.index = hoverBlock;
        menu_.actorRow = hoverActorRow;
        ImGui::OpenPopup(kStripContextId);
    }
    drawStripContextMenu(engine);
}

// ---- the strip's context menu (the addendum's section 3) ----------------------------------------
//
// What a right-click was over, captured at the click. See the note at the call site: a popup
// outlives the frame that opened it, so nothing here may hit-test the live pointer.
void SequencePanel::drawStripContextMenu(app::Engine& engine) {
    seq::Sequence& piece = engine.sequence();
    ContextMenu menu(kStripContextId, false);
    if (!menu) {
        return;
    }
    const double at = menu_.seconds;
    const auto valid = [](int index, std::size_t size) {
        return index >= 0 && static_cast<std::size_t>(index) < size;
    };

    // A header: the lane's own actions, not the timeline's.
    if (menu_.header == StripLane::Actors && valid(menu_.actorRow, piece.actors.size())) {
        seq::Actor& actor = piece.actors[static_cast<std::size_t>(menu_.actorRow)];
        menuSubject(fmt::format("Actor: {}", actor.id));
        if (menuToggle("Visible", actor.visible)) {
            actor.visible = !actor.visible;
            touch();
        }
        ImGui::Separator();
        if (menuAction("Add clip cue here")) {
            actor.clips.push_back(seq::ClipCue{.timeSeconds = at});
            std::stable_sort(actor.clips.begin(), actor.clips.end(),
                             [](const seq::ClipCue& l, const seq::ClipCue& r) {
                                 return l.timeSeconds < r.timeSeconds;
                             });
            selection_ = Selection::Actor;
            selected_ = menu_.actorRow;
            touch();
        }
        if (menuAction("Remove actor")) {
            piece.actors.erase(piece.actors.begin() + menu_.actorRow);
            selection_ = Selection::None;
            selected_ = -1;
            touch();
        }
        return;
    }
    if (menu_.header != StripLane::None) {
        // The other headers name a lane that is a category rather than an object -- Shots, Audio,
        // Sections, Overlays. There is nothing a professional editor does to "the shots lane" as
        // such in this application, so rather than a menu of disabled rows there is one useful
        // thing per lane and no menu where there is none.
        if (menu_.header == StripLane::Shots && menuAction("Add shot at end")) {
            addShotAtEnd(engine);
        }
        if (menu_.header == StripLane::Audio && menuAction("Audio clips...")) {
            ImGui::CloseCurrentPopup();
            openAudioClips_ = true;
        }
        return;
    }

    switch (menu_.lane) {
    case StripLane::Shots: {
        if (valid(menu_.index, piece.shots.size())) {
            const auto index = static_cast<std::size_t>(menu_.index);
            seq::Shot& shot = piece.shots[index];
            menuSubject(fmt::format("Shot: {}", shot.name));
            if (menuAction("Select")) {
                selection_ = Selection::Shot;
                selected_ = menu_.index;
            }
            if (menuAction("Duplicate")) {
                if (const auto made = seq::duplicateShot(piece.shots, index)) {
                    selection_ = Selection::Shot;
                    selected_ = static_cast<int>(*made);
                    touch();
                }
            }
            // Splitting is a real operation on a shot -- two shots whose durations add up to the
            // original's -- and it is offered only where it would produce two shots that are not
            // degenerate, rather than as a row that is always there and usually refuses.
            const bool splittable = at > shot.startSeconds + kMinBlockSeconds &&
                                    at < shot.endSeconds() - kMinBlockSeconds;
            if (menuAction("Split at pointer", nullptr, splittable)) {
                if (seq::splitShot(piece.shots, index, at, kMinBlockSeconds)) {
                    touch();
                }
            }
            ImGui::Separator();
            if (menuAction("Delete", shortcut::kDelete)) {
                beginEdit(engine);
                piece.shots.erase(piece.shots.begin() + static_cast<std::ptrdiff_t>(index));
                selection_ = Selection::None;
                selected_ = -1;
                touch();
                commitEdit(engine, "Delete shot");
            }
        } else {
            menuSubject("Shots lane");
            if (menuAction("Add shot here")) {
                addShotAt(engine, at);
            }
            if (menuAction("Add shot at end")) {
                addShotAtEnd(engine);
            }
        }
        break;
    }
    case StripLane::Overlays: {
        if (valid(menu_.index, piece.overlays.size())) {
            const auto index = static_cast<std::size_t>(menu_.index);
            seq::OverlayCue& cue = piece.overlays[index];
            menuSubject(fmt::format("Overlay: {}", cue.content.empty() ? cue.id : cue.content));
            if (menuAction("Select")) {
                selection_ = Selection::Overlay;
                selected_ = menu_.index;
            }
            if (menuAction("Duplicate")) {
                seq::OverlayCue copy = cue;
                copy.id = fmt::format("{}-copy{}", cue.id, piece.overlays.size());
                const double length = cue.durationSeconds();
                copy.startSeconds = cue.endSeconds;
                copy.endSeconds = copy.startSeconds + length;
                piece.overlays.insert(piece.overlays.begin() + static_cast<std::ptrdiff_t>(index) + 1,
                                      std::move(copy));
                selected_ = menu_.index + 1;
                selection_ = Selection::Overlay;
                touch();
            }
            ImGui::Separator();
            if (menuAction("Delete", shortcut::kDelete)) {
                beginEdit(engine);
                piece.overlays.erase(piece.overlays.begin() + static_cast<std::ptrdiff_t>(index));
                selection_ = Selection::None;
                selected_ = -1;
                touch();
                commitEdit(engine, "Delete lyric");
            }
        } else {
            menuSubject("Overlays lane");
            if (menuAction("Add lyric here")) {
                addLyricAt(engine, at);
            }
        }
        break;
    }
    case StripLane::Audio: {
        if (valid(menu_.index, static_cast<int>(engine.audioClips().size()))) {
            const auto index = static_cast<std::size_t>(menu_.index);
            std::vector<audio::AudioClip> clips = engine.audioClips();
            menuSubject(fmt::format("Clip: {}", clips[index].name.empty()
                                                    ? clips[index].file.filename().string()
                                                    : clips[index].name));
            if (menuToggle("Enabled", clips[index].enabled)) {
                clips[index].enabled = !clips[index].enabled;
                applyAudioClips(engine, std::move(clips));
                break;
            }
            if (menuAction("Move start to pointer")) {
                clips[index].startSeconds = std::max(0.0, at);
                applyAudioClips(engine, std::move(clips));
                break;
            }
            ImGui::Separator();
            if (menuAction("Remove clip", shortcut::kDelete)) {
                beginEdit(engine, /*touchesAudio=*/true);
                clips.erase(clips.begin() + static_cast<std::ptrdiff_t>(index));
                audioSelected_ = -1;
                applyAudioClips(engine, std::move(clips));
                commitEdit(engine, "Remove audio clip");
                break;
            }
            if (menuAction("Audio clips...")) {
                ImGui::CloseCurrentPopup();
                openAudioClips_ = true;
            }
        } else {
            menuSubject("Audio");
            if (menuAction("Open audio...", shortcut::kOpenAudio, onOpenAudio != nullptr)) {
                ImGui::CloseCurrentPopup();
                if (onOpenAudio) {
                    onOpenAudio();
                }
            }
        }
        break;
    }
    case StripLane::Sections: {
        if (valid(menu_.index, piece.sectionTimeline.sections.size())) {
            const auto index = static_cast<std::size_t>(menu_.index);
            menuSubject(fmt::format("Section: {}",
                                    piece.shotLanguage.displayName(piece.sectionTimeline.sections[index])));
            if (menuAction("Select")) {
                selection_ = Selection::Section;
                selected_ = menu_.index;
            }
            const song::Section& section = piece.sectionTimeline.sections[index];
            // `splitSection` takes a time and refuses a split that would leave a section shorter
            // than its minimum, so the enabled test asks the same question the operation will:
            // an item that is offered and then declines is worse than one that was never offered.
            const bool splittable = at > section.startSeconds + kMinSectionSeconds &&
                                    at < section.endSeconds - kMinSectionSeconds;
            if (menuAction("Split here", nullptr, splittable)) {
                if (song::splitSection(piece.sectionTimeline, at, kMinSectionSeconds)) {
                    piece.refreshSectionMarkers();
                    touch();
                }
            }
            // Not "delete": the structure is gapless, so removing a section gives its time to a
            // neighbour rather than leaving a hole, and the label says which of those it is.
            if (menuAction("Remove (merge into neighbour)", nullptr,
                           piece.sectionTimeline.sections.size() > 1)) {
                if (song::removeSection(piece.sectionTimeline, index)) {
                    selection_ = Selection::None;
                    selected_ = -1;
                    piece.refreshSectionMarkers();
                    touch();
                }
            }
        } else {
            menuSubject("Sections");
            if (menuAction("Analyze song", nullptr, engine.track() != nullptr && work_ == nullptr)) {
                startStructureAnalysis(engine, true);
            }
        }
        break;
    }
    case StripLane::Ruler:
    case StripLane::None:
    default: {
        menuSubject("Timeline");
        if (menuAction("Play / pause", shortcut::kPlayPause)) {
            engine.togglePlay();
        }
        if (menuAction("Move playhead here")) {
            engine.seekSeconds(std::clamp(at, 0.0, std::max(piece.duration(), 1.0)));
        }
        ImGui::Separator();
        if (menuAction("Add shot here")) {
            addShotAt(engine, at);
        }
        if (menuAction("Add lyric here")) {
            addLyricAt(engine, at);
        }
        ImGui::Separator();
        if (menuAction("Zoom to fit")) {
            zoom_ = 1.0f;
            view_ = 0.0;
        }
        break;
    }
    }
}

// ---- the small mutations the toolbar and the context menu share ---------------------------------
//
// Factored out of `drawToolbar` rather than copied into the menu. Two paths that add a shot and
// disagree about what a new shot looks like is exactly the drift the addendum's section 10 is
// about; the world editor avoids it by routing everything through `EditSystem`, and the sequencer
// has no such system, so the next best thing is one function.

void SequencePanel::addShotAt(app::Engine& engine, double seconds) {
    seq::Sequence& piece = engine.sequence();
    seq::Shot shot;
    shot.name = fmt::format("shot {}", piece.shots.size() + 1);
    shot.startSeconds = std::max(0.0, seconds);
    shot.durationSeconds = 8.0;
    if (!piece.shots.empty()) {
        shot.scene = piece.shots.back().scene;
    } else if (!piece.scenes.empty()) {
        shot.scene = piece.scenes.front().id;
    }
    app::FocalTarget subject;
    subject.radius = 10.0f;
    if (!piece.actors.empty()) {
        subject.position = piece.actors.front().positionAt(shot.startSeconds);
        subject.name = piece.actors.front().id;
    }
    shot.camera = seq::cameraFromPreset(seq::CameraPreset::Wide, subject);
    if (!piece.actors.empty()) {
        shot.camera.lookAtActor = piece.actors.front().id;
    }
    piece.shots.push_back(std::move(shot));
    // Kept in time order, because a shot inherits the *previous* shot's scene slot and "previous"
    // is a position in this vector. Adding one in the middle without sorting would silently change
    // which slot the shots after it inherit.
    std::stable_sort(piece.shots.begin(), piece.shots.end(),
                     [](const seq::Shot& l, const seq::Shot& r) {
                         return l.startSeconds < r.startSeconds;
                     });
    selection_ = Selection::Shot;
    selected_ = static_cast<int>(std::distance(
        piece.shots.begin(),
        std::find_if(piece.shots.begin(), piece.shots.end(), [&](const seq::Shot& s) {
            return s.startSeconds == std::max(0.0, seconds);
        })));
    touch();
}

void SequencePanel::addShotAtEnd(app::Engine& engine) {
    const seq::Sequence& piece = engine.sequence();
    // A new shot starts where the piece currently ends, so shots never overlap by accident --
    // which validate() would refuse anyway, loudly, on the next install.
    addShotAt(engine, piece.shots.empty() ? 0.0 : piece.shots.back().endSeconds());
}

void SequencePanel::addLyricAt(app::Engine& engine, double seconds) {
    seq::Sequence& piece = engine.sequence();
    seq::OverlayCue cue;
    cue.id = fmt::format("cue{:03}", piece.overlays.size() + 1);
    cue.content = "LYRIC";
    cue.style = "lyric";
    cue.startSeconds = std::max(0.0, seconds);
    cue.endSeconds = cue.startSeconds + 3.0;
    cue.anchor = glm::vec2(0.5f, 0.16f);
    cue.preset = seq::OverlayPreset::FadeInOut;
    cue.order = 10;
    piece.overlays.push_back(std::move(cue));
    selection_ = Selection::Overlay;
    selected_ = static_cast<int>(piece.overlays.size()) - 1;
    touch();
}

// ---- the selection set -------------------------------------------------------------------------

bool SequencePanel::isChosen(Selection kind, int index) const {
    if (selection_ == kind && selected_ == index) {
        return true; // the primary is always part of the selection
    }
    return std::any_of(chosen_.begin(), chosen_.end(), [&](const SelectedItem& i) {
        return i.kind == kind && i.index == index;
    });
}

void SequencePanel::chooseOne(Selection kind, int index) {
    chosen_.clear();
    selection_ = kind;
    selected_ = index;
    // The audio lane keeps its own index because a clip is addressed by it everywhere else in this
    // file; kept in step here so the two cannot disagree about what is selected.
    audioSelected_ = kind == Selection::Clip ? index : -1;
}

std::vector<SequencePanel::SelectedItem> SequencePanel::itemsIn(
    const app::Engine& engine, const StripLanes& lanes, float axisX, float width, double view,
    double span, float x0, float y0, float x1, float y1) const {
    // **Computed from the data and the lane geometry, never from anything that was drawn.** The
    // blocks on screen are themselves a function of those two things, so working from the same
    // inputs is what stops the band and the picture disagreeing about where a block is -- the exact
    // class of bug ADR-103 records, where the hit test and the drawing each did their own
    // arithmetic.
    const seq::Sequence& piece = engine.sequence();
    const float left = std::min(x0, x1);
    const float right = std::max(x0, x1);
    const float top = std::min(y0, y1);
    const float bottom = std::max(y0, y1);

    const auto toX = [&](double seconds) {
        return axisX + static_cast<float>((seconds - view) / span) * width;
    };
    // Two spans overlap if neither ends before the other starts. Touching counts: a band dragged
    // exactly onto a block's edge has selected it as far as anyone watching is concerned.
    const auto overlaps = [](float a0, float a1, float b0, float b1) {
        return a0 <= b1 && b0 <= a1;
    };
    const auto rowHit = [&](float laneTop, float laneHeight, double startSeconds, double endSeconds) {
        return overlaps(top, bottom, laneTop, laneTop + laneHeight) &&
               overlaps(left, right, toX(startSeconds), toX(endSeconds));
    };

    std::vector<SelectedItem> out;
    if (lanes.hasAudio) {
        const auto& clips = engine.audioClips();
        for (std::size_t i = 0; i < clips.size(); ++i) {
            // `durationSeconds == 0` means "the rest of the file", so the arithmetic spelling of a
            // clip's end is a zero-length span for almost every clip there is -- and a band that
            // tests it catches nothing. The same call the lane draws with.
            const double end = audio::clipEndSeconds(clips[i], engine.clipSource(clips[i].file).get());
            if (rowHit(lanes.audioTop(), lanes.audioLaneHeight, clips[i].startSeconds, end)) {
                out.push_back({Selection::Clip, static_cast<int>(i)});
            }
        }
    }
    for (std::size_t i = 0; i < piece.shots.size(); ++i) {
        if (rowHit(lanes.shotsTop(), lanes.laneHeight, piece.shots[i].startSeconds,
                   piece.shots[i].endSeconds())) {
            out.push_back({Selection::Shot, static_cast<int>(i)});
        }
    }
    for (std::size_t a = 0; a < piece.actors.size(); ++a) {
        const float laneTop = lanes.actorsTop() + static_cast<float>(a) * (lanes.laneHeight + lanes.gap);
        // An actor is selected by touching its lane at all, not by touching one of its cues: the
        // lane is the object here, and a performer with no clips would otherwise be unselectable.
        if (overlaps(top, bottom, laneTop, laneTop + lanes.laneHeight) &&
            overlaps(left, right, axisX, axisX + width)) {
            out.push_back({Selection::Actor, static_cast<int>(a)});
        }
    }
    if (lanes.hasOverlays) {
        for (std::size_t i = 0; i < piece.overlays.size(); ++i) {
            if (rowHit(lanes.overlaysTop(), lanes.laneHeight, piece.overlays[i].startSeconds,
                       piece.overlays[i].endSeconds)) {
                out.push_back({Selection::Overlay, static_cast<int>(i)});
            }
        }
    }
    // Sections are deliberately not selectable by band, for the reason they are not deletable by
    // key: a section is a landmark rather than an object, and sweeping a band across the strip
    // should not put the song's structure in the bin along with the shots.
    return out;
}

void SequencePanel::beginEdit(app::Engine& engine, bool touchesAudio) {
    if (edits == nullptr) {
        return;
    }
    auto change = std::make_unique<TimelineChange>();
    change->before = engine.sequence();
    change->clipsTouched = touchesAudio;
    if (touchesAudio) {
        change->clipsBefore.assign(engine.audioClips().begin(), engine.audioClips().end());
    }
    pendingEdit_ = std::move(change);
}

void SequencePanel::commitEdit(app::Engine& engine, std::string label) {
    if (!pendingEdit_) {
        return;
    }
    std::unique_ptr<TimelineChange> change = std::move(pendingEdit_);
    change->after = engine.sequence();
    if (change->clipsTouched) {
        change->clipsAfter.assign(engine.audioClips().begin(), engine.audioClips().end());
    }
    EditCommand command(std::move(label));
    command.timeline = std::move(change);
    edits->history().push(std::move(command));
}

void SequencePanel::applyAudioClips(app::Engine& engine, std::vector<audio::AudioClip> clips) {
    // A re-mix is a pass over every sample in the piece (ADR-103), which is why this is only ever
    // reached from a discrete act -- a menu item, a field committed -- and never from a drag.
    if (auto r = engine.setAudioClips(std::move(clips)); !r) {
        status_ = r.error().message;
    }
}

// The grips on a block's edges. Drawn only under the pointer or while that edge is being dragged:
// a lane of clips each wearing two permanent handles is a picket fence, and the affordance is
// wanted at the moment somebody reaches for it rather than at all times.
void SequencePanel::drawEdgeGrip(ImDrawList* draw, ImVec2 a, ImVec2 b, BlockZone zone, Drag active,
                                 bool isShot) {
    const bool leftLive = zone == BlockZone::LeftEdge ||
                          active == (isShot ? Drag::TrimShotStart : Drag::TrimOverlayStart);
    const bool rightLive = zone == BlockZone::RightEdge ||
                           active == (isShot ? Drag::TrimShotEnd : Drag::TrimOverlayEnd);
    if (!leftLive && !rightLive) {
        return;
    }
    const Palette& pal = palette();
    const float inset = 2.5f;
    const auto grip = [&](float x) {
        draw->AddRectFilled(ImVec2(x - 1.5f, a.y + inset), ImVec2(x + 1.5f, b.y - inset),
                            pal.borderStrong, 1.0f);
    };
    if (leftLive) {
        grip(a.x + 3.0f);
    }
    if (rightLive) {
        grip(b.x - 3.0f);
    }
}


// ---- inspectors --------------------------------------------------------------------------------

void SequencePanel::drawInspector(app::Engine& engine) {
    seq::Sequence& piece = engine.sequence();
    // The selected thing first, then the piece-wide collections. What a person is looking at should
    // be at the top of what they are looking at: Scenes and Performers belong to the whole piece and
    // do not change when the selection does, so they were pushing the thing that DID change off the
    // bottom of the panel.
    switch (selection_) {
    case Selection::Shot:
        if (selected_ >= 0 && selected_ < static_cast<int>(piece.shots.size())) {
            drawShotInspector(engine, piece.shots[static_cast<std::size_t>(selected_)]);
        }
        break;
    case Selection::Actor:
        if (selected_ >= 0 && selected_ < static_cast<int>(piece.actors.size())) {
            drawActorInspector(engine, piece.actors[static_cast<std::size_t>(selected_)]);
        }
        break;
    case Selection::Overlay:
        if (selected_ >= 0 && selected_ < static_cast<int>(piece.overlays.size())) {
            drawOverlayInspector(engine, piece.overlays[static_cast<std::size_t>(selected_)]);
        }
        break;
    case Selection::Section:
        if (selected_ >= 0 && selected_ < static_cast<int>(piece.sectionTimeline.sections.size())) {
            drawSectionInspector(engine, static_cast<std::size_t>(selected_));
        }
        break;
    case Selection::Clip: {
        // An audio clip has an inspector already -- the Audio... dialog, which owns adding,
        // removing, gain and audition. Repeating those controls here would be a second place to
        // edit one thing, so this says what is selected and what the two ways to act on it are.
        const auto& clips = engine.audioClips();
        if (audioSelected_ >= 0 && audioSelected_ < static_cast<int>(clips.size())) {
            const audio::AudioClip& clip = clips[static_cast<std::size_t>(audioSelected_)];
            ImGui::TextUnformatted(clip.name.empty() ? clip.file.filename().string().c_str()
                                                     : clip.name.c_str());
            ImGui::TextDisabled("audio clip -- %s to remove it, or open Audio... to set its gain.",
                                shortcut::kDelete);
            if (ImGui::SmallButton("Audio clips...")) {
                openAudioClips_ = true;
            }
        } else {
            ImGui::TextDisabled("the selected clip is gone.");
        }
        break;
    }
    case Selection::None:
        ImGui::TextDisabled("Click a section, a shot, a character lane or a lyric to edit it.");
        break;
    }
    ImGui::Spacing();
    drawSceneSlots(engine);
    drawPerformerRules(engine);
}

void SequencePanel::drawSectionInspector(app::Engine& engine, std::size_t index) {
    seq::Sequence& piece = engine.sequence();
    song::Section& section = piece.sectionTimeline.sections[index];
    ImGui::SeparatorText("Section");

    std::strncpy(labelBuffer_, section.label.c_str(), sizeof(labelBuffer_) - 1);
    labelBuffer_[sizeof(labelBuffer_) - 1] = '\0';
    ImGui::SetNextItemWidth(200);
    if (ImGui::InputText("name", labelBuffer_, sizeof(labelBuffer_))) {
        if (song::setSectionLabel(piece.sectionTimeline, index, labelBuffer_)) {
            piece.refreshSectionMarkers();
            touch();
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("What events call this section. Leave it empty to use the type's name.");
    }

    // The type, from the `ShotLanguage` -- which is the person's vocabulary, built-ins and their own
    // together. This used to offer `analysis::allSectionFunctions()`: the DETECTOR's thirteen
    // categories, which is a different list for a different purpose. A person choosing what a
    // passage *is* is choosing from their own vocabulary, and "Ocean Ambience" has to be in it or
    // the custom-type feature has nowhere to appear.
    {
        const std::vector<const song::SectionType*> types = piece.shotLanguage.types();
        int current = 0;
        std::vector<const char*> names;
        names.reserve(types.size());
        for (std::size_t i = 0; i < types.size(); ++i) {
            names.push_back(types[i]->name.c_str());
            if (types[i]->id == section.type) {
                current = static_cast<int>(i);
            }
        }
        ImGui::SetNextItemWidth(200);
        if (!names.empty() &&
            ImGui::Combo("type", &current, names.data(), static_cast<int>(names.size()))) {
            if (song::setSectionType(piece.sectionTimeline, index,
                                     types[static_cast<std::size_t>(current)]->id,
                                     piece.shotLanguage)) {
                piece.refreshSectionMarkers();
                touch();
            }
        }
        if (ImGui::IsItemHovered() && current < static_cast<int>(types.size())) {
            ImGui::SetTooltip("%s", types[static_cast<std::size_t>(current)]->description.c_str());
        }
    }

    // The treatment. Storing the ABSENCE of an override is what makes "change the type and the
    // treatment follows, unless I chose one" fall out of the model rather than needing a rule, so
    // the first entry clears the override rather than setting a value equal to the default.
    {
        const song::SectionType* type = piece.shotLanguage.type(section.type);
        const std::vector<const song::ShotIntent*> intents = piece.shotLanguage.intents();
        std::vector<std::string> owned;
        owned.emplace_back(type != nullptr
                               ? fmt::format("default ({})", type->defaultShotIntent)
                               : std::string("default"));
        int current = 0;
        for (std::size_t i = 0; i < intents.size(); ++i) {
            owned.push_back(intents[i]->name);
            if (section.shotIntent && *section.shotIntent == intents[i]->id) {
                current = static_cast<int>(i) + 1;
            }
        }
        std::vector<const char*> names;
        names.reserve(owned.size());
        for (const std::string& n : owned) {
            names.push_back(n.c_str());
        }
        ImGui::SetNextItemWidth(200);
        if (ImGui::Combo("shot", &current, names.data(), static_cast<int>(names.size()))) {
            const bool ok =
                current == 0
                    ? song::clearSectionShotIntent(piece.sectionTimeline, index)
                    : song::setSectionShotIntent(piece.sectionTimeline, index,
                                                 intents[static_cast<std::size_t>(current - 1)]->id,
                                                 piece.shotLanguage);
            if (ok) {
                touch();
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("What this section should look like. 'default' follows the type, so "
                              "changing the type changes the treatment too.");
        }
    }

    // **Making a type, which until now had to be done by editing the project file.**
    //
    // The model has supported custom types since ADR-247 and the picker above lists them, but there
    // was no way to *create* one from the editor -- so "Ocean Ambience", the spec's own proof that
    // the architecture is not hard-coded around Verse and Chorus, was unreachable in the feature
    // built to hold it.
    //
    // Two fields and a treatment, which is what `SectionType` needs and no more. The id is derived
    // from the name rather than asked for: an id is a stable key a project file carries and a person
    // should not have to invent one, and `song::makeId` is the one place that derivation lives.
    if (ImGui::Button("+ New type...")) {
        newTypeName_[0] = '\0';
        newTypeDescription_[0] = '\0';
        newTypeIntent_ = 0;
        ImGui::OpenPopup("new-section-type");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Add a section type of your own -- \"Ocean Ambience\", \"Dream "
                          "Sequence\" -- and give it a default treatment.");
    }
    if (ImGui::BeginPopup("new-section-type")) {
        ImGui::SetNextItemWidth(220);
        ImGui::InputText("name", newTypeName_, sizeof(newTypeName_));
        ImGui::SetNextItemWidth(220);
        ImGui::InputText("description", newTypeDescription_, sizeof(newTypeDescription_));

        const std::vector<const song::ShotIntent*> intents = piece.shotLanguage.intents();
        std::vector<const char*> intentNames;
        intentNames.reserve(intents.size());
        for (const song::ShotIntent* intent : intents) {
            intentNames.push_back(intent->name.c_str());
        }
        ImGui::SetNextItemWidth(220);
        if (!intentNames.empty()) {
            ImGui::Combo("default shot", &newTypeIntent_, intentNames.data(),
                         static_cast<int>(intentNames.size()));
        }

        // The id is shown, not editable: a person should see the key their project will carry
        // without being asked to choose it, and seeing it is what makes "that name is already taken"
        // comprehensible when it happens.
        const std::optional<song::SectionTypeId> id = song::makeId(newTypeName_);
        const bool taken = id && piece.shotLanguage.hasType(*id);
        if (id) {
            ImGui::TextDisabled("id: %s%s", id->c_str(), taken ? "  (already exists)" : "");
        } else {
            ImGui::TextDisabled("id: --");
        }

        const bool ready = id && !taken && !intents.empty();
        ImGui::BeginDisabled(!ready);
        if (ImGui::Button("Create")) {
            song::SectionType type;
            type.id = *id;
            type.name = newTypeName_;
            type.description = newTypeDescription_;
            type.category = song::SectionCategory::Custom;
            type.defaultShotIntent = intents[static_cast<std::size_t>(newTypeIntent_)]->id;
            if (auto ok = piece.shotLanguage.defineType(std::move(type)); !ok) {
                status_ = ok.error().message;
            } else {
                // Applied to the section the inspector is on, because a person who just made a type
                // while looking at a section meant it for that section. If they did not, the picker
                // above takes it back in one click.
                (void)song::setSectionType(piece.sectionTimeline, index, *id, piece.shotLanguage);
                piece.refreshSectionMarkers();
                touch();
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Times in full, because a boundary at 1:02.409117 is the thing this whole feature is about and
    // a display rounded to two places would hide the precision it is claiming to keep.
    ImGui::Text("%s -> %s   (%.3f s)", clock(section.startSeconds).c_str(),
                clock(section.endSeconds).c_str(), section.durationSeconds());
    ImGui::TextDisabled("%.6f -> %.6f s", section.startSeconds, section.endSeconds);

    // Provenance, which the film's model records per FIELD rather than per section: a boundary a
    // person dragged and a label they typed are different claims on a re-analysis, and ADR-247's
    // whole reconciliation rests on telling them apart.
    const std::vector<std::string> editedFields = song::sectionFieldNames(section.edited);
    std::string originText;
    if (section.authored) {
        originText = "authored";
    } else if (editedFields.empty()) {
        originText = "detected";
    } else {
        originText = "edited: ";
        for (std::size_t i = 0; i < editedFields.size(); ++i) {
            originText += (i == 0 ? "" : ", ") + editedFields[i];
        }
    }
    if (!section.authored && editedFields.empty()) {
        ImGui::TextDisabled("%s", originText.c_str());
    } else {
        ImGui::TextColored(ImVec4(0.96f, 0.88f, 0.58f, 1.0f), "%s -- kept when you analyze again",
                           originText.c_str());
    }
    // The detector's confidences are deliberately NOT shown here any more. They are a claim about
    // `analysis::SongStructure`'s sections, and this inspector now edits the film's -- which a person
    // may have moved, split or retyped since. A number carried across that boundary would be a
    // confidence about a span that no longer exists, which is worse than no number (ADR-215 makes
    // the same argument about a touched section).

    if (ImGui::Button("Split at playhead")) {
        const double at = engine.timelineClock().seconds;
        if (const auto made = song::splitSection(piece.sectionTimeline, snapSection(engine, at))) {
            selected_ = static_cast<int>(*made);
            piece.refreshSectionMarkers();
            touch();
            status_ = "split; the new section is yours, not the analyzer's";
        } else {
            status_ = "the playhead is not far enough inside a section to split it";
        }
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(piece.sectionTimeline.sections.size() <= 1);
    if (ImGui::Button("Delete")) {
        if (song::removeSection(piece.sectionTimeline, index)) {
            selection_ = Selection::None;
            selected_ = -1;
            piece.refreshSectionMarkers();
            touch();
        }
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Its time goes to the section before it, so the song stays covered.");
    }
}

// The performer rules, as a list somebody can actually edit (ADR-216).
//
// These were authored by hand in the project JSON until now, which made the one control that
// consumes them permanently greyed out for anybody who had not read the file format. The rules are
// small -- a section kind, who, what, and the verb's object -- so this is a list rather than a
// system.
//
// **Every field is a picker where a picker is possible**, and that is the whole design. A free text
// box here would let somebody name an entity the scene does not have or a verb the action system
// cannot map, and both of those fail at RUN time with a log line nobody is reading. Choosing from
// what exists cannot produce either.
void SequencePanel::drawPerformerRules(app::Engine& engine) {
    seq::Sequence& piece = engine.sequence();
    // Open when this project HAS rules: a collapsed header is the right default for something
    // nobody has used, and the wrong one for something already in the file, where it hides the only
    // place those rules are visible.
    const ImGuiTreeNodeFlags flags =
        piece.sectionPerformance.empty() ? 0 : ImGuiTreeNodeFlags_DefaultOpen;
    if (!ImGui::CollapsingHeader("Performers", flags)) {
        return;
    }
    ImGui::TextDisabled("What the CAST does on a section boundary -- walk, turn, pose, react.\n"
                        "Separate from Song Mode, which directs cameras. Neither needs the other.");

    scene::Composition* composition = engine.composition();
    if (composition == nullptr) {
        ImGui::TextDisabled("no scene loaded");
        return;
    }

    // What this scene actually contains, so a rule cannot name something that is not there.
    std::vector<const char*> entityNames;
    for (const auto& entity : composition->entityWorld().entities()) {
        entityNames.push_back(entity->name().c_str());
    }
    if (entityNames.empty()) {
        ImGui::TextDisabled("this scene has no characters to direct");
        return;
    }

    static constexpr const char* kVerbs[] = {"wait", "move",    "face",    "pose",
                                             "interact", "equip", "unequip", "set"};
    const auto sections = signals::allMusicalSections();
    std::vector<const char*> sectionNames;
    sectionNames.reserve(sections.size());
    for (const signals::MusicalSection kind : sections) {
        sectionNames.push_back(signals::musicalSectionName(kind));
    }

    const auto indexOf = [](const std::vector<const char*>& list, std::string_view value) {
        for (std::size_t i = 0; i < list.size(); ++i) {
            if (value == list[i]) {
                return static_cast<int>(i);
            }
        }
        return 0;
    };

    int removeAt = -1;
    for (std::size_t i = 0; i < piece.sectionPerformance.entries.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        seq::SectionPerformanceEntry& entry = piece.sectionPerformance.entries[i];

        int section = indexOf(sectionNames, signals::musicalSectionName(entry.kind));
        ImGui::SetNextItemWidth(110);
        if (ImGui::Combo("##section", &section, sectionNames.data(),
                         static_cast<int>(sectionNames.size()))) {
            entry.kind = sections[static_cast<std::size_t>(section)];
            touch();
        }
        ImGui::SameLine();

        int subject = indexOf(entityNames, entry.direction.subject);
        ImGui::SetNextItemWidth(110);
        if (ImGui::Combo("##subject", &subject, entityNames.data(),
                         static_cast<int>(entityNames.size()))) {
            entry.direction.subject = entityNames[static_cast<std::size_t>(subject)];
            touch();
        }
        ImGui::SameLine();

        int verb = indexOf({std::begin(kVerbs), std::end(kVerbs)}, entry.direction.verb);
        ImGui::SetNextItemWidth(90);
        if (ImGui::Combo("##verb", &verb, kVerbs, IM_ARRAYSIZE(kVerbs))) {
            entry.direction.verb = kVerbs[verb];
            // The argument means something different for the new verb, so keeping the old one would
            // produce a rule that reads sensibly and does nothing -- an activity name in a target
            // slot, or an entity name where an activity belongs.
            entry.direction.argument.clear();
            touch();
        }
        ImGui::SameLine();

        // **The argument follows the verb**, which is the difference between a form you can use and
        // one where you have to already know the answer. `move` and `face` travel toward a
        // character; `pose` names an activity the rig has; the rest are free or take nothing.
        const std::string& v = entry.direction.verb;
        if (v == "move" || v == "face") {
            int target = indexOf(entityNames, entry.direction.argument);
            ImGui::SetNextItemWidth(110);
            if (ImGui::Combo("##argument", &target, entityNames.data(),
                             static_cast<int>(entityNames.size()))) {
                entry.direction.argument = entityNames[static_cast<std::size_t>(target)];
                touch();
            }
        } else if (v == "pose") {
            // The activities this particular character has, not a fixed list: the packs differ, and
            // offering a clip a rig does not carry is how a rule silently does nothing.
            std::vector<const char*> activities;
            if (const entity::Entity* e = composition->entityWorld().find(entry.direction.subject)) {
                for (const auto& [activity, clip] : e->desc().clips) {
                    static_cast<void>(clip);
                    activities.push_back(activity.c_str());
                }
            }
            if (activities.empty()) {
                ImGui::TextDisabled("(no activities)");
            } else {
                int activity = indexOf(activities, entry.direction.argument);
                ImGui::SetNextItemWidth(110);
                if (ImGui::Combo("##argument", &activity, activities.data(),
                                 static_cast<int>(activities.size()))) {
                    entry.direction.argument = activities[static_cast<std::size_t>(activity)];
                    touch();
                }
            }
        } else if (v == "wait") {
            ImGui::TextDisabled("(nothing to aim at)");
        } else {
            char buffer[96];
            std::strncpy(buffer, entry.direction.argument.c_str(), sizeof(buffer) - 1);
            buffer[sizeof(buffer) - 1] = '\0';
            ImGui::SetNextItemWidth(110);
            if (ImGui::InputText("##argument", buffer, sizeof(buffer))) {
                entry.direction.argument = buffer;
                touch();
            }
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) {
            removeAt = static_cast<int>(i);
        }
        ImGui::PopID();
    }
    if (removeAt >= 0) {
        piece.sectionPerformance.entries.erase(piece.sectionPerformance.entries.begin() + removeAt);
        touch();
    }

    if (ImGui::Button("+ Add rule")) {
        seq::SectionPerformanceEntry entry;
        entry.kind = sections.front();
        entry.direction.subject = entityNames.front();
        entry.direction.verb = "pose";
        piece.sectionPerformance.entries.push_back(std::move(entry));
        touch();
    }
    if (!piece.sectionPerformance.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%zu rule(s) -- tick \"Generate performer actions\" when you import",
                            piece.sectionPerformance.entries.size());
    }
}

void SequencePanel::drawSceneSlots(app::Engine& engine) {
    seq::Sequence& piece = engine.sequence();
    if (!ImGui::CollapsingHeader("Scenes")) {
        return;
    }
    ImGui::TextDisabled("A slot is a node a shot can cut to. Switching is visibility, so every\n"
                        "slot stays loaded and a cut costs nothing.");
    scene::Composition* composition = engine.composition();
    for (std::size_t i = 0; i < piece.scenes.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        seq::SceneSlot& slot = piece.scenes[i];
        char id[96];
        std::strncpy(id, slot.id.c_str(), sizeof(id) - 1);
        id[sizeof(id) - 1] = '\0';
        ImGui::SetNextItemWidth(120);
        if (ImGui::InputText("id", id, sizeof(id))) {
            slot.id = id;
            touch();
        }
        ImGui::SameLine();
        char node[96];
        std::strncpy(node, slot.node.c_str(), sizeof(node) - 1);
        node[sizeof(node) - 1] = '\0';
        ImGui::SetNextItemWidth(160);
        if (ImGui::InputText("node", node, sizeof(node))) {
            slot.node = node;
            touch();
        }
        ImGui::SameLine();
        const bool exists = composition != nullptr && composition->findNode(slot.nodeName()) != nullptr;
        if (exists) {
            ImGui::TextDisabled("ok");
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "no such node");
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) {
            piece.scenes.erase(piece.scenes.begin() + static_cast<std::ptrdiff_t>(i));
            touch();
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }
    if (ImGui::Button("Add Slot")) {
        piece.scenes.push_back(seq::SceneSlot{.id = fmt::format("scene{}", piece.scenes.size() + 1)});
        touch();
    }
}

// A camera behaviour's controls, and **only the ones the chosen behaviour reads**.
//
// The mandate is explicit about this and it is right: a chase does not have an orbit radius, and
// showing one invites somebody to set it and wonder why nothing moved. So the shared controls are
// drawn once and the rest is a switch.
void SequencePanel::drawBehaviorInspector(app::Engine& engine, seq::Shot& shot) {
    seq::Sequence& piece = engine.sequence();
    seq::CameraBehavior& b = shot.camera.behavior;

    // Which behaviour. Separate from the preset buttons, because the preset is a stamp and this is
    // the shot's own state -- changing it here keeps the offsets you have tuned.
    {
        int kind = static_cast<int>(b.kind);
        const char* kinds[] = {"chase", "orbit", "pov"};
        if (ImGui::Combo("behavior", &kind, kinds, IM_ARRAYSIZE(kinds))) {
            b.kind = static_cast<seq::CameraBehaviorKind>(kind);
            touch();
        }
    }

    // The performer. The list is the piece's cast for the same reason `look at`'s is: a behaviour is
    // resolved at bake against `Actor::positionAt`, so it can only follow something whose position
    // is a function of time.
    {
        std::vector<const char*> names{"(none)"};
        int current = 0;
        for (std::size_t i = 0; i < piece.actors.size(); ++i) {
            names.push_back(piece.actors[i].id.c_str());
            if (piece.actors[i].id == b.actor) {
                current = static_cast<int>(i) + 1;
            }
        }
        if (ImGui::Combo("performer", &current, names.data(), static_cast<int>(names.size()))) {
            b.actor = current == 0 ? std::string{}
                                   : piece.actors[static_cast<std::size_t>(current - 1)].id;
            touch();
        }
        if (b.actor.empty()) {
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.45f, 1.0f),
                               "a %s camera needs a performer to be about",
                               seq::cameraBehaviorName(b.kind));
        }
    }

    switch (b.kind) {
    case seq::CameraBehaviorKind::Chase:
        // Spelled as the three words an operator would use, not as x/y/z: "four behind, two above"
        // is the instruction, and the axis convention is an implementation detail of it.
        if (ImGui::DragFloat("behind", &shot.camera.behavior.offset.z, 0.1f, -200.0f, 200.0f,
                             "%.2f m")) {
            touch();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Negative is behind the performer, positive is in front of them.");
        }
        if (ImGui::DragFloat("above", &b.offset.y, 0.1f, -200.0f, 200.0f, "%.2f m")) {
            touch();
        }
        if (ImGui::DragFloat("lateral", &b.offset.x, 0.1f, -200.0f, 200.0f, "%.2f m")) {
            touch();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Positive is the performer's right.");
        }
        if (ImGui::Checkbox("offset turns with the performer", &b.actorSpace)) {
            touch();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("On: \"behind\" stays behind through a turn.\n"
                              "Off: the offset is world axes, so the camera holds a compass bearing\n"
                              "while the performer turns under it.");
        }
        {
            auto lag = static_cast<float>(b.lagSeconds);
            if (ImGui::DragFloat("lag", &lag, 0.01f, 0.0f, 5.0f, "%.2f s")) {
                b.lagSeconds = static_cast<double>(std::max(0.0f, lag));
                touch();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Stand where the performer WAS, this long ago.\n"
                                  "A time lag, not a spring -- so scrubbing to a frame and playing\n"
                                  "to it give the same camera. It trails and catches up; it does\n"
                                  "not overshoot and settle.");
            }
        }
        break;
    case seq::CameraBehaviorKind::Orbit:
        if (ImGui::DragFloat("radius", &b.radius, 0.1f, 0.1f, 1000.0f, "%.2f m")) {
            touch();
        }
        if (ImGui::DragFloat("height", &b.height, 0.1f, -200.0f, 200.0f, "%.2f m")) {
            touch();
        }
        if (ImGui::DragFloat("from angle", &b.startDegrees, 1.0f, -1440.0f, 1440.0f, "%.0f deg")) {
            touch();
        }
        if (ImGui::DragFloat("to angle", &b.endDegrees, 1.0f, -1440.0f, 1440.0f, "%.0f deg")) {
            touch();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("The arc travelled over the shot. A full circle is 360 degrees\n"
                              "further than the start; the direction is the sign.\n"
                              "Evaluated from shot time, so the same frame is the same pose.");
        }
        if (ImGui::Checkbox("ease the arc", &b.easeInOut)) {
            touch();
        }
        break;
    case seq::CameraBehaviorKind::Pov:
        if (ImGui::DragFloat("eye height", &b.eyeOffset.y, 0.05f, -10.0f, 20.0f, "%.2f m")) {
            touch();
        }
        if (ImGui::DragFloat("forward", &b.eyeOffset.z, 0.05f, -10.0f, 10.0f, "%.2f m")) {
            touch();
        }
        if (ImGui::DragFloat("lateral ", &b.eyeOffset.x, 0.05f, -10.0f, 10.0f, "%.2f m")) {
            touch();
        }
        break;
    }

    // ---- aim: shared, because where a camera looks is a separate decision from where it is -------
    {
        int aim = static_cast<int>(b.aim);
        const char* aims[] = {"the performer", "where they are going", "a fixed point"};
        if (ImGui::Combo("aim at", &aim, aims, IM_ARRAYSIZE(aims))) {
            b.aim = static_cast<seq::CameraAim>(aim);
            touch();
        }
        if (b.aim == seq::CameraAim::Custom) {
            if (ImGui::DragFloat3("point", &b.aimPoint.x, 0.1f)) {
                touch();
            }
        } else if (ImGui::DragFloat3("aim offset", &b.aimOffset.x, 0.05f)) {
            touch();
        }
        auto ahead = static_cast<float>(b.lookAheadSeconds);
        if (ImGui::DragFloat("look ahead", &ahead, 0.01f, 0.0f, 5.0f, "%.2f s")) {
            b.lookAheadSeconds = static_cast<double>(std::max(0.0f, ahead));
            touch();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Aim at where the performer will be, this far ahead.\n"
                              "Leads them into a turn instead of following them round it.");
        }
    }

    if (ImGui::DragFloat("clearance", &b.clearance, 0.05f, 0.0f, 50.0f, "%.2f m")) {
        touch();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Keep the camera at least this far above the ground.\n"
                          "0 leaves it alone. Applied at bake, where the whole path is known, so a\n"
                          "chase that would cross a hill is lifted over it before anything renders.\n"
                          "The ground only -- not trunks or rocks.");
    }
    if (ImGui::DragInt("samples", &shot.camera.samples, 1.0f, 2, 256)) {
        touch();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("How many camera keys this shot bakes. A behaviour follows something that\n"
                          "moves, so it wants more than a move does.");
    }
}

void SequencePanel::drawShotInspector(app::Engine& engine, seq::Shot& shot) {
    seq::Sequence& piece = engine.sequence();
    ImGui::SeparatorText("Transitions");
    const char* transitions[] = {"cut", "fade in", "fade out"};
    int in = static_cast<int>(shot.in.kind);
    ImGui::SetNextItemWidth(100);
    if (ImGui::Combo("in", &in, transitions, IM_ARRAYSIZE(transitions))) {
        shot.in.kind = static_cast<seq::TransitionKind>(in);
        touch();
    }
    if (shot.in.kind != seq::TransitionKind::Cut) {
        ImGui::SameLine();
        auto seconds = static_cast<float>(shot.in.seconds);
        ImGui::SetNextItemWidth(90);
        if (ImGui::DragFloat("##inlen", &seconds, 0.02f, 0.0f, 10.0f, "%.2f s")) {
            shot.in.seconds = static_cast<double>(seconds);
            touch();
        }
    }
    int out = static_cast<int>(shot.out.kind);
    ImGui::SetNextItemWidth(100);
    if (ImGui::Combo("out", &out, transitions, IM_ARRAYSIZE(transitions))) {
        shot.out.kind = static_cast<seq::TransitionKind>(out);
        touch();
    }
    if (shot.out.kind != seq::TransitionKind::Cut) {
        ImGui::SameLine();
        auto seconds = static_cast<float>(shot.out.seconds);
        ImGui::SetNextItemWidth(90);
        if (ImGui::DragFloat("##outlen", &seconds, 0.02f, 0.0f, 10.0f, "%.2f s")) {
            shot.out.seconds = static_cast<double>(seconds);
            touch();
        }
    }

    ImGui::SeparatorText("Shot");
    char name[96];
    std::strncpy(name, shot.name.c_str(), sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';
    if (ImGui::InputText("name", name, sizeof(name))) {
        shot.name = name;
        touch();
    }

    auto start = static_cast<float>(shot.startSeconds);
    auto length = static_cast<float>(shot.durationSeconds);
    if (ImGui::DragFloat("start", &start, 0.05f, 0.0f, 100000.0f, "%.2f s")) {
        shot.startSeconds = snap(engine, static_cast<double>(start));
        touch();
    }
    if (ImGui::DragFloat("duration", &length, 0.05f, 0.25f, 100000.0f, "%.2f s")) {
        shot.durationSeconds = std::max(0.25, static_cast<double>(length));
        touch();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("-> %s", clock(shot.endSeconds()).c_str());

    // Scene slot.
    {
        std::vector<const char*> names{"(inherit)"};
        int current = 0;
        for (std::size_t i = 0; i < piece.scenes.size(); ++i) {
            names.push_back(piece.scenes[i].id.c_str());
            if (piece.scenes[i].id == shot.scene) {
                current = static_cast<int>(i) + 1;
            }
        }
        if (ImGui::Combo("scene", &current, names.data(), static_cast<int>(names.size()))) {
            shot.scene = current == 0 ? std::string{} : piece.scenes[static_cast<std::size_t>(current - 1)].id;
            touch();
        }
    }

    // Camera.
    ImGui::SeparatorText("Camera");
    int kind = static_cast<int>(shot.camera.kind);
    const char* kinds[] = {"inherit", "move", "keys", "behavior"};
    if (ImGui::Combo("kind", &kind, kinds, IM_ARRAYSIZE(kinds))) {
        shot.camera.kind = static_cast<seq::CameraKind>(kind);
        touch();
    }
    if (shot.camera.kind == seq::CameraKind::Move) {
        ImGui::TextDisabled("preset");
        for (const seq::CameraPreset preset : seq::allCameraPresets()) {
            ImGui::SameLine();
            if (ImGui::SmallButton(seq::cameraPresetName(preset))) {
                app::FocalTarget subject;
                subject.radius = std::max(shot.camera.move.subject.radius, 1.0f);
                subject.position = shot.camera.move.subject.position;
                subject.name = shot.camera.move.subject.name;
                if (const seq::Actor* actor = piece.actorNamed(shot.camera.lookAtActor)) {
                    subject.position = actor->positionAt(shot.startSeconds);
                    subject.name = actor->id;
                }
                const std::string keepLookAt = shot.camera.lookAtActor;
                shot.camera = seq::cameraFromPreset(preset, subject);
                shot.camera.lookAtActor = keepLookAt;
                // A behaviour needs a performer, and the shot usually already names one. Carrying
                // it over is the difference between a preset that works on the click and one that
                // lands refusing to validate until you notice a second empty field.
                if (shot.camera.kind == seq::CameraKind::Behavior &&
                    shot.camera.behavior.actor.empty()) {
                    shot.camera.behavior.actor =
                        !keepLookAt.empty() ? keepLookAt
                                            : (piece.actors.empty() ? std::string{}
                                                                    : piece.actors.front().id);
                }
                touch();
            }
        }
        ImGui::Spacing();
        if (ImGui::DragFloat3("subject", &shot.camera.move.subject.position.x, 0.1f)) {
            touch();
        }
        if (ImGui::DragFloat("radius", &shot.camera.move.subject.radius, 0.1f, 0.1f, 10000.0f)) {
            touch();
        }
        if (ImGui::DragFloat("focal mm", &shot.camera.move.composition.focalLength, 0.5f, 8.0f, 400.0f)) {
            touch();
        }
        if (ImGui::DragFloat("from", &shot.camera.move.startDistance, 0.1f, 0.1f, 100000.0f)) {
            touch();
        }
        if (ImGui::DragFloat("to", &shot.camera.move.endDistance, 0.1f, 0.1f, 100000.0f)) {
            touch();
        }
        // How far the camera swings around the subject across the shot, in degrees.
        //
        // The azimuth *pair* is what the shot actually stores and what `cameraAt` reads; a start and
        // an end angle in radians are not two numbers anybody wants to type. So the control is the
        // difference, in degrees, and the start angle stays wherever the preset put it -- which
        // keeps "where the camera stands" and "how far it travels" as separate decisions.
        //
        // Zero means a held viewpoint. Follow ships at zero for that reason; Wide, Close, Tracking
        // and Reveal ship with their own drift because the movement is the shot.
        {
            float drift = glm::degrees(shot.camera.move.endAzimuth - shot.camera.move.startAzimuth);
            if (ImGui::DragFloat("drift", &drift, 0.25f, -360.0f, 360.0f, "%.1f deg")) {
                shot.camera.move.endAzimuth =
                    shot.camera.move.startAzimuth + glm::radians(drift);
                touch();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "How far the camera swings around the subject over the shot.\n"
                    "0 holds the viewpoint still -- the aim can still follow a performer.\n"
                    "The eye moves at a constant distance and height, so a small drift is\n"
                    "read as parallax rather than as a move.");
            }
        }
        if (ImGui::DragInt("samples", &shot.camera.samples, 1.0f, 2, 256)) {
            touch();
        }
    } else if (shot.camera.kind == seq::CameraKind::Behavior) {
        drawBehaviorInspector(engine, shot);
    } else if (shot.camera.kind == seq::CameraKind::Keys) {
        for (std::size_t i = 0; i < shot.camera.keys.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            seq::CameraKey& key = shot.camera.keys[i];
            auto t = static_cast<float>(key.timeSeconds);
            ImGui::SetNextItemWidth(70);
            if (ImGui::DragFloat("t", &t, 0.02f, 0.0f, static_cast<float>(shot.durationSeconds))) {
                key.timeSeconds = static_cast<double>(t);
                touch();
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(180);
            if (ImGui::DragFloat3("pos", &key.position.x, 0.1f)) {
                touch();
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(180);
            if (ImGui::DragFloat3("aim", &key.target.x, 0.1f)) {
                touch();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("x")) {
                shot.camera.keys.erase(shot.camera.keys.begin() + static_cast<std::ptrdiff_t>(i));
                touch();
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
        if (ImGui::Button("Key from viewport")) {
            // The one gesture that makes hand-authored camera work bearable: fly there, press this.
            seq::CameraKey key;
            key.timeSeconds = std::clamp(engine.timelineClock().seconds - shot.startSeconds, 0.0,
                                         shot.durationSeconds);
            key.position = engine.scene().camera.position;
            key.target = engine.scene().camera.target;
            shot.camera.keys.push_back(key);
            std::stable_sort(shot.camera.keys.begin(), shot.camera.keys.end(),
                             [](const seq::CameraKey& a, const seq::CameraKey& b) {
                                 return a.timeSeconds < b.timeSeconds;
                             });
            touch();
        }
    }

    if (shot.camera.kind != seq::CameraKind::Inherit) {
        std::vector<const char*> actors{"(none)"};
        int current = 0;
        for (std::size_t i = 0; i < piece.actors.size(); ++i) {
            actors.push_back(piece.actors[i].id.c_str());
            if (piece.actors[i].id == shot.camera.lookAtActor) {
                current = static_cast<int>(i) + 1;
            }
        }
        if (ImGui::Combo("look at", &current, actors.data(), static_cast<int>(actors.size()))) {
            shot.camera.lookAtActor =
                current == 0 ? std::string{} : piece.actors[static_cast<std::size_t>(current - 1)].id;
            touch();
        }
        if (!shot.camera.lookAtActor.empty()) {
            if (ImGui::DragFloat("eye height", &shot.camera.lookAtHeight, 0.05f, 0.0f, 100.0f)) {
                touch();
            }
            if (ImGui::SliderFloat("weight", &shot.camera.lookAtWeight, 0.0f, 1.0f)) {
                touch();
            }
        }
    }

    if (ImGui::Button("Delete shot")) {
        piece.shots.erase(piece.shots.begin() + selected_);
        selection_ = Selection::None;
        selected_ = -1;
        touch();
    }
}

void SequencePanel::drawActorInspector(app::Engine& engine, seq::Actor& actor) {
    seq::Sequence& piece = engine.sequence();
    ImGui::SeparatorText("Actor");
    char node[96];
    std::strncpy(node, actor.node.c_str(), sizeof(node) - 1);
    node[sizeof(node) - 1] = '\0';
    if (ImGui::InputText("node", node, sizeof(node))) {
        actor.node = node;
        touch();
    }
    scene::Composition* composition = engine.composition();
    const scene::CompositionNode* driven =
        composition != nullptr ? composition->findNode(actor.nodeName()) : nullptr;
    if (driven == nullptr) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "no node called '%s' in this scene",
                           actor.nodeName().c_str());
    }

    ImGui::TextDisabled("Position keys");
    for (std::size_t i = 0; i < actor.keys.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        seq::ActorKey& key = actor.keys[i];
        auto t = static_cast<float>(key.timeSeconds);
        ImGui::SetNextItemWidth(70);
        if (ImGui::DragFloat("t", &t, 0.05f, 0.0f, 100000.0f)) {
            key.timeSeconds = static_cast<double>(t);
            touch();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(200);
        if (ImGui::DragFloat3("at", &key.position.x, 0.1f)) {
            touch();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) {
            actor.keys.erase(actor.keys.begin() + static_cast<std::ptrdiff_t>(i));
            touch();
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }
    if (ImGui::Button("Key here")) {
        seq::ActorKey key;
        key.timeSeconds = engine.timelineClock().seconds;
        key.position = driven != nullptr ? driven->transform.position : glm::vec3(0.0f);
        actor.keys.push_back(key);
        std::stable_sort(actor.keys.begin(), actor.keys.end(),
                         [](const seq::ActorKey& a, const seq::ActorKey& b) {
                             return a.timeSeconds < b.timeSeconds;
                         });
        touch();
    }

    ImGui::TextDisabled("Animation cues");
    // The clip names the node's rig actually has, so a typo is impossible rather than merely
    // warned about.
    std::vector<std::string> states;
    if (driven != nullptr) {
        for (const scene::RigId id : driven->rigs) {
            if (id < engine.scene().rigs.size()) {
                for (const scene::AnimationState& state : engine.scene().rigs[id].player.states()) {
                    if (std::find(states.begin(), states.end(), state.name) == states.end()) {
                        states.push_back(state.name);
                    }
                }
            }
        }
    }
    for (std::size_t i = 0; i < actor.clips.size(); ++i) {
        ImGui::PushID(static_cast<int>(i + 1000));
        seq::ClipCue& cue = actor.clips[i];
        auto t = static_cast<float>(cue.timeSeconds);
        ImGui::SetNextItemWidth(70);
        if (ImGui::DragFloat("t", &t, 0.05f, 0.0f, 100000.0f)) {
            cue.timeSeconds = snap(engine, static_cast<double>(t));
            touch();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120);
        if (states.empty()) {
            char clipName[64];
            std::strncpy(clipName, cue.clip.c_str(), sizeof(clipName) - 1);
            clipName[sizeof(clipName) - 1] = '\0';
            if (ImGui::InputText("clip", clipName, sizeof(clipName))) {
                cue.clip = clipName;
                touch();
            }
        } else {
            std::vector<const char*> names;
            int current = 0;
            for (std::size_t s = 0; s < states.size(); ++s) {
                names.push_back(states[s].c_str());
                if (states[s] == cue.clip) {
                    current = static_cast<int>(s);
                }
            }
            if (ImGui::Combo("clip", &current, names.data(), static_cast<int>(names.size()))) {
                cue.clip = states[static_cast<std::size_t>(current)];
                touch();
            }
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90);
        if (ImGui::DragFloat("rate", &cue.speed, 0.01f, 0.05f, 4.0f)) {
            touch();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) {
            actor.clips.erase(actor.clips.begin() + static_cast<std::ptrdiff_t>(i));
            touch();
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }
    if (ImGui::Button("Delete actor")) {
        piece.actors.erase(piece.actors.begin() + selected_);
        selection_ = Selection::None;
        selected_ = -1;
        touch();
        return;
    }
    ImGui::SameLine();
    if (ImGui::Button("Cue here")) {
        seq::ClipCue cue;
        cue.timeSeconds = snap(engine, engine.timelineClock().seconds);
        cue.clip = states.empty() ? std::string("Idle") : states.front();
        actor.clips.push_back(cue);
        std::stable_sort(actor.clips.begin(), actor.clips.end(),
                         [](const seq::ClipCue& a, const seq::ClipCue& b) {
                             return a.timeSeconds < b.timeSeconds;
                         });
        touch();
    }
}

void SequencePanel::drawOverlayInspector(app::Engine& engine, seq::OverlayCue& cue) {
    seq::Sequence& piece = engine.sequence();
    ImGui::SeparatorText(cue.kind == seq::OverlayKind::Shape ? "Graphic" : "Lyric");
    std::strncpy(textBuffer_, cue.content.c_str(), sizeof(textBuffer_) - 1);
    textBuffer_[sizeof(textBuffer_) - 1] = '\0';
    if (ImGui::InputTextMultiline("##text", textBuffer_, sizeof(textBuffer_),
                                  ImVec2(-1, ImGui::GetTextLineHeight() * 3.0f))) {
        cue.content = textBuffer_;
        touch();
    }
    auto start = static_cast<float>(cue.startSeconds);
    auto end = static_cast<float>(cue.endSeconds);
    if (ImGui::DragFloat("start", &start, 0.02f, 0.0f, 100000.0f, "%.2f s")) {
        cue.startSeconds = snap(engine, static_cast<double>(start));
        touch();
    }
    if (ImGui::DragFloat("end", &end, 0.02f, 0.0f, 100000.0f, "%.2f s")) {
        cue.endSeconds = std::max(cue.startSeconds + 0.1, snap(engine, static_cast<double>(end)));
        touch();
    }
    if (cue.kind == seq::OverlayKind::Text) {
        std::vector<const char*> styles;
        int current = 0;
        const auto names = seq::overlayStyleNames();
        for (std::size_t i = 0; i < names.size(); ++i) {
            styles.push_back(names[i].data());
            if (names[i] == cue.style) {
                current = static_cast<int>(i);
            }
        }
        if (ImGui::Combo("style", &current, styles.data(), static_cast<int>(styles.size()))) {
            cue.style = names[static_cast<std::size_t>(current)];
            touch();
        }
    }
    if (ImGui::DragFloat2("anchor", &cue.anchor.x, 0.005f, -1.0f, 2.0f)) {
        touch();
    }
    if (ImGui::DragFloat("size", &cue.size, 0.01f, 0.05f, 10.0f)) {
        touch();
    }
    if (ImGui::ColorEdit4("colour", &cue.color.r)) {
        touch();
    }
    int preset = static_cast<int>(cue.preset);
    const char* presets[] = {"none", "fade in", "fade out", "fade in/out", "scale pop", "slide up",
                             "slide down"};
    if (ImGui::Combo("preset", &preset, presets, IM_ARRAYSIZE(presets))) {
        cue.preset = static_cast<seq::OverlayPreset>(preset);
        touch();
    }
    if (ImGui::DragInt("order", &cue.order, 0.2f, -100, 100)) {
        touch();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Lower draws first, under everything above it.");
    }
    if (ImGui::Button("Delete")) {
        piece.overlays.erase(piece.overlays.begin() + selected_);
        selection_ = Selection::None;
        selected_ = -1;
        touch();
    }
}

// ---- plumbing ----------------------------------------------------------------------------------

void SequencePanel::installIfDirty(app::Engine& engine) {
    if (!dirty_) {
        return;
    }
    dirty_ = false;
    auto report = engine.installSequence();
    if (!report) {
        status_ = report.error().message;
        return;
    }
    status_.clear();
    for (const std::string& warning : report->warnings) {
        status_ = warning; // the most recent one; the rest are in the log and in projectWarnings
    }
}

// ---- song structure (ADR-215, ADR-216) ----------------------------------------------------------

void SequencePanel::startStructureAnalysis(app::Engine& engine, bool merge) {
    if (work_ != nullptr) {
        return;
    }
    auto track = engine.trackShared();
    if (track == nullptr || track->empty()) {
        status_ = "no analyzed audio to look at";
        return;
    }
    // Claimed before the job starts, not after it finishes. Otherwise the auto-import trigger sees
    // "revision changed, no structure yet" on the very next frame and queues a second analysis of
    // the same audio.
    structureRevision_ = engine.audioRevision();
    auto work = std::make_shared<StructureWork>();
    work->track = std::move(track);
    work->revision = structureRevision_;
    work->merge = merge;
    work_ = work;
    status_.clear();

    const auto body = [work](app::JobContext& ctx) -> Result<void> {
        ctx.setStages({"beats and features", "self-similarity", "sections"});
        ctx.beginStage(0);
        ctx.setOperation("reading the song");
        // One call, and no progress fraction reported from inside it: `detectStructure` does not
        // know how far through itself it is, and a number made up here would look exactly like a
        // measured one (job_system.hpp's rule).
        ctx.beginStage(2);
        auto found = analysis::detectStructure(*work->track);
        if (!found) {
            work->error = found.error().message;
            work->finished.store(true, std::memory_order_release);
            return std::unexpected(found.error());
        }
        work->result = std::move(*found);
        work->finished.store(true, std::memory_order_release);
        return {};
    };

    if (jobs == nullptr) {
        // No job system: a headless host or a test. Inline, and stated as such rather than silently
        // skipped -- an editor never takes this path.
        struct Inline final : app::JobContext {
            void setStages(std::vector<std::string>) override {}
            void beginStage(int) override {}
            void setStageProgress(float) override {}
            void setOperation(std::string) override {}
            void log(std::string) override {}
            [[nodiscard]] bool shouldCancel() const override { return false; }
            [[nodiscard]] bool waitWhilePaused() override { return true; }
        } context;
        (void)body(context);
        // Applied here rather than left for the next frame's poll: a caller with no job system is
        // almost always a test, and "it appears one frame later" is a difference between the two
        // paths that a test would have to know about.
        pollStructureAnalysis(engine);
        return;
    }
    workJob_ = jobs->submit(app::JobRequest{.type = "analysis.structure",
                                            .name = "song structure",
                                            .body = body,
                                            .cancellable = true});
}

void SequencePanel::pollStructureAnalysis(app::Engine& engine) {
    if (work_ == nullptr || !work_->finished.load(std::memory_order_acquire)) {
        return;
    }
    const std::shared_ptr<StructureWork> work = std::exchange(work_, nullptr);
    workJob_ = 0;
    if (!work->error.empty()) {
        status_ = work->error;
        return;
    }
    if (work->revision != engine.audioRevision()) {
        // The audio changed while this was running. Its answer is about a file that is no longer
        // open, and merging it would put another song's sections on this one.
        status_ = "the audio changed while the song was being analyzed; nothing applied";
        return;
    }
    seq::Sequence& piece = engine.sequence();
    // Two layers, updated from the same detection under two policies (ADR-247). `structure` is the
    // analyzer's report and follows ADR-215's per-section rule; `sectionTimeline` is the film and
    // follows ADR-247's per-field one, so a passage somebody retyped keeps its type *and* gets the
    // better boundaries. Neither is derived from the other after this point.
    //
    // The status line reports the *analysis* layer only, because the section lane below it edits
    // that layer and nothing in the UI can yet author the film. Once the lane moves onto the
    // authored model, this is where `song::ReanalysisReport::summary()` belongs -- it is the one
    // that will have something to say about kept work.
    const song::ReanalysisPolicy policy =
        work->merge ? song::ReanalysisPolicy::Merge : song::ReanalysisPolicy::Replace;
    song::applyAnalysis(piece.sectionTimeline, work->result, piece.shotLanguage, policy);
    if (work->merge) {
        const seq::ReanalysisReport report = seq::reanalyze(piece.structure, work->result);
        status_ = report.summary();
    } else {
        piece.structure = std::move(work->result);
        status_ = fmt::format("{} section(s) found", piece.structure.sections.size());
    }
    piece.refreshSectionMarkers();
    if (engine.track() != nullptr) {
        piece.setBeatMarkers(engine.track()->beats().beatTimes);
    }
    // ADR-216: the generated Director events, if this project has a table and the box was ticked.
    //
    // After the markers are refreshed, because a generated event's trigger is a section NAME and the
    // markers are what carry those names -- generating before them would produce events keyed to the
    // previous analysis's sections.
    if (generateOnImport_ && !piece.sectionPerformance.empty()) {
        seq::GenerationOptions options;
        options.table = seq::tableFrom(piece.sectionPerformance);
        const seq::GeneratedDirection generated = seq::generatePerformanceEvents(piece.structure, options);
        // Replace what a previous generation left rather than accumulating: pressing Analyze twice
        // must not leave two events per section, and these are identified by their `director.` id
        // prefix precisely so a regeneration can find its own work.
        std::erase_if(piece.events, [&](const seq::SequenceEvent& e) {
            return e.id.rfind(options.idPrefix + ".", 0) == 0;
        });
        piece.events.insert(piece.events.end(), generated.events.begin(), generated.events.end());
        std::string note = fmt::format("{}, {} director event(s)", status_, generated.events.size());
        if (!generated.warnings.empty()) {
            // The declines, said out loud. "The table had no answer for bridge" is the single most
            // useful thing this can report while a table is being filled in, and an event count
            // alone cannot say it.
            note += " (" + generated.warnings.front();
            if (generated.warnings.size() > 1) {
                note += fmt::format(" and {} more", generated.warnings.size() - 1);
            }
            note += ")";
        }
        status_ = std::move(note);
    }
    touch();
}

double SequencePanel::snapSection(const app::Engine& engine, double seconds) const {
    if (sectionSnap_ == 0) {
        return seconds;
    }
    const analysis::AnalysisTrack* track = engine.track();
    if (track == nullptr) {
        return seconds;
    }
    const std::vector<double>& beats = track->beats().beatTimes;
    if (beats.empty()) {
        return seconds;
    }
    if (sectionSnap_ == 1) {
        return seq::snapTime(seconds, seq::SnapMode::Beats, beats);
    }
    // Bars: every fourth beat, which is the same assumption `seq::BakeOptions::beatsPerBar` makes
    // and is stated in one place there. The returned value is still a beat's own time.
    std::vector<double> bars;
    bars.reserve(beats.size() / 4 + 1);
    for (std::size_t i = 0; i < beats.size(); i += 4) {
        bars.push_back(beats[i]);
    }
    return seq::snapTime(seconds, seq::SnapMode::Beats, bars);
}

double SequencePanel::snap(const app::Engine& engine, double seconds) const {
    const auto mode = static_cast<seq::SnapMode>(std::clamp(snapMode_, 0, 3));
    switch (mode) {
    case seq::SnapMode::Off:
        return seconds;
    case seq::SnapMode::Frames:
        return seq::snapTime(seconds, mode, {}, std::max(1.0, engine.renderSettings().fps));
    case seq::SnapMode::Beats: {
        const analysis::AnalysisTrack* track = engine.track();
        if (track == nullptr) {
            return seconds;
        }
        return seq::snapTime(seconds, mode, track->beats().beatTimes);
    }
    case seq::SnapMode::Markers: {
        std::vector<double> points;
        for (const seq::Marker& marker : engine.sequence().markers) {
            if (marker.kind != seq::MarkerKind::Beat) {
                points.push_back(marker.timeSeconds);
            }
        }
        std::sort(points.begin(), points.end());
        return seq::snapTime(seconds, mode, points);
    }
    }
    return seconds;
}

const std::vector<double>& SequencePanel::beats(const app::Engine& engine) {
    const analysis::AnalysisTrack* track = engine.track();
    if (track == nullptr) {
        beatCache_.clear();
        beatRevision_ = 0;
        return beatCache_;
    }
    if (beatRevision_ != engine.audioRevision()) {
        beatCache_ = track->beats().beatTimes;
        beatRevision_ = engine.audioRevision();
    }
    return beatCache_;
}

void SequencePanel::drawAudioClips(app::Engine& engine) {
    const std::vector<audio::AudioClip>& clips = engine.audioClips();
    ImGui::TextDisabled("%zu clip(s)", clips.size());
    if (const audio::MixReport& mix = engine.audioMix(); mix.clipsMixed > 1) {
        ImGui::SameLine();
        ImGui::TextDisabled("mixed to %.1f s at %u Hz in %.0f ms", mix.durationSeconds, mix.sampleRate,
                            mix.millis);
    }
    ImGui::Separator();

    ImGui::SetNextItemWidth(-90.0f);
    ImGui::InputTextWithHint("##clippath", "path to an audio file", audioPath_, sizeof(audioPath_));
    ImGui::SameLine();
    ImGui::BeginDisabled(audioPath_[0] == '\0');
    if (ImGui::Button("Add", ImVec2(-1.0f, 0.0f))) {
        // At the playhead, which is where a person looking at the strip means. Appending at the end
        // would be right for a first clip and wrong for every one after it.
        std::vector<audio::AudioClip> next = clips;
        next.push_back(audio::AudioClip{.file = audioPath_, .startSeconds = engine.positionSeconds()});
        if (auto r = engine.setAudioClips(std::move(next)); !r) {
            status_ = r.error().message;
        } else {
            audioSelected_ = static_cast<int>(engine.audioClips().size()) - 1;
            audioPath_[0] = '\0';
        }
    }
    ImGui::EndDisabled();

    if (clips.empty()) {
        ImGui::TextWrapped("A project can be made of several files: a song and a spoken outro, two "
                           "cues with a gap, a stem set. They are mixed into one piece; drag them on "
                           "the strip to move or trim them.");
        return;
    }

    // Edited through a copy and applied once, so one field change is one re-mix rather than one per
    // keystroke of a drag.
    std::vector<audio::AudioClip> next = clips;
    bool changed = false;
    for (std::size_t i = 0; i < next.size(); ++i) {
        audio::AudioClip& clip = next[i];
        ImGui::PushID(static_cast<int>(i));
        const bool chosen = audioSelected_ == static_cast<int>(i);
        const std::string label = (clip.name.empty() ? clip.file.filename().string() : clip.name) +
                                  (engine.clipSource(clip.file) == nullptr ? "  (missing)" : "");
        if (ImGui::Selectable(label.c_str(), chosen)) {
            audioSelected_ = static_cast<int>(i);
        }
        if (chosen) {
            ImGui::Indent();
            changed |= ImGui::Checkbox("Play", &clip.enabled);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(70.0f);
            auto gain = clip.gain;
            if (ImGui::DragFloat("gain", &gain, 0.01f, 0.0f, 4.0f, "%.2f")) {
                clip.gain = gain;
                changed = true;
            }
            auto seconds = [&](const char* name, double& value, double low, const char* tip) {
                auto v = static_cast<float>(value);
                ImGui::SetNextItemWidth(80.0f);
                if (ImGui::DragFloat(name, &v, 0.02f, static_cast<float>(low), 1e5f, "%.2f s")) {
                    value = std::max(low, static_cast<double>(v));
                    changed = true;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", tip);
                }
            };
            seconds("start", clip.startSeconds, 0.0, "Where it sits on the timeline.");
            ImGui::SameLine();
            seconds("in", clip.inSeconds, 0.0, "How far into the file it starts.");
            ImGui::SameLine();
            seconds("length", clip.durationSeconds, 0.0, "0 plays to the end of the file.");
            seconds("fade in", clip.fadeInSeconds, 0.0,
                    "A cut between two takes clicks without one.");
            ImGui::SameLine();
            seconds("fade out", clip.fadeOutSeconds, 0.0, "Likewise at the other edge.");
            if (ImGui::SmallButton("Remove")) {
                next.erase(next.begin() + static_cast<std::ptrdiff_t>(i));
                audioSelected_ = -1;
                changed = true;
                ImGui::Unindent();
                ImGui::PopID();
                break;
            }
            ImGui::Unindent();
        }
        ImGui::PopID();
    }
    if (changed) {
        if (auto r = engine.setAudioClips(std::move(next)); !r) {
            status_ = r.error().message;
        }
    }
}

const audio::WaveformSummary& SequencePanel::waveform(const app::Engine& engine) {
    const std::shared_ptr<const audio::AudioFile> file = engine.audioFile();
    if (file == nullptr) {
        waveCache_ = {};
        waveRevision_ = 0;
        return waveCache_;
    }
    // Keyed on the engine's audio revision, so a load or a re-mix rebuilds and scrubbing does not.
    // The summary is the one expensive thing on this panel and it must happen exactly as often as
    // the audio changes -- no more, and crucially no less.
    if (waveRevision_ != engine.audioRevision()) {
        const auto started = std::chrono::steady_clock::now();
        waveCache_ = audio::summarise(*file);
        waveRevision_ = engine.audioRevision();
        // Logged because it is the one pass over the whole file this panel makes, and because a
        // line that appears once per load rather than once per frame is the cheapest possible proof
        // that the cache is a cache.
        log::info("sequence: waveform for '{}' summarised in {:.1f} ms ({} buckets over {:.1f} s)",
                  engine.audioPath().filename().string(),
                  std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count(),
                  waveCache_.bucketCount(), waveCache_.durationSeconds);
    }
    return waveCache_;
}

void SequencePanel::importLyrics(app::Engine& engine, const std::filesystem::path& path) {
    auto lines = seq::loadLyrics(path);
    if (!lines) {
        status_ = lines.error().message;
        return;
    }
    seq::LyricImport options;
    options.idPrefix = "lyric";
    auto cues = seq::lyricCues(*lines, options);
    seq::Sequence& piece = engine.sequence();
    // Replace a previous import rather than doubling it: an import is idempotent, because the
    // second thing anyone does after importing lyrics is import them again with a better offset.
    std::erase_if(piece.overlays, [&](const seq::OverlayCue& cue) {
        return cue.id.rfind(options.idPrefix, 0) == 0;
    });
    for (seq::OverlayCue& cue : cues) {
        piece.overlays.push_back(std::move(cue));
    }
    status_ = fmt::format("{} lyric line(s) from {}", lines->size(), path.filename().string());
    lyricPath_ = path.string();
    touch();
}

} // namespace avgen::ui
