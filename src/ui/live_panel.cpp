// ADR-1025: the Live panel -- the smallest useful surface for playing AV Gen with a MIDI keyboard and a synth
// (brief 01-brief-live.md PART 13). Debug-level on purpose: pickers, two receiving lights, enable, the live
// antialiasing setting (ADR-1024's, the same one Settings shows), sensitivity and smoothing, and the measured
// latencies. Everything it changes is either this machine's (devices, smoothing, AA: the settings file) or the
// project's existing `audio/inputGain` parameter.

#include "ui/control_panel.hpp"

#include "audio/audio_input.hpp"
#include "control/midi.hpp"
#include "params/parameter.hpp"
#include "platform/window.hpp"
#include "sonic/notes.hpp"
#include "ui/style.hpp"
#include "ui/theme.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace avgen::ui {

namespace {

void light(bool on, const char* label) {
    const float h = ImGui::GetTextLineHeight();
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const ImU32 colour = on ? IM_COL32(90, 230, 120, 255) : IM_COL32(90, 90, 90, 255);
    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(at.x + h * 0.5f, at.y + h * 0.55f), h * 0.35f, colour);
    ImGui::Dummy(ImVec2(h, h));
    ImGui::SameLine();
    if (on) {
        ImGui::TextUnformatted(label);
    } else {
        ImGui::TextDisabled("%s", label);
    }
}

std::vector<app::ProjectionDisplay> connectedDisplays() {
    std::vector<app::ProjectionDisplay> out;
    for (const auto& d : platform::Window::displays()) {
        out.push_back({.index = d.index, .name = d.name, .width = d.width, .height = d.height, .primary = d.primary});
    }
    return out;
}

} // namespace

