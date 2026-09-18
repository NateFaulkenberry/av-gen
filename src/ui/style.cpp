#include "ui/style.hpp"

#include <algorithm>
#include <cmath>
#include <cstdarg>

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

ImU32 withAlpha(ImU32 colour, float alpha) {
    const float a = std::clamp(alpha, 0.0f, 1.0f);
    const ImU32 rgb = colour & ~IM_COL32_A_MASK;
    const auto original = static_cast<float>((colour >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f;
    // Scaled by what the colour already had rather than replacing it, so asking for half of a
    // colour that was already half gives a quarter and not a half.
    const auto out = static_cast<ImU32>(std::lround(a * original * 255.0f));
    return rgb | (out << IM_COL32_A_SHIFT);
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

WrapText::WrapText() { ImGui::PushTextWrapPos(0.0f); }

WrapText::WrapText(float wrapPosX) { ImGui::PushTextWrapPos(wrapPosX); }

WrapText::~WrapText() { ImGui::PopTextWrapPos(); }

float itemWidthForLabel(const char* label, float trailing) {
    // ImGui hides everything from the first "##" onwards, so a label of "##id" occupies no width
    // and this correctly returns the whole region -- which is what `SetNextItemWidth(-1)` was
    // already right about for the unlabelled widgets.
    const float labelWidth = (label == nullptr) ? 0.0f : ImGui::CalcTextSize(label, nullptr, true).x;
    const float spacing = labelWidth > 0.0f ? ImGui::GetStyle().ItemInnerSpacing.x : 0.0f;
    return itemWidthBesideLabel(ImGui::GetContentRegionAvail().x, labelWidth, spacing, trailing);
}

namespace {
// The wrap width a tooltip opened right now should use. Read from the *viewport* rather than from
// the tooltip window, which does not exist yet and whose width is the thing being decided.
float currentTooltipWrap() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float viewportWidth = viewport != nullptr ? viewport->WorkSize.x : 0.0f;
    return tooltipWrapWidth(ImGui::GetFontSize(), viewportWidth);
}
} // namespace

void bulletWrapped(const char* fmt, ...) {
    ImGui::Bullet();
    ImGui::SameLine(0.0f, 0.0f);
    va_list args;
    va_start(args, fmt);
    // `TextWrappedV` keeps an outer wrap position when one is already set, which is the panel-wide
    // guard's, and falls back to the window edge when it is not.
    ImGui::TextWrappedV(fmt, args);
    va_end(args);
}

void tooltip(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(currentTooltipWrap());
    ImGui::TextV(fmt, args);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
    ImGui::PopStyleVar();
    va_end(args);
}

void tooltipUnformatted(const char* text) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(currentTooltipWrap());
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
    ImGui::PopStyleVar();
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
    tooltipUnformatted(text);
}

} // namespace avgen::ui
