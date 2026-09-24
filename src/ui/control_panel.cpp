#include "ui/control_panel.hpp"

#include "ui/param_widget.hpp"
#include "ui/environment_panel.hpp"
#include "ui/lights_panel.hpp"

#include <cstring>

#include "app/frame_range.hpp"
#include "app/transport.hpp"

#include "ui/editor_shell.hpp"
#include "ui/viewport_overlay.hpp"
#include "ui/ui_logic.hpp"

#include "audio/audio_input.hpp"
#include "control/midi.hpp"
#include "core/log.hpp"
#include "platform/window.hpp"
#include "scene/composition.hpp"
#include "app/job_system.hpp"
#include "ui/style.hpp"

#include <SDL3/SDL_misc.h>
#include "ui/world_context_menu.hpp"
#include "ui/theme.hpp"
#include "stage/staging.hpp"

#include <imgui.h>
#include <implot.h>

#include <algorithm>
#include <cstdlib>
#include <optional>
#include <string_view>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace avgen::ui {

namespace {

constexpr std::size_t kBandHistory = 240;

// Nameless so it never appears in a window list; ImGui still needs a key for it.
constexpr const char* kStatusBarName = "##avgen-status";
// How long a change to the open-panel set waits before it reaches the disk. ImGui throttles its
// own ini the same way, and for the same reason: dragging down the View menu with the mouse held
// toggles items on the way past, and each of those should not be a file write.
constexpr double kLayoutSaveIntervalSeconds = 2.0;


const char* bandName(std::size_t i) {
    static const char* names[] = {"bass", "lowMid", "mid", "highMid", "treble"};
    return i < 5 ? names[i] : "?";
}

} // namespace

void ControlPanel::setLayoutStore(std::filesystem::path file, bool imguiHasSavedLayout) {
    layoutFile_ = std::move(file);
    if (auto r = layout_.load(layoutFile_); !r) {
        // A layout that will not parse is worth one line and nothing more: the defaults are
        // already in place, and refusing to start the editor over a preferences file would be a
        // worse answer than opening it arranged the way it ships.
        log::warn("editor layout: {}", r.error().message);
    }
    bool degenerateLayout = false;
    // A saved layout may have every panel in a region closed, and an empty dock node is collapsed
    // by ImGui so the centre expands into the space -- which is how a canvas ends up spanning to
    // the window edge with nothing beside it. Restoring one panel per side region keeps the world a
    // canvas rather than a backdrop, which is the whole point of the shell.
    if (const std::size_t reopened = layout_.ensureRegionsOccupied(); reopened > 0) {
        // Reopening the panel is not enough on its own. The dock *tree* is ImGui's and lives in its
        // own ini; a region whose panels were all closed has already been collapsed there, and a
        // window reopened into a collapsed node does not give the node its width back. The saved
        // arrangement was degenerate rather than merely unusual, so it is rebuilt.
        degenerateLayout = true;
        log::info("editor layout: a saved region was empty, so the canvas had taken its space; "
                  "rebuilding the default arrangement ({} panel(s) reopened)", reopened);
    }
    storedLayoutSignature_ = layout_.signature();
    // A dock tree restored from ImGui's own ini is somebody's arrangement and is left alone.
    // Without one there is nothing to keep, and the default has to exist before the first panel is
    // submitted or every panel spends its first frames floating over the world.
    // Rebuilt on a first run, and also when the saved arrangement was degenerate: reopening a
    // panel does not give a collapsed dock node its width back, and ImGui's own ini is the
    // authority on the tree.
    rebuildLayout_ = !imguiHasSavedLayout || degenerateLayout;
}

void ControlPanel::saveLayout() {
    if (layoutFile_.empty()) {
        return;
    }
    if (auto r = layout_.save(layoutFile_); !r) {
        log::warn("editor layout: {}", r.error().message);
        return;
    }
    storedLayoutSignature_ = layout_.signature();
    lastLayoutSave_ = ImGui::GetTime();
}

void ControlPanel::serviceLayoutStore() {
    if (layoutFile_.empty() || layout_.signature() == storedLayoutSignature_) {
        return;
    }
    if (ImGui::GetTime() - lastLayoutSave_ < kLayoutSaveIntervalSeconds) {
        return;
    }
    saveLayout();
}

void ControlPanel::restoreDefaultLayout() {
    layout_.restoreDefaults();
    rebuildLayout_ = true;
}

void ControlPanel::draw(app::Engine& engine, const FrameStats& stats) {
    // ---- one selection, two panels -------------------------------------------------------------
    //
    // The Edit panel's list and the World panel's inspector had separate selections, so choosing a
    // hero in the list left the inspector showing whatever was last picked in the viewport -- and
    // the inspector is where "what is modulating this" lives. A viewport pick already synced them
    // (`Application::pick`); a click in the list, a box select, a group operation and an undo that
    // restores a selection did not.
    //
    // Followed here, once a frame, rather than at each of those call sites. The editor's selection
    // is the authority and this is a projection of it, so every route that can change it is covered
    // by construction -- including the ones nobody has written yet, which is the half of this that
    // patching call sites would have kept getting wrong.
    //
    // Only on a *change*, so the World panel's own Overview can still select a field, a material or
    // the environment without this dragging it back to a node every frame.
    {
        const std::string primary = editor.selection.empty() ? std::string() : editor.selection.primary();
        if (primary != lastEditorSelection_) {
            lastEditorSelection_ = primary;
            if (primary.empty()) {
                world.selection = WorldSelection{};
            } else {
                world.selection.kind = WorldSelection::Kind::Node;
                world.selection.name = primary;
            }
        }
    }

    // Menu bar and status bar first: both take their height out of the viewport's work area, and
    // the dockspace is sized from what is left.
    drawMenuBar(engine);
    drawStatusBar(engine, stats);

    const ImGuiID dockspace = beginEditorDockspace();
    // The emptiness check is asked once, on the first frame. Asking it every frame would rebuild
    // the default the moment a user dragged the last panel out of the tree, which is fighting them
    // over their own layout rather than restoring anything.
    const bool nothingToRestore = firstFrame_ && dockspaceIsEmpty(dockspace);
    firstFrame_ = false;
    if (rebuildLayout_ || nothingToRestore) {
        rebuildLayout_ = false;
        buildDefaultDockLayout(dockspace, layout_);
    }

    // The centre is the canvas's alone, and that has to be reasserted every run rather than
    // trusted to the saved layout: ImGui writes `NoTabBar` into the .ini but not
    // `NoDockingOverMe`, so a panel dropped into the centre once stays there for ever, sharing one
    // rectangle with the world and with no tab bar to show that it is doing so.
    enforceCanvasCentre(dockspace, layout_);

    // The canvas before the panels, so the world is submitted whatever a panel does afterwards.
    // The editor runs inside it (ADR-092): the pointer is only the canvas's while the canvas is the
    // current window, and the canvas's rectangle is only known once ImGui has laid it out.
    statusAgeSeconds_ += static_cast<double>(ImGui::GetIO().DeltaTime);
    // ADR-246. Three things are handed to the canvas and all three come from one computation:
    // where the picture goes inside the window, what the letterbox is painted with, and -- through
    // the overlay -- which rectangle every coordinate conversion divides by.
    const app::RenderSettings* out = renderSettings;
    const std::uint32_t outW = out != nullptr ? out->width : 0;
    const std::uint32_t outH = out != nullptr ? out->height : 0;
    const bool framed = preview.showsOutputFrame() && outW > 0 && outH > 0;
    const float pixelScale = std::max(ImGui::GetIO().DisplayFramebufferScale.x, 1.0f);

    CanvasPlacement placement;
    if (framed) {
        placement = [&](const CanvasRect& rect) {
            // The pan is clamped against the *unpanned* frame every frame rather than only when a
            // drag ends: the canvas can be resized under a held pan (a panel opens, the window is
            // dragged to another display) and a pan that was legal at the old size can put the
            // whole frame off the new one.
            const PreviewFrame unpanned = fitOutputFrame(rect, outW, outH, preview.zoom, pixelScale);
            clampPreviewPan(rect, unpanned, preview.panX, preview.panY);
            return fitOutputFrame(rect, outW, outH, preview.zoom, pixelScale, preview.panX, preview.panY);
        };
    }
    std::uint32_t outside = 0;
    if (framed) {
        switch (preview.outside) {
        case OutsideFrame::Show: outside = 0; break;
        case OutsideFrame::Dim: outside = IM_COL32(0, 0, 0, 150); break;
        case OutsideFrame::Hide: outside = palette().ground | IM_COL32(0, 0, 0, 255); break;
        }
    }
    canvas_ = drawCanvasWindow(canvasTexture, layout_.regionNode(DockRegion::Centre),
                               [&](const CanvasRect& rect, const PreviewFrame& frame) {
                                   previewFrame_ = frame;
                                   drawViewportEditor(engine, rect, frame);
                               },
                               placement, outside);
    // What the host will render at next frame. Computed here because this is where the frame's
    // size is known, and stored rather than recomputed by the host so the extent the picture is
    // rendered at and the rectangle it is drawn into cannot come to disagree.
    if (framed && previewFrame_.valid()) {
        previewRender_ = previewRenderExtent(previewFrame_, outW, outH, pixelScale, preview.quality,
                                             maxTextureDimension != 0 ? maxTextureDimension
                                                                      : kMaxOutputDimension);
    } else {
        previewRender_ = PreviewRender{};
    }
    drawPanels(engine, stats);
    serviceLayoutStore();
}

