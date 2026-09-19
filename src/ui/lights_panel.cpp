// ADR-375's pattern applied to lights. See lights_panel.hpp for the boundary and for what is new
// here rather than merely re-grouped.

#include "ui/lights_panel.hpp"

#include "app/engine.hpp"
#include "params/parameter.hpp"
#include "scene/composition.hpp"
#include "ui/edit_history.hpp"
#include "ui/lights_panel_logic.hpp"
#include "ui/world_edit.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace avgen::ui {
namespace {

const ImVec4 kMuted{0.62f, 0.66f, 0.72f, 1.0f};
const ImVec4 kOff{0.55f, 0.55f, 0.58f, 1.0f};

// The same row geometry the Environment and Tree panels use, so a person moving between them does
// not have to re-learn where a label ends.
void rowLabel(const char* label) {
    ImGui::TextUnformatted(label);
    ImGui::SameLine(ImGui::CalcTextSize("Shadow strength  ").x + ImGui::GetStyle().ItemSpacing.x * 2.0f);
    ImGui::SetNextItemWidth(-1.0f);
}

// Writes a parameter's BASE, never its final. Final is this frame's modulated value, and a slider
// that writes it snaps back on the next frame -- which is how a panel loses somebody's trust.
bool slider(app::Engine& engine, const std::string& path, const char* label, const char* fmt = "%.3f",
            bool logarithmic = false) {
    params::IParameter* p = engine.params().find(path);
    if (p == nullptr) {
        return false;
    }
    float v = p->baseComponent(0);
    rowLabel(label);
    ImGui::PushID(path.c_str());
    const bool changed = ImGui::SliderFloat("##v", &v, p->softMin(0), p->softMax(0), fmt,
                                            logarithmic ? ImGuiSliderFlags_Logarithmic : 0);
    ImGui::PopID();
    if (changed) {
        p->setBaseComponent(0, v);
    }
    // Modulation is visible rather than mysterious: a light whose intensity is driven by the bass
    // shows where the slider is AND what the music is doing to it.
    const float f = p->finalComponent(0);
    if (std::abs(f - p->baseComponent(0)) > 1e-4f) {
        ImGui::SameLine();
        ImGui::TextColored(kMuted, "= %.3f", static_cast<double>(f));
    }
    return changed;
}

bool checkbox(app::Engine& engine, const std::string& path, const char* label) {
    params::IParameter* p = engine.params().find(path);
    if (p == nullptr) {
        return false;
    }
    bool v = p->baseComponent(0) >= 0.5f;
    ImGui::PushID(path.c_str());
    const bool changed = ImGui::Checkbox(label, &v);
    ImGui::PopID();
    if (changed) {
        p->setBaseComponent(0, v ? 1.0f : 0.0f);
    }
    return changed;
}

bool colorRow(app::Engine& engine, const std::string& path, const char* label) {
    params::IParameter* p = engine.params().find(path);
    if (p == nullptr) {
        return false;
    }
    float rgb[3] = {p->baseComponent(0), p->baseComponent(1), p->baseComponent(2)};
    rowLabel(label);
    ImGui::PushID(path.c_str());
    const bool changed = ImGui::ColorEdit3("##c", rgb, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
    ImGui::PopID();
    if (changed) {
        for (std::size_t i = 0; i < 3; ++i) {
            p->setBaseComponent(i, rgb[i]);
        }
    }
    return changed;
}

bool vec3Row(app::Engine& engine, const std::string& path, const char* label, const char* fmt = "%.2f") {
    params::IParameter* p = engine.params().find(path);
    if (p == nullptr) {
        return false;
    }
    float v[3] = {p->baseComponent(0), p->baseComponent(1), p->baseComponent(2)};
    rowLabel(label);
    ImGui::PushID(path.c_str());
    const bool changed = ImGui::DragFloat3("##v", v, 0.05f, p->hardMin(0), p->hardMax(0), fmt);
    ImGui::PopID();
    if (changed) {
        for (std::size_t i = 0; i < 3; ++i) {
            p->setBaseComponent(i, v[i]);
        }
    }
    return changed;
}

const char* typeLabel(scene::PunctualLight::Type type) {
    using T = scene::PunctualLight::Type;
    switch (type) {
    case T::Directional: return "Directional";
    case T::Point: return "Point";
    case T::Spot: return "Spot";
    case T::Rect: return "Rect";
    case T::Disk: return "Disk";
    case T::Tube: return "Tube";
    case T::Sphere: return "Sphere";
    }
    return "Light";
}

// A glyph per kind, so the list is scannable without reading the type column. Text rather than an
// icon font because this application has none, and a row of identical bullets would be worse than
// letters that differ.
const char* typeGlyph(scene::PunctualLight::Type type) {
    using T = scene::PunctualLight::Type;
    switch (type) {
    case T::Directional: return "==>";
    case T::Point: return " * ";
    case T::Spot: return "\\V/";
    default: return "[ ]";
    }
}

// Intensity's unit is per-kind, and a slider labelled only "Intensity" invites somebody to type a
// directional light's 3 into a spot and conclude the spot is broken.
const char* intensityLabelFor(scene::PunctualLight::Type type) {
    using T = scene::PunctualLight::Type;
    switch (type) {
    case T::Directional: return "Intensity (lux)";
    case T::Point:
    case T::Spot: return "Intensity (cd)";
    default: return "Intensity (nits)";
    }
}

bool isArea(scene::PunctualLight::Type t) {
    using T = scene::PunctualLight::Type;
    return t == T::Rect || t == T::Disk || t == T::Tube || t == T::Sphere;
}

std::vector<std::string> displayNames(const std::vector<scene::Composition::AuthoredLight>& lights) {
    std::vector<std::string> out;
    out.reserve(lights.size());
    for (const auto& a : lights) {
        out.push_back(a.light.name);
    }
    return out;
}

std::vector<std::string> takenIds(const std::vector<scene::Composition::AuthoredLight>& lights) {
    std::vector<std::string> out;
    out.reserve(lights.size());
    for (const auto& a : lights) {
        out.push_back(scene::Composition::authoredLightId(a));
    }
    return out;
}

// Applies a new authored-light list as one undoable command.
//
// Every structural edit in this panel goes through here, which is what makes add, duplicate, delete
// and rename a single undo step each and keeps them from being four different ways of touching the
// same list. `rebind()` afterwards because `setAuthoredLights` re-registers the parameters, which
// invalidates every raw `IParameter*` a route or a timeline track is holding -- the same reason
// ADR-375's wind-body button rebinds.
void commitLights(app::Engine& engine, EditHistory& history, const char* label,
                  std::vector<scene::Composition::AuthoredLight> next, std::string* problem) {
    scene::Composition* comp = engine.composition();
    if (comp == nullptr) {
        return;
    }
    EditCommand command(label);
    command.lights = std::make_unique<LightChange>();
    command.lights->before = comp->authoredLights();
    command.lights->after = std::move(next);
    const EditApply applied = applyEdit(engine, command, true);
    if (!applied.ok()) {
        if (problem != nullptr) {
            *problem = applied.problems.front();
        }
        return;
    }
    if (problem != nullptr) {
        problem->clear();
    }
    history.push(std::move(command));
    engine.rebind();
}

} // namespace

void drawLightsPanel(app::Engine& engine, Selection& selection, EditHistory& history) {
    scene::Composition* comp = engine.composition();
    if (comp == nullptr) {
        ImGui::TextColored(kMuted, "No scene is open.");
        return;
    }

    // Panel-local and not worth a member: the error from the last structural edit, and whether the
    // list is soloed. Solo is deliberately editor state and never reaches the scene -- the brief
    // says so, and a solo saved into a film is a film with one light.
    static std::string problem;
    static std::string soloed;

    const std::vector<scene::Composition::AuthoredLight>& lights = comp->authoredLights();

    // ---- create ---------------------------------------------------------------------------------
    if (ImGui::Button("+ Add Light")) {
        ImGui::OpenPopup("##addlight");
    }
    if (ImGui::BeginPopup("##addlight")) {
        for (const scene::PunctualLight::Type type : creatableLightTypes()) {
            const char* label = type == scene::PunctualLight::Type::Rect ? "Area" : typeLabel(type);
            if (ImGui::MenuItem(label)) {
                const scene::Camera& camera = comp->scene().camera;
                const LightPlacement placement = placeNewLight(camera.position, camera.target, type);
                const std::string name = uniqueLightName(std::string(label) + " Light", displayNames(lights));
                std::vector<scene::Composition::AuthoredLight> next = lights;
                next.push_back(makeLight(type, placement,
                                         name, scene::Composition::uniqueAuthoredLightId(name, takenIds(lights))));
                commitLights(engine, history, "Add light", std::move(next), &problem);
                // Selected on creation, so its properties are already showing and its gizmo is
                // already in the viewport -- the brief's §4, and the difference between "a light
                // was added somewhere" and "here is your light".
                selection.set(SelectionRef{SelectionRef::Kind::Light, name});
            }
        }
        ImGui::EndPopup();
    }
    if (!problem.empty()) {
        ImGui::TextColored(ImVec4(0.95f, 0.5f, 0.45f, 1.0f), "%s", problem.c_str());
    }

    if (lights.empty()) {
        // An absence somebody can read is not the same as an absence (ADR-375).
        ImGui::Spacing();
        ImGui::TextColored(kMuted, "This scene authors no lights.");
        ImGui::TextColored(kMuted, "Add one above, or the renderer will light it with a default key.");
        return;
    }

    // ---- the list -------------------------------------------------------------------------------
    ImGui::Spacing();
    ImGui::TextColored(kMuted, "%zu light%s", lights.size(), lights.size() == 1 ? "" : "s");
    if (!soloed.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.95f, 0.8f, 0.4f, 1.0f), "  SOLO: %s", soloed.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("clear")) {
            soloed.clear();
        }
    }

    // Solo, actually applied. It writes each other light's `enabled` **final**, not its base: the
    // final is this frame's value and is reset from the base at the top of the next one, so solo
    // darkens the scene for exactly as long as it is on and leaves nothing behind. Writing the base
    // would be the same gesture and would save a film with one light in it -- the brief's §19 warns
    // about precisely that, and it is also why letting the panel close is a safe way out of solo.
    //
    // A badge on its own would have been a control that does nothing, which ADR-375 calls worse
    // than no control at all.
    if (!soloed.empty()) {
        for (const scene::Composition::AuthoredLight& a : lights) {
            if (a.light.name == soloed) {
                continue;
            }
            if (params::IParameter* p = engine.params().find("lights/" + scene::Composition::authoredLightId(a) +
                                                             "/enabled");
                p != nullptr) {
                p->setFinalComponent(0, 0.0f);
            }
        }
    }

    // A child so a scene with eighty lights scrolls its list rather than its properties. The brief
    // asks for the panel to stay usable at "dozens/hundreds".
    const float listHeight = std::min(260.0f, std::max(90.0f, static_cast<float>(lights.size()) * 22.0f + 10.0f));
    if (ImGui::BeginChild("##lightlist", ImVec2(0.0f, listHeight), true)) {
        for (const scene::Composition::AuthoredLight& a : lights) {
            const std::string id = scene::Composition::authoredLightId(a);
            // Keyed on the id and not the row index or the display name: ADR-361 was an id conflict
            // from one window drawing the same list twice keyed on a document id alone, and two
            // lights can share a display name only until `setAuthoredLights` refuses it -- but the
            // id is what is guaranteed unique.
            ImGui::PushID(id.c_str());

            const std::string enabledPath = "lights/" + id + "/enabled";
            checkbox(engine, enabledPath, "##en");
            ImGui::SameLine();

            const bool on = a.light.enabled && (soloed.empty() || soloed == a.light.name);
            ImGui::TextColored(on ? ImGui::GetStyleColorVec4(ImGuiCol_Text) : kOff, "%s",
                               typeGlyph(a.light.type));
            ImGui::SameLine();

            const bool chosen = selection.contains(SelectionRef{SelectionRef::Kind::Light, a.light.name});
            if (ImGui::Selectable(a.light.name.c_str(), chosen, ImGuiSelectableFlags_AllowDoubleClick)) {
                // Shift extends, exactly as it does in the viewport: one selection model, one set
                // of modifiers (the viewport spec's §16/§17).
                if (ImGui::GetIO().KeyShift) {
                    selection.toggle(SelectionRef{SelectionRef::Kind::Light, a.light.name});
                } else {
                    selection.set(SelectionRef{SelectionRef::Kind::Light, a.light.name});
                }
            }
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 52.0f);
            ImGui::TextColored(kMuted, "%s", typeLabel(a.light.type));
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    // ---- the selected light ---------------------------------------------------------------------
    const SelectionRef& active = selection.primaryRef();
    if (active.kind != SelectionRef::Kind::Light) {
        ImGui::Spacing();
        ImGui::TextColored(kMuted, "Choose a light to edit it.");
        return;
    }
    const auto it = std::find_if(lights.begin(), lights.end(), [&](const auto& a) {
        return a.light.name == active.name;
    });
    if (it == lights.end()) {
        return;
    }
    const scene::Composition::AuthoredLight& light = *it;
    const std::string id = scene::Composition::authoredLightId(light);
    const std::string base = "lights/" + id + "/";
    const scene::PunctualLight::Type type = light.light.type;

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Rename. The id underneath does not move, so every route and timeline track bound to this
    // light survives -- which is the whole reason the two are separate fields.
    {
        static char buffer[128];
        static std::string editing;
        if (editing != active.name) {
            editing = active.name;
            std::snprintf(buffer, sizeof(buffer), "%s", active.name.c_str());
        }
        rowLabel("Name");
        ImGui::PushID("rename");
        if (ImGui::InputText("##name", buffer, sizeof(buffer), ImGuiInputTextFlags_EnterReturnsTrue)) {
            std::vector<scene::Composition::AuthoredLight> next = lights;
            const auto target = static_cast<std::size_t>(std::distance(lights.begin(), it));
            next[target].light.name = buffer;
            const std::string was = active.name;
            commitLights(engine, history, "Rename light", std::move(next), &problem);
            if (problem.empty()) {
                selection.set(SelectionRef{SelectionRef::Kind::Light, std::string(buffer)});
            } else {
                editing.clear(); // put the old name back in the box
            }
            (void)was;
        }
        ImGui::PopID();
        ImGui::TextColored(kMuted, "  id: %s", id.c_str());
        if (id != light.light.name) {
            ImGui::SameLine();
            ImGui::TextColored(kMuted, "(parameters keep this id through a rename)");
        }
    }

    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Emission", ImGuiTreeNodeFlags_DefaultOpen)) {
        slider(engine, base + "intensity", intensityLabelFor(type), "%.2f", true);
        colorRow(engine, base + "color", "Colour");
        slider(engine, base + "temperature", "Temperature", "%.0f K");
        slider(engine, base + "tint", "Tint", "%.2f");
        ImGui::TextColored(kMuted,
                           "  Colour and temperature multiply: 6500 K is neutral and leaves the "
                           "colour alone.");
    }

    if (ImGui::CollapsingHeader("Placement", ImGuiTreeNodeFlags_DefaultOpen)) {
        vec3Row(engine, base + "position", "Position");
        if (type == scene::PunctualLight::Type::Directional || type == scene::PunctualLight::Type::Spot) {
            slider(engine, base + "azimuth", "Azimuth", "%.1f deg");
            slider(engine, base + "elevation", "Elevation", "%.1f deg");
            ImGui::TextColored(kMuted, "  Where the light comes FROM, in world space.");
        }
        if (!light.node.empty()) {
            ImGui::TextColored(kMuted, "  Rides the node '%s'; position is in its local frame.",
                               light.node.c_str());
        }
        if (type == scene::PunctualLight::Type::Directional) {
            ImGui::TextColored(kMuted, "  A directional light's position does not shade; it is where"
                                       " the gizmo sits.");
        }
    }

    if (type != scene::PunctualLight::Type::Directional || type == scene::PunctualLight::Type::Spot ||
        isArea(type)) {
        if (ImGui::CollapsingHeader("Shape", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (type != scene::PunctualLight::Type::Directional) {
                slider(engine, base + "range", "Range", "%.1f m");
                ImGui::TextColored(kMuted, "  0 is no cutoff: the inverse square is the falloff.");
            }
            if (type == scene::PunctualLight::Type::Spot) {
                slider(engine, base + "innerCone", "Inner cone", "%.1f deg");
                slider(engine, base + "outerCone", "Outer cone", "%.1f deg");
                ImGui::TextColored(kMuted, "  The renderer clamps both at 90 degrees.");
            }
            if (isArea(type)) {
                slider(engine, base + "width", "Width", "%.2f m");
                slider(engine, base + "height", "Height", "%.2f m");
                slider(engine, base + "radius", "Radius", "%.2f m");
                ImGui::TextColored(kMuted,
                                   "  Rect uses width x height; Disk and Sphere use radius; Tube "
                                   "uses both.");
                ImGui::TextColored(kMuted, "  Intensity is nits over that area, so a bigger emitter "
                                           "throws more light.");
            }
        }
    }

    if (ImGui::CollapsingHeader("Shadows")) {
        checkbox(engine, base + "castsShadow", "Cast shadows");
        slider(engine, base + "shadowStrength", "Shadow strength", "%.2f");
        slider(engine, base + "angularSize", "Angular size", "%.2f deg");
        slider(engine, base + "shadowBias", "Shadow bias", "%.4f");
        checkbox(engine, base + "contactShadow", "Contact shadows");
        ImGui::TextColored(kMuted, "  Angular size is the source's apparent diameter and drives the "
                                   "penumbra.");
        // The capability boundary, said where the control is rather than in a document. This is the
        // one the brief's §23 asks for and the one a user would otherwise diagnose from an AOV.
        if (type == scene::PunctualLight::Type::Directional) {
            ImGui::TextColored(kMuted, "  Only one directional light casts a cascaded shadow per "
                                       "frame; a second is lit but unshadowed.");
        }
        if (isArea(type)) {
            ImGui::TextColored(kMuted, "  An area light shadows through a cube map and competes for "
                                       "the same eight shadow views.");
        }
    }

    if (ImGui::CollapsingHeader("Atmosphere")) {
        slider(engine, base + "volumetric", "In-scatter", "%.2f");
        ImGui::TextColored(kMuted, "  How much this light lights the fog. The volume march treats an"
                                   " area light as its centre.");
    }

    // ---- actions --------------------------------------------------------------------------------
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Duplicate")) {
        std::vector<scene::Composition::AuthoredLight> next = lights;
        scene::Composition::AuthoredLight copy = light;
        copy.light.name = duplicateLightName(light.light.name, displayNames(lights));
        // A new id as well as a new name, or the copy would answer to the original's parameters and
        // the two would move together for ever.
        copy.id = scene::Composition::uniqueAuthoredLightId(copy.light.name, takenIds(lights));
        next.push_back(copy);
        const std::string created = copy.light.name;
        commitLights(engine, history, "Duplicate light", std::move(next), &problem);
        selection.set(SelectionRef{SelectionRef::Kind::Light, created});
    }
    ImGui::SameLine();
    if (ImGui::Button(soloed == light.light.name ? "Unsolo" : "Solo")) {
        soloed = soloed == light.light.name ? std::string() : light.light.name;
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete")) {
        std::vector<scene::Composition::AuthoredLight> next;
        for (const auto& a : lights) {
            if (a.light.name != light.light.name) {
                next.push_back(a);
            }
        }
        commitLights(engine, history, "Delete light", std::move(next), &problem);
        selection.clear();
    }

    ImGui::TextColored(kMuted, "Solo is an editor state and is never saved into the scene.");
}

} // namespace avgen::ui
