#pragma once

// Procedural graph editor (ADR-028): a lightweight ImGui canvas over `graph::Graph`. Nodes are
// draggable boxes with typed pins; links are curves between them; the side panel edits the
// selected node's parameters. The editor only mutates the graph and asks the host to re-evaluate;
// it never touches the scene directly.

#include "graph/graph.hpp"

#include <glm/glm.hpp>

#include <functional>
#include <string>
#include <vector>

namespace avgen::ui {

class GraphEditor {
public:
    // Called when the graph changed and the host should re-evaluate and re-install it.
    std::function<void()> onChanged;

    // Draws the canvas plus the inspector for `graph` (null draws a hint).
    void draw(graph::Graph* graph);
    [[nodiscard]] const std::string& selected() const { return selected_; }
    void select(std::string node) { selected_ = std::move(node); }

private:
    void drawCanvas(graph::Graph& graph);
    void drawNodeInspector(graph::Graph& graph);
    void drawAddMenu(graph::Graph& graph);
    // Screen position of a pin, for the link curves.
    [[nodiscard]] glm::vec2 pinPosition(const graph::Graph& graph, const std::string& node, const std::string& pin,
                                        bool input) const;

    std::string selected_;
    // Drag state for a link being made.
    std::string linkFromNode_;
    std::string linkFromPin_;
    bool linkFromInput_ = false;
    glm::vec2 canvasOrigin_{0.0f};
    glm::vec2 pan_{0.0f};
    float zoom_ = 1.0f;
    char search_[64] = "";
    std::string lastError_;
    // Node screen rectangles from the last frame (for pin hit-testing and link drawing).
    struct NodeRect {
        std::string name;
        glm::vec2 min{0.0f};
        glm::vec2 max{0.0f};
        std::vector<glm::vec2> inputs;
        std::vector<glm::vec2> outputs;
    };
    std::vector<NodeRect> rects_;
};

} // namespace avgen::ui
