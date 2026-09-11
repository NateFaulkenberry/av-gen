#pragma once

// Scripted interaction: what a person does to the editor, done by the program, at a rate and in an
// order a run repeats exactly.
//
// The hard part of a UI performance pass is that the thing complained about -- "it feels sluggish"
// -- only happens while somebody is touching it, and a headless run touches nothing. Measuring the
// idle editor answers a different question from the one being asked. So the interaction is
// scripted: each arm is one kind of interaction and nothing else, the arms compose, and two runs of
// the same arm do the same thing.
//
// Where the interaction enters matters, and it is different per arm:
//
//  * Pointer arms push real SDL events onto the process queue. They therefore travel the whole
//    path -- SDL queue, the application's event sink, the ImGui backend, the viewport routing --
//    and cost what the real path costs. Nothing is faked past the device.
//  * Value arms write the parameter directly, which is precisely what the widget's own code does
//    when a drag changes it (`param->setBaseComponent`). Driving the widget through the pointer
//    would additionally measure ImGui's hit-testing for the widget, which the Hover arm already
//    measures on its own, and would depend on where the panel happened to be docked.
//
// Both are honest about what they are; neither claims to be the other.

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::platform {
class Window;
}
namespace avgen::ui {
class ControlPanel;
}

namespace avgen::app {

class Engine;

enum class UiScriptArm : std::uint32_t {
    None = 0,
    Hover = 1u << 0,    // sweep the pointer over the window: hit-testing, hover, tooltips
    Sliders = 1u << 1,  // drag parameters continuously, as a slider drag does
    Panels = 1u << 2,   // open and close every panel on a cycle
    Select = 1u << 3,   // move the World panel's selection from node to node
    Scrub = 1u << 4,    // move the transport every frame
    Camera = 1u << 5,   // orbit the viewport by dragging on the canvas
    Tabs = 1u << 6,     // switch the authoring layer and the World panel's tab
    // ADR-092. The world editor, driven through the pointer: arm a brush, paint a stroke across the
    // canvas, undo it, select what is under the cursor, group it, take that back too. It exists
    // because the editor's *wiring* -- SDL to ImGui to the canvas's hover state to the ghost to the
    // placement -- is the part that unit tests cannot reach and that this repository cannot
    // screenshot, so the only honest way to know it is connected is to make the program do it and
    // then ask the scene what happened.
    Edit = 1u << 7,
};

[[nodiscard]] constexpr UiScriptArm operator|(UiScriptArm a, UiScriptArm b) {
    return static_cast<UiScriptArm>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}
[[nodiscard]] constexpr bool has(UiScriptArm set, UiScriptArm arm) {
    return (static_cast<std::uint32_t>(set) & static_cast<std::uint32_t>(arm)) != 0;
}

// "hover,sliders" -> the pair. "all" -> every arm. "idle"/"" -> None. Returns nullopt on an
// unknown arm name, so a typo in a benchmark invocation fails loudly rather than silently
// measuring the idle editor and calling it an interaction.
[[nodiscard]] std::optional<UiScriptArm> parseUiScript(std::string_view spec);
[[nodiscard]] std::string uiScriptNames();

class UiScript {
public:
    UiScript() = default;
    explicit UiScript(UiScriptArm arms) : arms_(arms) {}

    [[nodiscard]] bool active() const { return arms_ != UiScriptArm::None; }
    [[nodiscard]] UiScriptArm arms() const { return arms_; }

    // Called once per frame, at the top of the loop, before events are polled: the events this
    // pushes are read by the same frame's poll, so an arm's cost lands in the frame it belongs to.
    void step(Engine& engine, ui::ControlPanel* panel, platform::Window& window, std::uint64_t frame);

    // What the Edit arm did, in the order it did it, for the host to log on the way out. The whole
    // value of a scripted edit is the sentence it can print afterwards: "painted 23, undid to 0,
    // selected fern_4, grouped 2". Empty unless the arm ran.
    [[nodiscard]] const std::vector<std::string>& editLog() const { return editLog_; }

    // What the Sliders arm wrote this frame, as "path=value". The host logs it when a frame turns
    // out to have been slow, which is how "changing a property costs 14 ms" becomes the name of the
    // property. Empty on a frame that wrote nothing.
    [[nodiscard]] const std::vector<std::string>& lastWrites() const { return lastWrites_; }

private:
    void stepEdit(Engine& engine, ui::ControlPanel& panel, platform::Window& window, std::uint64_t frame);

    std::vector<std::string> editLog_;
    std::size_t editNodesBefore_ = 0;
    std::vector<std::string> lastWrites_;
    UiScriptArm arms_ = UiScriptArm::None;
    std::size_t paramCursor_ = 0;
    glm::vec3 boxCamera_{0.0f}; // camera pose at the start of the box drag, for the report
    float phase_ = 0.0f;
};

} // namespace avgen::app
