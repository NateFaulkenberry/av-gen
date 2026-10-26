#pragma once

// The MIDI panel (ADR-1704): MIDI Learn, the assignments, a live monitor and the devices, in one
// dockable registry panel. It draws what the hub and the control map already hold -- the mapper's
// per-mapping status, the monitor ring, the device table -- and edits the map through the helpers
// in control/midi_learn.hpp, which keep the scene's shipped defaults unwritten (ADR-1700). Every
// edit ends in ControlHub::mappingsChanged(), and is saved with the project through the ordinary
// Save and unsaved-changes prompt.

#include "control/control_map.hpp"
#include "control/midi_learn.hpp"

#include <functional>
#include <string>
#include <vector>

namespace avgen::app {
class Engine;
}

namespace avgen::ui {

class MidiPanel {
public:
    enum class Tab : int { Assignments = 0, Monitor, Devices };

    void draw(app::Engine& engine);
    // The Parameters panel's right-click: this parameter is the destination, and Learn is armed.
    void learnFor(app::Engine& engine, const std::string& parameterPath);
    // Raises a tab on the next draw (the View menu, a capture).
    void showTab(Tab tab) { requestedTab_ = static_cast<int>(tab); }
    // Selects a mapping by id (the scripted capture arm; a click does the same).
    void select(const std::string& id) { selected_ = id; }

    // Installed by the host: the devices switched off on this machine changed (saved in the app settings).
    std::function<void(const std::vector<std::string>&)> onDisabledDevicesChanged;

private:
    void drawLearnBar(app::Engine& engine);
    void drawAssignments(app::Engine& engine);
    void drawEditor(app::Engine& engine);
    void drawMonitor(app::Engine& engine);
    void drawDevices(app::Engine& engine);
    // A searchable destination tree (control channels, then the parameter registry by group). True on a change.
    bool destinationPicker(const char* id, app::Engine& engine, control::BindingTarget& target, float width);

    // Learn.
    control::BindingTarget learnTarget_;
    control::MidiScope learnScope_ = control::MidiScope::Scene;
    bool learnAnyDevice_ = false;
    bool learnAnyChannel_ = false;
    bool learnAsTrigger_ = false;
    std::string learnMessage_;

    // Assignments.
    std::string selected_; // a mapping id, never a row index
    bool editorBeside_ = false;
    char search_[96] = "";
    int scopeFilter_ = 0;  // 0 all, 1 default, 2 rig, 3 project, 4 scene
    bool showInactive_ = true;

    // Monitor.
    bool paused_ = false;
    std::vector<control::MidiMonitor::Row> frozen_;
    int monitorDevice_ = -1;
    int monitorChannel_ = -1;
    int monitorKind_ = 0; // 0 all, 1 notes, 2 CC, 3 bend and pressure, 4 NRPN/RPN, 5 program and system
    int monitorSelected_ = -1;

    // Picker.
    char pickerSearch_[96] = "";
    char newChannel_[48] = "";
    bool pickerForRig_ = false; // ADR-1705: the picker is choosing for a rig mapping (common parameters first)

    int requestedTab_ = -1;
};

} // namespace avgen::ui
