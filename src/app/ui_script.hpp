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

#include "ui/gizmo.hpp"

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
namespace avgen::params {
class IParameter;
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
    // The sequencer strip, driven through the pointer: scrub along the ruler, then grab a shot and
    // drag it. It exists for the same reason `Edit` does -- the strip's hit testing, its drag state
    // and its bake-on-release are wiring that no unit test reaches, and the brief's scenario C asks
    // for drag, scrub and resize responsiveness as numbers rather than as an impression.
    Strip = 1u << 8,
    // Moving an object with the gizmo, and dragging a selection box: the two canvas gestures the
    // latency brief names that no arm reached. Both repeat on a cycle rather than running once, so
    // a block of frames measures the gesture rather than the one frame it started on.
    Gizmo = 1u << 9,
    Box = 1u << 10,
    // Starring a hero, through the same EditCommand the star button builds (ADR-193). Phase 1 of
    // the UI-responsiveness investigation measured this headlessly at 282 ms and could not put it
    // beside the other interactions in one process, because there was no arm for it.
    Star = 1u << 11,
    // A *discrete* timeline click: press and release on the ruler, at a new second, with no pointer
    // motion between clicks. `Strip` is a press and a 52-frame drag, so its frames are drag frames;
    // this one's are click frames, which is what "clicking the timeline feels slow" is about. No
    // motion between the clicks is deliberate: it makes `input->ui.build ms` sample only the frames
    // that carried a click.
    Click = 1u << 12,
    // A *repeating* scrub gesture: press on the ruler, drag along it for forty frames, release.
    // `Strip` already does this once -- it is keyed on the absolute frame number, so inside
    // `--ui-ab` it acts only during whichever block happens to cover its window, and its column in
    // every other block is an idle editor. This one counts from its own first step, so it repeats
    // in every block it is given and can be put beside another arm in one process. A drag and a
    // click are different measurements and only this arm measures the drag.
    Drag = 1u << 13,
    // The strip's context menu, opened and left open (ADR-356). Not a measurement: a *capture* arm.
    // The slice tool's menu rows only exist while a popup is up, and neither the author of this
    // feature nor the person who asked for it can see Dear ImGui -- so the only way to know the
    // rows are there, spelled right and not clipped, is to make the program open the menu and then
    // photograph it with `--capture-ui`. It right-clicks a shot and does nothing else, ever, so the
    // popup is still on screen whenever the capture frame arrives.
    SliceMenu = 1u << 14,
    // The slice tool's *gesture* (ADR-356): Cmd held, left button clicked on a shot. It exists
    // because the menu capture proves the row is drawn and proves nothing at all about the click --
    // and a Cmd+click is exactly the wiring no unit test reaches: SDL to the backend's `KeySuper`
    // to the strip's hit test to `performSlice`. It reports the shot count before and after, so the
    // run either says the piece gained a shot or says it did not.
    Slice = 1u << 15,
    // ADR-762: the Director panel's buttons, pressed through the pointer against a proposal that is
    // waiting (run with `--ai-script`): Preview then Reject, or Preview then Accept then Cmd+Z. After
    // each step the arm checks the project -- the preview edit is there and then gone, Accept makes
    // exactly one undo labelled with the request, undo restores the plan list -- and a failed check
    // makes the run exit non-zero. The same frames every run, so `--capture-ui-frame` can photograph
    // the panel after any step.
    DirectorReject = 1u << 16,
    DirectorAccept = 1u << 17,
    // ADR-765: Record, against a waiting GOAL proposal (run with a goal `--ai-script`): press Record,
    // wait for the recording to replace the proposal, check it is baked; then Accept at frame 1500,
    // check one undo labelled with the request and the recorded actor installed; Cmd+Z at 1600 and
    // check it is all gone. Captures after 1450 show the recorded proposal, 1550 the accepted plan.
    DirectorRecord = 1u << 18,
    // Record, then Cancel recording while it runs: the recording stops and the proposal is untouched.
    DirectorCancel = 1u << 19,
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

// Lets go of the left button, wherever an arm had it. Called when `--ui-ab` switches arms: a block
// that ends mid-drag would otherwise hand the next arm a held button, and every gesture that arm
// makes would be part of its predecessor's drag. A measurement taken in that state is not the
// measurement it claims to be.
void releaseScriptedPointer(platform::Window& window);

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
    // Checks an arm made that did not hold (the director arms'). The host exits non-zero on any.
    [[nodiscard]] int failedChecks() const { return failedChecks_; }

private:
    void stepEdit(Engine& engine, ui::ControlPanel& panel, platform::Window& window, std::uint64_t frame);
    void stepStrip(Engine& engine, ui::ControlPanel& panel, platform::Window& window, std::uint64_t frame);
    void stepSliceMenu(Engine& engine, ui::ControlPanel& panel, platform::Window& window,
                       std::uint64_t frame);
    void stepSlice(Engine& engine, ui::ControlPanel& panel, platform::Window& window,
                   std::uint64_t frame);
    void stepGizmo(Engine& engine, ui::ControlPanel& panel, platform::Window& window, std::uint64_t frame);
    void stepBox(Engine& engine, ui::ControlPanel& panel, platform::Window& window, std::uint64_t frame);
    void stepStar(Engine& engine, ui::ControlPanel& panel, std::uint64_t frame);
    void stepClick(Engine& engine, ui::ControlPanel& panel, platform::Window& window, std::uint64_t frame);
    void stepDrag(Engine& engine, ui::ControlPanel& panel, platform::Window& window, std::uint64_t frame);
    void stepDirector(Engine& engine, ui::ControlPanel& panel, platform::Window& window, std::uint64_t frame,
                      bool accept);
    void check(bool ok, const std::string& what);
    int failedChecks_ = 0;
    std::size_t directorUndoBefore_ = 0;
    std::uint64_t directorStateBefore_ = 0;
    std::string directorSequenceBefore_;
    std::uint64_t recordedAt_ = 0; // the frame the recording replaced the proposal, 0 = not yet
    void stepDirectorRecord(Engine& engine, ui::ControlPanel& panel, platform::Window& window, std::uint64_t frame);
    void stepDirectorCancel(Engine& engine, ui::ControlPanel& panel, platform::Window& window, std::uint64_t frame);