void ControlPanel::drawLive(app::Engine& engine) {
    if (engine.mode() != app::EngineMode::Live) {
        ImGui::TextDisabled("Live input is for the live editor.");
        return;
    }
    const double now = ImGui::GetTime();
    if (now - liveLastScan_ > 3.0) {
        liveAudioDevices_ = audio::listCaptureDevices();
        liveMidiDevices_ = control::listMidiInputs();
        liveDisplays_ = connectedDisplays();
        liveLastScan_ = now;
    }
    app::AppSettings* machine = settings.settings;
    const bool on = engine.liveSonic();
    const auto st = engine.liveSonicStatus();

    // ---- projection (ADR-1026): first, because it is the one thing done in front of an audience ----
    {
        const bool active = projection.active;
        const ImU32 base = active ? palette().error : palette().accent;
        ImGui::PushStyleColor(ImGuiCol_Button, base);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, mixColour(base, IM_COL32(255, 255, 255, 255), 0.15f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, mixColour(base, IM_COL32(0, 0, 0, 255), 0.15f));
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
        const char* label = !active ? "Start projection" : "Stop projection";
        if (ImGui::Button(label, ImVec2(-1, ImGui::GetFrameHeight() * 1.6f))) {
            if (active && onStopProjection) {
                onStopProjection();
            } else if (!active && onStartProjection) {
                onStartProjection();
            }
        }
        ImGui::PopStyleColor(4);
        if (ImGui::IsItemHovered()) {
            tooltip("Opens a clean window with just the picture of this project -- no panels -- for a projector or a "
                    "second screen. Esc in the projection window, closing it, or Stop ends it. For the Sonic Live "
                    "demo, open it first (Open live demo, below).");
        }
        if (machine != nullptr) {
            auto& pj = machine->projection;
            bool changed = false;
            const app::ProjectionDisplayChoice choice = app::chooseProjectionDisplay(liveDisplays_, pj.display);
            // The display: Automatic, every connected display, and the remembered one if it is missing.
            std::vector<std::string> labels;
            const app::ProjectionDisplayChoice automatic = app::chooseProjectionDisplay(liveDisplays_, {});
            labels.push_back(automatic.name.empty() ? std::string("Automatic")
                                                    : "Automatic (" + automatic.name + ")");
            int current = 0;
            for (std::size_t i = 0; i < liveDisplays_.size(); ++i) {
                const auto& d = liveDisplays_[i];
                labels.push_back(d.name + "  " + std::to_string(d.width) + "x" + std::to_string(d.height) +
                                 (d.primary ? "  (this screen)" : ""));
                if (!pj.display.empty() && d.name == pj.display) {
                    current = static_cast<int>(i) + 1;
                }
            }
            const bool missing = !pj.display.empty() && current == 0;
            if (missing) {
                labels.push_back(pj.display + "  (not connected)");
                current = static_cast<int>(labels.size()) - 1;
            }
            std::vector<const char*> names;
            for (const auto& l : labels) {
                names.push_back(l.c_str());
            }
            ImGui::SetNextItemWidth(-90);
            if (ImGui::Combo("Display", &current, names.data(), static_cast<int>(names.size()))) {
                if (current == 0) {
                    pj.display.clear();
                    pj.fullscreen.reset(); // automatic: fullscreen exactly when it is not this screen
                    changed = true;
                } else if (current <= static_cast<int>(liveDisplays_.size())) {
                    const auto& d = liveDisplays_[static_cast<std::size_t>(current - 1)];
                    pj.display = d.name;
                    pj.fullscreen = !d.primary; // a projector fills; this screen gets a window
                    changed = true;
                }
            }
            if (ImGui::IsItemHovered()) {
                tooltip("Where the projection opens. Automatic is the first display that is not this screen, or "
                        "this screen when there is only one. Remembered on this Mac, by name.");
            }
            bool fullscreen = app::projectionFullscreen(pj, choice);
            if (ImGui::Checkbox("Fullscreen", &fullscreen)) {
                pj.fullscreen = fullscreen;
                changed = true;
            }
            ImGui::SameLine();
            {
                static const char* sizes[] = {"Auto size", "1920 x 1080", "1280 x 720", "960 x 540"};
                static const std::uint32_t widths[] = {0, 1920, 1280, 960};
                static const std::uint32_t heights[] = {0, 1080, 720, 540};
                int size = 0;
                for (int i = 1; i < 4; ++i) {
                    if (pj.windowWidth == widths[i] && pj.windowHeight == heights[i]) {
                        size = i;
                    }
                }
                ImGui::BeginDisabled(fullscreen);
                ImGui::SetNextItemWidth(120);
                if (ImGui::Combo("##projection-size", &size, sizes, 4)) {
                    pj.windowWidth = widths[size];
                    pj.windowHeight = heights[size];
                    changed = true;
                }
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    tooltip("The window's size when not fullscreen, in points. Automatic: the display's size on another display, "
                            "half of it on this screen. Fullscreen always fills the display.");
                }
            }
            ImGui::SameLine();
            {
                static const char* scalings[] = {"Fit", "Fill", "Stretch"};
                int scaling = static_cast<int>(pj.scaling);
                ImGui::SetNextItemWidth(-1);
                if (ImGui::Combo("##projection-scaling", &scaling, scalings, 3)) {
                    pj.scaling = static_cast<app::ProjectionScaling>(scaling);
                    changed = true;
                }
                if (ImGui::IsItemHovered()) {
                    tooltip("When the screen's shape is not the picture's. Fit: the whole picture, black bars. Fill: "
                            "the screen full, the picture's edges cut. Stretch: distorted to fill. For a picture "
                            "that fills a 16:9 projector exactly, put the canvas in Output Frame mode.");
                }
            }
            if (missing) {
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "%s is not connected: projecting on %s",
                                   pj.display.c_str(), choice.name.empty() ? "the default display" : choice.name.c_str());
            }
            if (changed && onProjectionSettingsChanged) {
                onProjectionSettingsChanged();
            }
        }
        if (!projection.status.empty()) {
            ImGui::TextWrapped("%s", projection.status.c_str());
        }
    }
    ImGui::Separator();

    // ---- live quality (ADR-1087): what the frame is doing to hold the target, and the two choices ----
    if (liveQuality.available) {
        const auto& q = liveQuality;
        ImGui::TextDisabled("LIVE QUALITY");
        if (machine != nullptr) {
            static const char* levels[] = {"Auto", "Ultra", "High", "Medium", "Low", "Emergency"};
            int current = machine->liveQualityPinned ? 1 + static_cast<int>(*machine->liveQualityPinned) : 0;
            ImGui::BeginDisabled(q.qualityFromCommandLine);
            ImGui::SetNextItemWidth(120);
            if (ImGui::Combo("Quality", &current, levels, 6)) {
                if (current == 0) {
                    machine->liveQualityPinned.reset();
                } else {
                    machine->liveQualityPinned = static_cast<app::LiveQualityLevel>(current - 1);
                }
                if (onLiveQualityChanged) {
                    onLiveQualityChanged();
                }
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                tooltip(q.qualityFromCommandLine
                            ? "Set on the command line for this run (--live-quality or --adaptive-scale)."
                            : "Auto lowers the picture's quality step by step when the GPU cannot hold the target, and "
                              "raises it again once the frame has fitted comfortably for a while. Ultra .. Emergency "
                              "hold that level whatever it costs. Never affects a render.");
            }
            ImGui::SameLine();
            char target[32];
            std::snprintf(target, sizeof(target), "%d fps", machine->liveTargetFps);
            ImGui::BeginDisabled(q.targetFromCommandLine);
            ImGui::SetNextItemWidth(90);
            if (ImGui::BeginCombo("Target", target)) {
                for (const int fps : app::kLiveTargetChoices) {
                    char label[16];
                    std::snprintf(label, sizeof(label), "%d fps", fps);
                    if (ImGui::Selectable(label, fps == machine->liveTargetFps)) {
                        machine->liveTargetFps = fps;
                        if (onLiveQualityChanged) {
                            onLiveQualityChanged();
                        }
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                tooltip(q.targetFromCommandLine
                            ? "Set on the command line for this run (--live-target)."
                            : "The frame rate the live picture is held to. The GPU budget is that frame minus 12%% "
                              "headroom. Your choice, not the display's refresh rate: 60 on a 120 Hz screen is fine.");
            }
        }
        ImGui::Text("Now: %s%s  scale %.2f  %ux%u into %ux%u", q.level.c_str(), q.automatic ? " (auto)" : "",
                    static_cast<double>(q.renderScale), q.internalWidth, q.internalHeight, q.outputWidth,
                    q.outputHeight);
        if (ImGui::IsItemHovered()) {
            tooltipUnformatted(("This project gives up " + q.strategy +
                     (q.strategyStated ? std::string(" (live.qualityStrategy in the project file).")
                                       : std::string(" (the default; a project can say otherwise with live.qualityStrategy)."))
                     + " Quality changes this session: " + std::to_string(q.transitions) + ".").c_str());
        }
        const bool gpuOver = q.gpuMs > q.budgetMs;
        if (q.gpuMs >= 0.0) {
            ImGui::TextColored(gpuOver ? ImVec4(1.0f, 0.6f, 0.3f, 1.0f) : ImGui::GetStyleColorVec4(ImGuiCol_Text),
                               "GPU %.1f ms of %.1f ms budget", q.gpuMs, q.budgetMs);
        } else {
            ImGui::Text("GPU -- of %.1f ms budget", q.budgetMs);
        }
        ImGui::SameLine();
        ImGui::Text("  CPU %.1f ms", q.cpuMs >= 0.0 ? q.cpuMs : 0.0);
        // The CPU is diagnosed, never acted on (§8): no level of quality makes the main thread faster.
        if (q.cpuMs > q.targetFrameMs) {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f),
                               "The CPU alone (%.1f ms) is longer than a %.0f fps frame: lowering quality cannot reach it.",
                               q.cpuMs, q.targetFps);
        } else if (q.atBottom && gpuOver) {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "At the lowest quality and still over the budget.");
        }
        ImGui::Separator();
    }

    ImGui::TextDisabled("LIVE SONIC INPUT");
    bool enabled = on;
    if (ImGui::Checkbox("Enabled", &enabled)) {
        if (auto r = engine.setLiveSonic(enabled); !r) {
            status_ = r.error().message;
        }
    }
    if (ImGui::IsItemHovered()) {
        tooltip("MIDI notes and the audio input drive the Sonic Character (sonic.*, timbre.*) and the musical "
                "context (notes.*) in place of the project's MIDI file and audio. Off: nothing live runs.");
    }
    ImGui::SameLine();
    if (onOpenLiveDemo && ImGui::Button("Open live demo")) {
        onOpenLiveDemo();
    }
    ImGui::Separator();

    // ---- audio ----
    {
        std::vector<const char*> names;
        names.push_back("(none)");
        int current = 0;
        const std::string openName = engine.audioInput() != nullptr ? engine.audioInput()->deviceName() : std::string();
        for (std::size_t i = 0; i < liveAudioDevices_.size(); ++i) {
            names.push_back(liveAudioDevices_[i].name.c_str());
            if (!openName.empty() && liveAudioDevices_[i].name == openName) {
                current = static_cast<int>(i) + 1;
            }
        }
        ImGui::TextUnformatted("Audio input");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("##live-audio", &current, names.data(), static_cast<int>(names.size())) && onLiveAudioInput) {
            onLiveAudioInput(current == 0 ? std::string() : liveAudioDevices_[static_cast<std::size_t>(current - 1)].name);
        }
        light(st.audioReceiving, st.audioReceiving ? "Audio receiving" : "Audio: no signal");
        ImGui::SameLine();
        const float level = std::clamp((st.audioLevelDb + 60.0f) / 60.0f, 0.0f, 1.0f);
        char text[32];
        std::snprintf(text, sizeof text, "%.0f dBFS", static_cast<double>(std::max(st.audioLevelDb, -120.0f)));
        ImGui::ProgressBar(on ? level : 0.0f, ImVec2(-1, 0), text);
        if (engine.audioInput() != nullptr) {
            ImGui::TextDisabled("%s, %u Hz", openName.c_str(), engine.audioInput()->sampleRate());
        } else if (engine.runner() != nullptr) {
            ImGui::TextDisabled("no input device: listening to the playing audio file");
        } else {
            ImGui::TextDisabled("choose the input your synth is connected to");
        }
        if (auto* gain = engine.params().find("audio/inputGain")) {
            // Sensitivity is the input gain, shown in dB: the character reads levels on fixed dB ranges, so a
            // quiet interface channel reads as a quiet sound until it is brought up here.
            const float linear = std::max(gain->baseComponent(0), 1e-4f);
            float db = 20.0f * std::log10(linear);
            ImGui::SetNextItemWidth(-90);
            if (ImGui::SliderFloat("Sensitivity", &db, -24.0f, 18.0f, "%+.1f dB")) {
                gain->setBaseComponent(0, std::clamp(std::pow(10.0f, db / 20.0f), 0.0f, 8.0f));
            }
            if (ImGui::IsItemHovered()) {
                tooltip("The input gain before analysis (audio/inputGain). Aim for the level meter to peak "
                        "around -12 dBFS when you play hard.");
            }
        }
    }
    ImGui::Separator();

    // ---- MIDI ----
    {
        auto& hub = engine.control();
        const std::string filter = hub.map().midiFilter.empty() ? "*" : hub.map().midiFilter;
        std::vector<const char*> names;
        names.push_back("All MIDI inputs");
        int current = 0;
        for (std::size_t i = 0; i < liveMidiDevices_.size(); ++i) {
            names.push_back(liveMidiDevices_[i].name.c_str());
            if (filter != "*" && liveMidiDevices_[i].name == filter) {
                current = static_cast<int>(i) + 1;
            }
        }
        ImGui::TextUnformatted("MIDI input");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("##live-midi", &current, names.data(), static_cast<int>(names.size()))) {
            const std::string chosen =
                current == 0 ? std::string("*") : liveMidiDevices_[static_cast<std::size_t>(current - 1)].name;
            hub.map().midiEnabled = true;
            hub.map().midiFilter = chosen;
            hub.applyIo();
            if (onLiveMidiInput) {
                onLiveMidiInput(chosen);
            }
        }
        light(st.midiReceiving, st.midiReceiving ? "MIDI receiving" : "MIDI: nothing received");
        ImGui::SameLine();
        if (st.lastKey >= 0) {
            ImGui::TextDisabled("last %s vel %.2f ch %d   held %d   notes %llu",
                                sonic::pitchName(static_cast<float>(st.lastKey)).c_str(),
                                static_cast<double>(st.lastVelocity), st.lastChannel + 1, st.held,
                                static_cast<unsigned long long>(st.notes));
        } else {
            ImGui::TextDisabled("no notes yet");
        }
        const auto hubStatus = hub.status();
        if (!hubStatus.midiOpen) {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "MIDI closed%s%s",
                               hubStatus.midiError.empty() ? "" : ": ", hubStatus.midiError.c_str());
        } else if (liveMidiDevices_.empty()) {
            ImGui::TextDisabled("no MIDI device found: plug the keyboard in (it connects on arrival)");
        } else {
            ImGui::TextDisabled("%zu source(s) connected", hubStatus.midiSources.size());
        }
    }
    ImGui::Separator();

    // ---- feel and picture ----
    {
        float smoothing = engine.liveSonicSmoothing();
        ImGui::SetNextItemWidth(-90);
        if (ImGui::SliderFloat("Smoothing", &smoothing, 0.25f, 4.0f, "%.2fx", ImGuiSliderFlags_Logarithmic) &&
            onLiveSmoothing) {
            onLiveSmoothing(smoothing);
        }
        if (ImGui::IsItemHovered()) {
            tooltip("Scales how fast the Sonic Character follows the sound: below 1 snappier, above 1 calmer. "
                    "1 is the project's own tuning.");
        }
        if (machine != nullptr) {
            const char* modes[] = {"Off", "FXAA"};
            int mode = machine->liveAntialias ? 1 : 0;
            ImGui::SetNextItemWidth(-90);
            if (ImGui::Combo("Anti-aliasing", &mode, modes, 2)) {
                machine->liveAntialias = mode == 1;
                if (settings.onChanged) {
                    settings.onChanged();
                }
            }
            if (ImGui::IsItemHovered()) {
                tooltip("The live viewport's edge antialiasing (ADR-1024), the same setting as Settings > "
                        "Rendering. Settings also has \"Lowest scale\", which keeps thin geometry intact.");
            }
        }
    }
    ImGui::Separator();

    // ---- what it costs and how late it is ----
    if (on) {
        ImGui::TextDisabled("MIDI -> frame %.1f ms   audio analysis -> frame %.1f ms", st.midiToFrameMs,
                            st.snapshotToFrameMs);
        ImGui::TextDisabled("timbre %.0f us/frame on the analysis thread   %llu frames   %llu dropped",
                            st.timbreMicros, static_cast<unsigned long long>(st.audioFrames),
                            static_cast<unsigned long long>(st.audioDropped));
    } else {
        ImGui::TextDisabled("Enable to play. The Analysis panel's Sonic section shows the character live.");
    }
}

} // namespace avgen::ui
