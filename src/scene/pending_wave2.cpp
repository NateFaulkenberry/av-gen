// TEMPORARY weak stubs so the tree links while wave-2/3 agents implement these declarations in
// their own files. Every definition here is weak: a real (strong) definition anywhere in
// avgen_core overrides it. Deleted when the last wave-2 branch merges.
#include "scene/grammar.hpp"
#include "scene/material_program.hpp"
#include "scene/procedural.hpp"
#include "scene/sdf_object.hpp"

#include <nlohmann/json.hpp>

#define AVGEN_WEAK __attribute__((weak))

namespace avgen::scene {

// ---- grammar (agent: hierarchy/grammar) ----
AVGEN_WEAK const char* grammarOpName(GrammarOp) { return "place"; }
AVGEN_WEAK std::optional<GrammarOp> grammarOpFromName(std::string_view) { return std::nullopt; }
AVGEN_WEAK Result<void> Grammar::validate() const { return {}; }
AVGEN_WEAK const GrammarRule* Grammar::find(std::string_view) const { return nullptr; }
AVGEN_WEAK std::uint64_t Grammar::structuralHash() const { return 0; }
AVGEN_WEAK spatial::PointCloud Grammar::expand() const { return spatial::PointCloud(); }
AVGEN_WEAK nlohmann::json Grammar::toJson() const { return nlohmann::json::object(); }
AVGEN_WEAK Result<Grammar> Grammar::fromJson(const nlohmann::json&) { return Grammar{}; }
AVGEN_WEAK std::uint64_t HierarchySpec::structuralHash() const { return 0; }
AVGEN_WEAK Result<void> ProceduralGeometry::validateReferences(const std::vector<ProceduralGeometry>&) { return {}; }
AVGEN_WEAK Result<MeshData> ProceduralGeometry::resolveSourceMesh(const GenerationContext&) const { return makeSourceMesh(source); }
AVGEN_WEAK std::uint64_t ProceduralGeometry::contextualHash(const GenerationContext&) const { return structuralHash(); }

// ---- path deformer / spline distribution (agent: splines) ----
AVGEN_WEAK glm::vec3 applyPathDeformer(const Deformer&, glm::vec3 p, const spatial::Spline&, float) { return p; }
AVGEN_WEAK glm::vec3 deformPointWith(const std::vector<Deformer>& stack, glm::vec3 p, const glm::mat4& world, double time,
                                     const DeformContext& ctx, glm::vec3 normal) {
    return deformPoint(stack, p, world, time, ctx.fields, normal);
}

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
