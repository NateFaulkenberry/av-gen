#pragma once

// JSON serialisation for parameters and modulation (ADR-010). Document envelope (version 3):
//   { "format": "avgen-project", "version": 3, "parameters": { path: value }, "routes": [...],
//     "sources": [...], "presets": [...], "shaders": [...], "timeline": {...} }
// Version 1 documents (no sources/presets) and version 2 documents (no shaders/timeline; both
// are added by the engine) load unchanged.

#include "core/error.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "params/preset.hpp"
#include "signals/source.hpp"

#include <nlohmann/json_fwd.hpp>

#include <filesystem>
#include <string>

namespace avgen::params {

constexpr int kProjectFormatVersion = 3;
constexpr const char* kProjectFormatName = "avgen-project";

nlohmann::json parameterToJson(const IParameter& param);        // base value only
Result<void> parameterFromJson(IParameter& param, const nlohmann::json& value);

nlohmann::json chainToJson(const ProcessorChain& chain);
Result<ProcessorChain> chainFromJson(const nlohmann::json& j);

nlohmann::json routeToJson(const ModRoute& route);
Result<ModRoute> routeFromJson(const nlohmann::json& j);

// Whole document. Sources and presets are optional (nullptr = omitted / left untouched).
nlohmann::json saveProject(const ParameterSet& params, const Modulator& modulator,
                           const signals::SourceRack* sources = nullptr, const PresetBank* presets = nullptr);
// Loads in this order: sources (rack replaced and re-attached by the caller), parameter values
// (unknown paths ignored with a warning), routes (replaced), presets (bank replaced). The caller
// attaches the rack and re-binds the modulator afterwards. Nothing is mutated on failure.
Result<void> loadProject(const nlohmann::json& doc, ParameterSet& params, Modulator& modulator,
                         signals::SourceRack* sources = nullptr, PresetBank* presets = nullptr);

Result<void> saveProjectFile(const std::filesystem::path& path, const ParameterSet& params, const Modulator& modulator,
                             const signals::SourceRack* sources = nullptr, const PresetBank* presets = nullptr);
Result<void> loadProjectFile(const std::filesystem::path& path, ParameterSet& params, Modulator& modulator,
                             signals::SourceRack* sources = nullptr, PresetBank* presets = nullptr);

} // namespace avgen::params
