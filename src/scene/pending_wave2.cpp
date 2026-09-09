// TEMPORARY weak stubs so the tree links until the SDF renderer agent lands its definitions. Every definition here is weak: a real (strong) definition anywhere in
// avgen_core overrides it. Deleted when the last wave-2 branch merges.
#include "scene/material_program.hpp"
#include "scene/sdf_object.hpp"

#include <nlohmann/json.hpp>

#define AVGEN_WEAK __attribute__((weak))

namespace avgen::scene {

// ---- sdf object (agent: sdf renderer) ----
AVGEN_WEAK const char* sdfRenderModeName(SdfRenderMode) { return "raymarch"; }
AVGEN_WEAK std::optional<SdfRenderMode> sdfRenderModeFromName(std::string_view) { return std::nullopt; }
AVGEN_WEAK Result<void> SdfObject::validate() const { return {}; }
AVGEN_WEAK bool SdfObject::rebuild(double, const spatial::FieldSet*) { return false; }
AVGEN_WEAK std::uint64_t SdfObject::structuralHash() const { return 0; }
AVGEN_WEAK nlohmann::json SdfObject::toJson() const { return nlohmann::json::object(); }
AVGEN_WEAK Result<SdfObject> SdfObject::fromJson(const nlohmann::json&) { return SdfObject{}; }
AVGEN_WEAK SdfParameters registerSdfParameters(params::ParameterSet&, const SdfObject&, const std::string& prefix) {
    SdfParameters p;
    p.prefix = prefix;
    return p;
}
AVGEN_WEAK bool applySdfParameters(const SdfParameters&, const SdfObject& rest, SdfObject& live) {
    live = rest;
    return false;
}
AVGEN_WEAK void unregisterSdfParameters(params::ParameterSet&, const SdfParameters&) {}

} // namespace avgen::scene
