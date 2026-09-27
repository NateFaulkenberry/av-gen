// ADR-934: a retired body leaves the world.
//
// A set piece's `retire` step (`stage::StepKind::Retire`) hid the animal it had taken and handed it
// back to its own behaviours. The crowd and the sense stage take every body, seen or not, so GV3's
// abducted animals grazed on invisibly in the meadows the aliens walk (bull-10 136.7 m after it
// vanished), and the aliens chose to go to them seven times -- tide to cow-12, ember to horse-11
// across the river (gv3-cast's iteration 2, traced with `--decisions`). Now a retired body is not
// simulated, not in the crowd, not perceived, not a place to be sent, and is held where it was taken.
//
// The fixture is a director and two bodies, the director run where the composition runs one -- at
// the top of every frame, before any body steps (ADR-209) -- and handed to `EntityWorld::seek` as
// its replay hook, so a scrub retires the cow on the step a play does:
//
//   left      after the retire the cow stops where it was, is out of the crowd, and is not
//             perceived; the alien that was walking over to greet it gives up and never goes back
//             |  the same film with no retire: the cow wanders on, the crowd holds it, and the alien
//             reaches it
//   scrubbed  a seek past the retire lands where the play did: the cow retired where it was taken,
//             the alien where it stood, nobody's senses holding the cow
//   metrics   the quality recorder's track of the cow ends where it was taken

#include "entity/character_quality.hpp"
#include "entity/entity.hpp"
#include "support/cast_world.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

#include <cmath>
#include <string>
#include <vector>

using namespace avgen;
using Catch::Approx;
using testsupport::behavior;
using testsupport::CastMember;
using testsupport::CastWorld;

namespace {

constexpr double kRetireAt = 2.0;

// A grazing cow with a body, and an alien across the field that notices cows and goes over to them.
std::vector<CastMember> field() {
    CastMember cow;
    cow.name = "cow";
    cow.seed = 1107;
    cow.tags = {"cow"};
    cow.gait.walkSpeed = 1.0f;
    cow.behaviors.push_back(behavior("ground", {{"bodyRadius", 1.2}}));
    cow.behaviors.push_back(behavior("wander", {{"speed", 1.0}, {"minRange", 2.0}, {"maxRange", 4.0},
                                                {"pauseMin", 0.3}, {"pauseMax", 0.8}, {"homeRadius", 5.0}}));
    CastMember alien;
    alien.name = "alien";
    alien.seed = 2207;
    alien.at = glm::vec3(-18.0f, 0.0f, 0.0f);
    alien.tags = {"alien"};
    alien.perceives = true;
    alien.perception.range = 60.0f;
    alien.perception.fieldOfView = 360.0f;
    alien.gait.walkSpeed = 2.0f;
    alien.behaviors.push_back(behavior(
        "decide", {{"hertz", 2.0},
                   {"memorySeconds", 9.0},
                   {"mind", nlohmann::json::object()},
                   {"considerers",
                    {{{"kind", "social"}, {"name", "company"}, {"tags", {"cow"}}, {"weight", 1.0}, {"distance", 3.0},
                      {"personalSpace", 2.5}, {"dwell", 2.0}},
                     {{"kind", "idle"}, {"name", "idle"}, {"weight", 0.05}}}}}));
    return {cow, alien};
}

std::size_t indexOf(const entity::EntityWorld& world, std::string_view name) {
    for (std::size_t i = 0; i < world.entities().size(); ++i) {
        if (world.entities()[i]->name() == name) {
            return i;
        }
    }
    return world.entities().size();
}

bool perceives(const entity::Entity& who, std::size_t other) {
    for (const entity::Percept& p : who.percepts()) {
        if (p.kind == entity::InterestKind::Character && p.source == other) {
            return true;
        }
    }
    return false;
}

glm::vec2 flatOf(const entity::Entity& e) {
    const glm::vec3 p = e.state().position();
    return glm::vec2(p.x, p.z);
}

// What happened after the retire (or after where it would have been), frame by frame.
struct Watch {
    glm::vec2 cowAtRetire{0.0f};
    float cowMovedAfter = 0.0f;       // farthest the cow was from where it stood at the retire
    int framesPerceived = 0;          // frames the alien's senses held the cow
    int framesInCrowd = 0;            // frames the crowd held the cow's body
    float alienClosest = 1e9f;        // nearest the alien came to the cow
    std::size_t crowdAfter = 0;
};

Watch run(CastWorld& w, bool retire, double seconds) {
    entity::Entity& cow = w.body("cow");
    const std::size_t cowIndex = indexOf(w.world, "cow");
    if (retire) {
        entity::EntityWorld& world = w.world;
        w.director = [&world](double now) {
            if (now >= kRetireAt) {
                world.retire("cow"); // what `StepKind::Retire` does to the body, on the step it runs
            }
        };
    }
    Watch out;
    bool marked = false;
    w.play(seconds, [&] {
        if (w.time() - CastWorld::kStep < kRetireAt) {
            return;
        }
        if (!marked) {
            out.cowAtRetire = flatOf(cow);
            marked = true;
        }
        out.cowMovedAfter = std::max(out.cowMovedAfter, glm::length(flatOf(cow) - out.cowAtRetire));
        out.framesPerceived += perceives(w.body("alien"), cowIndex) ? 1 : 0;
        // A body standing exactly where the cow is: pushed out of it only while the cow is a body.
        const glm::vec2 push = w.world.crowdSeparation(indexOf(w.world, "alien"), flatOf(cow), 0.5f);
        out.framesInCrowd += glm::length(push) > 1e-4f ? 1 : 0;
        out.alienClosest = std::min(out.alienClosest, glm::length(flatOf(w.body("alien")) - flatOf(cow)));
        out.crowdAfter = w.world.crowd().obstacles().size();
    });
    return out;
}

} // namespace

