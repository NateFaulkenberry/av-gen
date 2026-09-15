#include "ui/world_edit_panel.hpp"

#include "core/log.hpp"

#include "scene/composition.hpp"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

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

// An eye, a padlock and a star, drawn rather than typed. The UI's font is ImGui's default, which is
// ASCII only, so the symbols a layer list is actually read by cannot be written as text -- and a
// column of the letters "V", "L" and "H" is not a thing anyone scans. Returns true when clicked.
enum class Icon : std::uint8_t { Eye, Padlock, Star };

bool iconToggle(const char* id, bool on, Icon icon, const char* tooltip) {
    const float side = ImGui::GetFrameHeight() * 0.78f;
    const ImVec2 lo = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(side, side));
    const bool hovered = ImGui::IsItemHovered();
    if (hovered && tooltip != nullptr) {
        ImGui::SetTooltip("%s", tooltip);
    }
    // Off is dim rather than absent: an empty cell reads as "this row has no eye", and the artist
    // then cannot find the thing they hid.
    const ImU32 colour = on ? (hovered ? IM_COL32(235, 238, 245, 255) : IM_COL32(196, 202, 212, 255))
                            : (hovered ? IM_COL32(150, 154, 162, 255) : IM_COL32(92, 96, 104, 255));
    ImDrawList* list = ImGui::GetWindowDrawList();
    const ImVec2 c(lo.x + side * 0.5f, lo.y + side * 0.5f);
    if (icon == Icon::Eye) {
        if (on) {
            list->AddCircle(c, side * 0.30f, colour, 0, 1.4f);
            list->AddCircleFilled(c, side * 0.12f, colour);
        } else {
            // A shut lid: the eye's outline flattened to the line it closes to.
            list->AddLine(ImVec2(lo.x + side * 0.16f, c.y), ImVec2(lo.x + side * 0.84f, c.y), colour, 1.6f);
        }
        return clicked;
    }
    if (icon == Icon::Star) {
        // Five points, filled when it is a hero and outlined when it is not, so the row reads at a
        // glance the way the eye does: the shape is always there, and only the fill changes.
        constexpr int kPoints = 5;
        constexpr float kTurn = 3.14159265f * 2.0f / static_cast<float>(kPoints);
        const float outer = side * 0.42f;
        const float inner = outer * 0.42f;
        for (int i = 0; i < kPoints * 2; ++i) {
            const float radius = (i % 2 == 0) ? outer : inner;
            const float angle = -3.14159265f * 0.5f + static_cast<float>(i) * kTurn * 0.5f;
            list->PathLineTo(ImVec2(c.x + std::cos(angle) * radius, c.y + std::sin(angle) * radius));
        }
        if (on) {
            list->PathFillConvex(colour);   // a concave path, but at this size the difference is
        } else {                            // under a pixel and the alternative is a triangulator
            list->PathStroke(colour, ImDrawFlags_Closed, 1.3f);
        }
        return clicked;
    }

    const ImVec2 bodyLo(c.x - side * 0.26f, c.y - side * 0.02f);
    const ImVec2 bodyHi(c.x + side * 0.26f, c.y + side * 0.34f);
    list->AddRectFilled(bodyLo, bodyHi, colour, 1.5f);
    // The shackle. Closed it sits centred on the body; open it is swung clear to one side, which is
    // the difference a padlock is recognised by at this size.
    const float shackleX = on ? c.x : c.x + side * 0.22f;
    constexpr float kPi = 3.14159265f; // IM_PI lives in imgui_internal.h, which this does not include
    list->PathArcTo(ImVec2(shackleX, bodyLo.y), side * 0.17f, kPi, kPi * 2.0f);
    list->PathStroke(colour, 0, 1.5f);
    return clicked;
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
    drawObjects(engine, editor);
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
    // Through the application's dispatcher, like the menu and the keyboard. A panel button with its
    // own route is a second implementation of the same action, and the one that drifts.
    app::EditSystem* edits = editor.edits();
    const auto act = [&](app::EditAction action, const char* text, ImVec2 size) {
        const bool available = edits != nullptr && edits->canExecute(action);
        ImGui::BeginDisabled(!available);
        const bool pressed = ImGui::Button(text, size);
        ImGui::EndDisabled();
        if (pressed && available) {
            static_cast<void>(edits->execute(action, engine));
        }
    };
    act(app::EditAction::Duplicate, "Duplicate", ImVec2(third, 0.0f));
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
    act(app::EditAction::Delete, "Delete", ImVec2(-1.0f, 0.0f));
}

