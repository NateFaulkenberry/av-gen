// ADR-1025: the Live panel -- the smallest useful surface for playing AV Gen with a MIDI keyboard and a synth
// (brief 01-brief-live.md PART 13). Debug-level on purpose: pickers, two receiving lights, enable, the live
// antialiasing setting (ADR-1024's, the same one Settings shows), sensitivity and smoothing, and the measured
// latencies. Everything it changes is either this machine's (devices, smoothing, AA: the settings file) or the
// project's existing `audio/inputGain` parameter.

#include "ui/control_panel.hpp"

#include "audio/audio_input.hpp"
#include "control/midi.hpp"
#include "params/parameter.hpp"
#include "sonic/notes.hpp"
#include "ui/style.hpp"

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
        liveLastScan_ = now;
    }
    app::AppSettings* machine = settings.settings;
    const bool on = engine.liveSonic();
    const auto st = engine.liveSonicStatus();

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
