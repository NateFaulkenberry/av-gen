#include "core/interaction_latency.hpp"
#include "app/ui_script.hpp"

#include "app/engine.hpp"
#include "platform/window.hpp"
#include "app/placement.hpp"
#include "assets/asset_library.hpp"
#include "ui/control_panel.hpp"
#include "ui/gizmo.hpp"
#include "ui/world_probe.hpp"
#include "ui/world_edit.hpp"
#include "ui/sequence_panel.hpp"
#include "ai/control_plane.hpp"
#include "directing/plan.hpp"
#include "app/edit_system.hpp"
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
constexpr std::array<ArmName, 19> kArms{{
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
    {"star", UiScriptArm::Star},
    {"click", UiScriptArm::Click},
    {"drag", UiScriptArm::Drag},
    {"slicemenu", UiScriptArm::SliceMenu},
    {"slice", UiScriptArm::Slice},
    {"director-reject", UiScriptArm::DirectorReject},
    {"director-accept", UiScriptArm::DirectorAccept},
    {"director-record", UiScriptArm::DirectorRecord},
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

void pushButton(platform::Window& window, float x, float y, bool down,
                std::uint8_t which = SDL_BUTTON_LEFT) {
    SDL_Event e{};
    e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    e.button.timestamp = SDL_GetTicksNS();
    e.button.windowID = window.id();
    e.button.which = 0;
    e.button.button = which;
    e.button.down = down;
    e.button.clicks = 1;
    e.button.x = x;
    e.button.y = y;
    SDL_PushEvent(&e);
}

// There is deliberately no `pushKey` here. A synthetic `SDL_EVENT_KEY_DOWN` pushed with
// `SDL_PushEvent` was written, tried and removed: it never reached the ImGui backend at all, while
// the synthetic *button* events beside it worked -- so a scripted modifier goes in through
// `ImGui::GetIO().AddKeyEvent`, which is where the backend would have put it anyway, and nothing
// overwrites it because `ImGui_ImplSDL3_UpdateKeyModifiers` runs only from a real key event. See
// `stepSlice`, which is the only arm that holds one.

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
        // **Option is held for the whole gesture, and without it this arm was never the camera.**
        // `ui::viewportIntent` maps a plain left drag to `EditorPointer`; only `left && alt`
        // reaches `CameraOrbit`. So for as long as this arm has existed it pushed a left drag over
        // the canvas, drove the editor's selection and gizmo path, and was called `camera` -- which
        // is also why `Interaction::CameraOrbit` could sit in the latency log with no call site and
        // no one notice that nothing was exercising it. Measured: with the modifier, the arm
        // produces camera-orbit records; without it, none at all.
        //
        // `SDL_SetModState` rather than a synthetic key event, because the handler asks
        // `SDL_GetModState()` at the moment it classifies the press -- a key event pushed onto the
        // queue is read later and would arrive after the decision it is supposed to inform.
        if (inCycle == 0) {
            SDL_SetModState(SDL_KMOD_LALT);
            pushButton(window, x, y, true);
        } else if (inCycle == 110) {
            pushButton(window, x, y, false);
            // Cleared on release and not merely at the end of the run: an arm that leaves a
            // modifier latched changes what every arm after it means, and `--ui-ab` interleaves
            // them inside one process.
            SDL_SetModState(SDL_KMOD_NONE);
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
            // The arm writes the parameter directly, which is exactly what a widget's own `changed`
            // branch does, so it opens the record that branch would. Without an input event there
            // is no T0, and `input->ack` reports unavailable rather than a number -- which is the
            // honest answer for a value arm and not a hole in the table.
            core::interactions().beginWithoutInput(core::Interaction::PropertyDrag);
            core::interactions().markCommand();
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
            // T3: the parameter set holds the new base values. Everything downstream -- resetFinals,
            // the timeline, the modulation routes, `controller_->update()` -- happens in the next
            // frame's `Engine::update`, which is where T4 lands.
            core::interactions().markModel();
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
            core::interactions().beginWithoutInput(core::Interaction::Selection);
            core::interactions().markCommand();
            panel->world.selection.kind = kKinds[(frame / 7) % kKinds.size()];
            const auto& nodes = composition->nodes();
            if (!nodes.empty()) {
                panel->world.selection.name = nodes[(frame / 7) % nodes.size()]->name;
            }
            // Selection is panel state and nothing else; there is no engine call to make, so the
            // model change is the assignment above. That it is the same instant as the command is
            // the finding, not a gap in the instrument.
            core::interactions().markModel();
        }
    }

    if (has(arms_, UiScriptArm::Tabs)) {
        if ((frame % 11) == 0) {
            core::interactions().beginWithoutInput(core::Interaction::TabSwitch);
            core::interactions().markCommand();
            panel->world.layer = static_cast<ui::AuthoringLayer>(static_cast<int>((frame / 11) % 3));
            core::interactions().markModel();
        }
    }

    if (has(arms_, UiScriptArm::Edit)) {
        stepEdit(engine, *panel, window, frame);
    }

    if (has(arms_, UiScriptArm::SliceMenu)) {
        stepSliceMenu(engine, *panel, window, frame);
    }
    if (has(arms_, UiScriptArm::Slice)) {
        stepSlice(engine, *panel, window, frame);
    }
    if (panel != nullptr && has(arms_, UiScriptArm::DirectorRecord)) {
        stepDirectorRecord(engine, *panel, window, frame);
    }
    if (panel != nullptr && (has(arms_, UiScriptArm::DirectorReject) || has(arms_, UiScriptArm::DirectorAccept))) {
        stepDirector(engine, *panel, window, frame, has(arms_, UiScriptArm::DirectorAccept));
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

    if (has(arms_, UiScriptArm::Star)) {
        stepStar(engine, *panel, frame);
    }

    if (has(arms_, UiScriptArm::Click)) {
        stepClick(engine, *panel, window, frame);
    }

    if (has(arms_, UiScriptArm::Drag)) {
        stepDrag(engine, *panel, window, frame);
    }

    if (has(arms_, UiScriptArm::Panels)) {
        // Open and close one panel every few frames. Panel open/close is the interaction most
        // likely to cost something structural -- a dock rebuild, a first-use layout, a texture --
        // so it gets its own arm rather than being folded into the pointer sweep.
        const auto panels = ui::editorPanels();
        if (!panels.empty() && (frame % 20) == 0) {
            const ui::EditorPanel& target = panels[(frame / 20) % panels.size()];
            if (bool* slot = panel->layout().slot(target.id); slot != nullptr) {
                core::interactions().beginWithoutInput(core::Interaction::PanelToggle);
                core::interactions().markCommand();
                *slot = !*slot;
                core::interactions().markModel();
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
// The strip's context menu, opened on a shot and left open (ADR-356).
//
// A capture arm, not a measurement one. The slice tool's rows live in a popup, and a popup only
// exists on the frames it is up -- so `--capture-ui` on an idle editor photographs a strip with no
// menu on it, which is a picture of nothing this change touched. This opens one and then stops,
// so every frame after the release has the menu on screen and any capture frame will do.
//
//     tools/gpu-lock.sh ./build/release/src/avgen --project examples/camera/behaviors.json \
//         --ui-script slicemenu --capture-ui out.png --capture-ui-frame 120
//
// Timing copied from `stepStrip` and for its reasons: the layout is restored first, the Sequence
// tab is raised (a background tab's body never runs, so `stripRect_` would be a stale frame's), and
// the pointer is parked on the target for several frames before the press, because the SDL3 backend
// rewrites `io.MousePos` from the OS cursor on any frame with no button held.
// Cmd+click on a shot: the slice tool's actual gesture (ADR-356).
//
// The menu capture shows the row exists. This shows the *click* works, which is a different claim
// and the one that cannot be tested any other way -- `ui::planSlice` is covered by unit tests, and
// everything between an SDL button event and that function is not.
//
//     tools/gpu-lock.sh ./build/release/src/avgen --project examples/camera/behaviors.json \
//         --ui-script slice --frames 160 --headless=false
//
// It says the shot count before and after in so many words, including when nothing happened, for
// the reason the Star arm says "THIS ARM MEASURED NOTHING": a probe that cannot report its own
// failure is not a probe (ADR-182).
void UiScript::stepSlice(Engine& engine, ui::ControlPanel& panel, platform::Window& window,
                         std::uint64_t frame) {
    const ui::SequencePanel::StripRect& strip = panel.sequence.stripRect();
    if (frame == 2) {
        panel.restoreDefaultLayout();
        return;
    }
    if (frame == 4 || frame == 5) {
        if (bool* slot = panel.layout().slot("Sequence"); slot != nullptr) {
            *slot = true;
        }
        ImGui::SetWindowFocus("Sequence");
        return;
    }
    if (!strip.valid()) {
        if (frame == 20) {
            editLog_.emplace_back("slice: the Sequence panel never laid out; THIS ARM MEASURED NOTHING");
        }
        return;
    }
    const float x = strip.x + strip.gutter + (strip.width - strip.gutter) * 0.25f;
    // The audio lane when the project has clips, the shots lane otherwise -- the same rule
    // `stepSliceMenu` uses, so one arm proves the gesture on the two lanes that matter by choice of
    // `--project`. The audio lane is the one worth proving: ADR-103 keeps it a scrub, and the claim
    // that a Cmd+click does not take the click that scrubs is a claim about this lane specifically.
    const bool onAudio = !engine.audioClips().empty();
    const float y = onAudio
                        ? strip.y + strip.shotsTop - ui::kStripLaneGap -
                              ui::kStripAudioLaneHeight * 0.5f
                        : strip.y + strip.shotsTop + ui::kStripLaneHeight * 0.5f;
    const auto count = [&] {
        return onAudio ? engine.audioClips().size() : engine.sequence().shots.size();
    };
    const char* const noun = onAudio ? "audio clip(s)" : "shot(s)";
    switch (frame) {
    case 20:
        sliceShotsBefore_ = count();
        editLog_.push_back(fmt::format("slice: {} {} before the gesture", sliceShotsBefore_, noun));
        break;
    case 26:
    case 27:
    case 28:
    case 29:
    case 30:
    case 31:
        // Parked on the target first, for the reason `warpAndMove` documents: with no button held
        // the backend rewrites the pointer from the OS cursor, so the press frame must find the real
        // cursor already there.
        warpAndMove(window, x, y);
        break;
    // **Cmd is held through ImGui's own input queue, not as an SDL key event**, and that is a
    // finding rather than a shortcut. A synthetic `SDL_EVENT_KEY_DOWN` pushed with `SDL_PushEvent`
    // never reaches the backend -- this arm's first two runs reported `KeySuper false` with the
    // pointer exactly on target and `mouseDown true`, so the button half of the same mechanism was
    // working and the key half was being dropped. `AddKeyEvent` is where the backend would have put
    // it anyway, and nothing overwrites it: `ImGui_ImplSDL3_UpdateKeyModifiers` is called only from
    // a real key event, and the arm generates none.
    //
    // Held across the press and the release rather than tapped, because that is what a hand does.
    case 32:
    case 33:
    case 34:
    case 35:
    case 36:
    case 37:
        // **`ImGuiMod_Super`, which arrives as `io.KeyCtrl`.** The swap in `AddKeyEvent` under
        // `ConfigMacOSXBehaviors` runs in both directions, so the rule is: *say what the backend
        // says*. A real Cmd press reaches `AddKeyEvent` as `ImGuiMod_Super`, and this arm stands in
        // for the backend, so it says the same thing. Saying `ImGuiMod_Ctrl` here instead sets
        // `io.KeySuper` -- which is physical Ctrl -- and ImGui then aliases the left press that
        // follows into a RIGHT click ("Super+Left Click aliased into Right Click", imgui.cpp), so
        // the run reported no Cmd and no left button down at all. Both directions of this were
        // walked; this is the one that matches a keyboard.
        ImGui::GetIO().AddKeyEvent(ImGuiMod_Super, true);
        if (frame == 34) {
            pushButton(window, x, y, true);
        } else if (frame == 36) {
            editLog_.push_back(fmt::format(
                "slice: at the click -- Cmd (io.KeyCtrl) {}, mouseDown {}, strip hovered {}",
                ImGui::GetIO().KeyCtrl, ImGui::IsMouseDown(ImGuiMouseButton_Left), strip.hovered));
            pushButton(window, x, y, false);
        }
        break;

    case 38:
        ImGui::GetIO().AddKeyEvent(ImGuiMod_Super, false);
        break;
    case 50: {
        const std::size_t after = count();
        editLog_.push_back(fmt::format("slice: {} {} after the Cmd+click", after, noun));
        if (after == sliceShotsBefore_ + 1) {
            editLog_.push_back(fmt::format("slice: the gesture cut a {} in two",
                                           onAudio ? "clip" : "shot"));
        } else {
            editLog_.emplace_back("slice: the count did not change -- THIS ARM MEASURED NOTHING");
        }
        break;
    }
    default:
        break;
    }
}

void UiScript::stepSliceMenu(Engine& engine, ui::ControlPanel& panel, platform::Window& window,
                             std::uint64_t frame) {
    const ui::SequencePanel::StripRect& strip = panel.sequence.stripRect();
    if (frame == 2) {
        panel.restoreDefaultLayout();
        return;
    }
    if (frame == 4 || frame == 5) {
        if (bool* slot = panel.layout().slot("Sequence"); slot != nullptr) {
            *slot = true;
        }
        ImGui::SetWindowFocus("Sequence");
        return;
    }
    if (!strip.valid()) {
        return;
    }
    // A quarter of the way along the lane: far enough into a block that the cut is legal and the
    // Split row is enabled rather than greyed, which is the state worth photographing. (Pushing it
    // to a third lands near a shot boundary in `behaviors.json` and captures the greyed state
    // instead, which is also worth having and is how that capture was taken.)
    const float x = strip.x + strip.gutter + (strip.width - strip.gutter) * 0.25f;
    // **Which lane is chosen by the project, not by a flag.** The audio lane is the contentious one
    // -- ADR-103 keeps it a scrub -- so a project with clips gets its menu photographed, and one
    // without gets the shots lane. One arm, two captures, by choice of `--project`.
    const bool onAudio = !engine.audioClips().empty();
    const float y = onAudio
                        ? strip.y + strip.shotsTop - ui::kStripLaneGap -
                              ui::kStripAudioLaneHeight * 0.5f
                        : strip.y + strip.shotsTop + ui::kStripLaneHeight * 0.5f;
    if (frame >= 26 && frame <= 33) {
        warpAndMove(window, x, y);
        return;
    }
    if (frame == 34) {
        pushButton(window, x, y, true, SDL_BUTTON_RIGHT);
        return;
    }
    if (frame == 36) {
        // The release is what opens it: the strip treats a right press that stays put as a menu and
        // one that travels as a pan, so the press and the release must be at the same point.
        pushButton(window, x, y, false, SDL_BUTTON_RIGHT);
        return;
    }
    // And then nothing, deliberately. Any further pointer event would close the popup, and the
    // whole purpose of the arm is that the capture frame finds it still up.
}

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

// ---- star: declaring a hero, which is the interaction phase 1 measured at 282 ms --------------
//
// Through `ui::setNodesHero`, which is the function the star button in the Objects list calls --
// not through `Composition::setHeroes`, which would skip the command, the validation and the undo
// entry that a person's click pays for.
//
// A cycle of 40 frames: star on frame 0, unstar on frame 20. Alternating rather than starring once
// is what makes a block of frames measure the interaction: a one-shot arm would put one edit in a
// hundred and twenty frames and report the idle editor as the cost of starring.
//
// It proves it did something (ADR-182). A hero list that did not change, or a flatten that did not
// happen, means the frames in this block measured nothing, and the arm says so in its log rather
// than letting a healthy frame time stand as evidence that starring is cheap.
void UiScript::stepStar(Engine& engine, ui::ControlPanel& panel, std::uint64_t frame) {
    scene::Composition* composition = engine.composition();
    if (composition == nullptr) {
        return;
    }
    const std::uint64_t inCycle = frame % 40;
    if (inCycle != 0 && inCycle != 20) {
        return;
    }
    if (starSubject_.empty()) {
        // Something that is not already a hero, and not the terrain: starring a hero again is a
        // no-op that `setNodesHero` refuses, and an arm whose edit is refused measures nothing.
        for (const auto& node : composition->nodes()) {
            if (!node || node->kind == scene::NodeKind::Terrain || node->kind == scene::NodeKind::Group) {
                continue;
            }
            bool already = false;
            for (const world::HeroPoint& h : composition->heroes()) {
                if (h.name == node->name) {
                    already = true;
                    break;
                }
            }
            if (!already) {
                starSubject_ = node->name;
                break;
            }
        }
        if (starSubject_.empty()) {
            editLog_.emplace_back("star: every node is already a hero; this arm measured nothing");
            return;
        }
    }
    const std::size_t heroesBefore = composition->heroes().size();
    const std::uint64_t flattensBefore = composition->flattenCount();
    const std::array<std::string, 1> names{starSubject_};
    const ui::EditCommand command = ui::setNodesHero(engine, names, inCycle == 0);
    const std::size_t heroesAfter = composition->heroes().size();
    // The flatten is triggered by `dirty_` and paid by the next `controller_->update()`, so the
    // count cannot have moved yet. What can be checked here is that the edit was accepted at all.
    if (starToggles_ == 0) {
        editLog_.push_back(fmt::format(
            "star: '{}' -- command {} hero change(s), heroes {} -> {}, flattens at {} (the flatten "
            "this caused is paid by the next engine.update, so watch '# scene flattens')",
            starSubject_, command.heroes.size(), heroesBefore, heroesAfter, flattensBefore));
        if (command.heroes.empty() || heroesAfter == heroesBefore) {
            editLog_.emplace_back("star: the hero list did not change -- THIS ARM MEASURED NOTHING");
        }
    }
    if (inCycle == 20) {
        ++starToggles_;
    }
}

// ---- click: a discrete press on the timeline ruler ---------------------------------------------
//
// The gesture the complaint is actually about: put the pointer on the ruler, press, let go, and see
// how long it takes for the playhead to be somewhere else. `Strip` already drags; a drag and a
// click are different measurements, because a drag frame has a held button (which stops Dear
// ImGui's SDL3 backend overwriting the pointer) and a click frame does not.
//
// A cycle of 15 frames: park the pointer for four frames, press on frame 8, release on frame 10,
// and touch nothing else. The parking frames are what make the press land -- `HoveredWindow` is
// computed from the previous frame's pointer -- and the silent frames between clicks are what make
// `input->ui.build ms` a distribution over click frames rather than over hover frames.
void UiScript::stepClick(Engine& engine, ui::ControlPanel& panel, platform::Window& window,
                         std::uint64_t frame) {
    // Counted from the arm's own first step rather than from the application's frame number.
    // `--ui-ab` rebuilds the script at every block switch and keeps handing it the *global* frame
    // counter, so an arm whose setup is written as `case 2:` sets itself up in exactly one block of
    // one run and does nothing in every other -- which is what `strip` and `edit` do today. The
    // layout is *not* restored here: that would change the canvas size, and an arm that resizes the
    // render target is not comparable with the arm interleaved beside it.
    ++clickSteps_;
    if (clickSteps_ <= 3) {
        if (bool* slot = panel.layout().slot("Sequence"); slot != nullptr) {
            *slot = true;
        }
        ImGui::SetWindowFocus("Sequence");
        return;
    }
    const ui::SequencePanel::StripRect& strip = panel.sequence.stripRect();
    if (!strip.valid()) {
        if (clickSteps_ == 20) {
            editLog_.emplace_back("click: the Sequence panel never laid out; this arm measured nothing");
        }
        return;
    }
    // A different second every cycle, walking across the middle of the ruler, so no click is a
    // no-op repeat of the last one and every click is a real discontinuity.
    const std::uint64_t cycle = frame / 15;
    // Across the whole ruler, starting near zero. Where the click lands is not cosmetic: the
    // re-simulation a seek performs is proportional to the second seeked to, so an arm that only
    // ever clicks in the second half measures one point of a straight line and cannot see that it
    // is one.
    const float u = 0.02f + 0.06f * static_cast<float>(cycle % 15);
    const float x = strip.x + strip.gutter + (strip.width - strip.gutter) * u;
    const float y = strip.y + strip.rulerHeight * 0.5f;
    const std::uint64_t inCycle = frame % 15;
    if (inCycle >= 4 && inCycle <= 7) {
        warpAndMove(window, x, y);
    } else if (inCycle == 8) {
        warpAndMove(window, x, y);
        pushButton(window, x, y, true);
    } else if (inCycle == 10) {
        pushButton(window, x, y, false);
    } else if (inCycle == 12) {
        const double now = engine.timelineClock().seconds;
        ++clicks_;
        if (clicks_ <= 16) {
            editLog_.push_back(fmt::format(
                "click #{}: u={:.2f} x={:.0f} -> playhead {:.3f} s (was {:.3f} s, piece is {:.1f} s)",
                clicks_, u, x, now, clickLastSeconds_, engine.durationSeconds()));
            if (clicks_ >= 2 && std::abs(now - clickLastSeconds_) < 1e-6) {
                editLog_.emplace_back("click: the playhead did not move -- THIS ARM MEASURED NOTHING");
            }
        }
        clickLastSeconds_ = now;
    }
}

// ---- drag: a repeating scrub along the ruler ----------------------------------------------------
//
// A cycle of 60 arm-steps: park for four, press on 6, drag along the ruler on 7..46, release on 48,
// stand still until 60. The parking frames are what make the press land -- `HoveredWindow` is
// computed from the previous frame's pointer -- and the still frames at the end are what let the
// deferred evaluation land inside the gesture's own cycle rather than inside the next one's.
//
// It proves it did something (ADR-182): it reports where the playhead started, where it finished,
// and says so in its own log when the two are the same, because a scrub that moved nothing is a
// block of frames that measured an idle editor.
void UiScript::stepDrag(Engine& engine, ui::ControlPanel& panel, platform::Window& window,
                        std::uint64_t frame) {
    (void)frame;
    ++dragSteps_;
    if (dragSteps_ <= 3) {
        if (bool* slot = panel.layout().slot("Sequence"); slot != nullptr) {
            *slot = true;
        }
        ImGui::SetWindowFocus("Sequence");
        return;
    }
    const ui::SequencePanel::StripRect& strip = panel.sequence.stripRect();
    if (!strip.valid()) {
        if (dragSteps_ == 20) {
            editLog_.emplace_back("drag: the Sequence panel never laid out; this arm measured nothing");
        }
        return;
    }
    const std::uint64_t inCycle = (dragSteps_ - 4) % 60;
    const std::uint64_t cycle = (dragSteps_ - 4) / 60;
    const float y = strip.y + strip.rulerHeight * 0.5f;
    // Each gesture starts somewhere different and sweeps forward, so no two cycles repeat a
    // position and the re-simulation's dependence on *where* is inside the measurement rather than
    // outside it.
    const float u0 = 0.05f + 0.10f * static_cast<float>(cycle % 6);
    const auto xAt = [&](float u) {
        return strip.x + strip.gutter + (strip.width - strip.gutter) * u;
    };
    if (inCycle >= 2 && inCycle <= 5) {
        warpAndMove(window, xAt(u0), y);
    } else if (inCycle == 6) {
        dragStartSeconds_ = engine.timelineClock().seconds;
        warpAndMove(window, xAt(u0), y);
        pushButton(window, xAt(u0), y, true);
    } else if (inCycle >= 7 && inCycle <= 46) {
        const float t = static_cast<float>(inCycle - 7) / 39.0f;
        warpAndMove(window, xAt(u0 + 0.30f * t), y);
    } else if (inCycle == 48) {
        pushButton(window, xAt(u0 + 0.30f), y, false);
    } else if (inCycle == 55) {
        ++drags_;
        const double now = engine.timelineClock().seconds;
        if (drags_ <= 8) {
            editLog_.push_back(fmt::format(
                "drag #{}: u {:.2f} -> {:.2f}, playhead {:.3f} -> {:.3f} s (piece is {:.1f} s)",
                drags_, u0, u0 + 0.30f, dragStartSeconds_, now, engine.durationSeconds()));
            if (std::abs(now - dragStartSeconds_) < 1e-6) {
                editLog_.emplace_back("drag: the playhead did not move -- THIS ARM MEASURED NOTHING");
            }
        }
    }
}

} // namespace avgen::app

namespace avgen::app {

void UiScript::check(bool ok, const std::string& what) {
    editLog_.push_back(fmt::format("director: {} {}", ok ? "PASS" : "FAIL", what));
    failedChecks_ += ok ? 0 : 1;
}

// ADR-762. The schedule, in frames (the capture frame for "after step N" is the next checkpoint):
//   4-5    open and raise the Director panel
//   60     record the state before anything is pressed (the proposal is waiting)
//   80-88  press Preview                                  -> checked at 110
//   130-138 press Reject (or Accept)                       -> checked at 160
//   180-186 Cmd+Z (accept path only)                       -> checked at 210
void UiScript::stepDirector(Engine& engine, ui::ControlPanel& panel, platform::Window& window, std::uint64_t frame,
                            bool accept) {
    ui::DirectorPanel& director = panel.director;
    if (frame == 4 || frame == 5) {
        if (bool* slot = panel.layout().slot("Director"); slot != nullptr) {
            *slot = true;
        }
        ImGui::SetWindowFocus("Director");
        return;
    }
    if (director.plane == nullptr || director.edits == nullptr) {
        if (frame == 60) {
            check(false, "the Director panel has no control plane or history; THIS ARM TESTED NOTHING");
        }
        return;
    }
    ui::EditHistory& history = director.edits->history();
    const auto task = director.plane->currentTask();
    const auto press = [&](const ui::DirectorPanel::Rect& r, std::uint64_t start) {
        if (!r.valid) {
            return;
        }
        if (frame >= start && frame < start + 6) {
            warpAndMove(window, r.cx(), r.cy()); // parked first: see `warpAndMove`
        } else if (frame == start + 6) {
            pushButton(window, r.cx(), r.cy(), true);
        } else if (frame == start + 8) {
            pushButton(window, r.cx(), r.cy(), false);
        }
    };
    const auto plans = [&] { return engine.directingPlans().size(); };
    switch (frame) {
    case 60:
        directorUndoBefore_ = history.undoSize();
        directorStateBefore_ = history.stateId();
        directorSequenceBefore_ = engine.sequence().toJson().dump();
        check(task != nullptr && task->state() == ai::TaskState::AwaitingApproval,
              "a proposal is waiting for approval before anything is pressed");
        check(plans() == 0, fmt::format("the project carries no plan yet ({})", plans()));
        check(director.buttons().preview.valid && director.buttons().accept.valid && director.buttons().reject.valid,
              "the panel's buttons are drawn and in view");
        return;
    case 110:
        check(director.previewing(), "Preview put the panel into preview");
        check(history.undoSize() == directorUndoBefore_ + 1, fmt::format("the preview is one edit ({} -> {})",
                                                                         directorUndoBefore_, history.undoSize()));
        check(history.undoLabel().rfind("Director: ", 0) == 0,
              fmt::format("the preview edit is labelled as one: \"{}\"", history.undoLabel()));
        check(plans() == 1, fmt::format("the previewed plan is installed ({})", plans()));
        check(engine.sequence().toJson().dump() != directorSequenceBefore_, "the preview changed the sequence");
        check(task != nullptr && task->state() == ai::TaskState::AwaitingApproval,
              "the proposal is still waiting while it is previewed");
        return;
    case 160:
        check(!director.previewing(), "the preview ended");
        if (!accept) {
            check(history.undoSize() == directorUndoBefore_, fmt::format("Reject left the history as it was ({} -> {})",
                                                                         directorUndoBefore_, history.undoSize()));
            check(plans() == 0, fmt::format("Reject left no plan ({})", plans()));
            check(engine.sequence().toJson().dump() == directorSequenceBefore_, "Reject left the sequence as it was");
            check(task != nullptr && task->state() == ai::TaskState::Rejected, "the task is rejected");
        } else {
            check(history.undoSize() == directorUndoBefore_ + 1,
                  fmt::format("Accept made exactly one undo ({} -> {})", directorUndoBefore_, history.undoSize()));
            check(task != nullptr && history.undoLabel() == task->prompt(),
                  fmt::format("the undo is labelled with the request: \"{}\"", history.undoLabel()));
            check(plans() == 1 && engine.directingPlans()[0].revision == 1,
                  fmt::format("the plan is installed once, as revision 1 ({})", plans()));
            check(engine.sequence().toJson().dump() != directorSequenceBefore_, "Accept changed the sequence");
            check(task != nullptr && task->state() == ai::TaskState::Completed, "the task is completed");
        }
        return;
    case 180:
    case 181:
    case 182:
    case 183:
        if (accept) {
            // Cmd+Z as the keyboard sends it: a key event with the command modifier, into the
            // application's own shortcut handler (`handleEditorShortcut` reads `SDL_GetModState`).
            SDL_SetModState(SDL_KMOD_GUI);
        }
        if (accept && frame == 182) {
            SDL_Event e{};
            e.type = SDL_EVENT_KEY_DOWN;
            e.key.timestamp = SDL_GetTicksNS();
            e.key.windowID = window.id();
            e.key.key = SDLK_Z;
            e.key.scancode = SDL_SCANCODE_Z;
            e.key.mod = SDL_KMOD_GUI;
            e.key.down = true;
            SDL_PushEvent(&e);
            e.type = SDL_EVENT_KEY_UP;
            e.key.down = false;
            SDL_PushEvent(&e);
        }
        return;
    case 186:
        if (accept) {
            SDL_SetModState(SDL_KMOD_NONE);
        }
        return;
    case 210:
        if (accept) {
            check(history.undoSize() == directorUndoBefore_,
                  fmt::format("Cmd+Z took the one undo back ({} -> {})", directorUndoBefore_, history.undoSize()));
            check(plans() == 0, fmt::format("the plan is gone with it ({})", plans()));
            check(engine.sequence().toJson().dump() == directorSequenceBefore_, "the sequence is as it was");
        }
        return;
    default:
        break;
    }
    press(director.buttons().preview, 80);
    press(accept ? director.buttons().accept : director.buttons().reject, 130);
    // ADR-764: while the reject path's preview stands, press Stills again after the first stills
    // (made by the preview) have had time to finish -- the second request must reuse the session.
    if (!accept) {
        press(director.buttons().stills, 240);
    }
}

void UiScript::stepDirectorRecord(Engine& engine, ui::ControlPanel& panel, platform::Window& window,
                                  std::uint64_t frame) {
    ui::DirectorPanel& director = panel.director;
    if (frame == 4 || frame == 5) {
        if (bool* slot = panel.layout().slot("Director"); slot != nullptr) {
            *slot = true;
        }
        ImGui::SetWindowFocus("Director");
        return;
    }
    if (director.plane == nullptr || director.edits == nullptr) {
        if (frame == 60) {
            check(false, "the Director panel has no control plane or history; THIS ARM TESTED NOTHING");
        }
        return;
    }
    ui::EditHistory& history = director.edits->history();
    const auto task = director.plane->currentTask();
    const auto proposalPlan = [&]() -> nlohmann::json {
        const auto p = task ? task->proposal() : std::nullopt;
        return p ? p->plan : nlohmann::json();
    };
    const auto press = [&](const ui::DirectorPanel::Rect& r, std::uint64_t start) {
        if (!r.valid) {
            return;
        }
        if (frame >= start && frame < start + 6) {
            warpAndMove(window, r.cx(), r.cy());
        } else if (frame == start + 6) {
            pushButton(window, r.cx(), r.cy(), true);
        } else if (frame == start + 8) {
            pushButton(window, r.cx(), r.cy(), false);
        }
    };
    if (frame == 60) {
        directorUndoBefore_ = history.undoSize();
        directorSequenceBefore_ = engine.sequence().toJson().dump();
        check(task != nullptr && task->state() == ai::TaskState::AwaitingApproval, "a goal proposal is waiting");
        check(proposalPlan().value("tier", std::string()) == "goal", "the proposal is live (tier goal)");
        check(director.buttons().record.valid, "the Record button is drawn and in view");
        return;
    }
    press(director.buttons().record, 80);
    if (frame == 120) {
        check(director.recording(), "Record started a recording, off the editor's frames");
    }
    if (recordedAt_ == 0 && frame > 120 && !director.recording() && director.status().rfind("recorded", 0) == 0) {
        recordedAt_ = frame;
        const nlohmann::json plan = proposalPlan();
        editLog_.push_back(fmt::format("director: the recording replaced the proposal at frame {}", frame));
        check(plan.value("tier", std::string()) == "baked", "the revised proposal is baked");
        const bool recorded = plan.contains("performances") && !plan["performances"].empty() &&
                              plan["performances"][0].contains("recording");
        check(recorded, "its performance carries the recording");
        check(task != nullptr && task->state() == ai::TaskState::AwaitingApproval,
              "the recording waits for approval like any proposal");
        check(history.undoSize() == directorUndoBefore_, "recording changed nothing in the project");
        check(engine.sequence().toJson().dump() == directorSequenceBefore_, "the sequence is as it was");
    }
    if (frame == 1490) {
        check(recordedAt_ != 0 && recordedAt_ < 1490, fmt::format("the recording finished before frame 1490 ({})", recordedAt_));
    }
    press(director.buttons().accept, 1500);
    if (frame == 1560) {
        check(history.undoSize() == directorUndoBefore_ + 1,
              fmt::format("Accept made exactly one undo ({} -> {})", directorUndoBefore_, history.undoSize()));
        check(task != nullptr && history.undoLabel() == task->prompt(), "the undo is labelled with the request");
        const auto& plans = engine.directingPlans();
        check(plans.size() == 1 && !plans.empty() && plans[0].tier == directing::Tier::Baked &&
                  !plans[0].performances.empty() && plans[0].performances[0].recording.has_value(),
              "the installed plan is the recorded, baked one");
        const auto& seq = engine.sequence();
        check(std::any_of(seq.actors.begin(), seq.actors.end(), [](const seq::Actor& a) { return a.id == "rook"; }),
              "Rook's recorded actor is installed");
        check(std::none_of(seq.events.begin(), seq.events.end(),
                           [](const seq::SequenceEvent& e) { return e.what.kind == seq::EventActionKind::CharacterGoal; }),
              "no live goal was installed");
    }
    if (frame >= 1600 && frame <= 1603) {
        SDL_SetModState(SDL_KMOD_GUI);
        if (frame == 1602) {
            SDL_Event e{};
            e.type = SDL_EVENT_KEY_DOWN;
            e.key.timestamp = SDL_GetTicksNS();
            e.key.windowID = window.id();
            e.key.key = SDLK_Z;
            e.key.scancode = SDL_SCANCODE_Z;
            e.key.mod = SDL_KMOD_GUI;
            e.key.down = true;
            SDL_PushEvent(&e);
            e.type = SDL_EVENT_KEY_UP;
            e.key.down = false;
            SDL_PushEvent(&e);
        }
    }
    if (frame == 1606) {
        SDL_SetModState(SDL_KMOD_NONE);
    }
    if (frame == 1640) {
        check(history.undoSize() == directorUndoBefore_, "Cmd+Z took it back");
        check(engine.directingPlans().empty(), "the plan is gone with it");
        check(engine.sequence().toJson().dump() == directorSequenceBefore_, "the sequence is as it was");
    }
}

} // namespace avgen::app
