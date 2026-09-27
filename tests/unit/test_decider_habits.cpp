// ADR-909: a decider that does not walk straight back to where it just was, does not stand about
// for ever when its scene says it may not, and does not walk every errand at one pace.
//
// The GV3 audit traced the aliens' patterns to the decider: `sage` alternated between a glow patch
// and the ring of its post every 15 s for two minutes (holdPost pulled it back to the edge of its
// tolerance, the next errand pulled it out again), then stood on the ring for 67 s to the end of the
// film because nothing limits how long `idle` or an open-ended pose may win; and every alien moved
// at exactly 3.07 m/s, because no considerer ever set a pace. Each arm reads the answer off the
// ADR-910 recorder or off the options themselves, with a control that must read the opposite
// (ADR-182):
//
//   loop        a post-and-errand decider revisits A->B->A with the loop memory off  |  on (the
//               default) it does not
//   still       an idle-heavy decider stands the whole run  |  `maxStillSeconds` sends it walking
//   post        holdPost walks back to half its tolerance, and a `duration` ends its pose  |  the
//               considerer's own defaults are the sentry's open-ended stand
//   pace        a `speedRange` draws each errand's pace from the range, the same pace for the same
//               decision, a different one for the next  |  without one, the walk speed
//   urgency     a reaction hurries by its urgency  |  `urgentSpeed` 1 is the walk speed
//   seek        every one of those survives a seek: the scrub lands where the play did

#include "entity/decision.hpp"
#include "entity/entity.hpp"
#include "entity/mind.hpp"
#include "support/cast_world.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

using namespace avgen;
using Catch::Approx;
using testsupport::behavior;
using testsupport::CastMember;
using testsupport::CastWorld;

namespace {

// Four places around home, 22 m out: a grazing round the post keeps pulling the body back to.
std::vector<entity::InterestPoint> fourPlaces() {
    std::vector<entity::InterestPoint> points;
    const char* names[] = {"east", "north", "west", "south"};
    const glm::vec2 at[] = {{22.0f, 0.0f}, {0.0f, 22.0f}, {-22.0f, 0.0f}, {0.0f, -22.0f}};
    for (int i = 0; i < 4; ++i) {
        entity::InterestPoint p;
        p.name = names[i];
        p.position = glm::vec3(at[i].x, 0.0f, at[i].y);
        p.kind = entity::InterestKind::Landmark;
        p.weight = 1.0f;
        points.push_back(p);
    }
    return points;
}

// `sage`'s shape, scaled down: an aware decider (Phase D, as every GV3 alien is) with a post it is
// pulled back to harder the further it goes, and a round of places it is pulled out to, with
// nothing remembering where it has been -- so what keeps it from pacing is the loop memory and
// nothing else. `maxStillSeconds` as GV3 will have it, in both arms, so the only difference
// between them is the loop memory.
CastMember poster(nlohmann::json decideExtra = nlohmann::json::object()) {
    nlohmann::json decide = {
        {"hertz", 2.0},
        {"dwellTicks", 2.0},
        {"margin", 0.05},
        {"visitedCapacity", 0.0},
        {"maxStillSeconds", 8.0},
        {"mind", nlohmann::json::object()},
        {"considerers",
         {{{"kind", "holdPost"}, {"name", "post"}, {"weight", 0.6}, {"tolerance", 4.0}, {"pull", 0.2},
           {"activity", "observe"}, {"duration", 2.0}},
          {{"kind", "interest"}, {"name", "graze"}, {"source", "omniscient"}, {"weight", 1.5},
           {"weights", {{"landmark", 1.0}}}, {"minRange", 8.0}, {"maxRange", 60.0}, {"approach", 2.0},
           {"dwell", 2.0}, {"activity", "observe"}, {"noveltyPenalty", 1.0}}}},
    };
    for (const auto& [k, v] : decideExtra.items()) {
        decide[k] = v;
    }
    CastMember m;
    m.name = "poster";
    m.seed = 31337;
    m.gait.walkSpeed = 2.0f;
    m.behaviors.push_back(behavior("decide", decide));
    return m;
}

const entity::DecisionDebug debugOf(const entity::Entity& e) {
    entity::DecisionDebug out;
    for (const auto& b : e.behaviors()) {
        if (b->decisionDebug(out)) {
            break;
        }
    }
    return out;
}

// Where a body stood and when, with the recorder's own definition of a stop (still under 0.1 m/s
// for at least a quarter of a second), so an A->B->A can be timed as well as placed.
struct Stop {
    glm::vec2 at{0.0f};
    double from = 0.0;
    double until = 0.0;
};

struct StopLog {
    std::vector<Stop> stops;
    double still = 0.0;
    bool open = false;
    void sample(const entity::Entity& e, double time) {
        if (e.state().speed < 0.1f) {
            still += CastWorld::kStep;
            if (!open && still >= 0.25) {
                const glm::vec3 p = e.state().position();
                stops.push_back(Stop{glm::vec2(p.x, p.z), time, time});
                open = true;
            }
            if (open) {
                stops.back().until = time;
            }
        } else {
            still = 0.0;
            open = false;
        }
    }
    // A->B->A round trips: a stop within `radius` of the stop before last, reached less than
    // `window` seconds after the body left that one.
    [[nodiscard]] int quickLoops(float radius, double window) const {
        int n = 0;
        for (std::size_t k = 2; k < stops.size(); ++k) {
            if (glm::length(stops[k].at - stops[k - 2].at) < radius && stops[k].from - stops[k - 2].until < window) {
                ++n;
            }
        }
        return n;
    }
};

} // namespace

