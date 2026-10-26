#include "ui/midi_panel.hpp"

#include "app/engine.hpp"
#include "control/midi.hpp"
#include "control/midi_mapper.hpp"
#include "control/midi_rig.hpp"
#include "params/parameter.hpp"
#include "ui/help_panel.hpp"
#include "ui/style.hpp"
#include "ui/theme.hpp"
#include "ui/ui_logic.hpp"

#include <fmt/format.h>
#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_map>

namespace avgen::ui {

namespace {

using control::MidiBindKind;
using control::MidiBinding;
using control::MidiMappingState;
using control::MidiScope;

ImVec4 colour(ImU32 c) {
    return ImGui::ColorConvertU32ToFloat4(c);
}

bool containsNoCase(std::string_view haystack, std::string_view needle) {
    if (needle.empty()) {
        return true;
    }
    const auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(), [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    });
    return it != haystack.end();
}

// A device as the person knows it. Two connected devices with the same name are numbered, because a name alone does
// not say which of two identical keyboards a mapping listens to.
std::string slotLabel(const std::vector<control::MidiDeviceSlot>& devices, std::size_t slot) {
    if (slot >= devices.size()) {
        return "(no device)";
    }
    // Numbered only among CONNECTED devices: a slot left behind by a device that came back under another id is not a
    // second keyboard.
    int same = 0;
    int index = 0;
    for (std::size_t i = 0; i < devices.size(); ++i) {
        if (devices[i].name == devices[slot].name && devices[i].connected) {
            ++same;
            if (i <= slot) {
                ++index;
            }
        }
    }
    return same > 1 && devices[slot].connected ? fmt::format("{} ({})", devices[slot].name, index) : devices[slot].name;
}

std::string deviceLabel(const MidiBinding& b) {
    if (!b.device.empty()) {
        return b.deviceName.empty() ? b.device : b.deviceName;
    }
    if (b.source.empty() || b.source == "*") {
        return "Any device";
    }
    return fmt::format("\"{}\"", b.source);
}

std::string channelLabel(int channel) {
    return channel < 0 ? std::string("Any") : std::to_string(channel + 1);
}

std::string destinationLabel(const control::BindingTarget& t) {
    std::string out;
    if (t.action != control::MidiAction::None) { // ADR-1705
        return std::string("action: ") + control::midiActionLabel(t.action);
    }
    if (!t.signal.empty()) {
        out = "control." + t.signal;
    }
    if (!t.parameter.empty()) {
        if (!out.empty()) {
            out += " + ";
        }
        out += t.parameter;
        if (t.component >= 0) {
            out += fmt::format("[{}]", t.component);
        }
    }
    return out.empty() ? std::string("(no destination)") : out;
}

std::string behaviourLabel(const MidiBinding& b) {
    std::string out;
    if (b.target.action != control::MidiAction::None) {
        return "press"; // ADR-1705: a rising edge fires it
    }
    const auto range = fmt::format("{:.3g}..{:.3g}", static_cast<double>(b.target.min), static_cast<double>(b.target.max));
    switch (b.kind) {
    case MidiBindKind::Note:
        out = b.toggle ? "toggle " + range : (b.velocity ? "gate, velocity " : "gate ") + range;
        break;
    case MidiBindKind::NoteEvent:
        out = b.velocity ? "trigger, velocity" : "trigger";
        break;
    case MidiBindKind::Program:
        out = b.number >= 0 ? "trigger" : range;
        break;
    default:
        out = range;
        if (b.encoder != control::MidiEncoder::Absolute) out += ", relative";
        break;
    }
    if (b.invert) out += ", inverted";
    if (b.curve != control::MidiCurve::Linear) out += std::string(", ") + control::midiCurveName(b.curve);
    if (b.smoothMs > 0.0f) out += fmt::format(", smooth {:.0f} ms", static_cast<double>(b.smoothMs));
    if (b.pickup) out += ", pickup";
    return out;
}

struct StatusText {
    std::string text;
    ImU32 colour = 0;
};

StatusText statusFor(const MidiBinding& b, const control::MidiMappingStatus* s, const control::ControlMap& map) {
    const Palette& p = palette();
    if (b.invalid()) {
        return {"invalid: " + b.invalidReason, p.error};
    }
    if (s == nullptr) {
        return {"-", p.textDisabled};
    }
    const auto byLabel = [&](const std::string& id) {
        const MidiBinding* other = map.findMidi(id);
        return other != nullptr ? fmt::format("{} ({})", control::midiSourceLabel(*other), control::midiScopeName(other->scope))
                                : id;
    };
    switch (s->state) {
    case MidiMappingState::Disabled: return {"off", p.textDisabled};
    case MidiMappingState::DefaultOff: return {"default switched off", p.textDisabled};
    case MidiMappingState::Overridden:
        // ADR-1705: a rig mapping is replaced (or switched off) in this scene or project, and still applies elsewhere.
        return {b.scope == MidiScope::Rig ? "replaced here (" + byLabel(s->by) + ")" : std::string("replaced by your edit"),
                p.textMuted};
    case MidiMappingState::Shadowed: return {"shadowed by " + byLabel(s->by), p.warning};
    case MidiMappingState::OtherScene: return {"for scene " + b.scene, p.textDisabled};
    case MidiMappingState::Invalid: return {"invalid", p.error};
    case MidiMappingState::Active: break;
    }
    if (s->inactiveForScene) {
        // ADR-1705: not a warning -- the rig is for every scene, and this one does not have that destination.
        return {"inactive for this scene", p.textDisabled};
    }
    if (s->destinationMissing) {
        return {"not in this scene", p.warning};
    }
    if (s->device == control::MidiDeviceState::Absent) {
        return {"device not connected", p.warning};
    }
    if (s->waitingForPickup) {
        return {"waiting for pickup", p.accent};
    }
    if (s->device == control::MidiDeviceState::ByName) {
        return {"active (device matched by name)", p.accent};
    }
    return {"active", p.success};
}

// A dot that is lit while something arrived in the last quarter second, and fades over the next.
void activityDot(double now, double last) {
    const float h = ImGui::GetTextLineHeight();
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const double age = last < 0.0 ? 1e9 : now - last;
    const float glow = age < 0.25 ? 1.0f : (age < 1.0 ? static_cast<float>(1.0 - (age - 0.25) / 0.75) : 0.0f);
    const ImU32 lit = palette().success;
    const ImU32 dim = palette().border;
    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(at.x + h * 0.5f, at.y + h * 0.55f), h * 0.3f,
                                                mixColour(dim, lit, glow));
    ImGui::Dummy(ImVec2(h, h));
}

// A thin bar showing a 0..1 value.
void valueBar(float value01, float width) {
    const float h = ImGui::GetTextLineHeight() * 0.45f;
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float y = at.y + ImGui::GetTextLineHeight() * 0.3f;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(ImVec2(at.x, y), ImVec2(at.x + width, y + h), palette().control, 2.0f);
    if (value01 >= 0.0f) {
        draw->AddRectFilled(ImVec2(at.x, y), ImVec2(at.x + width * std::clamp(value01, 0.0f, 1.0f), y + h),
                            palette().accent, 2.0f);
    }
    ImGui::Dummy(ImVec2(width, ImGui::GetTextLineHeight()));
}

const char* kKindLabels[] = {"CC", "Note (gate/toggle)", "Note (trigger)", "Pitch bend", "Channel pressure",
                             "Program change", "Poly pressure", "NRPN", "RPN"};
const char* kCurveLabels[] = {"Linear", "Exponential (fine at the bottom)", "Logarithmic (fine at the top)",
                              "S-curve (fine at both ends)"};
const char* kEncoderLabels[] = {"Absolute", "Relative: two's complement", "Relative: binary offset",
                                "Relative: sign-magnitude"};
const char* kCombineLabels[] = {"Replace (latest wins)", "Max (largest wins)", "Add (sum)"};

} // namespace

// ---- the panel -------------------------------------------------------------------------------------------------------

void MidiPanel::learnFor(app::Engine& engine, const std::string& parameterPath) {
    const params::IParameter* p = engine.params().find(parameterPath);
    if (p == nullptr) {
        return;
    }
    learnTarget_ = control::BindingTarget{};
    learnTarget_.parameter = parameterPath;
    learnTarget_.component = p->componentCount() > 1 ? 0 : -1;
    learnTarget_.min = p->softMin(0);
    learnTarget_.max = p->softMax(0);
    engine.control().learn().arm();
    learnMessage_.clear();
}

