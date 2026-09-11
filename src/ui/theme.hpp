#pragma once

#include "app/settings.hpp"

#include <imgui.h>

namespace avgen::ui {

enum class ResolvedTheme { Dark, Light };

[[nodiscard]] ResolvedTheme resolveTheme(app::AppearanceTheme preference);
[[nodiscard]] const char* appearanceThemeName(app::AppearanceTheme theme);
[[nodiscard]] bool appearanceThemeFromName(const std::string& name, app::AppearanceTheme& out);

// Applies the complete global palette and metrics. Call after ImGui::CreateContext and again only
// when the preference or resolved system appearance changes.
void applyTheme(app::AppearanceTheme preference);

} // namespace avgen::ui
