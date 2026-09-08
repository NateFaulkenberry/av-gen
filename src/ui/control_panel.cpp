#include "ui/control_panel.hpp"

#include "ui/ui_logic.hpp"

#include <imgui.h>
#include <implot.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

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
            if (ImGui::MenuItem("Open Scene (glTF)...", "S") && onOpenScene) {
                onOpenScene();
            }
            if (ImGui::MenuItem("Open Environment (HDR)...", "E") && onOpenEnvironment) {
                onOpenEnvironment();
            }
            if (ImGui::MenuItem("Add Background Shader...") && onOpenShader) {
                onOpenShader();
            }
            if (ImGui::MenuItem("Add Post Shader...") && onOpenPostShader) {
                onOpenPostShader();
            }
            if (ImGui::MenuItem("Built-in Orb Scene") && onOrbScene) {
                onOrbScene();
            }
            if (ImGui::MenuItem("Save Scene As...") && onSaveScene) {
                onSaveScene();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Open Project...") && onOpenProject) {
                onOpenProject();
            }
            if (ImGui::MenuItem("Save Project As...") && onSaveProject) {
                onSaveProject();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Parameters", nullptr, &showParameters_);
            ImGui::MenuItem("Analysis", nullptr, &showAnalysis_);
            ImGui::MenuItem("Modulation", nullptr, &showModulation_);
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
    if (showModulation_) {
        ImGui::SetNextWindowSize(ImVec2(560, 420), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(stats.width > 0 ? std::max(16.0f, stats.width / 2.0f + 20.0f) : 1000.0f, 40),
                                ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Modulation", &showModulation_)) {
            drawModulation(engine);
        }
        ImGui::End();
    }
    if (showDemo_) {
        ImGui::ShowDemoWindow(&showDemo_);
    }
}

