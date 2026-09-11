#pragma once

// The 2D composition (ADR-081): an ordered stack of layers drawn over the finished 3D frame.
//
// Named LayerStack and not Composition on purpose. `avgen::scene::Composition` already exists and
// is the *3D* authoring object -- a tree of nodes flattened into a Scene -- and the two would be
// indistinguishable at a call site. The word "composition" still means what an artist means by it
// in the UI, in the project file and in the documentation; the type says what it is.
//
// The stack owns:
//   * the layers, in compositing order (index 0 is drawn first, nearest the 3D render);
//   * the glyph atlas they share;
//   * the cached vertex geometry, rebuilt only when content or typography changes;
//   * the per-frame item buffer, rebuilt every frame and costing 96 bytes per drawn item.
//
// It owns nothing about the GPU, nothing about the scene, and nothing about time beyond "what
// second is it". rendering::CompositionRenderer draws what this produces.

#include "comp/glyph_atlas.hpp"
#include "comp/layer.hpp"
#include "comp/shape_layer.hpp"
#include "comp/text_layer.hpp"

#include <memory>
#include <vector>

namespace avgen::comp {

// One draw the compositor records: a contiguous span of the vertex buffer under one blend mode.
// Adjacent layers that agree on the blend mode are merged, so the common case -- a stack of
// ordinary layers -- is a single draw call however many layers there are.
struct CompositionDraw {
    std::uint32_t firstVertex = 0;
    std::uint32_t vertexCount = 0;
    BlendMode blend = BlendMode::Normal;
};

struct CompositionFrame {
    std::vector<LayerItem> items;
    std::vector<CompositionDraw> draws;
    std::uint32_t drawnLayers = 0;
    [[nodiscard]] bool empty() const { return draws.empty(); }
};

class LayerStack {
public:
    LayerStack();
    ~LayerStack();
    LayerStack(const LayerStack&) = delete;
    LayerStack& operator=(const LayerStack&) = delete;

    // The resolution the composition was authored against. Layout does not depend on it -- that is
    // the point of the coordinate system -- but the editor needs to know what shape of frame the
    // author had in mind, and a wildly different output aspect is worth saying out loud.
    Frame reference{1920, 1080};

    // ---- layers ----
    [[nodiscard]] const std::vector<std::unique_ptr<Layer>>& layers() const { return layers_; }
    [[nodiscard]] std::size_t size() const { return layers_.size(); }
    [[nodiscard]] bool empty() const { return layers_.empty(); }
    [[nodiscard]] Layer* find(std::uint32_t id);
    [[nodiscard]] const Layer* find(std::uint32_t id) const;
    [[nodiscard]] Layer* at(std::size_t index);
    [[nodiscard]] int indexOf(std::uint32_t id) const; // -1 when absent

    // Appends on top of the stack. An id of 0 (the default) is replaced with a fresh one; a
    // non-zero id already present is also replaced, so loading can never produce two layers that
    // register the same parameter paths.
    Layer& add(std::unique_ptr<Layer> layer);
    TextLayer& addText(std::string text, double startSeconds = 0.0, double endSeconds = 0.0);
    ShapeLayer& addShape(ShapeKind shape);
    bool remove(std::uint32_t id);
    Layer* duplicate(std::uint32_t id);
    // Moves a layer to a new index, clamped. Returns false for an unknown id.
    bool moveTo(std::uint32_t id, std::size_t index);
    void clear();

    // ---- parameters ----
    // Registers every layer's animatable properties. Call before the timeline binds.
    void attach(params::ParameterSet& set);
    void detach();
    // Reads the parameter bases back into the authored fields. Call before saving: the inspector
    // and the timeline write through the parameters, and a project saved from the authored fields
    // alone would lose every edit made with a slider.
    void pullAuthored();
    void pushAuthored();
    [[nodiscard]] bool attached() const { return attached_; }
    // Every parameter path the stack registers, in order.
    [[nodiscard]] std::vector<std::string> parameterPaths() const;
    // Removes one layer's parameters from the set it was attached to. Used when a layer is deleted
    // or renamed away, so the set does not accumulate paths for layers that no longer exist.
    void removeParameters(params::ParameterSet& set, const Layer& layer) const;

    // ---- per frame ----
    // Builds this frame's item buffer and draw list. Pure: the same stack, frame and time produce
    // the same bytes whether they were reached by playing forward or by seeking.
    const CompositionFrame& build(const Frame& frame, double seconds);
    [[nodiscard]] const CompositionFrame& lastFrame() const { return frame_; }
    [[nodiscard]] const std::vector<LayerVertex>& vertices() const { return vertices_; }
    // Bumped whenever `vertices()` changes, so the GPU side re-uploads only then.
    [[nodiscard]] std::uint64_t vertexVersion() const { return vertexVersion_; }
    [[nodiscard]] std::uint32_t itemCount() const { return itemCount_; }
    [[nodiscard]] GlyphAtlas& atlas() { return atlas_; }
    [[nodiscard]] const GlyphAtlas& atlas() const { return atlas_; }
    // Forces a geometry rebuild on the next build() (a font was installed, the atlas was cleared).
    void invalidateGeometry() { geometryDirty_ = true; }

    // ---- JSON ----
    // { "version": 1, "reference": {"width", "height"}, "layers": [ ... ] }
    [[nodiscard]] nlohmann::json toJson() const;
    [[nodiscard]] Result<void> fromJson(const nlohmann::json& j);
    static constexpr int kFormatVersion = 1;

private:
    struct Slot {
        std::uint32_t firstRun = 0;
        std::uint32_t runCount = 0;
        std::uint32_t firstItem = 0;
        std::uint32_t itemCount = 0;
        std::uint64_t geometryVersion = 0;
    };
    void rebuildGeometry();
    [[nodiscard]] bool geometryStale() const;
    [[nodiscard]] std::uint32_t nextId();

    std::vector<std::unique_ptr<Layer>> layers_;
    std::vector<Slot> slots_;
    std::vector<LayerVertex> vertices_;
    std::vector<LayerVertex> scratch_; // layers build into this; rebuildGeometry repacks it
    std::vector<LayerRun> runs_;
    CompositionFrame frame_;
    GlyphAtlas atlas_;
    std::uint32_t itemCount_ = 0;
    std::uint64_t vertexVersion_ = 1;
    std::uint64_t atlasVersion_ = 0;
    bool geometryDirty_ = true;
    bool attached_ = false;
};

} // namespace avgen::comp