void ControlPanel::drawMenuBar(app::Engine& engine) {
    if (!ImGui::BeginMainMenuBar()) {
        return;
    }
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Open Audio...", "O") && onOpenAudio) {
            onOpenAudio();
        }
        if (ImGui::MenuItem("Open Scene (glTF)...", "S") && onOpenScene) {
            onOpenScene();
        }
        if (ImGui::MenuItem("Open Environment (HDR)...", "E") && onOpenEnvironment) {
            onOpenEnvironment();
        }
        if (ImGui::MenuItem("Add Background Shader...") && onOpenShader) {
            onOpenShader();
        }
        if (ImGui::MenuItem("Add Post Shader...") && onOpenPostShader) {
            onOpenPostShader();
        }
        if (ImGui::MenuItem("Built-in Orb Scene") && onOrbScene) {
            onOrbScene();
        }
        if (ImGui::MenuItem("Save Scene As...") && onSaveScene) {
            onSaveScene();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("New Project") && onNewProject) {
            onNewProject();
        }
        if (ImGui::MenuItem("Open Project...") && onOpenProject) {
            onOpenProject();
        }
        if (ImGui::BeginMenu("Examples", !examples.empty())) {
            std::string category;
            for (const auto& ex : examples) {
                if (ex.category != category) {
                    if (!category.empty()) {
                        ImGui::Separator();
                    }
                    ImGui::TextDisabled("%s", ex.category.c_str());
                    category = ex.category;
                }
                if (ImGui::MenuItem(ex.name.c_str()) && onOpenExample) {
                    onOpenExample(ex);
                }
                if (ImGui::IsItemHovered() && !ex.description.empty()) {
                    tooltip("%s", ex.description.c_str());
                }
            }
            ImGui::EndMenu();
        }
        // The Engineering Lab Suite (ADR-261, spec 6). Beside Examples rather than in a panel of
        // its own: opening a lab *is* opening a scene, and this is the menu a person already uses
        // to do that. What it adds over Examples is the two things a file list cannot carry -- the
        // lab's overlay profile, applied on open, and its boundary, in the tooltip, which is what
        // stops somebody diagnosing a LOD symptom in the Temporal Lab.
        //
        // Every lab is shown, including the twelve that are unbuilt, with its status. Hiding them
        // would make this menu useless for handing work out; opening one silently would teach
        // people the suite is decorative. A lab with no fixture is greyed, because a menu item
        // that opens nothing is the defect ADR-225 is about.
        if (ImGui::BeginMenu("Engineering Labs")) {
            for (const labs::LabDescriptor& lab : labs::labs()) {
                const bool openable = !lab.fixture.empty() && onOpenLab;
                const std::string label =
                    lab.status == labs::LabStatus::Built
                        ? std::string(lab.title)
                        : fmt::format("{}  ({})", lab.title, labs::statusName(lab.status));
                if (ImGui::MenuItem(label.c_str(), nullptr, false, openable)) {
                    onOpenLab(lab);
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    tooltip("%s\n\nOwns: %s\nNot its: %s\nDecided in: %s%s",
                                      std::string(lab.question).c_str(),
                                      std::string(lab.owns).c_str(),
                                      std::string(lab.doesNotOwn).c_str(),
                                      std::string(lab.decides).c_str(),
                                      lab.fixture.empty() ? "\n\nNo fixture yet." : "");
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Open Recent", !recentProjects.empty())) {
            // Two entries called `night-shift.json` -- one in this checkout, one in a worktree --
            // are not duplicates, so the list is right to hold both. The label has to say which is
            // which, and only the ambiguous ones grow: lengthening every label to separate two of
            // them turns the menu into a column of full paths.
            const std::vector<std::string> labels = ui::uniqueFileLabels(recentProjects);
            for (std::size_t i = 0; i < recentProjects.size(); ++i) {
                const std::filesystem::path& recent = recentProjects[i];
                // By index, not by label. Two files can be identical all the way up -- a symlinked
                // checkout, say -- and then even the longest label repeats; an id that repeats is a
                // menu item that answers to the wrong click, which is what the warning was about.
                ImGui::PushID(static_cast<int>(i));
                const std::string& label =
                    i < labels.size() ? labels[i] : recent.filename().string();
                if (ImGui::MenuItem(label.c_str()) && onOpenRecent) {
                    onOpenRecent(recent);
                }
                if (ImGui::IsItemHovered()) {
                    tooltip("%s", recent.string().c_str());
                }
                ImGui::PopID();
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Save Project", "Cmd+S", false, !engine.projectPath().empty()) && onSaveProjectHere) {
            onSaveProjectHere();
        }
        if (ImGui::MenuItem("Save Project As...") && onSaveProject) {
            onSaveProject();
        }
        if (ImGui::MenuItem("Export Bundle...") && onExportBundle) {
            onExportBundle();
        }
        // ---- Auto-director settings ---------------------------------------------------------
        //
        // Only controls with an implemented effect. Three a panel would obviously want are absent on
        // purpose and are listed in `AutoDirectorSettings`' own comment: framing and headroom are
        // dead fields, a hero's preferred elevation is never read by the director, and `Shot::speed`
        // only works through a retime the director does not call. A knob wired to nothing is worse
        // than a missing knob.
        // The Auto-director settings live in their own panel, not in this menu. They are a set of
        // controls somebody adjusts while watching the result, and a menu that closes on every
        // click is the wrong shape for that -- it was also, being under File, in a different menu
        // from the Enable and Disable items it belongs with.
        if (autoDirector != nullptr && ImGui::MenuItem("Auto-director settings...")) {
            layout_.setVisible("Auto-director", true);
        }
        ImGui::EndMenu();
    }
    drawEditMenu(engine);
    if (ImGui::BeginMenu("Camera")) {
        // Directing needs a track to cut to. Disabling rather than hiding, with the reason in the
        // tooltip: a menu item that is absent looks like a feature that does not exist, and one
        // that fails on click looks like a bug.
        const bool haveAudio = engine.track() != nullptr;
        // ...and something to point at. This used to be gated on the audio alone, so a world with no
        // heroes gave an enabled menu item that failed with "declare some in the scene's heroes
        // block" -- an instruction that could only be followed in a text editor. Saying it here, in
        // terms of the button that now does it, is the difference between a dead end and a next step.
        const scene::Composition* composition = engine.composition();
        const bool haveHeroes =
            (composition != nullptr && !composition->heroes().empty()) ||
            (worldBuilder.lastWorld && !worldBuilder.lastWorld->composed.plan.heroes.empty());
        ImGui::BeginDisabled(!haveAudio || !haveHeroes || !onDirectCamera);
        if (ImGui::MenuItem("Enable Auto-director")) {
            onDirectCamera();
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            tooltip(!haveAudio
                                  ? "load audio first: the camera is cut to the track's structure, "
                                    "so there has to be a track"
                              : !haveHeroes
                                  ? "nothing to travel to: star an object in World > Objects to make "
                                    "it a hero, or generate a world"
                                  : "folds the track into musical sections and cuts the camera "
                                    "between this world's heroes, landing a reveal on the drop");
        }
        ImGui::BeginDisabled(!onClearCameraAutomation || !engine.timeline().isAutomated("camera/position"));
        if (ImGui::MenuItem("Disable Auto-director")) {
            onClearCameraAutomation();
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            tooltip("gives you the camera back. The director's cut is kept, not deleted: "
                    "world effects keep following it, and Resume Director puts it back "
                    "exactly as it was.");
        }
        // ADR-582: beside the hand-back, because it is the way back from it.
        ImGui::BeginDisabled(!onResumeDirector || !engine.directorParked());
        if (ImGui::MenuItem("Resume Director")) {
            onResumeDirector();
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            tooltip(engine.directorParked()
                        ? "gives the camera back to the director, with the same cut it had"
                        : "nothing to resume: the director has not been set aside");
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        drawViewMenu();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        drawHelpMenu();
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

void ControlPanel::drawHelpMenu() {
    // Every entry opens the Help panel on a topic. The panel is a panel like any other, so the flag
    // it toggles is the same one the View menu toggles; there is no second way for Help to be open.
    const auto go = [this](const char* label, const char* topic, const char* hint) {
        if (ImGui::MenuItem(label)) {
            if (bool* open = layout_.slot("Help"); open != nullptr) {
                *open = true;
            }
            help.open(topic);
        }
        if (hint != nullptr) {
            helpTooltip(hint, topic);
        }
    };
    go("Help Contents", "start/welcome", "the documentation, from the beginning");
    ImGui::Separator();
    go("How AV Gen Works", "start/how-it-works", "audio to analysis to signals to parameters to a frame");
    go("The Interface", "start/interface", "the dockspace, the panels, the status bar");
    go("Keyboard Shortcuts", "reference/keyboard-shortcuts", "every key this build binds");
    go("Modulation Recipes", "modulation/recipes", "worked examples: bass-reactive glow, a beat-synced camera");
    go("Troubleshooting", "troubleshooting/index", "symptoms, and the topic that explains each one");
    go("Performance Guide", "performance/diagnosis", "how to find out what a frame is spent on");
}

// The Edit menu (ADR-101). Every item is the same action the keyboard dispatches, asked of the same
// system -- so an item is grey exactly when the shortcut would do nothing, and neither can drift.
//
// Nothing here decides *what* an action means. The menu does not know the world editor exists.
void ControlPanel::drawEditMenu(app::Engine& engine) {
    if (edits == nullptr) {
        return; // no edit system attached: better no menu than one that does nothing
    }
    if (!ImGui::BeginMenu("Edit")) {
        return;
    }
    const auto item = [this, &engine](app::EditAction action) {
        // The label says what will happen -- "Undo Move 3 objects" -- because a menu that only says
        // "Undo" asks the user to find out by trying it.
        const std::string label = edits->menuLabel(action);
        const bool available = edits->canExecute(action);
        if (ImGui::MenuItem(label.c_str(), app::editActionShortcut(action), false, available)) {
            static_cast<void>(edits->execute(action, engine));
        }
    };
    item(app::EditAction::Undo);
    item(app::EditAction::Redo);
    ImGui::Separator();
    item(app::EditAction::Cut);
    item(app::EditAction::Copy);
    item(app::EditAction::Paste);
    item(app::EditAction::Duplicate);
    item(app::EditAction::Delete);
    ImGui::Separator();
    item(app::EditAction::SelectAll);
    item(app::EditAction::SelectNone);
    ImGui::EndMenu();
}

void ControlPanel::drawViewMenu() {
    // Straight off the panel registry, so a panel added there is reachable from the menu without a
    // second edit here -- the way panels used to go missing.
    DockRegion previous = DockRegion::Left;
    bool first = true;
    for (const EditorPanel& panel : editorPanels()) {
        if (!first && panel.region != previous) {
            ImGui::Separator();
        }
        previous = panel.region;
        first = false;
        ImGui::MenuItem(panel.label.data(), nullptr, layout_.slot(panel.id));
        if (ImGui::IsItemHovered() && !panel.hint.empty()) {
            tooltip("%s\n(%s)", panel.hint.data(), dockRegionName(panel.region));
        }
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Restore Default Layout")) {
        restoreDefaultLayout();
    }
    if (ImGui::IsItemHovered()) {
        tooltip("rebuilds the dock tree and reopens the panels this editor ships with");
    }
    if (ImGui::MenuItem("Save Layout Now")) {
        if (const char* ini = ImGui::GetIO().IniFilename; ini != nullptr) {
            ImGui::SaveIniSettingsToDisk(ini);
        }
        saveLayout();
    }
}

void ControlPanel::drawStatusBar(app::Engine& engine, const FrameStats& stats) {
    if (!beginStatusBar(kStatusBarName)) {
        return;
    }
    // Every number here is measured, not estimated. An unmeasured one says so.
    const ImVec4 healthy(0.42f, 0.86f, 0.64f, 1.0f);
    const ImVec4 caution(0.96f, 0.72f, 0.34f, 1.0f);
    ImGui::TextColored(static_cast<double>(stats.fps) >= 30.0 ? healthy : caution, "%.0f fps",
                      static_cast<double>(stats.fps));
    ImGui::Separator();
    ImGui::Text("%.1f ms frame", stats.frameIntervalMs);
    ImGui::Separator();
    ImGui::Text("%.1f ms cpu", stats.cpuFrameMs);
    ImGui::Separator();
    if (stats.gpuFrameMs >= 0.0) {
        ImGui::TextColored(stats.gpuFrameMs <= 16.67 ? healthy : caution, "%.2f ms gpu", stats.gpuFrameMs);
    } else {
        ImGui::TextDisabled("gpu n/a");
    }
    ImGui::Separator();
    ImGui::Text("%u x %u", stats.width, stats.height);
    ImGui::Separator();
    ImGui::Text("%u draws / %u tris", stats.drawCalls, stats.triangles);
    ImGui::Separator();
    {
        // The playhead, in whichever format the transport bar is showing -- the same call, so the
        // status bar and the bar cannot disagree about what second it is.
        const app::TransportSnapshot snapshot = engine.transport().snapshot();
        const ImVec4 running(0.5f, 0.88f, 0.62f, 1.0f);
        if (snapshot.playing()) {
            ImGui::TextColored(running, "%s", transport.format(snapshot, snapshot.positionSeconds).c_str());
        } else {
            ImGui::TextDisabled("%s", transport.format(snapshot, snapshot.positionSeconds).c_str());
        }
        if (ImGui::IsItemHovered()) {
            tooltip("%s%s", app::transportStateName(snapshot.state),
                              snapshot.loop.usable() ? ", looping" : "");
        }
    }
    ImGui::Separator();
    // The editor's selection, which is the one that can hold several things. The World panel's
    // single `selection` is still what the inspector shows; this is what the artist has in hand.
    if (editor.selection.size() > 1) {
        ImGui::Text("%zu selected", editor.selection.size());
    } else if (!editor.selection.empty()) {
        ImGui::Text("selected %s", editor.selection.primary().c_str());
    } else if (world.selection.kind == WorldSelection::Kind::Node && !world.selection.name.empty()) {
        ImGui::Text("selected %s", world.selection.name.c_str());
    } else {
        ImGui::TextDisabled("No selection");
    }
    ImGui::Separator();
    // What the next click does, and what the last edit was. Both are things an artist checks
    // constantly and neither of them was on screen before (ADR-092).
    if (editor.mode == EditorMode::Place) {
        ImGui::TextColored(editor.preview().placeable() ? ImVec4(0.5f, 0.88f, 0.62f, 1.0f)
                                                        : ImVec4(0.94f, 0.45f, 0.4f, 1.0f),
                           "%s", editor.status().c_str());
    } else {
        ImGui::TextDisabled("%s", editor.status().c_str());
    }
    if (editor.history().canUndo()) {
        ImGui::Separator();
        ImGui::TextDisabled("undo: %s", editor.history().undoLabel().c_str());
    }
    if (!engine.projectPath().empty()) {
        ImGui::Separator();
        ImGui::TextDisabled("%s", engine.projectPath().filename().string().c_str());
    }
    if (!status_.empty()) {
        ImGui::Separator();
        ImGui::TextColored(statusColour(), "%s", status_.c_str());
    }
    // The adapter is the one thing here that never changes, so it goes where it will be clipped
    // first when the window is narrow.
    const std::string adapter = stats.adapter + " (" + stats.backend + ")";
    const float width = ImGui::CalcTextSize(adapter.c_str()).x;
    if (const float room = ImGui::GetContentRegionAvail().x; room > width + ImGui::GetStyle().ItemSpacing.x) {
        ImGui::SameLine(0.0f, room - width);
        ImGui::TextDisabled("%s", adapter.c_str());
    }
    endStatusBar();
}

void ControlPanel::drawPanels(app::Engine& engine, const FrameStats& stats) {
    // A panel is its registry entry plus its body. The size is only ever used when the panel is
    // floating; docked, the node decides. The dock hint applies to a window ImGui has no settings
    // for -- a brand-new panel lands in its own region instead of over the world, and one the user
    // deliberately tore off stays torn off.
    const auto panel = [this](std::string_view id, ImVec2 floatingSize, auto&& body) {
        bool* open = layout_.slot(id);
        if (open == nullptr || !*open) {
            return;
        }
        const EditorPanel* entry = findEditorPanel(id);
        if (entry != nullptr) {
            if (const std::uint32_t node = layout_.regionNode(entry->region); node != 0) {
                ImGui::SetNextWindowDockID(node, ImGuiCond_FirstUseEver);
            }
        }
        ImGui::SetNextWindowSize(floatingSize, ImGuiCond_FirstUseEver);
        if (ImGui::Begin(id.data(), open)) {
            // Every panel in the editor wraps its prose, declared once, here, because this lambda
            // is the only place a panel window is begun. Before it there was exactly one
            // `PushTextWrapPos` in the whole of `src/ui` and every explanatory line in the
            // application ran off the side of its panel and stopped mid-sentence -- the Control
            // panel's project warnings ended at "unknown target parameter 'atmos/", which is
            // information the application drew and nobody could read.
            //
            // It does NOT reach into child windows: ImGui resets the wrap position per window, so
            // each `BeginChild` that holds prose carries its own guard (Help's article, Settings'
            // content, the AI conversation, the graph inspector).
            const WrapText wrapPanelText;
            // Reserve a column for labels. ImGui draws a widget's label to its *right*, and these
            // panels are full of sliders at the default item width -- which is most of the window --
            // so every label ran off the edge and was clipped: "Foreground" read as "Foregroun",
            // "material/baseColor" as "material/b". From inside a narrow docked panel that looks
            // like the panel is underlapping whatever is beside it.
            //
            // A negative item width means "stop this far short of the right edge", so this is the
            // label column. Proportional with a floor, because a fixed pixel column is either
            // wasteful in a wide panel or useless in a narrow one, and a proportional one alone
            // collapses to nothing when somebody drags a splitter in.
            // Sized from a real label rather than a guessed fraction. The first attempt reserved
            // 42% of the panel and "Foreground" still lost its last character: the column has to
            // hold the widest label these panels actually use, not a proportion that looks about
            // right. Measured against the font in use, so it follows the DPI scale.
            const float avail = ImGui::GetContentRegionAvail().x;
            const float labelColumn =
                ImGui::CalcTextSize("Bioluminescence").x + ImGui::GetStyle().ItemInnerSpacing.x * 2.0f;
            // Never more than half the panel: past that the slider becomes unusable, and a label
            // that cannot fit is better truncated than a control that cannot be dragged.
            ImGui::PushItemWidth(-std::min(labelColumn, avail * 0.5f));
            body();
            ImGui::PopItemWidth();
        }
        ImGui::End();
    };

    panel("World Builder", ImVec2(400, 620), [&] { drawWorldBuilderWindow(engine); });
    panel("Edit", ImVec2(400, 640), [&] { drawEditWindow(engine); });
    panel("Assets", ImVec2(520, 420), [&] { drawAssetsWindow(); });
    panel("World", ImVec2(460, 520), [&] { drawWorldWindow(engine); });
    panel("Parameters", ImVec2(420, 360), [&] { drawParameters(engine); });
    panel("Composition", ImVec2(460, 640), [&] { composition.draw(engine); });
    panel("Render", ImVec2(460, 420), [&] { drawRender(engine); });
    panel("Auto-director", ImVec2(440, 560), [&] { drawAutoDirector(engine); });
    panel("Cameras", ImVec2(420, 560), [&] { drawCameras(engine); });
    panel("Environment", ImVec2(460, 620), [&] { ui::drawEnvironmentPanel(engine); });
    panel("Lights", ImVec2(460, 700),
          [&] { ui::drawLightsPanel(engine, editor.selection, editor.history()); });
    panel("Sequence", ImVec2(900, 420), [&] {
        // The transport across the top of the timeline, where the timeline is. Drawn here rather
        // than inside SequencePanel so that one TransportBar serves both places and the time format
        // a person picks in one is the format they get in the other.
        transport.draw(engine);
        ImGui::Separator();
        // The sequencer joins the editor's one undo stack rather than keeping its own; see the note
        // on `SequencePanel::edits`.
        sequence.edits = edits;
        sequence.draw(engine);
    });
    panel("Control", ImVec2(420, 300), [&] {
        drawTransport(engine);
        ImGui::Separator();
        drawResponse(engine);
        ImGui::Separator();
        drawPerformance(engine, stats);
    });
    panel("Performance", ImVec2(560, 700), [&] { drawPerformanceDashboard(engine, stats); });
    panel("Analysis", ImVec2(520, 620), [&] { drawAnalysis(engine); });
    panel("Modulation", ImVec2(560, 420), [&] { drawModulation(engine); });
    panel("Graph", ImVec2(900, 560), [&] { drawGraphWindow(engine); });
    // Full item width: the label column the other panels reserve is for sliders, and an article
    // laid out against it would wrap two inches short of the panel edge.
    if (bool* open = layout_.slot("Help"); open != nullptr && *open) {
        help.openFlag = open;
        if (const std::uint32_t node = layout_.regionNode(DockRegion::Right); node != 0) {
            ImGui::SetNextWindowDockID(node, ImGuiCond_FirstUseEver);
        }
        ImGui::SetNextWindowSize(ImVec2(560, 720), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Help", open)) {
            const WrapText wrapPanelText;
            help.draw();
        }
        ImGui::End();
    }
    panel("AI", ImVec2(460, 620), [&] { ai.draw(engine); });
    panel("Settings", ImVec2(560, 520), [&] { settings.draw(engine); });
}

void ControlPanel::drawGraphWindow(app::Engine& engine) {
    // `comp`, not `composition`: this class has a `composition` member (the CompositionPanel), and a
    // local of the same name shadows it. Harmless here, and exactly the kind of thing that is not
    // harmless the day somebody adds a line to this function meaning the panel.
    scene::Composition* comp = engine.composition();
    if (comp == nullptr) {
        ImGui::TextDisabled("The current scene is not a composition, so it cannot hold a graph.");
        return;
    }
    graphEditor.onChanged = [comp] { comp->markGraphDirty(); };
    graphEditor.draw(comp->graph());
    if (!comp->graphWarnings().empty()) {
        ImGui::Separator();
        for (const std::string& warning : comp->graphWarnings()) {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "%s", warning.c_str());
        }
    }
}

void ControlPanel::drawWorldBuilderWindow(app::Engine& engine) {
    if (jobs == nullptr || builder == nullptr) {
        ImGui::TextDisabled("The world builder is not available in this session.");
        return;
    }
    // Installing is a scene mutation, so it happens here on the UI thread rather than on the
    // worker that composed it.
    worldBuilder.applyFinished(engine, *builder);
    worldBuilder.draw(engine, *jobs, *builder);
    // Generating replaces the composer's own nodes, so anything selected that it took away has to
    // go out of the selection rather than sit there as a name with no object (ADR-092).
    editor.reconcile(engine);
}

void ControlPanel::drawViewportEditor(app::Engine& engine, const CanvasRect& rect,
                                     const PreviewFrame& frame) {
    if (!rect.valid()) {
        return;
    }
    // The frame, not the window. This one substitution is the whole of ADR-246's §10.2 answer: the
    // image was drawn by a projection built from the frame's aspect ratio, so a conversion that
    // divided by the window's would put every gizmo handle, every selection outline and every
    // picked ray out by the width of the letterbox -- close enough to look right, which is why it
    // would have survived a screenshot.
    //
    // In Workspace mode the frame *is* the window, so nothing about the old behaviour changes.
    const CanvasRect image = frame.valid() ? frame.asCanvasRect(rect.hovered) : rect;
    const float aspect = image.width / std::max(image.height, 1.0f);
    const EditorInput input = editorInputFromImGui(image);
    editor.update(engine, worldBuilder.library(), engine.scene().camera, aspect, input);
    // AVGEN_EDITOR_TRACE=1 prints the editor's per-frame inputs and what it made of them. The
    // editor's wiring is the half of it that no unit test reaches and that this machine cannot
    // screenshot, so it needs a way to say what it saw (ADR-092).
    static const bool trace = std::getenv("AVGEN_EDITOR_TRACE") != nullptr;
    if (trace) {
        log::info("editor: mode={} over={} ndc=({:.2f},{:.2f}) down={} armed={} ground={} n={}",
                  editorModeName(editor.mode), input.overCanvas, input.ndc.x, input.ndc.y,
                  input.leftDown, editor.preview().armed, editor.preview().ground.valid,
                  editor.preview().instances.size());
    }
    // In Output Preview mode the editor's overlays are suppressed (spec §10.1): the mode exists to
    // look at the picture, and a gizmo over it is the one thing guaranteed not to be in the
    // deliverable. Navigation and selection still work -- this hides the drawing, not the editor --
    // and the toolbar's mode selector is the obvious way back, which is what §10.1 asks be
    // "consistent, discoverable, and documented".
    const bool chrome = preview.mode != PreviewViewMode::OutputPreview;
    if (chrome) {
        drawViewportOverlay(editor, engine.scene().camera, image, aspect);
        drawViewportHud(editor, image);
    }
    drawPreviewGuides(frame);
    drawCanvasActivity(engine, rect);
    drawCanvasContextMenu(engine, rect);
    drawPreviewToolbar(engine, rect);
}

// ---- the preview's guides (spec §9) ------------------------------------------------------------
//
// Editor presentation, drawn into the canvas *window's* draw list. That is the same rule
// `drawCanvasActivity` states and it is what makes "guides are not rendered into the scene or
// export" (§9.1) true by construction rather than by care: this draw list only ever reaches the
// main window's swapchain image, and the offline renderer builds its own engine from the project
// file and never runs any of this code at all.
void ControlPanel::drawPreviewGuides(const PreviewFrame& frame) {
    if (!preview.showsOutputFrame() || !frame.valid()) {
        return;
    }
    ImDrawList* list = ImGui::GetWindowDrawList();
    if (list == nullptr) {
        return;
    }
    const GuideSettings& g = preview.guides;
    const ImVec2 a(frame.x, frame.y);
    const ImVec2 b(frame.x + frame.width, frame.y + frame.height);

    if (g.frameBorder) {
        // Two strokes, dark under light: a single hairline vanishes over a bright sky and over a
        // dark forest in turn, and the frame edge is the one line in this feature that must be
        // findable on every possible image.
        list->AddRect(ImVec2(a.x - 1.0f, a.y - 1.0f), ImVec2(b.x + 1.0f, b.y + 1.0f),
                      IM_COL32(0, 0, 0, 160), 0.0f, 0, 3.0f);
        list->AddRect(a, b, IM_COL32(255, 255, 255, 200), 0.0f, 0, 1.0f);
    }
    if (g.thirds) {
        const ImU32 colour = IM_COL32(255, 255, 255, 70);
        for (int i = 1; i <= 2; ++i) {
            const float t = static_cast<float>(i) / 3.0f;
            const float x = frame.x + frame.width * t;
            const float y = frame.y + frame.height * t;
            list->AddLine(ImVec2(x, a.y), ImVec2(x, b.y), colour, 1.0f);
            list->AddLine(ImVec2(a.x, y), ImVec2(b.x, y), colour, 1.0f);
        }
    }
    if (g.centreCross) {
        const ImU32 colour = IM_COL32(255, 255, 255, 120);
        const float cx = frame.x + frame.width * 0.5f;
        const float cy = frame.y + frame.height * 0.5f;
        const float arm = std::min(frame.width, frame.height) * 0.03f;
        list->AddLine(ImVec2(cx - arm, cy), ImVec2(cx + arm, cy), colour, 1.0f);
        list->AddLine(ImVec2(cx, cy - arm), ImVec2(cx, cy + arm), colour, 1.0f);
    }
    if (g.safeAreas) {
        const auto box = [&](float fraction, ImU32 colour, const char* label) {
            const GuideRect r = insetFrame(frame, fraction);
            list->AddRect(ImVec2(r.x, r.y), ImVec2(r.x + r.width, r.y + r.height), colour, 0.0f, 0,
                          1.0f);
            // Inside the box and hard against its corner, so a label never sits over the subject --
            // §9.1: "Labels, if provided, do not obscure the scene."
            if (r.width > 120.0f && r.height > 40.0f) {
                list->AddText(ImVec2(r.x + 4.0f, r.y + 2.0f), colour, label);
            }
        };
        box(preview.guides.safe.actionFraction, IM_COL32(255, 210, 90, 140), "action");
        box(preview.guides.safe.titleFraction, IM_COL32(120, 200, 255, 140), "title");
    }
}


// ---- the output resolution (spec §5.2, §5.3) ----------------------------------------------------
//
// One function, drawn in two places -- the preview toolbar and the Render panel. Two surfaces
// editing one setting through two bits of code is how they come to disagree about what is valid,
// and the resolution is the setting where disagreeing matters most: the preview would be framing
// the shot at a size the render then refuses.
bool ControlPanel::drawOutputResolutionControls(app::RenderSettings& output) {
    bool changed = false;
    const std::uint32_t cap = maxTextureDimension != 0 ? maxTextureDimension : kMaxOutputDimension;

    // The boxes hold the *pending* numbers, which are the last thing typed rather than the last
    // thing accepted. §5.3: "Preserve the last valid configuration if the new value is rejected" --
    // and equally, do not silently put the old number back under someone who is halfway through
    // typing a new one.
    if (pendingOutputWidth_ == 0 || (outputError_.empty() && (pendingOutputWidth_ != output.width ||
                                                              pendingOutputHeight_ != output.height))) {
        pendingOutputWidth_ = output.width;
        pendingOutputHeight_ = output.height;
    }

    const std::string current = fmt::format("{} x {}", output.width, output.height);
    ImGui::SetNextItemWidth(150.0f);
    if (ImGui::BeginCombo("##output-preset", current.c_str())) {
        for (const OutputPreset& preset : outputPresets()) {
            const std::string label =
                fmt::format("{}  {} x {}  ({})", preset.label, preset.width, preset.height,
                            aspectLabel(preset.width, preset.height));
            const bool selected = output.width == preset.width && output.height == preset.height;
            if (ImGui::Selectable(label.c_str(), selected)) {
                // A preset is known-good by construction (a unit test proves every one of them
                // validates), so it clears any error the custom boxes left standing.
                output.width = preset.width;
                output.height = preset.height;
                pendingOutputWidth_ = preset.width;
                pendingOutputHeight_ = preset.height;
                outputError_.clear();
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", aspectLabel(output.width, output.height).c_str());
    if (ImGui::IsItemHovered()) {
        tooltip("The output's aspect ratio, derived from its width and height.\nIt is not "
                          "separately editable: an aspect that could disagree with the pixel\n"
                          "dimensions is an aspect that will.");
    }

    int custom[2] = {static_cast<int>(pendingOutputWidth_), static_cast<int>(pendingOutputHeight_)};
    ImGui::SetNextItemWidth(150.0f);
    const bool edited = ImGui::InputInt2("custom", custom);
    const bool commit = edited && ImGui::IsItemDeactivatedAfterEdit();
    if (edited) {
        pendingOutputWidth_ = static_cast<std::uint32_t>(std::max(0, custom[0]));
        pendingOutputHeight_ = static_cast<std::uint32_t>(std::max(0, custom[1]));
    }
    if (edited || commit) {
        // Validated as 64-bit against the *adapter's* limit, and applied only if it passes. A
        // rejected size never reaches `RenderSettings`, so no GPU resource is ever allocated for
        // one (spec §5.3: "Avoid allocating a render target until the configuration is valid").
        const auto ok = validateOutputResolution(static_cast<std::uint64_t>(std::max(0, custom[0])),
                                                 static_cast<std::uint64_t>(std::max(0, custom[1])), cap);
        if (ok) {
            outputError_.clear();
            if (output.width != pendingOutputWidth_ || output.height != pendingOutputHeight_) {
                output.width = pendingOutputWidth_;
                output.height = pendingOutputHeight_;
                changed = true;
            }
        } else {
            outputError_ = ok.error().message;
        }
    }
    if (!outputError_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(palette().error));
        ImGui::TextWrapped("%s", outputError_.c_str());
        ImGui::PopStyleColor();
        ImGui::TextDisabled("still rendering at %u x %u", output.width, output.height);
    }
    return changed;
}

// ---- the preview toolbar (spec §8.1) ------------------------------------------------------------
//
// Drawn inside the canvas window, over the top edge of the picture, rather than as a panel. The
// canvas is the point of this editor (ADR-076) and a permanent strip of chrome above it would cost
// the world a row of pixels in every session, including the ones that never use the preview. It
// collapses to a single chevron, and it is absent entirely in Workspace mode until the mode
// selector is reached for.
void ControlPanel::drawPreviewToolbar(app::Engine& engine, const CanvasRect& rect) {
    static_cast<void>(engine);
    if (!rect.valid()) {
        return;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(
                                                (palette().panel & 0x00FFFFFFu) | IM_COL32(0, 0, 0, 205)));
    const float height = ImGui::GetFrameHeight() + ImGui::GetStyle().WindowPadding.y * 2.0f;
    // Bottom-centred. The width has to be measured rather than predicted -- the bar auto-sizes to
    // whichever controls the current mode shows -- so this uses what the last frame came out at.
    // Before the first measurement the collapsed width is the better guess of the two: it is exact
    // when the bar is collapsed, and when it is not, one frame of a too-far-right bar is less
    // visible than one frame of a bar centred as though it had no width at all.
    const float measured = previewToolbarWidth_ > 0.0f ? previewToolbarWidth_ : 34.0f;
    const ToolbarOrigin origin = previewToolbarOrigin(rect, measured, height);
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y));
    if (ImGui::BeginChild("preview-toolbar", ImVec2(preview.toolbar ? 0.0f : 34.0f, height),
                          ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        if (ImGui::SmallButton(preview.toolbar ? "<<" : ">>")) {
            preview.toolbar = !preview.toolbar;
        }
        if (ImGui::IsItemHovered()) {
            tooltip("Show or hide the output preview controls.");
        }
        if (preview.toolbar) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(130.0f);
            if (ImGui::BeginCombo("##view-mode", previewViewModeLabel(preview.mode))) {
                for (const auto mode : {PreviewViewMode::Workspace, PreviewViewMode::OutputFrame,
                                        PreviewViewMode::OutputPreview}) {
                    if (ImGui::Selectable(previewViewModeLabel(mode), mode == preview.mode)) {
                        preview.mode = mode;
                    }
                }
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered()) {
                tooltip(
                    "Workspace -- the world fills the canvas, at the canvas's own shape.\n"
                    "Output Frame -- the world is rendered at the output's shape and placed in the\n"
                    "  canvas. This is the deliverable's framing: same camera, same projection.\n"
                    "Preview -- the same frame with the editor's overlays out of the way.");
            }

            if (preview.showsOutputFrame()) {
                ImGui::SameLine();
                drawPreviewFrameControls(engine);
            } else {
                ImGui::SameLine();
                ImGui::TextDisabled("the canvas's own shape, not the output's");
            }
        }
    }
    ImGui::EndChild();
    // EndChild submits the child as an item, so this is the width the bar actually came out at.
    // Fed back into the placement above on the next frame.
    previewToolbarWidth_ = ImGui::GetItemRectSize().x;
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

// The half of the toolbar that only means anything while a frame is on screen. Split out so the
// mode selector above stays readable, not because it is a different concern.
void ControlPanel::drawPreviewFrameControls(app::Engine& engine) {
    app::RenderSettings* out = renderSettings;
    if (out == nullptr) {
        ImGui::TextDisabled("no render settings");
        return;
    }
    ImGui::SetNextItemWidth(120.0f);
    const std::string size = fmt::format("{} x {}  {}", out->width, out->height,
                                         aspectLabel(out->width, out->height));
    if (ImGui::BeginCombo("##output-size", size.c_str(), ImGuiComboFlags_HeightLarge)) {
        static_cast<void>(drawOutputResolutionControls(*out));
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) {
        tooltip("The output resolution -- the same setting the Render panel edits and the\n"
                          "one the deliverable is rendered at. It is the project's, not the editor's.");
    }

    // ---- zoom ----
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110.0f);
    const std::string zoom = zoomLabel(preview.zoom, previewFrame_.fittedScale);
    if (ImGui::BeginCombo("##zoom", zoom.c_str())) {
        if (ImGui::Selectable("Fit", preview.zoom.fit)) {
            preview.zoom.fit = true;
            preview.panX = 0.0f;
            preview.panY = 0.0f;
        }
        for (const float stop : zoomStops()) {
            const std::string label = fmt::format("{:.0f}%", static_cast<double>(stop) * 100.0);
            const bool selected = !preview.zoom.fit && preview.zoom.scale == stop;
            if (ImGui::Selectable(label.c_str(), selected)) {
                preview.zoom.fit = false;
                preview.zoom.scale = stop;
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) {
        tooltip("How large the frame is *displayed*. 100%% is one output pixel per screen\n"
                          "pixel. Zoom changes nothing about the output resolution, the camera or\n"
                          "the scene -- for that, use the size and the quality controls beside it.");
    }

    // ---- preview quality ----
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140.0f);
    if (ImGui::BeginCombo("##quality", previewQualityLabel(preview.quality))) {
        for (const auto q : {PreviewQuality::Draft, PreviewQuality::Realtime, PreviewQuality::Native}) {
            if (ImGui::Selectable(previewQualityLabel(q), q == preview.quality)) {
                preview.quality = q;
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) {
        tooltip("How many pixels the frame is *rendered* at, which is a different question\n"
                          "from how large it is shown. Every rung here is a real render-target\n"
                          "extent at the output's aspect ratio -- none of them is a label.");
    }

    // ---- what is actually happening, said out loud (spec §8.1) ----
    ImGui::SameLine();
    if (previewResizing) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(palette().processing));
        ImGui::TextUnformatted("rebuilding...");
        ImGui::PopStyleColor();
    } else if (previewRender_.width == 0) {
        ImGui::TextDisabled("no frame");
    } else {
        const std::string what = previewRender_.describe(out->width, out->height);
        const bool reduced = !previewRender_.nativeOutput;
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(
                                                 reduced ? palette().warning : palette().success));
        ImGui::TextUnformatted(what.c_str());
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) {
            tooltip(
                "What the frame on screen was actually rendered at.\n\n"
                "The framing is the deliverable's exactly -- the projection is built from this\n"
                "extent's aspect ratio, which is the output's. What differs is how finely it is\n"
                "sampled, and these two things it cannot show you at all:\n"
                "  - an offline render may supersample (ADR-212); this preview never does.\n"
                "  - an offline render lifts the distance detail limits and live playback does\n"
                "    not (ADR-186), so a wide shot has more in it in the deliverable. The Render\n"
                "    panel's \"viewport matches the render\" puts the viewport on those terms.");
        }
    }
    if (!previewRender_.aspectMatches) {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(palette().error));
        ImGui::TextUnformatted("framing differs");
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) {
            tooltip("The output is larger than this device will allocate, so the preview\n"
                              "could not be given the output's exact shape. It is NOT showing the\n"
                              "deliverable's framing. Lower the output resolution to fix it.");
        }
    }

    // ---- the toggles ----
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::Checkbox("safe", &preview.guides.safeAreas);
    if (ImGui::IsItemHovered()) {
        tooltip("Title-safe (%.0f%%) and action-safe (%.0f%%) areas, SMPTE ST 2046-1 /\n"
                          "EBU R 95. Editor guides: they cannot reach a render.",
                          static_cast<double>(preview.guides.safe.titleFraction) * 100.0,
                          static_cast<double>(preview.guides.safe.actionFraction) * 100.0);
    }
    ImGui::SameLine();
    ImGui::Checkbox("thirds", &preview.guides.thirds);
    ImGui::SameLine();
    ImGui::Checkbox("centre", &preview.guides.centreCross);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    if (ImGui::BeginCombo("##outside", outsideFrameName(preview.outside))) {
        for (const auto o : {OutsideFrame::Show, OutsideFrame::Dim, OutsideFrame::Hide}) {
            if (ImGui::Selectable(outsideFrameName(o), o == preview.outside)) {
                preview.outside = o;
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) {
        tooltip("What the canvas outside the frame looks like. An editor overlay: it is\n"
                          "painted after the picture and reaches nothing but this window.");
    }

    // ---- what the viewport is looking through (ADR-391) -----------------------------------------
    //
    // **The state has to be visible, and leaving it must not be a gesture you have to know about.**
    // The viewport and the film's camera are two things now, and this is the one control that says
    // which of them is on screen. In the canvas toolbar because that is where a person is looking
    // when they wonder why the view will not move -- or why their drag did not stick.
    //
    // Always shown, unlike the free-roam toggle it replaces. That toggle appeared only on projects
    // with more than one camera, which was right when the only thing it could do was stand a
    // director down; it is wrong now, because on a one-camera project the difference between
    // "flying" and "moving the camera this project renders" is exactly what a person needs told.
    if (scene::Composition* comp = engine.composition(); comp != nullptr) {
        ImGui::SameLine();
        const scene::ViewportView view = viewportView;
        const bool film = viewportViewForced || view.showsFilm();
        const scene::ActiveCameraState& live = comp->activeCamera();
        std::string label = "film camera";
        if (!film) {
            if (view.mode == scene::ViewportCamera::Editor) {
                label = "editor view";
            } else if (const scene::CameraRig* rig = comp->cameraDirection().find(view.camera)) {
                label = "through: " + rig->name;
            } else {
                label = "through: (gone)";
            }
        }
        if (!film) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.85f, 0.65f, 1.0f));
        } else if (viewportViewForced) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.75f, 0.45f, 1.0f));
        }
        if (ImGui::SmallButton(label.c_str())) {
            ImGui::OpenPopup("viewport-view");
        }
        if (!film || viewportViewForced) {
            ImGui::PopStyleColor();
        }
        if (ImGui::IsItemHovered()) {
            if (viewportViewForced) {
                tooltip("The canvas is showing the film because %s.\n\n"
                        "Your choice is remembered and comes back when it does.",
                        viewportViewNote.empty() ? "something else is watching it"
                                                 : viewportViewNote.c_str());
            } else if (film) {
                tooltip("The canvas is showing '%s' -- what this project renders. Moving the view\n"
                        "here MOVES THAT CAMERA, which is how a shot is composed and also how the\n"
                        "director is set aside (its cut is kept; Resume Director restores it).\n\n"
                        "Click for the editor's own viewpoint, which the film does not own.",
                        live.name.empty() ? "the main camera" : live.name.c_str());
            } else {
                tooltip("The viewport is yours. Fly anywhere: nothing you do here reaches\n"
                        "camera/position, the render or the file.\n\n"
                        "The film is still on '%s' and still renders from it.",
                        live.name.empty() ? "the main camera" : live.name.c_str());
            }
        }
        if (ImGui::BeginPopup("viewport-view")) {
            ImGui::TextDisabled("the canvas shows");
            if (ImGui::MenuItem("Editor viewpoint", nullptr, view.mode == scene::ViewportCamera::Editor)) {
                if (onViewportView) {
                    onViewportView({scene::ViewportCamera::Editor, scene::kNoCamera});
                }
                setStatus("the canvas is the editor's viewpoint -- flying it changes nothing the "
                          "project renders");
            }
            if (ImGui::MenuItem("The film's camera", nullptr, view.showsFilm())) {
                if (onViewportView) {
                    onViewportView({});
                }
                setStatus("the canvas is showing the film's camera");
            }
            ImGui::Separator();
            ImGui::TextDisabled("through a camera");
            for (const scene::CameraRig& rig : comp->cameraDirection().cameras) {
                const bool on = view.mode == scene::ViewportCamera::Through && view.camera == rig.id;
                if (ImGui::MenuItem(rig.name.c_str(), nullptr, on)) {
                    if (onViewportView) {
                        onViewportView({scene::ViewportCamera::Through, rig.id});
                    }
                    setStatus("looking through '" + rig.name + "' -- the film is unchanged");
                }
            }
            ImGui::EndPopup();
        }
    }

    // ---- fullscreen (spec §11) ----
    ImGui::SameLine();
    if (ImGui::SmallButton(preview.fullscreen ? "exit full" : "fullscreen")) {
        setFullscreenPreview(!preview.fullscreen);
    }
    if (ImGui::IsItemHovered()) {
        tooltip("Close the panels and leave the canvas the whole window. Everything that\n"
                          "was open comes back when you leave. Esc also leaves.");
    }
    if (preview.fullscreen && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        setFullscreenPreview(false);
    }

    // ---- which camera (spec §8.1: "Camera/shot indicator") ----
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::TextDisabled("%s", previewCameraLabel.c_str());
    if (ImGui::IsItemHovered()) {
        // The old text here said "there is only one camera to look through, so the preview, the
        // viewport and the deliverable cannot be looking through different ones". That stopped
        // being true at ADR-245 (a shot can put an authored rig on screen) and is the opposite of
        // true at ADR-391 (the viewport has its own viewpoint). A tooltip that states an invariant
        // the code has since abandoned is worse than none: it is the sentence somebody reasons from.
        tooltip("Which camera this frame was rendered through -- the film's, unless the toolbar\n"
                          "to the left says the canvas is showing the editor's own viewpoint or is\n"
                          "pinned through a camera. A render always uses the film's.");
    }
}