void ControlPanel::drawModulation(app::Engine& engine) {
    if (ImGui::BeginTabBar("modtabs")) {
        if (ImGui::BeginTabItem("Routes")) {
            drawRoutesTab(engine);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Sources")) {
            drawSourcesTab(engine);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Presets")) {
            drawPresetsTab(engine);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Shaders")) {
            drawShadersTab(engine);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Scene")) {
            drawSceneTab(engine);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

void ControlPanel::drawRoutesTab(app::Engine& engine) {
    using namespace params;
    auto& bus = engine.signals();
    auto& paramSet = engine.params();
    auto& modulator = engine.modulator();

    // ---- add route ----
    std::vector<const char*> signalNames;
    signalNames.reserve(bus.size());
    for (const auto& info : bus.infos()) {
        signalNames.push_back(info.name.c_str());
    }
    std::vector<const char*> targetNames;
    for (const auto* p : paramSet.ordered()) {
        if (p->flags().modulatable) {
            targetNames.push_back(p->path().c_str());
        }
    }
    newRouteSource_ = std::clamp(newRouteSource_, 0, std::max(0, static_cast<int>(signalNames.size()) - 1));
    newRouteTarget_ = std::clamp(newRouteTarget_, 0, std::max(0, static_cast<int>(targetNames.size()) - 1));
    ImGui::SetNextItemWidth(200);
    ImGui::Combo("##src", &newRouteSource_, signalNames.data(), static_cast<int>(signalNames.size()));
    ImGui::SameLine();
    ImGui::TextUnformatted("->");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200);
    ImGui::Combo("##dst", &newRouteTarget_, targetNames.data(), static_cast<int>(targetNames.size()));
    ImGui::SameLine();
    if (ImGui::Button("Add route") && !signalNames.empty() && !targetNames.empty()) {
        ModRoute r;
        r.source = signalNames[static_cast<std::size_t>(newRouteSource_)];
        r.target = targetNames[static_cast<std::size_t>(newRouteTarget_)];
        r.amount = 1.0f;
        r.chain.attackMs = 20.0f;
        r.chain.decayMs = 200.0f;
        modulator.addRoute(r);
        engine.rebind();
    }
    ImGui::Separator();

    // ---- route list ----
    static const char* ops[] = {"add", "multiply", "replace", "min", "max"};
    static const char* curves[] = {"linear", "power", "log", "exp", "scurve"};
    static const char* envelopes[] = {"none", "peak hold", "linear fall"};
    int removeIndex = -1;
    auto& routes = modulator.routes();
    for (std::size_t i = 0; i < routes.size(); ++i) {
        auto& route = routes[i];
        ImGui::PushID(static_cast<int>(i));
        const std::string header = route.source + " -> " + route.target;
        const bool open = ImGui::TreeNodeEx(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 60);
        ImGui::Checkbox("##on", &route.enabled);
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) {
            removeIndex = static_cast<int>(i);
        }
        if (open) {
            route.amount = sanitiseFinite(route.amount);
            const auto [lo, hi] = routeAmountBounds(route);
            ImGui::SliderFloat("amount", &route.amount, lo, hi);
            ImGui::SameLine();
            ImGui::TextDisabled("= %+.3f", static_cast<double>(route.lastOutput));
            int op = static_cast<int>(route.op);
            ImGui::SetNextItemWidth(110);
            if (ImGui::Combo("op", &op, ops, 5)) {
                route.op = static_cast<ModOp>(op);
                engine.rebind();
            }
            ImGui::SameLine();
            bool bipolar = route.polarity == Polarity::Bipolar;
            if (ImGui::Checkbox("bipolar", &bipolar)) {
                route.polarity = bipolar ? Polarity::Bipolar : Polarity::Unipolar;
            }
            ImGui::SetNextItemWidth(140);
            ImGui::SliderFloat("attack ms", &route.chain.attackMs, 0.0f, 2000.0f, "%.0f", ImGuiSliderFlags_Logarithmic);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(140);
            ImGui::SliderFloat("decay ms", &route.chain.decayMs, 0.0f, 5000.0f, "%.0f", ImGuiSliderFlags_Logarithmic);
            int curve = static_cast<int>(route.chain.curve);
            ImGui::SetNextItemWidth(110);
            if (ImGui::Combo("curve", &curve, curves, 5)) {
                route.chain.curve = static_cast<CurveType>(curve);
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100);
            ImGui::SliderFloat("k", &route.chain.curveAmount, 0.1f, 5.0f);
            int env = static_cast<int>(route.chain.envelope);
            ImGui::SetNextItemWidth(110);
            if (ImGui::Combo("envelope", &env, envelopes, 3)) {
                route.chain.envelope = static_cast<EnvelopeMode>(env);
            }
            if (route.chain.envelope != EnvelopeMode::None) {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(100);
                ImGui::SliderFloat("fall/s", &route.chain.envelopeFallPerSecond, 0.1f, 20.0f);
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if (removeIndex >= 0) {
        routes.erase(routes.begin() + removeIndex);
        engine.rebind();
    }
}

void ControlPanel::drawSourcesTab(app::Engine& engine) {
    static const char* kinds[] = {"lfo", "envelope", "noise", "random", "timeline", "macro"};
    ImGui::SetNextItemWidth(110);
    ImGui::Combo("##kind", &newSourceKind_, kinds, 6);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140);
    ImGui::InputText("##name", newSourceName_, sizeof(newSourceName_));
    ImGui::SameLine();
    if (ImGui::Button("Add source")) {
        engine.addSource(kinds[newSourceKind_], newSourceName_[0] ? newSourceName_ : "source");
    }
    ImGui::Separator();
    auto& bus = engine.signals();
    std::string removeKind;
    std::string removeName;
    for (const auto& source : engine.sources().sources()) {
        ImGui::PushID(source.get());
        const std::string header = source->kind() + " " + source->name();
        const bool open = ImGui::TreeNodeEx(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 24);
        if (ImGui::SmallButton("x")) {
            removeKind = source->kind();
            removeName = source->name();
        }
        if (open) {
            for (const auto& out : source->outputs()) {
                if (auto id = bus.find(out)) {
                    ImGui::ProgressBar(std::clamp(bus.value(*id), 0.0f, 1.0f), ImVec2(160, 0), out.c_str());
                }
            }
            if (source->kind() == "lfo") {
                auto* lfo = dynamic_cast<signals::LfoSource*>(source.get());
                int shape = static_cast<int>(lfo->shape());
                static const char* shapes[] = {"sine", "triangle", "saw", "square", "sample&hold"};
                ImGui::SetNextItemWidth(140);
                if (ImGui::Combo("shape", &shape, shapes, 5)) {
                    lfo->setShape(static_cast<signals::LfoShape>(shape));
                }
            }
            ImGui::TextDisabled("settings: Parameters window, group 'sources'");
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if (!removeName.empty()) {
        engine.removeSource(removeKind, removeName);
    }
}

void ControlPanel::drawPresetsTab(app::Engine& engine) {
    ImGui::SetNextItemWidth(200);
    ImGui::InputText("##preset", presetName_, sizeof(presetName_));
    ImGui::SameLine();
    if (ImGui::Button("Store")) {
        engine.storePreset(presetName_[0] ? presetName_ : "preset");
    }
    ImGui::Separator();
    auto& bank = engine.presets();
    std::string removeName;
    std::vector<const char*> names;
    for (const auto& preset : bank.presets()) {
        names.push_back(preset.name.c_str());
    }
    for (const auto& preset : bank.presets()) {
        ImGui::PushID(preset.name.c_str());
        if (ImGui::Button("Recall") && !engine.recallPreset(preset.name)) {
            status_ = "preset not found: " + preset.name;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) {
            removeName = preset.name;
        }
        ImGui::SameLine();
        ImGui::Text("%s (%zu values)", preset.name.c_str(), preset.values.size());
        ImGui::PopID();
    }
    if (!removeName.empty()) {
        bank.remove(removeName);
    }
    if (names.size() >= 2) {
        ImGui::Separator();
        ImGui::TextUnformatted("Morph");
        morphA_ = std::clamp(morphA_, 0, static_cast<int>(names.size()) - 1);
        morphB_ = std::clamp(morphB_, 0, static_cast<int>(names.size()) - 1);
        ImGui::SetNextItemWidth(140);
        ImGui::Combo("A", &morphA_, names.data(), static_cast<int>(names.size()));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(140);
        ImGui::Combo("B", &morphB_, names.data(), static_cast<int>(names.size()));
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderFloat("##morph", &morphT_, 0.0f, 1.0f, "A %.2f B")) {
            engine.morphPresets(names[static_cast<std::size_t>(morphA_)], names[static_cast<std::size_t>(morphB_)], morphT_);
        }
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
    ImGui::Text("Scene: %s", engine.controller().name().c_str());
    if (!engine.environmentPath().empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("env: %s", engine.environmentPath().filename().string().c_str());
    }
    ImGui::TextUnformatted("Audio response (modulation routes)");
    auto& modulator = engine.modulator();
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("Master gain", &modulator.masterGain, 0.0f, 3.0f);
    int id = 0;
    for (auto& route : modulator.routes()) {
        ImGui::PushID(id++);
        ImGui::Checkbox("##on", &route.enabled);
        ImGui::SameLine();
        const std::string label = route.source + " -> " + route.target;
        route.amount = sanitiseFinite(route.amount);
        const auto [lo, hi] = routeAmountBounds(route);
        ImGui::SetNextItemWidth(-90);
        // Ctrl+click still allows typing values beyond the slider range.
        ImGui::SliderFloat(label.c_str(), &route.amount, lo, hi);
        ImGui::SameLine();
        ImGui::Text("%+.2f", static_cast<double>(route.lastOutput));
        ImGui::PopID();
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
        default: {
            // ImGui dereferences the range pointers; use the component-0 soft range for all lanes.
            const float lo = param->softMin(0);
            const float hi = param->softMax(0);
            changed = ImGui::SliderScalarN(param->label().c_str(), ImGuiDataType_Float, values, static_cast<int>(n),
                                           &lo, &hi);
            break;
        }
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
    ImGui::Text("%.1f fps (%.1f ms)  cpu work %.2f ms  gpu %s", stats.fps, stats.frameIntervalMs, stats.cpuFrameMs,
                stats.gpuFrameMs >= 0.0 ? (std::to_string(stats.gpuFrameMs).substr(0, 5) + " ms").c_str() : "n/a");
    ImGui::Text("%ux%u  %u draws  %u tris  analysis %.0f us/hop (%llu frames)  modulation %.0f us", stats.width,
                stats.height, stats.drawCalls, stats.triangles, engine.stats().analysisHopMicros,
                static_cast<unsigned long long>(engine.stats().analysisFrames), engine.stats().modulationMicros);
    ImGui::TextDisabled("%s (%s)", stats.adapter.c_str(), stats.backend.c_str());
}


void ControlPanel::drawShadersTab(app::Engine& engine) {
    if (ImGui::Button("Add background...") && onOpenShader) {
        onOpenShader();
    }
    ImGui::SameLine();
    if (ImGui::Button("Add post...") && onOpenPostShader) {
        onOpenPostShader();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%zu reload(s) this session", engine.shaderLayers().reloadsThisSession());
    ImGui::Separator();
    auto& layers = engine.shaderLayers();
    std::uint32_t removeId = 0;
    std::uint32_t moveId = 0;
    int moveDelta = 0;
    std::uint32_t reloadId = 0;
    for (const auto& layer : layers.layers()) {
        ImGui::PushID(static_cast<int>(layer->id));
        ImGui::Checkbox("##on", &layer->enabled);
        ImGui::SameLine();
        ImGui::Text("%s  [%s]  %zu inputs, %zu passes", layer->name.c_str(), shaders::layerStageName(layer->stage),
                    layer->parsed.description.inputs.size(), layer->parsed.description.passes.size());
        ImGui::SameLine();
        if (ImGui::SmallButton("^")) {
            moveId = layer->id;
            moveDelta = -1;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("v")) {
            moveId = layer->id;
            moveDelta = 1;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("reload")) {
            reloadId = layer->id;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) {
            removeId = layer->id;
        }
        ImGui::TextDisabled("%s", layer->path.string().c_str());
        std::string error = layer->parseError;
        if (error.empty() && shaderErrorFor) {
            error = shaderErrorFor(layer->id);
        }
        if (!error.empty()) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", error.c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::PopID();
    }
    if (removeId != 0) {
        engine.removeShaderLayer(removeId);
    }
    if (moveId != 0) {
        layers.move(moveId, moveDelta);
    }
    if (reloadId != 0) {
        (void)layers.reload(reloadId);
    }
    if (layers.size() == 0) {
        ImGui::TextDisabled("no user shaders; drop a .wgsl file on the window or use Add");
    }
    ImGui::TextDisabled("inputs appear in the Parameters window under 'shader'");
}


void ControlPanel::drawSceneTab(app::Engine& engine) {
    auto* comp = engine.composition();
    if (comp == nullptr) {
        ImGui::TextDisabled("current scene: %s (not a composition)", engine.controller().name().c_str());
        if (ImGui::Button("New composition")) {
            engine.newComposition();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("or add a node to convert");
    } else {
        ImGui::Text("composition '%s': %zu nodes, radius %.2f", comp->name().c_str(), comp->nodeCount(),
                    static_cast<double>(comp->boundsRadius()));
        if (!engine.compositionPath().empty()) {
            ImGui::TextDisabled("%s", engine.compositionPath().string().c_str());
        }
    }
    ImGui::Separator();
    static const char* kinds[] = {"gltf", "orb", "grid", "particles", "scene"};
    ImGui::SetNextItemWidth(110);
    ImGui::Combo("##nodekind", &newNodeKind_, kinds, 5);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140);
    ImGui::InputText("##nodename", nodeName_, sizeof(nodeName_));
    ImGui::SameLine();
    const auto kind = static_cast<scene::NodeKind>(newNodeKind_);
    if (kind == scene::NodeKind::Gltf) {
        if (ImGui::Button("Add glTF node...") && onAddGltfNode) {
            onAddGltfNode();
        }
    } else if (kind == scene::NodeKind::Scene) {
        if (ImGui::Button("Add scene node...") && onAddSceneNode) {
            onAddSceneNode();
        }
    } else if (ImGui::Button("Add node")) {
        scene::CompositionNode node;
        node.name = nodeName_[0] ? nodeName_ : "node";
        node.kind = kind;
        if (auto r = engine.addNode(std::move(node)); !r) {
            status_ = r.error().message;
        }
    }
    if (comp == nullptr) {
        return;
    }
    ImGui::Separator();
    std::string removeName;
    for (const auto& node : comp->nodes()) {
        ImGui::PushID(node->name.c_str());
        ImGui::Text("%s  [%s]%s", node->name.c_str(), scene::nodeKindName(node->kind),
                    node->asset.empty() ? "" : (std::string("  ") + node->asset.filename().string()).c_str());
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 24);
        if (ImGui::SmallButton("x")) {
            removeName = node->name;
        }
        ImGui::PopID();
    }
    if (!removeName.empty()) {
        engine.removeNode(removeName);
    }
    ImGui::TextDisabled("node transforms and overrides: Parameters window, group 'nodes'");
}

} // namespace avgen::ui
