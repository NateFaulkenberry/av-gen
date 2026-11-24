// No network port by default (ADR-2133).
//
// Before this, every engine -- the editor, a headless capture, an `--ai-script` run -- opened an OSC listener on
// 0.0.0.0:9000 whose direct scheme sets any parameter by path, with no authentication, and opened MIDI input; the
// evaluation's runs 06, 07 and 09 each logged `osc: listening on 0.0.0.0:9000`. And every save wrote that bind address
// into the project, so a file could carry "listen on every interface" to the next machine that opened it.
//
// The arms:
//   A  an engine opens nothing until it is told to (the headless runs never are)
//   B  told to, OSC listens on loopback, and the socket really is on 127.0.0.1
//   C  every interface is an explicit, separate opt-in, and it is flagged
//   D  a port another program holds is a reported error, not a crash and not a shared port
//   E  a project cannot choose the interface: `control.osc.bind` is not read, is said, and is never written
//   F  the CLI rule: which runs open OSC and MIDI

#include "app/cli_batch.hpp"
#include "app/control_hub.hpp"
#include "app/engine.hpp"
#include "control/control_map.hpp"
#include "control/osc.hpp"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <string>

using namespace avgen;

namespace {

control::ControlMap anyPortMap() {
    control::ControlMap map;
    map.oscPort = 0; // any free port, so a developer's own session on 9000 cannot interfere
    map.midiEnabled = false;
    return map;
}

} // namespace

TEST_CASE("an engine opens no OSC listener and no MIDI input until it is asked to", "[control][osc][adr2133]") {
    for (const app::EngineMode mode : {app::EngineMode::Offline, app::EngineMode::Live}) {
        app::Engine engine(mode);
        control::ControlMap map;
        map.oscPort = 0;
        engine.control().setMap(map); // what a project load does
        const auto status = engine.control().status();
        CHECK_FALSE(status.oscOpen);
        CHECK(status.oscError.empty());
        CHECK_FALSE(status.midiOpen);
        CHECK(status.midiError.empty());
        CHECK_FALSE(engine.control().liveOsc());
        CHECK_FALSE(engine.control().liveMidi());
    }
}

TEST_CASE("asked to, OSC listens on loopback only", "[control][osc][adr2133]") {
    app::Engine engine(app::EngineMode::Live);
    engine.control().setMap(anyPortMap());
    engine.control().setLiveOsc(true);
    const auto status = engine.control().status();
    if (!status.oscOpen) {
        SKIP("cannot open a UDP socket here: " << status.oscError);
    }
    CHECK(status.oscBind == "127.0.0.1");
    CHECK_FALSE(status.oscExposed);
    CHECK(status.midiOpen == false); // OSC alone does not open MIDI

    // The receiver's own default, for any other caller.
    control::OscReceiver receiver;
    REQUIRE(receiver.open(0).has_value());
    CHECK(receiver.boundAddress() == "127.0.0.1");
}

TEST_CASE("every interface is an explicit opt-in, and it is flagged", "[control][osc][adr2133]") {
    app::Engine engine(app::EngineMode::Live);
    engine.control().setMap(anyPortMap());
    REQUIRE(engine.control().setOscBind("0.0.0.0").has_value());
    engine.control().setLiveOsc(true);
    const auto status = engine.control().status();
    if (!status.oscOpen) {
        SKIP("cannot open a UDP socket here: " << status.oscError);
    }
    CHECK(status.oscBind == "0.0.0.0");
    CHECK(status.oscExposed);
    CHECK_FALSE(engine.control().setOscBind("not-an-address").has_value());
    CHECK(engine.control().oscBind() == "0.0.0.0"); // a refused address changes nothing

    // Back to loopback reopens the socket on the new interface (it used to reopen only on a port change).
    REQUIRE(engine.control().setOscBind("127.0.0.1").has_value());
    CHECK(engine.control().status().oscBind == "127.0.0.1");
    CHECK_FALSE(engine.control().status().oscExposed);
}

TEST_CASE("a port another program holds is reported, and nothing crashes or shares it", "[control][osc][adr2133]") {
    control::OscReceiver holder;
    if (auto r = holder.open(0, "127.0.0.1"); !r) {
        SKIP("cannot open a UDP socket here: " << r.error().message);
    }
    const std::uint16_t taken = holder.port();

    app::Engine engine(app::EngineMode::Live);
    control::ControlMap map = anyPortMap();
    map.oscPort = taken;
    engine.control().setMap(map);
    engine.control().setLiveOsc(true);
    const auto status = engine.control().status();
    CHECK_FALSE(status.oscOpen);
    CHECK(status.oscError.find("in use") != std::string::npos);
    CHECK(status.oscError.find(std::to_string(taken)) != std::string::npos);
    CHECK(holder.isOpen());

    // The wildcard does not slip past a loopback holder either: without SO_REUSEADDR the two do not share a port, so
    // the messages never split silently between two programs.
    control::OscReceiver wildcard;
    CHECK_FALSE(wildcard.open(taken, "0.0.0.0").has_value());
}

TEST_CASE("a project cannot choose the interface OSC listens on", "[control][osc][adr2133]") {
    const nlohmann::json block = {{"osc", {{"enabled", true}, {"port", 9000}, {"bind", "0.0.0.0"}}}};
    auto map = control::ControlMap::fromJson(block);
    REQUIRE(map.has_value());
    bool said = false;
    for (const std::string& w : map->warnings) {
        said = said || w.find("control.osc.bind") != std::string::npos;
    }
    CHECK(said);
    const nlohmann::json out = map->toJson();
    CHECK_FALSE(out.contains("osc")); // enabled + 9000 are the defaults: nothing to say
    CHECK(control::ControlMap{}.toJson().empty());

    control::ControlMap changed;
    changed.oscPort = 9100;
    changed.midiFilter = "SE49";
    const nlohmann::json written = changed.toJson();
    CHECK(written["osc"] == nlohmann::json({{"port", 9100}}));
    CHECK(written["midi"] == nlohmann::json({{"filter", "SE49"}}));
    auto back = control::ControlMap::fromJson(written);
    REQUIRE(back.has_value());
    CHECK(back->oscPort == 9100);
    CHECK(back->midiFilter == "SE49");
    CHECK(back->oscEnabled);
    CHECK(back->warnings.empty());
}

TEST_CASE("which runs open OSC and MIDI", "[control][cli][adr2133]") {
    using app::LiveControlAsk;
    using app::liveControlFor;
    // The editor, as the owner launches it: OSC on loopback and MIDI, as before.
    auto editor = liveControlFor(LiveControlAsk{.interactive = true});
    CHECK(editor.osc);
    CHECK(editor.midi);
    // Every headless or scripted run: nothing.
    auto batch = liveControlFor(LiveControlAsk{.interactive = false});
    CHECK_FALSE(batch.osc);
    CHECK_FALSE(batch.midi);
    // Asked by flag: exactly what was asked.
    auto oscOnly = liveControlFor(LiveControlAsk{.interactive = false, .oscFlag = true});
    CHECK(oscOnly.osc);
    CHECK_FALSE(oscOnly.midi);
    auto midiOnly = liveControlFor(LiveControlAsk{.interactive = false, .midiFlag = true});
    CHECK_FALSE(midiOnly.osc);
    CHECK(midiOnly.midi);
    // --no-osc wins over everything, the editor included.
    auto quiet = liveControlFor(LiveControlAsk{.interactive = true, .oscFlag = true, .noOsc = true});
    CHECK_FALSE(quiet.osc);
    CHECK(quiet.midi);
}
