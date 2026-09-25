// An engine that only evaluates a project -- a render, a trace, the Director's scratch copy -- opens
// no live control source (Engine::setLiveControl). Two engines in one process used to fight over the
// OSC port: the second logged "cannot bind ... address already in use" on every load.

#include "app/control_hub.hpp"
#include "app/engine.hpp"
#include "control/control_map.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace avgen;

TEST_CASE("an engine with live control off binds no OSC port, and leaves the editor's alone", "[control][osc][engine]") {
    control::ControlMap map;
    map.oscEnabled = true;
    map.oscPort = 47931; // not the default, so a developer's own session on 9000 cannot interfere
    map.oscBind = "127.0.0.1";
    map.midiEnabled = false;

    app::Engine editor(app::EngineMode::Offline);
    editor.control().setMap(map);
    REQUIRE(editor.control().status().oscOpen);

    app::Engine scratch(app::EngineMode::Offline);
    scratch.setLiveControl(false);
    scratch.control().setMap(map);
    CHECK_FALSE(scratch.control().status().oscOpen);
    CHECK(scratch.control().status().oscError.empty()); // not a failure: it never tried
    CHECK(scratch.control().map().oscPort == 47931);     // the map is kept; only the socket is shut
    CHECK(editor.control().status().oscOpen);

    // The switch is what kept it shut: turned back on, it tries -- and finds the editor's port taken.
    scratch.setLiveControl(true);
    CHECK_FALSE(scratch.control().status().oscOpen);
    CHECK_FALSE(scratch.control().status().oscError.empty());
    CHECK(editor.control().status().oscOpen);
}
