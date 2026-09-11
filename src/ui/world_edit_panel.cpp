#include "ui/world_edit_panel.hpp"

#include "scene/composition.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstdio>

namespace avgen::ui {
namespace {

// A swatch standing in for a thumbnail (§19: "an artist palette, not a filename list"). A rendered
// thumbnail per asset would be the real answer and needs an offscreen pass this editor does not own
// this pass; a shape and a colour derived from the asset's own category, height and emission is not
// a picture of the asset, but it does sort the palette visually and it does not pretend to be more
// than it is.
ImU32 swatchColour(const assets::AssetDescriptor& asset) {
    switch (asset.category) {
    case assets::AssetCategory::Flora:
        return IM_COL32(96, 170, 108, 255);
    case assets::AssetCategory::Fungi:
        return IM_COL32(178, 126, 196, 255);
    case assets::AssetCategory::Rock:
        return IM_COL32(140, 140, 146, 255);
    case assets::AssetCategory::Crystal:
        return IM_COL32(110, 196, 220, 255);
    case assets::AssetCategory::Creature:
        return IM_COL32(224, 158, 96, 255);
    case assets::AssetCategory::Structure:
    case assets::AssetCategory::Architectural:
        return IM_COL32(176, 152, 112, 255);
    case assets::AssetCategory::Water:
        return IM_COL32(84, 132, 200, 255);
    default:
        return IM_COL32(150, 154, 160, 255);
    }
}

// The swatch: a bar whose height is the asset's real height against the tallest in view, so a
// palette shows scale at a glance. That is the number a placement gets wrong most often.
void drawSwatch(const assets::AssetDescriptor& asset, float tallest, float side) {
    ImDrawList* list = ImGui::GetWindowDrawList();
    const ImVec2 lo = ImGui::GetCursorScreenPos();
    const ImVec2 hi(lo.x + side, lo.y + side);
    list->AddRectFilled(lo, hi, IM_COL32(30, 33, 38, 255), 3.0f);
    const float fraction =
        tallest > 0.01f ? std::clamp(asset.effectiveHeight() / tallest, 0.06f, 1.0f) : 0.5f;
    const float barTop = hi.y - (side - 6.0f) * fraction - 3.0f;
    list->AddRectFilled(ImVec2(lo.x + side * 0.32f, barTop), ImVec2(hi.x - side * 0.32f, hi.y - 3.0f),
                        swatchColour(asset), 1.5f);
    if (asset.material.emissive > 0.01f) {
        // A ring for anything that glows, because in this world that is the single most important
        // fact about a species and it is invisible in a name.
        list->AddCircle(ImVec2((lo.x + hi.x) * 0.5f, lo.y + side * 0.22f), side * 0.12f,
                        IM_COL32(255, 236, 170, 220), 0, 1.6f);
    }
    list->AddRect(lo, hi, IM_COL32(70, 74, 82, 255), 3.0f);
    ImGui::Dummy(ImVec2(side, side));
}

void helpMarker(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", text);
    }
}

} // namespace

void WorldEditPanel::draw(app::Engine& engine, WorldEditor& editor, const assets::AssetLibrary* library) {
    drawModeBar(engine, editor);
    ImGui::Separator();
    if (editor.mode == EditorMode::Place) {
        drawPalette(editor, library);
        ImGui::Separator();
        drawBrush(editor);
    } else {
        drawSelection(engine, editor);
    }
    ImGui::Separator();
    drawHistory(engine, editor);
}

