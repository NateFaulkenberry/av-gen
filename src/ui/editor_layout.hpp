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

// Where the default layout docks a panel. Centre is the world itself: nothing is ever docked there,
// and the enumerator exists so that a panel wanting to cover the canvas would have to say so in the
// registry where it can be seen -- and where a test will catch it.
enum class DockRegion { Left, Right, Bottom, Centre, Floating };

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
    float bottom = 0.26f;

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
