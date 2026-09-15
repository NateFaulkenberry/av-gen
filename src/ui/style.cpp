#include "ui/style.hpp"

#include <algorithm>
#include <cmath>

namespace avgen::ui {
namespace {

ImVec4 toVec(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }

} // namespace

ImGuiMouseCursor cursorForBlockZone(BlockZone zone) {
    switch (zone) {
    case BlockZone::LeftEdge:
    case BlockZone::RightEdge:
        return ImGuiMouseCursor_ResizeEW;
    case BlockZone::Body:
        // See the header: there is no open-hand cursor to reach for. The move cursor is what is
        // available and it says the same thing.
        return ImGuiMouseCursor_ResizeAll;
    case BlockZone::None:
        break;
    }
    return ImGuiMouseCursor_Arrow;
}

void setHoverCursor(ImGuiMouseCursor cursor) { ImGui::SetMouseCursor(cursor); }

ImU32 mixColour(ImU32 under, ImU32 over, float alpha) {
    alpha = std::clamp(alpha, 0.0f, 1.0f);
    const ImVec4 a = toVec(under);
    const ImVec4 b = toVec(over);
    return ImGui::GetColorU32(ImVec4(a.x + (b.x - a.x) * alpha, a.y + (b.y - a.y) * alpha,
                                     a.z + (b.z - a.z) * alpha, a.w + (b.w - a.w) * alpha));
}

ImU32 interactionFill(ImU32 base, bool hovered, bool selected, bool active) {
    const Palette& p = palette();
    // The order is what stops a selected row appearing to deselect itself as the pointer crosses
    // it: being dragged beats being selected beats being hovered, and a hover over something
    // already selected *lifts the selection colour* rather than replacing it with the hover one.
    if (active) {
        return p.active;
    }
    if (selected) {
        return hovered ? mixColour(p.selected, p.hover, 0.28f) : p.selected;
    }
    if (hovered) {
        // A lift rather than a fixed colour, so a hover reads the same amount brighter on a clip,
        // a list row and a lane, whatever each of those starts from.
        return mixColour(base, p.hover, 0.55f);
    }
    return base;
}

ImU32 interactionOutline(bool hovered, bool selected, bool active) {
    const Palette& p = palette();
    if (active || selected) {
        return p.borderStrong;
    }
    if (hovered) {
        return mixColour(p.border, p.borderStrong, 0.5f);
    }
    return 0;
}

void drawProcessingIndicator(ImDrawList* draw, ImVec2 topLeft, ImVec2 bottomRight, float opacity,
                             const char* stage, float fraction, double seconds) {
    if (draw == nullptr || opacity <= 0.0f) {
        return;
    }
    opacity = std::clamp(opacity, 0.0f, 1.0f);
    const Palette& p = palette();
    const char* label = (stage != nullptr && *stage != '\0') ? stage : "Updating world";

    // A pill in the top-left of the canvas, inset. Not centred and not full-screen: the scene
    // underneath is still the thing being looked at, and a modal veil over it would be the
    // "everything has stopped" reading the brief rules out.
    const float pad = 10.0f;
    const float radius = 7.0f;
    const ImVec2 textSize = ImGui::CalcTextSize(label);
    const bool bar = fraction >= 0.0f;
    const float pillW = std::min(radius * 2.0f + 10.0f + textSize.x + 22.0f,
                                 std::max(bottomRight.x - topLeft.x - pad * 2.0f, 80.0f));
    const float pillH = bar ? 40.0f : 28.0f;
    const ImVec2 a(topLeft.x + pad, topLeft.y + pad);
    const ImVec2 b(a.x + pillW, a.y + pillH);
    if (b.x <= a.x || b.y <= a.y) {
        return;
    }

    const auto fade = [opacity](ImU32 colour, float scale = 1.0f) {
        ImVec4 v = ImGui::ColorConvertU32ToFloat4(colour);
        v.w *= opacity * scale;
        return ImGui::GetColorU32(v);
    };

    draw->AddRectFilled(a, b, fade(p.ground, 0.88f), 8.0f);
    draw->AddRect(a, b, fade(p.border), 8.0f);

    // The rotating arc. One revolution every 1.1 s, and a gap of a fifth of the circle so the
    // rotation is legible -- a full ring rotating is a ring.
    const ImVec2 centre(a.x + pad + radius - 2.0f, a.y + (bar ? 15.0f : pillH * 0.5f));
    const float spin = static_cast<float>(seconds) * (2.0f * 3.14159265f / 1.1f);
    draw->PathClear();
    draw->PathArcTo(centre, radius, spin, spin + 3.14159265f * 1.6f, 24);
    draw->PathStroke(fade(p.processing), 0, 2.0f);

    draw->AddText(ImVec2(centre.x + radius + 8.0f, a.y + (bar ? 7.0f : (pillH - textSize.y) * 0.5f)),
                  fade(p.text), label);

    if (bar) {
        // A real bar, only ever drawn for work that measures itself (ADR-064). The caller passes a
        // negative fraction when it does not know, and then there is no bar to misread.
        const ImVec2 barA(a.x + pad, b.y - 12.0f);
        const ImVec2 barB(b.x - pad, b.y - 8.0f);
        draw->AddRectFilled(barA, barB, fade(p.lane), 2.0f);
        const float t = std::clamp(fraction, 0.0f, 1.0f);
        if (t > 0.0f) {
            draw->AddRectFilled(barA, ImVec2(barA.x + (barB.x - barA.x) * t, barB.y),
                                fade(p.processing), 2.0f);
        }
    }
}

// ---- context menus -------------------------------------------------------------------------------

ContextMenu::ContextMenu(const char* id) {
    // `BeginPopupContextItem` both opens (on a right-click over the item just submitted) and
    // begins. That is the right shape everywhere a right-drag means nothing -- which is everywhere
    // except the sequencer strip, and the strip uses the other constructor.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 3.0f));
    styled_ = true;
    open_ = ImGui::BeginPopupContextItem(id);
    if (!open_) {
        ImGui::PopStyleVar(2);
        styled_ = false;
    }
}

ContextMenu::ContextMenu(const char* id, bool openNow) {
    if (openNow) {
        ImGui::OpenPopup(id);
    }
    begin(id);
}

void ContextMenu::begin(const char* id) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 3.0f));
    styled_ = true;
    open_ = ImGui::BeginPopup(id);
    if (!open_) {
        ImGui::PopStyleVar(2);
        styled_ = false;
    }
}