    std::vector<std::string> editLog_;
    std::size_t sliceShotsBefore_ = 0;
    std::size_t editNodesBefore_ = 0;
    std::vector<std::string> lastWrites_;
    UiScriptArm arms_ = UiScriptArm::None;
    std::size_t paramCursor_ = 0;
    std::vector<params::IParameter*> sliderTargets_; // the filtered drag set, resolved once
    bool sliderTargetsResolved_ = false;
    std::size_t gizmoDrags_ = 0;   // completed gizmo drags, for the report
    std::size_t boxDrags_ = 0;     // completed box drags
    // Sampled mid-drag, because that is the only moment either is true: `hovered` is None while a
    // handle is being dragged, and a selection box exists only between the press and the release.
    ui::GizmoHandle gizmoDragged_ = ui::GizmoHandle::None;
    bool boxOpened_ = false;
    bool saidNoSubject_ = false;
    std::size_t starToggles_ = 0;      // completed star/unstar pairs, for the arm's own report
    std::string starSubject_;          // the node this arm keeps starring and unstarring
    std::size_t clicks_ = 0;           // completed ruler clicks
    std::size_t clickSteps_ = 0;       // steps since this arm started, not the global frame number
    double clickLastSeconds_ = -1.0;   // where the playhead was left by the last click
    std::size_t dragSteps_ = 0;        // steps since the drag arm started
    std::size_t drags_ = 0;            // completed scrub gestures
    double dragStartSeconds_ = -1.0;   // where the playhead was when the gesture began
    glm::vec3 boxCamera_{0.0f}; // camera pose at the start of the box drag, for the report
    float phase_ = 0.0f;
};

} // namespace avgen::app