TEST_CASE("a decider does not walk straight back to the place it has just left",
          "[decide][adr909][loop]") {
    const auto run = [](double loopSeconds) {
        CastWorld w({poster({{"loopSeconds", loopSeconds}})});
        w.world.setExtraInterestPoints(fourPlaces());
        StopLog log;
        w.play(240.0, [&] { log.sample(w.body("poster"), w.time()); });
        return std::pair(log, w.quality("poster"));
    };
    // The control first. With nothing remembering where it set out from, the post's pull -- rising
    // with every metre -- and the errand's -- rising with every metre nearer -- cross halfway, and
    // the body paces between the crossings: out to 11.6 m, turn, back to 4.5 m, turn, out again,
    // every nine seconds, for the whole run. The owner's walk, stop, turn round, walk back, in its
    // purest form, and it survives Phase D's commitment boost.
    const auto [pacing, pacingQ] = run(0.0);
    const int pacingLoops = pacing.quickLoops(4.0f, 20.0);
    WARN(fmt::format("loop memory off: {} stops, {} A->B->A round trips inside 20 s, {} revisits in all, "
                     "{:.0f} m travelled",
                     pacing.stops.size(), pacingLoops, pacingQ.behaviour.revisits, pacingQ.motion.travelMetres));
    REQUIRE(pacing.stops.size() > 10);
    CHECK(pacingLoops > 10);

    // The default: 20 s of memory. It walks all the way out to what it chose, and it may go home
    // again -- a post is a post -- but never by turning round on the way, and never home within 20 s
    // of having left it.
    const auto [ranging, rangingQ] = run(20.0);
    const int rangingLoops = ranging.quickLoops(4.0f, 20.0);
    WARN(fmt::format("loop memory 20 s (the default): {} stops, {} A->B->A round trips inside 20 s, {} "
                     "revisits in all, {:.0f} m travelled",
                     ranging.stops.size(), rangingLoops, rangingQ.behaviour.revisits, rangingQ.motion.travelMetres));
    REQUIRE(ranging.stops.size() > 5);
    {
        std::string list;
        for (const Stop& st : ranging.stops) {
            list += fmt::format("({:.1f},{:.1f}) {:.1f}-{:.1f}s; ", st.at.x, st.at.y, st.from, st.until);
        }
        INFO(list);
    }
    CHECK(rangingLoops == 0);
    CHECK(rangingQ.behaviour.revisits * 4 <= pacingQ.behaviour.revisits);
}