void WorldEditPanel::drawModeBar(app::Engine& engine, WorldEditor& editor) {
    // Two buttons the size of the panel, always in the same place, always saying which one is on.
    // §43's "visible state": the mode is the thing that changes what every click does, so it cannot
    // be a line of text somewhere.
    const float width = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    const auto modeButton = [&](const char* label, EditorMode want, const char* shortcut) {
        const bool on = editor.mode == want;
        if (on) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.46f, 0.30f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.26f, 0.52f, 0.34f, 1.0f));
        }
        if (ImGui::Button(label, ImVec2(width, 0.0f))) {
            editor.mode = want;
        }
        if (on) {
            ImGui::PopStyleColor(2);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", shortcut);
        }
    };
    modeButton("Select", EditorMode::Select, "Q -- click objects, move them, group them");
    ImGui::SameLine();
    modeButton("Place", EditorMode::Place, "B -- paint assets into the world");

    if (editor.mode == EditorMode::Select) {
        int gizmo = static_cast<int>(editor.gizmoMode);
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo("##gizmo", &gizmo, "Move (W)\0Rotate (E)\0Scale (R)\0")) {
            editor.gizmoMode = static_cast<GizmoMode>(gizmo);
        }
        ImGui::Checkbox("Local space", &editor.localSpace);
        helpMarker("Move along the object's own axes rather than the world's. X toggles it.");
        ImGui::SetNextItemWidth(70.0f);
        ImGui::DragFloat("Grid", &editor.snap.move, 0.05f, 0.0f, 50.0f, "%.2f m");
        helpMarker("0 is off. With a grid set, a move lands on multiples of it.");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70.0f);
        ImGui::DragFloat("Angle", &editor.snap.rotate, 1.0f, 0.0f, 90.0f, "%.0f deg");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70.0f);
        ImGui::DragFloat("Step", &editor.snap.scale, 0.01f, 0.0f, 1.0f, "%.2fx");
    }
    static_cast<void>(engine);
}

void WorldEditPanel::drawPalette(WorldEditor& editor, const assets::AssetLibrary* library) {
    if (library == nullptr || library->size() == 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "No asset library loaded.");
        ImGui::TextWrapped("The World Builder finds one at start-up; without a manifest there is "
                           "nothing to paint with.");
        return;
    }

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##filter", "filter the palette", filter_, sizeof(filter_));
    const std::string filter = filter_;

    // Category chips rather than a dropdown: the categories are the way an artist thinks about this
    // library ("show me the fungi"), and a dropdown hides them behind a click.
    const std::vector<assets::AssetCategory> categories = assets::allAssetCategories();
    if (ImGui::SmallButton("All")) {
        categoryFilter_ = 0;
    }
    for (std::size_t i = 0; i < categories.size(); ++i) {
        const int id = static_cast<int>(i) + 1;
        const bool on = categoryFilter_ == id;
        ImGui::SameLine();
        if (on) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.28f, 0.42f, 0.56f, 1.0f));
        }
        if (ImGui::SmallButton(assets::assetCategoryName(categories[i]))) {
            categoryFilter_ = on ? 0 : id;
        }
        if (on) {
            ImGui::PopStyleColor();
        }
        if (ImGui::GetContentRegionAvail().x < 90.0f) {
            ImGui::NewLine();
        }
    }

    // The tallest thing in view, so the swatch bars mean something relative to each other.
    float tallest = 0.01f;
    for (const auto& asset : library->assets()) {
        tallest = std::max(tallest, asset.effectiveHeight());
    }

    const float side = 46.0f;
    ImGui::BeginChild("##palette", ImVec2(0.0f, 200.0f), ImGuiChildFlags_Borders);
    const float available = ImGui::GetContentRegionAvail().x;
    const auto perRow = std::max(1, static_cast<int>(available / (side + ImGui::GetStyle().ItemSpacing.x)));
    int column = 0;
    for (const auto& asset : library->assets()) {
        if (categoryFilter_ != 0 &&
            asset.category != categories[static_cast<std::size_t>(categoryFilter_ - 1)]) {
            continue;
        }
        if (!filter.empty() && asset.id.find(filter) == std::string::npos &&
            asset.name.find(filter) == std::string::npos) {
            continue;
        }
        ImGui::PushID(asset.id.c_str());
        const bool armed = asset.id == editor.brushAssetId;
        const ImVec2 start = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##pick", ImVec2(side, side))) {
            editor.brushAssetId = armed ? std::string() : asset.id;
            editor.mode = EditorMode::Place;
        }
        const bool hovered = ImGui::IsItemHovered();
        ImGui::SetCursorScreenPos(start);
        drawSwatch(asset, tallest, side);
        if (armed || hovered) {
            ImGui::GetWindowDrawList()->AddRect(start, ImVec2(start.x + side, start.y + side),
                                                armed ? IM_COL32(255, 214, 96, 255)
                                                      : IM_COL32(170, 178, 190, 200),
                                                3.0f, 0, armed ? 2.5f : 1.5f);
        }
        if (hovered) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(asset.name.empty() ? asset.id.c_str() : asset.name.c_str());
            ImGui::Separator();
            ImGui::TextDisabled("%s", assets::assetCategoryName(asset.category));
            ImGui::Text("%.1f m tall, %.1f x %.1f m footprint",
                        static_cast<double>(asset.effectiveHeight()),
                        static_cast<double>(asset.naturalSize.x), static_cast<double>(asset.naturalSize.z));
            if (asset.triangles > 0) {
                ImGui::Text("%d triangles", asset.triangles);
            }
            ImGui::Text("importance %.2f", static_cast<double>(asset.visualImportance));
            if (asset.material.emissive > 0.01f) {
                ImGui::TextColored(ImVec4(1.0f, 0.92f, 0.66f, 1.0f), "glows (%.2f of its rung)",
                                   static_cast<double>(asset.material.emissive));
            }
            ImGui::EndTooltip();
        }
        ImGui::PopID();
        if (++column % perRow != 0) {
            ImGui::SameLine();
        }
    }
    ImGui::EndChild();

    if (editor.brushAssetId.empty()) {
        ImGui::TextDisabled("Pick something to paint with.");
    } else {
        ImGui::TextColored(ImVec4(0.55f, 0.88f, 0.62f, 1.0f), "Painting: %s", editor.brushAssetId.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear")) {
            editor.brushAssetId.clear();
        }
    }
}

