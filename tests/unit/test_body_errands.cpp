// ADR-944: an errand to a body walks to where the body is.
//
// `interest` (GV3's roam, graze and watch) walked to a point: its stand-off from where the body stood
// when the choice was made. A body moves. GV2-multicam's vane chose to watch ember at 33.3 s, ember
// walked across vane's line and stopped, and vane walked on to its point through ember -- 0.26 m apart
// at 45.0 s, where the test that guards against walking through each other wants over 1.2 m. The same
// film at another seed did it at 0.73 m. `social`'s greeting has walked to the body since Phase D for
// the same reason; now `interest` does.
//
// Each case with a control that must read the opposite (ADR-182):
//
//   into line  a watcher chooses a body; the body walks into the watcher's line and stops; the watcher
//              stops outside it, within its approach of where the body now is  |  the old errand, a
//              walk to the stand-off from where the body stood, walks through it
//   judged     the selector judges a body errand where the body is (ADR-909's habits hold it)  |  a
//              greeting, which follows a body too, is still judged nowhere
//   scrubbed   a seek mid-errand lands where the play did

#include "entity/action.hpp"
#include "entity/decision.hpp"
#include "entity/entity.hpp"
#include "support/cast_world.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

using namespace avgen;
using Catch::Approx;
using testsupport::behavior;
using testsupport::CastMember;
using testsupport::CastWorld;

namespace {

constexpr float kApproach = 7.0f;

// Watches bodies, and nothing else: an aware decider with one `interest` that only characters score.
CastMember watcher() {
    CastMember m;
    m.name = "watcher";
    m.seed = 5151;
    m.perceives = true;
    m.perception.range = 60.0f;
    m.perception.fieldOfView = 360.0f;
    m.gait.walkSpeed = 3.0f;
    m.behaviors.push_back(behavior(
        "decide", {{"hertz", 4.0},
                   {"dwellTicks", 1.0},
                   {"mind", nlohmann::json::object()},
                   {"considerers",
                    {{{"kind", "interest"},
                      {"name", "watch"},
                      {"source", "perceived"},
                      {"weights", {{"character", 1.0}, {"glow", 0.0}, {"landmark", 0.0}, {"water", 0.0}, {"vista", 0.0}}},
                      {"approach", kApproach},
                      {"minRange", kApproach},
                      {"maxRange", 60.0},
                      {"dwell", 3.0},
                      {"activity", "observe"}},
                     {{"kind", "idle"}, {"name", "idle"}, {"weight", 0.01}}}}}));
    m.behaviors.push_back(behavior("ground", {{"bodyRadius", 0.9}}));
    return m;
}

// Stands 30 m north of the watcher, then walks south into the watcher's line and stops 16 m from it.
CastMember subject() {
    CastMember m;
    m.name = "subject";
    m.seed = 5252;
    m.at = glm::vec3(0.0f, 0.0f, 30.0f);
    m.gait.walkSpeed = 3.0f;
    m.behaviors.push_back(behavior("ground", {{"bodyRadius", 0.9}}));
    return m;
}

std::function<void(double)> intoLine(entity::EntityWorld& world) {
    return [&world](double now) {
        if (now < 1e-6) {
            entity::ActionDesc walk;
            walk.kind = entity::ActionKind::Move;
            walk.name = "across";
            walk.target.kind = entity::TargetKind::Point;
            walk.target.point = glm::vec3(0.0f, 0.0f, 16.0f);
            world.find("subject")->actions().override(std::vector<entity::ActionDesc>{walk},
                                                      entity::Authority::Action, now);
        }
    };
}

float apart(const entity::Entity& a, const entity::Entity& b) {
    const glm::vec3 p = a.state().position();
    const glm::vec3 q = b.state().position();
    return glm::length(glm::vec2(p.x - q.x, p.z - q.z));
}

} // namespace

