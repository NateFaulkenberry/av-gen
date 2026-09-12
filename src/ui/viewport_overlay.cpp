#include "ui/viewport_overlay.hpp"

#include "ui/ui_logic.hpp"

#include "ui/brush.hpp"
#include "ui/gizmo.hpp"
#include "ui/world_probe.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace avgen::ui {
namespace {

// The axis colours everybody already knows: X red, Y green, Z blue. Learning a new set of them is
// a tax on every artist who opens this for the first time.
constexpr ImU32 kAxisColour[3] = {IM_COL32(232, 86, 86, 255), IM_COL32(126, 211, 106, 255),
                                  IM_COL32(96, 150, 244, 255)};
constexpr ImU32 kHighlight = IM_COL32(255, 214, 96, 255);
constexpr ImU32 kSelection = IM_COL32(255, 190, 60, 220);
constexpr ImU32 kGhostOk = IM_COL32(120, 225, 160, 210);
constexpr ImU32 kGhostBad = IM_COL32(240, 96, 88, 220);
constexpr ImU32 kFootprint = IM_COL32(140, 225, 180, 120);
constexpr ImU32 kFootprintBad = IM_COL32(240, 110, 100, 130);
// A hero's mark. Dim, because it is a standing annotation rather than something happening now, and
// the subject reads a little brighter than the rest so the ranking is visible without a number.
constexpr ImU32 kHero = IM_COL32(150, 190, 255, 130);
constexpr ImU32 kHeroSubject = IM_COL32(190, 216, 255, 210);

// NDC -> the canvas's own pixels, in ImGui's screen space. The one conversion in the file; every
// world point goes through here and nowhere else.
ImVec2 toScreen(const CanvasRect& canvas, glm::vec2 ndc) {
    return ImVec2(canvas.x + (ndc.x * 0.5f + 0.5f) * canvas.width,
                  canvas.y + (0.5f - ndc.y * 0.5f) * canvas.height);
}

struct Painter {
    ImDrawList* list = nullptr;
    const scene::Camera* camera = nullptr;
    const CanvasRect* canvas = nullptr;
    float aspect = 1.0f;

