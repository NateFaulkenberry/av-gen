#include "ui/graph_editor.hpp"

#include "ui/style.hpp"

#include "core/log.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>

namespace avgen::ui {

namespace {

constexpr float kNodeWidth = 168.0f;
constexpr float kRowHeight = 18.0f;
constexpr float kHeaderHeight = 24.0f;
constexpr float kPinRadius = 4.5f;

ImU32 colourForType(graph::PinType type) {
    switch (type) {
    case graph::PinType::Float:
    case graph::PinType::Int:
        return IM_COL32(150, 200, 255, 255);
    case graph::PinType::Vec2:
    case graph::PinType::Vec3:
        return IM_COL32(160, 255, 190, 255);
    case graph::PinType::Color:
        return IM_COL32(255, 210, 120, 255);
    case graph::PinType::Transform:
        return IM_COL32(220, 180, 255, 255);
    case graph::PinType::PointCloud:
        return IM_COL32(120, 220, 255, 255);
    case graph::PinType::Spline:
        return IM_COL32(255, 170, 200, 255);
    case graph::PinType::Field:
        return IM_COL32(255, 140, 90, 255);
    case graph::PinType::Mesh:
        return IM_COL32(200, 200, 200, 255);
    case graph::PinType::Sdf:
        return IM_COL32(150, 255, 240, 255);
    case graph::PinType::Material:
        return IM_COL32(255, 230, 160, 255);
    case graph::PinType::Particles:
        return IM_COL32(190, 160, 255, 255);
    case graph::PinType::Volume:
        return IM_COL32(140, 190, 210, 255);
    case graph::PinType::Bool:
        return IM_COL32(255, 150, 150, 255);
    case graph::PinType::String:
        return IM_COL32(180, 180, 160, 255);
    case graph::PinType::Any:
        break;
    }
    return IM_COL32(200, 200, 200, 255);
}

ImVec2 toIm(const glm::vec2& v) {
    return ImVec2(v.x, v.y);
}

} // namespace

void GraphEditor::draw(graph::Graph* graph) {
    if (graph == nullptr) {
        ImGui::TextDisabled("No graph in this scene.");
        ImGui::TextWrapped("A scene file gains one with a \"graph\" key naming a .graph.json file, "
                           "or load one from the Assets window.");
        return;
    }
    if (!lastError_.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "%s", lastError_.c_str());
    }
    ImGui::Text("%s", graph->name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%zu nodes, %zu links", graph->nodes.size(), graph->links.size());
    ImGui::SameLine();
    if (ImGui::SmallButton("Re-evaluate") && onChanged) {
        graph->markAllDirty();
        onChanged();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Add node")) {
        ImGui::OpenPopup("add-node");
    }
    drawAddMenu(*graph);
    ImGui::Separator();

    const float inspectorWidth = 240.0f;
    const float canvasWidth = std::max(160.0f, ImGui::GetContentRegionAvail().x - inspectorWidth - 8.0f);
    ImGui::BeginChild("canvas", ImVec2(canvasWidth, 0), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    drawCanvas(*graph);
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("inspector", ImVec2(0, 0), ImGuiChildFlags_Borders);
    const WrapText wrapChildText;
    drawNodeInspector(*graph);
    ImGui::EndChild();
}

void GraphEditor::drawCanvas(graph::Graph& graph) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 size = ImGui::GetContentRegionAvail();
    canvasOrigin_ = glm::vec2(origin.x, origin.y);
    draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(24, 26, 30, 255));

    // Pan with the middle mouse button or a drag on empty canvas.
    ImGui::InvisibleButton("canvas-bg", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
    // The graph's background pans on a middle-drag, so it says so while one is happening. Not on
    // hover: a plain arrow over empty canvas is correct, and a permanent move cursor there would
    // claim the background is draggable with any button, which it is not.
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        setHoverCursor(ImGuiMouseCursor_ResizeAll);
    }
    const bool canvasHovered = ImGui::IsItemHovered();
    if (canvasHovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        const ImVec2 delta = ImGui::GetIO().MouseDelta;
        pan_ += glm::vec2(delta.x, delta.y);
    }
    if (canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        selected_.clear();
    }
    // Grid.
    const float step = 32.0f * zoom_;
    for (float x = std::fmod(pan_.x, step); x < size.x; x += step) {
        draw->AddLine(ImVec2(origin.x + x, origin.y), ImVec2(origin.x + x, origin.y + size.y),
                      IM_COL32(255, 255, 255, 10));
    }
    for (float y = std::fmod(pan_.y, step); y < size.y; y += step) {
        draw->AddLine(ImVec2(origin.x, origin.y + y), ImVec2(origin.x + size.x, origin.y + y),
                      IM_COL32(255, 255, 255, 10));
    }

