// The Effects section (ADR-702). See effects_panel.hpp for what it is and the two edit paths, and
// effects_panel_logic.hpp for the decisions it draws.
//
// Nothing in this file names an effect type. If a card needs something a type knows, the schema is
// asked; if the schema cannot answer, that is a gap in the registry to report, not a branch to add
// here.

#include "ui/effects_panel.hpp"

#include "app/engine.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "ui/edit_history.hpp"
#include "ui/effects_panel_logic.hpp"
#include "ui/style.hpp"
#include "world/atmospherics.hpp"
#include "world/effects/effect_params.hpp"
#include "world/effects/effect_registry.hpp"
#include "world/effects/effect_stack.hpp"
#include "world/effects/field_bus.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::ui {
namespace {

constexpr ImVec4 kMuted(0.6f, 0.62f, 0.66f, 1.0f);
constexpr ImVec4 kOk(0.5f, 0.85f, 0.6f, 1.0f);
constexpr ImVec4 kWarning(1.0f, 0.6f, 0.35f, 1.0f);

// The drag-and-drop payload type for reordering a card within its owner's stack.
constexpr const char* kCardPayload = "AVGEN_EFFECT_CARD";

const char* const kGroundGlowNames[] = {"off", "subtle", "strong"};
const char* const kSkyAnchorNames[] = {"a fixed world point", "the camera"};
// §68. Index 0 of the Follows combo is always the empty subscription, whatever the scene publishes.
const char* const kNoFieldLabel = "nothing (still air)";

ImVec4 severityColour(BadgeSeverity s) {
    switch (s) {
    case BadgeSeverity::Ok: return kOk;
    case BadgeSeverity::Warning: return kWarning;
    case BadgeSeverity::Muted: break;
    }
    return kMuted;
}

// A label column wide enough for the longest label a card draws, then the widget. Measured from
// where this line starts, so a row inside the Advanced tree (indented twice) keeps its label clear.
void rowLabel(const char* label) {
    const float start = ImGui::GetCursorPosX();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(start + ImGui::CalcTextSize("Sparkle brightness  ").x + ImGui::GetStyle().ItemSpacing.x);
    ImGui::SetNextItemWidth(-1.0f);
}

params::ModRoute* findRoute(params::Modulator& modulator, const std::string& source, const std::string& target) {
    for (params::ModRoute& r : modulator.routes()) {
        if (r.source == source && r.target == target) {
            return &r;
        }
    }
    return nullptr;
}

const char* targetNoun(world::EffectTarget t) {
    switch (t) {
    case world::EffectTarget::World: return "the World";
    case world::EffectTarget::Entity: return "an object";
    case world::EffectTarget::Camera: return "a camera";
    case world::EffectTarget::Light: return "a light";
    }
    return "this";
}

// A queued edit's effect, found again by id inside the list the edit is handed -- never by index,
// because the list the edit sees is a fresh capture, not the one the card was drawn from.
Result<void> withEffect(std::vector<world::EffectInstance>& list, const std::string& id,
                        const std::function<void(world::EffectInstance&)>& edit) {
    const std::size_t at = world::findEffect(list, id);
    if (at == list.size()) {
        return fail("effect '{}' is no longer in this scene", id);
    }
    edit(list[at]);
    return {};
}

} // namespace

// ---- queued structural edits -----------------------------------------------------------------------

void EffectsSection::queue(std::string label, ListEdit edit) {
    // One per frame is all a person can do; a second in the same frame is a double-click that should
    // not become two edits on a list the first one already changed.
    if (pendingEdit_ || pendingAdd_) {
        return;
    }
    pendingLabel_ = std::move(label);
    pendingEdit_ = std::move(edit);
}

void EffectsSection::queueInstance(std::string label, const std::string& id, InstanceEdit edit) {
    queue(std::move(label), [id, edit = std::move(edit)](std::vector<world::EffectInstance>& list) {
        return withEffect(list, id, edit);
    });
}

// ---- parameter undo -------------------------------------------------------------------------------

void EffectsSection::noteParamEdit(app::Engine& engine, const std::string& path, std::vector<float> before,
                                   std::string label) {
    if (paramOpen_ && paramPath_ == path) {
        return; // the same gesture, still going: the record keeps its first `before`
    }
    flushParamEdit(engine);
    paramOpen_ = true;
    paramPath_ = path;
    paramLabel_ = std::move(label);
    paramBefore_ = std::move(before);
}