TEST_CASE("a retired body neither blocks nor draws another body, and stays where it was taken",
          "[entity][staging][adr934]") {
    CastWorld left(field());
    const Watch gone = run(left, true, 14.0);
    INFO("retired: cow moved " << gone.cowMovedAfter << " m after, perceived " << gone.framesPerceived
                               << " frames, in the crowd " << gone.framesInCrowd << " frames, alien closest "
                               << gone.alienClosest << " m");
    REQUIRE(left.body("cow").retired());
    // Not simulated: held where it was taken, to the millimetre, for twelve seconds.
    CHECK(gone.cowMovedAfter < 1e-3f);
    // Not a body anyone walks round, and not a body anyone notices.
    CHECK(gone.framesInCrowd == 0);
    CHECK(gone.crowdAfter == 0);
    CHECK(gone.framesPerceived == 0);
    // And not somewhere anyone is sent: the alien, which set out to greet it, never gets there.
    CHECK(gone.alienClosest > 8.0f);
    glm::vec3 nowhere(0.0f);
    CHECK_FALSE(left.world.pointOfInterest("cow", nowhere));

    // The control: the same film, no retire. The cow wanders on, the crowd holds it, the alien
    // notices it and walks over to it.
    CastWorld stayed(field());
    const Watch here = run(stayed, false, 14.0);
    INFO("control: cow moved " << here.cowMovedAfter << " m after, perceived " << here.framesPerceived
                               << " frames, in the crowd " << here.framesInCrowd << " frames, alien closest "
                               << here.alienClosest << " m");
    CHECK_FALSE(stayed.body("cow").retired());
    CHECK(here.cowMovedAfter > 1.0f);
    CHECK(here.framesInCrowd > 300);
    CHECK(here.framesPerceived > 300);
    CHECK(here.alienClosest < 6.0f);
    CHECK(stayed.world.pointOfInterest("cow", nowhere));
    WARN(fmt::format("ADR-934: retired -- cow moved {:.4f} m after, in the crowd {} frames, perceived {} frames, alien "
                     "closest {:.2f} m; no retire -- cow moved {:.2f} m, in the crowd {} frames, perceived {} frames, "
                     "alien closest {:.2f} m",
                     gone.cowMovedAfter, gone.framesInCrowd, gone.framesPerceived, gone.alienClosest,
                     here.cowMovedAfter, here.framesInCrowd, here.framesPerceived, here.alienClosest));
}

TEST_CASE("a scrub past a retire lands where the play did", "[entity][staging][adr934][seek]") {
    CastWorld played(field());
    (void)run(played, true, 12.0);
    REQUIRE(played.body("cow").retired());

    CastWorld sought(field());
    entity::EntityWorld& world = sought.world;
    const auto director = [&world](double now) {
        if (now >= kRetireAt) {
            world.retire("cow");
        }
    };
    sought.director = director;
    sought.step();
    entity::EntityWorld::SeekHooks hooks;
    hooks.before = [&director](double now, double) { director(now); };
    sought.world.seek(played.time() - CastWorld::kStep, &sought.params, &sought.bus, CastWorld::kStep, {}, &hooks);

    CHECK(sought.body("cow").retired());
    for (const char* name : {"cow", "alien"}) {
        const glm::vec3 a = played.body(name).state().position();
        const glm::vec3 b = sought.body(name).state().position();
        INFO(name << ": played (" << a.x << ", " << a.z << ") sought (" << b.x << ", " << b.z << ")");
        CHECK(glm::length(a - b) < 1e-3f);
        CHECK(played.body(name).state().yaw == Approx(sought.body(name).state().yaw).margin(1e-4));
    }
    CHECK_FALSE(perceives(sought.body("alien"), indexOf(sought.world, "cow")));

    // Control: a seek back before the retire gives the cow back to the world -- `reset` un-retires it
    // and the replay does not reach the step that retires it again.
    sought.world.seek(1.0, &sought.params, &sought.bus, CastWorld::kStep, {}, &hooks);
    CHECK_FALSE(sought.body("cow").retired());
}

TEST_CASE("the quality recorder's track of a retired body ends where it was taken", "[motion][quality][adr934]") {
    CastWorld w(field());
    (void)run(w, true, 10.0);
    const entity::CharacterQuality cow = w.quality("cow");
    const entity::CharacterQuality alien = w.quality("alien");
    // Two seconds of cow, ten of alien: nothing after the retire is the cow's to be measured by --
    // standing in a beam, or (before ADR-934) grazing unseen.
    CHECK(cow.seconds == Approx(kRetireAt).margin(2.0 * CastWorld::kStep));
    CHECK(alien.seconds > 9.9);
}