// Fullscreen preview (spec §11): a view-state change inside the existing window, not an OS-level
// mode. That is the least disruptive option and the one consistent with what this editor already
// does -- the canvas already grows when panels close, and this is that gesture with one click and
// a way back. Nothing about the project, the transport or the dock tree is touched, so there is
// nothing to restore beyond the panel set.
void ControlPanel::setFullscreenPreview(bool on) {
    if (on == preview.fullscreen) {
        return;
    }
    if (on) {
        suspendedPanels_.clear();
        for (const EditorPanel& panel : editorPanels()) {
            if (layout_.visible(panel.id)) {
                suspendedPanels_.emplace_back(panel.id);
                layout_.setVisible(panel.id, false);
            }
        }
        // A preview with nothing else on screen wants to be the deliverable's frame and not the
        // canvas's, so entering fullscreen from Workspace arrives in Preview rather than showing
        // the same wrong shape at a larger size.
        if (!preview.showsOutputFrame()) {
            preview.mode = PreviewViewMode::OutputPreview;
        }
    } else {
        for (const std::string& id : suspendedPanels_) {
            layout_.setVisible(id, true);
        }
        suspendedPanels_.clear();
    }
    preview.fullscreen = on;
}

// ---- right-clicking the world (the addendum's section 4) --------------------------------------
//
// The right button over the canvas already means "look around" (`ui::viewportIntent` ->
// `CameraLook`), and that must not change: turning the head is a gesture people use constantly and
// a menu that ate it would be a straight loss. So the same rule the sequencer strip uses applies
// here -- a right press that *travels* is the camera and a right press that does not is a menu --
// and `ui::updateContextClick` is the shared arithmetic that tells them apart. Dear ImGui's own
// `BeginPopupContextWindow` cannot: it opens on the release and measures nothing, so it would open
// a menu at the end of every single look-around.
//
// The menu acts on the current selection rather than on whatever is under the pointer, and that is
// a limitation worth naming: resolving "what is under the cursor" is a GPU readback that this
// application deliberately performs only on a left click (`Application::serviceViewportPick`), and
// doing one on a right press would put a blocking round trip on a gesture that is usually the
// camera. Right-clicking with nothing selected therefore offers the scene-level items -- paste,
// select all, undo -- which is the honest menu for "you have not told me which object".
void ControlPanel::drawCanvasContextMenu(app::Engine& engine, const CanvasRect& rect) {
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const bool over = rect.hovered && rect.contains(mouse.x, mouse.y);
    const bool wants = updateContextClick(canvasContextClick_,
                                          over && ImGui::IsMouseClicked(ImGuiMouseButton_Right),
                                          ImGui::IsMouseReleased(ImGuiMouseButton_Right), mouse.x,
                                          mouse.y);
    if (wants) {
        ImGui::OpenPopup("canvas-context");
    }
    if (ContextMenu menu("canvas-context", false); menu) {
        static_cast<void>(worldObjectMenuBody(engine, editor, {},
                                              WorldMenuHost{.frameSelection =
                                                                &editPanel.frameSelectionRequested}));
    }
}

// ---- the canvas says what the world is doing (the brief's section 4) ---------------------------
//
// Drawn here rather than inside `ui::drawViewportOverlay`, and the distinction matters: the overlay
// is what `tools/avgen_overlay_shot` rasterises headlessly, and it is the one ImGui surface in this
// application whose output is compared byte for byte. An indicator that animates off a wall clock
// belongs nowhere near it. This draws into the canvas *window's* draw list, which only ever reaches
// the window's own swapchain image.
void ControlPanel::drawCanvasActivity(app::Engine& engine, const CanvasRect& rect) {
    const char* stage = nullptr;
    float fraction = -1.0f;
    std::string scratch;

    // A load outranks everything: it is the only one of these that owns the main thread, so
    // nothing else can be running while it is.
    if (loading.active) {
        scratch = loading.stage.empty()
                      ? fmt::format("Opening {}", loading.what)
                      : fmt::format("Opening {} -- {} ({}/{})", loading.what, loading.stage,
                                    loading.index + 1, loading.count);
        stage = scratch.c_str();
    } else if (jobs != nullptr) {
        // A world-generation job. `progressKnown` is the job system's own honesty flag (ADR-064):
        // a stage that never measured itself leaves it false and gets no bar, only its name.
        for (const app::JobStatus& job : jobs->statuses()) {
            if (app::jobStateIsTerminal(job.state)) {
                continue;
            }
            scratch = job.stageName.empty() ? job.name : fmt::format("{} -- {}", job.name, job.stageName);
            stage = scratch.c_str();
            fraction = job.progressKnown ? job.progress : -1.0f;
            break;
        }
    }
    if (stage == nullptr && sequence.analyzing()) {
        stage = "Analyzing the song";
    }
    if (stage == nullptr) {
        // Geometry deliberately a few frames behind a slider (ADR-084). Its consequences section
        // ends "`proceduralsAwaitingRebuild()` exists so the editor can say so; nothing shows it
        // yet, and it should." This is that.
        if (const scene::Composition* comp = engine.composition(); comp != nullptr) {
            if (const std::size_t waiting = comp->proceduralsAwaitingRebuild(); waiting > 0) {
                scratch = waiting == 1 ? std::string("Updating geometry")
                                       : fmt::format("Updating geometry ({} objects)", waiting);
                stage = scratch.c_str();
            }
        }
    }
    if (stage == nullptr && environmentBehind) {
        // The same rule for the same reason, one subsystem along (ADR-233): the sky and everything
        // it lights are a few frames behind the slider, and the alternative to saying so is a
        // picture that quietly disagrees with the numbers in the panel.
        stage = "Updating the sky";
    }

    // A load is shown the instant it is known, with no threshold: the host only sets the flag on
    // the frame before it blocks, so a fade would mean showing nothing at all on the one frame
    // that gets presented. Everything else earns the indicator by lasting.
    ProcessingHint hint;
    if (loading.active) {
        hint.visible = true;
        hint.opacity = 1.0f;
        processing_.busyForMs = 0.0;
    } else {
        hint = processing_.advance(stage != nullptr,
                                   static_cast<double>(ImGui::GetIO().DeltaTime) * 1000.0);
    }
    if (!hint.visible) {
        return;
    }
    drawProcessingIndicator(ImGui::GetWindowDrawList(), ImVec2(rect.x, rect.y),
                            ImVec2(rect.x + rect.width, rect.y + rect.height), hint.opacity, stage,
                            fraction, ImGui::GetTime());
}

void ControlPanel::drawEditWindow(app::Engine& engine) {
    editPanel.draw(engine, editor, worldBuilder.library());
}

void ControlPanel::drawAssetsWindow() {
    if (ImGui::Button("Rescan") && onRescanAssets) {
        onRescanAssets();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%zu catalog asset(s)", catalogAssets.size());
    ImGui::SameLine();
    const char* kinds[] = {"all", "project", "scene", "graph", "preset", "model", "environment", "shader", "audio"};
    ImGui::SetNextItemWidth(140.0f);
    ImGui::Combo("kind", &assetKind_, kinds, IM_ARRAYSIZE(kinds));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200.0f);
    ImGui::InputText("search", assetSearch_, sizeof(assetSearch_));
    const app::AssetKind kind = assetKind_ == 0 ? app::AssetKind::Unknown : static_cast<app::AssetKind>(assetKind_ - 1);
    const auto shown = app::filterAssets(assets, kind, assetSearch_);
    ImGui::TextDisabled("%zu of %zu", shown.size(), assets.size());
    ImGui::Separator();
    if (ImGui::BeginTable("assets", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable)) {
        // All three text columns stretch, and the name stretches hardest. With "kind" and
        // "category" fixed at 90 and 120 the two of them plus the thumbnail claimed 260 points, and
        // the name -- the only column that identifies the row -- got whatever was left. Docked in
        // the left column at 1440x900 that was about two characters: the list read "el / be / be /
        // Ca", and its header was an ellipsis. Proportional, the name keeps the largest share at
        // every width, and the columns stay draggable for anyone who wants a different split.
        ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch, 3.0f);
        ImGui::TableSetupColumn("kind", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("category", ImGuiTableColumnFlags_WidthStretch, 1.4f);
        ImGui::TableSetupColumn("thumb", ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableHeadersRow();
        for (const app::AssetEntry* a : shown) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushID(a->path.string().c_str());
            if (ImGui::Selectable(a->name.c_str(), false, ImGuiSelectableFlags_SpanAllColumns) && onOpenAsset) {
                onOpenAsset(*a);
            }
            if (ImGui::IsItemHovered()) {
                // The row is a thing you click to open, so it says so with the pointer as well as
                // with a tooltip.
                setHoverCursor(ImGuiMouseCursor_Hand);
                if (!a->description.empty()) {
                    tooltip("%s\n%s", a->description.c_str(), a->path.string().c_str());
                }
            }
            // ---- the asset row's menu (the addendum's section 5) ---------------------------
            //
            // Short, because this browser is short: `scanAssets`/`filterAssets` and the host's
            // `onOpenAsset`/`onRescanAssets` are the entire asset API. There is no rename, no
            // duplicate, no import and no remove anywhere in `src/` -- the browser reads a
            // directory and opens what is in it -- so none of those are offered. A menu of six
            // greyed-out rows would communicate less than a menu of three live ones.
            if (ContextMenu menu("##assetmenu"); menu) {
                menuSubject(a->name);
                if (menuAction("Open", nullptr, onOpenAsset != nullptr) && onOpenAsset) {
                    onOpenAsset(*a);
                }
                if (menuAction("Show in Finder")) {
                    // The containing folder, not the file: opening the file itself would hand it
                    // to whatever application claims the extension, which for a `.json` is a text
                    // editor and is not what "reveal" means.
                    const std::string url = "file://" + a->path.parent_path().string();
                    static_cast<void>(SDL_OpenURL(url.c_str()));
                }
                if (menuAction("Copy path")) {
                    ImGui::SetClipboardText(a->path.string().c_str());
                }
                ImGui::Separator();
                if (menuAction("Rescan assets", nullptr, onRescanAssets != nullptr) && onRescanAssets) {
                    onRescanAssets();
                }
            }
            ImGui::PopID();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(app::assetKindName(a->kind));
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(a->category.c_str());
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", a->thumbnail.empty() ? "-" : "png");
        }
        ImGui::EndTable();
    }
    if (!catalogAssets.empty() && ImGui::TreeNodeEx("Ownership catalog", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* sources[] = {"all sources", "project", "built-in"};
        const char* types[] = {"all types", "model", "environment", "texture", "audio", "scene"};
        ImGui::SetNextItemWidth(130.0f);
        ImGui::Combo("##catalog-source", &catalogSource_, sources, IM_ARRAYSIZE(sources));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(130.0f);
        ImGui::Combo("##catalog-type", &catalogType_, types, IM_ARRAYSIZE(types));
        ImGui::SameLine();
        ImGui::TextDisabled("stable IDs");
        for (const auto& asset : catalogAssets) {
            const bool project = asset.source == assets::AssetSource::Project;
            const char* sourceName = assets::assetSourceName(asset.source);
            if (catalogSource_ == 1 && !project) continue;
            if (catalogSource_ == 2 && project) continue;
            if (catalogType_ > 0 && asset.type != types[catalogType_]) continue;
            if (assetSearch_[0] != '\0') {
                const std::string haystack = asset.name + " " + asset.id + " " + asset.type;
                if (haystack.find(assetSearch_) == std::string::npos) continue;
            }
            bulletWrapped("[%s] %s  %s", sourceName, asset.name.c_str(), asset.type.c_str());
            if (ImGui::IsItemHovered()) {
                tooltip("%s\n%s", asset.id.c_str(), asset.path.string().c_str());
            }
        }
        ImGui::TreePop();
    }
}

void ControlPanel::drawWorldWindow(app::Engine& engine) {
    // A capture hook, for photographing the Inspector headlessly (`--capture-ui`), which has no way
    // to click an Overview row: `AVGEN_CAPTURE_WORLD_INSPECTOR` = `environment`, `camera` or
    // `node:<name>` pins the World panel's selection to that, brings the Inspector tab to the front
    // and scrolls it to the Effects section, every frame. Unset -- which is always, outside a capture -- it does nothing.
    static const char* const pinned = std::getenv("AVGEN_CAPTURE_WORLD_INSPECTOR");
    ImGuiTabItemFlags inspectorFlags = ImGuiTabItemFlags_None;
    if (pinned != nullptr && pinned[0] != '\0') {
        const std::string_view want(pinned);
        if (want == "environment") {
            world.selection = WorldSelection{WorldSelection::Kind::Environment, ""};
        } else if (want == "camera") {
            world.selection = WorldSelection{WorldSelection::Kind::Camera, ""};
        } else if (want.starts_with("node:")) {
            world.selection = WorldSelection{WorldSelection::Kind::Node, std::string(want.substr(5))};
        }
        inspectorFlags = ImGuiTabItemFlags_SetSelected;
        world.scrollToEffects = true;
    }
    world.drawLayerSelector();
    if (ImGui::BeginTabBar("world")) {
        if (ImGui::BeginTabItem("Overview")) {
            world.drawOverview(engine);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Inspector", nullptr, inspectorFlags)) {
            world.drawInspector(engine, &editor.history());
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("States")) {
            world.drawStates(engine);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Direction")) {
            world.drawDirector(engine);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Scenarios")) {
            world.drawScenarios(engine);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Macros")) {
            world.drawMacros(engine);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Debug")) {
            world.drawDebugOptions(engine, &editor);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

void ControlPanel::drawModulation(app::Engine& engine) {
    helpHeader("modulation/overview");

    if (ImGui::BeginTabBar("modtabs")) {
        if (ImGui::BeginTabItem("Routes")) {
            drawRoutesTab(engine);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Sources")) {
            drawSourcesTab(engine);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Presets")) {
            drawPresetsTab(engine);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Shaders")) {
            drawShadersTab(engine);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Scene")) {
            drawSceneTab(engine);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Timeline")) {
            drawTimelineTab(engine);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Control")) {
            drawControlTab(engine);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Outputs")) {
            drawOutputsTab(engine);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

void ControlPanel::drawRoutesTab(app::Engine& engine) {
    using namespace params;
    auto& bus = engine.signals();
    auto& paramSet = engine.params();
    auto& modulator = engine.modulator();

    // ---- add route ----
    std::vector<const char*> signalNames;
    signalNames.reserve(bus.size());
    for (const auto& info : bus.infos()) {
        signalNames.push_back(info.name.c_str());
    }
    std::vector<const char*> targetNames;
    for (const auto* p : paramSet.ordered()) {
        if (p->flags().modulatable) {
            targetNames.push_back(p->path().c_str());
        }
    }
    // Alphabetical, not registration order. This list is every modulatable parameter in the project
    // -- over three thousand on Glowmere Valley 2 -- and registration order is an implementation
    // detail of who called `add` first, so a person hunting for `fx/cosmic-vortex/density` had no
    // way to predict where it sat. Sorted, the prefix groups everything that belongs together and
    // the combo's own type-ahead starts working, because ImGui matches against consecutive entries.
    //
    // `newRouteTarget_` is an index into this vector, so the ordering has to be the same every
    // frame or the selection would drift under the cursor. Sorting by the path is deterministic and
    // the paths are unique (a `ParameterSet` is keyed by them), so there are no ties to break.
    std::sort(targetNames.begin(), targetNames.end(),
              [](const char* a, const char* b) { return std::string_view(a) < std::string_view(b); });
    newRouteSource_ = std::clamp(newRouteSource_, 0, std::max(0, static_cast<int>(signalNames.size()) - 1));
    newRouteTarget_ = std::clamp(newRouteTarget_, 0, std::max(0, static_cast<int>(targetNames.size()) - 1));
    ImGui::SetNextItemWidth(200);
    ImGui::Combo("##src", &newRouteSource_, signalNames.data(), static_cast<int>(signalNames.size()));
    ImGui::SameLine();
    ImGui::TextUnformatted("->");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200);
    ImGui::Combo("##dst", &newRouteTarget_, targetNames.data(), static_cast<int>(targetNames.size()));
    ImGui::SameLine();
    if (ImGui::Button("Add route") && !signalNames.empty() && !targetNames.empty()) {
        ModRoute r;
        r.source = signalNames[static_cast<std::size_t>(newRouteSource_)];
        r.target = targetNames[static_cast<std::size_t>(newRouteTarget_)];
        r.amount = 1.0f;
        r.chain.attackMs = 20.0f;
        r.chain.decayMs = 200.0f;
        modulator.addRoute(r);
        engine.rebind();
    }
    ImGui::Separator();

    // ---- route list ----
    static const char* ops[] = {"add", "multiply", "replace", "min", "max"};
    static const char* curves[] = {"linear", "power", "log", "exp", "scurve"};
    static const char* envelopes[] = {"none", "peak hold", "linear fall"};
    int removeIndex = -1;
    auto& routes = modulator.routes();
    for (std::size_t i = 0; i < routes.size(); ++i) {
        auto& route = routes[i];
        ImGui::PushID(static_cast<int>(i));
        const std::string header = route.source + " -> " + route.target;
        // A click on a route in the World panel's Influences list lands here (ADR-211): open it,
        // scroll it into view and mark it, then clear the request so it fires once. Matched on
        // (source, target) rather than an index, because both panels redraw from the same live list
        // and an index is stale the moment a route above it is deleted.
        const bool focused = world.wantsRouteFocus() && route.source == world.focusRouteSource &&
                             route.target == world.focusRouteTarget;
        if (focused) {
            ImGui::SetNextItemOpen(true);
            world.focusRouteSource.clear();
            world.focusRouteTarget.clear();
        }
        if (focused) {
            // The accent the rest of this UI uses for "this is the thing you asked for".
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.78f, 0.35f, 1.0f));
        }
        const bool open = ImGui::TreeNodeEx(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
        if (focused) {
            ImGui::PopStyleColor();
            // After the item, so the scroll target is the row that was just laid out.
            ImGui::SetScrollHereY(0.35f);
        }
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 60);
        ImGui::Checkbox("##on", &route.enabled);
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) {
            removeIndex = static_cast<int>(i);
        }
        if (open) {
            route.amount = sanitiseFinite(route.amount);
            const auto [lo, hi] = routeAmountBounds(route);
            ImGui::SliderFloat("amount", &route.amount, lo, hi);
            ImGui::SameLine();
            ImGui::TextDisabled("= %+.3f", static_cast<double>(route.lastOutput));
            int op = static_cast<int>(route.op);
            ImGui::SetNextItemWidth(110);
            if (ImGui::Combo("op", &op, ops, 5)) {
                route.op = static_cast<ModOp>(op);
                engine.rebind();
            }
            ImGui::SameLine();
            bool bipolar = route.polarity == Polarity::Bipolar;
            if (ImGui::Checkbox("bipolar", &bipolar)) {
                route.polarity = bipolar ? Polarity::Bipolar : Polarity::Unipolar;
            }
            ImGui::SetNextItemWidth(140);
            ImGui::SliderFloat("attack ms", &route.chain.attackMs, 0.0f, 2000.0f, "%.0f", ImGuiSliderFlags_Logarithmic);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(140);
            ImGui::SliderFloat("decay ms", &route.chain.decayMs, 0.0f, 5000.0f, "%.0f", ImGuiSliderFlags_Logarithmic);
            int curve = static_cast<int>(route.chain.curve);
            ImGui::SetNextItemWidth(110);
            if (ImGui::Combo("curve", &curve, curves, 5)) {
                route.chain.curve = static_cast<CurveType>(curve);
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100);
            ImGui::SliderFloat("k", &route.chain.curveAmount, 0.1f, 5.0f);
            int env = static_cast<int>(route.chain.envelope);
            ImGui::SetNextItemWidth(110);
            if (ImGui::Combo("envelope", &env, envelopes, 3)) {
                route.chain.envelope = static_cast<EnvelopeMode>(env);
            }
            if (route.chain.envelope != EnvelopeMode::None) {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(100);
                ImGui::SliderFloat("fall/s", &route.chain.envelopeFallPerSecond, 0.1f, 20.0f);
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if (removeIndex >= 0) {
        routes.erase(routes.begin() + removeIndex);
        engine.rebind();
    }
}

void ControlPanel::drawSourcesTab(app::Engine& engine) {
    static const char* kinds[] = {"lfo", "envelope", "noise", "random", "timeline", "macro"};
    ImGui::SetNextItemWidth(110);
    ImGui::Combo("##kind", &newSourceKind_, kinds, 6);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140);
    ImGui::InputText("##name", newSourceName_, sizeof(newSourceName_));
    ImGui::SameLine();
    if (ImGui::Button("Add source")) {
        engine.addSource(kinds[newSourceKind_], newSourceName_[0] ? newSourceName_ : "source");
    }
    ImGui::Separator();
    auto& bus = engine.signals();
    std::string removeKind;
    std::string removeName;
    for (const auto& source : engine.sources().sources()) {
        ImGui::PushID(source.get());
        const std::string header = source->kind() + " " + source->name();
        const bool open = ImGui::TreeNodeEx(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 24);
        if (ImGui::SmallButton("x")) {
            removeKind = source->kind();
            removeName = source->name();
        }
        if (open) {
            for (const auto& out : source->outputs()) {
                if (auto id = bus.find(out)) {
                    ImGui::ProgressBar(std::clamp(bus.value(*id), 0.0f, 1.0f), ImVec2(160, 0), out.c_str());
                }
            }
            if (source->kind() == "lfo") {
                auto* lfo = dynamic_cast<signals::LfoSource*>(source.get());
                int shape = static_cast<int>(lfo->shape());
                static const char* shapes[] = {"sine", "triangle", "saw", "square", "sample&hold"};
                ImGui::SetNextItemWidth(140);
                if (ImGui::Combo("shape", &shape, shapes, 5)) {
                    lfo->setShape(static_cast<signals::LfoShape>(shape));
                }
            }
            ImGui::TextDisabled("settings: Parameters window, group 'sources'");
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if (!removeName.empty()) {
        engine.removeSource(removeKind, removeName);
    }
}

void ControlPanel::drawPresetsTab(app::Engine& engine) {
    ImGui::SetNextItemWidth(200);
    ImGui::InputText("##preset", presetName_, sizeof(presetName_));
    ImGui::SameLine();
    if (ImGui::Button("Store")) {
        engine.storePreset(presetName_[0] ? presetName_ : "preset");
    }
    ImGui::Separator();
    auto& bank = engine.presets();
    std::string removeName;
    std::vector<const char*> names;
    for (const auto& preset : bank.presets()) {
        names.push_back(preset.name.c_str());
    }
    for (const auto& preset : bank.presets()) {
        ImGui::PushID(preset.name.c_str());
        if (ImGui::Button("Recall") && !engine.recallPreset(preset.name)) {
            status_ = "preset not found: " + preset.name;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) {
            removeName = preset.name;
        }
        ImGui::SameLine();
        ImGui::Text("%s (%zu values)", preset.name.c_str(), preset.values.size());
        ImGui::PopID();
    }
    if (!removeName.empty()) {
        bank.remove(removeName);
    }
    if (names.size() >= 2) {
        ImGui::Separator();
        ImGui::TextUnformatted("Morph");
        morphA_ = std::clamp(morphA_, 0, static_cast<int>(names.size()) - 1);
        morphB_ = std::clamp(morphB_, 0, static_cast<int>(names.size()) - 1);
        ImGui::SetNextItemWidth(140);
        ImGui::Combo("A", &morphA_, names.data(), static_cast<int>(names.size()));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(140);
        ImGui::Combo("B", &morphB_, names.data(), static_cast<int>(names.size()));
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderFloat("##morph", &morphT_, 0.0f, 1.0f, "A %.2f B")) {
            engine.morphPresets(names[static_cast<std::size_t>(morphA_)], names[static_cast<std::size_t>(morphB_)], morphT_);
        }
    }
}

void ControlPanel::drawTransport(app::Engine& engine) {
    if (engine.mode() == app::EngineMode::Live) {
        static std::vector<audio::AudioDeviceInfo> devices;
        static double lastScan = -1.0;
        const double now = ImGui::GetTime();
        if (now - lastScan > 5.0) {
            devices = audio::listCaptureDevices();
            lastScan = now;
        }
        std::vector<const char*> names;
        names.push_back("(audio file)");
        for (const auto& d : devices) {
            names.push_back(d.name.c_str());
        }
        inputDevice_ = std::clamp(inputDevice_, 0, static_cast<int>(names.size()) - 1);
        ImGui::SetNextItemWidth(220);
        if (ImGui::Combo("input", &inputDevice_, names.data(), static_cast<int>(names.size()))) {
            if (inputDevice_ == 0) {
                if (onStopAudioInput) onStopAudioInput();
            } else if (onUseAudioInput) {
                onUseAudioInput(devices[static_cast<std::size_t>(inputDevice_ - 1)].name);
            }
        }
        if (auto* input = engine.audioInput()) {
            ImGui::SameLine();
            ImGui::ProgressBar(std::clamp(input->lastPeak(), 0.0f, 1.0f), ImVec2(80, 0), "peak");
            ImGui::SameLine();
            ImGui::TextDisabled("%s %u Hz", input->deviceName().c_str(), input->sampleRate());
        }
    }
    if (!engine.projectPath().empty()) {
        ImGui::TextDisabled("project: %s", engine.projectPath().filename().string().c_str());
        if (!engine.projectWarnings().empty()) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "(%zu warning(s))", engine.projectWarnings().size());
            if (ImGui::IsItemHovered()) {
                std::string all;
                for (const auto& w : engine.projectWarnings()) {
                    all += w + "\n";
                }
                tooltip("%s", all.c_str());
            }
        }
    }
    // The import button and the compact transport used to be here, and both are gone (ADR-216):
    // audio belongs to the Sequencer, which is where the waveform, the sections and the timeline
    // are. Three things remain, and each is here because it is *not* a duplicate of anything the
    // Sequencer has: which file is loaded, where the playhead is as a draggable value, and how loud
    // it is.
    //
    // What must not come back with the button is the assumption that went with it. The old Control
    // transport was disabled whenever no audio was loaded, and ADR-102 removed exactly that: a
    // project without audio has a transport like any other. Nothing below is gated on `hasAudio`
    // except the volume, which genuinely has nothing to be the volume of.
    if (engine.hasAudio()) {
        ImGui::TextUnformatted(engine.audioPath().filename().string().c_str());
    } else {
        ImGui::TextDisabled("no audio loaded (Sequencer > Import Audio..., the File menu, O, or "
                            "drop a file on the window)");
    }
    if (!status_.empty()) {
        ImGui::TextColored(statusColour(), "%s", status_.c_str());
    }

    float position = static_cast<float>(engine.positionSeconds());
    const float duration = static_cast<float>(std::max(engine.durationSeconds(), 0.001));
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderFloat("##seek", &position, 0.0f, duration, "%.2f s")) {
        engine.seekSeconds(static_cast<double>(position));
    }
    ImGui::BeginDisabled(!engine.hasAudio());
    float volume = engine.volume();
    // Not `-1`. ImGui draws a widget's label *after* the widget, so "fill to the right edge" puts
    // the label past the edge, where it is clipped: this slider was drawn as an unlabelled bar with
    // a stray "V" against the border.
    ImGui::SetNextItemWidth(itemWidthForLabel("Volume"));
    if (ImGui::SliderFloat("Volume", &volume, 0.0f, 1.0f)) {
        engine.setVolume(volume);
    }
    ImGui::EndDisabled();
}

void ControlPanel::drawResponse(app::Engine& engine) {
    ImGui::Text("Scene: %s", engine.controller().name().c_str());
    if (!engine.environmentPath().empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("env: %s", engine.environmentPath().filename().string().c_str());
    }
    ImGui::TextUnformatted("Audio response (modulation routes)");
    auto& modulator = engine.modulator();
    ImGui::SetNextItemWidth(itemWidthForLabel("Master gain"));
    ImGui::SliderFloat("Master gain", &modulator.masterGain, 0.0f, 3.0f);
    int id = 0;
    for (auto& route : modulator.routes()) {
        ImGui::PushID(id++);
        ImGui::Checkbox("##on", &route.enabled);
        ImGui::SameLine();
        const std::string label = route.source + " -> " + route.target;
        route.amount = sanitiseFinite(route.amount);
        const auto [lo, hi] = routeAmountBounds(route);
        // A route's label is a source and a target path joined by an arrow --
        // "audio.bass -> particles/emission" -- and the 90 points this used to reserve held none of
        // it. Every slider in the list was drawn with its label outside the window, so the panel
        // was a column of identical bars with nothing to say which route each one was. Measured
        // rather than guessed, with room kept for the live readout that follows on the same line.
        const float readout = ImGui::CalcTextSize("-0.00").x + ImGui::GetStyle().ItemSpacing.x;
        ImGui::SetNextItemWidth(itemWidthForLabel(label.c_str(), readout));
        // Ctrl+click still allows typing values beyond the slider range.
        ImGui::SliderFloat(label.c_str(), &route.amount, lo, hi);
        ImGui::SameLine();
        ImGui::Text("%+.2f", static_cast<double>(route.lastOutput));
        ImGui::PopID();
    }
}

void ControlPanel::drawParameters(app::Engine& engine) {
    helpHeader("modulation/parameters");

    using namespace params;
    // Gathered by group before anything is drawn, rather than emitting a header whenever the group
    // changes from one parameter to the next.
    //
    // Parameter order is registration order, and a group's members are not contiguous in it: every
    // node registers its own handful under "nodes", interleaved with whatever registered between
    // them. Comparing against only the *previous* group therefore emitted the "nodes" header once
    // per node -- eight times on Glowmere -- each an ImGui tree node with the same id, which is
    // what ImGui was reporting as two visible items with conflicting IDs. It was also simply wrong
    // as a list: one group, shown eight times, each holding a fraction of its parameters.
    //
    // Groups keep first-appearance order so the panel does not reshuffle itself when a node is
    // added.
    std::vector<std::string> order;
    std::unordered_map<std::string, std::vector<IParameter*>> grouped;
    for (IParameter* param : engine.params().ordered()) {
        if (!param->flags().exposed) {
            continue;
        }
        if (!world.shows(param->path())) {
            continue; // hidden by the authoring layer (World window)
        }
        auto [it, inserted] = grouped.try_emplace(param->group());
        if (inserted) {
            order.push_back(param->group());
        }
        it->second.push_back(param);
    }

    // What the camera is doing right now, so a parameter belonging to one of the other two ways of
    // placing it can say that it is not being read (`parameterInertness`).
    const auto readInt = [&](const char* path, int fallback) {
        const params::IParameter* p = engine.params().find(path);
        return p != nullptr ? static_cast<int>(std::lround(p->finalComponent(0))) : fallback;
    };
    const int cameraMode = readInt("camera/mode", 1);
    const bool explicitFov = readInt("camera/lens/useExplicitFov", 1) != 0;

    for (const std::string& group : order) {
      // Closed by default. This panel lists every exposed parameter in the project -- Glowmere
      // Valley 2 registers over three thousand -- and opening all of them at once gives somebody a
      // wall to scroll rather than a set of groups to choose between. ImGui remembers each node's
      // state per window, so a group somebody opens stays open across sessions; this only decides
      // what an untouched group does the first time the panel is seen.
      const bool groupOpen = ImGui::TreeNodeEx(group.c_str());
      if (!groupOpen) {
          continue;
      }
      // One row. Extracted so the sub-group pass below can draw rows in two places -- the group's
      // own direct members and each sub-group's -- without a second copy of the annotations.
      const auto drawRow = [&](IParameter* param) {
        ImGui::PushID(param->path().c_str());
        const std::size_t n = param->componentCount();
        // ADR-387: the one implementation, shared with the World panel's Inspector.
        drawParameterValue(*param);
        // Show the modulated (final) value next to the slider when it differs.
        if (n == 1 && std::abs(param->finalComponent(0) - param->baseComponent(0)) > 1e-5f) {
            ImGui::SameLine();
            ImGui::TextDisabled("= %.3f", static_cast<double>(param->finalComponent(0)));
        }
        if (engine.timeline().isAutomated(param->path())) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "[A]");
            if (ImGui::IsItemHovered()) {
                tooltip("automated by the timeline; the slider is the base value");
            }
        }
        // Not reaching the picture in the state the scene is in. Said next to the slider, in the
        // same place and the same way as "[A]", because the two are the same kind of fact: the
        // number is real and something else is deciding what you see.
        const ParameterInertness inert = parameterInertness(param->path(), cameraMode, explicitFov);
        if (inert.inert) {
            ImGui::SameLine();
            ImGui::TextDisabled("[ignored]");
            if (ImGui::IsItemHovered()) {
                tooltip("%.*s, and this belongs to %.*s. The value is kept and saved; "
                                  "right-click to switch.",
                                  static_cast<int>(inert.because.size()), inert.because.data(),
                                  static_cast<int>(inert.belongsTo.size()), inert.belongsTo.data());
            }
        }
        if (ImGui::BeginPopupContextItem("reset")) {
            if (inert.inert) {
                // The explanation, as an action. Knowing *why* a slider does nothing is only half
                // an answer if acting on it means going to find another parameter by name.
                const std::string label(inert.fixLabel);
                if (ImGui::MenuItem(label.c_str())) {
                    if (params::IParameter* fix = engine.params().find(std::string(inert.fixPath))) {
                        fix->setBaseComponent(0, inert.fixValue);
                    }
                }
                ImGui::Separator();
            }
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

      // ---- sub-groups, because "enabled" is not the name of anything ----------------------------
      //
      // `group()` is the FIRST path segment and `label()` is the LAST, so everything between them
      // was being thrown away. Under `post` that put `post/bloom/enabled`, `post/halation/enabled`
      // and `post/anamorphic/enabled` in one flat list as three checkboxes all labelled "enabled",
      // above three sliders all labelled "intensity". The owner reported it exactly: "I have no
      // idea what I'm enabling when I click a checkbox."
      //
      // So the middle of the path becomes a heading. Nothing here knows the name of any subsystem
      // -- the structure is read off the paths, the same way the World panel's Inspector groups a
      // node's properties (ADR-387), which is what keeps this from becoming a table of special
      // cases that goes stale the moment somebody registers a new effect.
      std::vector<IParameter*> direct;
      std::vector<std::string> subOrder;
      std::unordered_map<std::string, std::vector<IParameter*>> subs;
      for (IParameter* param : grouped[group]) {
          const std::string sub = ui::parameterSubGroup(param->path(), group);
          if (sub.empty()) {
              direct.push_back(param);
              continue;
          }
          auto [it, inserted] = subs.try_emplace(sub);
          if (inserted) {
              subOrder.push_back(sub);
          }
          it->second.push_back(param);
      }
      for (IParameter* param : direct) {
          drawRow(param);
      }
      // Open by default only where that is not itself a wall. `post` has a handful of sub-groups and
      // wants them all visible; `nodes` has one per node -- eighty on Glowmere Valley 2 -- and
      // opening those would undo the reason the outer groups are closed in the first place.
      const bool subsOpenByDefault = subOrder.size() <= 8;
      for (const std::string& sub : subOrder) {
          std::vector<IParameter*>& members = subs[sub];
          // The section's own switch, found by its leaf rather than by a list of known names.
          IParameter* gate = nullptr;
          for (IParameter* param : members) {
              if (ui::parameterLeaf(param->path()) == "enabled") {
                  gate = param;
              }
          }
          const bool on = gate == nullptr || gate->baseComponent(0) >= 0.5f;
          ImGui::PushID(sub.c_str());
          const bool subOpen = ImGui::TreeNodeEx(
              sub.c_str(), subsOpenByDefault ? ImGuiTreeNodeFlags_DefaultOpen : 0);
          if (gate != nullptr && !on) {
              // Said on the header, so a collapsed section still tells the truth about itself.
              ImGui::SameLine();
              ImGui::TextDisabled("(off)");
          }
          if (subOpen) {
              // The switch first and always live, then everything it gates. Greying the rest is the
              // owner's second request and it is also an answer to a real ambiguity: a slider that
              // reads 0.5 in a section that is off looks exactly like a slider that is doing
              // something. The values are kept and saved either way -- this changes what the panel
              // says, not what the project holds.
              if (gate != nullptr) {
                  drawRow(gate);
              }
              ImGui::BeginDisabled(!on);
              for (IParameter* param : members) {
                  if (param != gate) {
                      drawRow(param);
                  }
              }
              ImGui::EndDisabled();
              ImGui::TreePop();
          }
          ImGui::PopID();
      }
      ImGui::TreePop();
    }
}

void ControlPanel::drawAnalysis(app::Engine& engine) {
    helpHeader("audio/analysis");

    const auto& frame = engine.latestFrame();
    const bool have = engine.hasFrame();

    // Band history ring
    for (std::size_t b = 0; b < 5; ++b) {
        if (bandHistory_[b].size() != kBandHistory) {
            bandHistory_[b].assign(kBandHistory, 0.0f);
        }
        bandHistory_[b][bandHistoryHead_] = have && b < frame.bandCount ? frame.bands[b] : 0.0f;
    }
    bandHistoryHead_ = (bandHistoryHead_ + 1) % kBandHistory;
    onsetFlash_ = have && frame.onset ? 1.0f : onsetFlash_ * 0.85f;

    const auto d = [have](float v) { return have ? static_cast<double>(v) : 0.0; };
    ImGui::Text("rms %.3f  peak %.3f  centroid %.0f Hz  flux %.3f  onset %.2f", d(frame.rms), d(frame.peak),
                d(frame.centroidHz), d(frame.flux), d(frame.onsetStrength));
    ImGui::SameLine();
    ImGui::ColorButton("##onset", ImVec4(onsetFlash_, onsetFlash_ * 0.5f, 0.1f, 1.0f), ImGuiColorEditFlags_NoTooltip,
                       ImVec2(18, 18));

    // Bands
    if (ImPlot::BeginPlot("Bands", ImVec2(-1, 140), ImPlotFlags_NoLegend | ImPlotFlags_NoMenus)) {
        ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoDecorations, ImPlotAxisFlags_Lock);
        ImPlot::SetupAxesLimits(-0.5, 4.5, 0.0, 1.0, ImPlotCond_Always);
        float values[5] = {};
        for (std::size_t b = 0; b < 5; ++b) {
            values[b] = have && b < frame.bandCount ? frame.bands[b] : 0.0f;
        }
        ImPlot::PlotBars("bands", values, 5, 0.7);
        ImPlot::EndPlot();
    }
    ImGui::Text("bass %.2f  lowMid %.2f  mid %.2f  highMid %.2f  treble %.2f", d(frame.bands[0]), d(frame.bands[1]),
                d(frame.bands[2]), d(frame.bands[3]), d(frame.bands[4]));

    // Band history
    if (ImPlot::BeginPlot("Band history", ImVec2(-1, 140), ImPlotFlags_NoMenus)) {
        ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoDecorations, ImPlotAxisFlags_Lock);
        ImPlot::SetupAxesLimits(0, static_cast<double>(kBandHistory), 0.0, 1.05, ImPlotCond_Always);
        plotY_.resize(kBandHistory);
        for (std::size_t b = 0; b < 5; ++b) {
            for (std::size_t i = 0; i < kBandHistory; ++i) {
                plotY_[i] = bandHistory_[b][(bandHistoryHead_ + i) % kBandHistory];
            }
            ImPlot::PlotLine(bandName(b), plotY_.data(), static_cast<int>(kBandHistory));
        }
        ImPlot::EndPlot();
    }

    // Spectrum
    if (ImPlot::BeginPlot("Spectrum", ImVec2(-1, 160), ImPlotFlags_NoLegend | ImPlotFlags_NoMenus)) {
        ImPlot::SetupAxes("Hz", nullptr, ImPlotAxisFlags_None, ImPlotAxisFlags_Lock);
        ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);
        const auto& cfg = engine.analyzerConfig();
        const double nyquist = cfg.sampleRate / 2.0;
        ImPlot::SetupAxesLimits(20.0, nyquist, 0.0, 1.0, ImPlotCond_Always);
        if (have && !frame.spectrum.empty()) {
            const std::size_t bins = frame.spectrum.size();
            plotX_.resize(bins);
            plotY_.resize(bins);
            for (std::size_t i = 0; i < bins; ++i) {
                plotX_[i] = static_cast<float>(i) * static_cast<float>(cfg.sampleRate) / static_cast<float>(cfg.windowSize);
                plotY_[i] = frame.spectrum[i];
            }
            ImPlot::PlotShaded("spectrum", plotX_.data() + 1, plotY_.data() + 1, static_cast<int>(bins - 1), 0.0);
            ImPlot::PlotLine("spectrum", plotX_.data() + 1, plotY_.data() + 1, static_cast<int>(bins - 1));
        }
        ImPlot::EndPlot();
    }

    // Waveform: a window of the decoded file around the play-head.
    if (ImPlot::BeginPlot("Waveform", ImVec2(-1, 120), ImPlotFlags_NoLegend | ImPlotFlags_NoMenus)) {
        ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoDecorations, ImPlotAxisFlags_Lock);
        constexpr int kPoints = 1024;
        ImPlot::SetupAxesLimits(0, kPoints, -1.0, 1.0, ImPlotCond_Always);
        if (auto file = engine.audioFile()) {
            const auto mono = file->mono();
            const double centre = engine.positionSeconds() * file->sampleRate();
            const double span = file->sampleRate() * 0.2; // 200 ms window
            waveform_.resize(kPoints);
            for (int i = 0; i < kPoints; ++i) {
                const double t = centre - span * 0.5 + span * static_cast<double>(i) / kPoints;
                const auto idx = static_cast<long long>(t);
                waveform_[static_cast<std::size_t>(i)] =
                    idx >= 0 && idx < static_cast<long long>(mono.size()) ? mono[static_cast<std::size_t>(idx)] : 0.0f;
            }
            ImPlot::PlotLine("wave", waveform_.data(), kPoints);
        }
        ImPlot::EndPlot();
    }
}

