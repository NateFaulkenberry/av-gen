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

const char* projectionScalingName(ProjectionScaling scaling) {
    switch (scaling) {
    case ProjectionScaling::Fit: return "fit";
    case ProjectionScaling::Fill: return "fill";
    case ProjectionScaling::Stretch: return "stretch";
    }
    return "fit";
}

bool projectionScalingFromName(const std::string& name, ProjectionScaling& out) {
    for (const auto s : {ProjectionScaling::Fit, ProjectionScaling::Fill, ProjectionScaling::Stretch}) {
        if (name == projectionScalingName(s)) {
            out = s;
            return true;
        }
    }
    return false;
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

json AppSettings::toJson() const {
    json doc;
    doc["format"] = kFormatName;
    doc["version"] = kFormatVersion;
    doc["general"] = json{{"canvasRenderScale", canvasRenderScale},
                          {"liveQuality", liveQualityPinned ? std::string(liveQualityLevelToken(*liveQualityPinned))
                                                            : std::string("auto")},
                          {"liveTargetFps", liveTargetFps},
                          {"adaptiveCanvasFloor", adaptiveCanvasFloor},
                          {"liveAntialias", liveAntialias ? "fxaa" : "off"},
                          {"appearance", appearanceThemeName(appearance)},
                          {"renderFramePreview", renderFramePreview},
                          {"suspendViewportDuringRender", suspendViewportDuringRender}};
    doc["outputPreview"] = json{
        {"mode", ui::previewViewModeName(preview.mode)},
        {"outside", ui::outsideFrameName(preview.outside)},
        {"quality", ui::previewQualityName(preview.quality)},
        {"zoomFit", preview.zoom.fit},
        {"zoomScale", preview.zoom.scale},
        {"safeAreas", preview.guides.safeAreas},
        {"thirds", preview.guides.thirds},
        {"centreCross", preview.guides.centreCross},
        {"frameBorder", preview.guides.frameBorder},
        {"actionSafe", preview.guides.safe.actionFraction},
        {"titleSafe", preview.guides.safe.titleFraction},
        {"toolbar", preview.toolbar},
    };
    doc["live"] = json{{"audioInput", live.audioInput}, {"midiInput", live.midiInput}, {"smoothing", live.smoothing}};
    {
        json pj{{"display", projection.display},
                {"windowWidth", projection.windowWidth},
                {"windowHeight", projection.windowHeight},
                {"scaling", projectionScalingName(projection.scaling)}};
        if (projection.fullscreen.has_value()) {
            pj["fullscreen"] = *projection.fullscreen;
        }
        doc["projection"] = std::move(pj);
    }
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
        // ADR-1083: "auto" or a level's token. Anything else is refused rather than guessed at.
        if (const auto quality = general->find("liveQuality"); quality != general->end()) {
            const std::string name = quality->is_string() ? quality->get<std::string>() : std::string{};
            if (name == "auto") {
                out.liveQualityPinned.reset();
            } else if (const auto level = liveQualityLevelFromToken(name)) {
                out.liveQualityPinned = *level;
            } else {
                return fail("general.liveQuality must be \"auto\", \"ultra\", \"high\", \"medium\", \"low\" or "
                            "\"emergency\"");
            }
        }
        // ADR-1080. Clamped rather than refused, like the scale: a hand-edited 1000 fps is "as fast as
        // it can", not a reason to lose the rest of the file.
        out.liveTargetFps =
            std::clamp(general->value("liveTargetFps", out.liveTargetFps), kLiveTargetFpsMin, kLiveTargetFpsMax);
        // Clamped into the ladder's range (ADR-1024, ADR-1083).
        out.adaptiveCanvasFloor =
            std::clamp(general->value("adaptiveCanvasFloor", out.adaptiveCanvasFloor), kLiveScaleFloorMin, 1.0f);
        if (const auto aa = general->find("liveAntialias"); aa != general->end()) {
            const std::string name = aa->is_string() ? aa->get<std::string>() : std::string{};
            if (name != "fxaa" && name != "off") {
                return fail("general.liveAntialias must be \"fxaa\" or \"off\"");
            }
            out.liveAntialias = name == "fxaa";
        }
        out.renderFramePreview = general->value("renderFramePreview", out.renderFramePreview);
        out.suspendViewportDuringRender =
            general->value("suspendViewportDuringRender", out.suspendViewportDuringRender);
        if (const auto appearance = general->find("appearance"); appearance != general->end()) {
            if (!appearance->is_string() || !appearanceThemeFromName(appearance->get<std::string>(), out.appearance)) {
                return fail("general.appearance must be System, Dark or Light");
            }
        }
    }
    if (const auto lv = doc.find("live"); lv != doc.end() && lv->is_object()) {
        out.live.audioInput = lv->value("audioInput", out.live.audioInput);
        out.live.midiInput = lv->value("midiInput", out.live.midiInput);
        if (out.live.midiInput.empty()) {
            out.live.midiInput = "*";
        }
        out.live.smoothing = std::clamp(lv->value("smoothing", out.live.smoothing), 0.25f, 4.0f);
    }
    if (const auto pj = doc.find("projection"); pj != doc.end() && pj->is_object()) {
        // Lenient like `live`: a projector preference is never worth refusing the settings file over.
        out.projection.display = pj->value("display", out.projection.display);
        if (const auto fs = pj->find("fullscreen"); fs != pj->end() && fs->is_boolean()) {
            out.projection.fullscreen = fs->get<bool>();
        }
        const auto size = [&](const char* key) -> std::uint32_t {
            const auto it = pj->find(key);
            if (it == pj->end() || !it->is_number()) {
                return 0;
            }
            const double v = it->get<double>();
            return v >= 16.0 && v <= 16384.0 ? static_cast<std::uint32_t>(v) : 0;
        };
        out.projection.windowWidth = size("windowWidth");
        out.projection.windowHeight = size("windowHeight");
        if (out.projection.windowWidth == 0 || out.projection.windowHeight == 0) {
            out.projection.windowWidth = out.projection.windowHeight = 0;
        }
        if (const auto sc = pj->find("scaling"); sc != pj->end() && sc->is_string()) {
            static_cast<void>(projectionScalingFromName(sc->get<std::string>(), out.projection.scaling));
        }
    }
    if (const auto pv = doc.find("outputPreview"); pv != doc.end() && pv->is_object()) {
        // A name this build does not know is a hard failure rather than a silent fall back to the
        // default. The whole point of the view mode is that the editor is in a state the person put
        // it in; quietly opening in Workspace because a newer build wrote "outputStereo" would be
        // the same class of defect ADR-225 exists about, one step removed.
        if (const auto it = pv->find("mode"); it != pv->end()) {
            if (!it->is_string() || !ui::previewViewModeFromName(it->get<std::string>(), out.preview.mode)) {
                return fail("outputPreview.mode is not a view mode this build knows");
            }
        }
        if (const auto it = pv->find("outside"); it != pv->end()) {
            if (!it->is_string() || !ui::outsideFrameFromName(it->get<std::string>(), out.preview.outside)) {
                return fail("outputPreview.outside must be show, dim or hide");
            }
        }
        if (const auto it = pv->find("quality"); it != pv->end()) {
            if (!it->is_string() ||
                !ui::previewQualityFromName(it->get<std::string>(), out.preview.quality)) {
                return fail("outputPreview.quality must be draft, realtime or native");
            }
        }
        out.preview.zoom.fit = pv->value("zoomFit", out.preview.zoom.fit);
        out.preview.zoom.scale = pv->value("zoomScale", out.preview.zoom.scale);
        out.preview.guides.safeAreas = pv->value("safeAreas", out.preview.guides.safeAreas);
        out.preview.guides.thirds = pv->value("thirds", out.preview.guides.thirds);
        out.preview.guides.centreCross = pv->value("centreCross", out.preview.guides.centreCross);
        out.preview.guides.frameBorder = pv->value("frameBorder", out.preview.guides.frameBorder);
        out.preview.guides.safe.actionFraction =
            pv->value("actionSafe", out.preview.guides.safe.actionFraction);
        out.preview.guides.safe.titleFraction =
            pv->value("titleSafe", out.preview.guides.safe.titleFraction);
        out.preview.toolbar = pv->value("toolbar", out.preview.toolbar);
        // Refused rather than clamped. A title-safe area outside the action-safe area draws two
        // boxes in the wrong order and looks entirely plausible, which is precisely why a silent
        // repair here would be worse than a loud refusal.
        if (auto r = ui::validatePreviewViewState(out.preview); !r) {
            return fail("outputPreview: {}", r.error().message);
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
