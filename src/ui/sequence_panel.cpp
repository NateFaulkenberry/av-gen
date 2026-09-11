#include "ui/sequence_panel.hpp"

#include "analysis/analysis_track.hpp"
#include "app/camera_director.hpp"
#include "core/log.hpp"
#include "scene/composition.hpp"
#include "seq/layer_sink.hpp"
#include "seq/lyrics.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

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

ImU32 shotColour(int index, bool selected) {
    // Alternating so a cut is visible even between two shots on the same scene, and warmer when
    // selected rather than merely brighter -- a brighter blue next to a blue reads as "nearer".
    const float base = index % 2 == 0 ? 0.30f : 0.24f;
    return selected ? IM_COL32(196, 140, 72, 235)
                    : ImGui::GetColorU32(ImVec4(base, base + 0.10f, base + 0.22f, 0.92f));
}

const char* kSnapNames[] = {"Off", "Frames", "Beats", "Markers"};

} // namespace

void SequencePanel::draw(app::Engine& engine) {
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
    ImGui::BeginDisabled(engine.track() == nullptr);
    if (ImGui::Button("Sections")) {
        // spec 19/20: the labels and the beat grid come from the music, not from typing.
        auto structure = app::structureOfTrack(*engine.track(), engine.phraseBars(), engine.sectionPhrases());
        if (structure) {
            piece.setSectionMarkers(*structure);
            piece.setBeatMarkers(engine.track()->beats().beatTimes);
            status_ = fmt::format("{} section(s), {} beat(s) from the track",
                                  structure->sections.size(), engine.track()->beats().beatTimes.size());
        } else {
            status_ = structure.error().message;
        }
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Label the strip from the song's own structure, and draw its beats.\n"
                          "Needs analysed audio.");
    }

    ImGui::SameLine();
    if (ImGui::Button("Import Lyrics...")) {
        if (onImportLyrics) {
            onImportLyrics();
        } else {
            ImGui::OpenPopup("import-lyrics");
        }
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
    if (!status_.empty()) {
        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.45f, 1.0f), "%s", status_.c_str());
    }
}

// ---- the strip ---------------------------------------------------------------------------------

void SequencePanel::drawStrip(app::Engine& engine) {
    seq::Sequence& piece = engine.sequence();
    const double duration = std::max({piece.duration(), engine.durationSeconds(), 1.0});

    const float width = std::max(ImGui::GetContentRegionAvail().x, 80.0f);
    const int lanes = 1 + static_cast<int>(piece.actors.size()) + (piece.overlays.empty() ? 0 : 1);
    const float height = kRulerHeight + kMarkerHeight +
                         static_cast<float>(lanes) * (kLaneHeight + kLaneGap) + kLaneGap;

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

    // ---- lanes ----
    float laneY = markerTop + kMarkerHeight + kLaneGap;
    const auto laneRect = [&](double from, double to) {
        return std::pair<ImVec2, ImVec2>{ImVec2(toX(from), laneY),
                                         ImVec2(toX(to), laneY + kLaneHeight)};
    };

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
    const float shotLaneY = laneY;
    laneY += kLaneHeight + kLaneGap;

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
    const float overlayLaneY = laneY;
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
        // Which lane was hit decides what the click means. Above the lanes -- the ruler and the
        // marker row -- is always a scrub, so there is one place on the strip that is guaranteed
        // not to grab a block.
        dragKind_ = 0;
        dragIndex_ = -1;
        bool hitBlock = false;
        if (mouse.y >= shotLaneY && mouse.y < shotLaneY + kLaneHeight) {
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
        } else if (!piece.overlays.empty() && mouse.y >= overlayLaneY &&
                   mouse.y < overlayLaneY + kLaneHeight) {
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
        } else if (mouse.y >= shotLaneY + kLaneHeight + kLaneGap && mouse.y < overlayLaneY) {
            const int lane = static_cast<int>((mouse.y - (shotLaneY + kLaneHeight + kLaneGap)) /
                                              (kLaneHeight + kLaneGap));
            if (lane >= 0 && lane < static_cast<int>(piece.actors.size())) {
                selection_ = Selection::Actor;
                selected_ = lane;
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
        }
    } else if (dragKind_ == 0 && hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        engine.seekSeconds(std::clamp(snap(engine, mouseTime), 0.0, duration));
    }
    if (dragKind_ != 0 && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        // The bake waits for the mouse. A drag is sixty edits a second and a bake rebuilds tracks
        // and layers; doing both together would make a smooth drag feel like a stutter.
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
    case Selection::None:
        ImGui::TextDisabled("Click a shot, a character lane or a lyric to edit it.");
        break;
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
        beatSource_ = nullptr;
        return beatCache_;
    }
    if (beatSource_ != track) {
        beatCache_ = track->beats().beatTimes;
        beatSource_ = track;
    }
    return beatCache_;
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
