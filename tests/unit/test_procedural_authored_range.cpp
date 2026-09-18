// A scene file could not author an emissive above 50, and the clamp said nothing (ADR-321).
//
// `material/emissive` was registered with a hard maximum of 50, so a scene asking for 256 ran at 50
// and the file gave no sign. Found by the HDR Lab, whose own fixture asked for 256 and got 50, and
// recorded in `docs/engineering-labs.md` §8 item 6 as "ADR-225 in an authoring format -- the same
// shape as the Lighting Lab's `coneDegrees`, in a different parser". It matters because `emissive`
// is the only route above unit radiance a scene file has: `baseColor` and `emissiveColor` are
// [0, 1] by the colour they are. The HDR Lab measured a neutral highlight beginning to bloom at
// scene-linear 0.55 and a pure blue not until 7.6, so values well above 1 are the working range.
//
// Two decisions, and the arms are one each:
//
//   the cap    a hard range is an affordance -- what keeps a slider and a modulation route inside
//              something somebody meant -- and 50 was a round number that was picked, not a bound
//              the quantity has the way `roughness`'s 1 is. So it is raised to hold what the file
//              says, and not removed: with no ceiling a route could drive this to infinity and a
//              typo would be indistinguishable from an intention.
//   the speech a clamp that still happens must name itself. That is ADR-278's rule for keys applied
//              to values, and `ProceduralParameters::clamped` is its `unknownKeys`: data, so a test
//              can assert what would be warned about without capturing a log.

#include "params/parameter_set.hpp"
#include "scene/procedural.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <string>

using namespace avgen;

namespace {

// A registered object and the report its registration produced. The geometry is never generated:
// this is a claim about the parameter table, and generating a cloud to make it would be slower and
// would test something else.
struct Registered {
    params::ParameterSet params;
    scene::ProceduralGeometry rest;
    scene::ProceduralParameters p;

    explicit Registered(const scene::Material& material) {
        rest.material = material;
        p = scene::registerProceduralParameters(params, rest, "proc/thing/");
    }
    [[nodiscard]] bool reported(const std::string& path) const {
        return std::any_of(p.clamped.begin(), p.clamped.end(),
                           [&](const scene::ClampedAuthoredValue& c) { return c.path == path; });
    }
};

scene::Material lit(float emissive) {
    scene::Material m;
    m.emissiveIntensity = emissive;
    return m;
}

} // namespace

TEST_CASE("a scene file can author an emissive above the slider's ceiling", "[procedural][range]") {
    // The report, measured: the HDR Lab's fixture asked for 256 and the engine ran 50.
    Registered high(lit(256.0f));
    REQUIRE(high.p.emissive != nullptr);
    CHECK_THAT(high.p.emissive->value(), Catch::Matchers::WithinAbs(256.0, 1e-3));
    CHECK_THAT(high.p.emissive->hardMax(0), Catch::Matchers::WithinAbs(256.0, 1e-3));
    // And it is not a clamp any more, so it does not report one. A fix that raised the ceiling and
    // still announced a clamp would be telling the author their value was overruled when it was not.
    CHECK_FALSE(high.reported("material/emissive"));
    // The slider is unchanged: the soft range is what the panel draws, and 0..8 is where a person
    // works. Widening the *hard* range is the whole of the change.
    CHECK_THAT(high.p.emissive->softMax(0), Catch::Matchers::WithinAbs(8.0, 1e-3));

    // The control, and it is what keeps this from being "the cap was deleted": an ordinary object
    // keeps exactly the range it had, so nothing shipped moves.
    Registered ordinary(lit(2.5f));
    CHECK_THAT(ordinary.p.emissive->value(), Catch::Matchers::WithinAbs(2.5, 1e-3));
    CHECK_THAT(ordinary.p.emissive->hardMax(0), Catch::Matchers::WithinAbs(50.0, 1e-3));
    CHECK(ordinary.p.clamped.empty());
}

TEST_CASE("a clamp the table still makes says so, by name", "[procedural][range]") {
    // `roughness` is a bound the quantity has rather than an affordance -- there is no meaning
    // above 1 -- so the value is still moved. What changes is that the file is told.
    scene::Material m;
    m.roughness = 5.0f;
    m.baseColor = glm::vec3(0.5f, 2.0f, -1.0f);
    Registered r(m);

    REQUIRE(r.reported("material/roughness"));
    const auto& all = r.p.clamped;
    const auto rough = std::find_if(all.begin(), all.end(), [](const scene::ClampedAuthoredValue& c) {
        return c.path == "material/roughness";
    });
    REQUIRE(rough != all.end());
    CHECK_THAT(rough->authored, Catch::Matchers::WithinAbs(5.0, 1e-3));
    CHECK_THAT(rough->running, Catch::Matchers::WithinAbs(1.0, 1e-3));
    CHECK_THAT(rough->high, Catch::Matchers::WithinAbs(1.0, 1e-3));
    // And the parameter really does run at what the report says it runs at, which is the half of
    // this that a message on its own could get wrong.
    CHECK_THAT(r.p.roughness->value(), Catch::Matchers::WithinAbs(1.0, 1e-3));

    // Per component and named, because a colour clamps channel by channel: "baseColor was moved"
    // would not say which channel the file lost.
    CHECK_FALSE(r.reported("material/baseColor.x")); // 0.5 is in range -- the control inside the vector
    CHECK(r.reported("material/baseColor.y"));       // 2.0 -> 1
    CHECK(r.reported("material/baseColor.z"));       // -1.0 -> 0
}

TEST_CASE("an object whose every authored value is in range reports nothing", "[procedural][range]") {
    // The control that makes the warning a finding rather than noise. A default `ProceduralGeometry`
    // is what every scene in the repository starts from, and none of its eighty-odd values is
    // outside the range its own row declares -- so a report from a real file means something.
    params::ParameterSet params;
    const scene::ProceduralGeometry rest;
    const scene::ProceduralParameters p = scene::registerProceduralParameters(params, rest, "proc/plain/");
    for (const scene::ClampedAuthoredValue& c : p.clamped) {
        UNSCOPED_INFO("reported: " << c.path << " authored " << c.authored);
    }
    CHECK(p.clamped.empty());
}