void WorldEditPanel::drawObjects(app::Engine& engine, WorldEditor& editor) {
    scene::Composition* composition = engine.composition();
    if (!ImGui::TreeNodeEx("Objects", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    if (composition == nullptr || composition->nodeCount() == 0) {
        ImGui::TextDisabled("no objects");
        ImGui::TreePop();
        return;
    }

    // Children by parent, once, rather than a scan of every node per row. A scene with four hundred
    // nodes would otherwise be a hundred and sixty thousand string comparisons a frame.
    std::unordered_map<std::string, std::vector<const scene::CompositionNode*>> children;
    std::vector<const scene::CompositionNode*> roots;
    std::size_t lockedCount = 0;
    for (const auto& node : composition->nodes()) {
        if (!node) {
            continue;
        }
        if (node->locked) {
            ++lockedCount;
        }
        if (node->parent.empty() || composition->findNode(node->parent) == nullptr) {
            roots.push_back(node.get());
        } else {
            children[node->parent].push_back(node.get());
        }
    }

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##objectfilter", "filter", objectFilter_, sizeof(objectFilter_));
    if (lockedCount > 0) {
        // A way back. Locking is the one thing here that hides its own effect -- a locked object
        // looks exactly like an unlocked one in the viewport -- so "why can I not click this" has to
        // have an answer that does not involve finding the row.
        ImGui::Text("%zu locked", lockedCount);
        ImGui::SameLine();
        if (ImGui::SmallButton("Unlock all")) {
            std::vector<std::string> all;
            for (const auto& node : composition->nodes()) {
                if (node && node->locked) {
                    all.push_back(node->name);
                }
            }
            editor.setNodesLocked(engine, all, false);
        }
    }
    // The heroes, as a list of their own and not only as stars on the rows.
    //
    // Two reasons it cannot be rows alone. A hero has no appearance in the viewport beyond the mark
    // the editor draws, so "what is this world about, and in what order" has to be answerable
    // somewhere. And a hero can outlive its object -- a file written by hand, an object renamed or
    // deleted -- and one with no row would be a subject the director keeps travelling to that
    // nothing in the application can take back.
    // Only the ones that need it. Every hero standing on an object already has a star on its row,
    // and listing them again was the same information twice -- reported as redundant, and it was.
    //
    // What survives is the half the rows cannot do: a hero can outlive its object, when a file is
    // written by hand or a node is renamed or deleted. That hero has no row, so without this it is a
    // subject the director keeps travelling to that nothing in the application can take back. When
    // there are none -- the ordinary case -- nothing is drawn at all.
    std::vector<world::HeroPoint> orphans;
    for (const world::HeroPoint& hero : composition->heroes()) {
        if (composition->findNode(hero.name) == nullptr) {
            orphans.push_back(hero);
        }
    }
    if (const std::size_t heroCount = orphans.size(); heroCount > 0) {
        if (ImGui::TreeNodeEx("##heroes", ImGuiTreeNodeFlags_DefaultOpen,
                              "Heroes with no object (%zu)", heroCount)) {
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Heroes whose object is gone -- renamed, deleted, or never in "
                                  "this scene. The Auto-director still travels to them and no row "
                                  "below can take them back, so they are listed here to be "
                                  "unstarred. Heroes that do have an object are starred on their "
                                  "own row instead.");
            }
            // Copied rather than iterated: unstarring one rewrites the list under the loop.
            const std::vector<world::HeroPoint>& heroes = orphans;
            for (std::size_t i = 0; i < heroes.size(); ++i) {
                const world::HeroPoint& hero = heroes[i];
                ImGui::PushID(static_cast<int>(i));
                if (iconToggle("##declared", true, Icon::Star,
                               "Not a hero -- the Auto-director stops travelling to it")) {
                    const std::vector<std::string> one{hero.name};
                    editor.setNodesHero(engine, one, false);
                }
                ImGui::SameLine();
                const bool onANode = composition->findNode(hero.name) != nullptr;
                // No subject marker. Rank comes from importance and nothing else, so a star that
                // said "this one is different" was naming a category that does not exist -- an
                // author who wants a particular hero opened on raises its importance, which is the
                // same act and one fewer concept (ADR-201).
                ImGui::TextUnformatted(hero.name.c_str());
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("importance %.2f, %.0f m tall, camera stands off %.0f m%s",
                                      static_cast<double>(hero.importance),
                                      static_cast<double>(hero.height),
                                      static_cast<double>(hero.preferredCameraDistance),
                                      onANode ? "" : "\nno object of this name is in the scene");
                }
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
    }

    const std::string filter = objectFilter_;
    const auto matches = [&](const std::string& name) {
        if (filter.empty()) {
            return true;
        }
        const auto lower = [](std::string t) {
            std::transform(t.begin(), t.end(), t.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            return t;
        };
        return lower(name).find(lower(filter)) != std::string::npos;
    };

    // The list an artist actually works in, so it gets the height rather than a fixed 190 px that
    // showed six rows in a scene with sixty. Grows with the panel and keeps a floor, so it is usable
    // when the panel is short and generous when it is tall.
    {
        const float available = ImGui::GetContentRegionAvail().y;
        ImGui::BeginChild("##objects", ImVec2(0.0f, std::max(available - 90.0f, 240.0f)),
                          ImGuiChildFlags_Borders);
    }
    // Recursive, and depth-limited for the same reason every other walk of this graph is: a scene
    // file can be hand-edited into a cycle, and a panel that recurses forever takes the app with it.
    const std::function<void(const scene::CompositionNode&, int)> row = [&](const scene::CompositionNode& node,
                                                                           int depth) {
        const auto kids = children.find(node.name);
        const bool hasKids = kids != children.end() && !kids->second.empty();
        const bool showSelf = matches(node.name);
        ImGui::PushID(node.name.c_str());

        if (showSelf) {
            // A disclosure triangle, as a layer has in an image editor: the row stays one line and
            // what is behind it is this object's settings. Drawn first so every row's switches line
            // up whether or not it is open.
            const bool open = expanded_.contains(node.name);
            if (ImGui::ArrowButton("##open", open ? ImGuiDir_Down : ImGuiDir_Right)) {
                if (open) {
                    expanded_.erase(node.name);
                } else {
                    expanded_.insert(node.name);
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("what this object is worth to the Auto-director");
            }
            ImGui::SameLine();
            const bool visible = node.visibleParam != nullptr ? node.visibleParam->base() : node.visible;
            if (iconToggle("##eye", visible, Icon::Eye,
                           visible ? "Hide (undoable, saved with the scene)" : "Show")) {
                const std::vector<std::string> one{node.name};
                editor.setNodesVisible(engine, one, !visible);
            }
            ImGui::SameLine();
            if (iconToggle("##lock", node.locked, Icon::Padlock,
                           node.locked ? "Unlock -- let it answer clicks again"
                                       : "Lock -- stop it answering clicks and drag boxes")) {
                const std::vector<std::string> one{node.name};
                editor.setNodesLocked(engine, one, !node.locked);
            }
            ImGui::SameLine();
            // A hero is what the Auto-director travels towards and what a reaction profile answers
            // the music through (ADR-072/074). Declaring one describes an object that is already
            // placed -- nothing moves, resizes or relights -- and what it is worth is measured from
            // the object itself.
            const bool hero = nodeIsHero(*composition, node.name);
            if (iconToggle("##hero", hero, Icon::Star,
                           hero ? "Not a hero -- the Auto-director stops travelling to it"
                                : "Make it a hero -- somewhere the Auto-director travels to, and "
                                  "something a reaction profile can answer the music through")) {
                const std::vector<std::string> one{node.name};
                editor.setNodesHero(engine, one, !hero);
            }
            ImGui::SameLine();
            if (depth > 0) {
                ImGui::Dummy(ImVec2(static_cast<float>(depth) * 10.0f, 0.0f));
                ImGui::SameLine();
            }

            const bool selected = editor.selection.contains(node.name);
            // Locked rows are greyed and inert. A lock means "this does not get selected"; a panel
            // that let you select it anyway would put a gizmo back on the thing you locked to get
            // the gizmo off, and there would then be two answers to what locked means.
            if (node.locked) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            }
            if (ImGui::Selectable(node.name.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick) &&
                !node.locked) {
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    // Go and look at it, the way double-clicking a layer's thumbnail does. The
                    // selection is replaced rather than added to: a double-click is about *this*
                    // object, and framing a set that happens to include it would put it in the
                    // corner of the shot. Same request the Frame button makes, so there is one
                    // answer to "what does framing mean" and the viewport still owns the camera.
                    editor.selection.set(node.name);
                    frameSelectionRequested = true;
                } else if (ImGui::GetIO().KeyShift) {
                    editor.selection.toggle(node.name);
                } else {
                    editor.selection.set(node.name);
                }
            }
            if (node.locked) {
                ImGui::PopStyleColor();
            }
            if (node.kind == scene::NodeKind::Group && hasKids) {
                ImGui::SameLine();
                ImGui::TextDisabled("(%zu)", kids->second.size());
            }
            if (open) {
                drawObjectSettings(engine, editor, node.name);
            }
        }

        if (hasKids && depth < 12) {
            for (const scene::CompositionNode* child : kids->second) {
                if (child != nullptr) {
                    row(*child, showSelf ? depth + 1 : depth);
                }
            }
        }
        ImGui::PopID();
    };
    for (const scene::CompositionNode* node : roots) {
        if (node != nullptr) {
            row(*node, 0);
        }
    }
    ImGui::EndChild();
    ImGui::TreePop();
}

// What a hero is worth and where the camera should look at it.
//
// These three numbers decided every directed shot and could only be reached by hand-editing the
// scene file: importance chooses the subject, the aim offset says where *on* an object the camera
// points, and the stand-off is how far away it stops -- which is the difference between a shot of a
// tree and a shot from inside one.
//
// The offset is expressed against the object rather than in world space, because that is how it is
// thought about ("a bit above the base") and because a hero follows its object (ADR-106): storing
// the absolute position and showing the difference keeps one source of truth and lets the number
// stay meaningful when the object moves.
// Where a child sits relative to the thing it is attached to (ADR-188).
//
// Shown for any node with a parent, and silent for one without -- a root node has no offset, and a
// control reading "0, 0, 0 from nothing" would be worse than no control.
//
// It writes the node's local position through `setNodePosition`, which is the same path the gizmo
// and the arrow keys use: parameter base and authored transform together, so the edit is undoable,
// saveable and survives a rebuild. Writing the world position instead would be the derived-copy
// mistake this project has made repeatedly -- the flattener recomputes world from parent x local on
// the next update and the edit would last exactly one frame.
void WorldEditPanel::drawParentOffset(app::Engine& engine, WorldEditor& editor, const std::string& node) {
    scene::Composition* composition = engine.composition();
    if (composition == nullptr) {
        return;
    }
    const scene::CompositionNode* child = composition->findNode(node);
    if (child == nullptr || child->parent.empty()) {
        return;
    }
    const bool parentExists = composition->findNode(child->parent) != nullptr;

    ImGui::TextColored(ImVec4(0.75f, 0.51f, 0.92f, 1.0f), "attached to %s", child->parent.c_str());
    if (!parentExists) {
        // Said plainly rather than shown as a working control: a child whose parent is missing is
        // drawn at its local transform, so the number below would be an offset from nothing.
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "(missing)");
    }

    glm::vec3 local = child->transform.position;
    ImGui::SetNextItemWidth(-90.0f);
    const bool edited = ImGui::DragFloat3("offset", &local.x, 0.01f, -1000.0f, 1000.0f, "%.3f m");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("where this sits relative to %s, in the parent's own frame.\n\n"
                          "This is the offset parenting exists for: move the parent and this stays "
                          "put against it. The line and cross in the viewport are the same tie -- "
                          "the cross marks the point these numbers are measured from.",
                          child->parent.c_str());
    }
    // One undo step per drag, not per frame. The same `beginDrag` / `commitDrag` the gizmo uses,
    // rather than a second mechanism beside it: the drag opens a command, writes through it for as
    // long as the mouse is down, and closes it once on release.
    if (ImGui::IsItemActivated()) {
        editor.history().beginDrag(engine, fmt::format("Offset {}", node),
                                   ui::transformParamPaths(std::array<std::string, 1>{node}),
                                   std::vector<std::string>{node});
    }
    if (edited) {
        ui::setNodePosition(engine, node, local);
    }
    if (ImGui::IsItemDeactivatedAfterEdit() && editor.history().dragging()) {
        editor.history().commitDrag(engine);
    }
    ImGui::Separator();
}

