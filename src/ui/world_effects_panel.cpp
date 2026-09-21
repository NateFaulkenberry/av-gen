#include "ui/world_effects_panel.hpp"

#include "ui/style.hpp"
#include "app/engine.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "scene/temporal_settings.hpp"
#include "ui/help_panel.hpp"
#include "ui/ui_logic.hpp"
#include "world/atmospheric_params.hpp"
#include "world/atmospherics.hpp"
#include "world/effect_params.hpp"
#include "world/effects.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::ui {
namespace {

// A button row that WRAPS instead of running off the edge of the panel.
//
// ADR-500's promise is that adding an effect means no edit in this file at all, and it kept
// that promise -- which is exactly how this broke. The registry loop below is `SameLine()`
// unconditionally, so every kind added since made the row wider with nobody editing it, and at
// six kinds it runs past the edge of a docked panel and the last button cannot be clicked. The
// cost of adding an effect was not zero; it was being paid somewhere nobody was looking.
//
// `GetContentRegionAvail` is read ONCE, in the constructor, before the first button. After a
// `SameLine` it reports what is left of the current line rather than the width of the region,
// so reading it per-iteration wraps after every button on a narrow panel -- which looks like a
// fix on a wide one and is the same defect on the panel that needed fixing.
class WrapRow {
public:
    WrapRow()
        : avail_(ImGui::GetContentRegionAvail().x),
          spacing_(ImGui::GetStyle().ItemSpacing.x) {}

    // Called immediately BEFORE the button, with the label that button will carry.
    void next(const char* label) {
        const float width =
            ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        if (!first_ && used_ + spacing_ + width <= avail_) {
            ImGui::SameLine();
            used_ += spacing_ + width;
        } else {
            used_ = width; // a new line: this button is all that is on it so far
        }
        first_ = false;
    }

private:
    float avail_ = 0.0f;
    float spacing_ = 0.0f;
    float used_ = 0.0f;
    bool first_ = true;
};

constexpr ImVec4 kMuted(0.6f, 0.62f, 0.66f, 1.0f);
constexpr ImVec4 kWarning(1.0f, 0.6f, 0.35f, 1.0f);

// One row: a label column wide enough for the longest label in this panel, then the widget. The same
// shape `CompositionPanel::rowLabel` uses -- copied rather than shared because it is four lines and
// the column width is per panel.
void rowLabel(const char* label) {
    ImGui::TextUnformatted(label);
    ImGui::SameLine(ImGui::CalcTextSize("Sparkle amount").x + ImGui::GetStyle().ItemSpacing.x * 2.0f);
    ImGui::SetNextItemWidth(-1.0f);
}

params::IParameter* find(app::Engine& engine, const std::string& prefix, const char* leaf) {
    return engine.params().find(prefix + leaf);
}

// A float slider over a parameter's own *soft* range, writing its base value. Base rather than
// final, because final is this frame's modulated value and writing it would be overwritten by the
// next route -- a slider that visibly snaps back is how a panel loses somebody's trust.
bool paramSlider(app::Engine& engine, const std::string& prefix, const char* leaf, const char* label,
                 const char* format = "%.2f", bool logarithmic = false) {
    params::IParameter* p = find(engine, prefix, leaf);
    if (p == nullptr) {
        return false;
    }
    float v = p->baseComponent(0);
    rowLabel(label);
    const ImGuiSliderFlags flags = logarithmic ? ImGuiSliderFlags_Logarithmic : 0;
    ImGui::PushID(leaf);
    const bool changed = ImGui::SliderFloat("##v", &v, p->softMin(0), p->softMax(0), format, flags);
    ImGui::PopID();
    if (changed) {
        p->setBaseComponent(0, v);
    }
    // What the routes and the timeline made of it this frame, when that is not simply the base.
    const float finalValue = p->finalComponent(0);
    if (std::abs(finalValue - p->baseComponent(0)) > 1e-4f) {
        ImGui::SameLine();
        ImGui::TextColored(kMuted, "= %.2f", static_cast<double>(finalValue));
    }
    return changed;
}

bool paramCheckbox(app::Engine& engine, const std::string& prefix, const char* leaf, const char* label) {
    params::IParameter* p = find(engine, prefix, leaf);
    if (p == nullptr) {
        return false;
    }
    bool v = p->baseComponent(0) >= 0.5f;
    ImGui::PushID(leaf);
    const bool changed = ImGui::Checkbox(label, &v);
    ImGui::PopID();
    if (changed) {
        p->setBaseComponent(0, v ? 1.0f : 0.0f);
    }
    return changed;
}

bool paramColor(app::Engine& engine, const std::string& prefix, const char* leaf, const char* label) {
    params::IParameter* p = find(engine, prefix, leaf);
    if (p == nullptr) {
        return false;
    }
    float rgb[3] = {p->baseComponent(0), p->baseComponent(1), p->baseComponent(2)};
    rowLabel(label);
    ImGui::PushID(leaf);
    const bool changed = ImGui::ColorEdit3("##c", rgb, ImGuiColorEditFlags_Float);
    ImGui::PopID();
    if (changed) {
        for (std::size_t i = 0; i < 3; ++i) {
            p->setBaseComponent(i, rgb[i]);
        }
    }
    return changed;
}

params::ModRoute* findRoute(params::Modulator& modulator, const std::string& source, const std::string& target) {
    for (params::ModRoute& r : modulator.routes()) {
        if (r.source == source && r.target == target) {
            return &r;
        }
    }
    return nullptr;
}

const char* const kSourceKindNames[] = {"world position", "scene node", "hero", "active camera",
                                        "the hero in focus"};
const char* const kActivationNames[] = {"always", "authored window", "while the camera travels",
                                        "while a hero is in focus"};
const char* const kPropagationNames[] = {"directional wave", "radial wave"};
const char* const kDirectionNames[] = {"explicit",       "source forward",   "camera forward",
                                       "camera velocity", "source to target", "camera to target",
                                       "blended"};

} // namespace