    graph::NodeRegistry& registry = graph::NodeRegistry::instance();
    rects_.clear();
    rects_.reserve(graph.nodes.size());

    // Nodes.
    for (graph::Node& node : graph.nodes) {
        const graph::NodeTypeInfo* info = registry.find(node.type);
        const std::size_t inputs = info != nullptr ? info->inputs.size() : 0;
        const std::size_t outputs = info != nullptr ? info->outputs.size() : 0;
        const float height = kHeaderHeight + kRowHeight * static_cast<float>(std::max(inputs, outputs)) + 8.0f;
        const glm::vec2 topLeft = glm::vec2(origin.x, origin.y) + pan_ + node.position * zoom_;
        const ImVec2 a = toIm(topLeft);
        const ImVec2 b = ImVec2(a.x + kNodeWidth * zoom_, a.y + height * zoom_);

        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(node.name.c_str());
        ImGui::InvisibleButton("node", ImVec2(b.x - a.x, kHeaderHeight * zoom_));
        // A node's header is what moves it, and nothing said so: the pointer showed an arrow over
        // the one strip of a node that is draggable and an arrow over the rest of it too.
        if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
            setHoverCursor(ImGuiMouseCursor_ResizeAll);
        }
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            const ImVec2 delta = ImGui::GetIO().MouseDelta;
            node.position += glm::vec2(delta.x, delta.y) / zoom_;
        }
        if (ImGui::IsItemClicked()) {
            selected_ = node.name;
        }
        ImGui::PopID();

        const bool isSelected = selected_ == node.name;
        const ImU32 body = node.enabled ? IM_COL32(44, 48, 56, 240) : IM_COL32(40, 40, 40, 200);
        draw->AddRectFilled(a, b, body, 4.0f);
        draw->AddRect(a, b, isSelected ? IM_COL32(255, 200, 100, 255) : IM_COL32(90, 96, 110, 255), 4.0f, 0, 
                      isSelected ? 2.0f : 1.0f);
        draw->AddRectFilled(a, ImVec2(b.x, a.y + kHeaderHeight * zoom_), IM_COL32(60, 66, 78, 255), 4.0f,
                            ImDrawFlags_RoundCornersTop);
        draw->AddText(ImVec2(a.x + 6.0f, a.y + 4.0f), IM_COL32(230, 232, 236, 255), node.name.c_str());

        NodeRect rect;
        rect.name = node.name;
        rect.min = glm::vec2(a.x, a.y);
        rect.max = glm::vec2(b.x, b.y);
        for (std::size_t i = 0; i < inputs; ++i) {
            const ImVec2 p(a.x, a.y + (kHeaderHeight + kRowHeight * (static_cast<float>(i) + 0.5f)) * zoom_);
            draw->AddCircleFilled(p, kPinRadius, colourForType(info->inputs[i].type));
            draw->AddText(ImVec2(p.x + 7.0f, p.y - 7.0f), IM_COL32(180, 184, 192, 255), info->inputs[i].name.c_str());
            rect.inputs.emplace_back(p.x, p.y);
        }
        for (std::size_t i = 0; i < outputs; ++i) {
            const ImVec2 p(b.x, a.y + (kHeaderHeight + kRowHeight * (static_cast<float>(i) + 0.5f)) * zoom_);
            draw->AddCircleFilled(p, kPinRadius, colourForType(info->outputs[i].type));
            const ImVec2 textSize = ImGui::CalcTextSize(info->outputs[i].name.c_str());
            draw->AddText(ImVec2(p.x - 7.0f - textSize.x, p.y - 7.0f), IM_COL32(180, 184, 192, 255),
                          info->outputs[i].name.c_str());
            rect.outputs.emplace_back(p.x, p.y);
        }
        rects_.push_back(std::move(rect));
    }

    // Links.
    for (const graph::Link& link : graph.links) {
        const glm::vec2 from = pinPosition(graph, link.fromNode, link.fromPin, false);
        const glm::vec2 to = pinPosition(graph, link.toNode, link.toPin, true);
        if (from == glm::vec2(0.0f) || to == glm::vec2(0.0f)) {
            continue;
        }
        const float dx = std::max(30.0f, std::abs(to.x - from.x) * 0.5f);
        draw->AddBezierCubic(toIm(from), ImVec2(from.x + dx, from.y), ImVec2(to.x - dx, to.y), toIm(to),
                             IM_COL32(190, 195, 205, 220), 2.0f);
    }

    // Pin interaction: click an output then an input (or the reverse) to connect.
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        for (const NodeRect& rect : rects_) {
            const graph::NodeTypeInfo* info = registry.find(graph.findNode(rect.name)->type);
            if (info == nullptr) {
                continue;
            }
            const auto hit = [&](const glm::vec2& p) {
                const float dx = mouse.x - p.x;
                const float dy = mouse.y - p.y;
                return dx * dx + dy * dy <= (kPinRadius + 3.0f) * (kPinRadius + 3.0f);
            };
            for (std::size_t i = 0; i < rect.outputs.size(); ++i) {
                if (!hit(rect.outputs[i])) {
                    continue;
                }
                if (!linkFromNode_.empty() && linkFromInput_) {
                    auto ok = graph.connect(rect.name, info->outputs[i].name, linkFromNode_, linkFromPin_);
                    lastError_ = ok ? std::string() : ok.error().message;
                    if (ok && onChanged) {
                        onChanged();
                    }
                    linkFromNode_.clear();
                } else {
                    linkFromNode_ = rect.name;
                    linkFromPin_ = info->outputs[i].name;
                    linkFromInput_ = false;
                }
            }
            for (std::size_t i = 0; i < rect.inputs.size(); ++i) {
                if (!hit(rect.inputs[i])) {
                    continue;
                }
                if (!linkFromNode_.empty() && !linkFromInput_) {
                    auto ok = graph.connect(linkFromNode_, linkFromPin_, rect.name, info->inputs[i].name);
                    lastError_ = ok ? std::string() : ok.error().message;
                    if (ok && onChanged) {
                        onChanged();
                    }
                    linkFromNode_.clear();
                } else if (ImGui::GetIO().KeyAlt) {
                    graph.disconnect(rect.name, info->inputs[i].name);
                    if (onChanged) {
                        onChanged();
                    }
                } else {
                    linkFromNode_ = rect.name;
                    linkFromPin_ = info->inputs[i].name;
                    linkFromInput_ = true;
                }
            }
        }
    }
    // The link being dragged.
    if (!linkFromNode_.empty()) {
        const glm::vec2 from = pinPosition(graph, linkFromNode_, linkFromPin_, linkFromInput_);
        if (from != glm::vec2(0.0f)) {
            draw->AddLine(toIm(from), ImGui::GetIO().MousePos, IM_COL32(255, 220, 120, 200), 2.0f);
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            linkFromNode_.clear();
        }
    }
}