void ControlPanel::drawForensicArms() {
    // §13 / renderer-forensics Phase 4.2. Drawn from one function into two panels -- Control's
    // compact section and the Performance dashboard -- so the two cannot come to offer different
    // arms. An arm that exists in one surface and not the other is how somebody clears a subsystem
    // that was never actually switched off.
    if (renderer == nullptr) {
        return;
    }
        rendering::SceneRenderer::PassToggles toggles = renderer->passToggles();
        bool changed = false;
        const auto arm = [&](const char* label, bool& value, const char* tip) {
            // Shown as "off" switches: the question being asked is always "does the symptom
            // survive without this", so the box that is *ticked* is the one taking something
            // away.
            bool disabled = !value;
            if (ImGui::Checkbox(label, &disabled)) {
                value = !disabled;
                changed = true;
            }
            if (ImGui::IsItemHovered()) {
                tooltip("%s", tip);
            }
        };
        arm("no shadows", toggles.shadows, "the cascade and spot depth passes");
        ImGui::SameLine();
        arm("no shadow mask", toggles.shadowMask, "ADR-087's screen-space mask; the lit pass "
                                                  "computes the term per pixel instead");
        arm("no AO", toggles.ao, "GTAO and its temporal resolve");
        ImGui::SameLine();
        arm("no volumetrics", toggles.volume, "the volumetric march and composite");
        ImGui::SameLine();
        arm("no post", toggles.post, "the built-in post chain");
        ImGui::SameLine();
        arm("no FXAA", toggles.antialias,
            "the edge antialiasing at the end of the post chain (ADR-059), on its own.\n\n"
            "Its own arm rather than part of 'no post', which also removes the tone map and\n"
            "changes the whole frame's brightness. This removes one filter and nothing else,\n"
            "so 'are these crawling edges FXAA?' has a direct answer: tick it and look.\n\n"
            "Measured as the largest single source of temporal instability in Glowmere --\n"
            "about 20% of the flickering area. How much antialiasing there is at all is\n"
            "post/output/antialias in the Parameters panel; this is the on/off for looking.");
        arm("no culling", toggles.culling, "submit everything, whatever the camera cull decided");
        ImGui::SameLine();
        arm("no water", toggles.water, "water surfaces are not drawn");
        arm("no transparency", toggles.transparency, "blended entities are not drawn");
        ImGui::SameLine();
        arm("no particles", toggles.particles, "no simulation and no draw, so switching it back "
                                               "on does not reveal a system that has been "
                                               "running invisibly");
        arm("no animation", toggles.animation, "skinned meshes draw in bind pose; the palettes "
                                               "are not uploaded at all");
        ImGui::SameLine();
        arm("freeze animation", toggles.animationMotion,
            "every skinned character holds the pose it has now. Not the same control as 'no "
            "animation', which takes the pose away and leaves a bind pose: this one stops the "
            "character moving where it is, so 'the pose is wrong' and 'the pose is not "
            "changing' can be told apart.");
        arm("freeze the view", toggles.cameraMotion,
            "holds the view and projection the scene pass draws with, and ignores the cull "
            "verdicts decided against the camera that has since moved. The sky, the volumetrics "
            "and the particles read the live camera themselves, so the frame still changes -- "
            "what is frozen is the projection of the geometry.");
        if (changed) {
            renderer->setPassToggles(toggles);
        }
        const rendering::SceneRenderer::PassToggles defaults;
        // Asked of the arm table rather than restated here. The hand-written version of this
        // line had to be edited every time an arm was added, and the failure when it was not is
        // the worst one this panel has: the warning below goes quiet while an arm is off, so the
        // frame that is an A/B arm looks like the picture.
        bool anythingOff = false;
        for (const auto& entry : rendering::SceneRenderer::passArms()) {
            anythingOff = anythingOff || !(toggles.*(entry.flag));
        }
        if (anythingOff) {
            // Loud, because a frame with an arm switched off is not a frame anybody should
            // judge the renderer by, and this panel is not always on screen.
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f),
                               "isolation active: this frame is an A/B arm, not the picture");
            ImGui::SameLine();
            if (ImGui::SmallButton("restore all")) {
                renderer->setPassToggles(defaults);
            }
        }
}

// §13. "Why is this frame expensive?" answered without reading a log.
//
// The data has been machine-readable since ADR-148 -- `--bench-json` carries percentiles, counters
// and conditions -- and had no surface at all, which is the one gap in this area the assessment says
// genuinely needs a person rather than an agent. This is that surface.
//
// Three things it deliberately does and one it deliberately does not. It shows a *window* rather
// than this frame, because a single sample of a frame time on this machine says very little -- the
// same unmodified scene has measured 10.9 ms and 13.7 ms in consecutive runs. It sorts the phases
// by cost, because the question is always "what is the biggest thing" and a fixed order makes that
// a reading exercise. It puts the isolation arms in the same panel, because the next question after
// "the scene pass is 11 ms" is "of what", and that is an arm away.
//
// What it does not do is present any of these numbers as a measurement. Timings taken while an
// editor is drawing are not evidence (ADR-170), and the panel says so rather than letting a figure
// read off it be quoted later.
void ControlPanel::drawPerformanceDashboard(app::Engine& engine, const FrameStats& stats) {
    helpHeader("performance/diagnosis");

    // ---- the window -----------------------------------------------------------------------------
    frameMsHistory_[perfCursor_] = static_cast<float>(stats.frameIntervalMs);
    gpuMsHistory_[perfCursor_] = static_cast<float>(stats.gpuFrameMs >= 0.0 ? stats.gpuFrameMs : 0.0);
    perfCursor_ = (perfCursor_ + 1) % kPerfHistory;
    ++perfSamples_;
    const std::size_t filled = std::min<std::size_t>(perfSamples_, kPerfHistory);

    // Median and the tail, not the mean: one 40 ms hitch moves a mean and is exactly the thing a
    // person is usually looking for, so it gets its own number instead of being averaged away.
    std::vector<float> sorted(frameMsHistory_.begin(), frameMsHistory_.begin() + static_cast<long>(filled));
    std::sort(sorted.begin(), sorted.end());
    const auto at = [&](double q) {
        if (sorted.empty()) {
            return 0.0f;
        }
        const auto i = static_cast<std::size_t>(q * static_cast<double>(sorted.size() - 1));
        return sorted[i];
    };
    ImGui::Text("%.1f fps   frame %.2f ms median, %.2f ms p95, %.2f ms worst   over %zu frames",
                stats.fps, at(0.5), at(0.95), sorted.empty() ? 0.0f : sorted.back(), filled);
    ImGui::Text("cpu work %.2f ms   gpu %s   %ux%u", stats.cpuFrameMs,
                stats.gpuFrameMs >= 0.0 ? fmt::format("{:.2f} ms", stats.gpuFrameMs).c_str() : "n/a",
                stats.width, stats.height);
    // §15-§17 / §41. The line that answers "why does this look softer than it did a moment ago",
    // and the one a reader needs before any per-pixel figure on this panel means anything. Shown
    // only when the two extents differ, so an editor keeping up says nothing new.
    if (stats.sceneWidth > 0 && stats.sceneHeight > 0 &&
        (stats.sceneWidth != stats.width || stats.sceneHeight != stats.height)) {
        const double canvasMpx = static_cast<double>(stats.width) * stats.height / 1.0e6;
        const double sceneMpx = static_cast<double>(stats.sceneWidth) * stats.sceneHeight / 1.0e6;
        ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.35f, 1.0f),
                           "world drawn at %ux%u (%.2f of %.2f Mpx) and sharpened up",
                           stats.sceneWidth, stats.sceneHeight, sceneMpx, canvasMpx);
        if (ImGui::IsItemHovered()) {
            tooltip("The viewport's adaptive render scale (Settings > Rendering). The scene is\n"
                    "rendered below the canvas's resolution and filtered back up into it, which\n"
                    "is a change to this picture and to nothing else -- the scene, its\n"
                    "parameters and every render are unaffected. It engages only when the GPU is\n"
                    "both over its budget and what the frame is waiting for, and it goes back up\n"
                    "when the scene gets cheaper.");
        }
    }
    if (filled > 2) {
        const float ceiling = std::max(sorted.back() * 1.1f, 20.0f);
        ImGui::PlotLines("##frame", frameMsHistory_.data(), static_cast<int>(kPerfHistory),
                         static_cast<int>(perfCursor_), "frame ms", 0.0f, ceiling, ImVec2(-1, 48));
        if (stats.gpuFrameMs >= 0.0) {
            ImGui::PlotLines("##gpu", gpuMsHistory_.data(), static_cast<int>(kPerfHistory),
                             static_cast<int>(perfCursor_), "gpu ms", 0.0f, ceiling, ImVec2(-1, 48));
        }
    }
    ImGui::TextColored(ImVec4(0.65f, 0.65f, 0.7f, 1.0f),
                       "a timing taken while the editor is drawing is not a measurement");
    if (ImGui::IsItemHovered()) {
        tooltip("ADR-170: tools/gpu-lock.sh serialises agents, not the device. Anything\n"
                          "else using the GPU -- including this window -- is invisible to it, and a\n"
                          "17%% confounder survives it. For a number worth quoting use\n"
                          "--headless --bench-json, or --ab to compare two arms interleaved inside\n"
                          "one process. These figures are for finding the big thing, not for\n"
                          "reporting it.");
    }

    // ---- where the GPU time goes ----------------------------------------------------------------
    //
    // Summed per label, because several passes share one by design (two cascades, a bloom pyramid,
    // a scene pass split around the SDF raymarch) and it is the phase's total that an arm moves.
    if (renderer != nullptr) {
        const std::vector<gpu::TimelineInterval> byLabel =
            rendering::sumByLabel(renderer->timeline().passes());
        if (!byLabel.empty()) {
            ImGui::SeparatorText("GPU passes, this frame");
            std::vector<gpu::TimelineInterval> rows = byLabel;
            std::sort(rows.begin(), rows.end(),
                      [](const gpu::TimelineInterval& a, const gpu::TimelineInterval& b) { return a.ms > b.ms; });
            double total = 0.0;
            for (const auto& r : rows) {
                total += r.ms;
            }
            const double widest = rows.empty() ? 1.0 : std::max(rows.front().ms, 1e-6);
            for (const auto& r : rows) {
                // A bar rather than a column of numbers: the shape of the answer is "one of these is
                // most of it", and a bar says that at a glance where eleven decimals do not.
                ImGui::ProgressBar(static_cast<float>(r.ms / widest), ImVec2(-190, 0), "");
                ImGui::SameLine();
                ImGui::Text("%6.3f ms  %4.1f%%  %s", r.ms, total > 0.0 ? 100.0 * r.ms / total : 0.0,
                            r.label.c_str());
            }
            ImGui::TextDisabled("%zu labels, %.3f ms attributed", rows.size(), total);
        }
    }

    // ---- where the CPU time goes ----------------------------------------------------------------
    if (renderer != nullptr) {
        const rendering::CpuFrameBreakdown& cpu = renderer->stats().cpu;
        ImGui::SeparatorText("CPU phases, this frame");
        struct Row {
            const char* name;
            double ms;
        };
        std::array<Row, 15> rows{{
            {"uploads", cpu.uploadsMs},         {"lights", cpu.lightsMs},
            {"objects", cpu.objectsMs},         {"fields", cpu.fieldsMs},
            {"simulation", cpu.simulationMs},   {"particles", cpu.particlesMs},
            {"procedural", cpu.proceduralMs},   {"sdf", cpu.sdfMs},
            {"shadow encode", cpu.shadowEncodeMs}, {"background encode", cpu.backgroundEncodeMs},
            {"depth encode", cpu.depthEncodeMs}, {"scene encode", cpu.sceneEncodeMs},
            {"volume encode", cpu.volumeEncodeMs}, {"post encode", cpu.postEncodeMs},
            {"tonemap encode", cpu.tonemapEncodeMs},
        }};
        std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.ms > b.ms; });
        const double widest = std::max(rows.front().ms, 1e-6);
        for (const Row& r : rows) {
            if (r.ms < 0.005) {
                continue; // below the clock's own resolution; a row of zeroes is noise, not data
            }
            ImGui::ProgressBar(static_cast<float>(r.ms / widest), ImVec2(-190, 0), "");
            ImGui::SameLine();
            ImGui::Text("%6.3f ms  %s", r.ms, r.name);
        }
        // Stated rather than hidden: the phases are a partition of `render()`, so anything they do
        // not account for is real time spent somewhere nobody has labelled.
        ImGui::TextDisabled("total %.3f ms, of which %.3f ms unattributed", cpu.totalMs,
                            cpu.unattributedMs());
    }

    // ---- what the frame contains ----------------------------------------------------------------
    ImGui::SeparatorText("What the frame contains");
    ImGui::Text("%u draws   %u triangles   %u entities", stats.drawCalls, stats.triangles,
                renderer != nullptr ? renderer->stats().entities : 0u);
    if (renderer != nullptr) {
        const auto& rs = renderer->stats();
        ImGui::Text("%u shadow casters   %u lights shaded", rs.shadowCasters, rs.shadedLights);
    }
    if (stats.procedural.objects > 0) {
        const auto& pr = stats.procedural;
        ImGui::Text("procedural: %u objects, %llu instances (%llu visible, %llu culled)", pr.objects,
                    static_cast<unsigned long long>(pr.instances),
                    static_cast<unsigned long long>(pr.visibleInstances),
                    static_cast<unsigned long long>(pr.culledInstances));
        ImGui::Text("  lod rungs %llu / %llu / %llu / %llu", static_cast<unsigned long long>(pr.lodCounts[0]),
                    static_cast<unsigned long long>(pr.lodCounts[1]),
                    static_cast<unsigned long long>(pr.lodCounts[2]),
                    static_cast<unsigned long long>(pr.lodCounts[3]));
    }
    // ADR-351: entity LOD, beside the scatter's ladder rather than folded into it. The two answer
    // the same question about different populations, and one number covering "the hero tree
    // demoted" and "eighty thousand ferns demoted" is a number nobody can read. Shown only when
    // something in the scene has a chain, which is nothing until a node asks.
    if (renderer != nullptr && renderer->stats().entityLod.drawables > 0) {
        const auto& el = renderer->stats().entityLod;
        ImGui::Text("entity lod: %u of %u demoted, %llu -> %llu triangles (%.1f%%)", el.demoted,
                    el.drawables, static_cast<unsigned long long>(el.sourceTriangles),
                    static_cast<unsigned long long>(el.drawnTriangles),
                    100.0 * static_cast<double>(el.ratio()));
        ImGui::Text("  rungs %u / %u / %u / %u / %u   %u changed   %u held", el.rungs[0], el.rungs[1],
                    el.rungs[2], el.rungs[3], el.rungs[4], el.changed, el.held);
        if (el.held > 0) {
            ImGui::SameLine();
            ImGui::TextDisabled("(hysteresis is on: what you see depends on how the camera arrived)");
        }
    }
    if (stats.particles.systems > 0) {
        ImGui::Text("particles: %u systems, %u capacity, %u emitted this frame", stats.particles.systems,
                    stats.particles.capacity, stats.particles.emittedThisFrame);
    }
    if (stats.sdf.objects > 0) {
        ImGui::Text("sdf: %u objects (%u raymarched, %u meshed)", stats.sdf.objects, stats.sdf.raymarchObjects,
                    stats.sdf.meshObjects);
    }
    ImGui::TextDisabled("%s / %s", stats.adapter.c_str(), stats.backend.c_str());

    // ---- the arms -------------------------------------------------------------------------------
    //
    // In this panel because the next question after "the scene pass is most of it" is "of what",
    // and the answer is one tick away. The same function draws them in Control, so the two surfaces
    // cannot come to offer different arms.
    ImGui::SeparatorText("Take a subsystem away");
    drawForensicArms();
    static_cast<void>(engine);
}