void WorldEffectsPanel::commit(app::Engine& engine, std::size_t index,
                              const std::function<void(world::WorldEffect&)>& edit) {
    const scene::Composition* comp = engine.composition();
    if (comp == nullptr) {
        return;
    }
    std::vector<world::WorldEffect> effects = comp->worldEffects();
    if (index >= effects.size()) {
        return;
    }
    // Every slider anybody has moved, carried onto the authored set before the structural change --
    // re-registration takes its defaults from these, so without this a change of activation would
    // silently reset the colour somebody spent ten minutes on.
    world::captureWorldEffectParameters(engine.worldEffectParameters(), effects);
    edit(effects[index]);
    if (auto ok = engine.setWorldEffects(std::move(effects)); !ok) {
        status_ = ok.error().message;
    } else {
        status_.clear();
    }
}

void WorldEffectsPanel::draw(app::Engine& engine) {
    helpHeader("reference/panels");

    const scene::Composition* comp = engine.composition();
    if (comp == nullptr) {
        ImGui::TextDisabled("World effects live on a scene; open or generate one first.");
        return;
    }

    // The authored set is what a save writes and what the structural controls edit. The live set the
    // engine resolves each frame is the same list with this frame's modulation on it, and reading
    // *that* here would show sliders jittering to the music.
    const std::vector<world::WorldEffect>& authored = comp->worldEffects();

    WrapRow propagationRow;
    propagationRow.next("Add camera beam");
    if (ImGui::Button("Add camera beam")) {
        std::vector<world::WorldEffect> next = authored;
        std::string name = "Camera Travel Beam";
        for (int n = 2; std::any_of(next.begin(), next.end(),
                                    [&](const world::WorldEffect& e) { return e.name == name; });
             ++n) {
            name = "Camera Travel Beam " + std::to_string(n);
        }
        next.push_back(world::cameraTravelBeam(name));
        if (auto ok = engine.setWorldEffects(std::move(next)); !ok) {
            status_ = ok.error().message;
        }
    }
    propagationRow.next("Add hero pulse");
    if (ImGui::Button("Add hero pulse")) {
        std::vector<world::WorldEffect> next = authored;
        std::string name = "Hero Pulse";
        for (int n = 2; std::any_of(next.begin(), next.end(),
                                    [&](const world::WorldEffect& e) { return e.name == name; });
             ++n) {
            name = "Hero Pulse " + std::to_string(n);
        }
        next.push_back(world::heroGroundPulse(name));
        if (auto ok = engine.setWorldEffects(std::move(next)); !ok) {
            status_ = ok.error().message;
        }
    }
    if (ImGui::IsItemHovered()) {
        tooltip("A ripple that spreads from whichever hero the directed camera is holding.\n"
                          "It needs a directed camera: with none, there is no cut to gate it on.");
    }

    if (!status_.empty()) {
        ImGui::TextColored(kWarning, "%s", status_.c_str());
    }
    if (authored.empty()) {
        // Not a `return`: ADR-230's section lives below, and a scene with an aurora and no
        // propagation effects is an ordinary scene rather than an empty panel.
        ImGui::TextDisabled("No propagation effects in this scene.");
    }
    // A cut is what a camera-gated effect activates against, and a scene with none will simply never
    // fire one. Said here rather than left to be discovered as "my effect does nothing".
    if (!authored.empty() && engine.shotSpans().empty()) {
        const bool gated = std::any_of(authored.begin(), authored.end(), [](const world::WorldEffect& e) {
            return e.activation == world::Activation::CameraTravel ||
                   e.activation == world::Activation::HeroFocus;
        });
        if (gated) {
            ImGui::TextColored(kWarning, "No directed camera: effects gated on the cut cannot fire.");
            if (ImGui::IsItemHovered()) {
                tooltip("Camera > Enable Auto-director, or set the activation to Always or a window.");
            }
        }
    }
    ImGui::Separator();

    pendingRemove_ = -1;
    for (std::size_t i = 0; i < authored.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        drawEffect(engine, authored[i], i);
        ImGui::PopID();
    }
    if (pendingRemove_ >= 0 && static_cast<std::size_t>(pendingRemove_) < authored.size()) {
        std::vector<world::WorldEffect> next = authored;
        next.erase(next.begin() + pendingRemove_);
        if (auto ok = engine.setWorldEffects(std::move(next)); !ok) {
            status_ = ok.error().message;
        }
        pendingRemove_ = -1;
    }
    drawAtmosphericSection(engine);
    drawTemporalSection(engine);
}