glm::vec2 GraphEditor::pinPosition(const graph::Graph& graph, const std::string& node, const std::string& pin,
                                   bool input) const {
    const graph::NodeTypeInfo* info = nullptr;
    if (const graph::Node* n = graph.findNode(node)) {
        info = graph::NodeRegistry::instance().find(n->type);
    }
    if (info == nullptr) {
        return glm::vec2(0.0f);
    }
    const auto& pins = input ? info->inputs : info->outputs;
    for (std::size_t i = 0; i < pins.size(); ++i) {
        if (pins[i].name != pin) {
            continue;
        }
        for (const NodeRect& rect : rects_) {
            if (rect.name != node) {
                continue;
            }
            const auto& list = input ? rect.inputs : rect.outputs;
            if (i < list.size()) {
                return list[i];
            }
        }
    }
    return glm::vec2(0.0f);
}

void GraphEditor::drawAddMenu(graph::Graph& graph) {
    if (!ImGui::BeginPopup("add-node")) {
        return;
    }
    ImGui::SetNextItemWidth(200.0f);
    ImGui::InputText("search", search_, sizeof(search_));
    const std::string needle(search_);
    graph::NodeRegistry& registry = graph::NodeRegistry::instance();
    ImGui::BeginChild("types", ImVec2(260, 320));
    graph::NodeCategory current = graph::NodeCategory::Generators;
    bool first = true;
    for (const graph::NodeTypeInfo* info : registry.types()) {
        if (!needle.empty() && info->type.find(needle) == std::string::npos &&
            info->label.find(needle) == std::string::npos) {
            continue;
        }
        if (first || info->category != current) {
            current = info->category;
            first = false;
            ImGui::SeparatorText(graph::nodeCategoryName(current));
        }
        if (ImGui::Selectable(info->type.c_str())) {
            auto added = graph.addNode(info->type, {}, glm::vec2(40.0f, 40.0f) - pan_);
            if (added) {
                selected_ = (*added)->name;
                lastError_.clear();
                if (onChanged) {
                    onChanged();
                }
            } else {
                lastError_ = added.error().message;
            }
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::IsItemHovered() && !info->description.empty()) {
            tooltip("%s", info->description.c_str());
        }
    }
    ImGui::EndChild();
    ImGui::EndPopup();
}

