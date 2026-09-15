#include "app/ui_script.hpp"

#include "app/engine.hpp"
#include "platform/window.hpp"
#include "app/placement.hpp"
#include "assets/asset_library.hpp"
#include "ui/control_panel.hpp"
#include "ui/gizmo.hpp"
#include "ui/world_probe.hpp"
#include "core/log.hpp"
#include "params/parameter.hpp"
#include "scene/composition.hpp"

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
constexpr std::array<ArmName, 11> kArms{{
    {"hover", UiScriptArm::Hover},
    {"sliders", UiScriptArm::Sliders},
    {"panels", UiScriptArm::Panels},
    {"select", UiScriptArm::Select},
    {"scrub", UiScriptArm::Scrub},
    {"camera", UiScriptArm::Camera},
    {"tabs", UiScriptArm::Tabs},
    {"edit", UiScriptArm::Edit},
    {"strip", UiScriptArm::Strip},
    {"gizmo", UiScriptArm::Gizmo},
    {"box", UiScriptArm::Box},
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

// Moves the *real* pointer, and therefore the one Dear ImGui will believe.
//
// `pushMotion` alone is not enough for an arm that has to press an exact spot. The SDL3 backend
// rewrites `io.MousePos` from the operating system's cursor at the top of every frame in which the
// window is focused and no button is held, and it queues that write *after* the synthetic motion
// the poll just delivered -- so on the one frame that matters, the frame of the press, the OS wins
// and the click lands wherever the physical mouse happens to be sitting.
//
// The Edit arm lives with that because a paint stroke only needs the *drag* to be in the right
// place. A scrub does not: the press is the gesture. And the failure is silent and total, because
// a click that lands on some other widget takes `ActiveId` with it, and ImGui then reports the
// strip as not hovered for the whole of the drag that follows -- so even the drag-to-scrub
// fallback never fires. The first run of the strip arm reported a scrub to 0.00 s for exactly this
// reason, which is what a probe is for (ADR-182).
//
// Warping makes the operating system agree with the script, so there is nothing left to overwrite.
void warpAndMove(platform::Window& window, float x, float y) {
    SDL_WarpMouseInWindow(window.handle(), x, y);
    pushMotion(window, x, y);
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

void releaseScriptedPointer(platform::Window& window) {
    pushButton(window, 0.0f, 0.0f, false);
}

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
        // The filter restricts the *cycle*, not only the writes.
        //
        // It used to advance the cursor over every parameter and write only the matching ones,
        // which meant a filter naming one slider wrote it on about one frame in a thousand -- so
        // `AVGEN_SLIDER_FILTER=env/sky` measured an editor that was almost always idle and reported
        // it as a sky drag. "Dragging one slider" has to mean writing that slider every frame,
        // because that is what a hand on a slider does and it is the whole question being asked.
        if (!sliderTargetsResolved_) {
            sliderTargetsResolved_ = true;
            for (params::IParameter* p : params.ordered()) {
                if (p == nullptr || !p->flags().exposed) {
                    continue;
                }
                if (!filter.empty() && !std::string_view(p->path()).starts_with(filter)) {
                    continue;
                }
                sliderTargets_.push_back(p);
            }
            if (!filter.empty()) {
                log::info("ui-script sliders: {} parameter(s) under '{}'", sliderTargets_.size(), filter);
            }
        }
        const std::vector<params::IParameter*>& ordered = sliderTargets_;
        if (!ordered.empty()) {
            for (int i = 0; i < 2; ++i) { // two per frame: a drag crosses several widgets
                params::IParameter* p = ordered[paramCursor_ % ordered.size()];
                ++paramCursor_;
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

    if (has(arms_, UiScriptArm::Strip)) {
        stepStrip(engine, *panel, window, frame);
    }

    if (has(arms_, UiScriptArm::Gizmo)) {
        stepGizmo(engine, *panel, window, frame);
    }

    if (has(arms_, UiScriptArm::Box)) {
        stepBox(engine, *panel, window, frame);
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
                        nodes() - editNodesBefore_, editor.history().undoSize(),
                        editor.history().undoLabel()));
        break;
    case 120:
        if (app::EditSystem* edits = editor.edits(); edits != nullptr) {
            static_cast<void>(edits->execute(app::EditAction::Undo, engine));
        }
        say(fmt::format("edit: after undo, {} nodes (started at {})", nodes(), editNodesBefore_));
        break;
    case 135:
        if (app::EditSystem* edits = editor.edits(); edits != nullptr) {
            static_cast<void>(edits->execute(app::EditAction::Redo, engine));
        }
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
        if (app::EditSystem* edits = editor.edits(); edits != nullptr) {
            static_cast<void>(edits->execute(app::EditAction::Undo, engine));
        }
        say(fmt::format("edit: ungrouped by undo, {} nodes, selection {}", nodes(),
                        editor.selection.size()));
        break;
    case 190: {
        // A drag box over most of the frame. Press and move together for the same reason as the
        // paint stroke: a synthetic pointer only survives while a button is held.
        editor.selection.clear();
        // Where the camera was before the drag. A bare left drag is the *editor's* gesture now
        // (ui::viewportIntent), and this records the defect it fixed: the box selected and the
        // camera orbited under it at the same time, so the box could not be aimed. A camera that
        // has moved by the end of a box drag is that regression, in one line.
        boxCamera_ = engine.scene().camera.position;
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
        say(fmt::format("edit: the camera moved {:.3f} m during the box drag (0 means the box had "
                        "the pointer to itself)",
                        glm::length(engine.scene().camera.position - boxCamera_)));
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




// The sequencer strip, driven the way a person drives it (the brief's scenario C).
//
// The same rules as the Edit arm, and the same reason for them: the events go onto the SDL queue
// and travel the whole path, and a press is issued with a motion in the same frame because Dear
// ImGui's SDL3 backend overwrites the pointer position from the operating system's cursor on any
// frame where the window is focused and no button is held. A synthetic motion on its own never
// survives to the frame that would have used it.
//
// The script, in frames:
//
//     2  restore the default layout, so the Sequence panel is where it ships
//     4  open it, in case a saved layout had it closed
//    20  report where the strip ended up
//    30  press on the ruler and scrub along it to 80 -- the scrub half of scenario C
//    82  release
//    90  press on the shots lane and drag a shot to 140 -- the drag half
//   142  release, which is the frame the bake happens on
//   150  report what moved
void UiScript::stepStrip(Engine& engine, ui::ControlPanel& panel, platform::Window& window,
                         std::uint64_t frame) {
    const auto say = [&](std::string line) { editLog_.push_back(std::move(line)); };
    const ui::SequencePanel::StripRect& strip = panel.sequence.stripRect();

    if (frame == 2) {
        panel.restoreDefaultLayout();
        return;
    }
    if (frame == 4 || frame == 5) {
        // Open *and* bring to the front. The bottom dock holds Sequence, Control, Analysis,
        // Modulation and Graph as tabs, and opening a panel does not make it the active one -- a
        // background tab's `Begin` returns false and its body never runs at all.
        //
        // That was the whole of the first failure, and it was invisible because `stripRect_` keeps
        // its last value: the panel laid out once while the default layout was being rebuilt, and
        // every later report was that stale frame's rectangle. The pointer really was on the
        // strip's remembered coordinates; the strip simply was not being drawn there any more.
        if (bool* slot = panel.layout().slot("Sequence"); slot != nullptr) {
            *slot = true;
        }
        ImGui::SetWindowFocus("Sequence");
        return;
    }
    if (!strip.valid()) {
        if (frame == 20) {
            say("strip: the Sequence panel never laid out; nothing to drive");
        }
        return;
    }
    // A point on the strip's time axis, `u` of the way across it. Never in the gutter: a click
    // there is a header and means something else entirely.
    const auto at = [&](float u, float localY) {
        return std::pair<float, float>(strip.x + strip.gutter + (strip.width - strip.gutter) * u,
                                       strip.y + localY);
    };

    switch (frame) {
    case 20:
        say(fmt::format("strip: at ({:.0f},{:.0f}) {:.0f}x{:.0f} points, gutter {:.0f}, {:.0f} points of "
                        "toolbar above it, {:.0f} visible", strip.x, strip.y, strip.width, strip.height,
                        strip.gutter, strip.toolbarHeight, strip.visibleHeight));
        break;
    case 26:
    case 27:
    case 28:
    case 29: {
        // Park the pointer on the target for a few frames *before* pressing. The backend rewrites
        // `io.MousePos` from the OS cursor on every frame with no button held, so the only way the
        // press frame sees the right position is for the OS cursor to already be there -- and
        // ImGui's `HoveredWindow` is computed from that position, so it also needs a frame to
        // become the Sequence panel. Pressing on the first frame of a move satisfies neither.
        const auto [x, y] = at(0.15f, strip.rulerHeight * 0.5f);
        warpAndMove(window, x, y);
        break;
    }
    case 30: {
        const auto [x, y] = at(0.15f, strip.rulerHeight * 0.5f);
        warpAndMove(window, x, y);
        pushButton(window, x, y, true);
        break;
    }
    case 82: {
        const auto [x, y] = at(0.70f, strip.rulerHeight * 0.5f);
        warpAndMove(window, x, y);
        pushButton(window, x, y, false);
        break;
    }
    case 50: {
        // Mid-stroke, like the Edit arm's report: what the pointer actually is, and whether the
        // press reached the panel at all. A probe that cannot show it established the state it
        // measures is worth nothing (ADR-182), and the first run of this arm reported a scrub to
        // 0.00 s -- which is exactly what "the click never landed" looks like.
        const ImGuiIO& io = ImGui::GetIO();
        say(fmt::format("strip: mid-scrub mouse=({:.0f},{:.0f}) down={} hovered={} visible={:.0f}/{:.0f} clock={:.2f} s",
                        io.MousePos.x, io.MousePos.y, io.MouseDown[0], strip.hovered,
                        strip.visibleHeight, strip.height, engine.timelineClock().seconds));
        break;
    }
    case 84:
        say(fmt::format("strip: scrubbed to {:.2f} s", engine.timelineClock().seconds));
        break;
    case 86:
    case 87:
    case 88:
    case 89: {
        const auto [x, y] = at(0.10f, strip.shotsTop + 10.0f);
        warpAndMove(window, x, y);
        break;
    }
    case 90: {
        const auto [x, y] = at(0.10f, strip.shotsTop + 10.0f);
        warpAndMove(window, x, y);
        pushButton(window, x, y, true);
        break;
    }
    case 142: {
        const auto [x, y] = at(0.45f, strip.shotsTop + 10.0f);
        warpAndMove(window, x, y);
        pushButton(window, x, y, false);
        break;
    }
    case 150: {
        const seq::Sequence& piece = engine.sequence();
        say(fmt::format("strip: {} shot(s); first starts at {:.2f} s", piece.shots.size(),
                        piece.shots.empty() ? -1.0 : piece.shots.front().startSeconds));
        break;
    }
    default:
        // Between the presses: keep the pointer where it is travelling to, every frame, which is
        // what makes it a drag rather than a teleport.
        if (frame > 30 && frame < 82) {
            const float u = 0.15f + 0.55f * static_cast<float>(frame - 30) / 52.0f;
            const auto [x, y] = at(u, strip.rulerHeight * 0.5f);
            warpAndMove(window, x, y);
        } else if (frame > 90 && frame < 142) {
            const float u = 0.10f + 0.35f * static_cast<float>(frame - 90) / 52.0f;
            const auto [x, y] = at(u, strip.shotsTop + 10.0f);
            warpAndMove(window, x, y);
        }
        break;
    }
}

// ---- gizmo: moving an object with the handles ---------------------------------------------------
//
// A cycle of 60 frames: select, press on the X arm, drag for forty frames, let go. It repeats so a
// block of frames measures the *drag*, which is the thing the complaint is about -- a one-shot
// script would put one drag frame in ninety and report the idle editor.
//
// Aimed at the handle rather than at a fixed point on the canvas, and aimed through the same
// projection the overlay draws it with (`ui::projectPoint`), so the press lands on the arm at
// whatever distance the camera happens to be. A gizmo arm is a few points wide; guessing at it
// would be an arm that silently measured an empty canvas, which ADR-182 calls worse than no arm.
void UiScript::stepGizmo(Engine& engine, ui::ControlPanel& panel, platform::Window& window,
                         std::uint64_t frame) {
    const ui::CanvasRect& canvas = panel.canvas();
    scene::Composition* composition = engine.composition();
    if (!canvas.valid() || composition == nullptr) {
        return;
    }
    ui::WorldEditor& editor = panel.editor;
    const std::uint64_t inCycle = frame % 60;
    const scene::Camera& camera = engine.scene().camera;
    const float aspect = canvas.height > 0.0f ? canvas.width / canvas.height : 1.0f;
    if (inCycle == 0) {
        // Something with geometry that is **on screen**, re-chosen every cycle.
        //
        // The first version took the first Gltf node and kept whatever was already selected, and a
        // nine-arm run caught it doing nothing at all: the box arm ahead of it had cleared the
        // selection, the first Gltf node happened to be one standing off past the edge of the
        // frame, and the arm pressed at (-20092, 26181) for three blocks running while reporting a
        // perfectly healthy frame time. The probe is what noticed; this is the repair.
        editor.mode = ui::EditorMode::Select;
        editor.gizmoMode = ui::GizmoMode::Move;
        editor.selection.clear();
        // The candidate nearest the middle of the frame, not the first one that is merely inside
        // it: the X arm reaches out from the origin and has to land somewhere a person could have
        // clicked, so the further from centre the subject is the likelier the grab point is off the
        // canvas entirely.
        std::string best;
        float bestDistance = 0.9f; // and no further out than this, or there is no arm to grab
        for (const auto& node : composition->nodes()) {
            if (!node || node->locked || node->kind == scene::NodeKind::Terrain ||
                node->kind == scene::NodeKind::Group) {
                continue;
            }
            const scene::WorldBounds bounds = composition->nodeBounds(node->name);
            if (!bounds.valid) {
                continue;
            }
            const ui::Projected at = ui::projectPoint(camera, aspect, bounds.centre());
            if (!at.inFront) {
                continue;
            }
            const float distance = std::max(std::abs(at.ndc.x), std::abs(at.ndc.y));
            if (distance < bestDistance) {
                bestDistance = distance;
                best = node->name;
            }
        }
        if (best.empty()) {
            if (gizmoDrags_ == 0 && !saidNoSubject_) {
                saidNoSubject_ = true;
                editLog_.emplace_back("gizmo: nothing selectable is in frame; this arm measured nothing");
            }
            return;
        }
        editor.selection.set({best});
        return;
    }
    if (editor.selection.empty()) {
        return;
    }
    const ui::EditorVisuals& visuals = editor.visuals();
    const auto [from, to] = ui::axisSegment(visuals.gizmo, ui::GizmoHandle::AxisX);
    // Where along the arm to press, decided by asking the editor's own hit test rather than by
    // picking a fraction and hoping. `pickHandle` with `kHandlePickRadius` is literally the
    // function `WorldEditor::updateGizmo` runs against the live pointer, so a point this loop
    // accepts is a point the editor will accept -- which turns "the press landed on the handle"
    // from an assumption into a construction. A fixed two-thirds looked right and missed: the
    // plane handles crowd the inner arm and the arrow head narrows the outer one, and which
    // fraction is clear depends on the angle the arm is seen at.
    ui::Projected grab{};
    bool aimed = false;
    for (int step = 0; step <= 12 && !aimed; ++step) {
        const float t = 0.35f + 0.05f * static_cast<float>(step);
        const ui::Projected at = ui::projectPoint(camera, aspect, glm::mix(from, to, t));
        if (!at.inFront) {
            continue;
        }
        if (ui::pickHandle(camera, aspect, visuals.gizmo, ui::GizmoMode::Move, at.ndc,
                           ui::kHandlePickRadius) == ui::GizmoHandle::AxisX) {
            grab = at;
            aimed = true;
        }
    }
    if (!aimed) {
        return;
    }
    const float gx = canvas.x + (grab.ndc.x * 0.5f + 0.5f) * canvas.width;
    const float gy = canvas.y + (0.5f - grab.ndc.y * 0.5f) * canvas.height;
    if (inCycle < 4) {
        // Three frames of hovering before the press, and they are not padding. Dear ImGui resolves
        // which window is hovered from the *previous* frame's layout, and `WorldEditor` only looks
        // at a press when `input.overCanvas` is already true -- so a press delivered in the same
        // frame as the pointer's first appearance over the canvas is a press the editor never sees.
        // Without these the arm pressed exactly on the X arm, by construction, and reported the
        // handle it was dragging as `none` for every block of every run.
        warpAndMove(window, gx, gy);
    } else if (inCycle == 4) {
        warpAndMove(window, gx, gy);
        pushButton(window, gx, gy, true);
    } else if (inCycle > 4 && inCycle < 50) {
        // A there-and-back sweep along the screen, so the object ends the cycle near where it
        // started and a hundred cycles do not walk it out of the world.
        const float t = static_cast<float>(inCycle - 4) / 45.0f;
        const float travel = canvas.width * 0.12f * std::sin(t * 6.2831853f);
        warpAndMove(window, gx + travel, gy);
        if (visuals.dragging != ui::GizmoHandle::None) {
            gizmoDragged_ = visuals.dragging; // sampled mid-drag: `hovered` is None while dragging
        }
    } else if (inCycle == 50) {
        warpAndMove(window, gx, gy);
        pushButton(window, gx, gy, false);
        ++gizmoDrags_;
        // An arm that cannot fail is worse than no arm (ADR-182). A press two points off a handle
        // measures an empty canvas and reports a lovely frame time, so the arm says once what it
        // actually achieved: whether a drag opened, and how far the object moved.
        if (gizmoDrags_ == 1) {
            const scene::CompositionNode* node = composition->findNode(editor.selection.primary());
            const glm::vec3 now = node != nullptr ? composition->nodeWorldTransform(*node).position
                                                  : glm::vec3(0.0f);
            editLog_.push_back(fmt::format(
                "gizmo: '{}' grabbed at ({:.0f},{:.0f}); the handle being dragged was {}; the "
                "object ended the cycle at ({:.2f}, {:.2f}, {:.2f})",
                editor.selection.primary(), gx, gy, ui::gizmoHandleName(gizmoDragged_), now.x, now.y, now.z));
        }
    }
}

// ---- box: dragging a selection rectangle --------------------------------------------------------
//
// The other canvas gesture the brief names. A cycle of 60: clear, press near one corner, sweep to
// the other, release. The sweep is what costs -- every frame of it re-tests what is inside the
// rectangle -- so the frames in the middle are the measurement and the press is not.
void UiScript::stepBox(Engine& engine, ui::ControlPanel& panel, platform::Window& window,
                       std::uint64_t frame) {
    const ui::CanvasRect& canvas = panel.canvas();
    if (!canvas.valid() || engine.composition() == nullptr) {
        return;
    }
    const auto at = [&](float u, float v) {
        return std::pair<float, float>(canvas.x + canvas.width * u, canvas.y + canvas.height * v);
    };
    ui::WorldEditor& editor = panel.editor;
    const std::uint64_t inCycle = frame % 60;
    if (inCycle == 0) {
        editor.mode = ui::EditorMode::Select;
        editor.selection.clear();
        const auto [x, y] = at(0.06f, 0.10f);
        warpAndMove(window, x, y);
        pushButton(window, x, y, true);
    } else if (inCycle < 50) {
        const float t = static_cast<float>(inCycle) / 50.0f;
        const auto [x, y] = at(0.06f + 0.88f * t, 0.10f + 0.84f * t);
        warpAndMove(window, x, y);
        boxOpened_ = boxOpened_ || editor.visuals().boxing;
    } else if (inCycle == 50) {
        const auto [x, y] = at(0.94f, 0.94f);
        warpAndMove(window, x, y);
        pushButton(window, x, y, false);
        ++boxDrags_;
        if (boxDrags_ == 1) {
            editLog_.push_back(
                fmt::format("box: the editor had a box open mid-drag: {}; it selected {} of {} object(s)",
                            boxOpened_, editor.selection.size(), engine.composition()->nodeCount()));
        }
    }
}

} // namespace avgen::app
