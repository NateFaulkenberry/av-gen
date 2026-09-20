#include "ui/world_panel.hpp"

#include "ui/param_widget.hpp"

#include "ui/style.hpp"
#include "params/preset.hpp"
#include "scene/composition.hpp"
#include "stage/staging.hpp"
#include "ui/world_editor.hpp"

#include <fmt/format.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unordered_map>

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

namespace {

// Everything that writes to exactly this path. `influencesOf` below is this plus what the node
// inherits from above it.
std::vector<Influence> directInfluencesOf(app::Engine& engine, const std::string& path) {
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
        i.routeSource = r.source;
        i.routeTarget = r.target;
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
    // The behaviour layer (ADR-088, ADR-211). Entities write a `MotionOffset` straight onto their
    // node's transform parameters -- `entity.cpp` calls `setFinalComponent` on position, rotation
    // and scale -- so a body with a hover, a sway or a walk cycle is moving for a reason this panel
    // has to be able to name. It could not, and said "nothing modulates this object; its parameters
    // are static" about a character walking across the valley.
    //
    // Matched on the parameter path rather than by asking the entity what it drives: an entity's
    // node is `desc.node` (or its name), and the three parameters it writes are that node's
    // transform. Deriving the paths here keeps the check in one place and means a behaviour that
    // writes nothing this frame is still listed -- the question is "what can move this", not "what
    // moved it in the last sixteen milliseconds".
    if (const scene::Composition* comp = const_cast<app::Engine&>(engine).composition()) {
        for (const auto& entity : comp->entityWorld().entities()) {
            if (entity == nullptr) {
                continue;
            }
            const entity::EntityDesc& desc = entity->desc();
            const std::string node = desc.node.empty() ? desc.name : desc.node;
            const std::string base = "nodes/" + node + "/";
            if (path != base + "position" && path != base + "rotation" && path != base + "scale") {
                continue;
            }
            for (const entity::BehaviorDesc& b : desc.behaviors) {
                Influence i;
                i.kind = Influence::Kind::Entity;
                i.source = desc.name;
                i.detail = b.name.empty() ? b.kind : b.kind + " '" + b.name + "'";
                out.push_back(std::move(i));
            }
            if (desc.behaviors.empty()) {
                // An entity with no behaviours still moves if the action system is driving it
                // (ADR-096 writes the transform directly), so its presence is the answer.
                Influence i;
                i.kind = Influence::Kind::Entity;
                i.source = desc.name;
                i.detail = "entity";
                out.push_back(std::move(i));
            }
        }
    }
    // The staging layer (ADR-210, ADR-241). A scenario writes a parameter's *base* directly --
    // `Staging::writeParameter` -- so a tractor beam's visibility, a particle system's spawn rate or
    // any absolute path a `set` step names is driven by something no other branch here can see. The
    // entity branch above names a staged *body*, because a scenario moves one through
    // `DirectorMotion`; it says nothing about the parameters the scenario writes itself, which is
    // exactly the hole ADR-211 recorded and declined to fill.
    if (const scene::Composition* comp = const_cast<app::Engine&>(engine).composition()) {
        for (const stage::Staging::PathWriter& w : comp->director().writersOf(path)) {
            Influence i;
            i.kind = Influence::Kind::Staging;
            i.source = w.scenario;
            std::string detail = w.role.empty() ? std::string("scenario") : "role '" + w.role + "'";
            // Said plainly, because the difference decides whether the user should expect to see the
            // value move right now.
            detail += w.running ? ", running" : ", not running";
            if (!w.live) {
                detail += ", declared";
            }
            i.detail = std::move(detail);
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

// "nodes/<name>/position" -> {"<name>", "position"}. Empty name for anything else.
struct NodeField {
    std::string node;
    std::string field;
};
[[nodiscard]] NodeField nodeFieldOf(const std::string& path) {
    constexpr std::string_view kPrefix = "nodes/";
    if (!path.starts_with(kPrefix)) {
        return {};
    }
    const std::size_t slash = path.rfind('/');
    if (slash == std::string::npos || slash <= kPrefix.size()) {
        return {};
    }
    return {path.substr(kPrefix.size(), slash - kPrefix.size()), path.substr(slash + 1)};
}

[[nodiscard]] bool isTransformField(const std::string& field) {
    return field == "position" || field == "rotation" || field == "scale";
}

} // namespace

std::vector<Influence> influencesOf(app::Engine& engine, const std::string& path) {
    std::vector<Influence> out = directInfluencesOf(engine, path);

    // ---- what the node inherits ----------------------------------------------------------------
    //
    // A node's transform is not the only thing that moves it. Everything in the composition hangs
    // off a root whose rotation integrates `root/rotationSpeed`, and `addDefaultRoutes` wires that
    // to `audio.mid` on **every** composition that does not already route it -- so with music
    // playing, the whole world turns. A node inside a group inherits its group's transform the same
    // way.
    //
    // Asked "why is this moving" about a spinning terrain, this panel answered "nothing modulates
    // this object; its parameters are static". That was *true* -- nothing modulates
    // `nodes/terrain/rotation` -- and completely useless, which is the second time this panel has
    // given that answer about something visibly in motion. The first time it was an entity walking
    // (see the note in `directInfluencesOf`); this is the same miss one level up, and the fix is
    // the same shape: the question is "what can move this", and an ancestor's transform can.
    //
    // Reported with `via` set rather than merged in silently, because the distinction is what the
    // user needs: a route on this node is edited here, and a route on the root is not.
    const NodeField self = nodeFieldOf(path);
    if (self.node.empty() || !isTransformField(self.field)) {
        return out;
    }

    const auto inherit = [&out](std::vector<Influence> more, const std::string& via) {
        for (Influence& i : more) {
            i.via = via;
            out.push_back(std::move(i));
        }
    };

    if (scene::Composition* comp = engine.composition()) {
        // The ancestor chain, nearest first. Bounded by the node count for the same reason
        // `nodeVisible` is: a hand-edited file can describe a parent cycle.
        const scene::CompositionNode* node = comp->findNode(self.node);
        std::size_t guard = 0;
        while (node != nullptr && !node->parent.empty() && guard++ < 1024) {
            const scene::CompositionNode* parent = comp->findNode(node->parent);
            if (parent == nullptr) {
                break;
            }
            const std::string base = "nodes/" + parent->name + "/";
            const std::string via = "parent '" + parent->name + "'";
            for (const char* field : {"position", "rotation", "scale"}) {
                inherit(directInfluencesOf(engine, base + field), via);
            }
            node = parent;
        }
    }

    // The root, which every node is under whether or not it has a parent. `root/impulse` is in the
    // list because it is a transform term too -- a scene that punches on an onset punches here.
    for (const char* rootPath : {"root/rotationSpeed", "root/scale", "root/impulse"}) {
        inherit(directInfluencesOf(engine, rootPath), "the world root");
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
        tooltip("Beginner: world macros, atmosphere, camera.\n"
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
    // ADR-387: the composition's own nodes -- the things somebody placed and named. This list was
    // missing, and its absence is half of why the Tree panel was written: every other kind of
    // object in the scene could be selected here and inspected, and the authored nodes could only
    // be reached by clicking them in the viewport, which does not work for a group whose children
    // are what the click lands on. With the list here, "select the tree and open its wind group" is
    // a thing somebody can do without knowing any parameter path.
    if (scene::Composition* comp = engine.composition(); comp != nullptr) {
        if (ImGui::TreeNodeEx("Objects", ImGuiTreeNodeFlags_DefaultOpen)) {
            const auto& nodes = comp->nodes();
            for (const auto& node : nodes) {
                const std::string label = node->name + "  (" + scene::nodeKindName(node->kind) + ")";
                select(WorldSelection::Kind::Node, node->name, label.c_str(), 0);
            }
            if (nodes.empty()) {
                ImGui::TextDisabled("no authored objects");
            }
            ImGui::TreePop();
        }
    }
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
                    bulletWrapped("%s -> %s (%s, strength %.2f)%s", e.field.c_str(), spatial::effectorOpName(e.op),
                                      spatial::effectorBlendName(e.blend), static_cast<double>(e.strength),
                                      e.enabled ? "" : " [off]");
                }
                ImGui::TreePop();
            }
            if (!pg.cloud.attributes.buffers().empty() && ImGui::TreeNode("Attributes")) {
                for (const spatial::AttributeBuffer& b : pg.cloud.attributes.buffers()) {
                    const spatial::AttributeStats st = spatial::attributeStats(b);
                    bulletWrapped("%s (%s): min %.3f max %.3f mean %.3f", b.name.c_str(),
                                      spatial::attributeTypeName(b.type), static_cast<double>(st.min.x),
                                      static_cast<double>(st.max.x), static_cast<double>(st.mean.x));
                }
                ImGui::TreePop();
            }
            break;
        }
    }
    // ADR-387: the selected object's own controls, editable, grouped by the sub-prefix the
    // registration already gives them -- `nodes/<n>/wind/strength` sits under "wind", and every
    // node that declares a wind body gets that group for free. Nothing here knows the name of any
    // scene, any node or any effect: the grouping is read off the paths, which is what makes this
    // the answer to the Tree panel rather than a second Tree panel. A node with no sub-groups shows
    // one flat list, which is what a plain mesh should look like.
    {
        std::vector<params::IParameter*> plain;
        std::vector<std::string> groupOrder;
        std::unordered_map<std::string, std::vector<params::IParameter*>> groups;
        for (params::IParameter* param : engine.params().ordered()) {
            if (!param->flags().exposed || !detail::pathStartsWith(param->path(), prefix)) {
                continue;
            }
            if (!shows(param->path())) {
                continue;
            }
            const std::string rel = param->path().substr(prefix.size());
            const std::size_t slash = rel.find('/');
            if (slash == std::string::npos) {
                plain.push_back(param);
                continue;
            }
            const std::string group = rel.substr(0, slash);
            auto [it, inserted] = groups.try_emplace(group);
            if (inserted) {
                groupOrder.push_back(group);
            }
            it->second.push_back(param);
        }
        if (!plain.empty() || !groupOrder.empty()) {
            ImGui::SeparatorText("Properties");
        }
        const auto row = [&](params::IParameter* param, std::size_t cut) {
            ImGui::PushID(param->path().c_str());
            const std::string rel = param->path().substr(cut);
            drawParameterValue(*param, rel.c_str());
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
        };
        for (params::IParameter* param : plain) {
            row(param, prefix.size());
        }
        for (const std::string& group : groupOrder) {
            if (!ImGui::TreeNodeEx(group.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                continue;
            }
            for (params::IParameter* param : groups[group]) {
                row(param, prefix.size() + group.size() + 1);
            }
            ImGui::TreePop();
        }
    }

    ImGui::SeparatorText("Influences");
    ImGui::TextDisabled("Why is this moving? Timeline, routes and state changes are listed here,\n"
                        "including the ones that move this object by moving what it hangs off.");
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
                case Influence::Kind::Entity: kind = "entity"; break;
                case Influence::Kind::Staging: kind = "staging"; break;
                }
                // Build the trailing value first: a temporary std::string's c_str() must not
                // outlive the full expression it was created in.
                std::string value;
                if (i.kind == Influence::Kind::Route) {
                    value = fmt::format("= {:.3f}", i.value);
                }
                const std::string text =
                    i.via.empty()
                        ? fmt::format("{}: {} ({}) {}", kind, i.source, i.detail, value)
                        // The via clause goes first, because it is the answer to the question the
                        // user actually asked: this object is moving because something above it is.
                        : fmt::format("via {} -- {}: {} ({}) {}", i.via, kind, i.source, i.detail,
                                      value);
                if (i.kind != Influence::Kind::Route) {
                    bulletWrapped("%s", text.c_str());
                    continue;
                }
                // A route is clickable: it takes you to the thing you would have gone looking for
                // (ADR-211). `Selectable` rather than a button, so the row reads as a row and the
                // whole width is the hit target -- a bullet list you have to hit a five-pixel
                // widget inside is not clickable in any sense a person cares about.
                ImGui::Bullet();
                ImGui::PushID(&i);
                if (ImGui::Selectable(text.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick)) {
                    focusRouteSource = i.routeSource;
                    focusRouteTarget = i.routeTarget;
                }
                if (ImGui::IsItemHovered()) {
                    tooltip("Open this route in the Modulation panel.");
                }
                ImGui::PopID();
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
    ImGui::SeparatorText("Scene state");
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
                bulletWrapped("%s  %.2f .. %.2f", t.path.c_str(), static_cast<double>(t.min),
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

// The staging director's own state, as distinct from `app::WorldDirector`'s artistic knobs below
// (two unrelated things called "director"; see `src/stage/staging.hpp`). ADR-385: the brief asked
// for an overlay that could diagnose a sequencing bug, and the list it gave -- state, time, world
// position, velocity, beam state, abduction state, lift and fade progress -- is exactly what
// `Staging::sequenceStates()` publishes. Read-only: this window reports the frame and never steers
// it.
void WorldPanel::drawScenarios(app::Engine& engine) {
    scene::Composition* comp = engine.composition();
    if (comp == nullptr) {
        ImGui::TextDisabled("No composition.");
        return;
    }
    const auto& states = comp->director().sequenceStates();
    if (states.empty()) {
        ImGui::TextDisabled("This world has no staging scenarios.");
        ImGui::TextWrapped("A scenario is a cast of actors and a list of beats, authored in the "
                           "scene file under `staging`. Glowmere Valley 2's abduction is one.");
        return;
    }
    for (const stage::Staging::SequenceState& st : states) {
        ImGui::PushID(st.scenario.c_str());
        ImGui::Text("%s", st.scenario.c_str());
        ImGui::SameLine();
        if (!st.running) {
            ImGui::TextDisabled("(not running)");
        } else if (st.gated) {
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f), "WAITING  %s",
                               st.gatedOn.c_str());
        } else {
            ImGui::TextDisabled("beat '%s' for %.2fs", st.beat.c_str(), st.time - st.beatStart);
        }
        if (st.maxCycles > 0) {
            ImGui::TextDisabled("cycle %d of %d      t = %.3f s", st.cycle + 1, st.maxCycles,
                                st.time);
        } else {
            ImGui::TextDisabled("cycle %d            t = %.3f s", st.cycle + 1, st.time);
        }
        if (st.running && !st.cues.empty()
            && ImGui::BeginTable("cues", 6, ImGuiTableFlags_SizingStretchProp
                                                | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("role");
            ImGui::TableSetupColumn("entity");
            ImGui::TableSetupColumn("step");
            ImGui::TableSetupColumn("progress");
            ImGui::TableSetupColumn("world position");
            ImGui::TableSetupColumn("speed");
            ImGui::TableHeadersRow();
            for (const stage::Staging::CueState& c : st.cues) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(c.role.empty() ? "-" : c.role.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(c.entity.empty() ? "-" : c.entity.c_str());
                ImGui::TableNextColumn();
                if (c.done) {
                    ImGui::TextDisabled("done");
                } else {
                    ImGui::Text("%s (%zu/%zu)", c.step.c_str(), c.index + 1, c.steps);
                }
                ImGui::TableNextColumn();
                if (c.done) {
                    ImGui::TextDisabled("-");
                } else if (c.duration > 0.0) {
                    ImGui::Text("%.0f%%  %.2f/%.2fs", static_cast<double>(c.progress) * 100.0,
                                c.elapsed, c.duration);
                } else {
                    ImGui::Text("%.2fs", c.elapsed);
                }
                ImGui::TableNextColumn();
                ImGui::Text("%.2f, %.2f, %.2f", static_cast<double>(c.position.x),
                            static_cast<double>(c.position.y), static_cast<double>(c.position.z));
                ImGui::TableNextColumn();
                // The number `BeatDesc::stillRoles` gates on, in the place somebody would look to
                // find out why a beat is waiting.
                ImGui::Text("%.3f m/s", static_cast<double>(c.speed));
            }
            ImGui::EndTable();
        }
        ImGui::Separator();
        ImGui::PopID();
    }
    ImGui::TextDisabled("Read-only. The contract these beats keep is docs/abduction-authoring.md.");
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
                tooltip("%s", app::directorKnobDescription(mapping.knob));
            }
            if (ImGui::TreeNode("targets")) {
                for (const app::WorldMacroTarget& t : mapping.targets) {
                    bulletWrapped("%s  %.2f .. %.2f", t.path.c_str(), static_cast<double>(t.min),
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

void WorldPanel::drawDebugOptions(app::Engine& engine, WorldEditor* editor) {
    (void)engine;
    static char selectedEntity[128] = {};
    if (std::string(selectedEntity) != debug.selectedEntity) {
        std::snprintf(selectedEntity, sizeof(selectedEntity), "%s", debug.selectedEntity.c_str());
    }
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
    if (ImGui::IsItemHovered()) {
        tooltip("Colour every scattered instance by the LOD rung the cull pass gave it:\n"
                          "green 0 (full mesh), yellow 1, orange 2, red 3, purple culled.\n"
                          "Grey means the pass did not run for that object this frame, so there is\n"
                          "no decision to show. Costs one buffer readback per scattered object.");
    }
    ImGui::SameLine();
    ImGui::Checkbox("Culling", &debug.culling);
    ImGui::Checkbox("Entity bounds", &debug.entityBounds);
    ImGui::SameLine();
    ImGui::Checkbox("Entity origins", &debug.entityOrigins);
    ImGui::SameLine();
    ImGui::Checkbox("Entity ids", &debug.entityIds);
    if (ImGui::IsItemHovered()) {
        tooltip("Colour each entity's bounds by the pick id the identifier target writes.");
    }
    ImGui::Checkbox("Submitted only", &debug.submittedOnly);
    if (ImGui::IsItemHovered()) {
        tooltip("Draw entity diagnostics only for what survived the camera cull.");
    }
    ImGui::SameLine();
    ImGui::Checkbox("World axes", &debug.worldAxes);
    ImGui::SameLine();
    ImGui::Checkbox("Frustum", &debug.frustum);
    if (ImGui::IsItemHovered()) {
        tooltip("The camera's own frustum and basis. On a live camera this is the screen edge;\n"
                          "it is worth seeing while the view is frozen, when it is the volume the cull used.");
    }
    ImGui::SameLine();
    ImGui::Checkbox("Transform trail", &debug.transformTrail);
    if (ImGui::IsItemHovered()) {
        tooltip("The recorded world path of the selected entity, over the last few seconds.");
    }
    ImGui::SameLine();
    ImGui::Checkbox("Skeletons", &debug.skeletons);
    if (ImGui::IsItemHovered()) {
        tooltip("Every skinned entity's joints and bones, in world space.");
    }
    ImGui::SameLine();
    ImGui::Checkbox("Beam axes", &debug.beams);
    if (ImGui::IsItemHovered()) {
        tooltip("Every particle emitter's disc, the axis its column actually fires\n"
                          "along, and a second ring where the column ends -- its own speed and\n"
                          "lifetime, integrated. The end ring is the point: a beam treated as an\n"
                          "axis is a line of infinite length, and the tractor beam was stopping\n"
                          "four metres above the animal it was supposedly lifting (ADR-262).");
    }
    // Shadow Lab (§15). Three switches, and the third is the one that answers the question people
    // actually ask: an entity's box coloured by what the shadow passes did with it.
    ImGui::Checkbox("Cascade volumes", &debug.shadowCascades);
    if (ImGui::IsItemHovered()) {
        tooltip("The orthographic box each cascade rasterises into, drawn from the matrix the\n"
                          "renderer uploaded -- not from a second fit. The dot is the texel-snapped centre:\n"
                          "it moves in whole texels or not at all, so a crawling cascade can be watched crawl.");
    }
    ImGui::SameLine();
    ImGui::Checkbox("Cascade slices", &debug.shadowCascadeSlices);
    if (ImGui::IsItemHovered()) {
        tooltip("The part of the camera frustum whose pixels select each cascade, in the same\n"
                          "colour as its volume. Like Frustum, this is the screen edge on a live camera and\n"
                          "is worth seeing from a second view or while frozen.");
    }
    ImGui::SameLine();
    ImGui::Checkbox("Shadow casters", &debug.shadowCasters);
    if (ImGui::IsItemHovered()) {
        tooltip("Green casts. Amber casts although the camera cannot see it -- the second cull\n"
                          "kept it (ADR-046). Red does not cast: not drawable, castsShadow off, a style the\n"
                          "shadow passes skip, or outside every cascade.");
    }
    ImGui::SliderInt("Cascade shown", &debug.shadowCascade, -1, 7);
    if (ImGui::IsItemHovered()) {
        tooltip("-1 draws every shadow view; 0..7 restricts both cascade overlays to one layer.");
    }
    if (ImGui::InputText("Selected entity", selectedEntity, sizeof(selectedEntity))) {
        debug.selectedEntity = selectedEntity;
    }
    drawNavigationOptions(editor);
    ImGui::Checkbox("SDF slice", &debug.sdfSlice);
    ImGui::SameLine();
    ImGui::SliderFloat("slice y", &debug.sliceHeight, -20.0f, 20.0f);
    ImGui::SliderInt("field grid", &debug.fieldGrid, 2, 24);
    ImGui::SliderFloat("point size", &debug.pointSize, 1.0f, 12.0f);
    ImGui::Checkbox("Depth test", &debug.depthTest);
}

// The navigation overlay's switches (ADR-197).
//
// Here, and not in the Performance panel, for one reason: the questions this answers -- "why is my
// character going that way", "why is it not going anywhere", "can it even get across the river" --
// are asked while looking at a character in the world, and this is the tab a person is already in
// when they are looking at entity bounds, the transform trail and the skeletons. Performance is
// where you go when a frame is too slow; nothing here is about frame time, and filing it there
// would be filing it by what it is made of rather than by what it is for.
//
// The line under the switches is not decoration. Every one of these can legitimately draw nothing
// -- no entities, no navigation graph, nothing selected, the camera pointing off the grid -- and a
// checkbox whose effect cannot be seen is indistinguishable from a checkbox wired to nothing, which
// is the defect this project keeps shipping. So the editor publishes why, and it is printed.
void WorldPanel::drawNavigationOptions(WorldEditor* editor) {
    ImGui::SeparatorText("Navigation");
    if (editor == nullptr) {
        ImGui::TextDisabled("This session has no world editor, so there is no navigation overlay.");
        return;
    }
    ImGui::Checkbox("Route", &editor->showNavRoute);
    if (ImGui::IsItemHovered()) {
        tooltip("The selected entity's planned waypoints, the leg it is walking, its\n"
                          "destination, and its phase and path status as a label. Drawn from the\n"
                          "selection: select the node an entity drives to see its route.");
    }
    ImGui::SameLine();
    ImGui::Checkbox("Nav grid", &editor->showNavGrid);
    if (ImGui::IsItemHovered()) {
        tooltip("Walkable / water / steep / blocked cells near the view.\n"
                          "Green is standable, brighter green is a walkable cell against an edge.");
    }
    ImGui::SameLine();
    // Greyed rather than merely labelled when the grid is off. Both of these are settings *of* the
    // grid: leaving them live while nothing draws is a control that responds and does nothing,
    // which is the same defect as one wired to nothing and harder to notice.
    ImGui::BeginDisabled(!editor->showNavGrid);
    ImGui::Checkbox("Regions", &editor->navGridRegions);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) {
        tooltip("Colour walkable cells by connected region instead of by flag. Two cells\n"
                          "the same colour are reachable from each other; two different colours are\n"
                          "not, whatever the distance between them. Needs 'Nav grid'.");
    }
    ImGui::SameLine();
    ImGui::Checkbox("Shore / vista", &editor->showNavPoints);
    if (ImGui::IsItemHovered()) {
        tooltip("The interest points the grid extracts while it builds: rings where\n"
                          "walkable ground meets water, stalks on walkable local maxima. These are\n"
                          "what `explore` picks destinations from.");
    }
    ImGui::SetNextItemWidth(200.0f);
    ImGui::BeginDisabled(!editor->showNavGrid);
    ImGui::SliderFloat("grid radius", &editor->navGridRadius, 10.0f, 400.0f, "%.0f m");
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) {
        tooltip("How far around the point the view is aimed at the grid is drawn.\n"
                          "Every cell is four world points projected on the CPU, so this is the\n"
                          "whole cost of the overlay -- see the line below for what it is drawing.");
    }
    ImGui::TextWrapped("%s", editor->navStatus().c_str());
}

} // namespace avgen::ui
