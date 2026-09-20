#include "ui/world_effects_panel.hpp"

#include "ui/style.hpp"
#include "app/engine.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
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
    ImGui::SameLine();
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
        std::vector<world::AtmosphericEffect> next = authored;
        next.push_back(std::move(effect));
        if (auto ok = engine.setAtmosphericEffects(std::move(next)); !ok) {
            status_ = ok.error().message;
        }
    };

    if (ImGui::Button("Add comet")) {
        append(world::bioluminescentComet(unique("Bioluminescent Comet")));
    }
    if (ImGui::IsItemHovered()) {
        tooltip("A celestial object on a great-circle arc across the sky.\n"
                          "It arrives as an event with an authored window, so it launches once;\n"
                          "give it a repeat interval to make it a shower.");
    }
    ImGui::SameLine();
    if (ImGui::Button("Add aurora")) {
        append(world::glowmereAurora(unique("Aurora")));
    }
    if (ImGui::IsItemHovered()) {
        tooltip("Curtains rising from the horizon, shaped by the audio spectrum.\n"
                          "Always on and fading up: an aurora is scenery that breathes.");
    }
    ImGui::SameLine();
    if (ImGui::Button("Add vortex")) {
        append(world::cosmicVortex(unique("Cosmic Vortex")));
    }
    if (ImGui::IsItemHovered()) {
        tooltip("A turning funnel of luminous medium, drawn inside the volumetric march.\n"
                          "It is placed in the world rather than on the sky dome, and it is the\n"
                          "only atmospheric that a particle system can name as its attractor.");
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
    const bool isComet = authored.kind == world::AtmosphereKind::Comet;
    const bool isVortex = authored.kind == world::AtmosphereKind::Vortex;
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

    // ---- preset ----
    const auto styles = isComet    ? world::cometStyleNames()
                        : isVortex ? world::vortexStyleNames()
                                   : world::auroraStyleNames();
    std::vector<const char*> styleNames;
    int current = -1;
    for (std::size_t s = 0; s < styles.size(); ++s) {
        styleNames.push_back(styles[s].data());
        if (authored.style == styles[s]) {
            current = static_cast<int>(s);
        }
    }
    rowLabel("Preset");
    if (ImGui::Combo("##preset", &current, styleNames.data(), static_cast<int>(styleNames.size())) &&
        current >= 0) {
        // A preset is a shortcut through the same parameters, never a second way to control the
        // effect: it rewrites the authored values and the parameters take their defaults from those.
        const std::string_view style = styles[static_cast<std::size_t>(current)];
        commitAtmospheric(engine, index, [isComet, isVortex, style](world::AtmosphericEffect& e) {
            if (isComet) {
                world::applyCometStyle(e, style);
            } else if (isVortex) {
                world::applyVortexStyle(e, style);
            } else {
                world::applyAuroraStyle(e, style);
            }
        });
        ImGui::Unindent();
        return; // the parameters below have just been re-registered; draw them next frame
    }

    if (isVortex) {
        // ADR-387. `density` is extinction and `emission` is light -- ADR-374 is the whole reason
        // they are two knobs, so they are labelled as two different things rather than as
        // "opacity" and "glow", which is how they got confused in the first place. The rows are
        // `ui::vortexRows()` rather than a page of calls, so that a test can ask the same question
        // this code asks: does every leaf named here exist?
        // Each row carries its own tooltip (ADR-388); the panel used to attach one to whatever it
        // had drawn last, which meant appending a row moved somebody else's explanation.
        drawEffectRows(engine, prefix, vortexRows());
    } else if (isComet) {
        paramColor(engine, prefix, "coreColor", "Core colour");
        paramColor(engine, prefix, "tailColor", "Tail colour");
        paramSlider(engine, prefix, "coreIntensity", "Core brightness");
        paramSlider(engine, prefix, "headSize", "Head size", "%.0f m");
        paramSlider(engine, prefix, "tailLength", "Tail length", "%.0f m");
        paramSlider(engine, prefix, "tailWidth", "Tail width", "%.0f m");
        paramCheckbox(engine, prefix, "sparkle", "Sparkling fragments");
        paramCheckbox(engine, prefix, "rainbow", "Rainbow");
        paramSlider(engine, prefix, "travelSeconds", "Crossing", "%.1f s");
    } else {
        paramColor(engine, prefix, "lowColor", "Base colour");
        paramColor(engine, prefix, "midColor", "Middle colour");
        paramColor(engine, prefix, "topColor", "Top colour");
        paramSlider(engine, prefix, "intensity", "Brightness");
        paramSlider(engine, prefix, "curtainHeight", "Height", "%.0f m");
        paramSlider(engine, prefix, "curtains", "Curtains", "%.0f");
        paramSlider(engine, prefix, "flowSpeed", "Flow");
        paramSlider(engine, prefix, "audioSensitivity", "Audio response");
        paramSlider(engine, prefix, "spectrumShape", "Spectrum shape");
        paramCheckbox(engine, prefix, "rainbow", "Colour cycle");
    }

    // ---- ground illumination (section 6) ----
    // The mode is a structural choice rather than a parameter: automating "should this light the
    // valley" is not a thing anybody wants, while automating *how much* is -- so the three words are
    // a combo and the intensity below is an ordinary modulatable parameter.
    // ADR-387: not offered for a vortex. The ground glow is a pool on terrain, and the vortex is
    // under the island with no terrain beneath it; its light on the world is `spill`, above. A combo
    // that changed nothing would be worse than no combo.
    int ground = static_cast<int>(authored.ground.mode);
    if (!isVortex) {
        rowLabel("Ground glow");
        if (ImGui::Combo("##ground", &ground, kGroundGlowNames, 3)) {
            commitAtmospheric(engine, index, [ground](world::AtmosphericEffect& e) {
                e.ground.mode = static_cast<world::GroundGlow>(std::clamp(ground, 0, 2));
            });
            ImGui::Unindent();
            return;
        }
    }
    if (!isVortex && authored.ground.mode != world::GroundGlow::Off) {
        paramColor(engine, prefix, "groundColor", "Glow colour");
        paramSlider(engine, prefix, "groundIntensity", "Glow amount");
    }

    // ---- beat response ----
    // The same construction the propagation section uses, and for the same reason: this writes an
    // ordinary modulation route, visible and editable in the Modulation panel, rather than a hidden
    // audio hook inside the effect.
    {
        const char* leaf = isComet    ? "coreIntensity"
                           : isVortex ? "emission"
                                      : "edgeBrightness";
        params::IParameter* target = find(engine, prefix, leaf);
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
    const bool isComet = authored.kind == world::AtmosphereKind::Comet;
    const bool isVortex = authored.kind == world::AtmosphereKind::Vortex;

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
        paramSlider(engine, prefix, "windowStart", "Window start", "%.2f s");
        paramSlider(engine, prefix, "windowSeconds", "Window length", "%.2f s");
    }
    paramSlider(engine, prefix, "delay", "Delay", "%.2f s");
    paramSlider(engine, prefix, "fadeIn", "Fade in", "%.2f s");
    paramSlider(engine, prefix, "fadeOut", "Fade out", "%.2f s");
    paramSlider(engine, prefix, "lifetime", "Lifetime", "%.2f s");
    paramSlider(engine, prefix, "repeat", "Repeat every", "%.2f s");

    if (isVortex) {
        drawEffectRows(engine, prefix, vortexAdvancedRows());
    } else if (isComet) {
        ImGui::SeparatorText("Trajectory");
        int anchor = static_cast<int>(authored.comet.path.anchor);
        rowLabel("Anchored to");
        if (ImGui::Combo("##anchor", &anchor, kSkyAnchorNames, 2)) {
            commitAtmospheric(engine, index, [anchor](world::AtmosphericEffect& e) {
                e.comet.path.anchor = static_cast<world::SkyAnchor>(std::clamp(anchor, 0, 1));
            });
            return;
        }
        if (ImGui::IsItemHovered()) {
            tooltip("A world anchor gives the comet real parallax: it slides against the\n"
                              "stars as the camera travels, and it can leave frame. A camera anchor\n"
                              "keeps its bearing however far the camera goes.");
        }
        paramSlider(engine, prefix, "startAzimuth", "Start bearing", "%.0f deg");
        paramSlider(engine, prefix, "startElevation", "Start height", "%.0f deg");
        paramSlider(engine, prefix, "endAzimuth", "End bearing", "%.0f deg");
        paramSlider(engine, prefix, "endElevation", "End height", "%.0f deg");
        paramSlider(engine, prefix, "distance", "Distance", "%.0f m", true);
        paramSlider(engine, prefix, "speed", "Speed");
        paramSlider(engine, prefix, "acceleration", "Acceleration");
        paramSlider(engine, prefix, "arcLift", "Arc lift", "%.0f m");
        paramSlider(engine, prefix, "curvature", "Curvature", "%.0f m");

        ImGui::SeparatorText("Appearance");
        paramColor(engine, prefix, "haloColor", "Halo colour");
        paramSlider(engine, prefix, "haloIntensity", "Halo brightness");
        paramSlider(engine, prefix, "haloSize", "Halo size", "%.0f m");
        paramSlider(engine, prefix, "tailIntensity", "Tail brightness");
        paramSlider(engine, prefix, "tailFalloff", "Tail falloff");
        paramSlider(engine, prefix, "wispAmount", "Wisp amount", "%.0f m");
        paramSlider(engine, prefix, "wispScale", "Wisp scale", "%.4f");
        paramSlider(engine, prefix, "flowSpeed", "Wisp flow");

        ImGui::SeparatorText("Fragments");
        paramSlider(engine, prefix, "sparkleDensity", "Density", "%.3f /m");
        paramSlider(engine, prefix, "sparkleSize", "Size");
        paramSlider(engine, prefix, "sparkleIntensity", "Brightness");
        paramSlider(engine, prefix, "sparkleSpeed", "Twinkle");
    } else {
        ImGui::SeparatorText("Shape");
        int anchor = static_cast<int>(authored.aurora.shape.anchor);
        rowLabel("Anchored to");
        if (ImGui::Combo("##anchor", &anchor, kSkyAnchorNames, 2)) {
            commitAtmospheric(engine, index, [anchor](world::AtmosphericEffect& e) {
                e.aurora.shape.anchor = static_cast<world::SkyAnchor>(std::clamp(anchor, 0, 1));
            });
            return;
        }
        paramSlider(engine, prefix, "radius", "Distance", "%.0f m", true);
        paramSlider(engine, prefix, "layerSpacing", "Layer spacing");
        paramSlider(engine, prefix, "baseHeight", "Base height", "%.0f m");
        paramSlider(engine, prefix, "waveAmplitude", "Wave amount");
        paramSlider(engine, prefix, "waveScale", "Wave scale");
        paramSlider(engine, prefix, "turbulence", "Turbulence");
        paramSlider(engine, prefix, "complexity", "Ray structure", "%.0f");
        paramSlider(engine, prefix, "driftSpeed", "Fold drift");
        paramSlider(engine, prefix, "verticalSpeed", "Vertical drift");

        ImGui::SeparatorText("Appearance");
        paramSlider(engine, prefix, "emission", "Bloom weight");
        paramSlider(engine, prefix, "opacity", "Curtain opacity");
        paramSlider(engine, prefix, "edgeBrightness", "Edge brightness");
        paramSlider(engine, prefix, "filaments", "Filaments");
        paramSlider(engine, prefix, "sparkle", "Sparkle");
        paramSlider(engine, prefix, "horizonGlow", "Horizon glow");

        // Section 4.2's per-band depths. These scale the bands already in the frame block; they are
        // not a second analyzer, and every one of them is itself an ordinary parameter a route can
        // drive.
        ImGui::SeparatorText("Audio response");
        paramSlider(engine, prefix, "audioBass", "Bass -> height");
        paramSlider(engine, prefix, "audioLowMid", "Low-mid -> waves");
        paramSlider(engine, prefix, "audioMid", "Mid -> folds");
        paramSlider(engine, prefix, "audioHigh", "High -> filaments");
        paramSlider(engine, prefix, "audioBeat", "Beat -> pulse");
    }

    // The rainbow and the ground pool belong to the two sky kinds; a vortex registers neither, and
    // the rows would silently draw nothing (ADR-375's lesson, the other way round).
    if (isVortex) {
        ImGui::TextColored(kMuted, "Every control here is the parameter atmos/%s/...",
                           authored.name.c_str());
        return;
    }

    ImGui::SeparatorText("Rainbow");
    paramSlider(engine, prefix, "rainbowSpeed", "Speed");
    paramSlider(engine, prefix, "rainbowScale", "Scale");
    paramSlider(engine, prefix, "rainbowHue", "Hue offset");
    paramSlider(engine, prefix, "rainbowSaturation", "Saturation");
    paramSlider(engine, prefix, "rainbowBrightness", "Brightness");

    if (authored.ground.mode != world::GroundGlow::Off) {
        ImGui::SeparatorText("Ground illumination");
        paramSlider(engine, prefix, "groundRadius", "Radius", "%.0f m", true);
        paramSlider(engine, prefix, "groundFalloff", "Falloff");
    }

    ImGui::TextColored(kMuted, "Every control here is the parameter atmos/%s/...", authored.name.c_str());
}

} // namespace avgen::ui
