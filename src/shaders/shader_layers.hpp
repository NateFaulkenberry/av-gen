#pragma once

// GPU-free state of user shader layers (milestone 0.4). A layer is a parsed user shader, a stage
// (Background: drawn behind the scene geometry; Post: applied to the HDR scene image before tone
// mapping), its inputs registered as parameters "shader/<name>/<input>", the packed uniform bytes
// for this frame, and a version bumped on every hot reload. The GPU side (rendering::ShaderStack)
// mirrors this by (id, version).

#include "core/error.hpp"
#include "core/file_watcher.hpp"
#include "core/time.hpp"
#include "params/parameter_set.hpp"
#include "shaders/shader_format.hpp"

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace avgen::shaders {

enum class LayerStage : std::uint8_t { Background, Post };
const char* layerStageName(LayerStage stage);
Result<LayerStage> layerStageFromName(const std::string& name);

struct ShaderLayer {
    std::uint32_t id = 0;
    std::filesystem::path path;
    std::string name;             // parameter group: shader/<name>
    LayerStage stage = LayerStage::Background;
    bool enabled = true;
    std::uint64_t version = 1;    // bumped when the source changes (hot reload)
    ParsedShader parsed;          // last successfully parsed source
    InputsLayout layout;
    std::string moduleSource;     // generated WGSL (all passes share it)
    std::string parseError;       // non-empty when the latest file version failed to parse
    std::vector<params::IParameter*> inputParams; // one per input, in order
    std::vector<std::uint8_t> packedInputs;       // this frame's uniform bytes
    StdUniforms std;                              // this frame's standard uniforms (pass fields patched by GPU side)
};

class ShaderLayerSet {
public:
    ShaderLayerSet(params::ParameterSet& params, double watchIntervalSeconds = 0.25);

    // Parses the file and registers its inputs. Returns the layer id.
    Result<std::uint32_t> add(const std::filesystem::path& path, LayerStage stage);
    bool remove(std::uint32_t id);
    void clear();
    [[nodiscard]] ShaderLayer* find(std::uint32_t id);
    [[nodiscard]] const std::vector<std::unique_ptr<ShaderLayer>>& layers() const { return layers_; }
    [[nodiscard]] std::size_t size() const { return layers_.size(); }
    bool move(std::uint32_t id, int delta); // reorder (post chain order matters)

    // Per frame: polls the watcher (reloading changed files), then packs inputs from the
    // parameters' finals and stores the standard uniforms (time/audio/beat) for every layer.
    void update(const FrameTime& time, const StdUniforms& base);
    // Forces a re-parse of one layer (UI "reload" button). Keeps the old module on failure.
    Result<void> reload(std::uint32_t id);
    [[nodiscard]] std::size_t reloadsThisSession() const { return reloads_; }

    [[nodiscard]] nlohmann::json toJson() const;
    // Replaces the layers from JSON ("shaders": [{"path", "stage", "enabled"}]); missing files are
    // skipped with a warning so a project still loads.
    Result<void> fromJson(const nlohmann::json& j);

    // Re-registers every layer's parameters (after the parameter set was cleared by a scene swap).
    void reattach();

private:
    Result<void> parseInto(ShaderLayer& layer, bool keepValues);
    void registerInputs(ShaderLayer& layer, const std::vector<std::vector<float>>* previousValues);
    void unregisterInputs(ShaderLayer& layer);
    std::string uniqueName(const std::string& base) const;

    params::ParameterSet& params_;
    FileWatcher watcher_;
    std::vector<std::unique_ptr<ShaderLayer>> layers_;
    std::uint32_t nextId_ = 1;
    std::size_t reloads_ = 0;
};

} // namespace avgen::shaders