TEST_CASE("the selector's loop rule, one option at a time", "[decide][adr909][loop]") {
    // The rule itself, against options a test builds: a walk to where the body set out from is
    // discounted inside the window and not after it; a walk that turns the body round is discounted
    // while it walks and not while it stands; the errand in hand, an order and a reaction are left
    // alone. Each against the case beside it that is not discounted (ADR-182).
    class Listed final : public entity::IConsiderer {
    public:
        explicit Listed(std::vector<std::pair<std::string, glm::vec3>> places) : places_(std::move(places)) {}
        bool directedHome = false;
        float urgentHome = 0.0f;
        void consider(const entity::DecisionContext& ctx, std::vector<entity::Option>& out) const override {
            (void)ctx;
            actions_.clear();
            actions_.reserve(places_.size());
            for (const auto& place : places_) {
                entity::ActionDesc walk;
                walk.kind = entity::ActionKind::Move;
                walk.target.kind = entity::TargetKind::Point;
                walk.target.point = place.second;
                walk.tolerance = 1.0f;
                actions_.push_back(walk);
            }
            for (std::size_t i = 0; i < places_.size(); ++i) {
                out.push_back(entity::Option{places_[i].first, 1.0f,
                                             std::span<const entity::ActionDesc>(&actions_[i], 1),
                                             entity::Authority::Routine});
                if (places_[i].first == "home") {
                    out.back().directed = directedHome;
                    out.back().urgency = urgentHome;
                }
            }
        }

    private:
        std::vector<std::pair<std::string, glm::vec3>> places_;
        mutable std::vector<entity::ActionDesc> actions_;
    };
    // The body at (10, 0), facing east (+x), having set out from home at (0, 0) five seconds ago.
    // Standing, a walk home is a revisit and not on offer inside the window; walking an errand east,
    // a walk back is not on offer until the errand is over.
    Listed listed({{"home", glm::vec3(0.0f)}, {"ahead", glm::vec3(30.0f, 0.0f, 0.0f)},
                   {"aside", glm::vec3(10.0f, 0.0f, 20.0f)}, {"behind", glm::vec3(-20.0f, 0.0f, 5.0f)}});
    const entity::IConsiderer* views[] = {&listed};
    entity::EntityState state;
    state.travel = glm::vec3(10.0f, 0.0f, 0.0f);
    state.yaw = 1.5707963f;
    const entity::Departure left{glm::vec2(0.0f), 0.0};
    const auto scores = [&](double time, float speed, std::string_view walkingTo = {}) {
        state.speed = speed;
        entity::Selector selector;
        entity::SelectorSettings settings;
        settings.hertz = 0.0f; // decide every call
        selector.setSettings(settings);
        entity::DecisionContext ctx;
        ctx.time = time;
        ctx.state = &state;
        ctx.departures = std::span<const entity::Departure>(&left, 1);
        ctx.loopSeconds = 20.0;
        if (!walkingTo.empty()) {
            // Commit to an errand first, so there is an errand in hand for the rule to exempt.
            entity::DecisionContext first = ctx;
            first.time = time - 0.5;
            first.departures = {};
            first.loopSeconds = 0.0;
            Listed only({{std::string(walkingTo), glm::vec3(30.0f, 0.0f, 0.0f)}});
            const entity::IConsiderer* one[] = {&only};
            (void)selector.select(first, one);
        }
        (void)selector.select(ctx, views);
        std::map<std::string, float> out;
        for (const entity::Option& o : selector.options()) {
            out[std::string(o.name)] = o.score;
        }
        return out;
    };
    // Standing, five seconds after leaving home: home is a revisit; nothing is a turn back.
    auto standing = scores(5.0, 0.0f);
    CHECK(standing["home"] == 0.0f);
    CHECK(standing["behind"] == Approx(1.0f));
    CHECK(standing["ahead"] == Approx(1.0f));
    CHECK(standing["aside"] == Approx(1.0f));
    // Twenty-five seconds after: the window is over, and home is home again.
    CHECK(scores(25.0, 0.0f)["home"] == Approx(1.0f));
    // An author who wants the soft rule says how soft.
    {
        entity::Selector selector;
        entity::SelectorSettings settings;
        settings.hertz = 0.0f;
        selector.setSettings(settings);
        entity::DecisionContext ctx;
        ctx.time = 5.0;
        state.speed = 0.0f;
        ctx.state = &state;
        ctx.departures = std::span<const entity::Departure>(&left, 1);
        ctx.loopSeconds = 20.0;
        ctx.loopPenalty = 0.25f;
        (void)selector.select(ctx, views);
        for (const entity::Option& o : selector.options()) {
            CHECK(o.score == Approx(o.name == "home" ? 0.25f : 1.0f));
        }
    }
    // Walking east on an errand to "ahead": what lies behind it is a turn back; to the side is not;
    // the errand in hand is not discounted even though it is listed again.
    auto walking = scores(5.0, 2.0f, "ahead");
    CHECK(walking["behind"] == 0.0f); // not on offer until this walk is over
    CHECK(walking["home"] == 0.0f);   // behind it too, which outranks being where it set out from
    CHECK(walking["aside"] == Approx(1.0f));
    CHECK(walking["ahead"] == Approx(1.0f));
    // The window at 0 is no memory at all, and no turn-back rule either: the control for both.
    state.speed = 2.0f;
    {
        entity::Selector selector;
        entity::SelectorSettings settings;
        settings.hertz = 0.0f;
        selector.setSettings(settings);
        entity::DecisionContext ctx;
        ctx.time = 5.0;
        ctx.state = &state;
        ctx.departures = std::span<const entity::Departure>(&left, 1);
        ctx.loopSeconds = 0.0;
        (void)selector.select(ctx, views);
        for (const entity::Option& o : selector.options()) {
            CHECK(o.score == 1.0f);
        }
    }
    // An order and a reaction are not habits.
    listed.directedHome = true;
    CHECK(scores(5.0, 0.0f)["home"] == Approx(1.0f));
    listed.directedHome = false;
    listed.urgentHome = 0.8f;
    CHECK(scores(5.0, 0.0f)["home"] == Approx(1.0f));
}