void WorldEditPanel::drawBrush(WorldEditor& editor) {
    app::PlacementSettings& brush = editor.brush;
    int mode = static_cast<int>(brush.mode);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::Combo("##mode", &mode, "Single\0Scatter\0Cluster\0Landmark\0Eraser\0Replace\0")) {
        brush.mode = static_cast<app::PlacementMode>(mode);
    }
    switch (brush.mode) {
    case app::PlacementMode::Brush:
    case app::PlacementMode::Replace:
        ImGui::SliderFloat("Radius", &brush.brushRadius, 0.5f, 60.0f, "%.1f m");
        ImGui::SliderFloat("Spacing", &brush.spacing, 0.1f, 12.0f, "%.2f m");
        helpMarker("The closest two placements may ever be. Density thins the stroke without "
                   "changing this.");
        ImGui::SliderFloat("Density", &brush.density, 0.05f, 1.0f, "%.2f");
        helpMarker("How much of the room the spacing allows to actually use. It stops at 0.05 rather "
                   "than 0 because a brush that places nothing is a brush that looks broken.");
        break;
    case app::PlacementMode::Cluster:
        ImGui::SliderInt("Count", &brush.clusterCount, 1, 60);
        ImGui::SliderFloat("Spread", &brush.clusterRadius, 0.1f, 20.0f, "%.2f m");
        ImGui::SliderFloat("Clumping", &brush.clustering, 0.0f, 1.0f, "%.2f");
        helpMarker("0 fills the patch evenly; 1 piles it toward the middle.");
        break;
    case app::PlacementMode::Landmark:
        ImGui::SliderFloat("Times normal size", &brush.landmarkScale, 1.0f, 30.0f, "%.1fx");
        break;
    case app::PlacementMode::Eraser:
        ImGui::SliderFloat("Radius", &brush.brushRadius, 0.5f, 60.0f, "%.1f m");
        ImGui::TextDisabled("Drag over what you want gone. Undo brings it back.");
        break;
    case app::PlacementMode::Single:
        break;
    }

    if (brush.mode != app::PlacementMode::Eraser) {
        if (ImGui::TreeNodeEx("Variation", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::SliderFloat("Scale range", &brush.scaleJitter, 0.0f, 0.9f, "+/- %.2f");
            ImGui::SliderFloat("Random turn", &brush.yawJitter, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("Sink", &brush.sink, -2.0f, 2.0f, "%.2f m");
            helpMarker("Metres pushed into the ground. Negative lifts it clear.");
            ImGui::Checkbox("Lie along the slope", &brush.alignToNormal);
            helpMarker("Stand along the surface normal rather than upright. Right for rocks and "
                       "fallen logs, wrong for anything that grows toward the sky.");
            int seed = static_cast<int>(brush.seed);
            if (ImGui::InputInt("Seed", &seed)) {
                brush.seed = static_cast<std::uint32_t>(std::max(seed, 0));
            }
            helpMarker("0 means a new arrangement every stroke. Anything else repeats exactly.");
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Where it may land", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::SliderFloat("Max slope", &brush.maxSlopeDegrees, 0.0f, 90.0f, "%.0f deg");
            ImGui::Checkbox("Keep out of water", &brush.avoidWater);
            ImGui::Checkbox("Keep clear of other objects", &brush.avoidCollisions);
            if (brush.avoidCollisions) {
                ImGui::SliderFloat("Clearance", &brush.collisionPadding, 0.0f, 8.0f, "%.2f m");
            }
            ImGui::Checkbox("Nudge to the nearest valid spot", &brush.snapToValid);
            helpMarker("Instead of refusing, move a blocked instance to the nearest place the world "
                       "would accept. The ghost shows where it will actually go, so nothing is "
                       "placed anywhere you have not already seen it.");
            if (brush.snapToValid) {
                ImGui::SliderFloat("Look within", &brush.snapSearchRadius, 1.0f, 80.0f, "%.0f m");
            }
            ImGui::TreePop();
        }
        ImGui::Checkbox("A stroke makes a group", &editor.groupStrokes);
        helpMarker("Everything one drag lays down becomes a single group, so a thicket you painted "
                   "in one gesture is one thing to move.");
    }

    // What the ghost is currently saying, in the panel too: the viewport heads-up is where the eye
    // is, but the panel is where somebody looks when they are wondering why nothing is happening.
    const BrushPreview& preview = editor.preview();
    ImGui::Separator();
    if (!preview.reason.empty() && !preview.placeable()) {
        ImGui::TextColored(ImVec4(0.94f, 0.43f, 0.39f, 1.0f), "%s", preview.reason.c_str());
    } else {
        ImGui::TextDisabled("%s", previewSummary(preview).c_str());
    }
}

void WorldEditPanel::drawSelection(app::Engine& engine, WorldEditor& editor) {
    scene::Composition* composition = engine.composition();
    if (composition == nullptr) {
        ImGui::TextDisabled("no scene");
        return;
    }
    const Selection& selection = editor.selection;
    if (selection.empty()) {
        ImGui::TextDisabled("Nothing selected.");
        ImGui::TextWrapped("Click an object in the viewport. Shift-click adds; drag a box to catch "
                           "several; alt-click reaches inside a group.");
        return;
    }

    ImGui::Text("%zu selected", selection.size());
    ImGui::SameLine();
    if (ImGui::SmallButton("Frame")) {
        frameSelectionRequested = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear")) {
        editor.selection.clear();
    }

    if (ImGui::BeginListBox("##selected", ImVec2(-1.0f, 74.0f))) {
        // Over a copy. Clicking a row calls `Selection::set`, which clears and repopulates the very
        // vector a range-for would be walking -- and `clear()` keeps the capacity, so the loop's
        // cached end still points past the old size and the next row dereferences a destroyed
        // string. Two objects selected and one click is enough.
        const std::vector<std::string> rows = selection.nodes();
        for (const std::string& name : rows) {
            const scene::CompositionNode* node = composition->findNode(name);
            const bool active = name == selection.primary();
            ImGui::PushID(name.c_str());
            if (ImGui::Selectable(name.c_str(), active)) {
                editor.selection.set(name);
            }
            if (node != nullptr && node->kind == scene::NodeKind::Group) {
                ImGui::SameLine();
                ImGui::TextDisabled("(group)");
            }
            ImGui::PopID();
        }
        ImGui::EndListBox();
    }

    // Numeric transforms for the active object. Written through the editor so they are one undo
    // step each, exactly like a gizmo drag.
    const std::string& active = selection.primary();
    const scene::CompositionNode* node = composition->findNode(active);
    if (node != nullptr) {
        const auto read = [&](const char* field, glm::vec3 fallback) {
            const auto* p = engine.params().find("nodes/" + active + "/" + field);
            return p != nullptr ? glm::vec3(p->baseComponent(0), p->baseComponent(1), p->baseComponent(2))
                                : fallback;
        };
        glm::vec3 position = read("position", node->transform.position);
        glm::vec3 rotation = read("rotation", scene::eulerDegrees(node->transform.rotation));
        glm::vec3 scale = read("scale", node->transform.scale);
        ImGui::TextDisabled("%s", active.c_str());
        if (ImGui::DragFloat3("Position", &position.x, 0.05f, -1e4f, 1e4f, "%.3f")) {
            editor.setSelectionPosition(engine, position);
        }
        if (ImGui::DragFloat3("Rotation", &rotation.x, 0.5f, -360.0f, 360.0f, "%.1f")) {
            editor.setSelectionRotation(engine, rotation);
        }
        if (ImGui::DragFloat3("Scale", &scale.x, 0.01f, 0.001f, 100.0f, "%.3f")) {
            editor.setSelectionScale(engine, scale);
        }
        const scene::WorldBounds bounds = composition->nodeBounds(active);
        if (bounds.valid) {
            const glm::vec3 size = bounds.size();
            ImGui::TextDisabled("%.2f x %.2f x %.2f m", static_cast<double>(size.x),
                                static_cast<double>(size.y), static_cast<double>(size.z));
        }
        if (!node->parent.empty()) {
            ImGui::TextDisabled("in group '%s'", node->parent.c_str());
        }
    }

    ImGui::Separator();
    const float third = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2.0f) / 3.0f;
    if (ImGui::Button("Duplicate", ImVec2(third, 0.0f))) {
        editor.duplicateSelection(engine);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Cmd+D");
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(selection.size() < 2);
    if (ImGui::Button("Group", ImVec2(third, 0.0f))) {
        editor.groupSelection(engine);
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Cmd+G -- the group moves as one and stays editable inside");
    }
    ImGui::SameLine();
    if (ImGui::Button("Ungroup", ImVec2(third, 0.0f))) {
        editor.ungroupSelection(engine);
    }
    if (ImGui::Button("Delete", ImVec2(-1.0f, 0.0f))) {
        editor.deleteSelection(engine);
    }
}

void WorldEditPanel::drawHistory(app::Engine& engine, WorldEditor& editor) {
    EditHistory& history = editor.history;
    const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    ImGui::BeginDisabled(!history.canUndo());
    if (ImGui::Button(history.canUndo() ? ("Undo " + history.undoLabel()).c_str() : "Undo",
                      ImVec2(half, 0.0f))) {
        editor.undo(engine);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!history.canRedo());
    if (ImGui::Button(history.canRedo() ? ("Redo " + history.redoLabel()).c_str() : "Redo",
                      ImVec2(half, 0.0f))) {
        editor.redo(engine);
    }
    ImGui::EndDisabled();

    if (ImGui::TreeNode("History")) {
        const std::vector<std::string> labels = history.labels();
        if (labels.empty()) {
            ImGui::TextDisabled("nothing yet");
        }
        // Newest first: that is the order they will be taken back in.
        for (auto it = labels.rbegin(); it != labels.rend(); ++it) {
            ImGui::BulletText("%s", it->c_str());
        }
        if (history.redoSize() > 0) {
            ImGui::TextDisabled("%zu step(s) redoable", history.redoSize());
        }
        ImGui::TreePop();
    }
}

} // namespace avgen::ui
