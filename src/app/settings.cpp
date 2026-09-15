#include "app/settings.hpp"

#include "core/log.hpp"

#include <algorithm>
#include <fstream>
#include <system_error>
#include <cstdint>

namespace avgen::app {

using nlohmann::json;

const char* appearanceThemeName(AppearanceTheme theme) {
    switch (theme) {
    case AppearanceTheme::System: return "System";
    case AppearanceTheme::Dark: return "Dark";
    case AppearanceTheme::Light: return "Light";
    }
    return "System";
}

bool appearanceThemeFromName(const std::string& name, AppearanceTheme& out) {
    for (const auto theme : {AppearanceTheme::System, AppearanceTheme::Dark, AppearanceTheme::Light}) {
        if (name == appearanceThemeName(theme)) {
            out = theme;
            return true;
        }
    }
    return false;
}

namespace {

// The Auto-director's controls, by the names the `--director` flag already uses for the same
// fields. One spelling for a setting whether it arrives from a command line or a preferences file
// is worth more than a prettier key.
json directorToJson(const AutoDirectorSettings& s) {
    return json{{"mode", directorModeName(s.mode)},
                {"minShot", s.minShotSeconds},
                {"minBuildShot", s.minBuildShotSeconds},
                {"maxShot", s.maxShotSeconds},
                {"wide", s.wideFocalLength},
                {"hero", s.heroFocalLength},
                {"maxSpeed", s.maxCameraSpeed},
                {"maxSwing", s.maxViewRate},
                {"dwell", s.dwellShots},
                {"seed", s.seed}};
}

// Refused rather than silently reset, the way the AI section is. This file is machine-written, so
// the only way to a value outside `validate()`'s range is somebody editing it by hand -- and being
// told which field is wrong beats launching with defaults and wondering where the settings went.
Result<AutoDirectorSettings> directorFromJson(const json& doc) {
    if (!doc.is_object()) {
        return fail("director settings must be a JSON object");
    }
    AutoDirectorSettings out;
    if (const auto mode = doc.find("mode"); mode != doc.end()) {
        if (!mode->is_string()) {
            return fail("director.mode must be a string");
        }
        const auto parsed = directorModeFromName(mode->get<std::string>());
        if (!parsed) {
            return fail("director.mode '{}' is not a shot mode", mode->get<std::string>());
        }
        out.mode = *parsed;
    }
    out.minShotSeconds = doc.value("minShot", out.minShotSeconds);
    out.minBuildShotSeconds = doc.value("minBuildShot", out.minBuildShotSeconds);
    out.maxShotSeconds = doc.value("maxShot", out.maxShotSeconds);
    out.wideFocalLength = doc.value("wide", out.wideFocalLength);
    out.heroFocalLength = doc.value("hero", out.heroFocalLength);
    out.maxCameraSpeed = doc.value("maxSpeed", out.maxCameraSpeed);
    out.maxViewRate = doc.value("maxSwing", out.maxViewRate);
    out.dwellShots = doc.value("dwell", out.dwellShots);
    out.seed = doc.value("seed", out.seed);
    if (auto ok = out.validate(); !ok) {
        return std::unexpected(ok.error());
    }
    return out;
}

} // namespace

json AppSettings::toJson() const {
    json doc;
    doc["format"] = kFormatName;
    doc["version"] = kFormatVersion;
    doc["general"] = json{{"canvasRenderScale", canvasRenderScale}, {"appearance", appearanceThemeName(appearance)}};
    doc["ai"] = ai.toJson();
    doc["director"] = directorToJson(director);
    return doc;
}

Result<AppSettings> AppSettings::fromJson(const json& doc) {
    if (!doc.is_object()) {
        return fail("settings must be a JSON object");
    }
    if (doc.value("format", std::string{}) != kFormatName) {
        return fail("not an avgen settings document");
    }
    const int version = doc.value("version", 0);
    if (version > kFormatVersion) {
        return fail("settings version {} is newer than this build understands ({})", version,
                    kFormatVersion);
    }
    AppSettings out;
    if (const auto general = doc.find("general"); general != doc.end() && general->is_object()) {
        out.canvasRenderScale = std::clamp(general->value("canvasRenderScale", 1.0f), 0.25f, 2.0f);
        if (const auto appearance = general->find("appearance"); appearance != general->end()) {
            if (!appearance->is_string() || !appearanceThemeFromName(appearance->get<std::string>(), out.appearance)) {
                return fail("general.appearance must be System, Dark or Light");
            }
        }
    }
    if (const auto ai = doc.find("ai"); ai != doc.end()) {
        auto parsed = ai::AiSettings::fromJson(*ai);
        if (!parsed) {
            // Deliberately a hard failure rather than a silent reset. The one way this fires in
            // practice is a settings file that somehow carries a credential, and whoever put it
            // there needs to be told rather than have it quietly ignored -- or quietly used.
            return std::unexpected(parsed.error());
        }
        out.ai = std::move(*parsed);
    }
    // No version bump for this section, deliberately. `version` guards the *format*, and a build
    // that predates this key ignores it and rewrites the file without it -- which loses the
    // director settings and nothing else. Writing version 2 would instead make that build refuse
    // the whole document and lose the provider configuration too, which is a worse trade for a
    // section that is additive in both directions.
    if (const auto d = doc.find("director"); d != doc.end()) {
        auto parsed = directorFromJson(*d);
        if (!parsed) {
            return std::unexpected(parsed.error());
        }
        out.director = *parsed;
    }
    return out;
}

Result<void> AppSettings::save(const std::filesystem::path& path) const {
    if (path.empty()) {
        return {};
    }
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    // Beside-and-renamed: a crash mid-write must not leave a half-written file that reads as
    // corrupt settings on the next launch and loses the user's provider configuration.
    const std::filesystem::path temp = path.string() + ".tmp";
    {
        std::ofstream out(temp, std::ios::trunc);
        if (!out) {
            return fail("cannot write '{}'", temp.string());
        }
        out << toJson().dump(2) << '\n';
        if (!out) {
            return fail("failed writing '{}'", temp.string());
        }
    }
    std::filesystem::rename(temp, path, ec);
    if (ec) {
        std::filesystem::remove(temp, ec);
        return fail("cannot replace '{}': {}", path.string(), ec.message());
    }
    return {};
}

Result<AppSettings> AppSettings::load(const std::filesystem::path& path) {
    if (path.empty()) {
        return AppSettings{};
    }
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return AppSettings{}; // a first run, not a fault
    }
    std::ifstream in(path);
    if (!in) {
        return fail("cannot open '{}'", path.string());
    }
    json doc = json::parse(in, nullptr, false);
    if (doc.is_discarded()) {
        return fail("'{}' is not valid JSON", path.string());
    }
    return fromJson(doc);
}

std::filesystem::path AppSettings::pathIn(const std::filesystem::path& preferencesDir) {
    return preferencesDir.empty() ? std::filesystem::path{} : preferencesDir / "settings.json";
}

} // namespace avgen::app
