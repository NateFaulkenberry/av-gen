#include "ui/sequence_panel.hpp"

#include "ui/shortcuts.hpp"
#include "ui/style.hpp"
#include "ui/theme.hpp"
#include "ui/ui_logic.hpp"

#include <chrono>

#include "analysis/analysis_track.hpp"
#include "analysis/structure.hpp"
#include "core/log.hpp"
#include "scene/composition.hpp"
#include "seq/layer_sink.hpp"
#include "seq/lyrics.hpp"
#include "seq/section_direction.hpp"
#include "seq/song_structure.hpp"
#include "song/from_analysis.hpp"

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
constexpr float kRulerHeight = 22.0f;
constexpr float kMarkerHeight = 16.0f;
constexpr float kLaneHeight = 26.0f;
constexpr float kLaneGap = 3.0f;
constexpr float kSectionLaneHeight = 22.0f;
// How far inside an edge still grabs it. Seven rather than five: five points is a comfortable
// target with a mouse and a fiddly one on a trackpad, and `blockZoneAt` shrinks it on a narrow
// block anyway, so the generous number costs nothing where it would have hurt.
constexpr float kEdgeGrab = 7.0f;
// The audio lane is half again as tall as the others. It is the only lane whose content is a
// picture rather than a label, and a waveform drawn three pixels high says nothing about the music.
constexpr float kAudioLaneHeight = kLaneHeight * 1.5f;
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
constexpr float kStripBottomReserve = 46.0f;
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
    drawStripControls(engine);
    ImGui::Separator();
    drawInspector(engine);
    installIfDirty(engine);
}

// ---- the toolbar -------------------------------------------------------------------------------