TEST_CASE("the loop memory is on by default, and its window is a parameter", "[decide][adr909][loop]") {
    // The default is the arm above's 20 s: a decider written with no `loopSeconds` at all is held to
    // it, and the registered parameter is what a scene keyframes. A decider that says nothing else.
    CastMember m;
    m.name = "poster";
    m.behaviors.push_back(behavior("decide", {{"considerers", {{{"kind", "idle"}, {"name", "idle"}}}}}));
    CastWorld w({m});
    auto* p = w.params.findAs<float>("entity/poster/decide/loopSeconds");
    REQUIRE(p != nullptr);
    CHECK(p->value() == 20.0f);
    auto* still = w.params.findAs<float>("entity/poster/decide/maxStillSeconds");
    REQUIRE(still != nullptr);
    CHECK(still->value() == 0.0f); // standing is the scene's to limit: off unless it asks
    // What "back where it was" means and how firmly a walk back is refused are parameters too.
    auto* radius = w.params.findAs<float>("entity/poster/decide/loopRadius");
    auto* penalty = w.params.findAs<float>("entity/poster/decide/loopPenalty");
    REQUIRE(radius != nullptr);
    REQUIRE(penalty != nullptr);
    CHECK(radius->value() == 4.0f);
    CHECK(penalty->value() == 0.0f);
}

TEST_CASE("the loop rule's firmness, moved as a parameter, reaches the choice", "[decide][adr909][loop]") {
    // `loopPenalty` read from the parameter the Inspector and the Parameters panel draw, not only from
    // the file. The fixture is the one place the departure rule alone decides: a post, and one place
    // 16 m out. The body walks out to it, looks, and -- standing there, so the turn-back rule has
    // nothing to say -- its best and only option is home, where it set out from moments ago. Refused
    // (the default), home is not on offer until 20 s after it set out and it waits where it is;
    // allowed (the parameter at 1, a walk straight back worth all of its score), it turns straight
    // round. Measured as the seconds from leaving home to being back.
    const auto roundTrip = [](float firmness) {
        nlohmann::json decide = {
            {"hertz", 2.0},
            {"dwellTicks", 1.0},
            {"margin", 0.05},
            {"visitedCapacity", 0.0},
            {"considerers",
             {{{"kind", "holdPost"}, {"name", "post"}, {"weight", 0.5}, {"tolerance", 3.0}, {"pull", 0.3},
               {"duration", 1.0}},
              {{"kind", "interest"}, {"name", "graze"}, {"source", "omniscient"}, {"weight", 2.0},
               {"weights", {{"landmark", 1.0}}}, {"minRange", 8.0}, {"maxRange", 60.0}, {"approach", 2.0},
               {"dwell", 1.0}, {"activity", "observe"}, {"noveltyPenalty", 1.0}}}},
        };
        CastMember m;
        m.name = "homebody";
        m.seed = 2718;
        m.gait.walkSpeed = 2.0f;
        m.behaviors.push_back(behavior("decide", decide));
        CastWorld w({m});
        entity::InterestPoint place;
        place.name = "place";
        place.position = glm::vec3(16.0f, 0.0f, 0.0f);
        place.kind = entity::InterestKind::Landmark;
        place.weight = 1.0f;
        w.world.setExtraInterestPoints({place});
        auto* penalty = w.params.findAs<float>("entity/homebody/decide/loopPenalty");
        REQUIRE(penalty != nullptr);
        penalty->setBase(firmness);
        double left = -1.0;
        double back = -1.0;
        bool wentOut = false;
        w.play(90.0, [&] {
            const glm::vec3 p = w.body("homebody").state().position();
            const float fromHome = glm::length(glm::vec2(p.x, p.z));
            if (left < 0.0 && fromHome > 3.0f) {
                left = w.time();
            }
            wentOut = wentOut || fromHome > 12.0f;
            if (wentOut && back < 0.0 && fromHome < 3.0f) {
                back = w.time();
            }
        });
        REQUIRE(left >= 0.0);
        REQUIRE(back >= 0.0); // it went out, and it came home
        return back - left;
    };
    const double refused = roundTrip(0.0f);
    const double allowed = roundTrip(1.0f);
    WARN(fmt::format("out to the place and home again: {:.1f} s with loopPenalty 0 (the default), {:.1f} s "
                     "with the parameter at 1",
                     refused, allowed));
    // Allowed, home inside the 20 s window; refused, not inside it. (Measured 11.2 s and 24.2 s: the
    // refused body stands at the place until the window, counted from when it set out, is over, and
    // then walks the 11.5 m home.)
    CHECK(allowed < 20.0);
    CHECK(refused > 20.0);
}

