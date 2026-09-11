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

void applyTheme(app::AppearanceTheme preference) {
    ImGuiStyle& style = ImGui::GetStyle();
    const bool light = resolveTheme(preference) == ResolvedTheme::Light;
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
    style.FrameBorderSize = 0.0f;
    style.PopupBorderSize = 1.0f;
    style.ScrollbarSize = 13.0f;
    style.GrabMinSize = 10.0f;

    ImVec4* c = style.Colors;
    const ImVec4 accent = light ? ImVec4(0.12f, 0.35f, 0.72f, 1.0f) : ImVec4(0.32f, 0.62f, 0.96f, 1.0f);
    const ImVec4 accentHover = light ? ImVec4(0.18f, 0.43f, 0.82f, 1.0f) : ImVec4(0.42f, 0.70f, 1.0f, 1.0f);
    c[ImGuiCol_WindowBg] = light ? ImVec4(0.94f, 0.95f, 0.97f, 1.0f) : ImVec4(0.075f, 0.085f, 0.105f, 1.0f);
    c[ImGuiCol_ChildBg] = light ? ImVec4(0.90f, 0.92f, 0.95f, 1.0f) : ImVec4(0.095f, 0.105f, 0.13f, 1.0f);
    c[ImGuiCol_PopupBg] = light ? ImVec4(0.98f, 0.98f, 0.99f, 1.0f) : ImVec4(0.12f, 0.13f, 0.16f, 1.0f);
    c[ImGuiCol_Border] = light ? ImVec4(0.72f, 0.75f, 0.80f, 0.65f) : ImVec4(0.22f, 0.25f, 0.31f, 0.8f);
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_Text] = light ? ImVec4(0.10f, 0.12f, 0.16f, 1.0f) : ImVec4(0.90f, 0.92f, 0.96f, 1.0f);
    c[ImGuiCol_TextDisabled] = light ? ImVec4(0.42f, 0.45f, 0.50f, 1.0f) : ImVec4(0.48f, 0.52f, 0.60f, 1.0f);
    c[ImGuiCol_FrameBg] = light ? ImVec4(0.84f, 0.86f, 0.90f, 1.0f) : ImVec4(0.13f, 0.145f, 0.18f, 1.0f);
    c[ImGuiCol_FrameBgHovered] = light ? ImVec4(0.78f, 0.82f, 0.88f, 1.0f) : ImVec4(0.18f, 0.21f, 0.27f, 1.0f);
    c[ImGuiCol_FrameBgActive] = light ? ImVec4(0.72f, 0.78f, 0.87f, 1.0f) : ImVec4(0.21f, 0.27f, 0.36f, 1.0f);
    c[ImGuiCol_TitleBg] = c[ImGuiCol_WindowBg];
    c[ImGuiCol_TitleBgActive] = light ? ImVec4(0.86f, 0.89f, 0.94f, 1.0f) : ImVec4(0.10f, 0.115f, 0.15f, 1.0f);
    c[ImGuiCol_MenuBarBg] = c[ImGuiCol_ChildBg];
    c[ImGuiCol_ScrollbarBg] = c[ImGuiCol_ChildBg];
    c[ImGuiCol_ScrollbarGrab] = light ? ImVec4(0.62f, 0.66f, 0.73f, 1.0f) : ImVec4(0.27f, 0.31f, 0.39f, 1.0f);
    c[ImGuiCol_Button] = light ? ImVec4(0.80f, 0.83f, 0.88f, 1.0f) : ImVec4(0.16f, 0.18f, 0.23f, 1.0f);
    c[ImGuiCol_ButtonHovered] = accentHover;
    c[ImGuiCol_ButtonActive] = accent;
    c[ImGuiCol_Header] = light ? ImVec4(0.78f, 0.83f, 0.91f, 1.0f) : ImVec4(0.15f, 0.22f, 0.32f, 1.0f);
    c[ImGuiCol_HeaderHovered] = accentHover;
    c[ImGuiCol_HeaderActive] = accent;
    c[ImGuiCol_Tab] = c[ImGuiCol_ChildBg];
    c[ImGuiCol_TabHovered] = accentHover;
    c[ImGuiCol_TabActive] = light ? ImVec4(0.78f, 0.84f, 0.93f, 1.0f) : ImVec4(0.16f, 0.25f, 0.38f, 1.0f);
    c[ImGuiCol_CheckMark] = accent;
    c[ImGuiCol_SliderGrab] = accent;
    c[ImGuiCol_SliderGrabActive] = accentHover;
    c[ImGuiCol_NavHighlight] = accent;
    c[ImGuiCol_Separator] = c[ImGuiCol_Border];
    c[ImGuiCol_SeparatorHovered] = accentHover;
    c[ImGuiCol_SeparatorActive] = accent;
}

} // namespace avgen::ui