void EffectsSection::flushParamEdit(app::Engine& engine) {
    if (!paramOpen_) {
        return;
    }
    paramOpen_ = false;
    std::vector<float> after = baseComponents(engine, paramPath_);
    if (history_ == nullptr || after.empty() || after == paramBefore_) {
        return;
    }
    EditCommand command(paramLabel_);
    command.params.push_back(ParamChange{paramPath_, std::move(paramBefore_), std::move(after)});
    history_->push(std::move(command));
}

// ---- the section ----------------------------------------------------------------------------------

void EffectsSection::draw(app::Engine& engine, const world::EffectOwner& owner, EditHistory* history) {
    history_ = history;
    owner_ = &owner;
    if (paramOpen_ && !ImGui::IsAnyItemActive()) {
        flushParamEdit(engine);
    }

    ImGui::PushID("effects");
    ImGui::PushID(static_cast<int>(owner.kind));
    ImGui::PushID(owner.name.c_str());

    ImGui::SeparatorText("Effects");

    const std::vector<world::EffectInstance>& list = authoredEffects(engine);
    const std::vector<std::size_t> stack = world::effectsOf(list, owner);

    // ---- + Add Effect, from the registry ----
    const std::vector<AddEffectGroup> menu = addEffectMenu(owner.kind);
    if (menu.empty()) {
        ImGui::BeginDisabled();
        ImGui::Button("+ Add Effect");
        ImGui::EndDisabled();
        ImGui::TextDisabled("No effect type can be attached to %s yet.", targetNoun(owner.kind));
    } else {
        if (ImGui::Button("+ Add Effect")) {
            ImGui::OpenPopup("##addeffect");
        }
        if (ImGui::IsItemHovered()) {
            tooltip("Attach a new effect to %s. It goes to the bottom of the stack.", owner.label().c_str());
        }
        if (ImGui::BeginPopup("##addeffect")) {
            for (const AddEffectGroup& group : menu) {
                ImGui::SeparatorText(group.name);
                for (const AddEffectEntry& entry : group.entries) {
                    if (ImGui::MenuItem(entry.label)) {
                        pendingAdd_ = entry.kind;
                    }
                    if (entry.tip[0] != '\0' && ImGui::IsItemHovered()) {
                        tooltipUnformatted(entry.tip);
                    }
                }
            }
            ImGui::EndPopup();
        }
    }
    if (!status_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, kWarning);
        ImGui::TextWrapped("%s", status_.c_str());
        ImGui::PopStyleColor();
    }

    // §68: a subscription naming a field this scene does not publish, said where the artist is.
    {
        std::vector<std::string> names;
        std::vector<world::fields::Subscription> subs;
        for (const std::size_t i : stack) {
            names.push_back(list[i].name);
            subs.push_back(list[i].flow);
        }
        for (const world::fields::DeadSubscription& d : engine.fieldBus().unresolved(names, subs)) {
            ImGui::PushStyleColor(ImGuiCol_Text, kWarning);
            ImGui::TextWrapped("'%s' follows a field called '%s', which this scene does not publish -- it "
                               "is standing still.",
                               d.subscriber.c_str(), d.field.c_str());
            ImGui::PopStyleColor();
        }
    }
    // A cut is what a camera-gated effect activates against; a scene with none never fires one.
    if (engine.shotSpans().empty() &&
        std::any_of(stack.begin(), stack.end(), [&](std::size_t i) { return activationNeedsCut(list[i].activation); })) {
        ImGui::TextColored(kWarning, "No directed camera: effects gated on the cut cannot fire.");
        if (ImGui::IsItemHovered()) {
            tooltip("Camera > Enable Auto-director, or set the activation to Always or a window.");
        }
    }

    if (stack.empty()) {
        ImGui::TextDisabled("No effects on %s.", owner.isWorld() ? "the World" : owner.label().c_str());
    }
    for (const std::size_t i : stack) {
        drawCard(engine, list[i]);
    }

    // The World's section also answers "what in this scene is attached to nothing": an effect whose
    // owner was deleted or renamed has no inspector to appear in, so it is listed here, where it can
    // be removed, rather than drawing nothing with nowhere saying so.
    if (owner.isWorld()) {
        bool header = false;
        for (const world::EffectInstance& e : list) {
            if (engine.effectStatus(e.id) != world::EffectStatus::Orphaned) {
                continue;
            }
            if (!header) {
                ImGui::Spacing();
                ImGui::TextColored(kWarning, "Attached to something that is not in this scene:");
                header = true;
            }
            ImGui::PushID(e.id.c_str());
            ImGui::BulletText("%s  (on %s '%s')", e.name.c_str(), world::effectTargetName(e.owner.kind),
                              e.owner.name.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove")) {
                const std::string id = e.id;
                queue("Remove " + e.name, [id](std::vector<world::EffectInstance>& l) -> Result<void> {
                    if (!world::removeEffect(l, id)) {
                        return fail("effect '{}' is no longer in this scene", id);
                    }
                    return {};
                });
            }
            ImGui::PopID();
        }
    }

    ImGui::PopID();
    ImGui::PopID();
    ImGui::PopID();

    // ---- apply what this frame asked for, once, after everything that held a parameter ----
    if (pendingAdd_ || pendingEdit_) {
        flushParamEdit(engine); // so the history reads in the order things happened
    }
    if (pendingAdd_) {
        const world::EffectKind kind = *pendingAdd_;
        pendingAdd_.reset();
        if (auto added = addEffectTo(engine, history_, owner, kind); !added) {
            status_ = added.error().message;
        } else {
            status_.clear();
        }
    }
    if (pendingEdit_) {
        ListEdit edit = std::move(pendingEdit_);
        pendingEdit_ = nullptr;
        if (auto ok = commitEffectEdit(engine, history_, std::move(pendingLabel_), edit); !ok) {
            status_ = ok.error().message;
        } else {
            status_.clear();
        }
    }
    if (paramOpen_ && !ImGui::IsAnyItemActive()) {
        flushParamEdit(engine);
    }
    owner_ = nullptr;
}