void WorldEditPanel::drawObjectSettings(app::Engine& engine, WorldEditor& editor, const std::string& node) {
    scene::Composition* composition = engine.composition();
    if (composition == nullptr) {
        return;
    }
    ImGui::Indent(18.0f);

    // ---- the tie to a parent (ADR-188) ---------------------------------------------------------
    //
    // Drawn before the hero controls and outside their gate, because it applies to a different and
    // much larger set of objects. A spore emitter parented to the cap it falls from is not a hero
    // and never will be, and its offset from that cap was the one number about it that mattered and
    // the one number no panel showed -- reported as "I have no way to change the offset position of
    // spores to their parent in the UI", after two rounds of the same emitter being misaligned.
    //
    // The number edited is the node's *local* position, which is exactly what parenting means: the
    // world position is the parent's transform times this. So an offset typed here survives the
    // parent moving, which is the whole reason the emitter was parented rather than placed.
    drawParentOffset(engine, editor, node);

    const auto& heroes = composition->heroes();
    const auto it = std::find_if(heroes.begin(), heroes.end(),
                                 [&](const world::HeroPoint& h) { return h.name == node; });
    if (it == heroes.end()) {
        // Shown rather than hidden: an empty disclosure reads as a broken one, and the sentence is
        // also the instruction for turning the controls on.
        ImGui::TextDisabled("not a hero -- star it to aim the Auto-director at it");
        ImGui::Unindent(18.0f);
        return;
    }
    world::HeroPoint hero = *it;
    const scene::CompositionNode* object = composition->findNode(node);
    const glm::vec3 origin =
        object != nullptr ? composition->nodeWorldTransform(*object).position : glm::vec3(0.0f);

    // One undo step per drag, not per frame: the same coalescing the gizmo uses.
    auto beginDrag = [&] {
        if (ImGui::IsItemActivated()) {
            heroBeforeDrag_ = *it;
        }
    };
    auto endDrag = [&] {
        if (ImGui::IsItemDeactivatedAfterEdit() && heroBeforeDrag_) {
            editor.recordHeroEdit(engine, *heroBeforeDrag_, hero);
            heroBeforeDrag_.reset();
        }
    };
    bool changed = false;

    ImGui::SetNextItemWidth(-90.0f);
    changed |= ImGui::SliderFloat("importance", &hero.importance, 0.0f, 1.0f, "%.2f");
    beginDrag();
    const bool subject = !heroes.empty() && heroes.front().name == node;
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s\nthe most important hero is the subject: it gets the builds and the "
                          "drops, and the rest of the cast gets the passages",
                          subject ? "this is the subject of the film" : "not the subject");
    }
    endDrag();

    glm::vec3 offset = hero.position - origin;
    ImGui::SetNextItemWidth(-90.0f);
    changed |= ImGui::DragFloat3("aim offset", &offset.x, 0.05f, -1000.0f, 1000.0f, "%.2f m");
    beginDrag();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("where on the object the camera looks, relative to its origin. The mark in "
                          "the viewport is this point; it shows while the object is selected.");
    }
    endDrag();
    hero.position = origin + offset;

    ImGui::SetNextItemWidth(-90.0f);
    changed |= ImGui::DragFloat("stand-off", &hero.preferredCameraDistance, 0.25f, 0.5f, 5000.0f, "%.1f m");
    beginDrag();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("how far the camera stops from it. Raise it if a shot ends up inside the "
                          "object; shot distances are otherwise in multiples of its own size.");
    }
    endDrag();
    // A hero that activates closer than the camera is meant to stand never activates on the shot
    // designed for it, and `validate()` refuses it -- so the reach follows the stand-off up rather
    // than the edit being rejected under the mouse. Three times, the ratio the authored heroes use.
    hero.activationRadius = std::max(hero.activationRadius, hero.preferredCameraDistance * 3.0f);

    if (changed) {
        // Straight to the composition while the mouse is down: the mark, the clearance field and
        // the obstacles all follow immediately, and the directed shot is re-cut once the drag
        // settles rather than sixty times a second (ADR-106's debounce).
        if (auto ok = composition->editHero(node, hero); !ok) {
            avgen::log::warn("hero '{}': {}", node, ok.error().message);
        }
    }
    ImGui::TextDisabled("%.0f m tall, %.0f m across", static_cast<double>(hero.height),
                        static_cast<double>(hero.radius * 2.0f));
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("measured from the object when it was starred, and used to size every "
                          "shot. Unstar and star it again to re-measure a scaled object.");
    }
    ImGui::Unindent(18.0f);
}