TEST_CASE("a decider that has stood maxStillSeconds is sent on its way", "[decide][adr909][still]") {
    // An idler: `idle` outscores every errand, so without a limit it stands the whole run -- the
    // GV3 audit's `sage` on its ring, in miniature.
    const auto run = [](double maxStill) {
        nlohmann::json decide = {
            {"hertz", 2.0},
            {"maxStillSeconds", maxStill},
            {"considerers",
             {{{"kind", "idle"}, {"name", "idle"}, {"weight", 1.0}, {"activity", "observe"}},
              {{"kind", "interest"}, {"name", "graze"}, {"source", "omniscient"}, {"weight", 0.3},
               {"weights", {{"landmark", 1.0}}}, {"minRange", 3.0}, {"maxRange", 60.0},
               {"approach", 2.0}, {"dwell", 1.5}, {"activity", "observe"}}}},
        };
        CastMember m;
        m.name = "idler";
        m.seed = 777;
        m.gait.walkSpeed = 2.0f;
        m.behaviors.push_back(behavior("decide", decide));
        CastWorld w({m});
        w.world.setExtraInterestPoints(fourPlaces());
        w.play(180.0);
        return std::pair(w.quality("idler"), debugOf(w.body("idler")));
    };
    const auto [stood, stoodDebug] = run(0.0);
    WARN(fmt::format("no limit: longest still {:.1f} s, {:.0f} m travelled", stood.behaviour.longestStillSeconds,
                     stood.motion.travelMetres));
    CHECK(stood.behaviour.longestStillSeconds > 170.0); // the control: it never moved
    CHECK(stoodDebug.stillBreaks == 0);

    constexpr double kLimit = 6.0;
    const auto [walked, walkedDebug] = run(kLimit);
    WARN(fmt::format("maxStillSeconds {:.0f}: longest still {:.1f} s, {} still breaks, {:.0f} m travelled, "
                     "{:.0f}% of the run still",
                     kLimit, walked.behaviour.longestStillSeconds, walkedDebug.stillBreaks,
                     walked.motion.travelMetres, 100.0 * walked.behaviour.stillFraction));
    CHECK(walkedDebug.stillBreaks > 3);
    CHECK(walked.motion.travelMetres > 100.0);
    // Never much longer than the limit: the clock runs from when the body stopped, and what it
    // chooses next begins on the decision tick after that (half a second at 2 Hz) and walks.
    CHECK(walked.behaviour.longestStillSeconds < kLimit + 1.5);
}

TEST_CASE("a restless decider with nothing on offer that walks takes a walk of its own",
          "[decide][adr909][still]") {
    // GV3's `sage`, measured with the recommended settings: at its post, its limit made it restless,
    // and the only options were the post (already reached, a stand) and `idle` -- both of which stand,
    // so both were refused, nothing was chosen, and it stood 26 s against a limit of 10. Here the
    // same shape with nothing else in the world: a post at its own anchor and an `idle`.
    const auto run = [](double maxStill) {
        nlohmann::json decide = {
            {"hertz", 2.0},
            {"maxStillSeconds", maxStill},
            {"considerers",
             {{{"kind", "holdPost"}, {"name", "post"}, {"weight", 0.5}, {"tolerance", 3.0}, {"duration", 2.0}},
              {{"kind", "idle"}, {"name", "idle"}, {"weight", 0.2}}}},
        };
        CastMember m;
        m.name = "keeper";
        m.seed = 99;
        m.gait.walkSpeed = 2.0f;
        m.behaviors.push_back(behavior("decide", decide));
        CastWorld w({m});
        w.play(120.0);
        return std::pair(w.quality("keeper"), debugOf(w.body("keeper")));
    };
    const auto [stood, stoodDebug] = run(0.0);
    WARN(fmt::format("no limit: longest still {:.1f} s, {:.0f} m travelled", stood.behaviour.longestStillSeconds,
                     stood.motion.travelMetres));
    CHECK(stood.behaviour.longestStillSeconds > 110.0); // the control: nothing ever walks it anywhere
    CHECK(stoodDebug.strolls == 0);

    constexpr double kLimit = 6.0;
    const auto [walked, walkedDebug] = run(kLimit);
    WARN(fmt::format("maxStillSeconds {:.0f}: longest still {:.1f} s, {} walks of its own, {:.0f} m travelled",
                     kLimit, walked.behaviour.longestStillSeconds, walkedDebug.strolls, walked.motion.travelMetres));
    CHECK(walkedDebug.strolls > 3);
    CHECK(walked.motion.travelMetres > 40.0);
    CHECK(walked.behaviour.longestStillSeconds < kLimit + 1.5);
}