TEST_CASE("a walk to a body whose subject steps into its line and stops ends outside it",
          "[decide][adr944]") {
    CastWorld w({watcher(), subject()});
    w.director = intoLine(w.world);
    entity::Entity& me = w.body("watcher");
    entity::Entity& them = w.body("subject");
    std::string chose;
    double arrivedAt = -1.0;
    w.world.setActionListener([&](const entity::ActionEvent& e) {
        if (e.entity == "watcher" && e.action == "subject" && e.result == entity::ActionResult::Completed &&
            arrivedAt < 0.0) {
            arrivedAt = e.time;
        }
    });
    float closest = 1e9f;
    float atArrival = -1.0f;
    w.play(14.0, [&] {
        closest = std::min(closest, apart(me, them));
        if (arrivedAt > 0.0 && atArrival < 0.0) {
            atArrival = apart(me, them);
        }
        for (const auto& b : me.behaviors()) {
            entity::DecisionDebug dbg;
            if (b->decisionDebug(dbg) && !dbg.chosen.empty() && chose.empty()) {
                chose = std::string(dbg.chosen);
            }
        }
    });
    INFO("chose '" << chose << "'; the walk ended at " << arrivedAt << " s, " << atArrival << " m from the subject;"
                   << " closest " << closest << " m; the subject ended at z " << them.state().position().z);
    REQUIRE(chose == "subject");
    REQUIRE(arrivedAt > 0.0);
    // The subject did step into the line: it stands where the old walk's point lay beyond it.
    REQUIRE(them.state().position().z == Approx(16.0f).margin(entity::kMoveTolerance + 0.05f));
    // Outside the subject, all the way, and ended within its approach of where the subject now is.
    CHECK(closest >= 1.8f);
    CHECK(atArrival <= kApproach + 0.5f);

    // Control: the errand before ADR-944 -- a walk to the stand-off from where the subject stood when
    // the choice was made, 7 m this side of (0, 30). The subject stops at (0, 16) in its line, and the
    // walk goes through it.
    CastMember plain = watcher();
    plain.behaviors.erase(plain.behaviors.begin()); // no decider: the old errand is pushed by hand
    CastWorld old({plain, subject()});
    old.director = intoLine(old.world);
    entity::ActionDesc stale;
    stale.kind = entity::ActionKind::Move;
    stale.name = "subject";
    stale.target.kind = entity::TargetKind::Point;
    stale.target.point = glm::vec3(0.0f, 0.0f, 30.0f - kApproach);
    stale.tolerance = kApproach * 0.5f;
    old.body("watcher").actions().push(std::vector<entity::ActionDesc>{stale}, entity::Authority::Routine);
    float oldClosest = 1e9f;
    old.play(14.0, [&] { oldClosest = std::min(oldClosest, apart(old.body("watcher"), old.body("subject"))); });
    INFO("the old errand came within " << oldClosest << " m");
    CHECK(oldClosest < 1.2f);
    WARN(fmt::format("ADR-944, a subject stepping into the line: closest {:.2f} m, the walk ended {:.2f} m from it; "
                     "the old errand {:.2f} m",
                     closest, atArrival, oldClosest));
}

TEST_CASE("a body errand is judged where the body is, and a greeting still nowhere", "[decide][adr944]") {
    CastWorld w({watcher(), subject()});
    w.step();
    entity::Option watch;
    watch.name = "subject";
    watch.kind = static_cast<std::uint8_t>(entity::InterestKind::Character);
    watch.target = glm::vec3(0.0f, 0.0f, 30.0f);
    watch.hasTarget = true;
    entity::ActionDesc walk;
    walk.kind = entity::ActionKind::Move;
    walk.target.kind = entity::TargetKind::EntityRef;
    walk.target.name = "subject";
    walk.tolerance = kApproach;
    const std::vector<entity::ActionDesc> actions{walk};
    watch.actions = std::span<const entity::ActionDesc>(actions);
    glm::vec2 end(0.0f);
    float tolerance = 0.0f;
    bool goes = false;
    CHECK(entity::optionDestination(watch, glm::vec3(0.0f), end, tolerance, goes));
    CHECK(end == glm::vec2(0.0f, 30.0f));
    CHECK(tolerance == kApproach);
    CHECK(goes);
    // Control: a greeting walks after a body too, and says nothing about a place -- unchanged.
    entity::Option greet = watch;
    greet.kind = 255;
    CHECK_FALSE(entity::optionDestination(greet, glm::vec3(0.0f), end, tolerance, goes));
    CHECK(goes);
}

TEST_CASE("a scrub mid-errand to a body lands where the play did", "[decide][adr944][seek]") {
    for (const double at : {4.0, 9.0}) {
        CastWorld played({watcher(), subject()});
        played.director = intoLine(played.world);
        played.play(at);
        CastWorld sought({watcher(), subject()});
        const auto director = intoLine(sought.world);
        sought.director = director;
        sought.step();
        entity::EntityWorld::SeekHooks hooks;
        hooks.before = [&director](double now, double) { director(now); };
        sought.world.seek(played.time() - CastWorld::kStep, &sought.params, &sought.bus, CastWorld::kStep, {}, &hooks);
        for (const char* name : {"watcher", "subject"}) {
            const glm::vec3 a = played.body(name).state().position();
            const glm::vec3 b = sought.body(name).state().position();
            INFO(name << " at " << at << " s: played (" << a.x << ", " << a.z << ") sought (" << b.x << ", " << b.z << ")");
            CHECK(glm::length(a - b) < 1e-3f);
        }
    }
}