void SequencePanel::drawToolbar(app::Engine& engine) {
    seq::Sequence& piece = engine.sequence();

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 4.0f));
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
    ImGui::SetNextItemWidth(120);
    ImGui::SliderFloat("zoom", &zoom_, 0.25f, 8.0f, "%.2fx");
    ImGui::SameLine();
    if (ImGui::Button("Fit")) {
        zoom_ = 1.0f;
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

    const seq::InstallReport& report = engine.sequenceReport();
    ImGui::TextDisabled("%d track(s), %d key(s), %d layer(s)", report.trackCount, report.keyCount,
                        report.layersRealised);
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
    // The Director decision layer exists now, and so does the thing that applies what it produces
    // (`seq::actionFromEvent`, `Engine::applySectionActions`). What gates this is no longer whether
    // the code is written -- it is whether this project has said what a section should MAKE HAPPEN.
    //
    // That table is authored on purpose (ADR-216): `section_direction.hpp` refuses to hold an
    // opinion about the vocabulary of behaviours, because a built-in table saying *a Drop means the
    // visitor hovers* would put one scene's cast into a generic seam. So a project with no table
    // generates nothing, and the checkbox says that rather than producing an empty sequence and
    // calling it a result.
    const std::size_t directorRows = engine.sequence().sectionDirection.entries.size();
    ImGui::BeginDisabled(directorRows == 0);
    ImGui::Checkbox("Generate initial Director sequence", &generateOnImport_);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        if (directorRows == 0) {
            ImGui::SetTooltip("This project has no director table yet, so there is nothing to\n"
                              "generate. A table says what each kind of section should make\n"
                              "happen -- \"on a Drop, the visitor poses\" -- and lives in the\n"
                              "project's `sectionDirection`.");
        } else {
            ImGui::SetTooltip("Turn each section into a Director event, using this project's\n"
                              "%zu-row table. Sections whose kind the table does not name are\n"
                              "skipped, and the panel says which.",
                              directorRows);
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
    StripLanes lanes{.hasAudio = hasAudio,
                     .hasSections = !piece.sectionTimeline.sections.empty(),
                     .actorCount = piece.actors.size(),
                     .hasOverlays = !piece.overlays.empty(),
                     .rulerHeight = kRulerHeight,
                     .markerHeight = kMarkerHeight,
                     .laneHeight = kLaneHeight,
                     .audioLaneHeight = kAudioLaneHeight,
                     .sectionLaneHeight = kSectionLaneHeight,
                     .gap = kLaneGap};
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
        laneBackground(lanes.audioTop(), kAudioLaneHeight, false);
        laneHeader(lanes.audioTop(), kAudioLaneHeight, "Audio", audioSelected_ >= 0);
        const float mid = laneY + kAudioLaneHeight * 0.5f;
        const float halfHeight = kAudioLaneHeight * 0.5f - 3.0f;
        const double perPixel = span / static_cast<double>(width);

        for (std::size_t i = 0; i < engine.audioClips().size(); ++i) {
            const audio::AudioClip& clip = engine.audioClips()[i];
            const double end = audio::clipEndSeconds(clip, engine.clipSource(clip.file).get());
            const ImVec2 lo(std::max(toX(clip.startSeconds), axisX), laneY);
            const ImVec2 hi(std::min(toX(end), axisRight), laneY + kAudioLaneHeight);
            if (hi.x <= lo.x) {
                continue;
            }
            const bool chosen = audioSelected_ == static_cast<int>(i);
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

            if (const ImU32 outline = interactionOutline(over, chosen, false); outline != 0) {
                draw->AddRect(lo, hi, outline, 3.0f, 0, chosen ? 2.0f : 1.0f);
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
    laneBackground(lanes.shotsTop(), kLaneHeight, true);
    laneHeader(lanes.shotsTop(), kLaneHeight, "Shots", selection_ == Selection::Shot);
    const double now = engine.timelineClock().seconds;
    for (std::size_t i = 0; i < piece.shots.size(); ++i) {
        const seq::Shot& shot = piece.shots[i];
        ImVec2 a(toX(shot.startSeconds), laneY);
        ImVec2 b(toX(shot.endSeconds()), laneY + kLaneHeight);
        if (b.x < axisX || a.x > axisRight) {
            continue;
        }
        const float trueLeft = a.x;
        const float trueRight = b.x;
        a.x = std::max(a.x, axisX);
        b.x = std::min(b.x, axisRight);
        const bool isSelected = selection_ == Selection::Shot && selected_ == static_cast<int>(i);
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
        const float laneTop = lanes.actorsTop() + static_cast<float>(ai) * (kLaneHeight + kLaneGap);
        const bool isSelected = selection_ == Selection::Actor && selected_ == static_cast<int>(ai);
        laneBackground(laneTop, kLaneHeight, ai % 2 == 1);
        // The actor's own header: its id, and a dot that says whether it is visible. `Actor::visible`
        // is real state the bake reads, so the dot is a control rather than a decoration -- which is
        // why it is here and why there is no mute dot on the shots lane, where there is nothing for
        // one to mean.
        if (hovered && localY >= laneTop && localY < laneTop + kLaneHeight && hoverActorRow < 0) {
            hoverActorRow = static_cast<int>(ai);
        }
        const auto [ha, hb] = laneHeader(laneTop, kLaneHeight, actor.id.c_str(), isSelected);
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
            ImVec2 b(toX(to), origin.y + laneTop + kLaneHeight);
            if (b.x < axisX || a.x > axisRight) {
                continue;
            }
            a.x = std::max(a.x, axisX);
            b.x = std::min(b.x, axisRight);
            const bool over = overAxis && hoverLane == StripLane::Actors && mouse.x >= a.x &&
                              mouse.x <= b.x && localY >= laneTop && localY < laneTop + kLaneHeight;
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
        laneBackground(lanes.overlaysTop(), kLaneHeight, false);
        laneHeader(lanes.overlaysTop(), kLaneHeight, "Overlays", selection_ == Selection::Overlay);
        for (std::size_t i = 0; i < piece.overlays.size(); ++i) {
            const seq::OverlayCue& cue = piece.overlays[i];
            ImVec2 a(toX(cue.startSeconds), origin.y + lanes.overlaysTop());
            ImVec2 b(toX(cue.endSeconds), origin.y + lanes.overlaysTop() + kLaneHeight);
            if (b.x < axisX || a.x > axisRight) {
                continue;
            }
            const float trueLeft = a.x;
            const float trueRight = b.x;
            a.x = std::max(a.x, axisX);
            b.x = std::min(b.x, axisRight);
            const bool isSelected = selection_ == Selection::Overlay && selected_ == static_cast<int>(i);
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
        const StripLane lane = lanes.at(localY);
        if (overGutter) {
            // A header click selects the lane rather than scrubbing. It is the one place on the
            // strip where a click means "this row" instead of "this moment", which is exactly why
            // the headers are outside the time axis rather than floating over it.
            hitBlock = true;
            if (lane == StripLane::Actors) {
                const int row = static_cast<int>((localY - lanes.actorsTop()) / (kLaneHeight + kLaneGap));
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
            engine.seekSeconds(std::clamp(snap(engine, mouseTime), 0.0, duration));
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
            const int row = static_cast<int>((localY - lanes.actorsTop()) / (kLaneHeight + kLaneGap));
            if (row >= 0 && row < static_cast<int>(piece.actors.size())) {
                selection_ = Selection::Actor;
                selected_ = row;
                hitBlock = true;
            }
        }
        if (!hitBlock) {
            // Scrubbing. Spec 30 wants this to update everything, and it does: seekSeconds moves
            // the audio play-head, the timeline clock follows it, and every track is an evaluation.
            engine.seekSeconds(std::clamp(snap(engine, mouseTime), 0.0, duration));
        }
    }

    if (drag_ != Drag::None && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        const double t = snap(engine, mouseTime);
        const auto shot = [&]() -> seq::Shot* {
            return dragIndex_ >= 0 && dragIndex_ < static_cast<int>(piece.shots.size())
                       ? &piece.shots[static_cast<std::size_t>(dragIndex_)]
                       : nullptr;
        };
        const auto overlay = [&]() -> seq::OverlayCue* {
            return dragIndex_ >= 0 && dragIndex_ < static_cast<int>(piece.overlays.size())
                       ? &piece.overlays[static_cast<std::size_t>(dragIndex_)]
                       : nullptr;
        };
        switch (drag_) {
        case Drag::Playhead:
            engine.seekSeconds(std::clamp(t, 0.0, duration));
            break;
        case Drag::MoveShot:
            if (seq::Shot* s = shot(); s != nullptr) {
                s->startSeconds = std::max(0.0, t - dragGrab_);
            }
            break;
        case Drag::TrimShotEnd:
            if (seq::Shot* s = shot(); s != nullptr) {
                s->durationSeconds = std::max(kMinBlockSeconds, t - s->startSeconds);
            }
            break;
        case Drag::TrimShotStart:
            if (seq::Shot* s = shot(); s != nullptr) {
                // The end stays where it was: trimming the start of a clip is not the same gesture
                // as moving it, and a duration recomputed from the live end would do both at once.
                const double start = std::clamp(t, 0.0, dragAnchor_ - kMinBlockSeconds);
                s->startSeconds = start;
                s->durationSeconds = dragAnchor_ - start;
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
        engine.seekSeconds(std::clamp(snap(engine, mouseTime), 0.0, duration));
    }
    if (drag_ != Drag::None && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
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
        // release of a scrub rebuilt every track in the piece.
        if (finished != Drag::Playhead) {
            touch();
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
    if (hovered && ImGui::GetIO().MouseWheel != 0.0f) {
        const double anchor = mouseTime;
        zoom_ = std::clamp(zoom_ * (ImGui::GetIO().MouseWheel > 0.0f ? 1.15f : 1.0f / 1.15f), 0.25f, 64.0f);
        const double newSpan = duration / static_cast<double>(zoom_);
        view_ = anchor - (anchor - view_) * (newSpan / span);
    }
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
                seq::Shot copy = shot;
                copy.name = fmt::format("{} copy", shot.name);
                copy.startSeconds = shot.endSeconds();
                piece.shots.insert(piece.shots.begin() + static_cast<std::ptrdiff_t>(index) + 1,
                                   std::move(copy));
                selection_ = Selection::Shot;
                selected_ = menu_.index + 1;
                touch();
            }
            // Splitting is a real operation on a shot -- two shots whose durations add up to the
            // original's -- and it is offered only where it would produce two shots that are not
            // degenerate, rather than as a row that is always there and usually refuses.
            const bool splittable = at > shot.startSeconds + kMinBlockSeconds &&
                                    at < shot.endSeconds() - kMinBlockSeconds;
            if (menuAction("Split at pointer", nullptr, splittable)) {
                seq::Shot tail = shot;
                tail.name = fmt::format("{} b", shot.name);
                tail.startSeconds = at;
                tail.durationSeconds = shot.endSeconds() - at;
                tail.in = seq::Transition{seq::TransitionKind::Cut, 0.0};
                piece.shots[index].durationSeconds = at - shot.startSeconds;
                piece.shots[index].out = seq::Transition{seq::TransitionKind::Cut, 0.0};
                piece.shots.insert(piece.shots.begin() + static_cast<std::ptrdiff_t>(index) + 1,
                                   std::move(tail));
                touch();
            }
            ImGui::Separator();
            if (menuAction("Delete", shortcut::kDelete)) {
                piece.shots.erase(piece.shots.begin() + static_cast<std::ptrdiff_t>(index));
                selection_ = Selection::None;
                selected_ = -1;
                touch();
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
                piece.overlays.erase(piece.overlays.begin() + static_cast<std::ptrdiff_t>(index));
                selection_ = Selection::None;
                selected_ = -1;
                touch();
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
                clips.erase(clips.begin() + static_cast<std::ptrdiff_t>(index));
                audioSelected_ = -1;
                applyAudioClips(engine, std::move(clips));
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
    drawSceneSlots(engine);
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
    case Selection::None:
        ImGui::TextDisabled("Click a section, a shot, a character lane or a lyric to edit it.");
        break;
    }
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

void SequencePanel::drawShotInspector(app::Engine& engine, seq::Shot& shot) {
    seq::Sequence& piece = engine.sequence();
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
    const char* kinds[] = {"inherit", "move", "keys"};
    if (ImGui::Combo("kind", &kind, kinds, IM_ARRAYSIZE(kinds))) {
        shot.camera.kind = static_cast<seq::CameraKind>(kind);
        touch();
    }
    if (shot.camera.kind == seq::CameraKind::Move) {
        ImGui::TextDisabled("preset");
        for (const seq::CameraPreset preset :
             {seq::CameraPreset::Isometric, seq::CameraPreset::Follow, seq::CameraPreset::Wide,
              seq::CameraPreset::Close, seq::CameraPreset::TopDown, seq::CameraPreset::Tracking,
              seq::CameraPreset::Reveal}) {
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
        if (ImGui::DragInt("samples", &shot.camera.samples, 1.0f, 2, 256)) {
            touch();
        }
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
    if (generateOnImport_ && !piece.sectionDirection.empty()) {
        seq::GenerationOptions options;
        options.table = seq::tableFrom(piece.sectionDirection);
        const seq::GeneratedDirection generated = seq::generateDirectorEvents(piece.structure, options);
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