void ControlPanel::drawPerformance(app::Engine& engine, const FrameStats& stats) {
    helpHeader("performance/diagnosis");

    ImGui::Text("%.1f fps (%.1f ms)  cpu work %.2f ms  gpu %s", stats.fps, stats.frameIntervalMs, stats.cpuFrameMs,
                stats.gpuFrameMs >= 0.0 ? (std::to_string(stats.gpuFrameMs).substr(0, 5) + " ms").c_str() : "n/a");
    ImGui::Text("%ux%u  %u draws  %u tris  analysis %.0f us/hop (%llu frames)  modulation %.0f us", stats.width,
                stats.height, stats.drawCalls, stats.triangles, engine.stats().analysisHopMicros,
                static_cast<unsigned long long>(engine.stats().analysisFrames), engine.stats().modulationMicros);
    if (stats.procedural.objects > 0) {
        const auto& pr = stats.procedural;
        ImGui::Text("procedural: %u objects  %llu instances  %u src tris  %llu logical tris  %u deformers  %.1f KB instance buffers  cpu %.3f ms",
                    pr.objects, static_cast<unsigned long long>(pr.instances), pr.sourceTriangles,
                    static_cast<unsigned long long>(pr.logicalTriangles), pr.deformers,
                    static_cast<double>(pr.instanceBufferBytes) / 1024.0, pr.cpuUpdateMs);
        if (pr.effectorObjects > 0 || pr.fieldDeformers > 0 || pr.pointObjects > 0) {
            ImGui::Text("fields: %u effector objects  %llu records  %u effectors  pass %.3f ms  %u field deformers  %u point objects",
                        pr.effectorObjects, static_cast<unsigned long long>(pr.effectorInstances), pr.effectors,
                        pr.effectorPassMs, pr.fieldDeformers, pr.pointObjects);
        }
        // Culling / LOD (ADR-029). The counts come from an asynchronous readback, so they trail
        // the drawn frame by a frame or two; the pass time is the GPU timer's.
        if (pr.cullObjects > 0) {
            ImGui::Text("culling: %u objects  %llu visible  %llu culled  lod %llu/%llu/%llu/%llu  pass %.3f ms",
                        pr.cullObjects, static_cast<unsigned long long>(pr.visibleInstances),
                        static_cast<unsigned long long>(pr.culledInstances),
                        static_cast<unsigned long long>(pr.lodCounts[0]),
                        static_cast<unsigned long long>(pr.lodCounts[1]),
                        static_cast<unsigned long long>(pr.lodCounts[2]),
                        static_cast<unsigned long long>(pr.lodCounts[3]), pr.cullMs);
        }
    }
    if (stats.sdf.objects > 0) {
        ImGui::Text("sdf: %u objects (%u raymarched, %u meshed)  %u packed nodes  %u mesh tris  pass %.3f ms",
                    stats.sdf.objects, stats.sdf.raymarchObjects, stats.sdf.meshObjects, stats.sdf.packedNodes,
                    stats.sdf.meshTriangles, stats.sdf.raymarchMs);
    }
    if (stats.particles.systems > 0) {
        ImGui::Text("particles: %u systems  %u capacity  %u emitted  simulate %.3f ms", stats.particles.systems,
                    stats.particles.capacity, stats.particles.emittedThisFrame, stats.particles.simulateMs);
    }
    // ---- Renderer Forensics (the plan's Phase 4) ------------------------------------------------
    //
    // The isolation arms, where a person can reach them. These are the same switches `--disable`
    // drives headlessly, so a symptom cornered in the editor and a symptom cornered in a script are
    // cornered with the same instrument.
    //
    // Every control here removes a real subsystem and is proved to by
    // `[gpu][composition][forensics][isolation]`. Nothing is offered that does nothing: "disable
    // terrain" and "disable LOD" are absent because the renderer cannot honestly implement them
    // today, and an inert checkbox is worse than a missing one -- somebody switches it off, the
    // symptom stays, and a subsystem is wrongly cleared.
    if (ImGui::TreeNode("Renderer forensics")) {
        drawForensicArms();
        ImGui::TreePop();
    }
    if (renderer != nullptr && !world.debug.selectedEntity.empty()) {
        if (const auto* object = renderer->diagnosticObject(world.debug.selectedEntity); object != nullptr) {
            const auto& frame = renderer->diagnosticFrame();
            if (ImGui::TreeNode("Selected renderer diagnostic")) {
                ImGui::Text("frame %llu  entity %zu  slot %s", static_cast<unsigned long long>(frame.frameIndex),
                            object->entityIndex,
                            !object->submitted ? "unassigned" : std::to_string(object->objectSlot).c_str());
                ImGui::Text("world (%.4f, %.4f, %.4f)", static_cast<double>(object->worldPosition.x),
                            static_cast<double>(object->worldPosition.y), static_cast<double>(object->worldPosition.z));
                ImGui::Text("bounds min (%.3f, %.3f, %.3f)", static_cast<double>(object->worldBoundsMin.x),
                            static_cast<double>(object->worldBoundsMin.y), static_cast<double>(object->worldBoundsMin.z));
                ImGui::Text("bounds max (%.3f, %.3f, %.3f)", static_cast<double>(object->worldBoundsMax.x),
                            static_cast<double>(object->worldBoundsMax.y), static_cast<double>(object->worldBoundsMax.z));
                ImGui::Text("frustum margins L %.3f R %.3f B %.3f T %.3f N %.3f F %.3f",
                            static_cast<double>(object->frustumMargins[0]), static_cast<double>(object->frustumMargins[1]),
                            static_cast<double>(object->frustumMargins[2]), static_cast<double>(object->frustumMargins[3]),
                            static_cast<double>(object->frustumMargins[4]), static_cast<double>(object->frustumMargins[5]));
                ImGui::Text("camera (%.4f, %.4f, %.4f)", static_cast<double>(frame.cameraPosition.x),
                            static_cast<double>(frame.cameraPosition.y), static_cast<double>(frame.cameraPosition.z));
                ImGui::Text("reason %s", object->cullReason.c_str());
                if (object->rigIndex != std::numeric_limits<std::uint32_t>::max()) {
                    ImGui::Text("rig %u  joints %u  palette v%llu  time %.4f", object->rigIndex,
                                object->jointCount, static_cast<unsigned long long>(object->paletteVersion),
                                object->paletteTime);
                }
                ImGui::Text("visible %s  culled %s  submitted %s  finite %s", object->visible ? "yes" : "no",
                            object->cameraCulled ? "yes" : "no", object->submitted ? "yes" : "no",
                            object->finite ? "yes" : "no");
                for (int row = 0; row < 4; ++row) {
                    ImGui::Text("model %d  %.4f  %.4f  %.4f  %.4f", row,
                                static_cast<double>(object->worldMatrix[0][row]),
                                static_cast<double>(object->worldMatrix[1][row]),
                                static_cast<double>(object->worldMatrix[2][row]),
                                static_cast<double>(object->worldMatrix[3][row]));
                }
                ImGui::TreePop();
            }
        }
    }
    ImGui::Separator();
    // The one lever in the editor that moves the GPU's share of the frame, and the reason it is
    // here rather than buried: on a Retina display the canvas is several times the pixel count of
    // the sizes the renderer is benchmarked at, and nothing in the interface said so.
    ImGui::SliderFloat("Canvas scale", &canvasRenderScale, 0.25f, 1.0f, "%.2f");
    if (ImGui::IsItemHovered()) {
        tooltip("Render the world at a fraction of the canvas's pixels and show it "
                          "stretched. 1.00 is every pixel. Lower is softer and much faster; it "
                          "changes nothing about the picture itself, and nothing about a render.");
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%.2f Mpx", static_cast<double>(stats.width) * stats.height / 1.0e6);
    ImGui::TextDisabled("%s (%s)", stats.adapter.c_str(), stats.backend.c_str());
}


void ControlPanel::drawShadersTab(app::Engine& engine) {
    helpHeader("shaders/overview");

    if (ImGui::Button("Add background...") && onOpenShader) {
        onOpenShader();
    }
    ImGui::SameLine();
    if (ImGui::Button("Add post...") && onOpenPostShader) {
        onOpenPostShader();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%zu reload(s) this session", engine.shaderLayers().reloadsThisSession());
    ImGui::Separator();
    auto& layers = engine.shaderLayers();
    std::uint32_t removeId = 0;
    std::uint32_t moveId = 0;
    int moveDelta = 0;
    std::uint32_t reloadId = 0;
    for (const auto& layer : layers.layers()) {
        ImGui::PushID(static_cast<int>(layer->id));
        ImGui::Checkbox("##on", &layer->enabled);
        ImGui::SameLine();
        ImGui::Text("%s  [%s]  %zu inputs, %zu passes", layer->name.c_str(), shaders::layerStageName(layer->stage),
                    layer->parsed.description.inputs.size(), layer->parsed.description.passes.size());
        ImGui::SameLine();
        if (ImGui::SmallButton("^")) {
            moveId = layer->id;
            moveDelta = -1;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("v")) {
            moveId = layer->id;
            moveDelta = 1;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("reload")) {
            reloadId = layer->id;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) {
            removeId = layer->id;
        }
        ImGui::TextDisabled("%s", layer->path.string().c_str());
        std::string error = layer->parseError;
        if (error.empty() && shaderErrorFor) {
            error = shaderErrorFor(layer->id);
        }
        if (!error.empty()) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", error.c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::PopID();
    }
    if (removeId != 0) {
        engine.removeShaderLayer(removeId);
    }
    if (moveId != 0) {
        layers.move(moveId, moveDelta);
    }
    if (reloadId != 0) {
        (void)layers.reload(reloadId);
    }
    if (layers.size() == 0) {
        ImGui::TextDisabled("no user shaders; drop a .wgsl file on the window or use Add");
    }
    ImGui::TextDisabled("inputs appear in the Parameters window under 'shader'");
}


void ControlPanel::drawSceneTab(app::Engine& engine) {
    auto* comp = engine.composition();
    if (comp == nullptr) {
        ImGui::TextDisabled("current scene: %s (not a composition)", engine.controller().name().c_str());
        if (ImGui::Button("New composition")) {
            engine.newComposition();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("or add a node to convert");
    } else {
        ImGui::Text("composition '%s': %zu nodes, radius %.2f", comp->name().c_str(), comp->nodeCount(),
                    static_cast<double>(comp->boundsRadius()));
        if (!engine.compositionPath().empty()) {
            ImGui::TextDisabled("%s", engine.compositionPath().string().c_str());
        }
    }
    ImGui::Separator();
    static const char* kinds[] = {"gltf", "orb", "grid", "particles", "scene", "procedural", "field", "spline", "sdf"};
    ImGui::SetNextItemWidth(110);
    // IM_ARRAYSIZE, not a literal: the count said 6 against a nine-entry list, so `field`,
    // `spline` and `sdf` could not be added from this menu at all -- three node kinds the rest
    // of the editor supports fully, unreachable because a number did not move when the list grew.
    ImGui::Combo("##nodekind", &newNodeKind_, kinds, IM_ARRAYSIZE(kinds));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140);
    ImGui::InputText("##nodename", nodeName_, sizeof(nodeName_));
    ImGui::SameLine();
    const auto kind = static_cast<scene::NodeKind>(newNodeKind_);
    if (kind == scene::NodeKind::Gltf) {
        if (ImGui::Button("Add glTF node...") && onAddGltfNode) {
            onAddGltfNode();
        }
    } else if (kind == scene::NodeKind::Scene) {
        if (ImGui::Button("Add scene node...") && onAddSceneNode) {
            onAddSceneNode();
        }
    } else if (ImGui::Button("Add node")) {
        scene::CompositionNode node;
        node.name = nodeName_[0] ? nodeName_ : "node";
        node.kind = kind;
        if (auto r = engine.addNode(std::move(node)); !r) {
            status_ = r.error().message;
        }
    }
    if (comp == nullptr) {
        return;
    }
    ImGui::Separator();
    std::string removeName;
    for (const auto& node : comp->nodes()) {
        ImGui::PushID(node->name.c_str());
        ImGui::Text("%s  [%s]%s", node->name.c_str(), scene::nodeKindName(node->kind),
                    node->asset.empty() ? "" : (std::string("  ") + node->asset.filename().string()).c_str());
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 24);
        if (ImGui::SmallButton("x")) {
            removeName = node->name;
        }
        ImGui::PopID();
    }
    if (!removeName.empty()) {
        engine.removeNode(removeName);
    }
    ImGui::TextDisabled("node transforms and overrides: Parameters window, group 'nodes'");
}


void ControlPanel::drawTimelineTab(app::Engine& engine) {
    using namespace params;
    auto& timeline = engine.timeline();
    const auto& clock = engine.timelineClock();
    ImGui::Checkbox("enabled", &timeline.enabled);
    ImGui::SameLine();
    ImGui::Text("t = %.2f s, beat %.2f", clock.seconds, clock.beats);
    if (const auto cue = engine.cueState(); cue.index >= 0 &&
                                            static_cast<std::size_t>(cue.index) < timeline.cues().size()) {
        ImGui::SameLine();
        ImGui::TextDisabled("cue '%s' %.0f%%", timeline.cues()[static_cast<std::size_t>(cue.index)].name.c_str(),
                            static_cast<double>(cue.progress) * 100.0);
    }
    ImGui::Separator();

    // ---- add a key for a parameter at the current time ----
    static const char* interps[] = {"step", "linear", "smooth", "easeIn", "easeOut", "easeInOut", "bezier"};
    static const char* bases[] = {"seconds", "beats"};
    std::vector<const char*> targets;
    for (const auto* p : engine.params().ordered()) {
        if (p->flags().modulatable) {
            targets.push_back(p->path().c_str());
        }
    }
    keyTarget_ = std::clamp(keyTarget_, 0, std::max(0, static_cast<int>(targets.size()) - 1));
    ImGui::SetNextItemWidth(220);
    ImGui::Combo("##keytarget", &keyTarget_, targets.data(), static_cast<int>(targets.size()));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    ImGui::Combo("##keyinterp", &keyInterp_, interps, 7);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    ImGui::Combo("##keybase", &keyBase_, bases, 2);
    ImGui::SameLine();
    if (ImGui::Button("Add key") && !targets.empty()) {
        engine.recordKey(targets[static_cast<std::size_t>(keyTarget_)], -1, static_cast<KeyInterp>(keyInterp_),
                         static_cast<TimeBase>(keyBase_));
    }

    // ---- tracks ----
    int removeTrack = -1;
    auto& tracks = timeline.tracks();
    for (std::size_t i = 0; i < tracks.size(); ++i) {
        auto& track = tracks[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::Checkbox("##on", &track.enabled);
        ImGui::SameLine();
        const bool open = ImGui::TreeNodeEx("track", ImGuiTreeNodeFlags_None, "%s%s  (%zu keys, %s%s)",
                                            track.target.c_str(), track.param == nullptr ? " [unbound]" : "",
                                            track.keys.size(), timeBaseName(track.timeBase),
                                            track.loopLength > 0.0 ? ", loop" : "");
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 24);
        if (ImGui::SmallButton("x")) {
            removeTrack = static_cast<int>(i);
        }
        if (open) {
            static const char* modes[] = {"replace", "add", "multiply"};
            int mode = static_cast<int>(track.mode);
            ImGui::SetNextItemWidth(90);
            if (ImGui::Combo("mode", &mode, modes, 3)) {
                track.mode = static_cast<TrackMode>(mode);
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(90);
            auto loop = static_cast<float>(track.loopLength);
            if (ImGui::DragFloat("loop", &loop, 0.1f, 0.0f, 3600.0f, "%.2f")) {
                track.loopLength = static_cast<double>(std::max(0.0f, loop));
            }
            // Curve preview over the key span (component 0).
            if (!track.keys.empty() && ImPlot::BeginPlot("##curve", ImVec2(-1, 90), ImPlotFlags_NoLegend | ImPlotFlags_NoMenus)) {
                const double t0 = track.firstKeyTime();
                const double t1 = std::max(track.lastKeyTime(), t0 + 1e-3);
                const double span = track.loopLength > 0.0 ? std::max(track.loopLength, t1 - t0) : (t1 - t0);
                constexpr int kSamples = 128;
                plotX_.resize(kSamples);
                std::vector<float> ys(kSamples);
                for (int k = 0; k < kSamples; ++k) {
                    const double t = t0 + span * k / (kSamples - 1);
                    plotX_[static_cast<std::size_t>(k)] = static_cast<float>(t);
                    ys[static_cast<std::size_t>(k)] = track.evaluate(t)[0];
                }
                ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoDecorations, ImPlotAxisFlags_AutoFit);
                ImPlot::SetupAxisLimits(ImAxis_X1, t0, t0 + span, ImPlotCond_Always);
                ImPlot::PlotLine("value", plotX_.data(), ys.data(), kSamples);
                const double now = track.localTime(clock.at(track.timeBase));
                const double nowX[1] = {now};
                ImPlot::PlotInfLines("now", nowX, 1);
                // Curve editor: every key (component 0) is a draggable point; time re-sorts on
                // release so the curve stays a function of time while dragging.
                bool dragging = false;
                for (std::size_t k = 0; k < track.keys.size(); ++k) {
                    auto& key = track.keys[k];
                    double kx = key.time;
                    double ky = static_cast<double>(key.value[0]);
                    bool held = false;
                    if (ImPlot::DragPoint(static_cast<int>(k), &kx, &ky, ImVec4(1.0f, 0.75f, 0.3f, 1.0f), 6.0f, ImPlotDragToolFlags_None, nullptr, nullptr, &held)) {
                        key.time = std::max(0.0, kx);
                        key.value[0] = static_cast<float>(ky);
                    }
                    dragging = dragging || held;
                }
                if (!dragging && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    track.sortKeys();
                }
                ImPlot::EndPlot();
            }
            int removeKey = -1;
            bool resort = false;
            const std::size_t comps = track.keyedComponents();
            for (std::size_t k = 0; k < track.keys.size(); ++k) {
                auto& key = track.keys[k];
                ImGui::PushID(static_cast<int>(k));
                ImGui::SetNextItemWidth(70);
                auto t = static_cast<float>(key.time);
                if (ImGui::DragFloat("##t", &t, 0.01f, 0.0f, 0.0f, "%.2f")) {
                    key.time = static_cast<double>(std::max(0.0f, t));
                    resort = true;
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(160);
                ImGui::DragScalarN("##v", ImGuiDataType_Float, key.value.data(), static_cast<int>(std::min<std::size_t>(comps, 4)), 0.01f);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(90);
                int interp = static_cast<int>(key.interp);
                if (ImGui::Combo("##i", &interp, interps, 7)) {
                    key.interp = static_cast<KeyInterp>(interp);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("x")) {
                    removeKey = static_cast<int>(k);
                }
                ImGui::PopID();
            }
            if (removeKey >= 0) {
                track.keys.erase(track.keys.begin() + removeKey);
            }
            if (resort && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                track.sortKeys();
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if (removeTrack >= 0) {
        timeline.removeTrack(static_cast<std::size_t>(removeTrack));
    }

    // ---- cues ----
    ImGui::Separator();
    ImGui::TextUnformatted("Cues");
    std::vector<const char*> presetNames;
    presetNames.push_back("(marker)");
    for (const auto& preset : engine.presets().presets()) {
        presetNames.push_back(preset.name.c_str());
    }
    cuePreset_ = std::clamp(cuePreset_, 0, static_cast<int>(presetNames.size()) - 1);
    ImGui::SetNextItemWidth(100);
    ImGui::InputText("##cuename", cueName_, sizeof(cueName_));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    ImGui::Combo("##cuepreset", &cuePreset_, presetNames.data(), static_cast<int>(presetNames.size()));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70);
    ImGui::DragFloat("morph s", &cueMorph_, 0.05f, 0.0f, 60.0f, "%.2f");
    ImGui::SameLine();
    if (ImGui::Button("Add cue at t")) {
        Cue cue;
        cue.time = clock.seconds;
        cue.name = cueName_[0] ? cueName_ : "cue";
        cue.preset = cuePreset_ > 0 ? presetNames[static_cast<std::size_t>(cuePreset_)] : "";
        cue.morphSeconds = static_cast<double>(cueMorph_);
        timeline.addCue(std::move(cue));
    }
    int removeCue = -1;
    auto& cues = timeline.cues();
    for (std::size_t i = 0; i < cues.size(); ++i) {
        auto& cue = cues[i];
        ImGui::PushID(static_cast<int>(i) + 10000);
        ImGui::SetNextItemWidth(70);
        auto t = static_cast<float>(cue.time);
        if (ImGui::DragFloat("##ct", &t, 0.01f, 0.0f, 0.0f, "%.2f")) {
            cue.time = static_cast<double>(std::max(0.0f, t));
        }
        ImGui::SameLine();
        ImGui::Text("%s -> %s (%.2f s morph, %s)", cue.name.c_str(), cue.preset.empty() ? "marker" : cue.preset.c_str(),
                    cue.morphSeconds, timeBaseName(cue.timeBase));
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) {
            removeCue = static_cast<int>(i);
        }
        ImGui::PopID();
    }
    if (removeCue >= 0) {
        timeline.removeCue(static_cast<std::size_t>(removeCue));
    } else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        timeline.sortCues();
    }
}


// The path-tracing half of the Render panel. A view of `pathtrace::TraceJob` and nothing more:
// every number shown comes from `pathTraceProgress()`, and the panel keeps no copy of it.
void ControlPanel::drawPathTrace() {
    if (pathTraceSettings == nullptr) return;
    auto& t = *pathTraceSettings;

    const pathtrace::TraceProgress p =
        pathTraceProgress ? pathTraceProgress() : pathtrace::TraceProgress{};
    // ADR-383: a sequence reports frames AND samples-within-the-frame, because a path-traced frame
    // takes long enough that a frame counter on its own reads as a hang.
    const std::optional<app::SequenceProgress> seq =
        pathTraceSequenceProgress ? pathTraceSequenceProgress() : std::nullopt;
    const bool seqRunning = seq.has_value() && !seq->finished;
    const bool running =
        seqRunning || (!p.finished() && p.state != pathtrace::TraceJobState::Queued);

    ImGui::BeginDisabled(running);

    // Resolution comes from the same control the realtime half uses, so the two cannot disagree
    // about what "the output size" means.
    // Resolution comes from the render settings and is not copied into the trace's own struct:
    // the two renderers share one output size and a second copy of it is a second thing to keep in
    // step. `traceSettingsFrom` reads it at the point of use.
    if (renderSettings != nullptr) {
        static_cast<void>(drawOutputResolutionControls(*renderSettings));
    }

    {
        auto seconds = static_cast<float>(t.seconds);
        if (ImGui::InputFloat("second", &seconds, 0.1f, 1.0f, "%.3f")) {
            t.seconds = std::max(0.0, static_cast<double>(seconds));
        }
        if (ImGui::IsItemHovered()) {
            tooltip("The timeline second to trace, or the first second of a range.");
        }

        // ADR-383. A path-traced SEQUENCE. The tooltip above used to end "one frame, not a
        // sequence -- a path-traced sequence is a queue of these and is not built yet", and this
        // is the thing that makes that sentence false.
        bool sequence = t.isSequence();
        if (ImGui::Checkbox("range", &sequence)) {
            // Turning it on proposes a second of footage rather than an empty range, so the
            // control does something the moment it is ticked; turning it off returns to the one
            // frame every existing project describes.
            t.endSeconds = sequence ? t.seconds + 1.0 : -1.0;
        }
        if (ImGui::IsItemHovered()) {
            tooltip("Trace a range of frames instead of one. The end is exclusive, exactly as a\n"
                    "realtime render's is, so 12.0 to 13.0 at 24 fps is twenty-four frames\n"
                    "starting at 12.0 and the last one is at 12.958.");
        }
        if (sequence) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(90.0f);
            auto end = static_cast<float>(t.endSeconds);
            if (ImGui::InputFloat("to", &end, 0.1f, 1.0f, "%.3f")) {
                t.endSeconds = static_cast<double>(end);
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(70.0f);
            auto fps = static_cast<float>(t.fps);
            if (ImGui::InputFloat("fps", &fps, 1.0f, 10.0f, "%.3g")) {
                t.fps = std::max(0.001, static_cast<double>(fps));
            }
            const app::FrameRange range = t.frameRange();
            const std::uint64_t frames = range.frameCount(range.resolvedEnd(0.0, 0.0));
            ImGui::TextDisabled("%llu frame(s); a movie if the output ends .mov or .mp4",
                                static_cast<unsigned long long>(frames));
            if (ImGui::IsItemHovered()) {
                tooltip("A path-traced movie is tone mapped on the CPU, so it needs no GPU at all\n"
                        "-- which is the whole reason to use this renderer while the device is\n"
                        "busy. Anything else writes one scene-linear EXR per frame into a folder\n"
                        "of that name.");
            }
        }
    }

    int samples = static_cast<int>(t.samplesPerPixel);
    if (ImGui::SliderInt("samples", &samples, 1, 1024, "%d spp", ImGuiSliderFlags_Logarithmic)) {
        t.samplesPerPixel = static_cast<std::uint32_t>(std::max(1, samples));
    }
    if (ImGui::IsItemHovered()) {
        tooltip("Paths per pixel. Noise falls as the square root of this, so four times the\n"
                "samples is half the noise and four times the wait.");
    }

    int depth = static_cast<int>(t.maxDepth);
    if (ImGui::SliderInt("bounces", &depth, 0, 16)) {
        t.maxDepth = static_cast<std::uint32_t>(std::max(0, depth));
    }
    if (ImGui::IsItemHovered()) {
        tooltip("Surface interactions per path. 0 is direct lighting only; each further bounce\n"
                "adds indirect light and costs time. Russian roulette ends long paths early.");
    }

    // ADR-366: the trace's own destination. It used to read the raster job's, which this function
    // returns before ever drawing -- so a trace's output path was unreachable while a trace was
    // selected, and an unset one went to $TMPDIR without saying so.
    {
        std::string out = t.outputPath.generic_string();
        out.resize(512, '\0');
        ImGui::SetNextItemWidth(-72.0f);
        if (ImGui::InputText("##pt-out", out.data(), out.size())) {
            t.outputPath = std::filesystem::path(out.c_str());
        }
        if (ImGui::IsItemHovered()) {
            tooltip("Where the scene-linear EXR goes, relative to the project. The extension is\n"
                    "forced to .exr: the tracer writes nothing else, and a float image in a file\n"
                    "called .png is worse than a refusal.");
        }
        ImGui::SameLine();
        if (ImGui::Button("Choose...") && onChoosePathTraceOutput) {
            onChoosePathTraceOutput();
        }
        ImGui::SameLine();
        ImGui::TextUnformatted("output");
    }

    {
        ImGui::BeginDisabled(!pathTraceDenoiseAvailable);
        ImGui::Checkbox("denoise", &t.denoise);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered()) {
            tooltip(pathTraceDenoiseAvailable
                        ? "Open Image Denoise, using the albedo and normal passes as guides.\n"
                          "Turns a low-sample render into a usable one; it cannot invent detail\n"
                          "that no path ever found."
                        : "This build has no denoiser. Configure with\n"
                          "-DAVGEN_PATHTRACE_DENOISE=ON to fetch it (ADR-353).");
        }
        ImGui::SameLine();
        ImGui::Checkbox("AOVs", &t.writeAovs);
        if (ImGui::IsItemHovered()) {
            tooltip("Write albedo, normal, emission, depth and id as named layers in the same\n"
                    "EXR. Normals go in normal.X/Y/Z, never R/G/B, so a colour-managed\n"
                    "pipeline downstream does not transform them as if they were colour.");
        }
    }

    ImGui::Checkbox("albedo probe", &t.albedoProbe);
    if (ImGui::IsItemHovered()) {
        tooltip("Report surfaces whose directional albedo exceeds 1 (ADR-352). The glTF BRDF is\n"
                "kept faithful to the specification and gains energy at grazing angles; this\n"
                "measures where. Diagnostic only -- it cannot change a pixel.");
    }

    ImGui::EndDisabled();
    if (const auto ok = t.validate(); !ok) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "%s", ok.error().message.c_str());
    }
    ImGui::Separator();

    if (seqRunning) {
        const auto samples = pathTraceSequenceSamples ? pathTraceSequenceSamples()
                                                      : std::pair<std::uint32_t, std::uint32_t>{0, 0};
        const std::string overlay = fmt::format("frame {} / {}", seq->framesSubmitted, seq->framesTotal);
        ImGui::ProgressBar(static_cast<float>(std::clamp(seq->fraction(), 0.0, 1.0)), ImVec2(-1, 0),
                           overlay.c_str());
        if (samples.second > 0) {
            ImGui::Text("%u / %u samples on this frame -- %s elapsed", samples.first, samples.second,
                        elapsedClock(seq->elapsedSeconds).c_str());
        } else {
            ImGui::Text("%s elapsed", elapsedClock(seq->elapsedSeconds).c_str());
        }
        if (seq->estimatedRemainingSeconds >= 0.0) {
            ImGui::SameLine();
            ImGui::TextDisabled("(about %s left)",
                                elapsedClock(seq->estimatedRemainingSeconds).c_str());
        } else if (ImGui::IsItemHovered()) {
            tooltip("No estimate yet. Eight frames is the least this will guess from -- a rate\n"
                    "taken over the first frame is not a rate, and a confident wrong number at\n"
                    "the only moment somebody looks at it teaches them to ignore the number.");
        }
        drawViewportSuspension();
        if (ImGui::Button("Cancel") && onCancelPathTrace) {
            onCancelPathTrace();
        }
        if (ImGui::IsItemHovered()) {
            tooltip("Stops after the frame being traced. Every frame already written is kept --\n"
                    "a cancelled sequence is a short sequence, not a deleted one.");
        }
    } else if (running) {
        // PROGRESS HONESTY (spec section 36). A bar is drawn only for a stage that can actually
        // measure itself. Rendering counts finished samples, so it gets one. Scene build, BVH,
        // denoise and write emit no intermediate signal, so they get the STAGE NAME instead -- a
        // bar sitting at 40% while nothing is known is a lie, and the usual place that discipline
        // dies is exactly here, because a still bar looks broken and a creeping one looks fine.
        if (p.fractionKnown) {
            const std::string overlay =
                fmt::format("{} / {} samples", p.samplesDone, p.samplesTotal);
            ImGui::ProgressBar(std::clamp(p.fraction, 0.0f, 1.0f), ImVec2(-1, 0), overlay.c_str());
            ImGui::Text("%s -- %s elapsed", p.stage.c_str(), elapsedClock(p.elapsedSeconds).c_str());
        } else {
            ImGui::TextColored(ImVec4(0.72f, 0.80f, 0.95f, 1.0f), "%s...", p.stage.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("(%s elapsed)", elapsedClock(p.elapsedSeconds).c_str());
            if (ImGui::IsItemHovered()) {
                tooltip("This stage reports no intermediate progress, so none is shown. A bar here\n"
                        "would be an interpolation rather than a measurement.");
            }
        }
        drawViewportSuspension();
        if (ImGui::Button("Cancel") && onCancelPathTrace) {
            onCancelPathTrace();
        }
        if (ImGui::IsItemHovered()) {
            tooltip("Stops after the current sample batch. A cancelled trace writes no file.");
        }
    } else {
        if (ImGui::Button("Path trace") && onStartPathTrace) {
            onStartPathTrace();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("writes one EXR");
        if (seq.has_value() && seq->finished) {
            if (!seq->error.empty()) {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "failed: %s", seq->error.c_str());
            } else if (seq->cancelled) {
                ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.4f, 1.0f), "cancelled -- %llu frame(s) kept",
                                   static_cast<unsigned long long>(seq->framesWritten));
            } else {
                ImGui::TextColored(ImVec4(0.55f, 0.85f, 0.55f, 1.0f),
                                   "done: %llu frame(s) in %s, hash %016llx",
                                   static_cast<unsigned long long>(seq->framesWritten),
                                   elapsedClock(seq->elapsedSeconds).c_str(),
                                   static_cast<unsigned long long>(seq->sequenceHash));
            }
        } else if (p.state == pathtrace::TraceJobState::Complete) {
            ImGui::TextColored(ImVec4(0.55f, 0.85f, 0.55f, 1.0f), "done in %s",
                               elapsedClock(p.elapsedSeconds).c_str());
        } else if (p.state == pathtrace::TraceJobState::Cancelled) {
            ImGui::TextDisabled("cancelled -- no file written");
        } else if (p.state == pathtrace::TraceJobState::Failed) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "failed: %s",
                               p.error.empty() ? "unknown" : p.error.c_str());
        }
    }
}

