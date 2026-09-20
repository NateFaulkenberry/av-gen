#include "scene/temporal_conformance.hpp"

#include "params/parameter_set.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <nlohmann/json.hpp>

namespace avgen::scene::conformance {
namespace {

void report(Report& r, std::string subject, std::string rule, std::string detail) {
    r.findings.push_back(Finding{std::move(subject), std::move(rule), std::move(detail)});
}

// A value inside the hard range, different from what is there now, and different again for each
// salt. Used to prove a round trip carries values rather than defaults: a round trip checked with
// the default value passes even when nothing is written at all.
[[nodiscard]] float distinctValue(const params::IParameter& p, std::size_t component, int salt) {
    const float lo = p.hardMin(component);
    const float hi = p.hardMax(component);
    const float t = 0.2f + 0.13f * static_cast<float>(salt % 5);
    float v = lo + (hi - lo) * t;
    if (std::abs(v - p.defaultComponent(component)) < 1.0e-4f) {
        v = lo + (hi - lo) * (t + 0.37f > 1.0f ? t - 0.37f : t + 0.37f);
    }
    // A boolean's hard range is [0,1] and anything between reads as true; flip it instead so the
    // value genuinely differs from the default rather than rounding back to it.
    if (hi - lo <= 1.0f + 1.0e-6f && std::abs(hi - lo - 1.0f) < 1.0e-6f && p.componentCount() == 1) {
        const float def = p.defaultComponent(component);
        if (def <= 0.5f) {
            v = hi;
        } else {
            v = lo;
        }
    }
    return v;
}

} // namespace

std::string Report::summary() const {
    std::string out;
    for (const auto& f : findings) {
        out += std::format("[{}] {}: {}\n", f.rule, f.subject, f.detail);
    }
    return out;
}

bool registered(const params::ParameterSet& params, const std::string& path) {
    return params.find(path) != nullptr;
}

std::vector<DeclaredBound> declaredBounds(const TemporalSettings& settings) {
    std::vector<DeclaredBound> out;
    out.reserve(kTemporalEffectKinds.size());
    for (const auto kind : kTemporalEffectKinds) {
        out.push_back(DeclaredBound{temporalEffectKindName(kind), temporalEffectEnabled(kind, settings),
                                    temporalEffectHistoryFrames(kind, settings)});
    }
    return out;
}

Report checkBounds(std::span<const DeclaredBound> bounds) {
    Report r;
    for (const auto& b : bounds) {
        if (!b.enabled) {
            // A disabled kind must ask for nothing, or it holds a ring it is not using -- §8's
            // "do not blindly retain expensive full-resolution buffers", arriving as a leak rather
            // than as a setting.
            if (b.frames != 0) {
                report(r, b.subject, "bounded-k",
                       std::format("disabled but still declares {} frames of history", b.frames));
            }
            continue;
        }
        if (b.frames == 0) {
            report(r, b.subject, "bounded-k",
                   "enabled but declares no history bound -- an effect that cannot state a bound is "
                   "the accumulator ADR-394 forbids");
            continue;
        }
        if (b.frames > static_cast<std::uint32_t>(kMaxTemporalFrames)) {
            report(r, b.subject, "bounded-k",
                   std::format("declares {} frames, above the {}-frame ceiling the ring can hold", b.frames,
                               kMaxTemporalFrames));
        }
    }
    return r;
}

std::vector<std::string> registeredPaths(const TemporalSettings& settings) {
    params::ParameterSet scratch;
    (void)registerTemporalParameters(scratch, settings);
    std::vector<std::string> paths;
    for (const auto* p : scratch.ordered()) {
        paths.push_back(p->path());
    }
    return paths;
}

Report checkTemporal(const TemporalSettings& settings) {
    Report r;

    // ---- 1. bounded k, over the real declarations ----
    //
    // Probed with every kind ON, because a bound only has to exist when the effect does. Checking
    // the default settings -- where nothing is enabled -- would pass a family that declares no
    // bounds at all.
    {
        TemporalSettings all = settings;
        all.echo.enabled = true;
        const auto bounds = declaredBounds(all);
        for (auto& f : checkBounds(bounds).findings) {
            r.findings.push_back(std::move(f));
        }
        if (bounds.size() != kTemporalEffectKinds.size()) {
            report(r, "family", "bounded-k",
                   std::format("{} kinds declared, {} in kTemporalEffectKinds", bounds.size(),
                               kTemporalEffectKinds.size()));
        }
    }

    // ---- 5. names ----
    for (const auto kind : kTemporalEffectKinds) {
        TemporalEffectKind back{};
        if (!temporalEffectKindFromName(temporalEffectKindName(kind), back) || back != kind) {
            report(r, temporalEffectKindName(kind), "names", "does not survive name -> kind -> name");
        }
    }

    // ---- 2 + 4. registration, obtained from the registrar rather than from a table ----
    params::ParameterSet probe;
    const TemporalParameters registeredParams = registerTemporalParameters(probe, settings);
    for (const auto kind : kTemporalEffectKinds) {
        const std::string prefix = temporalParameterPrefix(kind);
        const bool any = std::any_of(probe.ordered().begin(), probe.ordered().end(),
                                     [&](const params::IParameter* p) { return p->path().starts_with(prefix); });
        if (!any) {
            report(r, temporalEffectKindName(kind), "paths",
                   std::format("registers nothing under its own prefix '{}'", prefix));
        }
    }
    for (const auto* p : probe.ordered()) {
        for (std::size_t c = 0; c < p->componentCount(); ++c) {
            const float def = p->defaultComponent(c);
            if (def < p->hardMin(c) - 1.0e-5f || def > p->hardMax(c) + 1.0e-5f) {
                report(r, p->path(), "ranges", std::format("default {} outside hard range", def));
            }
            if (p->softMin(c) < p->hardMin(c) - 1.0e-5f || p->softMax(c) > p->hardMax(c) + 1.0e-5f) {
                report(r, p->path(), "ranges", "soft range escapes the hard range");
            }
        }
        if (!p->flags().modulatable) {
            report(r, p->path(), "paths", "not modulatable, so no ModRoute can reach it (§39)");
        }
        if (!p->flags().serialized) {
            report(r, p->path(), "paths", "not serialized, so a project cannot keep it (ADR-225)");
        }
    }

    // ---- 3. round trip, read back through re-registration ----
    //
    // Every registered component is pushed to a distinct non-default value, applied to the
    // settings, written, read, and then re-registered -- because registration takes each default
    // from the settings themselves, so comparing the re-registered DEFAULT against the value we
    // set is what proves the value made the trip. Comparing JSON to JSON would not: a field absent
    // from both the writer and the reader compares equal.
    {
        params::ParameterSet before;
        const TemporalParameters bp = registerTemporalParameters(before, settings);
        int salt = 0;
        for (auto* p : before.ordered()) {
            for (std::size_t c = 0; c < p->componentCount(); ++c) {
                p->setBaseComponent(c, distinctValue(*p, c, salt++));
            }
        }
        before.resetFinals();
        TemporalSettings authored = settings;
        applyTemporalParameters(bp, authored);

        // What the settings actually hold once the probe's values have been applied -- NOT the raw
        // values the probe wrote. Some parameters are quantised on the way in (`frames` is an
        // integer carried in a float), so comparing the raw probe value against the reloaded one
        // reports a loss of 0.23 of a frame that never existed. Registering the authored settings
        // gives the same quantisation on both sides of the save, which leaves the comparison
        // measuring what it claims to: whether the VALUE made the trip.
        //
        // This was not a hypothetical. The check's first run reported exactly that false positive,
        // which is the mechanism working -- it noticed a disagreement between what a parameter
        // accepts and what its setting keeps, and that disagreement is worth knowing about even
        // though it is not a lost value.
        params::ParameterSet expected;
        (void)registerTemporalParameters(expected, authored);

        auto reloaded = temporalFromJson(temporalToJson(authored));
        if (!reloaded) {
            report(r, "family", "round-trip", std::format("load failed: {}", reloaded.error().message));
        } else {
            // Second trip: a writer that emits a block its own reader ignores survives one trip
            // and not two (ADR-350's control).
            auto twice = temporalFromJson(temporalToJson(*reloaded));
            if (!twice) {
                report(r, "family", "round-trip", std::format("second load failed: {}", twice.error().message));
            } else {
                params::ParameterSet after;
                (void)registerTemporalParameters(after, *twice);
                std::size_t lost = 0;
                for (const auto* p : expected.ordered()) {
                    const auto* q = after.find(p->path());
                    if (q == nullptr) {
                        report(r, p->path(), "round-trip", "path vanished after a save/load");
                        continue;
                    }
                    for (std::size_t c = 0; c < p->componentCount(); ++c) {
                        // Rounded comparison: `frames` is an integer carried in a float, so a
                        // half-ulp difference is not a lost value.
                        const float want = p->defaultComponent(c);
                        const float got = q->defaultComponent(c);
                        if (std::abs(want - got) > std::max(1.0e-3f, std::abs(want) * 1.0e-3f)) {
                            // Cap the report: naming every one of a large family's paths is how a
                            // failure message becomes unreadable (ADR-362's spirit).
                            if (++lost <= 12) {
                                report(r, p->path(), "round-trip",
                                       std::format("component {} was {} and came back {}", c, want, got));
                            }
                        }
                    }
                }
            }
        }
    }
    (void)registeredParams;
    return r;
}

} // namespace avgen::scene::conformance
