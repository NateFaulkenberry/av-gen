#pragma once

// The Edit panel (ADR-092, world-authoring-spec §19-§21, §23-§28, §42-§43).
//
// One panel for the whole editor, showing the controls for the mode it is in: the asset palette and
// the brush in Place, the selection and its transform in Select, and the history in both. §43's
// warning -- *do not turn Glowmere into a collection of engineering panels* -- is the reason this is
// one window with contextual contents rather than a Brush window, a Transform window and a History
// window that an artist has to keep arranged.
//
// It owns nothing. The editor's state is `ui::WorldEditor`; the library is the World Builder's; this
// is a view over both.

#include "app/engine.hpp"
#include "assets/asset_library.hpp"
#include "ui/world_editor.hpp"

#include <optional>
#include <set>
#include <string>

namespace avgen::ui {

class WorldEditPanel {
public:
    void draw(app::Engine& engine, WorldEditor& editor, const assets::AssetLibrary* library);

    // Set by the panel when the artist asks the viewport to look at what is selected; the host
    // clears it once it has. The panel does not own the camera.
    bool frameSelectionRequested = false;

private:
    void drawModeBar(app::Engine& engine, WorldEditor& editor);
    void drawPalette(WorldEditor& editor, const assets::AssetLibrary* library);
    void drawBrush(WorldEditor& editor);
    void drawSelection(app::Engine& engine, WorldEditor& editor);
    // The object list: every node in the scene, with the two switches an image editor puts beside a
    // layer -- an eye and a padlock. Here rather than in a window of its own because §43 asks for
    // one editor panel with contextual contents, and because what you lock is what you are about to
    // stop clicking on, which is a thing you do in the middle of selecting.
    void drawObjects(app::Engine& engine, WorldEditor& editor);
    // The settings behind an object's disclosure triangle: what it is worth to the camera director,
    // where on it the camera looks, and how far the camera stands off. All of it is hero state, so
    // all of it is inert until the object is starred.
    void drawObjectSettings(app::Engine& engine, WorldEditor& editor, const std::string& node);
    void drawHistory(app::Engine& engine, WorldEditor& editor);

    char filter_[64] = {};
    char objectFilter_[64] = {};
    int categoryFilter_ = 0;
    // Which rows are expanded, by node name. A set rather than a flag on the node: this is how the
    // panel is being *looked at*, not something about the scene, and it must not make a document
    // modified or end up in a file.
    std::set<std::string> expanded_;
    // The hero as it was when a drag began, so the whole drag is one undo step rather than sixty.
    // `ImGui::IsItemActivated` opens it and `IsItemDeactivatedAfterEdit` closes it, which is the
    // same shape as the gizmo's drag coalescing.
    std::optional<world::HeroPoint> heroBeforeDrag_;
};

} // namespace avgen::ui
