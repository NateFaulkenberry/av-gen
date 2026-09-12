#include "ui/transport_bar.hpp"

#include "app/transport.hpp"
#include "core/log.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <string>

namespace avgen::ui {
namespace {

// What a transport button draws. Named rather than parameterised because every one of them is a
// specific, recognised shape and a generic "draw n triangles" helper would be harder to read than
// the shapes.
enum class Glyph { Play, Pause, Stop, Start, End, StepBack, StepForward, Rewind, Forward, Loop };

constexpr ImU32 kOn = IM_COL32(232, 238, 248, 255);
constexpr ImU32 kOff = IM_COL32(168, 174, 186, 255);
constexpr ImU32 kLit = IM_COL32(120, 200, 255, 255);
constexpr ImU32 kPlaying = IM_COL32(130, 225, 150, 255);

void triangle(ImDrawList* list, ImVec2 c, float r, ImU32 colour, bool pointRight) {
    const float d = pointRight ? 1.0f : -1.0f;
    list->AddTriangleFilled(ImVec2(c.x - d * r * 0.55f, c.y - r), ImVec2(c.x - d * r * 0.55f, c.y + r),
                            ImVec2(c.x + d * r * 0.75f, c.y), colour);
}

void bar(ImDrawList* list, ImVec2 c, float r, ImU32 colour, float offsetX, float width) {
    list->AddRectFilled(ImVec2(c.x + offsetX - width * 0.5f, c.y - r),
                        ImVec2(c.x + offsetX + width * 0.5f, c.y + r), colour);
}

void drawGlyph(ImDrawList* list, Glyph glyph, ImVec2 c, float r, ImU32 colour) {
    switch (glyph) {
    case Glyph::Play:
        triangle(list, c, r, colour, true);
        break;
    case Glyph::Pause:
        bar(list, c, r, colour, -r * 0.42f, r * 0.42f);
        bar(list, c, r, colour, r * 0.42f, r * 0.42f);
        break;
    case Glyph::Stop:
        list->AddRectFilled(ImVec2(c.x - r * 0.8f, c.y - r * 0.8f), ImVec2(c.x + r * 0.8f, c.y + r * 0.8f),
                            colour, 1.0f);
        break;
    case Glyph::Start:
        bar(list, c, r, colour, -r * 0.8f, r * 0.3f);
        triangle(list, ImVec2(c.x + r * 0.15f, c.y), r, colour, false);
        break;
    case Glyph::End:
        triangle(list, ImVec2(c.x - r * 0.15f, c.y), r, colour, true);
        bar(list, c, r, colour, r * 0.8f, r * 0.3f);
        break;
    case Glyph::StepBack:
        // One frame back: an arrow against a wall, the wall on the side it is moving toward.
        bar(list, c, r * 0.85f, colour, -r * 0.75f, r * 0.28f);
        triangle(list, ImVec2(c.x + r * 0.25f, c.y), r * 0.8f, colour, false);
        break;
    case Glyph::StepForward:
        triangle(list, ImVec2(c.x - r * 0.25f, c.y), r * 0.8f, colour, true);
        bar(list, c, r * 0.85f, colour, r * 0.75f, r * 0.28f);
        break;
    case Glyph::Rewind:
        triangle(list, ImVec2(c.x - r * 0.45f, c.y), r * 0.85f, colour, false);
        triangle(list, ImVec2(c.x + r * 0.55f, c.y), r * 0.85f, colour, false);
        break;
    case Glyph::Forward:
        triangle(list, ImVec2(c.x - r * 0.55f, c.y), r * 0.85f, colour, true);
        triangle(list, ImVec2(c.x + r * 0.45f, c.y), r * 0.85f, colour, true);
        break;
    case Glyph::Loop: {
        // A rounded rectangle with an arrow head: the cycle symbol, at a size where a circle with an
        // arrow on it reads as a blob.
        const ImVec2 lo(c.x - r * 0.85f, c.y - r * 0.6f);
        const ImVec2 hi(c.x + r * 0.85f, c.y + r * 0.6f);
        list->AddRect(lo, hi, colour, r * 0.5f, 0, 1.6f);
        triangle(list, ImVec2(c.x + r * 0.2f, lo.y), r * 0.42f, colour, true);
        break;
    }
    }
}

// Returns true when clicked. `lit` colours it as an active state rather than a press.
bool glyphButton(const char* id, Glyph glyph, const char* tooltip, bool enabled = true,
                 ImU32 colour = kOff) {
    const float height = ImGui::GetFrameHeight();
    const ImVec2 size(height * 1.25f, height);
    const ImVec2 lo = ImGui::GetCursorScreenPos();
    ImGui::BeginDisabled(!enabled);
    const bool clicked = ImGui::Button((std::string("##") + id).c_str(), size);
    ImGui::EndDisabled();
    const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
    if (hovered && tooltip != nullptr) {
        ImGui::SetTooltip("%s", tooltip);
    }
    ImU32 shade = colour;
    if (!enabled) {
        shade = IM_COL32(96, 100, 108, 255);
    } else if (hovered && colour == kOff) {
        shade = kOn;
    }
    drawGlyph(ImGui::GetWindowDrawList(), glyph, ImVec2(lo.x + size.x * 0.5f, lo.y + size.y * 0.5f),
              height * 0.26f, shade);
    return clicked;
}

const char* const kRateNames[] = {"0.25x", "0.5x", "1x", "2x", "4x"};
constexpr double kRates[] = {0.25, 0.5, 1.0, 2.0, 4.0};

int rateIndex(double rate) {
    int best = 2;
    double bestDelta = 1e9;
    for (int i = 0; i < 5; ++i) {
        const double delta = std::abs(kRates[i] - rate);
        if (delta < bestDelta) {
            bestDelta = delta;
            best = i;
        }
    }
    return best;
}

} // namespace

std::string TransportBar::format(const app::TransportSnapshot& snapshot, double seconds) const {
    switch (format_) {
    case TimeFormat::Timecode:
        return app::formatTimecode(seconds, snapshot.frameRate);
    case TimeFormat::Frames: {
        char buffer[32];
        const double fps = snapshot.frameRate.fps();
        std::snprintf(buffer, sizeof(buffer), "%lld",
                      static_cast<long long>(fps > 0.0 ? std::floor(seconds * fps + 1e-9) : 0.0));
        return buffer;
    }
    case TimeFormat::Bars: {
        std::string bars = app::formatBarsBeats(seconds, snapshot.tempoBpm, snapshot.beatsPerBar);
        // Falls back rather than showing nothing: a piece with no analysed tempo still has a time,
        // and an empty readout looks like a broken panel rather than an absent tempo.
        return bars.empty() ? app::formatClockTime(seconds) : bars;
    }
    case TimeFormat::Clock:
        break;
    }
    return app::formatClockTime(seconds);
}

void TransportBar::drawTransportButtons(app::Engine& engine, const app::TransportSnapshot& snapshot) {
    const bool playing = snapshot.playing();

    if (glyphButton("start", Glyph::Start, "Return to start (Home)")) {
        engine.seekSeconds(engine.transport().playStartSeconds());
    }
    ImGui::SameLine(0.0f, 2.0f);
    if (glyphButton("rew", Glyph::Rewind, "Back one beat (Shift+Left)")) {
        engine.stepBeats(-1);
    }
    ImGui::SameLine(0.0f, 2.0f);
    if (glyphButton("stepback", Glyph::StepBack, "Back one frame (Left)")) {
        engine.stepFrames(-1);
    }
    ImGui::SameLine(0.0f, 2.0f);
    if (glyphButton("play", playing ? Glyph::Pause : Glyph::Play,
                    playing ? "Pause (Space)" : "Play (Space)", true, playing ? kPlaying : kOff)) {
        engine.togglePlay();
    }
    ImGui::SameLine(0.0f, 2.0f);
    if (glyphButton("stop", Glyph::Stop, "Stop and return to the start")) {
        engine.stop();
    }
    ImGui::SameLine(0.0f, 2.0f);
    if (glyphButton("stepfwd", Glyph::StepForward, "Forward one frame (Right)")) {
        engine.stepFrames(1);
    }
    ImGui::SameLine(0.0f, 2.0f);
    if (glyphButton("ffwd", Glyph::Forward, "Forward one beat (Shift+Right)")) {
        engine.stepBeats(1);
    }
    ImGui::SameLine(0.0f, 2.0f);
    if (glyphButton("end", Glyph::End, "Go to the end")) {
        engine.seekSeconds(engine.transport().playEndSeconds());
    }
}

void TransportBar::drawTimeDisplay(app::Engine& engine, const app::TransportSnapshot& snapshot) {
    // Clickable, cycling through the four ways of reading the same second. The position and the
    // duration always read the same way as each other, because a position in frames beside a
    // duration in minutes is two numbers that cannot be compared.
    const std::string position = format(snapshot, snapshot.positionSeconds);
    const std::string duration = format(snapshot, snapshot.durationSeconds);
    const std::string label = position + " / " + duration;
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.82f, 0.88f, 0.98f, 1.0f));
    if (ImGui::Selectable(label.c_str(), false, 0, ImVec2(ImGui::CalcTextSize(label.c_str()).x + 8.0f, 0.0f))) {
        format_ = static_cast<TimeFormat>((static_cast<int>(format_) + 1) % 4);
    }
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s -- click to change: seconds, timecode, frames, bars",
                          snapshot.durationSeconds > 0.0 ? "position / length"
                                                         : "position (this project has no stated length)");
    }
    static_cast<void>(engine);
}