TEST_CASE("maxStillSeconds cuts a long look short, even in the middle of an aware plan",
          "[decide][adr909][still]") {
    // The other way a body stands: an aware decider (as every GV3 alien is) on an errand whose look
    // at the end lasts longer than the limit -- `vane`'s 8 s, in the audit. Phase D holds a running
    // plan until something beats it, so the still clock has to give the plan up, not just forget
    // the choice, or the hold takes it straight back.
    const auto run = [](double maxStill) {
        nlohmann::json decide = {
            {"hertz", 2.0},
            {"maxStillSeconds", maxStill},
            {"mind", nlohmann::json::object()},
            {"considerers",
             {{{"kind", "interest"}, {"name", "graze"}, {"source", "omniscient"}, {"weight", 1.0},
               {"weights", {{"landmark", 1.0}}}, {"minRange", 8.0}, {"maxRange", 60.0},
               {"approach", 2.0}, {"dwell", 20.0}, {"activity", "observe"}}}},
        };
        CastMember m;
        m.name = "gazer";
        m.seed = 1234;
        m.gait.walkSpeed = 2.0f;
        m.behaviors.push_back(behavior("decide", decide));
        CastWorld w({m});
        w.world.setExtraInterestPoints(fourPlaces());
        w.play(120.0);
        return std::pair(w.quality("gazer"), debugOf(w.body("gazer")));
    };
    const auto [gazed, gazedDebug] = run(0.0);
    WARN(fmt::format("no limit: longest still {:.1f} s", gazed.behaviour.longestStillSeconds));
    CHECK(gazed.behaviour.longestStillSeconds > 19.0); // the control: the whole 20 s look
    constexpr double kLimit = 5.0;
    const auto [cut, cutDebug] = run(kLimit);
    WARN(fmt::format("maxStillSeconds {:.0f}: longest still {:.1f} s, {} still breaks", kLimit,
                     cut.behaviour.longestStillSeconds, cutDebug.stillBreaks));
    CHECK(cutDebug.stillBreaks > 2);
    CHECK(cut.behaviour.longestStillSeconds < kLimit + 1.5);
}

TEST_CASE("holdPost walks back to half its tolerance, and a duration ends its pose",
          "[decide][adr909][post]") {
    entity::EntityState state;
    state.anchor = glm::vec3(0.0f);
    state.travel = glm::vec3(20.0f, 0.0f, 0.0f); // 20 m from its post
    entity::DecisionContext ctx;
    ctx.state = &state;

    nlohmann::json timed = {{"tolerance", 8.0}, {"activity", "observe"}, {"duration", 5.0}};
    entity::HoldPostConsiderer post(&timed);
    post.setName("post");
    std::vector<entity::Option> out;
    post.consider(ctx, out);
    REQUIRE(out.size() == 1);
    REQUIRE(out[0].actions.size() == 2);
    CHECK(out[0].actions[0].kind == entity::ActionKind::Move);
    // Half the tolerance: it arrives 4 m from its post, not on the 8 m ring.
    CHECK(out[0].actions[0].tolerance == Approx(4.0f));
    CHECK(out[0].actions[1].kind == entity::ActionKind::Pose);
    CHECK(out[0].actions[1].duration == Approx(5.0));

    // The control: the considerer's defaults. No duration is the sentry's stand -- open-ended.
    nlohmann::json sentry = {{"tolerance", 8.0}, {"activity", "observe"}};
    entity::HoldPostConsiderer guard(&sentry);
    guard.setName("post");
    std::vector<entity::Option> guardOut;
    guard.consider(ctx, guardOut);
    REQUIRE(guardOut.size() == 1);
    REQUIRE(guardOut[0].actions.size() == 2);
    CHECK(guardOut[0].actions[1].duration == 0.0);
    // And a duration with no activity still ends: a wait rather than a pose.
    nlohmann::json quiet = {{"tolerance", 8.0}, {"duration", 3.0}};
    entity::HoldPostConsiderer plain(&quiet);
    plain.setName("post");
    state.travel = glm::vec3(1.0f, 0.0f, 0.0f); // on its post: no walk
    std::vector<entity::Option> plainOut;
    plain.consider(ctx, plainOut);
    REQUIRE(plainOut.size() == 1);
    REQUIRE(plainOut[0].actions.size() == 1);
    CHECK(plainOut[0].actions[0].kind == entity::ActionKind::Wait);
    CHECK(plainOut[0].actions[0].duration == Approx(3.0));
}

