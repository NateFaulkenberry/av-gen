#include "ui/sequence_panel.hpp"

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

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
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
constexpr float kRulerHeight = 20.0f;
constexpr float kMarkerHeight = 16.0f;
constexpr float kLaneHeight = 24.0f;
constexpr float kLaneGap = 3.0f;
constexpr float kEdgeGrab = 5.0f; // points either side of a block's right edge that resize it
// The audio lane is half again as tall as the others. It is the only lane whose content is a
// picture rather than a label, and a waveform drawn three pixels high says nothing about the music.
constexpr float kAudioLaneHeight = kLaneHeight * 1.5f;

ImU32 shotColour(int index, bool selected) {
    // Alternating so a cut is visible even between two shots on the same scene, and warmer when
    // selected rather than merely brighter -- a brighter blue next to a blue reads as "nearer".
    const ImVec4 surface = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
    const ImVec4 active = ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive);
    if (selected) {
        return ImGui::GetColorU32(ImVec4(active.x, active.y, active.z, 0.95f));
    }
    const float lift = index % 2 == 0 ? 0.08f : 0.03f;
    return ImGui::GetColorU32(ImVec4(std::min(surface.x + lift, 1.0f),
                                     std::min(surface.y + lift, 1.0f),
                                     std::min(surface.z + lift * 1.4f, 1.0f), 0.92f));
}

const char* kSnapNames[] = {"Off", "Frames", "Beats", "Markers"};
const char* kSectionSnapNames[] = {"free", "beat", "bar"};

// One colour per section function, so the shape of a song is readable without reading any of it.
// Warm for the payoffs, cool for the passages, grey for "the detector did not claim anything".
ImU32 sectionColour(avgen::analysis::SectionFunction f, bool selected) {
    using F = avgen::analysis::SectionFunction;
    ImU32 base = IM_COL32(88, 96, 112, 220);
    switch (f) {
    case F::Intro:
    case F::Outro:
        base = IM_COL32(72, 92, 116, 220);
        break;
    case F::Verse:
    case F::Instrumental:
        base = IM_COL32(64, 108, 104, 220);
        break;
    case F::PreChorus:
    case F::Build:
        base = IM_COL32(150, 120, 60, 225);
        break;
    case F::Chorus:
    case F::Drop:
    case F::FinalChorus:
        base = IM_COL32(176, 88, 86, 235);
        break;
    case F::Break:
    case F::Breakdown:
        base = IM_COL32(70, 74, 96, 220);
        break;
    case F::Bridge:
        base = IM_COL32(112, 84, 140, 225);
        break;
    case F::Other:
        base = IM_COL32(88, 96, 112, 220);
        break;
    }
    if (!selected) {
        return base;
    }
    // Lifted rather than outlined: the outline is what marks the boundary being dragged, and two
    // outlines in one lane is two things saying "this one".
    const ImVec4 c = ImGui::ColorConvertU32ToFloat4(base);
    return ImGui::GetColorU32(ImVec4(std::min(c.x + 0.18f, 1.0f), std::min(c.y + 0.18f, 1.0f),
                                     std::min(c.z + 0.18f, 1.0f), 1.0f));
}

} // namespace