// ADR-364. Drawn under both renderers' progress lines, because a trace suspends the viewport too.
void ControlPanel::drawViewportSuspension() {
    if (viewportSuspended) {
        ImGui::TextColored(ImVec4(0.72f, 0.80f, 0.95f, 1.0f),
                           "viewport suspended -- %llu frames not drawn",
                           static_cast<unsigned long long>(viewportFramesSuspended));
        if (ImGui::IsItemHovered()) {
            tooltip("The world is not being drawn into the canvas while this runs, so the render\n"
                    "gets the machine. What you are looking at is the last frame before it\n"
                    "started, not a live view.\n\n"
                    "The count is here so you can tell the difference between a suspension that\n"
                    "is working and one that is only switched on. Turn it off in Settings if you\n"
                    "would rather keep working while a render goes.");
        }
    }
}

void ControlPanel::drawRender(app::Engine& engine) {
    helpHeader("rendering/offline-render");

    // ---- which renderer (spec section 67) --------------------------------------------------------
    //
    // Two renderers over one scene, so one panel with a choice at the top rather than two panels
    // that would each need their own resolution, output path and progress line. "Path trace" rather
    // than "offline": this codebase already spends that word on a quality tier of the rasteriser
    // and on the batch pipeline, and a second meaning in the UI would be the same collision
    // ADR-351 exists to avoid.
    if (pathTraceSettings != nullptr) {
        ImGui::TextUnformatted("Renderer");
        ImGui::SameLine();
        ImGui::RadioButton("Realtime", &rendererChoice_, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Path trace", &rendererChoice_, 1);
        if (ImGui::IsItemHovered()) {
            tooltip(
                "A CPU path tracer (ADR-351). Slower by a long way and not constrained by what a\n"
                "frame budget allows: true soft shadows, real reflections and bounced light.\n\n"
                "Writes one scene-linear EXR, never a tonemapped image -- the colour pipeline is\n"
                "downstream of the file. Uses no GPU at all, so it runs while the viewport does.");
        }
        ImGui::Separator();
        if (rendererChoice_ == 1) {
            drawPathTrace();
            return;
        }
    }

    if (renderSettings == nullptr) {
        ImGui::TextDisabled("render settings unavailable");
        return;
    }
    auto& s = *renderSettings;
    const app::RenderProgress current = renderProgress ? renderProgress() : app::RenderProgress{};
    const bool running = current.framesTotal > 0 && !current.finished;
    ImGui::BeginDisabled(running);
    // ADR-246: the same control the preview toolbar offers, drawn from the same function. Before
    // this it was a bare `InputInt2` that clamped silently to [2, 16384] -- so typing 1 gave you 2
    // and typing 20000 gave you 16384, with nothing said either way, which is exactly the "silently
    // alter the aspect ratio or clamp dimensions without informing the user" the spec's §5.3
    // refuses. It now rejects and explains, and keeps the last valid size.
    static_cast<void>(drawOutputResolutionControls(s));
    auto fps = static_cast<float>(s.fps);
    if (ImGui::InputFloat("fps", &fps, 1.0f, 10.0f, "%.3f")) {
        s.fps = static_cast<double>(std::clamp(fps, 1.0f, 240.0f));
    }
    // ADR-147: the tier the deliverable is rendered at. This existed as a field and as a command
    // line flag and had no control, which is how a batch render spent a long time coming out
    // byte-identical to an interactive Realtime frame without anybody being told. Offline is the
    // default and is what a deliverable should almost always be; the others are here because a
    // preview render of a long sequence is a real thing to want.
    {
        static constexpr const char* kTierNames[] = {"preview", "realtime", "high", "offline"};
        int tier = 3;
        for (int i = 0; i < 4; ++i) {
            if (s.tier == kTierNames[i]) {
                tier = i;
            }
        }
        // "tier", not "quality": the video section a few lines below already has a `quality`
        // slider for the encoder, and two visible items with the same label are the same ImGui ID.
        // They also mean different things -- this is how much work the renderer does, that is how
        // many bits the encoder spends -- so sharing a name would have been wrong even if ImGui
        // had allowed it.
        if (ImGui::Combo("tier", &tier, kTierNames, 4)) {
            s.tier = kTierNames[tier];
        }
        if (liftViewportLimits != nullptr) {
            ImGui::Checkbox("viewport matches the render", liftViewportLimits);
            if (ImGui::IsItemHovered()) {
                tooltip(
                    "Lift the distance limits in the VIEWPORT, so it shows what a render will.\n\n"
                    "A render already lifts them (ADR-186): distant characters keep simulating and\n"
                    "stay posed. Live playback does not, so past the scene's thresholds a far body\n"
                    "is simulated in steps and posed at a lower rate -- which reads as gliding and\n"
                    "as stepping between positions. That is the editor, not the deliverable.\n\n"
                    "Costs real time on a wide shot, which is why live playback does not do it by\n"
                    "default. Turn it on to judge a wide shot; turn it off to work at speed.");
            }
        }
        if (ImGui::IsItemHovered()) {
            tooltip(
                "The quality tier this render is made at -- not the viewport's.\n\n"
                "offline  every shadow tap, AO and the shadow mask at full resolution,\n"
                "         volumetrics per pixel, no LOD or material demotion, no\n"
                "         temporal shortcut. Slower, and what a deliverable wants.\n"
                "high     the reference live picture.\n"
                "realtime what the viewport draws.\n"
                "preview  fastest; for checking timing on a long sequence.");
        }
        if (s.tier != "offline") {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.90f, 0.72f, 0.25f, 1.0f), "(not final quality)");
        }
        // ADR-186: the distance reductions the live path applies. Next to the tier because it is
        // the same kind of decision -- how much work a frame is allowed to skip -- and because the
        // default answer *is* the tier's.
        static const char* kLimitNames[] = {"tier", "live", "unlimited"};
        int limits = s.limits == "live" ? 1 : (s.limits == "unlimited" ? 2 : 0);
        ImGui::SetNextItemWidth(140);
        if (ImGui::Combo("draw distance", &limits, kLimitNames, 3)) {
            s.limits = kLimitNames[limits];
        }
        if (ImGui::IsItemHovered()) {
            tooltip(
                "Distance limits the live path applies so a frame fits in a frame.\n\n"
                "tier      the tier decides: offline lifts them, anything else keeps them.\n"
                "live      keep them -- a fast proof that matches the viewport.\n"
                "unlimited lift them at any tier.\n\n"
                "Lifted, nothing is dropped for being far away: scatter draws its real\n"
                "mesh instead of a billboard however distant, every character is posed\n"
                "every frame, and nothing past its cull radius stands frozen. Only\n"
                "frustum culling stays -- what is off screen is still off screen.\n\n"
                "Slower, sometimes much slower on a world with heavy scatter.");
        }
        const scene::DetailLimits resolved = s.resolvedLimits();
        if (resolved.anyLifted()) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.45f, 0.80f, 0.55f, 1.0f), "(no distance cull)");
        }
    }
    float range[2] = {static_cast<float>(s.startSeconds), static_cast<float>(s.endSeconds)};
    if (ImGui::InputFloat2("range (s, end<0 = auto)", range, "%.2f")) {
        s.startSeconds = static_cast<double>(std::max(0.0f, range[0]));
        s.endSeconds = static_cast<double>(range[1]);
    }
    const double end = s.resolvedEnd(engine.durationSeconds(), engine.timeline().durationSeconds());
    ImGui::TextDisabled("%llu frames (%.2f s .. %.2f s)", static_cast<unsigned long long>(s.frameCount(end)),
                        s.startSeconds, end);
    int output = s.output == app::RenderOutput::Video ? 1 : s.output == app::RenderOutput::ExrSequence ? 2 : 0;
    if (ImGui::RadioButton("PNG sequence", &output, 0)) {
        s.output = app::RenderOutput::PngSequence;
        s.normalisePattern();
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("EXR sequence", &output, 2)) {
        s.output = app::RenderOutput::ExrSequence;
        s.normalisePattern();
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Video", &output, 1)) {
        s.output = app::RenderOutput::Video;
    }
    char pathBuf[512];
    std::snprintf(pathBuf, sizeof(pathBuf), "%s", s.outputPath.string().c_str());
    ImGui::SetNextItemWidth(-90);
    if (ImGui::InputText("##out", pathBuf, sizeof(pathBuf))) {
        s.outputPath = pathBuf;
    }
    ImGui::SameLine();
    if (ImGui::Button("Choose...") && onChooseRenderOutput) {
        onChooseRenderOutput();
    }
    if (s.output == app::RenderOutput::Video) {
        // ADR-383 / audit G4: only what this machine can actually produce. This was a hardcoded
        // array of eight, four of them ffmpeg-only, offered whether or not an ffmpeg exists
        // anywhere -- so picking one was a render that failed when the output was opened, after
        // the project had been saved. The decision is `ui::availableCodecs`, which a test can ask
        // every combination of without an encoder.
        const std::vector<std::string> codecs =
            ui::availableCodecs(assets::nativeCodecs(), !assets::findFfmpeg().empty(), s.backend);
        if (codecs.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f),
                               "no video encoder: this build has no native backend and no ffmpeg "
                               "was found");
        } else {
            std::vector<const char*> items;
            items.reserve(codecs.size());
            int codec = 0;
            for (std::size_t i = 0; i < codecs.size(); ++i) {
                items.push_back(codecs[i].c_str());
                if (s.codec == codecs[i]) {
                    codec = static_cast<int>(i);
                }
            }
            // A setting that names a codec this machine cannot produce is shown as selected anyway
            // -- it came from the project and quietly rewriting somebody's authored choice because
            // their laptop lacks an encoder is worse than saying so.
            const bool unavailable =
                std::find(codecs.begin(), codecs.end(), s.codec) == codecs.end();
            ImGui::SetNextItemWidth(140);
            if (ImGui::Combo("codec", &codec, items.data(), static_cast<int>(items.size()))) {
                s.codec = codecs[static_cast<std::size_t>(codec)];
            }
            if (unavailable) {
                ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.35f, 1.0f),
                                   "'%s' is not available here; the render will fail unless you "
                                   "change it or install ffmpeg",
                                   s.codec.c_str());
            }
        }
        ImGui::SameLine();
        static const char* backends[] = {"auto", "native", "ffmpeg"};
        int backend = s.backend == "native" ? 1 : (s.backend == "ffmpeg" ? 2 : 0);
        ImGui::SetNextItemWidth(90);
        if (ImGui::Combo("backend", &backend, backends, 3)) {
            s.backend = backends[backend];
        }
        ImGui::SliderInt("encoder quality", &s.quality, 0, 100);
        if (ImGui::IsItemHovered()) {
            tooltip("How many bits the video encoder spends. Unrelated to the render tier\n"
                              "above, which is how much work the renderer does per frame.");
        }
        ImGui::Checkbox("mux audio", &s.muxAudio);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90.0f);
        // ADR-383 / audit G3. `encoderThreads` round-tripped through the project and had no widget
        // anywhere: CLI only, on a setting whose whole purpose is "this machine is busy, spend
        // fewer cores on the encoder".
        ImGui::InputInt("encoders", &s.encoderThreads, 1, 2);
        s.encoderThreads = std::clamp(s.encoderThreads, 0, 16);
        if (ImGui::IsItemHovered()) {
            tooltip("Threads compressing frames. 0 lets the job choose -- one per core less one,\n"
                    "capped at sixteen, and always 1 for a video because the muxer is sequential.\n"
                    "Lower it when you want the machine back while a render goes.");
        }
        if (!videoBackends.empty()) {
            ImGui::TextWrapped("%s", videoBackends.c_str());
        }
    } else {
        char pat[128];
        std::snprintf(pat, sizeof(pat), "%s", s.pattern.c_str());
        ImGui::SetNextItemWidth(200);
        if (ImGui::InputText("pattern", pat, sizeof(pat))) {
            s.pattern = pat;
        }
    }

    // ---- properties of the deliverable that were CLI-only (audit G3, ADR-383) --------------------
    {
        ImGui::SetNextItemWidth(120.0f);
        float supersample = s.supersample;
        if (ImGui::SliderFloat("supersample", &supersample, 1.0f, 2.0f, "%.2fx")) {
            s.supersample = supersample;
        }
        if (ImGui::IsItemHovered()) {
            // `tooltipUnformatted`, not `tooltip`: this text contains two literal per-cent signs,
            // and `tooltip` is printf-style (`IM_FMTARGS(1)`). As `tooltip` it read two doubles off
            // an empty varargs list every frame the pointer rested here. Found by a warning census
            // rather than by anybody looking at the tooltip, which is the point ADR-393 makes about
            // `-Werror=format`: one instance in the tree, and it was a live bug.
            tooltipUnformatted("Render at this multiple of the output size and resolve back down (ADR-212).\n"
                               "It is the documented answer to foliage shimmer: neighbour-to-neighbour chroma\n"
                               "noise measures 2.80% at 1280x720 against 1.86% at 2560x1440, and a 720p\n"
                               "deliverable had no other way to buy its way out of it.\n\n"
                               "1.00x is off. Costs the square of what it says.");
        }

        // The AOV set, as checkboxes over `RenderSettings::aovNames()` rather than a text field, so
        // a typo cannot silently export nothing -- which is the failure `aovList()` exists to
        // refuse and which a free-text box would keep re-creating.
        if (ImGui::TreeNode("auxiliary passes (AOVs)")) {
            // Read through `aovList()`, which parses the comma list properly, rather than by
            // substring: `s.aovs.find("id")` is true for a list containing nothing but a name that
            // happens to contain those two letters, and a checkbox that lies about its own state is
            // worse than no checkbox.
            const auto parsed = s.aovList();
            const std::vector<std::string> selected = parsed ? *parsed : std::vector<std::string>{};
            const auto isOn = [&selected](const std::string& n) {
                return std::find(selected.begin(), selected.end(), n) != selected.end();
            };
            for (const std::string_view name : app::RenderSettings::aovNames()) {
                const std::string n(name);
                bool on = isOn(n);
                if (ImGui::Checkbox(n.c_str(), &on)) {
                    // Rebuilt from the boxes in `aovNames()`'s order, so the list is canonical and
                    // two identical selections cannot produce two different strings.
                    std::string next;
                    for (const std::string_view other : app::RenderSettings::aovNames()) {
                        const std::string o(other);
                        const bool keep = o == n ? on : isOn(o);
                        if (keep) {
                            if (!next.empty()) next += ',';
                            next += o;
                        }
                    }
                    s.aovs = next;
                }
                if (ImGui::IsItemHovered()) {
                    tooltip("Written as its own scene-linear EXR sequence beside the beauty pass\n"
                            "(ADR-242). The renderer already draws these every frame; this is the\n"
                            "consumer.");
                }
            }
            if (!s.aovs.empty()) {
                ImGui::TextDisabled("%s", s.aovs.c_str());
            }
            ImGui::TreePop();
        }
    }
    if (auto v = s.validate(); !v) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "%s", v.error().message.c_str());
    }
    ImGui::EndDisabled();
    ImGui::Separator();
    if (running) {
        ImGui::ProgressBar(static_cast<float>(current.fraction()), ImVec2(-1, 0));
        // Remaining, from the rate the job reports rather than from a rate computed here: a long
        // render's average is the honest predictor, and the first frames of one are not -- shader
        // compilation, asset upload and a cold pipeline cache all land on them.
        //
        // Withheld rather than guessed until the job has actually produced frames. "about 0 s left"
        // on the first frame of a ten-minute render is worse than saying nothing, and a number that
        // is wrong at the only moment somebody looks at it teaches them to ignore the number.
        std::string remaining;
        if (current.framesTotal > current.framesRendered && current.renderFps > 1e-3 &&
            current.framesRendered >= 8) {
            const double left =
                static_cast<double>(current.framesTotal - current.framesRendered) / current.renderFps;
            remaining = left >= 90.0
                            ? fmt::format(", about {:.0f} min left", left / 60.0)
                            : fmt::format(", about {:.0f} s left", left);
        }
        ImGui::Text("%llu / %llu frames, %.1f fps, %s elapsed%s, written %llu",
                    static_cast<unsigned long long>(current.framesRendered),
                    static_cast<unsigned long long>(current.framesTotal), current.renderFps,
                    elapsedClock(current.elapsedSeconds).c_str(),
                    remaining.c_str(),
                    static_cast<unsigned long long>(current.framesWritten));
        if (!remaining.empty() && ImGui::IsItemHovered()) {
            tooltip("An estimate from the average rate so far. It settles as the render\n"
                              "goes on; early frames are slower because shaders are still\n"
                              "compiling and assets are still uploading.");
        }
        drawViewportSuspension();
        if (ImGui::Button("Cancel") && onCancelRender) {
            onCancelRender();
        }
    } else {
        if (ImGui::Button("Render") && onStartRender) {
            onStartRender();
        }
        ImGui::SameLine();
        if (ImGui::Button("Add to queue") && onEnqueueRender) {
            onEnqueueRender();
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(queuedRenders == 0);
        if (ImGui::Button("Run queue") && onRunQueue) {
            onRunQueue();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("%zu queued", queuedRenders);
        if (current.finished) {
            if (!current.error.empty()) {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "last render failed: %s", current.error.c_str());
            } else if (current.framesTotal > 0) {
                ImGui::TextDisabled("last render: %llu frames in %s, sequence hash %016llx%s",
                                    static_cast<unsigned long long>(current.framesRendered),
                                    elapsedClock(current.elapsedSeconds).c_str(),
                                    static_cast<unsigned long long>(current.sequenceHash),
                                    current.cancelled ? " (cancelled)" : "");
            }
        }
    }
    // ---- ADR-320: the frames the render is actually writing -------------------------------------
    //
    // One contiguous block on purpose. Everything it needs is in `renderPreview`, which the host
    // fills from `RenderJob::takePreview`; the panel owns none of the GPU work and none of the
    // threading.
    //
    // `kRenderPreviewMaxPoints` is the tallest the thumbnail is allowed to be, in ImGui points. A
    // 16:9 frame comes out 320x180 and a 9:16 one 101x180, so a portrait output cannot push the
    // caption off the panel either.
    constexpr float kRenderPreviewMaxPoints = 180.0f;
    ImGui::Separator();
    ImGui::Checkbox("Show output frames", &renderPreview.enabled);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("The frames this render is writing to the file -- the encoder's own pixels,\n"
                          "read where they are hashed, not a second render of the same moment.\n"
                          "Point-sampled down to at most 480 px, so fine detail aliases here and\n"
                          "not in the file. Only the newest frame is kept.");
    }
    if (renderPreview.enabled) {
        if (renderPreview.hasFrame()) {
            // Fitted into a box, not given the width it would like. The first version took the
            // panel's full content width, and on a docked Render panel that pushed the caption
            // below the fold -- a preview whose only guarantee is that it says which frame it is
            // showing, with the line that says so scrolled out of sight.
            const float avail = std::max(ImGui::GetContentRegionAvail().x, 64.0f);
            const float aspect = static_cast<float>(renderPreview.height) /
                                 static_cast<float>(std::max<std::uint32_t>(renderPreview.width, 1));
            float shown = std::min(avail, static_cast<float>(renderPreview.width));
            float tall = shown * aspect;
            if (tall > kRenderPreviewMaxPoints) {
                tall = kRenderPreviewMaxPoints;
                shown = tall / std::max(aspect, 0.01f);
            }
            ImGui::Image(static_cast<ImTextureID>(renderPreview.texture), ImVec2(shown, tall),
                         ImVec2(0.0f, 0.0f), ImVec2(renderPreview.u1, renderPreview.v1));
            // The hash is the point of printing it: it is the frame's own hash as written to the
            // file, so what is on screen can be tied to a specific frame of the deliverable rather
            // than being taken on trust (ADR-182).
            ImGui::TextDisabled("frame %llu of %ux%u, hash %016llx",
                                static_cast<unsigned long long>(renderPreview.index),
                                renderPreview.sourceWidth, renderPreview.sourceHeight,
                                static_cast<unsigned long long>(renderPreview.hash));
            if (!renderPreview.live) {
                // A finished, failed or cancelled render leaves its last frame on screen, and a
                // still picture cannot tell you which of those happened. Saying so is the whole
                // difference between a record and a lie.
                ImGui::TextColored(ImVec4(0.85f, 0.70f, 0.30f, 1.0f),
                                   "this render has ended -- the last frame it wrote");
            }
            if (renderPreview.linearSource) {
                ImGui::TextWrapped("EXR is scene-linear, so it has no display appearance of its own. "
                                   "Shown clamped to 0-1 and sRGB-encoded. The project's tone map, "
                                   "exposure, vignette and grain are NOT applied: this shows what is "
                                   "in the file, not what a graded view of it looks like.");
            }
            if (renderPreview.dropped > 0) {
                ImGui::TextDisabled("%llu frames went by unshown (the newest always wins)",
                                    static_cast<unsigned long long>(renderPreview.dropped));
            }
        } else if (renderPreview.live) {
            ImGui::TextDisabled("waiting for the first frame to come back");
        } else {
            ImGui::TextDisabled("frames appear here once a render starts");
        }
    }
    ImGui::TextDisabled("renders load the saved project; the live view keeps playing");
}


