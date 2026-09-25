#include "ui/world_panel.hpp"

#include "ui/param_widget.hpp"

#include "ui/style.hpp"
#include "params/preset.hpp"
#include "scene/composition.hpp"
#include "scene/temporal_settings.hpp"
#include "stage/staging.hpp"
#include "ui/world_editor.hpp"

#include <fmt/format.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unordered_map>

namespace avgen::ui {

// The same red every other panel uses for "this names something that is not there".
constexpr ImVec4 kPanelWarning(1.0f, 0.45f, 0.35f, 1.0f);

namespace {

// ---- ADR-410: Frame echo --------------------------------------------------------------------------
//
// A scene-level post setting (`temporal/<effect>/...`), NOT an effect instance: ADR-702 made effects
// things attached to owners, and a temporal pass over the finished image belongs to no owner. It
// lived in the removed World Effects panel as its "Reality / Temporal / Digital" section; it is
// drawn here, on the Environment selection, with the same rows and the same history disclosure.
void drawFrameEcho(app::Engine& engine) {
    ImGui::SeparatorText("Frame echo");
    const std::string prefix = scene::temporalParameterPrefix(scene::TemporalEffectKind::FrameEcho);
    if (ImGui::IsItemHovered()) {
        // The limitation an author needs at the moment of choosing (ADR-410): a mosh or an echo
        // over fog advecting with the surface behind it looks exactly like a bug.
        tooltipUnformatted(
            "Reaches back over previous frames, so a moving object leaves a trail of where it was.\n\n"
            "Describes OPAQUE surfaces only. Fog, aurora and the vortex are composited after the\n"
            "scene pass and carry no motion of their own, so an echo over them follows whatever\n"
            "solid surface is behind them rather than the effect itself.");
    }
    const auto rowLabel = [](const char* label) {
        const float start = ImGui::GetCursorPosX();
        ImGui::TextUnformatted(label);
        ImGui::SameLine(start + ImGui::CalcTextSize("Sparkle brightness  ").x + ImGui::GetStyle().ItemSpacing.x);
        ImGui::SetNextItemWidth(-1.0f);
    };
    if (params::IParameter* on = engine.params().find(prefix + "enabled"); on != nullptr) {
        bool v = on->baseComponent(0) >= 0.5f;
        ImGui::PushID("echo-enabled");
        if (ImGui::Checkbox("Enabled", &v)) {
            on->setBaseComponent(0, v ? 1.0f : 0.0f);
        }
        ImGui::PopID();
    }

    // The disclosure ADR-410 turns on, phrased as what to DO: "settling, 3 of 8" says the picture
    // is on its way; the stuck wording is kept for the case that genuinely is stuck.
    const scene::TemporalHistoryReport& report = engine.temporalHistoryReport();
    if (report.stalled) {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "Temporal: cold - this is not the rendered picture");
    } else if (report.settling()) {
        ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.35f, 1.0f), "Temporal: settling - %u of %u frames",
                           report.framesValid, report.framesNeeded);
    } else if (report.framesNeeded > 0) {
        ImGui::TextDisabled("Temporal: %u frames, %.1f MB at %ux%u", report.framesValid,
                            static_cast<double>(report.bytes) / (1024.0 * 1024.0), report.width, report.height);
    }

    for (const EffectRow& r : temporalEchoRows()) {
        if (!r.section.empty()) {
            const std::string section(r.section);
            ImGui::SeparatorText(section.c_str());
        }
        const std::string path = prefix + std::string(r.leaf);
        params::IParameter* p = engine.params().find(path);
        if (p == nullptr) {
            continue;
        }
        const std::string label(r.label);
        const std::string format = r.format.empty() ? std::string("%.2f") : std::string(r.format);
        ImGui::PushID(path.c_str());
        if (r.color) {
            float rgb[3] = {p->baseComponent(0), p->baseComponent(1), p->baseComponent(2)};
            rowLabel(label.c_str());
            if (ImGui::ColorEdit3("##c", rgb, ImGuiColorEditFlags_Float)) {
                for (std::size_t i = 0; i < 3; ++i) {
                    p->setBaseComponent(i, rgb[i]);
                }
            }
        } else {
            float v = p->baseComponent(0);
            rowLabel(label.c_str());
            if (ImGui::SliderFloat("##v", &v, p->softMin(0), p->softMax(0), format.c_str(),
                                   r.logarithmic ? ImGuiSliderFlags_Logarithmic : 0)) {
                p->setBaseComponent(0, v);
            }
        }
        if (!r.tip.empty() && ImGui::IsItemHovered()) {
            tooltipUnformatted(std::string(r.tip).c_str());
        }
        ImGui::PopID();
    }
}

} // namespace

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
        ImGui::TextDisabled("fog %.4f  horizon %.2f  march %.0f m", static_cast<double>(s.environment.volumeDensity),
                            static_cast<double>(s.environment.horizonDensity),
                            static_cast<double>(s.environment.volumeMaxDistance));
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Camera & lighting")) {
        select(WorldSelection::Kind::Camera, "", "camera", 0);
        ImGui::TextDisabled("%zu light(s)", s.lights.size());
        ImGui::TreePop();
    }
}

