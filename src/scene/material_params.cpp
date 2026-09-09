#include "scene/material_params.hpp"

#include <utility>

namespace avgen::scene {

namespace {
template <typename T>
params::Parameter<T>* findRel(const MaterialProgramParameters& p, const std::string& rel) {
    for (params::IParameter* ip : p.all) {
        const std::string& path = ip->path();
        if (path.size() == p.prefix.size() + rel.size() && path.compare(p.prefix.size(), rel.size(), rel) == 0) {
            return dynamic_cast<params::Parameter<T>*>(ip);
        }
    }
    return nullptr;
}
template <typename T>
void copyValue(const MaterialProgramParameters& p, const std::string& rel, T& target) {
    if (auto* param = findRel<T>(p, rel)) {
        target = param->value();
    }
}
} // namespace

MaterialProgramParameters registerMaterialProgramParameters(params::ParameterSet& params, const MaterialProgram& rest,
                                                            const std::string& prefix) {
    MaterialProgramParameters p;
    p.prefix = prefix;
    std::string group = prefix;
    while (!group.empty() && group.back() == '/') {
        group.pop_back();
    }
    const auto addF = [&](const std::string& rel, const std::string& label, float value, float lo, float hi, float slo,
                          float shi) {
        params::ParamDesc<float> d;
        d.defaultValue = value;
        d.hardMin = lo;
        d.hardMax = hi;
        d.softMin = slo;
        d.softMax = shi;
        d.path = prefix + rel;
        d.label = label;
        d.group = group;
        auto& param = params.add(std::move(d));
        p.all.push_back(&param);
        return &param;
    };
    const auto addV4 = [&](const std::string& rel, const std::string& label, glm::vec4 value) {
        params::ParamDesc<glm::vec4> d;
        d.defaultValue = value;
        d.hardMin = glm::vec4(-1000.0f);
        d.hardMax = glm::vec4(1000.0f);
        d.softMin = glm::vec4(-2.0f);
        d.softMax = glm::vec4(2.0f);
        d.path = prefix + rel;
        d.label = label;
        d.group = group;
        auto& param = params.add(std::move(d));
        p.all.push_back(&param);
        return &param;
    };
    const auto addB = [&](const std::string& rel, const std::string& label, bool value) {
        params::ParamDesc<bool> d;
        d.defaultValue = value;
        d.hardMin = false;
        d.hardMax = true;
        d.path = prefix + rel;
        d.label = label;
        d.group = group;
        p.all.push_back(&params.add(std::move(d)));
    };
    p.emissionIntensity = addF("emissionIntensity", "emissionIntensity", rest.emissionIntensity, 0.0f, 100.0f, 0.0f, 8.0f);
    for (std::size_t i = 0; i < rest.ops.size(); ++i) {
        const MaterialOp& op = rest.ops[i];
        const std::string base = "op/" + std::to_string(i + 1) + "/";
        const std::string kind = materialOpKindName(op.kind);
        p.opValue.push_back(addF(base + "value", kind + "/value", op.value, -1000.0f, 1000.0f, -4.0f, 4.0f));
        addV4(base + "constant", kind + "/constant", op.constant);
        addV4(base + "constant2", kind + "/constant2", op.constant2);
        addV4(base + "constant3", kind + "/constant3", op.constant3);
        addV4(base + "constant4", kind + "/constant4", op.constant4);
        addB(base + "enabled", kind + "/enabled", op.enabled);
    }
    return p;
}

void applyMaterialProgramParameters(const MaterialProgramParameters& p, const MaterialProgram& rest,
                                    MaterialProgram& live) {
    live = rest;
    copyValue(p, "emissionIntensity", live.emissionIntensity);
    for (std::size_t i = 0; i < live.ops.size(); ++i) {
        const std::string base = "op/" + std::to_string(i + 1) + "/";
        copyValue(p, base + "value", live.ops[i].value);
        copyValue(p, base + "constant", live.ops[i].constant);
        copyValue(p, base + "constant2", live.ops[i].constant2);
        copyValue(p, base + "constant3", live.ops[i].constant3);
        copyValue(p, base + "constant4", live.ops[i].constant4);
        copyValue(p, base + "enabled", live.ops[i].enabled);
    }
}

void unregisterMaterialProgramParameters(params::ParameterSet& params, const MaterialProgramParameters& p) {
    for (params::IParameter* ip : p.all) {
        if (ip != nullptr) {
            const std::string path = ip->path();
            params.remove(path);
        }
    }
}

} // namespace avgen::scene