// ---- one card -------------------------------------------------------------------------------------

void EffectsSection::drawCard(app::Engine& engine, const world::EffectInstance& effect) {
    const std::vector<world::EffectInstance>& list = authoredEffects(engine);
    const StackPosition pos = effectStackPosition(list, effect.id);
    const StatusBadge badge = effectStatusBadge(engine.effectStatus(effect.id));
    const std::string id = effect.id;
    const std::string name = effect.name;

    ImGui::PushID(effect.id.c_str());
    const std::string header = effect.name + "###card";
    if (badge.severity == BadgeSeverity::Warning) {
        ImGui::PushStyleColor(ImGuiCol_Text, kWarning);
    }
    const bool open = ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen |
                                                                  ImGuiTreeNodeFlags_AllowOverlap);
    if (badge.severity == BadgeSeverity::Warning) {
        ImGui::PopStyleColor();
    }

    // ---- drag to reorder, within this owner's stack ----
    if (ImGui::BeginDragDropSource()) {
        ImGui::SetDragDropPayload(kCardPayload, id.c_str(), id.size() + 1);
        ImGui::Text("Move %s", name.c_str());
        ImGui::EndDragDropSource();
    }
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kCardPayload); payload != nullptr) {
            const std::string dragged(static_cast<const char*>(payload->Data));
            const std::size_t from = world::findEffect(list, dragged);
            // Only a card from this same stack: dropping one owner's effect on another's card is not
            // a reattach, and silently doing nothing is better than doing that.
            if (dragged != id && from < list.size() && list[from].owner == effect.owner) {
                const int to = static_cast<int>(pos.index);
                queue("Move " + list[from].name,
                      [dragged, to](std::vector<world::EffectInstance>& l) -> Result<void> {
                          if (!world::moveEffectTo(l, dragged, to)) {
                              return fail("could not move effect '{}'", dragged);
                          }
                          return {};
                      });
            }
        }
        ImGui::EndDragDropTarget();
    }
    if (ImGui::IsItemHovered() && !ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        const world::EffectSchema* schema = world::effectSchema(effect.kind);
        tooltip("%s -- %s stage. Drag to reorder.", schema != nullptr ? schema->displayName : "unknown type",
                schema != nullptr ? world::renderStageName(schema->stage) : "no");
    }

    // ---- the right-hand cluster: status, on/off, menu ----
    const ImGuiStyle& style = ImGui::GetStyle();
    const float menuWidth = ImGui::CalcTextSize("...").x + style.FramePadding.x * 2.0f;
    const float cluster = ImGui::CalcTextSize(badge.label).x + ImGui::GetFrameHeight() + menuWidth +
                          style.ItemSpacing.x * 3.0f;
    ImGui::SameLine(std::max(ImGui::GetContentRegionAvail().x - cluster, 120.0f));
    ImGui::TextColored(severityColour(badge.severity), "%s", badge.label);
    if (ImGui::IsItemHovered()) {
        tooltipUnformatted(badge.explanation);
    }
    ImGui::SameLine();
    drawEnableBox(engine, effect);
    ImGui::SameLine();
    if (ImGui::SmallButton("...")) {
        ImGui::OpenPopup("##cardmenu");
    }
    if (ImGui::BeginPopup("##cardmenu")) {
        menuSubject(name);
        if (ImGui::MenuItem("Move up", nullptr, false, pos.canMoveUp)) {
            queue("Move " + name + " up", [id](std::vector<world::EffectInstance>& l) -> Result<void> {
                if (!world::moveEffect(l, id, -1)) {
                    return fail("'{}' is already at the top of its stack", id);
                }
                return {};
            });
        }
        if (ImGui::MenuItem("Move down", nullptr, false, pos.canMoveDown)) {
            queue("Move " + name + " down", [id](std::vector<world::EffectInstance>& l) -> Result<void> {
                if (!world::moveEffect(l, id, 1)) {
                    return fail("'{}' is already at the bottom of its stack", id);
                }
                return {};
            });
        }
        if (ImGui::MenuItem("Duplicate")) {
            queue("Duplicate " + name, [id](std::vector<world::EffectInstance>& l) -> Result<void> {
                auto copied = world::duplicateEffect(l, id);
                if (!copied) {
                    return std::unexpected(copied.error());
                }
                return {};
            });
        }
        if (ImGui::MenuItem("Reset parameters")) {
            queueInstance("Reset " + name, id, [](world::EffectInstance& e) { resetEffectParameters(e); });
        }
        if (ImGui::IsItemHovered()) {
            tooltip("Put the type's own values back on every row of this effect.\n"
                    "Timing, activation, source and anchor are left as they are.");
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Remove")) {
            queue("Remove " + name, [id](std::vector<world::EffectInstance>& l) -> Result<void> {
                if (!world::removeEffect(l, id)) {
                    return fail("effect '{}' is no longer in this scene", id);
                }
                return {};
            });
        }
        ImGui::EndPopup();
    }

    if (open) {
        ImGui::Indent();
        drawCardBody(engine, effect);
        ImGui::Unindent();
        ImGui::Spacing();
    }
    ImGui::PopID();
}

