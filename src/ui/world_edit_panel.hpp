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
    void drawHistory(app::Engine& engine, WorldEditor& editor);

    char filter_[64] = {};
    int categoryFilter_ = 0;
    bool showHistory_ = false;
};

} // namespace avgen::ui
