#include "ui/world_panel.hpp"

#include "params/preset.hpp"

#include <fmt/format.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace avgen::ui {

std::string WorldSelection::parameterPrefix() const {
    switch (kind) {
    case Kind::Node: return "nodes/" + name + "/";
    case Kind::Procedural: return "procedural/" + name + "/";
    case Kind::Field: return "field/" + name + "/";
    case Kind::Spline: return "spline/" + name + "/";
    case Kind::Sdf: return "sdf/" + name + "/";
    case Kind::Particles: return "particles/" + name + "/";
    case Kind::Material: return "material/" + name + "/";
    case Kind::Environment: return "scene/";
    case Kind::Camera: return "camera/";
    case Kind::None: break;
    }
    return {};
}

std::vector<Influence> influencesOf(app::Engine& engine, const std::string& path) {
    std::vector<Influence> out;
    for (const params::ModRoute& r : engine.modulator().routes()) {
        if (r.target != path || !r.enabled) {
            continue;
        }
        Influence i;
        i.kind = Influence::Kind::Route;
        i.source = r.source;
        i.detail = "amount " + std::to_string(r.amount);
        i.value = r.lastOutput;
        out.push_back(std::move(i));
    }
    for (const params::Track& t : engine.timeline().tracks()) {
        if (t.target != path || !t.enabled) {
            continue;
        }
        Influence i;
        i.kind = Influence::Kind::Timeline;
        i.source = "timeline";
        i.detail = std::to_string(t.keys.size()) + " keys";
        out.push_back(std::move(i));
    }
    for (const params::Cue& c : engine.timeline().cues()) {
        const params::Preset* preset = c.preset.empty() ? nullptr : engine.presets().find(c.preset);
        if (preset != nullptr && preset->values.count(path) != 0) {
            Influence i;
            i.kind = Influence::Kind::Cue;
            i.source = c.name.empty() ? c.preset : c.name;
            i.detail = "preset '" + c.preset + "' at " + std::to_string(c.time);
            out.push_back(std::move(i));
        }
    }
    for (const app::SceneState& s : engine.states().states) {
        const params::Preset* preset = s.preset.empty() ? nullptr : engine.presets().find(s.preset);
        if (preset != nullptr && preset->values.count(path) != 0) {
            Influence i;
            i.kind = Influence::Kind::State;
            i.source = s.name;
            i.detail = "preset '" + s.preset + "'";
            out.push_back(std::move(i));
        }
    }
    for (const app::WorldMacro& m : engine.worldMacros()) {
        for (const app::WorldMacroTarget& t : m.targets) {
            if (t.path == path) {
                Influence i;
                i.kind = Influence::Kind::Macro;
                i.source = m.label.empty() ? m.name : m.label;
                i.detail = "range " + std::to_string(t.min) + ".." + std::to_string(t.max);
                out.push_back(std::move(i));
            }
        }
    }
    return out;
}

void WorldPanel::drawLayerSelector() {
    int current = static_cast<int>(layer);
    const char* items[] = {"Beginner", "Intermediate", "Advanced"};
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::Combo("Layer", &current, items, 3)) {
        layer = static_cast<AuthoringLayer>(std::clamp(current, 0, 2));
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Beginner: world macros, atmosphere, camera.\n"
                          "Intermediate: + generators, fields, materials, deformers.\n"
                          "Advanced: every parameter.");
    }
}

