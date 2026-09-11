#include "app/ui_script.hpp"

#include "app/engine.hpp"
#include "platform/window.hpp"
#include "app/placement.hpp"
#include "assets/asset_library.hpp"
#include "ui/control_panel.hpp"

#include <SDL3/SDL.h>

#include <fmt/format.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <string>
#include <utility>

namespace avgen::app {

namespace {

struct ArmName {
    std::string_view name;
    UiScriptArm arm;
};
constexpr std::array<ArmName, 8> kArms{{
    {"hover", UiScriptArm::Hover},
    {"sliders", UiScriptArm::Sliders},
    {"panels", UiScriptArm::Panels},
    {"select", UiScriptArm::Select},
    {"scrub", UiScriptArm::Scrub},
    {"camera", UiScriptArm::Camera},
    {"tabs", UiScriptArm::Tabs},
    {"edit", UiScriptArm::Edit},
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

    if (has(arms_, UiScriptArm::Edit)) {
        stepEdit(engine, *panel, window, frame);
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


// The world editor, driven the way a person drives it: with the pointer, over the canvas, through
// SDL. Nothing is called directly that a click would not reach -- the events go onto the process
// queue, are routed by Window::pumpEvents, seen by ImGui, gated by the canvas's hover state and
// answered by the editor inside the canvas window -- because the point of this arm is to prove the
// wiring, and a call straight into WorldEditor would prove only the arithmetic the unit tests
// already cover.
//
// **The press comes before the moves, and it has to.** Dear ImGui's SDL3 backend replaces the
// pointer position with the operating system's cursor at the top of every frame in which the window
// is focused and no mouse button is held (`ImGui_ImplSDL3_UpdateMouseData`). A synthetic motion on
// its own is therefore overwritten before the frame that would have used it -- which is why the
// first version of this arm reported "no ground under the cursor" while the physical mouse sat over
// a panel. While a button is down the backend leaves the position alone, so a press with a motion
// in the same frame pins the pointer for the whole of the drag. Anything this arm wants to check
// about the pointer it therefore checks mid-stroke.
//
// The script, in frames, over a single run:
//
//     2  restore the default layout, so "the middle of the canvas" means the same thing every run
//    30  arm the first asset in the library, Place mode, a scatter brush
//    34  motion + press together, low in the frame where there is ground
//  35-90 drag across the canvas, painting
//    44  report what the ghost says: position, slope, instance count, refusals
//    92  release -- one stroke, one undo step
//   110  count what was made
//   120  undo, and count again: it must be back to where it started
//   135  redo
//   150  Select mode, click
//   165  group two objects
//   180  undo the group
//   190  drag a selection box over most of the frame; 205 release, 212 report
void UiScript::stepEdit(Engine& engine, ui::ControlPanel& panel, platform::Window& window,
                        std::uint64_t frame) {
    const ui::CanvasRect& canvas = panel.canvas();
    if (!canvas.valid()) {
        return;
    }
    const auto at = [&](float u, float v) {
        return std::pair<float, float>(canvas.x + canvas.width * u, canvas.y + canvas.height * v);
    };
    const auto say = [&](std::string line) { editLog_.push_back(std::move(line)); };
    const auto nodes = [&] {
        const auto* composition = engine.composition();
        return composition != nullptr ? composition->nodeCount() : 0u;
    };

    ui::WorldEditor& editor = panel.editor;
    switch (frame) {
    case 2:
        // The layout this editor ships with, not whatever the machine happened to have saved. A
        // scripted run that clicks at "the middle of the canvas" has to know where the canvas is,
        // and a restored layout from another session can put a panel there -- the first attempt at
        // this arm opened an example project because its click landed in the Assets list.
        panel.restoreDefaultLayout();
        break;
    case 20:
        say(fmt::format("edit: canvas at ({:.0f},{:.0f}) {:.0f}x{:.0f} points", canvas.x, canvas.y,
                        canvas.width, canvas.height));
        break;
    case 30: {
        const assets::AssetLibrary* library = panel.worldBuilder.library();
        if (library == nullptr || library->size() == 0) {
            say("edit: no asset library, nothing to paint with");
            return;
        }
        editor.mode = ui::EditorMode::Place;
        editor.brushAssetId = library->assets().front().id;
        editor.brush.mode = PlacementMode::Brush;
        editor.brush.brushRadius = 6.0f;
        editor.brush.spacing = 2.0f;
        editor.brush.seed = 11u; // a fixed seed: a scripted run has to be the same run twice
        editor.brush.avoidCollisions = false; // the world is already full; this arm is about wiring
        editNodesBefore_ = nodes();
        say(fmt::format("edit: library has {} assets; armed '{}'; {} nodes to start", library->size(),
                        editor.brushAssetId, editNodesBefore_));
        break;
    }
    case 34: {
        // Low in the frame, where a camera framed on a subject is looking at ground rather than at
        // the horizon. Motion and press together: see the note above.
        const auto [x, y] = at(0.45f, 0.78f);
        pushMotion(window, x, y);
        pushButton(window, x, y, true);
        break;
    }
    case 44: {
        // Mid-stroke, which is when a person is actually looking at the ghost. Everything §22 asks
        // it to communicate, printed.
        const ui::BrushPreview& preview = editor.preview();
        const ImGuiIO& io = ImGui::GetIO();
        say(fmt::format("edit: ghost armed={} mouse=({:.0f},{:.0f}) ground=({:.1f}, {:.1f}, {:.1f}) "
                        "slope {:.0f} deg, {} instance(s), {} valid, {} blocked -- {}",
                        preview.armed, io.MousePos.x, io.MousePos.y, preview.ground.position.x,
                        preview.ground.position.y, preview.ground.position.z,
                        preview.ground.slopeDegrees, preview.instances.size(), preview.validCount,
                        preview.blockedCount, ui::previewSummary(preview)));
        if (!preview.instances.empty()) {
            const ui::GhostInstance& first = preview.instances.front();
            say(fmt::format("edit: first ghost at ({:.1f}, {:.1f}, {:.1f}), {:.2f} m tall, footprint "
                            "{:.2f} m, {}",
                            first.position.x, first.position.y, first.position.z, first.height,
                            first.footprintRadius, ui::placementIssueName(first.issue)));
        }
        break;
    }
    case 92: {
        const auto [x, y] = at(0.60f, 0.86f);
        pushMotion(window, x, y);
        pushButton(window, x, y, false);
        break;
    }
    case 110:
        say(fmt::format("edit: painted {} node(s) in one stroke; undo stack {} deep, top '{}'",
                        nodes() - editNodesBefore_, editor.history.undoSize(),
                        editor.history.undoLabel()));
        break;
    case 120:
        editor.undo(engine);
        say(fmt::format("edit: after undo, {} nodes (started at {})", nodes(), editNodesBefore_));
        break;
    case 135:
        editor.redo(engine);
        say(fmt::format("edit: after redo, {} nodes", nodes()));
        break;
    case 150: {
        editor.mode = ui::EditorMode::Select;
        const auto [x, y] = at(0.45f, 0.78f);
        pushMotion(window, x, y);
        pushButton(window, x, y, true);
        break;
    }
    case 151:
    case 152: {
        // A motion with every button event, press and release alike. Without one on the release
        // frame the pointer reverts to the operating system's cursor for exactly that frame, and
        // the click completes wherever the physical mouse is sitting -- which on one run of this
        // opened an example project out of a panel nobody had pointed at.
        const auto [x, y] = at(0.45f, 0.78f);
        pushMotion(window, x, y);
        if (frame == 152) {
            pushButton(window, x, y, false);
        }
        break;
    }
    case 160:
        say(fmt::format("edit: click selected {} -- '{}'", editor.selection.size(),
                        editor.selection.primary()));
        break;
    case 165: {
        // Two objects in hand, however the click went: a scripted click cannot guarantee it landed
        // on something, and the grouping half of this arm is about the command path rather than
        // about picking.
        const auto* composition = engine.composition();
        if (composition != nullptr) {
            std::vector<std::string> pair;
            for (const auto& node : composition->nodes()) {
                if (node && node->kind == scene::NodeKind::Gltf && pair.size() < 2) {
                    pair.push_back(node->name);
                }
            }
            editor.selection.set(pair);
        }
        editor.groupSelection(engine);
        say(fmt::format("edit: grouped -> '{}', {} nodes", editor.selection.primary(), nodes()));
        break;
    }
    case 180:
        editor.undo(engine);
        say(fmt::format("edit: ungrouped by undo, {} nodes, selection {}", nodes(),
                        editor.selection.size()));
        break;
    case 190: {
        // A drag box over most of the frame. Press and move together for the same reason as the
        // paint stroke: a synthetic pointer only survives while a button is held.
        editor.selection.clear();
        const auto [x, y] = at(0.06f, 0.10f);
        pushMotion(window, x, y);
        pushButton(window, x, y, true);
        break;
    }
    case 205: {
        // The motion goes with the release, not before it. On the frame the button comes up the
        // backend is free to overwrite the pointer with the operating system's cursor again, and
        // the box would be closed at wherever the physical mouse happens to be -- which is how the
        // first version of this closed a full-frame box into a degenerate one and selected the one
        // thing under the real cursor.
        const auto [x, y] = at(0.94f, 0.94f);
        pushMotion(window, x, y);
        pushButton(window, x, y, false);
        break;
    }
    case 212:
        // Of the nodes in the scene, not of the nodes in the *frame*: most of what a paint stroke
        // laid down at the bottom of the view projects outside the visible rectangle, and a box
        // that caught those would be a box that was not doing its job.
        say(fmt::format("edit: a box over the frame selected {} of {} objects",
                        editor.selection.size(), nodes()));
        break;
    default:
        break;
    }
    // The drag itself: every frame between the press and the release, so the stroke actually travels
    // and the brush lays down more than one dab.
    if (frame > 34 && frame < 92) {
        const float t = static_cast<float>(frame - 34) / 58.0f;
        const auto [x, y] = at(0.45f + 0.15f * t, 0.78f + 0.08f * t);
        pushMotion(window, x, y);
    }
    // ... and the box's own drag, which has to travel far enough to stop being a click.
    if (frame > 190 && frame < 205) {
        const float t = static_cast<float>(frame - 190) / 15.0f;
        const auto [x, y] = at(0.06f + 0.88f * t, 0.10f + 0.84f * t);
        pushMotion(window, x, y);
    }
}

} // namespace avgen::app