TEST_CASE("a speedRange gives each errand its own pace, drawn once per decision",
          "[decide][adr909][pace]") {
    CastMember m;
    m.name = "strider";
    m.seed = 4096;
    m.gait.walkSpeed = 2.0f;
    CastWorld w({m});
    w.world.setExtraInterestPoints(fourPlaces());
    entity::Entity& body = w.body("strider");
    w.step();

    const auto paces = [&](const nlohmann::json& settings, std::uint64_t tick) {
        entity::InterestConsiderer interest(&settings);
        interest.setName("graze");
        entity::DecisionContext ctx;
        ctx.time = static_cast<double>(tick) * 0.5;
        ctx.tick = tick;
        ctx.state = &body.state();
        ctx.world = &w.world;
        ctx.seed = body.seed();
        std::vector<entity::Option> out;
        interest.consider(ctx, out);
        std::vector<float> speeds;
        for (const entity::Option& o : out) {
            REQUIRE_FALSE(o.actions.empty());
            speeds.push_back(o.actions[0].speed);
        }
        return speeds;
    };
    const nlohmann::json ranged = {{"source", "omniscient"}, {"weights", {{"landmark", 1.0}}}, {"maxRange", 60.0},
                                   {"speedRange", {0.6, 1.4}}};
    const std::vector<float> first = paces(ranged, 10);
    REQUIRE(first.size() == 4);
    std::set<float> distinct;
    for (const float s : first) {
        CHECK(s >= 0.6f * 2.0f);
        CHECK(s <= 1.4f * 2.0f);
        distinct.insert(s);
    }
    CHECK(distinct.size() == 4); // each errand its own pace
    // A pure function of the decision: the same tick draws the same paces (D2) ...
    CHECK(paces(ranged, 10) == first);
    // ... and the next decision draws others.
    CHECK(paces(ranged, 11) != first);

    // The control: no range, no pace -- 0, which a `move` reads as the gait's walk speed.
    const nlohmann::json plain = {{"source", "omniscient"}, {"weights", {{"landmark", 1.0}}}, {"maxRange", 60.0}};
    for (const float s : paces(plain, 10)) {
        CHECK(s == 0.0f);
    }

    // An end at 0 is the walk speed, as the Parameters panel's label says ("0 = walk"): from 0 to
    // 1.5 is a walk to one and a half times a walk, never a crawl. Read literally it would have drawn
    // paces from a standstill up.
    const nlohmann::json halfOpen = {{"source", "omniscient"}, {"weights", {{"landmark", 1.0}}}, {"maxRange", 60.0},
                                     {"speedRange", {0.0, 1.5}}};
    for (const std::uint64_t tick : {10u, 11u, 12u}) {
        for (const float s : paces(halfOpen, tick)) {
            CHECK(s >= 1.0f * 2.0f);
            CHECK(s <= 1.5f * 2.0f);
        }
    }
    CHECK(entity::SpeedRange::of(0.0f, 0.0f).set() == false);
    CHECK(entity::SpeedRange::of(1.5f, 0.0f).lo == 1.0f);
    CHECK(entity::SpeedRange::of(1.5f, 0.0f).hi == 1.5f);
    CHECK(entity::SpeedRange::of(0.6f, 1.4f).lo == Approx(0.6f));

    // And the body walks at it: a `move` with a pace is travelled at that pace.
    entity::ActionDesc go;
    go.kind = entity::ActionKind::Move;
    go.target.kind = entity::TargetKind::Point;
    go.target.point = glm::vec3(0.0f, 0.0f, 60.0f);
    go.speed = first[0];
    body.actions().push(go, entity::Authority::Routine);
    float fastest = 0.0f;
    w.play(8.0, [&] { fastest = std::max(fastest, body.state().speed); });
    CHECK(fastest == Approx(first[0]).epsilon(1e-4));
}