void SequencePanel::draw(app::Engine& engine) {
    // Before anything is drawn, so a finished analysis is on screen in the frame it finished in
    // rather than the one after.
    pollStructureAnalysis(engine);
    // The one automatic trigger: audio arrived, the option is on, and this piece has no structure
    // of its own yet. A project that was saved with a structure is *not* re-analysed on open --
    // that is the whole point of caching it -- and re-running is an explicit button.
    if (analyseOnImport_ && work_ == nullptr && engine.track() != nullptr &&
        engine.audioRevision() != structureRevision_ &&
        engine.sequence().structure.sections.empty()) {
        startStructureAnalysis(engine, false);
    }
    drawToolbar(engine);
    ImGui::Separator();
    drawStrip(engine);
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
    if (ImGui::Button(busy ? "Analysing..." : "Analyse Song")) {
        // Re-running merges rather than replacing: `seq::reanalyse` puts back what a person moved
        // or named and reports it (ADR-215). Pressing this twice is safe, which is the property
        // that makes it worth having a button at all.
        startStructureAnalysis(engine, !piece.structure.sections.empty());
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
        ImGui::TextColored(ImVec4(0.6f, 0.82f, 0.95f, 1.0f), "analysing the song%s%s",
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
    ImGui::Checkbox("Analyse song structure", &analyseOnImport_);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Find the sections -- intro, verse, chorus -- and lay them on the\n"
                          "timeline, where you can move and rename them.");
    }
    // Present, and honest about being unavailable. Generation is a thin translation on top of the
    // Director decision layer (seq/section_direction.hpp), and that layer does not exist yet; a
    // checkbox that silently did nothing would be worse than one that says why.
    const bool directorReady = false;
    ImGui::BeginDisabled(!directorReady);
    ImGui::Checkbox("Generate initial Director sequence", &generateOnImport_);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Not yet: this turns each section into a Director event, and the\n"
                          "Director decision layer it would call is still being built.\n"
                          "The connection is seq/section_direction.hpp.");
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

    const float width = std::max(ImGui::GetContentRegionAvail().x, 80.0f);
    const bool hasAudio = engine.audioFile() != nullptr;
    // One description of where the lanes are, for the height, the drawing and the hit test alike.
    // They were three separate calculations, and when two of them disagreed a click meant to scrub
    // the music moved the music instead (ADR-103); the arithmetic is in `ui_logic.hpp` so it can be
    // checked without a window, and `tests/unit/test_ui_logic.cpp` checks it.
    const StripLanes lanes{.hasAudio = hasAudio,
                           .hasSections = !piece.structure.sections.empty(),
                           .actorCount = piece.actors.size(),
                           .hasOverlays = !piece.overlays.empty(),
                           .rulerHeight = kRulerHeight,
                           .markerHeight = kMarkerHeight,
                           .laneHeight = kLaneHeight,
                           .audioLaneHeight = kAudioLaneHeight,
                           .gap = kLaneGap};
    const float height = lanes.height();

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("strip", ImVec2(width, height),
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    // Time axis. `view_` is the leftmost second; `zoom_` how many screens the piece takes.
    const double span = duration / static_cast<double>(std::max(zoom_, 0.01f));
    view_ = std::clamp(view_, 0.0, std::max(0.0, duration - span));
    const auto toX = [&](double seconds) {
        return origin.x + static_cast<float>((seconds - view_) / span) * width;
    };
    const auto toTime = [&](float x) {
        return view_ + static_cast<double>((x - origin.x) / width) * span;
    };

    draw->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height),
                        ImGui::GetColorU32(ImVec4(0.09f, 0.09f, 0.11f, 1.0f)), 3.0f);

    // ---- ruler ----
    // A tick every 1, 5, 10, 30 or 60 seconds, whichever gives about ten labels at this zoom.
    const double targets[] = {1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 60.0, 120.0};
    double step = targets[0];
    for (const double candidate : targets) {
        step = candidate;
        if (span / candidate <= 12.0) {
            break;
        }
    }
    const ImU32 rulerColour = ImGui::GetColorU32(ImVec4(0.5f, 0.52f, 0.58f, 0.75f));
    for (double t = std::floor(view_ / step) * step; t <= view_ + span; t += step) {
        const float x = toX(t);
        if (x < origin.x - 1.0f || x > origin.x + width) {
            continue;
        }
        draw->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + height), IM_COL32(255, 255, 255, 16));
        char label[24];
        std::snprintf(label, sizeof(label), "%d:%02d", static_cast<int>(t / 60.0),
                      static_cast<int>(std::fmod(t, 60.0)));
        draw->AddText(ImVec2(x + 3.0f, origin.y + 2.0f), rulerColour, label);
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
                if (x < origin.x || x > origin.x + width) {
                    continue;
                }
                draw->AddLine(ImVec2(x, markerTop + kMarkerHeight - 5.0f),
                              ImVec2(x, markerTop + kMarkerHeight), IM_COL32(120, 150, 200, 90));
            }
        }
    }
    for (const seq::Marker& marker : piece.markers) {
        if (marker.kind != seq::MarkerKind::Section && marker.kind != seq::MarkerKind::Cue) {
            continue;
        }
        const float x = toX(marker.timeSeconds);
        if (x < origin.x - 40.0f || x > origin.x + width) {
            continue;
        }
        const ImU32 colour = marker.kind == seq::MarkerKind::Section ? IM_COL32(120, 210, 190, 220)
                                                                    : IM_COL32(220, 190, 120, 220);
        draw->AddLine(ImVec2(x, markerTop), ImVec2(x, origin.y + height), colour);
        draw->AddText(ImVec2(x + 3.0f, markerTop), colour, marker.name.c_str());
    }

    // ---- the section lane (ADR-216) ----
    //
    // The song's own shape, directly under the ruler. Each section is a block; the line between two
    // blocks is the boundary and is what a drag grabs. A boundary is drawn as a full-height line
    // through the whole strip because that is what it is for: seeing whether a cut lands on one.
    const auto& sections = piece.structure.sections;
    if (!sections.empty()) {
        const float top = origin.y + lanes.sectionsTop();
        const float bottom = top + lanes.sectionLaneHeight;
        for (std::size_t i = 0; i < sections.size(); ++i) {
            const analysis::SongSection& section = sections[i];
            ImVec2 a(toX(section.startSeconds), top);
            ImVec2 b(toX(section.endSeconds), bottom);
            if (b.x < origin.x || a.x > origin.x + width) {
                continue;
            }
            a.x = std::max(a.x, origin.x);
            b.x = std::min(b.x, origin.x + width);
            const bool isSelected = selection_ == Selection::Section && selected_ == static_cast<int>(i);
            draw->AddRectFilled(a, b, sectionColour(section.function, isSelected), 2.0f);
            if (section.origin != analysis::SectionOrigin::Detected) {
                // A person's section is marked, because whether the analyser or a person decided a
                // boundary is the single most useful thing to know before pressing Analyse again.
                draw->AddRect(ImVec2(a.x + 1.0f, a.y + 1.0f), ImVec2(b.x - 1.0f, b.y - 1.0f),
                              IM_COL32(245, 225, 150, 190), 2.0f);
            }
            if (b.x - a.x > 26.0f) {
                draw->PushClipRect(a, ImVec2(b.x - 3.0f, b.y), true);
                draw->AddText(ImVec2(a.x + 5.0f, a.y + 3.0f), IM_COL32(244, 244, 248, 235),
                              seq::sectionDisplayName(section).c_str());
                draw->PopClipRect();
            }
        }
        // The boundaries, over the blocks and down the whole strip.
        for (std::size_t i = 1; i < sections.size(); ++i) {
            const float x = toX(sections[i].startSeconds);
            if (x < origin.x || x > origin.x + width) {
                continue;
            }
            const bool dragging = dragKind_ == 5 && dragIndex_ == static_cast<int>(i);
            draw->AddLine(ImVec2(x, top), ImVec2(x, origin.y + height),
                          dragging ? IM_COL32(255, 235, 150, 255) : IM_COL32(230, 236, 246, 120),
                          dragging ? 2.0f : 1.0f);
        }
        draw->AddRect(ImVec2(origin.x, top), ImVec2(origin.x + width, bottom),
                      IM_COL32(0, 0, 0, 110), 2.0f);
    }

    // ---- lanes ----
    float laneY = origin.y + lanes.audioTop();
    const auto laneRect = [&](double from, double to) {
        return std::pair<ImVec2, ImVec2>{ImVec2(toX(from), laneY),
                                         ImVec2(toX(to), laneY + kLaneHeight)};
    };

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
    //
    // The summary is of the mixdown and covers the whole timeline, so a clip's own shape is that
    // summary restricted to the clip's span -- and the gaps between clips draw nothing, which is
    // more truthful than a flat line through silence that might be a bug.
    if (hasAudio) {
        const audio::WaveformSummary& wave = waveform(engine);
        const ImVec2 laneA(origin.x, laneY);
        const ImVec2 laneB(origin.x + width, laneY + kAudioLaneHeight);
        // The empty channel the clips sit in.
        draw->AddRectFilled(laneA, laneB, IM_COL32(20, 23, 30, 200), 3.0f);
        const float mid = laneY + kAudioLaneHeight * 0.5f;
        const float halfHeight = kAudioLaneHeight * 0.5f - 3.0f;
        const double perPixel = span / static_cast<double>(width);

        for (std::size_t i = 0; i < engine.audioClips().size(); ++i) {
            const audio::AudioClip& clip = engine.audioClips()[i];
            const double end = audio::clipEndSeconds(clip, engine.clipSource(clip.file).get());
            const ImVec2 lo(std::max(toX(clip.startSeconds), origin.x), laneY);
            const ImVec2 hi(std::min(toX(end), origin.x + width), laneY + kAudioLaneHeight);
            if (hi.x <= lo.x) {
                continue;
            }
            const bool chosen = audioSelected_ == static_cast<int>(i);
            draw->AddRectFilled(lo, hi,
                                clip.enabled ? IM_COL32(33, 48, 70, 235) : IM_COL32(40, 42, 48, 210),
                                3.0f);

            // The waveform, inside the box and clipped to it.
            draw->PushClipRect(lo, hi, true);
            const ImU32 ink = clip.enabled ? IM_COL32(108, 156, 214, 200) : IM_COL32(120, 124, 134, 150);
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
            draw->AddLine(ImVec2(lo.x, mid), ImVec2(hi.x, mid), IM_COL32(120, 150, 200, 40));
            const std::string clipLabel = clip.name.empty() ? clip.file.filename().string() : clip.name;
            draw->AddText(ImVec2(lo.x + 5.0f, lo.y + 2.0f),
                          clip.enabled ? IM_COL32(215, 232, 250, 210) : IM_COL32(150, 150, 160, 170),
                          clipLabel.c_str());
            draw->PopClipRect();

            draw->AddRect(lo, hi, chosen ? IM_COL32(240, 220, 150, 255) : IM_COL32(0, 0, 0, 140), 3.0f,
                          0, chosen ? 2.0f : 1.0f);
        }
        draw->AddRect(laneA, laneB, IM_COL32(0, 0, 0, 120), 3.0f);
    }
    laneY = origin.y + lanes.shotsTop();

    // Shots (spec 29).
    for (std::size_t i = 0; i < piece.shots.size(); ++i) {
        const seq::Shot& shot = piece.shots[i];
        auto [a, b] = laneRect(shot.startSeconds, shot.endSeconds());
        if (b.x < origin.x || a.x > origin.x + width) {
            continue;
        }
        a.x = std::max(a.x, origin.x);
        b.x = std::min(b.x, origin.x + width);
        const bool isSelected = selection_ == Selection::Shot && selected_ == static_cast<int>(i);
        draw->AddRectFilled(a, b, shotColour(static_cast<int>(i), isSelected), 3.0f);
        draw->AddRect(a, b, IM_COL32(0, 0, 0, 120), 3.0f);
        if (b.x - a.x > 24.0f) {
            draw->PushClipRect(a, ImVec2(b.x - 3.0f, b.y), true);
            draw->AddText(ImVec2(a.x + 5.0f, a.y + 4.0f), IM_COL32(240, 240, 245, 255),
                          shot.name.c_str());
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
    }
    laneY = origin.y + lanes.actorsTop();

    // One lane per actor, showing its clip cues (spec 13).
    for (std::size_t ai = 0; ai < piece.actors.size(); ++ai) {
        const seq::Actor& actor = piece.actors[ai];
        const double endOfPiece = piece.duration();
        for (std::size_t c = 0; c < actor.clips.size(); ++c) {
            const seq::ClipCue& cue = actor.clips[c];
            const double to = c + 1 < actor.clips.size() ? actor.clips[c + 1].timeSeconds : endOfPiece;
            if (cue.clip.empty() || to <= cue.timeSeconds) {
                continue;
            }
            auto [a, b] = laneRect(cue.timeSeconds, to);
            if (b.x < origin.x || a.x > origin.x + width) {
                continue;
            }
            a.x = std::max(a.x, origin.x);
            b.x = std::min(b.x, origin.x + width);
            const bool isSelected = selection_ == Selection::Actor && selected_ == static_cast<int>(ai);
            draw->AddRectFilled(a, b,
                                isSelected ? IM_COL32(96, 150, 110, 220) : IM_COL32(66, 108, 84, 200),
                                3.0f);
            if (b.x - a.x > 22.0f) {
                draw->PushClipRect(a, ImVec2(b.x - 3.0f, b.y), true);
                draw->AddText(ImVec2(a.x + 5.0f, a.y + 4.0f), IM_COL32(225, 240, 228, 255),
                              cue.clip.c_str());
                draw->PopClipRect();
            }
        }
        draw->AddText(ImVec2(origin.x + 4.0f, laneY + 4.0f), IM_COL32(160, 190, 170, 110),
                      actor.id.c_str());
        laneY += kLaneHeight + kLaneGap;
    }

    // The overlay lane (spec 23, 26).
    const float overlayLaneY = origin.y + lanes.overlaysTop();
    laneY = overlayLaneY;
    if (!piece.overlays.empty()) {
        for (std::size_t i = 0; i < piece.overlays.size(); ++i) {
            const seq::OverlayCue& cue = piece.overlays[i];
            auto [a, b] = laneRect(cue.startSeconds, cue.endSeconds);
            if (b.x < origin.x || a.x > origin.x + width) {
                continue;
            }
            a.x = std::max(a.x, origin.x);
            b.x = std::min(b.x, origin.x + width);
            const bool isSelected = selection_ == Selection::Overlay && selected_ == static_cast<int>(i);
            const ImU32 colour = cue.kind == seq::OverlayKind::Shape
                                     ? IM_COL32(120, 110, 150, 190)
                                     : (isSelected ? IM_COL32(190, 150, 200, 235)
                                                   : IM_COL32(120, 96, 148, 210));
            draw->AddRectFilled(a, b, colour, 3.0f);
            if (b.x - a.x > 22.0f) {
                draw->PushClipRect(a, ImVec2(b.x - 3.0f, b.y), true);
                draw->AddText(ImVec2(a.x + 5.0f, a.y + 4.0f), IM_COL32(240, 232, 245, 255),
                              cue.content.c_str());
                draw->PopClipRect();
            }
        }
    }

    // ---- the playhead (spec 30) ----
    const double now = engine.timelineClock().seconds;
    const float playX = toX(now);
    if (playX >= origin.x && playX <= origin.x + width) {
        draw->AddLine(ImVec2(playX, origin.y), ImVec2(playX, origin.y + height),
                      IM_COL32(255, 220, 120, 230), 1.5f);
    }

    // ---- interaction ----
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const double mouseTime = toTime(mouse.x);
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        // Which lane was hit decides what the click means. The ruler, the marker row and the
        // **waveform** are always a scrub, so there is one place on the strip that is guaranteed not
        // to grab a block. The waveform holds no blocks and is deliberately left that way: clicking
        // a moment in the music to hear it is worth more than anything a block there could offer.
        //
        // Audio clips are blocks, so they live in a thin lane of their own *under* the waveform
        // (ADR-103). Drawing them on the waveform was tried and reported within the hour: a click
        // meant to scrub moved the audio instead, leaving the piece starting fourteen seconds in.
        dragKind_ = 0;
        dragIndex_ = -1;
        bool hitBlock = false;
        const StripLane lane = lanes.at(mouse.y - origin.y);
        if (lane == StripLane::Sections) {
            // A boundary first, because it is a five-point target inside a block and the block is
            // the thing you get when you miss it.
            auto& structure = piece.structure;
            for (std::size_t i = 1; i < structure.sections.size(); ++i) {
                if (std::fabs(mouse.x - toX(structure.sections[i].startSeconds)) <= kEdgeGrab) {
                    dragKind_ = 5;
                    dragIndex_ = static_cast<int>(i);
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
                if (mouse.x < a || mouse.x > b) {
                    continue;
                }
                selection_ = Selection::Shot;
                selected_ = static_cast<int>(i);
                hitBlock = true;
                dragIndex_ = static_cast<int>(i);
                dragKind_ = mouse.x > b - kEdgeGrab ? 2 : 1;
                dragGrab_ = mouseTime - shot.startSeconds;
                break;
            }
        } else if (lane == StripLane::Overlays) {
            for (std::size_t i = 0; i < piece.overlays.size(); ++i) {
                const seq::OverlayCue& cue = piece.overlays[i];
                const float a = toX(cue.startSeconds);
                const float b = toX(cue.endSeconds);
                if (mouse.x < a || mouse.x > b) {
                    continue;
                }
                selection_ = Selection::Overlay;
                selected_ = static_cast<int>(i);
                hitBlock = true;
                dragIndex_ = static_cast<int>(i);
                dragKind_ = mouse.x > b - kEdgeGrab ? 4 : 3;
                dragGrab_ = mouseTime - cue.startSeconds;
                break;
            }
        } else if (lane == StripLane::Actors) {
            const int row = static_cast<int>((mouse.y - origin.y - lanes.actorsTop()) /
                                             (kLaneHeight + kLaneGap));
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

    if (dragKind_ != 0 && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        const double t = snap(engine, mouseTime);
        if (dragKind_ == 1 && dragIndex_ >= 0 && dragIndex_ < static_cast<int>(piece.shots.size())) {
            seq::Shot& shot = piece.shots[static_cast<std::size_t>(dragIndex_)];
            shot.startSeconds = std::max(0.0, t - dragGrab_);
        } else if (dragKind_ == 2 && dragIndex_ >= 0 &&
                   dragIndex_ < static_cast<int>(piece.shots.size())) {
            seq::Shot& shot = piece.shots[static_cast<std::size_t>(dragIndex_)];
            shot.durationSeconds = std::max(0.25, t - shot.startSeconds);
        } else if (dragKind_ == 3 && dragIndex_ >= 0 &&
                   dragIndex_ < static_cast<int>(piece.overlays.size())) {
            seq::OverlayCue& cue = piece.overlays[static_cast<std::size_t>(dragIndex_)];
            const double length = cue.durationSeconds();
            cue.startSeconds = std::max(0.0, t - dragGrab_);
            cue.endSeconds = cue.startSeconds + length;
        } else if (dragKind_ == 4 && dragIndex_ >= 0 &&
                   dragIndex_ < static_cast<int>(piece.overlays.size())) {
            seq::OverlayCue& cue = piece.overlays[static_cast<std::size_t>(dragIndex_)];
            cue.endSeconds = std::max(cue.startSeconds + 0.2, t);
        } else if (dragKind_ == 5 && dragIndex_ > 0) {
            // The section boundary has its *own* snap, not the strip's: `t` above has already been
            // through the shot grid, so this starts again from the raw time. A snapped boundary
            // takes the beat's own value, bit for bit; a free one takes the millisecond it landed
            // on. Neither is rounded.
            (void)seq::moveBoundary(piece.structure, static_cast<std::size_t>(dragIndex_),
                                    snapSection(engine, mouseTime));
        }
    } else if (dragKind_ == 0 && hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        engine.seekSeconds(std::clamp(snap(engine, mouseTime), 0.0, duration));
    }
    if (dragKind_ != 0 && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        // The bake waits for the mouse. A drag is sixty edits a second and a bake rebuilds tracks
        // and layers; doing both together would make a smooth drag feel like a stutter. The
        // arrangement waits for the same reason and costs more: a re-mix is a pass over every
        // sample in the piece.
        // The markers are derived from the structure, and re-deriving them means re-sorting a list
        // that holds every beat in the song. Once, when the drag ends, rather than sixty times a
        // second while it is moving.
        if (dragKind_ == 5) {
            piece.refreshSectionMarkers();
        }
        dragKind_ = 0;
        dragIndex_ = -1;
        touch();
    }
    // Right-drag pans, wheel zooms about the pointer: the two gestures a time strip needs and the
    // two everyone already knows.
    if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
        view_ -= static_cast<double>(ImGui::GetIO().MouseDelta.x / width) * span;
    }
    if (hovered && ImGui::GetIO().MouseWheel != 0.0f) {
        const double anchor = mouseTime;
        zoom_ = std::clamp(zoom_ * (ImGui::GetIO().MouseWheel > 0.0f ? 1.15f : 1.0f / 1.15f), 0.25f, 64.0f);
        const double newSpan = duration / static_cast<double>(zoom_);
        view_ = anchor - (anchor - view_) * (newSpan / span);
    }
    view_ = std::clamp(view_, 0.0, std::max(0.0, duration - duration / static_cast<double>(zoom_)));
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
        if (selected_ >= 0 && selected_ < static_cast<int>(piece.structure.sections.size())) {
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
    analysis::SongSection& section = piece.structure.sections[index];
    ImGui::SeparatorText("Section");

    std::strncpy(labelBuffer_, section.label.c_str(), sizeof(labelBuffer_) - 1);
    labelBuffer_[sizeof(labelBuffer_) - 1] = '\0';
    ImGui::SetNextItemWidth(200);
    if (ImGui::InputText("name", labelBuffer_, sizeof(labelBuffer_))) {
        if (seq::setSectionLabel(piece.structure, index, labelBuffer_)) {
            piece.refreshSectionMarkers();
            touch();
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("What events call this section. Leave it empty to use the type's name.");
    }

    // The type, from the one list `analysis::allSectionFunctions()` keeps, so this cannot come to
    // offer eleven of thirteen.
    const auto functions = analysis::allSectionFunctions();
    int current = 0;
    std::vector<const char*> names;
    names.reserve(functions.size());
    for (std::size_t i = 0; i < functions.size(); ++i) {
        names.push_back(analysis::sectionFunctionName(functions[i]));
        if (functions[i] == section.function) {
            current = static_cast<int>(i);
        }
    }
    ImGui::SetNextItemWidth(200);
    if (ImGui::Combo("type", &current, names.data(), static_cast<int>(names.size()))) {
        if (seq::setSectionFunction(piece.structure, index, functions[static_cast<std::size_t>(current)])) {
            piece.refreshSectionMarkers();
            touch();
        }
    }

    // Times in full, because a boundary at 1:02.409117 is the thing this whole feature is about and
    // a display rounded to two places would hide the precision it is claiming to keep.
    ImGui::Text("%s -> %s   (%.3f s)", clock(section.startSeconds).c_str(),
                clock(section.endSeconds).c_str(), section.durationSeconds());
    ImGui::TextDisabled("%.6f -> %.6f s", section.startSeconds, section.endSeconds);

    const char* originText = analysis::sectionOriginName(section.origin);
    if (section.origin == analysis::SectionOrigin::Detected) {
        ImGui::TextDisabled("%s", originText);
    } else {
        ImGui::TextColored(ImVec4(0.96f, 0.88f, 0.58f, 1.0f), "%s -- kept when you analyse again",
                           originText);
    }
    if (seq::confidenceIsMeaningful(section)) {
        // Only for a detected section. On one a person has touched the number is not a smaller
        // claim, it is not a claim at all (ADR-215), so it is not shown rather than shown small.
        ImGui::TextDisabled("label %.0f%%, boundary %.0f%%",
                            static_cast<double>(section.labelConfidence) * 100.0,
                            static_cast<double>(section.startConfidence) * 100.0);
        if (section.repetitionGroup >= 0) {
            ImGui::SameLine();
            ImGui::TextDisabled("| repeat %d, occurrence %d", section.repetitionGroup,
                                section.occurrence + 1);
        }
    }

    if (ImGui::Button("Split at playhead")) {
        const double at = engine.timelineClock().seconds;
        if (const auto made = seq::splitSection(piece.structure, snapSection(engine, at))) {
            selected_ = static_cast<int>(*made);
            piece.refreshSectionMarkers();
            touch();
            status_ = "split; the new section is yours, not the analyser's";
        } else {
            status_ = "the playhead is not far enough inside a section to split it";
        }
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(piece.structure.sections.size() <= 1);
    if (ImGui::Button("Delete")) {
        if (seq::removeSection(piece.structure, index)) {
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
        status_ = "no analysed audio to look at";
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
        status_ = "the audio changed while the song was being analysed; nothing applied";
        return;
    }
    seq::Sequence& piece = engine.sequence();
    if (work->merge) {
        const seq::ReanalysisReport report = seq::reanalyse(piece.structure, work->result);
        status_ = report.summary();
    } else {
        piece.structure = std::move(work->result);
        status_ = fmt::format("{} section(s) found", piece.structure.sections.size());
    }
    piece.refreshSectionMarkers();
    if (engine.track() != nullptr) {
        piece.setBeatMarkers(engine.track()->beats().beatTimes);
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
