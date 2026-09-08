#pragma once

// JSON serialisation for parameters and modulation (ADR-010). Document envelope:
//   { "format": "avgen-project", "version": 1, "parameters": { path: value }, "routes": [...] }

#include "core/error.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"

#include <nlohmann/json_fwd.hpp>

#include <filesystem>
#include <string>

namespace avgen::params {

constexpr int kProjectFormatVersion = 1;
constexpr const char* kProjectFormatName = "avgen-project";

nlohmann::json parameterToJson(const IParameter& param);        // base value only
Result<void> parameterFromJson(IParameter& param, const nlohmann::json& value);

nlohmann::json chainToJson(const ProcessorChain& chain);
Result<ProcessorChain> chainFromJson(const nlohmann::json& j);

nlohmann::json routeToJson(const ModRoute& route);
Result<ModRoute> routeFromJson(const nlohmann::json& j);

// Whole document.
nlohmann::json saveProject(const ParameterSet& params, const Modulator& modulator);
// Applies values to existing parameters (unknown paths are ignored with a warning) and replaces
// the modulator's routes. The caller re-binds the modulator afterwards.
Result<void> loadProject(const nlohmann::json& doc, ParameterSet& params, Modulator& modulator);

Result<void> saveProjectFile(const std::filesystem::path& path, const ParameterSet& params, const Modulator& modulator);
Result<void> loadProjectFile(const std::filesystem::path& path, ParameterSet& params, Modulator& modulator);

} // namespace avgen::params