void TransportBar::drawLoopControls(app::Engine& engine, const app::TransportSnapshot& snapshot) {
    app::Transport& transport = engine.transport();
    if (glyphButton("loop", Glyph::Loop,
                    snapshot.loop.enabled ? "Looping -- play the range over and over (L)"
                                          : "Loop the range over and over (L)",
                    true, snapshot.loop.enabled ? kLit : kOff)) {
        transport.setLoopEnabled(!snapshot.loop.enabled);
        if (transport.loop().enabled && !transport.loop().usable() && snapshot.durationSeconds > 0.0) {
            // Turning the loop on with no range set would light a button that does nothing. A first
            // range over the whole piece is the one that is never wrong.
            transport.setLoop(app::TransportLoop{true, 0.0, snapshot.durationSeconds});
        }
    }
    if (!snapshot.loop.enabled) {
        return;
    }
    auto start = static_cast<float>(snapshot.loop.startSeconds);
    auto end = static_cast<float>(snapshot.loop.endSeconds);
    ImGui::SameLine(0.0f, 6.0f);
    ImGui::SetNextItemWidth(70.0f);
    const bool movedStart = ImGui::DragFloat("##loopstart", &start, 0.05f, 0.0f, 1e6f, "%.2f");
    ImGui::SameLine(0.0f, 2.0f);
    ImGui::TextUnformatted("..");
    ImGui::SameLine(0.0f, 2.0f);
    ImGui::SetNextItemWidth(70.0f);
    const bool movedEnd = ImGui::DragFloat("##loopend", &end, 0.05f, 0.0f, 1e6f, "%.2f");
    if (movedStart || movedEnd) {
        transport.setLoop(app::TransportLoop{true, static_cast<double>(start), static_cast<double>(end)});
    }
}

