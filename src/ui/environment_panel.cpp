// ADR-375, revised by ADR-387. See environment_panel.hpp for why this exists when every control
// in it was already reachable from the Parameters panel, and for where the Tree panel's sections
// went.

#include "ui/environment_panel.hpp"

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

// An integer row. ADR-570 needed one for `scene/volumeShadowSteps`, and a step COUNT drawn as a
// float slider reading "3.47" is a control an artist has to interpret -- the same argument
// `FieldType::Choice` makes in ADR-566, one type down.
bool intSlider(app::Engine& engine, const char* path, const char* label) {
    params::IParameter* p = engine.params().find(path);
    if (p == nullptr) {
        return false;
    }
    int v = static_cast<int>(p->baseComponent(0) + 0.5f);
    rowLabel(label);
    ImGui::PushID(path);
    const bool changed = ImGui::SliderInt("##v", &v, static_cast<int>(p->softMin(0)),
                                          static_cast<int>(p->softMax(0)));
    ImGui::PopID();
    if (changed) {
        p->setBaseComponent(0, static_cast<float>(v));
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

} // namespace

void drawEnvironmentPanel(app::Engine& engine) {
    if (ImGui::CollapsingHeader("Sky and fog", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushID("atmos");
        slider(engine, "scene/volumeDensity", "Fog density", "%.3f");
        slider(engine, "scene/fogHeight", "Fog height", "%.1f m");
        slider(engine, "scene/fogHeightFalloff", "Horizon falloff", "%.2f");
        // ADR-568 (the fog brief's §7). Drawn beside the falloff they shape rather than in an
        // advanced section: a parameter an artist cannot reach is a parameter that does not exist
        // as far as the work is concerned (ADR-421), and these two are what turn one exponential
        // haze into the range §7 asks for -- clear air above the mist, a definite layer with a
        // top, or a thin global haze that never quite clears.
        slider(engine, "scene/fogUpperDensity", "Upper density", "%.2f");
        slider(engine, "scene/fogHeightCurve", "Height curve", "%.2f");
        // ADR-570 (§20/§22). Named for what an artist is buying rather than for the algorithm:
        // what these do is make a bank light from a direction and cast a shaft, and "shadow steps"
        // is the number that costs frame time. Both are drawn because a control that exists and
        // cannot be reached is a control that does not exist (ADR-421) -- and at 0 the first one
        // is the whole feature's off switch, which an artist should be able to find.
        intSlider(engine, "scene/volumeShadowSteps", "Fog shadow steps");
        slider(engine, "scene/volumeShadowStrength", "Fog shadow strength", "%.2f");
        // ADR-573 (§27). "Local lights in fog" is the artist's name for it; `volumeLocalLights` is
        // the engine's. The capability has existed since ADR-053 and twelve shipped scenes use it,
        // every one of them hand-edited, because until now there was no row to move.
        slider(engine, "scene/volumeLocalLights", "Local lights in fog", "%.2f");
        slider(engine, "scene/volumeMaxDistance", "Fog march distance", "%.0f m", true);
        slider(engine, "env/intensity", "Ambient", "%.2f");
        slider(engine, "env/sky/intensity", "Sky intensity", "%.2f");
        ImGui::PopID();
    }

    // ADR-055's field, and ADR-387's §18: ONE authoritative global wind. `scene/windSpeed` and
    // `scene/windDirection` are it. What a given tree does with that air is a response multiplier
    // on the tree, not a second global, and it is edited where the tree is -- see the note at the
    // bottom of this section.
    if (ImGui::CollapsingHeader("Wind", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushID("wind");
        if (!have(engine, "scene/windSpeed")) {
            absent("This scene has no wind field.");
            ImGui::PopID();
            return;
        }
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

        // ADR-375: creating a wind body, not just tuning one. Before that, `windAuthored` could
        // only be set by hand-editing the scene file, so the wind was reachable and not
        // *creatable* -- ADR-360's defect one step earlier in the workflow. Kept here, and kept
        // generic: it offers every group node in the scene and knows the name of none of them.
        if (scene::Composition* comp = engine.composition(); comp != nullptr) {
            ImGui::Separator();
            bool anyCandidate = false;
            for (const auto& node : comp->nodes()) {
                if (node->kind == scene::NodeKind::Group && !node->windAuthored) {
                    anyCandidate = true;
                    break;
                }
            }
            ImGui::BeginDisabled(!anyCandidate);
            if (ImGui::BeginCombo("##addwind", anyCandidate ? "Give a group a wind body..."
                                                            : "Every group already has a wind body")) {
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
            ImGui::EndDisabled();
            ImGui::TextColored(kMuted, "A new body starts at strength 1, not 0 -- somebody who "
                                       "presses this means it to do something.");
        }
        ImGui::TextColored(kMuted, "How hard one object answers this air is a property of that "
                                   "object: select it in the World panel and open its \"wind\" "
                                   "group. There is no second global wind.");
        ImGui::PopID();
    }
}

} // namespace avgen::ui