void WorldEffectsPanel::drawEffect(app::Engine& engine, const world::WorldEffect& authored, std::size_t index) {
    const std::string prefix = world::worldEffectParameterPrefix(authored.name);
    const bool open = ImGui::CollapsingHeader(authored.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen |
                                                                        ImGuiTreeNodeFlags_AllowOverlap);
    // The on/off switch on the header row, where it can be reached without opening the section.
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 74.0f);
    paramCheckbox(engine, prefix, "enabled", "##on");
    ImGui::SameLine();
    if (ImGui::SmallButton("x")) {
        pendingRemove_ = static_cast<int>(index);
    }
    if (ImGui::IsItemHovered()) {
        tooltip("Remove this effect and its parameters");
    }
    if (!open) {
        return;
    }
    ImGui::Indent();

    // ---- style ----
    const bool isBeam = authored.propagation.kind == world::PropagationKind::DirectionalWave;
    const auto styles = isBeam ? world::beamStyleNames() : world::pulseStyleNames();
    std::vector<const char*> styleNames;
    styleNames.reserve(styles.size());
    int current = -1;
    for (std::size_t s = 0; s < styles.size(); ++s) {
        styleNames.push_back(styles[s].data());
        if (authored.style == styles[s]) {
            current = static_cast<int>(s);
        }
    }
    rowLabel("Style");
    if (ImGui::Combo("##style", &current, styleNames.data(), static_cast<int>(styleNames.size())) &&
        current >= 0) {
        // A style is a shortcut through the same parameters, never a second way to control the
        // effect: it rewrites the authored values and the parameters take their defaults from those.
        const std::string_view style = styles[static_cast<std::size_t>(current)];
        commit(engine, index, [isBeam, style](world::WorldEffect& e) {
            if (isBeam) {
                world::applyBeamStyle(e, style);
            } else {
                world::applyPulseStyle(e, style);
            }
        });
        ImGui::Unindent();
        return; // the parameters below have just been re-registered; draw them next frame
    }
    if (ImGui::IsItemHovered()) {
        tooltip("Sets colour, intensity, sparkle and the wave's shape.\n"
                          "Every one of them stays editable below.");
    }

    // ---- the controls an author reaches for ----
    paramColor(engine, prefix, "color", "Colour");
    paramCheckbox(engine, prefix, "rainbow", "Rainbow");
    if (ImGui::IsItemHovered()) {
        tooltip("Replaces the hue with a procedural ramp travelling through the wave.\n"
                          "The colour above still sets how bright it is.");
    }
    paramSlider(engine, prefix, "intensity", "Intensity");
    paramSlider(engine, prefix, "width", "Width", "%.2fx");
    paramCheckbox(engine, prefix, "sparkle", "Sparkle");
    paramSlider(engine, prefix, "sparkleIntensity", "Sparkle amount");

    // ---- beat response ----
    //
    // A slider that owns one ordinary modulation route. Not a hidden audio hook: the route it writes
    // is in the Modulation panel, can be re-pointed, curved, enveloped or deleted there, and is
    // saved in the project like every other route.
    {
        params::IParameter* intensity = find(engine, prefix, "intensity");
        const std::string target = beatResponseTarget(authored.name);
        params::Modulator& modulator = engine.modulator();
        params::ModRoute* route = findRoute(modulator, beatResponseSource(), target);
        const float softRange =
            intensity != nullptr ? std::max(intensity->softMax(0) - intensity->softMin(0), 1e-3f) : 1.0f;
        float amount = route != nullptr ? std::clamp(route->amount / softRange, 0.0f, 1.0f) : 0.0f;
        rowLabel("Beat response");
        if (ImGui::SliderFloat("##beat", &amount, 0.0f, 1.0f, amount > 0.0f ? "%.2f" : "off")) {
            if (amount <= 0.0f) {
                auto& routes = modulator.routes();
                std::erase_if(routes, [&](const params::ModRoute& r) {
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
            tooltip("Writes a beat.pulse -> %s route.\nEdit or delete it in the Modulation panel "
                              "like any other.",
                              target.c_str());
        }
    }

    ImGui::TextColored(kMuted, "%s, %s, %s", kPropagationNames[static_cast<int>(authored.propagation.kind)],
                       kSourceKindNames[static_cast<int>(authored.source.kind)],
                       kActivationNames[static_cast<int>(authored.activation)]);

    if (ImGui::TreeNode("Advanced")) {
        drawAdvanced(engine, authored, index);
        ImGui::TreePop();
    }
    ImGui::Unindent();
    ImGui::Spacing();
}

void WorldEffectsPanel::drawAdvanced(app::Engine& engine, const world::WorldEffect& authored,
                                     std::size_t index) {
    const std::string prefix = world::worldEffectParameterPrefix(authored.name);

    // ---- what the effect is about ----
    //
    // Structural rather than parametric: these change which world position the effect resolves to
    // and when it exists, so they rewrite the authored set rather than a parameter. Combos rather
    // than free text, because every one of them is a closed vocabulary.
    ImGui::SeparatorText("Source and activation");
    {
        int kind = static_cast<int>(authored.propagation.kind);
        rowLabel("Propagation");
        if (ImGui::Combo("##prop", &kind, kPropagationNames, 2)) {
            commit(engine, index, [kind](world::WorldEffect& e) {
                e.propagation.kind = static_cast<world::PropagationKind>(kind);
            });
        }
        int source = static_cast<int>(authored.source.kind);
        rowLabel("Source");
        if (ImGui::Combo("##src", &source, kSourceKindNames, 5)) {
            commit(engine, index, [source](world::WorldEffect& e) {
                e.source.kind = static_cast<world::SourceKind>(source);
            });
        }
        if (authored.source.kind == world::SourceKind::Node ||
            authored.source.kind == world::SourceKind::Hero) {
            char buffer[128] = {};
            std::snprintf(buffer, sizeof(buffer), "%s", authored.source.name.c_str());
            rowLabel("Source name");
            if (ImGui::InputText("##srcname", buffer, sizeof(buffer),
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
                const std::string name = buffer;
                commit(engine, index, [name](world::WorldEffect& e) { e.source.name = name; });
            }
        }
        float offset = authored.source.groundOffset;
        rowLabel("Drop to base");
        if (ImGui::SliderFloat("##groundoffset", &offset, -20.0f, 40.0f, "%.2f m")) {
            commit(engine, index, [offset](world::WorldEffect& e) { e.source.groundOffset = offset; });
        }
        if (ImGui::IsItemHovered()) {
            tooltip("Metres to drop the resolved origin, for a source whose transform is\n"
                              "somewhere up its stem.");
        }
        int activation = static_cast<int>(authored.activation);
        rowLabel("Activation");
        if (ImGui::Combo("##act", &activation, kActivationNames, 4)) {
            commit(engine, index, [activation](world::WorldEffect& e) {
                e.activation = static_cast<world::Activation>(activation);
            });
        }
        if (authored.propagation.kind == world::PropagationKind::DirectionalWave) {
            int direction = static_cast<int>(authored.propagation.direction);
            rowLabel("Direction");
            if (ImGui::Combo("##dir", &direction, kDirectionNames, 7)) {
                commit(engine, index, [direction](world::WorldEffect& e) {
                    e.propagation.direction = static_cast<world::DirectionMode>(direction);
                    const bool needsTarget =
                        e.propagation.direction == world::DirectionMode::SourceToTarget ||
                        e.propagation.direction == world::DirectionMode::CameraToTarget ||
                        e.propagation.direction == world::DirectionMode::Blended;
                    // A direction that needs somewhere to point gets somewhere to point: the hero the
                    // cut is heading for. Refusing the edit instead would be a combo entry that only
                    // works if you already knew to set something else first.
                    if (needsTarget && !e.hasTarget) {
                        e.hasTarget = true;
                        e.target.kind = world::SourceKind::FocusHero;
                    }
                });
            }
        }
    }

    ImGui::SeparatorText("Shape");
    paramSlider(engine, prefix, "speed", "Speed", "%.1f m/s", true);
    paramSlider(engine, prefix, "range", "Range", "%.0f m", true);
    paramSlider(engine, prefix, "frontWidth", "Front width", "%.1f m");
    paramSlider(engine, prefix, "trailLength", "Trail", "%.1f m");
    paramSlider(engine, prefix, "falloff", "Trail falloff");
    paramSlider(engine, prefix, "startOffset", "Start offset", "%.1f m");
    paramSlider(engine, prefix, "verticalExtent", "Vertical reach", "%.1f m");
    paramSlider(engine, prefix, "ringCount", "Secondary ripples", "%.1f");
    paramSlider(engine, prefix, "beamRadius", "Beam radius", "%.0f m");
    if (ImGui::IsItemHovered()) {
        tooltip("0 makes a directional wave a moving plane; above 0 it is a beam this wide.");
    }

    ImGui::SeparatorText("Appearance");
    paramColor(engine, prefix, "edgeColor", "Edge colour");
    paramSlider(engine, prefix, "edgeIntensity", "Edge intensity");
    paramSlider(engine, prefix, "rainbowSpeed", "Rainbow speed", "%.2f Hz");
    paramSlider(engine, prefix, "rainbowScale", "Rainbow scale", "%.4f /m");
    paramSlider(engine, prefix, "rainbowSaturation", "Rainbow saturation");
    paramSlider(engine, prefix, "rainbowBrightness", "Rainbow brightness");

    ImGui::SeparatorText("Sparkle");
    paramSlider(engine, prefix, "sparkleDensity", "Sparkle density", "%.2f /m");
    paramSlider(engine, prefix, "sparkleSize", "Sparkle size");
    paramSlider(engine, prefix, "sparkleSpeed", "Sparkle speed", "%.2f Hz");

    ImGui::SeparatorText("Material response");
    paramSlider(engine, prefix, "response/ground", "Terrain");
    paramSlider(engine, prefix, "response/foliage", "Vegetation and rocks");
    paramSlider(engine, prefix, "response/surface", "Props and characters");
    paramSlider(engine, prefix, "response/emissive", "Existing glow");
    if (ImGui::IsItemHovered()) {
        tooltip("How much the wave amplifies emission a surface already had.");
    }

    ImGui::SeparatorText("Timing");
    paramSlider(engine, prefix, "delay", "Delay", "%.2f s");
    paramSlider(engine, prefix, "fadeIn", "Fade in", "%.2f s");
    paramSlider(engine, prefix, "fadeOut", "Fade out", "%.2f s");
    paramSlider(engine, prefix, "lifetime", "Lifetime", "%.2f s");
    if (ImGui::IsItemHovered()) {
        tooltip("0 means the effect lasts as long as its activation does.");
    }
    paramSlider(engine, prefix, "repeat", "Repeat every", "%.2f s");
    if (ImGui::IsItemHovered()) {
        tooltip("Restarts the wave front. 0 is one pass.\nThis is the knob a bar-length ripple "
                          "is made of.");
    }

    ImGui::TextColored(kMuted, "Every control here is the parameter worldfx/%s/...", authored.name.c_str());
}

// ---- ADR-230: the Atmospheric section --------------------------------------------------------------
//
// The same split the propagation section uses, for the same reason: on/off, preset, the two or three
// colours, and the handful of numbers somebody reaches for, with everything else behind Advanced.
// Every slider writes the *base* of an ordinary `atmos/...` parameter, so a keyframe, a cue, a
// sequencer event and a route all keep working and the Parameters panel shows the same numbers.

namespace {

const char* const kGroundGlowNames[] = {"off", "subtle", "strong"};

// ADR-387: walks a declared row list. The panel and `tests/unit/test_world_effects_panel.cpp` ask
// the same question of the same data, which is the thing ADR-382's defect was missing -- there, the
// path arithmetic lived inside the draw call and no test could reach it.
// ADR-500. The atmospheric family's rows are its schema's. This draws one page of them.
//
// The row table and the registration are now the SAME list, which is what closes ADR-392's
// headline defect: a panel row naming a parameter nobody registered drew an empty box,
// indistinguishable from "this scene has no such effect", and the owner reported that class of
// defect twice. There is no longer a second list to be five characters wrong in.
void drawSchemaRows(app::Engine& engine, const std::string& prefix, const world::EffectSchema& schema,
                    world::FieldPage page) {
    for (const world::EffectField& field : schema.fields) {
        if (field.page != page) {
            continue;
        }
        if (field.section[0] != '\0') {
            ImGui::SeparatorText(field.section);
        }
        switch (field.type) {
        case world::FieldType::Color:
            paramColor(engine, prefix, field.leaf, field.label);
            break;
        case world::FieldType::Bool:
            paramCheckbox(engine, prefix, field.leaf, field.label);
            break;
        case world::FieldType::Float:
            paramSlider(engine, prefix, field.leaf, field.label,
                        field.format[0] != '\0' ? field.format : "%.2f", field.logarithmic);
            break;
        }
        if (field.tip[0] != '\0' && ImGui::IsItemHovered()) {
            tooltipUnformatted(field.tip);
        }
    }
}

// The shared rows -- ground illumination and the field subscription -- drawn by leaf name from
// `sharedEffectFields()` so that the panel and the registrar name them once between them.
void drawSharedRow(app::Engine& engine, const std::string& prefix, const char* leaf) {
    for (const world::EffectField& field : world::sharedEffectFields()) {
        if (std::string_view(field.leaf) != leaf) {
            continue;
        }
        switch (field.type) {
        case world::FieldType::Color: paramColor(engine, prefix, field.leaf, field.label); break;
        case world::FieldType::Bool: paramCheckbox(engine, prefix, field.leaf, field.label); break;
        case world::FieldType::Float:
            paramSlider(engine, prefix, field.leaf, field.label,
                        field.format[0] != '\0' ? field.format : "%.2f", field.logarithmic);
            break;
        }
        if (field.tip[0] != '\0' && ImGui::IsItemHovered()) {
            tooltipUnformatted(field.tip);
        }
        return;
    }
}

void drawEffectRows(app::Engine& engine, const std::string& prefix, std::span<const EffectRow> rows) {
    for (const EffectRow& r : rows) {
        if (!r.section.empty()) {
            const std::string section(r.section);
            ImGui::SeparatorText(section.c_str());
        }
        const std::string leaf(r.leaf);
        const std::string label(r.label);
        if (r.color) {
            paramColor(engine, prefix, leaf.c_str(), label.c_str());
            continue;
        }
        const std::string format(r.format);
        paramSlider(engine, prefix, leaf.c_str(), label.c_str(),
                    format.empty() ? "%.2f" : format.c_str(), r.logarithmic);
        if (!r.tip.empty() && ImGui::IsItemHovered()) {
            tooltipUnformatted(std::string(r.tip).c_str());
        }
    }
}
const char* const kSkyAnchorNames[] = {"a fixed world point", "the camera"};

// §68. The word for "subscribed to nothing", first in the combo so index 0 is always the empty
// subscription whatever the scene publishes.
const char* const kNoFieldLabel = "nothing (still air)";

} // namespace

void WorldEffectsPanel::commitAtmospheric(app::Engine& engine, std::size_t index,
                                          const std::function<void(world::AtmosphericEffect&)>& edit) {
    const scene::Composition* comp = engine.composition();
    if (comp == nullptr) {
        return;
    }
    std::vector<world::AtmosphericEffect> effects = comp->atmosphericEffects();
    if (index >= effects.size()) {
        return;
    }
    // As `commit` above: every slider anybody has moved, carried onto the authored set before the
    // structural change, because re-registration takes its defaults from these.
    world::captureAtmosphericParameters(engine.atmosphericParameters(), effects);
    edit(effects[index]);
    if (auto ok = engine.setAtmosphericEffects(std::move(effects)); !ok) {
        status_ = ok.error().message;
    } else {
        status_.clear();
    }
}

void WorldEffectsPanel::drawAtmosphericSection(app::Engine& engine) {
    const scene::Composition* comp = engine.composition();
    if (comp == nullptr) {
        return;
    }
    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGui::SeparatorText("Atmospheric");

    const std::vector<world::AtmosphericEffect>& authored = comp->atmosphericEffects();

    // ADR-420, §68. Every subscription that names a field this scene does not publish, said out
    // loud where an artist is standing.
    //
    // The engine already logs this once per name, and a CPU log is a diagnostic for whoever is
    // reading a terminal -- which is never the person whose aurora has stopped moving. The whole
    // point of the report is that a name resolving to nothing must not be indistinguishable from a
    // setting nobody used, and it is indistinguishable from exactly that until it is on screen.
    //
    // Drawn at the top of the section rather than inside each effect because the question is "what
    // in this sky is asking for something that is not here", which is one question about the scene.
    // The per-effect combo says the same thing again in the place where it can be fixed.
    {
        std::vector<std::string> names;
        std::vector<world::fields::Subscription> subs;
        names.reserve(authored.size());
        subs.reserve(authored.size());
        for (const world::AtmosphericEffect& e : authored) {
            names.push_back(e.name);
            subs.push_back(e.flow);
        }
        const std::vector<world::fields::DeadSubscription> dead =
            engine.fieldBus().unresolved(names, subs);
        for (const world::fields::DeadSubscription& d : dead) {
            ImGui::TextColored(kWarning, "'%s' follows a field called '%s', which this scene does "
                                         "not publish -- it is standing still.",
                               d.subscriber.c_str(), d.field.c_str());
        }
    }

    // A unique name, because a name is half of a parameter path and two effects sharing one is two
    // things writing the same path.
    const auto unique = [&](std::string base) {
        std::string name = base;
        for (int n = 2; std::any_of(authored.begin(), authored.end(),
                                    [&](const world::AtmosphericEffect& e) { return e.name == name; });
             ++n) {
            name = base + " " + std::to_string(n);
        }
        return name;
    };
    const auto append = [&](world::AtmosphericEffect effect) {
        const std::string name = effect.name;
        std::vector<world::AtmosphericEffect> next = authored;
        next.push_back(std::move(effect));
        if (auto ok = engine.setAtmosphericEffects(std::move(next)); !ok) {
            status_ = ok.error().message;
            return;
        }
        // ADR-392. The family's premise is that audio reaches an effect as an ordinary modulation
        // route rather than as a hook, and `defaultAtmosphericRoutes` is what implements it -- but
        // until now nothing called it, so "Add aurora" made an aurora that answered nothing and six
        // routes somebody had to author by hand. The routes are ordinary: they appear in the
        // Modulation panel, can be re-pointed, curved or deleted there, and are saved in the project
        // like every other route. Attached after the effect, because registration happens in the
        // call above and a route bound before its target exists binds to nothing.
        engine.addDefaultAtmosphericRoutes(name);
    };

    // ADR-500: one button per declared kind, from the registry. Adding a kind used to mean adding
    // a button, a tooltip and a factory call here; it now means nothing here at all, which is the
    // difference between "seventy effects" being a list of edits and being a list of files.
    //
    // There was a SECOND "Add cosmic ocean" button below this loop, hand-written with its own
    // factory call and its own copy of the tooltip. It predates the registry: when ADR-390 shipped
    // the Cosmic Ocean it had to add its own button here, and when the kind was ported onto the
    // ADR-500 registry the loop above started drawing it from `schema->addLabel` -- so the panel
    // offered the same kind twice, and the two buttons did not even take the same route to the
    // scene. Both are gone now with the kind itself, but the shape of the defect is worth
    // keeping: a hand-written button for a kind the registry already draws is invisible until
    // somebody counts the buttons. A kind with a factory in the registry is drawn by the loop,
    // and that is the whole rule.
    {
        WrapRow row;
        for (const world::EffectSchema* schema : world::effectSchemas()) {
            if (schema->factory == nullptr) {
                continue;
            }
            row.next(schema->addLabel);
            if (ImGui::Button(schema->addLabel)) {
                append(world::makeAtmosphericEffect(schema->kind, unique(schema->displayName)));
            }
            if (schema->addTip[0] != '\0' && ImGui::IsItemHovered()) {
                tooltipUnformatted(schema->addTip);
            }
        }
    }

    if (authored.empty()) {
        ImGui::TextDisabled("No atmospheric effects in this scene.");
        return;
    }

    pendingAtmosphericRemove_ = -1;
    for (std::size_t i = 0; i < authored.size(); ++i) {
        ImGui::PushID(static_cast<int>(10000 + i)); // clear of the propagation list's ids above
        drawAtmospheric(engine, authored[i], i);
        ImGui::PopID();
    }
    if (pendingAtmosphericRemove_ >= 0 &&
        static_cast<std::size_t>(pendingAtmosphericRemove_) < authored.size()) {
        std::vector<world::AtmosphericEffect> next = authored;
        next.erase(next.begin() + pendingAtmosphericRemove_);
        if (auto ok = engine.setAtmosphericEffects(std::move(next)); !ok) {
            status_ = ok.error().message;
        }
        pendingAtmosphericRemove_ = -1;
    }
}

void WorldEffectsPanel::drawAtmospheric(app::Engine& engine, const world::AtmosphericEffect& authored,
                                        std::size_t index) {
    const std::string prefix = world::atmosphericParameterPrefix(authored.name);
    const world::EffectSchema* schema = world::effectSchema(authored.kind);
    const bool open = ImGui::CollapsingHeader(authored.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen |
                                                                        ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 74.0f);
    paramCheckbox(engine, prefix, "enabled", "##on");
    ImGui::SameLine();
    if (ImGui::SmallButton("x")) {
        pendingAtmosphericRemove_ = static_cast<int>(index);
    }
    if (!open) {
        return;
    }
    ImGui::Indent();

    // A kind the registry does not declare. Said out loud, in the place an artist is looking, rather
    // than drawn as an empty section -- ADR-392's defect was precisely a section that looked like an
    // effect with nothing in it. `checkRegistry` has already named it in the suite.
    if (schema == nullptr) {
        ImGui::TextColored(kWarning, "This effect's kind is not registered, so nothing can be "
                                     "changed here and nothing renders it.");
        ImGui::Unindent();
        return;
    }

    // ---- the medium slot, said where the artist is looking -------------------------------------
    //
    // The volumetric march has ONE medium slot, for the cost ADR-374 measured: a single vortex is
    // +5.5 ms of a 13.5 ms frame, the most expensive term in the scene. So a cosmic vortex and a
    // fog bank compete for it, and the second one in the list does not draw.
    //
    // That limit was already deliberate and already counted -- `AtmosphericCounts::dropped` --
    // and `volumetric_fog_effect.cpp` says in as many words that it is "counted ... and reported,
    // a stated limit, not a silent no-op". **Nothing read that counter.** Not the panel, not a log,
    // not the overlay: the owner turned fog on, the vortex vanished, and the engine's only comment
    // on it was a number nobody printed. A limit nobody is told about is indistinguishable from a
    // bug, which is the whole of ADR-421's "a control that does nothing teaches an artist that the
    // system is broken".
    //
    // Computed here from the authored list rather than plumbed from the frame, because the panel
    // already has everything the question needs and `AtmosphericFrame` is memcmp'd against a
    // `static_assert`ed size -- a string on it would be the wrong shape for the wrong reason.
    if (schema->resolve.bucket == world::EffectBucket::Vortex && authored.enabled) {
        std::size_t ahead = 0;
        for (const world::AtmosphericEffect& other : engine.atmosphericEffects()) {
            if (&other == &authored) {
                break;
            }
            const world::EffectSchema* s2 = world::effectSchema(other.kind);
            if (other.enabled && s2 != nullptr && s2->resolve.bucket == world::EffectBucket::Vortex) {
                ++ahead;
            }
        }
        if (ahead > 0) {
            ImGui::TextColored(kWarning, "Not drawn: the volumetric march has one medium slot and "
                                         "an effect above this one is using it.");
            if (ImGui::IsItemHovered()) {
                tooltipUnformatted(
                    "A cosmic vortex and a fog bank are the same placed medium, marched by the\n"
                    "same pass, and there is room for one of them. The first enabled one in this\n"
                    "list wins; disable it, or disable this one, to see the other.\n\n"
                    "The limit is the cost: one medium is +5.5 ms of a 13.5 ms frame (ADR-374),\n"
                    "which is the most expensive term in the scene.");
            }
        }
    }

    // ---- preset ----
    const std::span<const std::string_view> styles = world::effectStyleNames(authored.kind);
    std::vector<const char*> styleNames;
    int current = -1;
    for (std::size_t s = 0; s < styles.size(); ++s) {
        styleNames.push_back(styles[s].data());
        if (authored.style == styles[s]) {
            current = static_cast<int>(s);
        }
    }
    if (!styleNames.empty()) {
        rowLabel("Preset");
        if (ImGui::Combo("##preset", &current, styleNames.data(), static_cast<int>(styleNames.size())) &&
            current >= 0) {
            // A preset is a shortcut through the same parameters, never a second way to control the
            // effect: it rewrites the authored values and the parameters take their defaults from
            // those.
            const std::string_view style = styles[static_cast<std::size_t>(current)];
            const world::AtmosphereKind kind = authored.kind;
            commitAtmospheric(engine, index, [kind, style](world::AtmosphericEffect& e) {
                world::applyEffectStyle(e, kind, style);
            });
            ImGui::Unindent();
            return; // the parameters below have just been re-registered; draw them next frame
        }
    }

    drawSchemaRows(engine, prefix, *schema, world::FieldPage::Main);

    // ---- ground illumination (section 6) ----
    // The mode is a structural choice rather than a parameter: automating "should this light the
    // valley" is not a thing anybody wants, while automating *how much* is -- so the three words are
    // a combo and the intensity below is an ordinary modulatable parameter.
    //
    // Offered only by the kinds that declare one. ADR-387: a vortex does not, because the glow is a
    // pool on terrain and the funnel is under the island with no terrain beneath it; its light on
    // the world is `spill`. A combo that changed nothing would be worse than no combo.
    int ground = static_cast<int>(authored.ground.mode);
    if (schema->groundGlow) {
        rowLabel("Ground glow");
        if (ImGui::Combo("##ground", &ground, kGroundGlowNames, 3)) {
            commitAtmospheric(engine, index, [ground](world::AtmosphericEffect& e) {
                e.ground.mode = static_cast<world::GroundGlow>(std::clamp(ground, 0, 2));
            });
            ImGui::Unindent();
            return;
        }
        if (authored.ground.mode != world::GroundGlow::Off) {
            drawSharedRow(engine, prefix, "groundColor");
            drawSharedRow(engine, prefix, "groundIntensity");
        }
    }

    // ---- beat response ----
    // The same construction the propagation section uses, and for the same reason: this writes an
    // ordinary modulation route, visible and editable in the Modulation panel, rather than a hidden
    // audio hook inside the effect.
    if (schema->beatLeaf[0] != '\0') {
        params::IParameter* target = find(engine, prefix, schema->beatLeaf);
        const std::string path = atmosphericBeatTarget(authored.name, authored.kind);
        params::Modulator& modulator = engine.modulator();
        params::ModRoute* route = findRoute(modulator, beatResponseSource(), path);
        const float softRange =
            target != nullptr ? std::max(target->softMax(0) - target->softMin(0), 1e-3f) : 1.0f;
        float amount = route != nullptr ? std::clamp(route->amount / softRange, 0.0f, 1.0f) : 0.0f;
        rowLabel("Beat response");
        if (ImGui::SliderFloat("##beat", &amount, 0.0f, 1.0f, amount > 0.0f ? "%.2f" : "off")) {
            if (amount <= 0.0f) {
                auto& routes = modulator.routes();
                std::erase_if(routes, [&](const params::ModRoute& r) {
                    return r.source == beatResponseSource() && r.target == path;
                });
                engine.rebind();
            } else if (route != nullptr) {
                route->amount = beatResponseDepth(amount, softRange);
            } else {
                params::ModRoute r;
                r.source = beatResponseSource();
                r.target = path;
                r.amount = beatResponseDepth(amount, softRange);
                // A pulse with no decay is a step; these are the shape of a beat, not a gate.
                r.chain.attackMs = 10.0f;
                r.chain.decayMs = 260.0f;
                modulator.addRoute(r);
                engine.rebind();
            }
        }
    }

    if (ImGui::TreeNode("Advanced")) {
        drawAtmosphericAdvanced(engine, authored, index);
        ImGui::TreePop();
    }
    ImGui::Unindent();
}

void WorldEffectsPanel::drawAtmosphericAdvanced(app::Engine& engine,
                                                const world::AtmosphericEffect& authored,
                                                std::size_t index) {
    const std::string prefix = world::atmosphericParameterPrefix(authored.name);
    const world::EffectSchema* schema = world::effectSchema(authored.kind);
    if (schema == nullptr) {
        return;
    }

    // ---- §68: which field this effect follows ----
    //
    // The rule the owner states is that if it is visible in the picture an artist must be able to
    // find it and change it, and that findable is not reachable-in-principle. A subscription that
    // could only be typed into a scene file would be exactly the sixteen-parameter travelling band
    // of light ADR-375 was written about. So: a combo of the names this scene actually publishes --
    // never a text box, because a typo in a text box and a field nobody has made yet look the same
    // -- and the one slider, from the shared rows so that the panel and the registrar name it once
    // between them (ADR-382).
    {
        ImGui::SeparatorText("Field");
        const world::fields::FieldBus& bus = engine.fieldBus();
        const std::vector<std::string_view> published = bus.names();

        std::vector<const char*> labels;
        std::vector<std::string> storage;
        labels.reserve(published.size() + 1);
        storage.reserve(published.size());
        labels.push_back(kNoFieldLabel);
        int current = 0;
        for (std::size_t i = 0; i < published.size(); ++i) {
            storage.emplace_back(published[i]);
            if (storage.back() == authored.flow.field) {
                current = static_cast<int>(i) + 1;
            }
        }
        for (const std::string& name : storage) {
            labels.push_back(name.c_str());
        }

        // The case that must not be silent: the effect names a field this scene does not publish.
        // Showing "nothing" would be a lie an artist could not act on, so the dead name is offered
        // as its own entry, marked, and selected -- which is how somebody discovers that the vortex
        // they subscribed to has been renamed or switched off.
        std::string dead;
        if (!authored.flow.field.empty() && current == 0) {
            dead = authored.flow.field + "  (not published by this scene)";
            labels.push_back(dead.c_str());
            current = static_cast<int>(labels.size()) - 1;
        }

        rowLabel("Follows");
        if (ImGui::Combo("##flowfield", &current, labels.data(), static_cast<int>(labels.size()))) {
            std::string chosen;
            if (current > 0 && current <= static_cast<int>(storage.size())) {
                chosen = storage[static_cast<std::size_t>(current) - 1];
            }
            commitAtmospheric(engine, index, [chosen](world::AtmosphericEffect& e) {
                e.flow.field = chosen;
            });
            return;
        }
        if (ImGui::IsItemHovered()) {
            tooltip("The spatial field this effect's motion answers to: the world's wind, or\n"
                    "the medium inside a cosmic vortex. Two effects that follow the same field\n"
                    "move together because they are asking one question, not because somebody\n"
                    "matched two speeds by hand.");
        }
        if (!dead.empty()) {
            ImGui::TextColored(kMuted, "'%s' is not published by this scene -- nothing follows it.",
                               authored.flow.field.c_str());
        }
        drawSharedRow(engine, prefix, "flowInfluence");
    }

    ImGui::SeparatorText("Lifetime");
    int activation = static_cast<int>(authored.activation);
    rowLabel("Activation");
    if (ImGui::Combo("##act", &activation, kActivationNames, 4)) {
        commitAtmospheric(engine, index, [activation](world::AtmosphericEffect& e) {
            e.activation = static_cast<world::Activation>(std::clamp(activation, 0, 3));
        });
        return;
    }
    if (authored.activation == world::Activation::Window) {
        drawSharedRow(engine, prefix, "windowStart");
        drawSharedRow(engine, prefix, "windowSeconds");
    }
    drawSharedRow(engine, prefix, "delay");
    drawSharedRow(engine, prefix, "fadeIn");
    drawSharedRow(engine, prefix, "fadeOut");
    drawSharedRow(engine, prefix, "lifetime");
    drawSharedRow(engine, prefix, "repeat");

    // The anchor combo, for the kinds that have one. It stays inline rather than being a row
    // because it writes a `SkyAnchor` enum on the effect rather than a parameter -- putting it in
    // the row table would put a leaf there that registration does not produce, which is the
    // opposite of the point (ADR-392).
    if (schema->anchorSection != nullptr && schema->getAnchor != nullptr) {
        ImGui::SeparatorText(schema->anchorSection);
        int anchor = static_cast<int>(schema->getAnchor(authored));
        rowLabel("Anchored to");
        if (ImGui::Combo("##anchor", &anchor, kSkyAnchorNames, 2)) {
            const world::EffectSchema* captured = schema;
            commitAtmospheric(engine, index, [anchor, captured](world::AtmosphericEffect& e) {
                captured->setAnchor(e, static_cast<world::SkyAnchor>(std::clamp(anchor, 0, 1)));
            });
            return;
        }
        if (ImGui::IsItemHovered()) {
            tooltip("A world anchor gives real parallax: the effect slides against the stars\n"
                    "as the camera travels, and it can leave frame. A camera anchor keeps its\n"
                    "bearing however far the camera goes.");
        }
    }

    drawSchemaRows(engine, prefix, *schema, world::FieldPage::Advanced);

    if (schema->groundGlow && authored.ground.mode != world::GroundGlow::Off) {
        drawSharedRow(engine, prefix, "groundRadius");
        drawSharedRow(engine, prefix, "groundFalloff");
    }

    ImGui::TextColored(kMuted, "Every control here is the parameter atmos/%s/...", authored.name.c_str());
}


// ---- ADR-410: temporal media -------------------------------------------------------------------
void WorldEffectsPanel::drawTemporalSection(app::Engine& engine) {
    ImGui::SeparatorText("Reality / Temporal / Digital");

    const std::string prefix = scene::temporalParameterPrefix(scene::TemporalEffectKind::FrameEcho);
    const bool open = ImGui::CollapsingHeader("Frame echo", ImGuiTreeNodeFlags_DefaultOpen);
    if (ImGui::IsItemHovered()) {
        // The limitation an author needs at the moment of choosing, not in an ADR they will never
        // open (ADR-410). A mosh or an echo over fog advecting with the surface behind it looks
        // exactly like a bug, and finding out afterwards costs an afternoon.
        tooltipUnformatted(
            "Reaches back over previous frames, so a moving object leaves a trail of where it was.\n\n"
            "Describes OPAQUE surfaces only. Fog, aurora and the vortex are composited after the\n"
            "scene pass and carry no motion of their own, so an echo over them follows whatever\n"
            "solid surface is behind them rather than the effect itself.");
    }
    if (!open) {
        return;
    }

    paramCheckbox(engine, prefix, "enabled", "Enabled");

    // The disclosure ADR-410 turns on. Phrased as what to DO -- "settling, 3 of 8" tells somebody
    // the picture is on its way; "not what will be rendered" tells them only that they are stuck.
    // The stuck wording is kept for the case that genuinely is stuck.
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

    drawEffectRows(engine, prefix, temporalEchoRows());
}

} // namespace avgen::ui