void TransportBar::draw(app::Engine& engine, bool compact) {
    const app::TransportSnapshot snapshot = engine.transport().snapshot();

    drawTransportButtons(engine, snapshot);
    ImGui::SameLine(0.0f, 10.0f);
    drawTimeDisplay(engine, snapshot);

    if (compact) {
        return;
    }

    ImGui::SameLine(0.0f, 10.0f);
    drawLoopControls(engine, snapshot);

    ImGui::SameLine(0.0f, 10.0f);
    ImGui::SetNextItemWidth(72.0f);
    int rate = rateIndex(snapshot.rate);
    if (ImGui::Combo("##rate", &rate, kRateNames, IM_ARRAYSIZE(kRateNames))) {
        engine.transport().setRate(kRates[rate]);
    }
    if (ImGui::IsItemHovered()) {
        // Said plainly rather than hidden: `AudioPlayer` has no rate control, and a device left
        // running at 1x under a 2x transport drifts a second out every second. Silence is the honest
        // answer, and the user has to be told which one they are getting.
        ImGui::SetTooltip("Playback speed.\nAudio plays at 1x only; at any other speed it is silent "
                          "and the picture runs alone.\nOffline renders are not affected.");
    }

    ImGui::SameLine(0.0f, 10.0f);
    ImGui::TextDisabled("%.6g fps", snapshot.frameRate.fps());
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("The project's frame rate: what a frame step moves by and what timecode "
                          "counts in.\nSet it in Render.");
    }
    if (snapshot.tempoBpm > 0.0) {
        ImGui::SameLine(0.0f, 10.0f);
        ImGui::TextDisabled("%.1f bpm", snapshot.tempoBpm);
    }
    if (snapshot.rate != 1.0 && engine.hasAudio()) {
        ImGui::SameLine(0.0f, 10.0f);
        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.45f, 1.0f), "audio silent at %s", kRateNames[rate]);
    }
}

} // namespace avgen::ui
