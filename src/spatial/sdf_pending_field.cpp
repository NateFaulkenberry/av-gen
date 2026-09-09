// TEMPORARY (agent/sdf): minimal FieldSet lookups and a constant sampleScalar so the SDF unit
// tests link while spatial/field.cpp is written on another branch. Dropped at merge.
#include "spatial/field.hpp"

namespace avgen::spatial {

const FieldSpec* FieldSet::find(std::string_view name) const {
    for (const FieldSpec& f : fields) {
        if (f.name == name) {
            return &f;
        }
    }
    return nullptr;
}

int FieldSet::indexOf(std::string_view name) const {
    for (std::size_t i = 0; i < fields.size(); ++i) {
        if (fields[i].name == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

float sampleScalar(const FieldSpec& field, const glm::vec3& /*p*/, double /*time*/, const FieldSet* /*set*/) {
    return field.strength;
}

} // namespace avgen::spatial
