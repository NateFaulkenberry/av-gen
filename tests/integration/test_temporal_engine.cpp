// ADR-410: the temporal family has to be REACHED, not merely registered.
//
// This repo's most expensive recurring defect is a subsystem that is built, tested and
// unreachable -- ADR-039's selective bloom and ADR-035's identifier mask both shipped with every
// parameter resolving, round-tripping and reaching no frame, because one assignment was missing
// between the settings and the scene. A unit test over the parameters cannot see that gap. This
// one drives the real Engine and asks the scene what it ended up with.

#include "app/engine.hpp"
#include "core/time.hpp"
#include "scene/temporal_settings.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace avgen;

TEST_CASE("a temporal parameter set on the engine arrives in the scene", "[integration][temporal]") {
    app::Engine engine(app::EngineMode::Offline);

    const std::string prefix = scene::temporalParameterPrefix(scene::TemporalEffectKind::FrameEcho);

    // 1. The engine registers the paths at all. Without this the panel draws an empty box and
    //    nothing fails.
    auto* enabled = engine.params().findAs<bool>(prefix + "enabled");
    auto* frames = engine.params().findAs<float>(prefix + "frames");
    auto* strength = engine.params().findAs<float>(prefix + "strength");
    REQUIRE(enabled != nullptr);
    REQUIRE(frames != nullptr);
    REQUIRE(strength != nullptr);

    // The control: a path the engine does not register must not resolve, or `find` is saying yes
    // to everything and the three REQUIREs above prove nothing.
    CHECK(engine.params().find(prefix + "nonesuch") == nullptr);

    // 2. Default: off, and the scene says so.
    FixedStepClock clock(60.0);
    clock.restartAt(0.0);
    engine.update(clock.tick());
    CHECK_FALSE(engine.scene().temporal.echo.enabled);
    CHECK(engine.scene().temporal.historyFrames() == 0u);

    // 3. Set through the parameter system, the way a panel slider does -- base value, then an
    //    ordinary frame -- and the SCENE the renderer is handed must carry it.
    enabled->setBase(true);
    frames->setBase(9.0f);
    strength->setBase(0.6f);
    engine.update(clock.tick());

    CHECK(engine.scene().temporal.echo.enabled);
    CHECK(engine.scene().temporal.echo.frames == 9);
    CHECK_THAT(engine.scene().temporal.echo.strength, Catch::Matchers::WithinAbs(0.6, 1e-4));
    // The declared bound follows, which is what sizes the ring and the settling badge.
    CHECK(engine.scene().temporal.historyFrames() == 9u);

    // 4. And it turns back off. A one-way switch is the same defect wearing the other face.
    enabled->setBase(false);
    engine.update(clock.tick());
    CHECK_FALSE(engine.scene().temporal.echo.enabled);
    CHECK(engine.scene().temporal.historyFrames() == 0u);
}
