#pragma once

// ADR-520: the weather controls the Edit panel's particle section draws, as data.
//
// It is here rather than in `world_edit_panel.cpp` for the reason `tests/CMakeLists.txt` states
// about that file: the drawing half of the editor is not on the unit-test source list, and a
// table that only the drawing half can see is a table no test can check. The defect §77 names is
// exactly this one -- a panel asks the parameter set for a path by string, and a path that
// resolves to nothing draws an EMPTY BOX rather than failing. ADR-375 found sixteen parameters no
// panel named; ADR-392 found a modulation route aimed at three paths that do not exist. Both were
// silent.
//
// So the panel walks this table and `tests/unit/test_particle_weather.cpp` walks this table, and
// "the panel asks for particles/<node>/pulseRate" and "particles/<node>/pulseRate is registered"
// are one string in one place instead of two strings in two files that agree until one is renamed.

#include <span>

namespace avgen::ui {

struct ParticleWeatherRow {
    const char* leaf;  // appended to "particles/<node>/"
    const char* label; // the caption on the control
    float lo;
    float hi;
    const char* fmt;
    const char* tip;
};

// The rows, in the order an artist reaches for them: what it hits, how big they are, how fast that
// makes them, whether they blink, how they move, and how they catch the light.
[[nodiscard]] std::span<const ParticleWeatherRow> particleWeatherRows();

} // namespace avgen::ui
