#include "scene/grid_params.hpp"

namespace avgen::scene {

namespace {

struct GridRegistrar {
    params::ParameterSet& params;
    GridParameters& out;
    std::string group;

    void f(const char* rel, const char* label, float def, float lo, float hi, float slo, float shi) {
        params::ParamDesc<float> d;
        d.path = out.prefix + rel;
        d.label = label;
        d.group = group;
        d.defaultValue = def;
        d.hardMin = lo;
        d.hardMax = hi;
        d.softMin = slo;
        d.softMax = shi;
        out.all.push_back(&params.add(std::move(d)));
    }
};

void copy(const GridParameters& p, const char* rel, float& target) {
    const std::size_t n = p.prefix.size();
    const std::string_view leaf(rel);
    for (params::IParameter* ip : p.all) {
        const std::string& path = ip->path();
        if (path.size() == n + leaf.size() && path.compare(n, leaf.size(), leaf) == 0) {
            if (auto* fp = dynamic_cast<params::Parameter<float>*>(ip)) {
                target = fp->value();
            }
            return;
        }
    }
}

} // namespace

GridParameters registerGridParameters(params::ParameterSet& params, const spatial::GridField& rest,
                                      const std::string& prefix) {
    GridParameters p;
    p.prefix = prefix;
    std::string group = prefix;
    while (!group.empty() && group.back() == '/') {
        group.pop_back();
    }
    GridRegistrar r{params, p, group};
    r.f("injectRate", "inject rate", rest.injectRate, 0.0f, 1000.0f, 0.0f, 10.0f);
    r.f("advect", "advection (x the velocity field)", rest.advect, -100.0f, 100.0f, -4.0f, 4.0f);
    r.f("diffusion", "diffusion", rest.diffusion, 0.0f, 100.0f, 0.0f, 2.0f);
    r.f("dissipation", "fades (fraction per second)", rest.dissipation, 0.0f, 100.0f, 0.0f, 10.0f);
    if (rest.mode == spatial::GridMode::Excitable) { // ADR-1201
        r.f("threshold", "drive a rested cell needs to fire", rest.threshold, 0.0001f, 100.0f, 0.05f, 2.0f);
        r.f("coupling", "drive a passing front gives (x conductivity)", rest.coupling, 0.0f, 100.0f, 0.0f, 3.0f);
        r.f("waveSpeed", "front speed (m/s)", rest.waveSpeed, 0.001f, 1000.0f, 0.25f, 30.0f);
        r.f("riseRate", "excitation attack (1/s; u reaches 1 in 1/riseRate s)", rest.riseRate, 0.01f, 1000.0f, 0.5f,
            60.0f);
        r.f("excitationDecay", "excitation fades over (s)", rest.excitationDecay, 0.001f, 100.0f, 0.05f, 5.0f);
        r.f("refractoryTime", "refractory recovers over (s)", rest.refractoryTime, 0.001f, 100.0f, 0.1f, 10.0f);
        r.f("refractoryStrength", "recently fired cells resist by (x threshold)", rest.refractoryStrength, 0.0f,
            1000.0f, 0.0f, 20.0f);
        r.f("energyTime", "energy integrates over (s)", rest.energyTime, 0.001f, 1000.0f, 0.25f, 20.0f);
        r.f("wakeTime", "wake integrates over (s)", rest.wakeTime, 0.001f, 10000.0f, 1.0f, 120.0f);
        r.f("noise", "front delay jitter (0..1)", rest.noise, 0.0f, 1.0f, 0.0f, 1.0f);
    }
    if (rest.mode == spatial::GridMode::Scalar || rest.mode == spatial::GridMode::Excitable) {
        r.f("ceiling", "saturates at (0 = unbounded; ADR-1163)", rest.ceiling, 0.0f, 1000.0f, 0.0f, 4.0f);
    }
    if (rest.mode == spatial::GridMode::ReactionDiffusion) {
        r.f("feed", "Gray-Scott feed", rest.feed, 0.0f, 0.2f, 0.0f, 0.1f);
        r.f("kill", "Gray-Scott kill", rest.kill, 0.0f, 0.2f, 0.0f, 0.1f);
        r.f("diffusionA", "diffusion of A", rest.diffusionA, 0.0f, 10.0f, 0.0f, 2.0f);
        r.f("diffusionB", "diffusion of B", rest.diffusionB, 0.0f, 10.0f, 0.0f, 2.0f);
    }
    if (rest.mode == spatial::GridMode::Agents) {
        r.f("sensorAngle", "agents look this far aside (rad)", rest.sensorAngle, 0.0f, 3.1416f, 0.0f, 1.6f);
        r.f("sensorDistance", "agents look this far ahead (cells)", rest.sensorDistance, 0.0f, 256.0f, 0.0f, 32.0f);
        r.f("turnAngle", "agents turn by (rad per step)", rest.turnAngle, 0.0f, 3.1416f, 0.0f, 1.6f);
        r.f("stepSize", "agents move (cells per step)", rest.stepSize, 0.0f, 64.0f, 0.0f, 4.0f);
        r.f("depositAmount", "trail each agent leaves per step", rest.depositAmount, 0.0f, 64.0f, 0.0f, 0.2f);
        r.f("repel", "other species' trails count against a turn", rest.repel, -10.0f, 10.0f, -1.0f, 2.0f);
    }
    return p;
}

void applyGridParameters(const GridParameters& p, const spatial::GridField& rest, spatial::GridField& live) {
    live.injectRate = rest.injectRate;
    live.advect = rest.advect;
    live.diffusion = rest.diffusion;
    live.dissipation = rest.dissipation;
    live.ceiling = rest.ceiling;
    live.feed = rest.feed;
    live.kill = rest.kill;
    live.diffusionA = rest.diffusionA;
    live.diffusionB = rest.diffusionB;
    live.sensorAngle = rest.sensorAngle;
    live.sensorDistance = rest.sensorDistance;
    live.turnAngle = rest.turnAngle;
    live.stepSize = rest.stepSize;
    live.depositAmount = rest.depositAmount;
    live.repel = rest.repel;
    live.threshold = rest.threshold;
    live.coupling = rest.coupling;
    live.waveSpeed = rest.waveSpeed;
    live.riseRate = rest.riseRate;
    live.excitationDecay = rest.excitationDecay;
    live.refractoryTime = rest.refractoryTime;
    live.refractoryStrength = rest.refractoryStrength;
    live.energyTime = rest.energyTime;
    live.wakeTime = rest.wakeTime;
    live.noise = rest.noise;
    copy(p, "injectRate", live.injectRate);
    copy(p, "advect", live.advect);
    copy(p, "diffusion", live.diffusion);
    copy(p, "dissipation", live.dissipation);
    copy(p, "ceiling", live.ceiling);
    copy(p, "feed", live.feed);
    copy(p, "kill", live.kill);
    copy(p, "diffusionA", live.diffusionA);
    copy(p, "diffusionB", live.diffusionB);
    copy(p, "sensorAngle", live.sensorAngle);
    copy(p, "sensorDistance", live.sensorDistance);
    copy(p, "turnAngle", live.turnAngle);
    copy(p, "stepSize", live.stepSize);
    copy(p, "depositAmount", live.depositAmount);
    copy(p, "repel", live.repel);
    copy(p, "threshold", live.threshold);
    copy(p, "coupling", live.coupling);
    copy(p, "waveSpeed", live.waveSpeed);
    copy(p, "riseRate", live.riseRate);
    copy(p, "excitationDecay", live.excitationDecay);
    copy(p, "refractoryTime", live.refractoryTime);
    copy(p, "refractoryStrength", live.refractoryStrength);
    copy(p, "energyTime", live.energyTime);
    copy(p, "wakeTime", live.wakeTime);
    copy(p, "noise", live.noise);
}

} // namespace avgen::scene
