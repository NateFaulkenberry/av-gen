#include "ui/viewport_overlay.hpp"

#include "ui/ui_logic.hpp"

#include "entity/nav_grid.hpp"
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
// ADR-188. A distinct hue from the selection amber and the hero blue, because the question it
// answers is a third one: not "what is selected" or "what is the camera about", but "what does this
// thing move with".
constexpr ImU32 kParentLink = IM_COL32(190, 130, 235, 200);

// ---- navigation (ADR-197) -----------------------------------------------------------------------
//
// A fourth hue, for the fourth question: not "what is selected", not "what is the camera about",
// not "what does this move with", but "where is this going and may it get there". Teal, because it
// is the one part of the overlay that is about the *ground* rather than about an object, and it has
// to stay legible over the amber selection box that is usually on the same character.
constexpr ImU32 kRoute = IM_COL32(90, 210, 200, 170);      // the plan beyond the next waypoint
constexpr ImU32 kRouteLeg = IM_COL32(150, 255, 235, 240);  // the leg being walked right now
constexpr ImU32 kRouteGoal = IM_COL32(120, 245, 185, 230);
// A route that failed. The same refusal red as a placement that would not take, deliberately: the
// two mean the same thing to a person -- "this will not work" -- and giving them two colours would
// be two things to learn for one fact.
constexpr ImU32 kRouteFailed = IM_COL32(240, 96, 88, 230);

// The grid. Low alpha throughout: it is a carpet under everything else, and at four-metre cells a
// screenful of it is a thousand quads that must not drown the scene they are explaining.
constexpr ImU32 kCellWalkable = IM_COL32(80, 190, 150, 30);
constexpr ImU32 kCellEdge = IM_COL32(150, 230, 190, 80);   // walkable, and up against something
constexpr ImU32 kCellWater = IM_COL32(70, 140, 235, 60);
constexpr ImU32 kCellSteep = IM_COL32(235, 165, 70, 60);
constexpr ImU32 kCellBlocked = IM_COL32(235, 80, 80, 70);
constexpr ImU32 kCellUnknown = IM_COL32(150, 150, 150, 34); // not walkable, and no reason recorded
constexpr ImU32 kShore = IM_COL32(120, 200, 255, 200);
constexpr ImU32 kVista = IM_COL32(255, 220, 130, 200);

// A stable colour per connected region. The number itself is meaningless -- what matters is that
// two cells the same colour are reachable from each other and two cells different colours are not,
// which is the one fact "my character will not go there" usually turns out to be.
ImU32 regionColour(std::uint16_t region) {
    if (region == 0) {
        return kCellUnknown;
    }
    // A golden-ratio walk over hue, so consecutive region ids are far apart rather than adjacent.
    const float hue = std::fmod(static_cast<float>(region) * 0.61803398875f, 1.0f);
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    ImGui::ColorConvertHSVtoRGB(hue, 0.65f, 1.0f, r, g, b);
    return IM_COL32(static_cast<int>(r * 255.0f), static_cast<int>(g * 255.0f),
                    static_cast<int>(b * 255.0f), 66);
}

