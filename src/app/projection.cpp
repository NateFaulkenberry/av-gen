// ADR-1026: the projection's decisions (see projection.hpp). GPU-free, window-free, ImGui-free.
#include "app/projection.hpp"

#include <algorithm>
#include <cmath>

namespace avgen::app {

ProjectionDisplayChoice chooseProjectionDisplay(const std::vector<ProjectionDisplay>& displays,
                                                const std::string& remembered) {
    ProjectionDisplayChoice out;
    if (displays.empty()) {
        return out; // index -1: the platform's default display
    }
    const auto take = [&out](const ProjectionDisplay& d) {
        out.index = d.index;
        out.name = d.name;
        out.primary = d.primary;
        out.width = d.width;
        out.height = d.height;
    };
    if (!remembered.empty()) {
        const auto it = std::find_if(displays.begin(), displays.end(),
                                     [&](const ProjectionDisplay& d) { return d.name == remembered; });
        if (it != displays.end()) {
            take(*it);
            return out;
        }
        out.fellBack = true;
    }
    // A second display is the projector; with only the primary, the projection is a window beside the editor.
    const auto secondary =
        std::find_if(displays.begin(), displays.end(), [](const ProjectionDisplay& d) { return !d.primary; });
    if (secondary != displays.end()) {
        take(*secondary);
        return out;
    }
    const auto primary =
        std::find_if(displays.begin(), displays.end(), [](const ProjectionDisplay& d) { return d.primary; });
    take(primary != displays.end() ? *primary : displays.front());
    return out;
}

bool projectionFullscreen(const AppSettings::Projection& settings, const ProjectionDisplayChoice& display) {
    return settings.fullscreen.value_or(!display.primary);
}

OutputDesc makeProjectionOutput(const AppSettings::Projection& settings,
                                const std::vector<ProjectionDisplay>& displays) {
    const ProjectionDisplayChoice choice = chooseProjectionDisplay(displays, settings.display);
    OutputDesc desc;
    desc.name = kProjectionOutputName;
    desc.display = choice.index;
    desc.fullscreen = projectionFullscreen(settings, choice);
    if (settings.windowWidth > 0 && settings.windowHeight > 0) {
        desc.width = settings.windowWidth;
        desc.height = settings.windowHeight;
    } else if (choice.width > 0 && choice.height > 0) {
        // Automatic: the display's size on another display; on this screen, half of it, so the editor stays usable
        // beside a windowed projection.
        const int divisor = choice.primary && !desc.fullscreen ? 2 : 1;
        desc.width = static_cast<std::uint32_t>(std::max(16, choice.width / divisor));
        desc.height = static_cast<std::uint32_t>(std::max(16, choice.height / divisor));
    } // else OutputDesc's 1920x1080
    // A fullscreen projection has nothing to show but the picture. A windowed one keeps its title bar: without it
    // there is no way to move it to another screen or close it with the mouse.
    desc.borderless = desc.fullscreen;
    desc.alwaysOnTop = false;
    desc.enabled = true;
    desc.mapping = rendering::OutputMapping::identity();
    return desc;
}

rendering::OutputMapping projectionMapping(ProjectionScaling scaling, std::uint32_t sourceW, std::uint32_t sourceH,
                                           std::uint32_t targetW, std::uint32_t targetH) {
    rendering::OutputMapping m = rendering::OutputMapping::identity();
    if (scaling == ProjectionScaling::Stretch || sourceW == 0 || sourceH == 0 || targetW == 0 || targetH == 0) {
        return m;
    }
    const double source = static_cast<double>(sourceW) / static_cast<double>(sourceH);
    const double target = static_cast<double>(targetW) / static_cast<double>(targetH);
    if (scaling == ProjectionScaling::Fit) {
        if (source > target) {
            // Wider than the screen: full width, bars above and below.
            const double h = target / source;
            if ((1.0 - h) * static_cast<double>(targetH) < 1.0) {
                return m;
            }
            const auto top = static_cast<float>((1.0 - h) * 0.5);
            const auto bottom = static_cast<float>((1.0 + h) * 0.5);
            m.corners = {{{0.0f, top}, {1.0f, top}, {1.0f, bottom}, {0.0f, bottom}}};
        } else {
            const double w = source / target;
            if ((1.0 - w) * static_cast<double>(targetW) < 1.0) {
                return m;
            }
            const auto left = static_cast<float>((1.0 - w) * 0.5);
            const auto right = static_cast<float>((1.0 + w) * 0.5);
            m.corners = {{{left, 0.0f}, {right, 0.0f}, {right, 1.0f}, {left, 1.0f}}};
        }
        return m;
    }
    // Fill: the centre of the frame, cut to the screen's shape.
    if (source > target) {
        const double w = target / source;
        if ((1.0 - w) * static_cast<double>(sourceW) < 1.0) {
            return m;
        }
        m.crop = {static_cast<float>((1.0 - w) * 0.5), 0.0f, static_cast<float>(w), 1.0f};
    } else {
        const double h = source / target;
        if ((1.0 - h) * static_cast<double>(sourceH) < 1.0) {
            return m;
        }
        m.crop = {0.0f, static_cast<float>((1.0 - h) * 0.5), 1.0f, static_cast<float>(h)};
    }
    return m;
}

Projection::Action Projection::start(bool liveProject) {
    if (state_ != State::Idle) {
        return Action::None;
    }
    message_.clear();
    displayName_.clear();
    if (liveProject) {
        state_ = State::Running;
        windowRequested_ = true;
        return Action::OpenWindow;
    }
    state_ = State::AwaitingProject;
    return Action::OpenLiveDemo;
}

Projection::Action Projection::stop() {
    const State was = state_;
    state_ = State::Idle;
    windowRequested_ = false;
    message_.clear();
    // A stop while the demo is still loading just means no window follows it; the load itself carries on.
    return was == State::Running ? Action::CloseWindow : Action::None;
}

Projection::Action Projection::update(const Observed& observed) {
    switch (state_) {
    case State::Idle:
        return Action::None;
    case State::AwaitingProject:
        if (observed.loading) {
            return Action::None;
        }
        if (observed.liveProject) {
            state_ = State::Running;
            windowRequested_ = true;
            return Action::OpenWindow;
        }
        // The prompt was cancelled, or the demo failed to load: nothing is projected, and the person is told.
        state_ = State::Idle;
        message_ = "Projection not started: the Sonic Live demo did not open.";
        return Action::None;
    case State::Running:
        if (windowRequested_) {
            return Action::None; // the host has not reported the open yet
        }
        if (!observed.windowOpen) {
            state_ = State::Idle;
            message_ = "The projection window was closed.";
            return Action::CloseWindow;
        }
        if (!observed.displays.empty() && !displayName_.empty() &&
            std::none_of(observed.displays.begin(), observed.displays.end(),
                         [this](const ProjectionDisplay& d) { return d.name == displayName_; })) {
            // macOS would move the window to the remaining screen -- fullscreen over the editor. Stop instead.
            state_ = State::Idle;
            message_ = "Projection stopped: " + displayName_ + " was disconnected.";
            return Action::CloseWindow;
        }
        return Action::None;
    }
    return Action::None;
}

void Projection::opened(bool ok, const std::string& displayName, const std::string& error) {
    windowRequested_ = false;
    if (state_ != State::Running) {
        return;
    }
    if (ok) {
        displayName_ = displayName;
        return;
    }
    state_ = State::Idle;
    message_ = "Projection failed: " + (error.empty() ? std::string("the window did not open") : error);
}

void Projection::failed(const std::string& why) {
    state_ = State::Idle;
    windowRequested_ = false;
    message_ = why;
}

} // namespace avgen::app
