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