ContextMenu::~ContextMenu() {
    if (open_) {
        ImGui::EndPopup();
    }
    if (styled_) {
        ImGui::PopStyleVar(2);
    }
}

bool menuAction(const char* label, const char* shortcut, bool enabled) {
    return ImGui::MenuItem(label, shortcut, false, enabled);
}

bool menuToggle(const char* label, bool checked, const char* shortcut, bool enabled) {
    return ImGui::MenuItem(label, shortcut, checked, enabled);
}

void menuSubject(const std::string& text) {
    if (text.empty()) {
        return;
    }
    ImGui::PushStyleColor(ImGuiCol_Text, palette().textMuted);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopStyleColor();
    ImGui::Separator();
}

// ---- small shared widgets ------------------------------------------------------------------------

namespace {

void drawGlyph(ImDrawList* draw, Glyph glyph, ImVec2 centre, float size, ImU32 colour) {
    const float h = size * 0.5f;
    switch (glyph) {
    case Glyph::Play:
        draw->AddTriangleFilled(ImVec2(centre.x - h * 0.7f, centre.y - h),
                                ImVec2(centre.x - h * 0.7f, centre.y + h),
                                ImVec2(centre.x + h * 0.9f, centre.y), colour);
        break;
    case Glyph::Pause:
        draw->AddRectFilled(ImVec2(centre.x - h * 0.75f, centre.y - h),
                            ImVec2(centre.x - h * 0.15f, centre.y + h), colour);
        draw->AddRectFilled(ImVec2(centre.x + h * 0.15f, centre.y - h),
                            ImVec2(centre.x + h * 0.75f, centre.y + h), colour);
        break;
    case Glyph::Stop:
        draw->AddRectFilled(ImVec2(centre.x - h * 0.8f, centre.y - h * 0.8f),
                            ImVec2(centre.x + h * 0.8f, centre.y + h * 0.8f), colour, 1.0f);
        break;
    case Glyph::Loop:
        draw->PathClear();
        draw->PathArcTo(centre, h * 0.8f, 0.6f, 5.8f, 20);
        draw->PathStroke(colour, 0, 1.6f);
        draw->AddTriangleFilled(ImVec2(centre.x + h * 0.45f, centre.y - h * 0.95f),
                                ImVec2(centre.x + h * 1.05f, centre.y - h * 0.5f),
                                ImVec2(centre.x + h * 0.35f, centre.y - h * 0.25f), colour);
        break;
    case Glyph::Record:
        draw->AddCircleFilled(centre, h * 0.8f, colour, 20);
        break;
    case Glyph::Chevron:
        draw->PathClear();
        draw->PathLineTo(ImVec2(centre.x - h * 0.4f, centre.y - h * 0.6f));
        draw->PathLineTo(ImVec2(centre.x + h * 0.4f, centre.y));
        draw->PathLineTo(ImVec2(centre.x - h * 0.4f, centre.y + h * 0.6f));
        draw->PathStroke(colour, 0, 1.6f);
        break;
    case Glyph::Plus:
        draw->AddLine(ImVec2(centre.x - h * 0.75f, centre.y), ImVec2(centre.x + h * 0.75f, centre.y),
                      colour, 1.6f);
        draw->AddLine(ImVec2(centre.x, centre.y - h * 0.75f), ImVec2(centre.x, centre.y + h * 0.75f),
                      colour, 1.6f);
        break;
    case Glyph::Minus:
        draw->AddLine(ImVec2(centre.x - h * 0.75f, centre.y), ImVec2(centre.x + h * 0.75f, centre.y),
                      colour, 1.6f);
        break;
    case Glyph::Dots:
        for (int i = -1; i <= 1; ++i) {
            draw->AddCircleFilled(ImVec2(centre.x + static_cast<float>(i) * h * 0.55f, centre.y),
                                  1.6f, colour, 8);
        }
        break;
    }
}

} // namespace