void WorldEditPanel::drawHistory(app::Engine& engine, WorldEditor& editor) {
    app::EditSystem* edits = editor.edits();
    if (edits == nullptr) {
        ImGui::TextDisabled("no edit system attached");
        return;
    }
    // The application's history, not this editor's. It is shown here because this is where the
    // editor's other controls are, but what it lists is every edit the application has made -- so an
    // entry from another editor appears in it, and undoing from here takes that back.
    const EditHistory& history = edits->history();
    const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;

    // Through `execute`, the same call the menu and the keyboard make. A button that undid by its
    // own route would be a second implementation of undo, and the first thing to disagree with the
    // other two about what is available.
    const auto button = [&](app::EditAction action) {
        const bool available = edits->canExecute(action);
        ImGui::BeginDisabled(!available);
        if (ImGui::Button(edits->menuLabel(action).c_str(), ImVec2(half, 0.0f))) {
            static_cast<void>(edits->execute(action, engine));
        }
        ImGui::EndDisabled();
    };
    button(app::EditAction::Undo);
    ImGui::SameLine();
    button(app::EditAction::Redo);

    if (ImGui::TreeNodeEx("History", ImGuiTreeNodeFlags_DefaultOpen)) {
        // One list, oldest at the bottom, with the document's current position marked -- and every
        // row clickable, because "take me back to before I did that" is the question a history is
        // actually asked. Clicking runs the undos or redos it would have taken by hand, so nothing
        // here can reach a state that stepping could not.
        const std::vector<std::string> done = history.labels();      // oldest first
        const std::vector<std::string> ahead = history.redoLabels();  // next to redo first
        const std::size_t here = done.size();

        if (done.empty() && ahead.empty()) {
            ImGui::TextDisabled("nothing yet");
        }

        // The future, newest last: walking down the list is walking forward in time.
        for (std::size_t i = ahead.size(); i-- > 0;) {
            const std::size_t depth = here + i + 1;
            ImGui::PushID(static_cast<int>(depth));
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            if (ImGui::Selectable(("   " + ahead[i]).c_str())) {
                static_cast<void>(edits->jumpTo(depth, engine));
            }
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Redo forward to here");
            }
            ImGui::PopID();
        }

        // The past, newest first, with the top one marking where the document stands.
        for (std::size_t i = done.size(); i-- > 0;) {
            const bool current = i + 1 == here;
            ImGui::PushID(static_cast<int>(i));
            if (current) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.80f, 1.0f, 1.0f));
            }
            const std::string row = (current ? "-> " : "   ") + done[i];
            if (ImGui::Selectable(row.c_str(), current)) {
                static_cast<void>(edits->jumpTo(i + 1, engine));
            }
            if (current) {
                ImGui::PopStyleColor();
            }
            if (ImGui::IsItemHovered() && !current) {
                ImGui::SetTooltip("Undo back to here");
            }
            ImGui::PopID();
        }

        // Before anything the history still holds. Named rather than blank, because "the beginning"
        // is only the beginning of what is *kept*: once commands have been trimmed off the bottom,
        // this is as far back as the history can take you and saying so avoids implying otherwise.
        if (!done.empty()) {
            ImGui::PushID("start");
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            if (ImGui::Selectable("   (as far back as this goes)")) {
                static_cast<void>(edits->jumpTo(0, engine));
            }
            ImGui::PopStyleColor();
            ImGui::PopID();
        }
        ImGui::TreePop();
    }

}

} // namespace avgen::ui
