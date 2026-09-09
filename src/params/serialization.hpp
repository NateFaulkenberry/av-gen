#pragma once

// JSON serialisation for parameters and modulation (ADR-010). Document envelope (version 4):
//   { "format": "avgen-project", "version": 4, "parameters": { path: value }, "routes": [...],
//     "sources": [...], "presets": [...], "shaders": [...], "timeline": {...},
//     "assets": {...}, "app": { "name", "version" } }
// History: version 1 had parameters and routes only; version 2 added sources, presets and route
// polarity; version 3 declared "shaders" (written by the engine since 0.4) and "timeline";
// version 4 adds "assets" (audio/scene/environment references) and "app" (the writer), both
// filled in by the engine. Older documents are upgraded in memory by migrateProject() before
// loading; a missing "timeline" means none.

#include "core/error.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "params/preset.hpp"
#include "signals/source.hpp"

#include <filesystem>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::params {

constexpr int kProjectFormatVersion = 4;
constexpr const char* kProjectFormatName = "avgen-project";

nlohmann::json parameterToJson(const IParameter& param); // base value only
Result<void> parameterFromJson(IParameter& param, const nlohmann::json& value);

// Enum names shared with other serialisers (lower-case: "linear", "power", "log", "exp", "scurve";
// "add", "multiply", "replace", "min", "max").
[[nodiscard]] std::string_view curveTypeName(CurveType curve);
[[nodiscard]] std::optional<CurveType> curveTypeFromName(std::string_view name);
[[nodiscard]] std::string_view modOpName(ModOp op);
[[nodiscard]] std::optional<ModOp> modOpFromName(std::string_view name);

nlohmann::json chainToJson(const ProcessorChain& chain);
Result<ProcessorChain> chainFromJson(const nlohmann::json& j);

nlohmann::json routeToJson(const ModRoute& route);
Result<ModRoute> routeFromJson(const nlohmann::json& j);

struct MigrationReport {
    int fromVersion = 0;
    int toVersion = 0;
    std::vector<std::string> steps; // one human-readable line per version hop
};

// Upgrades a project document in place, one version at a time, to kProjectFormatVersion.
// Returns the report (empty steps when already current). Errors: not an object, wrong format,
// missing/invalid version, version newer than supported. Unknown keys are preserved.
Result<MigrationReport> migrateProject(nlohmann::json& doc);

// Whole document. Sources and presets are optional (nullptr = omitted / left untouched).
nlohmann::json saveProject(const ParameterSet& params, const Modulator& modulator,
                           const signals::SourceRack* sources = nullptr, const PresetBank* presets = nullptr);
// Migrates a copy of the document to the current version (the caller's document is untouched;
// each step is logged), then loads in this order: sources (rack replaced and re-attached by the
// caller), parameter values (unknown paths ignored with a warning), routes (replaced), presets
// (bank replaced). The caller attaches the rack and re-binds the modulator afterwards. Nothing
// is mutated on failure.
Result<void> loadProject(const nlohmann::json& doc, ParameterSet& params, Modulator& modulator,
                         signals::SourceRack* sources = nullptr, PresetBank* presets = nullptr);

Result<void> saveProjectFile(const std::filesystem::path& path, const ParameterSet& params,
                             const Modulator& modulator, const signals::SourceRack* sources = nullptr,
                             const PresetBank* presets = nullptr);
Result<void> loadProjectFile(const std::filesystem::path& path, ParameterSet& params, Modulator& modulator,
                             signals::SourceRack* sources = nullptr, PresetBank* presets = nullptr);

} // namespace avgen::params
