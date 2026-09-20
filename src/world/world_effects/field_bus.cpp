#include "world/world_effects/field_bus.hpp"

#include <algorithm>
#include <string>

namespace avgen::world::fields {
namespace {

// The vortex's velocity is already a world-space m/s vector and its `density` is already the
// normalised shape, so the vortex arm is a rename rather than a conversion. The wind's is not: the
// wind field answers a unit direction in XZ and a separate strength, because a plant wants those
// apart. Multiplying them here is the whole of the translation, and it is done here rather than in
// `sampleWind` so that nothing about the wind field changes to serve this file.
[[nodiscard]] FlowSample fromWind(const wind::WindSample& w) {
    FlowSample out;
    out.units = FlowUnits::Normalised;
    const float speed = w.strength * (1.0f + w.gust);
    out.flow = glm::vec3(w.direction.x * speed, 0.0f, w.direction.y * speed);
    out.strength = w.strength;
    out.gust = w.gust;
    out.phase = w.phase;
    return out;
}

// `density` is the funnel's normalised shape in 0..1 and is exactly 0 outside it, which is what
// makes `strength` mean the same thing for both sources: how much of this field is here.
//
// The phase is derived rather than carried, because `VortexSample` has no phase and inventing one
// inside `core/vortex.cpp` would change a file two GPU parity tests pin. `radialT` and `depthT` are
// positions within the funnel, so a turn of tau across each is a spatial phase with the property
// that matters: the same number at the same place, a different one a few metres away, and no
// dependence on a clock.
[[nodiscard]] FlowSample fromVortex(const vortex::VortexSample& v) {
    FlowSample out;
    out.units = FlowUnits::MetresPerSecond;
    out.flow = v.velocity;
    out.strength = v.density;
    out.gust = 0.0f; // a funnel has no travelling fronts; see FlowSample::gust
    out.phase = vortex::kTau * (v.radialT + v.depthT);
    return out;
}

} // namespace

const char* flowUnitsName(FlowUnits units) {
    switch (units) {
    case FlowUnits::Normalised: return "normalised";
    case FlowUnits::MetresPerSecond: return "m/s";
    }
    return "unknown";
}

const char* fieldSourceName(FieldSource source) {
    // Exhaustive, no `default`: a third publisher is a -Wswitch diagnostic here. That is not a
    // guard on its own -- AVGEN_WARNINGS_AS_ERRORS is OFF (CMakeLists.txt) -- which is why the
    // dispatch in `sample` below does not rely on it either.
    switch (source) {
    case FieldSource::Wind: return "wind";
    case FieldSource::Vortex: return "vortex";
    }
    return "unknown";
}

std::string vortexFieldName(std::string_view effectName) {
    std::string out = "atmos/";
    out.append(effectName);
    return out;
}

void FieldBus::clear() { entries_.clear(); }

void FieldBus::publishWind(std::string name, const wind::WindUniforms& uniforms) {
    const FieldHandle existing = resolve(name);
    Entry entry;
    entry.name = std::move(name);
    entry.source = FieldSource::Wind;
    entry.wind = uniforms;
    if (existing != kNoField) {
        entries_[static_cast<std::size_t>(existing)] = std::move(entry);
        return;
    }
    entries_.push_back(std::move(entry));
}

void FieldBus::publishVortex(std::string name, const vortex::VortexUniforms& uniforms) {
    const FieldHandle existing = resolve(name);
    Entry entry;
    entry.name = std::move(name);
    entry.source = FieldSource::Vortex;
    entry.vortex = uniforms;
    if (existing != kNoField) {
        entries_[static_cast<std::size_t>(existing)] = std::move(entry);
        return;
    }
    entries_.push_back(std::move(entry));
}

FieldHandle FieldBus::resolve(std::string_view name) const {
    // An empty name is "not subscribed" and must not match a field somebody published under the
    // empty string -- but nothing can, because `publishWind("")` would then be findable and a
    // subscription that named nothing would silently acquire it. Refusing here is cheaper than
    // refusing at publish and says the same thing.
    if (name.empty()) {
        return kNoField;
    }
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].name == name) {
            return static_cast<FieldHandle>(i);
        }
    }
    return kNoField;
}

std::string_view FieldBus::nameOf(FieldHandle handle) const {
    if (handle < 0 || static_cast<std::size_t>(handle) >= entries_.size()) {
        return {};
    }
    return entries_[static_cast<std::size_t>(handle)].name;
}

FieldSource FieldBus::sourceOf(FieldHandle handle) const {
    if (handle < 0 || static_cast<std::size_t>(handle) >= entries_.size()) {
        return FieldSource::Wind;
    }
    return entries_[static_cast<std::size_t>(handle)].source;
}

std::vector<std::string_view> FieldBus::names() const {
    std::vector<std::string_view> out;
    out.reserve(entries_.size());
    for (const Entry& e : entries_) {
        out.emplace_back(e.name);
    }
    return out;
}

FlowSample FieldBus::sample(FieldHandle handle, const glm::vec3& position, float t) const {
    if (handle < 0 || static_cast<std::size_t>(handle) >= entries_.size()) {
        return {};
    }
    const Entry& e = entries_[static_cast<std::size_t>(handle)];
    switch (e.source) {
    case FieldSource::Wind: return fromWind(wind::sampleWind(e.wind, position, t));
    case FieldSource::Vortex: return fromVortex(vortex::sampleVortex(e.vortex, position, t));
    }
    // Not reachable for a declared enumerator, and deliberately a zero rather than the first arm:
    // a publisher this function has not learned about must answer "nothing here", never "wind".
    // ADR-392 records what the other choice costs -- a kind that fell into the vortex arm of
    // `resolveAtmosphericEffects` and was then dropped as a duplicate.
    return {};
}

FlowSample FieldBus::sample(std::string_view name, const glm::vec3& position, float t) const {
    return sample(resolve(name), position, t);
}

std::vector<DeadSubscription> FieldBus::unresolved(std::span<const std::string> subscribers,
                                                   std::span<const Subscription> subs) const {
    std::vector<DeadSubscription> out;
    for (std::size_t i = 0; i < subs.size(); ++i) {
        // An empty name is the honest "no subscription" and is not reported. Reporting it would
        // make the report a list of every effect in the scene, which is a list nobody reads -- and
        // a report nobody reads is the failure mode this whole mechanism exists to avoid.
        if (!subs[i].requested()) {
            continue;
        }
        if (resolve(subs[i].field) != kNoField) {
            continue;
        }
        DeadSubscription dead;
        dead.subscriber = i < subscribers.size() ? subscribers[i] : std::string("<unnamed>");
        dead.field = subs[i].field;
        out.push_back(std::move(dead));
    }
    return out;
}

} // namespace avgen::world::fields