ImU32 cellColour(const EditorVisuals::NavCellMark& cell, bool regions) {
    using namespace avgen::entity;
    if ((cell.flags & NavWalkable) != 0) {
        if (regions) {
            return regionColour(cell.region);
        }
        return (cell.flags & NavEdge) != 0 ? kCellEdge : kCellWalkable;
    }
    if ((cell.flags & NavBlocked) != 0) {
        return kCellBlocked;
    }
    if ((cell.flags & NavWater) != 0) {
        return kCellWater;
    }
    if ((cell.flags & NavSteep) != 0) {
        return kCellSteep;
    }
    return kCellUnknown;
}

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
    // A cell-sized square lying flat at `centre`'s own height. Four projections and one filled
    // quad; this is the unit the navigation grid's cost is counted in.
    void flatQuad(glm::vec3 centre, float half, ImU32 colour) const {
        ImVec2 corner[4];
        static constexpr float kSx[4] = {-1.0f, 1.0f, 1.0f, -1.0f};
        static constexpr float kSz[4] = {-1.0f, -1.0f, 1.0f, 1.0f};
        for (int i = 0; i < 4; ++i) {
            if (!point(centre + glm::vec3(kSx[i] * half, 0.0f, kSz[i] * half), corner[i])) {
                return; // behind the eye: refused whole, as every other shape here is
            }
        }
        list->AddQuadFilled(corner[0], corner[1], corner[2], corner[3], colour);
    }
    void label(glm::vec3 world, const char* text, ImU32 colour, ImVec2 offset = ImVec2(8.0f, -8.0f)) const {
        ImVec2 at;
        if (!point(world, at)) {
            return;
        }
        at.x += offset.x;
        at.y += offset.y;
        // Held inside the canvas. A label is drawn at its object, and an object near the right edge
        // put its text off the side of the viewport where the clip rect cut it in half -- which was
        // found by looking at a rasterised overlay rather than reasoned about (ADR-197). Sliding it
        // rather than dropping it: a label that moved a little still says which object it is over,
        // and a label that is not there says nothing at all.
        const ImVec2 size = ImGui::CalcTextSize(text);
        const ImVec2 want = at;
        at.x = std::clamp(at.x, canvas->x + 2.0f, canvas->x + canvas->width - size.x - 2.0f);
        at.y = std::clamp(at.y, canvas->y + 2.0f, canvas->y + canvas->height - size.y - 2.0f);
        // ...but only a slide, not a pin. Clamping unconditionally puts a label for something well
        // off the side of the view hard against the border, where it says nothing about where its
        // object is and lands on top of every other off-screen label doing the same thing -- two of
        // them overlapping in the top-right corner is what this looked like. Past a slide of about
        // a label's own width the object is simply not on screen, and no label is the honest
        // answer. Found the same way the clamp itself was: by looking at a rasterised overlay.
        const float slack = 24.0f;
        if (std::abs(at.x - want.x) > size.x + slack || std::abs(at.y - want.y) > size.y + slack) {
            return;
        }
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

    // ---- the navigation grid (ADR-197) ----
    //
    // First, so everything else reads over it. This is the carpet: what a walker may stand on, what
    // it is refused by, and -- with regions on -- which parts of the world are reachable from which
    // other parts. It is the answer to "my character will not cross the valley", which until now
    // had to be guessed at from the character's behaviour.
    if (!visuals.navCells.empty() && visuals.navCellSize > 0.0f) {
        const float half = visuals.navCellSize * 0.5f * 0.9f; // a hair of gap, so cells read as cells
        for (const EditorVisuals::NavCellMark& cell : visuals.navCells) {
            painter.flatQuad(cell.centre, half, cellColour(cell, visuals.navRegionColours));
        }
    }
    // The places the grid found while it was being built and nothing has ever looked at: where
    // walkable ground meets water, and the walkable local maxima. They are what `explore` picks
    // destinations from, so an author asking "why does it keep going *there*" is asking about these.
    for (const glm::vec3& point : visuals.navShore) {
        painter.ring(point, glm::vec3(0.0f, 1.0f, 0.0f), 1.1f, kShore, 10, 1.2f);
    }
    for (const glm::vec3& point : visuals.navVistas) {
        painter.line(point, point + glm::vec3(0.0f, 2.2f, 0.0f), kVista, 1.4f);
        painter.ring(point, glm::vec3(0.0f, 1.0f, 0.0f), 1.4f, kVista, 12, 1.2f);
    }

    // ---- the selected walker's route ----
    //
    // The polyline the behaviour planned, the leg it is on picked out brighter, and a ring at the
    // destination. The label is the part that does not exist anywhere else: a phase and a
    // `PathStatus`, so "standing still because the goal is unreachable" stops looking exactly like
    // "standing still because it is idling".
    for (const EditorVisuals::NavRoute& route : visuals.navRoutes) {
        const ImU32 colour = route.failed ? kRouteFailed : kRoute;
        glm::vec3 from = route.position;
        for (std::size_t i = 0; i < route.waypoints.size(); ++i) {
            const bool walking = i == route.leg;
            painter.line(from, route.waypoints[i], walking ? kRouteLeg : colour, walking ? 2.6f : 1.5f);
            // A tick at each waypoint, so a route with two legs in nearly the same direction is
            // still visibly two legs -- which is what a string-pulled path usually looks like.
            painter.ring(route.waypoints[i], glm::vec3(0.0f, 1.0f, 0.0f), 0.45f, colour, 8, 1.2f);
            from = route.waypoints[i];
        }
        if (route.hasDestination) {
            painter.ring(route.destination, glm::vec3(0.0f, 1.0f, 0.0f), 1.6f, kRouteGoal, 20, 1.6f);
            painter.line(route.destination, route.destination + glm::vec3(0.0f, 2.0f, 0.0f), kRouteGoal,
                         1.4f);
        }
        // Over the walker's head rather than at its feet, where the selection box's own name is,
        // and offset up so the two lines do not sit on each other.
        painter.label(route.position + glm::vec3(0.0f, 0.4f, 0.0f), route.label.c_str(),
                      route.failed ? kRouteFailed : kRouteLeg, ImVec2(10.0f, 6.0f));
    }

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

    // ---- the tie to a parent (ADR-188) ----
    // Parenting has no appearance of its own: an emitter parented to the cap it falls from looks
    // exactly like one dropped at the same world position, and the difference only shows when the
    // cap moves. That is how a misaligned spore fall was reported twice and why the fix could not
    // be seen to have worked. The line is the tie; the tick at the parent end says which way it
    // runs, so "this is attached to that" rather than "these two are related somehow".
    for (const EditorVisuals::ParentLink& link : visuals.parentLinks) {
        painter.line(link.childPosition, link.parentPosition, kParentLink, 1.4f);
        // A small cross at the parent's origin: the point the child's offset is measured from, which
        // is the number the inspector edits.
        const float tick = 0.35f;
        painter.line(link.parentPosition - glm::vec3(tick, 0.0f, 0.0f),
                     link.parentPosition + glm::vec3(tick, 0.0f, 0.0f), kParentLink, 1.4f);
        painter.line(link.parentPosition - glm::vec3(0.0f, tick, 0.0f),
                     link.parentPosition + glm::vec3(0.0f, tick, 0.0f), kParentLink, 1.4f);
        painter.line(link.parentPosition - glm::vec3(0.0f, 0.0f, tick),
                     link.parentPosition + glm::vec3(0.0f, 0.0f, tick), kParentLink, 1.4f);
        char text[192];
        std::snprintf(text, sizeof(text), "parent: %s", link.parent.c_str());
        painter.label(link.parentPosition, text, kParentLink);
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