void WorldPanel::drawOverview(app::Engine& engine) {
    const scene::Scene& s = engine.scene();
    const auto select = [&](WorldSelection::Kind kind, const std::string& name, const char* label, std::size_t extra) {
        const bool selected = selection.kind == kind && selection.name == name;
        ImGui::PushID(label);
        if (ImGui::Selectable(label, selected)) {
            selection.kind = kind;
            selection.name = name;
        }
        if (extra > 0) {
            ImGui::SameLine();
            ImGui::TextDisabled("%zu", extra);
        }
        ImGui::PopID();
    };
    if (ImGui::TreeNodeEx("Architecture", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (const scene::ProceduralGeometry& pg : s.procedurals) {
            select(WorldSelection::Kind::Procedural, pg.name, pg.name.c_str(), pg.instances.size());
        }
        if (s.procedurals.empty()) {
            ImGui::TextDisabled("no procedural objects");
        }
        ImGui::TreePop();
    }
    if (ImGui::TreeNodeEx("Fields", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (const spatial::FieldSpec& f : s.fields.fields) {
            const std::string label = f.name + "  (" + spatial::fieldKindName(f.kind) + ")";
            select(WorldSelection::Kind::Field, f.name, label.c_str(), 0);
        }
        if (s.fields.fields.empty()) {
            ImGui::TextDisabled("no fields");
        }
        ImGui::TreePop();
    }
    if (!s.splines.splines.empty() && ImGui::TreeNode("Splines")) {
        for (const spatial::Spline& sp : s.splines.splines) {
            select(WorldSelection::Kind::Spline, sp.name, sp.name.c_str(), 0);
        }
        ImGui::TreePop();
    }
    if (!s.sdfs.empty() && ImGui::TreeNode("SDF")) {
        for (const scene::SdfObject& so : s.sdfs) {
            select(WorldSelection::Kind::Sdf, so.name, so.name.c_str(), static_cast<std::size_t>(so.tree.nodeCount()));
        }
        ImGui::TreePop();
    }
    if (!s.particles.empty() && ImGui::TreeNode("Particles")) {
        for (const scene::ParticleSystem& ps : s.particles) {
            select(WorldSelection::Kind::Particles, ps.name, ps.name.c_str(), ps.capacity);
        }
        ImGui::TreePop();
    }
    if (!s.materialPrograms.empty() && ImGui::TreeNode("Materials")) {
        for (const scene::MaterialProgram& mp : s.materialPrograms) {
            select(WorldSelection::Kind::Material, mp.name, mp.name.c_str(), mp.ops.size());
        }
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Atmosphere")) {
        select(WorldSelection::Kind::Environment, "", "environment", 0);
        ImGui::TextDisabled("fog %.3f  volume %.3f", static_cast<double>(s.environment.fogDensity),
                            static_cast<double>(s.environment.volumeDensity));
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Camera & lighting")) {
        select(WorldSelection::Kind::Camera, "", "camera", 0);
        ImGui::TextDisabled("%zu light(s)", s.lights.size());
        ImGui::TreePop();
    }
}

void WorldPanel::drawInspector(app::Engine& engine) {
    if (selection.kind == WorldSelection::Kind::None) {
        ImGui::TextDisabled("Select an object in the World overview.");
        return;
    }
    const std::string prefix = selection.parameterPrefix();
    ImGui::Text("%s", selection.name.empty() ? prefix.c_str() : selection.name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("(%s)", prefix.c_str());
    if (selection.kind == WorldSelection::Kind::Procedural) {
        for (const scene::ProceduralGeometry& pg : engine.scene().procedurals) {
            if (pg.name != selection.name) {
                continue;
            }
            ImGui::Text("%zu instances, %d deformer(s), %zu op(s), %zu effector(s)", pg.instances.size(),
                        static_cast<int>(pg.deformers.size()), pg.pointOps.size(), pg.effectors.size());
            ImGui::Text("bounds (%.1f %.1f %.1f) .. (%.1f %.1f %.1f)", static_cast<double>(pg.boundsMin.x),
                        static_cast<double>(pg.boundsMin.y), static_cast<double>(pg.boundsMin.z),
                        static_cast<double>(pg.boundsMax.x), static_cast<double>(pg.boundsMax.y),
                        static_cast<double>(pg.boundsMax.z));
            if (!pg.effectors.empty() && ImGui::TreeNodeEx("Effectors", ImGuiTreeNodeFlags_DefaultOpen)) {
                for (const spatial::Effector& e : pg.effectors) {
                    ImGui::BulletText("%s -> %s (%s, strength %.2f)%s", e.field.c_str(), spatial::effectorOpName(e.op),
                                      spatial::effectorBlendName(e.blend), static_cast<double>(e.strength),
                                      e.enabled ? "" : " [off]");
                }
                ImGui::TreePop();
            }
            if (!pg.cloud.attributes.buffers().empty() && ImGui::TreeNode("Attributes")) {
                for (const spatial::AttributeBuffer& b : pg.cloud.attributes.buffers()) {
                    const spatial::AttributeStats st = spatial::attributeStats(b);
                    ImGui::BulletText("%s (%s): min %.3f max %.3f mean %.3f", b.name.c_str(),
                                      spatial::attributeTypeName(b.type), static_cast<double>(st.min.x),
                                      static_cast<double>(st.max.x), static_cast<double>(st.mean.x));
                }
                ImGui::TreePop();
            }
            break;
        }
    }
    ImGui::Separator();
    ImGui::TextDisabled("Why is this moving?");
    bool any = false;
    for (params::IParameter* param : engine.params().ordered()) {
        if (!detail::pathStartsWith(param->path(), prefix)) {
            continue;
        }
        const auto influences = influencesOf(engine, param->path());
        if (influences.empty()) {
            continue;
        }
        any = true;
        const std::string rel = param->path().substr(prefix.size());
        if (ImGui::TreeNodeEx(rel.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            for (const Influence& i : influences) {
                const char* kind = "route";
                switch (i.kind) {
                case Influence::Kind::Route: kind = "route"; break;
                case Influence::Kind::Timeline: kind = "timeline"; break;
                case Influence::Kind::Cue: kind = "cue"; break;
                case Influence::Kind::State: kind = "state"; break;
                case Influence::Kind::Macro: kind = "macro"; break;
                }
                // Build the trailing value first: a temporary std::string's c_str() must not
                // outlive the full expression it was created in.
                std::string value;
                if (i.kind == Influence::Kind::Route) {
                    value = fmt::format("= {:.3f}", i.value);
                }
                ImGui::BulletText("%s: %s (%s) %s", kind, i.source.c_str(), i.detail.c_str(), value.c_str());
            }
            ImGui::Text("base %.3f  final %.3f", static_cast<double>(param->baseComponent(0)),
                        static_cast<double>(param->finalComponent(0)));
            ImGui::TreePop();
        }
    }
    if (!any) {
        ImGui::TextDisabled("nothing modulates this object; its parameters are static.");
    }
}

void WorldPanel::drawStates(app::Engine& engine) {
    app::StateMachine& machine = engine.states();
    ImGui::Text("current: %s", machine.current().empty() ? "(none)" : machine.current().c_str());
    if (machine.transitioning()) {
        ImGui::SameLine();
        ImGui::Text("-> %s", machine.pending().c_str());
        ImGui::ProgressBar(machine.progress(), ImVec2(-1.0f, 0.0f));
    }
    for (std::size_t i = 0; i < machine.states.size(); ++i) {
        app::SceneState& s = machine.states[i];
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::Button("Go")) {
            engine.goToState(s.name);
        }
        ImGui::SameLine();
        if (ImGui::Button("Snap")) {
            engine.goToState(s.name, true);
        }
        ImGui::SameLine();
        const bool open = ImGui::TreeNodeEx(s.name.c_str(), machine.current() == s.name ? ImGuiTreeNodeFlags_DefaultOpen : 0);
        if (open) {
            ImGui::Text("preset: %s", s.preset.c_str());
            float seconds = static_cast<float>(s.transition.seconds);
            if (ImGui::SliderFloat("seconds", &seconds, 0.0f, 30.0f)) {
                s.transition.seconds = static_cast<double>(seconds);
            }
            int easing = static_cast<int>(s.transition.easing);
            const char* easings[] = {"linear", "smooth", "easeIn", "easeOut", "easeInOut", "bezier"};
            if (ImGui::Combo("easing", &easing, easings, 6)) {
                s.transition.easing = static_cast<app::TransitionEasing>(easing);
            }
            int quantize = static_cast<int>(s.transition.quantize);
            const char* quantizes[] = {"none", "beat", "bar"};
            if (ImGui::Combo("quantize", &quantize, quantizes, 3)) {
                s.transition.quantize = static_cast<app::TransitionQuantize>(quantize);
            }
            for (std::size_t t = 0; t < s.triggers.size(); ++t) {
                app::StateTrigger& trig = s.triggers[t];
                ImGui::PushID(static_cast<int>(t));
                ImGui::Checkbox("##on", &trig.enabled);
                ImGui::SameLine();
                ImGui::TextUnformatted(app::triggerKindName(trig.kind));
                if (!trig.signal.empty()) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s >= %.2f", trig.signal.c_str(), static_cast<double>(trig.threshold));
                }
                if (trig.kind == app::TriggerKind::Beat || trig.kind == app::TriggerKind::Bar) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("every %d", trig.every);
                }
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    ImGui::Separator();
    ImGui::InputText("name", stateName_, sizeof(stateName_));
    const auto& presets = engine.presets().presets();
    std::vector<const char*> names;
    names.reserve(presets.size());
    for (const auto& p : presets) {
        names.push_back(p.name.c_str());
    }
    if (!names.empty()) {
        statePreset_ = std::clamp(statePreset_, 0, static_cast<int>(names.size()) - 1);
        ImGui::Combo("preset", &statePreset_, names.data(), static_cast<int>(names.size()));
    }
    ImGui::SliderFloat("transition", &stateSeconds_, 0.0f, 30.0f);
    if (ImGui::Button("Add state") && !names.empty() && stateName_[0] != '\0') {
        app::SceneState s;
        s.name = stateName_;
        s.preset = names[static_cast<std::size_t>(statePreset_)];
        s.transition.seconds = static_cast<double>(stateSeconds_);
        machine.states.push_back(std::move(s));
    }
    ImGui::SameLine();
    if (ImGui::Button("Capture preset + state") && stateName_[0] != '\0') {
        engine.storePreset(stateName_);
        app::SceneState s;
        s.name = stateName_;
        s.preset = stateName_;
        s.transition.seconds = static_cast<double>(stateSeconds_);
        machine.states.push_back(std::move(s));
    }
}

void WorldPanel::drawMacros(app::Engine& engine) {
    auto& macros = engine.worldMacros();
    for (std::size_t i = 0; i < macros.size(); ++i) {
        app::WorldMacro& m = macros[i];
        ImGui::PushID(static_cast<int>(i));
        params::IParameter* knob = engine.params().find("macros/" + m.name);
        float value = knob != nullptr ? knob->baseComponent(0) : m.defaultValue;
        if (ImGui::SliderFloat(m.label.empty() ? m.name.c_str() : m.label.c_str(), &value, 0.0f, 1.0f) && knob != nullptr) {
            knob->setBaseComponent(0, value);
        }
        if (ImGui::TreeNode("targets")) {
            for (const app::WorldMacroTarget& t : m.targets) {
                ImGui::BulletText("%s  %.2f .. %.2f", t.path.c_str(), static_cast<double>(t.min),
                                  static_cast<double>(t.max));
            }
            ImGui::InputText("path", macroTargetPath_, sizeof(macroTargetPath_));
            ImGui::SliderFloat("min", &macroTargetMin_, -20.0f, 20.0f);
            ImGui::SliderFloat("max", &macroTargetMax_, -20.0f, 20.0f);
            if (ImGui::Button("Add target") && macroTargetPath_[0] != '\0') {
                app::WorldMacroTarget t;
                t.path = macroTargetPath_;
                t.min = macroTargetMin_;
                t.max = macroTargetMax_;
                app::WorldMacro updated = m;
                updated.targets.push_back(std::move(t));
                engine.setWorldMacro(updated);
            }
            ImGui::TreePop();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove")) {
            const std::string name = m.name;
            engine.removeWorldMacro(name);
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }
    ImGui::Separator();
    ImGui::InputText("macro name", macroName_, sizeof(macroName_));
    if (ImGui::Button("Add macro") && macroName_[0] != '\0') {
        app::WorldMacro m;
        m.name = macroName_;
        m.label = macroName_;
        engine.setWorldMacro(std::move(m));
    }
}

void WorldPanel::drawDirector(app::Engine& engine) {
    const app::WorldDirector& director = engine.director();
    if (director.mappings.empty()) {
        ImGui::TextDisabled("This world has no director.");
        ImGui::TextWrapped("A director maps artistic words onto the parameters they should move. "
                           "Load one from the Assets window, or add world macros by hand in the "
                           "Macros tab.");
    } else {
        ImGui::Text("%s", director.name.c_str());
        ImGui::TextDisabled("%zu knob(s); each one is an ordinary macro", director.mappings.size());
        ImGui::Separator();
        for (const app::DirectorMapping& mapping : director.mappings) {
            const char* name = app::directorKnobName(mapping.knob);
            params::IParameter* knob = engine.params().find(std::string("macros/") + name);
            if (knob == nullptr) {
                continue;
            }
            ImGui::PushID(name);
            float value = knob->baseComponent(0);
            if (ImGui::SliderFloat(name, &value, 0.0f, 1.0f)) {
                knob->setBaseComponent(0, value);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", app::directorKnobDescription(mapping.knob));
            }
            if (ImGui::TreeNode("targets")) {
                for (const app::WorldMacroTarget& t : mapping.targets) {
                    ImGui::BulletText("%s  %.2f .. %.2f", t.path.c_str(), static_cast<double>(t.min),
                                      static_cast<double>(t.max));
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    }

    ImGui::Separator();
    ImGui::TextDisabled("Looks");
    const auto& looks = engine.looks();
    if (looks.empty()) {
        ImGui::TextDisabled("no looks loaded");
    } else {
        std::vector<const char*> names;
        names.reserve(looks.size());
        for (const app::LookPreset& look : looks) {
            names.push_back(look.name.c_str());
        }
        selectedLook_ = std::clamp(selectedLook_, 0, static_cast<int>(names.size()) - 1);
        ImGui::SetNextItemWidth(180.0f);
        ImGui::Combo("look", &selectedLook_, names.data(), static_cast<int>(names.size()));
        const app::LookPreset& look = looks[static_cast<std::size_t>(selectedLook_)];
        if (!look.description.empty()) {
            ImGui::TextWrapped("%s", look.description.c_str());
        }
        if (ImGui::Button("Apply look")) {
            const app::LookApplyResult result = engine.applyLookByName(look.name);
            lastLookResult_ = fmt::format("{}: {} applied, {} not in this world", look.name, result.applied,
                                          result.missing);
        }
        if (!lastLookResult_.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", lastLookResult_.c_str());
        }
    }
    ImGui::InputText("name", lookName_, sizeof(lookName_));
    ImGui::SameLine();
    if (ImGui::Button("Capture look") && lookName_[0] != '\0') {
        std::vector<app::LookPreset> updated = engine.looks();
        updated.push_back(app::captureLook(engine.params(), lookName_));
        engine.setLooks(std::move(updated));
        selectedLook_ = static_cast<int>(engine.looks().size()) - 1;
    }
}

void WorldPanel::drawDebugOptions(app::Engine& engine) {
    (void)engine;
    ImGui::Checkbox("Points", &debug.points);
    ImGui::SameLine();
    ImGui::Checkbox("Bounds", &debug.bounds);
    ImGui::SameLine();
    ImGui::Checkbox("Normals", &debug.normals);
    ImGui::Checkbox("Fields", &debug.fields);
    ImGui::SameLine();
    ImGui::Checkbox("Field vectors", &debug.fieldVectors);
    ImGui::SameLine();
    ImGui::Checkbox("Splines", &debug.splines);
    ImGui::Checkbox("Density colour", &debug.density);
    ImGui::SameLine();
    ImGui::Checkbox("Instance ids", &debug.instanceIds);
    ImGui::SameLine();
    ImGui::Checkbox("LOD", &debug.lod);
    ImGui::SameLine();
    ImGui::Checkbox("Culling", &debug.culling);
    ImGui::Checkbox("SDF slice", &debug.sdfSlice);
    ImGui::SameLine();
    ImGui::SliderFloat("slice y", &debug.sliceHeight, -20.0f, 20.0f);
    ImGui::SliderInt("field grid", &debug.fieldGrid, 2, 24);
    ImGui::SliderFloat("point size", &debug.pointSize, 1.0f, 12.0f);
    ImGui::Checkbox("Depth test", &debug.depthTest);
}

} // namespace avgen::ui
