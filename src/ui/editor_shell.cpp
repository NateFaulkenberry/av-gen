#include "ui/editor_shell.hpp"

#include "core/log.hpp"

#include <imgui_internal.h>

namespace avgen::ui {

namespace {

// The centre belongs to the canvas window and to nothing else. The flag has to be repeated when
// the node is rebuilt: DockBuilderAddNode() starts from nothing.
//
// There is deliberately no ImGuiDockNodeFlags_PassthruCentralNode. That flag makes the dockspace's
// host window transparent so a full-window render shows through it, which is the arrangement this
// shell exists to end: the panels around the edge are then chrome painted *over* the world rather
// than beside it, and with the default theme's 94%-opaque window background the world bleeds
// through them.
constexpr ImGuiDockNodeFlags kDockspaceFlags = ImGuiDockNodeFlags_NoDockingOverCentralNode;

} // namespace

ImGuiID beginEditorDockspace() {
    return ImGui::DockSpaceOverViewport(0, nullptr, kDockspaceFlags);
}

bool dockspaceIsEmpty(ImGuiID dockspace) {
    const ImGuiDockNode* node = ImGui::DockBuilderGetNode(dockspace);
    return node == nullptr || (node->IsLeafNode() && node->Windows.Size == 0);
}

void buildDefaultDockLayout(ImGuiID dockspace, EditorLayout& layout) {
    const LayoutRatios ratios = defaultLayoutRatios();

    ImGui::DockBuilderRemoveNode(dockspace);
    ImGui::DockBuilderAddNode(dockspace, kDockspaceFlags | ImGuiDockNodeFlags_DockSpace);
    // Splits divide the node's current size, so the size has to be right before the first one or
    // the ratios come out of a node that is still zero by zero.
    ImGui::DockBuilderSetNodeSize(dockspace, ImGui::GetMainViewport()->WorkSize);

    ImGuiID centre = dockspace;
    ImGuiID bottom = 0;
    ImGuiID left = 0;
    ImGuiID right = 0;
    // The foot first, then the two columns out of what is left above it. The other order gives a
    // bottom strip that runs the full width *under* the columns, which reads as a third column
    // lying on its side rather than as a transport bar.
    ImGui::DockBuilderSplitNode(centre, ImGuiDir_Down, ratios.bottom, &bottom, &centre);
    ImGui::DockBuilderSplitNode(centre, ImGuiDir_Left, splitRatioWithin(ratios.left, 1.0f), &left, &centre);
    ImGui::DockBuilderSplitNode(centre, ImGuiDir_Right, splitRatioWithin(ratios.right, 1.0f - ratios.left),
                                &right, &centre);

    // The canvas first, so the central node has its occupant before anything else is placed.
    ImGui::DockBuilderDockWindow(kCanvasWindow.data(), centre);
    if (ImGuiDockNode* node = ImGui::DockBuilderGetNode(centre); node != nullptr) {
        // No tab bar over the world, and nothing else may be dropped in beside it. Both are saved
        // with the node, so they survive a restart rather than having to be reapplied each run.
        node->SetLocalFlags(node->LocalFlags | ImGuiDockNodeFlags_NoTabBar |
                            ImGuiDockNodeFlags_NoDockingOverMe);
    }

    for (const EditorPanel& panel : editorPanels()) {
        ImGuiID node = 0;
        switch (panel.region) {
        case DockRegion::Left:
            node = left;
            break;
        case DockRegion::Right:
            node = right;
            break;
        case DockRegion::Bottom:
            node = bottom;
            break;
        case DockRegion::Centre:
        case DockRegion::Floating:
            // Nothing but the canvas is docked into the centre, whatever the registry says. A panel
            // that asked for it floats instead, which is visible and recoverable, where a canvas
            // quietly covered by a panel is neither.
            break;
        }
        if (node != 0) {
            ImGui::DockBuilderDockWindow(panel.id.data(), node);
        }
    }
    ImGui::DockBuilderFinish(dockspace);

    layout.setRegionNode(DockRegion::Left, left);
    layout.setRegionNode(DockRegion::Right, right);
    layout.setRegionNode(DockRegion::Bottom, bottom);
    layout.setRegionNode(DockRegion::Centre, centre);
    log::info("editor: default layout rebuilt (left {:.0f}%, right {:.0f}%, foot {:.0f}%)",
              static_cast<double>(ratios.left) * 100.0, static_cast<double>(ratios.right) * 100.0,
              static_cast<double>(ratios.bottom) * 100.0);
}

namespace {

// Where a panel that has strayed into the centre should go back to.
//
// `EditorLayout`'s region nodes are only filled in when the default layout is *built*, so on a run
// that loaded a saved layout they are all zero and cannot be used. The saved layout does know the
// answer, though, indirectly: whichever node this panel's registered neighbours are sitting in is
// the region, whatever id it happens to have this run. Falling back to that is what makes the
// repair work on exactly the layouts that need repairing.
ImGuiID homeNodeFor(DockRegion region, const EditorLayout& layout, const ImGuiDockNode* centre) {
    if (const std::uint32_t known = layout.regionNode(region); known != 0) {
        return static_cast<ImGuiID>(known);
    }
    for (const EditorPanel& sibling : editorPanels()) {
        if (sibling.region != region) {
            continue;
        }
        const ImGuiWindow* window = ImGui::FindWindowByName(sibling.id.data());
        if (window == nullptr || window->DockId == 0) {
            continue;
        }
        if (centre != nullptr && window->DockId == centre->ID) {
            continue;   // also stranded; it is not evidence of where the region is
        }
        return window->DockId;
    }
    return 0;
}

} // namespace

std::size_t enforceCanvasCentre(ImGuiID dockspace, const EditorLayout& layout) {
    ImGuiDockNode* centre = ImGui::DockBuilderGetCentralNode(dockspace);
    if (centre == nullptr) {
        return 0;
    }
    // Reapplied every run rather than once at build: `NoDockingOverMe` is not written to the .ini,
    // so without this the rule exists only until the first restart.
    centre->SetLocalFlags(centre->LocalFlags | ImGuiDockNodeFlags_NoTabBar |
                          ImGuiDockNodeFlags_NoDockingOverMe);

    std::size_t evicted = 0;
    // Backwards, because docking a window elsewhere removes it from this node's list.
    for (int i = centre->Windows.Size - 1; i >= 0; --i) {
        const ImGuiWindow* window = centre->Windows[i];
        if (window == nullptr || window->Name == nullptr) {
            continue;
        }
        const std::string_view name(window->Name);
        if (name == kCanvasWindow) {
            continue;
        }
        DockRegion region = DockRegion::Floating;
        if (const EditorPanel* panel = findEditorPanel(name); panel != nullptr) {
            region = panel->region;
        }
        // A panel with no registered home, or one whose region cannot be located, is floated --
        // visible and recoverable, where leaving it in the centre is neither.
        const ImGuiID home = homeNodeFor(region, layout, centre);
        ImGui::DockBuilderDockWindow(window->Name, home);
        log::warn("editor: '{}' was sharing the canvas's centre node and has been moved {}", name,
                  home != 0 ? "back beside its neighbours" : "out to a floating window");
        ++evicted;
    }
    // A panel that has not been shown yet has no window, so it is not in the node's list above --
    // it exists only as the settings the .ini was parsed into. Left alone, it would appear in the
    // centre for the one frame between being opened and being evicted, which is a flash of a panel
    // over the world every time a hidden panel is first shown.
    if (ImGuiContext* g = ImGui::GetCurrentContext(); g != nullptr) {
        for (ImGuiWindowSettings* settings = g->SettingsWindows.begin(); settings != nullptr;
             settings = g->SettingsWindows.next_chunk(settings)) {
            if (settings->DockId != centre->ID) {
                continue;
            }
            const char* name = settings->GetName();
            if (name == nullptr || std::string_view(name) == kCanvasWindow) {
                continue;
            }
            if (ImGui::FindWindowByName(name) != nullptr) {
                continue;   // live, and already handled above
            }
            DockRegion region = DockRegion::Floating;
            if (const EditorPanel* panel = findEditorPanel(name); panel != nullptr) {
                region = panel->region;
            }
            settings->DockId = homeNodeFor(region, layout, centre);
            log::warn("editor: '{}' was recorded in the canvas's centre node and has been moved {}",
                      name, settings->DockId != 0 ? "back beside its neighbours" : "out to float");
            ++evicted;
        }
    }

    if (evicted > 0) {
        // Dock positions live in ImGui's own .ini rather than in `EditorLayout`, so the repair is
        // persisted by marking that dirty. Without it the broken layout stays on disk and the fix
        // is redone on every launch -- which would work, but would also mean the warning above is
        // printed for ever.
        ImGui::MarkIniSettingsDirty();
    }
    return evicted;
}

CanvasRect drawCanvasWindow(std::uint64_t texture, std::uint32_t centreNode) {
    CanvasRect rect;
    if (centreNode != 0) {
        ImGui::SetNextWindowDockID(centreNode, ImGuiCond_FirstUseEver);
    }
    // No padding and no scrolling: the image is the window, edge to edge. Padding here would show
    // as a band of panel colour around the world that resizes with the theme.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const bool open = ImGui::Begin(kCanvasWindow.data(), nullptr,
                                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                                       ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoNavFocus);
    ImGui::PopStyleVar();
    if (open) {
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 size = ImGui::GetContentRegionAvail();
        rect.x = origin.x;
        rect.y = origin.y;
        rect.width = size.x;
        rect.height = size.y;
        if (texture != 0 && rect.valid()) {
            ImGui::Image(static_cast<ImTextureID>(texture), size);
        }
        // What decides whether a mouse event is the scene's. Asked here, while the canvas is the
        // current window, so that a panel, a popup or a menu over the canvas answers false.
        rect.hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
    }
    ImGui::End();
    return rect;
}

bool beginStatusBar(const char* name) {
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_MenuBar;
    // The bar's contents sit in a menu bar inside it, which is what makes a single line of text
    // vertically centred in a window the height of one frame.
    if (ImGui::BeginViewportSideBar(name, ImGui::GetMainViewport(), ImGuiDir_Down,
                                    ImGui::GetFrameHeight(), flags)) {
        if (ImGui::BeginMenuBar()) {
            return true;
        }
    }
    ImGui::End();
    return false;
}

void endStatusBar() {
    ImGui::EndMenuBar();
    ImGui::End();
}

} // namespace avgen::ui