void MidiPanel::draw(app::Engine& engine) {
    helpHeader("modulation/midi-mapping");
    drawLearnBar(engine);
    ImGui::Separator();
    if (ImGui::BeginTabBar("##midi-tabs")) {
        const auto flagsFor = [&](Tab t) {
            if (requestedTab_ == static_cast<int>(t)) {
                return ImGuiTabItemFlags_SetSelected;
            }
            return ImGuiTabItemFlags_None;
        };
        const auto& map = engine.control().map();
        const std::string assignments = fmt::format("Assignments ({})###assignments",
                                                    map.midi.size() + map.midiUser.size() + map.midiRig.size());
        if (ImGui::BeginTabItem(assignments.c_str(), nullptr, flagsFor(Tab::Assignments))) {
            drawAssignments(engine);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Monitor", nullptr, flagsFor(Tab::Monitor))) {
            drawMonitor(engine);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Devices", nullptr, flagsFor(Tab::Devices))) {
            drawDevices(engine);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    requestedTab_ = -1;
}

// ---- Learn -----------------------------------------------------------------------------------------------------------

void MidiPanel::drawLearnBar(app::Engine& engine) {
    auto& hub = engine.control();
    auto& learn = hub.learn();
    const Palette& p = palette();
    const auto& devices = hub.midiDevices();
    using State = control::MidiLearn::State;

    if (learn.state() != State::Idle && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        learn.cancel();
    }

    // Destination and scope, always visible: what Learn would assign to.
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Destination");
    ImGui::SameLine();
    const float scopeWidth = ImGui::CalcTextSize("Project").x * 3.0f + ImGui::GetFrameHeight() * 4.5f;
    pickerForRig_ = learnScope_ == MidiScope::Rig;
    destinationPicker("##learn-destination", engine, learnTarget_,
                      std::clamp(ImGui::GetContentRegionAvail().x - scopeWidth - buttonWidth("Learn") - 24.0f, 160.0f, 460.0f));
    pickerForRig_ = false;
    ImGui::SameLine();
    const int scope = learnScope_ == MidiScope::Project ? 1 : (learnScope_ == MidiScope::Rig ? 2 : 0);
    if (ImGui::RadioButton("Scene", scope == 0)) {
        learnScope_ = MidiScope::Scene;
    }
    if (ImGui::IsItemHovered()) {
        tooltip("Saved with this project, and active only while its scene is %s.", hub.sceneKey().c_str());
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Project", scope == 1)) {
        learnScope_ = MidiScope::Project;
    }
    if (ImGui::IsItemHovered()) {
        tooltip("Saved with this project and active whatever scene it shows.");
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Rig", scope == 2)) {
        learnScope_ = MidiScope::Rig;
    }
    if (ImGui::IsItemHovered()) {
        tooltip("Your controller setup, for every scene and project: kept on this computer (midi-rig.json beside the "
                "settings), never in a project. A project's or a scene's own mapping wins over it.");
    }
    ImGui::SameLine();

    const bool hasTarget = !learnTarget_.signal.empty() || !learnTarget_.parameter.empty() ||
                           learnTarget_.action != control::MidiAction::None;
    if (learn.state() == State::Idle) {
        if (ImGui::Button("Learn")) {
            learn.arm();
            learnMessage_.clear();
        }
        if (ImGui::IsItemHovered()) {
            tooltip("Listen for the next knob, fader, pad or note. You can also pick the destination afterwards.");
        }
    } else {
        if (ImGui::Button("Cancel")) {
            learn.cancel();
        }
    }
    // Options for what is learned.
    ImGui::Checkbox("Any device", &learnAnyDevice_);
    if (ImGui::IsItemHovered()) {
        tooltip("Off: the mapping listens only to the device it was learned from, so two instruments sending the same "
                "CC do not collide. On: every device.");
    }
    ImGui::SameLine();
    ImGui::Checkbox("Any channel", &learnAnyChannel_);
    ImGui::SameLine();
    ImGui::Checkbox("Notes trigger", &learnAsTrigger_);
    if (ImGui::IsItemHovered()) {
        tooltip("Off: a learned note is a gate (high while held). On: a one-frame trigger, for an event channel.");
    }
    ImGui::SameLine();
    {
        // Learn only from one device, when the person says so (the filter respects their choice of input).
        std::vector<std::string> labels{"from every device"};
        for (std::size_t i = 0; i < devices.size(); ++i) {
            labels.push_back("from " + slotLabel(devices, i));
        }
        int current = learn.filterDevice() + 1;
        current = std::clamp(current, 0, static_cast<int>(labels.size()) - 1);
        ImGui::SetNextItemWidth(std::min(220.0f, ImGui::GetContentRegionAvail().x));
        if (ImGui::BeginCombo("##learn-from", labels[static_cast<std::size_t>(current)].c_str())) {
            for (std::size_t i = 0; i < labels.size(); ++i) {
                if (ImGui::Selectable(labels[i].c_str(), static_cast<int>(i) == current)) {
                    learn.setFilter(static_cast<int>(i) - 1, learn.filterChannel());
                }
            }
            ImGui::EndCombo();
        }
    }

    if (learn.state() == State::Listening) {
        // Armed: impossible to miss.
        const ImVec2 start = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x;
        const float height = ImGui::GetFrameHeight() * 1.4f;
        const float pulse = 0.55f + 0.45f * static_cast<float>(std::sin(ImGui::GetTime() * 6.0));
        ImGui::GetWindowDrawList()->AddRectFilled(start, ImVec2(start.x + width, start.y + height),
                                                  withAlpha(p.warning, 0.25f + 0.25f * pulse), 4.0f);
        ImGui::GetWindowDrawList()->AddRect(start, ImVec2(start.x + width, start.y + height), p.warning, 4.0f, 0, 2.0f);
        ImGui::SetCursorScreenPos(ImVec2(start.x + 8.0f, start.y + (height - ImGui::GetTextLineHeight()) * 0.5f));
        ImGui::TextColored(colour(p.warning), "LISTENING");
        ImGui::SameLine();
        ImGui::TextUnformatted(hasTarget ? fmt::format("Move a control or play a note for {}  (Esc cancels)",
                                                       destinationLabel(learnTarget_)).c_str()
                                         : "Move a control or play a note  (Esc cancels)");
        ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + height + ImGui::GetStyle().ItemSpacing.y));
        ImGui::Dummy(ImVec2(0, 0));
    } else if (learn.state() == State::Captured) {
        const control::MidiMessage& m = learn.captured();
        MidiBinding candidate =
            control::midiBindingFromMessage(m, devices, learnAnyDevice_, learnAnyChannel_, learnAsTrigger_);
        const auto& map = hub.map();
        const auto collisions = control::midiCollisions(map, hub.sceneKey(), candidate);
        ImGui::TextColored(colour(p.accent), "Detected");
        ImGui::SameLine();
        ImGui::Text("%s  ·  ch %d  ·  %s", m.device < devices.size() ? slotLabel(devices, m.device).c_str() : "(no device)",
                    m.channel + 1, control::midiSourceLabel(candidate).c_str());
        ImGui::SameLine();
        valueBar(learn.liveValue(), 90.0f);
        ImGui::SameLine();
        ImGui::TextDisabled("%s", control::midiMessageDescription(m).c_str());
        if (collisions.empty()) {
            ImGui::TextDisabled("Not assigned yet.");
        } else {
            ImGui::TextColored(colour(p.warning), "Already assigned:");
            for (const MidiBinding* c : collisions) {
                ImGui::BulletText("%s -> %s (%s)", control::midiSourceLabel(*c).c_str(), destinationLabel(c->target).c_str(),
                                  control::midiScopeName(c->scope));
            }
        }
        if (!hasTarget) {
            ImGui::TextColored(colour(p.warning), "Pick a destination above to assign it.");
        }
        candidate.target = learnTarget_;
        candidate.scope = learnScope_;
        std::string invalid;
        if (candidate.kind == MidiBindKind::NoteEvent && learnTarget_.signal.empty() &&
            learnTarget_.action == control::MidiAction::None && hasTarget) {
            invalid = "A trigger is a one-frame pulse: map it to a control channel (a route turns it into a flash), or "
                      "untick Notes trigger for a gate.";
        }
        if (!invalid.empty()) {
            ImGui::TextColored(colour(p.warning), "%s", invalid.c_str());
        }
        ImGui::BeginDisabled(!hasTarget || !invalid.empty());
        const auto commit = [&](control::MidiLearnCommit how) {
            const std::string id = control::commitMidiLearn(hub.map(), hub.sceneKey(), candidate, how);
            hub.mappingsChanged();
            selected_ = id;
            learn.cancel();
            learnMessage_ = fmt::format("Assigned {} to {}{}.", control::midiSourceLabel(candidate), destinationLabel(learnTarget_),
                                        candidate.scope == MidiScope::Rig ? " in the rig (every scene)" : "");
        };
        if (collisions.empty()) {
            if (ImGui::Button("Assign")) {
                commit(control::MidiLearnCommit::Add);
            }
        } else {
            if (ImGui::Button("Add destination")) {
                commit(control::MidiLearnCommit::Add);
            }
            if (ImGui::IsItemHovered()) {
                tooltip("Keep the existing assignments and drive this destination too.");
            }
            ImGui::SameLine();
            if (ImGui::Button("Replace existing")) {
                commit(control::MidiLearnCommit::Replace);
            }
            if (ImGui::IsItemHovered()) {
                tooltip("Switch the existing assignments off (a default is kept, only switched off) and use this one.");
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Learn again")) {
            learn.arm();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel##captured")) {
            learn.cancel();
        }
    } else if (!learnMessage_.empty()) {
        ImGui::TextColored(colour(p.success), "%s", learnMessage_.c_str());
    }
}

// ---- the destination picker ------------------------------------------------------------------------------------------

bool MidiPanel::destinationPicker(const char* id, app::Engine& engine, control::BindingTarget& target, float width) {
    bool changed = false;
    const std::string preview =
        (target.signal.empty() && target.parameter.empty() && target.action == control::MidiAction::None)
            ? std::string("Choose a destination...")
            : destinationLabel(target);
    if (ImGui::Button(fmt::format("{}{}", preview, id).c_str(), ImVec2(width, 0.0f))) {
        ImGui::OpenPopup(id);
        pickerSearch_[0] = '\0';
    }
    if (ImGui::IsItemHovered() && !target.parameter.empty()) {
        if (const params::IParameter* param = engine.params().find(target.parameter)) {
            tooltip("%s: base %.3f, range %.3g..%.3g", param->label().c_str(), static_cast<double>(param->baseComponent(0)),
                    static_cast<double>(param->softMin(0)), static_cast<double>(param->softMax(0)));
        } else {
            tooltip("%s is not in this scene.", target.parameter.c_str());
        }
    }
    ImGui::SetNextWindowSizeConstraints(ImVec2(380.0f, 240.0f), ImVec2(640.0f, 560.0f));
    if (!ImGui::BeginPopup(id)) {
        return false;
    }
    if (ImGui::IsWindowAppearing()) {
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##picker-search", "Search parameters and control channels", pickerSearch_,
                             sizeof(pickerSearch_));
    const std::string_view query = pickerSearch_;
    const bool searching = !query.empty();
    ImGui::BeginChild("##picker-tree", ImVec2(0.0f, 380.0f));
    ImGui::PushTextWrapPos(0.0f);

    // Control channels first: the modulation path (routes read them), and what the shipped scenes map.
    ImGui::SetNextItemOpen(true, ImGuiCond_Appearing);
    if (ImGui::TreeNodeEx("Control channels", searching ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
        ImGui::TextDisabled("A channel publishes control.<name>; the scene's routes decide what it moves.");
        for (const std::string& channel : engine.controlSource().channels()) {
            const std::string label = "control." + channel;
            if (!containsNoCase(label, query)) {
                continue;
            }
            const bool event = engine.controlSource().isEventChannel(channel);
            if (ImGui::Selectable(fmt::format("{}{}##ch-{}", label, event ? "  (event)" : "", channel).c_str(),
                                  target.signal == channel && target.parameter.empty())) {
                target = control::BindingTarget{};
                target.signal = channel;
                changed = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%.2f", static_cast<double>(engine.controlSource().value(channel)));
        }
        ImGui::SetNextItemWidth(160.0f);
        ImGui::InputTextWithHint("##new-channel", "new channel", newChannel_, sizeof(newChannel_));
        ImGui::SameLine();
        ImGui::BeginDisabled(newChannel_[0] == '\0');
        if (ImGui::Button("Use")) {
            target = control::BindingTarget{};
            target.signal = newChannel_;
            newChannel_[0] = '\0';
            changed = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::TreePop();
    }

    // ADR-1705: the app actions a press can fire, the keyboard's own commands.
    if (ImGui::TreeNodeEx("App actions", searching ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
        ImGui::TextDisabled("Fired once per press, in every scene: map a pad or a button.");
        for (const control::MidiAction a : control::kMidiActions) {
            const std::string label = control::midiActionLabel(a);
            if (!containsNoCase(label, query) && !containsNoCase(control::midiActionName(a), query)) {
                continue;
            }
            if (ImGui::Selectable(fmt::format("{}##act-{}", label, control::midiActionName(a)).c_str(), target.action == a)) {
                target = control::BindingTarget{};
                target.action = a;
                changed = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%s", control::midiActionName(a));
        }
        ImGui::TreePop();
    }
    if (pickerForRig_) {
        ImGui::TextDisabled("For the rig: control channels, app actions and the engine's own parameters (post/, temporal/, "
                            "camera lens/exposure/focus, sonic/response/, scene/brightness) exist in every scene. A parameter marked \"this scene\" is inactive in scenes without it.");
    }

    // Then the parameter registry: every exposed, modulatable parameter, by group and sub-group (the Parameters panel's
    // own structure, read off the paths).
    std::vector<std::string> groups;
    std::unordered_map<std::string, std::vector<params::IParameter*>> byGroup;
    for (params::IParameter* param : engine.params().ordered()) {
        if (!param->flags().exposed || !param->flags().modulatable) {
            continue;
        }
        if (searching && !containsNoCase(param->path(), query) && !containsNoCase(param->label(), query)) {
            continue;
        }
        const std::string group = param->group().empty() ? std::string("(ungrouped)") : param->group();
        auto [it, inserted] = byGroup.try_emplace(group);
        if (inserted) {
            groups.push_back(group);
        }
        it->second.push_back(param);
    }
    for (const std::string& group : groups) {
        const auto& members = byGroup[group];
        if (!ImGui::TreeNodeEx(fmt::format("{}  ({})###grp-{}", group, members.size(), group).c_str(),
                               searching ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
            continue;
        }
        std::string currentSub = "\x01";
        bool subOpen = true;
        std::vector<params::IParameter*> sorted = members;
        std::stable_sort(sorted.begin(), sorted.end(), [&](const params::IParameter* a, const params::IParameter* b) {
            return parameterSubGroup(a->path(), group) < parameterSubGroup(b->path(), group);
        });
        for (params::IParameter* param : sorted) {
            const std::string sub = parameterSubGroup(param->path(), group);
            if (sub != currentSub) {
                if (currentSub != "\x01" && !currentSub.empty() && subOpen) {
                    ImGui::TreePop();
                }
                currentSub = sub;
                subOpen = sub.empty() || ImGui::TreeNodeEx(fmt::format("{}###sub-{}-{}", sub, group, sub).c_str(),
                                                           searching ? ImGuiTreeNodeFlags_DefaultOpen : 0);
            }
            if (!subOpen) {
                continue;
            }
            const bool chosen = target.parameter == param->path();
            if (ImGui::Selectable(fmt::format("{}##{}", param->label(), param->path()).c_str(), chosen)) {
                target = control::BindingTarget{};
                target.parameter = param->path();
                target.component = param->componentCount() > 1 ? 0 : -1;
                target.min = param->softMin(0);
                target.max = param->softMax(0);
                changed = true;
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::IsItemHovered()) {
                tooltip("%s", param->path().c_str());
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%s  %.3g  [%.3g..%.3g]", param->path().c_str(), static_cast<double>(param->baseComponent(0)),
                                static_cast<double>(param->softMin(0)), static_cast<double>(param->softMax(0)));
            if (pickerForRig_ && !control::midiRigCommonParameter(param->path())) {
                ImGui::SameLine();
                ImGui::TextColored(colour(palette().warning), "this scene");
            }
        }
        if (currentSub != "\x01" && !currentSub.empty() && subOpen) {
            ImGui::TreePop();
        }
        ImGui::TreePop();
    }
    if (groups.empty() && searching) {
        ImGui::TextDisabled("No parameter matches \"%s\".", pickerSearch_);
    }
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
    ImGui::EndPopup();
    return changed;
}

// ---- Assignments -----------------------------------------------------------------------------------------------------

void MidiPanel::drawAssignments(app::Engine& engine) {
    auto& hub = engine.control();
    auto& map = hub.map();
    const auto& mapper = hub.mapper();
    const double now = hub.clockSeconds();
    const Palette& p = palette();

    // Toolbar.
    ImGui::SetNextItemWidth(std::min(240.0f, ImGui::GetContentRegionAvail().x * 0.4f));
    ImGui::InputTextWithHint("##midi-search", "Search source, device, destination", search_, sizeof(search_));
    ImGui::SameLine();
    const char* scopes[] = {"All scopes", "Defaults", "Rig", "Project", "Scene"}; // MidiScope order, after "All"
    ImGui::SetNextItemWidth(110.0f);
    ImGui::Combo("##midi-scope", &scopeFilter_, scopes, 5);
    ImGui::SameLine();
    ImGui::Checkbox("Inactive", &showInactive_);
    if (ImGui::IsItemHovered()) {
        tooltip("Also list mappings that do not apply now: switched off, replaced, shadowed or for another scene.");
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset scene mappings")) {
        ImGui::OpenPopup("Reset this scene's MIDI mappings?");
    }
    if (ImGui::BeginPopupModal("Reset this scene's MIDI mappings?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Removes your scene mappings for this scene and switches every default back on.");
        ImGui::TextUnformatted("Project mappings and the rig stay. Undo by not saving.");
        if (ImGui::Button("Reset")) {
            control::resetSceneMidiMappings(map, hub.sceneKey());
            hub.mappingsChanged();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    const auto resolved = control::resolveMidiMappings(map, hub.sceneKey());
    int activeCount = 0;
    int inactiveCount = 0;
    for (const auto& r : resolved) {
        (r.state == MidiMappingState::Active ? activeCount : inactiveCount) += 1;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%d active, %d inactive  ·  scene %s", activeCount, inactiveCount, hub.sceneKey().c_str());

    // Rows, filtered, by stable id.
    struct Row {
        const MidiBinding* b = nullptr;
        MidiMappingState state = MidiMappingState::Active;
        std::string device;
        std::string source;
        std::string destination;
        int sources = 1; // how many active mappings share this source (one source, many destinations)
    };
    std::vector<Row> rows;
    const std::string_view query = search_;
    for (const auto& r : resolved) {
        const MidiBinding& b = *r.binding;
        if (!showInactive_ && r.state != MidiMappingState::Active) {
            continue;
        }
        if (scopeFilter_ > 0 && static_cast<int>(b.scope) != scopeFilter_ - 1) {
            continue;
        }
        Row row{&b, r.state, deviceLabel(b), b.invalid() ? std::string("(unreadable)") : control::midiSourceLabel(b),
                destinationLabel(b.target)};
        if (!query.empty() && !containsNoCase(row.device, query) && !containsNoCase(row.source, query) &&
            !containsNoCase(row.destination, query) && !containsNoCase(control::midiScopeName(b.scope), query)) {
            continue;
        }
        rows.push_back(std::move(row));
    }
    for (Row& row : rows) {
        if (row.state != MidiMappingState::Active || row.b->invalid()) {
            continue;
        }
        row.sources = static_cast<int>(std::count_if(resolved.begin(), resolved.end(), [&](const auto& r) {
            return r.state == MidiMappingState::Active && control::midiSourcesOverlap(*r.binding, *row.b);
        }));
    }

    // Deferred actions: a row's buttons must not edit the vectors the rows point into mid-loop.
    enum class Action { None, Toggle, Duplicate, Remove, Restore, Promote, ToRig, RigToScene, RigOverride };
    Action action = Action::None;
    std::string actionId;

    // Wide (the bottom dock): the editor sits beside the table, which keeps its height. Narrow: below it.
    const bool sideBySide = !selected_.empty() && ImGui::GetContentRegionAvail().x > 1100.0f;
    const float editorHeight =
        selected_.empty() || sideBySide ? 0.0f : std::min(300.0f, ImGui::GetContentRegionAvail().y * 0.5f);
    editorBeside_ = sideBySide;
    if (sideBySide) {
        ImGui::BeginChild("##midi-table-side", ImVec2(ImGui::GetContentRegionAvail().x * 0.6f, 0.0f));
    }
    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable |
                                  ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable | ImGuiTableFlags_SizingFixedFit |
                                  ImGuiTableFlags_Hideable;
    if (ImGui::BeginTable("##midi-assignments", 10, flags, ImVec2(0.0f, -editorHeight))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("On", ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Device");
        ImGui::TableSetupColumn("Ch");
        ImGui::TableSetupColumn("Type");
        ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_DefaultSort);
        ImGui::TableSetupColumn("Destination", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Range / behaviour", ImGuiTableColumnFlags_NoSort);
        ImGui::TableSetupColumn("Scope");
        ImGui::TableSetupColumn("Status");
        ImGui::TableSetupColumn("Live", ImGuiTableColumnFlags_NoSort);
        ImGui::TableHeadersRow();
        if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs(); specs != nullptr && specs->SpecsCount > 0) {
            const ImGuiTableColumnSortSpecs& s = specs->Specs[0];
            const bool asc = s.SortDirection == ImGuiSortDirection_Ascending;
            std::stable_sort(rows.begin(), rows.end(), [&](const Row& a, const Row& b) {
                const auto cmp = [&](const auto& x, const auto& y) { return asc ? x < y : y < x; };
                switch (s.ColumnIndex) {
                case 1: return cmp(a.device, b.device);
                case 2: return cmp(a.b->channel, b.b->channel);
                case 3: return cmp(static_cast<int>(a.b->kind), static_cast<int>(b.b->kind));
                case 4: return cmp(std::make_pair(static_cast<int>(a.b->kind), a.b->number),
                                   std::make_pair(static_cast<int>(b.b->kind), b.b->number));
                case 5: return cmp(a.destination, b.destination);
                case 7: return cmp(static_cast<int>(a.b->scope), static_cast<int>(b.b->scope));
                case 8: return cmp(static_cast<int>(a.state), static_cast<int>(b.state));
                default: return false;
                }
            });
        }
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(rows.size()));
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const Row& row = rows[static_cast<std::size_t>(i)];
                const MidiBinding& b = *row.b;
                const control::MidiMappingStatus* status = mapper.status(b.id);
                ImGui::PushID(b.id.c_str());
                ImGui::TableNextRow();
                const bool inactive = row.state != MidiMappingState::Active;
                ImGui::TableNextColumn();
                bool on = control::midiMappingEnabled(map, b.id);
                ImGui::BeginDisabled(b.invalid());
                if (ImGui::Checkbox("##on", &on)) {
                    action = Action::Toggle;
                    actionId = b.id;
                }
                ImGui::EndDisabled();
                ImGui::TableNextColumn();
                const bool selected = selected_ == b.id;
                if (inactive) {
                    ImGui::PushStyleColor(ImGuiCol_Text, colour(p.textDisabled));
                }
                if (ImGui::Selectable(row.device.c_str(), selected,
                                      ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) {
                    selected_ = b.id;
                }
                if (ImGui::BeginPopupContextItem("##row-menu")) {
                    selected_ = b.id;
                    if (ImGui::MenuItem(on ? "Disable" : "Enable", nullptr, false, !b.invalid())) {
                        action = Action::Toggle;
                        actionId = b.id;
                    }
                    if (ImGui::MenuItem("Duplicate", nullptr, false, !b.invalid())) {
                        action = Action::Duplicate;
                        actionId = b.id;
                    }
                    // ADR-1705: between the rig and this project.
                    if (b.scope != MidiScope::Rig) {
                        if (ImGui::MenuItem(b.scope == MidiScope::Default ? "Copy to the rig" : "Move to the rig", nullptr,
                                            false, !b.invalid())) {
                            action = Action::ToRig;
                            actionId = b.id;
                        }
                        if (ImGui::IsItemHovered()) {
                            tooltip("The rig is your controller setup for every scene, kept on this computer and never "
                                    "in a project. A project's or a scene's own mapping still wins over it.");
                        }
                    } else {
                        if (ImGui::MenuItem("Move into this scene", nullptr, false, !b.invalid())) {
                            action = Action::RigToScene;
                            actionId = b.id;
                        }
                        if (ImGui::IsItemHovered()) {
                            tooltip("Takes it out of the rig (other scenes stop using it) and saves it with this project, "
                                    "for scene %s.", hub.sceneKey().c_str());
                        }
                        if (ImGui::MenuItem("Override in this scene", nullptr, false, !b.invalid())) {
                            action = Action::RigOverride;
                            actionId = b.id;
                        }
                        if (ImGui::IsItemHovered()) {
                            tooltip("Keeps it in the rig and makes a scene copy that replaces it here only.");
                        }
                    }
                    if (b.scope == MidiScope::Default) {
                        if (ImGui::MenuItem("Restore default")) {
                            action = Action::Restore;
                            actionId = b.id;
                        }
                    } else if (b.scope == MidiScope::Rig) {
                        if (ImGui::MenuItem("Remove from the rig")) {
                            action = Action::Remove;
                            actionId = b.id;
                        }
                        if (ImGui::IsItemHovered()) {
                            tooltip("Removes it for every scene.");
                        }
                    } else {
                        if (!b.overrides.empty() && ImGui::MenuItem("Restore the default it replaces")) {
                            action = Action::Restore;
                            actionId = b.overrides;
                        }
                        if (ImGui::MenuItem("Make it a scene default", nullptr, false, !b.invalid())) {
                            action = Action::Promote;
                            actionId = b.id;
                        }
                        if (ImGui::IsItemHovered()) {
                            tooltip("For the scene's author: moves this mapping into the scene's shipped defaults "
                                    "(control.midi.bindings) when the project is saved.");
                        }
                        if (ImGui::MenuItem("Remove")) {
                            action = Action::Remove;
                            actionId = b.id;
                        }
                    }
                    ImGui::EndPopup();
                }
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(channelLabel(b.channel).c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(control::midiBindKindName(b.kind));
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(row.source.c_str());
                if (row.sources > 1) {
                    ImGui::SameLine();
                    ImGui::TextColored(colour(p.accent), "x%d", row.sources);
                    if (ImGui::IsItemHovered()) {
                        tooltip("This source drives %d destinations.", row.sources);
                    }
                }
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(row.destination.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(b.invalid() ? "" : behaviourLabel(b).c_str());
                ImGui::TableNextColumn();
                if (b.scope == MidiScope::Rig && !inactive) {
                    ImGui::TextColored(colour(p.accent), "rig"); // ADR-1705: every scene
                } else {
                    ImGui::TextUnformatted(control::midiScopeName(b.scope));
                }
                if (b.scope == MidiScope::Rig && ImGui::IsItemHovered()) {
                    tooltip("Rig: your mapping for every scene, kept on this computer, not in the project.");
                }
                if (inactive) {
                    ImGui::PopStyleColor();
                }
                ImGui::TableNextColumn();
                const StatusText st = statusFor(b, status, map);
                ImGui::TextColored(colour(st.colour), "%s", st.text.c_str());
                ImGui::TableNextColumn();
                activityDot(now, status != nullptr ? status->lastSeconds : -1.0);
                if (status != nullptr && status->lastInput >= 0.0f) {
                    ImGui::SameLine();
                    valueBar(status->lastInput, 44.0f);
                }
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }

    switch (action) {
    case Action::Toggle: control::setMidiMappingEnabled(map, actionId, !control::midiMappingEnabled(map, actionId)); break;
    case Action::Duplicate: selected_ = control::duplicateMidiMapping(map, hub.sceneKey(), actionId); break;
    case Action::Remove:
        control::removeMidiMapping(map, actionId);
        if (selected_ == actionId) selected_.clear();
        break;
    case Action::Restore: control::restoreMidiDefault(map, actionId); selected_ = actionId; break;
    case Action::Promote: control::promoteMidiMappingToDefault(map, actionId); break;
    case Action::ToRig: selected_ = control::promoteMidiMappingToRig(map, actionId); break;
    case Action::RigToScene: control::demoteMidiRigMappingToScene(map, hub.sceneKey(), actionId); break;
    case Action::RigOverride: selected_ = control::overrideMidiRigMappingInScene(map, hub.sceneKey(), actionId); break;
    case Action::None: break;
    }
    if (action != Action::None) {
        hub.mappingsChanged();
    }
    if (sideBySide) {
        ImGui::EndChild();
        ImGui::SameLine();
    }
    if (rows.empty()) {
        ImGui::TextDisabled(map.midi.empty() && map.midiUser.empty() && map.midiRig.empty()
                                ? "No MIDI mappings yet. Pick a destination above and press Learn, or right-click a "
                                  "parameter in the Parameters panel."
                                : "Nothing matches the filter.");
    }
    if (!selected_.empty()) {
        drawEditor(engine);
    }
}

// ---- the editor ------------------------------------------------------------------------------------------------------

void MidiPanel::drawEditor(app::Engine& engine) {
    auto& hub = engine.control();
    auto& map = hub.map();
    const Palette& p = palette();
    const MidiBinding* shown = map.findMidi(selected_);
    if (shown == nullptr) {
        selected_.clear();
        return;
    }
    if (!editorBeside_) {
        ImGui::Separator(); // a separator beside the table would start a new line under it
    }
    ImGui::BeginChild("##midi-editor", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);
    ImGui::PushTextWrapPos(0.0f);
    const control::MidiMappingStatus* status = hub.mapper().status(shown->id);
    ImGui::Text("%s  ->  %s", control::midiSourceLabel(*shown).c_str(), destinationLabel(shown->target).c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("(%s, %s)", control::midiScopeName(shown->scope), shown->id.c_str());
    if (shown->invalid()) {
        ImGui::TextColored(colour(p.error), "This saved mapping could not be read: %s", shown->invalidReason.c_str());
        ImGui::TextDisabled("It is kept as it was and written back on save. Remove it, or fix it in the project file.");
        if (ImGui::Button("Remove it")) {
            control::removeMidiMapping(map, shown->id);
            hub.mappingsChanged();
            selected_.clear();
        }
        ImGui::PopTextWrapPos();
        ImGui::EndChild();
        return;
    }
    if (shown->scope == MidiScope::Default) {
        ImGui::TextColored(colour(p.textMuted),
                           "Shipped with the scene. A change is saved as your scene mapping that replaces it; the default "
                           "itself is never written.");
    } else if (shown->scope == MidiScope::Rig) {
        ImGui::TextColored(colour(p.accent), "Rig");
        ImGui::SameLine();
        ImGui::TextColored(colour(p.textMuted),
                           "Your mapping for every scene, kept on this computer and never in a project. A change here "
                           "changes it everywhere; to change it for this scene only, right-click it: Override in this "
                           "scene.");
    }
    if (status != nullptr && status->lastSeconds >= 0.0) {
        ImGui::TextDisabled("last input %.3f  ->  output %.4g  ·  %.1f s ago%s", static_cast<double>(status->lastInput),
                            static_cast<double>(status->lastOutput), hub.clockSeconds() - status->lastSeconds,
                            status->held > 0 ? fmt::format("  ·  {} note(s) held", status->held).c_str() : "");
    } else {
        ImGui::TextDisabled("nothing received for it yet");
    }

    // Every edit goes through editableMidiMapping: a user mapping is edited in place, a default gets its overriding copy.
    MidiBinding edit = *shown;
    bool changed = false;
    const auto& devices = hub.midiDevices();
    const float half = ImGui::GetContentRegionAvail().x * 0.5f - 8.0f;

    if (ImGui::BeginTable("##midi-edit-cols", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::PushItemWidth(std::min(220.0f, half * 0.6f));
        ImGui::SeparatorText("Source");
        // Device.
        {
            std::string preview = deviceLabel(edit);
            if (status != nullptr && status->device == control::MidiDeviceState::Absent) {
                preview += " (not connected)";
            }
            if (ImGui::BeginCombo("Device", preview.c_str())) {
                if (ImGui::Selectable("Any device", edit.device.empty() && (edit.source == "*" || edit.source.empty()))) {
                    edit.device.clear();
                    edit.deviceName.clear();
                    edit.source = "*";
                    changed = true;
                }
                for (std::size_t i = 0; i < devices.size(); ++i) {
                    const std::string label =
                        slotLabel(devices, i) + (devices[i].connected ? "" : "  (disconnected)") + "##dev" + std::to_string(i);
                    if (ImGui::Selectable(label.c_str(), edit.device == devices[i].id)) {
                        edit.device = devices[i].id;
                        edit.deviceName = devices[i].name;
                        edit.source = "*";
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered()) {
                tooltip("A specific device is matched by its system id; on another computer, by its name. Any device "
                        "is an intentional choice: two instruments sending the same message will both drive it.");
            }
        }
        // Channel.
        {
            int channel = edit.channel + 1;
            const char* channels[] = {"Any", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14",
                                      "15", "16"};
            if (ImGui::Combo("Channel", &channel, channels, 17)) {
                edit.channel = channel - 1;
                changed = true;
            }
        }
        // Type and number.
        int kind = static_cast<int>(edit.kind);
        if (ImGui::Combo("Type", &kind, kKindLabels, IM_ARRAYSIZE(kKindLabels))) {
            edit.kind = static_cast<MidiBindKind>(kind);
            changed = true;
        }
        if (control::midiBindKindHasNumber(edit.kind)) {
            const bool wide = edit.kind == MidiBindKind::Nrpn || edit.kind == MidiBindKind::Rpn;
            int number = edit.number;
            if (ImGui::InputInt(wide ? "Parameter" : (control::midiBindKindIsNote(edit.kind) ||
                                                      edit.kind == MidiBindKind::PolyPressure
                                                          ? "Note"
                                                          : "Number"),
                                &number)) {
                edit.number = std::clamp(number, -1, wide ? 16383 : 127);
                changed = true;
            }
            if (control::midiBindKindIsNote(edit.kind) || edit.kind == MidiBindKind::PolyPressure) {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", edit.number < 0 ? "any" : control::midiNoteName(edit.number).c_str());
                int high = edit.numberHigh;
                if (ImGui::InputInt("to note", &high)) {
                    edit.numberHigh = std::clamp(high, -1, 127);
                    changed = true;
                }
                ImGui::SameLine();
                ImGui::TextDisabled("%s", edit.numberHigh < 0 ? "(one note)" : control::midiNoteName(edit.numberHigh).c_str());
            } else if (edit.number < 0) {
                ImGui::SameLine();
                ImGui::TextDisabled("any");
            }
        }
        if (edit.kind == MidiBindKind::ControlChange) {
            int encoder = static_cast<int>(edit.encoder);
            if (ImGui::Combo("Encoder", &encoder, kEncoderLabels, IM_ARRAYSIZE(kEncoderLabels))) {
                edit.encoder = static_cast<control::MidiEncoder>(encoder);
                changed = true;
            }
            if (ImGui::IsItemHovered()) {
                tooltip("Absolute for knobs and faders. An endless encoder sends steps instead; which of the three "
                        "formats it uses is in its manual (it is never guessed).");
            }
        }
        ImGui::PopItemWidth();

        ImGui::TableNextColumn();
        ImGui::PushItemWidth(std::min(220.0f, half * 0.6f));
        ImGui::SeparatorText("Destination");
        pickerForRig_ = shown->scope == MidiScope::Rig;
        if (destinationPicker("##edit-destination", engine, edit.target, std::min(320.0f, half))) {
            changed = true;
        }
        pickerForRig_ = false;
        if (status != nullptr && status->inactiveForScene) {
            ImGui::TextDisabled("Inactive for this scene: it has no %s. Kept, and applied in every scene that has it.",
                                edit.target.parameter.c_str());
        } else if (status != nullptr && status->destinationMissing) {
            ImGui::TextColored(colour(p.warning), "Not in this scene: kept, and applied when a scene has it.");
        }
        if (!edit.target.parameter.empty()) {
            if (const params::IParameter* param = engine.params().find(edit.target.parameter);
                param != nullptr && param->componentCount() > 1) {
                int component = edit.target.component;
                if (ImGui::InputInt("Component", &component)) {
                    edit.target.component = std::clamp(component, -1, static_cast<int>(param->componentCount()) - 1);
                    changed = true;
                }
            }
        }
        const bool note = control::midiBindKindIsNote(edit.kind);
        const bool isAction = edit.target.action != control::MidiAction::None; // ADR-1705
        if (isAction) {
            ImGui::TextDisabled("Fires once per press: a note-on, a program change, or the control crossing half-way up.");
        }
        if (note && !isAction) {
            int behaviour = edit.kind == MidiBindKind::NoteEvent ? 2 : (edit.toggle ? 1 : 0);
            const char* behaviours[] = {"Gate (held = on)", "Toggle (each press flips)", "Trigger (one-frame pulse)"};
            if (ImGui::Combo("Behaviour", &behaviour, behaviours, 3)) {
                edit.kind = behaviour == 2 ? MidiBindKind::NoteEvent : MidiBindKind::Note;
                edit.toggle = behaviour == 1;
                changed = true;
            }
            if (!(edit.kind == MidiBindKind::Note && edit.toggle)) {
                changed |= ImGui::Checkbox("Velocity sets the level", &edit.velocity);
            }
            if (edit.kind == MidiBindKind::NoteEvent && edit.target.signal.empty()) {
                ImGui::TextColored(colour(p.warning),
                                   "A trigger on a parameter sets it and leaves it there. For a flash, map the trigger "
                                   "to a control channel and let a route shape it.");
            }
        }
        float range[2] = {edit.target.min, edit.target.max};
        if (!isAction && ImGui::DragFloat2(note ? "Off / on" : "Min / max", range, 0.01f)) {
            edit.target.min = range[0];
            edit.target.max = range[1];
            changed = true;
        }
        changed |= ImGui::Checkbox("Invert", &edit.invert);
        const bool relative = edit.kind == MidiBindKind::ControlChange && edit.encoder != control::MidiEncoder::Absolute;
        if (!relative && edit.kind != MidiBindKind::Program) {
            int curve = static_cast<int>(edit.curve);
            if (ImGui::Combo(note ? "Velocity curve" : "Curve", &curve, kCurveLabels, IM_ARRAYSIZE(kCurveLabels))) {
                edit.curve = static_cast<control::MidiCurve>(curve);
                changed = true;
            }
        }
        changed |= ImGui::SliderFloat(relative ? "Step size" : "Sensitivity", &edit.scale, 0.0f, 4.0f, "%.2f");
        if (!relative) {
            changed |= ImGui::SliderFloat("Offset", &edit.offset, -1.0f, 1.0f, "%.2f");
        }
        if (control::midiBindKindIsContinuous(edit.kind)) {
            changed |= ImGui::SliderFloat("Smoothing", &edit.smoothMs, 0.0f, 1000.0f, "%.0f ms");
            if (!relative) {
                changed |= ImGui::Checkbox("Pickup (soft takeover)", &edit.pickup);
                if (ImGui::IsItemHovered()) {
                    tooltip("Wait until the knob reaches the destination's value before following it, so the first "
                            "touch does not jump.");
                }
            }
        }
        // Combination: a property of the destination, shared by every mapping on it.
        const auto keys = control::midiDestinationKeys(edit.target);
        if (!keys.empty()) {
            const std::string& key = keys.back();
            int drivers = 0;
            for (const auto& r : control::resolveMidiMappings(map, hub.sceneKey())) {
                if (r.state != MidiMappingState::Active) continue;
                const auto other = control::midiDestinationKeys(r.binding->target);
                drivers += std::find(other.begin(), other.end(), key) != other.end() ? 1 : 0;
            }
            int policy = static_cast<int>(map.combineFor(key));
            ImGui::BeginDisabled(note && edit.kind == MidiBindKind::NoteEvent);
            if (ImGui::Combo("Combine", &policy, kCombineLabels, IM_ARRAYSIZE(kCombineLabels))) {
                map.setCombine(key, static_cast<control::MidiCombine>(policy));
                hub.mappingsChanged();
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered()) {
                tooltip("How several mappings on %s combine. Replace: the latest message wins. Max: the largest current "
                        "value. Add: their sum, clamped to the destination. Applies to every mapping on it.",
                        key.c_str());
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%d mapping(s) drive it", drivers);
        }
        ImGui::PopItemWidth();
        ImGui::EndTable();
    }

    // Actions.
    const bool enabled = control::midiMappingEnabled(map, shown->id);
    if (ImGui::Button(enabled ? "Disable" : "Enable")) {
        control::setMidiMappingEnabled(map, shown->id, !enabled);
        hub.mappingsChanged();
    }
    ImGui::SameLine();
    if (ImGui::Button("Duplicate")) {
        selected_ = control::duplicateMidiMapping(map, hub.sceneKey(), shown->id);
        hub.mappingsChanged();
        changed = false;
    }
    ImGui::SameLine();
    if (shown->scope == MidiScope::Default) {
        ImGui::BeginDisabled(enabled && (status == nullptr || status->state != MidiMappingState::Overridden));
        if (ImGui::Button("Restore default")) {
            control::restoreMidiDefault(map, shown->id);
            hub.mappingsChanged();
        }
        ImGui::EndDisabled();
    } else {
        if (!shown->overrides.empty()) {
            if (ImGui::Button("Restore default")) {
                const std::string def = shown->overrides;
                control::restoreMidiDefault(map, def);
                selected_ = def;
                hub.mappingsChanged();
                changed = false;
            }
            ImGui::SameLine();
        }
        if (ImGui::Button("Remove")) {
            control::removeMidiMapping(map, shown->id);
            selected_.clear();
            hub.mappingsChanged();
            changed = false;
        }
        ImGui::SameLine();
        const int was = shown->scope == MidiScope::Project ? 1 : (shown->scope == MidiScope::Rig ? 2 : 0);
        int scope = was;
        const char* scopes[] = {"Scene", "Project", "Rig"};
        ImGui::SetNextItemWidth(100.0f);
        if (ImGui::Combo("Scope", &scope, scopes, 3) && scope != was) {
            const std::string id = shown->id;
            if (scope == 2) {
                // ADR-1705: into the rig, out of the project.
                selected_ = control::promoteMidiMappingToRig(map, id);
                hub.mappingsChanged();
                changed = false;
            } else if (was == 2) {
                // Out of the rig into this project (this scene, or the whole project).
                if (control::demoteMidiRigMappingToScene(map, hub.sceneKey(), id) && scope == 1) {
                    if (MidiBinding* moved = map.findMidi(id)) {
                        moved->scope = MidiScope::Project;
                        moved->scene.clear();
                    }
                }
                hub.mappingsChanged();
                changed = false;
            } else {
                edit.scope = scope == 1 ? MidiScope::Project : MidiScope::Scene;
                edit.scene = scope == 1 ? std::string() : hub.sceneKey();
                changed = true;
            }
        }
        if (ImGui::IsItemHovered()) {
            tooltip("Scene: this project, this scene. Project: this project, every scene it shows. Rig: every scene and "
                    "project, kept on this computer.");
        }
    }
    if (changed && !selected_.empty()) {
        if (MidiBinding* target = control::editableMidiMapping(map, hub.sceneKey(), selected_)) {
            // The edit lands on the user mapping; its identity and layer stay its own.
            const std::string id = target->id;
            const MidiScope scope = shown->scope == MidiScope::Default ? target->scope : edit.scope;
            const std::string scene = shown->scope == MidiScope::Default ? target->scene : edit.scene;
            const std::string overrides = target->overrides;
            *target = edit;
            target->id = id;
            target->idAuthored = false;
            target->scope = scope;
            target->scene = scene;
            target->overrides = overrides;
            selected_ = id;
            hub.mappingsChanged();
        }
    }
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
}

// ---- Monitor ---------------------------------------------------------------------------------------------------------

void MidiPanel::drawMonitor(app::Engine& engine) {
    auto& hub = engine.control();
    auto& monitor = hub.monitor();
    const auto& devices = hub.midiDevices();
    const double now = hub.clockSeconds();
    const Palette& p = palette();

    if (ImGui::Button(paused_ ? "Resume" : "Pause")) {
        paused_ = !paused_;
        frozen_.clear();
        if (paused_) {
            for (std::size_t i = 0; i < monitor.size(); ++i) {
                frozen_.push_back(monitor.row(i));
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        monitor.clear();
        frozen_.clear();
        monitorSelected_ = -1;
    }
    ImGui::SameLine();
    {
        std::vector<std::string> labels{"All devices"};
        for (std::size_t i = 0; i < devices.size(); ++i) {
            labels.push_back(slotLabel(devices, i));
        }
        int current = std::clamp(monitorDevice_ + 1, 0, static_cast<int>(labels.size()) - 1);
        ImGui::SetNextItemWidth(160.0f);
        if (ImGui::BeginCombo("##mon-device", labels[static_cast<std::size_t>(current)].c_str())) {
            for (std::size_t i = 0; i < labels.size(); ++i) {
                if (ImGui::Selectable(labels[i].c_str(), static_cast<int>(i) == current)) {
                    monitorDevice_ = static_cast<int>(i) - 1;
                }
            }
            ImGui::EndCombo();
        }
    }
    ImGui::SameLine();
    {
        int channel = monitorChannel_ + 1;
        const char* channels[] = {"All ch", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14",
                                  "15", "16"};
        ImGui::SetNextItemWidth(70.0f);
        if (ImGui::Combo("##mon-channel", &channel, channels, 17)) {
            monitorChannel_ = channel - 1;
        }
    }
    ImGui::SameLine();
    const char* kinds[] = {"All types", "Notes", "CC", "Bend + pressure", "NRPN / RPN", "Program + system"};
    ImGui::SetNextItemWidth(130.0f);
    ImGui::Combo("##mon-kind", &monitorKind_, kinds, 6);
    ImGui::SameLine();
    bool coalesce = monitor.coalesce();
    if (ImGui::Checkbox("Merge repeats", &coalesce)) {
        monitor.setCoalesce(coalesce);
    }
    if (ImGui::IsItemHovered()) {
        tooltip("A knob sends a stream of values: shown as one row that updates (with a count) rather than a row each.");
    }
    ImGui::SameLine();
    bool clock = monitor.showClock();
    if (ImGui::Checkbox("Clock", &clock)) {
        monitor.setShowClock(clock);
    }
    ImGui::SameLine();
    double newest = -1.0;
    for (const auto& a : hub.deviceActivity()) {
        newest = std::max(newest, a.lastSeconds);
    }
    activityDot(now, newest);
    ImGui::SameLine();
    ImGui::TextDisabled(newest >= 0.0 && now - newest < 1.0 ? "receiving" : "idle");

    const auto passes = [&](const control::MidiMonitor::Row& row) {
        const control::MidiMessage& m = row.message;
        if (monitorDevice_ >= 0 && m.device != monitorDevice_) return false;
        const bool hasChannel = m.continuous() || m.kind == control::MidiKind::NoteOn ||
                                m.kind == control::MidiKind::NoteOff || m.kind == control::MidiKind::ProgramChange;
        if (monitorChannel_ >= 0 && (!hasChannel || m.channel != monitorChannel_)) return false;
        using K = control::MidiKind;
        switch (monitorKind_) {
        case 1: return m.kind == K::NoteOn || m.kind == K::NoteOff;
        case 2: return m.kind == K::ControlChange;
        case 3: return m.kind == K::PitchBend || m.kind == K::ChannelPressure || m.kind == K::PolyPressure;
        case 4: return m.kind == K::Nrpn || m.kind == K::Rpn || (m.flags & control::kMidiNrpnPart) != 0;
        case 5: return !m.continuous() && m.kind != K::NoteOn && m.kind != K::NoteOff;
        default: return true;
        }
    };
    std::vector<control::MidiMonitor::Row> rows;
    if (paused_) {
        for (const auto& r : frozen_) {
            if (passes(r)) rows.push_back(r);
        }
    } else {
        rows.reserve(monitor.size());
        for (std::size_t i = 0; i < monitor.size(); ++i) {
            if (passes(monitor.row(i))) rows.push_back(monitor.row(i));
        }
    }

    const float footer = ImGui::GetFrameHeightWithSpacing() * 1.2f;
    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY |
                                  ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Resizable;
    if (ImGui::BeginTable("##midi-monitor", 9, flags, ImVec2(0.0f, -footer))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Time");
        ImGui::TableSetupColumn("Device");
        ImGui::TableSetupColumn("Ch");
        ImGui::TableSetupColumn("Type");
        ImGui::TableSetupColumn("Number");
        ImGui::TableSetupColumn("Raw");
        ImGui::TableSetupColumn("Value");
        ImGui::TableSetupColumn("Meaning", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Mapped");
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(rows.size()));
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                // Newest first.
                const auto& row = rows[rows.size() - 1 - static_cast<std::size_t>(i)];
                const control::MidiMessage& m = row.message;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushID(i);
                const bool part = (m.flags & control::kMidiNrpnPart) != 0;
                if (row.ignored || part) {
                    ImGui::PushStyleColor(ImGuiCol_Text, colour(p.textDisabled));
                }
                if (ImGui::Selectable(fmt::format("{:8.3f}", row.seconds).c_str(), monitorSelected_ == i,
                                      ImGuiSelectableFlags_SpanAllColumns)) {
                    monitorSelected_ = i;
                }
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(m.device < devices.size() ? slotLabel(devices, m.device).c_str() : "-");
                ImGui::TableNextColumn();
                if (m.continuous() || m.kind == control::MidiKind::NoteOn || m.kind == control::MidiKind::NoteOff ||
                    m.kind == control::MidiKind::ProgramChange) {
                    ImGui::Text("%d", m.channel + 1);
                }
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(control::midiKindName(m.kind));
                ImGui::TableNextColumn();
                switch (m.kind) {
                case control::MidiKind::Nrpn:
                case control::MidiKind::Rpn: ImGui::Text("%u", m.param); break;
                case control::MidiKind::NoteOn:
                case control::MidiKind::NoteOff:
                case control::MidiKind::PolyPressure:
                    ImGui::Text("%u %s", m.data1, control::midiNoteName(m.data1).c_str());
                    break;
                case control::MidiKind::ControlChange:
                case control::MidiKind::ProgramChange: ImGui::Text("%u", m.data1); break;
                default: break;
                }
                ImGui::TableNextColumn();
                if (m.kind == control::MidiKind::PitchBend || m.kind == control::MidiKind::Nrpn || m.kind == control::MidiKind::Rpn) {
                    ImGui::Text("%u", m.value14);
                } else if (m.continuous() || m.kind == control::MidiKind::NoteOn) {
                    ImGui::Text("%u", m.data2);
                }
                ImGui::TableNextColumn();
                if (m.continuous() || m.kind == control::MidiKind::NoteOn) {
                    ImGui::Text("%.3f", static_cast<double>(m.normalized()));
                }
                ImGui::TableNextColumn();
                std::string meaning = control::midiMessageDescription(m);
                if (row.repeats > 1) meaning += fmt::format("   (x{})", row.repeats);
                if (part) meaning += "   (part of an NRPN/RPN)";
                if (row.ignored) meaning += "   (device switched off)";
                ImGui::TextUnformatted(meaning.c_str());
                ImGui::TableNextColumn();
                if (row.matched > 0) {
                    ImGui::TextColored(colour(p.success), "%u", row.matched);
                }
                if (row.ignored || part) {
                    ImGui::PopStyleColor();
                }
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    // The reverse workflow: a message seen here becomes the source to assign.
    const bool haveRow = monitorSelected_ >= 0 && monitorSelected_ < static_cast<int>(rows.size());
    ImGui::BeginDisabled(!haveRow ||
                         !control::MidiLearn::learnable(rows[rows.size() - 1 - static_cast<std::size_t>(std::max(0, monitorSelected_))].message));
    if (ImGui::Button("Learn from selected")) {
        hub.learn().captureFrom(rows[rows.size() - 1 - static_cast<std::size_t>(monitorSelected_)].message);
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        tooltip("Use the selected message as the detected source in the Learn bar, then pick its destination.");
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%zu of %zu rows (the last %zu are kept)", rows.size(), monitor.size(), control::MidiMonitor::kCapacity);
}

// ---- Devices ---------------------------------------------------------------------------------------------------------

void MidiPanel::drawDevices(app::Engine& engine) {
    auto& hub = engine.control();
    auto& map = hub.map();
    const auto status = hub.status();
    const auto& devices = hub.midiDevices();
    const auto& activity = hub.deviceActivity();
    const double now = hub.clockSeconds();
    const Palette& p = palette();

    if (!control::hasMidiBackend()) {
        ImGui::TextColored(colour(p.warning), "No MIDI backend on this platform.");
        return;
    }
    if (status.midiOpen) {
        ImGui::Text("Listening to %s  ·  %zu source(s) connected", map.midiFilter.empty() || map.midiFilter == "*"
                                                                       ? "all MIDI inputs"
                                                                       : fmt::format("inputs matching \"{}\"", map.midiFilter).c_str(),
                    status.midiSources.size());
    } else {
        ImGui::TextColored(colour(p.warning), "MIDI closed%s%s", status.midiError.empty() ? "" : ": ", status.midiError.c_str());
    }
    if (map.midiFilter != "*" && !map.midiFilter.empty()) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Listen to all inputs")) {
            map.midiEnabled = true;
            map.midiFilter = "*";
            hub.applyIo();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Rescan")) {
        map.midiEnabled = true;
        hub.applyIo(); // reopens: every current source is connected again (and a new one appears)
    }
    if (ImGui::IsItemHovered()) {
        tooltip("Reopen MIDI input. With all inputs selected a device that is plugged in connects by itself; this is "
                "for anything else.");
    }
    ImGui::TextDisabled("received %llu  ·  merged under load %llu  ·  dropped %llu  ·  ignored (switched off) %llu",
                        static_cast<unsigned long long>(status.midi.messages),
                        static_cast<unsigned long long>(status.midi.coalesced),
                        static_cast<unsigned long long>(status.midi.dropped), static_cast<unsigned long long>(status.ignored));

    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit |
                                  ImGuiTableFlags_ScrollY;
    if (ImGui::BeginTable("##midi-devices", 7, flags)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("On");
        ImGui::TableSetupColumn("Device", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Connection");
        ImGui::TableSetupColumn("Receiving");
        ImGui::TableSetupColumn("Channels 1-16");
        ImGui::TableSetupColumn("Messages");
        ImGui::TableSetupColumn("Clock");
        ImGui::TableHeadersRow();
        for (std::size_t i = 0; i < devices.size(); ++i) {
            const control::MidiDeviceSlot& d = devices[i];
            static const app::ControlHub::DeviceActivity none;
            const auto& a = i < activity.size() ? activity[i] : none;
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            bool on = hub.deviceEnabled(static_cast<std::uint16_t>(i));
            if (ImGui::Checkbox("##on", &on)) {
                std::vector<std::string> off = hub.disabledDevices();
                std::erase(off, d.id);
                if (!on) {
                    off.push_back(d.id);
                }
                hub.setDisabledDevices(off);
                if (onDisabledDevicesChanged) {
                    onDisabledDevicesChanged(off);
                }
            }
            if (ImGui::IsItemHovered()) {
                tooltip("Off: this device's messages are shown in the monitor and otherwise ignored, on this computer.");
            }
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(slotLabel(devices, i).c_str());
            if (ImGui::IsItemHovered()) {
                tooltip("id %s%s", d.id.c_str(), d.virtualSource ? " (virtual source)" : "");
            }
            ImGui::TableNextColumn();
            if (d.connected) {
                ImGui::TextColored(colour(p.success), "connected");
            } else {
                ImGui::TextDisabled("disconnected");
            }
            ImGui::TableNextColumn();
            // Receiving is about traffic, not connection: a connected keyboard nobody plays is not receiving.
            activityDot(now, a.lastSeconds);
            ImGui::SameLine();
            if (a.lastSeconds < 0.0) {
                ImGui::TextDisabled("nothing yet");
            } else if (now - a.lastSeconds < 1.0) {
                ImGui::TextUnformatted("receiving");
            } else {
                ImGui::TextDisabled("%.0f s ago", now - a.lastSeconds);
            }
            ImGui::TableNextColumn();
            {
                const float h = ImGui::GetTextLineHeight();
                const float cell = h * 0.7f;
                const ImVec2 at = ImGui::GetCursorScreenPos();
                ImDrawList* draw = ImGui::GetWindowDrawList();
                for (int c = 0; c < 16; ++c) {
                    const double last = a.channelSeconds[static_cast<std::size_t>(c)];
                    const double age = last < 0.0 ? 1e9 : now - last;
                    const float glow = age < 0.3 ? 1.0f : (age < 2.0 ? static_cast<float>(1.0 - (age - 0.3) / 1.7) * 0.6f : 0.0f);
                    const ImVec2 lo(at.x + static_cast<float>(c) * (cell + 2.0f), at.y + (h - cell) * 0.5f);
                    const ImU32 fill = last < 0.0 ? p.control : mixColour(p.raised, p.accent, glow);
                    draw->AddRectFilled(lo, ImVec2(lo.x + cell, lo.y + cell), fill, 2.0f);
                }
                ImGui::Dummy(ImVec2(16.0f * (cell + 2.0f), h));
                if (ImGui::IsItemHovered()) {
                    std::string used;
                    for (int c = 0; c < 16; ++c) {
                        if (a.channelSeconds[static_cast<std::size_t>(c)] >= 0.0) {
                            used += (used.empty() ? "" : ", ") + std::to_string(c + 1);
                        }
                    }
                    tooltip("Channels heard from: %s", used.empty() ? "none yet" : used.c_str());
                }
            }
            ImGui::TableNextColumn();
            ImGui::Text("%llu", static_cast<unsigned long long>(a.messages));
            ImGui::TableNextColumn();
            ImGui::Text("%llu", static_cast<unsigned long long>(a.clock));
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (devices.empty()) {
        ImGui::TextDisabled("No MIDI device has connected. Plug one in (with all inputs selected it connects on arrival), "
                            "or check the Live panel's MIDI input.");
    }
}

} // namespace avgen::ui
