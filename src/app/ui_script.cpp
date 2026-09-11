#include "app/ui_script.hpp"

#include "app/engine.hpp"
#include "platform/window.hpp"
#include "ui/control_panel.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>

namespace avgen::app {

namespace {

struct ArmName {
    std::string_view name;
    UiScriptArm arm;
};
constexpr std::array<ArmName, 7> kArms{{
    {"hover", UiScriptArm::Hover},
    {"sliders", UiScriptArm::Sliders},
    {"panels", UiScriptArm::Panels},
    {"select", UiScriptArm::Select},
    {"scrub", UiScriptArm::Scrub},
    {"camera", UiScriptArm::Camera},
    {"tabs", UiScriptArm::Tabs},
}};

// Pushes a motion event as though the device had produced it. SDL routes it to the window under
// the coordinates on its own; naming the window explicitly is what makes the run independent of
// where the pointer physically is, which matters because the machine running the benchmark is a
// machine somebody may be using.
void pushMotion(platform::Window& window, float x, float y) {
    SDL_Event e{};
    e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.timestamp = SDL_GetTicksNS();
    e.motion.windowID = window.id();
    e.motion.which = 0;
    e.motion.x = x;
    e.motion.y = y;
    SDL_PushEvent(&e);
}

void pushButton(platform::Window& window, float x, float y, bool down) {
    SDL_Event e{};
    e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    e.button.timestamp = SDL_GetTicksNS();
    e.button.windowID = window.id();
    e.button.which = 0;
    e.button.button = SDL_BUTTON_LEFT;
    e.button.down = down;
    e.button.clicks = 1;
    e.button.x = x;
    e.button.y = y;
    SDL_PushEvent(&e);
}

} // namespace

std::optional<UiScriptArm> parseUiScript(std::string_view spec) {
    if (spec.empty() || spec == "idle" || spec == "none") {
        return UiScriptArm::None;
    }
    if (spec == "all") {
        UiScriptArm all = UiScriptArm::None;
        for (const ArmName& a : kArms) {
            all = all | a.arm;
        }
        return all;
    }
    UiScriptArm out = UiScriptArm::None;
    std::size_t start = 0;
    while (start <= spec.size()) {
        const std::size_t comma = spec.find(',', start);
        const std::string_view token =
            spec.substr(start, comma == std::string_view::npos ? std::string_view::npos : comma - start);
        if (!token.empty()) {
            const auto it = std::find_if(kArms.begin(), kArms.end(),
                                         [token](const ArmName& a) { return a.name == token; });
            if (it == kArms.end()) {
                return std::nullopt;
            }
            out = out | it->arm;
        }
        if (comma == std::string_view::npos) {
            break;
        }
        start = comma + 1;
    }
    return out;
}

std::string uiScriptNames() {
    std::string out = "idle";
    for (const ArmName& a : kArms) {
        out += ",";
        out += a.name;
    }
    return out + ",all";
}

void UiScript::step(Engine& engine, ui::ControlPanel* panel, platform::Window& window, std::uint64_t frame) {
    if (arms_ == UiScriptArm::None) {
        return;
    }
    phase_ += 0.05f;
    const float w = static_cast<float>(window.pixelWidth()) / std::max(window.pixelScale(), 1e-3f);
    const float h = static_cast<float>(window.pixelHeight()) / std::max(window.pixelScale(), 1e-3f);

    if (has(arms_, UiScriptArm::Hover)) {
        // A Lissajous sweep rather than a straight line: it crosses the docked panels, the canvas
        // and the menu bar in an order that does not repeat every few frames, so no single widget's
        // hover path dominates the sample.
        const float x = w * (0.5f + 0.45f * std::sin(phase_));
        const float y = h * (0.5f + 0.45f * std::sin(phase_ * 0.61f));
        pushMotion(window, x, y);
    }

    if (has(arms_, UiScriptArm::Camera)) {
        // A drag on the canvas: press once, then move, then release, then press again. The canvas
        // is the centre of the window under the default layout.
        const float x = w * (0.5f + 0.12f * std::sin(phase_ * 0.7f));
        const float y = h * (0.42f + 0.08f * std::cos(phase_ * 0.7f));
        const std::uint64_t inCycle = frame % 120;
        if (inCycle == 0) {
            pushButton(window, x, y, true);
        } else if (inCycle == 110) {
            pushButton(window, x, y, false);
        } else if (inCycle < 110) {
            pushMotion(window, x, y);
        }
    }

    lastWrites_.clear();
    if (has(arms_, UiScriptArm::Sliders)) {
        // What a drag does: write one parameter's base value, every frame, the way the widget's own
        // `changed` branch does. Cycling the parameter rather than holding one is deliberate --
        // holding one measures a single dependency chain, and the complaint is about the editor.
        // AVGEN_SLIDER_FILTER=<prefix> restricts the drag to parameters whose path starts with it.
        // Bisecting by prefix is how a "changing a property costs 14 ms" observation is turned into
        // the name of the property that costs it, and the bisect has to be repeatable to be worth
        // anything, so it is a documented input rather than a temporary edit.
        static const char* const filterEnv = std::getenv("AVGEN_SLIDER_FILTER");
        const std::string_view filter = filterEnv != nullptr ? std::string_view(filterEnv) : std::string_view();
        auto& params = engine.params();
        const auto& ordered = params.ordered();
        if (!ordered.empty()) {
            for (int i = 0; i < 2; ++i) { // two per frame: a drag crosses several widgets
                params::IParameter* p = ordered[paramCursor_ % ordered.size()];
                ++paramCursor_;
                if (!p->flags().exposed) {
                    continue;
                }
                if (!filter.empty() && !std::string_view(p->path()).starts_with(filter)) {
                    continue;
                }
                const std::size_t n = std::min<std::size_t>(p->componentCount(), 4);
                for (std::size_t c = 0; c < n; ++c) {
                    const float lo = p->softMin(c);
                    const float hi = p->softMax(c);
                    const float t = 0.5f + 0.4f * std::sin(phase_ + static_cast<float>(c));
                    const float v = lo + (hi - lo) * t;
                    p->setBaseComponent(c, v);
                    if (c == 0) {
                        lastWrites_.push_back(p->path() + "=" + std::to_string(v));
                    }
                }
            }
        }
    }

    if (has(arms_, UiScriptArm::Scrub)) {
        const double duration = engine.durationSeconds() > 0.0 ? engine.durationSeconds() : 60.0;
        engine.seekSeconds(duration * (0.5 + 0.45 * static_cast<double>(std::sin(phase_ * 0.3f))));
    }

    if (panel == nullptr) {
        return;
    }

    if (has(arms_, UiScriptArm::Select)) {
        using Kind = ui::WorldSelection::Kind;
        static constexpr std::array<Kind, 5> kKinds{Kind::Node, Kind::Material, Kind::Camera,
                                                    Kind::Environment, Kind::Procedural};
        const auto* composition = engine.composition();
        if (composition != nullptr && (frame % 7) == 0) {
            panel->world.selection.kind = kKinds[(frame / 7) % kKinds.size()];
            const auto& nodes = composition->nodes();
            if (!nodes.empty()) {
                panel->world.selection.name = nodes[(frame / 7) % nodes.size()]->name;
            }
        }
    }

    if (has(arms_, UiScriptArm::Tabs)) {
        if ((frame % 11) == 0) {
            panel->world.layer = static_cast<ui::AuthoringLayer>(static_cast<int>((frame / 11) % 3));
        }
    }

    if (has(arms_, UiScriptArm::Panels)) {
        // Open and close one panel every few frames. Panel open/close is the interaction most
        // likely to cost something structural -- a dock rebuild, a first-use layout, a texture --
        // so it gets its own arm rather than being folded into the pointer sweep.
        const auto panels = ui::editorPanels();
        if (!panels.empty() && (frame % 20) == 0) {
            const ui::EditorPanel& target = panels[(frame / 20) % panels.size()];
            if (bool* slot = panel->layout().slot(target.id); slot != nullptr) {
                *slot = !*slot;
            }
        }
    }
}

} // namespace avgen::app