    [[nodiscard]] bool point(glm::vec3 world, ImVec2& out) const {
        const Projected p = projectPoint(*camera, aspect, world);
        if (!p.inFront) {
            return false;
        }
        out = toScreen(*canvas, p.ndc);
        return true;
    }
    void line(glm::vec3 a, glm::vec3 b, ImU32 colour, float thickness = 1.5f) const {
        ImVec2 pa;
        ImVec2 pb;
        // Both ends have to be in front. Clipping a segment that crosses the eye plane would be the
        // correct answer; refusing to draw it is the honest one, and a gizmo arm that vanishes when
        // the camera is inside the object is better than one that shoots across the screen.
        if (point(a, pa) && point(b, pb)) {
            list->AddLine(pa, pb, colour, thickness);
        }
    }
    // A circle lying on a surface, drawn as a polygon in world space so it follows the ground
    // rather than being a flat disc pasted over it.
    void ring(glm::vec3 centre, glm::vec3 normal, float radius, ImU32 colour, int segments = 40,
              float thickness = 1.5f) const {
        if (radius <= 1e-4f || segments < 3) {
            return;
        }
        const glm::vec3 reference =
            std::abs(normal.y) > 0.95f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
        const glm::vec3 u = glm::normalize(glm::cross(reference, normal)) * radius;
        const glm::vec3 v = glm::normalize(glm::cross(normal, u)) * radius;
        ImVec2 previous;
        bool have = false;
        for (int i = 0; i <= segments; ++i) {
            const float angle = 6.2831853f * static_cast<float>(i) / static_cast<float>(segments);
            ImVec2 here;
            if (!point(centre + u * std::cos(angle) + v * std::sin(angle), here)) {
                have = false;
                continue;
            }
            if (have) {
                list->AddLine(previous, here, colour, thickness);
            }
            previous = here;
            have = true;
        }
    }
    // The twelve edges of a world-space box.
    void box(const scene::WorldBounds& bounds, ImU32 colour, float thickness = 1.5f) const {
        if (!bounds.valid) {
            return;
        }
        std::array<glm::vec3, 8> corner{};
        for (int i = 0; i < 8; ++i) {
            corner[static_cast<std::size_t>(i)] =
                glm::vec3((i & 1) ? bounds.max.x : bounds.min.x, (i & 2) ? bounds.max.y : bounds.min.y,
                          (i & 4) ? bounds.max.z : bounds.min.z);
        }
        static constexpr int kEdges[12][2] = {{0, 1}, {1, 3}, {3, 2}, {2, 0}, {4, 5}, {5, 7},
                                              {7, 6}, {6, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
        for (const auto& edge : kEdges) {
            line(corner[static_cast<std::size_t>(edge[0])], corner[static_cast<std::size_t>(edge[1])],
                 colour, thickness);
        }
    }
    void label(glm::vec3 world, const char* text, ImU32 colour, ImVec2 offset = ImVec2(8.0f, -8.0f)) const {
        ImVec2 at;
        if (!point(world, at)) {
            return;
        }
        at.x += offset.x;
        at.y += offset.y;
        // A shadow, because the world behind the text is any colour it likes.
        list->AddText(ImVec2(at.x + 1.0f, at.y + 1.0f), IM_COL32(0, 0, 0, 190), text);
        list->AddText(at, colour, text);
    }
};

// One ghost: a footprint circle on the ground, the box it will occupy, and a stalk showing which
// way is up for it. Three marks, each answering a different question the artist has -- how much
// room does it take, how big is it, and which way will it stand.
void drawGhost(const Painter& painter, const GhostInstance& ghost, bool detailed) {
    const bool ok = ghost.valid();
    painter.ring(ghost.position + ghost.groundNormal * 0.02f, ghost.groundNormal, ghost.footprintRadius,
                 ok ? kFootprint : kFootprintBad, 28, ok ? 1.5f : 2.0f);
    const glm::vec3 up = glm::normalize(ghost.rotation * glm::vec3(0.0f, 1.0f, 0.0f));
    painter.line(ghost.position, ghost.position + up * ghost.height, ok ? kGhostOk : kGhostBad,
                 ok ? 1.5f : 2.0f);
    if (!detailed) {
        return;
    }
    // The box it will occupy, so "approximate bounds" is a thing on screen rather than a promise.
    const glm::vec3 forward = glm::normalize(ghost.rotation * glm::vec3(0.0f, 0.0f, 1.0f));
    const glm::vec3 right = glm::normalize(glm::cross(up, forward));
    const float r = ghost.footprintRadius;
    const glm::vec3 top = ghost.position + up * ghost.height;
    for (int i = 0; i < 4; ++i) {
        const float sx = (i & 1) ? 1.0f : -1.0f;
        const float sz = (i & 2) ? 1.0f : -1.0f;
        const glm::vec3 offset = right * (r * sx) + forward * (r * sz);
        painter.line(ghost.position + offset, top + offset, ok ? kGhostOk : kGhostBad, 1.0f);
    }
    // The orientation indicator: which way it faces, so a yaw jitter is visible before it is
    // committed rather than after.
    painter.line(ghost.position, ghost.position + forward * r * 1.8f, kHighlight, 1.5f);
}

} // namespace

EditorInput editorInputFromImGui(const CanvasRect& canvas) {
    EditorInput input;
    const ImGuiIO& io = ImGui::GetIO();
    if (!canvas.valid()) {
        return input;
    }
    input.overCanvas = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
    const float u = (io.MousePos.x - canvas.x) / canvas.width;
    const float v = (io.MousePos.y - canvas.y) / canvas.height;
    input.ndc = glm::vec2(u * 2.0f - 1.0f, 1.0f - v * 2.0f);
    input.leftDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    // Pressed only counts when it lands on the world. A press that began on a panel and travelled
    // over the canvas is that panel's, and a paint stroke that starts when you release a slider
    // over the viewport is a stroke nobody asked for.
    input.leftPressed = ImGui::IsMouseClicked(ImGuiMouseButton_Left) && input.overCanvas;
    input.leftReleased = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
    input.shift = io.KeyShift;
    input.alt = io.KeyAlt;
    // Asked of the same rule the camera asks, so the two cannot disagree about whose drag this is.
    input.cameraDrag = ui::intentIsCamera(ui::viewportIntent(input.leftDown, false, false, io.KeyAlt,
                                                             io.KeyShift));
    input.escape = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    return input;
}

void drawViewportOverlay(const WorldEditor& editor, const scene::Camera& camera, const CanvasRect& canvas,
                         float aspect) {
    if (!canvas.valid()) {
        return;
    }
    Painter painter;
    painter.list = ImGui::GetWindowDrawList();
    painter.camera = &camera;
    painter.canvas = &canvas;
    painter.aspect = aspect;
    if (painter.list == nullptr) {
        return;
    }
    painter.list->PushClipRect(ImVec2(canvas.x, canvas.y),
                               ImVec2(canvas.x + canvas.width, canvas.y + canvas.height), true);

    const EditorVisuals& visuals = editor.visuals();

    // ---- what is selected ----
    for (const EditorVisuals::SelectedBox& selected : visuals.selectionBoxes) {
        painter.box(selected.bounds, kSelection, 1.6f);
        // The name goes at the top of the box, not the centre: the centre is inside the object and
        // this is the one label that must stay legible over anything.
        char text[160];
        if (selected.isGroup) {
            std::snprintf(text, sizeof(text), "%s  (%zu in group)", selected.name.c_str(),
                          selected.members);
        } else {
            std::snprintf(text, sizeof(text), "%s", selected.name.c_str());
        }
        painter.label(glm::vec3(selected.bounds.centre().x, selected.bounds.max.y,
                                selected.bounds.centre().z),
                      text, kSelection);
    }

    // ---- the heroes ----
    // The circle is the space the hero claims, the stalk is how tall it is, and the label says
    // which of them the director would open on. Drawn before the selection reads over it, and thin
    // enough to stay out of the way of the work.
    for (const EditorVisuals::HeroMarker& hero : visuals.heroMarkers) {
        const ImU32 colour = hero.subject ? kHeroSubject : kHero;
        const glm::vec3 base(hero.position.x, hero.position.y - hero.height * 0.5f, hero.position.z);
        painter.ring(base, glm::vec3(0.0f, 1.0f, 0.0f), hero.radius, colour, 32, 1.4f);
        painter.line(base, glm::vec3(base.x, base.y + hero.height, base.z), colour, 1.2f);
        char text[192];
        std::snprintf(text, sizeof(text), "%s hero %s  %.2f", hero.subject ? "*" : " ",
                      hero.name.c_str(), static_cast<double>(hero.importance));
        painter.label(glm::vec3(base.x, base.y + hero.height, base.z), text, colour);
    }

    // ---- the ghost ----
    const BrushPreview& preview = editor.preview();
    if (visuals.showGhost && preview.ground.valid) {
        // The brush disc itself, on the ground, so the radius is a thing you can see rather than a
        // number you have to imagine.
        if (preview.brushRadius > 0.0f) {
            painter.ring(preview.ground.position + preview.ground.normal * 0.03f, preview.ground.normal,
                         preview.brushRadius, kHighlight, 48, 1.6f);
        }
        // The terrain-contact cross: the exact point on the surface the cursor is over.
        const glm::vec3 n = preview.ground.normal;
        const glm::vec3 reference =
            std::abs(n.y) > 0.95f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
        const glm::vec3 u = glm::normalize(glm::cross(reference, n)) * 0.6f;
        const glm::vec3 v = glm::normalize(glm::cross(n, u)) * 0.6f;
        painter.line(preview.ground.position - u, preview.ground.position + u, kHighlight, 1.2f);
        painter.line(preview.ground.position - v, preview.ground.position + v, kHighlight, 1.2f);
        // The surface normal, so "is it standing on the slope or through it" is answerable.
        painter.line(preview.ground.position, preview.ground.position + n * 1.2f, kHighlight, 1.2f);

        const bool detailed = preview.instances.size() <= 24; // beyond that the detail is noise
        for (const GhostInstance& ghost : preview.instances) {
            drawGhost(painter, ghost, detailed);
        }
    }

    // What is about to be erased, outlined in the refusal colour, whether or not there is an asset
    // armed -- an Eraser has no ghost of its own and its whole preview is this.
    for (const scene::WorldBounds& bounds : visuals.erasingBoxes) {
        painter.box(bounds, kGhostBad, 2.0f);
    }

    // ---- the gizmo ----
    if (visuals.showGizmo) {
        const GizmoFrame& frame = visuals.gizmo;
        const GizmoMode mode = editor.gizmoMode;
        if (mode == GizmoMode::Rotate) {
            for (int i = 0; i < 3; ++i) {
                const GizmoHandle handle = static_cast<GizmoHandle>(static_cast<int>(GizmoHandle::AxisX) + i);
                const bool hot = visuals.hovered == handle || visuals.dragging == handle;
                const std::vector<glm::vec3> ring = rotationRing(frame, handle, 64);
                for (std::size_t s = 0; s + 1 < ring.size(); ++s) {
                    painter.line(ring[s], ring[s + 1], hot ? kHighlight : kAxisColour[i], hot ? 3.0f : 1.8f);
                }
                if (!ring.empty()) {
                    painter.line(ring.back(), ring.front(), hot ? kHighlight : kAxisColour[i],
                                 hot ? 3.0f : 1.8f);
                }
            }
        } else {
            for (int i = 0; i < 3; ++i) {
                const GizmoHandle handle = static_cast<GizmoHandle>(static_cast<int>(GizmoHandle::AxisX) + i);
                const bool hot = visuals.hovered == handle || visuals.dragging == handle;
                const auto [a, b] = axisSegment(frame, handle);
                painter.line(a, b, hot ? kHighlight : kAxisColour[i], hot ? 4.0f : 2.4f);
                ImVec2 tip;
                if (painter.point(b, tip)) {
                    const float size = mode == GizmoMode::Scale ? 4.0f : 5.0f;
                    // A cube for scale, a dot for move: the shape of the handle says which tool is
                    // in the hand without reading the toolbar.
                    if (mode == GizmoMode::Scale) {
                        painter.list->AddRectFilled(ImVec2(tip.x - size, tip.y - size),
                                                    ImVec2(tip.x + size, tip.y + size),
                                                    hot ? kHighlight : kAxisColour[i]);
                    } else {
                        painter.list->AddCircleFilled(tip, size, hot ? kHighlight : kAxisColour[i]);
                    }
                }
            }
            for (int i = 0; i < 3; ++i) {
                const GizmoHandle handle =
                    static_cast<GizmoHandle>(static_cast<int>(GizmoHandle::PlaneYZ) + i);
                const bool hot = visuals.hovered == handle || visuals.dragging == handle;
                const std::array<glm::vec3, 4> quad = planeQuad(frame, handle);
                ImVec2 screen[4];
                bool all = true;
                for (int c = 0; c < 4; ++c) {
                    all = all && painter.point(quad[static_cast<std::size_t>(c)], screen[c]);
                }
                if (!all) {
                    continue;
                }
                const ImU32 base = kAxisColour[i];
                const ImU32 fill = hot ? IM_COL32(255, 214, 96, 110) : (base & 0x00FFFFFFu) | 0x50000000u;
                painter.list->AddQuadFilled(screen[0], screen[1], screen[2], screen[3], fill);
                painter.list->AddQuad(screen[0], screen[1], screen[2], screen[3],
                                      hot ? kHighlight : base, 1.4f);
            }
        }
        ImVec2 centre;
        if (painter.point(frame.origin, centre)) {
            painter.list->AddCircleFilled(centre, 3.5f, IM_COL32(230, 230, 230, 220));
        }
        if (!visuals.dragReadout.empty()) {
            const ImVec2 at = ImGui::GetIO().MousePos;
            const ImVec2 text = ImGui::CalcTextSize(visuals.dragReadout.c_str());
            const ImVec2 lo(at.x + 16.0f, at.y + 10.0f);
            const ImVec2 hi(lo.x + text.x + 10.0f, lo.y + text.y + 6.0f);
            painter.list->AddRectFilled(lo, hi, IM_COL32(18, 20, 24, 225), 3.0f);
            painter.list->AddText(ImVec2(lo.x + 5.0f, lo.y + 3.0f), IM_COL32(240, 240, 240, 255),
                                  visuals.dragReadout.c_str());
        }
    }

    // ---- the drag box ----
    if (visuals.boxing) {
        const ImVec2 a = toScreen(canvas, visuals.boxFrom);
        const ImVec2 b = toScreen(canvas, visuals.boxTo);
        painter.list->AddRectFilled(ImVec2(std::min(a.x, b.x), std::min(a.y, b.y)),
                                    ImVec2(std::max(a.x, b.x), std::max(a.y, b.y)),
                                    IM_COL32(255, 190, 60, 40));
        painter.list->AddRect(ImVec2(std::min(a.x, b.x), std::min(a.y, b.y)),
                              ImVec2(std::max(a.x, b.x), std::max(a.y, b.y)), kSelection, 0.0f, 0, 1.4f);
    }

    painter.list->PopClipRect();
}

void drawViewportHud(const WorldEditor& editor, const CanvasRect& canvas) {
    if (!canvas.valid()) {
        return;
    }
    ImDrawList* list = ImGui::GetWindowDrawList();
    if (list == nullptr) {
        return;
    }
    const bool placing = editor.mode == EditorMode::Place;
    const BrushPreview& preview = editor.preview();
    // Red when the next click would do nothing, green when it would place, neutral when selecting.
    // The colour of the panel is the fastest thing in the frame to read, and "can I click here" is
    // the question being asked every second.
    ImU32 accent = IM_COL32(150, 160, 175, 255);
    if (placing) {
        accent = preview.placeable() ? IM_COL32(120, 225, 160, 255) : IM_COL32(240, 110, 100, 255);
    }

    const std::string title =
        placing ? std::string("PLACE  ") + app::placementModeName(editor.brush.mode) : std::string("SELECT");
    const std::string& detail = editor.status();

    const ImVec2 titleSize = ImGui::CalcTextSize(title.c_str());
    const ImVec2 detailSize = ImGui::CalcTextSize(detail.c_str());
    const float width = std::max(titleSize.x, detailSize.x) + 20.0f;
    const float height = titleSize.y + detailSize.y + 14.0f;
    const ImVec2 lo(canvas.x + 12.0f, canvas.y + 12.0f);
    const ImVec2 hi(lo.x + width, lo.y + height);
    list->AddRectFilled(lo, hi, IM_COL32(14, 16, 20, 190), 4.0f);
    list->AddRect(lo, hi, accent, 4.0f, 0, 1.5f);
    list->AddText(ImVec2(lo.x + 10.0f, lo.y + 5.0f), accent, title.c_str());
    list->AddText(ImVec2(lo.x + 10.0f, lo.y + 7.0f + titleSize.y), IM_COL32(225, 228, 232, 255),
                  detail.c_str());

    // The terrain facts, under the panel, while a brush is live: the four numbers §22 asks the
    // preview to communicate, as numbers rather than as a colour.
    if (placing && preview.ground.valid) {
        char line[192];
        std::snprintf(line, sizeof(line), "ground %.1f m   slope %.0f deg%s%s",
                      static_cast<double>(preview.ground.position.y),
                      static_cast<double>(preview.ground.slopeDegrees),
                      preview.ground.submerged ? "   WATER" : "",
                      preview.ground.insideWorld ? "" : "   OUTSIDE WORLD");
        const ImVec2 size = ImGui::CalcTextSize(line);
        const ImVec2 flo(lo.x, hi.y + 4.0f);
        const ImVec2 fhi(flo.x + size.x + 20.0f, flo.y + size.y + 8.0f);
        list->AddRectFilled(flo, fhi, IM_COL32(14, 16, 20, 165), 4.0f);
        list->AddText(ImVec2(flo.x + 10.0f, flo.y + 4.0f), IM_COL32(190, 198, 208, 255), line);
    }
}

} // namespace avgen::ui