void WorldPanel::drawInspector(app::Engine& engine, EditHistory* history) {
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
            ImGui::Text("%zu instances, %zu op(s), %zu effector(s)", pg.instances.size(),
                        pg.pointOps.size(), pg.effectors.size());
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
            drawDeformerStack(engine, pg);
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
            // ADR-421: the deformer stack has its own editor above, which draws these same
            // parameters labelled by the kind that owns them. Left in the generic list they would
            // appear a second time as `1/amount`, `2/amount`, with nothing saying that slot 1 is a
            // Twist -- two controls on one path, one of which explains itself and one of which does
            // not. Only suppressed for a procedural, because that is the only owner of the prefix.
            if (group == "deform" && selection.kind == WorldSelection::Kind::Procedural) {
                continue;
            }
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

    // ADR-702: the selected owner's effect stack. The same section for every owner; which owner a
    // selection is, is the only thing decided here. The other selection kinds (a procedural, a
    // field, a spline...) are not effect owners, so they get no section rather than an empty one.
    if (scrollToEffects) {
        ImGui::SetScrollHereY(0.0f);
        scrollToEffects = false;
    }
    switch (selection.kind) {
    case WorldSelection::Kind::Environment:
        effects_.draw(engine, world::EffectOwner::world(), history);
        drawFrameEcho(engine);
        break;
    case WorldSelection::Kind::Node:
        effects_.draw(engine, world::EffectOwner::entity(selection.name), history);
        break;
    case WorldSelection::Kind::Camera:
        effects_.draw(engine, world::EffectOwner::camera(), history);
        break;
    default:
        break;
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

// ADR-421. The deformer stack, as a stack.
//
// **What was wrong.** `scene::Deformer` is an ordered, eight-slot, GPU-evaluated, serialised stack
// with seven kinds, authored in eight shipped scenes -- and the entire user interface for it was
// the words `"%d deformer(s)"` on the line above this function. Its numbers were *reachable*: the
// Inspector's generic grouping put every `deform/<n>/<leaf>` under one tree node called "deform",
// flattened to `1/amount`, `2/axis`, with nothing anywhere saying that slot 1 is a Twist and slot 2
// is a Bend. That is precisely the owner's distinction -- reachable in principle is not findable --
// and it is ADR-375's travelling band of light again, on a feature that shipped.
//
// **The structure was not reachable at all.** Which kind a slot is, which space it acts in, its
// order, whether it exists: all of those are read from the file by
// `registerProceduralParameters` and none of them had a writer outside the scene parser. Nor did
// the per-kind fields that registration does not cover -- a Sine's `displacementAxis`, which is the
// direction it actually pushes vertices, a Noise's `axisMask` and `seed`, a Field deformer's field
// name. Those are visible in the picture and were unreachable in the application.
//
// **Two rules shape what is drawn.** Only the leaves the kind's arithmetic reads
// (`ui::deformerRowsFor`), because registration writes the same nine leaves for every slot and a
// Bend's registered `speed` moves nothing -- a control that does nothing teaches an artist the
// system is broken, which is worse than a control that is absent. And every leaf comes from that
// one table, which `test_deformer_panel.cpp` holds against what the object really registers
// (ADR-382).
void WorldPanel::drawDeformerStack(app::Engine& engine, const scene::ProceduralGeometry& object) {
    scene::Composition* composition = engine.composition();
    const std::string prefix = "procedural/" + object.name + "/";

    ImGui::SeparatorText("Deformers");
    if (!deformerStatus_.empty()) {
        ImGui::TextColored(kPanelWarning, "%s", deformerStatus_.c_str());
    }

    // Every structural edit goes through here: it rewrites the authored stack and re-registers, so
    // the `deform/<n>/*` paths describe the stack that is actually there. Per ADR-387, a parameter
    // path is storage, a project's `parameters` block and a route target at once -- so a slot that
    // moved and did not take its paths with it would leave three things pointing at the wrong
    // deformer, silently.
    const auto commit = [&](std::vector<scene::Deformer> next) {
        if (composition == nullptr) {
            return;
        }
        if (auto ok = composition->setNodeDeformers(object.name, std::move(next)); !ok) {
            deformerStatus_ = ok.error().message;
        } else {
            deformerStatus_.clear();
        }
    };

    if (object.deformers.empty()) {
        ImGui::TextDisabled("No deformers. Add one to bend, twist, wave or displace this object.");
    }

    for (std::size_t slot = 0; slot < object.deformers.size(); ++slot) {
        const scene::Deformer& d = object.deformers[slot];
        ImGui::PushID(static_cast<int>(slot));

        // The header says what the slot IS, which is the whole complaint about `1/amount`.
        const std::string title =
            fmt::format("{}. {}{}", slot + 1, scene::deformerKindName(d.kind), d.enabled ? "" : "  [off]");
        const bool open = ImGui::TreeNodeEx(title.c_str(), ImGuiTreeNodeFlags_DefaultOpen);

        // Reorder and remove, on the header's line: a stack whose order cannot be changed is a list.
        // Order is load-bearing here -- a twist then a bend is not a bend then a twist -- and until
        // now it could only be changed by editing the scene file.
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 74.0f);
        ImGui::BeginDisabled(slot == 0);
        if (ImGui::SmallButton("up")) {
            std::vector<scene::Deformer> next = object.deformers;
            std::swap(next[slot - 1], next[slot]);
            commit(std::move(next));
            ImGui::EndDisabled();
            if (open) {
                ImGui::TreePop();
            }
            ImGui::PopID();
            return; // the stack we are walking has been replaced
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(slot + 1 >= object.deformers.size());
        if (ImGui::SmallButton("dn")) {
            std::vector<scene::Deformer> next = object.deformers;
            std::swap(next[slot], next[slot + 1]);
            commit(std::move(next));
            ImGui::EndDisabled();
            if (open) {
                ImGui::TreePop();
            }
            ImGui::PopID();
            return;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) {
            std::vector<scene::Deformer> next = object.deformers;
            next.erase(next.begin() + static_cast<std::ptrdiff_t>(slot));
            commit(std::move(next));
            if (open) {
                ImGui::TreePop();
            }
            ImGui::PopID();
            return;
        }

        if (!open) {
            ImGui::PopID();
            continue;
        }

        // `enabled` is an ordinary registered parameter, so it is keyable and modulatable like
        // anything else and does NOT go through `commit`. Muting a deformer is a performance and an
        // animation gesture, not a structural one.
        if (params::IParameter* p = engine.params().find(prefix + "deform/" +
                                                         std::to_string(slot + 1) + "/enabled")) {
            drawParameterValue(*p, "enabled");
        }

        // Kind and space are structural: registration labels every leaf by kind and
        // `applyProceduralParameters` reads both off the rest copy every frame.
        int kindIndex = 0;
        std::vector<const char*> kindNames;
        kindNames.reserve(std::size(kDeformerKinds));
        for (std::size_t k = 0; k < std::size(kDeformerKinds); ++k) {
            kindNames.push_back(scene::deformerKindName(kDeformerKinds[k]));
            if (kDeformerKinds[k] == d.kind) {
                kindIndex = static_cast<int>(k);
            }
        }
        if (ImGui::Combo("kind", &kindIndex, kindNames.data(), static_cast<int>(kindNames.size()))) {
            std::vector<scene::Deformer> next = object.deformers;
            next[slot].kind = kDeformerKinds[static_cast<std::size_t>(kindIndex)];
            commit(std::move(next));
            ImGui::TreePop();
            ImGui::PopID();
            return;
        }
        static const char* const kSpaceNames[] = {"local (with the object)", "world"};
        int spaceIndex = d.space == scene::DeformSpace::World ? 1 : 0;
        if (ImGui::Combo("space", &spaceIndex, kSpaceNames, 2)) {
            std::vector<scene::Deformer> next = object.deformers;
            next[slot].space = spaceIndex == 1 ? scene::DeformSpace::World : scene::DeformSpace::Local;
            commit(std::move(next));
            ImGui::TreePop();
            ImGui::PopID();
            return;
        }
        if (ImGui::IsItemHovered()) {
            tooltip("Local deforms the object before it is placed, so it travels with it.\n"
                    "World deforms the placed position, so the object moves through a\n"
                    "standing pattern as it travels.");
        }

        // The kind's own numbers, from the one table the test also reads.
        for (const DeformerRow& r : deformerRowsFor(d.kind)) {
            params::IParameter* p = engine.params().find(deformerParameterPath(prefix, slot, r.leaf));
            if (p == nullptr) {
                // Never silent. A leaf this table names and registration does not produce is the
                // empty-box defect, and the panel says so rather than drawing nothing (ADR-375).
                ImGui::TextColored(kPanelWarning, "%.*s: no such parameter", static_cast<int>(r.leaf.size()),
                                   r.leaf.data());
                continue;
            }
            drawParameterValue(*p, std::string(r.label).c_str());
            if (!r.tip.empty() && ImGui::IsItemHovered()) {
                tooltipUnformatted(std::string(r.tip).c_str());
            }
        }

        // The per-kind fields registration does not cover. These are structural and, before this,
        // had no writer anywhere in the application -- a Sine deformer's push direction was a
        // number in a file.
        switch (d.kind) {
        case scene::DeformerKind::Sine: {
            glm::vec3 v = d.displacementAxis;
            if (ImGui::DragFloat3("pushes along", &v.x, 0.01f, -1.0f, 1.0f, "%.2f")) {
                std::vector<scene::Deformer> next = object.deformers;
                next[slot].displacementAxis = v;
                commit(std::move(next));
                ImGui::TreePop();
                ImGui::PopID();
                return;
            }
            if (ImGui::IsItemHovered()) {
                tooltip("The direction vertices are pushed. The axis above is the direction the\n"
                        "wave TRAVELS; this is the direction it moves things in. Set them\n"
                        "perpendicular for a wave, parallel for a squeeze.");
            }
            break;
        }
        case scene::DeformerKind::Noise: {
            glm::vec3 m = d.axisMask;
            if (ImGui::DragFloat3("axis mask", &m.x, 0.01f, 0.0f, 1.0f, "%.2f")) {
                std::vector<scene::Deformer> next = object.deformers;
                next[slot].axisMask = m;
                commit(std::move(next));
                ImGui::TreePop();
                ImGui::PopID();
                return;
            }
            if (ImGui::IsItemHovered()) {
                tooltip("Which axes the noise is allowed to move things along. (1,0,1) jitters\n"
                        "sideways and leaves height alone.");
            }
            int seed = static_cast<int>(d.seed);
            if (ImGui::InputInt("seed", &seed)) {
                std::vector<scene::Deformer> next = object.deformers;
                next[slot].seed = static_cast<std::uint32_t>(std::max(seed, 0));
                commit(std::move(next));
                ImGui::TreePop();
                ImGui::PopID();
                return;
            }
            break;
        }
        case scene::DeformerKind::Field: {
            // The field name is a name, so it is a combo over what exists rather than a text box --
            // the same refusal ADR-420 makes for a subscription. A typo and a field nobody has made
            // yet look identical in a text box.
            std::vector<std::string> names;
            for (const spatial::FieldSpec& f : engine.scene().fields.fields) {
                names.push_back(f.name);
            }
            std::vector<const char*> labels{"(none)"};
            int current = 0;
            for (std::size_t i = 0; i < names.size(); ++i) {
                labels.push_back(names[i].c_str());
                if (names[i] == d.field) {
                    current = static_cast<int>(i) + 1;
                }
            }
            std::string dead;
            if (!d.field.empty() && current == 0) {
                dead = d.field + "  (no such field)";
                labels.push_back(dead.c_str());
                current = static_cast<int>(labels.size()) - 1;
            }
            if (ImGui::Combo("field", &current, labels.data(), static_cast<int>(labels.size()))) {
                std::vector<scene::Deformer> next = object.deformers;
                next[slot].field = (current > 0 && current <= static_cast<int>(names.size()))
                                       ? names[static_cast<std::size_t>(current) - 1]
                                       : std::string();
                commit(std::move(next));
                ImGui::TreePop();
                ImGui::PopID();
                return;
            }
            if (!dead.empty()) {
                ImGui::TextColored(kPanelWarning, "'%s' is not a field in this scene -- it displaces nothing.",
                                   d.field.c_str());
            }
            bool alongNormal = d.alongNormal;
            if (ImGui::Checkbox("scalar fields push along the normal", &alongNormal)) {
                std::vector<scene::Deformer> next = object.deformers;
                next[slot].alongNormal = alongNormal;
                commit(std::move(next));
                ImGui::TreePop();
                ImGui::PopID();
                return;
            }
            break;
        }
        case scene::DeformerKind::Path: {
            std::vector<std::string> names;
            for (const spatial::Spline& sp : engine.scene().splines.splines) {
                names.push_back(sp.name);
            }
            std::vector<const char*> labels{"(none)"};
            int current = 0;
            for (std::size_t i = 0; i < names.size(); ++i) {
                labels.push_back(names[i].c_str());
                if (names[i] == d.spline) {
                    current = static_cast<int>(i) + 1;
                }
            }
            std::string dead;
            if (!d.spline.empty() && current == 0) {
                dead = d.spline + "  (no such spline)";
                labels.push_back(dead.c_str());
                current = static_cast<int>(labels.size()) - 1;
            }
            if (ImGui::Combo("spline", &current, labels.data(), static_cast<int>(labels.size()))) {
                std::vector<scene::Deformer> next = object.deformers;
                next[slot].spline = (current > 0 && current <= static_cast<int>(names.size()))
                                        ? names[static_cast<std::size_t>(current) - 1]
                                        : std::string();
                commit(std::move(next));
                ImGui::TreePop();
                ImGui::PopID();
                return;
            }
            if (!dead.empty()) {
                ImGui::TextColored(kPanelWarning, "'%s' is not a spline in this scene -- the object stays "
                                             "where it is.", d.spline.c_str());
            }
            // World-space Path deformers are skipped by the implementation
            // (`scene/procedural.hpp`'s note), so say so rather than letting somebody set a
            // combination that silently does nothing.
            if (d.space == scene::DeformSpace::World) {
                ImGui::TextColored(kPanelWarning, "A path deformer only works in local space; this one is "
                                             "skipped.");
            }
            break;
        }
        case scene::DeformerKind::Bend:
        case scene::DeformerKind::Twist:
        case scene::DeformerKind::Displacement:
            break;
        }

        ImGui::TreePop();
        ImGui::PopID();
    }

    // Adding. Capped at `kMaxDeformers`, and the cap is stated rather than enforced by a button
    // that quietly stops working.
    if (object.deformers.size() >= static_cast<std::size_t>(scene::kMaxDeformers)) {
        ImGui::TextDisabled("%d deformers is the limit.", scene::kMaxDeformers);
        return;
    }
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::BeginCombo("##adddeform", "Add deformer")) {
        for (const scene::DeformerKind kind : kDeformerKinds) {
            if (!ImGui::Selectable(scene::deformerKindName(kind))) {
                continue;
            }
            std::vector<scene::Deformer> next = object.deformers;
            scene::Deformer add;
            add.kind = kind;
            // A deformer added at amount 0 is the thing ADR-360 shipped by mistake and the owner
            // reported as "it looks unchanged". Somebody who presses Add means it to do something,
            // so each kind starts at a value that is visible and small. `setNodeWindBody` made the
            // same decision for the same reason.
            switch (kind) {
            case scene::DeformerKind::Bend: add.amount = 0.05f; add.falloff = 1.0f; break;
            case scene::DeformerKind::Twist: add.amount = 0.25f; break;
            case scene::DeformerKind::Sine:
                add.amount = 0.1f;
                add.frequency = 1.0f;
                add.displacementAxis = glm::vec3(1.0f, 0.0f, 0.0f);
                break;
            case scene::DeformerKind::Noise: add.amount = 0.1f; add.scale = 0.5f; break;
            case scene::DeformerKind::Displacement: add.amount = 0.1f; add.scale = 0.5f; break;
            case scene::DeformerKind::Field: add.amount = 1.0f; break;
            case scene::DeformerKind::Path: add.amount = 1.0f; break;
            }
            next.push_back(add);
            commit(std::move(next));
            break;
        }
        ImGui::EndCombo();
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
    // Phase B §50. Beside the other debug toggles rather than in a panel of their own: these are
    // diagnostics for a reusable engine system, which is where ADR-387 says first-class UI goes,
    // and a person looking for "why is this leg doing that" looks where Skeletons already is.
    ImGui::SameLine();
    ImGui::Checkbox("IK chains", &debug.motionChains);
    if (ImGui::IsItemHovered()) {
        tooltip("Every IK chain the layer stack solved this frame, coloured by what the solver\n"
                "said: green solved, amber clamped at the limb's reach, red degenerate. The\n"
                "colour is the diagnostic -- a chain drawn the same whatever the solve returned\n"
                "is a picture of a leg.");
    }
    ImGui::SameLine();
    ImGui::Checkbox("IK targets", &debug.motionTargets);
    if (ImGui::IsItemHovered()) {
        tooltip("What each aim and IK layer was asked to reach, with a line from the thing that\n"
                "was asked to reach it -- so 'the hand is off the target' and 'the target is not\n"
                "where you think' are different pictures.");
    }
    ImGui::SameLine();
    ImGui::Checkbox("Contacts", &debug.motionContacts);
    if (ImGui::IsItemHovered()) {
        tooltip("The ground plane under each foot layer and the gap between it and the foot.\n"
                "Absent, not flat, when there is no ground: no answer is not no ground.");
    }
    ImGui::SameLine();
    ImGui::Checkbox("Motion vectors", &debug.motionVectors);
    if (ImGui::IsItemHovered()) {
        tooltip("Body velocity, acceleration and facing, from the state the layers actually ran\n"
                "with this frame.");
    }
    ImGui::SameLine();
    ImGui::Checkbox("Body comp.", &debug.motionCompensation);
    if (ImGui::IsItemHovered()) {
        tooltip("The pelvis correction: where the body joint was and where the solve moved it.\n"
                "Red when a limb still cannot reach afterwards -- 'it ran' and 'it worked' are\n"
                "different answers.");
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
    // ADR-421, §54: do not make the artist guess what an invisible field is doing.
    //
    // These four overlays were all fully implemented, all read by `debug_visualizer.cpp`, and all
    // reachable ONLY from `--debug-draw` -- which is to say, not from the application at all. The
    // wind arrow grid in particular has been there since ADR-055 and is the one thing that answers
    // "which way is the air moving here", which is the question every subscriber to the field bus
    // (ADR-420) now raises. A field nobody can see is a field nobody can tune.
    ImGui::Checkbox("Wind field", &debug.wind);
    if (ImGui::IsItemHovered()) {
        tooltip("Arrows on a grid showing which way the air is moving and how hard, sampled from\n"
                "the same function the vertex shader and the field bus use. Plus each declared\n"
                "wind body's origin, height and radius.");
    }
    ImGui::SameLine();
    ImGui::Checkbox("Vortex", &debug.vortex);
    if (ImGui::IsItemHovered()) {
        tooltip("The funnel's mouth, throat, depth and the direction it turns -- the geometry the\n"
                "volumetric march is sampling, drawn as lines so you can see where it actually is\n"
                "rather than inferring it from the haze.");
    }
    ImGui::SameLine();
    ImGui::Checkbox("Lights", &debug.lights);
    if (ImGui::IsItemHovered()) {
        tooltip("Every light's position, type and reach.");
    }
    ImGui::SameLine();
    ImGui::Checkbox("Light clusters", &debug.lightClusters);
    if (ImGui::IsItemHovered()) {
        tooltip("The cluster grid the forward pass assigns lights to, and how many landed in each.");
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
