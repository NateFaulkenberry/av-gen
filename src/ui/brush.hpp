#pragma once

// The asset brush and the live ghost under the cursor (ADR-092, world-authoring-spec §18-§22).
//
// The requirement this exists for is one sentence: **the artist should never guess where an asset
// will land.** Everything here follows from taking that literally. Before the click, the editor
// knows the exact position, orientation and scale of every instance the click would create, the
// footprint each one covers, the box each one occupies, and -- for every one of them separately --
// whether it may be placed there and, if not, which constraint said no and by how much.
//
// So the preview is not a decoration drawn near the cursor. It is the *plan*, computed once, shown,
// and then committed unchanged when the button goes down. `makeNodes(preview)` turns the very
// instances that were on screen into scene nodes; there is no second layout pass that could
// disagree with the first. A ghost that is computed differently from the placement is a ghost that
// lies, and it lies rarely enough to be believed.
//
// ImGui-free; see tests/unit/test_brush.cpp.

#include "app/placement.hpp"
#include "assets/asset_library.hpp"
#include "scene/composition.hpp"
#include "ui/world_probe.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <optional>
#include <string>
#include <vector>

namespace avgen::ui {

// Why one instance may not be placed. Ordered by how much the artist needs to know about it: the
// later ones override the earlier when several apply, because "there is nothing under the cursor"
// is a more useful thing to be told than "that would be in water".
enum class PlacementIssue : std::uint8_t {
    None,
    Collides,      // something is already there
    TooSteep,      // the ground is a cliff
    InWater,       // the ground here is under the surface
    OutsideWorld,  // past the edge of the terrain
    NoSurface,     // the ray met no ground at all -- the sky, or past the far edge
    NoAsset,       // nothing is armed, or the armed asset is not in the library
};
[[nodiscard]] const char* placementIssueName(PlacementIssue issue);

// One thing the click would make.
struct GhostInstance {
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    float scale = 1.0f;           // multiplies the asset's normalising scale
    float footprintRadius = 0.5f; // metres, horizontal: the circle drawn on the ground
    float height = 1.0f;          // metres, as it will stand
    glm::vec3 groundNormal{0.0f, 1.0f, 0.0f};
    float slopeDegrees = 0.0f;
    PlacementIssue issue = PlacementIssue::None;
    // What it collided with, when it did. Named because "something is in the way" and "the elder
    // tree is in the way" are different amounts of help.
    std::string blockedBy;
    [[nodiscard]] bool valid() const { return issue == PlacementIssue::None; }
};

// Everything the viewport draws and the status line says, for one position of the cursor.
struct BrushPreview {
    bool armed = false;            // an asset is chosen
    std::string assetId;
    std::string assetName;
    app::PlacementMode mode = app::PlacementMode::Single;
    GroundSample ground;           // under the cursor itself
    float brushRadius = 0.0f;      // the disc drawn on the ground, 0 for a single placement
    std::vector<GhostInstance> instances;
    std::size_t validCount = 0;
    std::size_t blockedCount = 0;
    // The worst issue across the plan, and it in words with its numbers in it -- "too steep: 47
    // degrees, limit 30". A warning that does not say by how much is a warning you cannot act on.
    PlacementIssue issue = PlacementIssue::None;
    std::string reason;
    // Eraser / Replace: what this click would remove.
    std::vector<std::string> erasing;

    [[nodiscard]] bool placeable() const { return armed && validCount > 0; }
};

// Everything a brush needs to know that is not in `PlacementSettings`: which asset, and how big it
// is. Separated because the settings are what an artist edits and this is what the library says.
struct BrushAsset {
    const assets::AssetDescriptor* descriptor = nullptr;
    std::string file;              // resolved path for the node's `asset` field
    [[nodiscard]] bool ready() const { return descriptor != nullptr && !file.empty(); }
};

// Plans what a click at `cursor` would do, and checks every instance of it.
//
// `seed` is the stroke's seed. `PlacementSettings::seed` overrides it when non-zero, so a run can
// be made reproducible without the brush losing its variety by default.
[[nodiscard]] BrushPreview planBrush(scene::Composition& composition, const app::PlacementSettings& settings,
                                     const BrushAsset& asset, const GroundSample& cursor,
                                     std::uint32_t seed);

// Turns the valid instances of a plan into nodes, ready for `placeNodes`. Uses the preview's own
// positions and rotations rather than re-planning, which is the whole point: what was on screen is
// what is placed.
//
// `parent` puts them all under a group, for a brush stroke that should stay one thing.
[[nodiscard]] std::vector<scene::CompositionNode> makeNodes(const scene::Composition& composition,
                                                            const BrushPreview& preview,
                                                            const BrushAsset& asset,
                                                            const std::string& parent = {});

// A label for the status bar: "12 ferns · 3 blocked (in water)". The one line that has to be true.
[[nodiscard]] std::string previewSummary(const BrushPreview& preview);

} // namespace avgen::ui
