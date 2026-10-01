#pragma once

// ADR-1026: the Live panel's "Start projection". One button that gets the live picture onto a projector: it opens the
// Sonic Live demo when the open project is not a live one, turns live input on, and opens a clean output window --
// an `OutputManager` output like any other, flagged as this machine's projection.
//
// This file is the decisions, with no window, device or ImGui in it, so they can be checked in the CPU suite:
//   * which display (the remembered one by name, else the first non-primary display, else the primary);
//   * fullscreen (remembered, else on exactly when the display is not the primary);
//   * the window's size and how the frame meets a screen of another shape;
//   * the state machine: Idle -> (AwaitingProject) -> Running -> Idle, and every way out of Running.
// The host (Application) does what the actions say and reports what it observes each frame.

#include "app/output_manager.hpp"
#include "app/settings.hpp"
#include "rendering/output_mapping.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace avgen::app {

// The output's name. A project output with this name is refused (OutputManager::fromJson).
inline constexpr const char* kProjectionOutputName = "Live projection";

// The facts the projection needs about one connected display (platform::DisplayInfo, minus SDL).
struct ProjectionDisplay {
    int index = 0;           // into platform::Window::displays()
    std::string name;
    int width = 0;           // points
    int height = 0;
    bool primary = false;
};

struct ProjectionDisplayChoice {
    int index = -1;          // -1: no display list (the platform's default display)
    std::string name;
    bool primary = true;
    int width = 0;
    int height = 0;
    // The remembered display is not connected, and this is the automatic choice instead.
    bool fellBack = false;
};

// The remembered display when it is connected (by name: indices shift when a display comes or goes), else the first
// non-primary display, else the primary, else (no list at all) index -1.
[[nodiscard]] ProjectionDisplayChoice chooseProjectionDisplay(const std::vector<ProjectionDisplay>& displays,
                                                              const std::string& remembered);

// Fullscreen as remembered, else on for a display that is not the primary (the projector) and off for the primary
// (a fullscreen window there would cover the editor).
[[nodiscard]] bool projectionFullscreen(const AppSettings::Projection& settings, const ProjectionDisplayChoice& display);

// The output window for these settings on these displays: borderless only when fullscreen (a windowed projection
// keeps its title bar so it can be moved and closed); a windowed size of 0 matches the display.
[[nodiscard]] OutputDesc makeProjectionOutput(const AppSettings::Projection& settings,
                                              const std::vector<ProjectionDisplay>& displays);

// How a `sourceW x sourceH` frame lands on a `targetW x targetH` window. Fit puts the whole frame in, centred, with
// black bars; Fill crops the frame's centre to the window's shape; Stretch is the identity. Shapes that match to
// within a pixel give the identity (the mapper's plain-copy path).
[[nodiscard]] rendering::OutputMapping projectionMapping(ProjectionScaling scaling, std::uint32_t sourceW,
                                                         std::uint32_t sourceH, std::uint32_t targetW,
                                                         std::uint32_t targetH);

class Projection {
public:
    enum class State : std::uint8_t { Idle, AwaitingProject, Running };
    enum class Action : std::uint8_t {
        None,
        OpenLiveDemo, // open the Sonic Live demo (through the unsaved-changes prompt); the window follows its load
        OpenWindow,   // turn live input on, add the projection output and open it, then call opened()
        CloseWindow,  // remove the projection output
    };

    // What the host sees this frame.
    struct Observed {
        bool loading = false;      // a project open is pending, loading or waiting on the unsaved-changes prompt
        bool liveProject = false;  // the open project is a live sonic project (`sonic.live`)
        bool windowOpen = false;   // the projection output exists and its window is open
        // Connected displays; empty means "unknown" (no video subsystem), which never stops a projection.
        std::vector<ProjectionDisplay> displays;
    };

    [[nodiscard]] State state() const { return state_; }
    // The button says Stop while this is true.
    [[nodiscard]] bool active() const { return state_ != State::Idle; }
    // Why the last projection ended or failed; empty while running or after a plain Stop.
    [[nodiscard]] const std::string& message() const { return message_; }
    // The display the running projection opened on.
    [[nodiscard]] const std::string& displayName() const { return displayName_; }

    // The Start button. Not live: the demo opens first. Live: the window opens now. Ignored while active.
    [[nodiscard]] Action start(bool liveProject);
    // The Stop button (and quitting). Closes the window when one is open or about to be.
    [[nodiscard]] Action stop();
    // Every frame.
    [[nodiscard]] Action update(const Observed& observed);
    // After OpenWindow: whether the window opened, on which display, and the error when it did not.
    void opened(bool ok, const std::string& displayName, const std::string& error);
    // After OpenLiveDemo when the demo cannot even be asked for (not found): back to Idle with the reason.
    void failed(const std::string& why);

private:
    State state_ = State::Idle;
    bool windowRequested_ = false;
    std::string message_;
    std::string displayName_;
};

} // namespace avgen::app
