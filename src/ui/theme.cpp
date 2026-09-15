#include "ui/theme.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <string>

namespace avgen::ui {

ResolvedTheme resolveTheme(app::AppearanceTheme preference) {
    if (preference == app::AppearanceTheme::Light) {
        return ResolvedTheme::Light;
    }
    if (preference == app::AppearanceTheme::Dark) {
        return ResolvedTheme::Dark;
    }
    return SDL_GetSystemTheme() == SDL_SYSTEM_THEME_LIGHT ? ResolvedTheme::Light : ResolvedTheme::Dark;
}

const char* appearanceThemeName(app::AppearanceTheme theme) {
    switch (theme) {
    case app::AppearanceTheme::System: return "System";
    case app::AppearanceTheme::Dark: return "Dark";
    case app::AppearanceTheme::Light: return "Light";
    }
    return "System";
}

bool appearanceThemeFromName(const std::string& name, app::AppearanceTheme& out) {
    for (const auto theme : {app::AppearanceTheme::System, app::AppearanceTheme::Dark,
                             app::AppearanceTheme::Light}) {
        if (name == ui::appearanceThemeName(theme)) {
            out = theme;
            return true;
        }
    }
    return false;
}

namespace {

ImU32 rgba(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, a); }

// The dark palette is the one this application is designed in; the light one is a courtesy for a
// person whose system is set that way, and it is derived by role rather than by inverting numbers.
Palette darkPalette() {
    Palette p;
    p.ground        = rgba(14, 16, 20);
    p.panel         = rgba(19, 22, 27);
    p.lane          = rgba(24, 28, 35);
    p.raised        = rgba(38, 44, 55);
    p.control       = rgba(46, 53, 66);

    p.border        = rgba(56, 63, 76, 200);
    p.borderStrong  = rgba(120, 132, 152, 235);

    p.text          = rgba(230, 234, 242);
    p.textMuted     = rgba(150, 159, 175);
    p.textDisabled  = rgba(104, 112, 128);

    p.hover         = rgba(52, 60, 74);
    p.selected      = rgba(42, 78, 126);
    p.active        = rgba(56, 104, 168);
    p.playing       = rgba(82, 122, 172, 46);

    p.accent        = rgba(82, 158, 245);
    p.accentMuted   = rgba(60, 104, 158);
    p.processing    = rgba(92, 164, 236);
    p.warning       = rgba(226, 168, 72);
    p.error         = rgba(226, 96, 88);
    p.success       = rgba(104, 190, 128);

    // Warm, because everything else on the strip is cool. The playhead is the one mark that has to
    // be findable without looking for it, and hue separation does that where brightness cannot --
    // a brighter blue line among blue blocks reads as a nearer blue block.
    p.playhead      = rgba(255, 196, 92);
    return p;
}

Palette lightPalette() {
    Palette p;
    p.ground        = rgba(226, 229, 234);
    p.panel         = rgba(240, 242, 245);
    p.lane          = rgba(231, 234, 239);
    p.raised        = rgba(250, 251, 253);
    p.control       = rgba(255, 255, 255);

    p.border        = rgba(178, 185, 196, 220);
    p.borderStrong  = rgba(92, 102, 118, 235);

    p.text          = rgba(24, 28, 36);
    p.textMuted     = rgba(96, 104, 118);
    p.textDisabled  = rgba(148, 155, 166);

    p.hover         = rgba(214, 221, 231);
    p.selected      = rgba(178, 203, 235);
    p.active        = rgba(140, 178, 226);
    p.playing       = rgba(94, 140, 200, 40);

    p.accent        = rgba(30, 106, 200);
    p.accentMuted   = rgba(122, 160, 210);
    p.processing    = rgba(38, 116, 208);
    p.warning       = rgba(176, 118, 20);
    p.error         = rgba(184, 54, 48);
    p.success       = rgba(46, 132, 70);

    p.playhead      = rgba(198, 116, 10);
    return p;
}

Palette g_palette = darkPalette();

} // namespace

const Palette& palette() { return g_palette; }