void GraphEditor::drawNodeInspector(graph::Graph& graph) {
    graph::Node* node = graph.findNode(selected_);
    if (node == nullptr) {
        ImGui::TextDisabled("Select a node.");
        ImGui::TextWrapped("Click a pin, then a compatible pin on another node, to connect. "
                           "Alt-click an input to disconnect it. Middle-drag to pan.");
        return;
    }
    ImGui::Text("%s", node->name.c_str());
    ImGui::TextDisabled("%s", node->type.c_str());
    if (ImGui::Checkbox("enabled", &node->enabled) && onChanged) {
        onChanged();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Delete")) {
        graph.removeNode(node->name);
        selected_.clear();
        if (onChanged) {
            onChanged();
        }
        return;
    }
    const graph::NodeTypeInfo* info = graph::NodeRegistry::instance().find(node->type);
    if (info == nullptr) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "unknown node type");
        return;
    }
    if (!info->description.empty()) {
        ImGui::TextWrapped("%s", info->description.c_str());
    }
    ImGui::Separator();
    bool changed = false;
    for (const graph::ParamInfo& param : info->params) {
        ImGui::PushID(param.name.c_str());
        nlohmann::json& value = node->params[param.name];
        switch (param.type) {
        case graph::PinType::Bool: {
            bool b = value.is_boolean() ? value.get<bool>() : false;
            if (ImGui::Checkbox(param.name.c_str(), &b)) {
                value = b;
                changed = true;
            }
            break;
        }
        case graph::PinType::Int: {
            int v = value.is_number() ? value.get<int>() : 0;
            if (!param.choices.empty()) {
                std::vector<const char*> items;
                items.reserve(param.choices.size());
                for (const std::string& c : param.choices) {
                    items.push_back(c.c_str());
                }
                v = std::clamp(v, 0, static_cast<int>(items.size()) - 1);
                if (ImGui::Combo(param.name.c_str(), &v, items.data(), static_cast<int>(items.size()))) {
                    value = v;
                    changed = true;
                }
            } else if (ImGui::DragInt(param.name.c_str(), &v, 1.0f, static_cast<int>(param.min),
                                      static_cast<int>(param.max))) {
                value = v;
                changed = true;
            }
            break;
        }
        case graph::PinType::String: {
            char buffer[128] = "";
            const std::string text = value.is_string() ? value.get<std::string>() : std::string();
            std::snprintf(buffer, sizeof(buffer), "%s", text.c_str());
            if (ImGui::InputText(param.name.c_str(), buffer, sizeof(buffer))) {
                value = std::string(buffer);
                changed = true;
            }
            break;
        }
        case graph::PinType::Vec2:
        case graph::PinType::Vec3:
        case graph::PinType::Color: {
            const int n = param.type == graph::PinType::Vec2 ? 2 : 3;
            float v[4] = {0.0f, 0.0f, 0.0f, 1.0f};
            if (value.is_array()) {
                for (int i = 0; i < n && i < static_cast<int>(value.size()); ++i) {
                    v[i] = value[static_cast<std::size_t>(i)].get<float>();
                }
            }
            const bool edited = param.type == graph::PinType::Color
                                    ? ImGui::ColorEdit3(param.name.c_str(), v, ImGuiColorEditFlags_Float)
                                    : ImGui::DragFloat3(param.name.c_str(), v, 0.05f);
            if (edited) {
                nlohmann::json arr = nlohmann::json::array();
                for (int i = 0; i < n; ++i) {
                    arr.push_back(v[i]);
                }
                value = std::move(arr);
                changed = true;
            }
            break;
        }
        default: {
            float f = value.is_number() ? value.get<float>() : 0.0f;
            if (ImGui::DragFloat(param.name.c_str(), &f, 0.05f, param.min, param.max)) {
                value = f;
                changed = true;
            }
            break;
        }
        }
        ImGui::PopID();
    }
    if (changed && onChanged) {
        onChanged();
    }
}

} // namespace avgen::ui
