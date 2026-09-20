#include "ui/param_widget.hpp"

#include <imgui.h>

#include <cmath>
#include <cstddef>

namespace avgen::ui {

bool drawParameterValue(params::IParameter& param, const char* label) {
    using namespace params;
    const char* text = label != nullptr ? label : param.label().c_str();
    const std::size_t n = param.componentCount();
    float values[4] = {};
    for (std::size_t i = 0; i < n && i < 4; ++i) {
        values[i] = param.baseComponent(i);
    }
    bool changed = false;
    switch (param.kind()) {
    case ParamKind::Bool: {
        bool b = values[0] >= 0.5f;
        changed = ImGui::Checkbox(text, &b);
        values[0] = b ? 1.0f : 0.0f;
        break;
    }
    case ParamKind::Int: {
        int v = static_cast<int>(std::lround(values[0]));
        changed = ImGui::SliderInt(text, &v, static_cast<int>(param.softMin(0)),
                                   static_cast<int>(param.softMax(0)));
        values[0] = static_cast<float>(v);
        break;
    }
    case ParamKind::Color:
        changed = n == 4 ? ImGui::ColorEdit4(text, values, ImGuiColorEditFlags_Float)
                         : ImGui::ColorEdit3(text, values, ImGuiColorEditFlags_Float);
        break;
    case ParamKind::Float:
        changed = ImGui::SliderFloat(text, values, param.softMin(0), param.softMax(0));
        break;
    default: {
        // ImGui dereferences the range pointers; use the component-0 soft range for all lanes.
        const float lo = param.softMin(0);
        const float hi = param.softMax(0);
        changed = ImGui::SliderScalarN(text, ImGuiDataType_Float, values, static_cast<int>(n), &lo, &hi);
        break;
    }
    }
    if (changed) {
        for (std::size_t i = 0; i < n && i < 4; ++i) {
            param.setBaseComponent(i, values[i]);
        }
    }
    return changed;
}

} // namespace avgen::ui