void applyTheme(app::AppearanceTheme preference) {
    ImGuiStyle& style = ImGui::GetStyle();
    const bool light = resolveTheme(preference) == ResolvedTheme::Light;
    g_palette = light ? lightPalette() : darkPalette();
    ImGui::StyleColorsDark(&style);
    if (light) {
        ImGui::StyleColorsLight(&style);
    }

    style.WindowPadding = ImVec2(12.0f, 10.0f);
    style.FramePadding = ImVec2(8.0f, 5.0f);
    style.ItemSpacing = ImVec2(8.0f, 6.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
    style.WindowRounding = 5.0f;
    style.ChildRounding = 4.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 5.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding = 3.0f;
    style.TabRounding = 4.0f;
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.ScrollbarSize = 13.0f;
    style.GrabMinSize = 10.0f;

    // ---- the affordances a pointer has to be able to find (the brief's section 15) -------------
    //
    // A divider a person can see and cannot grab is worse than no divider: it advertises a
    // resize and then refuses one. These are the two numbers that decide whether a panel edge is
    // reachable, and both were at Dear ImGui's defaults, which are sized for a demo window rather
    // than for a docked editor somebody rearranges all day.
    //
    // `DockingSeparatorSize` is what is *drawn* between two docked panels, and
    // `WindowBorderHoverPadding` is how far either side of a border still counts as being on it --
    // the second is the one that actually matters, because it is the hit target rather than the
    // picture of one. Four points of reach around a two-point line is a ten-point target, which is
    // comfortable without making the border ambiguous with the first widget inside the panel.
    style.DockingSeparatorSize = 2.0f;
    style.WindowBorderHoverPadding = 4.0f;
    // Slightly generous around every other widget too. ImGui's own note warns against growing this
    // far, because overlapping widgets resolve to whichever was submitted first -- one point is
    // below the size of any gap in this UI, so nothing can overlap that did not already.
    style.TouchExtraPadding = ImVec2(1.0f, 1.0f);

    // ---- menus and popups (the addendum's section 8) ------------------------------------------
    //
    // Context menus are the one surface a person reads under time pressure, so they get more room
    // per row than a panel does and a tighter gap between rows, which is what makes a list scan as
    // a list rather than as a column of buttons.
    style.PopupRounding = 6.0f;
    style.WindowMenuButtonPosition = ImGuiDir_None;
    // A disabled item has to read as *present but unavailable*, not as absent. ImGui's default
    // multiplies alpha by 0.6, which on this dark palette leaves a grey close enough to the
    // enabled text that the difference is only visible side by side.
    style.DisabledAlpha = 0.42f;
    style.SeparatorTextBorderSize = 1.0f;
    style.TabBarOverlineSize = 2.0f;

    ImVec4* c = style.Colors;
    const Palette& p = g_palette;
    const auto v = [](ImU32 col) { return ImGui::ColorConvertU32ToFloat4(col); };
    // Opaque where a window background must hide what is behind it, and alpha-carrying where the
    // role is a wash. Taking the alpha from the palette everywhere would make a translucent
    // dockspace, which on this renderer shows the previous frame through the chrome.
    const auto solid = [&v](ImU32 col) {
        ImVec4 out = v(col);
        out.w = 1.0f;
        return out;
    };

    c[ImGuiCol_WindowBg] = solid(p.panel);
    c[ImGuiCol_ChildBg] = solid(p.lane);
    c[ImGuiCol_PopupBg] = solid(p.raised);
    c[ImGuiCol_Border] = v(p.border);
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_Text] = solid(p.text);
    c[ImGuiCol_TextDisabled] = solid(p.textDisabled);

    c[ImGuiCol_FrameBg] = solid(p.control);
    c[ImGuiCol_FrameBgHovered] = solid(p.hover);
    c[ImGuiCol_FrameBgActive] = solid(p.selected);

    c[ImGuiCol_TitleBg] = solid(p.ground);
    c[ImGuiCol_TitleBgActive] = solid(p.raised);
    c[ImGuiCol_TitleBgCollapsed] = solid(p.ground);
    c[ImGuiCol_MenuBarBg] = solid(p.ground);
    c[ImGuiCol_ScrollbarBg] = solid(p.ground);
    c[ImGuiCol_ScrollbarGrab] = solid(p.raised);
    c[ImGuiCol_ScrollbarGrabHovered] = solid(p.hover);
    c[ImGuiCol_ScrollbarGrabActive] = solid(p.accentMuted);

    // A button is a raised surface that brightens, and only *commits* to the accent while it is
    // held. Before this the accent was the hover colour, so merely sweeping the pointer across a
    // toolbar lit every button in it as though it had been pressed.
    c[ImGuiCol_Button] = solid(p.raised);
    c[ImGuiCol_ButtonHovered] = solid(p.hover);
    c[ImGuiCol_ButtonActive] = solid(p.active);

    c[ImGuiCol_Header] = solid(p.selected);
    c[ImGuiCol_HeaderHovered] = solid(p.hover);
    c[ImGuiCol_HeaderActive] = solid(p.active);

    c[ImGuiCol_Tab] = solid(p.ground);
    c[ImGuiCol_TabHovered] = solid(p.hover);
    c[ImGuiCol_TabSelected] = solid(p.lane);
    c[ImGuiCol_TabDimmed] = solid(p.ground);
    c[ImGuiCol_TabDimmedSelected] = solid(p.panel);
    // The one line that says which tab is live. A selected tab differs from its neighbours by a
    // surface that is one step lighter, which is legible and quiet; the accent overline is what
    // makes it unambiguous without shouting.
    c[ImGuiCol_TabSelectedOverline] = solid(p.accent);
    c[ImGuiCol_TabDimmedSelectedOverline] = v(p.accentMuted);

    c[ImGuiCol_CheckMark] = solid(p.accent);
    c[ImGuiCol_SliderGrab] = solid(p.accentMuted);
    c[ImGuiCol_SliderGrabActive] = solid(p.accent);
    c[ImGuiCol_NavCursor] = solid(p.accent);

    c[ImGuiCol_Separator] = v(p.border);
    c[ImGuiCol_SeparatorHovered] = solid(p.accentMuted);
    c[ImGuiCol_SeparatorActive] = solid(p.accent);

    // The dock separators and the window resize grip: the panel-resize affordance (the brief's
    // section 15). They are drawn, not invisible, and they answer the pointer -- a splitter you
    // cannot see is one you have to discover by accident.
    c[ImGuiCol_ResizeGrip] = v(p.border);
    c[ImGuiCol_ResizeGripHovered] = solid(p.accentMuted);
    c[ImGuiCol_ResizeGripActive] = solid(p.accent);
    c[ImGuiCol_DockingPreview] = v(p.accentMuted);
    c[ImGuiCol_DockingEmptyBg] = solid(p.ground);

    c[ImGuiCol_PlotLines] = solid(p.accent);
    c[ImGuiCol_PlotHistogram] = solid(p.accentMuted);
    c[ImGuiCol_TableHeaderBg] = solid(p.ground);
    c[ImGuiCol_TableBorderStrong] = v(p.border);
    c[ImGuiCol_TableBorderLight] = v(p.border);
    c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt] = v(p.lane);
    c[ImGuiCol_TextSelectedBg] = solid(p.selected);
    c[ImGuiCol_DragDropTarget] = solid(p.accent);
}

} // namespace avgen::ui
