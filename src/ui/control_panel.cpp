#include "ui/control_panel.hpp"

#include "ui/ui_logic.hpp"

#include "audio/audio_input.hpp"
#include "control/midi.hpp"
#include "platform/window.hpp"

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
    // Default window placement is in ImGui coordinates (logical points), which are not the
    // framebuffer pixels in `stats` on a scaled display: using the latter puts panels off-screen.
    const float uiWidth = ImGui::GetMainViewport()->Size.x;

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
            ImGui::Separator();
            if (ImGui::MenuItem("New Project") && onNewProject) {
                onNewProject();
            }
            if (ImGui::MenuItem("Open Project...") && onOpenProject) {
                onOpenProject();
            }
            if (ImGui::BeginMenu("Examples", !examples.empty())) {
                std::string category;
                for (const auto& ex : examples) {
                    if (ex.category != category) {
                        if (!category.empty()) {
                            ImGui::Separator();
                        }
                        ImGui::TextDisabled("%s", ex.category.c_str());
                        category = ex.category;
                    }
                    if (ImGui::MenuItem(ex.name.c_str()) && onOpenExample) {
                        onOpenExample(ex);
                    }
                    if (ImGui::IsItemHovered() && !ex.description.empty()) {
                        ImGui::SetTooltip("%s", ex.description.c_str());
                    }
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Open Recent", !recentProjects.empty())) {
                for (const auto& recent : recentProjects) {
                    if (ImGui::MenuItem(recent.filename().string().c_str()) && onOpenRecent) {
                        onOpenRecent(recent);
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", recent.string().c_str());
                    }
                }
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Save Project", "Cmd+S", false, !engine.projectPath().empty()) && onSaveProjectHere) {
                onSaveProjectHere();
            }
            if (ImGui::MenuItem("Save Project As...") && onSaveProject) {
                onSaveProject();
            }
            if (ImGui::MenuItem("Export Bundle...") && onExportBundle) {
                onExportBundle();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Parameters", nullptr, &showParameters_);
            ImGui::MenuItem("Analysis", nullptr, &showAnalysis_);
            ImGui::MenuItem("Modulation", nullptr, &showModulation_);
            ImGui::MenuItem("World", nullptr, &showWorld_);
            ImGui::MenuItem("Assets", nullptr, &showAssets_);
            ImGui::MenuItem("Graph", nullptr, &showGraph_);
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
        ImGui::SetNextWindowPos(ImVec2(std::max(16.0f, uiWidth * 0.5f - 540.0f), 40),
                                ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Analysis", &showAnalysis_)) {
            drawAnalysis(engine);
        }
        ImGui::End();
    }
    if (showModulation_) {
        ImGui::SetNextWindowSize(ImVec2(560, 420), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(std::max(16.0f, uiWidth * 0.5f + 20.0f), 40),
                                ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Modulation", &showModulation_)) {
            drawModulation(engine);
        }
        ImGui::End();
    }
    if (showWorld_) {
        ImGui::SetNextWindowSize(ImVec2(460, 520), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(std::max(16.0f, uiWidth - 480.0f), 40),
                                ImGuiCond_FirstUseEver);
        if (ImGui::Begin("World", &showWorld_)) {
            drawWorldWindow(engine);
        }
        ImGui::End();
    }
    if (showAssets_) {
        ImGui::SetNextWindowSize(ImVec2(520, 420), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(120, 120), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Assets", &showAssets_)) {
            drawAssetsWindow();
        }
        ImGui::End();
    }
    if (showGraph_) {
        ImGui::SetNextWindowSize(ImVec2(900, 560), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(80, 80), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Graph", &showGraph_)) {
            drawGraphWindow(engine);
        }
        ImGui::End();
    }
    if (showDemo_) {
        ImGui::ShowDemoWindow(&showDemo_);
    }
}

void ControlPanel::drawGraphWindow(app::Engine& engine) {
    scene::Composition* composition = engine.composition();
    if (composition == nullptr) {
        ImGui::TextDisabled("The current scene is not a composition, so it cannot hold a graph.");
        return;
    }
    graphEditor.onChanged = [composition] { composition->markGraphDirty(); };
    graphEditor.draw(composition->graph());
    if (!composition->graphWarnings().empty()) {
        ImGui::Separator();
        for (const std::string& warning : composition->graphWarnings()) {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "%s", warning.c_str());
        }
    }
}