void ControlPanel::drawControlTab(app::Engine& engine) {
    auto& hub = engine.control();
    auto& map = hub.map();
    const auto status = hub.status();
    // ---- OSC ----
    bool ioChanged = false;
    ioChanged |= ImGui::Checkbox("OSC", &map.oscEnabled);
    ImGui::SameLine();
    int port = map.oscPort;
    ImGui::SetNextItemWidth(80);
    if (ImGui::InputInt("port", &port, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue)) {
        map.oscPort = static_cast<std::uint16_t>(std::clamp(port, 0, 65535));
        ioChanged = true;
    }
    ImGui::SameLine();
    if (status.oscOpen) {
        ImGui::TextDisabled("listening on %u: %llu msgs, %llu errors", status.oscPort,
                            static_cast<unsigned long long>(status.osc.messages),
                            static_cast<unsigned long long>(status.osc.errors));
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), "%s", status.oscError.empty() ? "closed" : status.oscError.c_str());
    }
    char prefix[64];
    std::snprintf(prefix, sizeof(prefix), "%s", map.oscPrefix.c_str());
    ImGui::SetNextItemWidth(120);
    if (ImGui::InputText("prefix", prefix, sizeof(prefix), ImGuiInputTextFlags_EnterReturnsTrue)) {
        map.oscPrefix = prefix;
    }
    ImGui::SameLine();
    ImGui::Checkbox("direct scheme", &map.directOsc);
    // ---- MIDI ----
    ioChanged |= ImGui::Checkbox("MIDI", &map.midiEnabled);
    ImGui::SameLine();
    char filter[64];
    std::snprintf(filter, sizeof(filter), "%s", map.midiFilter.c_str());
    ImGui::SetNextItemWidth(120);
    if (ImGui::InputText("filter", filter, sizeof(filter), ImGuiInputTextFlags_EnterReturnsTrue)) {
        map.midiFilter = filter;
        ioChanged = true;
    }
    ImGui::SameLine();
    if (status.midiOpen) {
        ImGui::TextDisabled("%zu source(s), %llu msgs", status.midiSources.size(),
                            static_cast<unsigned long long>(status.midi.messages));
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), "%s", status.midiError.empty() ? "closed" : status.midiError.c_str());
    }
    // ---- feedback + tempo source ----
    ioChanged |= ImGui::Checkbox("OSC feedback", &map.feedbackEnabled);
    ImGui::SameLine();
    char host[64];
    std::snprintf(host, sizeof(host), "%s", map.feedbackHost.c_str());
    ImGui::SetNextItemWidth(120);
    if (ImGui::InputText("host", host, sizeof(host), ImGuiInputTextFlags_EnterReturnsTrue)) {
        map.feedbackHost = host;
        ioChanged = true;
    }
    ImGui::SameLine();
    int fport = map.feedbackPort;
    ImGui::SetNextItemWidth(70);
    if (ImGui::InputInt("fb port", &fport, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue)) {
        map.feedbackPort = static_cast<std::uint16_t>(std::clamp(fport, 0, 65535));
        ioChanged = true;
    }
    ImGui::SameLine();
    if (status.feedbackOpen) {
        ImGui::TextDisabled("sent %llu", static_cast<unsigned long long>(status.feedbackSent));
    } else if (!status.feedbackError.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), "%s", status.feedbackError.c_str());
    }
    int tempo = engine.tempoSource() == app::TempoSource::MidiClock ? 1 : 0;
    static const char* tempoNames[] = {"analysis", "MIDI clock"};
    ImGui::SetNextItemWidth(110);
    if (ImGui::Combo("tempo source", &tempo, tempoNames, 2)) {
        engine.setTempoSource(tempo == 1 ? app::TempoSource::MidiClock : app::TempoSource::Analysis);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s, %llu clock msgs", engine.midiClockActive() ? "MIDI clock active" : "analyzer tempo",
                        static_cast<unsigned long long>(status.clockMessages));
    if (ioChanged) {
        hub.applyIo();
    }
    ImGui::Separator();

    // ---- learn ----
    ImGui::TextUnformatted("Learn: last received");
    if (const auto& m = hub.lastMidi()) {
        ImGui::Text("MIDI %s ch %d #%d = %d (%s)", control::midiKindName(m->kind), m->channel + 1, m->data1, m->data2,
                    m->source.c_str());
    } else {
        ImGui::TextDisabled("MIDI: nothing yet");
    }
    if (const auto& o = hub.lastOsc()) {
        ImGui::Text("OSC %s (%zu args)%s", o->address.c_str(), o->args.size(),
                    o->hasNumber(0) ? fmt::format(" = {:.3f}", static_cast<double>(o->number(0))).c_str() : "");
    } else {
        ImGui::TextDisabled("OSC: nothing yet");
    }
    std::vector<const char*> targets;
    targets.push_back("(signal only)");
    for (const auto* p : engine.params().ordered()) {
        if (p->flags().modulatable) {
            targets.push_back(p->path().c_str());
        }
    }
    learnTarget_ = std::clamp(learnTarget_, 0, static_cast<int>(targets.size()) - 1);
    ImGui::SetNextItemWidth(120);
    ImGui::InputText("signal", learnSignal_, sizeof(learnSignal_));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200);
    ImGui::Combo("##learntarget", &learnTarget_, targets.data(), static_cast<int>(targets.size()));
    ImGui::SameLine();
    ImGui::Checkbox("event", &learnAsEvent_);
    const std::string parameter = learnTarget_ > 0 ? targets[static_cast<std::size_t>(learnTarget_)] : "";
    if (ImGui::Button("Bind last MIDI")) {
        if (!hub.bindLastMidi(learnSignal_, parameter, learnAsEvent_)) {
            status_ = "no MIDI message received yet";
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Bind last OSC")) {
        if (!hub.bindLastOsc(learnSignal_, parameter, learnAsEvent_)) {
            status_ = "no OSC message received yet";
        }
    }
    ImGui::Separator();

    // ---- bindings ----
    ImGui::Text("Bindings: %zu MIDI, %zu OSC (applied %llu, unmatched %llu)", map.midi.size(), map.osc.size(),
                static_cast<unsigned long long>(status.applied), static_cast<unsigned long long>(status.unmatched));
    static const char* bindKinds[] = {"cc", "note", "noteEvent", "pitchBend", "pressure", "program"};
    auto targetEditor = [&](control::BindingTarget& t) {
        char sig[64];
        std::snprintf(sig, sizeof(sig), "%s", t.signal.c_str());
        ImGui::SetNextItemWidth(90);
        if (ImGui::InputText("signal", sig, sizeof(sig))) {
            t.signal = sig;
        }
        ImGui::SameLine();
        int current = 0;
        for (int i = 1; i < static_cast<int>(targets.size()); ++i) {
            if (t.parameter == targets[static_cast<std::size_t>(i)]) {
                current = i;
            }
        }
        ImGui::SetNextItemWidth(170);
        if (ImGui::Combo("param", &current, targets.data(), static_cast<int>(targets.size()))) {
            t.parameter = current > 0 ? targets[static_cast<std::size_t>(current)] : "";
        }
        if (!t.parameter.empty()) {
            ImGui::SameLine();
            float range[2] = {t.min, t.max};
            ImGui::SetNextItemWidth(110);
            if (ImGui::DragFloat2("range", range, 0.01f)) {
                t.min = range[0];
                t.max = range[1];
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(40);
            ImGui::InputInt("comp", &t.component, 0, 0);
            t.component = std::clamp(t.component, -1, 3);
        }
    };
    int removeMidi = -1;
    for (std::size_t i = 0; i < map.midi.size(); ++i) {
        auto& b = map.midi[i];
        ImGui::PushID(static_cast<int>(i));
        int kind = static_cast<int>(b.kind);
        ImGui::SetNextItemWidth(90);
        if (ImGui::Combo("##kind", &kind, bindKinds, 6)) {
            b.kind = static_cast<control::MidiBindKind>(kind);
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(40);
        ImGui::InputInt("ch", &b.channel, 0, 0);
        b.channel = std::clamp(b.channel, -1, 15);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(40);
        ImGui::InputInt("#", &b.number, 0, 0);
        b.number = std::clamp(b.number, -1, 127);
        if (b.kind == control::MidiBindKind::Note) {
            ImGui::SameLine();
            ImGui::Checkbox("toggle", &b.toggle);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) {
            removeMidi = static_cast<int>(i);
        }
        ImGui::Indent();
        targetEditor(b.target);
        ImGui::Unindent();
        ImGui::PopID();
    }
    if (removeMidi >= 0) {
        map.midi.erase(map.midi.begin() + removeMidi);
    }
    int removeOsc = -1;
    for (std::size_t i = 0; i < map.osc.size(); ++i) {
        auto& b = map.osc[i];
        ImGui::PushID(1000 + static_cast<int>(i));
        char addr[128];
        std::snprintf(addr, sizeof(addr), "%s", b.address.c_str());
        ImGui::SetNextItemWidth(160);
        if (ImGui::InputText("OSC", addr, sizeof(addr))) {
            b.address = addr;
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(40);
        ImGui::InputInt("arg", &b.argIndex, 0, 0);
        b.argIndex = std::clamp(b.argIndex, 0, 15);
        ImGui::SameLine();
        ImGui::Checkbox("event", &b.event);
        ImGui::SameLine();
        float in[2] = {b.inMin, b.inMax};
        ImGui::SetNextItemWidth(110);
        if (ImGui::DragFloat2("in", in, 0.5f)) {
            b.inMin = in[0];
            b.inMax = in[1];
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) {
            removeOsc = static_cast<int>(i);
        }
        ImGui::Indent();
        targetEditor(b.target);
        ImGui::Unindent();
        ImGui::PopID();
    }
    if (removeOsc >= 0) {
        map.osc.erase(map.osc.begin() + removeOsc);
    }
    ImGui::TextDisabled("direct OSC: %s/param/<path> f, /signal/<ch> f, /pulse/<ch>, /preset/recall s, /transport/play",
                        map.oscPrefix.c_str());
}


void ControlPanel::drawOutputsTab(app::Engine& /*engine*/) {
    if (outputs == nullptr) {
        ImGui::TextDisabled("outputs unavailable");
        return;
    }
    const auto displays = platform::Window::displays();
    std::vector<std::string> displayLabels;
    std::vector<const char*> displayNames;
    for (const auto& d : displays) {
        displayLabels.push_back(fmt::format("{}: {} ({}x{} @ {:.0f} Hz{})", d.index, d.name, d.width, d.height,
                                            static_cast<double>(d.refreshRate), d.primary ? ", primary" : ""));
    }
    for (const auto& l : displayLabels) {
        displayNames.push_back(l.c_str());
    }
    newOutputDisplay_ = std::clamp(newOutputDisplay_, 0, std::max(0, static_cast<int>(displayNames.size()) - 1));
    ImGui::SetNextItemWidth(260);
    ImGui::Combo("##display", &newOutputDisplay_, displayNames.data(), static_cast<int>(displayNames.size()));
    ImGui::SameLine();
    ImGui::Checkbox("fullscreen", &newOutputFullscreen_);
    ImGui::SameLine();
    if (ImGui::Button("Add output") && !displays.empty()) {
        app::OutputDesc desc;
        desc.name = "output" + std::to_string(outputs->outputs().size() + 1);
        desc.display = displays[static_cast<std::size_t>(newOutputDisplay_)].index;
        desc.fullscreen = newOutputFullscreen_;
        if (auto r = outputs->add(desc); !r) {
            status_ = r.error().message;
        } else if (onOutputsChanged) {
            onOutputsChanged();
        }
    }
    ImGui::Separator();
    std::string removeName;
    bool changed = false;
    for (auto& out : outputs->outputs()) {
        auto& d = out->desc;
        ImGui::PushID(d.name.c_str());
        const bool open = ImGui::TreeNodeEx(d.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen, "%s  display %d%s  %s%s", d.name.c_str(),
                                            d.display, d.fullscreen ? " fullscreen" : "", out->open() ? "open" : "closed",
                                            out->lastError.empty() ? "" : "  !");
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 24);
        if (ImGui::SmallButton("x")) {
            removeName = d.name;
        }
        if (open) {
            if (!out->lastError.empty()) {
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), "%s", out->lastError.c_str());
            }
            changed |= ImGui::Checkbox("enabled", &d.enabled);
            ImGui::SameLine();
            changed |= ImGui::Checkbox("fullscreen", &d.fullscreen);
            ImGui::SameLine();
            changed |= ImGui::Checkbox("borderless", &d.borderless);
            ImGui::SameLine();
            changed |= ImGui::Checkbox("on top", &d.alwaysOnTop);
            ImGui::SetNextItemWidth(60);
            changed |= ImGui::InputInt("display", &d.display, 0, 0);
            ImGui::SameLine();
            int size[2] = {static_cast<int>(d.width), static_cast<int>(d.height)};
            ImGui::SetNextItemWidth(140);
            if (ImGui::InputInt2("size", size)) {
                d.width = static_cast<std::uint32_t>(std::clamp(size[0], 16, 16384));
                d.height = static_cast<std::uint32_t>(std::clamp(size[1], 16, 16384));
                changed = true;
            }
            // Mapping: crop, warp corners, blend, colour. Edits apply live (no reopen needed).
            auto& m = d.mapping;
            float crop[4] = {m.crop.x, m.crop.y, m.crop.w, m.crop.h};
            if (ImGui::DragFloat4("crop x y w h", crop, 0.002f, 0.0f, 1.0f)) {
                m.crop = {crop[0], crop[1], std::max(0.001f, crop[2]), std::max(0.001f, crop[3])};
            }
            static const char* cornerNames[] = {"top-left", "top-right", "bottom-right", "bottom-left"};
            for (int c = 0; c < 4; ++c) {
                float xy[2] = {m.corners[static_cast<std::size_t>(c)].x, m.corners[static_cast<std::size_t>(c)].y};
                ImGui::SetNextItemWidth(160);
                if (ImGui::DragFloat2(cornerNames[c], xy, 0.002f, -0.5f, 1.5f)) {
                    m.corners[static_cast<std::size_t>(c)] = {xy[0], xy[1]};
                }
            }
            float blend[4] = {m.blend.left, m.blend.right, m.blend.top, m.blend.bottom};
            if (ImGui::DragFloat4("blend l r t b", blend, 0.002f, 0.0f, 0.5f)) {
                m.blend = {blend[0], blend[1], blend[2], blend[3]};
            }
            ImGui::SetNextItemWidth(90);
            ImGui::DragFloat("blend gamma", &m.blendGamma, 0.01f, 0.1f, 5.0f);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(90);
            ImGui::DragFloat("brightness", &m.brightness, 0.01f, 0.0f, 4.0f);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(90);
            ImGui::DragFloat("gamma", &m.gamma, 0.01f, 0.2f, 4.0f);
            ImGui::Checkbox("flip X", &m.flipX);
            ImGui::SameLine();
            ImGui::Checkbox("flip Y", &m.flipY);
            ImGui::SameLine();
            if (ImGui::SmallButton("reset mapping")) {
                m = rendering::OutputMapping::identity();
            }
            ImGui::TextDisabled("%llu frames presented", static_cast<unsigned long long>(out->framesPresented));
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if (!removeName.empty()) {
        outputs->remove(removeName);
        changed = true;
    }
    if (changed && onOutputsChanged) {
        onOutputsChanged();
    }
    ImGui::Separator();
    ImGui::TextUnformatted("Share");
    static const char* shareKinds[] = {"off", "syphon", "ndi"};
    ImGui::SetNextItemWidth(90);
    ImGui::Combo("##sharekind", &shareKind_, shareKinds, 3);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140);
    ImGui::InputText("name", shareName_, sizeof(shareName_));
    ImGui::SameLine();
    if (ImGui::Button("Apply") && onShare) {
        onShare(shareKinds[shareKind_], shareName_);
    }
    if (!shareStatus.empty()) {
        ImGui::TextWrapped("%s", shareStatus.c_str());
    }
}


// The camera library (ADR-245).
//
// The whole novice workflow the multi-camera brief asks for, and nothing beyond it:
//
//   + Camera            -- makes one where the viewport is looking now
//   Place here          -- moves the selected camera to the viewport's pose
//   Available to Auto-director / lens / event scenario  -- what the director may do with it
//   Add shot at playhead -- puts it on the timeline
//
// Deeper editing is not duplicated here on purpose. A camera's channels are ordinary parameters, so
// keyframing one is the Sequence panel's job and tuning one is the Parameters panel's; a second set
// of controls for the same values would be a second source of truth, which section 28 of the brief
// forbids in as many words.
//
// **Unverified visually.** This agent cannot see ImGui; what is checked is the model underneath
// (tests/integration/test_camera_multicam.cpp), not the drawing.
void ControlPanel::drawCameras(app::Engine& engine) {
    scene::Composition* comp = engine.composition();
    if (comp == nullptr) {
        ImGui::TextUnformatted("This scene has no composition, so it has one fixed camera.");
        return;
    }
    const scene::ActiveCameraState active = engine.activeCamera();
    scene::CameraDirection direction = comp->cameraDirection();
    bool changed = false;
    // Every write this panel makes to the camera collection is one command on the editor's history
    // (ADR-752), labelled for what the person did. Before, none of them was undoable at all.
    std::string editLabel;
    std::optional<seq::Sequence> pendingPiece; // the "x" button's sequencer half, applied with it

    ImGui::Text("Live: %s (%s%s%s)", active.name.empty() ? "Main" : active.name.c_str(),
                scene::activeCameraReasonName(active.reason),
                active.eventName.empty() ? "" : " ", active.eventName.c_str());
    if (active.blending()) {
        ImGui::SameLine();
        ImGui::Text("-- blending %.0f%%", static_cast<double>(active.blend) * 100.0);
    }

    // The lock (viewport brief §7). It was written as a guard on data loss: dragging the viewport
    // used to stand the director down and discard the whole baked cut, and the next Save wrote the
    // loss. Since ADR-582 standing the director down parks the cut instead, so nothing is lost
    // either way; what the lock still prevents is an incidental drag taking the film's camera away
    // from the director at all.
    //
    // Shown only when there is something to protect, so an undirected project is not asked to think
    // about a lock that guards nothing.
    if (cameraDirected && cameraLocked != nullptr) {
        ImGui::Separator();
        bool locked = *cameraLocked;
        if (ImGui::Checkbox("Lock camera (this project's cut is baked)", &locked)) {
            *cameraLocked = locked;
        }
        if (locked) {
            ImGui::TextColored(ImVec4(0.62f, 0.66f, 0.72f, 1.0f),
                               "  The film's camera cannot be moved by hand. Navigating the canvas\n"
                               "  never could change the cut anyway -- the editor's viewpoint is\n"
                               "  not the film's camera (ADR-391).");
        } else {
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.4f, 1.0f),
                               "  Unlocked: moving the viewport camera takes the camera from the\n"
                               "  director. Its cut is kept -- world effects still follow it -- and\n"
                               "  Resume Director (Auto-director panel) puts it back.");
        }
    }
    ImGui::Separator();

    if (ImGui::Button("+ Camera")) {
        // Where the viewport is looking, because "make a camera here" is what the button means and
        // a camera that appears at the origin is one the user has to go and find.
        const scene::Camera& live = comp->scene().camera;
        scene::CameraRig rig;
        rig.name = "Camera " + std::to_string(direction.nextId);
        rig.position = live.position;
        rig.target = live.target;
        rig.fovDegrees = glm::degrees(live.effectiveFovY());
        selectedCamera_ = direction.addCamera(std::move(rig));
        changed = true;
        editLabel = "Add camera";
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(%zu in this scene)", direction.cameras.size());

    for (scene::CameraRig& rig : direction.cameras) {
        ImGui::PushID(static_cast<int>(rig.id));
        const bool isActive = rig.id == active.camera;
        const bool selected = rig.id == selectedCamera_;
        std::string label = rig.name;
        if (isActive) {
            label += "   [LIVE]";
        }
        if (rig.id != scene::kMainCamera && comp->cameraIsAnimated(rig.id, engine.timeline())) {
            label += "   animated";
        }
        if (ImGui::Selectable(label.c_str(), selected)) {
            selectedCamera_ = rig.id;
            // panel -> Canvas: the same `Selection` the viewport and the Lights panel share, so a
            // camera chosen here highlights in the world (§4's "selection must remain synchronized
            // in both directions"). Before this, `selectedCamera_` had no observer anywhere.
            editor.selection.set(SelectionRef{SelectionRef::Kind::Camera, rig.name});
        }
        if (selected) {
            ImGui::Indent();
            if (rig.id != scene::kMainCamera) {
                // **Look through** (the brief's §6), which could not be built until there was a
                // viewpoint the film did not own. It pins the canvas to this rig: the director goes
                // on cutting, the film goes on rendering from whatever it chose, and what is on
                // screen is this camera until you say otherwise. Moving the view takes you off it
                // and onto the editor's viewpoint, starting from here (`setViewportPose`).
                //
                // No longer blocked by the lock. There is nothing to protect: this writes no
                // parameter, no track and no file.
                const bool through = viewportView.mode == scene::ViewportCamera::Through &&
                                     viewportView.camera == rig.id;
                if (ImGui::Button(through ? "Stop looking through" : "Look through")) {
                    if (onViewportView) {
                        onViewportView(through ? scene::ViewportView{}
                                               : scene::ViewportView{scene::ViewportCamera::Through, rig.id});
                    }
                    setStatus(through ? "the canvas is showing the film again"
                                      : "looking through '" + rig.name + "' -- the film is unchanged");
                }
                if (ImGui::IsItemHovered()) {
                    tooltip("Pin the canvas to this camera. Nothing about the film changes:\n"
                            "the director still decides what renders.");
                }
                ImGui::SameLine();
                // "Go to camera": the *viewport* moves to match this camera and stays where it is
                // put. Through the host's one navigation seam, so it writes whatever a drag would
                // write -- the editor's viewpoint, ordinarily; `camera/*` only where a drag would
                // also have written it (an output frame preview, an open output).
                if (ImGui::Button("Go to camera")) {
                    const std::string prefix = rig.channelPrefix();
                    const auto* pos = engine.params().find(prefix + "position");
                    const auto* tgt = engine.params().find(prefix + "target");
                    if (onMoveViewport && pos != nullptr && tgt != nullptr) {
                        onMoveViewport(glm::vec3(pos->baseComponent(0), pos->baseComponent(1),
                                                 pos->baseComponent(2)),
                                       glm::vec3(tgt->baseComponent(0), tgt->baseComponent(1),
                                                 tgt->baseComponent(2)));
                        setStatus("the view moved to '" + rig.name + "' -- it is not pinned to it; "
                                  "use Look through for that");
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Place here")) {
                    // The viewport's pose onto this camera's *base* values. A parameter, never the
                    // derived scene camera: ADR-218's rule, and the reason this does not evaporate
                    // on the next frame. Measured into one undoable command like the rest.
                    app::EditCapture place;
                    place.begin(engine);
                    const scene::Camera& live = comp->scene().camera;
                    const std::string prefix = rig.channelPrefix();
                    if (auto* p = engine.params().find(prefix + "position")) {
                        p->setBaseComponent(0, live.position.x);
                        p->setBaseComponent(1, live.position.y);
                        p->setBaseComponent(2, live.position.z);
                    }
                    if (auto* t = engine.params().find(prefix + "target")) {
                        t->setBaseComponent(0, live.target.x);
                        t->setBaseComponent(1, live.target.y);
                        t->setBaseComponent(2, live.target.z);
                    }
                    editor.history().push(place.finish(engine, "Place " + rig.name));
                }
                ImGui::SameLine();
                if (ImGui::Button("Delete")) {
                    editLabel = "Delete " + rig.name;
                    direction.removeCamera(rig.id);
                    selectedCamera_ = scene::kMainCamera;
                    changed = true;
                    ImGui::Unindent();
                    ImGui::PopID();
                    break;
                }
                float mm = rig.focalLength;
                const bool lensMoved = ImGui::SliderFloat("lens (0 = use fov)", &mm, 0.0f, 200.0f, "%.0f mm");
                if (ImGui::IsItemActivated()) {
                    lensDrag_.begin(engine);
                }
                if (lensMoved) {
                    rig.focalLength = mm;
                    changed = true;
                }
                if (ImGui::IsItemDeactivated() && lensDrag_.open()) {
                    // Pushed after this frame's install below has landed, by the block at the end.
                    editLabel = "Change lens of " + rig.name;
                }
            }
            if (ImGui::Checkbox("available to the Auto-director", &rig.autoDirectorEligible)) {
                changed = true;
                editLabel = rig.autoDirectorEligible ? "Offer " + rig.name + " to the Auto-director"
                                                     : "Withhold " + rig.name + " from the Auto-director";
            }
            if (!rig.eventScenario.empty()) {
                ImGui::TextDisabled("watches the '%s' scenario", rig.eventScenario.c_str());
            }
            // "Cut to", not "Add shot": this list is the camera track and the word `shot` already
            // means something else two panels away. See the note on the heading below.
            if (ImGui::Button("Cut to this camera at the playhead")) {
                scene::CameraShot shot;
                shot.camera = rig.id;
                shot.startSeconds = engine.timelineClock().seconds;
                shot.endSeconds = shot.startSeconds + 4.0;
                direction.shots.push_back(shot);
                changed = true;
                editLabel = "Cut to " + rig.name;
            }
            ImGui::Unindent();
        }
        ImGui::PopID();
    }

    if (!direction.shots.empty()) {
        ImGui::Separator();
        // ---- this list is not the sequencer's shot list ------------------------------------------
        //
        // It was called "Shots", and so is the sequencer's lane, and they are different things:
        //
        //   scene::CameraShot  -- {camera, span, transition, locked}. WHICH CAMERA IS LIVE.
        //   seq::Shot          -- {scene, camera move, tracks, transitions}. WHAT THE SHOT IS.
        //
        // In a multi-camera piece one sequencer shot can legitimately span several camera cuts, and
        // one camera can legitimately stay live across several sequencer shots, so ADR-245 keeps
        // them apart on purpose and neither is derivable from the other.
        //
        // Reported as a disagreement -- "the shots list has incorrect start times", "clicking one
        // jumps the playhead to the wrong spot" -- and both halves of that were the same mistake:
        // two lists wearing one word. The row was going to the right place; it was not the place the
        // name promised. So the name changes, and every row that does not line up with a sequencer
        // shot says so, because a drift you cannot see is a drift you cannot fix.
        ImGui::TextUnformatted("Camera track");
        ImGui::TextDisabled("Which camera is live, and when. Not the sequencer's shot list:\n"
                            "a sequencer shot is the framing, a row here is the cut.");
        const std::vector<seq::Shot>& pieceShots = engine.sequence().shots;
        for (std::size_t i = 0; i < direction.shots.size(); ++i) {
            const scene::CameraShot& shot = direction.shots[i];
            ImGui::PushID(static_cast<int>(i));
            // Clickable: a row is a moment in the piece, and the useful thing to do with a moment is
            // go to it. Selectable rather than Text so the whole row is the target -- a row you have
            // to hit a word inside is a row people miss.
            // How this cut sits against the sequencer's shots. Silence when they agree, which is
            // the common case and the one that deserves no ink.
            const auto agrees = [&](const seq::Shot& s) {
                constexpr double kSame = 0.05; // a frame and a half at 30 fps
                return std::abs(s.startSeconds - shot.startSeconds) < kSame &&
                       std::abs(s.endSeconds() - shot.endSeconds) < kSame;
            };
            std::size_t covered = 0;
            bool exact = false;
            for (const seq::Shot& s : pieceShots) {
                if (s.startSeconds < shot.endSeconds - 1e-6 && s.endSeconds() > shot.startSeconds + 1e-6) {
                    ++covered;
                    exact = exact || agrees(s);
                }
            }
            const char* drift = "";
            if (pieceShots.empty()) {
                drift = ""; // nothing to disagree with
            } else if (covered == 0) {
                drift = "  -- no shot here";
            } else if (!exact) {
                drift = covered == 1 ? "  -- offset from its shot" : "  -- spans several shots";
            }
            char row[224];
            std::snprintf(row, sizeof(row), "%6.2f - %6.2f  %s  %s%s%s", shot.startSeconds,
                          shot.endSeconds, direction.nameOf(shot.camera).c_str(),
                          scene::shotTransitionName(shot.transition),
                          shot.locked ? "  locked" : "", drift);
            const double now = engine.timelineClock().seconds;
            const bool live = now >= shot.startSeconds && now < shot.endSeconds;
            if (ImGui::Selectable(row, live, ImGuiSelectableFlags_AllowOverlap)) {
                engine.seekSeconds(shot.startSeconds);
            }
            if (ImGui::IsItemHovered()) {
                tooltip("Go to %.2fs, where this camera becomes live.\n"
                                  "That is this cut's own start, which need not be a sequencer\n"
                                  "shot's start -- the two lists are different things.",
                                  shot.startSeconds);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("x")) {
                // **The sequencer shot goes too.** A camera shot says which camera is live over a
                // span; the sequencer shot is the framing over that same span. Removing one and
                // leaving the other is how a piece ends up with a cut to a camera that has nothing
                // to show, or a framing nobody cuts to -- two halves of one edit, deleted by halves.
                const double start = shot.startSeconds;
                const double end = shot.endSeconds;
                direction.shots.erase(direction.shots.begin() + static_cast<std::ptrdiff_t>(i));
                seq::Sequence piece = engine.sequence();
                const std::size_t before = piece.shots.size();
                std::erase_if(piece.shots, [&](const seq::Shot& s) {
                    // The span this camera shot covered, by overlap rather than by exact equality:
                    // a directed camera shot merges adjacent spans on the same camera, so its start
                    // and end need not match any one sequencer shot's to the microsecond.
                    return s.startSeconds < end - 1e-6 && s.endSeconds() > start + 1e-6;
                });
                if (piece.shots.size() != before) {
                    pendingPiece = std::move(piece); // installed below, inside the same command
                }
                changed = true;
                editLabel = "Remove cut";
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
    }

    // A lens drag in progress installs every frame but records nothing until release: its capture
    // opened on press. Anything else is measured around its own install, here.
    const bool dragging = lensDrag_.open() && editLabel.empty();
    app::EditCapture single;
    app::EditCapture& capture = lensDrag_.open() ? lensDrag_ : single;
    if (changed && !capture.open()) {
        capture.begin(engine);
    }
    if (changed) {
        if (pendingPiece) {
            if (auto ok = engine.setSequence(std::move(*pendingPiece)); !ok) {
                cameraProblem_ = ok.error().message;
            }
        }
        // Refused whole if it cannot be evaluated, and the message is shown rather than swallowed.
        if (auto ok = engine.setCameraDirection(std::move(direction)); !ok) {
            cameraProblem_ = ok.error().message;
        } else {
            cameraProblem_.clear();
        }
    }
    if (capture.open() && !dragging && !editLabel.empty()) {
        editor.history().push(capture.finish(engine, editLabel));
    } else if (&capture == &single) {
        single.cancel();
    }
    if (!cameraProblem_.empty()) {
        ImGui::TextUnformatted(cameraProblem_.c_str());
    }
}

// Song Mode's half of the Auto-director panel (ADR-249, the brief's section 17).
//
// Deliberately three things and not a matrix: what the plan is, how much freedom the director has
// over the whole film, and -- per section -- how much freedom that section allows. The brief asks
// for exactly this shape:
//
//     Section
//       Type: Chorus                     <- the Sequence panel's section inspector owns this
//       Shot: Dynamic Hero Coverage      <- ...and this
//       Director: Guided                 <- this panel owns this
//
// The type and the intent belong to the song, and the Sequence panel is where a person edits the
// song. What belongs here is the one decision that is about the *director*: how faithfully to
// execute what the song asked for.
//
// **Unverified visually beyond a capture.** This agent cannot see ImGui while it runs; what is
// checked here is the model underneath (`tests/unit/test_song_director.cpp`) and one `--capture-ui`
// screenshot of this panel with Song selected.
void ControlPanel::drawSongDirector(app::Engine& engine, app::AutoDirectorSettings& s) {
    ImGui::Separator();
    // The ceiling. Named for what it does rather than for what it is: "freedom" is the word a
    // person uses, and the tooltip carries the fact that it only ever reduces.
    ImGui::TextUnformatted("Director freedom");
    if (ImGui::IsItemHovered()) {
        tooltip(
            "The most freedom any section gets. A section may ask for less; none gets more,\n"
            "so lowering this can always be trusted to make the film more faithful to what\n"
            "you authored.\n\n"
            "Locked      one shot, one camera, framed exactly as the section asked.\n"
            "Guided      the intent is respected; the camera, the cuts and the framing are\n"
            "            the director's.\n"
            "Expressive  ...and the director also reads the section's own loudness and\n"
            "            busyness, so a loud section gets more coverage than a quiet one\n"
            "            carrying the same intent.");
    }
    int autonomy = static_cast<int>(s.autonomy);
    bool first = true;
    for (const app::Autonomy a : app::allAutonomies()) {
        if (!first) {
            ImGui::SameLine();
        }
        first = false;
        if (ImGui::RadioButton(app::autonomyName(a), &autonomy, static_cast<int>(a))) {
            s.autonomy = a;
        }
    }

    // What Song Mode would actually direct, asked rather than assumed. Cheap: it is either a copy
    // of the authored plan or one pass over the analyzed sections' measurements -- no analysis runs
    // here and none may (the brief's section 21).
    auto plan = app::songPlanForEngine(engine);
    if (!plan) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.35f, 1.0f), "%s", plan.error().message.c_str());
        return;
    }
    // **Where the plan came from, said the way `songPlanForEngine` decides it.**
    //
    // This used to read `!engine.songPlan().empty()` and call the answer "from this project's song
    // plan", which stopped being true the day the film outranked the saved plan: a project with a
    // section timeline *and* a saved plan is cut from the timeline and the panel said the opposite.
    // A panel that misreports its own source sends a person to edit the wrong object, which is most
    // of what made this feature look unwired.
    const bool fromFilm = !engine.sequence().sectionTimeline.sections.empty();
    const bool authored = !fromFilm && !engine.songPlan().empty();
    ImGui::TextDisabled("%zu section(s), %s", plan->sections.size(),
                        fromFilm    ? "from the Sequencer's sections"
                        : authored  ? "from this project's saved song plan"
                                    : "derived from the analyzed structure");
    if (ImGui::IsItemHovered()) {
        tooltip(
            fromFilm ? "Cut from the sections in the Sequencer, through each one's type and\n"
                       "treatment. Change a section's Type or Shot there and the film re-cuts."
            : authored ? "Cut from the song plan saved in this project. Add sections in the\n"
                         "Sequencer and they take over: the thing being edited outranks a\n"
                         "snapshot of what it used to be."
                       : "Nobody has authored sections for this song yet, so each section's\n"
                         "intent is derived from how loud and how busy it measured -- no labels\n"
                         "are read.");
    }

    // The per-section rows. In a scrolling child, because a four-minute track is twenty of them and
    // the panel has eight other controls under this one.
    const float rowHeight = ImGui::GetTextLineHeightWithSpacing();
    if (ImGui::BeginChild("song-sections", ImVec2(0.0f, std::min(9.0f, static_cast<float>(plan->sections.size()) + 0.5f) * rowHeight),
                          ImGuiChildFlags_Borders)) {
        for (std::size_t i = 0; i < plan->sections.size(); ++i) {
            const app::SongPlanSection& section = plan->sections[i];
            ImGui::PushID(static_cast<int>(i));
            // The combo first and the description after it, rather than the description with the
            // combo pushed to the right margin. The second reads better on paper and does not
            // survive a long intent name: `SameLine(avail - 96)` draws the combo *over* text that
            // is still running, and "Slow Environmental Exploration" put four characters of itself
            // on the far side of the dropdown. Seen in a `--capture-ui` screenshot, which is the
            // only way this kind of defect is ever seen.
            ImGui::SetNextItemWidth(92.0f);
            int rowAutonomy = static_cast<int>(section.autonomy);
            const char* names[] = {app::autonomyName(app::Autonomy::Locked),
                                   app::autonomyName(app::Autonomy::Guided),
                                   app::autonomyName(app::Autonomy::Expressive)};
            // **Disabled when the film is the source, because there is nowhere to put the answer.**
            //
            // This combo writes to `engine.songPlan()`, and a project with a section timeline is no
            // longer cut from `engine.songPlan()` at all -- so every click landed on an object the
            // director does not read. `song::Section` carries a type and a treatment and no
            // autonomy: per-section freedom is not a thing the film can currently hold, and a
            // control that cannot be kept is not a control (ADR-225). Shown rather than removed so
            // that the film-wide radio above it still reads as the thing that governs.
            ImGui::BeginDisabled(fromFilm);
            const bool retyped = ImGui::Combo("##autonomy", &rowAutonomy, names, IM_ARRAYSIZE(names));
            ImGui::EndDisabled();
            if (fromFilm && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                tooltip(
                    "These sections come from the Sequencer, and a section there does not carry a\n"
                    "freedom of its own -- only a type and a treatment. Use Director freedom above,\n"
                    "which applies to the whole film.");
            }
            ImGui::SameLine();
            ImGui::Text("%5.1f  %s  --  %s", section.startSeconds,
                        section.label.empty() ? "(unnamed)" : section.label.c_str(),
                        section.intent.id.c_str());
            if (ImGui::IsItemHovered()) {
                tooltip("%s\n\nhero %.0f%%  distance %.0f%%  movement %.0f%%\n"
                                  "variation %.0f%%  cut rate %.0f%%  cameras %d\n"
                                  "measured: energy %.0f%%, density %.0f%%\n"
                                  "pass %d over this material",
                                  section.intent.id.c_str(),
                                  static_cast<double>(section.intent.heroEmphasis) * 100.0,
                                  static_cast<double>(section.intent.distance) * 100.0,
                                  static_cast<double>(section.intent.movement) * 100.0,
                                  static_cast<double>(section.intent.variation) * 100.0,
                                  static_cast<double>(section.intent.cutRate) * 100.0,
                                  section.intent.cameras,
                                  static_cast<double>(section.energy) * 100.0,
                                  static_cast<double>(section.density) * 100.0,
                                  section.occurrence + 1);
            }
            if (retyped) {
                // The first edit to a derived plan makes it this project's own. Anything else would
                // be a control whose value is thrown away on the next frame, which ADR-225 calls a
                // setting nobody keeps.
                if (engine.songPlan().empty()) {
                    engine.songPlan() = *plan;
                }
                if (i < engine.songPlan().sections.size()) {
                    engine.songPlan().sections[i].autonomy =
                        static_cast<app::Autonomy>(rowAutonomy);
                    // Only while the director steers: a parked cut is replaced by an explicit
                    // Enable, never by editing a control (ADR-582).
                    if (onDirectCamera && engine.timeline().isAutomated("camera/position") &&
                        !engine.directorParked()) {
                        onDirectCamera();
                    }
                }
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
}

// The Auto-director panel (section 9). Everything here changes the film; the three properties
// that would *look* like controls and do nothing -- framing and headroom, a hero's preferred
// elevation, and Shot::speed -- are deliberately absent, and `AutoDirectorSettings` says why.
//
// A panel rather than a menu because these are adjusted while watching the result, and a menu
// closes on every click. It also puts the settings beside the enable and disable controls,
// which were previously in a different menu entirely.
void ControlPanel::drawAutoDirector(app::Engine& engine) {
    if (autoDirector == nullptr) {
        ImGui::TextUnformatted("The Auto-director is unavailable in this session.");
        return;
    }
    const bool directed = engine.timeline().isAutomated("camera/position");
    ImGui::TextUnformatted(directed ? "The Auto-director owns this camera."
                                    : "The camera is with the viewport.");
    // Song Mode does not fold audio -- the fold has already happened and somebody has edited the
    // result -- so it can direct a project whose track is not loaded, and the button must not be
    // disabled for the absence of something it does not need (ADR-249).
    const bool songMode = autoDirector->mode == app::DirectorMode::Song;
    const bool canDirect =
        songMode ? app::songPlanForEngine(engine).has_value() : engine.track() != nullptr;
    ImGui::BeginDisabled(!canDirect);
    if (ImGui::Button("Enable Auto-director") && onDirectCamera) {
        onDirectCamera();
    }
    ImGui::EndDisabled();
    if (!canDirect && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        // Stated rather than hidden: an absent control reads as a missing feature.
        tooltip(songMode
                              ? "Song Mode directs a song's own sections; analyze a track in the "
                                "Sequence panel, or load a song plan, first."
                              : "Directing cuts to the music; load a track first.");
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!directed);
    if (ImGui::Button("Disable Auto-director") && onClearCameraAutomation) {
        onClearCameraAutomation();
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        tooltip("Gives you the camera. The cut is kept, and world effects keep following it.");
    }
    // ADR-582: taking the camera back parks the cut, so the way back sits beside the way out. Named
    // for what it does rather than for the mechanism: nobody asked to "restore a parked bake".
    const bool parked = engine.directorParked();
    ImGui::SameLine();
    ImGui::BeginDisabled(!parked || !onResumeDirector);
    if (ImGui::Button("Resume Director")) {
        onResumeDirector();
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        tooltip(parked ? "Gives the camera back to the director, with exactly the cut it had."
                       : "Nothing to resume: the director has not been set aside.");
    }
    if (parked) {
        ImGui::TextColored(ImVec4(0.62f, 0.66f, 0.72f, 1.0f),
                           "The director's cut is set aside, not deleted. World effects still\n"
                           "follow it, in playback and in renders.");
    }
    // Deleting a cut is the one camera action Resume cannot undo, so it asks first.
    const bool haveCut = parked || directed || !engine.shotSpans().empty();
    ImGui::BeginDisabled(!haveCut || !onDiscardDirectorsCut);
    if (ImGui::Button("Discard Director's Cut...")) {
        ImGui::OpenPopup("Discard the director's cut?");
    }
    ImGui::EndDisabled();
    if (ImGui::BeginPopupModal("Discard the director's cut?", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("This deletes the cut: the camera moves, and the timing that world\n"
                               "effects such as the hero pulse follow. Enable Auto-director makes a\n"
                               "new cut, but not the same one.");
        if (ImGui::Button("Discard")) {
            onDiscardDirectorsCut();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Keep it")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::Separator();
            app::AutoDirectorSettings& s = *autoDirector;
            const app::AutoDirectorSettings before = s;

            int mode = static_cast<int>(s.mode);
            ImGui::TextUnformatted("Shot mode");
            if (ImGui::RadioButton("Continuous shot", &mode, 0)) {
                s.mode = app::DirectorMode::ContinuousShot;
            }
            if (ImGui::IsItemHovered()) {
                tooltip("One uninterrupted take. The camera travels through the world "
                                  "and around its subjects without editorial cuts: each move "
                                  "begins where the last ended and at the speed it ended with.");
            }
            if (ImGui::RadioButton("Edited sequence", &mode, 1)) {
                s.mode = app::DirectorMode::EditedSequence;
            }
            if (ImGui::IsItemHovered()) {
                tooltip("A cut list. Each shot is composed independently and the "
                                  "camera cuts between compositions and subjects, which is what "
                                  "a piece with distinct sections wants.");
            }
            // ADR-249. Third, because it is the newest and because the other two are what an
            // untouched project still means.
            if (ImGui::RadioButton("Song", &mode, 2)) {
                s.mode = app::DirectorMode::Song;
            }
            if (ImGui::IsItemHovered()) {
                tooltip(
                    "The song's own sections are the script, and the director decides what\n"
                    "happens inside each one: which camera, how it is framed, how it moves,\n"
                    "and when to cut.\n\n"
                    "Not a shot list played back. Each section carries a shot *intent* -- how\n"
                    "close, how much movement, how much coverage -- and the same intent gives a\n"
                    "different film the second time it comes round.\n\n"
                    "Uses every camera you have ticked 'available to the Auto-director'.\n\n"
                    "Needs only an analyzed song -- import audio, tick 'Analyze song structure',\n"
                    "and this has everything it needs. Performer actions are a separate, optional\n"
                    "system for what the CAST does; nothing here waits on them.");
            }

            const bool song = s.mode == app::DirectorMode::Song;
            const bool continuous = s.mode == app::DirectorMode::ContinuousShot;
            if (song) {
                drawSongDirector(engine, s);
            }

            ImGui::Separator();
            // Named for what the controls do in the mode that is actually selected. All three are
            // live in both modes -- they group the analyzer's sections into shots before either
            // mode sees them -- so none of them is disabled here (ADR-203). What changes is what a
            // "shot" *is*: a cut in an edited sequence, and a change of subject and intent inside
            // one unbroken move in a continuous take.
            ImGui::TextUnformatted(continuous ? "Shot timing (moves, not cuts)" : "Shot timing");
            if (ImGui::IsItemHovered()) {
                tooltip(
                    continuous
                        ? "A continuous take does not cut, so these do not set cut lengths.\n"
                          "They set how often the camera changes what it is doing and who it\n"
                          "is looking at -- the boundaries between moves inside the one take."
                        : "How long each cut runs before the next one.");
            }
            auto seconds = [](const char* label, double& v, double lo, double hi, const char* tip) {
                auto f = static_cast<float>(v);
                if (ImGui::SliderFloat(label, &f, static_cast<float>(lo), static_cast<float>(hi),
                                       "%.1f s")) {
                    v = static_cast<double>(f);
                }
                if (ImGui::IsItemHovered()) {
                    tooltip("%s", tip);
                }
            };
            seconds("shortest shot", s.minShotSeconds, 1.0, 30.0,
                    "Below this a musical section is folded into its neighbour rather than "
                    "given a cut of its own: a one-second shot reads as a glitch.");
            // Disabled in Song Mode rather than hidden: a control that vanishes reads as a missing
            // feature, and one that is live but read by nothing spends the user's trust -- which is
            // the rule `AutoDirectorSettings` states about its own three absent knobs. Song Mode has
            // no "build": it has an authored section carrying a cut rate, and the floor it runs into
            // is `shortest shot`.
            ImGui::BeginDisabled(song);
            seconds("shortest build", s.minBuildShotSeconds, 0.5, 10.0,
                    "A build is exempt from the minimum, because a build exists to end. This is "
                    "how short it may get.");
            ImGui::EndDisabled();
            if (song && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                tooltip("Song Mode has no builds to exempt: a section's own cut rate sets "
                                  "its shot length, and 'shortest shot' is the floor it stops at.");
            }
            seconds("longest shot", s.maxShotSeconds, 4.0, 60.0,
                    "A passage longer than this becomes several shots inside one section, each "
                    "cast separately -- so a thirty-second verse is the camera travelling "
                    "between subjects rather than holding one for a third of the piece.");

            ImGui::Separator();
            ImGui::TextUnformatted("Pace");
            // First in this block, because it is the one that actually lowers the floor the two
            // caps below run into. Measured on Glowmere with the caps at 0.4 m/s and 2 deg/s:
            // holding each subject for one shot peaks at 42.3 m/s, and for six shots at 0.8 m/s.
            int dwell = s.dwellShots;
            // Also inert in Song Mode, and for a better reason than the one above: how long the
            // film stays with one subject is a property of the *section* there, derived from its
            // intent's `variation`, so a single film-wide number would be a second answer to a
            // question the plan already answers per section.
            ImGui::BeginDisabled(song);
            if (ImGui::SliderInt("hold subject", &dwell, 1, 12,
                                 dwell == 1 ? "1 shot" : "%d shots")) {
                s.dwellShots = std::clamp(dwell, 1, 12);
            }
            ImGui::EndDisabled();
            if (song && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                tooltip("In Song Mode each section decides this for itself, from its shot "
                                  "intent's variation: a section that wants one setup held keeps its "
                                  "subject, and one that wants coverage moves on every shot.");
            }
            if (ImGui::IsItemHovered()) {
                tooltip(
                    "How many shots in a row one subject keeps before the film moves on.\n\n"
                    "Importance decides how often a subject's turn comes round; this decides\n"
                    "how long a turn lasts. They are two questions, and with a cast of eleven\n"
                    "no importance setting could answer the second one -- every turn was a\n"
                    "single shot however high the slider went.\n\n"
                    "It is also the control that makes a slow camera possible at all. In a\n"
                    "continuous take the camera has to physically cross the ground between one\n"
                    "subject and the next inside the time the music gave the shot, and that\n"
                    "distance over that duration is a floor the two caps below cannot go under.\n"
                    "Consecutive shots on the same subject have no ground to cross.\n\n"
                    "Measured on Glowmere at 0.4 m/s and 2 deg/s: 1 shot per subject peaks at\n"
                    "42.3 m/s, 6 shots per subject at 0.8 m/s.");
            }
            // ADR-200. Off is a real value here, not a disabled control: 0 means the director's own
            // geometry stands, and that is the right default for a scene nobody has complained
            // about. The format says so rather than showing a bare 0.
            ImGui::SliderFloat("max speed", &s.maxCameraSpeed, 0.0f, 120.0f,
                               s.maxCameraSpeed > 0.0f ? "%.1f m/s" : "off");
            if (ImGui::IsItemHovered()) {
                tooltip(
                    "The fastest the camera may travel. Off leaves the cut exactly as the\n"
                    "director built it.\n\n"
                    "What gives way is the distance, never the timing: every cut here lands on\n"
                    "the music, so stretching a shot to slow it would move every cut after it\n"
                    "off the beat it was built for. A shot that is too fast is one covering too\n"
                    "much ground for the time the music gave it, so the ground is what shrinks --\n"
                    "the move starts where it did and simply does not go as far.\n\n"
                    "A continuous cut re-chains afterwards, so the shots still join.");
            }

            ImGui::SliderFloat("max swing", &s.maxViewRate, 0.0f, 180.0f,
                               s.maxViewRate > 0.0f ? "%.0f deg/s" : "off");
            if (ImGui::IsItemHovered()) {
                tooltip(
                    "The fastest the view may swing. This is usually the one you want.\n\n"
                    "Measured on a reference cut: capping the camera to 1 m/s took its travel\n"
                    "from 23.2 down to 1.0 and left the view rotating at 63.7 deg/s -- faster\n"
                    "than the 52.0 it started at, because a camera that moves less still has to\n"
                    "sweep its aim the same distance in the same time. What reads as 'too fast'\n"
                    "is nearly always the swing, not the travel.\n\n"
                    "Widens each handoff's swing rather than shortening it, then shortens the\n"
                    "move for anything still over: the shot arrives where it was going, at the\n"
                    "same moment, having taken longer over the turn.\n\n"
                    "It will not always reach the number you set. A shot that hands one subject\n"
                    "over to another has to complete that turn inside its own duration, and the\n"
                    "angle between two subjects over the time the music gave the shot is a floor\n"
                    "nothing here can go under without moving the cut.");
            }

            // ADR-217. A list of the scenarios this scene actually stages, plus "off" -- rather
            // than a text box, because a scenario name that does not exist holds nothing and the
            // panel should not be able to ask for that.
            ImGui::Separator();
            auto seed = static_cast<int>(s.seed);
            if (ImGui::InputInt("seed", &seed)) {
                s.seed = static_cast<std::uint32_t>(std::max(0, seed));
            }
            if (ImGui::IsItemHovered()) {
                tooltip(
                    song ? "Same seed, same heroes, same plan, same world, same film. Change it "
                           "to ask for a different edit of the same piece -- in Song Mode it "
                           "picks the cameras, the subjects and the framing inside every "
                           "section."
                         : "Same seed, same heroes, same track, same film. Change it to "
                           "ask for a different edit of the same piece -- it picks which "
                           "supporting subject each section gets, and nothing else.");
            }

            if (!directorSummary.empty()) {
                ImGui::Separator();
                ImGui::TextWrapped("%s", directorSummary.c_str());
                if (ImGui::IsItemHovered()) {
                    tooltip(
                        "What the cut this panel produced actually does, rather than what was\n"
                        "asked of it.\n\n"
                        "The two caps are requests, and a continuous take can refuse them: the\n"
                        "camera has to get from one subject's stand-off point to the next inside\n"
                        "the time the music gave the shot. Hold each subject for more shots, or\n"
                        "give the shots longer, to lower that floor.");
                }
            }

            // Refuse rather than clamp, and say why in the panel rather than in a log.
            if (const auto ok = s.validate(); !ok) {
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.35f, 1.0f), "%s", ok.error().message.c_str());
            // `operator==` rather than a memcmp: the struct has padding, and since ADR-217 it has
            // std::strings in it, so comparing its bytes is both undefined and wrong.
            } else if (!(before == s) && onDirectCamera &&
                       engine.timeline().isAutomated("camera/position") &&
                       !engine.directorParked()) {
                // Already directed: a setting that changes the film should change the film,
                // rather than waiting for somebody to find the menu item again.
                onDirectCamera();
            }
}

} // namespace avgen::ui
