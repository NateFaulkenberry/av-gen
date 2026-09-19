// ADR-375. See cosmic_panel.hpp for why these exist when every control in them was already
// reachable from the Parameters panel.

#include "ui/cosmic_panel.hpp"

#include "app/engine.hpp"
#include "params/parameter.hpp"
#include "scene/composition.hpp"

#include <imgui.h>

#include <cmath>
#include <string>
#include <vector>

namespace avgen::ui {
namespace {

const ImVec4 kMuted{0.62f, 0.66f, 0.72f, 1.0f};

void rowLabel(const char* label) {
    ImGui::TextUnformatted(label);
    ImGui::SameLine(ImGui::CalcTextSize("Turbulence scale  ").x + ImGui::GetStyle().ItemSpacing.x * 2.0f);
    ImGui::SetNextItemWidth(-1.0f);
}

// A row over a parameter's own soft range, writing its BASE. Base and not final, for the reason
// world_effects_panel.cpp gives: final is this frame's modulated value, and a slider that writes it
// snaps back on the next frame, which is how a panel loses somebody's trust.
bool slider(app::Engine& engine, const char* path, const char* label, const char* fmt = "%.3f",
            bool logarithmic = false) {
    params::IParameter* p = engine.params().find(path);
    if (p == nullptr) {
        return false;
    }
    float v = p->baseComponent(0);
    rowLabel(label);
    ImGui::PushID(path);
    const bool changed =
        ImGui::SliderFloat("##v", &v, p->softMin(0), p->softMax(0), fmt,
                           logarithmic ? ImGuiSliderFlags_Logarithmic : 0);
    ImGui::PopID();
    if (changed) {
        p->setBaseComponent(0, v);
    }
    const float f = p->finalComponent(0);
    if (std::abs(f - p->baseComponent(0)) > 1e-4f) {
        ImGui::SameLine();
        ImGui::TextColored(kMuted, "= %.3f", static_cast<double>(f));
    }
    return changed;
}

bool checkbox(app::Engine& engine, const char* path, const char* label) {
    params::IParameter* p = engine.params().find(path);
    if (p == nullptr) {
        return false;
    }
    bool v = p->baseComponent(0) >= 0.5f;
    ImGui::PushID(path);
    const bool changed = ImGui::Checkbox(label, &v);
    ImGui::PopID();
    if (changed) {
        p->setBaseComponent(0, v ? 1.0f : 0.0f);
    }
    return changed;
}

bool colorRow(app::Engine& engine, const char* path, const char* label) {
    params::IParameter* p = engine.params().find(path);
    if (p == nullptr) {
        return false;
    }
    float rgb[3] = {p->baseComponent(0), p->baseComponent(1), p->baseComponent(2)};
    rowLabel(label);
    ImGui::PushID(path);
    const bool changed = ImGui::ColorEdit3("##c", rgb, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
    ImGui::PopID();
    if (changed) {
        for (std::size_t i = 0; i < 3; ++i) {
            p->setBaseComponent(i, rgb[i]);
        }
    }
    return changed;
}

bool have(app::Engine& engine, const char* path) {
    return engine.params().find(path) != nullptr;
}

// A section that says why it is empty rather than simply not appearing. A panel that silently omits
// a heading tells somebody looking for a control that they are in the wrong place; one that says
// "this scene has no falling leaves" tells them they are in the right place and the scene has not
// asked for it yet, which is a different and more useful message.
void absent(const char* what) {
    ImGui::TextColored(kMuted, "%s", what);
}

// The falling-leaf system, found by the convention its parameters are registered under. A particle
// node named "falling-leaves" registers `particles/falling-leaves/...`; a nested one gets a
// prefixed name, so the search is by suffix rather than by an exact path.
std::string leafPrefix(app::Engine& engine) {
    for (const params::IParameter* p : engine.params().ordered()) {
        const std::string& path = p->path();
        const std::string tail = "/spawnRate";
        if (path.size() <= tail.size() || path.compare(path.size() - tail.size(), tail.size(), tail) != 0) {
            continue;
        }
        if (path.find("leaves") == std::string::npos && path.find("leaf") == std::string::npos) {
            continue;
        }
        return path.substr(0, path.size() - tail.size() + 1);
    }
    return {};
}

// Every node that declares a wind body, by the parameter it registers.
std::vector<std::string> windBodies(app::Engine& engine) {
    std::vector<std::string> out;
    const std::string tail = "/wind/strength";
    for (const params::IParameter* p : engine.params().ordered()) {
        const std::string& path = p->path();
        if (path.size() > tail.size() &&
            path.compare(path.size() - tail.size(), tail.size(), tail) == 0) {
            out.push_back(path.substr(0, path.size() - tail.size() + 1));
        }
    }
    return out;
}

} // namespace

void drawEnvironmentPanel(app::Engine& engine) {
    if (ImGui::CollapsingHeader("Cosmic atmosphere", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushID("atmos");
        slider(engine, "scene/volumeDensity", "Fog density", "%.3f");
        slider(engine, "scene/fogHeight", "Fog height", "%.1f m");
        slider(engine, "scene/fogHeightFalloff", "Horizon falloff", "%.2f");
        slider(engine, "env/intensity", "Cosmic ambient", "%.2f");
        slider(engine, "env/sky/intensity", "Sky intensity", "%.2f");
        if (have(engine, "shader/glowmere-cosmos/nebulaIntensity")) {
            slider(engine, "shader/glowmere-cosmos/nebulaIntensity", "Nebula intensity", "%.2f");
            slider(engine, "shader/glowmere-cosmos/backgroundSaturation", "Background saturation", "%.2f");
        } else {
            absent("This scene has no cosmos background layer, so there is no nebula to scale.");
        }
        ImGui::PopID();
    }

    if (ImGui::CollapsingHeader("Cosmic vortex", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushID("vortex");
        if (!have(engine, "scene/vortex/radius")) {
            absent("No vortex in this scene.");
        } else {
            params::IParameter* radius = engine.params().find("scene/vortex/radius");
            const bool on = radius != nullptr && radius->baseComponent(0) > 0.0f;
            ImGui::TextColored(kMuted, on ? "On. Radius 0 switches it off and costs nothing."
                                          : "Off. Give it a radius to switch it on.");
            slider(engine, "scene/vortex/radius", "Radius", "%.0f m");
            ImGui::BeginDisabled(!on);
            slider(engine, "scene/vortex/funnelDepth", "Funnel depth", "%.0f m");
            slider(engine, "scene/vortex/throat", "Throat", "%.2f");
            slider(engine, "scene/vortex/throatDensity", "Throat density", "%.2f");
            slider(engine, "scene/vortex/thickness", "Wall thickness", "%.0f m");
            ImGui::Separator();
            slider(engine, "scene/vortex/swirl", "Swirl", "%.2f");
            slider(engine, "scene/vortex/rotationSpeed", "Rotation speed", "%.3f rad/s");
            slider(engine, "scene/vortex/turbulence", "Turbulence", "%.2f");
            ImGui::Separator();
            slider(engine, "scene/vortex/density", "Density", "%.4f", true);
            slider(engine, "scene/vortex/emission", "Emission", "%.3f", true);
            slider(engine, "scene/vortex/contrast", "Contrast", "%.2f");
            slider(engine, "scene/vortex/innerVoid", "Inner void", "%.2f");
            slider(engine, "scene/vortex/filaments", "Filaments", "%.2f");
            ImGui::Separator();
            slider(engine, "scene/vortex/breathAmount", "Breath amount", "%.3f");
            ImGui::Separator();
            colorRow(engine, "scene/vortex/colorDeep", "Deep colour");
            colorRow(engine, "scene/vortex/colorMid", "Mid colour");
            colorRow(engine, "scene/vortex/colorAccent", "Accent colour");
            ImGui::EndDisabled();
            // The one number a person needs when this gets expensive, said where they are looking
            // at the thing that costs it (ADR-374: the vortex is the most expensive term here, and
            // its cost is pixel coverage rather than march length).
            ImGui::TextColored(kMuted, "The vortex is the frame's most expensive term. Density and "
                                       "the quality tier's volume resolution are what to turn down.");
        }
        ImGui::PopID();
    }
}

void drawTreePanel(app::Engine& engine) {
    if (ImGui::CollapsingHeader("Wind", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushID("wind");
        checkbox(engine, "scene/wind/enabled", "Enabled");
        params::IParameter* en = engine.params().find("scene/wind/enabled");
        const bool on = en != nullptr && en->baseComponent(0) >= 0.5f;
        ImGui::BeginDisabled(!on);
        slider(engine, "scene/windSpeed", "Speed", "%.2f");
        slider(engine, "scene/windDirection", "Direction", "%.2f rad");
        ImGui::Separator();
        slider(engine, "scene/wind/gustAmount", "Gust amount", "%.2f");
        slider(engine, "scene/wind/gustScale", "Gust scale", "%.0f m");
        slider(engine, "scene/wind/gustSpeed", "Gust speed", "%.1f m/s");
        slider(engine, "scene/wind/gustSharpness", "Gust sharpness", "%.2f");
        ImGui::Separator();
        slider(engine, "scene/wind/turbulence", "Turbulence", "%.2f rad");
        slider(engine, "scene/wind/turbulenceScale", "Turbulence scale", "%.0f m");
        slider(engine, "scene/wind/turbulenceSpeed", "Turbulence speed", "%.1f m/s");
        ImGui::Separator();
        slider(engine, "scene/wind/regionScale", "Region scale", "%.0f m");
        slider(engine, "scene/wind/regionAmount", "Region amount", "%.2f");
        slider(engine, "scene/wind/regionDrift", "Region drift", "%.3f");
        slider(engine, "scene/wind/flutterScale", "Flutter scale", "%.1f m");
        ImGui::EndDisabled();

        // The per-body half. The field above says what the air does; this says what each tree does
        // with it, and they are different questions that used to look like one list.
        const std::vector<std::string> bodies = windBodies(engine);
        if (bodies.empty()) {
            ImGui::Separator();
            absent("No node in this scene declares a wind body, so nothing bends in the field "
                   "above.");
        }
        // ADR-375: creating one, not just tuning one. Until now `windAuthored` could only be set by
        // hand-editing the scene file, so the wind was reachable and not *creatable* -- ADR-360's
        // defect one step earlier in the workflow, and flagged in that ADR's own consequences.
        if (scene::Composition* comp = engine.composition(); comp != nullptr) {
            ImGui::Separator();
            if (ImGui::BeginCombo("##addwind", "Give a group a wind body...")) {
                for (const auto& node : comp->nodes()) {
                    if (node->kind != scene::NodeKind::Group || node->windAuthored) {
                        continue;
                    }
                    if (ImGui::Selectable(node->name.c_str())) {
                        // Structural: the parameters change shape, so the modulator and the
                        // timeline have to re-resolve or every route pointing into this node is
                        // left dangling.
                        if (comp->setNodeWindBody(node->name, true)) {
                            engine.rebind();
                        }
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::TextColored(kMuted, "A new body starts at strength 1, not 0 -- somebody who "
                                       "presses this means it to do something.");
        }
        for (const std::string& prefix : bodies) {
            ImGui::Separator();
            // `nodes/<name>/wind/` -> `<name>`
            const std::string body = prefix.substr(6, prefix.size() - 6 - 6);
            ImGui::TextColored(kMuted, "Body: %s", body.c_str());
            ImGui::PushID(prefix.c_str());
            slider(engine, (prefix + "strength").c_str(), "Strength", "%.2f");
            slider(engine, (prefix + "trunk").c_str(), "Trunk influence", "%.2f");
            slider(engine, (prefix + "branch").c_str(), "Branch influence", "%.2f");
            slider(engine, (prefix + "foliage").c_str(), "Foliage influence", "%.2f");
            slider(engine, (prefix + "flutter").c_str(), "Leaf flutter", "%.2f");
            slider(engine, (prefix + "lag").c_str(), "Response lag", "%.2f s");
            ImGui::PopID();
        }
        ImGui::PopID();
    }

    if (ImGui::CollapsingHeader("Falling leaves", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushID("leaves");
        const std::string p = leafPrefix(engine);
        if (p.empty()) {
            absent("This scene sheds no leaves. A particles node named \"falling-leaves\" with "
                   "shape2d \"leaf\" and a canopySource is what makes one.");
        } else {
            checkbox(engine, (p + "enabled").c_str(), "Enabled");
            slider(engine, (p + "spawnRate").c_str(), "Emission rate", "%.0f /s");
            slider(engine, (p + "lifetime").c_str(), "Lifetime", "%.2f x");
            slider(engine, (p + "size").c_str(), "Size", "%.2f x");
            ImGui::Separator();
            slider(engine, (p + "gravity").c_str(), "Gravity", "%.2f");
            slider(engine, (p + "drag").c_str(), "Drag", "%.2f");
            slider(engine, (p + "windInfluence").c_str(), "Wind influence", "%.2f");
            slider(engine, (p + "turbulence").c_str(), "Turbulence", "%.2f");
            slider(engine, (p + "turbulenceScale").c_str(), "Turbulence scale", "%.3f");
            ImGui::Separator();
            slider(engine, (p + "tumbleRate").c_str(), "Tumble rate", "%.2f rad/s");
            slider(engine, (p + "leafAspect").c_str(), "Leaf aspect", "%.2f");
            slider(engine, (p + "twoSided").c_str(), "Two-sided shading", "%.2f");
            slider(engine, (p + "softness").c_str(), "Soft fade", "%.2f m");
            ImGui::Separator();
            colorRow(engine, (p + "colorStart").c_str(), "Colour, fresh");
            colorRow(engine, (p + "colorEnd").c_str(), "Colour, spent");
            slider(engine, (p + "emissive").c_str(), "Emissive", "%.2f");
        }
        ImGui::PopID();
    }

    if (ImGui::CollapsingHeader("Tree energy, canopy shimmer, tree particles")) {
        absent("Not built. Phases 5, 6 and 7 of the brief; the wind, the leaves and the vortex "
               "came first because they read at a glance and these do not.");
    }
}

} // namespace avgen::ui