bool glyphButton(const char* id, Glyph glyph, const char* tooltip, bool enabled, bool engaged,
                 ImVec2 size) {
    const float side = ImGui::GetFrameHeight();
    if (size.x <= 0.0f) {
        size.x = side;
    }
    if (size.y <= 0.0f) {
        size.y = side;
    }
    const Palette& p = palette();
    ImGui::BeginDisabled(!enabled);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(id, size);
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 b(origin.x + size.x, origin.y + size.y);
    const ImU32 fill = interactionFill(engaged ? p.selected : p.raised, hovered && enabled,
                                       engaged, held);
    draw->AddRectFilled(origin, b, fill, 4.0f);
    if (hovered && enabled) {
        draw->AddRect(origin, b, p.border, 4.0f);
    }
    drawGlyph(draw, glyph, ImVec2((origin.x + b.x) * 0.5f, (origin.y + b.y) * 0.5f),
              size.y * 0.42f, enabled ? p.text : p.textDisabled);
    ImGui::EndDisabled();
    if (tooltip != nullptr && hovered) {
        hoverTip(tooltip);
    }
    return pressed && enabled;
}

void hoverTip(const char* text, bool shortDelay) {
    if (text == nullptr || *text == '\0') {
        return;
    }
    // `ForTooltip` is what applies the context's configured hover delay *and* the "stationary"
    // rule, so a pointer sweeping across a toolbar on its way somewhere else does not leave a
    // trail of tooltips behind it. The delay itself is ImGui's (`style.HoverDelayNormal` /
    // `HoverDelayShort`); a per-call number would need `imgui_internal.h`, and two delays are
    // enough to separate "a dense toolbar" from "a label that needs explaining".
    const ImGuiHoveredFlags flags = ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled |
                                    (shortDelay ? ImGuiHoveredFlags_DelayShort : ImGuiHoveredFlags_DelayNormal);
    if (!ImGui::IsItemHovered(flags)) {
        return;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));
    ImGui::BeginTooltip();
    ImGui::TextUnformatted(text);
    ImGui::EndTooltip();
    ImGui::PopStyleVar();
}

} // namespace avgen::ui
