#include "ui/control_panel.hpp"

#include <imgui.h>
#include <implot.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace avgen::ui {

namespace {

constexpr std::size_t kBandHistory = 240;

std::string formatTime(double seconds) {
    if (seconds < 0.0) {
        seconds = 0.0;
    }
    const int minutes = static_cast<int>(seconds / 60.0);
    const double rest = seconds - minutes * 60.0;
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%d:%05.2f", minutes, rest);
    return buffer;
}

const char* bandName(std::size_t i) {
    static const char* names[] = {"bass", "lowMid", "mid", "highMid", "treble"};
    return i < 5 ? names[i] : "?";
}

} // namespace

void ControlPanel::draw(app::Engine& engine, const FrameStats& stats) {
    ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_PassthruCentralNode);

    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open Audio...", "O") && onOpenAudio) {
                onOpenAudio();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Parameters", nullptr, &showParameters_);
            ImGui::MenuItem("Analysis", nullptr, &showAnalysis_);
            ImGui::MenuItem("ImGui Demo", nullptr, &showDemo_);
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(16, 40), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Control")) {
        drawTransport(engine);
        ImGui::Separator();
        drawResponse(engine);
        ImGui::Separator();
        drawPerformance(engine, stats);
    }
    ImGui::End();

    if (showParameters_) {
        ImGui::SetNextWindowSize(ImVec2(420, 360), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(16, 520), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Parameters", &showParameters_)) {
            drawParameters(engine);
        }
        ImGui::End();
    }
    if (showAnalysis_) {
        ImGui::SetNextWindowSize(ImVec2(520, 620), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(stats.width > 0 ? std::max(16.0f, stats.width / 2.0f - 540.0f) : 460.0f, 40),
                                ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Analysis", &showAnalysis_)) {
            drawAnalysis(engine);
        }
        ImGui::End();
    }
    if (showDemo_) {
        ImGui::ShowDemoWindow(&showDemo_);
    }
}

void ControlPanel::drawTransport(app::Engine& engine) {
    if (ImGui::Button("Open Audio") && onOpenAudio) {
        onOpenAudio();
    }
    ImGui::SameLine();
    if (engine.hasAudio()) {
        ImGui::TextUnformatted(engine.audioPath().filename().string().c_str());
    } else {
        ImGui::TextDisabled("no audio loaded (drop a file on the window)");
    }
    if (!status_.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.4f, 1.0f), "%s", status_.c_str());
    }

    ImGui::BeginDisabled(!engine.hasAudio());
    const bool playing = engine.isPlaying();
    if (ImGui::Button(playing ? "Pause" : "Play", ImVec2(80, 0))) {
        engine.togglePlay();
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop", ImVec2(80, 0))) {
        engine.stop();
    }
    ImGui::SameLine();
    ImGui::Text("%s / %s", formatTime(engine.positionSeconds()).c_str(), formatTime(engine.durationSeconds()).c_str());

    float position = static_cast<float>(engine.positionSeconds());
    const float duration = static_cast<float>(std::max(engine.durationSeconds(), 0.001));
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderFloat("##seek", &position, 0.0f, duration, "%.2f s")) {
        engine.seekSeconds(static_cast<double>(position));
    }
    float volume = engine.volume();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderFloat("Volume", &volume, 0.0f, 1.0f)) {
        engine.setVolume(volume);
    }
    ImGui::EndDisabled();
}

void ControlPanel::drawResponse(app::Engine& engine) {
    ImGui::TextUnformatted("Audio response (modulation route amounts)");
    auto& modulator = engine.modulator();
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("Master gain", &modulator.masterGain, 0.0f, 3.0f);
    struct Row {
        const char* label;
        const char* target;
        float max;
    };
    const Row rows[] = {{"Bass -> scale", "orb/scale", 4.0f},
                        {"Mid -> rotation", "orb/rotationSpeed", 10.0f},
                        {"High -> emission", "orb/emissive", 20.0f},
                        {"RMS -> brightness", "scene/brightness", 3.0f},
                        {"Onset -> impulse", "orb/impulse", 2.0f}};
    for (const auto& row : rows) {
        if (auto* route = engine.routeForTarget(row.target)) {
            ImGui::PushID(row.target);
            ImGui::Checkbox("##on", &route->enabled);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-90);
            ImGui::SliderFloat(row.label, &route->amount, 0.0f, row.max);
            ImGui::SameLine();
            ImGui::Text("%+.2f", static_cast<double>(route->lastOutput));
            ImGui::PopID();
        }
    }
}

