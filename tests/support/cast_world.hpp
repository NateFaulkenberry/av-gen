#pragma once

// A handful of bodies in an `EntityWorld`, stepped the application's way and watched by the
// character quality recorder (ADR-910). The fixture ADR-907, 908 and 909's regression gates share:
// each of those decisions claims to move a number the recorder measures, and each gate runs a body
// through the real behaviours and reads that number back.
//
// No composition and no GPU: the parameters an entity binds its transform to, an optional
// navigator (flat y = 0 ground when there is none, which is `Navigator`'s own answer), and the
// frame convention every other entity test uses -- frame 0 at t = 0 with a zero delta, then full
// 60 Hz steps.

#include "entity/character_quality.hpp"
#include "entity/entity.hpp"
#include "entity/navigation.hpp"
#include "params/parameter_set.hpp"
#include "signals/signal_bus.hpp"

#include <nlohmann/json.hpp>

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace avgen::testsupport {

inline entity::BehaviorDesc behavior(const char* kind, nlohmann::json settings = nlohmann::json::object()) {
    entity::BehaviorDesc d;
    d.kind = kind;
    d.name = kind;
    settings["kind"] = kind;
    d.settings = std::move(settings);
    return d;
}

// One body: its name, where it is placed, what it does and how it walks.
struct CastMember {
    std::string name;
    glm::vec3 at{0.0f};
    std::vector<entity::BehaviorDesc> behaviors;
    entity::GaitSettings gait{};
    std::uint32_t seed = 0; // 0 = derived from the name and the scene seed
    float yawDegrees = 0.0f; // placed facing
    // Senses and words, for a body that notices others (ADR-290) and a body others notice by what it
    // is (Phase D §25). Off and empty by default, which is every cast written before ADR-934.
    bool perceives = false;
    entity::PerceptionSettings perception{};
    std::vector<std::string> tags;
};

struct CastWorld {
    static constexpr double kStep = 1.0 / 60.0;

    params::ParameterSet params;
    signals::SignalBus bus;
    entity::EntityWorld world;
    entity::CharacterQualityRecorder recorder;
    int frame = 0;

    explicit CastWorld(std::vector<CastMember> cast, const entity::Navigator* navigator = nullptr,
                       entity::CharacterQualityThresholds thresholds = {}, std::uint32_t sceneSeed = 7u)
        : recorder(thresholds) {
        if (navigator != nullptr) {
            world.setNavigator(*navigator);
        }
        std::vector<entity::EntityDesc> descs;
        std::vector<entity::NodeBinding> bindings;
        for (CastMember& m : cast) {
            const std::string prefix = "nodes/" + m.name + "/";
            params.add(params::ParamDesc<glm::vec3>{.path = prefix + "position", .defaultValue = glm::vec3(0.0f),
                                                    .hardMin = glm::vec3(-1e4f), .hardMax = glm::vec3(1e4f)});
            params.add(params::ParamDesc<glm::vec3>{.path = prefix + "rotation",
                                                    .defaultValue = glm::vec3(0.0f, m.yawDegrees, 0.0f),
                                                    .hardMin = glm::vec3(-360.0f), .hardMax = glm::vec3(360.0f)});
            params.add(params::ParamDesc<glm::vec3>{.path = prefix + "scale", .defaultValue = glm::vec3(1.0f),
                                                    .hardMin = glm::vec3(0.001f), .hardMax = glm::vec3(100.0f)});
            entity::EntityDesc d;
            d.name = m.name;
            d.node = m.name;
            d.seed = m.seed;
            d.cullDistance = 0.0f; // the distance bands must not decide a behaviour test
            d.gait = m.gait;
            d.behaviors = std::move(m.behaviors);
            d.perceives = m.perceives;
            d.perception = m.perception;
            d.tags = m.tags;
            descs.push_back(std::move(d));
            entity::NodeBinding b;
            b.node = m.name;
            b.exists = true;
            b.transformPrefix = prefix;
            b.anchor = m.at;
            bindings.push_back(std::move(b));
        }
        world.setEntities(std::move(descs), sceneSeed);
        world.setBindings(std::move(bindings));
        world.registerParameters(params);
        world.bind(params);
    }

    [[nodiscard]] entity::Entity& body(std::string_view name) {
        entity::Entity* e = world.find(name);
        if (e == nullptr) {
            throw std::runtime_error("no body named " + std::string(name));
        }
        return *e;
    }

    [[nodiscard]] double time() const { return static_cast<double>(frame) * kStep; }

    // A director, as the composition runs one: at the top of every frame, before any body steps
    // (ADR-209). A test that also seeks hands the same function to `EntityWorld::seek`'s `before`
    // hook, so a scrub replays it on the step a play ran it. Empty: no director.
    std::function<void(double now)> director;

    // One application frame. The recorder sees every frame, the first as a baseline.
    void step() {
        params.resetFinals();
        if (director) {
            director(time());
        }
        entity::EntityUpdate u;
        u.time = time();
        u.dt = frame == 0 ? 0.0 : kStep;
        u.frameIndex = static_cast<std::uint64_t>(frame);
        u.bus = &bus;
        u.distanceDetail = false;
        world.update(u, params);
        recorder.record(world, u.time, u.dt);
        ++frame;
    }

    void play(double seconds, const std::function<void()>& each = {}) {
        const int frames = static_cast<int>(seconds / kStep + 0.5);
        for (int i = 0; i < frames; ++i) {
            step();
            if (each) {
                each();
            }
        }
    }

    [[nodiscard]] entity::CharacterQuality quality(std::string_view name) const {
        for (const entity::CharacterQuality& q : recorder.report().characters) {
            if (q.name == name) {
                return q;
            }
        }
        throw std::runtime_error("no quality track named " + std::string(name));
    }
};

} // namespace avgen::testsupport
