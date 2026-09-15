#pragma once

// The editor shell's inventory (ADR-076): which panels exist, which region of the dockspace the
// default layout puts each one in, and which of them are open. No ImGui here on purpose -- the
// rules about where things go are the part that goes quietly wrong, and a rule that needs a window
// and a GPU to check is a rule nobody checks. The ImGui half is in editor_shell.hpp.

#include "core/error.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::ui {

// Where the default layout docks a panel. Centre is the world itself: the canvas window is the only
// thing that ever goes there, and the enumerator exists so that a panel wanting to share it would
// have to say so in the registry where it can be seen -- and where a test will catch it.
enum class DockRegion { Left, Right, Bottom, Centre, Floating };

// The canvas is a window like any other as far as ImGui is concerned, but it is not a panel: it
// cannot be closed, it has no menu entry, and it is docked into the central node.
inline constexpr std::string_view kCanvasWindow = "Viewport";

// The rectangle the world is drawn into, in ImGui points relative to the main viewport -- which is
// the window's client area, because multi-viewport is off, so these are directly comparable with
// the positions SDL reports for a mouse event.
//
// Reported by the canvas window as it is laid out, and read by the host at the top of the *next*
// frame: how big the canvas is can only be known once ImGui has placed it, and the renderer needs
// the size before anything is drawn. One frame of lag on a resize; nothing on a still window.
struct CanvasRect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    // The pointer is over the canvas and no panel, popup or menu is above it. This, and not
    // ImGui's WantCaptureMouse, is what decides whether a mouse event belongs to the scene: the
    // canvas is an ImGui window now, so WantCaptureMouse is true whenever the pointer is on the
    // world.
    bool hovered = false;

    [[nodiscard]] bool valid() const { return width >= 1.0f && height >= 1.0f; }
    // Where a point in window coordinates falls inside the canvas, in canvas points.
    [[nodiscard]] float localX(float windowX) const { return windowX - x; }
    [[nodiscard]] float localY(float windowY) const { return windowY - y; }
    [[nodiscard]] bool contains(float windowX, float windowY) const {
        return windowX >= x && windowY >= y && windowX < x + width && windowY < y + height;
    }
};

// Where a mouse position in window points lands in the render target, in pixels. Two conversions
// in one place because they are always both needed and one of them is always the one forgotten:
// the canvas origin is subtracted first, then points are scaled to framebuffer pixels. Clamped to
// the target, so a drag released outside the canvas still names a pixel inside it.
//
// An invalid canvas means the world is still being drawn to the whole window (the first frame of a
// run), and the position is taken as-is.
struct CanvasPixel {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
};
[[nodiscard]] CanvasPixel canvasPixelFor(const CanvasRect& canvas, float windowX, float windowY, float scale,
                                         std::uint32_t pixelWidth, std::uint32_t pixelHeight);

[[nodiscard]] const char* dockRegionName(DockRegion region);

struct EditorPanel {
    // The ImGui window title, the key in the layout file and the name the dock builder docks by.
    // One string for all three, because a mapping table between them is a thing that drifts.
    // Always a literal, so id.data() is a usable C string for the ImGui calls that want one.
    std::string_view id;
    std::string_view label; // what the View menu calls it
    DockRegion region = DockRegion::Floating;
    bool openByDefault = false;
    std::string_view hint; // one line of tooltip in the View menu; may be empty
};

// Every panel the shell knows about, in menu order.
[[nodiscard]] std::span<const EditorPanel> editorPanels();
[[nodiscard]] const EditorPanel* findEditorPanel(std::string_view id);