TEST_CASE("a reaction hurries by its urgency", "[decide][adr909][pace]") {
    CastMember m;
    m.name = "listener";
    m.seed = 8;
    m.gait.walkSpeed = 2.0f;
    CastWorld w({m});
    entity::Entity& body = w.body("listener");
    w.step();

    const auto speeds = [&](const nlohmann::json& settings, float intensity) {
        entity::ReactConsiderer react(&settings);
        react.setName("beam");
        entity::PerceivedEvent heard;
        heard.sequence = 1;
        heard.position = glm::vec3(30.0f, 0.0f, 0.0f);
        heard.intensity = intensity;
        heard.time = 0.0;
        entity::MindView mind;
        mind.events = std::span<const entity::PerceivedEvent>(&heard, 1);
        entity::DecisionContext ctx;
        ctx.time = 0.5;
        ctx.tick = 1;
        ctx.state = &body.state();
        ctx.world = &w.world;
        ctx.seed = body.seed();
        ctx.mind = &mind;
        std::vector<entity::Option> out;
        react.consider(ctx, out);
        REQUIRE(out.size() == 2);
        return std::pair(out[0].actions[0].speed, out[1].actions[0].speed); // approach, flee
    };
    const nlohmann::json plain = nlohmann::json::object();
    // Full intensity: both at the default `urgentSpeed`, 1.5 x the walk speed.
    const auto [approachLoud, fleeLoud] = speeds(plain, 1.0f);
    CHECK(approachLoud == Approx(3.0f));
    CHECK(fleeLoud == Approx(3.0f));
    // Faint: the approach by its intensity, the flee by half as much again (its urgency is 1.5x).
    const auto [approachFaint, fleeFaint] = speeds(plain, 0.4f);
    CHECK(approachFaint == Approx(2.0f * (1.0f + 0.5f * 0.4f)));
    CHECK(fleeFaint == Approx(2.0f * (1.0f + 0.5f * 0.6f)));
    CHECK(approachFaint < approachLoud);

    // The control: `urgentSpeed` 1 is no hurry at all, which is the walk speed -- 0 in the action.
    const nlohmann::json calm = {{"urgentSpeed", 1.0}};
    const auto [approachCalm, fleeCalm] = speeds(calm, 1.0f);
    CHECK(approachCalm == 0.0f);
    CHECK(fleeCalm == 0.0f);
}

TEST_CASE("a scrub lands where the play did with every one of ADR-907 to 909's memories",
          "[decide][wander][adr909][seek]") {
    // A wanderer on its forward cone with a turning circle and an eased pace, beside a decider with
    // the loop memory, the still clock, a timed post and a paced errand: all of it state a replay
    // has to rebuild (ADR-700), and a seek that forgot any of it lands somewhere the play did not.
    const auto cast = [] {
        CastMember wanderer;
        wanderer.name = "wanderer";
        wanderer.seed = 5150;
        wanderer.gait.accel = 2.5f;
        wanderer.gait.decel = 3.5f;
        wanderer.gait.accelAuthored = true;
        wanderer.behaviors.push_back(behavior("wander", {{"speed", 1.4}, {"minRange", 4.0}, {"maxRange", 12.0},
                                                         {"pauseMin", 0.5}, {"pauseMax", 2.0},
                                                         {"homeRadius", 20.0}, {"turnRadius", 2.0}}));
        CastMember decider = poster({{"maxStillSeconds", 5.0}});
        decider.at = glm::vec3(40.0f, 0.0f, 0.0f);
        decider.gait.turnRadius = 1.5f;
        decider.gait.turnRate = 100.0f;
        return std::vector<CastMember>{wanderer, decider};
    };
    CastWorld played(cast());
    played.world.setExtraInterestPoints(fourPlaces());
    played.play(45.0);

    CastWorld sought(cast());
    sought.world.setExtraInterestPoints(fourPlaces());
    sought.step();
    sought.world.seek(played.time() - CastWorld::kStep, &sought.params, &sought.bus);

    for (const char* name : {"wanderer", "poster"}) {
        const glm::vec3 a = played.body(name).state().position();
        const glm::vec3 b = sought.body(name).state().position();
        INFO(name << ": played (" << a.x << ", " << a.z << ") sought (" << b.x << ", " << b.z << ")");
        CHECK(glm::length(a - b) < 1e-3f);
        CHECK(played.body(name).state().yaw == Approx(sought.body(name).state().yaw).margin(1e-4));
    }
    // And the arm has something in it: both bodies moved, and the decider decided.
    CHECK(glm::length(glm::vec2(played.body("wanderer").state().travel.x,
                                played.body("wanderer").state().travel.z)) > 1.0f);
    CHECK(debugOf(played.body("poster")).decisions > 2);
}