void ControlPanel::drawParameters(app::Engine& engine) {
    using namespace params;
    std::string currentGroup;
    bool groupOpen = false;
    for (IParameter* param : engine.params().ordered()) {
        if (!param->flags().exposed) {
            continue;
        }
        if (param->group() != currentGroup) {
            if (groupOpen) {
                ImGui::TreePop();
            }
            currentGroup = param->group();
            groupOpen = ImGui::TreeNodeEx(currentGroup.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
        }
        if (!groupOpen) {
            continue;
        }
        ImGui::PushID(param->path().c_str());
        const std::size_t n = param->componentCount();
        float values[4] = {};
        for (std::size_t i = 0; i < n && i < 4; ++i) {
            values[i] = param->baseComponent(i);
        }
        bool changed = false;
        switch (param->kind()) {
        case ParamKind::Bool: {
            bool b = values[0] >= 0.5f;
            changed = ImGui::Checkbox(param->label().c_str(), &b);
            values[0] = b ? 1.0f : 0.0f;
            break;
        }
        case ParamKind::Int: {
            int v = static_cast<int>(std::lround(values[0]));
            changed = ImGui::SliderInt(param->label().c_str(), &v, static_cast<int>(param->softMin(0)),
                                       static_cast<int>(param->softMax(0)));
            values[0] = static_cast<float>(v);
            break;
        }
        case ParamKind::Color:
            changed = n == 4 ? ImGui::ColorEdit4(param->label().c_str(), values, ImGuiColorEditFlags_Float)
                             : ImGui::ColorEdit3(param->label().c_str(), values, ImGuiColorEditFlags_Float);
            break;
        case ParamKind::Float:
            changed = ImGui::SliderFloat(param->label().c_str(), values, param->softMin(0), param->softMax(0));
            break;
        default:
            changed = ImGui::SliderScalarN(param->label().c_str(), ImGuiDataType_Float, values, static_cast<int>(n),
                                           nullptr, nullptr);
            break;
        }
        if (changed) {
            for (std::size_t i = 0; i < n && i < 4; ++i) {
                param->setBaseComponent(i, values[i]);
            }
        }
        // Show the modulated (final) value next to the slider when it differs.
        if (n == 1 && std::abs(param->finalComponent(0) - param->baseComponent(0)) > 1e-5f) {
            ImGui::SameLine();
            ImGui::TextDisabled("= %.3f", static_cast<double>(param->finalComponent(0)));
        }
        if (ImGui::BeginPopupContextItem("reset")) {
            if (ImGui::MenuItem("Reset to default")) {
                param->resetToDefault();
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    if (groupOpen) {
        ImGui::TreePop();
    }
}

void ControlPanel::drawAnalysis(app::Engine& engine) {
    const auto& frame = engine.latestFrame();
    const bool have = engine.hasFrame();

    // Band history ring
    for (std::size_t b = 0; b < 5; ++b) {
        if (bandHistory_[b].size() != kBandHistory) {
            bandHistory_[b].assign(kBandHistory, 0.0f);
        }
        bandHistory_[b][bandHistoryHead_] = have && b < frame.bandCount ? frame.bands[b] : 0.0f;
    }
    bandHistoryHead_ = (bandHistoryHead_ + 1) % kBandHistory;
    onsetFlash_ = have && frame.onset ? 1.0f : onsetFlash_ * 0.85f;

    const auto d = [have](float v) { return have ? static_cast<double>(v) : 0.0; };
    ImGui::Text("rms %.3f  peak %.3f  centroid %.0f Hz  flux %.3f  onset %.2f", d(frame.rms), d(frame.peak),
                d(frame.centroidHz), d(frame.flux), d(frame.onsetStrength));
    ImGui::SameLine();
    ImGui::ColorButton("##onset", ImVec4(onsetFlash_, onsetFlash_ * 0.5f, 0.1f, 1.0f), ImGuiColorEditFlags_NoTooltip,
                       ImVec2(18, 18));

    // Bands
    if (ImPlot::BeginPlot("Bands", ImVec2(-1, 140), ImPlotFlags_NoLegend | ImPlotFlags_NoMenus)) {
        ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoDecorations, ImPlotAxisFlags_Lock);
        ImPlot::SetupAxesLimits(-0.5, 4.5, 0.0, 1.0, ImPlotCond_Always);
        float values[5] = {};
        for (std::size_t b = 0; b < 5; ++b) {
            values[b] = have && b < frame.bandCount ? frame.bands[b] : 0.0f;
        }
        ImPlot::PlotBars("bands", values, 5, 0.7);
        ImPlot::EndPlot();
    }
    ImGui::Text("bass %.2f  lowMid %.2f  mid %.2f  highMid %.2f  treble %.2f", d(frame.bands[0]), d(frame.bands[1]),
                d(frame.bands[2]), d(frame.bands[3]), d(frame.bands[4]));

    // Band history
    if (ImPlot::BeginPlot("Band history", ImVec2(-1, 140), ImPlotFlags_NoMenus)) {
        ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoDecorations, ImPlotAxisFlags_Lock);
        ImPlot::SetupAxesLimits(0, static_cast<double>(kBandHistory), 0.0, 1.05, ImPlotCond_Always);
        plotY_.resize(kBandHistory);
        for (std::size_t b = 0; b < 5; ++b) {
            for (std::size_t i = 0; i < kBandHistory; ++i) {
                plotY_[i] = bandHistory_[b][(bandHistoryHead_ + i) % kBandHistory];
            }
            ImPlot::PlotLine(bandName(b), plotY_.data(), static_cast<int>(kBandHistory));
        }
        ImPlot::EndPlot();
    }

    // Spectrum
    if (ImPlot::BeginPlot("Spectrum", ImVec2(-1, 160), ImPlotFlags_NoLegend | ImPlotFlags_NoMenus)) {
        ImPlot::SetupAxes("Hz", nullptr, ImPlotAxisFlags_None, ImPlotAxisFlags_Lock);
        ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);
        const auto& cfg = engine.analyzerConfig();
        const double nyquist = cfg.sampleRate / 2.0;
        ImPlot::SetupAxesLimits(20.0, nyquist, 0.0, 1.0, ImPlotCond_Always);
        if (have && !frame.spectrum.empty()) {
            const std::size_t bins = frame.spectrum.size();
            plotX_.resize(bins);
            plotY_.resize(bins);
            for (std::size_t i = 0; i < bins; ++i) {
                plotX_[i] = static_cast<float>(i) * static_cast<float>(cfg.sampleRate) / static_cast<float>(cfg.windowSize);
                plotY_[i] = frame.spectrum[i];
            }
            ImPlot::PlotShaded("spectrum", plotX_.data() + 1, plotY_.data() + 1, static_cast<int>(bins - 1), 0.0);
            ImPlot::PlotLine("spectrum", plotX_.data() + 1, plotY_.data() + 1, static_cast<int>(bins - 1));
        }
        ImPlot::EndPlot();
    }

    // Waveform: a window of the decoded file around the play-head.
    if (ImPlot::BeginPlot("Waveform", ImVec2(-1, 120), ImPlotFlags_NoLegend | ImPlotFlags_NoMenus)) {
        ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoDecorations, ImPlotAxisFlags_Lock);
        constexpr int kPoints = 1024;
        ImPlot::SetupAxesLimits(0, kPoints, -1.0, 1.0, ImPlotCond_Always);
        if (auto file = engine.audioFile()) {
            const auto mono = file->mono();
            const double centre = engine.positionSeconds() * file->sampleRate();
            const double span = file->sampleRate() * 0.2; // 200 ms window
            waveform_.resize(kPoints);
            for (int i = 0; i < kPoints; ++i) {
                const double t = centre - span * 0.5 + span * static_cast<double>(i) / kPoints;
                const auto idx = static_cast<long long>(t);
                waveform_[static_cast<std::size_t>(i)] =
                    idx >= 0 && idx < static_cast<long long>(mono.size()) ? mono[static_cast<std::size_t>(idx)] : 0.0f;
            }
            ImPlot::PlotLine("wave", waveform_.data(), kPoints);
        }
        ImPlot::EndPlot();
    }
}

void ControlPanel::drawPerformance(app::Engine& engine, const FrameStats& stats) {
    ImGui::Text("%.1f fps  cpu %.2f ms  gpu %s", stats.fps, stats.cpuFrameMs,
                stats.gpuFrameMs >= 0.0 ? (std::to_string(stats.gpuFrameMs).substr(0, 5) + " ms").c_str() : "n/a");
    ImGui::Text("%ux%u  %u draws  %u tris  analysis %.0f us/hop (%llu frames)  modulation %.0f us", stats.width,
                stats.height, stats.drawCalls, stats.triangles, engine.stats().analysisHopMicros,
                static_cast<unsigned long long>(engine.stats().analysisFrames), engine.stats().modulationMicros);
    ImGui::TextDisabled("%s (%s)", stats.adapter.c_str(), stats.backend.c_str());
}

} // namespace avgen::ui