// How much of the dockspace each side region takes the first time the layout is built. The centre
// gets what is left, and gets to be the largest thing on screen: the world is the canvas.
struct LayoutRatios {
    float left = 0.19f;
    float right = 0.21f;
    // The bottom region has to hold the Sequence panel, and that is what sizes it: a transport bar,
    // a two-row toolbar, the strip, and an inspector under it.
    //
    // At 0.26 it did not. Measured with `--ui-script strip` on a 1440x900-point window, the strip
    // laid out at y = 890 in a 900-point window -- ten points of a 170-point strip on screen, and
    // the rest below the fold. The sequencer was unusable at the size the editor opens at until
    // somebody dragged the divider, and nothing said so; the scripted arm found it on its first
    // run by reporting a scrub that had gone to 0.00 s because the click landed outside the window.
    //
    // 0.32 fits the strip for a piece with audio, sections, shots and two actors, which is the
    // shape of an actual project rather than an empty one. The canvas keeps 68% of the height and
    // remains much the largest thing on screen, which is the rule the comment above states.
    float bottom = 0.32f;

    [[nodiscard]] float centreWidth() const { return 1.0f - left - right; }
    [[nodiscard]] float centreHeight() const { return 1.0f - bottom; }
};

[[nodiscard]] LayoutRatios defaultLayoutRatios();

// A dock split cuts the node that is left over, not the original one, so the second and third
// columns come out narrower than the numbers above unless the ratio is restated against what
// actually remains. Asking for 0.21 of the window after a 0.19 slice has gone would yield 0.17.
[[nodiscard]] float splitRatioWithin(float fractionOfWhole, float remainingFractionOfWhole);

// Which panels are open, and the dock nodes the default layout built. Persisted beside the
// recent-files list; see ADR-076 for why this is ours and the dock tree itself is ImGui's.
class EditorLayout {
public:
    EditorLayout();

    // Every panel back to its registry default, and the remembered dock nodes forgotten -- the
    // caller rebuilds the tree, which is what mints new ones.
    void restoreDefaults();

    [[nodiscard]] bool visible(std::string_view id) const;
    void setVisible(std::string_view id, bool on);
    // The address of the flag, for ImGui to write through: a menu item toggles it and a window's
    // close box clears it. Null for an unknown id. Stable for the lifetime of this object.
    [[nodiscard]] bool* slot(std::string_view id);

    // The dock node the default layout made for a region, so a panel opened later can be dropped
    // into its own side of the screen rather than floating over the world. Zero means unknown,
    // which is what a layout restored from an older file or a hand-edited dock tree looks like.
    [[nodiscard]] std::uint32_t regionNode(DockRegion region) const;
    void setRegionNode(DockRegion region, std::uint32_t node);

    // A missing file is not an error (first run). A malformed one is, and leaves the current state
    // untouched. Panels named in the file that this build has never heard of are ignored rather
    // than rejected, so a layout written by a newer build still opens here.
    [[nodiscard]] Result<void> load(const std::filesystem::path& file);
    [[nodiscard]] Result<void> save(const std::filesystem::path& file) const;

    // Changes by ImGui go straight through slot(), so there is no hook to set a dirty bit in.
    // The caller compares this between frames instead and writes when it moves.
    [[nodiscard]] std::uint64_t signature() const;

    [[nodiscard]] std::size_t size() const { return flags_.size(); }

    // Reopens one panel in any side region that has none, and returns how many it had to reopen.
    //
    // This is the invariant that makes the world a canvas rather than a backdrop: an empty dock
    // node is collapsed by ImGui and the centre expands into the space, so closing the last panel
    // on the right makes the world span to the window edge. That is correct docking behaviour and
    // wrong product behaviour -- the brief asks for space on both sides and under the canvas, full
    // stop, not "unless you closed something".
    //
    // Enforced on load rather than continuously, so a region can still be emptied during a session
    // by someone who wants that; it comes back next launch. The panel chosen is the first the
    // registry lists for the region, which is the one the default layout would have opened.
    std::size_t ensureRegionsOccupied();

private:
    struct Flag {
        std::string_view id;
        bool on = false;
    };

    [[nodiscard]] const Flag* find(std::string_view id) const;
    [[nodiscard]] Flag* find(std::string_view id);

    // One entry per registry panel, in registry order. Sized once in the constructor and never
    // resized, because slot() hands out pointers into it.
    std::vector<Flag> flags_;
    std::uint32_t regionNodes_[5] = {0, 0, 0, 0, 0};
};

} // namespace avgen::ui
