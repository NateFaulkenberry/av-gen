#pragma once

// Output windows (milestone 1.2): each OutputDesc is one extra window (optionally fullscreen on a
// chosen display) with its own swapchain and an OutputMapping that places the final frame on it.
// The descriptor side (add/remove/JSON) is GPU-free and lives in output_manager.cpp so it can be
// unit-tested; open/closeAll/pumpEvents/presentAll live in output_manager_gpu.cpp.
//
// Per-frame order in the application: Window::pollEvents (primary, pumps the SDL queue) ->
// OutputManager::pumpEvents (consumes what was routed to the output windows) -> render the final
// frame into a TextureBinding texture -> presentAll.

#include "core/error.hpp"
#include "rendering/output_mapping.hpp"

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace wgpu {
class Texture;
} // namespace wgpu

namespace avgen::gpu {
class Context;
class ShaderLibrary;
} // namespace avgen::gpu

namespace avgen::rendering {
class OutputMapper;
} // namespace avgen::rendering

namespace avgen::app {

struct OutputDesc {
    std::string name;
    int display = -1;             // index into platform::Window::displays(); -1 = default display
    bool fullscreen = false;      // borderless desktop fullscreen on that display
    std::uint32_t width = 1920;   // window size in points when not fullscreen
    std::uint32_t height = 1080;
    bool borderless = true;
    bool alwaysOnTop = false;
    rendering::OutputMapping mapping;
    bool enabled = true;

    [[nodiscard]] Result<void> validate() const; // non-empty name, positive size, valid mapping
    [[nodiscard]] nlohmann::json toJson() const;
    static Result<OutputDesc> fromJson(const nlohmann::json& j); // missing fields keep defaults
};

bool operator==(const OutputDesc& a, const OutputDesc& b);

struct OutputRuntime; // window + surface; defined in output_manager_gpu.cpp

struct Output {
    OutputDesc desc;
    std::shared_ptr<OutputRuntime> runtime; // null while closed
    std::uint64_t framesPresented = 0;
    std::string lastError;

    [[nodiscard]] bool open() const { return runtime != nullptr; }
    // Pixel size of the open window's swapchain (0 while closed). Defined in output_manager_gpu.cpp.
    [[nodiscard]] std::uint32_t pixelWidth() const;
    [[nodiscard]] std::uint32_t pixelHeight() const;
};

class OutputManager {
public:
    OutputManager();
    ~OutputManager();
    OutputManager(const OutputManager&) = delete;
    OutputManager& operator=(const OutputManager&) = delete;

    // ---- descriptors (GPU-free) ----
    // Fails on an invalid descriptor or a duplicate name. The returned pointer stays valid until
    // the output is removed or the set is replaced by fromJson().
    Result<Output*> add(OutputDesc desc);
    // Closes the window if open. Returns false when no output has that name.
    bool remove(const std::string& name);
    [[nodiscard]] Output* find(const std::string& name);
    [[nodiscard]] const Output* find(const std::string& name) const;
    [[nodiscard]] std::vector<std::unique_ptr<Output>>& outputs() { return outputs_; }
    [[nodiscard]] const std::vector<std::unique_ptr<Output>>& outputs() const { return outputs_; }
    [[nodiscard]] std::size_t openCount() const;

    // The project's "outputs" block: an array of OutputDesc. fromJson validates the whole array
    // first (names unique) and then replaces the set, closing any open windows.
    [[nodiscard]] nlohmann::json toJson() const;
    [[nodiscard]] Result<void> fromJson(const nlohmann::json& outputs);

    // ---- windows and presentation (output_manager_gpu.cpp) ----
    // Creates a window and a swapchain for every enabled output that is not open, and the mapper
    // on first use. Outputs that fail keep their error in Output::lastError; the first error is
    // returned after every output was tried.
    [[nodiscard]] Result<void> open(gpu::Context& context, gpu::ShaderLibrary& shaders);
    void closeAll();
    // Consumes the events routed to the output windows: resizes their swapchains and closes
    // windows the user closed (those outputs are disabled so they are not reopened). Call after
    // the primary window's pollEvents each frame; pumps the SDL queue itself when nothing else does.
    void pumpEvents();
    // For each open output: acquire, draw `finalTexture` (width x height, TextureBinding usage)
    // through its mapping in one command buffer, submit, present. Minimised windows are skipped.
    [[nodiscard]] Result<void> presentAll(gpu::Context& context, const wgpu::Texture& finalTexture,
                                          std::uint32_t width, std::uint32_t height);
    [[nodiscard]] rendering::OutputMapper* mapper() { return mapper_.get(); }

private:
    std::vector<std::unique_ptr<Output>> outputs_;
    std::shared_ptr<rendering::OutputMapper> mapper_;
};

} // namespace avgen::app