void ControlPanel::drawAssetsWindow() {
    if (ImGui::Button("Rescan") && onRescanAssets) {
        onRescanAssets();
    }
    ImGui::SameLine();
    const char* kinds[] = {"all", "project", "scene", "graph", "preset", "model", "environment", "shader", "audio"};
    ImGui::SetNextItemWidth(140.0f);
    ImGui::Combo("kind", &assetKind_, kinds, IM_ARRAYSIZE(kinds));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200.0f);
    ImGui::InputText("search", assetSearch_, sizeof(assetSearch_));
    const app::AssetKind kind = assetKind_ == 0 ? app::AssetKind::Unknown : static_cast<app::AssetKind>(assetKind_ - 1);
    const auto shown = app::filterAssets(assets, kind, assetSearch_);
    ImGui::TextDisabled("%zu of %zu", shown.size(), assets.size());
    ImGui::Separator();
    if (ImGui::BeginTable("assets", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("name");
        ImGui::TableSetupColumn("kind", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("category", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("thumb", ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableHeadersRow();
        for (const app::AssetEntry* a : shown) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushID(a->path.string().c_str());
            if (ImGui::Selectable(a->name.c_str(), false, ImGuiSelectableFlags_SpanAllColumns) && onOpenAsset) {
                onOpenAsset(*a);
            }
            if (ImGui::IsItemHovered() && !a->description.empty()) {
                ImGui::SetTooltip("%s\n%s", a->description.c_str(), a->path.string().c_str());
            }
            ImGui::PopID();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(app::assetKindName(a->kind));
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(a->category.c_str());
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", a->thumbnail.empty() ? "-" : "png");
        }
        ImGui::EndTable();
    }
}

void ControlPanel::drawWorldWindow(app::Engine& engine) {
    world.drawLayerSelector();
    if (ImGui::BeginTabBar("world")) {
        if (ImGui::BeginTabItem("Overview")) {
            world.drawOverview(engine);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Inspector")) {
            world.drawInspector(engine);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("States")) {
            world.drawStates(engine);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Direction")) {
            world.drawDirector(engine);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Macros")) {
            world.drawMacros(engine);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Debug")) {
            world.drawDebugOptions(engine);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
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
        if (ImGui::BeginTabItem("Timeline")) {
            drawTimelineTab(engine);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Control")) {
            drawControlTab(engine);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Outputs")) {
            drawOutputsTab(engine);
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
    if (engine.mode() == app::EngineMode::Live) {
        static std::vector<audio::AudioDeviceInfo> devices;
        static double lastScan = -1.0;
        const double now = ImGui::GetTime();
        if (now - lastScan > 5.0) {
            devices = audio::listCaptureDevices();
            lastScan = now;
        }
        std::vector<const char*> names;
        names.push_back("(audio file)");
        for (const auto& d : devices) {
            names.push_back(d.name.c_str());
        }
        inputDevice_ = std::clamp(inputDevice_, 0, static_cast<int>(names.size()) - 1);
        ImGui::SetNextItemWidth(220);
        if (ImGui::Combo("input", &inputDevice_, names.data(), static_cast<int>(names.size()))) {
            if (inputDevice_ == 0) {
                if (onStopAudioInput) onStopAudioInput();
            } else if (onUseAudioInput) {
                onUseAudioInput(devices[static_cast<std::size_t>(inputDevice_ - 1)].name);
            }
        }
        if (auto* input = engine.audioInput()) {
            ImGui::SameLine();
            ImGui::ProgressBar(std::clamp(input->lastPeak(), 0.0f, 1.0f), ImVec2(80, 0), "peak");
            ImGui::SameLine();
            ImGui::TextDisabled("%s %u Hz", input->deviceName().c_str(), input->sampleRate());
        }
    }
    if (!engine.projectPath().empty()) {
        ImGui::TextDisabled("project: %s", engine.projectPath().filename().string().c_str());
        if (!engine.projectWarnings().empty()) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "(%zu warning(s))", engine.projectWarnings().size());
            if (ImGui::IsItemHovered()) {
                std::string all;
                for (const auto& w : engine.projectWarnings()) {
                    all += w + "\n";
                }
                ImGui::SetTooltip("%s", all.c_str());
            }
        }
    }
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
        if (!world.shows(param->path())) {
            continue; // hidden by the authoring layer (World window)
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
        if (engine.timeline().isAutomated(param->path())) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "[A]");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("automated by the timeline; the slider is the base value");
            }
        }
        if (ImGui::BeginPopupContextItem("reset")) {
            if (ImGui::MenuItem("Reset to default")) {
                param->resetToDefault();
            }
            if (ImGui::MenuItem("Key at current time")) {
                engine.recordKey(param->path());
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
    if (stats.procedural.objects > 0) {
        const auto& pr = stats.procedural;
        ImGui::Text("procedural: %u objects  %llu instances  %u src tris  %llu logical tris  %u deformers  %.1f KB instance buffers  cpu %.3f ms",
                    pr.objects, static_cast<unsigned long long>(pr.instances), pr.sourceTriangles,
                    static_cast<unsigned long long>(pr.logicalTriangles), pr.deformers,
                    static_cast<double>(pr.instanceBufferBytes) / 1024.0, pr.cpuUpdateMs);
        if (pr.effectorObjects > 0 || pr.fieldDeformers > 0 || pr.pointObjects > 0) {
            ImGui::Text("fields: %u effector objects  %llu records  %u effectors  pass %.3f ms  %u field deformers  %u point objects",
                        pr.effectorObjects, static_cast<unsigned long long>(pr.effectorInstances), pr.effectors,
                        pr.effectorPassMs, pr.fieldDeformers, pr.pointObjects);
        }
        // Culling / LOD (ADR-029). The counts come from an asynchronous readback, so they trail
        // the drawn frame by a frame or two; the pass time is the GPU timer's.
        if (pr.cullObjects > 0) {
            ImGui::Text("culling: %u objects  %llu visible  %llu culled  lod %llu/%llu/%llu/%llu  pass %.3f ms",
                        pr.cullObjects, static_cast<unsigned long long>(pr.visibleInstances),
                        static_cast<unsigned long long>(pr.culledInstances),
                        static_cast<unsigned long long>(pr.lodCounts[0]),
                        static_cast<unsigned long long>(pr.lodCounts[1]),
                        static_cast<unsigned long long>(pr.lodCounts[2]),
                        static_cast<unsigned long long>(pr.lodCounts[3]), pr.cullMs);
        }
    }
    if (stats.sdf.objects > 0) {
        ImGui::Text("sdf: %u objects (%u raymarched, %u meshed)  %u packed nodes  %u mesh tris  pass %.3f ms",
                    stats.sdf.objects, stats.sdf.raymarchObjects, stats.sdf.meshObjects, stats.sdf.packedNodes,
                    stats.sdf.meshTriangles, stats.sdf.raymarchMs);
    }
    if (stats.particles.systems > 0) {
        ImGui::Text("particles: %u systems  %u capacity  %u emitted  simulate %.3f ms", stats.particles.systems,
                    stats.particles.capacity, stats.particles.emittedThisFrame, stats.particles.simulateMs);
    }
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
    static const char* kinds[] = {"gltf", "orb", "grid", "particles", "scene", "procedural", "field", "spline", "sdf"};
    ImGui::SetNextItemWidth(110);
    ImGui::Combo("##nodekind", &newNodeKind_, kinds, 6);
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


void ControlPanel::drawTimelineTab(app::Engine& engine) {
    using namespace params;
    auto& timeline = engine.timeline();
    const auto& clock = engine.timelineClock();
    ImGui::Checkbox("enabled", &timeline.enabled);
    ImGui::SameLine();
    ImGui::Text("t = %.2f s, beat %.2f", clock.seconds, clock.beats);
    if (const auto cue = engine.cueState(); cue.index >= 0 &&
                                            static_cast<std::size_t>(cue.index) < timeline.cues().size()) {
        ImGui::SameLine();
        ImGui::TextDisabled("cue '%s' %.0f%%", timeline.cues()[static_cast<std::size_t>(cue.index)].name.c_str(),
                            static_cast<double>(cue.progress) * 100.0);
    }
    ImGui::Separator();

    // ---- add a key for a parameter at the current time ----
    static const char* interps[] = {"step", "linear", "smooth", "easeIn", "easeOut", "easeInOut", "bezier"};
    static const char* bases[] = {"seconds", "beats"};
    std::vector<const char*> targets;
    for (const auto* p : engine.params().ordered()) {
        if (p->flags().modulatable) {
            targets.push_back(p->path().c_str());
        }
    }
    keyTarget_ = std::clamp(keyTarget_, 0, std::max(0, static_cast<int>(targets.size()) - 1));
    ImGui::SetNextItemWidth(220);
    ImGui::Combo("##keytarget", &keyTarget_, targets.data(), static_cast<int>(targets.size()));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    ImGui::Combo("##keyinterp", &keyInterp_, interps, 7);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    ImGui::Combo("##keybase", &keyBase_, bases, 2);
    ImGui::SameLine();
    if (ImGui::Button("Add key") && !targets.empty()) {
        engine.recordKey(targets[static_cast<std::size_t>(keyTarget_)], -1, static_cast<KeyInterp>(keyInterp_),
                         static_cast<TimeBase>(keyBase_));
    }

    // ---- tracks ----
    int removeTrack = -1;
    auto& tracks = timeline.tracks();
    for (std::size_t i = 0; i < tracks.size(); ++i) {
        auto& track = tracks[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::Checkbox("##on", &track.enabled);
        ImGui::SameLine();
        const bool open = ImGui::TreeNodeEx("track", ImGuiTreeNodeFlags_None, "%s%s  (%zu keys, %s%s)",
                                            track.target.c_str(), track.param == nullptr ? " [unbound]" : "",
                                            track.keys.size(), timeBaseName(track.timeBase),
                                            track.loopLength > 0.0 ? ", loop" : "");
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 24);
        if (ImGui::SmallButton("x")) {
            removeTrack = static_cast<int>(i);
        }
        if (open) {
            static const char* modes[] = {"replace", "add", "multiply"};
            int mode = static_cast<int>(track.mode);
            ImGui::SetNextItemWidth(90);
            if (ImGui::Combo("mode", &mode, modes, 3)) {
                track.mode = static_cast<TrackMode>(mode);
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(90);
            auto loop = static_cast<float>(track.loopLength);
            if (ImGui::DragFloat("loop", &loop, 0.1f, 0.0f, 3600.0f, "%.2f")) {
                track.loopLength = static_cast<double>(std::max(0.0f, loop));
            }
            // Curve preview over the key span (component 0).
            if (!track.keys.empty() && ImPlot::BeginPlot("##curve", ImVec2(-1, 90), ImPlotFlags_NoLegend | ImPlotFlags_NoMenus)) {
                const double t0 = track.firstKeyTime();
                const double t1 = std::max(track.lastKeyTime(), t0 + 1e-3);
                const double span = track.loopLength > 0.0 ? std::max(track.loopLength, t1 - t0) : (t1 - t0);
                constexpr int kSamples = 128;
                plotX_.resize(kSamples);
                std::vector<float> ys(kSamples);
                for (int k = 0; k < kSamples; ++k) {
                    const double t = t0 + span * k / (kSamples - 1);
                    plotX_[static_cast<std::size_t>(k)] = static_cast<float>(t);
                    ys[static_cast<std::size_t>(k)] = track.evaluate(t)[0];
                }
                ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoDecorations, ImPlotAxisFlags_AutoFit);
                ImPlot::SetupAxisLimits(ImAxis_X1, t0, t0 + span, ImPlotCond_Always);
                ImPlot::PlotLine("value", plotX_.data(), ys.data(), kSamples);
                const double now = track.localTime(clock.at(track.timeBase));
                const double nowX[1] = {now};
                ImPlot::PlotInfLines("now", nowX, 1);
                // Curve editor: every key (component 0) is a draggable point; time re-sorts on
                // release so the curve stays a function of time while dragging.
                bool dragging = false;
                for (std::size_t k = 0; k < track.keys.size(); ++k) {
                    auto& key = track.keys[k];
                    double kx = key.time;
                    double ky = static_cast<double>(key.value[0]);
                    bool held = false;
                    if (ImPlot::DragPoint(static_cast<int>(k), &kx, &ky, ImVec4(1.0f, 0.75f, 0.3f, 1.0f), 6.0f, ImPlotDragToolFlags_None, nullptr, nullptr, &held)) {
                        key.time = std::max(0.0, kx);
                        key.value[0] = static_cast<float>(ky);
                    }
                    dragging = dragging || held;
                }
                if (!dragging && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    track.sortKeys();
                }
                ImPlot::EndPlot();
            }
            int removeKey = -1;
            bool resort = false;
            const std::size_t comps = track.keyedComponents();
            for (std::size_t k = 0; k < track.keys.size(); ++k) {
                auto& key = track.keys[k];
                ImGui::PushID(static_cast<int>(k));
                ImGui::SetNextItemWidth(70);
                auto t = static_cast<float>(key.time);
                if (ImGui::DragFloat("##t", &t, 0.01f, 0.0f, 0.0f, "%.2f")) {
                    key.time = static_cast<double>(std::max(0.0f, t));
                    resort = true;
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(160);
                ImGui::DragScalarN("##v", ImGuiDataType_Float, key.value.data(), static_cast<int>(std::min<std::size_t>(comps, 4)), 0.01f);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(90);
                int interp = static_cast<int>(key.interp);
                if (ImGui::Combo("##i", &interp, interps, 7)) {
                    key.interp = static_cast<KeyInterp>(interp);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("x")) {
                    removeKey = static_cast<int>(k);
                }
                ImGui::PopID();
            }
            if (removeKey >= 0) {
                track.keys.erase(track.keys.begin() + removeKey);
            }
            if (resort && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                track.sortKeys();
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if (removeTrack >= 0) {
        timeline.removeTrack(static_cast<std::size_t>(removeTrack));
    }

    // ---- cues ----
    ImGui::Separator();
    ImGui::TextUnformatted("Cues");
    std::vector<const char*> presetNames;
    presetNames.push_back("(marker)");
    for (const auto& preset : engine.presets().presets()) {
        presetNames.push_back(preset.name.c_str());
    }
    cuePreset_ = std::clamp(cuePreset_, 0, static_cast<int>(presetNames.size()) - 1);
    ImGui::SetNextItemWidth(100);
    ImGui::InputText("##cuename", cueName_, sizeof(cueName_));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    ImGui::Combo("##cuepreset", &cuePreset_, presetNames.data(), static_cast<int>(presetNames.size()));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70);
    ImGui::DragFloat("morph s", &cueMorph_, 0.05f, 0.0f, 60.0f, "%.2f");
    ImGui::SameLine();
    if (ImGui::Button("Add cue at t")) {
        Cue cue;
        cue.time = clock.seconds;
        cue.name = cueName_[0] ? cueName_ : "cue";
        cue.preset = cuePreset_ > 0 ? presetNames[static_cast<std::size_t>(cuePreset_)] : "";
        cue.morphSeconds = static_cast<double>(cueMorph_);
        timeline.addCue(std::move(cue));
    }
    int removeCue = -1;
    auto& cues = timeline.cues();
    for (std::size_t i = 0; i < cues.size(); ++i) {
        auto& cue = cues[i];
        ImGui::PushID(static_cast<int>(i) + 10000);
        ImGui::SetNextItemWidth(70);
        auto t = static_cast<float>(cue.time);
        if (ImGui::DragFloat("##ct", &t, 0.01f, 0.0f, 0.0f, "%.2f")) {
            cue.time = static_cast<double>(std::max(0.0f, t));
        }
        ImGui::SameLine();
        ImGui::Text("%s -> %s (%.2f s morph, %s)", cue.name.c_str(), cue.preset.empty() ? "marker" : cue.preset.c_str(),
                    cue.morphSeconds, timeBaseName(cue.timeBase));
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) {
            removeCue = static_cast<int>(i);
        }
        ImGui::PopID();
    }
    if (removeCue >= 0) {
        timeline.removeCue(static_cast<std::size_t>(removeCue));
    } else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        timeline.sortCues();
    }
}


void ControlPanel::drawRender(app::Engine& engine) {
    ImGui::SetNextWindowSize(ImVec2(460, 420), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Render", &showRender_)) {
        ImGui::End();
        return;
    }
    if (renderSettings == nullptr) {
        ImGui::TextDisabled("render settings unavailable");
        ImGui::End();
        return;
    }
    auto& s = *renderSettings;
    const app::RenderProgress current = renderProgress ? renderProgress() : app::RenderProgress{};
    const bool running = current.framesTotal > 0 && !current.finished;
    ImGui::BeginDisabled(running);
    int size[2] = {static_cast<int>(s.width), static_cast<int>(s.height)};
    if (ImGui::InputInt2("size", size)) {
        s.width = static_cast<std::uint32_t>(std::clamp(size[0], 2, 16384));
        s.height = static_cast<std::uint32_t>(std::clamp(size[1], 2, 16384));
    }
    auto fps = static_cast<float>(s.fps);
    if (ImGui::InputFloat("fps", &fps, 1.0f, 10.0f, "%.3f")) {
        s.fps = static_cast<double>(std::clamp(fps, 1.0f, 240.0f));
    }
    float range[2] = {static_cast<float>(s.startSeconds), static_cast<float>(s.endSeconds)};
    if (ImGui::InputFloat2("range (s, end<0 = auto)", range, "%.2f")) {
        s.startSeconds = static_cast<double>(std::max(0.0f, range[0]));
        s.endSeconds = static_cast<double>(range[1]);
    }
    const double end = s.resolvedEnd(engine.durationSeconds(), engine.timeline().durationSeconds());
    ImGui::TextDisabled("%llu frames (%.2f s .. %.2f s)", static_cast<unsigned long long>(s.frameCount(end)),
                        s.startSeconds, end);
    int output = s.output == app::RenderOutput::Video ? 1 : s.output == app::RenderOutput::ExrSequence ? 2 : 0;
    if (ImGui::RadioButton("PNG sequence", &output, 0)) {
        s.output = app::RenderOutput::PngSequence;
        s.normalisePattern();
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("EXR sequence", &output, 2)) {
        s.output = app::RenderOutput::ExrSequence;
        s.normalisePattern();
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Video", &output, 1)) {
        s.output = app::RenderOutput::Video;
    }
    char pathBuf[512];
    std::snprintf(pathBuf, sizeof(pathBuf), "%s", s.outputPath.string().c_str());
    ImGui::SetNextItemWidth(-90);
    if (ImGui::InputText("##out", pathBuf, sizeof(pathBuf))) {
        s.outputPath = pathBuf;
    }
    ImGui::SameLine();
    if (ImGui::Button("Choose...") && onChooseRenderOutput) {
        onChooseRenderOutput();
    }
    if (s.output == app::RenderOutput::Video) {
        static const char* codecs[] = {"prores4444", "prores422", "h264", "hevc", "libx264", "libx265", "prores_ks", "libvpx-vp9"};
        int codec = 0;
        for (int i = 0; i < 8; ++i) {
            if (s.codec == codecs[i]) {
                codec = i;
            }
        }
        ImGui::SetNextItemWidth(140);
        if (ImGui::Combo("codec", &codec, codecs, 8)) {
            s.codec = codecs[codec];
        }
        ImGui::SameLine();
        static const char* backends[] = {"auto", "native", "ffmpeg"};
        int backend = s.backend == "native" ? 1 : (s.backend == "ffmpeg" ? 2 : 0);
        ImGui::SetNextItemWidth(90);
        if (ImGui::Combo("backend", &backend, backends, 3)) {
            s.backend = backends[backend];
        }
        ImGui::SliderInt("quality", &s.quality, 0, 100);
        ImGui::Checkbox("mux audio", &s.muxAudio);
        if (!videoBackends.empty()) {
            ImGui::TextWrapped("%s", videoBackends.c_str());
        }
    } else {
        char pat[128];
        std::snprintf(pat, sizeof(pat), "%s", s.pattern.c_str());
        ImGui::SetNextItemWidth(200);
        if (ImGui::InputText("pattern", pat, sizeof(pat))) {
            s.pattern = pat;
        }
    }
    if (auto v = s.validate(); !v) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "%s", v.error().message.c_str());
    }
    ImGui::EndDisabled();
    ImGui::Separator();
    if (running) {
        ImGui::ProgressBar(static_cast<float>(current.fraction()), ImVec2(-1, 0));
        ImGui::Text("%llu / %llu frames, %.1f fps, %.0f s elapsed, written %llu",
                    static_cast<unsigned long long>(current.framesRendered),
                    static_cast<unsigned long long>(current.framesTotal), current.renderFps, current.elapsedSeconds,
                    static_cast<unsigned long long>(current.framesWritten));
        if (ImGui::Button("Cancel") && onCancelRender) {
            onCancelRender();
        }
    } else {
        if (ImGui::Button("Render") && onStartRender) {
            onStartRender();
        }
        ImGui::SameLine();
        if (ImGui::Button("Add to queue") && onEnqueueRender) {
            onEnqueueRender();
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(queuedRenders == 0);
        if (ImGui::Button("Run queue") && onRunQueue) {
            onRunQueue();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("%zu queued", queuedRenders);
        if (current.finished) {
            if (!current.error.empty()) {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "last render failed: %s", current.error.c_str());
            } else if (current.framesTotal > 0) {
                ImGui::TextDisabled("last render: %llu frames in %.1f s, sequence hash %016llx%s",
                                    static_cast<unsigned long long>(current.framesRendered), current.elapsedSeconds,
                                    static_cast<unsigned long long>(current.sequenceHash),
                                    current.cancelled ? " (cancelled)" : "");
            }
        }
    }
    ImGui::TextDisabled("renders load the saved project; the live view keeps playing");
    ImGui::End();
}


void ControlPanel::drawControlTab(app::Engine& engine) {
    auto& hub = engine.control();
    auto& map = hub.map();
    const auto status = hub.status();
    // ---- OSC ----
    bool ioChanged = false;
    ioChanged |= ImGui::Checkbox("OSC", &map.oscEnabled);
    ImGui::SameLine();
    int port = map.oscPort;
    ImGui::SetNextItemWidth(80);
    if (ImGui::InputInt("port", &port, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue)) {
        map.oscPort = static_cast<std::uint16_t>(std::clamp(port, 0, 65535));
        ioChanged = true;
    }
    ImGui::SameLine();
    if (status.oscOpen) {
        ImGui::TextDisabled("listening on %u: %llu msgs, %llu errors", status.oscPort,
                            static_cast<unsigned long long>(status.osc.messages),
                            static_cast<unsigned long long>(status.osc.errors));
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), "%s", status.oscError.empty() ? "closed" : status.oscError.c_str());
    }
    char prefix[64];
    std::snprintf(prefix, sizeof(prefix), "%s", map.oscPrefix.c_str());
    ImGui::SetNextItemWidth(120);
    if (ImGui::InputText("prefix", prefix, sizeof(prefix), ImGuiInputTextFlags_EnterReturnsTrue)) {
        map.oscPrefix = prefix;
    }
    ImGui::SameLine();
    ImGui::Checkbox("direct scheme", &map.directOsc);
    // ---- MIDI ----
    ioChanged |= ImGui::Checkbox("MIDI", &map.midiEnabled);
    ImGui::SameLine();
    char filter[64];
    std::snprintf(filter, sizeof(filter), "%s", map.midiFilter.c_str());
    ImGui::SetNextItemWidth(120);
    if (ImGui::InputText("filter", filter, sizeof(filter), ImGuiInputTextFlags_EnterReturnsTrue)) {
        map.midiFilter = filter;
        ioChanged = true;
    }
    ImGui::SameLine();
    if (status.midiOpen) {
        ImGui::TextDisabled("%zu source(s), %llu msgs", status.midiSources.size(),
                            static_cast<unsigned long long>(status.midi.messages));
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), "%s", status.midiError.empty() ? "closed" : status.midiError.c_str());
    }
    // ---- feedback + tempo source ----
    ioChanged |= ImGui::Checkbox("OSC feedback", &map.feedbackEnabled);
    ImGui::SameLine();
    char host[64];
    std::snprintf(host, sizeof(host), "%s", map.feedbackHost.c_str());
    ImGui::SetNextItemWidth(120);
    if (ImGui::InputText("host", host, sizeof(host), ImGuiInputTextFlags_EnterReturnsTrue)) {
        map.feedbackHost = host;
        ioChanged = true;
    }
    ImGui::SameLine();
    int fport = map.feedbackPort;
    ImGui::SetNextItemWidth(70);
    if (ImGui::InputInt("fb port", &fport, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue)) {
        map.feedbackPort = static_cast<std::uint16_t>(std::clamp(fport, 0, 65535));
        ioChanged = true;
    }
    ImGui::SameLine();
    if (status.feedbackOpen) {
        ImGui::TextDisabled("sent %llu", static_cast<unsigned long long>(status.feedbackSent));
    } else if (!status.feedbackError.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), "%s", status.feedbackError.c_str());
    }
    int tempo = engine.tempoSource() == app::TempoSource::MidiClock ? 1 : 0;
    static const char* tempoNames[] = {"analysis", "MIDI clock"};
    ImGui::SetNextItemWidth(110);
    if (ImGui::Combo("tempo source", &tempo, tempoNames, 2)) {
        engine.setTempoSource(tempo == 1 ? app::TempoSource::MidiClock : app::TempoSource::Analysis);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s, %llu clock msgs", engine.midiClockActive() ? "MIDI clock active" : "analyser tempo",
                        static_cast<unsigned long long>(status.clockMessages));
    if (ioChanged) {
        hub.applyIo();
    }
    ImGui::Separator();

    // ---- learn ----
    ImGui::TextUnformatted("Learn: last received");
    if (const auto& m = hub.lastMidi()) {
        ImGui::Text("MIDI %s ch %d #%d = %d (%s)", control::midiKindName(m->kind), m->channel + 1, m->data1, m->data2,
                    m->source.c_str());
    } else {
        ImGui::TextDisabled("MIDI: nothing yet");
    }
    if (const auto& o = hub.lastOsc()) {
        ImGui::Text("OSC %s (%zu args)%s", o->address.c_str(), o->args.size(),
                    o->hasNumber(0) ? fmt::format(" = {:.3f}", static_cast<double>(o->number(0))).c_str() : "");
    } else {
        ImGui::TextDisabled("OSC: nothing yet");
    }
    std::vector<const char*> targets;
    targets.push_back("(signal only)");
    for (const auto* p : engine.params().ordered()) {
        if (p->flags().modulatable) {
            targets.push_back(p->path().c_str());
        }
    }
    learnTarget_ = std::clamp(learnTarget_, 0, static_cast<int>(targets.size()) - 1);
    ImGui::SetNextItemWidth(120);
    ImGui::InputText("signal", learnSignal_, sizeof(learnSignal_));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200);
    ImGui::Combo("##learntarget", &learnTarget_, targets.data(), static_cast<int>(targets.size()));
    ImGui::SameLine();
    ImGui::Checkbox("event", &learnAsEvent_);
    const std::string parameter = learnTarget_ > 0 ? targets[static_cast<std::size_t>(learnTarget_)] : "";
    if (ImGui::Button("Bind last MIDI")) {
        if (!hub.bindLastMidi(learnSignal_, parameter, learnAsEvent_)) {
            status_ = "no MIDI message received yet";
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Bind last OSC")) {
        if (!hub.bindLastOsc(learnSignal_, parameter, learnAsEvent_)) {
            status_ = "no OSC message received yet";
        }
    }
    ImGui::Separator();

    // ---- bindings ----
    ImGui::Text("Bindings: %zu MIDI, %zu OSC (applied %llu, unmatched %llu)", map.midi.size(), map.osc.size(),
                static_cast<unsigned long long>(status.applied), static_cast<unsigned long long>(status.unmatched));
    static const char* bindKinds[] = {"cc", "note", "noteEvent", "pitchBend", "pressure", "program"};
    auto targetEditor = [&](control::BindingTarget& t) {
        char sig[64];
        std::snprintf(sig, sizeof(sig), "%s", t.signal.c_str());
        ImGui::SetNextItemWidth(90);
        if (ImGui::InputText("signal", sig, sizeof(sig))) {
            t.signal = sig;
        }
        ImGui::SameLine();
        int current = 0;
        for (int i = 1; i < static_cast<int>(targets.size()); ++i) {
            if (t.parameter == targets[static_cast<std::size_t>(i)]) {
                current = i;
            }
        }
        ImGui::SetNextItemWidth(170);
        if (ImGui::Combo("param", &current, targets.data(), static_cast<int>(targets.size()))) {
            t.parameter = current > 0 ? targets[static_cast<std::size_t>(current)] : "";
        }
        if (!t.parameter.empty()) {
            ImGui::SameLine();
            float range[2] = {t.min, t.max};
            ImGui::SetNextItemWidth(110);
            if (ImGui::DragFloat2("range", range, 0.01f)) {
                t.min = range[0];
                t.max = range[1];
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(40);
            ImGui::InputInt("comp", &t.component, 0, 0);
            t.component = std::clamp(t.component, -1, 3);
        }
    };
    int removeMidi = -1;
    for (std::size_t i = 0; i < map.midi.size(); ++i) {
        auto& b = map.midi[i];
        ImGui::PushID(static_cast<int>(i));
        int kind = static_cast<int>(b.kind);
        ImGui::SetNextItemWidth(90);
        if (ImGui::Combo("##kind", &kind, bindKinds, 6)) {
            b.kind = static_cast<control::MidiBindKind>(kind);
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(40);
        ImGui::InputInt("ch", &b.channel, 0, 0);
        b.channel = std::clamp(b.channel, -1, 15);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(40);
        ImGui::InputInt("#", &b.number, 0, 0);
        b.number = std::clamp(b.number, -1, 127);
        if (b.kind == control::MidiBindKind::Note) {
            ImGui::SameLine();
            ImGui::Checkbox("toggle", &b.toggle);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) {
            removeMidi = static_cast<int>(i);
        }
        ImGui::Indent();
        targetEditor(b.target);
        ImGui::Unindent();
        ImGui::PopID();
    }
    if (removeMidi >= 0) {
        map.midi.erase(map.midi.begin() + removeMidi);
    }
    int removeOsc = -1;
    for (std::size_t i = 0; i < map.osc.size(); ++i) {
        auto& b = map.osc[i];
        ImGui::PushID(1000 + static_cast<int>(i));
        char addr[128];
        std::snprintf(addr, sizeof(addr), "%s", b.address.c_str());
        ImGui::SetNextItemWidth(160);
        if (ImGui::InputText("OSC", addr, sizeof(addr))) {
            b.address = addr;
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(40);
        ImGui::InputInt("arg", &b.argIndex, 0, 0);
        b.argIndex = std::clamp(b.argIndex, 0, 15);
        ImGui::SameLine();
        ImGui::Checkbox("event", &b.event);
        ImGui::SameLine();
        float in[2] = {b.inMin, b.inMax};
        ImGui::SetNextItemWidth(110);
        if (ImGui::DragFloat2("in", in, 0.5f)) {
            b.inMin = in[0];
            b.inMax = in[1];
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) {
            removeOsc = static_cast<int>(i);
        }
        ImGui::Indent();
        targetEditor(b.target);
        ImGui::Unindent();
        ImGui::PopID();
    }
    if (removeOsc >= 0) {
        map.osc.erase(map.osc.begin() + removeOsc);
    }
    ImGui::TextDisabled("direct OSC: %s/param/<path> f, /signal/<ch> f, /pulse/<ch>, /preset/recall s, /transport/play",
                        map.oscPrefix.c_str());
}


void ControlPanel::drawOutputsTab(app::Engine& /*engine*/) {
    if (outputs == nullptr) {
        ImGui::TextDisabled("outputs unavailable");
        return;
    }
    const auto displays = platform::Window::displays();
    std::vector<std::string> displayLabels;
    std::vector<const char*> displayNames;
    for (const auto& d : displays) {
        displayLabels.push_back(fmt::format("{}: {} ({}x{} @ {:.0f} Hz{})", d.index, d.name, d.width, d.height,
                                            static_cast<double>(d.refreshRate), d.primary ? ", primary" : ""));
    }
    for (const auto& l : displayLabels) {
        displayNames.push_back(l.c_str());
    }
    newOutputDisplay_ = std::clamp(newOutputDisplay_, 0, std::max(0, static_cast<int>(displayNames.size()) - 1));
    ImGui::SetNextItemWidth(260);
    ImGui::Combo("##display", &newOutputDisplay_, displayNames.data(), static_cast<int>(displayNames.size()));
    ImGui::SameLine();
    ImGui::Checkbox("fullscreen", &newOutputFullscreen_);
    ImGui::SameLine();
    if (ImGui::Button("Add output") && !displays.empty()) {
        app::OutputDesc desc;
        desc.name = "output" + std::to_string(outputs->outputs().size() + 1);
        desc.display = displays[static_cast<std::size_t>(newOutputDisplay_)].index;
        desc.fullscreen = newOutputFullscreen_;
        if (auto r = outputs->add(desc); !r) {
            status_ = r.error().message;
        } else if (onOutputsChanged) {
            onOutputsChanged();
        }
    }
    ImGui::Separator();
    std::string removeName;
    bool changed = false;
    for (auto& out : outputs->outputs()) {
        auto& d = out->desc;
        ImGui::PushID(d.name.c_str());
        const bool open = ImGui::TreeNodeEx(d.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen, "%s  display %d%s  %s%s", d.name.c_str(),
                                            d.display, d.fullscreen ? " fullscreen" : "", out->open() ? "open" : "closed",
                                            out->lastError.empty() ? "" : "  !");
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 24);
        if (ImGui::SmallButton("x")) {
            removeName = d.name;
        }
        if (open) {
            if (!out->lastError.empty()) {
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), "%s", out->lastError.c_str());
            }
            changed |= ImGui::Checkbox("enabled", &d.enabled);
            ImGui::SameLine();
            changed |= ImGui::Checkbox("fullscreen", &d.fullscreen);
            ImGui::SameLine();
            changed |= ImGui::Checkbox("borderless", &d.borderless);
            ImGui::SameLine();
            changed |= ImGui::Checkbox("on top", &d.alwaysOnTop);
            ImGui::SetNextItemWidth(60);
            changed |= ImGui::InputInt("display", &d.display, 0, 0);
            ImGui::SameLine();
            int size[2] = {static_cast<int>(d.width), static_cast<int>(d.height)};
            ImGui::SetNextItemWidth(140);
            if (ImGui::InputInt2("size", size)) {
                d.width = static_cast<std::uint32_t>(std::clamp(size[0], 16, 16384));
                d.height = static_cast<std::uint32_t>(std::clamp(size[1], 16, 16384));
                changed = true;
            }
            // Mapping: crop, warp corners, blend, colour. Edits apply live (no reopen needed).
            auto& m = d.mapping;
            float crop[4] = {m.crop.x, m.crop.y, m.crop.w, m.crop.h};
            if (ImGui::DragFloat4("crop x y w h", crop, 0.002f, 0.0f, 1.0f)) {
                m.crop = {crop[0], crop[1], std::max(0.001f, crop[2]), std::max(0.001f, crop[3])};
            }
            static const char* cornerNames[] = {"top-left", "top-right", "bottom-right", "bottom-left"};
            for (int c = 0; c < 4; ++c) {
                float xy[2] = {m.corners[static_cast<std::size_t>(c)].x, m.corners[static_cast<std::size_t>(c)].y};
                ImGui::SetNextItemWidth(160);
                if (ImGui::DragFloat2(cornerNames[c], xy, 0.002f, -0.5f, 1.5f)) {
                    m.corners[static_cast<std::size_t>(c)] = {xy[0], xy[1]};
                }
            }
            float blend[4] = {m.blend.left, m.blend.right, m.blend.top, m.blend.bottom};
            if (ImGui::DragFloat4("blend l r t b", blend, 0.002f, 0.0f, 0.5f)) {
                m.blend = {blend[0], blend[1], blend[2], blend[3]};
            }
            ImGui::SetNextItemWidth(90);
            ImGui::DragFloat("blend gamma", &m.blendGamma, 0.01f, 0.1f, 5.0f);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(90);
            ImGui::DragFloat("brightness", &m.brightness, 0.01f, 0.0f, 4.0f);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(90);
            ImGui::DragFloat("gamma", &m.gamma, 0.01f, 0.2f, 4.0f);
            ImGui::Checkbox("flip X", &m.flipX);
            ImGui::SameLine();
            ImGui::Checkbox("flip Y", &m.flipY);
            ImGui::SameLine();
            if (ImGui::SmallButton("reset mapping")) {
                m = rendering::OutputMapping::identity();
            }
            ImGui::TextDisabled("%llu frames presented", static_cast<unsigned long long>(out->framesPresented));
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if (!removeName.empty()) {
        outputs->remove(removeName);
        changed = true;
    }
    if (changed && onOutputsChanged) {
        onOutputsChanged();
    }
    ImGui::Separator();
    ImGui::TextUnformatted("Share");
    static const char* shareKinds[] = {"off", "syphon", "ndi"};
    ImGui::SetNextItemWidth(90);
    ImGui::Combo("##sharekind", &shareKind_, shareKinds, 3);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140);
    ImGui::InputText("name", shareName_, sizeof(shareName_));
    ImGui::SameLine();
    if (ImGui::Button("Apply") && onShare) {
        onShare(shareKinds[shareKind_], shareName_);
    }
    if (!shareStatus.empty()) {
        ImGui::TextWrapped("%s", shareStatus.c_str());
    }
}

} // namespace avgen::ui