void EffectsSection::drawEnableBox(app::Engine& engine, const world::EffectInstance& effect) {
    const std::string path = world::effectParameterPrefix(effect.id) + "enabled";
    params::IParameter* p = engine.params().find(path);
    if (p == nullptr) {
        ImGui::TextDisabled("--");
        return;
    }
    std::vector<float> before = baseComponents(engine, path);
    bool on = p->baseComponent(0) >= 0.5f;
    if (ImGui::Checkbox("##on", &on)) {
        p->setBaseComponent(0, on ? 1.0f : 0.0f);
        noteParamEdit(engine, path, std::move(before), (on ? "Enable " : "Disable ") + effect.name);
    }
    if (ImGui::IsItemHovered()) {
        tooltip("On / off. The parameter %s -- key it, route it, cue it.", path.c_str());
    }
}

void EffectsSection::drawCardBody(app::Engine& engine, const world::EffectInstance& effect) {
    const StatusBadge badge = effectStatusBadge(engine.effectStatus(effect.id));
    if (badge.severity == BadgeSeverity::Warning) {
        ImGui::PushStyleColor(ImGuiCol_Text, kWarning);
        ImGui::TextWrapped("%s", badge.explanation);
        // ADR-703: the builder's own sentence -- which budget, which part -- when it gave one.
        if (const std::string_view why = engine.effectStatusReason(effect.id); !why.empty()) {
            ImGui::TextWrapped("%.*s", static_cast<int>(why.size()), why.data());
        }
        ImGui::PopStyleColor();
    }

    const world::EffectSchema* schema = world::effectSchema(effect.kind);
    if (schema == nullptr) {
        // Said out loud rather than drawn as an empty card (ADR-392): a card with nothing in it
        // looks like an effect with no controls, not like a type this build does not know.
        ImGui::TextColored(kWarning, "This effect's type is not registered: nothing can be changed "
                                     "here and nothing renders it.");
        return;
    }
    ImGui::TextColored(kMuted, "%s  -  %s", schema->displayName, world::renderStageName(schema->stage));

    // ---- preset ----
    if (!schema->styles.empty()) {
        int current = -1;
        for (std::size_t s = 0; s < schema->styles.size(); ++s) {
            if (effect.style == schema->styles[s].name) {
                current = static_cast<int>(s);
            }
        }
        rowLabel("Preset");
        const char* preview = current >= 0 ? schema->styles[static_cast<std::size_t>(current)].name : "custom";
        if (ImGui::BeginCombo("##preset", preview)) {
            for (std::size_t s = 0; s < schema->styles.size(); ++s) {
                const world::EffectStyle& st = schema->styles[s];
                if (ImGui::Selectable(st.name, static_cast<int>(s) == current)) {
                    const std::string styleName = st.name;
                    const auto apply = st.apply;
                    queueInstance("Preset " + styleName + " on " + effect.name, effect.id,
                                  [styleName, apply](world::EffectInstance& e) {
                                      if (apply != nullptr) {
                                          apply(e);
                                      }
                                      e.style = styleName;
                                  });
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered()) {
            tooltip("Sets the look in one go. Every value stays editable below.");
        }
    }

    const CardRows rows = effectCardRows(*schema, effect);
    for (const world::EffectField* field : rows.main) {
        drawField(engine, effect, *field);
    }

    // ---- ground glow (ADR-230 §6): three words, structural; how much is a parameter ----
    if (schema->groundGlow) {
        int ground = static_cast<int>(effect.ground.mode);
        rowLabel("Ground glow");
        if (ImGui::Combo("##ground", &ground, kGroundGlowNames, 3)) {
            const auto mode = static_cast<world::GroundGlow>(std::clamp(ground, 0, 2));
            queueInstance("Ground glow on " + effect.name, effect.id,
                          [mode](world::EffectInstance& e) { e.ground.mode = mode; });
        }
        if (ImGui::IsItemHovered()) {
            tooltip("How much of this lands on the ground below it: a wash, not a shadow-casting light.");
        }
        for (const world::EffectField* field : rows.groundMain) {
            drawField(engine, effect, *field);
        }
    }

    drawBeatResponse(engine, effect);

    // ---- endpoints: what the effect is about ----
    if (schema->getSource != nullptr && schema->setSource != nullptr) {
        ImGui::SeparatorText("Source");
        drawEndpoint(engine, effect, false);
        if (schema->getTarget != nullptr && schema->setTarget != nullptr && schema->hasTarget != nullptr) {
            ImGui::SeparatorText("Target");
            drawEndpoint(engine, effect, true);
        }
    }

    // ---- Advanced ----
    const bool hasAdvanced = !rows.advanced.empty() || !rows.groundAdvanced.empty() || !rows.flow.empty() ||
                             effectHasFlowPicker(*schema) ||
                             (schema->anchorSection != nullptr && schema->getAnchor != nullptr);
    if (hasAdvanced && ImGui::TreeNode("Advanced")) {
        if (effectHasFlowPicker(*schema)) {
            drawFlowPicker(engine, effect);
        }
        for (const world::EffectField* field : rows.flow) {
            drawField(engine, effect, *field);
        }
        if (schema->anchorSection != nullptr && schema->getAnchor != nullptr && schema->setAnchor != nullptr) {
            ImGui::SeparatorText(schema->anchorSection);
            int anchor = static_cast<int>(schema->getAnchor(effect));
            rowLabel("Anchored to");
            if (ImGui::Combo("##anchor", &anchor, kSkyAnchorNames, 2)) {
                const auto setAnchor = schema->setAnchor;
                const auto value = static_cast<world::SkyAnchor>(std::clamp(anchor, 0, 1));
                queueInstance("Anchor " + effect.name, effect.id,
                              [setAnchor, value](world::EffectInstance& e) { setAnchor(e, value); });
            }
            if (ImGui::IsItemHovered()) {
                tooltip("A world anchor gives real parallax: the effect slides against the stars\n"
                        "as the camera travels, and it can leave frame. A camera anchor keeps its\n"
                        "bearing however far the camera goes.");
            }
        }
        for (const world::EffectField* field : rows.advanced) {
            drawField(engine, effect, *field);
        }
        for (const world::EffectField* field : rows.groundAdvanced) {
            drawField(engine, effect, *field);
        }
        ImGui::TreePop();
    }

    // ---- Timing ----
    if (ImGui::TreeNode("Timing")) {
        const std::span<const char* const> labels = activationLabels();
        int activation = static_cast<int>(effect.activation);
        rowLabel("Activation");
        if (ImGui::Combo("##activation", &activation, labels.data(), static_cast<int>(labels.size()))) {
            const auto value =
                static_cast<world::Activation>(std::clamp(activation, 0, static_cast<int>(labels.size()) - 1));
            queueInstance("Activation of " + effect.name, effect.id,
                          [value](world::EffectInstance& e) { e.activation = value; });
        }
        if (ImGui::IsItemHovered()) {
            tooltip("When the effect exists at all. The camera-gated two need a directed camera.");
        }
        for (const world::EffectField* field : rows.timing) {
            drawField(engine, effect, *field);
        }
        ImGui::TreePop();
    }

    ImGui::TextColored(kMuted, "Parameters: %s...", world::effectParameterPrefix(effect.id).c_str());
}

// ---- one row --------------------------------------------------------------------------------------

void EffectsSection::drawField(app::Engine& engine, const world::EffectInstance& effect,
                               const world::EffectField& field) {
    if (field.section != nullptr && field.section[0] != '\0') {
        ImGui::SeparatorText(field.section);
    }
    const std::string path = world::effectParameterPrefix(effect.id) + field.leaf;
    params::IParameter* p = engine.params().find(path);
    if (p == nullptr) {
        // A row naming a parameter nobody registered is said, not drawn as an empty box (ADR-392).
        rowLabel(field.label);
        ImGui::TextColored(kWarning, "not registered: %s", path.c_str());
        return;
    }
    std::vector<float> before = baseComponents(engine, path);
    const std::string undoLabel = std::string(field.label) + " of " + effect.name;

    ImGui::PushID(field.leaf);
    bool changed = false;
    switch (field.type) {
    case world::FieldType::Float: {
        float v = p->baseComponent(0);
        rowLabel(field.label);
        const ImGuiSliderFlags flags = field.logarithmic ? ImGuiSliderFlags_Logarithmic : 0;
        const char* format = field.format[0] != '\0' ? field.format : "%.2f";
        if (ImGui::SliderFloat("##v", &v, p->softMin(0), p->softMax(0), format, flags)) {
            p->setBaseComponent(0, v);
            changed = true;
        }
        break;
    }
    case world::FieldType::Choice: {
        if (field.choices == nullptr || field.choiceCount <= 0) {
            break;
        }
        int index = std::clamp(static_cast<int>(p->baseComponent(0) + 0.5f), 0, field.choiceCount - 1);
        rowLabel(field.label);
        if (ImGui::Combo("##v", &index, field.choices, field.choiceCount)) {
            p->setBaseComponent(0, static_cast<float>(index));
            changed = true;
        }
        break;
    }
    case world::FieldType::Color: {
        float rgb[3] = {p->baseComponent(0), p->baseComponent(1), p->baseComponent(2)};
        rowLabel(field.label);
        if (ImGui::ColorEdit3("##v", rgb,
                              ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_NoOptions)) {
            for (std::size_t i = 0; i < 3; ++i) {
                p->setBaseComponent(i, rgb[i]);
            }
            changed = true;
        }
        break;
    }
    case world::FieldType::Bool: {
        bool b = p->baseComponent(0) >= 0.5f;
        if (ImGui::Checkbox(field.label, &b)) {
            p->setBaseComponent(0, b ? 1.0f : 0.0f);
            changed = true;
        }
        break;
    }
    }
    if (field.tip != nullptr && field.tip[0] != '\0' && ImGui::IsItemHovered()) {
        tooltipUnformatted(field.tip);
    }
    // The same right-click the Inspector's rows and the Parameters panel offer.
    if (ImGui::BeginPopupContextItem("##rowmenu")) {
        menuSubject(path);
        if (ImGui::MenuItem("Reset to default")) {
            noteParamEdit(engine, path, before, "Reset " + undoLabel);
            p->resetToDefault();
            flushParamEdit(engine);
        }
        if (ImGui::MenuItem("Key at current time")) {
            engine.recordKey(path);
        }
        ImGui::EndPopup();
    }
    // What the routes and the timeline made of it this frame, when that is not simply the base.
    if (field.type == world::FieldType::Float) {
        const float finalValue = p->finalComponent(0);
        if (std::abs(finalValue - p->baseComponent(0)) > 1e-4f) {
            ImGui::SameLine();
            ImGui::TextColored(kMuted, "= %.2f", static_cast<double>(finalValue));
        }
    }
    ImGui::PopID();
    if (changed) {
        noteParamEdit(engine, path, std::move(before), undoLabel);
    }
}

// ---- beat response --------------------------------------------------------------------------------

void EffectsSection::drawBeatResponse(app::Engine& engine, const world::EffectInstance& effect) {
    const world::EffectSchema* schema = world::effectSchema(effect.kind);
    if (schema == nullptr) {
        return;
    }
    const std::string target = beatResponseTarget(effect.id, *schema);
    if (target.empty()) {
        return;
    }
    params::IParameter* driven = engine.params().find(target);
    params::Modulator& modulator = engine.modulator();
    params::ModRoute* route = findRoute(modulator, beatResponseSource(), target);
    const float softRange = driven != nullptr ? std::max(driven->softMax(0) - driven->softMin(0), 1e-3f) : 1.0f;
    float amount = route != nullptr ? std::clamp(route->amount / softRange, 0.0f, 1.0f) : 0.0f;
    rowLabel("Beat response");
    if (ImGui::SliderFloat("##beat", &amount, 0.0f, 1.0f, amount > 0.0f ? "%.2f" : "off")) {
        if (amount <= 0.0f) {
            std::erase_if(modulator.routes(), [&](const params::ModRoute& r) {
                return r.source == beatResponseSource() && r.target == target;
            });
            engine.rebind();
        } else if (route != nullptr) {
            route->amount = beatResponseDepth(amount, softRange);
        } else {
            params::ModRoute r;
            r.source = beatResponseSource();
            r.target = target;
            r.amount = beatResponseDepth(amount, softRange);
            // A pulse with no decay is a step; these are the shape of a beat, not a gate.
            r.chain.attackMs = 10.0f;
            r.chain.decayMs = 260.0f;
            modulator.addRoute(r);
            engine.rebind();
        }
    }
    if (ImGui::IsItemHovered()) {
        tooltip("Writes a beat.pulse -> %s route.\nEdit or delete it in the Modulation panel like any other.",
                target.c_str());
    }
}

// ---- the field subscription (§68) -----------------------------------------------------------------

void EffectsSection::drawFlowPicker(app::Engine& engine, const world::EffectInstance& effect) {
    ImGui::SeparatorText("Field");
    const std::vector<std::string_view> published = engine.fieldBus().names();
    std::vector<std::string> storage(published.begin(), published.end());
    std::vector<const char*> labels;
    labels.push_back(kNoFieldLabel);
    int current = 0;
    for (std::size_t i = 0; i < storage.size(); ++i) {
        labels.push_back(storage[i].c_str());
        if (storage[i] == effect.flow.field) {
            current = static_cast<int>(i) + 1;
        }
    }
    // A name this scene does not publish is offered as its own marked entry and selected: showing
    // "nothing" would be a lie an artist could not act on.
    std::string dead;
    if (!effect.flow.field.empty() && current == 0) {
        dead = effect.flow.field + "  (not published by this scene)";
        labels.push_back(dead.c_str());
        current = static_cast<int>(labels.size()) - 1;
    }
    rowLabel("Follows");
    if (ImGui::Combo("##flowfield", &current, labels.data(), static_cast<int>(labels.size()))) {
        std::string chosen;
        if (current > 0 && current <= static_cast<int>(storage.size())) {
            chosen = storage[static_cast<std::size_t>(current) - 1];
        } else if (current > static_cast<int>(storage.size())) {
            chosen = effect.flow.field; // re-picking the dead entry changes nothing
        }
        if (chosen != effect.flow.field) {
            queueInstance("Field of " + effect.name, effect.id,
                          [chosen](world::EffectInstance& e) { e.flow.field = chosen; });
        }
    }
    if (ImGui::IsItemHovered()) {
        tooltip("The spatial field this effect's motion answers to: the world's wind, or\n"
                "the medium inside a cosmic vortex. Two effects that follow the same field\n"
                "move together because they are asking one question.");
    }
}

// ---- source / target ------------------------------------------------------------------------------

void EffectsSection::drawEndpoint(app::Engine& engine, const world::EffectInstance& effect, bool target) {
    const world::EffectSchema* schema = world::effectSchema(effect.kind);
    if (schema == nullptr) {
        return;
    }
    ImGui::PushID(target ? "target" : "source");
    const bool has = target ? schema->hasTarget(effect) : true;
    const world::EffectEndpoint ep = target ? schema->getTarget(effect) : schema->getSource(effect);
    const auto setSource = schema->setSource;
    const auto setTarget = schema->setTarget;
    const auto commit = [&](std::string what, bool hasValue, world::EffectEndpoint value) {
        queueInstance(what + " of " + effect.name, effect.id,
                      [target, hasValue, value, setSource, setTarget](world::EffectInstance& e) {
                          if (target) {
                              setTarget(e, hasValue, value);
                          } else {
                              setSource(e, value);
                          }
                      });
    };

    if (target) {
        bool want = has;
        if (ImGui::Checkbox("Aims at a target", &want)) {
            commit("Target", want, ep);
        }
        if (ImGui::IsItemHovered()) {
            tooltip("Something for a directional effect to point towards. The directions that need\n"
                    "one (source to target, camera to target, blended) use it.");
        }
        if (!has) {
            ImGui::PopID();
            return;
        }
    }

    // Kind.
    const std::span<const world::SourceKind> kinds = endpointKinds();
    int current = 0;
    std::vector<const char*> kindLabels;
    for (std::size_t i = 0; i < kinds.size(); ++i) {
        kindLabels.push_back(endpointKindLabel(kinds[i]));
        if (kinds[i] == ep.kind) {
            current = static_cast<int>(i);
        }
    }
    rowLabel(target ? "Target" : "Source");
    if (ImGui::Combo("##kind", &current, kindLabels.data(), static_cast<int>(kindLabels.size()))) {
        world::EffectEndpoint next = ep;
        next.kind = kinds[static_cast<std::size_t>(current)];
        commit(target ? "Target" : "Source", true, next);
    }
    if (ep.kind == world::SourceKind::Owner && owner_ != nullptr && owner_->isWorld()) {
        ImGui::TextColored(kWarning, "The World has no position to lend, so this never activates.");
    }

    // Name: a combo of the things that exist, never a text box -- a typo and a thing nobody has
    // made yet look the same in a text box.
    if (endpointNeedsName(ep.kind)) {
        std::vector<std::string> names;
        if (const scene::Composition* comp = engine.composition(); comp != nullptr) {
            if (ep.kind == world::SourceKind::Hero) {
                for (const world::HeroPoint& h : comp->heroes()) {
                    names.push_back(h.name);
                }
            } else {
                for (const auto& node : comp->nodes()) {
                    names.push_back(node->name);
                }
            }
        }
        const bool missing = !ep.name.empty() && std::find(names.begin(), names.end(), ep.name) == names.end();
        const std::string preview = ep.name.empty() ? std::string("(choose)")
                                    : missing       ? ep.name + "  (not in this scene)"
                                                    : ep.name;
        rowLabel(ep.kind == world::SourceKind::Hero ? "Hero" : "Node");
        if (ImGui::BeginCombo("##name", preview.c_str())) {
            for (const std::string& n : names) {
                if (ImGui::Selectable(n.c_str(), n == ep.name) && n != ep.name) {
                    world::EffectEndpoint next = ep;
                    next.name = n;
                    commit(target ? "Target" : "Source", true, next);
                }
            }
            if (names.empty()) {
                ImGui::TextDisabled("nothing of that kind in this scene");
            }
            ImGui::EndCombo();
        }
        if (missing) {
            ImGui::TextColored(kWarning, "'%s' is not in this scene.", ep.name.c_str());
        }
    }

    // Position, committed on release.
    const std::string key = effect.id + (target ? "/target" : "/source");
    if (endpointNeedsPosition(ep.kind)) {
        glm::vec3 v = dragKey_ == key + "/pos" ? dragValue_ : ep.position;
        rowLabel("Position");
        if (ImGui::DragFloat3("##pos", &v.x, 0.5f, 0.0f, 0.0f, "%.1f")) {
            dragKey_ = key + "/pos";
            dragValue_ = v;
        }
        if (ImGui::IsItemDeactivatedAfterEdit() && dragKey_ == key + "/pos") {
            world::EffectEndpoint next = ep;
            next.position = dragValue_;
            dragKey_.clear();
            commit(target ? "Target position" : "Source position", true, next);
        }
    }
    {
        float offset = dragKey_ == key + "/drop" ? dragValue_.x : ep.groundOffset;
        rowLabel("Drop to base");
        if (ImGui::SliderFloat("##drop", &offset, -20.0f, 40.0f, "%.2f m")) {
            dragKey_ = key + "/drop";
            dragValue_.x = offset;
        }
        if (ImGui::IsItemHovered()) {
            tooltip("Metres to drop the resolved origin, for a thing whose transform is\n"
                    "somewhere up its stem.");
        }
        if (ImGui::IsItemDeactivatedAfterEdit() && dragKey_ == key + "/drop") {
            world::EffectEndpoint next = ep;
            next.groundOffset = dragValue_.x;
            dragKey_.clear();
            commit(target ? "Target offset" : "Source offset", true, next);
        }
    }
    ImGui::PopID();
}

} // namespace avgen::ui
