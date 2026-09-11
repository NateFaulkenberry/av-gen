#include "app/settings.hpp"

#include "core/log.hpp"

#include <algorithm>
#include <fstream>
#include <system_error>

namespace avgen::app {

using nlohmann::json;

json AppSettings::toJson() const {
    json doc;
    doc["format"] = kFormatName;
    doc["version"] = kFormatVersion;
    doc["general"] = json{{"canvasRenderScale", canvasRenderScale}};
    doc["ai"] = ai.toJson();
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
